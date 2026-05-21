#include "XVatsim/brain/BrainOwnedWorkerTypes.h"

#include "XVatsim/brain/RoutePolygonTransition.h"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <sstream>

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

std::string FormatFixed(double value, int precision) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

BrainRoutePolygonWorkerOutput RouteOutputFromState(
    const BrainOwnedRuntimeState& state,
    std::string reason) {
    BrainRoutePolygonWorkerOutput output;
    output.available = state.routePolygonSnapshot.available;
    output.stale = state.routePolygonSnapshot.stale;
    output.route = state.routePolygonSnapshot;
    output.routePolygonHash = state.routePolygonHash;
    output.currentPolygonIndex = state.currentPolygonIndex;
    output.currentPolygonKey = state.currentPolygonKey;
    output.nextPolygonKey = state.nextPolygonKey;
    output.arrivalPolygonKey = state.arrivalPolygonKey;
    output.finalRoutePolygonKey = state.finalRoutePolygonKey;
    output.reason = std::move(reason);
    return output;
}

void ResetRoutePolygonState(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->hasRoutePolygonSnapshot = false;
    state->routePlanKey.clear();
    state->routePolygonSnapshot = {};
    state->routePolygonHash = 0;
    state->currentPolygonIndex = 0;
    state->currentPolygonKey.clear();
    state->nextPolygonKey.clear();
    state->arrivalPolygonKey.clear();
    state->finalRoutePolygonKey.clear();
    state->routeProgressDistanceNm = 0.0;
    state->lastRoutePolygonTransitionReason.clear();
    state->lastRoutePolygonTransitionChanged = false;
}

RoutePolygonTransitionWorkerOutput ApplyRoutePolygonTransition(
    BrainOwnedRuntimeState* state,
    const AircraftStateSnapshot& aircraft,
    BrainRoutePolygonWorkerOutput* output) {
    RoutePolygonTransitionWorkerOutput transition;
    if (state == nullptr || output == nullptr || !output->route.available ||
        output->route.stale || !output->route.routeResolved) {
        return transition;
    }

    RoutePolygonTransitionWorkerInput input;
    input.aircraft = aircraft;
    input.route = output->route;
    input.previousPolygonKey = state->currentPolygonKey;
    transition = RunRoutePolygonTransitionWorker(input);

    if (transition.available && transition.routeResolved) {
        output->route = transition.route;
        output->routePolygonHash =
            HashBrainRouteSectorSnapshot(output->route);
        output->currentPolygonIndex = transition.currentPolygonIndex;
        output->currentPolygonKey = transition.currentPolygonKey;
        output->nextPolygonKey = transition.nextPolygonKey;
        output->arrivalPolygonKey = transition.finalRoutePolygonKey.empty()
                                        ? output->arrivalPolygonKey
                                        : transition.finalRoutePolygonKey;
        output->finalRoutePolygonKey = transition.finalRoutePolygonKey;
        state->routeProgressDistanceNm = transition.progressDistanceNm;
        state->finalRoutePolygonKey = transition.finalRoutePolygonKey;
        state->lastRoutePolygonTransitionReason = transition.reason;
        state->lastRoutePolygonTransitionChanged = transition.changed;
    }
    return transition;
}

std::string TransitionDiagnosticResult(
    const RoutePolygonTransitionWorkerOutput& transition) {
    std::ostringstream result;
    result << "available=" << (transition.available ? 1 : 0)
           << ",stale=" << (transition.stale ? 1 : 0)
           << ",resolved=" << (transition.routeResolved ? 1 : 0)
           << ",changed=" << (transition.changed ? 1 : 0)
           << ",wakeUi=" << (transition.shouldWakeUi ? 1 : 0)
           << ",final=" << (transition.enteredFinalRoutePolygon ? 1 : 0)
           << ",progressNm=" << FormatFixed(transition.progressDistanceNm, 1)
           << ",previous=" << transition.previousPolygonKey
           << ",current=" << transition.currentPolygonKey
           << ",next=" << transition.nextPolygonKey
           << ",finalKey=" << transition.finalRoutePolygonKey;
    return result.str();
}

