namespace XVatsim.XPilot4Bridge;

internal enum BridgeEventKind : ushort
{
    Hello = 1,
    SourceAvailable = 2,
    SourceUnavailable = 3,
    ConnectionStateChanged = 4,
    IncomingPrivateMessage = 5,
    ControllerSnapshotStarted = 6,
    ControllerSnapshotEntry = 7,
    ControllerSnapshotCompleted = 8,
    ControllerAdded = 9,
    ControllerDeleted = 10,
    ControllerFrequencyChanged = 11,
    ControllerLocationChanged = 12,
    CapacityLoss = 13,
    CorruptEnvelope = 14,
    SessionEnded = 15,
    ControllerSnapshotRequest = 100,
    Chunk = 200,
}

internal sealed class BridgeController
{
    internal BridgeController(
        string callsign,
        int frequencyHz,
        double latitude,
        double longitude,
        bool hasFrequency = true,
        bool hasLocation = true)
    {
        Callsign = callsign;
        FrequencyHz = frequencyHz;
        Latitude = latitude;
        Longitude = longitude;
        HasFrequency = hasFrequency;
        HasLocation = hasLocation;
    }

    public string Callsign { get; }
    public int FrequencyHz { get; }
    public double Latitude { get; }
    public double Longitude { get; }
    public bool HasFrequency { get; }
    public bool HasLocation { get; }
}

internal sealed class BridgeObservation
{
    public BridgeEventKind Kind { get; set; }
    public ulong ProcessEpoch { get; set; }
    public ulong ConnectionEpoch { get; set; }
    public ulong Sequence { get; set; }
    public long ReceivedUnixMicroseconds { get; set; }
    public bool Compatible { get; set; }
    public bool Connected { get; set; }
    public string CompanionVersion { get; set; } = string.Empty;
    public string ApiVersion { get; set; } = string.Empty;
    public string SourceKind { get; set; } = "xpilot4_plugin_sdk";
    public string SourceFamily { get; set; } = "xpilot_fsd";
    public string Reason { get; set; } = string.Empty;
    public string Callsign { get; set; } = string.Empty;
    public string Sender { get; set; } = string.Empty;
    public string Message { get; set; } = string.Empty;
    public BridgeController? Controller { get; set; }
    public ulong SnapshotRequestId { get; set; }
    public uint SnapshotEntryCount { get; set; }
    public ulong LostCount { get; set; }
    public ulong FirstLostSequence { get; set; }
    public ulong LastLostSequence { get; set; }
}

internal readonly struct BridgeFrameHeader
{
    internal BridgeFrameHeader(
        BridgeEventKind kind,
        uint payloadLength,
        ulong processEpoch,
        ulong connectionEpoch,
        ulong sequence,
        long receivedUnixMicroseconds)
    {
        Kind = kind;
        PayloadLength = payloadLength;
        ProcessEpoch = processEpoch;
        ConnectionEpoch = connectionEpoch;
        Sequence = sequence;
        ReceivedUnixMicroseconds = receivedUnixMicroseconds;
    }

    public BridgeEventKind Kind { get; }
    public uint PayloadLength { get; }
    public ulong ProcessEpoch { get; }
    public ulong ConnectionEpoch { get; }
    public ulong Sequence { get; }
    public long ReceivedUnixMicroseconds { get; }
}

internal sealed class ObservationTooLargeException : Exception
{
    public ObservationTooLargeException(int size)
        : base($"Observation payload exceeds the mechanical limit: {size} bytes.")
    {
    }
}

internal static class BridgeProtocol
{
    internal const uint Magic = 0x34425658U; // "XVB4" in little-endian order.
    internal const ushort Version = 1;
    internal const int HeaderBytes = 44;
    internal const int MaximumFramePayloadBytes = 256 * 1024;
    internal const int MaximumObservationBytes = 4 * 1024 * 1024;
    private const int ChunkMetadataBytes = 14;

