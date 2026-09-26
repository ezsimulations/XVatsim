using System.Buffers.Binary;
using System.Diagnostics;
using System.IO.Pipes;
using Vatsim.Xpilot.PluginSdk;
using Vatsim.Xpilot.PluginSdk.Events;
using Vatsim.Xpilot.PluginSdk.Exceptions;
using Vatsim.Xpilot.PluginSdk.Models;
using XVatsim.XPilot4Bridge;

static class Probe
{
    private static int _checks;

    public static async Task<int> Main()
    {
        try
        {
            await VerifySupportedReadOnlyFlow();
            await VerifyUnsupportedApiFailsBeforeSubscriptions();
            await VerifyCapacityLossIsExplicit();
            await VerifyHandlerPerformance();
            Console.WriteLine($"XPILOT4_COMPANION_PROBE_PASSED checks={_checks}");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine("XPILOT4_COMPANION_PROBE_FAILED " + exception);
            return 1;
        }
    }

    private static async Task VerifySupportedReadOnlyFlow()
    {
        var pipeName = UniquePipeName("flow");
        var broker = new FakeBroker(new Version(0, 1, 0));
        broker.Controllers.Add(new ControllerInfo("ML_TWR", 120_500_000, -37.6733, 144.8433));
        var plugin = new XVatsimXPilot4Bridge(
            new XPilot4BridgeOptions(pipeName, 512));
        plugin.Initialize(broker);

        await using var client = await Connect(pipeName);
        var hello = await ReadObservation(client);
        Check(hello.Kind == BridgeEventKind.Hello && hello.Compatible,
            "supported hello was not compatible");
        var available = await ReadUntil(client, BridgeEventKind.SourceAvailable);
        Check(available.ApiVersion.StartsWith("0.1", StringComparison.Ordinal),
            "source availability lost API version");

        broker.RaiseConnected("DAL250");
        var connected = await ReadUntil(client, BridgeEventKind.ConnectionStateChanged);
        Check(connected.Connected && connected.Callsign == "DAL250" && connected.ConnectionEpoch > 0,
            "network connection fact changed");

        const string sender = "YMML_DEL";
        var body = "PDC LINE 1\r\nUNICODE Ω ✓  spacing  preserved";
        broker.RaisePrivate(sender, body);
        var message = await ReadUntil(client, BridgeEventKind.IncomingPrivateMessage);
        Check(message.Sender == sender && message.Message == body,
            "private message did not round-trip exactly");

        var longBody = new string('A', BridgeProtocol.MaximumFramePayloadBytes + 8192) + "Ω";
        broker.RaisePrivate("TEST_LONG", longBody);
        var chunked = await ReadUntil(client, BridgeEventKind.IncomingPrivateMessage);
        Check(chunked.Sender == "TEST_LONG" && chunked.Message == longBody,
            "chunked private message did not round-trip exactly");

        broker.RaiseControllerAdded("ML_GND", 121_700_000, -37.67, 144.84);
        var added = await ReadUntil(client, BridgeEventKind.ControllerAdded);
        Check(added.Controller is { Callsign: "ML_GND", FrequencyHz: 121_700_000 },
            "controller add fact changed");
        broker.RaiseControllerFrequency("ML_GND", 121_800_000);
        var frequency = await ReadUntil(client, BridgeEventKind.ControllerFrequencyChanged);
        Check(frequency.Controller is { Callsign: "ML_GND", FrequencyHz: 121_800_000, HasLocation: false },
            "controller frequency fact changed");
        broker.RaiseControllerLocation("ML_GND", 0, 0);
        var location = await ReadUntil(client, BridgeEventKind.ControllerLocationChanged);
        Check(location.Controller is { Callsign: "ML_GND", Latitude: 0, Longitude: 0, HasFrequency: false },
            "zero controller location was filtered");
        broker.RaiseControllerDeleted("ML_GND");
        var deleted = await ReadUntil(client, BridgeEventKind.ControllerDeleted);
        Check(deleted.Controller?.Callsign == "ML_GND", "controller delete fact changed");

        const ulong snapshotRequest = 77;
        var request = BridgeProtocol.EncodeSnapshotRequest(snapshotRequest);
        await client.WriteAsync(request);
        await client.FlushAsync();
        var started = await ReadUntil(client, BridgeEventKind.ControllerSnapshotStarted);
        var entry = await ReadUntil(client, BridgeEventKind.ControllerSnapshotEntry);
        var completed = await ReadUntil(client, BridgeEventKind.ControllerSnapshotCompleted);
        Check(started.SnapshotRequestId == snapshotRequest && started.SnapshotEntryCount == 1 &&
              entry.Controller?.Callsign == "ML_TWR" &&
              completed.SnapshotRequestId == snapshotRequest && completed.SnapshotEntryCount == 1,
            "controller snapshot was incomplete or filtered");

        broker.RaiseSessionEnded();
        var ended = await ReadUntil(client, BridgeEventKind.SessionEnded);
        Check(ended.Kind == BridgeEventKind.SessionEnded && broker.SubscriberCount == 0,
            "session cleanup did not unsubscribe handlers");
        await plugin.StopForTestingAsync();
        Check(broker.ForbiddenActionCalls == 0, "companion invoked a write/control API");
    }

