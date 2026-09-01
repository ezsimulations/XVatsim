#include "XVatsim/modules/runtime_workers/AsyncDiagnosticsWriter.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace xvatsim::modules::runtime_workers {
namespace {

constexpr char kDiagnosticsLogFilePrefix[] = "xvatsim_diagnostics_";
constexpr char kDiagnosticsLogFileSuffix[] = ".log";

std::uint64_t ElapsedMicroseconds(
    const std::chrono::steady_clock::time_point& started) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
}

void UpdateMaximum(
    std::atomic<std::uint64_t>* destination,
    std::uint64_t candidate) {
    auto observed = destination->load(std::memory_order_relaxed);
    while (candidate > observed &&
           !destination->compare_exchange_weak(
               observed,
               candidate,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

std::uint64_t CurrentTickSeconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::string DateToken(std::time_t wallTime) {
    std::tm localTime{};
#if defined(_WIN32)
    if (localtime_s(&localTime, &wallTime) != 0) {
        return "unknown_date";
    }
#else
    if (localtime_r(&wallTime, &localTime) == nullptr) {
        return "unknown_date";
    }
#endif
    char buffer[16] = {};
    if (std::strftime(buffer, sizeof(buffer), "%Y_%m_%d", &localTime) == 0) {
        return "unknown_date";
    }
    return buffer;
}

bool IsDateToken(const std::string& token) {
    if (token.size() != 10 || token[4] != '_' || token[7] != '_') {
        return false;
    }
    for (std::size_t index = 0; index < token.size(); ++index) {
        if (index == 4 || index == 7) {
            continue;
        }
        if (token[index] < '0' || token[index] > '9') {
            return false;
        }
    }
    return true;
}

std::string ExtractDateToken(const std::filesystem::path& path) {
    const auto filename = path.filename().string();
    const std::string prefix = kDiagnosticsLogFilePrefix;
    const std::string suffix = kDiagnosticsLogFileSuffix;
    if (filename.size() != prefix.size() + 10 + suffix.size() ||
        filename.compare(0, prefix.size(), prefix) != 0 ||
        filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) !=
            0) {
        return {};
    }
    const auto token = filename.substr(prefix.size(), 10);
    return IsDateToken(token) ? token : std::string{};
}

void EnforceRetention(
    const std::filesystem::path& logDirectory,
    int retainedDateLogCount) {
    std::error_code error;
    if (!std::filesystem::exists(logDirectory, error) || error) {
        return;
    }

    static constexpr const char* legacyLogs[] = {
        "xvatsim_diagnostics.log",
        "xvatsim_diagnostics.log.1",
        "xvatsim_diagnostics.log.2",
    };
    for (const auto* filename : legacyLogs) {
        std::filesystem::remove(logDirectory / filename, error);
        error.clear();
    }

    std::vector<std::pair<std::string, std::filesystem::path>> datedLogs;
    for (std::filesystem::directory_iterator iterator(logDirectory, error), end;
         !error && iterator != end;
         iterator.increment(error)) {
        if (!iterator->is_regular_file(error) || error) {
            error.clear();
            continue;
        }
        const auto token = ExtractDateToken(iterator->path());
        if (!token.empty()) {
            datedLogs.push_back({token, iterator->path()});
        }
    }
    std::sort(
        datedLogs.begin(),
        datedLogs.end(),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.first != rhs.first) {
                return lhs.first > rhs.first;
            }
            return lhs.second.filename().string() >
                   rhs.second.filename().string();
        });

    std::vector<std::string> retainedDates;
    const auto retainedCount = static_cast<std::size_t>(
        std::max(0, retainedDateLogCount));
    for (const auto& [token, path] : datedLogs) {
        if (std::find(retainedDates.begin(), retainedDates.end(), token) ==
                retainedDates.end() &&
            retainedDates.size() < retainedCount) {
            retainedDates.push_back(token);
        }
        if (std::find(retainedDates.begin(), retainedDates.end(), token) ==
            retainedDates.end()) {
            std::filesystem::remove(path, error);
            error.clear();
        }
    }
}

}  // namespace

struct AsyncDiagnosticsWriter::Implementation {
    enum class RecordKind {
        Line,
        Deferred,
        FlightLoopTiming,
    };

    struct Record {
        RecordKind kind = RecordKind::Line;
        std::uint64_t tickSeconds = 0;
        std::time_t wallTime = 0;
        std::string line;
        DeferredFormatter formatter;
        FlightLoopTimingRecord flightLoopTiming;
    };

