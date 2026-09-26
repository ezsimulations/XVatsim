#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "XVatsim/brain/BrainDisplayIntent.h"
#include "XVatsim/brain/BrainPdcRuntime.h"
#include "XVatsim/brain/BrainXPilot4BridgeRuntime.h"
#include "XVatsim/brain/BrainTypes.h"
#include "XVatsim/brain/BrainWorkflow.h"
#include "XVatsim/brain/PhaseSnapshotPublisher.h"
#include "XVatsim/brain/RadioReachableSnapshot.h"

namespace xvatsim::brain {

enum class BrainOwnedCandidateDecision {
    Pending,
    Accepted,
    Rejected,
    NeedsVerification,
};

enum class BrainOwnedDisplayOverrideMode {
    Auto,
    ForcedOpen,
    ForcedSleep,
};

enum class BrainOwnedOperatingMode {
    IFR,
    VFR,
};

enum class BrainOwnedOperatingModeSource {
    Default,
    SettingsStore,
    PilotMenu,
};

enum class BrainOwnedOperatingModeLoadStatus {
    Missing,
    Valid,
    Invalid,
    Unavailable,
};

enum class BrainOwnedOperationalActivationReason {
    Operational,
    AircraftStateInvalid,
    BatteryOff,
    XPilotDisconnected,
};

enum class BrainOwnedOperationalServiceStage : std::size_t {
    VatsimFeed,
    ControllerSnapshot,
    FlightPlan,
    NetworkPlan,
    Radio,
    PdcPrivateSource,
    Atis,
    Metar,
    Ctaf,
    Route,
    Authority,
    ControllerRelevance,
    WorkflowPublication,
    StandbyAssist,
    Count,
};

constexpr std::size_t kBrainOwnedOperationalServiceStageCount =
    static_cast<std::size_t>(BrainOwnedOperationalServiceStage::Count);

struct BrainOwnedOperationalActivationInput {
    bool aircraftStateValid = false;
    bool batteryOn = false;
    bool xPilotConnected = false;
};

struct BrainOwnedOperationalActivationDecision {
    BrainOwnedOperationalActivationReason reason =
        BrainOwnedOperationalActivationReason::AircraftStateInvalid;
    bool operational = false;
    bool initialObservation = false;
    bool activationRisingEdge = false;
    bool deactivationFallingEdge = false;
    bool disconnectFallingEdge = false;
};

struct BrainOwnedOperationalActivationState {
    bool initialized = false;
    bool operational = false;
    bool enableImmediateWakePending = false;
    std::uint64_t callbacks = 0;
    std::uint64_t dormantCallbacks = 0;
    std::uint64_t operationalCallbacks = 0;
    std::uint64_t enableImmediateWakeRequests = 0;
    std::uint64_t enableImmediateCallbacks = 0;
    std::uint64_t enableImmediateOperationalCallbacks = 0;
    std::uint64_t enableImmediateDormantCallbacks = 0;
    std::uint64_t activationRisingEdges = 0;
    std::uint64_t deactivationFallingEdges = 0;
    std::uint64_t disconnectFallingEdges = 0;
    std::uint64_t dormantOperationalAttemptCount = 0;
    std::array<std::uint64_t, kBrainOwnedOperationalServiceStageCount>
        operationalServiceCalls{};
    std::array<std::uint64_t, kBrainOwnedOperationalServiceStageCount>
        dormantOperationalAttempts{};
};

struct BrainOwnedOperationalRefreshGateInput {
    long long monotonicMs = 0;
    long long settledRefreshIntervalMs = 1000;
    std::uint64_t vatsimGeneration = 0;
    std::uint64_t controllerContentDigest = 0;
    std::uint64_t radioIdentity = 0;
    std::uint64_t presentationIdentity = 0;
    std::uint64_t pdcSemanticGeneration = 0;
    bool aircraftOnGround = false;
    bool activationRisingEdge = false;
    bool routeWorkerRunning = false;
    bool authorityWorkerRunning = false;
    bool forceRefresh = false;
};

struct BrainOwnedOperationalRefreshGateDecision {
    bool shouldRunFullRefresh = true;
    bool settledFastPath = false;
    std::string reason;
};

struct BrainOwnedOperationalRefreshGateState {
    bool initialized = false;
    long long lastFullRefreshMonotonicMs = 0;
    std::uint64_t lastVatsimGeneration = 0;
    std::uint64_t lastControllerContentDigest = 0;
    std::uint64_t lastRadioIdentity = 0;
    std::uint64_t lastPresentationIdentity = 0;
    std::uint64_t lastPdcSemanticGeneration = 0;
    bool lastAircraftOnGround = false;
    std::uint64_t fullRefreshCount = 0;
    std::uint64_t settledFastPathCount = 0;
    std::string lastDecisionReason;
};

struct BrainOwnedOperatingModeState {
    BrainOwnedOperatingMode mode = BrainOwnedOperatingMode::IFR;
    BrainOwnedOperatingModeSource source =
        BrainOwnedOperatingModeSource::Default;
    std::string reason = "missing-setting";
    std::uint64_t generation = 0;
};

struct BrainOwnedOperatingModeInitializationInput {
    BrainOwnedOperatingModeLoadStatus loadStatus =
        BrainOwnedOperatingModeLoadStatus::Missing;
    BrainOwnedOperatingMode storedMode = BrainOwnedOperatingMode::IFR;
};

struct BrainOwnedOperatingModeSelectionResult {
    BrainOwnedOperatingMode previousMode = BrainOwnedOperatingMode::IFR;
    BrainOwnedOperatingMode requestedMode = BrainOwnedOperatingMode::IFR;
    BrainOwnedOperatingMode effectiveMode = BrainOwnedOperatingMode::IFR;
    BrainOwnedOperatingModeSource stateSource =
        BrainOwnedOperatingModeSource::Default;
    std::string stateReason;
    BrainOwnedOperatingModeSource requestSource =
        BrainOwnedOperatingModeSource::PilotMenu;
    std::string requestReason;
    std::uint64_t generation = 0;
    bool changed = false;
    bool persistenceRequested = false;
};

enum class BrainOwnedTextEntryMode {
    None,
    ManualCtaf,
    DiversionAirport,
    MetarAirportLookup,
    AtisAirportLookup,
};

enum class BrainOwnedAccessoryOperationStatus {
    Unavailable,
    Available,
};

enum class BrainOwnedAccessoryDrawerId {
    None,
    Metar,
    Atis,
    Pdc,
};

enum class BrainOwnedAccessoryDrawerAction {
    None,
    Opened,
    Closed,
    Switched,
    DuplicateRequestIgnored,
};

struct BrainOwnedAccessoryPresentationSnapshot;

struct BrainOwnedAccessoryHistoryEntryInput {
    BrainOwnedAccessoryDrawerId drawer = BrainOwnedAccessoryDrawerId::None;
    std::string stableKey;
    std::string title;
    std::string body;
    bool chronological = false;
    std::int64_t chronologyKey = 0;
};

struct BrainOwnedAccessoryHistoryEntry {
    std::string stableKey;
    std::string title;
    std::string body;
    std::uint64_t acceptedSequence = 0;
    bool chronological = false;
    std::int64_t chronologyKey = 0;
    std::array<std::uint8_t, 32> sourceContentDigest{};
    std::size_t retainedBytes = 0;
    bool contentLimited = false;
};

struct BrainOwnedAccessoryHistory {
    std::vector<BrainOwnedAccessoryHistoryEntry> entries;
    std::size_t retainedBytes = 0;
    std::uint64_t generation = 0;
    std::uint64_t nextAcceptedSequence = 1;
};

struct BrainOwnedAccessoryRuntimeState {
    std::uint64_t lifecycleEpoch = 1;
    BrainOwnedAccessoryDrawerId activeDrawer = BrainOwnedAccessoryDrawerId::None;
    std::uint64_t selectionGeneration = 0;
    std::uint64_t scrollResetGeneration = 0;
    std::uint64_t lastConsumedClickSequence = 0;
    std::uint64_t pendingPresentationClickSequence = 0;
    std::uint64_t pendingPresentationClickAcceptedMicroseconds = 0;
    std::uint64_t pendingPresentationMouseCallbackExitedMicroseconds = 0;
    std::string callsignIdentity;
    std::uint64_t callsignIdentityGeneration = 0;
    std::uint64_t historyClearGeneration = 0;
    std::array<BrainOwnedAccessoryHistory, 3> histories;
    std::shared_ptr<const BrainOwnedAccessoryPresentationSnapshot>
        cachedPresentationSnapshot;
    std::uint64_t semanticPresentationGeneration = 1;
    std::uint64_t cachedSemanticPresentationGeneration = 0;
    std::uint64_t nextPresentationSnapshotIdentity = 1;
    std::uint64_t nextPresentationCommandIdentity = 1;
    std::uint64_t railPresentationRevision = 1;
    std::array<std::uint64_t, 3> drawerContentRevisions{1, 1, 1};
    std::uint32_t visibleInvalidationBatchDepth = 0;
    std::array<bool, 3> pendingDrawerContentInvalidations{};
    bool pendingSelectionInvalidation = false;
    bool pendingLifecycleInvalidation = false;
    std::uint64_t publicationFactsConsumed = 0;
    std::uint64_t publicationFactsRejected = 0;
    std::uint64_t publicationLivenessFailureCount = 0;
    std::uint64_t maximumPublicationElapsedMicroseconds = 0;
    std::uint64_t lastTerminalPresentationCommandIdentity = 0;
    std::uint64_t lastVisiblePublicationAttemptIdentity = 0;
    std::uint64_t publicationOrderingLifecycleEpoch = 1;
    std::uint64_t maximumVisiblePublicationMicroseconds = 0;
    std::uint64_t maximumCancellationMicroseconds = 0;
};

struct BrainOwnedAccessoryHistoryDecision {
    BrainOwnedAccessoryOperationStatus status =
        BrainOwnedAccessoryOperationStatus::Unavailable;
    bool accepted = false;
    bool duplicate = false;
    bool contentLimited = false;
    std::uint64_t acceptedSequence = 0;
    std::uint64_t historyGeneration = 0;
};

struct BrainOwnedAccessorySelectionRequest {
    BrainOwnedAccessoryDrawerId drawer = BrainOwnedAccessoryDrawerId::None;
    std::uint64_t requestSequence = 0;
    std::uint64_t clickAcceptedMicroseconds = 0;
    std::uint64_t mouseCallbackEnteredMicroseconds = 0;
    std::uint64_t mouseCallbackExitedMicroseconds = 0;
};

struct BrainOwnedAccessorySelectionDecision {
    BrainOwnedAccessoryOperationStatus status =
        BrainOwnedAccessoryOperationStatus::Unavailable;
    BrainOwnedAccessoryDrawerAction action =
        BrainOwnedAccessoryDrawerAction::None;
    BrainOwnedAccessoryDrawerId previousDrawer =
        BrainOwnedAccessoryDrawerId::None;
    BrainOwnedAccessoryDrawerId activeDrawer =
        BrainOwnedAccessoryDrawerId::None;
    std::uint64_t selectionGeneration = 0;
    std::uint64_t scrollResetGeneration = 0;
    std::uint64_t requestSequence = 0;
    std::uint64_t clickAcceptedMicroseconds = 0;
    std::uint64_t mouseCallbackEnteredMicroseconds = 0;
    std::uint64_t mouseCallbackExitedMicroseconds = 0;
};

struct BrainOwnedAccessoryBoundaryDecision {
    BrainOwnedAccessoryOperationStatus status =
        BrainOwnedAccessoryOperationStatus::Unavailable;
    bool drawerClosed = false;
    bool historiesCleared = false;
    bool clearedBeforeIdentityProjection = false;
    std::string previousCallsign;
    std::string activeCallsign;
    std::uint64_t historyClearGeneration = 0;
    std::uint64_t callsignIdentityGeneration = 0;
};

struct BrainOwnedAccessoryOrbPresentation {
    BrainOwnedAccessoryDrawerId drawer = BrainOwnedAccessoryDrawerId::None;
    std::string label;
    bool neutral = true;
    bool selected = false;
    std::string selectedIndicator;
    std::string airportIcao;
    std::string categoryText;
    std::string stateText;
    enum class Tone {
        Neutral,
        Green,
        Blue,
        Red,
        Magenta,
        Gray,
        Cyan,
        Amber,
    } tone = Tone::Neutral;
};

enum class BrainOwnedAccessoryDrawerState {
    Ready,
    Empty,
    Loading,
    Unavailable,
};

struct BrainOwnedAccessoryPresentationSnapshot {
    BrainOwnedAccessoryOperationStatus status =
        BrainOwnedAccessoryOperationStatus::Unavailable;
    BrainOwnedAccessoryDrawerId activeDrawer =
        BrainOwnedAccessoryDrawerId::None;
    std::vector<BrainOwnedAccessoryOrbPresentation> orbs;
    std::vector<BrainOwnedAccessoryHistoryEntry> entries;
    BrainOwnedAccessoryDrawerState drawerState =
        BrainOwnedAccessoryDrawerState::Unavailable;
    std::string drawerTitle;
    std::string emptyStateText;
    std::string drawerStateText;
    std::string drawerFinalMarker;
    std::uint64_t selectionGeneration = 0;
    std::uint64_t historyGeneration = 0;
    std::uint64_t contentGeneration = 0;
    std::uint64_t snapshotIdentity = 0;
    std::uint64_t commandIdentity = 0;
    std::uint64_t lifecycleEpoch = 0;
    std::uint64_t railPresentationRevision = 0;
    std::uint64_t selectedDrawerContentRevision = 0;
    std::uint64_t scrollResetGeneration = 0;
    std::uint64_t originatingClickSequence = 0;
    std::uint64_t originatingClickAcceptedMicroseconds = 0;
    std::uint64_t originatingMouseCallbackExitedMicroseconds = 0;
    std::array<std::uint64_t, 3> drawerHistoryGenerations{};
    std::string callsignIdentity;
    std::string atisVisibleRevisionIdentity;
};

struct BrainOwnedAccessoryProjectionCounters {
    std::uint64_t historyVisits = 0;
    std::uint64_t entriesCopied = 0;
    std::uint64_t snapshotBuilds = 0;
};

struct BrainOwnedAccessoryPresentationHandle {
    std::shared_ptr<const BrainOwnedAccessoryPresentationSnapshot> snapshot;
};

enum class BrainOwnedAccessoryPublicationDisposition {
    Committed,
    CommittedNoVisibleFrameRequired,
    FirstFrameDisplayed,
    SupersededBeforeCommit,
    SupersededAfterCommitBeforeFirstFrame,
    PublicationFailed,
    LifecycleCancelled,
};

enum class BrainOwnedAccessoryPublicationFailureStage {
    None,
    Preparation,
    Commit,
    Rasterization,
    TextureUpload,
    PostCommit,
};

struct BrainOwnedAccessoryPublicationFact {
    std::uint64_t commandIdentity = 0;
    std::uint64_t lifecycleEpoch = 0;
    std::uint64_t appliedCommandIdentity = 0;
    std::uint64_t originatingClickSequence = 0;
    std::uint64_t originatingClickAcceptedMicroseconds = 0;
    std::uint64_t originatingMouseCallbackExitedMicroseconds = 0;
    BrainOwnedAccessoryPublicationDisposition disposition =
        BrainOwnedAccessoryPublicationDisposition::PublicationFailed;
    BrainOwnedAccessoryPublicationFailureStage failureStage =
        BrainOwnedAccessoryPublicationFailureStage::None;
    std::uint64_t appliedRailRevision = 0;
    std::uint64_t appliedDrawerRevision = 0;
    BrainOwnedAccessoryDrawerId activeDrawerRendered =
        BrainOwnedAccessoryDrawerId::None;
    std::uint64_t preparationElapsedMicroseconds = 0;
    std::uint64_t commitElapsedMicroseconds = 0;
    std::uint64_t firstFrameElapsedMicroseconds = 0;
    std::uint64_t commandElapsedMicroseconds = 0;
    std::uint64_t clickToTerminalMicroseconds = 0;
    std::uint64_t issueToCommitMicroseconds = 0;
    std::uint64_t visibleEligibilityToFirstFrameMicroseconds = 0;
    std::uint64_t intentionallyHiddenMicroseconds = 0;
    std::uint64_t issueToCancellationMicroseconds = 0;
    std::uint64_t visiblePublicationAttemptIdentity = 0;
    std::uint64_t visibilityEpoch = 0;
    std::uint64_t snapshotScrollResetGeneration = 0;
    std::uint64_t previouslyAppliedScrollResetGeneration = 0;
    int drawerOffsetBeforeCommit = 0;
    int drawerOffsetAfterCommit = 0;
    int firstVisibleLine = 0;
    bool clickTimingApplicable = false;
    bool visibilityTimingApplicable = false;
    bool intentionallyHiddenTimingApplicable = false;
    bool cancellationTimingApplicable = false;
    bool scrollResetApplied = false;
    bool firstVisibleLineApplicable = false;
    bool commandTerminal = true;
    std::string mechanicalFailureReason;
    std::string atisVisibleRevisionIdentity;
    std::vector<std::string> pdcVisibleRevisionIdentities;
};

struct BrainOwnedAccessoryPublicationDecision {
    bool consumed = false;
    bool terminal = false;
    bool staleEpoch = false;
    bool commandTerminalAccepted = false;
    bool visibleAttemptTerminalAccepted = false;
    bool combinedTerminalAccepted = false;
    std::string reason;
};

enum class BrainMetarRequestPurpose {
    PrimaryTarget,
    PilotLookup,
    PrimaryRefresh,
};

enum class BrainMetarWorkerStatus {
    None,
    Pending,
    Success,
    Cancelled,
    InvalidRequest,
    TransportFailure,
    HttpFailure,
    PayloadRejected,
    JsonRejected,
    WrongStation,
};

enum class BrainMetarTransportStage {
    None,
    Startup,
    SendStart,
    SendCompletion,
    ReceiveStart,
    ResponseHeaders,
    HttpStatus,
    DataAvailability,
    Read,
    PayloadValidation,
    JsonValidation,
    StationValidation,
    Completed,
};

enum class BrainMetarWinHttpOperation {
    None,
    CreateEventHandle,
    OpenSession,
    ConfigureTimeouts,
    Connect,
    OpenRequest,
    ConfigureRedirects,
    RegisterCallback,
    SendRequest,
    ReceiveResponse,
    QueryHeaders,
    QueryDataAvailable,
    ReadData,
    CloseRequest,
};

enum BrainMetarTransportProgress : std::uint32_t {
    BrainMetarTransportProgressNone = 0,
    BrainMetarSendCompletionObserved = 1U << 0,
    BrainMetarResponseHeadersReceived = 1U << 1,
    BrainMetarHttp200Accepted = 1U << 2,
    BrainMetarPayloadReadComplete = 1U << 3,
    BrainMetarJsonDecoded = 1U << 4,
};

enum class BrainMetarFlightCategory {
    Unknown,
    Vfr,
    Mvfr,
    Ifr,
    Lifr,
};

enum class BrainMetarSourceHealth {
    Unknown,
    Pending,
    Healthy,
    Failed,
};

enum class BrainMetarVisibleState {
    Unknown,
    Pending,
    Fresh,
    Cached,
    Stale,
    Unavailable,
};

enum class BrainMetarTransientPresentation {
    None,
    LookupPending,
    LookupSpotlight,
    LookupFailure,
};

struct BrainMetarWorkerRequest {
    std::string airportIcao;
    std::uint64_t commandIdentity = 0;
    std::uint64_t lifecycleEpoch = 0;
    std::uint64_t requestId = 0;
    BrainMetarRequestPurpose purpose = BrainMetarRequestPurpose::PrimaryRefresh;
    std::uint64_t primaryGeneration = 0;
    std::uint64_t lookupGeneration = 0;
    long long dispatchedMonotonicMs = 0;
};

enum class BrainMetarDecodeStatus {
    NotAttempted,
    Decoded,
    MalformedJson,
    RootTypeMismatch,
    ResourceFailure,
};

enum class BrainMetarDecodedFieldType {
    Missing,
    String,
    Null,
    Boolean,
    Number,
    Array,
    Object,
};

struct BrainMetarWorkerFact {
    BrainMetarWorkerStatus status = BrainMetarWorkerStatus::None;
    BrainMetarWorkerRequest request;
    std::string stationIcao;
    std::string rawMetar;
    BrainMetarDecodeStatus decodeStatus =
        BrainMetarDecodeStatus::NotAttempted;
    std::size_t reportCardinality = 0;
    bool reportCardinalityLimitExceeded = false;
    BrainMetarDecodedFieldType stationFieldType =
        BrainMetarDecodedFieldType::Missing;
    BrainMetarDecodedFieldType metarFieldType =
        BrainMetarDecodedFieldType::Missing;
    bool stationFieldMissing = true;
    bool metarFieldMissing = true;
    bool stationFieldMalformed = false;
    bool metarFieldMalformed = false;
    bool stationByteLimitExceeded = false;
    bool rawMetarByteLimitExceeded = false;
    bool rawMetarContainsNul = false;
    bool rawMetarHasLeadingWhitespace = false;
    bool rawMetarHasTrailingWhitespace = false;
    std::uint64_t decodingElapsedMicroseconds = 0;
    std::string decodeDiagnostic;
    int httpStatus = 0;
    long long completedMonotonicMs = 0;
    long long networkElapsedUs = 0;
    std::size_t payloadBytes = 0;
    BrainMetarTransportStage terminalStage = BrainMetarTransportStage::None;
    BrainMetarWinHttpOperation winHttpOperation =
        BrainMetarWinHttpOperation::None;
    std::uint64_t winHttpResult = 0;
    std::uint32_t winHttpError = 0;
    std::uint32_t transportProgress = BrainMetarTransportProgressNone;
    std::string diagnostic;
    std::string source = "VATSIM_METAR";
};

struct BrainMetarDispatchDiagnostic {
    bool available = false;
    BrainMetarWorkerRequest request;
};

struct BrainMetarTerminalDiagnostic {
    bool available = false;
    BrainMetarWorkerRequest request;
    std::string stationIcao;
    BrainMetarWorkerStatus status = BrainMetarWorkerStatus::None;
    BrainMetarTransportStage terminalStage = BrainMetarTransportStage::None;
    BrainMetarWinHttpOperation winHttpOperation =
        BrainMetarWinHttpOperation::None;
    std::uint64_t winHttpResult = 0;
    std::uint32_t winHttpError = 0;
    std::uint32_t transportProgress = BrainMetarTransportProgressNone;
    int httpStatus = 0;
    std::size_t payloadBytes = 0;
    BrainMetarDecodeStatus decodeStatus =
        BrainMetarDecodeStatus::NotAttempted;
    std::size_t reportCardinality = 0;
    std::uint64_t decodingElapsedMicroseconds = 0;
    std::string decodeDiagnostic;
    long long networkElapsedUs = 0;
    long long completedMonotonicMs = 0;
    std::string diagnostic;
    std::string source;
};

struct BrainMetarDispositionDiagnostic {
    bool available = false;
    std::uint64_t requestId = 0;
    std::string airportIcao;
    bool accepted = false;
    bool parsingAttempted = false;
    std::string parserReason;
    std::uint64_t parserElapsedMicroseconds = 0;
    bool parserRanOnSimulatorFlightLoopHarvestPath = false;
    BrainMetarFlightCategory acceptedCategory =
        BrainMetarFlightCategory::Unknown;
    bool historyMutated = false;
    bool presentationChanged = false;
    std::string reason;
};

struct BrainMetarWorkerShutdownSnapshot {
    long long lastShutdownLatencyMs = 0;
    bool running = false;
    bool handlesClosed = true;
    bool callbacksClosed = true;
    bool terminalFactDrained = false;
    BrainMetarTerminalDiagnostic terminalDiagnostic;
    BrainMetarDispositionDiagnostic dispositionDiagnostic;
};

class BrainMetarWorker {
public:
    virtual ~BrainMetarWorker() = default;
    virtual bool Start(const BrainMetarWorkerRequest& request) = 0;
    virtual bool TryHarvest(BrainMetarWorkerFact* fact) = 0;
    virtual bool IsRunning() const = 0;
    virtual void CancelAndJoin() = 0;
    virtual BrainMetarWorkerShutdownSnapshot ShutdownSnapshot() const = 0;
};

struct BrainMetarParsedObservation {
    bool valid = false;
    std::string stationIcao;
    std::string rawMetar;
    bool speci = false;
    std::int64_t observationUnixSeconds = 0;
    int observationDay = 0;
    int observationHour = 0;
    int observationMinute = 0;
    bool visibilityKnown = false;
    double visibilitySm = 0.0;
    bool ceilingKnown = false;
    bool noCeilingProven = false;
    int ceilingFeet = 0;
    BrainMetarFlightCategory category = BrainMetarFlightCategory::Unknown;
    std::string diagnostic;
};

struct BrainOwnedMetarRuntimeState {
    bool initialized = false;
    std::uint64_t lifecycleEpoch = 1;
    bool dispatchSuspendedForDisconnect = false;
    BrainOwnedOperatingMode lastOperatingMode = BrainOwnedOperatingMode::IFR;
    WorkflowStage lastWorkflowStage = WorkflowStage::None;
    bool targetContextInitialized = false;
    std::string targetContextKey;
    bool primaryLatchedFromVfr = false;
    std::string primaryAirportIcao;
    std::uint64_t primaryGeneration = 0;
    BrainMetarParsedObservation primaryObservation;
    std::uint64_t primaryContentFingerprint = 0;
    BrainMetarSourceHealth sourceHealth = BrainMetarSourceHealth::Unknown;
    BrainMetarVisibleState visibleState = BrainMetarVisibleState::Unknown;
    int consecutiveFailures = 0;
    long long lastSuccessMonotonicMs = 0;
    long long lastFailureMonotonicMs = 0;
    long long nextPrimaryEligibleMonotonicMs = 0;
    long long freshUntilMonotonicMs = 0;
    long long primaryContentAcceptedMonotonicMs = 0;
    long long nextVisibleAgeBucketMonotonicMs = 0;
    int visibleFetchAgeMinutes = 0;
    std::uint64_t nextRequestId = 1;
    std::optional<BrainMetarWorkerRequest> activeRequest;
    std::optional<BrainMetarWorkerFact> deferredDisconnectedFact;
    std::string pendingLookupIcao;
    std::uint64_t lookupGeneration = 0;
    bool lookupAwaitingDispatch = false;
    bool lookupInvalidated = false;
    BrainMetarParsedObservation lookupObservation;
    std::uint64_t lookupContentFingerprint = 0;
    bool lookupTimedOut = false;
    BrainMetarTransientPresentation transientPresentation =
        BrainMetarTransientPresentation::None;
    long long transientDeadlineMonotonicMs = 0;
    std::uint64_t lookupPresentationSelectionGeneration = 0;
    bool lookupPresentationOwnershipValid = false;
    std::uint64_t contentGeneration = 0;
    std::uint64_t sourceHealthGeneration = 0;
    std::uint64_t freshnessGeneration = 0;
    std::uint64_t presentationGeneration = 0;
    std::uint64_t parseCount = 0;
    std::uint64_t fingerprintCount = 0;
    std::uint64_t historyMutationCount = 0;
    std::uint64_t requestDispatchCount = 0;
    std::uint64_t completionAcceptedCount = 0;
    std::uint64_t completionRejectedCount = 0;
    long long lastAcceptedNetworkElapsedUs = 0;
    long long lastSimulatorThreadCycleUs = 0;
    long long maximumSimulatorThreadCycleUs = 0;
};

enum class BrainAtisServiceRole {
    Unknown,
    Departure,
    Arrival,
    Combined,
};

enum class BrainAtisAvailability {
    Idle,
    Available,
    ConfirmedUnavailable,
    SourceUnknown,
};

enum class BrainAtisTransientPresentation {
    None,
    LookupPending,
    LookupSpotlight,
    LookupUnavailable,
    LookupSourceUnknown,
};

struct BrainAtisRevision {
    bool valid = false;
    std::string airportIcao;
    BrainAtisServiceRole serviceRole = BrainAtisServiceRole::Unknown;
    std::string normalizedCallsign;
    std::string normalizedFrequency;
    std::string normalizedInformationCode;
    std::vector<std::string> textLines;
    std::string validSourceTime;
    std::string revisionIdentity;
    std::string deterministicOrderKey;
};

struct BrainOwnedAtisRuntimeState {
    bool initialized = false;
    BrainAtisAvailability availability = BrainAtisAvailability::Idle;
    WorkflowStage workflowStage = WorkflowStage::None;
    std::string automaticAirportIcao;
    BrainAtisRevision primaryRevision;
    std::vector<std::string> unreadRevisionIdentities;
    std::string pendingLookupIcao;
    std::uint64_t lookupGeneration = 0;
    BrainAtisTransientPresentation transientPresentation =
        BrainAtisTransientPresentation::None;
    long long transientDeadlineMonotonicMs = 0;
    std::uint64_t lookupPresentationSelectionGeneration = 0;
    bool lookupPresentationOwnershipValid = false;
    std::vector<BrainAtisRevision> lookupRevisions;
    std::uint64_t lastFeedGeneration = 0;
    bool lastFeedHasCache = false;
    bool lastFeedStale = true;
    bool lastFeedRootPresent = false;
    bool lastFeedRootArray = false;
    bool lastFeedComponentComplete = false;
    std::uint32_t lastFeedMechanicalIssueMask = 0;
    std::string lastEvaluationKey;
    std::uint64_t evaluationCount = 0;
    std::uint64_t examinedRecordCount = 0;
    std::uint64_t candidateCount = 0;
    std::uint64_t semanticChangeCount = 0;
    std::uint64_t historyMutationCount = 0;
    std::uint64_t unreadCreatedCount = 0;
    std::uint64_t unreadAcknowledgedCount = 0;
    std::uint64_t lookupAcceptedCount = 0;
    std::uint64_t lookupRejectedCount = 0;
    std::uint64_t sourceUnknownCount = 0;
    std::uint64_t confirmedUnavailableCount = 0;
    std::uint64_t maximumEvaluationMicroseconds = 0;
};

struct BrainOwnedAtisCycleInput {
    bool pluginEnabled = true;
    bool xpilotConnected = false;
    WorkflowStage workflowStage = WorkflowStage::None;
    BrainOwnedOperatingMode operatingMode = BrainOwnedOperatingMode::IFR;
    workflow::FlightContext flightContext;
    bool feedHasCache = false;
    bool feedStale = true;
    bool feedFetchInProgress = false;
    std::uint64_t feedGeneration = 0;
    bool atisRootPresent = false;
    bool atisRootArray = false;
    bool atisComponentComplete = false;
    std::uint32_t atisMechanicalIssueMask = 0;
    const std::vector<BrainRawVatsimAtisRecord>* atisRecords = nullptr;
    long long monotonicMs = 0;
};

struct BrainOwnedAtisCycleDecision {
    bool evaluated = false;
    bool semanticChanged = false;
    bool historyMutated = false;
    bool unreadCreated = false;
    bool lookupCompleted = false;
    bool lookupExpired = false;
    bool ownershipLost = false;
    std::string targetAirportIcao;
    BrainAtisAvailability availability = BrainAtisAvailability::Idle;
    std::string selectedRevisionIdentity;
    std::string reason;
    std::uint64_t feedGeneration = 0;
    std::uint64_t examinedRecords = 0;
    std::uint64_t candidates = 0;
    std::uint64_t evaluationMicroseconds = 0;
};

struct BrainOwnedTextEntryFact {
    BrainOwnedTextEntryMode mode = BrainOwnedTextEntryMode::None;
    std::string text;
    long long monotonicMs = 0;
};

struct BrainOwnedTextEntryDecision {
    bool accepted = false;
    bool presentationChanged = false;
    std::string normalizedText;
    std::string reason;
};

struct BrainOwnedAsyncWorkerBindings {
    BrainMetarWorker* metar = nullptr;
    BrainXPilot4Transport* xpilot4 = nullptr;
};

struct BrainOwnedPluginAdminLifecycleDecision {
    BrainOwnedAccessoryOperationStatus status =
        BrainOwnedAccessoryOperationStatus::Unavailable;
    bool stateChanged = false;
    bool suspended = false;
    bool resumed = false;
    std::uint64_t suspensionGeneration = 0;
    BrainOwnedAccessoryBoundaryDecision accessory;
    BrainMetarWorkerShutdownSnapshot workerShutdown;
};

struct BrainOwnedAsyncFactCycleInput {
    bool pluginEnabled = true;
    bool xpilotConnected = false;
    WorkflowStage workflowStage = WorkflowStage::None;
    BrainOwnedOperatingMode operatingMode = BrainOwnedOperatingMode::IFR;
    workflow::FlightContext flightContext;
    FlightPlanSnapshot flightPlan;
    long long monotonicMs = 0;
    std::int64_t utcUnixSeconds = 0;
};

struct BrainOwnedAsyncFactCycleOutput {
    bool requestDispatched = false;
    bool completionAccepted = false;
    bool completionRejected = false;
    bool presentationChanged = false;
    bool contentChanged = false;
    bool sourceHealthChanged = false;
    bool freshnessChanged = false;
    long long acceptedNetworkElapsedUs = 0;
    long long simulatorThreadElapsedUs = 0;
    std::string reason;
    BrainMetarDispatchDiagnostic dispatchDiagnostic;
    BrainMetarTerminalDiagnostic terminalDiagnostic;
    BrainMetarDispositionDiagnostic dispositionDiagnostic;
};

struct BrainOwnedCandidateCompletion {
    std::uint64_t radioBoardHash = 0;
    std::uint64_t routePolygonHash = 0;
    WorkflowStage workflowStage = WorkflowStage::None;
    int currentPolygonIndex = 0;
    std::string currentPolygonKey;
    std::string matchedPolygonKey;
    std::string callsign;
    std::string frequency;
    RadioReachableFacilityGroup facilityGroup = RadioReachableFacilityGroup::Other;
    DisplayRelation displayRelation = DisplayRelation::Unknown;
    BrainOwnedCandidateDecision decision = BrainOwnedCandidateDecision::Pending;
    bool displayed = false;
    bool hasRouteEntryDistance = false;
    double routeEntryDistanceNm = 0.0;
    int positiveVotes = 0;
    int negativeVotes = 0;
    int neutralVotes = 0;
    std::vector<BrainControllerEvidenceVote> evidenceVotes;
    std::string reason;
    std::string stableKey;
};

struct BrainTerminalAuthorityWorkerInput {
    std::string airportIcao;
    bool hasAirportCoordinates = false;
    double airportLatitudeDeg = 0.0;
    double airportLongitudeDeg = 0.0;
    long long nowSeconds = 0;
};

struct BrainTerminalAuthorityWorkerOutput {
    bool available = false;
    bool pending = false;
    bool resolved = false;
    bool stale = false;
    std::string airportIcao;
    std::vector<std::string> ownerTokens;
    std::vector<std::string> polygonKeys;
    std::string source;
    std::string status;
    std::string cacheStatus;
    std::uint64_t sourceGeneration = 0;
    long long lookupUs = 0;
};

class BrainTerminalAuthorityWorker {
public:
    virtual ~BrainTerminalAuthorityWorker() = default;