    private static async Task VerifyUnsupportedApiFailsBeforeSubscriptions()
    {
        var pipeName = UniquePipeName("unsupported");
        var broker = new FakeBroker(new Version(0, 2, 0));
        var plugin = new XVatsimXPilot4Bridge(
            new XPilot4BridgeOptions(pipeName, 16));
        plugin.Initialize(broker);
        await using var client = await Connect(pipeName);
        var hello = await ReadObservation(client);
        var unavailable = await ReadUntil(client, BridgeEventKind.SourceUnavailable);
        Check(!hello.Compatible && broker.SubscriberCount == 0 &&
              unavailable.Reason == "unsupported-xpilot-plugin-sdk-api",
            "unsupported API subscribed or appeared available");
        await plugin.StopForTestingAsync();
    }

    private static async Task VerifyCapacityLossIsExplicit()
    {
        var pipeName = UniquePipeName("capacity");
        var broker = new FakeBroker(new Version(0, 1, 0));
        var plugin = new XVatsimXPilot4Bridge(
            new XPilot4BridgeOptions(pipeName, 4));
        plugin.Initialize(broker);
        broker.RaiseConnected("CAP1");
        for (var index = 0; index < 50; ++index)
        {
            broker.RaisePrivate("CAP_TEST", $"MESSAGE {index}");
        }
        await using var client = await Connect(pipeName);
        _ = await ReadObservation(client); // hello
        var loss = await ReadUntil(client, BridgeEventKind.CapacityLoss, 100);
        Check(loss.LostCount > 0 && loss.FirstLostSequence > 0 &&
              loss.LastLostSequence >= loss.FirstLostSequence &&
              plugin.QueueSnapshot.Capacity == 4,
            "capacity loss was silent or unbounded");
        await plugin.StopForTestingAsync();
    }

    private static async Task VerifyHandlerPerformance()
    {
        var pipeName = UniquePipeName("performance");
        var broker = new FakeBroker(new Version(0, 1, 0));
        var plugin = new XVatsimXPilot4Bridge(
            new XPilot4BridgeOptions(pipeName, 512));
        plugin.Initialize(broker);
        var samples = new long[20_000];
        var burst = Stopwatch.StartNew();
        for (var index = 0; index < samples.Length; ++index)
        {
            var started = Stopwatch.GetTimestamp();
            broker.RaisePrivate("PERF", "BOUNDED");
            samples[index] = (long)(Stopwatch.GetElapsedTime(started).TotalMicroseconds);
        }
        burst.Stop();
        Array.Sort(samples);
        var p99 = samples[(samples.Length * 99) / 100];
        var maximum = samples[^1];
        Check(burst.Elapsed <= TimeSpan.FromSeconds(2),
            $"20,000-event burst exceeded 2 seconds: {burst.Elapsed}");
        Check(p99 <= 500, $"event-handler p99 exceeded 500us: {p99}us");
        Check(maximum <= 5_000, $"event-handler maximum exceeded 5ms: {maximum}us");
        Check(plugin.QueueSnapshot.Pending <= 512 && plugin.QueueSnapshot.Lost > 0,
            "performance burst did not remain mechanically bounded");
        Console.WriteLine(
            $"XPILOT4_COMPANION_PERF burstUs={(long)burst.Elapsed.TotalMicroseconds} p99Us={p99} maxUs={maximum} " +
            $"queueHigh={plugin.QueueSnapshot.HighWaterMark} lost={plugin.QueueSnapshot.Lost}");
        await plugin.StopForTestingAsync();
    }

    private static async Task<NamedPipeClientStream> Connect(string pipeName)
    {
        var client = new NamedPipeClientStream(
            ".", pipeName, PipeDirection.InOut, PipeOptions.Asynchronous);
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        await client.ConnectAsync(timeout.Token);
        return client;
    }

