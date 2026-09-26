using System.Reflection;
using Vatsim.Xpilot.PluginSdk;
using Vatsim.Xpilot.PluginSdk.Events;

namespace XVatsim.XPilot4Bridge;

internal sealed class XPilot4BridgeOptions
{
    internal XPilot4BridgeOptions(
        string pipeName = "XVatsim.XPilot4Bridge.v1",
        int queueCapacity = 512)
    {
        PipeName = pipeName;
        QueueCapacity = queueCapacity;
    }

    internal string PipeName { get; }
    internal int QueueCapacity { get; }
}

/// <summary>
/// Read-only xPilot 4 companion. It forwards exact SDK observations to XVatsim;
/// it does not classify, score, filter, transmit, or change client state.
/// </summary>
public sealed class XVatsimXPilot4Bridge : IPlugin
{
    private static readonly Version MinimumApiVersion = new(0, 1, 0);
    private static readonly Version MaximumExclusiveApiVersion = new(0, 2, 0);
    private readonly object _stateGate = new();
    private readonly XPilot4BridgeOptions _options;
    private readonly ulong _processEpoch;
    private BridgeTransport? _transport;
    private IBroker? _broker;
    private Version _apiVersion = new(0, 0);
    private long _sequence;
    private long _connectionEpoch;
    private int _initialized;
    private int _subscribed;
    private bool _compatible;
    private string _callsign = string.Empty;

    /// <summary>Creates the companion with its production local-pipe settings.</summary>
    public XVatsimXPilot4Bridge()
        : this(new XPilot4BridgeOptions())
    {
    }

    internal XVatsimXPilot4Bridge(XPilot4BridgeOptions options)
    {
        _options = options;
        _processEpoch = unchecked(
            ((ulong)DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() << 16) ^
            (uint)Environment.ProcessId);
    }

    /// <inheritdoc />
    public string Name => "XVatsim xPilot 4 Bridge 0.1.0 Experimental";

    internal ObservationQueueSnapshot QueueSnapshot =>
        _transport?.QueueSnapshot ?? default;

    /// <inheritdoc />
    public void Initialize(IBroker broker)
    {
        ArgumentNullException.ThrowIfNull(broker);
        if (Interlocked.Exchange(ref _initialized, 1) != 0)
        {
            return;
        }

        // This is intentionally the first SDK member used. Unsupported hosts
        // receive only a local compatibility status and no event subscriptions.
        _apiVersion = broker.ApiVersion;
        _compatible = _apiVersion >= MinimumApiVersion &&
                      _apiVersion < MaximumExclusiveApiVersion;
        _broker = _compatible ? broker : null;
        _transport = new BridgeTransport(
            new BridgeTransportOptions(_options.PipeName, _options.QueueCapacity),
            NextSequence,
            BuildHello,
            CaptureControllerSnapshot,
            reason =>
            {
                var observation = NewObservation(BridgeEventKind.CorruptEnvelope);
                observation.Reason = reason;
                return observation;
            });
        _transport.Start();

        if (!_compatible)
        {
            var unavailable = NewObservation(BridgeEventKind.SourceUnavailable);
            unavailable.ApiVersion = _apiVersion.ToString();
            unavailable.Reason = "unsupported-xpilot-plugin-sdk-api";
            Capture(unavailable);
            return;
        }

        Subscribe(broker);
        var available = NewObservation(BridgeEventKind.SourceAvailable);
        available.ApiVersion = _apiVersion.ToString();
        available.Reason = "supported-read-only-sdk";
        Capture(available);

        if (broker.IsConnected)
        {
            CaptureConnected(broker.Callsign ?? string.Empty);
        }
    }

    internal Task StopForTestingAsync()
    {
        Unsubscribe();
        return _transport?.StopForTestingAsync() ?? Task.CompletedTask;
    }

    private void Subscribe(IBroker broker)
    {
        broker.SessionEnded += OnSessionEnded;
        broker.NetworkConnected += OnNetworkConnected;
        broker.NetworkDisconnected += OnNetworkDisconnected;
        broker.PrivateMessageReceived += OnPrivateMessageReceived;
        broker.ControllerAdded += OnControllerAdded;
        broker.ControllerDeleted += OnControllerDeleted;
        broker.ControllerFrequencyChanged += OnControllerFrequencyChanged;
        broker.ControllerLocationChanged += OnControllerLocationChanged;
        Interlocked.Exchange(ref _subscribed, 1);
    }

    private void Unsubscribe()
    {
        if (Interlocked.Exchange(ref _subscribed, 0) == 0 || _broker is null)
        {
            return;
        }
        _broker.SessionEnded -= OnSessionEnded;
        _broker.NetworkConnected -= OnNetworkConnected;
        _broker.NetworkDisconnected -= OnNetworkDisconnected;
        _broker.PrivateMessageReceived -= OnPrivateMessageReceived;
        _broker.ControllerAdded -= OnControllerAdded;
        _broker.ControllerDeleted -= OnControllerDeleted;
        _broker.ControllerFrequencyChanged -= OnControllerFrequencyChanged;
        _broker.ControllerLocationChanged -= OnControllerLocationChanged;
    }

    private void OnSessionEnded(object? sender, EventArgs args)
    {
        Capture(NewObservation(BridgeEventKind.SessionEnded));
        Unsubscribe();
        _transport?.RequestGracefulStop();
    }

    private void OnNetworkConnected(object? sender, NetworkConnectedEventArgs args) =>
        CaptureConnected(args.Callsign);

