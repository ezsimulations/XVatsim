#include "XVatsim/modules/enroute/EnrouteModule.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <unordered_set>
#include <vector>

namespace xvatsim::modules::enroute {

namespace {

std::string NormalizeFrequency(std::string frequency) {
    frequency.erase(
        std::remove_if(
            frequency.begin(),
            frequency.end(),
            [](unsigned char c) { return std::isspace(c) != 0; }),
        frequency.end());

    std::string digits;
    bool sawDecimal = false;
    int decimals = 0;
    for (const auto character : frequency) {
        if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
            digits.push_back(character);
            if (sawDecimal && decimals < 3) {
                ++decimals;
            }
            continue;
        }

        if (character == '.' && !sawDecimal) {
            sawDecimal = true;
        }
    }

    if (digits.empty()) {
        return {};
    }

    if (sawDecimal) {
        while (decimals < 3) {
            digits.push_back('0');
            ++decimals;
        }
    } else if (digits.size() == 5) {
        digits.push_back('0');
    }

    return digits;
}

bool IsGuardFrequency(const std::string& frequency) {
    const auto normalizedFrequency = NormalizeFrequency(frequency);
    return normalizedFrequency == "121500" || normalizedFrequency == "199998";
}

bool IsFrequencyTuned(
    const std::string& targetFrequency,
    const brain::RadioStateSnapshot& radioStateSnapshot) {
    const auto normalizedTarget = NormalizeFrequency(targetFrequency);
    if (normalizedTarget.empty()) {
        return false;
    }

    return NormalizeFrequency(radioStateSnapshot.com1ActiveFrequency) == normalizedTarget ||
           NormalizeFrequency(radioStateSnapshot.com2ActiveFrequency) == normalizedTarget;
}

void AppendUnique(
    const brain::BoardStationSnapshot& station,
    std::vector<brain::BoardStationSnapshot>* stations,
    std::unordered_set<std::string>* keys) {
    if (stations == nullptr || keys == nullptr) {
        return;
    }

    if (IsGuardFrequency(station.frequency)) {
        return;
    }

    auto key = station.callsign + "|" + NormalizeFrequency(station.frequency);
    if (key == "|") {
        key = station.callsign;
    }

    if (!keys->insert(key).second) {
        return;
    }

    stations->push_back(station);
}

std::string FormatRouteDistanceAnnotation(double distanceNm) {
    const auto roundedDistanceNm =
        std::max(0, static_cast<int>(std::round(distanceNm)));
    return std::to_string(roundedDistanceNm) + "nm";
}

void AppendAuthorityController(
    const brain::RelevantAuthoritySnapshot& authority,
    const brain::RadioStateSnapshot& radioStateSnapshot,
    std::vector<brain::BoardStationSnapshot>* routeStations,
    std::unordered_set<std::string>* insertedKeys) {
    if (authority.kind == brain::AuthorityRelevanceKind::Terminal) {
        return;
    }
    if (authority.callsign.empty()) {
        return;
    }

    const auto tuned = IsFrequencyTuned(authority.frequency, radioStateSnapshot);
    const auto sectorActive =
        authority.aircraftInside ||
        (authority.routeIntersects && authority.routeEntryDistanceNm <= 0.0);

    brain::BoardStationSnapshot station;
    station.role = brain::StationRole::Center;
    station.callsign = authority.callsign;
    station.frequency = authority.frequency;
    station.tuned = tuned;
    station.sectorActive = sectorActive;
    station.online = false;
    station.hasRouteEntryDistance = true;
    station.routeEntryDistanceNm = std::max(0.0, authority.routeEntryDistanceNm);
    if (!sectorActive) {
        station.annotation = FormatRouteDistanceAnnotation(authority.routeEntryDistanceNm);
    }

    AppendUnique(
        station,
        routeStations,
        insertedKeys);
}

void CollectAuthorityDrivenCenters(
    const brain::AuthorityRelevanceSnapshot& authorityRelevanceSnapshot,
    const brain::RadioStateSnapshot& radioStateSnapshot,
    std::vector<brain::BoardStationSnapshot>* routeStations,
    std::unordered_set<std::string>* insertedKeys) {
    for (const auto& authority : authorityRelevanceSnapshot.relevantAuthorities) {
        AppendAuthorityController(
            authority,
            radioStateSnapshot,
            routeStations,
            insertedKeys);
    }
}

void SortStations(std::vector<brain::BoardStationSnapshot>* stations) {
    if (stations == nullptr) {
        return;
    }

    std::stable_sort(
        stations->begin(),
        stations->end(),
        [](const auto& left, const auto& right) {
            if (left.hasRouteEntryDistance != right.hasRouteEntryDistance) {
                return left.hasRouteEntryDistance && !right.hasRouteEntryDistance;
            }
            if (left.hasRouteEntryDistance && right.hasRouteEntryDistance &&
                left.routeEntryDistanceNm != right.routeEntryDistanceNm) {
                return left.routeEntryDistanceNm < right.routeEntryDistanceNm;
            }
            if (left.sectorActive != right.sectorActive) {
                return left.sectorActive && !right.sectorActive;
            }
            if (left.tuned != right.tuned) {
                return left.tuned && !right.tuned;
            }
            if (left.frequency != right.frequency) {
                return left.frequency < right.frequency;
            }
            return left.callsign < right.callsign;
        });
}

}  // namespace

brain::ModuleBoardSnapshot EnrouteModule::Collect(
    const brain::XPilotSessionSnapshot& xPilotSessionSnapshot,
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const brain::RadioStateSnapshot& radioStateSnapshot,
    const brain::RouteSectorSnapshot& routeSectorSnapshot,
    const brain::AuthorityRelevanceSnapshot* authorityRelevanceSnapshot) {
    (void)controllerFeedSnapshot;
    (void)routeSectorSnapshot;

    brain::ModuleBoardSnapshot snapshot;
    snapshot.source = brain::BoardSource::Enroute;

    if (!xPilotSessionSnapshot.connected) {
        return snapshot;
    }

    std::vector<brain::BoardStationSnapshot> currentStations;
    std::unordered_set<std::string> insertedKeys;
    bool hasLiveStations = false;

    if (authorityRelevanceSnapshot == nullptr ||
        !authorityRelevanceSnapshot->available ||
        authorityRelevanceSnapshot->stale) {
        return snapshot;
    }

    CollectAuthorityDrivenCenters(
        *authorityRelevanceSnapshot,
        radioStateSnapshot,
        &currentStations,
        &insertedKeys);
    hasLiveStations = !currentStations.empty();
    SortStations(&currentStations);

    snapshot.stations = currentStations;
    snapshot.available = hasLiveStations;
    return snapshot;
}

void EnrouteModule::Reset() {
}

}  // namespace xvatsim::modules::enroute
