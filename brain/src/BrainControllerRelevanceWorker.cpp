#include "XVatsim/brain/BrainOwnedWorkerTypes.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace xvatsim::brain {
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

std::string NormalizeIcaoInput(std::string value) {
    value.erase(
        std::remove_if(
            value.begin(),
            value.end(),
            [](unsigned char c) { return std::isspace(c) != 0; }),
        value.end());
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

std::string NormalizeCallsign(std::string value) {
    value.erase(
        std::remove_if(
            value.begin(),
            value.end(),
            [](unsigned char c) { return std::isspace(c) != 0; }),
        value.end());
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

std::vector<std::string> BuildAirportTokens(const std::string& airportIcao) {
    std::vector<std::string> tokens;
    const auto normalized = NormalizeIcaoInput(airportIcao);
    if (normalized.empty()) {
        return tokens;
    }

    tokens.push_back(normalized);
    if (normalized.size() == 4) {
        tokens.push_back(normalized.substr(1));
    }
    if (normalized.size() >= 3) {
        tokens.push_back(normalized.substr(normalized.size() - 3));
    }

    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
    return tokens;
}

bool SplitControllerCallsign(
    const std::string& callsign,
    std::string* outPrefix,
    std::string* outSuffix) {
    const auto normalized = NormalizeCallsign(callsign);
    const auto separatorIndex = normalized.rfind('_');
    if (separatorIndex == std::string::npos || separatorIndex == 0 ||
        separatorIndex >= (normalized.size() - 1)) {
        return false;
    }

    if (outPrefix != nullptr) {
        *outPrefix = normalized.substr(0, separatorIndex);
    }
    if (outSuffix != nullptr) {
        *outSuffix = normalized.substr(separatorIndex + 1);
    }
    return true;
}

bool ControllerMatchesAirport(
    const std::string& callsign,
    const std::vector<std::string>& airportTokens) {
    std::string prefix;
    if (!SplitControllerCallsign(callsign, &prefix, nullptr)) {
        return false;
    }

    return std::any_of(
        airportTokens.begin(),
        airportTokens.end(),
        [&](const auto& token) {
            return prefix == token ||
                   (prefix.size() > token.size() &&
                    prefix.compare(0, token.size(), token) == 0 &&
                    (prefix[token.size()] == '_' ||
                     prefix[token.size()] == '-'));
        });
}

bool FrequencyTuned(
    const std::string& frequency,
    const RadioStateSnapshot& radioStateSnapshot) {
    const auto normalizedTarget = NormalizeFrequency(frequency);
    if (normalizedTarget.empty()) {
        return false;
    }

    return NormalizeFrequency(radioStateSnapshot.com1ActiveFrequency) ==
               normalizedTarget ||
           NormalizeFrequency(radioStateSnapshot.com2ActiveFrequency) ==
               normalizedTarget;
}

bool GuardFrequency(const std::string& frequency) {
    const auto normalizedFrequency = NormalizeFrequency(frequency);
    return normalizedFrequency == "121500" || normalizedFrequency == "199998";
}

StationRole RoleFromRadioCandidate(
    const RadioReachableControllerCandidate& candidate) {
    switch (candidate.group) {
        case RadioReachableFacilityGroup::Delivery:
            return StationRole::Delivery;
        case RadioReachableFacilityGroup::Ground:
            return StationRole::Ground;
        case RadioReachableFacilityGroup::Tower:
            return StationRole::Tower;
        case RadioReachableFacilityGroup::AppDep: {
            std::string suffix;
            if (SplitControllerCallsign(candidate.callsign, nullptr, &suffix) &&
                suffix == "DEP") {
                return StationRole::Departure;
            }
            return StationRole::Approach;
        }
        case RadioReachableFacilityGroup::Center:
            return StationRole::Center;
        case RadioReachableFacilityGroup::Atis:
            return StationRole::Atis;
        case RadioReachableFacilityGroup::Other:
        default:
            return StationRole::Other;
    }
}

void AppendStationUnique(
    const BoardStationSnapshot& station,
    ModuleBoardSnapshot* board,
    std::unordered_set<std::string>* insertedKeys) {
    if (board == nullptr || insertedKeys == nullptr || station.frequency.empty() ||
        GuardFrequency(station.frequency)) {
        return;
    }

    const auto key =
        std::to_string(static_cast<int>(station.role)) + "|" +
        NormalizeCallsign(station.callsign) + "|" +
        NormalizeFrequency(station.frequency);
    if (!insertedKeys->insert(key).second) {
        return;
    }

    board->stations.push_back(station);
    board->available = true;
}

std::string CandidateStableKey(
    const RadioReachableControllerCandidate& candidate) {
    if (!candidate.stableKey.empty()) {
        return candidate.stableKey;
    }

    return NormalizeCallsign(candidate.callsign) + "|" +
           NormalizeFrequency(candidate.frequency) + "|" +
           std::to_string(static_cast<int>(candidate.group));
}

void RecordCandidateDecision(
    const BrainControllerRelevanceWorkerInput& input,
    const RadioReachableControllerCandidate& candidate,
    BrainOwnedCandidateDecision decision,
    const std::string& reason,
    const BoardStationSnapshot& station,
    std::vector<BrainOwnedCandidateCompletion>* completions) {
    if (completions == nullptr) {
        return;
    }

    auto keyedCandidate = candidate;
    keyedCandidate.stableKey = CandidateStableKey(candidate);

    BrainOwnedCandidateCompletion completion;
    completion.radioBoardHash = input.radioBoardHash;
    completion.routePolygonHash = input.routePolygonHash;
    completion.workflowStage = input.workflowStage;
    completion.currentPolygonIndex = input.currentPolygonIndex;
    completion.currentPolygonKey = input.currentPolygonKey;
    completion.matchedPolygonKey = station.polygonKey;
    completion.callsign = candidate.callsign;
    completion.frequency = candidate.frequency;
    completion.facilityGroup = candidate.group;
    completion.displayRelation = station.displayRelation;
    completion.decision = decision;
    completion.displayed = false;
    completion.hasRouteEntryDistance = station.hasRouteEntryDistance;
    completion.routeEntryDistanceNm = station.routeEntryDistanceNm;
    completion.reason = reason;
    completion.stableKey = BuildBrainOwnedCandidateCompletionKey(
        input.radioBoardHash,
        input.routePolygonHash,
        input.workflowStage,
        input.currentPolygonKey,
        keyedCandidate);
    if (completion.frequency.empty()) {
        completion.frequency = station.frequency;
    }
    completions->push_back(std::move(completion));
}

bool WildcardMatch(std::string pattern, std::string value) {
    if (pattern.empty()) {
        return false;
    }

    std::size_t patternIndex = 0;
    std::size_t valueIndex = 0;
    std::size_t starIndex = std::string::npos;
    std::size_t valueRetryIndex = 0;
    while (valueIndex < value.size()) {
        if (patternIndex < pattern.size() &&
            pattern[patternIndex] == value[valueIndex]) {
            ++patternIndex;
            ++valueIndex;
            continue;
        }

        if (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
            starIndex = patternIndex++;
            valueRetryIndex = valueIndex;
            continue;
        }

        if (starIndex != std::string::npos) {
            patternIndex = starIndex + 1;
            valueIndex = ++valueRetryIndex;
            continue;
        }

        return false;
    }

    while (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
        ++patternIndex;
    }
    return patternIndex == pattern.size();
}

bool CallsignMatchesPattern(
    const std::string& rawPattern,
    const std::string& rawCallsign) {
    const auto pattern = NormalizeCallsign(rawPattern);
    const auto callsign = NormalizeCallsign(rawCallsign);
    if (pattern.empty() || callsign.empty()) {
        return false;
    }
    if (pattern.find('*') != std::string::npos) {
        return WildcardMatch(pattern, callsign);
    }
    return pattern == callsign;
}

std::vector<std::string> CallsignPrefixCandidates(
    const std::string& rawCallsign) {
    std::vector<std::string> prefixes;
    const auto callsign = NormalizeCallsign(rawCallsign);
    if (callsign.empty()) {
        return prefixes;
    }

    prefixes.push_back(callsign);
    const auto firstSeparator = callsign.find('_');
    if (firstSeparator != std::string::npos && firstSeparator > 0) {
        prefixes.push_back(callsign.substr(0, firstSeparator));
    }
    const auto lastSeparator = callsign.rfind('_');
    if (lastSeparator != std::string::npos && lastSeparator > 0) {
        prefixes.push_back(callsign.substr(0, lastSeparator));
    }

    std::sort(prefixes.begin(), prefixes.end());
    prefixes.erase(std::unique(prefixes.begin(), prefixes.end()), prefixes.end());
    return prefixes;
}

bool CallsignMatchesPrefix(
    const std::string& rawPrefix,
    const std::string& rawCallsign) {
    const auto prefix = NormalizeCallsign(rawPrefix);
    if (prefix.empty() || prefix.size() < 2) {
        return false;
    }

    for (const auto& candidatePrefix : CallsignPrefixCandidates(rawCallsign)) {
        if (candidatePrefix == prefix) {
            return true;
        }
        if (candidatePrefix.size() > prefix.size() &&
            candidatePrefix.compare(0, prefix.size(), prefix) == 0 &&
            (candidatePrefix[prefix.size()] == '_' ||
             candidatePrefix[prefix.size()] == '-')) {
            return true;
        }
    }
    return false;
}

bool SectorHasCenterMetadata(const RouteSectorMatchSnapshot& sector) {
    return !sector.controllerCallsignPatterns.empty() ||
           !sector.controllerPrefixes.empty() ||
           !sector.matchTokens.empty();
}

bool SectorsHaveCenterMetadata(
    const std::vector<RouteSectorMatchSnapshot>& sectors) {
    return std::any_of(
        sectors.begin(),
        sectors.end(),
        [](const auto& sector) { return SectorHasCenterMetadata(sector); });
}

bool SectorMatchesCenterCandidate(
    const RouteSectorMatchSnapshot& sector,
    const RadioReachableControllerCandidate& candidate,
    std::string* reason) {
    for (const auto& pattern : sector.controllerCallsignPatterns) {
        if (CallsignMatchesPattern(pattern, candidate.callsign)) {
            if (reason != nullptr) {
                *reason = "pattern:" + NormalizeCallsign(pattern);
            }
            return true;
        }
    }

    for (const auto& prefix : sector.controllerPrefixes) {
        if (CallsignMatchesPrefix(prefix, candidate.callsign)) {
            if (reason != nullptr) {
                *reason = "prefix:" + NormalizeCallsign(prefix);
            }
            return true;
        }
    }

    for (const auto& token : sector.matchTokens) {
        if (CallsignMatchesPrefix(token, candidate.callsign)) {
            if (reason != nullptr) {
                *reason = "token:" + NormalizeCallsign(token);
            }
            return true;
        }
    }

    if (CallsignMatchesPrefix(sector.identifier, candidate.callsign)) {
        if (reason != nullptr) {
            *reason = "sector:" + NormalizeCallsign(sector.identifier);
        }
        return true;
    }
    return false;
}

struct CenterRouteMatch {
    bool hasRouteMetadata = false;
    bool matched = false;
    std::string polygonKey;
    DisplayRelation displayRelation = DisplayRelation::Unknown;
    bool hasRouteEntryDistance = false;
    double routeEntryDistanceNm = 0.0;
    std::string reason;
};

CenterRouteMatch MatchCenterToRoutePolygon(
    const BrainControllerRelevanceWorkerInput& input,
    const RadioReachableControllerCandidate& candidate) {
    CenterRouteMatch match;
    match.hasRouteMetadata =
        SectorsHaveCenterMetadata(input.currentSectors) ||
        SectorsHaveCenterMetadata(input.nextSectors);

    for (const auto& sector : input.currentSectors) {
        std::string proof;
        if (SectorMatchesCenterCandidate(sector, candidate, &proof)) {
            match.matched = true;
            match.polygonKey =
                !sector.identifier.empty() ? sector.identifier : input.currentPolygonKey;
            match.displayRelation = DisplayRelation::CurrentPolygon;
            match.hasRouteEntryDistance = false;
            match.routeEntryDistanceNm = 0.0;
            match.reason = "center-current-polygon-match:" + proof;
            return match;
        }
    }

    for (const auto& sector : input.nextSectors) {
        std::string proof;
        if (SectorMatchesCenterCandidate(sector, candidate, &proof)) {
            match.matched = true;
            match.polygonKey =
                !sector.identifier.empty() ? sector.identifier : input.nextPolygonKey;
            match.displayRelation = DisplayRelation::NextPolygon;
            match.hasRouteEntryDistance = true;
            match.routeEntryDistanceNm =
                std::max(0.0, sector.entryDistanceNm);
            match.reason = "center-next-polygon-match:" + proof;
            return match;
        }
    }

    match.reason = match.hasRouteMetadata ? "center-not-route-polygon-match"
                                          : "center-route-metadata-unavailable";
    return match;
}

struct CenterCandidate {
    BoardStationSnapshot station;
};

void AppendSelectedCenterStations(
    const std::vector<CenterCandidate>& centerCandidates,
    ModuleBoardSnapshot* enrouteBoard,
    std::unordered_set<std::string>* enrouteKeys) {
    if (enrouteBoard == nullptr || enrouteKeys == nullptr ||
        centerCandidates.empty()) {
        return;
    }

    for (const auto& candidate : centerCandidates) {
        AppendStationUnique(candidate.station, enrouteBoard, enrouteKeys);
    }
}

}  // namespace

BrainControllerRelevanceWorkerOutput RunBrainControllerRelevanceWorker(
    const BrainControllerRelevanceWorkerInput& input) {
    BrainControllerRelevanceWorkerOutput output;
    output.available = true;
    output.stale = false;
    output.reason = "controller-relevance-worker";
    output.departureBoard.source = BoardSource::Departure;
    output.arrivalBoard.source = BoardSource::Arrival;
    output.enrouteBoard.source = BoardSource::Enroute;
    output.departureBoard.airportIcao = input.departureIcao;
    output.arrivalBoard.airportIcao = input.arrivalIcao;

    const auto departureTokens = BuildAirportTokens(input.departureIcao);
    const auto arrivalTokens = BuildAirportTokens(input.arrivalIcao);
    std::unordered_set<std::string> departureKeys;
    std::unordered_set<std::string> arrivalKeys;
    std::unordered_set<std::string> enrouteKeys;
    std::vector<CenterCandidate> centerCandidates;

    const auto includeDepartureGroups =
        input.workflowStage == WorkflowStage::None ||
        input.workflowStage == WorkflowStage::Departure;
    const auto includeEnrouteGroups =
        input.workflowStage == WorkflowStage::None ||
        input.workflowStage == WorkflowStage::Departure ||
        input.workflowStage == WorkflowStage::Enroute ||
        input.workflowStage == WorkflowStage::Arrival;
    const auto includeArrivalGroups =
        input.workflowStage == WorkflowStage::None ||
        input.workflowStage == WorkflowStage::Arrival;

    for (const auto& candidate : input.candidates) {
        const auto role = RoleFromRadioCandidate(candidate);
        BoardStationSnapshot station;
        station.role = role;
        station.callsign = candidate.callsign;
        station.frequency = candidate.frequency;
        station.tuned = FrequencyTuned(candidate.frequency, input.radios);
        station.online = true;
        station.sectorActive = role == StationRole::Center && station.tuned;
        station.hasRouteEntryDistance = candidate.hasDistanceNm;
        station.routeEntryDistanceNm = candidate.distanceNm;

        if (role == StationRole::Other ||
            candidate.group == RadioReachableFacilityGroup::Atis) {
            RecordCandidateDecision(
                input,
                candidate,
                BrainOwnedCandidateDecision::Rejected,
                "facility-not-ui-relevant",
                station,
                &output.completions);
            continue;
        }

        if (GuardFrequency(candidate.frequency)) {
            RecordCandidateDecision(
                input,
                candidate,
                BrainOwnedCandidateDecision::Rejected,
                "guard-frequency-rejected",
                station,
                &output.completions);
            continue;
        }

        bool accepted = false;
        std::string reason;
        const auto localRole =
            role == StationRole::Delivery ||
            role == StationRole::Ground ||
            role == StationRole::Tower;
        const auto appDepRole =
            role == StationRole::Approach ||
            role == StationRole::Departure;

        if (role == StationRole::Center) {
            if (includeEnrouteGroups) {
                const auto routeMatch =
                    MatchCenterToRoutePolygon(input, candidate);
                if (routeMatch.matched || !routeMatch.hasRouteMetadata ||
                    station.tuned) {
                    station.polygonKey = routeMatch.matched
                                             ? routeMatch.polygonKey
                                             : input.currentPolygonKey;
                    station.displayRelation =
                        routeMatch.matched
                            ? routeMatch.displayRelation
                            : DisplayRelation::CurrentPolygon;
                    station.sectorActive =
                        station.displayRelation ==
                            DisplayRelation::CurrentPolygon ||
                        station.tuned;
                    station.next =
                        station.displayRelation == DisplayRelation::NextPolygon;
                    station.hasRouteEntryDistance =
                        routeMatch.matched &&
                        routeMatch.displayRelation ==
                            DisplayRelation::NextPolygon &&
                        routeMatch.hasRouteEntryDistance;
                    station.routeEntryDistanceNm =
                        station.hasRouteEntryDistance
                            ? routeMatch.routeEntryDistanceNm
                            : 0.0;
                    centerCandidates.push_back({station});
                    accepted = true;
                    reason = routeMatch.matched
                                 ? routeMatch.reason
                                 : (station.tuned
                                        ? "center-tuned-current-radio"
                                        : "center-route-metadata-unavailable-reachable");
                } else {
                    reason = routeMatch.reason;
                }
            } else {
                reason = "center-not-needed-for-phase";
            }
        } else if (includeDepartureGroups &&
                   localRole &&
                   ControllerMatchesAirport(candidate.callsign, departureTokens)) {
            station.polygonKey = input.currentPolygonKey;
            station.displayRelation = DisplayRelation::CurrentPolygon;
            AppendStationUnique(
                station,
                &output.departureBoard,
                &departureKeys);
            accepted = true;
            reason = "departure-airport-match";
        } else if (includeDepartureGroups && appDepRole) {
            station.polygonKey = input.currentPolygonKey;
            station.displayRelation = DisplayRelation::CurrentPolygon;
            AppendStationUnique(
                station,
                &output.departureBoard,
                &departureKeys);
            accepted = true;
            reason = "departure-terminal-reachable";
        } else if (includeArrivalGroups &&
                   (localRole || appDepRole) &&
                   ControllerMatchesAirport(candidate.callsign, arrivalTokens)) {
            station.polygonKey = input.arrivalPolygonKey;
            station.displayRelation = DisplayRelation::ArrivalPrep;
            AppendStationUnique(
                station,
                &output.arrivalBoard,
                &arrivalKeys);
            accepted = true;
            reason = "arrival-airport-match";
        } else {
            reason = "phase-or-airport-filter-rejected";
        }

        RecordCandidateDecision(
            input,
            candidate,
            accepted ? BrainOwnedCandidateDecision::Accepted
                     : BrainOwnedCandidateDecision::Rejected,
            reason,
            station,
            &output.completions);
    }

    AppendSelectedCenterStations(
        centerCandidates,
        &output.enrouteBoard,
        &enrouteKeys);
    return output;
}

BrainOwnedControllerRelevanceRuntimeOutput RunBrainOwnedControllerRelevance(
    BrainOwnedRuntimeState* state,
    const BrainControllerRelevanceWorkerInput& input) {
    BrainOwnedControllerRelevanceRuntimeOutput output;
    const auto canReuse =
        state != nullptr &&
        state->candidatesComplete &&
        state->hasRadioBoard &&
        state->lastRadioBoardHash == input.radioBoardHash &&
        state->routePolygonHash == input.routePolygonHash &&
        state->lastWorkflowStage == input.workflowStage &&
        state->currentPolygonKey == input.currentPolygonKey;

    if (canReuse) {
        output.cacheHit = true;
        output.cacheStatus = "brain-controller-relevance-cache-hit";
        output.relevance.available = true;
        output.relevance.stale = false;
        output.relevance.reason = "board-unchanged-no-relevance-work";
        output.relevance.departureBoard =
            state->relevanceDepartureBoardSnapshot;
        output.relevance.arrivalBoard =
            state->relevanceArrivalBoardSnapshot;
        output.relevance.enrouteBoard =
            state->relevanceEnrouteBoardSnapshot;
        output.relevance.completions = state->candidateCompletions;
        state->lastIdleReason = "board-unchanged-no-relevance-work";
        return output;
    }

    output.cacheHit = false;
    output.cacheStatus = "brain-controller-relevance-ran";
    output.relevance = RunBrainControllerRelevanceWorker(input);

    if (state == nullptr) {
        return output;
    }

    state->relevanceDepartureBoardSnapshot =
        output.relevance.departureBoard;
    state->relevanceArrivalBoardSnapshot =
        output.relevance.arrivalBoard;
    state->relevanceEnrouteBoardSnapshot =
        output.relevance.enrouteBoard;

    state->candidateCompletions.clear();
    for (const auto& completion : output.relevance.completions) {
        RecordBrainOwnedCandidateCompletion(state, completion);
    }
    state->candidatesComplete = true;
    state->lastIdleReason.clear();
    return output;
}

}  // namespace xvatsim::brain