    struct FixedQueue {
        std::vector<std::optional<Record>> slots;
        std::size_t head = 0;
        std::size_t tail = 0;
        std::size_t count = 0;

        void Reset(std::size_t capacity) {
            slots.clear();
            slots.resize(capacity);
            head = 0;
            tail = 0;
            count = 0;
        }

        bool Full() const {
            return count == slots.size();
        }

        bool Empty() const {
            return count == 0;
        }

        void Push(Record record) {
            slots[tail].emplace(std::move(record));
            tail = (tail + 1) % slots.size();
            ++count;
        }

        Record Pop() {
            Record record = std::move(*slots[head]);
            slots[head].reset();
            head = (head + 1) % slots.size();
            --count;
            return record;
        }
    };

    struct FlightLoopTimingEnvelope {
        std::uint64_t tickSeconds = 0;
        std::time_t wallTime = 0;
        FlightLoopTimingRecord timing;
    };

    // Single producer (the X-Plane flight loop), single consumer (the
    // diagnostics writer). Storage is allocated only during Start().
    struct FixedSpscFlightLoopTimingQueue {
        static_assert(
            std::atomic<std::size_t>::is_always_lock_free,
            "flight-loop timing lane requires lock-free indexes");
        std::vector<FlightLoopTimingEnvelope> slots;
        std::atomic<std::size_t> writeIndex{0};
        std::atomic<std::size_t> readIndex{0};

        void Reset(std::size_t capacity) {
            slots.clear();
            slots.resize(capacity + 1);
            writeIndex.store(0, std::memory_order_relaxed);
            readIndex.store(0, std::memory_order_relaxed);
        }

        bool TryPush(const FlightLoopTimingEnvelope& envelope) noexcept {
            const auto write = writeIndex.load(std::memory_order_relaxed);
            const auto next = (write + 1) % slots.size();
            if (next == readIndex.load(std::memory_order_acquire)) {
                return false;
            }
            slots[write] = envelope;
            writeIndex.store(next, std::memory_order_release);
            return true;
        }

        bool TryPop(FlightLoopTimingEnvelope* envelope) noexcept {
            const auto read = readIndex.load(std::memory_order_relaxed);
            if (read == writeIndex.load(std::memory_order_acquire)) {
                return false;
            }
            *envelope = slots[read];
            readIndex.store(
                (read + 1) % slots.size(),
                std::memory_order_release);
            return true;
        }

        bool Empty() const noexcept {
            return readIndex.load(std::memory_order_acquire) ==
                   writeIndex.load(std::memory_order_acquire);
        }

        std::size_t Depth() const noexcept {
            const auto read = readIndex.load(std::memory_order_acquire);
            const auto write = writeIndex.load(std::memory_order_acquire);
            return write >= read
                ? write - read
                : slots.size() - (read - write);
        }
    };

    DiagnosticsWriterOptions options;
    mutable std::mutex queueMutex;
    std::condition_variable workReady;
    std::condition_variable idleChanged;
    FixedQueue routineQueue;
    FixedQueue criticalQueue;
    FixedSpscFlightLoopTimingQueue flightLoopTimingQueue;
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> accepting{false};
    bool stopRequested = false;
    bool recordInFlight = false;

    std::ofstream output;
    std::string openDateToken;
    std::string retentionDateToken;
    std::chrono::steady_clock::time_point nextStorageAttempt{};
    std::size_t artificiallyDelayedRecords = 0;

    std::atomic<bool> workerPriorityRequested{false};
    std::atomic<bool> workerPrioritySucceeded{false};
    std::atomic<std::uint64_t> submittedRoutine{0};
    std::atomic<std::uint64_t> submittedCritical{0};
    std::atomic<std::uint64_t> submittedFlightLoopTiming{0};
    std::atomic<std::uint64_t> dequeued{0};
    std::atomic<std::uint64_t> dequeuedFlightLoopTiming{0};
    std::atomic<std::uint64_t> written{0};
    std::atomic<std::uint64_t> writtenFlightLoopTiming{0};
    std::atomic<std::uint64_t> droppedContention{0};
    std::atomic<std::uint64_t> droppedRoutineContention{0};
    std::atomic<std::uint64_t> droppedCriticalContention{0};
    std::atomic<std::uint64_t> droppedRoutineFull{0};
    std::atomic<std::uint64_t> droppedCriticalFull{0};
    std::atomic<std::uint64_t> droppedFlightLoopTimingFull{0};
    std::atomic<std::uint64_t> rejectedNotRunning{0};
    std::atomic<std::uint64_t> rejectedFlightLoopTimingNotRunning{0};
    std::atomic<std::uint64_t> formattingFailures{0};
    std::atomic<std::uint64_t> storageFailures{0};
    std::atomic<std::uint64_t> maximumQueueDepth{0};
    std::atomic<std::uint64_t> maximumFlightLoopTimingQueueDepth{0};
    std::atomic<std::uint64_t> maximumProducerMicroseconds{0};
    std::atomic<std::uint64_t> maximumFlightLoopTimingProducerMicroseconds{0};
    std::atomic<std::uint64_t> workerThreadIdentity{0};