    virtual BrainTerminalAuthorityWorkerOutput ResolveAirportTerminalOwner(
        const BrainTerminalAuthorityWorkerInput& input) = 0;
};

enum class BrainAirportFrequencyEndpoint {
    Unknown,
    Departure,
    Arrival,
};

struct BrainAirportFrequencyRecord {
    BrainAirportFrequencyEndpoint endpoint = BrainAirportFrequencyEndpoint::Unknown;
    std::string airportIcao;
    StationRole role = StationRole::Other;
    std::string frequency;
    std::string frequencyUse;
    std::string sectorization;
    std::string facility;
    std::string servicedFacility;
    std::string towerOrCommCall;
    std::string primaryApproachRadioCall;
};

struct BrainAirportFrequencyWorkerInput {
    std::string departureIcao;
    std::string arrivalIcao;
    long long nowSeconds = 0;
};

struct BrainAirportFrequencyWorkerOutput {
    bool available = false;
    bool pending = false;
    bool resolved = false;
    bool stale = false;
    std::string departureIcao;
    std::string arrivalIcao;
    std::vector<BrainAirportFrequencyRecord> departureFrequencies;
    std::vector<BrainAirportFrequencyRecord> arrivalFrequencies;
    std::string source;
    std::string status;
    std::string cacheStatus;
    std::uint64_t sourceGeneration = 0;
    long long lookupUs = 0;
};

class BrainAirportFrequencyWorker {
public:
    virtual ~BrainAirportFrequencyWorker() = default;

