using System.Diagnostics;
using System.Text.Json;

namespace XVatsim.Manager.Core;

public static class XPilotSelection
{
    public static XPilotInstallation? Preferred(IEnumerable<XPilotInstallation> installations) =>
        installations
            .OrderByDescending(item => item.Generation == XPilotGeneration.Version4 && item.IsSupportedByPayload)
            .ThenByDescending(item => item.Source.Equals("xPilot standard location", StringComparison.OrdinalIgnoreCase))
            .ThenByDescending(item => item.Generation)
            .ThenByDescending(item => item.ProductVersion, StringComparer.OrdinalIgnoreCase)
            .FirstOrDefault();
}

public sealed class ReceiptStore
{
    public string StateRoot { get; }

    public ReceiptStore(string? stateRoot = null)
    {
        StateRoot = stateRoot ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "XVatsimManager", "state");
    }

    public string ReceiptPath(string xplaneRoot)
    {
        var key = Hashing.TextSha256(Path.GetFullPath(xplaneRoot).ToUpperInvariant())[..16];
        return Path.Combine(StateRoot, $"install-receipt-{key}.json");
    }

    public InstallReceipt? Read(string xplaneRoot)
    {
        var path = ReceiptPath(xplaneRoot);
        if (!File.Exists(path)) return null;
        try
        {
            using var stream = File.OpenRead(path);
            return JsonSerializer.Deserialize(stream, ManagerJsonContext.Default.InstallReceipt);
        }
        catch (Exception ex) when (ex is IOException or JsonException)
        {
            return null;
        }
    }

    public async Task WriteAsync(InstallReceipt receipt, CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(StateRoot);
        var path = ReceiptPath(receipt.XPlaneRoot);
        var temporary = path + $".{Guid.NewGuid():N}.tmp";
        await using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None,
            32 * 1024, FileOptions.Asynchronous))
        {
            await JsonSerializer.SerializeAsync(stream, receipt, ManagerJsonContext.Default.InstallReceipt, cancellationToken);
            await stream.FlushAsync(cancellationToken);
        }
        File.Move(temporary, path, overwrite: true);
    }
}

public sealed class InstallPlanner
{
    private readonly IPayloadProvider _payload;
    private readonly ReceiptStore _receipts;

    public InstallPlanner(IPayloadProvider payload, ReceiptStore receipts)
    {
        _payload = payload;
        _receipts = receipts;
    }

    public async Task<InstallPlan> BuildAsync(
        XPlaneInstallation xplane,
        IReadOnlyList<XPilotInstallation> xpilots,
        CancellationToken cancellationToken = default)
    {
        var warnings = new List<string>();
        var selectedXPilot = XPilotSelection.Preferred(xpilots);
        var desktopSupportsBridge = selectedXPilot is
        {
            Generation: XPilotGeneration.Version4,
            IsSupportedByPayload: true
        };
        var simulatorPluginSupportsBridge = xplane.XPilotPluginSha256 is not null &&
            _payload.Manifest.SupportedXPilot4SimulatorPluginSha256.Any(hash =>
                Hashing.EqualsHash(hash, xplane.XPilotPluginSha256));
        var canInstallBridge = desktopSupportsBridge && simulatorPluginSupportsBridge;
        var canInstallSafely = selectedXPilot switch
        {
            { Generation: XPilotGeneration.Version3 } => xplane.HasXPilotPlugin,
            { Generation: XPilotGeneration.Version4 } => canInstallBridge,
            _ => false
        };

        if (selectedXPilot is null)
            warnings.Add("xPilot was not found. Select its installation folder before installing XVatsim.");
        else if (selectedXPilot.Generation == XPilotGeneration.Version3)
            warnings.Add("xPilot 3 detected. XVatsim will use its existing legacy integration; no xPilot 4 companion will be installed.");
        else if (selectedXPilot.Generation == XPilotGeneration.Version4 && !selectedXPilot.IsSupportedByPayload)
            warnings.Add($"xPilot {selectedXPilot.ProductVersion} is not on this manager's compatibility list. XVatsim will be installed without the companion.");
        else if (selectedXPilot.Generation == XPilotGeneration.Unknown)
            warnings.Add("The detected xPilot version could not be classified. The companion will not be installed.");

        if (selectedXPilot?.Generation == XPilotGeneration.Version4 && !xplane.HasXPilotPlugin)
            warnings.Add("The xPilot desktop client is version 4, but its X-Plane plugin was not found in this X-Plane installation. Reinstall xPilot before live use.");
        else if (desktopSupportsBridge && !simulatorPluginSupportsBridge)
            warnings.Add("The xPilot desktop client and its X-Plane plugin are not a verified pair for this bridge. Reinstall the matching xPilot beta; the companion will not be installed yet.");
        else if (selectedXPilot?.Generation == XPilotGeneration.Version3 && !xplane.HasXPilotPlugin)
            warnings.Add("xPilot was found, but its X-Plane plugin was not found in the selected X-Plane installation.");

        var files = new List<PlannedFile>();
        foreach (var file in _payload.Manifest.Files)
        {
            if (file.Target == PayloadTarget.XPilot4 && !canInstallBridge)
                continue;
            var destination = ResolveDestination(xplane, selectedXPilot, file);
            var exists = File.Exists(destination);
            string? existingHash = null;
            if (exists)
                existingHash = await Hashing.FileSha256Async(destination, cancellationToken);
            files.Add(new PlannedFile(file, destination, exists,
                exists && Hashing.EqualsHash(existingHash, file.Sha256), existingHash));
        }

        var xplaneFiles = files.Where(x => x.Payload.Target == PayloadTarget.XPlane).ToArray();
        var receipt = _receipts.Read(xplane.RootPath);
        RecommendedAction action;
        if (files.All(f => f.MatchesPayload))
            action = RecommendedAction.Current;
        else if (xplaneFiles.All(f => !f.Exists))
            action = RecommendedAction.Install;
        else if (receipt is not null && !string.Equals(receipt.ProductVersion, _payload.Manifest.ProductVersion,
                     StringComparison.OrdinalIgnoreCase))
            action = RecommendedAction.Update;
        else
            action = RecommendedAction.Repair;

        var mainPlugin = xplaneFiles.FirstOrDefault(f => f.Payload.RelativePath.EndsWith("XVatsim.xpl", StringComparison.OrdinalIgnoreCase));
        if (mainPlugin is { Exists: true, MatchesPayload: false } && receipt is null)
            warnings.Add("An unreceipted or modified XVatsim plugin is present. Repair will back it up before installing the verified version.");

        var summary = action switch
        {
            RecommendedAction.Install => "XVatsim is ready for a verified first installation.",
            RecommendedAction.Update => $"XVatsim can be updated to {_payload.Manifest.ProductVersion}.",
            RecommendedAction.Repair => "One or more managed files are missing or differ from the verified payload.",
            _ => $"XVatsim {_payload.Manifest.ProductVersion} is verified and current."
        };
        return new InstallPlan(xplane, selectedXPilot, action, files, warnings, summary, canInstallBridge, canInstallSafely);
    }