    internal static IReadOnlyList<byte[]> EncodeObservation(BridgeObservation observation)
    {
        var payload = EncodePayload(observation);
        if (payload.Length > MaximumObservationBytes)
        {
            throw new ObservationTooLargeException(payload.Length);
        }
        if (payload.Length <= MaximumFramePayloadBytes)
        {
            return new byte[][] { EncodeFrame(observation, observation.Kind, payload) };
        }

        var contentPerChunk = MaximumFramePayloadBytes - ChunkMetadataBytes;
        var chunkCount = checked((uint)((payload.Length + contentPerChunk - 1) / contentPerChunk));
        var frames = new List<byte[]>(checked((int)chunkCount));
        for (uint index = 0; index < chunkCount; ++index)
        {
            var offset = checked((int)index * contentPerChunk);
            var count = Math.Min(contentPerChunk, payload.Length - offset);
            var chunk = new byte[ChunkMetadataBytes + count];
            WriteUInt16(chunk, 0, (ushort)observation.Kind);
            WriteUInt32(chunk, 2, index);
            WriteUInt32(chunk, 6, chunkCount);
            WriteUInt32(chunk, 10, checked((uint)payload.Length));
            Array.Copy(payload, offset, chunk, ChunkMetadataBytes, count);
            frames.Add(EncodeFrame(observation, BridgeEventKind.Chunk, chunk));
        }
        return frames;
    }

    internal static byte[] EncodeSnapshotRequest(ulong requestId)
    {
        var observation = new BridgeObservation
        {
            Kind = BridgeEventKind.ControllerSnapshotRequest,
            Sequence = requestId,
            SnapshotRequestId = requestId,
            ReceivedUnixMicroseconds = CurrentUnixMicroseconds(),
        };
        return EncodeFrame(
            observation,
            observation.Kind,
            EncodePayload(observation));
    }

    internal static BridgeFrameHeader DecodeHeader(byte[] bytes)
    {
        if (bytes.Length != HeaderBytes ||
            ReadUInt32(bytes, 0) != Magic ||
            ReadUInt16(bytes, 4) != Version)
        {
            throw new InvalidDataException("Invalid xPilot 4 bridge frame header.");
        }
        return new BridgeFrameHeader(
            (BridgeEventKind)ReadUInt16(bytes, 6),
            ReadUInt32(bytes, 8),
            ReadUInt64(bytes, 12),
            ReadUInt64(bytes, 20),
            ReadUInt64(bytes, 28),
            unchecked((long)ReadUInt64(bytes, 36)));
    }

    internal static ulong DecodeSnapshotRequest(
        BridgeFrameHeader header,
        byte[] payload)
    {
        if (header.Kind != BridgeEventKind.ControllerSnapshotRequest || payload.Length != 8)
        {
            throw new InvalidDataException("Invalid controller snapshot request.");
        }
        return ReadUInt64(payload, 0);
    }

    internal static BridgeObservation DecodeObservationForProbe(
        BridgeFrameHeader header,
        byte[] payload)
    {
        if (header.Kind == BridgeEventKind.Chunk)
        {
            throw new InvalidDataException("Probe decoder requires reassembled payload.");
        }
        var reader = new PayloadReader(payload);
        var value = new BridgeObservation
        {
            Kind = header.Kind,
            ProcessEpoch = header.ProcessEpoch,
            ConnectionEpoch = header.ConnectionEpoch,
            Sequence = header.Sequence,
            ReceivedUnixMicroseconds = header.ReceivedUnixMicroseconds,
        };
        switch (header.Kind)
        {
            case BridgeEventKind.Hello:
                value.Compatible = reader.ReadBoolean();
                value.CompanionVersion = reader.ReadString();
                value.ApiVersion = reader.ReadString();
                value.SourceKind = reader.ReadString();
                value.SourceFamily = reader.ReadString();
                value.Reason = reader.ReadString();
                break;
            case BridgeEventKind.SourceAvailable:
            case BridgeEventKind.SourceUnavailable:
                value.ApiVersion = reader.ReadString();
                value.Reason = reader.ReadString();
                break;
            case BridgeEventKind.ConnectionStateChanged:
                value.Connected = reader.ReadBoolean();
                value.Callsign = reader.ReadString();
                break;
            case BridgeEventKind.IncomingPrivateMessage:
                value.Sender = reader.ReadString();
                value.Message = reader.ReadString();
                break;
            case BridgeEventKind.ControllerSnapshotStarted:
            case BridgeEventKind.ControllerSnapshotCompleted:
                value.SnapshotRequestId = reader.ReadUInt64();
                value.SnapshotEntryCount = reader.ReadUInt32();
                break;
            case BridgeEventKind.ControllerSnapshotEntry:
            case BridgeEventKind.ControllerAdded:
                value.SnapshotRequestId = reader.ReadUInt64();
                value.Controller = reader.ReadController(true, true);
                break;
            case BridgeEventKind.ControllerDeleted:
                value.Controller = new BridgeController(reader.ReadString(), 0, 0, 0, false, false);
                break;
            case BridgeEventKind.ControllerFrequencyChanged:
                value.Controller = new BridgeController(reader.ReadString(), reader.ReadInt32(), 0, 0, true, false);
                break;
            case BridgeEventKind.ControllerLocationChanged:
                value.Controller = new BridgeController(reader.ReadString(), 0, reader.ReadDouble(), reader.ReadDouble(), false, true);
                break;
            case BridgeEventKind.CapacityLoss:
                value.LostCount = reader.ReadUInt64();
                value.FirstLostSequence = reader.ReadUInt64();
                value.LastLostSequence = reader.ReadUInt64();
                value.Reason = reader.ReadString();
                break;
            case BridgeEventKind.CorruptEnvelope:
                value.Reason = reader.ReadString();
                break;
            case BridgeEventKind.SessionEnded:
                break;
            default:
                throw new InvalidDataException($"Unsupported event kind {header.Kind}.");
        }
        reader.RequireComplete();
        return value;
    }

