using System.Reflection;
using System.Text;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;
using XVatsim.Manager.Core;

namespace XVatsim.Manager;

public partial class MainWindow : Window
{
    private readonly ZipPayloadProvider? _payload;
    private readonly DiscoveryService? _discovery;
    private readonly ReceiptStore _receipts = new();
    private DiscoverySnapshot? _snapshot;
    private InstallPlan? _plan;
    private XPilotInstallation? _selectedXPilot;
    private bool _updatingSelection;

    private static readonly (int DelayMilliseconds, double Percent, string Message)[] InstallationVisualSteps =
    [
        (0, 8, "Preparing a safe installation"),
        (800, 24, "Verifying the installation package"),
        (1000, 43, "Preserving existing managed files"),
        (1000, 67, "Installing verified files"),
        (1200, 86, "Checking the completed installation"),
        (1000, 96, "Finalizing the installation record")
    ];

    public MainWindow()
    {
        InitializeComponent();
        try
        {
            _payload = ZipPayloadProvider.FromAssembly(Assembly.GetExecutingAssembly(), "XVatsim.Manager.payload.zip");
            _discovery = new DiscoveryService();
        }
        catch (Exception error)
        {
            Loaded += (_, _) => ShowFatal($"This manager package cannot be used: {error.Message}");
            return;
        }
        Loaded += async (_, _) => await ScanFastAsync();
    }

    private async Task ScanFastAsync(string? preferredRoot = null)
    {
        if (_discovery is null) return;
        SetBusy("Checking X-Plane and xPilot installations");
        try
        {
            _snapshot = await Task.Run(() => _discovery.DiscoverFastAsync());
            await PopulateDiscoveryAsync(preferredRoot);
        }
        catch (Exception error)
        {
            ShowFatal($"The scan could not finish: {error.Message}");
        }
    }

    private async Task PopulateDiscoveryAsync(string? preferredRoot = null)
    {
        if (_snapshot is null) return;
        XPlaneInstallation? selected = null;
        if (preferredRoot is not null)
            selected = _snapshot.XPlaneInstallations.FirstOrDefault(x =>
                string.Equals(x.RootPath, preferredRoot, StringComparison.OrdinalIgnoreCase));
        if (selected is null && _snapshot.XPlaneInstallations.Count == 1)
            selected = _snapshot.XPlaneInstallations[0];

        _updatingSelection = true;
        try
        {
            XPlaneSelector.ItemsSource = _snapshot.XPlaneInstallations;
            XPlaneSelector.SelectedItem = selected;
        }
        finally
        {
            _updatingSelection = false;
        }

        _selectedXPilot ??= XPilotSelection.Preferred(_snapshot.XPilotInstallations);
        RenderXPilotSelection();
        WorkProgress.Visibility = Visibility.Collapsed;
        if (selected is not null)
            XPlaneHint.Text = $"X-Plane folder found: {selected.RootPath}";
        if (_snapshot.XPlaneInstallations.Count == 0)
        {
            XPlaneHint.Text = "X-Plane folder not found. Choose folder to locate your X-Plane 12 installation.";
            ShowPrerequisiteState("Choose the X-Plane folder that contains X-Plane.exe.");
        }
        else if (_snapshot.XPlaneInstallations.Count > 1 && XPlaneSelector.SelectedItem is null)
        {
            XPlaneHint.Text = "More than one X-Plane folder was found. Select the folder where you want XVatsim installed.";
            ShowPrerequisiteState("Select the X-Plane folder where you want XVatsim installed.");
        }
        else if (_selectedXPilot is null)
        {
            ShowPrerequisiteState("Choose your xPilot installation folder before installing XVatsim.");
        }
        else if (selected is not null)
        {
            await LoadPlanAsync(selected);
        }
    }

