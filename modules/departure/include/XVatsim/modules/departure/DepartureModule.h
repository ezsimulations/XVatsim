#pragma once

#ifndef XVATSIM_ENABLE_HARNESS_LEGACY_BOARD_MODULES
#error "DepartureModule is harness-only legacy board coverage; live runtime must use brain-owned workers."
#endif

#include <string>

#include "XVatsim/brain/BrainTypes.h"
#include "XVatsim/modules/ctaf_lookup/CtafLookupService.h"

namespace xvatsim::modules::departure {

class DepartureModule {
public:
    DepartureModule() = default;

    brain::ModuleBoardSnapshot Collect(
        const brain::XPilotSessionSnapshot& xPilotSessionSnapshot,
        const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
        const brain::RadioStateSnapshot& radioStateSnapshot,
        const std::string& departureAirportIcao,
        const brain::AirportSectorSnapshot& airportSectorSnapshot,
        const brain::AuthorityRelevanceSnapshot* authorityRelevanceSnapshot,
        xvatsim::modules::ctaf_lookup::CtafLookupService* ctafLookupService) const;
};

}  // namespace xvatsim::modules::departure
