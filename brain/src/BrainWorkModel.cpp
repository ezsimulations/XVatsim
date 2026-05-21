#include "XVatsim/brain/BrainWorkModel.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace xvatsim::brain {

namespace {

void HashCombine(std::uint64_t* seed, std::uint64_t value) {
    if (seed == nullptr) {
        return;
    }
    *seed ^= value + 0x9e3779b97f4a7c15ull + (*seed << 6) + (*seed >> 2);
}

void HashCombine(std::uint64_t* seed, const std::string& value) {
    HashCombine(seed, static_cast<std::uint64_t>(value.size()));
    for (const auto character : value) {
        HashCombine(seed, static_cast<std::uint64_t>(
                             static_cast<unsigned char>(character)));
    }
}

void HashCombine(std::uint64_t* seed, double value) {
    const auto scaled = static_cast<std::uint64_t>(value * 1000.0);
    HashCombine(seed, scaled);
}

std::string HashToHex(std::uint64_t hash) {
    std::ostringstream stream;
    stream << std::hex << std::uppercase << hash;
    return stream.str();
}

std::string BuildFlightIdentityKey(const NetworkPlanSnapshot& networkPlanSnapshot) {
    std::ostringstream stream;
    if (!networkPlanSnapshot.matchedCallsign.empty()) {
        stream << networkPlanSnapshot.matchedCallsign;
    }
    stream << "|" << networkPlanSnapshot.departureIcao
           << "|" << networkPlanSnapshot.destinationIcao;
    return stream.str();
}

void HashRouteSector(
    std::uint64_t* hash,
    const RouteSectorMatchSnapshot& sector) {
    HashCombine(hash, sector.identifier);
    HashCombine(hash, sector.entryDistanceNm);
    for (const auto& token : sector.matchTokens) {
        HashCombine(hash, token);
    }
    for (const auto& pattern : sector.controllerCallsignPatterns) {
        HashCombine(hash, pattern);
    }
    for (const auto& prefix : sector.controllerPrefixes) {
        HashCombine(hash, prefix);
    }
    HashCombine(
        hash,
        static_cast<std::uint64_t>(sector.centerCoverage ? 1u : 0u));
    HashCombine(
        hash,
        static_cast<std::uint64_t>(sector.terminalCoverage ? 1u : 0u));
}

void HashBoardStation(
    std::uint64_t* hash,
    const BoardStationSnapshot& station) {
    HashCombine(hash, static_cast<std::uint64_t>(station.role));
    HashCombine(hash, station.callsign);
    HashCombine(hash, station.frequency);
    HashCombine(hash, station.polygonKey);
    HashCombine(hash, static_cast<std::uint64_t>(station.tuned ? 1u : 0u));
    HashCombine(hash, static_cast<std::uint64_t>(station.sectorActive ? 1u : 0u));
    HashCombine(hash, static_cast<std::uint64_t>(station.online ? 1u : 0u));
    HashCombine(hash, static_cast<std::uint64_t>(station.offline ? 1u : 0u));
    HashCombine(
        hash,
        static_cast<std::uint64_t>(station.hasRouteEntryDistance ? 1u : 0u));
    if (station.hasRouteEntryDistance) {
        HashCombine(hash, station.routeEntryDistanceNm);
    }
}

void HashModuleBoardSnapshot(
    std::uint64_t* hash,
    const ModuleBoardSnapshot& snapshot) {
    HashCombine(hash, static_cast<std::uint64_t>(snapshot.available ? 1u : 0u));
    HashCombine(
        hash,
        static_cast<std::uint64_t>(snapshot.displayStations ? 1u : 0u));
    HashCombine(hash, static_cast<std::uint64_t>(snapshot.source));
    HashCombine(hash, snapshot.airportIcao);
    HashCombine(hash, static_cast<std::uint64_t>(snapshot.stations.size()));
    for (const auto& station : snapshot.stations) {
        HashBoardStation(hash, station);
    }
}

void HashAirportSectorSnapshot(
    std::uint64_t* hash,
    const AirportSectorSnapshot& snapshot) {
    HashCombine(hash, static_cast<std::uint64_t>(snapshot.available ? 1u : 0u));
    HashCombine(hash, static_cast<std::uint64_t>(snapshot.stale ? 1u : 0u));
    HashCombine(
        hash,
        static_cast<std::uint64_t>(snapshot.hasCenterCoverageData ? 1u : 0u));
    HashCombine(
        hash,
        static_cast<std::uint64_t>(snapshot.hasTerminalCoverageData ? 1u : 0u));
    HashCombine(hash, snapshot.centerBoundaryGeneration);
    HashCombine(hash, snapshot.authorityCatalogGeneration);
    HashCombine(hash, snapshot.terminalCoverageGeneration);
    HashCombine(hash, snapshot.airportIcao);
    HashCombine(hash, static_cast<std::uint64_t>(snapshot.coveringSectors.size()));
    for (const auto& sector : snapshot.coveringSectors) {
        HashRouteSector(hash, sector);
    }
}

std::string BuildRouteHash(
    const NetworkPlanSnapshot& networkPlanSnapshot,
    const RouteSectorSnapshot& routeSectorSnapshot) {
    std::uint64_t hash = 1469598103934665603ull;
    HashCombine(&hash, networkPlanSnapshot.departureIcao);
    HashCombine(&hash, networkPlanSnapshot.destinationIcao);
    HashCombine(&hash, networkPlanSnapshot.routeText);
    HashCombine(&hash, routeSectorSnapshot.currentSectors.size());
    for (const auto& sector : routeSectorSnapshot.currentSectors) {
        HashRouteSector(&hash, sector);
    }
    HashCombine(&hash, routeSectorSnapshot.nextSectors.size());
    for (const auto& sector : routeSectorSnapshot.nextSectors) {
        HashRouteSector(&hash, sector);
    }
    HashCombine(&hash, routeSectorSnapshot.centerBoundaryGeneration);
    HashCombine(&hash, routeSectorSnapshot.authorityCatalogGeneration);
    return HashToHex(hash);
}

std::string BuildDepartureSnapshotHash(
    const NetworkPlanSnapshot& networkPlanSnapshot,
    const RouteAuthorityPlan& routeAuthorityPlan,
    const AirportSectorSnapshot& airportSectorSnapshot,
    const ModuleBoardSnapshot& boardSnapshot) {
    std::uint64_t hash = 1469598103934665603ull;
    HashCombine(&hash, BuildFlightIdentityKey(networkPlanSnapshot));
    HashCombine(&hash, routeAuthorityPlan.cacheKey);
    HashCombine(&hash, routeAuthorityPlan.routeMapGeneration);
    HashAirportSectorSnapshot(&hash, airportSectorSnapshot);
    HashModuleBoardSnapshot(&hash, boardSnapshot);
    return HashToHex(hash);
}

BrainRoutePolygonState BuildRoutePolygonState(
    const RouteSectorMatchSnapshot& sector,
    bool current) {
    BrainRoutePolygonState state;
    state.polygonKey = sector.identifier;
    state.entryDistanceNm = std::max(0.0, sector.entryDistanceNm);
    state.current = current;
    state.centerCoverage = sector.centerCoverage;
    state.terminalCoverage = sector.terminalCoverage;
    state.matchTokens = sector.matchTokens;
    state.controllerCallsignPatterns = sector.controllerCallsignPatterns;
    state.controllerPrefixes = sector.controllerPrefixes;
    state.hasSourceOwnership =
        !state.controllerCallsignPatterns.empty() ||
        !state.controllerPrefixes.empty();
    state.unresolved = !state.hasSourceOwnership;
    return state;
}

void AddRoutePolygonState(
    std::vector<BrainRoutePolygonState>* states,
    std::unordered_set<std::string>* seenKeys,
    const RouteSectorMatchSnapshot& sector,
    bool current) {
    if (states == nullptr || seenKeys == nullptr || sector.identifier.empty()) {
        return;
    }
    if (!seenKeys->insert(sector.identifier).second) {
        return;
    }
    states->push_back(BuildRoutePolygonState(sector, current));
}

std::string ClassifyRouteAuthorityPlanRebuildReason(
    const RouteAuthorityPlan* activePlan,
    const RouteAuthorityPlan& candidate,
    const std::string* activeCacheKey) {
    if (activeCacheKey == nullptr || activeCacheKey->empty() ||
        activePlan == nullptr || !activePlan->available) {
        return "route-plan-first-build";
    }
    if (activePlan->flightIdentityKey != candidate.flightIdentityKey) {
        if (activePlan->departureIcao != candidate.departureIcao) {
            return "route-plan-departure-changed";
        }
        if (activePlan->destinationIcao != candidate.destinationIcao) {
            return "route-plan-destination-changed";
        }
        return "route-plan-flight-identity-changed";
    }
    if (activePlan->centerBoundaryGeneration !=
        candidate.centerBoundaryGeneration) {
        return "route-plan-center-boundary-generation-changed";
    }
    if (activePlan->authorityCatalogGeneration !=
        candidate.authorityCatalogGeneration) {
        return "route-plan-authority-catalog-generation-changed";
    }
    if (activePlan->routeHash != candidate.routeHash) {
        return "route-plan-route-changed";
    }
    return "route-plan-key-changed";
}

const BrainRoutePolygonState* FindCurrentRoutePolygon(
    const RouteAuthorityPlan& routeAuthorityPlan) {
    for (const auto& polygon : routeAuthorityPlan.polygons) {
        if (polygon.current) {
            return &polygon;
        }
    }
    return routeAuthorityPlan.polygons.empty() ? nullptr
                                               : &routeAuthorityPlan.polygons.front();
}

}  // namespace

