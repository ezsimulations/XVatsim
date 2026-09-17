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
    output.available = state.routePolygonSnapshot != nullptr &&
        state.routePolygonSnapshot->available;
    output.stale = state.routePolygonSnapshot == nullptr ||
        state.routePolygonSnapshot->stale;
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
    state->routePolygonSnapshot.reset();
    state->routePolygonHash = 0;
    state->authorityRouteDigest = 0;
    state->authorityRouteSnapshot.reset();
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
    if (state == nullptr || output == nullptr || output->route == nullptr ||
        !output->route->available || output->route->stale ||
        !output->route->routeResolved) {
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
            HashBrainRouteSectorSnapshot(*output->route);
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
           << ",resolved="
           << (output.route != nullptr && output.route->routeResolved ? 1 : 0)
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
    state->authorityRouteDigest = output.route != nullptr
        ? HashBrainAuthorityRouteSnapshot(*output.route)
        : 0;
    state->authorityRouteSnapshot = output.route;
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
    for (const auto& alias : snapshot.airportCallsignAliases) {
        HashCombine(&hash, std::hash<std::string>{}(alias.airportIcao));
        HashCombine(&hash, std::hash<std::string>{}(alias.callsignPrefix));
        HashCombine(&hash, std::hash<std::string>{}(alias.boundaryId));
        HashCombine(&hash, std::hash<std::string>{}(alias.source));
    }
    return static_cast<std::uint64_t>(hash);
}

std::uint64_t HashBrainAuthorityRouteSnapshot(
    const RouteSectorSnapshot& snapshot) {
    std::size_t hash = static_cast<std::size_t>(
        HashBrainRouteSectorSnapshot(snapshot));
    HashCombine(&hash, snapshot.waypoints.size());
    for (const auto& waypoint : snapshot.waypoints) {
        HashCombineString(&hash, waypoint.ident);
        HashCombineDouble(&hash, waypoint.latitudeDeg);
        HashCombineDouble(&hash, waypoint.longitudeDeg);
    }
    return static_cast<std::uint64_t>(hash);
}

bool EqualBrainRouteSectorSnapshotsExact(
    const RouteSectorSnapshot& left,
    const RouteSectorSnapshot& right) {
    const auto equalSector = [](const RouteSectorMatchSnapshot& a,
                                const RouteSectorMatchSnapshot& b) {
        return a.identifier == b.identifier &&
               a.entryDistanceNm == b.entryDistanceNm &&
               a.matchTokens == b.matchTokens &&
               a.controllerCallsignPatterns == b.controllerCallsignPatterns &&
               a.controllerPrefixes == b.controllerPrefixes &&
               a.centerCoverage == b.centerCoverage &&
               a.terminalCoverage == b.terminalCoverage;
    };
    if (left.available != right.available || left.stale != right.stale ||
        left.routeResolved != right.routeResolved ||
        left.statusLine != right.statusLine ||
        left.diagnosticCacheStatus != right.diagnosticCacheStatus ||
        left.diagnosticReason != right.diagnosticReason ||
        left.centerBoundaryGeneration != right.centerBoundaryGeneration ||
        left.authorityCatalogGeneration !=
            right.authorityCatalogGeneration ||
        left.departureIcao != right.departureIcao ||
        left.destinationIcao != right.destinationIcao ||
        left.waypoints.size() != right.waypoints.size() ||
        left.currentSectors.size() != right.currentSectors.size() ||
        left.nextSectors.size() != right.nextSectors.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.waypoints.size(); ++index) {
        const auto& a = left.waypoints[index];
        const auto& b = right.waypoints[index];
        if (a.ident != b.ident || a.latitudeDeg != b.latitudeDeg ||
            a.longitudeDeg != b.longitudeDeg) {
            return false;
        }
    }
    for (std::size_t index = 0; index < left.currentSectors.size(); ++index) {
        if (!equalSector(left.currentSectors[index],
                         right.currentSectors[index])) {
            return false;
        }
    }
    for (std::size_t index = 0; index < left.nextSectors.size(); ++index) {
        if (!equalSector(left.nextSectors[index], right.nextSectors[index])) {
            return false;
        }
    }
    return true;
}

std::uint64_t HashBrainControllerEvidence(
    const std::vector<ControllerSnapshot>& controllers,
    bool available,
    bool stale,
    std::uint64_t generation,
    int connectedControllers) {
    return HashBrainControllerEvidenceFromContentDigest(
        HashBrainControllerEvidenceContent(controllers),
        available,
        stale,
        generation,
        connectedControllers);
}

