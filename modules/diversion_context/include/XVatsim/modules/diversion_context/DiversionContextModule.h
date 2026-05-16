#pragma once

#include <string>

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::modules::diversion_context {

struct DiversionOverrideSnapshot {
    bool active = false;
    std::string destinationIcao;
    double destinationLatDeg = 0.0;
    double destinationLonDeg = 0.0;
    bool hasDestinationCoordinates = false;
    std::string statusLine;
};

struct DiversionUpdateResult {
    bool accepted = false;
    bool changed = false;
    std::string airportIcao;
    std::string statusLine;
};

class DiversionContextModule {
public:
    DiversionContextModule() = default;

    DiversionUpdateResult SetDiversionAirport(const std::string& airportIcao);
    DiversionUpdateResult ClearOverride();
    const DiversionOverrideSnapshot& Snapshot() const;
    bool HasOverride() const;
    brain::NetworkPlanSnapshot BuildEffectivePlan(
        const brain::NetworkPlanSnapshot& networkPlanSnapshot) const;
    void Reset();

private:
    DiversionOverrideSnapshot overrideSnapshot_{};
};

}  // namespace xvatsim::modules::diversion_context