const char* ToString(BrainWorkType type) {
    switch (type) {
        case BrainWorkType::BuildRouteScopedMap:
            return "BuildRouteScopedMap";
        case BrainWorkType::ResolveDepartureAirportLocal:
            return "ResolveDepartureAirportLocal";
        case BrainWorkType::ResolveDepartureTerminal:
            return "ResolveDepartureTerminal";
        case BrainWorkType::ResolveCurrentCenter:
            return "ResolveCurrentCenter";
        case BrainWorkType::ResolveNextCenterWindow:
            return "ResolveNextCenterWindow";
        case BrainWorkType::ResolveArrivalAirportLocal:
            return "ResolveArrivalAirportLocal";
        case BrainWorkType::ResolveArrivalTerminal:
            return "ResolveArrivalTerminal";
        case BrainWorkType::RunAuthorityFastPath:
            return "RunAuthorityFastPath";
        case BrainWorkType::RunAuthorityProof:
            return "RunAuthorityProof";
        case BrainWorkType::BuildDepartureSnapshot:
            return "BuildDepartureSnapshot";
        case BrainWorkType::BuildEnrouteSnapshot:
            return "BuildEnrouteSnapshot";
        case BrainWorkType::BuildArrivalSnapshot:
            return "BuildArrivalSnapshot";
        case BrainWorkType::PublishUiSnapshot:
            return "PublishUiSnapshot";
        case BrainWorkType::Diagnostics:
        default:
            return "Diagnostics";
    }
}