std::uint64_t HashBrainControllerEvidenceContent(
    const std::vector<ControllerSnapshot>& controllers) {
    std::size_t hash = 0;
    HashCombine(&hash, controllers.size());
    for (const auto& controller : controllers) {
        HashCombineString(&hash, controller.callsign);
        HashCombineString(&hash, controller.frequency);
        HashCombine(&hash, static_cast<std::size_t>(controller.facility));
        HashCombine(&hash, static_cast<std::size_t>(controller.visualRangeNm));
        HashCombineBool(&hash, controller.actionable);
        HashCombineBool(&hash, controller.atis);
        HashCombineString(&hash, controller.textAtis);
    }
    return static_cast<std::uint64_t>(hash);
}

std::uint64_t HashBrainControllerEvidenceFromContentDigest(
    std::uint64_t controllerContentDigest,
    bool available,
    bool stale,
    std::uint64_t generation,
    int connectedControllers) {
    std::size_t hash = 0;
    HashCombineBool(&hash, available);
    HashCombineBool(&hash, stale);
    HashCombine(&hash, static_cast<std::size_t>(generation));
    HashCombine(&hash, static_cast<std::size_t>(connectedControllers));
    HashCombine(&hash, static_cast<std::size_t>(controllerContentDigest));
    return static_cast<std::uint64_t>(hash);
}

std::uint64_t HashBrainAuthorityControllerEvidenceContent(
    const std::vector<ControllerSnapshot>& controllers) {
    return HashBrainAuthorityControllerEvidenceContent(
        BuildBrainAuthorityControllerEvidence(controllers));
}

std::vector<AuthorityControllerSnapshot> BuildBrainAuthorityControllerEvidence(
    const std::vector<ControllerSnapshot>& controllers) {
    std::vector<AuthorityControllerSnapshot> projection;
    projection.reserve(controllers.size());
    for (const auto& controller : controllers) {
        AuthorityControllerSnapshot projected;
        projected.callsign = controller.callsign;
        projected.frequency = controller.frequency;
        projected.facility = controller.facility;
        projected.actionable = controller.actionable;
        projected.atis = controller.atis;
        projected.textAtis = controller.textAtis;
        projection.push_back(std::move(projected));
    }
    std::sort(
        projection.begin(),
        projection.end(),
        [](const auto& left, const auto& right) {
            if (left.callsign != right.callsign) {
                return left.callsign < right.callsign;
            }
            if (left.frequency != right.frequency) {
                return left.frequency < right.frequency;
            }
            if (left.facility != right.facility) {
                return left.facility < right.facility;
            }
            if (left.actionable != right.actionable) {
                return left.actionable < right.actionable;
            }
            if (left.atis != right.atis) {
                return left.atis < right.atis;
            }
            return left.textAtis < right.textAtis;
        });
    return projection;
}

std::vector<ControllerSnapshot> ExpandBrainAuthorityControllerEvidence(
    const std::vector<AuthorityControllerSnapshot>& controllers) {
    std::vector<ControllerSnapshot> expanded;
    expanded.reserve(controllers.size());
    for (const auto& controller : controllers) {
        ControllerSnapshot value;
        value.callsign = controller.callsign;
        value.frequency = controller.frequency;
        value.facility = controller.facility;
        value.actionable = controller.actionable;
        value.atis = controller.atis;
        value.textAtis = controller.textAtis;
        expanded.push_back(std::move(value));
    }
    return expanded;
}

std::uint64_t HashBrainAuthorityControllerEvidenceContent(
    const std::vector<AuthorityControllerSnapshot>& controllers) {
    std::size_t hash = 0;
    HashCombine(&hash, controllers.size());
    for (const auto& controller : controllers) {
        HashCombineString(&hash, controller.callsign);
        HashCombineString(&hash, controller.frequency);
        HashCombine(&hash, static_cast<std::size_t>(controller.facility));
        HashCombineBool(&hash, controller.actionable);
        HashCombineBool(&hash, controller.atis);
        HashCombineString(&hash, controller.textAtis);
    }
    return static_cast<std::uint64_t>(hash);
}

std::uint64_t HashBrainAuthorityControllerEvidence(
    const std::vector<ControllerSnapshot>& controllers,
    bool available,
    bool stale) {
    return HashBrainAuthorityControllerEvidenceFromContentDigest(
        HashBrainAuthorityControllerEvidenceContent(controllers),
        available,
        stale);
}

std::uint64_t HashBrainAuthorityControllerEvidenceFromContentDigest(
    std::uint64_t controllerContentDigest,
    bool available,
    bool stale) {
    std::size_t hash = 0;
    HashCombineBool(&hash, available);
    HashCombineBool(&hash, stale);
    HashCombine(
        &hash,
        static_cast<std::size_t>(controllerContentDigest));
    return static_cast<std::uint64_t>(hash);
}