    internal static long CurrentUnixMicroseconds() =>
        DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() * 1000;

    private static byte[] EncodeFrame(
        BridgeObservation observation,
        BridgeEventKind kind,
        byte[] payload)
    {
        var frame = new byte[HeaderBytes + payload.Length];
        WriteUInt32(frame, 0, Magic);
        WriteUInt16(frame, 4, Version);
        WriteUInt16(frame, 6, (ushort)kind);
        WriteUInt32(frame, 8, checked((uint)payload.Length));
        WriteUInt64(frame, 12, observation.ProcessEpoch);
        WriteUInt64(frame, 20, observation.ConnectionEpoch);
        WriteUInt64(frame, 28, observation.Sequence);
        WriteUInt64(frame, 36, unchecked((ulong)observation.ReceivedUnixMicroseconds));
        Array.Copy(payload, 0, frame, HeaderBytes, payload.Length);
        return frame;
    }

    private static byte[] EncodePayload(BridgeObservation observation)
    {
        var writer = new PayloadWriter();
        switch (observation.Kind)
        {
            case BridgeEventKind.Hello:
                writer.Write(observation.Compatible);
                WriteString(writer, observation.CompanionVersion);
                WriteString(writer, observation.ApiVersion);
                WriteString(writer, observation.SourceKind);
                WriteString(writer, observation.SourceFamily);
                WriteString(writer, observation.Reason);
                break;
            case BridgeEventKind.SourceAvailable:
            case BridgeEventKind.SourceUnavailable:
                WriteString(writer, observation.ApiVersion);
                WriteString(writer, observation.Reason);
                break;
            case BridgeEventKind.ConnectionStateChanged:
                writer.Write(observation.Connected);
                WriteString(writer, observation.Callsign);
                break;
            case BridgeEventKind.IncomingPrivateMessage:
                WriteString(writer, observation.Sender);
                WriteString(writer, observation.Message);
                break;
            case BridgeEventKind.ControllerSnapshotStarted:
            case BridgeEventKind.ControllerSnapshotCompleted:
                writer.Write(observation.SnapshotRequestId);
                writer.Write(observation.SnapshotEntryCount);
                break;
            case BridgeEventKind.ControllerSnapshotEntry:
            case BridgeEventKind.ControllerAdded:
                writer.Write(observation.SnapshotRequestId);
                WriteController(writer, observation.Controller);
                break;
            case BridgeEventKind.ControllerDeleted:
                WriteString(writer, observation.Controller?.Callsign ?? string.Empty);
                break;
            case BridgeEventKind.ControllerFrequencyChanged:
                WriteString(writer, observation.Controller?.Callsign ?? string.Empty);
                writer.Write(observation.Controller?.FrequencyHz ?? 0);
                break;
            case BridgeEventKind.ControllerLocationChanged:
                WriteString(writer, observation.Controller?.Callsign ?? string.Empty);
                writer.Write(observation.Controller?.Latitude ?? 0.0);
                writer.Write(observation.Controller?.Longitude ?? 0.0);
                break;
            case BridgeEventKind.CapacityLoss:
                writer.Write(observation.LostCount);
                writer.Write(observation.FirstLostSequence);
                writer.Write(observation.LastLostSequence);
                WriteString(writer, observation.Reason);
                break;
            case BridgeEventKind.CorruptEnvelope:
                WriteString(writer, observation.Reason);
                break;
            case BridgeEventKind.SessionEnded:
                break;
            case BridgeEventKind.ControllerSnapshotRequest:
                writer.Write(observation.SnapshotRequestId);
                break;
            default:
                throw new InvalidDataException($"Unsupported observation kind {observation.Kind}.");
        }
        return writer.ToArray();
    }