const char* ToString(BrainWorkPriority priority) {
    switch (priority) {
        case BrainWorkPriority::SafetyCurrentPosition:
            return "SafetyCurrentPosition";
        case BrainWorkPriority::DepartureAuthority:
            return "DepartureAuthority";
        case BrainWorkPriority::CurrentEnrouteAuthority:
            return "CurrentEnrouteAuthority";
        case BrainWorkPriority::NextCenterLookahead:
            return "NextCenterLookahead";
        case BrainWorkPriority::ArrivalAuthority:
            return "ArrivalAuthority";
        case BrainWorkPriority::EmptyCurrentPolygonRecheck:
            return "EmptyCurrentPolygonRecheck";
        case BrainWorkPriority::FutureRoutePreparation:
            return "FutureRoutePreparation";
        case BrainWorkPriority::Diagnostics:
        default:
            return "Diagnostics";
    }
}

const char* ToString(BrainWorkReason reason) {
    switch (reason) {
        case BrainWorkReason::NewFlightPlan:
            return "NewFlightPlan";
        case BrainWorkReason::RouteIdentityChanged:
            return "RouteIdentityChanged";
        case BrainWorkReason::SourceGenerationChanged:
            return "SourceGenerationChanged";
        case BrainWorkReason::AircraftMovementThreshold:
            return "AircraftMovementThreshold";
        case BrainWorkReason::CurrentPolygonChanged:
            return "CurrentPolygonChanged";
        case BrainWorkReason::NextPolygonLookahead:
            return "NextPolygonLookahead";
        case BrainWorkReason::ArrivalWakeDistance:
            return "ArrivalWakeDistance";
        case BrainWorkReason::ControllerFeedRelevantDiff:
            return "ControllerFeedRelevantDiff";
        case BrainWorkReason::TransceiverFeedRelevantDiff:
            return "TransceiverFeedRelevantDiff";
        case BrainWorkReason::ManualRecovery:
            return "ManualRecovery";
        case BrainWorkReason::ReconnectRecovery:
            return "ReconnectRecovery";
        case BrainWorkReason::DiversionOrReroute:
            return "DiversionOrReroute";
        case BrainWorkReason::StageChanged:
            return "StageChanged";
        case BrainWorkReason::UiRefresh:
            return "UiRefresh";
        case BrainWorkReason::BoardSnapshot:
            return "BoardSnapshot";
        case BrainWorkReason::EmptyPolygonRecheck:
            return "EmptyPolygonRecheck";
        case BrainWorkReason::FutureRoutePrep:
            return "FutureRoutePrep";
        case BrainWorkReason::Unknown:
        default:
            return "Unknown";
    }
}

const char* ToString(BrainWorkBudget budget) {
    switch (budget) {
        case BrainWorkBudget::Light:
            return "Light";
        case BrainWorkBudget::Medium:
            return "Medium";
        case BrainWorkBudget::Heavy:
            return "Heavy";
        default:
            return "Light";
    }
}

const char* ToString(BrainWorkResultStatus status) {
    switch (status) {
        case BrainWorkResultStatus::Completed:
            return "Completed";
        case BrainWorkResultStatus::Deferred:
            return "Deferred";
        case BrainWorkResultStatus::Skipped:
            return "Skipped";
        case BrainWorkResultStatus::Failed:
            return "Failed";
        case BrainWorkResultStatus::NotRun:
        default:
            return "NotRun";
    }
}

