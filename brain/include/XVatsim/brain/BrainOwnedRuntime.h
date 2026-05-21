#pragma once

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
    std::vector<BrainOwnedCandidateCompletion> candidateCompletions;
};

struct BrainOwnedBoardFilterOutput {
    ModuleBoardSnapshot board;
    int rejectedUnapprovedStations = 0;
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

void MarkBrainOwnedDisplayedCompletionsFromFinalDisplay(
    BrainOwnedRuntimeState* state,
    const ModuleBoardSnapshot& finalDisplay);

BrainOwnedPublisherOutput RunBrainOwnedPublisher(
    BrainOwnedRuntimeState* state,
    const BrainOwnedPublisherInput& input);

bool BrainOwnedCandidatesCompleteForCurrentBoard(
    const BrainOwnedRuntimeState& state,
    const RadioReachableControllerSnapshot& radioSnapshot,
    WorkflowStage workflowStage,
    const std::string& currentPolygonKey);

std::string BrainOwnedRuntimeStateSummary(const BrainOwnedRuntimeState& state);

}  // namespace xvatsim::brain
