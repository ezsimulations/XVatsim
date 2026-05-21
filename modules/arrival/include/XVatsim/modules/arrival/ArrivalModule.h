#pragma once

#ifndef XVATSIM_ENABLE_HARNESS_LEGACY_BOARD_MODULES
#error "ArrivalModule is harness-only legacy board coverage; live runtime must use brain-owned workers."
#endif

#include <string>

#include "XVatsim/brain/BrainTypes.h"
#include "XVatsim/modules/arrival/ArrivalAirspaceModule.h"
#include "XVatsim/modules/arrival/ArrivalLocalModule.h"
#include "XVatsim/modules/ctaf_lookup/CtafLookupService.h"

namespace xvatsim::modules::arrival {

class ArrivalModule {
public:
    ArrivalModule() = default;

    brain::ModuleBoardSnapshot Collect(
        const brain::XPilotSessionSnapshot& xPilotSessionSnapshot,
        const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
        const brain::RadioStateSnapshot& radioStateSnapshot,
        const std::string& arrivalAirportIcao,
        const brain::AirportSectorSnapshot& airportSectorSnapshot,
        const brain::AuthorityRelevanceSnapshot* authorityRelevanceSnapshot,
        xvatsim::modules::ctaf_lookup::CtafLookupService* ctafLookupService) const;

private:
    ArrivalLocalModule localModule_{};
    ArrivalAirspaceModule airspaceModule_{};
};

}  // namespace xvatsim::modules::arrival
