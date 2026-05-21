#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "XVatsim/brain/BrainDisplayIntent.h"
#include "XVatsim/brain/BrainTypes.h"
#include "XVatsim/brain/PhaseSnapshotPublisher.h"
#include "XVatsim/brain/RadioReachableSnapshot.h"

namespace xvatsim::brain {

enum class BrainOwnedCandidateDecision {
    Pending,
    Accepted,
    Rejected,
    NeedsVerification,
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

    ModuleBoardSnapshot departureBoardSnapshot;
    ModuleBoardSnapshot arrivalBoardSnapshot;
    ModuleBoardSnapshot enrouteBoardSnapshot;
    ModuleBoardSnapshot relevanceDepartureBoardSnapshot;
    ModuleBoardSnapshot relevanceArrivalBoardSnapshot;
    ModuleBoardSnapshot relevanceEnrouteBoardSnapshot;
    ModuleBoardSnapshot activeBoardSnapshot;
    ModuleBoardSnapshot finalDisplaySnapshot;
    PhaseSnapshotPublisherState phaseSnapshotPublisherState;
    std::uint64_t lastDisplayIntentHash = 0;

    WorkflowStage lastWorkflowStage = WorkflowStage::None;
    std::string lastPlanKey;
    std::uint64_t lastRadioBoardHash = 0;
    std::uint64_t lastRoutePolygonHash = 0;
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

struct BrainOwnedPublishedRuntimeInput {
    WorkflowStage workflowStage = WorkflowStage::None;
    std::string planKey;
    RadioReachableControllerSnapshot gatedRadioSnapshot;
    ModuleBoardSnapshot departureBoard;
    ModuleBoardSnapshot arrivalBoard;
    ModuleBoardSnapshot enrouteBoard;
    ModuleBoardSnapshot finalDisplay;
};

enum class BrainOwnedDisplayOverrideMode {
    Auto,
    ForcedOpen,
    ForcedSleep,
};

struct BrainOwnedOverlayWakeInput {
    AircraftStateSnapshot aircraftState;
    XPilotSessionSnapshot xPilotSession;
    WorkflowStage workflowStage = WorkflowStage::None;
    ModuleBoardSnapshot finalDisplay;
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

struct BrainOwnedStandbyAssistPlanInput {
    WorkflowStage workflowStage = WorkflowStage::None;
    std::string planKey;
    RadioStateSnapshot radios;
    ModuleBoardSnapshot board;
};

struct BrainOwnedStandbyAssistPlanOutput {
    bool hasTarget = false;
    WorkflowStage workflowStage = WorkflowStage::None;
    ModuleBoardSnapshot board;
    std::size_t targetStationIndex = 0;
    std::string targetFrequency;
    std::string latchKey;
    bool targetAlreadyInCom1Standby = false;
};

struct BrainOwnedPublisherInput {
    WorkflowStage workflowStage = WorkflowStage::None;
    double routeProgressDistanceNm = 0.0;
    std::string currentPolygonKey;
    std::string nextPolygonKey;
    std::string arrivalPolygonKey;
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
    ModuleBoardSnapshot finalDisplay;
    BrainDisplayIntentOutput displayIntent;
    PhaseSnapshotPublishResult phasePublish;
    std::string phasePublisherStateSummary;
    int rejectedUnapprovedStations = 0;
};

void ResetBrainOwnedRuntimeState(BrainOwnedRuntimeState* state);
void ResetBrainOwnedDisplayPublisherState(BrainOwnedRuntimeState* state);

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

BrainOwnedStandbyAssistPlanOutput BuildBrainOwnedStandbyAssistPlan(
    const BrainOwnedStandbyAssistPlanInput& input);

ModuleBoardSnapshot ApplyBrainOwnedStandbyAssistResult(
    const BrainOwnedStandbyAssistPlanOutput& plan,
    bool standbyLoaded);

BrainOwnedPublisherInput BuildBrainOwnedPublisherInputFromFacts(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedPublisherFactInput& facts);

void MarkBrainOwnedDisplayedCompletionsFromFinalDisplay(
    BrainOwnedRuntimeState* state,
    const ModuleBoardSnapshot& finalDisplay);

BrainOwnedPublisherOutput RunBrainOwnedPublisher(
    BrainOwnedRuntimeState* state,
    const BrainOwnedPublisherInput& input);

void CommitBrainOwnedPublishedRuntimeFromPublisherOutput(
    BrainOwnedRuntimeState* state,
    WorkflowStage workflowStage,
    const std::string& planKey,
    const RadioReachableControllerSnapshot& gatedRadioSnapshot,
    const BrainOwnedPublisherOutput& publisherOutput,
    const ModuleBoardSnapshot& finalDisplay);

bool BrainOwnedCandidatesCompleteForCurrentBoard(
    const BrainOwnedRuntimeState& state,
    const RadioReachableControllerSnapshot& radioSnapshot,
    WorkflowStage workflowStage,
    const std::string& currentPolygonKey);

std::string BrainOwnedRuntimeStateSummary(const BrainOwnedRuntimeState& state);

}  // namespace xvatsim::brain
