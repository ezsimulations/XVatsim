#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "XVatsim/brain/BrainDisplayIntent.h"
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

enum class BrainOwnedTextEntryMode {
    None,
    ManualCtaf,
    DiversionAirport,
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
    std::string reason;
    std::string stableKey;
};

struct BrainOwnedControllerMessageState {
    bool primed = false;
    int lastSequence = 0;
    bool visible = false;
    bool cachedAvailable = false;
    std::string from;
    std::string body;
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
    bool hasRoutePolygonSnapshot = false;
    RouteSectorSnapshot routePolygonSnapshot;
    std::uint64_t routePolygonHash = 0;
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
    TransceiverResolutionSnapshot transceiverSnapshot;
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
    BrainOwnedControllerMessageState controllerMessageState;
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

    WorkflowStage lastWorkflowStage = WorkflowStage::None;
    std::string lastPlanKey;
    std::uint64_t lastRadioBoardHash = 0;
    std::uint64_t lastRoutePolygonHash = 0;
    std::uint64_t lastDepartureTerminalAuthorityHash = 0;
    std::uint64_t lastArrivalTerminalAuthorityHash = 0;
    std::uint64_t lastAirportFrequencyHash = 0;
    std::uint64_t lastAuthorityRelevanceHash = 0;
    std::uint64_t lastRadioTuningHash = 0;
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
    bool controllerMessageVisible = false;
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

struct BrainOwnedStandbyAssistPlanInput {
    WorkflowStage workflowStage = WorkflowStage::None;
    std::string planKey;
    RadioStateSnapshot radios;
    FinalDisplaySnapshot board;
};

struct BrainOwnedStandbyAssistPlanOutput {
    bool hasTarget = false;
    WorkflowStage workflowStage = WorkflowStage::None;
    FinalDisplaySnapshot board;
    std::size_t targetStationIndex = 0;
    std::string targetFrequency;
    std::string latchKey;
    bool targetAlreadyInCom1Standby = false;
};

struct BrainOwnedStandbyAssistSideEffectDecision {
    bool shouldWriteCom1Standby = false;
    bool standbyLoaded = false;
    std::string targetFrequency;
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
    bool verificationPending = false;
    std::string publishReason;
};

struct BrainOwnedCtafLookupFact {
    std::string airportIcao;
    bool resolved = false;
    bool available = false;
    std::string frequency;
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
};

struct BrainOwnedPublisherOutput {
    ModuleBoardSnapshot departureBoard;
    ModuleBoardSnapshot arrivalBoard;
    ModuleBoardSnapshot enrouteBoard;
    FinalDisplaySnapshot finalDisplay;
    BrainDisplayIntentOutput displayIntent;
    PhaseSnapshotPublishResult phasePublish;
    std::string phasePublisherStateSummary;
    int rejectedUnapprovedStations = 0;
};

void ResetBrainOwnedRuntimeState(BrainOwnedRuntimeState* state);
void ResetBrainOwnedRuntimeCachePreservingFlightContext(
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

void ResetBrainOwnedControllerMessageState(BrainOwnedRuntimeState* state);

void ClearBrainOwnedControllerMessage(BrainOwnedRuntimeState* state);

void RecallBrainOwnedControllerMessage(BrainOwnedRuntimeState* state);

void UpdateBrainOwnedControllerMessageState(
    BrainOwnedRuntimeState* state,
    const XPilotPrivateMessageSnapshot& messageSnapshot,
    bool controllerMessageUiEnabled);

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
    const BrainOwnedRadioBoardCommitInput& input);

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