    virtual BrainAirportFrequencyWorkerOutput ResolveAirportFrequencies(
        const BrainAirportFrequencyWorkerInput& input) = 0;
};

struct BrainOwnedRuntimeState {
    BrainOwnedOperatingModeState operatingMode;
    BrainOwnedOperationalRefreshGateState operationalRefreshGate;
    BrainOwnedAccessoryRuntimeState accessory;
    BrainOwnedMetarRuntimeState metar;
    BrainOwnedAtisRuntimeState atis;
    BrainPdcRuntimeState pdc;
    BrainXPilot4BridgeRuntimeState xpilot4Bridge;
    bool hasRoutePolygonSnapshot = false;
    std::shared_ptr<const RouteSectorSnapshot> routePolygonSnapshot;
    std::uint64_t routePolygonHash = 0;
    std::uint64_t authorityRouteDigest = 0;
    std::shared_ptr<const RouteSectorSnapshot> authorityRouteSnapshot;
    long long lastRoutePolygonRefreshSeconds = 0;
    std::string routePlanKey;
    int currentPolygonIndex = 0;
    std::string currentPolygonKey;
    std::string nextPolygonKey;
    std::string arrivalPolygonKey;
    std::string finalRoutePolygonKey;
    double routeProgressDistanceNm = 0.0;
    std::string lastRoutePolygonTransitionReason;
    bool lastRoutePolygonTransitionChanged = false;

    bool hasRadioBoard = false;
    long long lastRadioBoardRefreshSeconds = 0;
    std::uint64_t lastControllerGeneration = 0;
    std::shared_ptr<const TransceiverResolutionSnapshot> transceiverSnapshot;
    std::shared_ptr<const AuthorityTransceiverEvidenceSnapshot>
        authorityTransceiverEvidence;
    std::uint64_t authorityTransceiverEvidenceDigest = 0;
    std::uint64_t transceiverObservationGeneration = 0;
    RadioReachableControllerSnapshot radioSnapshot;
    RadioReachableControllerSnapshot gatedRadioSnapshot;
    RadioReachableCandidateDiff radioDiff;
    bool hasDepartureTerminalAuthority = false;
    BrainTerminalAuthorityWorkerOutput departureTerminalAuthority;
    std::string departureTerminalAuthorityRequestKey;
    std::uint64_t departureTerminalAuthorityHash = 0;
    long long lastDepartureTerminalAuthorityLookupSeconds = 0;
    bool hasArrivalTerminalAuthority = false;
    BrainTerminalAuthorityWorkerOutput arrivalTerminalAuthority;
    std::string arrivalTerminalAuthorityRequestKey;
    std::uint64_t arrivalTerminalAuthorityHash = 0;
    long long lastArrivalTerminalAuthorityLookupSeconds = 0;
    bool hasAirportFrequencies = false;
    BrainAirportFrequencyWorkerOutput airportFrequencies;
    std::string airportFrequencyRequestKey;
    std::uint64_t airportFrequencyHash = 0;
    long long lastAirportFrequencyLookupSeconds = 0;