    private static async Task<BridgeObservation> ReadUntil(
        Stream stream,
        BridgeEventKind expected,
        int maximumFrames = 32)
    {
        for (var index = 0; index < maximumFrames; ++index)
        {
            var value = await ReadObservation(stream);
            if (value.Kind == expected)
            {
                return value;
            }
        }
        throw new InvalidDataException($"Did not receive expected event {expected}.");
    }

    private static async Task<BridgeObservation> ReadObservation(Stream stream)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        var headerBytes = new byte[BridgeProtocol.HeaderBytes];
        await stream.ReadExactlyAsync(headerBytes, timeout.Token);
        var header = BridgeProtocol.DecodeHeader(headerBytes);
        var payload = new byte[checked((int)header.PayloadLength)];
        await stream.ReadExactlyAsync(payload, timeout.Token);
        if (header.Kind != BridgeEventKind.Chunk)
        {
            return BridgeProtocol.DecodeObservationForProbe(header, payload);
        }

        if (payload.Length < 14)
        {
            throw new InvalidDataException("Short chunk frame.");
        }
        var originalKind = (BridgeEventKind)BinaryPrimitives.ReadUInt16LittleEndian(payload.AsSpan(0, 2));
        var index = BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(2, 4));
        var count = BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(6, 4));
        var total = BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(10, 4));
        Check(index == 0 && count > 1 && total <= BridgeProtocol.MaximumObservationBytes,
            "invalid first chunk metadata");
        using var combined = new MemoryStream(checked((int)total));
        combined.Write(payload, 14, payload.Length - 14);
        for (uint next = 1; next < count; ++next)
        {
            await stream.ReadExactlyAsync(headerBytes, timeout.Token);
            var nextHeader = BridgeProtocol.DecodeHeader(headerBytes);
            var nextPayload = new byte[checked((int)nextHeader.PayloadLength)];
            await stream.ReadExactlyAsync(nextPayload, timeout.Token);
            Check(nextHeader.Kind == BridgeEventKind.Chunk &&
                  nextHeader.ProcessEpoch == header.ProcessEpoch &&
                  nextHeader.ConnectionEpoch == header.ConnectionEpoch &&
                  nextHeader.Sequence == header.Sequence &&
                  nextPayload.Length >= 14 &&
                  BinaryPrimitives.ReadUInt16LittleEndian(nextPayload.AsSpan(0, 2)) == (ushort)originalKind &&
                  BinaryPrimitives.ReadUInt32LittleEndian(nextPayload.AsSpan(2, 4)) == next &&
                  BinaryPrimitives.ReadUInt32LittleEndian(nextPayload.AsSpan(6, 4)) == count &&
                  BinaryPrimitives.ReadUInt32LittleEndian(nextPayload.AsSpan(10, 4)) == total,
                "chunk sequence changed");
            combined.Write(nextPayload, 14, nextPayload.Length - 14);
        }
        Check(combined.Length == total, "reassembled payload length changed");
        var originalHeader = new BridgeFrameHeader(
            originalKind,
            total,
            header.ProcessEpoch,
            header.ConnectionEpoch,
            header.Sequence,
            header.ReceivedUnixMicroseconds);
        return BridgeProtocol.DecodeObservationForProbe(originalHeader, combined.ToArray());
    }

    private static string UniquePipeName(string purpose) =>
        $"XVatsim.XPilot4Bridge.Probe.{purpose}.{Environment.ProcessId}.{Guid.NewGuid():N}";

    private static void Check(bool condition, string message)
    {
        ++_checks;
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}

#pragma warning disable CS0067
sealed class FakeBroker : IBroker
{
    private EventHandler? _sessionEnded;
    private EventHandler<NetworkConnectedEventArgs>? _networkConnected;
    private EventHandler? _networkDisconnected;
    private EventHandler<PrivateMessageReceivedEventArgs>? _privateMessageReceived;
    private EventHandler<ControllerAddedEventArgs>? _controllerAdded;
    private EventHandler<ControllerDeletedEventArgs>? _controllerDeleted;
    private EventHandler<ControllerFrequencyChangedEventArgs>? _controllerFrequencyChanged;
    private EventHandler<ControllerLocationChangedEventArgs>? _controllerLocationChanged;

    internal FakeBroker(Version apiVersion) => ApiVersion = apiVersion;

    public Version ApiVersion { get; }
    public bool IsConnected { get; private set; }
    public string? Callsign { get; private set; }
    internal List<ControllerInfo> Controllers { get; } = [];
    internal int ForbiddenActionCalls { get; private set; }
    internal int SubscriberCount =>
        Count(_sessionEnded) + Count(_networkConnected) + Count(_networkDisconnected) +
        Count(_privateMessageReceived) + Count(_controllerAdded) + Count(_controllerDeleted) +
        Count(_controllerFrequencyChanged) + Count(_controllerLocationChanged);

