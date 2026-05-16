#include "XVatsim/modules/network_plan_link/NetworkPlanLink.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "XPLMNavigation.h"

namespace xvatsim::modules::network_plan_link {

namespace {

std::string NormalizeIcao(const std::string& airportIcao) {
    std::string normalized;
    normalized.reserve(airportIcao.size());

    for (const auto character : airportIcao) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0) {
            continue;
        }

        normalized.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(character))));
    }

    return normalized;
}

bool ResolveAirportCoordinates(
    const std::string& airportIcao,
    double* outLatitudeDeg,
    double* outLongitudeDeg) {
    const auto normalizedIcao = NormalizeIcao(airportIcao);
    if (normalizedIcao.empty()) {
        return false;
    }

    // Milestone 8: acceptable XPLMFindNavAid use. This resolves only filed
    // VATSIM departure/destination airport coordinates by exact airport ICAO;
    // it must not be used for route waypoint or controller-authority logic.
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
    float latitude = 0.0f;
    float longitude = 0.0f;
    char airportId[32] = {};

    XPLMGetNavAidInfo(
        airportRef,
        &navType,
        &latitude,
        &longitude,
        nullptr,
        nullptr,
        nullptr,
        airportId,
        nullptr,
        nullptr);

    if (navType != xplm_Nav_Airport ||
        NormalizeIcao(airportId) != normalizedIcao) {
        return false;
    }

    if (outLatitudeDeg != nullptr) {
        *outLatitudeDeg = latitude;
    }
    if (outLongitudeDeg != nullptr) {
        *outLongitudeDeg = longitude;
    }

    return true;
}

}  // namespace

void NetworkPlanLink::Reset() {
    hasPlanCache_ = false;
    cachedPlanFromFreshFeed_ = false;
    lastFeedGeneration_ = 0;
    lastNormalizedCallsign_.clear();
    cachedPlanSnapshot_ = {};
    airportCoordinateCache_.clear();
}

brain::NetworkPlanSnapshot NetworkPlanLink::Poll(
    const brain::PilotIdentitySnapshot& pilotIdentitySnapshot,
    const xvatsim::modules::vatsim_data_feed::VatsimDataFeedSnapshot& feedSnapshot) const {
    brain::NetworkPlanSnapshot snapshot;

    if (!pilotIdentitySnapshot.connected) {
        return snapshot;
    }

    if (!feedSnapshot.hasCache) {
        snapshot.statusLine = feedSnapshot.fetchInProgress
                                  ? "PLAN VATSIM pending"
                                  : "PLAN VATSIM unavailable";
        return snapshot;
    }

    if (feedSnapshot.stale &&
        !(hasPlanCache_ &&
          cachedPlanFromFreshFeed_ &&
          lastFeedGeneration_ == feedSnapshot.generation &&
          lastNormalizedCallsign_ == pilotIdentitySnapshot.normalizedCallsign)) {
        snapshot.stale = true;
        snapshot.statusLine = "PLAN VATSIM stale";
        return snapshot;
    }

    if (hasPlanCache_ &&
        lastFeedGeneration_ == feedSnapshot.generation &&
        lastNormalizedCallsign_ == pilotIdentitySnapshot.normalizedCallsign) {
        snapshot = cachedPlanSnapshot_;
    } else {
        snapshot = MatchPilotPlan(pilotIdentitySnapshot, feedSnapshot.pilotPlans);
        cachedPlanSnapshot_ = snapshot;
        hasPlanCache_ = true;
        cachedPlanFromFreshFeed_ = !feedSnapshot.stale;
        lastFeedGeneration_ = feedSnapshot.generation;
        lastNormalizedCallsign_ = pilotIdentitySnapshot.normalizedCallsign;
    }

    snapshot.feedAvailable = !feedSnapshot.stale;
    snapshot.stale = feedSnapshot.stale;
    if (snapshot.stale && !snapshot.statusLine.empty()) {
        snapshot.statusLine += " (stale)";
    }
    return snapshot;
}

