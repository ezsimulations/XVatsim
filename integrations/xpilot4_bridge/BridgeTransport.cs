using System.Runtime.InteropServices;

namespace XVatsim.XPilot4Bridge;

internal sealed class BridgeTransportOptions
{
    internal BridgeTransportOptions(string pipeName, int queueCapacity = 512)
    {
        PipeName = pipeName;
        QueueCapacity = queueCapacity;
    }

    internal string PipeName { get; }
    internal int QueueCapacity { get; }
}

internal sealed class BridgeTransport : IAsyncDisposable
{
    private readonly BridgeTransportOptions _options;
    private readonly BoundedObservationQueue _queue;
    private readonly Func<BridgeObservation> _helloFactory;
    private readonly Action<ulong> _snapshotRequest;
    private readonly Func<string, BridgeObservation> _protocolFaultFactory;
    private readonly SemaphoreSlim _wake = new(0, 1);
    private readonly CancellationTokenSource _stop = new();
    private readonly object _pipeGate = new();
    private Task? _runner;
    private nint _activePipe = NativeMethods.InvalidHandle;
    private int _started;
    private int _gracefulStop;

    internal BridgeTransport(
        BridgeTransportOptions options,
        Func<ulong> nextSequence,
        Func<BridgeObservation> helloFactory,
        Action<ulong> snapshotRequest,
        Func<string, BridgeObservation> protocolFaultFactory)
    {
        _options = options;
        _queue = new BoundedObservationQueue(options.QueueCapacity, nextSequence);
        _helloFactory = helloFactory;
        _snapshotRequest = snapshotRequest;
        _protocolFaultFactory = protocolFaultFactory;
    }

    internal ObservationQueueSnapshot QueueSnapshot => _queue.Snapshot();

    internal void Start()
    {
        if (Interlocked.Exchange(ref _started, 1) != 0)
        {
            return;
        }
        _runner = Task.Run(RunBlocking);
    }

    internal bool Enqueue(BridgeObservation observation)
    {
        var accepted = _queue.TryEnqueue(observation);
        Signal();
        return accepted;
    }

    internal void RequestGracefulStop()
    {
        Interlocked.Exchange(ref _gracefulStop, 1);
        Signal();
    }

    internal Task StopForTestingAsync()
    {
        StopTransport();
        return _runner ?? Task.CompletedTask;
    }

    public ValueTask DisposeAsync()
    {
        StopTransport();
        _runner?.GetAwaiter().GetResult();
        _wake.Dispose();
        _stop.Dispose();
        return default;
    }

    private void RunBlocking()
    {
        while (!_stop.IsCancellationRequested && Volatile.Read(ref _gracefulStop) == 0)
        {
            nint pipe = NativeMethods.InvalidHandle;
            try
            {
                pipe = NativeMethods.CreateCurrentUserPipe(_options.PipeName);
                SetActivePipe(pipe);
                NativeMethods.Connect(pipe);
                WriteObservation(pipe, _helloFactory());

                while (!_stop.IsCancellationRequested)
                {
                    if (_queue.TryPeek(out var observation) && observation is not null)
                    {
                        try
                        {
                            WriteObservation(pipe, observation);
                            _queue.CommitPeek(observation.Sequence, observation.Kind);
                        }
                        catch (ObservationTooLargeException)
                        {
                            _queue.CommitPeek(observation.Sequence, observation.Kind);
                            _queue.RecordLoss(
                                observation,
                                "companion-observation-size-limit");
                        }
                        continue;
                    }

                    if (Volatile.Read(ref _gracefulStop) != 0)
                    {
                        NativeMethods.Flush(pipe);
                        return;
                    }

                    if (NativeMethods.HasData(pipe))
                    {
                        ReadCommand(pipe);
                        continue;
                    }

                    // SDK facts signal this wait immediately. The short timed
                    // wait is only for infrequent commands arriving from the
                    // XVatsim side of the duplex pipe; it performs no polling
                    // work while asleep.
                    _wake.Wait(100, _stop.Token);
                }
            }
            catch (OperationCanceledException) when (_stop.IsCancellationRequested)
            {
                break;
            }
            catch (IOException)
            {
                if (!_stop.IsCancellationRequested)
                {
                    _stop.Token.WaitHandle.WaitOne(1000);
                }
            }
            finally
            {
                if (ClearActivePipe(pipe))
                {
                    NativeMethods.CancelAndDisconnect(pipe);
                    NativeMethods.Close(pipe);
                }
            }
        }
    }