    private void CaptureConnected(string callsign)
    {
        var epoch = checked((ulong)Interlocked.Increment(ref _connectionEpoch));
        lock (_stateGate)
        {
            _callsign = callsign;
        }
        var observation = NewObservation(BridgeEventKind.ConnectionStateChanged, epoch);
        observation.Connected = true;
        observation.Callsign = callsign;
        Capture(observation);
    }

    private void OnNetworkDisconnected(object? sender, EventArgs args)
    {
        string callsign;
        lock (_stateGate)
        {
            callsign = _callsign;
            _callsign = string.Empty;
        }
        var observation = NewObservation(BridgeEventKind.ConnectionStateChanged);
        observation.Connected = false;
        observation.Callsign = callsign;
        Capture(observation);
    }

    private void OnPrivateMessageReceived(
        object? sender,
        PrivateMessageReceivedEventArgs args)
    {
        var observation = NewObservation(BridgeEventKind.IncomingPrivateMessage);
        observation.Sender = args.From;
        observation.Message = args.Message;
        Capture(observation);
    }

    private void OnControllerAdded(object? sender, ControllerAddedEventArgs args)
    {
        var observation = NewObservation(BridgeEventKind.ControllerAdded);
        observation.Controller = new BridgeController(
            args.Callsign,
            args.Frequency,
            args.Latitude,
            args.Longitude);
        Capture(observation);
    }

    private void OnControllerDeleted(object? sender, ControllerDeletedEventArgs args)
    {
        var observation = NewObservation(BridgeEventKind.ControllerDeleted);
        observation.Controller = new BridgeController(
            args.Callsign,
            0,
            0,
            0,
            false,
            false);
        Capture(observation);
    }

    private void OnControllerFrequencyChanged(
        object? sender,
        ControllerFrequencyChangedEventArgs args)
    {
        var observation = NewObservation(BridgeEventKind.ControllerFrequencyChanged);
        observation.Controller = new BridgeController(
            args.Callsign,
            args.NewFrequency,
            0,
            0,
            true,
            false);
        Capture(observation);
    }

    private void OnControllerLocationChanged(
        object? sender,
        ControllerLocationChangedEventArgs args)
    {
        var observation = NewObservation(BridgeEventKind.ControllerLocationChanged);
        observation.Controller = new BridgeController(
            args.Callsign,
            0,
            args.NewLatitude,
            args.NewLongitude,
            false,
            true);
        Capture(observation);
    }

    private void CaptureControllerSnapshot(ulong requestId)
    {
        var broker = _broker;
        if (!_compatible || broker is null)
        {
            var unavailable = NewObservation(BridgeEventKind.CorruptEnvelope);
            unavailable.Reason = "controller-snapshot-request-without-supported-sdk";
            Capture(unavailable);
            return;
        }

        try
        {
            var controllers = broker.GetControllers();
            var count = checked((uint)controllers.Count);
            var started = NewObservation(BridgeEventKind.ControllerSnapshotStarted);
            started.SnapshotRequestId = requestId;
            started.SnapshotEntryCount = count;
            Capture(started);
            foreach (var controller in controllers)
            {
                var entry = NewObservation(BridgeEventKind.ControllerSnapshotEntry);
                entry.SnapshotRequestId = requestId;
                entry.Controller = new BridgeController(
                    controller.Callsign,
                    controller.Frequency,
                    controller.Latitude,
                    controller.Longitude);
                Capture(entry);
            }
            var completed = NewObservation(BridgeEventKind.ControllerSnapshotCompleted);
            completed.SnapshotRequestId = requestId;
            completed.SnapshotEntryCount = count;
            Capture(completed);
        }
        catch (Exception exception)
        {
            var failure = NewObservation(BridgeEventKind.CorruptEnvelope);
            failure.Reason = "controller-snapshot-read-failed:" + exception.GetType().Name;
            Capture(failure);
        }
    }

    private BridgeObservation BuildHello() => new()
    {
        Kind = BridgeEventKind.Hello,
        ProcessEpoch = _processEpoch,
        ConnectionEpoch = CurrentConnectionEpoch(),
        Sequence = 0,
        ReceivedUnixMicroseconds = BridgeProtocol.CurrentUnixMicroseconds(),
        Compatible = _compatible,
        CompanionVersion = Assembly.GetExecutingAssembly().GetName().Version?.ToString() ?? "0.1.0",
        ApiVersion = _apiVersion.ToString(),
        SourceKind = "xpilot4_plugin_sdk",
        SourceFamily = "xpilot_fsd",
        Reason = _compatible ? "supported-read-only-sdk" : "unsupported-xpilot-plugin-sdk-api",
    };

    private BridgeObservation NewObservation(
        BridgeEventKind kind,
        ulong? connectionEpoch = null) => new()
    {
        Kind = kind,
        ProcessEpoch = _processEpoch,
        ConnectionEpoch = connectionEpoch ?? CurrentConnectionEpoch(),
        Sequence = NextSequence(),
        ReceivedUnixMicroseconds = BridgeProtocol.CurrentUnixMicroseconds(),
    };

    private ulong CurrentConnectionEpoch() =>
        unchecked((ulong)Math.Max(0, Interlocked.Read(ref _connectionEpoch)));

    private ulong NextSequence() =>
        checked((ulong)Interlocked.Increment(ref _sequence));

    private void Capture(BridgeObservation observation)
    {
        _transport?.Enqueue(observation);
    }
}
