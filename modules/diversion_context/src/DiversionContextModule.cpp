#include "XVatsim/modules/diversion_context/DiversionContextModule.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "XPLMNavigation.h"

namespace xvatsim::modules::diversion_context {

namespace {

std::string NormalizeIcao(const std::string& airportIcao) {
    std::string normalized;
    normalized.reserve(airportIcao.size());
    for (const auto character : airportIcao) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0) {
            continue;
        }

        normalized.push_back(
            static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
    }
    return normalized;
}

bool ResolveAirport(
    const std::string& airportIcao,
    std::string* outResolvedIcao,
    double* outLatitudeDeg,
    double* outLongitudeDeg) {
    const auto normalizedIcao = NormalizeIcao(airportIcao);
    if (normalizedIcao.size() != 4) {
        return false;
    }

    // Milestone 8: acceptable XPLMFindNavAid use. Manual diversion accepts only
    // an exact airport ICAO resolved as an airport; this does not participate in
    // route waypoint expansion or controller-authority matching.
    const auto airportRef = XPLMFindNavAid(
        nullptr,
        normalizedIcao.c_str(),
        nullptr,
        nullptr,
        nullptr,
        xplm_Nav_Airport);
    if (airportRef == XPLM_NAV_NOT_FOUND) {
        return false;
    }

    XPLMNavType navType = 0;
    float latitudeDeg = 0.0f;
    float longitudeDeg = 0.0f;
    char airportId[32] = {};
    XPLMGetNavAidInfo(
        airportRef,
        &navType,
        &latitudeDeg,
        &longitudeDeg,
        nullptr,
        nullptr,
        nullptr,
        airportId,
        nullptr,
        nullptr);
    const auto resolvedIcao = NormalizeIcao(airportId);
    if (navType != xplm_Nav_Airport || resolvedIcao != normalizedIcao) {
        return false;
    }

    if (outResolvedIcao != nullptr) {
        *outResolvedIcao = resolvedIcao;
    }
    if (outLatitudeDeg != nullptr) {
        *outLatitudeDeg = latitudeDeg;
    }
    if (outLongitudeDeg != nullptr) {
        *outLongitudeDeg = longitudeDeg;
    }
    return true;
}

}  // namespace

DiversionUpdateResult DiversionContextModule::SetDiversionAirport(
    const std::string& airportIcao) {
    DiversionUpdateResult result;

    std::string resolvedIcao;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    if (!ResolveAirport(
            airportIcao,
            &resolvedIcao,
            &latitudeDeg,
            &longitudeDeg)) {
        result.statusLine = "DIVERT invalid airport";
        return result;
    }

    result.accepted = true;
    result.airportIcao = resolvedIcao;
    result.changed =
        !overrideSnapshot_.active ||
        overrideSnapshot_.destinationIcao != resolvedIcao ||
        !overrideSnapshot_.hasDestinationCoordinates ||
        overrideSnapshot_.destinationLatDeg != latitudeDeg ||
        overrideSnapshot_.destinationLonDeg != longitudeDeg;

    overrideSnapshot_.active = true;
    overrideSnapshot_.destinationIcao = resolvedIcao;
    overrideSnapshot_.destinationLatDeg = latitudeDeg;
    overrideSnapshot_.destinationLonDeg = longitudeDeg;
    overrideSnapshot_.hasDestinationCoordinates = true;
    overrideSnapshot_.statusLine = "DIVERT " + resolvedIcao;

    result.statusLine = "DIVERT " + resolvedIcao + " active";
    return result;
}

DiversionUpdateResult DiversionContextModule::ClearOverride() {
    DiversionUpdateResult result;
    result.accepted = overrideSnapshot_.active;
    result.changed = overrideSnapshot_.active;
    if (!overrideSnapshot_.active) {
        result.statusLine = "DIVERT no override active";
        return result;
    }

    result.airportIcao = overrideSnapshot_.destinationIcao;
    result.statusLine = "DIVERT cleared";
    overrideSnapshot_ = {};
    return result;
}

const DiversionOverrideSnapshot& DiversionContextModule::Snapshot() const {
    return overrideSnapshot_;
}

bool DiversionContextModule::HasOverride() const {
    return overrideSnapshot_.active && overrideSnapshot_.hasDestinationCoordinates &&
           !overrideSnapshot_.destinationIcao.empty();
}

brain::NetworkPlanSnapshot DiversionContextModule::BuildEffectivePlan(
    const brain::NetworkPlanSnapshot& networkPlanSnapshot) const {
    if (!HasOverride()) {
        return networkPlanSnapshot;
    }

    auto effectivePlan = networkPlanSnapshot;
    effectivePlan.destinationIcao = overrideSnapshot_.destinationIcao;
    effectivePlan.destinationLatDeg = overrideSnapshot_.destinationLatDeg;
    effectivePlan.destinationLonDeg = overrideSnapshot_.destinationLonDeg;
    effectivePlan.hasDestinationCoordinates = true;
    effectivePlan.routeText = overrideSnapshot_.destinationIcao;

    const auto departureIcao =
        effectivePlan.departureIcao.empty() ? std::string("----") : effectivePlan.departureIcao;
    effectivePlan.statusLine =
        "DIVERT " + departureIcao + " -> " + overrideSnapshot_.destinationIcao;
    if (effectivePlan.stale) {
        effectivePlan.statusLine += " (stale)";
    }
    return effectivePlan;
}

void DiversionContextModule::Reset() {
    overrideSnapshot_ = {};
}

}  // namespace xvatsim::modules::diversion_context
