#pragma once

#include "XVatsim/brain/BrainPdcLogTypes.h"
#include "XVatsim/brain/BrainTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xvatsim::brain {

struct BrainOwnedRuntimeState;

enum class BrainPdcAvailability {
    Uninitialized,
    SourceNotReady,
    SourceUnavailable,
    QualifiedIdle,
    Available,
};

enum class BrainPdcClassification {
    ControllerPdc,
    ControllerPrivateOperational,
    PilotPrivate,
    VatsimSystemOrSupervisor,
    UnknownDirectPrivate,
};

enum class BrainPdcSenderEvidenceKind {
    Unknown,
    Controller,
    Pilot,
    AuthoritativeStaff,
};

struct BrainPdcSenderEvidence {
    std::string callsign;
    BrainPdcSenderEvidenceKind kind = BrainPdcSenderEvidenceKind::Unknown;
    StationRole controllerRole = StationRole::Other;
    bool relevantDepartureAuthority = false;
};

struct BrainPdcEvaluationContext {
    bool flightContextActive = false;
    bool ifrMode = true;
    WorkflowStage workflowStage = WorkflowStage::None;
    std::string aircraftCallsign;
    std::string departureIcao;
    std::string destinationIcao;
    bool rosterAvailable = false;
    bool rosterStale = true;
    bool rosterComplete = false;
    std::vector<BrainPdcSenderEvidence> senders;
};

struct BrainPdcCapturedArtifact {
    std::uint64_t productLifecycleEpoch = 0;
    std::uint64_t sourceEpoch = 0;
    std::int64_t sourceSequence = 0;
    std::uint64_t contentDigest = 0;
    std::uint64_t acceptedMonotonicMicroseconds = 0;
    std::string flightIdentity;
    std::string aircraftCallsign;
    std::string departureIcao;
    std::string destinationIcao;
    std::string revisionIdentity;
    std::string sender;
    std::string normalizedSender;
    std::string body;
    std::string acceptanceReason;
    std::string sourceTimeUtc;
    bool unread = true;
};

struct BrainPdcRuntimeCounters {
    std::uint64_t acquisitionDecisions = 0;
    std::uint64_t payloadReadsPermitted = 0;
    std::uint64_t payloadReadsStopped = 0;
    std::uint64_t observationsEvaluated = 0;
    std::uint64_t observationsRejectedMechanical = 0;
    std::uint64_t stableConnectedObservations = 0;
    std::uint64_t stableDisconnectedObservations = 0;
    std::uint64_t transitionalObservations = 0;
    std::uint64_t callsignAmbiguousObservations = 0;
    std::uint64_t provisionalRetained = 0;
    std::uint64_t provisionalDuplicates = 0;
    std::uint64_t provisionalAdmitted = 0;
    std::uint64_t connectedDuplicates = 0;
    std::uint64_t classifiedCandidates = 0;
    std::uint64_t admittedMessages = 0;
    std::uint64_t rejectedMessages = 0;
    std::uint64_t postCaptureEarlyNoops = 0;
    std::uint64_t gapEvents = 0;
    std::uint64_t rollbackEvents = 0;
    std::uint64_t sourceEpochChanges = 0;
    std::uint64_t capacityLosses = 0;
    std::uint64_t historyEvictions = 0;
    std::uint64_t unreadAcknowledged = 0;
    std::uint64_t stalePublicationFacts = 0;
    std::uint64_t semanticMutations = 0;
    std::uint64_t logRequestsSubmitted = 0;
    std::uint64_t logFactsConsumed = 0;
    std::uint64_t logBytesRead = 0;
    std::uint64_t logWorkerWallMicroseconds = 0;
    std::uint64_t logWorkerCpuMicroseconds = 0;
    std::uint64_t logIgnoredProtocolLines = 0;
    std::uint64_t logExcludedRecords = 0;
    std::uint64_t logMalformedRecords = 0;
    std::uint64_t logStaleFacts = 0;
    std::uint64_t unreadEvictions = 0;
};

struct BrainPdcLogReplayCursor {
    BrainPdcLogFileFact file;
    std::uint64_t highWaterOffset = 0;
};

struct BrainPdcLogMonitorState {
    bool connectionKnown = false;
    bool connected = false;
    bool recoveryRequired = true;
    bool readingSnapshot = false;
    bool committingStagedMessages = false;
    bool sessionOpen = false;
    bool discardLeadingFragment = false;
    std::uint64_t connectionEpoch = 1;
    std::uint64_t fileIncarnation = 1;
    std::uint64_t nextRequestId = 0;
    std::uint64_t pendingRequestId = 0;
    std::uint64_t nextServiceMicroseconds = 0;
    std::uint64_t cursorOffset = 0;
    std::uint64_t snapshotEndOffset = 0;
    std::uint64_t sessionOpenOffset = 0;
    unsigned retryStep = 0;
    std::string sessionCallsign;
    std::string reason = "waiting-for-xpilot-connection";
    BrainPdcLogFileFact file;
    std::vector<BrainPdcLogRecordFact> stagedDirectMessages;
    std::size_t stagedCommitIndex = 0;
    std::array<BrainPdcLogReplayCursor, 4> replayCursors;
    std::size_t nextReplayCursorSlot = 0;
};

