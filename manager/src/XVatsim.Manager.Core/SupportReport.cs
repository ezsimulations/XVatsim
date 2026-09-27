using System.Text;

namespace XVatsim.Manager.Core;

public static class SupportReport
{
    public static string Create(DiscoverySnapshot snapshot, InstallPlan? plan)
    {
        var text = new StringBuilder();
        text.AppendLine("XVatsim Manager support report");
        text.AppendLine($"Generated UTC: {DateTimeOffset.UtcNow:O}");
        text.AppendLine($"Windows: {Environment.OSVersion.VersionString}");
        text.AppendLine($"Manager runtime: {Environment.Version}");
        text.AppendLine();
        text.AppendLine("Validated X-Plane installations:");
        foreach (var xplane in snapshot.XPlaneInstallations)
            text.AppendLine($"- {xplane.RootPath} | X-Plane={xplane.ProductVersion ?? "unknown"} | XVatsim={xplane.HasXVatsim} | xPilot plugin={xplane.HasXPilotPlugin} | source={xplane.Source}");
        text.AppendLine("Detected xPilot clients:");
        foreach (var xpilot in snapshot.XPilotInstallations)
            text.AppendLine($"- {xpilot.ExecutablePath} | version={xpilot.ProductVersion} | generation={xpilot.Generation} | source={xpilot.Source}");
        if (plan is not null)
        {
            text.AppendLine($"Recommended action: {plan.Action}");
            foreach (var file in plan.Files)
                text.AppendLine($"- managed {file.Payload.Target}/{file.Payload.RelativePath} | exists={file.Exists} | verified={file.MatchesPayload}");
            foreach (var warning in plan.Warnings)
                text.AppendLine($"- notice: {warning}");
        }
        text.AppendLine();
        text.AppendLine("This report contains installation metadata only. It does not include private messages, PDC/ACARS content, controller chat, or XVatsim logs.");
        return text.ToString();
    }
}