    void ResetCounters() {
        workerPriorityRequested.store(false, std::memory_order_relaxed);
        workerPrioritySucceeded.store(false, std::memory_order_relaxed);
        submittedRoutine.store(0, std::memory_order_relaxed);
        submittedCritical.store(0, std::memory_order_relaxed);
        submittedFlightLoopTiming.store(0, std::memory_order_relaxed);
        dequeued.store(0, std::memory_order_relaxed);
        dequeuedFlightLoopTiming.store(0, std::memory_order_relaxed);
        written.store(0, std::memory_order_relaxed);
        writtenFlightLoopTiming.store(0, std::memory_order_relaxed);
        droppedContention.store(0, std::memory_order_relaxed);
        droppedRoutineContention.store(0, std::memory_order_relaxed);
        droppedCriticalContention.store(0, std::memory_order_relaxed);
        droppedRoutineFull.store(0, std::memory_order_relaxed);
        droppedCriticalFull.store(0, std::memory_order_relaxed);
        droppedFlightLoopTimingFull.store(0, std::memory_order_relaxed);
        rejectedNotRunning.store(0, std::memory_order_relaxed);
        rejectedFlightLoopTimingNotRunning.store(0, std::memory_order_relaxed);
        formattingFailures.store(0, std::memory_order_relaxed);
        storageFailures.store(0, std::memory_order_relaxed);
        maximumQueueDepth.store(0, std::memory_order_relaxed);
        maximumFlightLoopTimingQueueDepth.store(0, std::memory_order_relaxed);
        maximumProducerMicroseconds.store(0, std::memory_order_relaxed);
        maximumFlightLoopTimingProducerMicroseconds.store(
            0, std::memory_order_relaxed);
        workerThreadIdentity.store(0, std::memory_order_relaxed);
    }

