using System.Reflection;
using System.Windows;
using XVatsim.Manager.Core;

namespace XVatsim.Manager;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        if (e.Args.Contains("--self-test", StringComparer.OrdinalIgnoreCase))
        {
            try
            {
                var payload = ZipPayloadProvider.FromAssembly(Assembly.GetExecutingAssembly(), "XVatsim.Manager.payload.zip");
                var snapshot = Task.Run(() => new DiscoveryService(payload.Manifest).DiscoverFastAsync())
                    .GetAwaiter().GetResult();
                if (payload.Manifest.Files.Count < 4 || snapshot.XPlaneInstallations.Count == 0)
                    throw new InvalidOperationException("Payload or local discovery self-test did not produce the expected facts.");
                Environment.Exit(0);
            }
            catch
            {
                Environment.Exit(2);
            }
            return;
        }

        new MainWindow().Show();
    }
}
