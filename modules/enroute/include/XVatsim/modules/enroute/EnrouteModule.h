#pragma once

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::modules::enroute {

class EnrouteModule {
public:
    EnrouteModule() = default;

    brain::ModuleBoardSnapshot Collect(
        const brain::XPilotSessionSnapshot& xPilotSessionSnapshot,
        const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
        const brain::RadioStateSnapshot& radioStateSnapshot,
        const brain::RouteSectorSnapshot& routeSectorSnapshot);
    void Reset();
};

}  // namespace xvatsim::modules::enroute