    private static void WriteController(PayloadWriter writer, BridgeController? controller)
    {
        controller ??= new BridgeController(string.Empty, 0, 0, 0);
        WriteString(writer, controller.Callsign);
        writer.Write(controller.FrequencyHz);
        writer.Write(controller.Latitude);
        writer.Write(controller.Longitude);
    }

    private static void WriteString(PayloadWriter writer, string value)
    {
        var bytes = EncodeUtf8(value);
        writer.Write(checked((uint)bytes.Length));
        writer.Write(bytes);
    }

    private static void WriteUInt16(byte[] bytes, int offset, ushort value)
    {
        bytes[offset] = (byte)value;
        bytes[offset + 1] = (byte)(value >> 8);
    }

    private static void WriteUInt32(byte[] bytes, int offset, uint value)
    {
        bytes[offset] = (byte)value;
        bytes[offset + 1] = (byte)(value >> 8);
        bytes[offset + 2] = (byte)(value >> 16);
        bytes[offset + 3] = (byte)(value >> 24);
    }

    private static void WriteUInt64(byte[] bytes, int offset, ulong value)
    {
        WriteUInt32(bytes, offset, (uint)value);
        WriteUInt32(bytes, offset + 4, (uint)(value >> 32));
    }

    private static ushort ReadUInt16(byte[] bytes, int offset) =>
        (ushort)(bytes[offset] | (bytes[offset + 1] << 8));

    private static uint ReadUInt32(byte[] bytes, int offset) =>
        (uint)(bytes[offset] |
               (bytes[offset + 1] << 8) |
               (bytes[offset + 2] << 16) |
               (bytes[offset + 3] << 24));

    private static ulong ReadUInt64(byte[] bytes, int offset) =>
        ReadUInt32(bytes, offset) | ((ulong)ReadUInt32(bytes, offset + 4) << 32);

    private static byte[] EncodeUtf8(string value)
    {
        var byteCount = 0;
        for (var index = 0; index < value.Length; ++index)
        {
            var codePoint = value[index];
            if (codePoint <= 0x7f)
            {
                ++byteCount;
            }
            else if (codePoint <= 0x7ff)
            {
                byteCount += 2;
            }
            else if (codePoint >= 0xd800 && codePoint <= 0xdbff)
            {
                if (index + 1 >= value.Length ||
                    value[index + 1] < 0xdc00 || value[index + 1] > 0xdfff)
                {
                    throw new InvalidDataException("Invalid unpaired UTF-16 high surrogate.");
                }
                ++index;
                byteCount += 4;
            }
            else if (codePoint >= 0xdc00 && codePoint <= 0xdfff)
            {
                throw new InvalidDataException("Invalid unpaired UTF-16 low surrogate.");
            }
            else
            {
                byteCount += 3;
            }
        }

        var bytes = new byte[byteCount];
        var output = 0;
        for (var index = 0; index < value.Length; ++index)
        {
            var codePoint = (int)value[index];
            if (codePoint <= 0x7f)
            {
                bytes[output++] = (byte)codePoint;
            }
            else if (codePoint <= 0x7ff)
            {
                bytes[output++] = (byte)(0xc0 | (codePoint >> 6));
                bytes[output++] = (byte)(0x80 | (codePoint & 0x3f));
            }
            else if (codePoint >= 0xd800 && codePoint <= 0xdbff)
            {
                var low = value[++index];
                codePoint = 0x10000 + ((codePoint - 0xd800) << 10) + (low - 0xdc00);
                bytes[output++] = (byte)(0xf0 | (codePoint >> 18));
                bytes[output++] = (byte)(0x80 | ((codePoint >> 12) & 0x3f));
                bytes[output++] = (byte)(0x80 | ((codePoint >> 6) & 0x3f));
                bytes[output++] = (byte)(0x80 | (codePoint & 0x3f));
            }
            else
            {
                bytes[output++] = (byte)(0xe0 | (codePoint >> 12));
                bytes[output++] = (byte)(0x80 | ((codePoint >> 6) & 0x3f));
                bytes[output++] = (byte)(0x80 | (codePoint & 0x3f));
            }
        }
        return bytes;
    }