std::uint64_t HashBrainTransceiverEvidence(
    const TransceiverResolutionSnapshot& snapshot) {
    std::size_t hash = 0;
    HashCombineBool(&hash, snapshot.available);
    HashCombineBool(&hash, snapshot.stale);
    HashCombine(&hash, static_cast<std::size_t>(snapshot.receivableControllers));
    HashCombine(&hash, static_cast<std::size_t>(snapshot.distanceRejectedControllers));
    HashCombineDouble(&hash, snapshot.maxCandidateDistanceNm);
    HashCombineString(&hash, snapshot.statusLine);
    HashCombineString(&hash, snapshot.resolutionPath);
    HashCombineBool(&hash, snapshot.candidatesCompatibilityOnly);
    HashCombine(
        &hash,
        static_cast<std::size_t>(snapshot.droppedBeforeBrainControllers));
    HashCombine(&hash, snapshot.candidates.size());
    for (const auto& candidate : snapshot.candidates) {
        HashCombineString(&hash, candidate.callsign);
        HashCombineString(&hash, candidate.frequency);
        HashCombineDouble(&hash, candidate.distanceNm);
        HashCombineDouble(&hash, candidate.score);
        HashCombineDouble(&hash, candidate.latitudeDeg);
        HashCombineDouble(&hash, candidate.longitudeDeg);
    }
    const auto& source = snapshot.sourceEvidence;
    HashCombineBool(&hash, source.feedCacheExists);
    HashCombine(&hash, static_cast<std::size_t>(source.cachedTransceiverCount));
    HashCombineBool(&hash, source.sourceControllerCountKnown);
    HashCombine(&hash, static_cast<std::size_t>(source.sourceControllerCount));
    HashCombineBool(&hash, source.cacheFresh);
    HashCombineBool(&hash, source.cacheStale);
    HashCombineBool(&hash, source.holdoverUsed);
    HashCombineBool(&hash, source.holdoverExpired);
    HashCombineBool(&hash, source.hasFeedAgeSeconds);
    HashCombine(&hash, static_cast<std::size_t>(source.feedAgeSeconds));
    HashCombineBool(&hash, source.fetchAttempted);
    HashCombineBool(&hash, source.fetchInProgress);
    HashCombineBool(&hash, source.fetchFailed);
    HashCombineString(&hash, source.failureReason);
    HashCombine(&hash, static_cast<std::size_t>(source.parser.invalidClientCallsign));
    HashCombine(&hash, static_cast<std::size_t>(source.parser.invalidTransceiverFrequency));
    HashCombine(&hash, static_cast<std::size_t>(source.parser.invalidPosition));
    HashCombine(&hash, static_cast<std::size_t>(source.parser.invalidHeight));
    HashCombine(&hash, static_cast<std::size_t>(source.parser.parseException));
    HashCombine(&hash, static_cast<std::size_t>(source.parser.emptyPayload));
    HashCombineBool(&hash, source.parser.maxTransceiverTruncation);
    HashCombine(&hash, snapshot.controllerEvidence.size());
    for (const auto& controller : snapshot.controllerEvidence) {
        HashCombineString(&hash, controller.callsign);
        HashCombineString(&hash, controller.controllerFrequency);
        HashCombine(&hash, static_cast<std::size_t>(controller.facility));
        HashCombineBool(&hash, controller.actionable);
        HashCombineBool(&hash, controller.atis);
        HashCombine(&hash, static_cast<std::size_t>(controller.visualRangeNm));
        HashCombineBool(&hash, controller.hasTransceiverEntry);
        HashCombine(
            &hash,
            static_cast<std::size_t>(controller.matchingTransceiverCount));
        HashCombineString(&hash, controller.resolvedDisplayFrequency);
        HashCombineString(&hash, controller.displayFrequencySource);
        HashCombineString(
            &hash,
            controller.displayFrequencyUnavailableReason);
        HashCombineString(&hash, controller.pathUnavailableReason);
        HashCombineBool(&hash, controller.controllerFrequencyGuard);
        HashCombineBool(&hash, controller.transceiverFrequencyGuard);
        HashCombine(&hash, controller.stations.size());
        for (const auto& station : controller.stations) {
            HashCombineString(&hash, station.sourceFrequency);
            HashCombineDouble(&hash, station.latitudeDeg);
            HashCombineDouble(&hash, station.longitudeDeg);
            HashCombineDouble(&hash, station.heightAglFt);
            HashCombineBool(&hash, station.hasAircraftDistance);
            HashCombineDouble(&hash, station.aircraftDistanceNm);
            HashCombineDouble(&hash, station.maxCandidateDistanceNm);
            HashCombineBool(&hash, station.withinMaxCandidateDistance);
            HashCombineBool(&hash, station.hasReceivableRange);
            HashCombineDouble(&hash, station.receivableRangeNm);
            HashCombineBool(&hash, station.withinReceivableRange);
            HashCombineDouble(&hash, station.score);
            HashCombineBool(&hash, station.bestByModuleScore);
            HashCombineBool(&hash, station.transceiverFrequencyGuard);
        }
    }
    return static_cast<std::uint64_t>(hash);
}