const char* ToString(BrainWorkCacheStatus status) {
    switch (status) {
        case BrainWorkCacheStatus::NotCacheable:
            return "NotCacheable";
        case BrainWorkCacheStatus::Hit:
            return "Hit";
        case BrainWorkCacheStatus::Miss:
            return "Miss";
        case BrainWorkCacheStatus::Reused:
            return "Reused";
        case BrainWorkCacheStatus::Invalidated:
            return "Invalidated";
        case BrainWorkCacheStatus::Unknown:
        default:
            return "Unknown";
    }
}

bool IsHeavyBrainWork(const BrainWorkItem& item) {
    return item.budget == BrainWorkBudget::Heavy;
}

bool BrainWorkComesBefore(const BrainWorkItem& left, const BrainWorkItem& right) {
    const auto leftPriority = static_cast<int>(left.priority);
    const auto rightPriority = static_cast<int>(right.priority);
    if (leftPriority != rightPriority) {
        return leftPriority < rightPriority;
    }

    const auto leftBudget = static_cast<int>(left.budget);
    const auto rightBudget = static_cast<int>(right.budget);
    if (leftBudget != rightBudget) {
        return leftBudget < rightBudget;
    }

    if (left.target.polygonSequence != right.target.polygonSequence) {
        if (left.target.polygonSequence < 0) {
            return false;
        }
        if (right.target.polygonSequence < 0) {
            return true;
        }
        return left.target.polygonSequence < right.target.polygonSequence;
    }

    if (left.target.distanceToTargetNm != right.target.distanceToTargetNm) {
        return left.target.distanceToTargetNm < right.target.distanceToTargetNm;
    }

    return left.enqueueSequence < right.enqueueSequence;
}

void SortBrainWorkQueue(std::vector<BrainWorkItem>* items) {
    if (items == nullptr) {
        return;
    }

    std::stable_sort(
        items->begin(),
        items->end(),
        [](const auto& left, const auto& right) {
            return BrainWorkComesBefore(left, right);
        });
}

std::string BrainWorkStableId(const BrainWorkItem& item) {
    std::ostringstream stream;
    stream << ToString(item.type)
           << "|priority=" << ToString(item.priority)
           << "|reason=" << ToString(item.reason)
           << "|budget=" << ToString(item.budget)
           << "|stage=";
    switch (item.target.stage) {
        case WorkflowStage::Departure:
            stream << "DEP";
            break;
        case WorkflowStage::Enroute:
            stream << "ENR";
            break;
        case WorkflowStage::Arrival:
            stream << "ARR";
            break;
        case WorkflowStage::None:
        default:
            stream << "NONE";
            break;
    }
    if (!item.target.airportIcao.empty()) {
        stream << "|airport=" << item.target.airportIcao;
    }
    if (!item.target.polygonKey.empty()) {
        stream << "|polygon=" << item.target.polygonKey;
    }
    if (item.target.polygonSequence >= 0) {
        stream << "|seq=" << item.target.polygonSequence;
    }
    if (!item.cacheKey.empty()) {
        stream << "|cache=" << item.cacheKey;
    }
    return stream.str();
}

