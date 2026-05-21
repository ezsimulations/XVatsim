#pragma once

#include <string>

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::brain {

struct RoutePolygonTransitionWorkerInput {
    AircraftStateSnapshot aircraft;
    RouteSectorSnapshot route;
    std::string previousPolygonKey;
    double transitionToleranceNm = 1.0;
};

struct RoutePolygonTransitionWorkerOutput {
    bool available = false;
    bool stale = true;
    bool routeResolved = false;
    bool changed = false;
    bool enteredFinalRoutePolygon = false;
    bool shouldWakeUi = false;
    double progressDistanceNm = 0.0;
    int currentPolygonIndex = 0;
    std::string previousPolygonKey;
    std::string currentPolygonKey;
    std::string nextPolygonKey;
    std::string finalRoutePolygonKey;
    std::string reason;
    RouteSectorSnapshot route;
};

RoutePolygonTransitionWorkerOutput RunRoutePolygonTransitionWorker(
    const RoutePolygonTransitionWorkerInput& input);

}  // namespace xvatsim::brain