AuthorityTransceiverEvidenceSnapshot BuildBrainAuthorityTransceiverEvidence(
    const TransceiverResolutionSnapshot& snapshot) {
    AuthorityTransceiverEvidenceSnapshot projection;
    projection.available = snapshot.available;
    projection.stale = snapshot.stale;
    projection.receivableControllers = snapshot.receivableControllers;
    projection.candidates.reserve(snapshot.candidates.size());
    for (const auto& candidate : snapshot.candidates) {
        AuthorityTransceiverCandidateSnapshot projected;
        projected.callsign = candidate.callsign;
        projected.frequency = candidate.frequency;
        projected.latitudeDeg = candidate.latitudeDeg;
        projected.longitudeDeg = candidate.longitudeDeg;
        projection.candidates.push_back(std::move(projected));
    }
    std::sort(
        projection.candidates.begin(),
        projection.candidates.end(),
        [](const auto& left, const auto& right) {
            if (left.callsign != right.callsign) {
                return left.callsign < right.callsign;
            }
            if (left.frequency != right.frequency) {
                return left.frequency < right.frequency;
            }
            if (left.latitudeDeg != right.latitudeDeg) {
                return left.latitudeDeg < right.latitudeDeg;
            }
            return left.longitudeDeg < right.longitudeDeg;
        });
    return projection;
}

std::uint64_t HashBrainAuthorityTransceiverEvidence(
    const AuthorityTransceiverEvidenceSnapshot& snapshot) {
    std::vector<std::size_t> candidateHashes;
    candidateHashes.reserve(snapshot.candidates.size());
    for (const auto& candidate : snapshot.candidates) {
        std::size_t candidateHash = 0;
        HashCombineString(&candidateHash, candidate.callsign);
        HashCombineString(&candidateHash, candidate.frequency);
        HashCombineDouble(&candidateHash, candidate.latitudeDeg);
        HashCombineDouble(&candidateHash, candidate.longitudeDeg);
        candidateHashes.push_back(candidateHash);
    }
    std::sort(candidateHashes.begin(), candidateHashes.end());

    std::size_t hash = 0;
    HashCombineBool(&hash, snapshot.available);
    HashCombineBool(&hash, snapshot.stale);
    HashCombine(
        &hash,
        static_cast<std::size_t>(snapshot.receivableControllers));
    HashCombine(&hash, candidateHashes.size());
    for (const auto candidateHash : candidateHashes) {
        HashCombine(&hash, candidateHash);
    }
    return static_cast<std::uint64_t>(hash);
}

TransceiverResolutionSnapshot ExpandBrainAuthorityTransceiverEvidence(
    const AuthorityTransceiverEvidenceSnapshot& snapshot) {
    TransceiverResolutionSnapshot expanded;
    expanded.available = snapshot.available;
    expanded.stale = snapshot.stale;
    expanded.receivableControllers = snapshot.receivableControllers;
    expanded.candidates.reserve(snapshot.candidates.size());
    for (const auto& candidate : snapshot.candidates) {
        ReceivableControllerSnapshot expandedCandidate;
        expandedCandidate.callsign = candidate.callsign;
        expandedCandidate.frequency = candidate.frequency;
        expandedCandidate.latitudeDeg = candidate.latitudeDeg;
        expandedCandidate.longitudeDeg = candidate.longitudeDeg;
        expanded.candidates.push_back(std::move(expandedCandidate));
    }
    return expanded;
}

