using System.IO.Compression;
using System.Text.Json;
using System.Text.Json.Serialization;
using XVatsim.Manager.Core;

if (args is ["--live-scan", var payloadPath])
{
    var provider = new ZipPayloadProvider(await File.ReadAllBytesAsync(payloadPath));
    var snapshot = await new DiscoveryService().DiscoverFastAsync();
    Console.WriteLine($"XVATSIM_MANAGER_LIVE_SCAN xplane={snapshot.XPlaneInstallations.Count} xpilot={snapshot.XPilotInstallations.Count}");
    foreach (var item in snapshot.XPlaneInstallations)
        Console.WriteLine($"XPLANE root={item.RootPath} xvatsim={item.HasXVatsim} xpilotPlugin={item.HasXPilotPlugin} source={item.Source}");
    foreach (var item in snapshot.XPilotInstallations)
        Console.WriteLine($"XPILOT version={item.ProductVersion} generation={item.Generation} path={item.ExecutablePath}");
    var planner = new InstallPlanner(provider, new ReceiptStore());
    foreach (var item in snapshot.XPlaneInstallations)
    {
        var plan = await planner.BuildAsync(item, snapshot.XPilotInstallations);
        Console.WriteLine($"PLAN root={item.RootPath} action={plan.Action} bridge={plan.CanInstallBridge} files={plan.Files.Count} warnings={plan.Warnings.Count}");
    }
    return;
}

var probe = new ManagerProbe();
await probe.RunAsync();

internal sealed class ManagerProbe
{
    private readonly List<string> _passed = [];
    private string _root = string.Empty;

    public async Task RunAsync()
    {
        _root = Path.Combine(Path.GetTempPath(), "XVatsimManagerProbe", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_root);
        try
        {
            await FirstInstallRepairAndPreservationAsync();
            await UpdateAndRollbackAsync();
            await LockedDestinationRollbackAsync();
            await CompatibilityMatrixAsync();
            await PackageRejectionAsync();
            await RunningProcessRefusalAsync();
            Console.WriteLine($"XVATSIM_MANAGER_PROBE_PASSED tests={_passed.Count}");
            foreach (var item in _passed) Console.WriteLine($"PASS {item}");
        }
        finally
        {
            try { Directory.Delete(_root, recursive: true); } catch { }
        }
    }

    private async Task FirstInstallRepairAndPreservationAsync()
    {
        var fixture = CreateFixture("2.0.2", "plugin-v202", "audio-v1", "registry-v1");
        var xplane = CreateXPlane("xp-main", hasPlugin: false, hasXPilotPlugin: true);
        var v3 = XPilot("3.2.0", XPilotGeneration.Version3);
        var unrelated = Path.Combine(xplane.PluginDirectory, "XVatsim", "settings.json");
        Directory.CreateDirectory(Path.GetDirectoryName(unrelated)!);
        await File.WriteAllTextAsync(unrelated, "keep-me");
        var receipts = new ReceiptStore(Path.Combine(_root, "state"));
        var planner = new InstallPlanner(fixture, receipts);
        var plan = await planner.BuildAsync(xplane, [v3]);
        Require(plan.Action == RecommendedAction.Install, "clean installation plan");
        var result = await new TransactionalInstaller(fixture, receipts, Path.Combine(_root, "manager"))
            .ExecuteAsync(plan, bypassProcessGuardForIsolatedTests: true);
        Require(result.Succeeded, "clean first install");
        Require(await File.ReadAllTextAsync(unrelated) == "keep-me", "unmanaged settings preserved");

        xplane = CreateXPlane("xp-main", hasPlugin: true, hasXPilotPlugin: true);
        plan = await planner.BuildAsync(xplane, [v3]);
        Require(plan.Action == RecommendedAction.Current, "verified installation is current");

        var pluginPath = Path.Combine(xplane.RootPath, "Resources", "plugins", "XVatsim", "win_x64", "XVatsim.xpl");
        await File.WriteAllTextAsync(pluginPath, "damaged");
        plan = await planner.BuildAsync(xplane, [v3]);
        Require(plan.Action == RecommendedAction.Repair, "modified managed file requests repair");
        result = await new TransactionalInstaller(fixture, receipts, Path.Combine(_root, "manager"))
            .ExecuteAsync(plan, bypassProcessGuardForIsolatedTests: true);
        Require(result.Succeeded && await File.ReadAllTextAsync(pluginPath) == "plugin-v202", "repair restores verified bytes");
    }

