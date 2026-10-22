using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace XVatsim.Manager.Core;

public sealed record RemoteManagerPayload(
    [property: JsonPropertyName("url")] string Url,
    [property: JsonPropertyName("size_bytes")] long SizeBytes,
    [property: JsonPropertyName("sha256")] string Sha256,
    [property: JsonPropertyName("minimum_manager_version")] string MinimumManagerVersion);

public sealed record PublicUpdateManifest(
    [property: JsonPropertyName("schema")] int Schema,
    [property: JsonPropertyName("product")] string Product,
    [property: JsonPropertyName("channel")] string Channel,
    [property: JsonPropertyName("latest_version")] string LatestVersion,
    [property: JsonPropertyName("manager_payload")] RemoteManagerPayload? ManagerPayload);

public enum PayloadSelectionStatus
{
    EmbeddedCurrent,
    RemotePayloadReady,
    ManualManagerRequired,
    PublishedPayloadUnavailable,
    CheckFailed
}

public sealed record PayloadSelection(
    IPayloadProvider Payload,
    PayloadSelectionStatus Status,
    string Message,
    string? PublishedVersion,
    string? Diagnostic = null)
{
    public bool UsesRemotePayload => Status == PayloadSelectionStatus.RemotePayloadReady;
}

public static class ReleaseVersion
{
    public static bool TryParse(string? text, out Version version)
    {
        version = new Version();
        if (string.IsNullOrWhiteSpace(text)) return false;
        var normalized = text.Trim();
        if (normalized.StartsWith('v') || normalized.StartsWith('V'))
            normalized = normalized[1..];
        normalized = normalized.Split(['-', '+'], 2)[0];
        var parts = normalized.Split('.');
        if (parts.Length != 3 || parts.Any(part =>
                !int.TryParse(part, out var value) || value < 0))
            return false;
        return Version.TryParse(normalized, out version!);
    }

    public static bool TryCompare(string left, string right, out int comparison)
    {
        comparison = 0;
        if (!TryParse(left, out var leftVersion) || !TryParse(right, out var rightVersion))
            return false;
        comparison = leftVersion.CompareTo(rightVersion);
        return true;
    }
}

public sealed class ManagerUpdateService
{
    public const long MaximumPayloadBytes = 128L * 1024L * 1024L;
    private const long MaximumManifestBytes = 256L * 1024L;

    private readonly HttpClient _http;
    private readonly Uri _manifestUri;

    public ManagerUpdateService(HttpClient http, Uri manifestUri)
    {
        _http = http ?? throw new ArgumentNullException(nameof(http));
        _manifestUri = manifestUri ?? throw new ArgumentNullException(nameof(manifestUri));
        if (!_manifestUri.IsAbsoluteUri || _manifestUri.Scheme != Uri.UriSchemeHttps)
            throw new ArgumentException("The update manifest URL must use HTTPS.", nameof(manifestUri));
    }