RouteAuthorityPlan BuildRouteAuthorityPlanFromRouteSectorSnapshot(
    const NetworkPlanSnapshot& networkPlanSnapshot,
    const RouteSectorSnapshot& routeSectorSnapshot,
    std::uint64_t routeMapGeneration) {
    RouteAuthorityPlan plan;
    plan.available = routeSectorSnapshot.available;
    plan.stale = routeSectorSnapshot.stale;
    plan.routeResolved = routeSectorSnapshot.routeResolved;
    plan.departureIcao = routeSectorSnapshot.departureIcao.empty()
                             ? networkPlanSnapshot.departureIcao
                             : routeSectorSnapshot.departureIcao;
    plan.destinationIcao = routeSectorSnapshot.destinationIcao.empty()
                               ? networkPlanSnapshot.destinationIcao
                               : routeSectorSnapshot.destinationIcao;
    plan.centerBoundaryGeneration = routeSectorSnapshot.centerBoundaryGeneration;
    plan.authorityCatalogGeneration =
        routeSectorSnapshot.authorityCatalogGeneration;
    plan.routeMapGeneration = routeMapGeneration;
    plan.flightIdentityKey = BuildFlightIdentityKey(networkPlanSnapshot);
    plan.routeHash = BuildRouteHash(networkPlanSnapshot, routeSectorSnapshot);
    plan.cacheKey =
        plan.flightIdentityKey + "|" + plan.routeHash +
        "|center=" + std::to_string(plan.centerBoundaryGeneration) +
        "|authority=" + std::to_string(plan.authorityCatalogGeneration);

    if (!routeSectorSnapshot.available) {
        plan.statusLine = "ROUTE_PLAN unavailable";
        plan.diagnosticCacheStatus = "route-plan-input-unavailable";
        plan.diagnosticReason = "route-sector-snapshot-unavailable";
        return plan;
    }
    if (routeSectorSnapshot.stale || !routeSectorSnapshot.routeResolved) {
        plan.statusLine = "ROUTE_PLAN unresolved";
        plan.diagnosticCacheStatus = routeSectorSnapshot.stale
                                         ? "route-plan-input-stale"
                                         : "route-plan-input-unresolved";
        plan.diagnosticReason = routeSectorSnapshot.diagnosticReason.empty()
                                    ? "route-sector-unresolved"
                                    : routeSectorSnapshot.diagnosticReason;
        return plan;
    }

    std::unordered_set<std::string> seenKeys;
    for (const auto& sector : routeSectorSnapshot.currentSectors) {
        AddRoutePolygonState(&plan.polygons, &seenKeys, sector, true);
    }
    for (const auto& sector : routeSectorSnapshot.nextSectors) {
        AddRoutePolygonState(&plan.polygons, &seenKeys, sector, false);
    }

    std::stable_sort(
        plan.polygons.begin(),
        plan.polygons.end(),
        [](const auto& left, const auto& right) {
            if (left.entryDistanceNm != right.entryDistanceNm) {
                return left.entryDistanceNm < right.entryDistanceNm;
            }
            return left.polygonKey < right.polygonKey;
        });

    bool markedNext = false;
    for (std::size_t index = 0; index < plan.polygons.size(); ++index) {
        auto& polygon = plan.polygons[index];
        polygon.sequence = static_cast<int>(index) + 1;
        if (index + 1 < plan.polygons.size()) {
            polygon.exitDistanceNm = plan.polygons[index + 1].entryDistanceNm;
            polygon.hasExitDistance = true;
        }
        if (!polygon.current && !markedNext) {
            polygon.next = true;
            markedNext = true;
        }
    }

    if (!plan.polygons.empty()) {
        plan.polygons.back().arrival = true;
    }

    plan.diagnosticCacheStatus = "route-plan-build";
    plan.diagnosticReason = routeSectorSnapshot.diagnosticReason.empty()
                                ? "route-sector-snapshot"
                                : routeSectorSnapshot.diagnosticReason;
    std::ostringstream status;
    status << "ROUTE_PLAN " << plan.polygons.size() << " polygons";
    if (!plan.polygons.empty()) {
        status << " current=";
        bool first = true;
        for (const auto& polygon : plan.polygons) {
            if (!polygon.current) {
                continue;
            }
            if (!first) {
                status << ",";
            }
            status << polygon.polygonKey;
            first = false;
        }
        status << " next=";
        first = true;
        for (const auto& polygon : plan.polygons) {
            if (!polygon.next) {
                continue;
            }
            if (!first) {
                status << ",";
            }
            status << polygon.polygonKey;
            first = false;
        }
        status << " arrival=" << plan.polygons.back().polygonKey;
    }
    plan.statusLine = status.str();
    return plan;
}

RouteAuthorityPlan UpdateRouteAuthorityPlanCache(
    const NetworkPlanSnapshot& networkPlanSnapshot,
    const RouteSectorSnapshot& routeSectorSnapshot,
    RouteAuthorityPlan* activePlan,
    std::string* activeCacheKey,
    std::uint64_t* activeGeneration) {
    const auto currentGeneration =
        activeGeneration == nullptr ? 0 : *activeGeneration;
    auto candidate =
        BuildRouteAuthorityPlanFromRouteSectorSnapshot(
            networkPlanSnapshot,
            routeSectorSnapshot,
            currentGeneration);

    if (!candidate.available || candidate.stale || !candidate.routeResolved) {
        if (activePlan != nullptr &&
            activePlan->available &&
            activePlan->routeResolved) {
            auto pendingPlan = *activePlan;
            pendingPlan.stale = true;
            pendingPlan.pendingRebuild = true;
            pendingPlan.usingLastProvenPlan = true;
            pendingPlan.previousCacheKey =
                activeCacheKey == nullptr ? std::string{} : *activeCacheKey;
            pendingPlan.pendingCacheKey = candidate.cacheKey;
            pendingPlan.diagnosticCacheStatus =
                "route-plan-pending-last-proven";
            pendingPlan.diagnosticReason =
                candidate.diagnosticReason.empty()
                    ? "route-plan-rebuild-pending"
                    : candidate.diagnosticReason;
            pendingPlan.statusLine = activePlan->statusLine + " pending rebuild";
            return pendingPlan;
        }
        return candidate;
    }

    if (activePlan != nullptr &&
        activeCacheKey != nullptr &&
        !activeCacheKey->empty() &&
        candidate.cacheKey == *activeCacheKey &&
        activePlan->available &&
        !activePlan->stale &&
        activePlan->routeResolved) {
        auto cached = *activePlan;
        cached.diagnosticCacheStatus = "route-plan-cache-hit";
        cached.diagnosticReason = "route-plan-key-unchanged";
        cached.pendingRebuild = false;
        cached.usingLastProvenPlan = false;
        cached.pendingCacheKey.clear();
        cached.previousCacheKey.clear();
        return cached;
    }

    if (activeGeneration != nullptr) {
        candidate.routeMapGeneration = ++(*activeGeneration);
    }
    candidate.diagnosticCacheStatus = "route-plan-rebuild";
    candidate.diagnosticReason = ClassifyRouteAuthorityPlanRebuildReason(
        activePlan,
        candidate,
        activeCacheKey);
    candidate.previousCacheKey =
        activeCacheKey == nullptr ? std::string{} : *activeCacheKey;
    candidate.pendingRebuild = false;
    candidate.usingLastProvenPlan = false;

    if (activePlan != nullptr) {
        *activePlan = candidate;
    }
    if (activeCacheKey != nullptr) {
        *activeCacheKey = candidate.cacheKey;
    }
    return candidate;
}

