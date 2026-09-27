using System.Diagnostics;
using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace XVatsim.Manager.Core;

public sealed class DiscoveryService
{
    private readonly ManagerManifest _manifest;

    public DiscoveryService(ManagerManifest manifest) => _manifest = manifest;

    public async Task<DiscoverySnapshot> DiscoverFastAsync(CancellationToken cancellationToken = default)
    {
        var candidatePaths = new List<(string Path, string Source)>();
        AddXPlaneInstallerRecords(candidatePaths);
        AddCommonXPlanePaths(candidatePaths);
        AddSteamXPlanePaths(candidatePaths);
        AddUninstallLocations(candidatePaths, includeXPlane: true);

        var xplanes = new List<XPlaneInstallation>();
        var rejected = new List<string>();
        foreach (var candidate in candidatePaths.DistinctBy(x => NormalizePath(x.Path), StringComparer.OrdinalIgnoreCase))
        {
            cancellationToken.ThrowIfCancellationRequested();
            var validated = await ValidateXPlaneAsync(candidate.Path, candidate.Source, cancellationToken);
            if (validated is not null)
                xplanes.Add(validated);
            else if (File.Exists(Path.Combine(candidate.Path, "X-Plane.exe")))
                rejected.Add($"{candidate.Path} (missing required X-Plane 12 structure)");
        }

        var xpilots = DiscoverXPilot();
        return new DiscoverySnapshot(
            xplanes.OrderBy(x => x.RootPath, StringComparer.OrdinalIgnoreCase).ToArray(),
            xpilots.OrderByDescending(x => x.Generation).ThenBy(x => x.ExecutablePath, StringComparer.OrdinalIgnoreCase).ToArray(),
            rejected, DateTimeOffset.UtcNow);
    }

    public Task<XPlaneInstallation?> ValidateSelectedXPlaneAsync(string root, CancellationToken cancellationToken = default) =>
        ValidateXPlaneAsync(root, "selected by pilot", cancellationToken);

    public Task<XPilotInstallation?> ValidateSelectedXPilotAsync(string selectedPath)
    {
        var candidates = new[]
        {
            selectedPath,
            Path.Combine(selectedPath, "xPilot.exe"),
            Path.Combine(selectedPath, "Application", "xPilot.exe")
        };
        foreach (var candidate in candidates.Distinct(StringComparer.OrdinalIgnoreCase))
        {
            var executable = File.Exists(candidate) ? candidate : string.Empty;
            if (string.IsNullOrEmpty(executable)) continue;
            var validated = ValidateXPilot(executable, "selected by pilot");
            if (validated is not null) return Task.FromResult<XPilotInstallation?>(validated);
        }
        return Task.FromResult<XPilotInstallation?>(null);
    }

    private async Task<XPlaneInstallation?> ValidateXPlaneAsync(string root, string source, CancellationToken cancellationToken)
    {
        string normalized;
        try { normalized = NormalizePath(root); }
        catch { return null; }
        var executable = Path.Combine(normalized, "X-Plane.exe");
        var resources = Path.Combine(normalized, "Resources");
        var plugins = Path.Combine(resources, "plugins");
        if (!File.Exists(executable) || !Directory.Exists(resources) || !Directory.Exists(plugins))
            return null;

        var info = FileVersionInfo.GetVersionInfo(executable);
        var identity = $"{info.ProductName} {info.FileDescription} {info.ProductVersion}";
        if (!identity.Contains("X-Plane", StringComparison.OrdinalIgnoreCase) &&
            !Directory.Exists(Path.Combine(resources, "default scenery")))
            return null;

        var xvatsim = Path.Combine(plugins, "XVatsim", "win_x64", "XVatsim.xpl");
        var xpilotPlugin = Path.Combine(plugins, "xPilot", "win_x64", "xPilot.xpl");
        string? xpilotHash = null;
        if (File.Exists(xpilotPlugin))
            xpilotHash = await Hashing.FileSha256Async(xpilotPlugin, cancellationToken);
        return new XPlaneInstallation(normalized, executable, plugins, info.ProductVersion,
            File.Exists(xvatsim), File.Exists(xpilotPlugin), xpilotHash, source);
    }