    private static string ResolveDestination(XPlaneInstallation xplane, XPilotInstallation? xpilot, PayloadFile file)
    {
        var relative = file.RelativePath.Replace('/', Path.DirectorySeparatorChar);
        var xpilotPluginRoot = xpilot is null ? null : ResolveXPilotPluginRoot(xpilot);
        var destination = file.Target switch
        {
            PayloadTarget.XPlane => Path.Combine(xplane.RootPath, relative),
            PayloadTarget.XPilot4 when xpilotPluginRoot is not null => Path.Combine(
                xpilotPluginRoot, "XVatsim.XPilot4Bridge", Path.GetFileName(relative)),
            _ => throw new InvalidOperationException("No compatible xPilot 4 destination is available.")
        };
        return EnsureUnderRoot(destination, file.Target == PayloadTarget.XPlane
            ? xplane.RootPath
            : xpilotPluginRoot!);
    }

    private static string ResolveXPilotPluginRoot(XPilotInstallation xpilot)
    {
        var applicationFolder = Directory.GetParent(xpilot.ExecutablePath)?.FullName
            ?? throw new InvalidDataException("The xPilot executable has no installation folder.");
        var installationRoot = string.Equals(Path.GetFileName(applicationFolder), "Application",
            StringComparison.OrdinalIgnoreCase)
            ? Directory.GetParent(applicationFolder)?.FullName
            : applicationFolder;
        if (string.IsNullOrWhiteSpace(installationRoot))
            throw new InvalidDataException("The xPilot plugin folder could not be resolved.");
        return Path.Combine(installationRoot, "Plugins");
    }

    private static string EnsureUnderRoot(string path, string root)
    {
        var fullRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        var fullPath = Path.GetFullPath(path);
        if (!fullPath.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException($"Destination escapes its managed root: {path}");
        return fullPath;
    }
}

public static class ProcessGuard
{
    private static readonly string[] Names = ["X-Plane", "X-Plane 12", "xPilot"];

    public static IReadOnlyList<string> RunningApplications()
    {
        var found = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var process in Process.GetProcesses())
        {
            using (process)
            {
                try
                {
                    if (Names.Any(name => string.Equals(name, process.ProcessName, StringComparison.OrdinalIgnoreCase)))
                        found.Add(process.ProcessName);
                }
                catch (InvalidOperationException) { }
            }
        }
        return found.OrderBy(x => x, StringComparer.OrdinalIgnoreCase).ToArray();
    }
}