std::uint64_t HashBrainAuthorityRelevanceSnapshot(
    const AuthorityRelevanceSnapshot& snapshot) {
    std::size_t hash = 0;
    const auto hashStrings = [&](const std::vector<std::string>& values) {
        HashCombine(&hash, values.size());
        for (const auto& value : values) {
            HashCombineString(&hash, value);
        }
    };
    const auto hashRelevant = [&](const RelevantAuthoritySnapshot& authority) {
        HashCombineString(&hash, authority.callsign);
        HashCombineString(&hash, authority.frequency);
        HashCombineString(&hash, authority.authorityId);
        HashCombineString(&hash, authority.polygonId);
        HashCombineString(&hash, authority.polygonKey);
        HashCombineString(&hash, authority.matchedPattern);
        HashCombineString(&hash, authority.proofSource);
        HashCombineString(&hash, authority.proofDetail);
        HashCombine(&hash, static_cast<std::size_t>(authority.kind));
        HashCombineBool(&hash, authority.aircraftInside);
        HashCombineBool(&hash, authority.routeIntersects);
        HashCombineDouble(&hash, authority.routeEntryDistanceNm);
    };
    const auto hashDecision = [&](const AuthorityDecisionEvidenceSnapshot& decision) {
        HashCombineString(&hash, decision.authorityId);
        HashCombineString(&hash, decision.authoritySource);
        HashCombineString(&hash, decision.authorityKind);
        HashCombineString(&hash, decision.polygonKey);
        HashCombineString(&hash, decision.matchedPattern);
        HashCombineBool(&hash, decision.accepted);
        HashCombineBool(&hash, decision.oldRouteScopeMatched);
        HashCombineBool(&hash, decision.oldRelevantAuthoritySurvivor);
        hashStrings(decision.rejectionReasons);
    };
    const auto hashPolygon = [&](const AuthorityPolygonEvidenceSnapshot& polygon) {
        HashCombineString(&hash, polygon.callsign);
        HashCombineString(&hash, polygon.authorityId);
        HashCombineString(&hash, polygon.polygonId);
        HashCombineString(&hash, polygon.polygonKey);
        HashCombineString(&hash, polygon.matchedPattern);
        HashCombineString(&hash, polygon.authoritySource);
        HashCombineString(&hash, polygon.authorityKind);
        HashCombineBool(&hash, polygon.routeKeyMatch);
        HashCombineBool(&hash, polygon.routeFamilyMatch);
        HashCombineBool(&hash, polygon.routeEndpointMatch);
        HashCombineBool(&hash, polygon.inOldScopedCatalog);
        HashCombineString(&hash, polygon.oldScopedOutReason);
        HashCombineBool(&hash, polygon.routeGeometryRelevant);
        HashCombineBool(&hash, polygon.aircraftInside);
        HashCombineBool(&hash, polygon.routeIntersects);
        HashCombineDouble(&hash, polygon.routeEntryDistanceNm);
        HashCombineBool(&hash, polygon.activePolygon);
        HashCombineString(&hash, polygon.activeProofSource);
        HashCombineString(&hash, polygon.activeProofDetail);
        HashCombineBool(&hash, polygon.routeKeyCompatible);
        HashCombineBool(&hash, polygon.geometryCompatible);
        HashCombineBool(&hash, polygon.oldCompatibilityRelevantSurvivor);
        HashCombineString(&hash, polygon.compatibilityFilteredReason);
    };

    HashCombineBool(&hash, snapshot.available);
    HashCombineBool(&hash, snapshot.stale);
    HashCombine(&hash, snapshot.controllerFeedGeneration);
    HashCombine(&hash, snapshot.centerBoundaryGeneration);
    HashCombine(&hash, snapshot.authorityCatalogGeneration);
    HashCombine(&hash, snapshot.terminalCoverageGeneration);
    HashCombineString(&hash, snapshot.diagnosticCacheStatus);
    HashCombineString(&hash, snapshot.diagnosticReason);
    HashCombineString(&hash, snapshot.diagnosticWorkStage);
    HashCombineDouble(&hash, snapshot.diagnosticWindowNm);
    HashCombine(
        &hash,
        static_cast<std::size_t>(snapshot.diagnosticDeferredSectorCount));
    HashCombineString(&hash, snapshot.statusLine);
    hashStrings(snapshot.diagnostics);
    HashCombineBool(&hash, snapshot.relevantAuthoritiesCompatibilityOnly);
    HashCombineBool(&hash, snapshot.liveRelevantAuthoritiesBrainOwned);
    HashCombine(
        &hash,
        static_cast<std::size_t>(snapshot.compatibilityRelevantAuthorityCount));
    HashCombine(
        &hash,
        static_cast<std::size_t>(snapshot.droppedBeforeBrainControllers));

    const auto& source = snapshot.evidence.source;
    HashCombineBool(&hash, source.scheduled);
    HashCombineString(&hash, source.scheduleReason);
    HashCombineBool(&hash, source.controllerFeedAvailable);
    HashCombineBool(&hash, source.controllerFeedStale);
    HashCombine(&hash, source.controllerFeedGeneration);
    HashCombineBool(&hash, source.sourceControllerCountKnown);
    HashCombine(&hash, static_cast<std::size_t>(source.sourceControllerCount));
    HashCombineBool(&hash, source.routeSnapshotAvailable);
    HashCombineBool(&hash, source.routeSnapshotStale);
    HashCombineBool(&hash, source.routeResolved);
    HashCombineString(&hash, source.workStage);
    HashCombineDouble(&hash, source.workWindowNm);
    HashCombine(&hash, static_cast<std::size_t>(source.workDeferredSectorCount));
    hashStrings(source.routeAuthorityKeys);
    hashStrings(source.routeAuthorityMatchKeys);
    HashCombineBool(&hash, source.authorityTransceiverSnapshotPresent);
    HashCombineBool(&hash, source.authorityTransceiverAvailable);
    HashCombineBool(&hash, source.authorityTransceiverStale);
    HashCombine(
        &hash,
        static_cast<std::size_t>(source.authorityTransceiverCandidateCount));
    HashCombineBool(&hash, source.cacheHit);
    HashCombineString(&hash, source.cacheStatus);
    HashCombineString(&hash, source.cacheReason);

    HashCombine(&hash, snapshot.evidence.controllerEvidence.size());
    for (const auto& controller : snapshot.evidence.controllerEvidence) {
        HashCombineString(&hash, controller.callsign);
        HashCombineString(&hash, controller.frequency);
        HashCombine(&hash, static_cast<std::size_t>(controller.facility));
        HashCombineBool(&hash, controller.actionable);
        HashCombineBool(&hash, controller.atis);
        HashCombineBool(&hash, controller.guardFrequency);
        HashCombineBool(&hash, controller.emptyCallsign);
        HashCombineBool(&hash, controller.airportLocalCandidate);
        HashCombineBool(&hash, controller.airspaceAuthorityCandidate);
        HashCombineBool(&hash, controller.sourceControllerConsidered);
        hashStrings(controller.evidenceReasons);
        HashCombine(&hash, controller.authorityDecisions.size());
        for (const auto& decision : controller.authorityDecisions) {
            hashDecision(decision);
        }
        HashCombine(&hash, controller.activePolygons.size());
        for (const auto& polygon : controller.activePolygons) {
            hashPolygon(polygon);
        }
    }
    HashCombine(&hash, snapshot.evidence.polygonEvidence.size());
    for (const auto& polygon : snapshot.evidence.polygonEvidence) {
        hashPolygon(polygon);
    }
    HashCombine(&hash, snapshot.evidence.activePolygonEvidence.size());
    for (const auto& polygon : snapshot.evidence.activePolygonEvidence) {
        hashPolygon(polygon);
    }
    HashCombine(
        &hash,
        snapshot.evidence.transceiverRouteProofEvidence.size());
    for (const auto& proof :
         snapshot.evidence.transceiverRouteProofEvidence) {
        HashCombineString(&hash, proof.callsign);
        HashCombine(&hash, static_cast<std::size_t>(proof.stationCandidateCount));
        HashCombineString(&hash, proof.stationCallsign);
        HashCombineString(&hash, proof.stationFrequency);
        HashCombineDouble(&hash, proof.stationLatitudeDeg);
        HashCombineDouble(&hash, proof.stationLongitudeDeg);
        HashCombineDouble(&hash, proof.stationScore);
        HashCombineBool(&hash, proof.bestByModuleScore);
        HashCombineString(&hash, proof.polygonId);
        HashCombineString(&hash, proof.polygonKey);
        HashCombineString(&hash, proof.authoritySource);
        HashCombineString(&hash, proof.authorityKind);
        HashCombineDouble(&hash, proof.stationPolygonDistanceNm);
        HashCombineDouble(&hash, proof.toleranceNm);
        HashCombineBool(&hash, proof.withinTolerance);
        HashCombineBool(&hash, proof.sourceOwnershipMatch);
        HashCombineBool(&hash, proof.unownedBorderMismatch);
        HashCombineBool(&hash, proof.blockedByDirectActiveProof);
        HashCombineBool(&hash, proof.noStationCandidates);
        HashCombineBool(&hash, proof.oldProofSurvivor);
        HashCombineString(&hash, proof.proofRejectionReason);
    }
    HashCombine(
        &hash,
        snapshot.evidence.duplicatedAtisProofEvidence.size());
    for (const auto& proof : snapshot.evidence.duplicatedAtisProofEvidence) {
        HashCombineString(&hash, proof.callsign);
        HashCombineBool(&hash, proof.textAtisPresent);
        hashStrings(proof.extractedCoveredTokens);
        HashCombineString(&hash, proof.coveredToken);
        hashStrings(proof.matchedRouteAuthorityAliases);
        HashCombineString(&hash, proof.authorityId);
        HashCombineString(&hash, proof.authoritySource);
        HashCombineString(&hash, proof.authorityKind);
        HashCombineString(&hash, proof.polygonKey);
        HashCombineBool(&hash, proof.sourceKindAllowed);
        HashCombineBool(&hash, proof.routeRelevantPolygonFound);
        HashCombineBool(&hash, proof.facilityEligible);
        HashCombineBool(&hash, proof.missingSourceOwnership);
        HashCombineBool(&hash, proof.oldProofSurvivor);
        HashCombineString(&hash, proof.proofRejectionReason);
    }
    HashCombine(&hash, snapshot.compatibilityRelevantAuthorities.size());
    for (const auto& authority : snapshot.compatibilityRelevantAuthorities) {
        hashRelevant(authority);
    }
    HashCombine(&hash, snapshot.relevantAuthorities.size());
    for (const auto& authority : snapshot.relevantAuthorities) {
        hashRelevant(authority);
    }
    return static_cast<std::uint64_t>(hash);
}