brain::NetworkPlanSnapshot NetworkPlanLink::MatchPilotPlan(
    const brain::PilotIdentitySnapshot& pilotIdentitySnapshot,
    const std::vector<xvatsim::modules::vatsim_data_feed::PilotPlanEntry>& cachedPilotPlans) const {
    brain::NetworkPlanSnapshot snapshot;

    if (!pilotIdentitySnapshot.ready) {
        snapshot.statusLine = "PLAN identity unavailable";
        return snapshot;
    }

    const auto matchedPilot = std::find_if(
        cachedPilotPlans.begin(),
        cachedPilotPlans.end(),
        [&](const auto& candidate) {
            return candidate.normalizedCallsign == pilotIdentitySnapshot.normalizedCallsign;
        });

    if (matchedPilot == cachedPilotPlans.end()) {
        snapshot.statusLine = "PLAN VATSIM not matched";
        return snapshot;
    }

    snapshot.matched = true;
    snapshot.cid = matchedPilot->cid;
    snapshot.matchedCallsign = matchedPilot->callsign;
    snapshot.departureIcao = matchedPilot->departureIcao;
    snapshot.destinationIcao = matchedPilot->destinationIcao;
    snapshot.filedCruiseAltitudeFt = matchedPilot->filedCruiseAltitudeFt;
    snapshot.hasFiledCruiseAltitude = matchedPilot->hasFiledCruiseAltitude;
    snapshot.routeText = matchedPilot->routeText;

    if (!snapshot.departureIcao.empty()) {
        snapshot.hasDepartureCoordinates = ResolveAirportCoordinatesCached(
            snapshot.departureIcao,
            &snapshot.departureLatDeg,
            &snapshot.departureLonDeg);
    }

    if (!snapshot.destinationIcao.empty()) {
        snapshot.hasDestinationCoordinates = ResolveAirportCoordinatesCached(
            snapshot.destinationIcao,
            &snapshot.destinationLatDeg,
            &snapshot.destinationLonDeg);
    }

    if (snapshot.departureIcao.empty() && snapshot.destinationIcao.empty()) {
        snapshot.statusLine = "PLAN VATSIM no filed route";
        return snapshot;
    }

    snapshot.statusLine = "PLAN VATSIM ";
    snapshot.statusLine += snapshot.departureIcao.empty() ? "----" : snapshot.departureIcao;
    snapshot.statusLine += " -> ";
    snapshot.statusLine += snapshot.destinationIcao.empty() ? "----" : snapshot.destinationIcao;
    return snapshot;
}

bool NetworkPlanLink::ResolveAirportCoordinatesCached(
    const std::string& airportIcao,
    double* outLatitudeDeg,
    double* outLongitudeDeg) const {
    const auto normalizedIcao = NormalizeIcao(airportIcao);
    if (normalizedIcao.empty()) {
        return false;
    }

    const auto cachedEntry = airportCoordinateCache_.find(normalizedIcao);
    if (cachedEntry != airportCoordinateCache_.end()) {
        if (cachedEntry->second.resolved) {
            if (outLatitudeDeg != nullptr) {
                *outLatitudeDeg = cachedEntry->second.latitudeDeg;
            }
            if (outLongitudeDeg != nullptr) {
                *outLongitudeDeg = cachedEntry->second.longitudeDeg;
            }
        }
        return cachedEntry->second.resolved;
    }

    AirportCoordinateCacheEntry cacheEntry;
    cacheEntry.resolved = ResolveAirportCoordinates(
        normalizedIcao,
        &cacheEntry.latitudeDeg,
        &cacheEntry.longitudeDeg);
    airportCoordinateCache_[normalizedIcao] = cacheEntry;
    if (!cacheEntry.resolved) {
        return false;
    }

    if (outLatitudeDeg != nullptr) {
        *outLatitudeDeg = cacheEntry.latitudeDeg;
    }
    if (outLongitudeDeg != nullptr) {
        *outLongitudeDeg = cacheEntry.longitudeDeg;
    }
    return true;
}

}  // namespace xvatsim::modules::network_plan_link
