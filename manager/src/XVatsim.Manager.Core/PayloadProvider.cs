using System.IO.Compression;
using System.Reflection;
using System.Text.Json;

namespace XVatsim.Manager.Core;

public interface IPayloadProvider
{
    ManagerManifest Manifest { get; }
    IReadOnlyCollection<string> EntryNames { get; }
    Task CopyFileToAsync(string relativePath, string destinationPath, CancellationToken cancellationToken);
}

public sealed class ZipPayloadProvider : IPayloadProvider
{
    private readonly byte[] _archiveBytes;
    private readonly Dictionary<string, string> _entries;

    public ManagerManifest Manifest { get; }
    public IReadOnlyCollection<string> EntryNames => _entries.Keys;

    public ZipPayloadProvider(byte[] archiveBytes)
    {
        _archiveBytes = archiveBytes ?? throw new ArgumentNullException(nameof(archiveBytes));
        using var archive = OpenArchive();
        _entries = new(StringComparer.OrdinalIgnoreCase);
        foreach (var entry in archive.Entries)
        {
            var normalized = NormalizeAndValidate(entry.FullName);
            if (normalized.EndsWith('/'))
                continue;
            if (!_entries.TryAdd(normalized, entry.FullName))
                throw new InvalidDataException($"Duplicate payload entry: {normalized}");
        }

        if (!_entries.TryGetValue("manifest.json", out var manifestEntryName))
            throw new InvalidDataException("The embedded payload has no manifest.json.");
        var manifestEntry = archive.GetEntry(manifestEntryName)!;
        using var stream = manifestEntry.Open();
        Manifest = JsonSerializer.Deserialize(stream, ManagerJsonContext.Default.ManagerManifest)
            ?? throw new InvalidDataException("The payload manifest is empty or invalid.");
        ValidateInventory();
    }

    public static ZipPayloadProvider FromAssembly(Assembly assembly, string resourceName)
    {
        using var stream = assembly.GetManifestResourceStream(resourceName)
            ?? throw new InvalidOperationException("This manager build does not contain an installation payload.");
        using var memory = new MemoryStream();
        stream.CopyTo(memory);
        return new ZipPayloadProvider(memory.ToArray());
    }

    public async Task CopyFileToAsync(string relativePath, string destinationPath, CancellationToken cancellationToken)
    {
        var normalized = NormalizeAndValidate(relativePath);
        if (!_entries.TryGetValue(normalized, out var entryName))
            throw new InvalidDataException($"The payload is missing {normalized}.");

        using var archive = OpenArchive();
        var entry = archive.GetEntry(entryName)
            ?? throw new InvalidDataException($"The payload is missing {normalized}.");
        Directory.CreateDirectory(Path.GetDirectoryName(destinationPath)!);
        await using var input = entry.Open();
        await using var output = new FileStream(destinationPath, FileMode.Create, FileAccess.Write, FileShare.None,
            128 * 1024, FileOptions.Asynchronous | FileOptions.SequentialScan);
        await input.CopyToAsync(output, cancellationToken);
        await output.FlushAsync(cancellationToken);
    }

    private ZipArchive OpenArchive() => new(new MemoryStream(_archiveBytes, writable: false), ZipArchiveMode.Read);

    private void ValidateInventory()
    {
        if (Manifest.SchemaVersion != "2")
            throw new InvalidDataException($"Unsupported payload schema {Manifest.SchemaVersion}.");
        if (string.IsNullOrWhiteSpace(Manifest.ProductVersion) || Manifest.Files.Count == 0)
            throw new InvalidDataException("The payload manifest has no product version or files.");

        var expected = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { "manifest.json" };
        foreach (var file in Manifest.Files)
        {
            var normalized = NormalizeAndValidate(file.RelativePath);
            if (normalized == "manifest.json" || !expected.Add(normalized))
                throw new InvalidDataException($"Invalid or duplicate manifest path: {normalized}");
            if (file.Length < 0 || file.Sha256.Length != 64 || file.Sha256.Any(c => !Uri.IsHexDigit(c)))
                throw new InvalidDataException($"Invalid size or SHA-256 for {normalized}.");
        }

        var unexpected = _entries.Keys.Where(entry => !expected.Contains(entry)).ToArray();
        var missing = expected.Where(entry => !_entries.ContainsKey(entry)).ToArray();
        if (unexpected.Length > 0 || missing.Length > 0)
            throw new InvalidDataException($"Payload inventory mismatch. Unexpected=[{string.Join(',', unexpected)}] Missing=[{string.Join(',', missing)}]");
    }

    internal static string NormalizeAndValidate(string path)
    {
        if (string.IsNullOrWhiteSpace(path))
            throw new InvalidDataException("Payload path is empty.");
        var normalized = path.Replace('\\', '/');
        var pathWithoutDirectoryMarker = normalized.TrimEnd('/');
        var parts = pathWithoutDirectoryMarker.Split('/');
        if (Path.IsPathRooted(normalized) || normalized.StartsWith('/') ||
            parts.Any(part => string.IsNullOrWhiteSpace(part) || part is ".." or "."))
            throw new InvalidDataException($"Unsafe payload path: {path}");
        return normalized;
    }
}