BrainAuthorityCompletionDecision DecideBrainAuthorityCompletion(
    const BrainAuthorityCompletionValidationInput& input) {
    BrainAuthorityCompletionDecision decision;
    decision.ageMs = std::max<long long>(
        0, input.nowMonotonicMs - input.completedMonotonicMs);
    const auto reject = [&](const char* reason) {
        decision.accepted = false;
        decision.staleRejected = true;
        decision.reason = reason;
        return decision;
    };

    const auto& expected = input.expected;
    const auto& completed = input.completed;
    if (completed.requestId == 0 || completed.requestId != expected.requestId) {
        return reject("request-id-mismatch");
    }
    if (completed.lifecycleEpoch != expected.lifecycleEpoch) {
        return reject("lifecycle-epoch-mismatch");
    }
    if (completed.planKey != expected.planKey) {
        return reject("plan-identity-mismatch");
    }
    if (completed.routeDigest != expected.routeDigest) {
        return reject("authority-route-digest-mismatch");
    }
    if (completed.controllerDigest != expected.controllerDigest) {
        return reject("controller-digest-mismatch");
    }
    if (completed.transceiverDigest != expected.transceiverDigest) {
        return reject("transceiver-digest-mismatch");
    }
    if (completed.datasetIdentity == 0 ||
        completed.datasetIdentity != expected.datasetIdentity) {
        return reject("authority-dataset-identity-mismatch");
    }
    if (decision.ageMs > input.maximumAgeMs) {
        return reject("completion-age-exceeded");
    }

    if (input.currentAircraft.valid && input.dispatchedAircraft.valid) {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kEarthRadiusNm = 3440.065;
        const auto radians = [&](double degrees) {
            return degrees * kPi / 180.0;
        };
        const auto lat1 = radians(input.dispatchedAircraft.latitudeDeg);
        const auto lat2 = radians(input.currentAircraft.latitudeDeg);
        const auto deltaLat = lat2 - lat1;
        const auto deltaLon = radians(
            input.currentAircraft.longitudeDeg -
            input.dispatchedAircraft.longitudeDeg);
        const auto a =
            std::sin(deltaLat / 2.0) * std::sin(deltaLat / 2.0) +
            std::cos(lat1) * std::cos(lat2) *
                std::sin(deltaLon / 2.0) * std::sin(deltaLon / 2.0);
        decision.displacementNm =
            2.0 * kEarthRadiusNm * std::asin(std::sqrt(std::clamp(a, 0.0, 1.0)));
        if (decision.displacementNm > input.maximumDisplacementNm) {
            return reject("completion-displacement-exceeded");
        }
    }

    decision.accepted = true;
    decision.reason = "exact-authority-completion-identity";
    return decision;
}