    private async Task UpdateAndRollbackAsync()
    {
        var xplane = CreateXPlane("xp-main", hasPlugin: true, hasXPilotPlugin: true);
        var v3 = XPilot("3.2.0", XPilotGeneration.Version3);
        var receipts = new ReceiptStore(Path.Combine(_root, "state"));
        var next = CreateFixture("2.0.3", "plugin-v203", "audio-v2", "registry-v2");
        var plan = await new InstallPlanner(next, receipts).BuildAsync(xplane, [v3]);
        Require(plan.Action == RecommendedAction.Update, "new payload version requests update");
        var result = await new TransactionalInstaller(next, receipts, Path.Combine(_root, "manager"))
            .ExecuteAsync(plan, bypassProcessGuardForIsolatedTests: true);
        Require(result.Succeeded, "verified update");

        var before = await SnapshotManagedAsync(xplane.RootPath);
        var future = CreateFixture("2.0.4", "plugin-v204", "audio-v3", "registry-v3");
        plan = await new InstallPlanner(future, receipts).BuildAsync(xplane, [v3]);
        result = await new TransactionalInstaller(future, receipts, Path.Combine(_root, "manager"), new FailBeforeIndex(1))
            .ExecuteAsync(plan, bypassProcessGuardForIsolatedTests: true);
        var after = await SnapshotManagedAsync(xplane.RootPath);
        Require(!result.Succeeded && result.RolledBack && before.SequenceEqual(after), "mid-install failure restores all previous files");
    }

    private async Task CompatibilityMatrixAsync()
    {
        var payload = CreateFixture("2.0.2", "plugin", "audio", "registry", includeBridge: true);
        var xplane = CreateXPlane("xp-compat", hasPlugin: false, hasXPilotPlugin: true);
        var planner = new InstallPlanner(payload, new ReceiptStore(Path.Combine(_root, "compat-state")));
        var v3 = XPilot("3.2.0", XPilotGeneration.Version3);
        var v4 = XPilot("4.0.0-beta.7+probe", XPilotGeneration.Version4);
        var futureV4 = XPilot("4.0.0-beta.99", XPilotGeneration.Version4);
        var unknown = XPilot("5.0.0", XPilotGeneration.Unknown);

        var noXPilot = await planner.BuildAsync(xplane, []);
        Require(!noXPilot.CanInstallBridge && !noXPilot.CanInstallSafely, "no xPilot blocks installation");
        var blocked = await new TransactionalInstaller(payload,
                new ReceiptStore(Path.Combine(_root, "blocked-state")), Path.Combine(_root, "blocked-manager"))
            .ExecuteAsync(noXPilot, bypassProcessGuardForIsolatedTests: true);
        Require(!blocked.Succeeded && blocked.Message.Contains("verified", StringComparison.OrdinalIgnoreCase),
            "installer enforces verified-folder gate");
        var v3Plan = await planner.BuildAsync(xplane, [v3]);
        Require(!v3Plan.CanInstallBridge && v3Plan.CanInstallSafely, "xPilot 3 uses verified legacy installation");
        var v4Plan = await planner.BuildAsync(xplane, [v4]);
        Require(v4Plan.CanInstallBridge && v4Plan.CanInstallSafely, "supported xPilot 4 includes bridge");
        var futurePlan = await planner.BuildAsync(xplane, [futureV4]);
        Require(futurePlan.CanInstallBridge && futurePlan.CanInstallSafely,
            "new xPilot 4 beta does not require manager rebuild");
        Require((await planner.BuildAsync(xplane, [v3, v4])).CanInstallBridge, "xPilot 3 and 4 prefers compatible bridge path");
        var missingSimulatorPlugin = CreateXPlane("xp-compat-missing", hasPlugin: false, hasXPilotPlugin: false);
        var missingPluginPlan = await planner.BuildAsync(missingSimulatorPlugin, [futureV4]);
        Require(!missingPluginPlan.CanInstallBridge && !missingPluginPlan.CanInstallSafely,
            "missing xPilot simulator plugin blocks installation");
        var unknownPlan = await planner.BuildAsync(xplane, [unknown]);
        Require(!unknownPlan.CanInstallBridge && !unknownPlan.CanInstallSafely,
            "future xPilot generation blocks installation");
    }

