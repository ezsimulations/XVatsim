#include "XVatsim/modules/aircraft_state/AircraftStateSampler.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "XPLMDataAccess.h"
#include "XPLMUtilities.h"

namespace xvatsim::modules::aircraft_state {

namespace {

constexpr double kMetersToFeet = 3.280839895;
constexpr double kMetersPerSecondToKnots = 1.943844492;
constexpr double kGroundedAglThresholdFeet = 75.0;
constexpr double kGroundedSpeedThresholdKnots = 55.0;

double NormalizeHeadingDeg(double headingDeg) {
    if (!std::isfinite(headingDeg)) {
        return 0.0;
    }

    headingDeg = std::fmod(headingDeg, 360.0);
    while (headingDeg < 0.0) {
        headingDeg += 360.0;
    }

    while (headingDeg >= 360.0) {
        headingDeg -= 360.0;
    }

    return headingDeg;
}

bool IsValidAircraftPosition(double latitudeDeg, double longitudeDeg) {
    return std::isfinite(latitudeDeg) &&
           std::isfinite(longitudeDeg) &&
           latitudeDeg >= -90.0 &&
           latitudeDeg <= 90.0 &&
           longitudeDeg >= -180.0 &&
           longitudeDeg <= 180.0;
}

void LogMissingDataRefOnce(bool* alreadyLogged, const char* dataRefName) {
    if (*alreadyLogged) {
        return;
    }

    std::string message = "[XVatsim] Missing required dataref: ";
    message += dataRefName;
    message += "\n";
    XPLMDebugString(message.c_str());
    *alreadyLogged = true;
}

}  // namespace

brain::AircraftStateSnapshot AircraftStateSampler::Sample() {
    ResolveDataRefs();

    brain::AircraftStateSnapshot snapshot;
    snapshot.valid = latitudeRef_ != nullptr &&
                     longitudeRef_ != nullptr &&
                     altitudeMslRef_ != nullptr &&
                     altitudeAglRef_ != nullptr &&
                     groundSpeedRef_ != nullptr &&
                     verticalSpeedRef_ != nullptr;

    if (!snapshot.valid) {
        return snapshot;
    }

    snapshot.latitudeDeg = ReadScalarDataRef(latitudeRef_);
    snapshot.longitudeDeg = ReadScalarDataRef(longitudeRef_);
    snapshot.altitudeMslFt = ReadScalarDataRef(altitudeMslRef_) * kMetersToFeet;
    snapshot.altitudeOperationalFt = snapshot.altitudeMslFt;
    if (altitudePilotRef_ != nullptr) {
        snapshot.altitudeOperationalFt = ReadScalarDataRef(altitudePilotRef_);
    } else if (altitudePilotRefLegacy_ != nullptr) {
        snapshot.altitudeOperationalFt = ReadScalarDataRef(altitudePilotRefLegacy_);
    } else if (altitudeCopilotRef_ != nullptr) {
        snapshot.altitudeOperationalFt = ReadScalarDataRef(altitudeCopilotRef_);
    }
    if (altimeterPilotRef_ != nullptr) {
        snapshot.altimeterSettingInHg = ReadScalarDataRef(altimeterPilotRef_);
        snapshot.hasAltimeterSetting = snapshot.altimeterSettingInHg > 0.0;
    } else if (altimeterLegacyRef_ != nullptr) {
        snapshot.altimeterSettingInHg = ReadScalarDataRef(altimeterLegacyRef_);
        snapshot.hasAltimeterSetting = snapshot.altimeterSettingInHg > 0.0;
    }
    snapshot.altitudeAglFt = ReadScalarDataRef(altitudeAglRef_) * kMetersToFeet;
    snapshot.groundSpeedKt = ReadScalarDataRef(groundSpeedRef_) * kMetersPerSecondToKnots;
    snapshot.verticalSpeedFpm = ReadScalarDataRef(verticalSpeedRef_);
    if (!IsValidAircraftPosition(snapshot.latitudeDeg, snapshot.longitudeDeg) ||
        !std::isfinite(snapshot.altitudeMslFt) ||
        !std::isfinite(snapshot.altitudeOperationalFt) ||
        !std::isfinite(snapshot.altitudeAglFt) ||
        !std::isfinite(snapshot.groundSpeedKt) ||
        !std::isfinite(snapshot.verticalSpeedFpm) ||
        (snapshot.hasAltimeterSetting && !std::isfinite(snapshot.altimeterSettingInHg))) {
        return {};
    }

    if (batteryOnRef_ != nullptr) {
        const auto typedDataRef = static_cast<XPLMDataRef>(batteryOnRef_);
        const auto dataRefTypes = XPLMGetDataRefTypes(typedDataRef);
        if ((dataRefTypes & xplmType_IntArray) != 0) {
            const auto count = XPLMGetDatavi(typedDataRef, nullptr, 0, 0);
            if (count > 0) {
                std::vector<int> values(static_cast<std::size_t>(count));
                const auto read = XPLMGetDatavi(
                    typedDataRef, values.data(), 0, count);
                snapshot.batterySwitchFactsAvailable = read > 0;
                snapshot.batterySwitchCount = read;
                for (int index = 0; index < read; ++index) {
                    if (values[static_cast<std::size_t>(index)] != 0) {
                        ++snapshot.batterySwitchOnCount;
                    }
                }
            }
        } else if ((dataRefTypes & (xplmType_Int | xplmType_Float |
                                    xplmType_Double)) != 0) {
            snapshot.batterySwitchFactsAvailable = true;
            snapshot.batterySwitchCount = 1;
            snapshot.batterySwitchOnCount =
                ReadBooleanLikeDataRef(batteryOnRef_) ? 1 : 0;
        }
    }
    snapshot.batteryOn = snapshot.batterySwitchOnCount > 0;

    if (busVoltsRef_ != nullptr) {
        const auto typedDataRef = static_cast<XPLMDataRef>(busVoltsRef_);
        const auto dataRefTypes = XPLMGetDataRefTypes(typedDataRef);
        if ((dataRefTypes & xplmType_FloatArray) != 0) {
            const auto count = XPLMGetDatavf(typedDataRef, nullptr, 0, 0);
            if (count > 0) {
                std::vector<float> values(static_cast<std::size_t>(count));
                const auto read = XPLMGetDatavf(
                    typedDataRef, values.data(), 0, count);
                snapshot.electricalBusVoltageFactsAvailable = read > 0;
                snapshot.electricalBusVoltageCount = read;
                for (int index = 0; index < read; ++index) {
                    const auto value = values[static_cast<std::size_t>(index)];
                    if (std::isfinite(value)) {
                        snapshot.maximumElectricalBusVoltage = (std::max)(
                            snapshot.maximumElectricalBusVoltage,
                            static_cast<double>(value));
                    }
                }
            }
        } else if ((dataRefTypes & (xplmType_Float | xplmType_Double |
                                    xplmType_Int)) != 0) {
            const auto value = ReadScalarDataRef(busVoltsRef_);
            if (std::isfinite(value)) {
                snapshot.electricalBusVoltageFactsAvailable = true;
                snapshot.electricalBusVoltageCount = 1;
                snapshot.maximumElectricalBusVoltage = (std::max)(0.0, value);
            }
        }
    }
    snapshot.onGround = snapshot.altitudeAglFt <= kGroundedAglThresholdFeet &&
                        snapshot.groundSpeedKt <= kGroundedSpeedThresholdKnots;

    auto trackDeg = 0.0;
    if (trackTrueRef_ != nullptr) {
        trackDeg = ReadScalarDataRef(trackTrueRef_);
        snapshot.hasTrack = std::isfinite(trackDeg);
    } else if (headingTrueRef_ != nullptr) {
        trackDeg = ReadScalarDataRef(headingTrueRef_);
        snapshot.hasTrack = std::isfinite(trackDeg);
    }

    if (snapshot.hasTrack) {
        snapshot.trackTrueDeg = NormalizeHeadingDeg(trackDeg);
    }

    return snapshot;
}

void AircraftStateSampler::Reset() {
    latitudeRef_ = nullptr;
    longitudeRef_ = nullptr;
    altitudeMslRef_ = nullptr;
    altitudePilotRef_ = nullptr;
    altitudePilotRefLegacy_ = nullptr;
    altitudeCopilotRef_ = nullptr;
    altimeterPilotRef_ = nullptr;
    altimeterLegacyRef_ = nullptr;
    altitudeAglRef_ = nullptr;
    groundSpeedRef_ = nullptr;
    verticalSpeedRef_ = nullptr;
    trackTrueRef_ = nullptr;
    headingTrueRef_ = nullptr;
    batteryOnRef_ = nullptr;
    busVoltsRef_ = nullptr;
    loggedMissingDataRefs_ = false;
}

void AircraftStateSampler::ResolveDataRefs() {
    auto resolveIfMissing = [](void*& dataRef, const char* name) {
        if (dataRef == nullptr) {
            dataRef = XPLMFindDataRef(name);
        }
    };

    resolveIfMissing(latitudeRef_, "sim/flightmodel/position/latitude");
    resolveIfMissing(longitudeRef_, "sim/flightmodel/position/longitude");
    resolveIfMissing(altitudeMslRef_, "sim/flightmodel/position/elevation");
    resolveIfMissing(altitudePilotRef_, "sim/cockpit2/gauges/indicators/altitude_ft_pilot");
    resolveIfMissing(altitudePilotRefLegacy_, "sim/cockpit/gauges/indicators/altitude_ft_pilot");
    resolveIfMissing(altitudeCopilotRef_, "sim/cockpit2/gauges/indicators/altitude_ft_copilot");
    resolveIfMissing(altimeterPilotRef_, "sim/cockpit2/gauges/actuators/barometer_setting_in_hg_pilot");
    resolveIfMissing(altimeterLegacyRef_, "sim/cockpit/misc/barometer_setting");
    resolveIfMissing(altitudeAglRef_, "sim/flightmodel/position/y_agl");
    resolveIfMissing(groundSpeedRef_, "sim/flightmodel/position/groundspeed");
    resolveIfMissing(verticalSpeedRef_, "sim/flightmodel/position/vh_ind_fpm");
    resolveIfMissing(trackTrueRef_, "sim/flightmodel/position/hpath");
    resolveIfMissing(headingTrueRef_, "sim/flightmodel/position/psi");
    resolveIfMissing(batteryOnRef_, "sim/cockpit2/electrical/battery_on");
    if (batteryOnRef_ == nullptr) {
        resolveIfMissing(batteryOnRef_, "sim/cockpit/electrical/battery_on");
    }
    resolveIfMissing(busVoltsRef_, "sim/cockpit2/electrical/bus_volts");

    if (latitudeRef_ == nullptr) {
        LogMissingDataRefOnce(&loggedMissingDataRefs_, "sim/flightmodel/position/latitude");
    }
    if (longitudeRef_ == nullptr) {
        LogMissingDataRefOnce(&loggedMissingDataRefs_, "sim/flightmodel/position/longitude");
    }
    if (altitudeMslRef_ == nullptr) {
        LogMissingDataRefOnce(&loggedMissingDataRefs_, "sim/flightmodel/position/elevation");
    }
    if (altitudeAglRef_ == nullptr) {
        LogMissingDataRefOnce(&loggedMissingDataRefs_, "sim/flightmodel/position/y_agl");
    }
    if (groundSpeedRef_ == nullptr) {
        LogMissingDataRefOnce(&loggedMissingDataRefs_, "sim/flightmodel/position/groundspeed");
    }
    if (verticalSpeedRef_ == nullptr) {
        LogMissingDataRefOnce(&loggedMissingDataRefs_, "sim/flightmodel/position/vh_ind_fpm");
    }
}

double AircraftStateSampler::ReadScalarDataRef(void* dataRef) const {
    if (dataRef == nullptr) {
        return 0.0;
    }

    const auto typedDataRef = static_cast<XPLMDataRef>(dataRef);
    const auto dataRefTypes = XPLMGetDataRefTypes(typedDataRef);

    if ((dataRefTypes & xplmType_Double) != 0) {
        return XPLMGetDatad(typedDataRef);
    }
    if ((dataRefTypes & xplmType_Float) != 0) {
        return XPLMGetDataf(typedDataRef);
    }
    if ((dataRefTypes & xplmType_Int) != 0) {
        return XPLMGetDatai(typedDataRef);
    }

    return 0.0;
}

bool AircraftStateSampler::ReadBooleanLikeDataRef(void* dataRef) const {
    if (dataRef == nullptr) {
        return false;
    }

    const auto typedDataRef = static_cast<XPLMDataRef>(dataRef);
    const auto dataRefTypes = XPLMGetDataRefTypes(typedDataRef);

    if ((dataRefTypes & xplmType_IntArray) != 0) {
        int value = 0;
        XPLMGetDatavi(typedDataRef, &value, 0, 1);
        return value != 0;
    }

    if ((dataRefTypes & xplmType_Int) != 0) {
        return XPLMGetDatai(typedDataRef) != 0;
    }

    if ((dataRefTypes & xplmType_Float) != 0) {
        return XPLMGetDataf(typedDataRef) > 0.5f;
    }

    if ((dataRefTypes & xplmType_Double) != 0) {
        return XPLMGetDatad(typedDataRef) > 0.5;
    }

    return false;
}

}  // namespace xvatsim::modules::aircraft_state