BrainRouteCompletionDecision DecideBrainRouteCompletion(
    const BrainRouteCompletionValidationInput& input) {
    BrainRouteCompletionDecision decision;
    const auto reject = [&](const char* reason) {
        decision.accepted = false;
        decision.staleRejected = true;
        decision.reason = reason;
        return decision;
    };

    const auto& eligible = input.eligible;
    const auto& desired = input.desired;
    const auto& completed = input.completed;
    if (completed.requestId == 0 ||
        completed.requestId != eligible.requestId) {
        return reject("request-id-mismatch");
    }
    if (completed.lifecycleEpoch != eligible.lifecycleEpoch ||
        completed.lifecycleEpoch != desired.lifecycleEpoch) {
        return reject("lifecycle-epoch-mismatch");
    }
    if (completed.planKey != eligible.planKey ||
        completed.planKey != desired.planKey) {
        return reject("plan-identity-mismatch");
    }
    if (completed.networkPlanDigest != eligible.networkPlanDigest ||
        completed.networkPlanDigest != desired.networkPlanDigest) {
        return reject("network-plan-digest-mismatch");
    }
    if (completed.routeSourceDatasetIdentity == 0 ||
        completed.routeSourceDatasetIdentity !=
            eligible.routeSourceDatasetIdentity ||
        completed.routeSourceDatasetIdentity !=
            desired.routeSourceDatasetIdentity) {
        return reject("route-source-dataset-identity-mismatch");
    }
    if (completed.preflightCandidateIdentity !=
            eligible.preflightCandidateIdentity ||
        completed.preflightCandidateIdentity !=
            desired.preflightCandidateIdentity) {
        return reject("preflight-candidate-identity-mismatch");
    }
    if (completed.routePolicyIdentity != eligible.routePolicyIdentity ||
        completed.routePolicyIdentity != desired.routePolicyIdentity) {
        return reject("route-policy-identity-mismatch");
    }
    if (completed.routeAnchorDigest != eligible.routeAnchorDigest ||
        completed.routeAnchorDigest != desired.routeAnchorDigest) {
        return reject("route-anchor-digest-mismatch");
    }
    if (completed.expandedFmsObservationIdentity == 0 ||
        completed.expandedFmsObservationIdentity !=
            eligible.expandedFmsObservationIdentity ||
        completed.expandedFmsObservationIdentity !=
            desired.expandedFmsObservationIdentity) {
        return reject("expanded-fms-observation-identity-mismatch");
    }

    decision.accepted = true;
    decision.reason = "exact-route-completion-identity";
    return decision;
}