std::vector<std::string> RouteAuthorityPlanPolygonSequence(
    const RouteAuthorityPlan& plan) {
    std::vector<std::string> sequence;
    sequence.reserve(plan.polygons.size());
    for (const auto& polygon : plan.polygons) {
        sequence.push_back(polygon.polygonKey);
    }
    return sequence;
}

std::vector<std::string> RouteAuthorityPlanFlagSummary(
    const RouteAuthorityPlan& plan) {
    std::vector<std::string> flags;
    flags.reserve(plan.polygons.size());
    for (const auto& polygon : plan.polygons) {
        std::ostringstream stream;
        stream << polygon.polygonKey << ":";
        bool wroteFlag = false;
        auto appendFlag = [&](const char* flag) {
            if (wroteFlag) {
                stream << ">";
            }
            stream << flag;
            wroteFlag = true;
        };
        if (polygon.current) {
            appendFlag("current");
        }
        if (polygon.next) {
            appendFlag("next");
        }
        if (polygon.arrival) {
            appendFlag("arrival");
        }
        if (!wroteFlag) {
            appendFlag("future");
        }
        flags.push_back(stream.str());
    }
    return flags;
}

std::vector<std::string> RouteAuthorityPlanSourceSummary(
    const RouteAuthorityPlan& plan) {
    std::vector<std::string> summaries;
    summaries.reserve(plan.polygons.size());
    for (const auto& polygon : plan.polygons) {
        std::ostringstream stream;
        stream << polygon.polygonKey
               << ":coverage="
               << (polygon.centerCoverage ? "C" : "")
               << (polygon.terminalCoverage ? "T" : "")
               << ":owned=" << (polygon.hasSourceOwnership ? 1 : 0)
               << ":patterns=" << polygon.controllerCallsignPatterns.size()
               << ":prefixes=" << polygon.controllerPrefixes.size();
        summaries.push_back(stream.str());
    }
    return summaries;
}

std::string RouteAuthorityPlanLifecycleSummary(
    const RouteAuthorityPlan& plan) {
    std::ostringstream stream;
    stream << "available=" << (plan.available ? 1 : 0)
           << ":stale=" << (plan.stale ? 1 : 0)
           << ":resolved=" << (plan.routeResolved ? 1 : 0)
           << ":pending=" << (plan.pendingRebuild ? 1 : 0)
           << ":lastProven=" << (plan.usingLastProvenPlan ? 1 : 0)
           << ":polygons=" << plan.polygons.size()
           << ":generation=" << plan.routeMapGeneration;
    if (!plan.diagnosticCacheStatus.empty()) {
        stream << ":cache=" << plan.diagnosticCacheStatus;
    }
    if (!plan.diagnosticReason.empty()) {
        stream << ":reason=" << plan.diagnosticReason;
    }
    return stream.str();
}

