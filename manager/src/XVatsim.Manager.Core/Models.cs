using System.Text.Json.Serialization;

namespace XVatsim.Manager.Core;

public enum PayloadTarget
{
    XPlane,
    XPilot4
}

public enum XPilotGeneration
{
    Unknown,
    Version3,
    Version4
}

public enum RecommendedAction
{
    Install,
    Update,
    Repair,
    Current
}

public sealed record PayloadFile(
    string RelativePath,
    PayloadTarget Target,
    long Length,
    string Sha256,
    bool Required = true);

public sealed record ManagerManifest(
    string SchemaVersion,
    string ProductVersion,
    string CreatedUtc,
    IReadOnlyList<PayloadFile> Files);

public sealed record XPlaneInstallation(
    string RootPath,
    string ExecutablePath,
    string PluginDirectory,
    string? ProductVersion,
    bool HasXVatsim,
    bool HasXPilotPlugin,
    string Source);

public sealed record XPilotInstallation(
    string ExecutablePath,
    string ProductVersion,
    XPilotGeneration Generation,
    string Source);

public sealed record DiscoverySnapshot(
    IReadOnlyList<XPlaneInstallation> XPlaneInstallations,
    IReadOnlyList<XPilotInstallation> XPilotInstallations,
    IReadOnlyList<string> RejectedCandidates,
    DateTimeOffset CompletedUtc);

public sealed record PlannedFile(
    PayloadFile Payload,
    string DestinationPath,
    bool Exists,
    bool MatchesPayload,
    string? ExistingSha256);

public sealed record InstallPlan(
    XPlaneInstallation XPlane,
    XPilotInstallation? XPilot,
    RecommendedAction Action,
    IReadOnlyList<PlannedFile> Files,
    IReadOnlyList<string> Warnings,
    string Summary,
    bool CanInstallBridge,
    bool CanInstallSafely);

public sealed record InstalledFileReceipt(
    string RelativePath,
    PayloadTarget Target,
    string DestinationPath,
    long Length,
    string Sha256);

public sealed record InstallReceipt(
    string SchemaVersion,
    string ProductVersion,
    string XPlaneRoot,
    string? XPilotExecutable,
    string TransactionId,
    DateTimeOffset InstalledUtc,
    IReadOnlyList<InstalledFileReceipt> Files);

public sealed record InstallResult(
    bool Succeeded,
    bool RolledBack,
    string TransactionId,
    string Message,
    string? BackupDirectory,
    InstallReceipt? Receipt);

[JsonSourceGenerationOptions(WriteIndented = true, PropertyNamingPolicy = JsonKnownNamingPolicy.CamelCase,
    UseStringEnumConverter = true)]
[JsonSerializable(typeof(ManagerManifest))]
[JsonSerializable(typeof(InstallReceipt))]
[JsonSerializable(typeof(DiscoverySnapshot))]
internal partial class ManagerJsonContext : JsonSerializerContext;
