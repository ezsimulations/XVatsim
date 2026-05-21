#include "XVatsim/brain/BrainOwnedWorkerTypes.h"

#include <algorithm>
#include <functional>

namespace xvatsim::brain {
namespace {

void HashCombine(std::size_t* seed, std::size_t value) {
    if (seed == nullptr) {
        return;
    }
    *seed ^= value + 0x9e3779b9U + (*seed << 6U) + (*seed >> 2U);
}

void HashCombineBool(std::size_t* seed, bool value) {
    HashCombine(seed, value ? 0x9e3779b9U : 0x85ebca6bU);
}

void HashCombineString(std::size_t* seed, const std::string& value) {
    HashCombine(seed, std::hash<std::string>{}(value));
}

void HashCombineDouble(std::size_t* seed, double value) {
    HashCombine(seed, std::hash<double>{}(value));
}

std::size_t HashRouteSectorMatch(const RouteSectorMatchSnapshot& match) {
    std::size_t hash = 0;
    HashCombineString(&hash, match.identifier);
    HashCombineDouble(&hash, match.entryDistanceNm);
    for (const auto& token : match.matchTokens) {
        HashCombineString(&hash, token);
    }
    for (const auto& pattern : match.controllerCallsignPatterns) {
        HashCombineString(&hash, pattern);
    }
    for (const auto& prefix : match.controllerPrefixes) {
        HashCombineString(&hash, prefix);
    }
    HashCombineBool(&hash, match.centerCoverage);
    HashCombineBool(&hash, match.terminalCoverage);
    return hash;
}

std::string FirstRoutePolygonKey(
    const std::vector<RouteSectorMatchSnapshot>& sectors) {
    for (const auto& sector : sectors) {
        if (!sector.identifier.empty()) {
            return sector.identifier;
        }
    }
    return {};
}

std::string LastRoutePolygonKey(const RouteSectorSnapshot& route) {
    std::string lastKey = FirstRoutePolygonKey(route.currentSectors);
    double lastEntryDistanceNm = -1.0;
    for (const auto& sector : route.currentSectors) {
        if (!sector.identifier.empty() &&
            sector.entryDistanceNm >= lastEntryDistanceNm) {
            lastKey = sector.identifier;
            lastEntryDistanceNm = sector.entryDistanceNm;
        }
    }
    for (const auto& sector : route.nextSectors) {
        if (!sector.identifier.empty() &&
            sector.entryDistanceNm >= lastEntryDistanceNm) {
            lastKey = sector.identifier;
            lastEntryDistanceNm = sector.entryDistanceNm;
        }
    }
    return lastKey;
}

}  // namespace

std::uint64_t HashBrainRouteSectorSnapshot(
    const RouteSectorSnapshot& snapshot) {
    std::size_t hash = 0;
    HashCombineBool(&hash, snapshot.available);
    HashCombineBool(&hash, snapshot.stale);
    HashCombineBool(&hash, snapshot.routeResolved);
    HashCombineString(&hash, snapshot.statusLine);
    HashCombine(&hash, snapshot.centerBoundaryGeneration);
    HashCombine(&hash, snapshot.authorityCatalogGeneration);
    HashCombineString(&hash, snapshot.departureIcao);
    HashCombineString(&hash, snapshot.destinationIcao);
    HashCombine(&hash, snapshot.currentSectors.size());
    for (const auto& sector : snapshot.currentSectors) {
        HashCombine(&hash, HashRouteSectorMatch(sector));
    }
    HashCombine(&hash, snapshot.nextSectors.size());
    for (const auto& sector : snapshot.nextSectors) {
        HashCombine(&hash, HashRouteSectorMatch(sector));
    }
    return static_cast<std::uint64_t>(hash);
}

BrainRoutePolygonWorkerOutput BuildBrainRoutePolygonWorkerOutput(
    const RouteSectorSnapshot& route) {
    BrainRoutePolygonWorkerOutput output;
    output.route = route;
    output.available = output.route.available;
    output.stale = output.route.stale;
    output.routePolygonHash = HashBrainRouteSectorSnapshot(output.route);
    output.currentPolygonIndex = output.route.currentSectors.empty() ? 0 : 1;
    output.currentPolygonKey = FirstRoutePolygonKey(output.route.currentSectors);
    output.nextPolygonKey = FirstRoutePolygonKey(output.route.nextSectors);
    output.arrivalPolygonKey = LastRoutePolygonKey(output.route);
    output.finalRoutePolygonKey = output.arrivalPolygonKey;
    output.reason = output.route.diagnosticReason.empty()
                        ? "route-polygon-worker"
                        : output.route.diagnosticReason;
    return output;
}

}  // namespace xvatsim::brain
