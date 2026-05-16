#pragma once

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
        xvatsim::modules::ctaf_lookup::CtafLookupService* ctafLookupService) const;
};

}  // namespace xvatsim::modules::departure