std::vector<BrainWorkItem> BuildDepartureAuthorityWorkQueue(
    const NetworkPlanSnapshot& networkPlanSnapshot,
    const RouteAuthorityPlan& routeAuthorityPlan,
    const AirportSectorSnapshot& airportSectorSnapshot,
    std::uint64_t enqueueStart) {
    std::vector<BrainWorkItem> items;
    items.reserve(4);

    const auto flightIdentityKey = BuildFlightIdentityKey(networkPlanSnapshot);
    const auto airportIcao = networkPlanSnapshot.departureIcao.empty()
                                 ? routeAuthorityPlan.departureIcao
                                 : networkPlanSnapshot.departureIcao;
    const auto* currentPolygon = FindCurrentRoutePolygon(routeAuthorityPlan);

    auto sourceGenerations = BrainWorkSourceGenerations{};
    sourceGenerations.centerBoundary =
        airportSectorSnapshot.centerBoundaryGeneration;
    sourceGenerations.authorityCatalog =
        airportSectorSnapshot.authorityCatalogGeneration;
    sourceGenerations.terminalCoverage =
        airportSectorSnapshot.terminalCoverageGeneration;
    sourceGenerations.routeMap = routeAuthorityPlan.routeMapGeneration;

    auto makeItem = [&](BrainWorkType type,
                        BrainWorkReason reason,
                        BrainWorkBudget budget,
                        std::string cacheSuffix) {
        BrainWorkItem item;
        item.type = type;
        item.priority = BrainWorkPriority::DepartureAuthority;
        item.reason = reason;
        item.budget = budget;
        item.target.stage = WorkflowStage::Departure;
        item.target.flightIdentityKey = flightIdentityKey;
        item.target.routeHash = routeAuthorityPlan.routeHash;
        item.target.airportIcao = airportIcao;
        if (currentPolygon != nullptr) {
            item.target.polygonSequence = currentPolygon->sequence;
            item.target.distanceToTargetNm = currentPolygon->entryDistanceNm;
        }
        item.sourceGenerations = sourceGenerations;
        item.cacheKey = "departure|" + flightIdentityKey + "|" +
                        std::move(cacheSuffix);
        item.enqueueSequence = enqueueStart++;
        return item;
    };

    items.push_back(makeItem(
        BrainWorkType::ResolveDepartureAirportLocal,
        BrainWorkReason::NewFlightPlan,
        BrainWorkBudget::Medium,
        "airport-local"));
    items.push_back(makeItem(
        BrainWorkType::ResolveDepartureTerminal,
        BrainWorkReason::NewFlightPlan,
        BrainWorkBudget::Medium,
        "terminal"));
    auto currentCenter = makeItem(
        BrainWorkType::ResolveCurrentCenter,
        BrainWorkReason::CurrentPolygonChanged,
        BrainWorkBudget::Medium,
        "current-center");
    if (currentPolygon != nullptr) {
        currentCenter.target.polygonKey = currentPolygon->polygonKey;
        currentCenter.target.polygonSequence = currentPolygon->sequence;
        currentCenter.target.distanceToTargetNm = currentPolygon->entryDistanceNm;
    }
    items.push_back(std::move(currentCenter));
    items.push_back(makeItem(
        BrainWorkType::BuildDepartureSnapshot,
        BrainWorkReason::BoardSnapshot,
        BrainWorkBudget::Medium,
        "board"));

    return items;
}

DepartureAuthoritySnapshot BuildDepartureAuthoritySnapshot(
    const NetworkPlanSnapshot& networkPlanSnapshot,
    const RouteAuthorityPlan& routeAuthorityPlan,
    const AirportSectorSnapshot& airportSectorSnapshot,
    const ModuleBoardSnapshot& boardSnapshot,
    std::uint64_t snapshotGeneration) {
    DepartureAuthoritySnapshot snapshot;
    snapshot.airportIcao = networkPlanSnapshot.departureIcao.empty()
                               ? airportSectorSnapshot.airportIcao
                               : networkPlanSnapshot.departureIcao;
    snapshot.airportSectorSnapshot = airportSectorSnapshot;
    snapshot.boardSnapshot = boardSnapshot;
    snapshot.snapshotGeneration = snapshotGeneration;
    snapshot.sourceGenerations.centerBoundary =
        airportSectorSnapshot.centerBoundaryGeneration;
    snapshot.sourceGenerations.authorityCatalog =
        airportSectorSnapshot.authorityCatalogGeneration;
    snapshot.sourceGenerations.terminalCoverage =
        airportSectorSnapshot.terminalCoverageGeneration;
    snapshot.sourceGenerations.routeMap = routeAuthorityPlan.routeMapGeneration;
    snapshot.available =
        !snapshot.airportIcao.empty() &&
        boardSnapshot.source == BoardSource::Departure;
    snapshot.stale =
        airportSectorSnapshot.stale ||
        (routeAuthorityPlan.available && routeAuthorityPlan.stale);
    snapshot.cacheKey =
        BuildFlightIdentityKey(networkPlanSnapshot) + "|dep|" +
        BuildDepartureSnapshotHash(
            networkPlanSnapshot,
            routeAuthorityPlan,
            airportSectorSnapshot,
            boardSnapshot);

    if (!snapshot.available) {
        snapshot.diagnosticCacheStatus = "departure-snapshot-unavailable";
        snapshot.diagnosticReason = snapshot.airportIcao.empty()
                                        ? "departure-airport-missing"
                                        : "departure-board-not-built";
        snapshot.statusLine = "DEPARTURE snapshot unavailable";
        return snapshot;
    }

    snapshot.diagnosticCacheStatus = "departure-snapshot-build";
    snapshot.diagnosticReason = snapshot.stale
                                    ? "departure-input-stale"
                                    : "departure-input-current";
    std::ostringstream status;
    status << "DEPARTURE snapshot stations="
           << snapshot.boardSnapshot.stations.size()
           << " airport=" << snapshot.airportIcao;
    snapshot.statusLine = status.str();
    return snapshot;
}