    private IReadOnlyList<XPilotInstallation> DiscoverXPilot()
    {
        var candidates = new List<(string Path, string Source)>
        {
            (Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "org.vatsim.xpilot", "Application", "xPilot.exe"), "xPilot standard location"),
            (Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "xPilot", "xPilot.exe"), "xPilot legacy location"),
            (Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
                "xPilot", "xPilot.exe"), "Program Files")
        };
        AddUninstallLocations(candidates, includeXPlane: false);
        return candidates
            .DistinctBy(x => NormalizePath(x.Path), StringComparer.OrdinalIgnoreCase)
            .Select(x => ValidateXPilot(x.Path, x.Source))
            .Where(x => x is not null)
            .Cast<XPilotInstallation>()
            .ToArray();
    }

    private XPilotInstallation? ValidateXPilot(string executable, string source)
    {
        if (!File.Exists(executable))
            return null;
        var info = FileVersionInfo.GetVersionInfo(executable);
        var identity = $"{info.ProductName} {info.FileDescription}";
        if (!identity.Contains("xPilot", StringComparison.OrdinalIgnoreCase))
            return null;
        var version = info.ProductVersion ?? info.FileVersion ?? "unknown";
        var generation = ParseMajor(version) switch
        {
            3 => XPilotGeneration.Version3,
            >= 4 => XPilotGeneration.Version4,
            _ => XPilotGeneration.Unknown
        };
        var supported = generation == XPilotGeneration.Version4 &&
            _manifest.SupportedXPilot4ProductVersionPrefixes.Any(prefix =>
                version.StartsWith(prefix, StringComparison.OrdinalIgnoreCase));
        return new XPilotInstallation(NormalizePath(executable), version, generation, supported, source);
    }

    private static int ParseMajor(string version)
    {
        var match = Regex.Match(version, @"(?<!\d)(\d+)(?:\.\d+)");
        return match.Success && int.TryParse(match.Groups[1].Value, out var major) ? major : 0;
    }

    private static void AddXPlaneInstallerRecords(List<(string Path, string Source)> candidates)
    {
        var local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        foreach (var file in Directory.EnumerateFiles(local, "x-plane_install_*.txt", SearchOption.TopDirectoryOnly))
        {
            try
            {
                candidates.AddRange(File.ReadAllLines(file)
                    .Where(line => !string.IsNullOrWhiteSpace(line))
                    .Select(line => (line.Trim(), $"{Path.GetFileName(file)} record")));
            }
            catch (IOException) { }
        }
    }

    private static void AddCommonXPlanePaths(List<(string Path, string Source)> candidates)
    {
        foreach (var drive in DriveInfo.GetDrives().Where(d => d.DriveType == DriveType.Fixed && d.IsReady))
        {
            candidates.Add((Path.Combine(drive.RootDirectory.FullName, "X-Plane 12"), "common location"));
            candidates.Add((Path.Combine(drive.RootDirectory.FullName, "Games", "X-Plane 12"), "common location"));
        }
    }

    private static void AddSteamXPlanePaths(List<(string Path, string Source)> candidates)
    {
        var steamRoots = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var root in new[]
        {
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Steam")
        })
            if (Directory.Exists(root)) steamRoots.Add(root);

        foreach (var root in steamRoots.ToArray())
        {
            var libraryFile = Path.Combine(root, "steamapps", "libraryfolders.vdf");
            if (!File.Exists(libraryFile)) continue;
            try
            {
                foreach (Match match in Regex.Matches(File.ReadAllText(libraryFile), "\\\"path\\\"\\s+\\\"([^\\\"]+)\\\""))
                    steamRoots.Add(match.Groups[1].Value.Replace("\\\\", "\\"));
            }
            catch (IOException) { }
        }
        foreach (var root in steamRoots)
            candidates.Add((Path.Combine(root, "steamapps", "common", "X-Plane 12"), "Steam library"));
    }

    private static void AddUninstallLocations(List<(string Path, string Source)> candidates, bool includeXPlane)
    {
        foreach (var hive in new[] { RegistryHive.CurrentUser, RegistryHive.LocalMachine })
        foreach (var view in new[] { RegistryView.Registry64, RegistryView.Registry32 })
        {
            try
            {
                using var baseKey = RegistryKey.OpenBaseKey(hive, view);
                using var uninstall = baseKey.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall");
                if (uninstall is null) continue;
                foreach (var name in uninstall.GetSubKeyNames())
                {
                    using var key = uninstall.OpenSubKey(name);
                    var displayName = key?.GetValue("DisplayName") as string ?? string.Empty;
                    var location = key?.GetValue("InstallLocation") as string;
                    if (string.IsNullOrWhiteSpace(location)) continue;
                    if (includeXPlane && displayName.Contains("X-Plane", StringComparison.OrdinalIgnoreCase))
                        candidates.Add((location, "Windows installed-app record"));
                    if (!includeXPlane && displayName.Contains("xPilot", StringComparison.OrdinalIgnoreCase))
                        candidates.Add((Path.Combine(location, "xPilot.exe"), "Windows installed-app record"));
                }
            }
            catch (Exception ex) when (ex is UnauthorizedAccessException or IOException) { }
        }
    }

    private static string NormalizePath(string path) =>
        Path.GetFullPath(Environment.ExpandEnvironmentVariables(path.Trim().Trim('"')))
            .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);

}