struct BrainPdcRuntimeState {
    bool initialized = false;
    bool sourceQualified = false;
    bool sourceAvailable = false;
    bool pluginAdminSuspended = false;
    bool acquisitionArmed = false;
    bool acquisitionClosed = false;
    bool captureComplete = false;
    bool preCaptureUncertain = false;
    bool hasObservedPositiveSequence = false;
    bool hasConnectedDisposition = false;
    BrainPdcAvailability availability = BrainPdcAvailability::Uninitialized;
    std::uint64_t productLifecycleEpoch = 1;
    std::uint64_t sourceEpoch = 1;
    std::uint64_t pluginInstanceIdentity = 0;
    std::uint64_t capabilityGeneration = 0;
    std::uint64_t semanticGeneration = 0;
    std::int64_t lastObservedSequence = 0;
    std::int64_t connectedDispositionedSequence = 0;
    std::string boundPlanIdentity;
    std::string boundFlightIdentity;
    std::string boundCallsign;
    std::string boundDepartureIcao;
    std::string boundDestinationIcao;
    std::optional<BrainPdcCapturedArtifact> capturedArtifact;
    std::vector<BrainPdcCapturedArtifact> olderArtifacts;
    BrainPdcLogMonitorState logMonitor;
    std::size_t retainedBytes = 0;
    BrainPdcRuntimeCounters counters;
};

struct BrainPdcAcquisitionDecision {
    bool evaluated = false;
    bool acquisitionArmed = false;
    bool contextReady = false;
    bool samplePrivatePayload = false;
    bool captureComplete = false;
    bool clearQueuedFacts = false;
    bool useSequenceOnlyFastPath = false;
    std::uint64_t pluginInstanceIdentity = 0;
    std::uint64_t capabilityGeneration = 0;
    std::int64_t brainOwnedSequence = 0;
    std::string reason;
};

struct BrainPdcObservationDecision {
    bool evaluated = false;
    bool mechanicallyAccepted = false;
    bool sourceStateChanged = false;
    bool provisionalRetained = false;
    bool duplicate = false;
    bool gapDetected = false;
    bool productChanged = false;
    bool presentationChanged = false;
    bool admitted = false;
    bool captureCompleted = false;
    bool classificationRejected = false;
    std::size_t admittedCount = 0;
    std::string reason;
};

void InitializeBrainOwnedPdcRuntime(BrainOwnedRuntimeState* state);
void ResetBrainOwnedPdcProductState(
    BrainOwnedRuntimeState* state,
    bool preserveConnectedReplayEvidence);
void StopBrainOwnedPdcRuntime(BrainOwnedRuntimeState* state);
void SuspendBrainOwnedPdcRuntime(BrainOwnedRuntimeState* state);
void ResumeBrainOwnedPdcRuntime(BrainOwnedRuntimeState* state);
void MarkBrainOwnedPdcSourceUnavailable(BrainOwnedRuntimeState* state);
void RecordBrainOwnedPdcTransportCapacityLoss(
    BrainOwnedRuntimeState* state,
    std::uint64_t lostObservationCount);

BrainPdcAcquisitionDecision EvaluateBrainOwnedPdcAcquisition(
    BrainOwnedRuntimeState* state,
    const BrainPdcEvaluationContext& context);

BrainPdcObservationDecision EvaluateBrainOwnedPdcObservation(
    BrainOwnedRuntimeState* state,
    const BrainPdcMechanicalObservation& observation,
    const BrainPdcEvaluationContext& context,
    std::uint64_t nowMicroseconds);

bool ReevaluateBrainOwnedPdcPendingContext(
    BrainOwnedRuntimeState* state,
    const BrainPdcEvaluationContext& context,
    std::uint64_t nowMicroseconds);

bool AcknowledgeBrainOwnedPdcVisibleEntries(
    BrainOwnedRuntimeState* state,
    const std::vector<std::string>& visibleRevisionIdentities);

bool AcknowledgeBrainOwnedPdcAllEntries(BrainOwnedRuntimeState* state);

std::size_t BrainOwnedPdcUnreadCount(const BrainPdcRuntimeState& state);
std::size_t BrainOwnedPdcMessageCount(const BrainPdcRuntimeState& state);
bool AdmitBrainOwnedPdcMessage(
    BrainOwnedRuntimeState* state,
    std::string sender,
    std::string body,
    std::string revisionIdentity,
    std::int64_t sourceSequence,
    std::string sourceTimeUtc,
    std::uint64_t nowMicroseconds);
std::string BrainOwnedPdcDiagnosticSummary(const BrainPdcRuntimeState& state);
const char* ToString(BrainPdcAvailability availability);
const char* ToString(BrainPdcClassification classification);

struct BrainPdcLogContext {
    bool flightContextActive = false;
    bool xpilotConnected = false;
    std::string_view flightCallsign;
    std::string_view xpilotCallsign;
    std::string_view departureIcao;
    std::string_view destinationIcao;
};

struct BrainPdcLogServiceResult {
    bool factConsumed = false;
    bool requestSubmitted = false;
    bool presentationChanged = false;
    BrainPdcLogIssue issue = BrainPdcLogIssue::None;
    std::string reason;
};

BrainPdcLogServiceResult ServiceBrainOwnedPdcLogMonitor(
    BrainOwnedRuntimeState* state,
    const BrainPdcLogContext& context,
    BrainPdcLogTransport* transport,
    std::uint64_t nowMicroseconds);

}  // namespace xvatsim::brain