    ModuleBoardSnapshot departureBoardSnapshot;
    ModuleBoardSnapshot arrivalBoardSnapshot;
    ModuleBoardSnapshot enrouteBoardSnapshot;
    ModuleBoardSnapshot relevanceDepartureBoardSnapshot;
    ModuleBoardSnapshot relevanceArrivalBoardSnapshot;
    ModuleBoardSnapshot relevanceEnrouteBoardSnapshot;
    FinalDisplaySnapshot finalDisplaySnapshot;
    PhaseSnapshotPublisherState phaseSnapshotPublisherState;
    std::uint64_t lastDisplayIntentHash = 0;
    AircraftStateSnapshot lastAircraftStateSnapshot;
    PilotIdentitySnapshot lastPilotIdentitySnapshot;
    FlightPlanSnapshot lastFlightPlanSnapshot;
    NetworkPlanSnapshot lastNetworkPlanSnapshot;
    bool hasFlightPlanSnapshot = false;
    FlightPlanSnapshot flightPlanSnapshot;
    long long lastFlightPlanSampleSeconds = 0;
    bool hasActiveCruiseTarget = false;
    bool cruiseTargetManualOverride = false;
    bool cruiseAltitudeReachedThisFlight = false;
    double activeCruiseTargetFt = 0.0;
    double cruiseGateSatisfiedSinceSeconds = -1.0;
    std::string cruiseTargetSourceKey;
    std::string standbyAssistLatchKey;
    bool standbyAssistWriteConsumed = false;
    std::string diversionOverrideSourceKey;
    std::string preflightRouteCacheAppliedPlanKey;
    BrainOwnedDisplayOverrideMode displayOverrideMode =
        BrainOwnedDisplayOverrideMode::Auto;
    BrainOwnedTextEntryMode pendingTextEntryMode =
        BrainOwnedTextEntryMode::None;
    ManualQuerySnapshot manualQuerySnapshot;
    long long manualQueryVisibleUntilSeconds = 0;
    bool departureReleasedThisFlight = false;
    bool arrivalAwakeThisFlight = false;
    double airborneSinceSeconds = -1.0;
    bool sawXPilotConnectedThisFlight = false;
    workflow::FlightContext flightContext;
    workflow::XPilotSessionBoundaryState xPilotSessionBoundaryState;
    bool coldDarkResetApplied = false;
    bool aircraftStateInvalidBoundaryActive = false;
    bool pendingAutomaticFlightRecovery = false;
    bool manualFlightRecoveryRequested = false;
    bool pluginAdminSuspended = false;
    std::uint64_t pluginAdminSuspensionGeneration = 0;

    WorkflowStage lastWorkflowStage = WorkflowStage::None;
    std::string lastPlanKey;
    std::uint64_t lastRadioBoardHash = 0;
    std::uint64_t lastRoutePolygonHash = 0;
    std::uint64_t lastDepartureTerminalAuthorityHash = 0;
    std::uint64_t lastArrivalTerminalAuthorityHash = 0;
    std::uint64_t lastAirportFrequencyHash = 0;
    std::uint64_t lastAuthorityRelevanceHash = 0;
    std::uint64_t lastVnasTerminalEvidenceHash = 0;
    std::uint64_t lastTerminalRelevancePolicyHash = 0;
    std::uint64_t lastRadioTuningHash = 0;
    std::uint64_t lastXPilot4ControllerHash = 0;
    std::string lastWakeReason;
    std::string lastIdleReason;

