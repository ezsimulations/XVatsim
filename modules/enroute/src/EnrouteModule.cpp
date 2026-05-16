#include "XVatsim/modules/enroute/EnrouteModule.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <unordered_set>
#include <vector>

namespace xvatsim::modules::enroute {

namespace {

constexpr int kVatsimFlightServiceFacility = 1;
constexpr int kVatsimCenterFacility = 6;

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

std::string ToUpperCopy(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
    return value;
}

bool SplitControllerCallsign(
    const std::string& callsign,
    std::string* outPrefix,
    std::string* outSuffix) {
    const auto separatorIndex = callsign.rfind('_');
    if (separatorIndex == std::string::npos || separatorIndex == 0 ||
        separatorIndex >= (callsign.size() - 1)) {
        return false;
    }

    if (outPrefix != nullptr) {
        *outPrefix = ToUpperCopy(callsign.substr(0, separatorIndex));
    }
    if (outSuffix != nullptr) {
        *outSuffix = ToUpperCopy(callsign.substr(separatorIndex + 1));
    }
    return true;
}

bool IsActionableCenter(const brain::ControllerSnapshot& controller) {
    if (!controller.actionable || IsGuardFrequency(controller.frequency)) {
        return false;
    }

    std::string prefix;
    if (!SplitControllerCallsign(controller.callsign, &prefix, nullptr) || prefix.empty()) {
        return false;
    }

    return controller.facility == kVatsimCenterFacility ||
           controller.facility == kVatsimFlightServiceFacility;
}

bool SectorMatchesController(
    const brain::RouteSectorMatchSnapshot& sector,
    const std::string& callsign) {
    std::string prefix;
    if (!SplitControllerCallsign(callsign, &prefix, nullptr) || prefix.empty()) {
        return false;
    }

    if (sector.controllerPrefixes.empty()) {
        return false;
    }

    for (const auto& controllerPrefix : sector.controllerPrefixes) {
        if (ToUpperCopy(controllerPrefix) == prefix) {
            return true;
        }
    }
    return false;
}

bool SectorSetMatchesController(
    const std::vector<brain::RouteSectorMatchSnapshot>& sectors,
    const std::string& callsign) {
    for (const auto& sector : sectors) {
        if (SectorMatchesController(sector, callsign)) {
            return true;
        }
    }
    return false;
}

bool RouteHasAuthoritativeSectors(const brain::RouteSectorSnapshot& routeSectorSnapshot) {
    return routeSectorSnapshot.available &&
           !routeSectorSnapshot.stale &&
           routeSectorSnapshot.routeResolved &&
           (!routeSectorSnapshot.currentSectors.empty() ||
            !routeSectorSnapshot.nextSectors.empty());
}

std::string NormalizeSectorIdentifier(std::string identifier) {
    identifier = ToUpperCopy(std::move(identifier));
    const auto suffixSeparator = identifier.find('-');
    if (suffixSeparator != std::string::npos && suffixSeparator > 0) {
        identifier.erase(suffixSeparator);
    }
    return identifier;
}

std::string ResolveSectorRegionKey(const brain::RouteSectorMatchSnapshot& sector) {
    return NormalizeSectorIdentifier(sector.identifier);
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

struct RouteControllerMatch {
    bool current = false;
    double entryDistanceNm = 0.0;
};

bool FindRouteControllerMatch(
    const brain::RouteSectorSnapshot& routeSectorSnapshot,
    const std::string& callsign,
    RouteControllerMatch* outMatch) {
    if (outMatch == nullptr) {
        return false;
    }

    if (SectorSetMatchesController(routeSectorSnapshot.currentSectors, callsign)) {
        outMatch->current = true;
        outMatch->entryDistanceNm = 0.0;
        return true;
    }

    for (const auto& sector : routeSectorSnapshot.nextSectors) {
        if (!SectorMatchesController(sector, callsign)) {
            continue;
        }

        outMatch->current = false;
        outMatch->entryDistanceNm = sector.entryDistanceNm;
        return true;
    }

    return false;
}

void AppendRouteController(
    const brain::ControllerSnapshot& controller,
    bool sectorActive,
    double routeEntryDistanceNm,
    const brain::RadioStateSnapshot& radioStateSnapshot,
    std::vector<brain::BoardStationSnapshot>* routeStations,
    std::unordered_set<std::string>* insertedKeys) {
    const auto tuned = IsFrequencyTuned(controller.frequency, radioStateSnapshot);
    brain::BoardStationSnapshot station;
    station.role = brain::StationRole::Center;
    station.callsign = controller.callsign;
    station.frequency = controller.frequency;
    station.tuned = tuned;
    station.next = false;
    station.standby = false;
    station.sectorActive = sectorActive;
    station.online = false;
    station.hasRouteEntryDistance = true;
    station.routeEntryDistanceNm = std::max(0.0, routeEntryDistanceNm);
    if (!sectorActive) {
        station.annotation = FormatRouteDistanceAnnotation(routeEntryDistanceNm);
    }

    AppendUnique(
        station,
        routeStations,
        insertedKeys);
}

void CollectRouteDrivenCenters(
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const brain::RadioStateSnapshot& radioStateSnapshot,
    const brain::RouteSectorSnapshot& routeSectorSnapshot,
    std::vector<brain::BoardStationSnapshot>* routeStations,
    std::unordered_set<std::string>* insertedKeys) {
    for (const auto& controller : controllerFeedSnapshot.Controllers()) {
        if (!IsActionableCenter(controller)) {
            continue;
        }

        RouteControllerMatch routeMatch;
        if (!FindRouteControllerMatch(
                routeSectorSnapshot,
                controller.callsign,
                &routeMatch)) {
            continue;
        }

        AppendRouteController(
            controller,
            routeMatch.current,
            routeMatch.entryDistanceNm,
            radioStateSnapshot,
            routeStations,
            insertedKeys);
    }
}

void AppendOfflineRouteRows(
    const brain::RouteSectorSnapshot& routeSectorSnapshot,
    std::vector<brain::BoardStationSnapshot>* routeStations,
    std::unordered_set<std::string>* insertedRegionKeys) {
    if (routeStations == nullptr || insertedRegionKeys == nullptr) {
        return;
    }

    auto appendSector = [&](
        const brain::RouteSectorMatchSnapshot& sector,
        bool current) {
        if (sector.controllerPrefixes.empty()) {
            return;
        }

        const auto regionKey = ResolveSectorRegionKey(sector);
        if (regionKey.empty() || !insertedRegionKeys->insert(regionKey).second) {
            return;
        }

        brain::BoardStationSnapshot station;
        station.role = brain::StationRole::Center;
        station.callsign = regionKey;
        station.offline = true;
        station.hasRouteEntryDistance = true;
        station.routeEntryDistanceNm = current ? 0.0 : std::max(0.0, sector.entryDistanceNm);
        if (!current) {
            station.annotation = FormatRouteDistanceAnnotation(sector.entryDistanceNm);
        }
        routeStations->push_back(std::move(station));
    };

    for (const auto& sector : routeSectorSnapshot.currentSectors) {
        appendSector(sector, true);
    }
    for (const auto& sector : routeSectorSnapshot.nextSectors) {
        appendSector(sector, false);
    }
}

void InsertMatchedRouteRegions(
    const brain::RouteSectorSnapshot& routeSectorSnapshot,
    const std::string& callsign,
    std::unordered_set<std::string>* insertedRegionKeys) {
    if (insertedRegionKeys == nullptr) {
        return;
    }

    auto insertMatches = [&](const std::vector<brain::RouteSectorMatchSnapshot>& sectors) {
        for (const auto& sector : sectors) {
            if (SectorMatchesController(sector, callsign)) {
                const auto regionKey = ResolveSectorRegionKey(sector);
                if (!regionKey.empty()) {
                    insertedRegionKeys->insert(regionKey);
                }
            }
        }
    };

    insertMatches(routeSectorSnapshot.currentSectors);
    insertMatches(routeSectorSnapshot.nextSectors);
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
    const brain::RouteSectorSnapshot& routeSectorSnapshot) {
    brain::ModuleBoardSnapshot snapshot;
    snapshot.source = brain::BoardSource::Enroute;

    if (!xPilotSessionSnapshot.connected) {
        return snapshot;
    }

    std::vector<brain::BoardStationSnapshot> currentStations;
    std::unordered_set<std::string> insertedKeys;
    bool hasLiveStations = false;

    if (!RouteHasAuthoritativeSectors(routeSectorSnapshot)) {
        return snapshot;
    }

    CollectRouteDrivenCenters(
        controllerFeedSnapshot,
        radioStateSnapshot,
        routeSectorSnapshot,
        &currentStations,
        &insertedKeys);
    hasLiveStations = !currentStations.empty();
    std::unordered_set<std::string> insertedRegionKeys;
    for (const auto& station : currentStations) {
        InsertMatchedRouteRegions(
            routeSectorSnapshot,
            station.callsign,
            &insertedRegionKeys);
    }
    AppendOfflineRouteRows(
        routeSectorSnapshot,
        &currentStations,
        &insertedRegionKeys);

    SortStations(&currentStations);

    snapshot.stations = currentStations;
    snapshot.available = hasLiveStations;
    snapshot.displayStations = !snapshot.stations.empty();
    return snapshot;
}

void EnrouteModule::Reset() {
}

}  // namespace xvatsim::modules::enroute