std::string RouteDiagnosticResult(
    const BrainRoutePolygonWorkerOutput& output,
    bool routeChanged,
    bool transitionChanged,
    double progressDistanceNm,
    const std::string& previousPolygonKey = {}) {
    std::ostringstream result;
    result << "available=" << (output.available ? 1 : 0)
           << ",stale=" << (output.stale ? 1 : 0)
           << ",resolved=" << (output.route.routeResolved ? 1 : 0)
           << ",current=" << output.currentPolygonKey
           << ",next=" << output.nextPolygonKey
           << ",final=" << output.finalRoutePolygonKey
           << ",hash=" << output.routePolygonHash;
    if (!previousPolygonKey.empty()) {
        result << ",transition=" << (transitionChanged ? 1 : 0)
               << ",previous=" << previousPolygonKey;
    } else {
        result << ",changed=" << (routeChanged ? 1 : 0)
               << ",transition=" << (transitionChanged ? 1 : 0);
    }
    result << ",progressNm=" << FormatFixed(progressDistanceNm, 1);
    return result.str();
}

void StoreRoutePolygonOutput(
    BrainOwnedRuntimeState* state,
    const std::string& routeRuntimeKey,
    long long nowSeconds,
    const BrainRoutePolygonWorkerOutput& output) {
    if (state == nullptr) {
        return;
    }
    state->hasRoutePolygonSnapshot = true;
    state->lastRoutePolygonRefreshSeconds = nowSeconds;
    state->routePlanKey = routeRuntimeKey;
    state->routePolygonSnapshot = output.route;
    state->routePolygonHash = output.routePolygonHash;
    state->currentPolygonIndex = output.currentPolygonIndex;
    state->currentPolygonKey = output.currentPolygonKey;
    state->nextPolygonKey = output.nextPolygonKey;
    state->arrivalPolygonKey = output.arrivalPolygonKey;
    state->finalRoutePolygonKey = output.finalRoutePolygonKey;
    state->lastRoutePolygonHash = output.routePolygonHash;
}

