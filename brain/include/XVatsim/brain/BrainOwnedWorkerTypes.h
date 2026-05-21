#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "XVatsim/brain/BrainOwnedRuntime.h"
#include "XVatsim/brain/BrainTypes.h"
#include "XVatsim/brain/RadioReachableSnapshot.h"

namespace xvatsim::brain {

struct BrainRadioRangeWorkerInput {
    AircraftStateSnapshot aircraft;
    RadioStateSnapshot radios;
    ControllerFeedSnapshot controllerFeed;
    std::string planKey;
};

struct BrainRadioRangeWorkerOutput {
    bool available = false;
    bool stale = true;
    std::string reason;
    TransceiverResolutionSnapshot transceivers;
    RadioReachableControllerSnapshot radioBoard;
    RadioReachableCandidateDiff diff;
};

struct BrainRoutePolygonWorkerInput {
    AircraftStateSnapshot aircraft;
    NetworkPlanSnapshot networkPlan;
    std::string planKey;
};

struct BrainRoutePolygonWorkerOutput {
    bool available = false;
    bool stale = true;
    std::string reason;
    RouteSectorSnapshot route;
    std::uint64_t routePolygonHash = 0;
    int currentPolygonIndex = 0;
    std::string currentPolygonKey;
    std::string nextPolygonKey;
    std::string arrivalPolygonKey;
    std::string finalRoutePolygonKey;
};

struct BrainControllerRelevanceWorkerInput {
    WorkflowStage workflowStage = WorkflowStage::None;
    std::uint64_t radioBoardHash = 0;
    std::uint64_t routePolygonHash = 0;
    int currentPolygonIndex = 0;
    std::string currentPolygonKey;
    std::string nextPolygonKey;
    std::string arrivalPolygonKey;
    double routeProgressDistanceNm = 0.0;
    std::string departureIcao;
    std::string arrivalIcao;
    RadioStateSnapshot radios;
    std::vector<RouteSectorMatchSnapshot> currentSectors;
    std::vector<RouteSectorMatchSnapshot> nextSectors;
    std::vector<RadioReachableControllerCandidate> candidates;
};

struct BrainControllerRelevanceWorkerOutput {
    bool available = false;
    bool stale = true;
    bool needsFallbackVerification = false;
    std::string reason;
    std::vector<BrainOwnedCandidateCompletion> completions;
    ModuleBoardSnapshot departureBoard;
    ModuleBoardSnapshot arrivalBoard;
    ModuleBoardSnapshot enrouteBoard;
};

struct BrainOwnedControllerRelevanceRuntimeOutput {
    BrainControllerRelevanceWorkerOutput relevance;
    bool cacheHit = false;
    std::string cacheStatus;
};

BrainControllerRelevanceWorkerOutput RunBrainControllerRelevanceWorker(
    const BrainControllerRelevanceWorkerInput& input);

BrainOwnedControllerRelevanceRuntimeOutput RunBrainOwnedControllerRelevance(
    BrainOwnedRuntimeState* state,
    const BrainControllerRelevanceWorkerInput& input);

struct BrainUiWorkerInput {
    WorkflowStage workflowStage = WorkflowStage::None;
    ModuleBoardSnapshot finalDisplay;
    std::string reason;
};

struct BrainUiWorkerOutput {
    bool rendered = false;
    std::string reason;
};

}  // namespace xvatsim::brain
