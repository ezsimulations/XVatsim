#pragma once

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::modules::aircraft_state {

class AircraftStateSampler {
public:
    AircraftStateSampler() = default;

    brain::AircraftStateSnapshot Sample();
    void Reset();

private:
    void ResolveDataRefs();
    double ReadScalarDataRef(void* dataRef) const;
    bool ReadBooleanLikeDataRef(void* dataRef) const;

    void* latitudeRef_ = nullptr;
    void* longitudeRef_ = nullptr;
    void* altitudeMslRef_ = nullptr;
    void* altitudePilotRef_ = nullptr;
    void* altitudePilotRefLegacy_ = nullptr;
    void* altitudeCopilotRef_ = nullptr;
    void* altimeterPilotRef_ = nullptr;
    void* altimeterLegacyRef_ = nullptr;
    void* altitudeAglRef_ = nullptr;
    void* groundSpeedRef_ = nullptr;
    void* verticalSpeedRef_ = nullptr;
    void* trackTrueRef_ = nullptr;
    void* headingTrueRef_ = nullptr;
    void* batteryOnRef_ = nullptr;

    bool loggedMissingDataRefs_ = false;
};

}  // namespace xvatsim::modules::aircraft_state