BrainRoutePolygonWorkerOutput BuildBrainRoutePolygonWorkerOutput(
    const RouteSectorSnapshot& route) {
    return BuildBrainRoutePolygonWorkerOutput(
        std::make_shared<const RouteSectorSnapshot>(route));
}

BrainRoutePolygonWorkerOutput BuildBrainRoutePolygonWorkerOutput(
    std::shared_ptr<const RouteSectorSnapshot> route) {
    BrainRoutePolygonWorkerOutput output;
    output.route = std::move(route);
    if (output.route == nullptr) {
        output.reason = "route-polygon-worker-unavailable";
        return output;
    }
    output.available = output.route->available;
    output.stale = output.route->stale;
    output.routePolygonHash = HashBrainRouteSectorSnapshot(*output.route);
    output.currentPolygonIndex = output.route->currentSectors.empty() ? 0 : 1;
    output.currentPolygonKey = FirstRoutePolygonKey(output.route->currentSectors);
    output.nextPolygonKey = FirstRoutePolygonKey(output.route->nextSectors);
    output.arrivalPolygonKey = LastRoutePolygonKey(*output.route);
    output.finalRoutePolygonKey = output.arrivalPolygonKey;
    output.reason = output.route->diagnosticReason.empty()
                        ? "route-polygon-worker"
                        : output.route->diagnosticReason;
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
        state->routePolygonSnapshot != nullptr &&
        state->routePolygonSnapshot->available &&
        !state->routePolygonSnapshot->stale &&
        state->routePolygonSnapshot->routeResolved;
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
        auto retiredRoute = state != nullptr
            ? state->routePolygonSnapshot
            : std::shared_ptr<const RouteSectorSnapshot>{};
        StoreRoutePolygonOutput(
            state,
            input.routeRuntimeKey,
            state->lastRoutePolygonRefreshSeconds,
            output.route);
        if (retiredRoute != output.route.route) {
            output.retiredRoute = std::move(retiredRoute);
        }
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
        output.route.route != nullptr &&
        output.route.route->available &&
        !output.route.route->stale &&
        output.route.route->routeResolved;
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

    auto retiredRoute = state != nullptr
        ? state->routePolygonSnapshot
        : std::shared_ptr<const RouteSectorSnapshot>{};
    StoreRoutePolygonOutput(
        state,
        input.routeRuntimeKey,
        input.nowSeconds,
        output.route);
    if (retiredRoute != output.route.route) {
        output.retiredRoute = std::move(retiredRoute);
    }
    if (output.routeChanged) {
        InvalidateRelevanceForRouteChange(
            state,
            output.route,
            output.transitionChanged);
    }

    output.reason = output.route.reason;
    output.cacheStatus =
        output.route.route == nullptr ||
                output.route.route->diagnosticCacheStatus.empty()
            ? "route-polygon-worker"
            : output.route.route->diagnosticCacheStatus;
    output.diagnosticResult =
        RouteDiagnosticResult(
            output.route,
            output.routeChanged,
            output.transitionChanged,
            state != nullptr ? state->routeProgressDistanceNm : 0.0);
    return output;
}

}  // namespace xvatsim::brain
