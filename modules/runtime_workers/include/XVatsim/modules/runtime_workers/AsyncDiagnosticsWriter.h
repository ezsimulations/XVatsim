#pragma once

#include <chrono>
#include <array>
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
    std::size_t routeEvidenceQueueCapacity = 1024;
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

enum class RouteEvidenceRecordType : std::uint8_t {
    Dispatch = 0,
    Terminal,
    Disposition,
    Lifecycle,
};

enum class RouteEvidenceWorkKind : std::uint8_t {
    None = 0,
    RoutePreparation,
    ExpandedFmsObservation,
};

enum class RouteEvidenceDisposition : std::uint8_t {
    None = 0,
    Published,
    Observed,
    Rejected,
    Cancelled,
};

struct RouteEvidenceRecord {
    std::uint64_t sequence = 0;
    RouteEvidenceRecordType recordType = RouteEvidenceRecordType::Dispatch;
    RouteEvidenceWorkKind workKind = RouteEvidenceWorkKind::None;
    RouteEvidenceDisposition disposition = RouteEvidenceDisposition::None;
    std::uint64_t requestId = 0;
    std::uint64_t lifecycleEpoch = 0;
    std::uint64_t networkDigest = 0;
    std::uint64_t sourceIdentity = 0;
    std::uint64_t preflightIdentity = 0;
    std::uint64_t policyIdentity = 0;
    std::uint64_t anchorDigest = 0;
    std::uint64_t fmsObservationIdentity = 0;
    std::uint64_t resultDigest = 0;
    std::uint64_t completeDesiredPackageSubmitUs = 0;
    std::uint64_t mailboxExchangeUs = 0;
    std::uint64_t maximumPendingDepth = 0;
    std::uint64_t workerWallUs = 0;
    std::uint64_t workerCpuUs = 0;
    bool workerCpuAvailable = false;
    bool running = false;
    bool pending = false;
    bool completed = false;
    bool cancelled = false;
    bool failed = false;
    bool rebuildDecisionApplicable = false;
    bool rebuildDecision = false;
    std::int64_t sourceUs = 0;
    std::int64_t navigationUs = 0;
    std::int64_t preflightUs = 0;
    std::int64_t fmsUs = 0;
    std::int64_t procedureUs = 0;
    std::int64_t grammarUs = 0;
    std::int64_t waypointUs = 0;
    std::int64_t traversalPreparationUs = 0;
    std::int64_t polygonUs = 0;
    std::int64_t prefixUs = 0;
    std::int64_t finalizeUs = 0;
    std::array<char, 96> planKey{};
    std::array<char, 128> reason{};
};

struct DiagnosticsWriterSnapshot {
    bool running = false;
    bool accepting = false;
    bool workerPriorityRequested = false;
    bool workerPrioritySucceeded = false;
    std::uint64_t submittedRoutine = 0;
    std::uint64_t submittedCritical = 0;
    std::uint64_t submittedFlightLoopTiming = 0;
    std::uint64_t submittedRouteEvidence = 0;
    std::uint64_t dequeued = 0;
    std::uint64_t dequeuedFlightLoopTiming = 0;
    std::uint64_t dequeuedRouteEvidence = 0;
    std::uint64_t written = 0;
    std::uint64_t writtenFlightLoopTiming = 0;
    std::uint64_t writtenRouteEvidence = 0;
    std::uint64_t droppedContention = 0;
    std::uint64_t droppedRoutineContention = 0;
    std::uint64_t droppedCriticalContention = 0;
    std::uint64_t droppedRoutineFull = 0;
    std::uint64_t droppedCriticalFull = 0;
    std::uint64_t droppedFlightLoopTimingFull = 0;
    std::uint64_t droppedRouteEvidenceFull = 0;
    std::uint64_t rejectedNotRunning = 0;
    std::uint64_t rejectedFlightLoopTimingNotRunning = 0;
    std::uint64_t rejectedRouteEvidenceNotRunning = 0;
    std::uint64_t formattingFailures = 0;
    std::uint64_t storageFailures = 0;
    std::uint64_t maximumQueueDepth = 0;
    std::uint64_t maximumFlightLoopTimingQueueDepth = 0;
    std::uint64_t maximumRouteEvidenceQueueDepth = 0;
    std::uint64_t maximumProducerMicroseconds = 0;
    std::uint64_t maximumFlightLoopTimingProducerMicroseconds = 0;
    std::uint64_t maximumRouteEvidenceProducerMicroseconds = 0;
    std::uint64_t lastSubmittedRouteEvidenceSequence = 0;
    std::uint64_t lastWrittenRouteEvidenceSequence = 0;
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
    bool TryEnqueueRouteEvidence(RouteEvidenceRecord evidence) noexcept;

    DiagnosticsWriterSnapshot Snapshot() const;
    bool WaitUntilIdle(std::chrono::milliseconds timeout);
    bool WaitUntilIdleForTesting(std::chrono::milliseconds timeout);

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

}  // namespace xvatsim::modules::runtime_workers