    private void ReadCommand(nint pipe)
    {
        var headerBytes = new byte[BridgeProtocol.HeaderBytes];
        NativeMethods.ReadExactly(pipe, headerBytes);
        var header = BridgeProtocol.DecodeHeader(headerBytes);
        if (header.PayloadLength > 64 * 1024)
        {
            throw new InvalidDataException("Bridge command exceeds the mechanical limit.");
        }
        var payload = new byte[header.PayloadLength];
        NativeMethods.ReadExactly(pipe, payload);
        try
        {
            _snapshotRequest(BridgeProtocol.DecodeSnapshotRequest(header, payload));
        }
        catch (InvalidDataException exception)
        {
            Enqueue(_protocolFaultFactory(exception.Message));
        }
    }

    private static void WriteObservation(nint pipe, BridgeObservation observation)
    {
        foreach (var frame in BridgeProtocol.EncodeObservation(observation))
        {
            NativeMethods.WriteAll(pipe, frame);
        }
    }

    private void StopTransport()
    {
        if (!_stop.IsCancellationRequested)
        {
            _stop.Cancel();
        }
        nint pipe;
        lock (_pipeGate)
        {
            pipe = _activePipe;
            _activePipe = NativeMethods.InvalidHandle;
        }
        NativeMethods.CancelAndClose(pipe);
        Signal();
    }

    private void SetActivePipe(nint pipe)
    {
        lock (_pipeGate)
        {
            _activePipe = pipe;
        }
    }

    private bool ClearActivePipe(nint pipe)
    {
        lock (_pipeGate)
        {
            if (_activePipe == pipe)
            {
                _activePipe = NativeMethods.InvalidHandle;
                return true;
            }
            return false;
        }
    }

    private void Signal()
    {
        try
        {
            _wake.Release();
        }
        catch (SemaphoreFullException)
        {
        }
        catch (ObjectDisposedException)
        {
        }
    }

    private static class NativeMethods
    {
        internal static readonly nint InvalidHandle = new(-1);
        private const uint PipeAccessDuplex = 0x00000003;
        private const uint FileFlagFirstPipeInstance = 0x00080000;
        private const uint PipeRejectRemoteClients = 0x00000008;
        private const uint TokenQuery = 0x00000008;
        private const int TokenUser = 1;
        private const uint SddlRevision1 = 1;
        private const int ErrorInsufficientBuffer = 122;
        private const int ErrorPipeConnected = 535;
        private const int ErrorOperationAborted = 995;

        [StructLayout(LayoutKind.Sequential)]
        private struct SecurityAttributes
        {
            internal int Length;
            internal nint SecurityDescriptor;
            internal int InheritHandle;
        }

        [DllImport("kernel32.dll")]
        private static extern nint GetCurrentProcess();

        [DllImport("advapi32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool OpenProcessToken(
            nint processHandle,
            uint desiredAccess,
            out nint tokenHandle);

        [DllImport("advapi32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetTokenInformation(
            nint tokenHandle,
            int tokenInformationClass,
            nint tokenInformation,
            int tokenInformationLength,
            out int returnLength);

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool ConvertSidToStringSidW(
            nint sid,
            out nint stringSid);

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool ConvertStringSecurityDescriptorToSecurityDescriptorW(
            string stringSecurityDescriptor,
            uint stringSDRevision,
            out nint securityDescriptor,
            out uint securityDescriptorSize);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern nint CreateNamedPipeW(
            string name,
            uint openMode,
            uint pipeMode,
            uint maxInstances,
            uint outBufferSize,
            uint inBufferSize,
            uint defaultTimeout,
            ref SecurityAttributes securityAttributes);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool ConnectNamedPipe(nint pipe, nint overlapped);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool ReadFile(
            nint file,
            [Out] byte[] buffer,
            int bytesToRead,
            out int bytesRead,
            nint overlapped);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool WriteFile(
            nint file,
            byte[] buffer,
            int bytesToWrite,
            out int bytesWritten,
            nint overlapped);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool PeekNamedPipe(
            nint pipe,
            nint buffer,
            int bufferSize,
            nint bytesRead,
            out int totalBytesAvailable,
            nint bytesLeftThisMessage);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CancelIoEx(nint file, nint overlapped);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool DisconnectNamedPipe(nint pipe);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool FlushFileBuffers(nint file);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CloseHandle(nint handle);

        [DllImport("kernel32.dll")]
        private static extern nint LocalFree(nint memory);

        internal static nint CreateCurrentUserPipe(string pipeName)
        {
            nint securityDescriptor = 0;
            try
            {
                var sddl = "D:P(A;;GA;;;SY)(A;;GA;;;" + CurrentUserSid() + ")";
                if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                        sddl,
                        SddlRevision1,
                        out securityDescriptor,
                        out _))
                {
                    ThrowLastError("build current-user pipe security");
                }
                var attributes = new SecurityAttributes
                {
                    Length = Marshal.SizeOf<SecurityAttributes>(),
                    SecurityDescriptor = securityDescriptor,
                    InheritHandle = 0,
                };
                var pipe = CreateNamedPipeW(
                    "\\\\.\\pipe\\" + pipeName,
                    PipeAccessDuplex | FileFlagFirstPipeInstance,
                    PipeRejectRemoteClients,
                    1,
                    64 * 1024,
                    64 * 1024,
                    0,
                    ref attributes);
                if (pipe == InvalidHandle)
                {
                    ThrowLastError("create current-user bridge pipe");
                }
                return pipe;
            }
            finally
            {
                if (securityDescriptor != 0)
                {
                    LocalFree(securityDescriptor);
                }
            }
        }