    public event EventHandler? SessionEnded { add => _sessionEnded += value; remove => _sessionEnded -= value; }
    public event EventHandler<NetworkConnectedEventArgs>? NetworkConnected { add => _networkConnected += value; remove => _networkConnected -= value; }
    public event EventHandler? NetworkDisconnected { add => _networkDisconnected += value; remove => _networkDisconnected -= value; }
    public event EventHandler<PrivateMessageReceivedEventArgs>? PrivateMessageReceived { add => _privateMessageReceived += value; remove => _privateMessageReceived -= value; }
    public event EventHandler<ControllerAddedEventArgs>? ControllerAdded { add => _controllerAdded += value; remove => _controllerAdded -= value; }
    public event EventHandler<ControllerDeletedEventArgs>? ControllerDeleted { add => _controllerDeleted += value; remove => _controllerDeleted -= value; }
    public event EventHandler<ControllerFrequencyChangedEventArgs>? ControllerFrequencyChanged { add => _controllerFrequencyChanged += value; remove => _controllerFrequencyChanged -= value; }
    public event EventHandler<ControllerLocationChangedEventArgs>? ControllerLocationChanged { add => _controllerLocationChanged += value; remove => _controllerLocationChanged -= value; }

    public event EventHandler<ServerMessageReceivedEventArgs>? ServerMessageReceived;
    public event EventHandler<RadioMessageReceivedEventArgs>? RadioMessageReceived;
    public event EventHandler<BroadcastMessageReceivedEventArgs>? BroadcastMessageReceived;
    public event EventHandler<RadioMessageSentEventArgs>? RadioMessageSent;
    public event EventHandler<PrivateMessageSentEventArgs>? PrivateMessageSent;
    public event EventHandler<MetarReceivedEventArgs>? MetarReceived;
    public event EventHandler<AtisReceivedEventArgs>? AtisReceived;
    public event EventHandler<SelcalAlertReceivedEventArgs>? SelcalAlertReceived;
    public event EventHandler<AircraftAddedEventArgs>? AircraftAdded;
    public event EventHandler<AircraftUpdatedEventArgs>? AircraftUpdated;
    public event EventHandler<AircraftDeletedEventArgs>? AircraftDeleted;

    public IReadOnlyList<ControllerInfo> GetControllers() => Controllers.ToArray();

    internal void RaiseConnected(string callsign)
    {
        IsConnected = true;
        Callsign = callsign;
        _networkConnected?.Invoke(this, new NetworkConnectedEventArgs("123", callsign, "B738", string.Empty, false));
    }

    internal void RaisePrivate(string from, string message) =>
        _privateMessageReceived?.Invoke(this, new PrivateMessageReceivedEventArgs(from, message));

    internal void RaiseControllerAdded(string callsign, int frequency, double latitude, double longitude) =>
        _controllerAdded?.Invoke(this, new ControllerAddedEventArgs(callsign, frequency, latitude, longitude));

    internal void RaiseControllerDeleted(string callsign) =>
        _controllerDeleted?.Invoke(this, new ControllerDeletedEventArgs(callsign));

    internal void RaiseControllerFrequency(string callsign, int frequency) =>
        _controllerFrequencyChanged?.Invoke(this, new ControllerFrequencyChangedEventArgs(callsign, frequency));

    internal void RaiseControllerLocation(string callsign, double latitude, double longitude) =>
        _controllerLocationChanged?.Invoke(this, new ControllerLocationChangedEventArgs(callsign, latitude, longitude));

    internal void RaiseSessionEnded() => _sessionEnded?.Invoke(this, EventArgs.Empty);

    private static int Count(Delegate? value) => value?.GetInvocationList().Length ?? 0;
    private void Forbidden() { ++ForbiddenActionCalls; throw new InvalidOperationException("write/control API invoked"); }

    public void RequestConnect(string callsign, string typeCode, string selcalCode) => Forbidden();
    public void RequestConnectAsObserver(string callsign) => Forbidden();
    public void RequestConnectAsTowerView() => Forbidden();
    public void RequestDisconnect() => Forbidden();
    public void RequestMetar(string station) => Forbidden();
    public void RequestAtis(string callsign) => Forbidden();
    public void SendPrivateMessage(string to, string message) => Forbidden();
    public void SendRadioMessage(string message) => Forbidden();
    public void PostDebugMessage(string message) => Forbidden();
    public void SetModeC(bool modeC) => Forbidden();
    public void SquawkIdent() => Forbidden();
    public void SetPtt(bool pressed) => Forbidden();
}
#pragma warning restore CS0067