    bool candidatesComplete = false;
    bool heavyFallbackRequested = false;
    bool heavyFallbackRunning = false;
    bool enrouteInitialHoldStarted = false;
    double enrouteInitialHoldUntilSeconds = -1.0;
    std::vector<BrainOwnedCandidateCompletion> candidateCompletions;
};

struct BrainOwnedBoardFilterOutput {
    ModuleBoardSnapshot board;
    int rejectedUnapprovedStations = 0;
};

struct BrainOwnedRadioBoardReuseInput {
    long long nowSeconds = 0;
    long long refreshIntervalSeconds = 0;
    std::uint64_t controllerGeneration = 0;
};

struct BrainOwnedRadioBoardReuseOutput {
    bool canReuse = false;
    RadioReachableControllerSnapshot radioSnapshot;
    std::string reason;
    std::string cacheStatus;
};

struct BrainOwnedRadioBoardCommitInput {
    long long nowSeconds = 0;
    std::uint64_t controllerGeneration = 0;
    TransceiverResolutionSnapshot transceiverSnapshot;
    RadioReachableControllerSnapshot radioSnapshot;
};

struct BrainOwnedRadioBoardCommitOutput {
    RadioReachableControllerSnapshot radioSnapshot;
    RadioReachableCandidateDiff diff;
    bool boardChanged = false;
    std::string reason;
    std::string cacheStatus;
};

struct BrainOwnedTerminalAuthorityRefreshInput {
    bool flightContextActive = false;
    std::string airportIcao;
    bool hasAirportCoordinates = false;
    double airportLatitudeDeg = 0.0;
    double airportLongitudeDeg = 0.0;
    long long nowSeconds = 0;
};

struct BrainOwnedTerminalAuthorityRefreshPlan {
    bool shouldRunWorker = false;
    BrainTerminalAuthorityWorkerInput workerInput;
    BrainTerminalAuthorityWorkerOutput cachedFact;
    std::string requestKey;
    std::string reason;
    std::string cacheStatus;
};

struct BrainOwnedAirportFrequencyRefreshInput {
    bool flightContextActive = false;
    std::string departureIcao;
    std::string arrivalIcao;
    long long nowSeconds = 0;
};

struct BrainOwnedAirportFrequencyRefreshPlan {
    bool shouldRunWorker = false;
    BrainAirportFrequencyWorkerInput workerInput;
    BrainAirportFrequencyWorkerOutput cachedFact;
    std::string requestKey;
    std::string reason;
    std::string cacheStatus;
};

struct BrainOwnedPublishedRuntimeInput {
    WorkflowStage workflowStage = WorkflowStage::None;
    std::string planKey;
    RadioReachableControllerSnapshot gatedRadioSnapshot;
    ModuleBoardSnapshot departureBoard;
    ModuleBoardSnapshot arrivalBoard;
    ModuleBoardSnapshot enrouteBoard;
    FinalDisplaySnapshot finalDisplay;
};

struct BrainOwnedOverlayWakeInput {
    AircraftStateSnapshot aircraftState;
    XPilotSessionSnapshot xPilotSession;
    WorkflowStage workflowStage = WorkflowStage::None;
    FinalDisplaySnapshot finalDisplay;
    BrainOwnedDisplayOverrideMode displayOverrideMode =
        BrainOwnedDisplayOverrideMode::Auto;
    bool manualQueryVisible = false;
    bool textEntryActive = false;
    bool sawXPilotConnectedThisFlight = false;
    bool enrouteInitialHoldActive = false;
};

struct BrainOwnedOverlayWakeDecision {
    bool shouldWake = false;
    bool hideUntilXpilotConnect = false;
    bool xPilotDisconnectedAlert = false;
    std::string reason;
};

struct BrainOwnedEnrouteInitialHoldInput {
    WorkflowStage workflowStage = WorkflowStage::None;
    double nowSeconds = 0.0;
    double holdSeconds = 0.0;
};

struct BrainOwnedEnrouteInitialHoldOutput {
    bool active = false;
    bool started = false;
    double holdUntilSeconds = -1.0;
};

struct BrainOwnedFlightPlanSampleInput {
    bool flightContextActive = false;
    long long nowSeconds = 0;
    long long sampleCadenceSeconds = 0;
};

struct BrainOwnedFlightPlanSampleDecision {
    bool shouldSample = true;
    FlightPlanSnapshot cachedSnapshot;
    std::string reason;
};

struct BrainOwnedFlightPlanSampleCommitInput {
    long long nowSeconds = 0;
    FlightPlanSnapshot snapshot;
};

struct BrainOwnedWorkflowSelectionInput {
    AircraftStateSnapshot aircraft;
    RadioStateSnapshot radios;
    RadioReachableControllerSnapshot radioSnapshot;
    std::string departureIcao;
    std::string arrivalIcao;
    BrainTerminalAuthorityWorkerOutput departureTerminalAuthority;
    double nowSeconds = 0.0;
    workflow::WorkflowTuning tuning;
};

struct BrainOwnedWorkflowSelectionOutput {
    workflow::HandoffDecision decision;
};

struct BrainOwnedCruiseTargetTuning {
    double gateToleranceFt = 1000.0;
    double stableVerticalSpeedFpm = 800.0;
    double gateDwellSeconds = 10.0;
};

struct BrainOwnedCruiseTargetPlanInput {
    bool flightContextActive = false;
    std::string planKey;
    NetworkPlanSnapshot networkPlan;
};

struct BrainOwnedCruiseTargetPlanOutput {
    bool changed = false;
    std::string logLine;
};

enum class BrainOwnedCruiseTargetCommand {
    CurrentAltitude,
    FiledAltitude,
};

struct BrainOwnedCruiseTargetCommandInput {
    BrainOwnedCruiseTargetCommand command =
        BrainOwnedCruiseTargetCommand::FiledAltitude;
    bool flightContextActive = false;
    std::string planKey;
    AircraftStateSnapshot aircraftState;
    NetworkPlanSnapshot networkPlan;
    double nowSeconds = 0.0;
    BrainOwnedCruiseTargetTuning tuning;
};

struct BrainOwnedCruiseTargetCommandOutput {
    bool accepted = false;
    bool changed = false;
    std::string statusLine;
};

struct BrainOwnedCruiseTargetProgressInput {
    AircraftStateSnapshot aircraftState;
    double nowSeconds = 0.0;
    BrainOwnedCruiseTargetTuning tuning;
};

struct BrainOwnedStandbyAssistAdvisoryCandidate {
    std::string sourceDecisionId;
    std::string sourceEvidenceId;
    std::string endpoint;
    std::string airportIcao;
    std::string advisoryDecision;
    std::string projectedRole;
    std::string projectedFrequency;
    bool acceptedByAdvisory = false;
    bool fallbackUsed = false;
    std::string sourceConfidence;
    std::string confidenceLevel;
    double positiveScore = 0.0;
    double negativeScore = 0.0;
    bool hardBlock = false;
    std::string hardBlockReason;
    std::string advisoryReason;
};

struct BrainOwnedStandbyRecommendationDecision {
    std::string standbyDecisionId;
    std::string subjectKey;
    std::string sourceDomain;
    std::string sourceDecisionId;
    std::string sourceEvidenceId;
    std::string endpoint;
    std::string airportIcao;
    std::string callsign;
    std::string role;
    std::string frequency;
    std::string workflowStage;
    std::string planKey;
    int boardIndex = -1;
    std::string displayRelation;
    bool candidateVisibleInFinalBoard = false;
    bool acceptedByAdvisory = false;
    std::string advisoryDecision;
    std::string sourceConfidence;
    std::string confidenceLevel;
    bool fallbackUsed = false;
    double positiveScore = 0.0;
    double negativeScore = 0.0;
    bool hardBlock = false;
    std::string hardBlockReason;
    bool alreadyCom1Active = false;
    bool alreadyCom2Active = false;
    bool alreadyCom1Standby = false;
    std::string targetCom;
    bool eligible = false;
    bool previewEligible = false;
    std::string previewRecommendation;
    std::string previewSkipReason;
    bool liveWriteEligible = false;
    bool productGateEnabled = false;
    bool directCtafLivePromotionAllowed = false;
    std::string livePromotionReason;
    std::string livePromotionBlockedReason;
    bool promotedFromDryRun = false;
    std::string actualSelectedTargetSource;
    std::string actualSelectedTargetFrequency;
    bool actualWriteEligible = false;
    bool noControllerTargetAvailable = false;
    bool controllerTargetPreserved = false;
    std::string featureGateRequired;
    bool featureGateSatisfied = false;
    std::string featureGateBlockedReason;
    bool dryRunLiveEligible = false;
    std::string dryRunLiveRecommendation;
    std::string dryRunSkipReason;
    std::string dryRunSafetyGate;
    bool dryRunWouldSelectTarget = false;
    bool dryRunWouldDisplaceControllerTarget = false;
    bool dryRunBlockedByExistingControllerTarget = false;
    bool dryRunBlockedByStandbyDisabled = false;
    bool dryRunBlockedByAlreadyCom1Standby = false;
    bool dryRunBlockedByFrequencyState = false;
    std::string dryRunTargetCom;
    std::string dryRunTargetFrequency;
    std::string dryRunPromotionClass;
    std::string advisoryProductGate;
    std::string advisoryWritePolicy;
    std::string advisoryFrequencyResolutionState;
    std::string advisoryCandidateType;
    std::string skipReason;
    std::string finalRecommendation;
};

struct BrainOwnedStandbyRecommendationSummary {
    int standbyEvidenceCount = 0;
    int standbyCandidateCount = 0;
    int advisoryCandidateCount = 0;
    int selectedTargetCount = 0;
    int writeDecisionCount = 0;
    int writeAttemptCount = 0;
    int writeSuccessCount = 0;
    int writeFailureCount = 0;
    int skippedEmptyFrequencyCount = 0;
    int skippedPendingLookupCount = 0;
    int skippedLookupFailedCount = 0;
    int skippedGuardFrequencyCount = 0;
    int skippedRoleNotEligibleCount = 0;
    int skippedAlreadyActiveCount = 0;
    int writerResultCount = 0;
    int writerSuccessCount = 0;
    int writerFailureCount = 0;
    int writerBlockedBeforeWriteCount = 0;
    int writerUnknownResultCount = 0;
    int writerDatarefMissingCount = 0;
    int writerDatarefNotWritableCount = 0;
    int writerInvalidFrequencyCount = 0;
    int writerNoTargetCount = 0;
    int writerNoWriteRequestedCount = 0;
    int writerControllerSourceCount = 0;
    int writerDirectCtafSourceCount = 0;
    bool standbyRecommendationsBrainOwned = true;
};

struct BrainOwnedStandbyAssistPlanInput {
    WorkflowStage workflowStage = WorkflowStage::None;
    std::string planKey;
    RadioStateSnapshot radios;
    bool standbyAssistEnabled = true;
    bool directCtafStandbyAssistEnabled = false;
    std::string directCtafGateSource = "unknown";
    FinalDisplaySnapshot board;
    std::vector<BrainOwnedStandbyAssistAdvisoryCandidate>
        ctafUnicomAdvisoryCandidates;
};

struct BrainOwnedStandbyAssistSettingsDiagnostics {
    bool standbyAssistEnabled = false;
    bool directCtafStandbyAssistEnabled = false;
    std::string directCtafGateSource = "unknown";
    bool directCtafGateEffective = false;
};

struct BrainOwnedStandbyAssistPlanOutput {
    bool hasTarget = false;
    WorkflowStage workflowStage = WorkflowStage::None;
    FinalDisplaySnapshot board;
    std::size_t targetStationIndex = 0;
    std::string targetFrequency;
    std::string latchKey;
    bool targetAlreadyInCom1Standby = false;
    std::string targetStandbyDecisionId;
    std::string targetAdvisorySourceDecisionId;
    std::string actualSelectedTargetSource = "none";
    std::string actualSelectedTargetFrequency;
    BrainOwnedStandbyAssistSettingsDiagnostics settingsDiagnostics;
    std::vector<BrainOwnedStandbyRecommendationDecision>
        standbyDecisions;
    BrainOwnedStandbyRecommendationSummary standbySummary;
};

struct BrainOwnedStandbyAssistWriterResult {
    bool writerResultKnown = false;
    std::string writerResultCode;
    std::string writerFailureReason;
    std::string writerFailureDomain;
    std::string writerInputFrequency;
    std::string writerNormalizedFrequency;
    std::string writerTargetCom;
    std::string writerDatarefName;
    bool writerDatarefAvailable = false;
    bool writerDatarefWritable = false;
    bool writerValidationPassed = false;
    bool writerWriteAttempted = false;
    bool writerWriteSucceeded = false;
    bool writerWriteBlockedBeforeSimWrite = false;
    bool writerWriteFailedAtSimLayer = false;
    std::string writerResultSource = "none";
    std::string writerResultDecisionId;
    std::string writerResultLinkedStandbyDecisionId;
};

struct BrainOwnedStandbyAssistSideEffectDecision {
    bool shouldWriteCom1Standby = false;
    bool standbyLoaded = false;
    std::string targetFrequency;
    std::string sideEffectDecisionId;
    std::string standbyDecisionId;
    bool standbyAssistEnabled = false;
    std::string latchKey;
    bool latchConsumed = false;
    bool writeAllowed = false;
    bool writeAttempted = false;
    bool writeSucceededKnown = false;
    bool writeSucceeded = false;
    std::string writerTarget;
    std::string failureReason;
    bool displayStandbyMarkerApplied = false;
    std::string actualSelectedTargetSource;
    std::string actualSelectedTargetFrequency;
    bool actualWriteEligible = false;
    bool actualWriteAttempted = false;
    bool actualWriteSucceededKnown = false;
    bool actualWriteSucceeded = false;
    BrainOwnedStandbyAssistWriterResult writerResult;
    BrainOwnedStandbyRecommendationSummary standbySummary;
};

struct BrainOwnedDiversionOverrideInput {
    bool hasOverride = false;
    std::string sourcePlanKey;
};

struct BrainOwnedDiversionOverrideDecision {
    bool useOverride = false;
    bool clearOverride = false;
    std::string logLine;
};

struct BrainOwnedPreflightRouteCacheInput {
    std::string planKey;
    bool hasCandidate = false;
};

struct BrainOwnedPreflightRouteCacheDecision {
    bool shouldClearRouteResolverCache = false;
    bool shouldValidateCandidate = false;
    std::string logLine;
};

struct BrainOwnedPreflightRouteCacheValidationInput {
    bool accepted = false;
    std::string reason;
};

struct BrainOwnedPreflightRouteCacheValidationDecision {
    bool shouldApplyRouteResolverCache = false;
    std::string logLine;
};

struct BrainOwnedCtafLookupFact {
    std::string airportIcao;
    bool resolved = false;
    bool available = false;
    std::string frequency;
    bool lookupAttempted = false;
    std::string lookupSkippedReason;
    bool cacheHit = false;
    bool fetchInProgress = false;
    bool requestSucceeded = false;
    std::string statusCodeClass;
    long long lastAttemptAgeSeconds = -1;
    int failureCount = 0;
    std::string pendingReason;
};

// CTAF/UNICOM lookup facts are source evidence only. The lookup layer reports
// what it knows; brain-owned advisory decisions below decide live row projection.
struct BrainOwnedCtafUnicomSourceEvidence {
    std::string evidenceId;
    std::string endpoint;
    std::string airportIcao;
    bool lookupAttempted = false;
    std::string lookupSkippedReason;
    bool cacheHit = false;
    bool fetchInProgress = false;
    bool requestSucceeded = false;
    std::string statusCodeClass;
    bool resolved = false;
    bool available = false;
    std::string frequency;
    long long lastAttemptAgeSeconds = -1;
    int failureCount = 0;
    bool fallbackEligible = false;
    std::string fallbackFrequency;
    std::string sourceConfidence;
    std::string sourceReason;
    std::string pendingReason;
};

// Compatibility/parity record for the legacy lookup-to-row projection. When
// source evidence exists, this vector is diagnostic only and is not live row
// authority.
struct BrainOwnedCtafUnicomProjectionEvidence {
    std::string projectionEvidenceId;
    std::string sourceEvidenceId;
    std::string endpoint;
    std::string airportIcao;
    std::string projectedRole;
    std::string projectedFrequency;
    bool fallbackUsed = false;
    bool unresolvedProjectedEmptyFrequency = false;
    int legacyRowRemovedCount = 0;
    int duplicateSuppressedCount = 0;
    bool diagnosticCompatibilityProjectionOnly = true;
    // Deprecated compatibility mirror retained for public/header consumers.
    // New diagnostics should use diagnosticCompatibilityProjectionOnly.
    bool completionBypassCompatibilityOnly = true;
    bool completionBypassRetired = true;
    bool completionBypassLiveAuthority = false;
    bool completionBypassDiagnosticOnly = true;
    bool legacyDiagnosticLiveRowEmitted = false;
    // Deprecated compatibility mirror retained for public/header consumers.
    // New diagnostics should use legacyDiagnosticLiveRowEmitted.
    bool liveRowEmitted = false;
};

// Summary for source and compatibility projection evidence. advisoryDecisionCount
// intentionally remains zero until CTAF/UNICOM gets live advisory completion
// records in a later migration step.
struct BrainOwnedCtafUnicomEvidenceSummary {
    int sourceEvidenceCount = 0;
    int projectionEvidenceCount = 0;
    int liveRowEmittedCount = 0;
    int diagnosticCompatibilityProjectionOnly = 0;
    // Deprecated compatibility mirror retained for public/header consumers.
    // New diagnostics should use diagnosticCompatibilityProjectionOnly.
    int completionBypassCompatibilityOnly = 0;
    int historicalCompatibilityRowCount = 0;
    int legacyDiagnosticLiveRowEmittedCount = 0;
    bool compatibilityRowsDiagnosticOnly = true;
    bool legacyBypassFieldsQuarantined = true;
    int advisoryDecisionCount = 0;
};

// Brain-owned advisory decision preview. These decisions are the live row source
// when evidence exists; matchesCurrentProjection proves parity with the
// compatibility projection path.
struct BrainOwnedCtafUnicomAdvisoryPreviewDecision {
    std::string advisoryDecisionId;
    std::string sourceEvidenceId;
    std::string endpoint;
    std::string airportIcao;
    std::string decision;
    std::string projectedRole;
    std::string projectedFrequency;
    bool fallbackUsed = false;
    std::string sourceConfidence;
    std::string confidenceLevel;
    double positiveScore = 0.0;
    double negativeScore = 0.0;
    bool hardBlock = false;
    std::string reason;
    bool wouldEmitLiveRow = false;
    bool matchesCurrentProjection = false;
};

// Diagnostic preview summary for CTAF/UNICOM advisory decisions.
struct BrainOwnedCtafUnicomAdvisoryPreviewSummary {
    int sourceEvidenceCount = 0;
    int projectionEvidenceCount = 0;
    int advisoryPreviewDecisionCount = 0;
    int previewWouldEmitLiveRowCount = 0;
    int previewMatchesCurrentProjectionCount = 0;
    int previewMismatchCount = 0;
    int diagnosticCompatibilityProjectionOnly = 0;
    // Deprecated compatibility mirror retained for public/header consumers.
    // New diagnostics should use diagnosticCompatibilityProjectionOnly.
    int completionBypassCompatibilityOnly = 0;
};

// Authority guardrail for CTAF/UNICOM advisory rows. liveRowsBrainOwned must be
// true whenever source evidence exists. The old completion bypass is retained
// only as diagnostic compatibility evidence.
struct BrainOwnedCtafUnicomAdvisoryAuthoritySummary {
    std::string advisoryAuthority;
    int sourceEvidenceCount = 0;
    int advisoryPreviewDecisionCount = 0;
    int liveAdvisoryRowCount = 0;
    int compatibilityProjectionCount = 0;
    int oldVsBrainMismatchCount = 0;
    int diagnosticCompatibilityProjectionOnly = 0;
    // Deprecated compatibility mirror retained for public/header consumers.
    // New diagnostics should use diagnosticCompatibilityProjectionOnly.
    int completionBypassCompatibilityOnly = 0;
    bool completionBypassRetired = true;
    int liveBypassAuthorityCount = 0;
    int diagnosticBypassRowCount = 0;
    int brainAdvisoryLiveRowCount = 0;
    int duplicateLiveRowCount = 0;
    bool bypassRetirementSafe = false;
    bool noLiveBypassAuthority = true;
    bool compatibilityRowsDiagnosticOnly = true;
    bool liveRowsBrainAdvisoryOwned = true;
    bool legacyBypassFieldsQuarantined = true;
    bool liveRowsBrainOwned = false;
};

struct BrainOwnedCtafUnicomBypassAuditDecision {
    std::string ctafUnicomBypassAuditDecisionId;
    std::string advisoryDecisionId;
    std::string sourceEvidenceId;
    std::string projectionEvidenceId;
    std::string endpoint;
    std::string airportIcao;
    std::string callsign;
    std::string role;
    std::string frequency;
    bool diagnosticCompatibilityWouldDisplay = false;
    bool bypassRequired = false;
    std::string diagnosticCompatibilityReason;
    std::string bypassReason;
    std::string advisoryAuthority;
    bool advisoryWouldEmitLiveRow = false;
    bool advisoryMatchesBypassRow = false;
    bool roleMatches = false;
    bool frequencyMatches = false;
    bool endpointMatches = false;
    bool airportMatches = false;
    bool visibilityMatches = false;
    bool bypassRowHasBrainEquivalent = false;
    bool brainRowHasBypassEquivalent = false;
    bool wouldRetireSafely = false;
    std::string retirementBlockedReason;
    bool diagnosticCompatibilityOnly = false;
    bool compatibilityOnly = false;
    std::string mismatchReason;
    bool missingAdvisoryDecision = false;
    bool missingSourceEvidence = false;
    bool pendingLookup = false;
    bool lookupFailed = false;
    bool emptyFrequency = false;
    bool unicomFallback = false;
    bool standbyConsumesAdvisoryDecision = false;
    bool standbyConsumesBypassRow = false;
    std::string retirementPolicy;
    std::string retirementPolicyReason;
    std::string retirementBlockerClass;
    bool retirementBlockerResolved = false;
    bool retirementStillBlocked = false;
    bool retirementSafeAfterPolicy = false;
    bool compatibilityDuplicateSuppressed = false;
    std::string duplicateSuppressionReason;
    bool nonDisplayableByPolicy = false;
    bool deferredByPolicy = false;
    bool failedLookupByPolicy = false;
    bool emptyFrequencyByPolicy = false;
    bool missingEvidenceByPolicy = false;
    bool wouldLoseFrequencyIfBypassRemoved = false;
    bool wouldLoseVisibilityIfBypassRemoved = false;
    bool safeToRemoveBypassAfterCleanup = false;
    bool completionBypassRetired = true;
    bool completionBypassLiveAuthority = false;
    bool completionBypassDiagnosticOnly = true;
    int retiredBypassCompatibilityRowCount = 0;
    bool bypassRetirementFallbackWarning = false;
    bool missingEvidenceWarningOnly = false;
    bool missingEvidenceFallbackPreserved = false;
    bool advisoryProjectionAuthority = false;
    std::string diagnosticLiveRowAuthority;
    std::string liveRowAuthority;
    std::string standbyAuthority;
    bool bypassRetirementRegressionSafe = false;
};

struct BrainOwnedCtafUnicomBypassAuditSummary {
    int bypassAuditDecisionCount = 0;
    int bypassRowCount = 0;
    int brainOwnedAdvisoryRowCount = 0;
    int matchingBrainEquivalentCount = 0;
    int missingBrainEquivalentCount = 0;
    int mismatchCount = 0;
    int safeToRetireCount = 0;
    int blockedRetirementCount = 0;
    int pendingLookupCount = 0;
    int lookupFailedCount = 0;
    int emptyFrequencyCount = 0;
    int unicomFallbackCount = 0;
    int standbyAdvisoryConsumerCount = 0;
    int standbyBypassConsumerCount = 0;
    int retirementPolicyDecisionCount = 0;
    int resolvedBlockerCount = 0;
    int stillBlockedCount = 0;
    int policyNonDisplayableCount = 0;
    int policyDeferredCount = 0;
    int policyFailedLookupCount = 0;
    int policyEmptyFrequencyCount = 0;
    int duplicateSuppressedCount = 0;
    int missingEvidencePolicyCount = 0;
    int wouldLoseFrequencyCount = 0;
    int wouldLoseVisibilityCount = 0;
    int bypassRemovalSafeCandidateCount = 0;
    int bypassRemovalStillUnsafeCount = 0;
    bool completionBypassRetired = true;
    int liveBypassAuthorityCount = 0;
    int diagnosticBypassRowCount = 0;
    int brainAdvisoryLiveRowCount = 0;
    int missingEvidenceWarningCount = 0;
    int compatibilityFallbackWarningCount = 0;
    int missingEvidenceFallbackWarningCount = 0;
    int duplicateLiveRowCount = 0;
    int pendingNonDisplayableCount = 0;
    int failedLookupNonDisplayableCount = 0;
    int emptyFrequencyNonDisplayableCount = 0;
    int retiredBypassCompatibilityRowCount = 0;
    bool bypassRetirementSafe = false;
    bool noLiveBypassAuthority = true;
    bool compatibilityRowsDiagnosticOnly = true;
    bool liveRowsBrainAdvisoryOwned = true;
    bool standbyRowsAdvisoryOwned = true;
    bool legacyBypassFieldsQuarantined = true;
    bool diagnosticCompatibilityProjectionOnly = false;
    bool completionBypassCompatibilityOnly = false;
    bool ctafUnicomBypassRetirementReady = false;
};

struct BrainOwnedCtafUnicomMissingEvidenceAuditDecision {
    std::string missingEvidenceAuditDecisionId;
    std::string missingEvidenceEndpoint;
    std::string missingEvidenceAirportIcao;
    std::string missingEvidenceRole;
    std::string missingEvidenceFrequency;
    std::string missingEvidenceCause;
    bool missingSourceEvidence = false;
    bool missingAdvisoryDecision = false;
    bool incompleteAdvisoryDecision = false;
    bool oldCompatibilityWouldDisplay = false;
    bool wouldLoseFrequency = false;
    bool wouldLoseVisibility = false;
    bool warningOnly = false;
    std::string warningReason;
    std::string recoveryHint;
    std::string warningLabel;
    bool liveAuthorityRestored = false;
    bool liveCompatibilityFallbackUsed = false;
    bool standbyConsumesWarning = false;
    bool standbyWriteBlockedByMissingEvidence = false;
    bool authorityInvariantPreserved = true;
    bool failSoftVisible = false;
    bool operatorActionRequired = false;
};

struct BrainOwnedCtafUnicomMissingEvidenceAuditSummary {
    int missingEvidenceAuditCount = 0;
    int missingSourceEvidenceCount = 0;
    int missingAdvisoryDecisionCount = 0;
    int incompleteAdvisoryDecisionCount = 0;
    int oldCompatibilityWouldDisplayCount = 0;
    int wouldLoseFrequencyCount = 0;
    int wouldLoseVisibilityCount = 0;
    int warningOnlyCount = 0;
    int liveAuthorityRestoredCount = 0;
    int liveCompatibilityFallbackUsedCount = 0;
    int standbyConsumesWarningCount = 0;
    int authorityInvariantPreservedCount = 0;
    int operatorActionRequiredCount = 0;
};

struct BrainOwnedCtafUnicomLegacyBypassAliasAuditDecision {
    std::string legacyAliasAuditId;
    std::string aliasName;
    std::string aliasLocation;
    std::string aliasCategory;
    std::string currentMeaning;
    std::string misleadingRisk;
    std::string recommendedAction;
    std::string migrationTarget;
    bool consumerKnown = false;
    std::string consumerRisk;
    bool canRenameNow = false;
    bool canRemoveNow = false;
    std::string removalBlockedReason;
    bool authorityInvariantProtected = true;
    bool liveAuthorityImplication = false;
    bool replacementFieldPresent = false;
    std::string replacementFieldName;
    bool legacyFieldStillPresent = false;
    bool replacementMatchesLegacy = false;
    bool harnessMigratedToReplacement = false;
    bool oldAliasDeprecated = false;
    bool safeToRemoveLegacyLater = false;
    bool replacementMigrationComplete = false;
    std::string replacementMismatchReason;
};

struct BrainOwnedCtafUnicomLegacyBypassAliasAuditSummary {
    int aliasAuditCount = 0;
    int renameNowCandidateCount = 0;
    int renameLaterCount = 0;
    int removeLaterCount = 0;
    int harnessOnlyAliasCount = 0;
    int reportOnlyAliasCount = 0;
    int publicConsumerRiskCount = 0;
    int unknownConsumerRiskCount = 0;
    int liveAuthorityMisleadingAliasCount = 0;
    int authorityInvariantProtectedCount = 0;
    int replacementFieldCount = 0;
    int legacyFieldStillPresentCount = 0;
    int replacementMatchesLegacyCount = 0;
    int replacementMismatchCount = 0;
    int harnessMigratedToReplacementCount = 0;
    int deprecatedAliasCount = 0;
    int safeToRemoveLegacyLaterCount = 0;
    int reportOnlyAliasRemovedCount = 0;
    int reportOnlyAliasRemovalSafeCount = 0;
    int reportOnlyAliasStillFoundCount = 0;
    bool replacementMigrationComplete = false;
};

struct BrainOwnedCtafUnicomPublicUnknownAliasConsumerAuditDecision {
    std::string consumerAliasName;
    std::string replacementName;
    std::string definitionLocation;
    std::string emissionLocation;
    int harnessUsageCount = 0;
    int reportUsageCount = 0;
    int runtimeUsageCount = 0;
    int docsUsageCount = 0;
    int pluginUsageCount = 0;
    bool externalConsumerRisk = false;
    bool unknownConsumerRisk = false;
    bool replacementEmittedSameScope = false;
    bool internalConsumersMigrated = false;
    bool aliasCompatibilityOnly = false;
    bool removalReadyLater = false;
    std::string removalBlockedReason;
    std::string nextMigrationAction;
};

struct BrainOwnedCtafUnicomPublicUnknownAliasConsumerAuditSummary {
    int publicUnknownAliasCount = 0;
    int replacementSameScopeCount = 0;
    int internalMigratedCount = 0;
    int compatibilityOnlyAliasCount = 0;
    int removalReadyLaterCount = 0;
    int removalBlockedCount = 0;
    int externalRiskCount = 0;
    int unknownRiskCount = 0;
    int runtimeUsageCount = 0;
    int harnessLegacyUsageCount = 0;
    int reportLegacyUsageCount = 0;
};

struct BrainOwnedCtafUnicomExternalAliasDeprecationDecision {
    std::string aliasName;
    std::string replacementName;
    std::string aliasRiskClass;
    std::string deprecationStatus;
    bool activeGeneratedAliasPresent = false;
    bool aliasRemovedFromActiveOutput = false;
    bool aliasDeprecated = false;
    bool canonicalReplacementPreferred = false;
    bool replacementUsedByHarness = false;
    bool replacementCarriesEquivalentMeaning = false;
    bool authorityInvariantProtected = true;
    bool liveAuthorityImplication = false;
    bool publicHeaderRiskAliasRetained = false;
    bool runtimeBehaviorChanged = false;
    std::string removalBlockedReason;
    std::string nextMigrationAction;
};

struct BrainOwnedCtafUnicomExternalAliasDeprecationSummary {
    int aliasDeprecationDecisionCount = 0;
    int externalRiskAliasCount = 0;
    int externalAliasDeprecatedCount = 0;
    int externalAliasRemovedCount = 0;
    int activeGeneratedAliasRetainedCount = 0;
    int canonicalReplacementPreferredCount = 0;
    int replacementEquivalentCount = 0;
    int publicHeaderRiskAliasRetainedCount = 0;
    bool liveRowEmittedRetained = false;
    bool completionBypassCompatibilityOnlyRetained = false;
    bool runtimeBehaviorChanged = false;
    bool noLiveBypassAuthority = true;
};

struct BrainOwnedCtafUnicomPublicHeaderAliasRiskClosureDecision {
    std::string publicHeaderAliasName;
    std::string deprecatedAliasName;
    std::string replacementName;
    std::string headerDefinitionLocation;
    std::string runtimeWriteLocation;
    std::string harnessOutputLocation;
    int harnessExpectationUsageCount = 0;
    int pluginUsageCount = 0;
    int moduleUsageCount = 0;
    int docsUsageCount = 0;
    int reportUsageCount = 0;
    bool replacementSameScope = false;
    bool replacementMatchesLegacy = false;
    bool compatibilityOnly = false;
    bool deprecatedPublicHeaderAliasRetained = false;
    bool deprecatedAliasStillEmitted = false;
    bool replacementPreferred = false;
    bool replacementMatchesDeprecatedAlias = false;
    bool canDeprecateNow = false;
    bool canRemoveLater = false;
    std::string removalBlockedReason;
    bool publicHeaderConsumerRisk = false;
    bool externalConsumerRisk = false;
    std::string recommendedAction;
    std::string nextMigrationStep;
};

struct BrainOwnedCtafUnicomPublicHeaderAliasRiskClosureSummary {
    int publicHeaderAliasCount = 0;
    int replacementSameScopeCount = 0;
    int replacementMatchesLegacyCount = 0;
    int compatibilityOnlyCount = 0;
    int deprecatedPublicHeaderAliasCount = 0;
    int deprecatedPublicHeaderAliasRetainedCount = 0;
    int deprecatedAliasReplacementMatchCount = 0;
    int deprecatedAliasReplacementMismatchCount = 0;
    int deprecatedAliasRemovalBlockedCount = 0;
    int canDeprecateNowCount = 0;
    int canRemoveLaterCount = 0;
    int removalBlockedCount = 0;
    int pluginUsageCount = 0;
    int moduleUsageCount = 0;
    int harnessLegacyUsageCount = 0;
    int publicHeaderRiskCount = 0;
    bool deprecatedAliasDocumentationPresent = false;
    bool publicHeaderCompatibilityWindowOpen = false;
    bool ctafUnicomAliasCleanupClosedExceptCompatibilityWindow = false;
};

struct BrainOwnedPublisherInput {
    WorkflowStage workflowStage = WorkflowStage::None;
    double routeProgressDistanceNm = 0.0;
    std::string currentPolygonKey;
    std::string nextPolygonKey;
    std::string arrivalPolygonKey;
    RadioStateSnapshot radios;
    ModuleBoardSnapshot departureBoard;
    ModuleBoardSnapshot arrivalBoard;
    ModuleBoardSnapshot enrouteBoard;
    std::vector<BrainOwnedCandidateCompletion> completions;
    bool hasDepartureCtafStation = false;
    BoardStationSnapshot departureCtafStation;
    bool hasArrivalCtafStation = false;
    BoardStationSnapshot arrivalCtafStation;
    std::vector<BrainOwnedCtafUnicomSourceEvidence> ctafUnicomSourceEvidence;
    bool omitDepartureCtafUnicomAdvisoryDecisionForDiagnostics = false;
    bool omitArrivalCtafUnicomAdvisoryDecisionForDiagnostics = false;
    bool incompleteDepartureCtafUnicomAdvisoryDecisionForDiagnostics = false;
    bool incompleteArrivalCtafUnicomAdvisoryDecisionForDiagnostics = false;
    bool verificationPending = false;
    std::string publishReason;
    std::string productPlanKey;
    std::string productPlanKeySource;
    std::string productPlanKeyMissingReason;
    bool sourceOwnedFallbackStableKeyShadowEnabled = false;
    std::string sourceOwnedFallbackStableKeyShadowGateSource = "default";
    bool sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled = false;
    std::string
        sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource =
            "default";
    bool sourceOwnedFallbackStableKeyLiveConsumptionEnabled = false;
    std::string sourceOwnedFallbackStableKeyLiveConsumptionGateSource =
        "default";
};

struct BrainOwnedPublisherFactInput {
    WorkflowStage workflowStage = WorkflowStage::None;
    RadioStateSnapshot radios;
    ModuleBoardSnapshot departureBoard;
    ModuleBoardSnapshot arrivalBoard;
    ModuleBoardSnapshot enrouteBoard;
    std::vector<BrainOwnedCandidateCompletion> completions;
    BrainOwnedCtafLookupFact departureCtaf;
    BrainOwnedCtafLookupFact arrivalCtaf;
    bool verificationPending = false;
    std::string publishReason;
    std::string productPlanKey;
    std::string productPlanKeySource;
    std::string productPlanKeyMissingReason;
    bool sourceOwnedFallbackStableKeyShadowEnabled = false;
    std::string sourceOwnedFallbackStableKeyShadowGateSource = "default";
    bool sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled = false;
    std::string
        sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource =
            "default";
    bool sourceOwnedFallbackStableKeyLiveConsumptionEnabled = false;
    std::string sourceOwnedFallbackStableKeyLiveConsumptionGateSource =
        "default";
};

struct BrainOwnedPublisherOutput {
    ModuleBoardSnapshot departureBoard;
    ModuleBoardSnapshot arrivalBoard;
    ModuleBoardSnapshot enrouteBoard;
    FinalDisplaySnapshot finalDisplay;
    BrainDisplayIntentOutput displayIntent;
    PhaseSnapshotPublishResult phasePublish;
    std::string phasePublisherStateSummary;
    std::vector<BrainOwnedCtafUnicomSourceEvidence> ctafUnicomSourceEvidence;
    std::vector<BrainOwnedCtafUnicomProjectionEvidence> ctafUnicomProjectionEvidence;
    BrainOwnedCtafUnicomEvidenceSummary ctafUnicomEvidenceSummary;
    std::vector<BrainOwnedCtafUnicomAdvisoryPreviewDecision>
        ctafUnicomAdvisoryPreviewDecisions;
    std::vector<BrainOwnedStandbyAssistAdvisoryCandidate>
        ctafUnicomStandbyAdvisoryCandidates;
    BrainOwnedCtafUnicomAdvisoryPreviewSummary
        ctafUnicomAdvisoryPreviewSummary;
    BrainOwnedCtafUnicomAdvisoryAuthoritySummary
        ctafUnicomAdvisoryAuthoritySummary;
    std::vector<BrainOwnedCtafUnicomBypassAuditDecision>
        ctafUnicomBypassAuditDecisions;
    BrainOwnedCtafUnicomBypassAuditSummary
        ctafUnicomBypassAuditSummary;
    std::vector<BrainOwnedCtafUnicomMissingEvidenceAuditDecision>
        ctafUnicomMissingEvidenceAuditDecisions;
    BrainOwnedCtafUnicomMissingEvidenceAuditSummary
        ctafUnicomMissingEvidenceAuditSummary;
    std::vector<BrainOwnedCtafUnicomLegacyBypassAliasAuditDecision>
        ctafUnicomLegacyBypassAliasAuditDecisions;
    BrainOwnedCtafUnicomLegacyBypassAliasAuditSummary
        ctafUnicomLegacyBypassAliasAuditSummary;
    std::vector<BrainOwnedCtafUnicomPublicUnknownAliasConsumerAuditDecision>
        ctafUnicomPublicUnknownAliasConsumerAuditDecisions;
    BrainOwnedCtafUnicomPublicUnknownAliasConsumerAuditSummary
        ctafUnicomPublicUnknownAliasConsumerAuditSummary;
    std::vector<BrainOwnedCtafUnicomExternalAliasDeprecationDecision>
        ctafUnicomExternalAliasDeprecationDecisions;
    BrainOwnedCtafUnicomExternalAliasDeprecationSummary
        ctafUnicomExternalAliasDeprecationSummary;
    std::vector<BrainOwnedCtafUnicomPublicHeaderAliasRiskClosureDecision>
        ctafUnicomPublicHeaderAliasRiskClosureDecisions;
    BrainOwnedCtafUnicomPublicHeaderAliasRiskClosureSummary
        ctafUnicomPublicHeaderAliasRiskClosureSummary;
    int rejectedUnapprovedStations = 0;
};

void BeginBrainOwnedAccessoryVisibleInvalidationBatch(
    BrainOwnedRuntimeState* state);
void RecordBrainOwnedAccessoryDrawerContentMutation(
    BrainOwnedRuntimeState* state,
    BrainOwnedAccessoryDrawerId drawer);
void RecordBrainOwnedAccessorySelectionMutation(
    BrainOwnedRuntimeState* state);
void RecordBrainOwnedAccessoryLifecycleMutation(
    BrainOwnedRuntimeState* state);
void EndBrainOwnedAccessoryVisibleInvalidationBatch(
    BrainOwnedRuntimeState* state);

class BrainOwnedAccessoryVisibleInvalidationBatch {
public:
    explicit BrainOwnedAccessoryVisibleInvalidationBatch(
        BrainOwnedRuntimeState* state);
    ~BrainOwnedAccessoryVisibleInvalidationBatch();

