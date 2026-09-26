namespace XVatsim.XPilot4Bridge;

internal readonly struct ObservationQueueSnapshot
{
    internal ObservationQueueSnapshot(
        int capacity,
        int pending,
        ulong enqueued,
        ulong dequeued,
        ulong lost,
        int highWaterMark)
    {
        Capacity = capacity;
        Pending = pending;
        Enqueued = enqueued;
        Dequeued = dequeued;
        Lost = lost;
        HighWaterMark = highWaterMark;
    }

    internal int Capacity { get; }
    internal int Pending { get; }
    internal ulong Enqueued { get; }
    internal ulong Dequeued { get; }
    internal ulong Lost { get; }
    internal int HighWaterMark { get; }
}

internal sealed class BoundedObservationQueue
{
    private readonly object _gate = new();
    private readonly Queue<BridgeObservation> _queue;
    private readonly int _capacity;
    private readonly Func<ulong> _nextSequence;
    private ulong _enqueued;
    private ulong _dequeued;
    private ulong _lost;
    private ulong _pendingLost;
    private ulong _firstLostSequence;
    private ulong _lastLostSequence;
    private ulong _lossProcessEpoch;
    private ulong _lossConnectionEpoch;
    private string _lossReason = string.Empty;
    private int _highWaterMark;

    internal BoundedObservationQueue(int capacity, Func<ulong> nextSequence)
    {
        if (capacity < 2)
        {
            throw new ArgumentOutOfRangeException(nameof(capacity));
        }
        _capacity = capacity;
        // xPilot 4 beta 7 publishes a trimmed runtime. Its retained Queue<T>
        // surface includes the basic FIFO operations but not the capacity
        // constructor or TryPeek, so keep this worker mechanically compatible
        // with the host without changing the bounded-queue policy.
        _queue = new Queue<BridgeObservation>();
        _nextSequence = nextSequence;
    }

    internal bool TryEnqueue(BridgeObservation observation)
    {
        lock (_gate)
        {
            if (_queue.Count >= _capacity)
            {
                RecordLossLocked(observation, "companion-observation-queue-capacity");
                return false;
            }
            _queue.Enqueue(observation);
            ++_enqueued;
            _highWaterMark = Math.Max(_highWaterMark, _queue.Count);
            return true;
        }
    }

    internal void RecordLoss(BridgeObservation observation, string reason)
    {
        lock (_gate)
        {
            RecordLossLocked(observation, reason);
        }
    }

    internal bool TryPeek(out BridgeObservation? observation)
    {
        lock (_gate)
        {
            MaterializeLossLocked();
            if (_queue.Count == 0)
            {
                observation = null;
                return false;
            }
            observation = _queue.Peek();
            return true;
        }
    }

    internal void CommitPeek(ulong expectedSequence, BridgeEventKind expectedKind)
    {
        lock (_gate)
        {
            if (_queue.Count == 0)
            {
                throw new InvalidOperationException("Bridge queue was empty before commit.");
            }
            var current = _queue.Peek();
            if (current.Sequence != expectedSequence || current.Kind != expectedKind)
            {
                throw new InvalidOperationException("Bridge queue head changed before commit.");
            }
            _queue.Dequeue();
            ++_dequeued;
            MaterializeLossLocked();
        }
    }

    internal ObservationQueueSnapshot Snapshot()
    {
        lock (_gate)
        {
            return new ObservationQueueSnapshot(
                _capacity,
                _queue.Count,
                _enqueued,
                _dequeued,
                _lost,
                _highWaterMark);
        }
    }

    private void RecordLossLocked(BridgeObservation observation, string reason)
    {
        ++_lost;
        ++_pendingLost;
        if (_firstLostSequence == 0)
        {
            _firstLostSequence = observation.Sequence;
            _lossProcessEpoch = observation.ProcessEpoch;
            _lossConnectionEpoch = observation.ConnectionEpoch;
            _lossReason = reason;
        }
        _lastLostSequence = observation.Sequence;
    }

    private void MaterializeLossLocked()
    {
        if (_pendingLost == 0 || _queue.Count >= _capacity)
        {
            return;
        }
        _queue.Enqueue(new BridgeObservation
        {
            Kind = BridgeEventKind.CapacityLoss,
            ProcessEpoch = _lossProcessEpoch,
            ConnectionEpoch = _lossConnectionEpoch,
            Sequence = _nextSequence(),
            ReceivedUnixMicroseconds = BridgeProtocol.CurrentUnixMicroseconds(),
            LostCount = _pendingLost,
            FirstLostSequence = _firstLostSequence,
            LastLostSequence = _lastLostSequence,
            Reason = _lossReason,
        });
        ++_enqueued;
        _highWaterMark = Math.Max(_highWaterMark, _queue.Count);
        _pendingLost = 0;
        _firstLostSequence = 0;
        _lastLostSequence = 0;
        _lossProcessEpoch = 0;
        _lossConnectionEpoch = 0;
        _lossReason = string.Empty;
    }
}
