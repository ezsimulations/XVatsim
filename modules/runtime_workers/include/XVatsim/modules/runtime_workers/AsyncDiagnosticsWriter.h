#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <string>

namespace xvatsim::modules::runtime_workers {

enum class DiagnosticsImportance {
    Routine = 0,
    Critical,
};

struct DiagnosticsWriterOptions {
    std::filesystem::path logDirectory;
    std::size_t routineQueueCapacity = 960;
    std::size_t criticalQueueCapacity = 64;
    std::size_t flightLoopTimingQueueCapacity = 4096;
    int retainedDateLogCount = 3;
    std::chrono::milliseconds artificialWorkerDelayForTesting{0};
    std::size_t artificialWorkerDelayRecordCountForTesting =
        std::numeric_limits<std::size_t>::max();
    std::chrono::milliseconds artificialQueueLockHoldForTesting{0};
    bool forceStorageFailureForTesting = false;
};

struct FlightLoopTimingRecord {
    std::uint64_t sequence = 0;
    std::uint64_t completeBeforeTelemetryUs = 0;
    std::uint64_t refreshUs = 0;
    std::uint64_t diagnosticsSubmissionUs = 0;
    bool accessoryPath = false;
    bool activeCadence = false;
};

struct DiagnosticsWriterSnapshot {
    bool running = false;
    bool accepting = false;
    bool workerPriorityRequested = false;
    bool workerPrioritySucceeded = false;
    std::uint64_t submittedRoutine = 0;
    std::uint64_t submittedCritical = 0;
    std::uint64_t submittedFlightLoopTiming = 0;
    std::uint64_t dequeued = 0;
    std::uint64_t dequeuedFlightLoopTiming = 0;
    std::uint64_t written = 0;
    std::uint64_t writtenFlightLoopTiming = 0;
    std::uint64_t droppedContention = 0;
    std::uint64_t droppedRoutineContention = 0;
    std::uint64_t droppedCriticalContention = 0;
    std::uint64_t droppedRoutineFull = 0;
    std::uint64_t droppedCriticalFull = 0;
    std::uint64_t droppedFlightLoopTimingFull = 0;
    std::uint64_t rejectedNotRunning = 0;
    std::uint64_t rejectedFlightLoopTimingNotRunning = 0;
    std::uint64_t formattingFailures = 0;
    std::uint64_t storageFailures = 0;
    std::uint64_t maximumQueueDepth = 0;
    std::uint64_t maximumFlightLoopTimingQueueDepth = 0;
    std::uint64_t maximumProducerMicroseconds = 0;
    std::uint64_t maximumFlightLoopTimingProducerMicroseconds = 0;
    std::uint64_t workerThreadIdentity = 0;
};

class AsyncDiagnosticsWriter {
public:
    using DeferredFormatter = std::function<std::string()>;

    AsyncDiagnosticsWriter();
    ~AsyncDiagnosticsWriter();
    AsyncDiagnosticsWriter(const AsyncDiagnosticsWriter&) = delete;
    AsyncDiagnosticsWriter& operator=(const AsyncDiagnosticsWriter&) = delete;

    bool Start(DiagnosticsWriterOptions options);
    void Stop();

    bool TryEnqueueLine(
        std::string line,
        DiagnosticsImportance importance = DiagnosticsImportance::Routine);
    bool TryEnqueueDeferred(
        DeferredFormatter formatter,
        DiagnosticsImportance importance = DiagnosticsImportance::Routine);
    bool TryEnqueueFlightLoopTiming(
        const FlightLoopTimingRecord& timing) noexcept;

    DiagnosticsWriterSnapshot Snapshot() const;
    bool WaitUntilIdleForTesting(std::chrono::milliseconds timeout);

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

}  // namespace xvatsim::modules::runtime_workers