    BrainOwnedAccessoryVisibleInvalidationBatch(
        const BrainOwnedAccessoryVisibleInvalidationBatch&) = delete;
    BrainOwnedAccessoryVisibleInvalidationBatch& operator=(
        const BrainOwnedAccessoryVisibleInvalidationBatch&) = delete;

private:
    BrainOwnedRuntimeState* state_ = nullptr;
};

void ResetBrainOwnedRuntimeState(BrainOwnedRuntimeState* state);
void ResetBrainOwnedRuntimeCachePreservingFlightContext(
    BrainOwnedRuntimeState* state,
    bool preserveXPilot4BridgeSession = false);
BrainOwnedOperationalActivationDecision DecideBrainOwnedOperationalActivation(
    const BrainOwnedOperationalActivationState& state,
    const BrainOwnedOperationalActivationInput& input);
void CommitBrainOwnedOperationalActivationDecision(
    BrainOwnedOperationalActivationState* state,
    const BrainOwnedOperationalActivationDecision& decision);
BrainOwnedOperationalRefreshGateDecision DecideBrainOwnedOperationalRefresh(
    const BrainOwnedOperationalRefreshGateState& state,
    const BrainOwnedOperationalRefreshGateInput& input);
void CommitBrainOwnedOperationalRefreshDecision(
    BrainOwnedOperationalRefreshGateState* state,
    const BrainOwnedOperationalRefreshGateInput& input,
    const BrainOwnedOperationalRefreshGateDecision& decision);
void ResetBrainOwnedOperationalRefreshGate(
    BrainOwnedOperationalRefreshGateState* state);
void RecordBrainOwnedOperationalEnableWakeRequest(
    BrainOwnedOperationalActivationState* state);
void RecordBrainOwnedOperationalServiceCall(
    BrainOwnedOperationalActivationState* state,
    const BrainOwnedOperationalActivationDecision& decision,
    BrainOwnedOperationalServiceStage stage);
void SetBrainOwnedOperationalActivationSuspended(
    BrainOwnedOperationalActivationState* state);
const char* ToString(BrainOwnedOperationalActivationReason reason);
const char* ToString(BrainOwnedOperationalServiceStage stage);
void InitializeBrainOwnedOperatingMode(
    BrainOwnedRuntimeState* state,
    const BrainOwnedOperatingModeInitializationInput& input);
BrainOwnedOperatingModeSelectionResult RequestBrainOwnedOperatingModeSelection(
    BrainOwnedRuntimeState* state,
    BrainOwnedOperatingMode requestedMode);
const char* ToString(BrainOwnedOperatingMode mode);
const char* ToString(BrainOwnedOperatingModeSource source);
const char* ToString(BrainOwnedOperatingModeLoadStatus status);

BrainOwnedAccessoryHistoryDecision AcceptBrainOwnedAccessoryHistoryEntry(
    BrainOwnedRuntimeState* state,
    const BrainOwnedAccessoryHistoryEntryInput& input);

BrainOwnedAccessorySelectionDecision RequestBrainOwnedAccessoryDrawerSelection(
    BrainOwnedRuntimeState* state,
    const BrainOwnedAccessorySelectionRequest& request);

BrainOwnedAccessoryPresentationHandle ProjectBrainOwnedAccessoryPresentation(
    BrainOwnedRuntimeState* state,
    BrainOwnedAccessoryProjectionCounters* counters);
inline BrainOwnedAccessoryPresentationHandle
ProjectBrainOwnedAccessoryPresentation(
    BrainOwnedRuntimeState* state,
    std::uint64_t,
    BrainOwnedAccessoryProjectionCounters* counters) {
    return ProjectBrainOwnedAccessoryPresentation(state, counters);
}
std::uint64_t BrainOwnedAccessorySemanticPresentationGeneration(
    const BrainOwnedRuntimeState& state);

BrainOwnedAccessoryPublicationDecision
ConsumeBrainOwnedAccessoryPublicationFact(
    BrainOwnedRuntimeState* state,
    const BrainOwnedAccessoryPublicationFact& fact);

BrainOwnedAccessoryBoundaryDecision CloseBrainOwnedAccessoryForDisplayClose(
    BrainOwnedRuntimeState* state);
BrainOwnedAccessoryBoundaryDecision CloseBrainOwnedAccessoryForTemporaryXPilotDisconnect(
    BrainOwnedRuntimeState* state);
BrainOwnedAccessoryBoundaryDecision CloseBrainOwnedAccessoryForInvalidAircraft(
    BrainOwnedRuntimeState* state);
BrainOwnedAccessoryBoundaryDecision DisableBrainOwnedAccessoryRuntime(
    BrainOwnedRuntimeState* state);
BrainOwnedAccessoryBoundaryDecision EnableBrainOwnedAccessoryRuntime(
    BrainOwnedRuntimeState* state);
BrainOwnedPluginAdminLifecycleDecision
SuspendBrainOwnedRuntimeForPluginAdmin(
    BrainOwnedRuntimeState* state,
    const BrainOwnedAsyncWorkerBindings& workers);
BrainOwnedPluginAdminLifecycleDecision
ResumeBrainOwnedRuntimeFromPluginAdmin(BrainOwnedRuntimeState* state);
BrainOwnedAccessoryBoundaryDecision CloseBrainOwnedAccessoryForTemporaryOverlaySleep(
    BrainOwnedRuntimeState* state);
BrainOwnedAccessoryBoundaryDecision ResetBrainOwnedAccessoryForSessionReset(
    BrainOwnedRuntimeState* state);
BrainOwnedAccessoryBoundaryDecision ResetBrainOwnedAccessoryForConfirmedNewFlight(
    BrainOwnedRuntimeState* state);
BrainOwnedAccessoryBoundaryDecision ResetBrainOwnedAccessoryForConfirmedColdDark(
    BrainOwnedRuntimeState* state);
BrainOwnedAccessoryBoundaryDecision ResetBrainOwnedAccessoryForCallsignChange(
    BrainOwnedRuntimeState* state,
    const std::string& previousCallsign,
    const std::string& nextCallsign);
BrainOwnedAccessoryBoundaryDecision StopBrainOwnedAccessoryRuntime(
    BrainOwnedRuntimeState* state);
void ResetBrainOwnedDisplayPublisherState(BrainOwnedRuntimeState* state);

void CommitBrainOwnedLastSampledFacts(
    BrainOwnedRuntimeState* state,
    const AircraftStateSnapshot& aircraftState,
    const PilotIdentitySnapshot& pilotIdentity,
    const FlightPlanSnapshot& flightPlan,
    const NetworkPlanSnapshot& networkPlan);

void ClearBrainOwnedLastSampledFacts(BrainOwnedRuntimeState* state);

std::string BuildBrainOwnedPlanIdentityKey(
    std::string callsign,
    std::string departureIcao,
    std::string destinationIcao);

std::string BuildBrainOwnedNetworkPlanIdentityKey(
    const NetworkPlanSnapshot& networkPlanSnapshot);

void CommitBrainOwnedFlightContext(
    BrainOwnedRuntimeState* state,
    const workflow::FlightContext& flightContext);

void ClearBrainOwnedFlightContext(BrainOwnedRuntimeState* state);

void ClearBrainOwnedXPilotConnectionTracking(BrainOwnedRuntimeState* state);

void ClearBrainOwnedFlightRecoveryRequests(BrainOwnedRuntimeState* state);

void SetBrainOwnedAutomaticFlightRecoveryPending(
    BrainOwnedRuntimeState* state,
    bool pending);

void SetBrainOwnedManualFlightRecoveryRequested(
    BrainOwnedRuntimeState* state,
    bool requested);

void SetBrainOwnedColdDarkResetApplied(
    BrainOwnedRuntimeState* state,
    bool applied);

void ClearBrainOwnedAircraftStateInvalidBoundary(
    BrainOwnedRuntimeState* state);

void ApplyBrainOwnedXPilotSessionBoundaryDecision(
    BrainOwnedRuntimeState* state,
    const workflow::XPilotSessionBoundaryDecision& decision);

void ApplyBrainOwnedAircraftRuntimeBoundaryDecision(
    BrainOwnedRuntimeState* state,
    const workflow::AircraftRuntimeBoundaryDecision& decision);

void ResetBrainOwnedStandbyAssistLatch(BrainOwnedRuntimeState* state);

void ClearBrainOwnedDiversionOverrideSource(BrainOwnedRuntimeState* state);

void SetBrainOwnedDiversionOverrideSourceKey(
    BrainOwnedRuntimeState* state,
    const std::string& sourcePlanKey);

BrainOwnedDiversionOverrideDecision DecideBrainOwnedDiversionOverride(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedDiversionOverrideInput& input);

void ClearBrainOwnedPreflightRouteCacheApplication(
    BrainOwnedRuntimeState* state);

void SetBrainOwnedDisplayOverrideMode(
    BrainOwnedRuntimeState* state,
    BrainOwnedDisplayOverrideMode mode);

void ClearBrainOwnedManualQuery(BrainOwnedRuntimeState* state);

void SetBrainOwnedPendingTextEntryMode(
    BrainOwnedRuntimeState* state,
    BrainOwnedTextEntryMode mode);

void ClearBrainOwnedPendingTextEntryMode(BrainOwnedRuntimeState* state);

BrainOwnedTextEntryMode ConsumeBrainOwnedPendingTextEntryMode(
    BrainOwnedRuntimeState* state);

bool HasBrainOwnedPendingTextEntry(const BrainOwnedRuntimeState& state);

void ShowBrainOwnedManualQueryLine(
    BrainOwnedRuntimeState* state,
    const std::string& line,
    long long visibleUntilSeconds);

void CommitBrainOwnedManualQuerySnapshot(
    BrainOwnedRuntimeState* state,
    ManualQuerySnapshot snapshot,
    long long visibleUntilSeconds);

void ExpireBrainOwnedManualQuery(
    BrainOwnedRuntimeState* state,
    long long nowSeconds);

BrainOwnedTextEntryDecision CommitBrainOwnedTextEntryFact(
    BrainOwnedRuntimeState* state,
    const BrainOwnedTextEntryFact& fact);

BrainOwnedTextEntryDecision CommitBrainOwnedAtisTextEntryFact(
    BrainOwnedRuntimeState* state,
    const BrainOwnedTextEntryFact& fact);

BrainOwnedAtisCycleDecision RunBrainOwnedAtisCycle(
    BrainOwnedRuntimeState* state,
    const BrainOwnedAtisCycleInput& input);

void ProjectBrainOwnedAtisOrbPresentation(
    const BrainOwnedRuntimeState& state,
    BrainOwnedAccessoryOrbPresentation* orb);

std::vector<BrainOwnedAccessoryHistoryEntry>
ProjectBrainOwnedAtisDrawerPresentation(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedAccessoryHistory& history,
    BrainOwnedAccessoryDrawerState* drawerState,
    std::string* drawerTitle,
    std::string* drawerStateText,
    std::string* emptyStateText,
    std::string* visibleRevisionIdentity);

bool AcknowledgeBrainOwnedAtisVisibleRevision(
    BrainOwnedRuntimeState* state,
    const std::string& visibleRevisionIdentity);

void ResetBrainOwnedAtisProductState(BrainOwnedRuntimeState* state);
void MarkBrainOwnedAtisSourceUnknownPreservingAcceptedState(
    BrainOwnedRuntimeState* state);

const char* ToString(BrainAtisServiceRole role);
const char* ToString(BrainAtisAvailability availability);
const char* ToString(BrainAtisTransientPresentation presentation);

BrainOwnedAsyncFactCycleOutput RunBrainOwnedAsyncFactCycle(
    BrainOwnedRuntimeState* state,
    const BrainOwnedAsyncFactCycleInput& input,
    const BrainOwnedAsyncWorkerBindings& workers);

BrainMetarWorkerShutdownSnapshot ApplyBrainOwnedAsyncWorkerLifecycleBoundary(
    BrainOwnedRuntimeState* state,
    const BrainOwnedAsyncWorkerBindings& workers,
    bool clearAcceptedState);

BrainOwnedPreflightRouteCacheDecision BeginBrainOwnedPreflightRouteCacheApplication(
    BrainOwnedRuntimeState* state,
    const BrainOwnedPreflightRouteCacheInput& input);

BrainOwnedPreflightRouteCacheValidationDecision
DecideBrainOwnedPreflightRouteCacheValidation(
    const BrainOwnedPreflightRouteCacheValidationInput& input);

std::string ToString(BrainOwnedCandidateDecision decision);

std::string BuildBrainOwnedCandidateCompletionKey(
    std::uint64_t radioBoardHash,
    std::uint64_t routePolygonHash,
    WorkflowStage workflowStage,
    const std::string& currentPolygonKey,
    const RadioReachableControllerCandidate& candidate);

void RecordBrainOwnedCandidateCompletion(
    BrainOwnedRuntimeState* state,
    BrainOwnedCandidateCompletion completion);

BrainOwnedBoardFilterOutput FilterBrainOwnedBoardByAcceptedCompletions(
    const ModuleBoardSnapshot& board,
    const std::vector<BrainOwnedCandidateCompletion>& completions);

BrainOwnedRadioBoardReuseOutput TryReuseBrainOwnedRadioBoard(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedRadioBoardReuseInput& input);

BrainOwnedRadioBoardCommitOutput CommitBrainOwnedRadioBoardRefresh(
    BrainOwnedRuntimeState* state,
    BrainOwnedRadioBoardCommitInput input);

BrainTerminalAuthorityWorkerOutput RefreshBrainOwnedDepartureTerminalAuthority(
    BrainOwnedRuntimeState* state,
    const workflow::FlightContext& flightContext,
    long long nowSeconds,
    BrainTerminalAuthorityWorker* worker);

BrainTerminalAuthorityWorkerOutput RefreshBrainOwnedArrivalTerminalAuthority(
    BrainOwnedRuntimeState* state,
    const workflow::FlightContext& flightContext,
    long long nowSeconds,
    BrainTerminalAuthorityWorker* worker);

BrainAirportFrequencyWorkerOutput RefreshBrainOwnedAirportFrequencies(
    BrainOwnedRuntimeState* state,
    const workflow::FlightContext& flightContext,
    long long nowSeconds,
    BrainAirportFrequencyWorker* worker);

BrainOwnedTerminalAuthorityRefreshPlan BeginBrainOwnedDepartureTerminalAuthorityRefresh(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedTerminalAuthorityRefreshInput& input);

BrainOwnedTerminalAuthorityRefreshPlan BeginBrainOwnedArrivalTerminalAuthorityRefresh(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedTerminalAuthorityRefreshInput& input);

void CommitBrainOwnedDepartureTerminalAuthorityRefresh(
    BrainOwnedRuntimeState* state,
    const BrainOwnedTerminalAuthorityRefreshPlan& plan,
    const BrainTerminalAuthorityWorkerOutput& workerOutput);

void CommitBrainOwnedArrivalTerminalAuthorityRefresh(
    BrainOwnedRuntimeState* state,
    const BrainOwnedTerminalAuthorityRefreshPlan& plan,
    const BrainTerminalAuthorityWorkerOutput& workerOutput);

BrainOwnedAirportFrequencyRefreshPlan BeginBrainOwnedAirportFrequencyRefresh(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedAirportFrequencyRefreshInput& input);

void CommitBrainOwnedAirportFrequencyRefresh(
    BrainOwnedRuntimeState* state,
    const BrainOwnedAirportFrequencyRefreshPlan& plan,
    const BrainAirportFrequencyWorkerOutput& workerOutput);

RadioReachableControllerSnapshot RunBrainOwnedRadioPhaseGate(
    BrainOwnedRuntimeState* state,
    const RadioReachableControllerSnapshot& radioSnapshot,
    WorkflowStage workflowStage,
    const std::string& reason);

void CommitBrainOwnedPublishedRuntime(
    BrainOwnedRuntimeState* state,
    const BrainOwnedPublishedRuntimeInput& input);

BrainOwnedOverlayWakeDecision DecideBrainOwnedOverlayWake(
    const BrainOwnedOverlayWakeInput& input);

void ResetBrainOwnedEnrouteInitialHold(BrainOwnedRuntimeState* state);

BrainOwnedEnrouteInitialHoldOutput UpdateBrainOwnedEnrouteInitialHold(
    BrainOwnedRuntimeState* state,
    const BrainOwnedEnrouteInitialHoldInput& input);

BrainOwnedFlightPlanSampleDecision DecideBrainOwnedFlightPlanSample(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedFlightPlanSampleInput& input);

void CommitBrainOwnedFlightPlanSample(
    BrainOwnedRuntimeState* state,
    const BrainOwnedFlightPlanSampleCommitInput& input);

void ResetBrainOwnedCruiseTarget(BrainOwnedRuntimeState* state);

BrainOwnedCruiseTargetPlanOutput SyncBrainOwnedCruiseTargetFromNetworkPlan(
    BrainOwnedRuntimeState* state,
    const BrainOwnedCruiseTargetPlanInput& input);

BrainOwnedCruiseTargetCommandOutput ApplyBrainOwnedCruiseTargetCommand(
    BrainOwnedRuntimeState* state,
    const BrainOwnedCruiseTargetCommandInput& input);

void UpdateBrainOwnedCruiseTargetProgress(
    BrainOwnedRuntimeState* state,
    const BrainOwnedCruiseTargetProgressInput& input);

std::string BuildBrainOwnedCruiseTargetHeaderText(
    const BrainOwnedRuntimeState& state);

void ResetBrainOwnedWorkflowProgress(BrainOwnedRuntimeState* state);

void ResetBrainOwnedWorkflowArrivalWake(BrainOwnedRuntimeState* state);

workflow::WorkflowState BuildBrainOwnedWorkflowState(
    const BrainOwnedRuntimeState& state);

void CommitBrainOwnedWorkflowState(
    BrainOwnedRuntimeState* state,
    const workflow::WorkflowState& workflowState);

BrainOwnedWorkflowSelectionOutput ResolveBrainOwnedWorkflowSelection(
    BrainOwnedRuntimeState* state,
    const BrainOwnedWorkflowSelectionInput& input);

void ApplyBrainOwnedWorkflowRecoveryStage(
    BrainOwnedRuntimeState* state,
    WorkflowStage stage,
    double nowSeconds);

void SetBrainOwnedXPilotConnectedSeen(
    BrainOwnedRuntimeState* state,
    bool seen);

void MarkBrainOwnedXPilotConnectedIfConnected(
    BrainOwnedRuntimeState* state,
    const XPilotSessionSnapshot& xPilotSession);

BrainOwnedStandbyAssistPlanOutput BuildBrainOwnedStandbyAssistPlan(
    const BrainOwnedStandbyAssistPlanInput& input);

BrainOwnedStandbyAssistSideEffectDecision
DecideBrainOwnedStandbyAssistSideEffect(
    BrainOwnedRuntimeState* state,
    const BrainOwnedStandbyAssistPlanOutput& plan,
    bool standbyAssistEnabled);

BrainOwnedStandbyAssistWriterResult
BuildBrainOwnedStandbyAssistWriterResult(
    const BrainOwnedStandbyAssistSideEffectDecision& decision,
    bool writeSucceeded);

BrainOwnedStandbyAssistWriterResult
BuildBrainOwnedStandbyAssistWriterResultFromCode(
    const BrainOwnedStandbyAssistSideEffectDecision& decision,
    const std::string& writerResultCode);

BrainOwnedStandbyAssistSideEffectDecision
CompleteBrainOwnedStandbyAssistSideEffectDecision(
    const BrainOwnedStandbyAssistPlanOutput& plan,
    BrainOwnedStandbyAssistSideEffectDecision decision,
    bool standbyLoaded);

BrainOwnedStandbyAssistSideEffectDecision
CompleteBrainOwnedStandbyAssistSideEffectDecision(
    const BrainOwnedStandbyAssistPlanOutput& plan,
    BrainOwnedStandbyAssistSideEffectDecision decision,
    const BrainOwnedStandbyAssistWriterResult& writerResult);

FinalDisplaySnapshot ApplyBrainOwnedStandbyAssistResult(
    const BrainOwnedStandbyAssistPlanOutput& plan,
    bool standbyLoaded);

BrainOwnedPublisherInput BuildBrainOwnedPublisherInputFromFacts(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedPublisherFactInput& facts);

void MarkBrainOwnedDisplayedCompletionsFromFinalDisplay(
    BrainOwnedRuntimeState* state,
    const FinalDisplaySnapshot& finalDisplay);

BrainOwnedPublisherOutput RunBrainOwnedPublisher(
    BrainOwnedRuntimeState* state,
    const BrainOwnedPublisherInput& input);

void CommitBrainOwnedPublishedRuntimeFromPublisherOutput(
    BrainOwnedRuntimeState* state,
    WorkflowStage workflowStage,
    const std::string& planKey,
    const RadioReachableControllerSnapshot& gatedRadioSnapshot,
    const BrainOwnedPublisherOutput& publisherOutput,
    const FinalDisplaySnapshot& finalDisplay);

bool BrainOwnedCandidatesCompleteForCurrentBoard(
    const BrainOwnedRuntimeState& state,
    const RadioReachableControllerSnapshot& radioSnapshot,
    WorkflowStage workflowStage,
    const std::string& currentPolygonKey);

std::string BrainOwnedRuntimeStateSummary(const BrainOwnedRuntimeState& state);

}  // namespace xvatsim::brain