    public async Task<PayloadSelection> SelectPayloadAsync(
        IPayloadProvider embeddedPayload,
        string managerVersion,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(embeddedPayload);
        string? publishedVersion = null;
        try
        {
            if (!ReleaseVersion.TryParse(managerVersion, out var runningManagerVersion))
                throw new InvalidDataException("The running Manager version is invalid.");

            using var manifestResponse = await _http.GetAsync(
                _manifestUri, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
            manifestResponse.EnsureSuccessStatusCode();
            var manifestBytes = await ReadLimitedAsync(
                manifestResponse.Content, MaximumManifestBytes, cancellationToken);
            var manifest = JsonSerializer.Deserialize(manifestBytes, ManagerJsonContext.Default.PublicUpdateManifest)
                ?? throw new InvalidDataException("The public update file is empty.");
            ValidatePublicManifest(manifest);
            publishedVersion = manifest.LatestVersion;

            if (!ReleaseVersion.TryCompare(
                    manifest.LatestVersion, embeddedPayload.Manifest.ProductVersion, out var publishedComparison))
                throw new InvalidDataException("The public or embedded product version is invalid.");
            if (publishedComparison <= 0)
            {
                return new PayloadSelection(embeddedPayload, PayloadSelectionStatus.EmbeddedCurrent,
                    $"Online check complete. This Manager contains XVatsim {embeddedPayload.Manifest.ProductVersion}.",
                    manifest.LatestVersion);
            }

            if (manifest.ManagerPayload is null)
            {
                return new PayloadSelection(embeddedPayload, PayloadSelectionStatus.ManualManagerRequired,
                    $"XVatsim {manifest.LatestVersion} is available, but it requires the Manager included with that release.",
                    manifest.LatestVersion);
            }

            ValidateDescriptor(manifest.ManagerPayload);
            if (!ReleaseVersion.TryParse(manifest.ManagerPayload.MinimumManagerVersion, out var minimumManagerVersion))
                throw new InvalidDataException("The minimum Manager version is invalid.");
            if (runningManagerVersion < minimumManagerVersion)
            {
                return new PayloadSelection(embeddedPayload, PayloadSelectionStatus.ManualManagerRequired,
                    $"XVatsim {manifest.LatestVersion} requires Manager {manifest.ManagerPayload.MinimumManagerVersion} or newer. Download the Manager included with that release once.",
                    manifest.LatestVersion);
            }

            var payloadUri = new Uri(manifest.ManagerPayload.Url, UriKind.Absolute);
            using var payloadResponse = await _http.GetAsync(
                payloadUri, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
            payloadResponse.EnsureSuccessStatusCode();
            var payloadBytes = await ReadLimitedAsync(
                payloadResponse.Content, manifest.ManagerPayload.SizeBytes, cancellationToken);
            if (payloadBytes.LongLength != manifest.ManagerPayload.SizeBytes)
                throw new InvalidDataException("The downloaded payload length does not match the published length.");
            var actualHash = Convert.ToHexString(SHA256.HashData(payloadBytes));
            if (!Hashing.EqualsHash(actualHash, manifest.ManagerPayload.Sha256))
                throw new InvalidDataException("The downloaded payload SHA-256 does not match the published SHA-256.");

            var remotePayload = new ZipPayloadProvider(payloadBytes);
            if (!string.Equals(remotePayload.Manifest.ProductVersion, manifest.LatestVersion,
                    StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("The downloaded payload version does not match the public update version.");
            return new PayloadSelection(remotePayload, PayloadSelectionStatus.RemotePayloadReady,
                $"XVatsim {manifest.LatestVersion} was downloaded and verified. Select Update when ready.",
                manifest.LatestVersion);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch (Exception error) when (error is HttpRequestException or OperationCanceledException or
                                      IOException or JsonException or InvalidDataException or ArgumentException)
        {
            var newerReleaseWasFound = publishedVersion is not null && ReleaseVersion.TryCompare(
                publishedVersion, embeddedPayload.Manifest.ProductVersion, out var comparison) && comparison > 0;
            return new PayloadSelection(embeddedPayload,
                newerReleaseWasFound
                    ? PayloadSelectionStatus.PublishedPayloadUnavailable
                    : PayloadSelectionStatus.CheckFailed,
                newerReleaseWasFound
                    ? $"XVatsim {publishedVersion} is available, but its online package could not be verified. Download the current release package to continue."
                    : "The Manager could not verify online updates. Its built-in package remains available.",
                publishedVersion,
                $"{error.GetType().Name}: {error.Message}");
        }
    }

    private static void ValidatePublicManifest(PublicUpdateManifest manifest)
    {
        if (manifest.Schema != 1 ||
            !string.Equals(manifest.Product, "XVatsim", StringComparison.Ordinal) ||
            !string.Equals(manifest.Channel, "stable", StringComparison.OrdinalIgnoreCase) ||
            !ReleaseVersion.TryParse(manifest.LatestVersion, out _))
            throw new InvalidDataException("The public update file identity or version is invalid.");
    }

    private static void ValidateDescriptor(RemoteManagerPayload descriptor)
    {
        if (string.IsNullOrWhiteSpace(descriptor.Url) ||
            !Uri.TryCreate(descriptor.Url, UriKind.Absolute, out var uri) || uri.Scheme != Uri.UriSchemeHttps)
            throw new InvalidDataException("The Manager payload URL must use HTTPS.");
        if (descriptor.SizeBytes <= 0 || descriptor.SizeBytes > MaximumPayloadBytes)
            throw new InvalidDataException("The Manager payload length is outside the permitted range.");
        if (string.IsNullOrWhiteSpace(descriptor.Sha256) || descriptor.Sha256.Length != 64 ||
            descriptor.Sha256.Any(character => !Uri.IsHexDigit(character)))
            throw new InvalidDataException("The Manager payload SHA-256 is invalid.");
    }

    private static async Task<byte[]> ReadLimitedAsync(
        HttpContent content,
        long maximumBytes,
        CancellationToken cancellationToken)
    {
        if (content.Headers.ContentLength is > 0 && content.Headers.ContentLength > maximumBytes)
            throw new InvalidDataException("The download is larger than the permitted length.");
        await using var input = await content.ReadAsStreamAsync(cancellationToken);
        using var output = new MemoryStream();
        var buffer = new byte[64 * 1024];
        while (true)
        {
            var read = await input.ReadAsync(buffer, cancellationToken);
            if (read == 0) break;
            if (output.Length + read > maximumBytes)
                throw new InvalidDataException("The download is larger than the permitted length.");
            output.Write(buffer, 0, read);
        }
        return output.ToArray();
    }
}
