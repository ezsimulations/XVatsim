#pragma once

#ifndef XVATSIM_ENABLE_HARNESS_LEGACY_BOARD_MODULES
#error "ArrivalAirspaceModule is harness-only legacy board coverage; live runtime must use brain-owned workers."
#endif

#include <string>

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::modules::arrival {

class ArrivalAirspaceModule {
public:
    ArrivalAirspaceModule() = default;

    brain::ModuleBoardSnapshot Collect(
        const brain::XPilotSessionSnapshot& xPilotSessionSnapshot,
        const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
        const brain::RadioStateSnapshot& radioStateSnapshot,
        const std::string& arrivalAirportIcao,
        const brain::AirportSectorSnapshot& airportSectorSnapshot,
        const brain::AuthorityRelevanceSnapshot* authorityRelevanceSnapshot) const;
};

}  // namespace xvatsim::modules::arrival