    private async void XPlaneSelector_OnSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_updatingSelection || XPlaneSelector.SelectedItem is not XPlaneInstallation selected)
            return;
        if (_selectedXPilot is null)
            ShowPrerequisiteState("Choose your xPilot installation folder before installing XVatsim.");
        else
            await LoadPlanAsync(selected);
    }

    private async Task LoadPlanAsync(XPlaneInstallation selected)
    {
        if (_payload is null || _snapshot is null) return;
        SetBusy("Checking managed files");
        try
        {
            _plan = await Task.Run(() =>
                new InstallPlanner(_payload, _receipts).BuildAsync(selected, [_selectedXPilot!]));
            RenderPlan();
        }
        catch (Exception error)
        {
            ShowFatal($"The installation could not be checked: {error.Message}");
        }
    }

    private void RenderPlan()
    {
        if (_plan is null) return;
        WorkProgress.Visibility = Visibility.Collapsed;
        SetNavigationEnabled(true);
        XPlaneHint.Text = $"X-Plane folder found: {_plan.XPlane.RootPath}";
        ActionTitle.Text = _plan.Action switch
        {
            RecommendedAction.Install => "Ready to install XVatsim",
            RecommendedAction.Update => "XVatsim update ready",
            RecommendedAction.Repair => "XVatsim needs repair",
            _ => "XVatsim is current"
        };
        ActionSummary.Text = _plan.CanInstallSafely
            ? _plan.Summary
            : "The selected X-Plane and xPilot installations could not be verified as a safe pair.";
        PrimaryAction.Content = _plan.Action.ToString();
        PrimaryAction.IsEnabled = _plan.CanInstallSafely && _plan.Action != RecommendedAction.Current;
        if (!_plan.CanInstallSafely)
            ActionTitle.Text = "Installation needs a verified xPilot setup";
        ProgressText.Text = !_plan.CanInstallSafely
            ? "Choose the matching xPilot folder, or reinstall xPilot into this X-Plane installation."
            : _plan.Action == RecommendedAction.Current
            ? "Every managed file matches the verified payload."
            : "X-Plane and xPilot must be closed before installation.";

        WarningsPanel.Children.Clear();
        foreach (var warning in _plan.Warnings)
        {
            WarningsPanel.Children.Add(new TextBlock
            {
                Text = $"• {warning}",
                Foreground = System.Windows.Media.Brushes.Orange,
                TextWrapping = TextWrapping.Wrap,
                Margin = new Thickness(0, 2, 0, 2)
            });
        }
        var details = new StringBuilder();
        details.AppendLine($"Payload version: {_payload!.Manifest.ProductVersion}");
        details.AppendLine($"X-Plane root: {_plan.XPlane.RootPath}");
        details.AppendLine($"xPilot: {_plan.XPilot?.ProductVersion ?? "not detected"}");
        details.AppendLine($"xPilot 4 companion: {(_plan.CanInstallBridge ? "will be installed" : "not applicable")}");
        details.AppendLine();
        foreach (var file in _plan.Files)
            details.AppendLine($"{(file.MatchesPayload ? "Verified" : file.Exists ? "Replace with backup" : "Install")} — {file.DestinationPath}");
        AdvancedDetails.Text = details.ToString();
    }

    private async void PrimaryAction_OnClick(object sender, RoutedEventArgs e)
    {
        if (_plan is null || _payload is null) return;
        var running = ProcessGuard.RunningApplications();
        if (running.Count > 0)
        {
            ProgressText.Text = $"Close {string.Join(" and ", running)}, then choose {_plan.Action} again.";
            ProgressText.Foreground = System.Windows.Media.Brushes.Orange;
            return;
        }

        SetBusy("Preparing the verified installation");
        BeginInstallationProgress();
        using var animationCancellation = new CancellationTokenSource();
        var animationTask = PlayInstallationProgressAsync(animationCancellation.Token);
        InstallResult result;
        try
        {
            result = await new TransactionalInstaller(_payload, _receipts).ExecuteAsync(_plan);
        }
        catch (Exception error)
        {
            animationCancellation.Cancel();
            await IgnoreCancelledAnimationAsync(animationTask);
            ShowInstallationFailure($"Installation stopped: {error.Message}");
            return;
        }

        if (!result.Succeeded)
        {
            animationCancellation.Cancel();
            await IgnoreCancelledAnimationAsync(animationTask);
            ShowInstallationFailure(result.Message);
            return;
        }

        await animationTask;
        SetInstallationProgress(100, "Installation verified and complete");
        ProgressText.Text = result.Message;
        ProgressText.Foreground = System.Windows.Media.Brushes.LightGreen;
        await Task.Delay(800);
        HideInstallationProgress();
        await ScanFastAsync(_plan.XPlane.RootPath);
    }

    private void BeginInstallationProgress()
    {
        WorkProgress.Visibility = Visibility.Collapsed;
        InstallProgressPanel.Visibility = Visibility.Visible;
        ProgressText.Text = "Installing XVatsim. Please keep this window open.";
        SetInstallationProgress(0, "Preparing the installation");
    }

    private async Task PlayInstallationProgressAsync(CancellationToken cancellationToken)
    {
        foreach (var step in InstallationVisualSteps)
        {
            if (step.DelayMilliseconds > 0)
                await Task.Delay(step.DelayMilliseconds, cancellationToken);
            cancellationToken.ThrowIfCancellationRequested();
            SetInstallationProgress(step.Percent, step.Message);
        }
    }

    private void SetInstallationProgress(double percent, string message)
    {
        InstallProgressBar.Value = percent;
        InstallProgressPercent.Text = $"{percent:0}%";
        InstallProgressStatus.Text = message;
    }

    private void HideInstallationProgress()
    {
        InstallProgressPanel.Visibility = Visibility.Collapsed;
        SetInstallationProgress(0, "Preparing the installation");
    }

    private void ShowInstallationFailure(string message)
    {
        WorkProgress.Visibility = Visibility.Collapsed;
        InstallProgressPanel.Visibility = Visibility.Visible;
        InstallProgressBar.Foreground = System.Windows.Media.Brushes.OrangeRed;
        InstallProgressStatus.Text = "Installation stopped safely";
        InstallProgressPercent.Text = "Stopped";
        ProgressText.Text = message;
        ProgressText.Foreground = System.Windows.Media.Brushes.OrangeRed;
        SetNavigationEnabled(true);
        PrimaryAction.IsEnabled = true;
    }

    private static async Task IgnoreCancelledAnimationAsync(Task animationTask)
    {
        try
        {
            await animationTask;
        }
        catch (OperationCanceledException)
        {
        }
    }

    private async void ChooseFolder_OnClick(object sender, RoutedEventArgs e)
    {
        if (_discovery is null) return;
        var picker = new OpenFolderDialog { Title = "Choose the X-Plane 12 folder", Multiselect = false };
        if (picker.ShowDialog(this) != true) return;
        SetBusy("Validating the selected X-Plane folder");
        var selected = await _discovery.ValidateSelectedXPlaneAsync(picker.FolderName);
        if (selected is null)
        {
            XPlaneHint.Text = "That folder is not an X-Plane 12 installation. Choose the folder containing X-Plane.exe.";
            ShowPrerequisiteState("That folder could not be verified. Choose the folder containing X-Plane.exe and Resources.");
            return;
        }
        _snapshot ??= new DiscoverySnapshot([], [], [], DateTimeOffset.UtcNow);
        var installs = _snapshot.XPlaneInstallations.Where(x =>
            !string.Equals(x.RootPath, selected.RootPath, StringComparison.OrdinalIgnoreCase)).Append(selected).ToArray();
        _snapshot = _snapshot with { XPlaneInstallations = installs };
        await PopulateDiscoveryAsync(selected.RootPath);
    }

    private async void ChooseXPilotFolder_OnClick(object sender, RoutedEventArgs e)
    {
        if (_discovery is null) return;
        var picker = new OpenFolderDialog { Title = "Choose the xPilot installation folder", Multiselect = false };
        if (picker.ShowDialog(this) != true) return;
        SetBusy("Verifying the selected xPilot folder");
        var selected = await Task.Run(() => _discovery.ValidateSelectedXPilotAsync(picker.FolderName));
        if (selected is null)
        {
            _selectedXPilot = null;
            XPilotFolderText.Text = string.Empty;
            XPilotStatus.Text = "That folder is not an xPilot installation. Choose the folder containing xPilot.exe or its Application folder.";
            ShowPrerequisiteState("The selected xPilot folder could not be verified.");
            return;
        }

        _selectedXPilot = selected;
        _snapshot ??= new DiscoverySnapshot([], [], [], DateTimeOffset.UtcNow);
        var xpilots = _snapshot.XPilotInstallations
            .Where(item => !string.Equals(item.ExecutablePath, selected.ExecutablePath, StringComparison.OrdinalIgnoreCase))
            .Append(selected).ToArray();
        _snapshot = _snapshot with { XPilotInstallations = xpilots };
        RenderXPilotSelection();
        if (XPlaneSelector.SelectedItem is XPlaneInstallation xplane)
            await LoadPlanAsync(xplane);
        else
            ShowPrerequisiteState("Choose your X-Plane 12 folder before installing XVatsim.");
    }

    private void SaveReport_OnClick(object sender, RoutedEventArgs e)
    {
        if (_snapshot is null) return;
        var dialog = new SaveFileDialog
        {
            Title = "Export XVatsim installation diagnostics",
            FileName = $"XVatsim_Installation_Diagnostics_{DateTime.Now:yyyyMMdd_HHmmss}.txt",
            Filter = "Text file (*.txt)|*.txt"
        };
        if (dialog.ShowDialog(this) != true) return;
        File.WriteAllText(dialog.FileName, SupportReport.Create(_snapshot, _plan));
        ProgressText.Text = $"Installation diagnostics exported to {dialog.FileName}";
    }

    private void SetBusy(string message)
    {
        HideInstallationProgress();
        InstallProgressBar.Foreground = (System.Windows.Media.Brush)FindResource("AccentBrush");
        PrimaryAction.IsEnabled = false;
        SetNavigationEnabled(false);
        WorkProgress.Visibility = Visibility.Visible;
        WorkProgress.IsIndeterminate = true;
        ProgressText.Text = message;
        ProgressText.Foreground = (System.Windows.Media.Brush)FindResource("AccentBrush");
    }

    private void ShowPrerequisiteState(string message)
    {
        _plan = null;
        WorkProgress.Visibility = Visibility.Collapsed;
        ActionTitle.Text = "Select the required installation folders";
        ActionSummary.Text = message;
        ProgressText.Text = "Install will become available after both folders are verified.";
        PrimaryAction.Content = "Install";
        PrimaryAction.IsEnabled = false;
        WarningsPanel.Children.Clear();
        AdvancedDetails.Text = string.Empty;
        SetNavigationEnabled(true);
    }

    private void ShowFatal(string message)
    {
        _plan = null;
        WorkProgress.Visibility = Visibility.Collapsed;
        ActionTitle.Text = "Manager cannot continue";
        ActionSummary.Text = message;
        ProgressText.Text = "No files were changed.";
        ProgressText.Foreground = System.Windows.Media.Brushes.OrangeRed;
        PrimaryAction.IsEnabled = false;
        XPlaneSelector.IsEnabled = false;
        ChooseFolderButton.IsEnabled = false;
        ChooseXPilotFolderButton.IsEnabled = false;
    }

    private void SetNavigationEnabled(bool enabled)
    {
        XPlaneSelector.IsEnabled = enabled;
        ChooseFolderButton.IsEnabled = enabled;
        ChooseXPilotFolderButton.IsEnabled = enabled;
    }

    private void RenderXPilotSelection()
    {
        if (_selectedXPilot is null)
        {
            XPilotFolderText.Text = string.Empty;
            XPilotStatus.Text = "xPilot folder not found. Choose folder to locate your xPilot installation.";
            return;
        }

        XPilotFolderText.Text = Path.GetDirectoryName(_selectedXPilot.ExecutablePath) ?? _selectedXPilot.ExecutablePath;
        var version = _selectedXPilot.ProductVersion.Split('+', 2)[0];
        XPilotStatus.Text = _selectedXPilot.Generation switch
        {
            XPilotGeneration.Version4 => $"xPilot {version} found. The companion will verify SDK compatibility when xPilot starts.",
            XPilotGeneration.Version3 => $"xPilot {version} found and supported through the legacy integration.",
            _ => "The selected xPilot version could not be verified."
        };
    }
}
