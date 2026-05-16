#pragma once

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::modules::flight_plan {

class FlightPlanSampler {
public:
    FlightPlanSampler() = default;

    brain::FlightPlanSnapshot Sample(const brain::AircraftStateSnapshot& aircraftState) const;
    void Reset();

private:
    mutable bool hasSampleCache_ = false;
    mutable brain::FlightPlanSnapshot cachedSnapshot_{};
    mutable long long lastSampleTickSeconds_ = 0;
    mutable double lastSampleLatitudeDeg_ = 0.0;
    mutable double lastSampleLongitudeDeg_ = 0.0;
};

}  // namespace xvatsim::modules::flight_plan