    private static string DecodeUtf8(byte[] bytes, int offset, int count)
    {
        var end = checked(offset + count);
        var chars = new char[count];
        var output = 0;
        while (offset < end)
        {
            var first = bytes[offset++];
            int codePoint;
            if (first <= 0x7f)
            {
                codePoint = first;
            }
            else if (first >= 0xc2 && first <= 0xdf)
            {
                var second = ReadContinuation(bytes, ref offset, end);
                codePoint = ((first & 0x1f) << 6) | second;
            }
            else if (first >= 0xe0 && first <= 0xef)
            {
                var second = ReadContinuation(bytes, ref offset, end);
                var third = ReadContinuation(bytes, ref offset, end);
                if ((first == 0xe0 && second < 0x20) ||
                    (first == 0xed && second >= 0x20))
                {
                    throw new InvalidDataException("Invalid UTF-8 three-byte sequence.");
                }
                codePoint = ((first & 0x0f) << 12) | (second << 6) | third;
            }
            else if (first >= 0xf0 && first <= 0xf4)
            {
                var second = ReadContinuation(bytes, ref offset, end);
                var third = ReadContinuation(bytes, ref offset, end);
                var fourth = ReadContinuation(bytes, ref offset, end);
                if ((first == 0xf0 && second < 0x10) ||
                    (first == 0xf4 && second >= 0x10))
                {
                    throw new InvalidDataException("Invalid UTF-8 four-byte sequence.");
                }
                codePoint = ((first & 0x07) << 18) |
                            (second << 12) |
                            (third << 6) |
                            fourth;
            }
            else
            {
                throw new InvalidDataException("Invalid UTF-8 leading byte.");
            }

            if (codePoint <= 0xffff)
            {
                chars[output++] = (char)codePoint;
            }
            else
            {
                codePoint -= 0x10000;
                chars[output++] = (char)(0xd800 + (codePoint >> 10));
                chars[output++] = (char)(0xdc00 + (codePoint & 0x3ff));
            }
        }
        return new string(chars, 0, output);
    }

    private static int ReadContinuation(byte[] bytes, ref int offset, int end)
    {
        if (offset >= end)
        {
            throw new InvalidDataException("Incomplete UTF-8 sequence.");
        }
        var value = bytes[offset++];
        if ((value & 0xc0) != 0x80)
        {
            throw new InvalidDataException("Invalid UTF-8 continuation byte.");
        }
        return value & 0x3f;
    }

    private sealed class PayloadWriter
    {
        private readonly MemoryStream _stream = new();

        internal void Write(bool value) => _stream.WriteByte(value ? (byte)1 : (byte)0);
        internal void Write(int value) => Write(unchecked((uint)value));
        internal void Write(uint value)
        {
            var bytes = new byte[4];
            WriteUInt32(bytes, 0, value);
            Write(bytes);
        }
        internal void Write(ulong value)
        {
            var bytes = new byte[8];
            WriteUInt64(bytes, 0, value);
            Write(bytes);
        }
        internal void Write(double value) => Write(unchecked((ulong)BitConverter.DoubleToInt64Bits(value)));
        internal void Write(byte[] value) => _stream.Write(value, 0, value.Length);
        internal byte[] ToArray() => _stream.ToArray();
    }

    private sealed class PayloadReader
    {
        private readonly byte[] _bytes;
        private int _offset;

        internal PayloadReader(byte[] bytes)
        {
            _bytes = bytes;
            _offset = 0;
        }

        internal bool ReadBoolean() => ReadByte() != 0;
        internal byte ReadByte()
        {
            RequireAvailable(1);
            return _bytes[_offset++];
        }
        internal int ReadInt32() => unchecked((int)ReadUInt32());
        internal uint ReadUInt32()
        {
            RequireAvailable(4);
            var value = BridgeProtocol.ReadUInt32(_bytes, _offset);
            _offset += 4;
            return value;
        }
        internal ulong ReadUInt64()
        {
            RequireAvailable(8);
            var value = BridgeProtocol.ReadUInt64(_bytes, _offset);
            _offset += 8;
            return value;
        }
        internal double ReadDouble() => BitConverter.Int64BitsToDouble(unchecked((long)ReadUInt64()));

        internal string ReadString()
        {
            var length = checked((int)ReadUInt32());
            RequireAvailable(length);
            var value = DecodeUtf8(_bytes, _offset, length);
            _offset += length;
            return value;
        }

        internal BridgeController ReadController(bool hasFrequency, bool hasLocation) =>
            new(ReadString(), ReadInt32(), ReadDouble(), ReadDouble(), hasFrequency, hasLocation);

        internal void RequireComplete()
        {
            if (_offset != _bytes.Length)
            {
                throw new InvalidDataException("Unexpected trailing bridge payload bytes.");
            }
        }

        private void RequireAvailable(int count)
        {
            if (count < 0 || _offset > _bytes.Length - count)
            {
                throw new EndOfStreamException("Incomplete bridge payload.");
            }
        }
    }
}