    private async Task LockedDestinationRollbackAsync()
    {
        var xplane = CreateXPlane("xp-locked", hasPlugin: false, hasXPilotPlugin: true);
        var v3 = XPilot("3.2.0", XPilotGeneration.Version3);
        var receipts = new ReceiptStore(Path.Combine(_root, "locked-state"));
        var managerRoot = Path.Combine(_root, "locked-manager");
        var baseline = CreateFixture("2.0.2", "plugin-before-lock", "audio-before-lock", "registry-before-lock");
        var plan = await new InstallPlanner(baseline, receipts).BuildAsync(xplane, [v3]);
        var setup = await new TransactionalInstaller(baseline, receipts, managerRoot)
            .ExecuteAsync(plan, bypassProcessGuardForIsolatedTests: true);
        if (!setup.Succeeded) throw new InvalidOperationException("Locked-file probe setup failed.");

        var before = await SnapshotManagedAsync(xplane.RootPath);
        var receiptBefore = receipts.Read(xplane.RootPath)
            ?? throw new InvalidOperationException("Locked-file probe receipt was not written.");
        var next = CreateFixture("2.0.3", "plugin-after-lock", "audio-after-lock", "registry-after-lock");
        plan = await new InstallPlanner(next, receipts).BuildAsync(xplane, [v3]);
        var lockedPath = Path.Combine(xplane.RootPath, "Resources", "plugins", "XVatsim", "win_x64", "ui_transition.mp3");
        InstallResult result;
        await using (var locked = new FileStream(lockedPath, FileMode.Open, FileAccess.Read, FileShare.Read))
        {
            result = await new TransactionalInstaller(next, receipts, managerRoot)
                .ExecuteAsync(plan, bypassProcessGuardForIsolatedTests: true);
        }

        var after = await SnapshotManagedAsync(xplane.RootPath);
        var receiptAfter = receipts.Read(xplane.RootPath);
        var stagingRoot = Path.Combine(managerRoot, "staging");
        var stagingEmpty = !Directory.Exists(stagingRoot) || !Directory.EnumerateFileSystemEntries(stagingRoot).Any();
        var temporaryFiles = Directory.EnumerateFiles(xplane.RootPath, "*.xvatsim-manager-*.tmp", SearchOption.AllDirectories).ToArray();

        Require(!result.Succeeded && result.RolledBack && before.SequenceEqual(after),
            "locked destination restores all previous files");
        Require(receiptAfter?.TransactionId == receiptBefore.TransactionId,
            "locked destination preserves prior receipt");
        Require(stagingEmpty && temporaryFiles.Length == 0,
            "locked destination removes staging and temporary files");
        Require(result.BackupDirectory is not null && Directory.Exists(result.BackupDirectory),
            "locked destination retains backup evidence");
    }

    private async Task PackageRejectionAsync()
    {
        var traversalRejected = false;
        try { _ = new ZipPayloadProvider(CreateTraversalZip()); }
        catch (InvalidDataException) { traversalRejected = true; }
        Require(traversalRejected, "zip traversal rejected");

        var corrupt = CreateFixture("2.0.2", "plugin", "audio", "registry", corruptHash: true);
        var xplane = CreateXPlane("xp-corrupt", hasPlugin: false, hasXPilotPlugin: true);
        var v3 = XPilot("3.2.0", XPilotGeneration.Version3);
        var receipts = new ReceiptStore(Path.Combine(_root, "corrupt-state"));
        var plan = await new InstallPlanner(corrupt, receipts).BuildAsync(xplane, [v3]);
        var result = await new TransactionalInstaller(corrupt, receipts, Path.Combine(_root, "corrupt-manager"))
            .ExecuteAsync(plan, bypassProcessGuardForIsolatedTests: true);
        Require(!result.Succeeded && !File.Exists(Path.Combine(xplane.PluginDirectory, "XVatsim", "win_x64", "XVatsim.xpl")),
            "corrupt payload rejected before installation");
    }

    private async Task RunningProcessRefusalAsync()
    {
        var payload = CreateFixture("2.0.2", "plugin", "audio", "registry");
        var xplane = CreateXPlane("xp-running", hasPlugin: false, hasXPilotPlugin: true);
        var v3 = XPilot("3.2.0", XPilotGeneration.Version3);
        var receipts = new ReceiptStore(Path.Combine(_root, "running-state"));
        var plan = await new InstallPlanner(payload, receipts).BuildAsync(xplane, [v3]);
        var installer = new TransactionalInstaller(payload, receipts, Path.Combine(_root, "running-manager"),
            runningApplications: () => ["X-Plane", "xPilot"]);
        var result = await installer.ExecuteAsync(plan);
        Require(!result.Succeeded && result.Message.Contains("Close", StringComparison.OrdinalIgnoreCase),
            "running applications block installation");
    }