DepartureAuthoritySnapshot UpdateDepartureAuthoritySnapshotCache(
    const NetworkPlanSnapshot& networkPlanSnapshot,
    const RouteAuthorityPlan& routeAuthorityPlan,
    const AirportSectorSnapshot& airportSectorSnapshot,
    const ModuleBoardSnapshot& boardSnapshot,
    DepartureAuthoritySnapshot* activeSnapshot,
    std::string* activeCacheKey,
    std::uint64_t* activeGeneration) {
    const auto currentGeneration =
        activeGeneration == nullptr ? 0 : *activeGeneration;
    auto candidate = BuildDepartureAuthoritySnapshot(
        networkPlanSnapshot,
        routeAuthorityPlan,
        airportSectorSnapshot,
        boardSnapshot,
        currentGeneration);

    if (!candidate.available || candidate.stale) {
        if (activeSnapshot != nullptr &&
            activeSnapshot->available &&
            !activeSnapshot->boardSnapshot.stations.empty()) {
            auto pendingSnapshot = *activeSnapshot;
            pendingSnapshot.stale = true;
            pendingSnapshot.pendingRebuild = true;
            pendingSnapshot.usingLastProvenSnapshot = true;
            pendingSnapshot.previousCacheKey =
                activeCacheKey == nullptr ? std::string{} : *activeCacheKey;
            pendingSnapshot.pendingCacheKey = candidate.cacheKey;
            pendingSnapshot.diagnosticCacheStatus =
                "departure-snapshot-pending-last-proven";
            pendingSnapshot.diagnosticReason =
                candidate.diagnosticReason.empty()
                    ? "departure-snapshot-rebuild-pending"
                    : candidate.diagnosticReason;
            pendingSnapshot.statusLine =
                activeSnapshot->statusLine + " pending rebuild";
            return pendingSnapshot;
        }
        return candidate;
    }

    if (activeSnapshot != nullptr &&
        activeCacheKey != nullptr &&
        !activeCacheKey->empty() &&
        candidate.cacheKey == *activeCacheKey &&
        activeSnapshot->available &&
        !activeSnapshot->stale) {
        auto cached = *activeSnapshot;
        cached.diagnosticCacheStatus = "departure-snapshot-cache-hit";
        cached.diagnosticReason = "departure-snapshot-key-unchanged";
        cached.pendingRebuild = false;
        cached.usingLastProvenSnapshot = false;
        cached.pendingCacheKey.clear();
        cached.previousCacheKey.clear();
        return cached;
    }

    if (activeGeneration != nullptr) {
        candidate.snapshotGeneration = ++(*activeGeneration);
    }
    candidate.diagnosticCacheStatus = "departure-snapshot-rebuild";
    candidate.diagnosticReason =
        activeCacheKey == nullptr || activeCacheKey->empty()
            ? "departure-snapshot-first-build"
            : "departure-snapshot-key-changed";
    candidate.previousCacheKey =
        activeCacheKey == nullptr ? std::string{} : *activeCacheKey;
    candidate.pendingRebuild = false;
    candidate.usingLastProvenSnapshot = false;

    if (activeSnapshot != nullptr) {
        *activeSnapshot = candidate;
    }
    if (activeCacheKey != nullptr) {
        *activeCacheKey = candidate.cacheKey;
    }
    return candidate;
}

std::string DepartureAuthoritySnapshotLifecycleSummary(
    const DepartureAuthoritySnapshot& snapshot) {
    std::ostringstream stream;
    stream << "available=" << (snapshot.available ? 1 : 0)
           << ":stale=" << (snapshot.stale ? 1 : 0)
           << ":pending=" << (snapshot.pendingRebuild ? 1 : 0)
           << ":lastProven=" << (snapshot.usingLastProvenSnapshot ? 1 : 0)
           << ":stations=" << snapshot.boardSnapshot.stations.size()
           << ":generation=" << snapshot.snapshotGeneration;
    if (!snapshot.diagnosticCacheStatus.empty()) {
        stream << ":cache=" << snapshot.diagnosticCacheStatus;
    }
    if (!snapshot.diagnosticReason.empty()) {
        stream << ":reason=" << snapshot.diagnosticReason;
    }
    return stream.str();
}

}  // namespace xvatsim::brain
