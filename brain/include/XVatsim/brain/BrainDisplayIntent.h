#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::brain {

struct BrainDisplayRelationFact {
    std::string callsign;
    std::string frequency;
    DisplayRelation displayRelation = DisplayRelation::Unknown;
    bool hasRouteEntryDistance = false;
    double routeEntryDistanceNm = 0.0;
};

struct BrainDisplayIntentInput {
    WorkflowStage workflowStage = WorkflowStage::None;
    double routeProgressDistanceNm = 0.0;
    std::string currentPolygonKey;
    std::string nextPolygonKey;
    std::string arrivalPolygonKey;
    RadioStateSnapshot radios;
    ModuleBoardSnapshot departureBoard;
    ModuleBoardSnapshot arrivalBoard;
    ModuleBoardSnapshot enrouteBoard;
    std::vector<BrainDisplayRelationFact> relationFacts;
};

struct BrainDisplayIntentOutput {
    ModuleBoardSnapshot departureBoard;
    ModuleBoardSnapshot arrivalBoard;
    ModuleBoardSnapshot enrouteBoard;
    FinalDisplaySnapshot finalDisplay;
    std::vector<std::string> diagnostics;
    std::string reason;
    std::uint64_t stableHash = 0;
    int displayed = 0;
    int hidden = 0;
    int filtered = 0;
};

const char* ToString(DisplayRelation relation);

BrainDisplayIntentOutput RunBrainDisplayIntentWorker(
    const BrainDisplayIntentInput& input);

}  // namespace xvatsim::brain