    bool Enqueue(Record record, DiagnosticsImportance importance) {
        const auto started = std::chrono::steady_clock::now();
        if (!accepting.load(std::memory_order_acquire)) {
            rejectedNotRunning.fetch_add(1, std::memory_order_relaxed);
            UpdateMaximum(&maximumProducerMicroseconds, ElapsedMicroseconds(started));
            return false;
        }
        if (!queueMutex.try_lock()) {
            droppedContention.fetch_add(1, std::memory_order_relaxed);
            if (importance == DiagnosticsImportance::Critical) {
                droppedCriticalContention.fetch_add(
                    1, std::memory_order_relaxed);
            } else {
                droppedRoutineContention.fetch_add(
                    1, std::memory_order_relaxed);
            }
            UpdateMaximum(&maximumProducerMicroseconds, ElapsedMicroseconds(started));
            return false;
        }
        if (options.artificialQueueLockHoldForTesting.count() > 0) {
            std::this_thread::sleep_for(
                options.artificialQueueLockHoldForTesting);
        }

        bool accepted = false;
        auto& queue = importance == DiagnosticsImportance::Critical
            ? criticalQueue
            : routineQueue;
        if (!accepting.load(std::memory_order_relaxed)) {
            rejectedNotRunning.fetch_add(1, std::memory_order_relaxed);
        } else if (queue.Full()) {
            if (importance == DiagnosticsImportance::Critical) {
                droppedCriticalFull.fetch_add(1, std::memory_order_relaxed);
            } else {
                droppedRoutineFull.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            queue.Push(std::move(record));
            accepted = true;
            if (importance == DiagnosticsImportance::Critical) {
                submittedCritical.fetch_add(1, std::memory_order_relaxed);
            } else {
                submittedRoutine.fetch_add(1, std::memory_order_relaxed);
            }
            UpdateMaximum(
                &maximumQueueDepth,
                static_cast<std::uint64_t>(
                    routineQueue.count + criticalQueue.count));
        }
        queueMutex.unlock();
        if (accepted) {
            workReady.notify_one();
        }
        UpdateMaximum(&maximumProducerMicroseconds, ElapsedMicroseconds(started));
        return accepted;
    }

    bool EnqueueFlightLoopTiming(
        const FlightLoopTimingRecord& timing) noexcept {
        const auto started = std::chrono::steady_clock::now();
        if (!accepting.load(std::memory_order_acquire)) {
            rejectedFlightLoopTimingNotRunning.fetch_add(
                1, std::memory_order_relaxed);
            UpdateMaximum(
                &maximumFlightLoopTimingProducerMicroseconds,
                ElapsedMicroseconds(started));
            return false;
        }

        FlightLoopTimingEnvelope envelope;
        envelope.tickSeconds = CurrentTickSeconds();
        envelope.wallTime = std::time(nullptr);
        envelope.timing = timing;
        if (!flightLoopTimingQueue.TryPush(envelope)) {
            droppedFlightLoopTimingFull.fetch_add(
                1, std::memory_order_relaxed);
            UpdateMaximum(
                &maximumFlightLoopTimingProducerMicroseconds,
                ElapsedMicroseconds(started));
            return false;
        }

        submittedFlightLoopTiming.fetch_add(1, std::memory_order_relaxed);
        UpdateMaximum(
            &maximumFlightLoopTimingQueueDepth,
            static_cast<std::uint64_t>(flightLoopTimingQueue.Depth()));
        workReady.notify_one();
        UpdateMaximum(
            &maximumFlightLoopTimingProducerMicroseconds,
            ElapsedMicroseconds(started));
        return true;
    }

    bool EnsureOutput(const Record& record) {
        if (options.forceStorageFailureForTesting) {
            storageFailures.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto dateToken = DateToken(record.wallTime);
        if (output.is_open() && openDateToken == dateToken) {
            return true;
        }
        if (now < nextStorageAttempt) {
            storageFailures.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        output.close();
        openDateToken.clear();
        std::error_code error;
        std::filesystem::create_directories(options.logDirectory, error);
        if (error) {
            storageFailures.fetch_add(1, std::memory_order_relaxed);
            nextStorageAttempt = now + std::chrono::seconds(1);
            return false;
        }
        if (retentionDateToken != dateToken) {
            EnforceRetention(options.logDirectory, options.retainedDateLogCount);
            retentionDateToken = dateToken;
        }
        const auto path =
            options.logDirectory /
            (std::string{kDiagnosticsLogFilePrefix} + dateToken +
             kDiagnosticsLogFileSuffix);
        output.open(path, std::ios::app);
        if (!output) {
            storageFailures.fetch_add(1, std::memory_order_relaxed);
            output.close();
            nextStorageAttempt = now + std::chrono::seconds(1);
            return false;
        }
        openDateToken = dateToken;
        nextStorageAttempt = {};
        return true;
    }

    std::string FormatRecord(Record* record) {
        if (record->kind == RecordKind::Line) {
            return std::move(record->line);
        }
        if (record->kind == RecordKind::Deferred) {
            return record->formatter != nullptr
                ? record->formatter()
                : std::string{};
        }
        const auto& timing = record->flightLoopTiming;
        std::ostringstream stream;
        stream << "event=flight-loop-complete"
               << " sequence=" << timing.sequence
               << " completeUs=" << timing.completeBeforeTelemetryUs
               << " refreshUs=" << timing.refreshUs
               << " diagnosticsSubmissionUs="
               << timing.diagnosticsSubmissionUs
               << " accessoryPath=" << (timing.accessoryPath ? 1 : 0)
               << " activeCadence=" << (timing.activeCadence ? 1 : 0)
               << " measurementBoundary=immediately-before-telemetry-publish";
        return stream.str();
    }

    void Process(Record record) {
        const bool flightLoopTiming =
            record.kind == RecordKind::FlightLoopTiming;
        if (options.artificialWorkerDelayForTesting.count() > 0 &&
            artificiallyDelayedRecords <
                options.artificialWorkerDelayRecordCountForTesting) {
            ++artificiallyDelayedRecords;
            std::this_thread::sleep_for(
                options.artificialWorkerDelayForTesting);
        }
        std::string line;
        try {
            line = FormatRecord(&record);
        } catch (...) {
            formattingFailures.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (!EnsureOutput(record)) {
            return;
        }
        output << "tick=" << record.tickSeconds << " " << line << '\n';
        output.flush();
        if (!output) {
            storageFailures.fetch_add(1, std::memory_order_relaxed);
            output.close();
            openDateToken.clear();
            nextStorageAttempt =
                std::chrono::steady_clock::now() + std::chrono::seconds(1);
            return;
        }
        written.fetch_add(1, std::memory_order_relaxed);
        if (flightLoopTiming) {
            writtenFlightLoopTiming.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void WorkerMain() {
        workerThreadIdentity.store(
            static_cast<std::uint64_t>(
                std::hash<std::thread::id>{}(std::this_thread::get_id())),
            std::memory_order_release);
#if defined(_WIN32)
        workerPriorityRequested.store(true, std::memory_order_release);
        workerPrioritySucceeded.store(
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL) !=
                0,
            std::memory_order_release);
#endif
        for (;;) {
            std::optional<Record> record;
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                workReady.wait_for(lock, std::chrono::milliseconds(20), [this]() {
                    return stopRequested || !criticalQueue.Empty() ||
                           !flightLoopTimingQueue.Empty() ||
                           !routineQueue.Empty();
                });
                if (criticalQueue.Empty() &&
                    flightLoopTimingQueue.Empty() &&
                    routineQueue.Empty()) {
                    if (stopRequested) {
                        break;
                    }
                    continue;
                }
                if (!criticalQueue.Empty()) {
                    record.emplace(criticalQueue.Pop());
                } else {
                    FlightLoopTimingEnvelope timing;
                    if (flightLoopTimingQueue.TryPop(&timing)) {
                        record.emplace();
                        record->kind = RecordKind::FlightLoopTiming;
                        record->tickSeconds = timing.tickSeconds;
                        record->wallTime = timing.wallTime;
                        record->flightLoopTiming = timing.timing;
                        dequeuedFlightLoopTiming.fetch_add(
                            1, std::memory_order_relaxed);
                    } else {
                        record.emplace(routineQueue.Pop());
                    }
                }
                recordInFlight = true;
                dequeued.fetch_add(1, std::memory_order_relaxed);
            }

            Process(std::move(*record));

            {
                std::lock_guard<std::mutex> lock(queueMutex);
                recordInFlight = false;
            }
            idleChanged.notify_all();
        }
        output.close();
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            recordInFlight = false;
        }
        idleChanged.notify_all();
        running.store(false, std::memory_order_release);
    }
};

AsyncDiagnosticsWriter::AsyncDiagnosticsWriter()
    : implementation_(std::make_unique<Implementation>()) {}

AsyncDiagnosticsWriter::~AsyncDiagnosticsWriter() {
    Stop();
}

bool AsyncDiagnosticsWriter::Start(DiagnosticsWriterOptions options) {
    auto& state = *implementation_;
    if (state.running.load(std::memory_order_acquire)) {
        return true;
    }
    if (options.routineQueueCapacity == 0 ||
        options.criticalQueueCapacity == 0 ||
        options.flightLoopTimingQueueCapacity == 0 ||
        options.flightLoopTimingQueueCapacity ==
            std::numeric_limits<std::size_t>::max() ||
        options.logDirectory.empty()) {
        return false;
    }

    state.options = std::move(options);
    state.routineQueue.Reset(state.options.routineQueueCapacity);
    state.criticalQueue.Reset(state.options.criticalQueueCapacity);
    state.flightLoopTimingQueue.Reset(
        state.options.flightLoopTimingQueueCapacity);
    state.stopRequested = false;
    state.recordInFlight = false;
    state.openDateToken.clear();
    state.retentionDateToken.clear();
    state.nextStorageAttempt = {};
    state.artificiallyDelayedRecords = 0;
    state.ResetCounters();
    state.running.store(true, std::memory_order_release);
    state.accepting.store(true, std::memory_order_release);
    try {
        state.worker = std::thread([&state]() { state.WorkerMain(); });
    } catch (...) {
        state.accepting.store(false, std::memory_order_release);
        state.running.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

void AsyncDiagnosticsWriter::Stop() {
    auto& state = *implementation_;
    state.accepting.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(state.queueMutex);
        state.stopRequested = true;
    }
    state.workReady.notify_all();
    if (state.worker.joinable()) {
        state.worker.join();
    }
    state.running.store(false, std::memory_order_release);
}

bool AsyncDiagnosticsWriter::TryEnqueueLine(
    std::string line,
    DiagnosticsImportance importance) {
    Implementation::Record record;
    record.kind = Implementation::RecordKind::Line;
    record.tickSeconds = CurrentTickSeconds();
    record.wallTime = std::time(nullptr);
    record.line = std::move(line);
    return implementation_->Enqueue(std::move(record), importance);
}

bool AsyncDiagnosticsWriter::TryEnqueueDeferred(
    DeferredFormatter formatter,
    DiagnosticsImportance importance) {
    Implementation::Record record;
    record.kind = Implementation::RecordKind::Deferred;
    record.tickSeconds = CurrentTickSeconds();
    record.wallTime = std::time(nullptr);
    record.formatter = std::move(formatter);
    return implementation_->Enqueue(std::move(record), importance);
}

bool AsyncDiagnosticsWriter::TryEnqueueFlightLoopTiming(
    const FlightLoopTimingRecord& timing) noexcept {
    return implementation_->EnqueueFlightLoopTiming(timing);
}

DiagnosticsWriterSnapshot AsyncDiagnosticsWriter::Snapshot() const {
    const auto& state = *implementation_;
    DiagnosticsWriterSnapshot snapshot;
    snapshot.running = state.running.load(std::memory_order_acquire);
    snapshot.accepting = state.accepting.load(std::memory_order_acquire);
    snapshot.workerPriorityRequested =
        state.workerPriorityRequested.load(std::memory_order_acquire);
    snapshot.workerPrioritySucceeded =
        state.workerPrioritySucceeded.load(std::memory_order_acquire);
    snapshot.submittedRoutine =
        state.submittedRoutine.load(std::memory_order_relaxed);
    snapshot.submittedCritical =
        state.submittedCritical.load(std::memory_order_relaxed);
    snapshot.submittedFlightLoopTiming =
        state.submittedFlightLoopTiming.load(std::memory_order_relaxed);
    snapshot.dequeued = state.dequeued.load(std::memory_order_relaxed);
    snapshot.dequeuedFlightLoopTiming =
        state.dequeuedFlightLoopTiming.load(std::memory_order_relaxed);
    snapshot.written = state.written.load(std::memory_order_relaxed);
    snapshot.writtenFlightLoopTiming =
        state.writtenFlightLoopTiming.load(std::memory_order_relaxed);
    snapshot.droppedContention =
        state.droppedContention.load(std::memory_order_relaxed);
    snapshot.droppedRoutineContention =
        state.droppedRoutineContention.load(std::memory_order_relaxed);
    snapshot.droppedCriticalContention =
        state.droppedCriticalContention.load(std::memory_order_relaxed);
    snapshot.droppedRoutineFull =
        state.droppedRoutineFull.load(std::memory_order_relaxed);
    snapshot.droppedCriticalFull =
        state.droppedCriticalFull.load(std::memory_order_relaxed);
    snapshot.droppedFlightLoopTimingFull =
        state.droppedFlightLoopTimingFull.load(std::memory_order_relaxed);
    snapshot.rejectedNotRunning =
        state.rejectedNotRunning.load(std::memory_order_relaxed);
    snapshot.rejectedFlightLoopTimingNotRunning =
        state.rejectedFlightLoopTimingNotRunning.load(
            std::memory_order_relaxed);
    snapshot.formattingFailures =
        state.formattingFailures.load(std::memory_order_relaxed);
    snapshot.storageFailures =
        state.storageFailures.load(std::memory_order_relaxed);
    snapshot.maximumQueueDepth =
        state.maximumQueueDepth.load(std::memory_order_relaxed);
    snapshot.maximumFlightLoopTimingQueueDepth =
        state.maximumFlightLoopTimingQueueDepth.load(
            std::memory_order_relaxed);
    snapshot.maximumProducerMicroseconds =
        state.maximumProducerMicroseconds.load(std::memory_order_relaxed);
    snapshot.maximumFlightLoopTimingProducerMicroseconds =
        state.maximumFlightLoopTimingProducerMicroseconds.load(
            std::memory_order_relaxed);
    snapshot.workerThreadIdentity =
        state.workerThreadIdentity.load(std::memory_order_acquire);
    return snapshot;
}

bool AsyncDiagnosticsWriter::WaitUntilIdleForTesting(
    std::chrono::milliseconds timeout) {
    auto& state = *implementation_;
    std::unique_lock<std::mutex> lock(state.queueMutex);
    return state.idleChanged.wait_for(lock, timeout, [&state]() {
        return state.routineQueue.Empty() && state.criticalQueue.Empty() &&
               state.flightLoopTimingQueue.Empty() &&
               !state.recordInFlight;
    });
}

}  // namespace xvatsim::modules::runtime_workers