    private XPlaneInstallation CreateXPlane(string name, bool hasPlugin, bool hasXPilotPlugin = false)
    {
        var root = Path.Combine(_root, name);
        var plugins = Path.Combine(root, "Resources", "plugins");
        Directory.CreateDirectory(plugins);
        var executable = Path.Combine(root, "X-Plane.exe");
        if (!File.Exists(executable)) File.WriteAllText(executable, "fixture");
        if (hasXPilotPlugin)
        {
            var path = Path.Combine(plugins, "xPilot", "win_x64", "xPilot.xpl");
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, "xpilot fixture");
        }
        return new XPlaneInstallation(root, executable, plugins, "12", hasPlugin, hasXPilotPlugin, "probe");
    }

    private static XPilotInstallation XPilot(string version, XPilotGeneration generation) =>
        new(Path.Combine(Path.GetTempPath(), "xPilot.exe"), version, generation, "probe");

    private static ZipPayloadProvider CreateFixture(string version, string plugin, string audio, string registry,
        bool includeBridge = false, bool corruptHash = false)
    {
        var files = new Dictionary<(string Path, PayloadTarget Target), byte[]>
        {
            [("Resources/plugins/XVatsim/win_x64/XVatsim.xpl", PayloadTarget.XPlane)] = System.Text.Encoding.UTF8.GetBytes(plugin),
            [("Resources/plugins/XVatsim/win_x64/ui_transition.mp3", PayloadTarget.XPlane)] = System.Text.Encoding.UTF8.GetBytes(audio),
            [("Resources/plugins/XVatsim/win_x64/authority_source_registry.json", PayloadTarget.XPlane)] = System.Text.Encoding.UTF8.GetBytes(registry)
        };
        if (includeBridge)
            files[("xpilot4/XVatsim.XPilot4Bridge.dll", PayloadTarget.XPilot4)] = System.Text.Encoding.UTF8.GetBytes("bridge");
        var manifestFiles = files.Select((item, index) => new PayloadFile(item.Key.Path, item.Key.Target,
            item.Value.LongLength, corruptHash && index == 0 ? new string('0', 64) : Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(item.Value)))).ToArray();
        var manifest = new ManagerManifest("2", version, DateTimeOffset.UtcNow.ToString("O"), manifestFiles);
        using var memory = new MemoryStream();
        using (var archive = new ZipArchive(memory, ZipArchiveMode.Create, leaveOpen: true))
        {
            foreach (var item in files)
            {
                var entry = archive.CreateEntry(item.Key.Path);
                using var stream = entry.Open();
                stream.Write(item.Value);
            }
            var manifestEntry = archive.CreateEntry("manifest.json");
            using var manifestStream = manifestEntry.Open();
            JsonSerializer.Serialize(manifestStream, manifest, JsonOptions());
        }
        return new ZipPayloadProvider(memory.ToArray());
    }

    private static byte[] CreateTraversalZip()
    {
        using var memory = new MemoryStream();
        using (var archive = new ZipArchive(memory, ZipArchiveMode.Create, leaveOpen: true))
        {
            var evil = archive.CreateEntry("../evil.txt");
            using (var stream = new StreamWriter(evil.Open())) stream.Write("bad");
            var manifest = archive.CreateEntry("manifest.json");
            using var writer = new StreamWriter(manifest.Open());
            writer.Write("{}");
        }
        return memory.ToArray();
    }

    private static JsonSerializerOptions JsonOptions()
    {
        var options = new JsonSerializerOptions { WriteIndented = true, PropertyNamingPolicy = JsonNamingPolicy.CamelCase };
        options.Converters.Add(new JsonStringEnumConverter());
        return options;
    }

    private static async Task<string[]> SnapshotManagedAsync(string root)
    {
        var folder = Path.Combine(root, "Resources", "plugins", "XVatsim", "win_x64");
        var names = new[] { "XVatsim.xpl", "ui_transition.mp3", "authority_source_registry.json" };
        var values = new List<string>();
        foreach (var name in names)
            values.Add(await File.ReadAllTextAsync(Path.Combine(folder, name)));
        return values.ToArray();
    }

    private void Require(bool condition, string name)
    {
        if (!condition) throw new InvalidOperationException($"Probe failed: {name}");
        _passed.Add(name);
    }

    private sealed class FailBeforeIndex(int index) : IInstallFaultInjector
    {
        public void BeforeReplace(int zeroBasedIndex, PlannedFile file)
        {
            if (zeroBasedIndex == index) throw new IOException("Injected disk failure");
        }
    }
}