        internal static void Connect(nint pipe)
        {
            if (ConnectNamedPipe(pipe, 0))
            {
                return;
            }
            var error = Marshal.GetLastWin32Error();
            if (error != ErrorPipeConnected)
            {
                throw new IOException("Bridge pipe connection failed with Windows error " + error + ".");
            }
        }

        internal static void ReadExactly(nint pipe, byte[] destination)
        {
            var offset = 0;
            while (offset < destination.Length)
            {
                var remaining = destination.Length - offset;
                var chunk = new byte[remaining];
                if (!ReadFile(pipe, chunk, chunk.Length, out var read, 0) || read <= 0)
                {
                    ThrowLastError("read bridge pipe");
                }
                Array.Copy(chunk, 0, destination, offset, read);
                offset += read;
            }
        }

        internal static bool HasData(nint pipe)
        {
            if (!PeekNamedPipe(pipe, 0, 0, 0, out var available, 0))
            {
                ThrowLastError("inspect bridge pipe");
            }
            return available > 0;
        }

        internal static void Flush(nint pipe)
        {
            if (!FlushFileBuffers(pipe))
            {
                ThrowLastError("flush bridge pipe");
            }
        }

        internal static void WriteAll(nint pipe, byte[] source)
        {
            var offset = 0;
            while (offset < source.Length)
            {
                var remaining = source.Length - offset;
                var chunk = new byte[remaining];
                Array.Copy(source, offset, chunk, 0, remaining);
                if (!WriteFile(pipe, chunk, chunk.Length, out var written, 0) || written <= 0)
                {
                    ThrowLastError("write bridge pipe");
                }
                offset += written;
            }
        }

        internal static void CancelAndDisconnect(nint pipe)
        {
            if (pipe == 0 || pipe == InvalidHandle)
            {
                return;
            }
            CancelIoEx(pipe, 0);
            DisconnectNamedPipe(pipe);
        }

        internal static void CancelAndClose(nint pipe)
        {
            CancelAndDisconnect(pipe);
            Close(pipe);
        }

        internal static void Close(nint pipe)
        {
            if (pipe != 0 && pipe != InvalidHandle)
            {
                CloseHandle(pipe);
            }
        }

        private static string CurrentUserSid()
        {
            if (!OpenProcessToken(GetCurrentProcess(), TokenQuery, out var token))
            {
                ThrowLastError("open current process token");
            }
            try
            {
                GetTokenInformation(token, TokenUser, 0, 0, out var required);
                if (Marshal.GetLastWin32Error() != ErrorInsufficientBuffer || required <= 0)
                {
                    ThrowLastError("size current process token");
                }
                var tokenInformation = Marshal.AllocHGlobal(required);
                try
                {
                    if (!GetTokenInformation(
                            token,
                            TokenUser,
                            tokenInformation,
                            required,
                            out _))
                    {
                        ThrowLastError("read current process token");
                    }
                    var sid = Marshal.ReadIntPtr(tokenInformation);
                    if (!ConvertSidToStringSidW(sid, out var stringSid))
                    {
                        ThrowLastError("format current user SID");
                    }
                    try
                    {
                        return Marshal.PtrToStringUni(stringSid) ??
                               throw new IOException("Current user SID was empty.");
                    }
                    finally
                    {
                        LocalFree(stringSid);
                    }
                }
                finally
                {
                    Marshal.FreeHGlobal(tokenInformation);
                }
            }
            finally
            {
                CloseHandle(token);
            }
        }

        private static void ThrowLastError(string action)
        {
            var error = Marshal.GetLastWin32Error();
            if (error == ErrorOperationAborted)
            {
                throw new OperationCanceledException(action + " was cancelled.");
            }
            throw new IOException(action + " failed with Windows error " + error + ".");
        }
    }
}
