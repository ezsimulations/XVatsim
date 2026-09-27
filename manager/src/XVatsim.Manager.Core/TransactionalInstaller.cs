namespace XVatsim.Manager.Core;

public interface IInstallFaultInjector
{
    void BeforeReplace(int zeroBasedIndex, PlannedFile file);
}

public sealed class NoInstallFaults : IInstallFaultInjector
{
    public static NoInstallFaults Instance { get; } = new();
    private NoInstallFaults() { }
    public void BeforeReplace(int zeroBasedIndex, PlannedFile file) { }
}

public sealed class TransactionalInstaller
{
    private sealed record RollbackItem(string Destination, string? Backup, bool Existed);

    private readonly IPayloadProvider _payload;
    private readonly ReceiptStore _receipts;
    private readonly string _managerRoot;
    private readonly IInstallFaultInjector _faults;
    private readonly Func<IReadOnlyList<string>> _runningApplications;

    public TransactionalInstaller(
        IPayloadProvider payload,
        ReceiptStore receipts,
        string? managerRoot = null,
        IInstallFaultInjector? faults = null,
        Func<IReadOnlyList<string>>? runningApplications = null)
    {
        _payload = payload;
        _receipts = receipts;
        _managerRoot = managerRoot ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "XVatsimManager");
        _faults = faults ?? NoInstallFaults.Instance;
        _runningApplications = runningApplications ?? ProcessGuard.RunningApplications;
    }

    public async Task<InstallResult> ExecuteAsync(
        InstallPlan plan,
        IProgress<string>? progress = null,
        CancellationToken cancellationToken = default,
        bool bypassProcessGuardForIsolatedTests = false)
    {
        if (!plan.CanInstallSafely)
            return new InstallResult(false, false, string.Empty,
                "Installation is unavailable until both X-Plane and xPilot are verified.", null, null);
        var running = bypassProcessGuardForIsolatedTests ? [] : _runningApplications();
        if (running.Count > 0)
            return new InstallResult(false, false, string.Empty,
                $"Close {string.Join(" and ", running)} and choose Retry.", null, null);
        if (plan.Action == RecommendedAction.Current)
            return new InstallResult(true, false, string.Empty, "The verified installation is already current.", null, null);

        var transactionId = $"{DateTime.UtcNow:yyyyMMdd-HHmmss}-{Guid.NewGuid():N}";
        var staging = Path.Combine(_managerRoot, "staging", transactionId);
        var backupRoot = Path.Combine(_managerRoot, "backups", transactionId);
        var rollback = new List<RollbackItem>();
        var preparedRollback = new Dictionary<string, RollbackItem>(StringComparer.OrdinalIgnoreCase);
        var changes = plan.Files.Where(file => !file.MatchesPayload).ToArray();

        try
        {
            progress?.Report("Verifying the installation package");
            Directory.CreateDirectory(staging);
            for (var i = 0; i < changes.Length; i++)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var planned = changes[i];
                var stagePath = Path.Combine(staging, planned.Payload.Target.ToString(),
                    planned.Payload.RelativePath.Replace('/', Path.DirectorySeparatorChar));
                await _payload.CopyFileToAsync(planned.Payload.RelativePath, stagePath, cancellationToken);
                var info = new FileInfo(stagePath);
                var hash = await Hashing.FileSha256Async(stagePath, cancellationToken);
                if (info.Length != planned.Payload.Length || !Hashing.EqualsHash(hash, planned.Payload.Sha256))
                    throw new InvalidDataException($"Package verification failed for {planned.Payload.RelativePath}.");
            }

            progress?.Report("Backing up existing managed files");
            foreach (var planned in changes)
            {
                var existed = File.Exists(planned.DestinationPath);
                string? backup = null;
                if (existed)
                {
                    backup = Path.Combine(backupRoot, planned.Payload.Target.ToString(),
                        planned.Payload.RelativePath.Replace('/', Path.DirectorySeparatorChar));
                    Directory.CreateDirectory(Path.GetDirectoryName(backup)!);
                    File.Copy(planned.DestinationPath, backup, overwrite: false);
                }
                preparedRollback[planned.DestinationPath] = new RollbackItem(planned.DestinationPath, backup, existed);
            }

            progress?.Report("Installing verified files");
            for (var i = 0; i < changes.Length; i++)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var planned = changes[i];
                _faults.BeforeReplace(i, planned);
                var stagePath = Path.Combine(staging, planned.Payload.Target.ToString(),
                    planned.Payload.RelativePath.Replace('/', Path.DirectorySeparatorChar));
                ReplaceFromFile(stagePath, planned.DestinationPath);
                rollback.Add(preparedRollback[planned.DestinationPath]);
            }

            progress?.Report("Checking the completed installation");
            foreach (var planned in plan.Files)
            {
                var info = new FileInfo(planned.DestinationPath);
                if (!info.Exists || info.Length != planned.Payload.Length ||
                    !Hashing.EqualsHash(await Hashing.FileSha256Async(planned.DestinationPath, cancellationToken), planned.Payload.Sha256))
                    throw new IOException($"Installed-file verification failed for {planned.Payload.RelativePath}.");
            }

            var receipt = new InstallReceipt("1", _payload.Manifest.ProductVersion, plan.XPlane.RootPath,
                plan.XPilot?.ExecutablePath, transactionId, DateTimeOffset.UtcNow,
                plan.Files.Select(file => new InstalledFileReceipt(file.Payload.RelativePath, file.Payload.Target,
                    file.DestinationPath, file.Payload.Length, file.Payload.Sha256)).ToArray());
            await _receipts.WriteAsync(receipt, cancellationToken);
            TryDeleteDirectory(staging);
            return new InstallResult(true, false, transactionId,
                "XVatsim was installed and every managed file passed verification.", backupRoot, receipt);
        }
        catch (Exception installError)
        {
            var rollbackErrors = new List<string>();
            for (var i = rollback.Count - 1; i >= 0; i--)
            {
                var item = rollback[i];
                try
                {
                    if (item.Existed && item.Backup is not null)
                        ReplaceFromFile(item.Backup, item.Destination);
                    else if (File.Exists(item.Destination))
                        File.Delete(item.Destination);
                }
                catch (Exception rollbackError)
                {
                    rollbackErrors.Add($"{Path.GetFileName(item.Destination)}: {rollbackError.Message}");
                }
            }
            TryDeleteDirectory(staging);
            var suffix = rollbackErrors.Count == 0
                ? "Previous files were restored."
                : $"Rollback needs attention: {string.Join("; ", rollbackErrors)}";
            return new InstallResult(false, rollback.Count > 0 && rollbackErrors.Count == 0, transactionId,
                $"Installation stopped: {installError.Message} {suffix}",
                Directory.Exists(backupRoot) ? backupRoot : null, null);
        }
    }

    private static void ReplaceFromFile(string source, string destination)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        var temporary = Path.Combine(Path.GetDirectoryName(destination)!,
            $".{Path.GetFileName(destination)}.xvatsim-manager-{Guid.NewGuid():N}.tmp");
        try
        {
            File.Copy(source, temporary, overwrite: false);
            File.Move(temporary, destination, overwrite: true);
        }
        finally
        {
            if (File.Exists(temporary)) File.Delete(temporary);
        }
    }

    private static void TryDeleteDirectory(string path)
    {
        try { if (Directory.Exists(path)) Directory.Delete(path, recursive: true); }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }
}