void InvalidateRelevanceForRouteChange(
    BrainOwnedRuntimeState* state,
    const BrainRoutePolygonWorkerOutput& output,
    bool transitionChanged) {
    if (state == nullptr) {
        return;
    }
    state->candidateCompletions.clear();
    state->candidatesComplete = false;
    state->lastWakeReason =
        transitionChanged
            ? (output.currentPolygonKey == output.finalRoutePolygonKey
                   ? "route-polygon-transition-final"
                   : "route-polygon-transition")
            : "route-polygon-changed";
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

BrainOwnedRoutePolygonRuntimeOutput BeginBrainOwnedRoutePolygonRefresh(
    BrainOwnedRuntimeState* state,
    const BrainOwnedRoutePolygonRefreshInput& input) {
    BrainOwnedRoutePolygonRuntimeOutput output;
    if (input.routeRuntimeKey.empty()) {
        ResetRoutePolygonState(state);
        output.reset = true;
        output.reason = "route-plan-unavailable";
        output.cacheStatus = "route-polygon-input-unavailable";
        output.diagnosticResult = "available=0,stale=1,resolved=0";
        return output;
    }

    const auto sameRoute =
        state != nullptr &&
        state->hasRoutePolygonSnapshot &&
        state->routePlanKey == input.routeRuntimeKey;
    const auto cachedRouteUsable =
        sameRoute &&
        state->routePolygonSnapshot.available &&
        !state->routePolygonSnapshot.stale &&
        state->routePolygonSnapshot.routeResolved;
    const auto pendingRetryDue =
        sameRoute &&
        !cachedRouteUsable &&
        (input.nowSeconds - state->lastRoutePolygonRefreshSeconds) >=
            input.pendingRetrySeconds;

    if (!sameRoute || (!cachedRouteUsable && pendingRetryDue)) {
        output.needsWorker = true;
        return output;
    }

    output.cacheHit = true;
    output.reason = cachedRouteUsable ? "route-polygon-unchanged"
                                      : "route-polygon-pending-retry";
    output.cacheStatus = cachedRouteUsable ? "route-polygon-cache-hit"
                                           : "route-polygon-cache-wait";
    output.route = RouteOutputFromState(*state, output.reason);
    const auto previousPolygonKey = state->currentPolygonKey;

    RoutePolygonTransitionWorkerOutput transition;
    if (cachedRouteUsable) {
        output.transitionEvaluated = true;
        transition =
            ApplyRoutePolygonTransition(
                state,
                input.aircraft,
                &output.route);
        output.transitionChanged = transition.changed;
        output.transitionReason = transition.reason;
        output.transitionCacheStatus =
            transition.changed ? "route-polygon-transition"
                               : "route-polygon-stable";
        output.transitionDiagnosticResult =
            TransitionDiagnosticResult(transition);
    }

    if (output.transitionChanged) {
        StoreRoutePolygonOutput(
            state,
            input.routeRuntimeKey,
            state->lastRoutePolygonRefreshSeconds,
            output.route);
        InvalidateRelevanceForRouteChange(
            state,
            output.route,
            true);
        output.reason = "route-polygon-transition-applied";
        output.routeChanged = true;
    }

    output.diagnosticResult =
        RouteDiagnosticResult(
            output.route,
            output.routeChanged,
            output.transitionChanged,
            state->routeProgressDistanceNm,
            previousPolygonKey);
    return output;
}

BrainOwnedRoutePolygonRuntimeOutput CommitBrainOwnedRoutePolygonRefresh(
    BrainOwnedRuntimeState* state,
    const BrainOwnedRoutePolygonRefreshInput& input,
    const BrainRoutePolygonWorkerOutput& workerOutput) {
    BrainOwnedRoutePolygonRuntimeOutput output;
    output.route = workerOutput;

    const auto shouldEvaluateTransition =
        output.route.route.available &&
        !output.route.route.stale &&
        output.route.route.routeResolved;
    RoutePolygonTransitionWorkerOutput transition;
    if (shouldEvaluateTransition) {
        output.transitionEvaluated = true;
        transition =
            ApplyRoutePolygonTransition(
                state,
                input.aircraft,
                &output.route);
        output.transitionChanged = transition.changed;
        output.transitionReason = transition.reason;
        output.transitionCacheStatus =
            transition.changed ? "route-polygon-transition"
                               : "route-polygon-stable";
        output.transitionDiagnosticResult =
            TransitionDiagnosticResult(transition);
    }

    output.routeChanged =
        state == nullptr ||
        !state->hasRoutePolygonSnapshot ||
        state->routePlanKey != input.routeRuntimeKey ||
        state->routePolygonHash != output.route.routePolygonHash ||
        state->currentPolygonKey != output.route.currentPolygonKey ||
        output.transitionChanged;

    StoreRoutePolygonOutput(
        state,
        input.routeRuntimeKey,
        input.nowSeconds,
        output.route);
    if (output.routeChanged) {
        InvalidateRelevanceForRouteChange(
            state,
            output.route,
            output.transitionChanged);
    }

    output.reason = output.route.reason;
    output.cacheStatus =
        output.route.route.diagnosticCacheStatus.empty()
            ? "route-polygon-worker"
            : output.route.route.diagnosticCacheStatus;
    output.diagnosticResult =
        RouteDiagnosticResult(
            output.route,
            output.routeChanged,
            output.transitionChanged,
            state != nullptr ? state->routeProgressDistanceNm : 0.0);
    return output;
}

}  // namespace xvatsim::brain
