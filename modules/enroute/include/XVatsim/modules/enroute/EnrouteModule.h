#pragma once

#ifndef XVATSIM_ENABLE_HARNESS_LEGACY_BOARD_MODULES
#error "EnrouteModule is harness-only legacy board coverage; live runtime must use brain-owned workers."
#endif

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::modules::enroute {

class EnrouteModule {
public:
    EnrouteModule() = default;

    brain::ModuleBoardSnapshot Collect(
        const brain::XPilotSessionSnapshot& xPilotSessionSnapshot,
        const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
        const brain::RadioStateSnapshot& radioStateSnapshot,
        const brain::RouteSectorSnapshot& routeSectorSnapshot,
        const brain::AuthorityRelevanceSnapshot* authorityRelevanceSnapshot);
    void Reset();
};

}  // namespace xvatsim::modules::enroute
