#include "XVatsim/brain/BrainOwnedWorkerTypes.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <unordered_set>

namespace xvatsim::brain {
namespace {

constexpr const char* kCenterTunedOffRouteNotRouteOwnedPolicy =
    "center-tuned-off-route-not-route-owned";

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

void HashCombine(std::uint64_t* seed, std::uint64_t value) {
    if (seed == nullptr) {
        return;
    }
    *seed ^= value + 0x9e3779b97f4a7c15ULL + (*seed << 6) + (*seed >> 2);
}

void HashCombine(std::uint64_t* seed, const std::string& value) {
    HashCombine(seed, static_cast<std::uint64_t>(value.size()));
    for (const auto ch : value) {
        HashCombine(
            seed,
            static_cast<std::uint64_t>(static_cast<unsigned char>(ch)));
    }
}

std::uint64_t HashRadioTuningIdentity(const RadioStateSnapshot& radios) {
    std::uint64_t hash = 1469598103934665603ULL;
    HashCombine(&hash, static_cast<std::uint64_t>(radios.valid ? 1u : 0u));
    HashCombine(&hash, NormalizeFrequency(radios.com1ActiveFrequency));
    HashCombine(&hash, NormalizeFrequency(radios.com2ActiveFrequency));
    HashCombine(&hash, NormalizeFrequency(radios.com1StandbyFrequency));
    return hash;
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

std::string TerminalServiceTokenFromControllerCallsign(std::string callsign) {
    callsign = NormalizeCallsign(std::move(callsign));
    const auto lastSeparator = callsign.rfind('_');
    if (lastSeparator == std::string::npos ||
        lastSeparator >= callsign.size() - 1) {
        return {};
    }

    const auto role = callsign.substr(lastSeparator + 1);
    if (role != "APP" && role != "DEP") {
        return {};
    }

    const auto terminalBase = callsign.substr(0, lastSeparator);
    if (terminalBase.empty()) {
        return {};
    }

    const auto firstSeparator = callsign.find('_');
    const auto serviceOwner =
        (firstSeparator == std::string::npos || firstSeparator == 0)
            ? terminalBase
            : terminalBase.substr(0, firstSeparator);
    return serviceOwner.empty() ? std::string{} : serviceOwner + "_" + role;
}

bool TerminalAuthorityFactAvailable(
    const BrainTerminalAuthorityWorkerOutput& authority) {
    return authority.available &&
           authority.resolved &&
           !authority.stale &&
           !authority.ownerTokens.empty();
}

bool AirportFrequencyFactAvailable(
    const BrainAirportFrequencyWorkerOutput& frequencies) {
    return frequencies.available &&
           frequencies.resolved &&
           !frequencies.stale;
}

struct AirportFrequencyEvidence {
    bool available = false;
    bool endpointRoleFacts = false;
    bool frequencyFound = false;
    bool roleMatch = false;
    StationRole matchedRole = StationRole::Other;
    std::string suffix;
};

std::string StationRoleToken(StationRole role) {
    switch (role) {
        case StationRole::Delivery:
            return "DEL";
        case StationRole::Ground:
            return "GND";
        case StationRole::Tower:
            return "TWR";
        case StationRole::Departure:
        case StationRole::Approach:
            return "APP_DEP";
        case StationRole::Atis:
            return "ATIS";
        case StationRole::Ctaf:
            return "CTAF";
        case StationRole::Unicom:
            return "UNICOM";
        case StationRole::Center:
            return "CTR";
        case StationRole::Other:
        default:
            return "OTHER";
    }
}

bool FrequencyRecordRoleMatches(
    StationRole candidateRole,
    StationRole frequencyRole) {
    if ((candidateRole == StationRole::Approach ||
         candidateRole == StationRole::Departure) &&
        (frequencyRole == StationRole::Approach ||
         frequencyRole == StationRole::Departure)) {
        return true;
    }
    return candidateRole == frequencyRole;
}

const std::vector<BrainAirportFrequencyRecord>& EndpointFrequencyRecords(
    const BrainAirportFrequencyWorkerOutput& frequencies,
    BrainAirportFrequencyEndpoint endpoint) {
    static const std::vector<BrainAirportFrequencyRecord> kEmpty;
    if (endpoint == BrainAirportFrequencyEndpoint::Departure) {
        return frequencies.departureFrequencies;
    }
    if (endpoint == BrainAirportFrequencyEndpoint::Arrival) {
        return frequencies.arrivalFrequencies;
    }
    return kEmpty;
}

AirportFrequencyEvidence ResolveAirportFrequencyEvidence(
    const BrainControllerRelevanceWorkerInput& input,
    const RadioReachableControllerCandidate& candidate,
    BrainAirportFrequencyEndpoint endpoint,
    StationRole candidateRole) {
    AirportFrequencyEvidence evidence;
    if (!AirportFrequencyFactAvailable(input.airportFrequencies)) {
        return evidence;
    }
    evidence.available = true;

    const auto normalizedFrequency = NormalizeFrequency(candidate.frequency);
    if (normalizedFrequency.empty()) {
        return evidence;
    }

    for (const auto& record :
         EndpointFrequencyRecords(input.airportFrequencies, endpoint)) {
        if (FrequencyRecordRoleMatches(candidateRole, record.role)) {
            evidence.endpointRoleFacts = true;
        }
        if (NormalizeFrequency(record.frequency) != normalizedFrequency) {
            continue;
        }
        evidence.frequencyFound = true;
        evidence.matchedRole = record.role;
        if (FrequencyRecordRoleMatches(candidateRole, record.role)) {
            evidence.roleMatch = true;
        }
    }

    const auto endpointToken =
        endpoint == BrainAirportFrequencyEndpoint::Departure ? "dep" : "arr";
    if (evidence.roleMatch) {
        evidence.suffix = ":freq=" + std::string(endpointToken) + "-" +
                          StationRoleToken(evidence.matchedRole);
    } else if (evidence.frequencyFound) {
        evidence.suffix = ":freq=" + std::string(endpointToken) + "-other-" +
                          StationRoleToken(evidence.matchedRole);
    } else {
        evidence.suffix = ":freq=" + std::string(endpointToken) + "-miss";
    }
    return evidence;
}

std::string AirportFrequencyEvidenceSuffix(
    const AirportFrequencyEvidence& evidence) {
    return evidence.suffix;
}

std::string CompactAuthorityValues(
    const std::vector<std::string>& values,
    std::size_t maxValues = 2) {
    std::vector<std::string> compactValues;
    compactValues.reserve(values.size());
    for (const auto& value : values) {
        auto normalized = NormalizeCallsign(value);
        if (normalized.empty()) {
            continue;
        }
        if (std::find(
                compactValues.begin(),
                compactValues.end(),
                normalized) == compactValues.end()) {
            compactValues.push_back(std::move(normalized));
        }
    }

    if (compactValues.empty()) {
        return "none";
    }

    std::ostringstream stream;
    const auto count = std::min(maxValues, compactValues.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (index > 0) {
            stream << '+';
        }
        stream << compactValues[index];
    }
    if (compactValues.size() > count) {
        stream << "+more";
    }
    return stream.str();
}

std::string TerminalAuthorityFactSuffix(
    const BrainTerminalAuthorityWorkerOutput& authority,
    bool includeStatus) {
    std::ostringstream stream;
    if (!authority.ownerTokens.empty()) {
        stream << ":owner=" << CompactAuthorityValues(authority.ownerTokens);
    }
    if (!authority.polygonKeys.empty()) {
        stream << ":poly=" << CompactAuthorityValues(authority.polygonKeys);
    }
    if ((includeStatus || stream.str().empty()) && !authority.status.empty()) {
        stream << ":status=" << NormalizeCallsign(authority.status);
    }
    return stream.str();
}

std::string TerminalAuthorityDecisionReason(
    const std::string& baseReason,
    const std::string& candidateOwner,
    const BrainTerminalAuthorityWorkerOutput& authority) {
    std::string reason = baseReason;
    if (!candidateOwner.empty()) {
        reason += ":" + candidateOwner;
    }
    reason += TerminalAuthorityFactSuffix(
        authority,
        baseReason.find("unavailable") != std::string::npos);
    return reason;
}

bool ControllerMatchesTerminalAuthority(
    const std::string& callsign,
    const BrainTerminalAuthorityWorkerOutput& authority,
    std::string* outCandidateOwner) {
    const auto candidateOwner =
        TerminalServiceTokenFromControllerCallsign(callsign);
    if (outCandidateOwner != nullptr) {
        *outCandidateOwner = candidateOwner;
    }
    if (candidateOwner.empty() || !TerminalAuthorityFactAvailable(authority)) {
        return false;
    }

    return std::any_of(
        authority.ownerTokens.begin(),
        authority.ownerTokens.end(),
        [&](const auto& ownerToken) {
            return NormalizeCallsign(ownerToken) == candidateOwner;
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
    DisplayRelation displayRelation,
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
    completion.displayRelation = displayRelation;
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

std::string ControllerRootToken(const std::string& callsign) {
    const auto normalized = NormalizeCallsign(callsign);
    if (normalized.empty()) {
        return {};
    }

    const auto separator = normalized.find('_');
    return separator == std::string::npos ? normalized
                                          : normalized.substr(0, separator);
}

std::unordered_set<std::string> BuildCurrentRouteCenterRoots(
    const BrainControllerRelevanceWorkerInput& input) {
    std::unordered_set<std::string> roots;
    for (const auto& candidate : input.candidates) {
        if (candidate.group != RadioReachableFacilityGroup::Center) {
            continue;
        }

        const auto routeMatch = MatchCenterToRoutePolygon(input, candidate);
        if (!routeMatch.matched ||
            routeMatch.displayRelation != DisplayRelation::CurrentPolygon) {
            continue;
        }

        const auto root = ControllerRootToken(candidate.callsign);
        if (!root.empty()) {
            roots.insert(root);
        }
    }
    return roots;
}

struct TerminalDecisionEvidence {
    bool terminalOwnerAvailable = false;
    bool terminalOwnerMatch = false;
    bool frequencyRoleMatch = false;
    bool frequencyRoleMiss = false;
    bool radioNearAirport = false;
    bool sourceAuthorityMatch = false;
    bool routeCenterRootMatch = false;
    bool accepted = false;
    int positiveScore = 0;
    int negativeScore = 0;
    int positiveNonFaaScore = 0;
    std::string candidateOwner;
    std::string sourceAuthorityProof;
    std::vector<std::string> positiveVotes;
    std::vector<std::string> negativeVotes;
    std::vector<std::string> neutralFacts;
    std::unordered_set<std::string> positiveFamilies;
    std::unordered_set<std::string> positiveNonFaaFamilies;
};

void AddUniqueToken(std::vector<std::string>* values, const std::string& value) {
    if (values == nullptr || value.empty()) {
        return;
    }
    if (std::find(values->begin(), values->end(), value) == values->end()) {
        values->push_back(value);
    }
}

void AddPositiveEvidence(
    TerminalDecisionEvidence* evidence,
    const std::string& token,
    const std::string& family,
    int weight,
    bool faaDerived = false) {
    if (evidence == nullptr || token.empty() || weight <= 0) {
        return;
    }
    AddUniqueToken(&evidence->positiveVotes, token);
    evidence->positiveScore += weight;
    if (!family.empty()) {
        evidence->positiveFamilies.insert(family);
    }
    if (!faaDerived) {
        evidence->positiveNonFaaScore += weight;
        if (!family.empty()) {
            evidence->positiveNonFaaFamilies.insert(family);
        }
    }
}

void AddNegativeEvidence(
    TerminalDecisionEvidence* evidence,
    const std::string& token,
    int weight) {
    if (evidence == nullptr || token.empty() || weight <= 0) {
        return;
    }
    AddUniqueToken(&evidence->negativeVotes, token);
    evidence->negativeScore += weight;
}

void AddNeutralFact(
    TerminalDecisionEvidence* evidence,
    const std::string& token) {
    if (evidence == nullptr || token.empty()) {
        return;
    }
    AddUniqueToken(&evidence->neutralFacts, token);
}

std::string JoinTokens(const std::vector<std::string>& values) {
    if (values.empty()) {
        return "none";
    }

    std::ostringstream stream;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            stream << '+';
        }
        stream << values[index];
    }
    return stream.str();
}

std::string TerminalDecisionConfidence(
    const TerminalDecisionEvidence& evidence) {
    const auto margin = evidence.positiveScore - evidence.negativeScore;
    if (evidence.sourceAuthorityMatch ||
        (evidence.terminalOwnerMatch && evidence.routeCenterRootMatch) ||
        margin >= 5) {
        return "high";
    }
    if (evidence.terminalOwnerMatch || evidence.routeCenterRootMatch ||
        margin >= 3) {
        return "medium";
    }
    return "low";
}

std::string TerminalDecisionVoteSuffix(
    const TerminalDecisionEvidence& evidence,
    bool accepted) {
    std::ostringstream stream;
    stream << ":votes=" << evidence.positiveVotes.size()
           << "/" << evidence.negativeVotes.size()
           << ":score=" << evidence.positiveScore
           << "/" << evidence.negativeScore
           << ":yes=" << JoinTokens(evidence.positiveVotes)
           << ":no=" << JoinTokens(evidence.negativeVotes)
           << ":neutral=" << JoinTokens(evidence.neutralFacts)
           << ":families=" << evidence.positiveNonFaaFamilies.size()
           << ":confidence=" << TerminalDecisionConfidence(evidence)
           << ":final=" << (accepted ? "accept-display" : "reject-hide");
    return stream.str();
}

const RelevantAuthoritySnapshot* FindRelevantSourceAuthority(
    const BrainControllerRelevanceWorkerInput& input,
    const RadioReachableControllerCandidate& candidate) {
    if (!input.authorityRelevance.available || input.authorityRelevance.stale) {
        return nullptr;
    }

    const auto candidateCallsign = NormalizeCallsign(candidate.callsign);
    const auto candidateFrequency = NormalizeFrequency(candidate.frequency);
    for (const auto& authority : input.authorityRelevance.relevantAuthorities) {
        if (authority.kind == AuthorityRelevanceKind::Center) {
            continue;
        }
        if (NormalizeCallsign(authority.callsign) != candidateCallsign) {
            continue;
        }
        const auto authorityFrequency = NormalizeFrequency(authority.frequency);
        if (!authorityFrequency.empty() &&
            !candidateFrequency.empty() &&
            authorityFrequency != candidateFrequency) {
            continue;
        }
        return &authority;
    }
    return nullptr;
}

constexpr const char* kAuthorityRelevancePreviewSurvivor =
    "brain-preview-authority-relevance-survivor";
constexpr const char* kAuthorityRelevancePreviewRejectedNonActionable =
    "brain-preview-authority-relevance-rejected-non-actionable";
constexpr const char* kAuthorityRelevancePreviewRejectedAtis =
    "brain-preview-authority-relevance-rejected-atis";
constexpr const char* kAuthorityRelevancePreviewRejectedGuardFrequency =
    "brain-preview-authority-relevance-rejected-guard-frequency";
constexpr const char* kAuthorityRelevancePreviewRejectedNotAuthorityCandidate =
    "brain-preview-authority-relevance-rejected-not-authority-candidate";
constexpr const char* kAuthorityRelevancePreviewRejectedUnmappedController =
    "brain-preview-authority-relevance-rejected-unmapped-controller";
constexpr const char* kAuthorityRelevancePreviewRejectedRouteScope =
    "brain-preview-authority-relevance-rejected-route-scope";
constexpr const char* kAuthorityRelevancePreviewRejectedActiveNotRelevant =
    "brain-preview-authority-relevance-rejected-active-not-relevant";
constexpr const char* kAuthorityRelevancePreviewRejectedTransceiverGeoMismatch =
    "brain-preview-authority-relevance-rejected-transceiver-geo-mismatch";
constexpr const char* kAuthorityRelevancePreviewRejectedTransceiverProof =
    "brain-preview-authority-relevance-rejected-transceiver-proof";
constexpr const char* kAuthorityRelevancePreviewRejectedDuplicatedAtisProof =
    "brain-preview-authority-relevance-rejected-duplicated-atis-proof";
constexpr const char* kAuthorityRelevancePreviewRejectedNoUsableProof =
    "brain-preview-authority-relevance-rejected-no-usable-proof";

bool HasEvidenceReason(
    const std::vector<std::string>& reasons,
    const std::string& reason) {
    return std::find(reasons.begin(), reasons.end(), reason) != reasons.end();
}

bool RelevantAuthorityMatchesActiveEvidence(
    const RelevantAuthoritySnapshot& relevant,
    const AuthorityPolygonEvidenceSnapshot& activeEvidence) {
    return relevant.callsign == activeEvidence.callsign &&
           relevant.authorityId == activeEvidence.authorityId &&
           relevant.polygonId == activeEvidence.polygonId &&
           relevant.polygonKey == activeEvidence.polygonKey &&
           relevant.matchedPattern == activeEvidence.matchedPattern;
}

bool PreviewDecisionMatchesRelevantAuthority(
    const BrainAuthorityRelevancePreviewDecision& decision,
    const RelevantAuthoritySnapshot& relevant) {
    return decision.callsign == relevant.callsign &&
           decision.authorityId == relevant.authorityId &&
           decision.polygonId == relevant.polygonId &&
           decision.polygonKey == relevant.polygonKey &&
           decision.matchedPattern == relevant.matchedPattern;
}

bool HasAuthorityRelevanceEvidenceLedger(
    const AuthorityRelevanceSnapshot& snapshot) {
    return snapshot.evidence.source.scheduled ||
           snapshot.evidence.source.sourceControllerCountKnown ||
           !snapshot.evidence.controllerEvidence.empty() ||
           !snapshot.evidence.polygonEvidence.empty() ||
           !snapshot.evidence.activePolygonEvidence.empty() ||
           !snapshot.evidence.transceiverRouteProofEvidence.empty() ||
           !snapshot.evidence.duplicatedAtisProofEvidence.empty();
}

const std::vector<RelevantAuthoritySnapshot>& CompatibilityRelevantAuthorities(
    const AuthorityRelevanceSnapshot& snapshot) {
    if (snapshot.liveRelevantAuthoritiesBrainOwned ||
        !snapshot.compatibilityRelevantAuthorities.empty()) {
        return snapshot.compatibilityRelevantAuthorities;
    }
    return snapshot.relevantAuthorities;
}

bool CallsignHasRelevantAuthority(
    const AuthorityRelevanceSnapshot& snapshot,
    const std::string& callsign) {
    const auto normalizedCallsign = NormalizeCallsign(callsign);
    const auto& compatibilityRelevantAuthorities =
        CompatibilityRelevantAuthorities(snapshot);
    return std::any_of(
        compatibilityRelevantAuthorities.begin(),
        compatibilityRelevantAuthorities.end(),
        [&](const auto& relevant) {
            return NormalizeCallsign(relevant.callsign) == normalizedCallsign;
        });
}

BrainAuthorityRelevancePreviewDecision BuildControllerAuthorityRelevanceRejection(
    const AuthorityControllerEvidenceSnapshot& controller,
    const std::string& decision,
    const std::string& reason) {
    BrainAuthorityRelevancePreviewDecision previewDecision;
    previewDecision.evidenceKind = "controller";
    previewDecision.callsign = controller.callsign;
    previewDecision.decision = decision;
    previewDecision.reason = reason;
    return previewDecision;
}

BrainAuthorityRelevancePreviewDecision BuildPolygonAuthorityRelevanceDecision(
    const std::string& evidenceKind,
    const AuthorityPolygonEvidenceSnapshot& polygon,
    const std::string& decision,
    const std::string& reason) {
    BrainAuthorityRelevancePreviewDecision previewDecision;
    previewDecision.evidenceKind = evidenceKind;
    previewDecision.callsign = polygon.callsign;
    previewDecision.authorityId = polygon.authorityId;
    previewDecision.polygonId = polygon.polygonId;
    previewDecision.polygonKey = polygon.polygonKey;
    previewDecision.matchedPattern = polygon.matchedPattern;
    previewDecision.proofSource = polygon.activeProofSource;
    previewDecision.decision = decision;
    previewDecision.reason = reason;
    return previewDecision;
}

BrainAuthorityRelevancePreviewDecision BuildTransceiverProofRejection(
    const AuthorityTransceiverRouteProofEvidenceSnapshot& proof) {
    BrainAuthorityRelevancePreviewDecision previewDecision;
    previewDecision.evidenceKind = "transceiver-proof";
    previewDecision.callsign = proof.callsign;
    previewDecision.polygonId = proof.polygonId;
    previewDecision.polygonKey = proof.polygonKey;
    previewDecision.proofSource = "TRANSCEIVER_GEO_ROUTE";
    previewDecision.decision =
        kAuthorityRelevancePreviewRejectedTransceiverProof;
    previewDecision.reason =
        proof.proofRejectionReason.empty()
            ? "transceiver-proof-rejected"
            : proof.proofRejectionReason;
    return previewDecision;
}

BrainAuthorityRelevancePreviewDecision BuildDuplicatedAtisProofRejection(
    const AuthorityDuplicatedAtisProofEvidenceSnapshot& proof) {
    BrainAuthorityRelevancePreviewDecision previewDecision;
    previewDecision.evidenceKind = "duplicated-atis-proof";
    previewDecision.callsign = proof.callsign;
    previewDecision.authorityId = proof.authorityId;
    previewDecision.polygonKey = proof.polygonKey;
    previewDecision.matchedPattern =
        proof.coveredToken.empty() ? std::string{} : "ATIS_COVERED:" + proof.coveredToken;
    previewDecision.proofSource = "DUPLICATED_ATIS_DERIVED";
    previewDecision.decision =
        kAuthorityRelevancePreviewRejectedDuplicatedAtisProof;
    previewDecision.reason =
        proof.proofRejectionReason.empty()
            ? "duplicated-atis-proof-rejected"
            : proof.proofRejectionReason;
    return previewDecision;
}

std::string ControllerAuthorityRelevanceRejectDecision(
    const AuthorityControllerEvidenceSnapshot& controller,
    std::string* outReason) {
    if (!controller.actionable) {
        if (outReason != nullptr) {
            *outReason = "controller-not-actionable";
        }
        return kAuthorityRelevancePreviewRejectedNonActionable;
    }
    if (controller.atis) {
        if (outReason != nullptr) {
            *outReason = "controller-atis";
        }
        return kAuthorityRelevancePreviewRejectedAtis;
    }
    if (controller.guardFrequency) {
        if (outReason != nullptr) {
            *outReason = "guard-frequency";
        }
        return kAuthorityRelevancePreviewRejectedGuardFrequency;
    }
    if (HasEvidenceReason(controller.evidenceReasons, "not-authority-candidate")) {
        if (outReason != nullptr) {
            *outReason = "not-authority-candidate";
        }
        return kAuthorityRelevancePreviewRejectedNotAuthorityCandidate;
    }
    if (HasEvidenceReason(controller.evidenceReasons, "unmapped-controller")) {
        if (outReason != nullptr) {
            *outReason = "unmapped-controller";
        }
        return kAuthorityRelevancePreviewRejectedUnmappedController;
    }
    if (HasEvidenceReason(
            controller.evidenceReasons,
            "no-route-scoped-authority-decision") ||
        HasEvidenceReason(
            controller.evidenceReasons,
            "airport-local-no-authority-decision")) {
        if (outReason != nullptr) {
            *outReason = "no-route-scoped-authority-decision";
        }
        return kAuthorityRelevancePreviewRejectedRouteScope;
    }
    if (HasEvidenceReason(
            controller.evidenceReasons,
            "transceiver-station-pending-compatibility-proof") ||
        HasEvidenceReason(
            controller.evidenceReasons,
            "duplicated-atis-pending-compatibility-proof")) {
        if (outReason != nullptr) {
            *outReason = "compatibility-proof-not-survivor";
        }
        return kAuthorityRelevancePreviewRejectedNoUsableProof;
    }
    if (!controller.authorityDecisions.empty()) {
        if (outReason != nullptr) {
            *outReason = "authority-decision-not-relevant";
        }
        return kAuthorityRelevancePreviewRejectedRouteScope;
    }
    if (outReason != nullptr) {
        *outReason = "no-usable-proof";
    }
    return kAuthorityRelevancePreviewRejectedNoUsableProof;
}

std::string ActivePolygonAuthorityRelevanceRejectDecision(
    const AuthorityPolygonEvidenceSnapshot& activePolygon,
    std::string* outReason) {
    const auto reason =
        activePolygon.compatibilityFilteredReason.empty()
            ? std::string("active-not-relevant")
            : activePolygon.compatibilityFilteredReason;
    if (outReason != nullptr) {
        *outReason = reason;
    }
    if (reason == "transceiver-geo-mismatch" ||
        !activePolygon.geometryCompatible) {
        return kAuthorityRelevancePreviewRejectedTransceiverGeoMismatch;
    }
    if (reason == "route-key-filtered" ||
        (!activePolygon.routeKeyCompatible &&
         activePolygon.compatibilityFilteredReason != "active-not-relevant")) {
        return kAuthorityRelevancePreviewRejectedRouteScope;
    }
    if (reason == "duplicate-active-key") {
        return kAuthorityRelevancePreviewRejectedNoUsableProof;
    }
    return kAuthorityRelevancePreviewRejectedActiveNotRelevant;
}

BrainAuthorityRelevanceDecisionPreview
BuildBrainAuthorityRelevanceDecisionPreviewInternal(
    const AuthorityRelevanceSnapshot& authorityRelevance) {
    BrainAuthorityRelevanceDecisionPreview preview;
    const auto& compatibilityRelevantAuthorities =
        CompatibilityRelevantAuthorities(authorityRelevance);
    preview.summary.authority =
        authorityRelevance.liveRelevantAuthoritiesBrainOwned
            ? "brain-evidence"
            : "preview-only";
    preview.summary.sourceControllerCount =
        authorityRelevance.evidence.source.sourceControllerCount;
    preview.summary.evidenceControllerCount =
        static_cast<int>(authorityRelevance.evidence.controllerEvidence.size());
    preview.summary.compatibilityRelevantAuthorityCount =
        static_cast<int>(compatibilityRelevantAuthorities.size());
    preview.summary.droppedBeforeBrainControllers =
        authorityRelevance.droppedBeforeBrainControllers;
    preview.summary.relevantAuthoritiesCompatibilityOnly =
        authorityRelevance.relevantAuthoritiesCompatibilityOnly;
    preview.summary.liveRelevantAuthoritiesBrainOwned =
        authorityRelevance.liveRelevantAuthoritiesBrainOwned;

    for (const auto& controller :
         authorityRelevance.evidence.controllerEvidence) {
        if (CallsignHasRelevantAuthority(authorityRelevance, controller.callsign)) {
            continue;
        }
        std::string reason;
        const auto decision =
            ControllerAuthorityRelevanceRejectDecision(controller, &reason);
        preview.decisions.push_back(
            BuildControllerAuthorityRelevanceRejection(
                controller,
                decision,
                reason));
    }

    for (const auto& polygon : authorityRelevance.evidence.polygonEvidence) {
        if (polygon.oldScopedOutReason.empty() ||
            polygon.oldCompatibilityRelevantSurvivor) {
            continue;
        }
        preview.decisions.push_back(
            BuildPolygonAuthorityRelevanceDecision(
                "route-scope-polygon",
                polygon,
                kAuthorityRelevancePreviewRejectedRouteScope,
                polygon.oldScopedOutReason));
    }

    for (const auto& activePolygon :
         authorityRelevance.evidence.activePolygonEvidence) {
        if (activePolygon.oldCompatibilityRelevantSurvivor) {
            auto decision = BuildPolygonAuthorityRelevanceDecision(
                "active-polygon",
                activePolygon,
                kAuthorityRelevancePreviewSurvivor,
                "old-relevant-authority");
            decision.matchesOldSurvivor =
                std::any_of(
                    compatibilityRelevantAuthorities.begin(),
                    compatibilityRelevantAuthorities.end(),
                    [&](const auto& relevant) {
                        return RelevantAuthorityMatchesActiveEvidence(
                            relevant,
                            activePolygon);
                    });
            preview.decisions.push_back(std::move(decision));
            continue;
        }

        std::string reason;
        const auto decision =
            ActivePolygonAuthorityRelevanceRejectDecision(activePolygon, &reason);
        preview.decisions.push_back(
            BuildPolygonAuthorityRelevanceDecision(
                "active-polygon",
                activePolygon,
                decision,
                reason));
    }

    for (const auto& proof :
         authorityRelevance.evidence.transceiverRouteProofEvidence) {
        if (proof.oldProofSurvivor) {
            continue;
        }
        preview.decisions.push_back(BuildTransceiverProofRejection(proof));
    }

    for (const auto& proof :
         authorityRelevance.evidence.duplicatedAtisProofEvidence) {
        if (proof.oldProofSurvivor) {
            continue;
        }
        preview.decisions.push_back(BuildDuplicatedAtisProofRejection(proof));
    }

    for (const auto& decision : preview.decisions) {
        if (decision.decision == kAuthorityRelevancePreviewSurvivor) {
            ++preview.summary.previewSurvivorCount;
        } else {
            ++preview.summary.previewRejectedCount;
        }
    }

    for (const auto& relevant : compatibilityRelevantAuthorities) {
        const auto matched = std::any_of(
            preview.decisions.begin(),
            preview.decisions.end(),
            [&](const auto& decision) {
                return decision.decision == kAuthorityRelevancePreviewSurvivor &&
                       PreviewDecisionMatchesRelevantAuthority(decision, relevant);
            });
        if (!matched) {
            ++preview.summary.oldSurvivorMismatchCount;
        }
    }

    for (const auto& decision : preview.decisions) {
        if (decision.decision == kAuthorityRelevancePreviewSurvivor &&
            !decision.matchesOldSurvivor) {
            ++preview.summary.oldSurvivorMismatchCount;
        }
    }

    return preview;
}

TerminalDecisionEvidence BuildTerminalDecisionEvidence(
    const BrainControllerRelevanceWorkerInput& input,
    const RadioReachableControllerCandidate& candidate,
    const BrainTerminalAuthorityWorkerOutput& terminalAuthority,
    const AirportFrequencyEvidence& frequencyEvidence,
    const std::unordered_set<std::string>& currentRouteCenterRoots) {
    TerminalDecisionEvidence evidence;

    AddPositiveEvidence(&evidence, "vatsim-appdep", "vatsim-live", 2);

    evidence.radioNearAirport =
        candidate.hasDistanceNm && candidate.distanceNm <= 5.0;
    if (evidence.radioNearAirport) {
        AddPositiveEvidence(&evidence, "radio-near5", "afv-radio", 2);
    } else if (candidate.hasDistanceNm) {
        AddNeutralFact(&evidence, "radio-distance-over5");
    }
    if (candidate.hasStationCoordinates) {
        AddNeutralFact(&evidence, "afv-station-geo");
    }

    const auto sourceAuthority = FindRelevantSourceAuthority(input, candidate);
    if (sourceAuthority != nullptr) {
        evidence.sourceAuthorityMatch = true;
        evidence.sourceAuthorityProof = sourceAuthority->proofSource;
        AddPositiveEvidence(
            &evidence,
            "source-owned-authority",
            "source-authority",
            4);
    }

    evidence.terminalOwnerAvailable =
        TerminalAuthorityFactAvailable(terminalAuthority);
    evidence.terminalOwnerMatch = ControllerMatchesTerminalAuthority(
        candidate.callsign,
        terminalAuthority,
        &evidence.candidateOwner);
    if (evidence.terminalOwnerMatch) {
        AddPositiveEvidence(&evidence, "terminal-owner", "terminal-source", 3);
    } else if (evidence.terminalOwnerAvailable) {
        AddNegativeEvidence(&evidence, "terminal-owner", 2);
    }

    evidence.frequencyRoleMatch = frequencyEvidence.roleMatch;
    evidence.frequencyRoleMiss =
        frequencyEvidence.endpointRoleFacts && !frequencyEvidence.roleMatch;
    if (evidence.frequencyRoleMatch) {
        AddPositiveEvidence(&evidence, "faa-frequency", "faa-frequency", 1, true);
    } else if (evidence.frequencyRoleMiss) {
        AddNeutralFact(&evidence, "faa-frequency-miss");
    }

    const auto candidateRoot = ControllerRootToken(candidate.callsign);
    evidence.routeCenterRootMatch =
        !candidateRoot.empty() &&
        currentRouteCenterRoots.find(candidateRoot) !=
            currentRouteCenterRoots.end();
    if (evidence.routeCenterRootMatch) {
        AddPositiveEvidence(&evidence, "route-center-root", "route-context", 3);
    }

    evidence.accepted =
        evidence.positiveScore > evidence.negativeScore &&
        evidence.positiveNonFaaScore > evidence.negativeScore &&
        evidence.positiveNonFaaFamilies.size() >= 2;
    return evidence;
}

std::string TerminalDecisionReason(
    const std::string& endpointToken,
    const TerminalDecisionEvidence& evidence,
    const BrainTerminalAuthorityWorkerOutput& terminalAuthority,
    const AirportFrequencyEvidence& frequencyEvidence) {
    std::string baseReason;
    if (evidence.accepted) {
        if (evidence.sourceAuthorityMatch) {
            baseReason = endpointToken + "-terminal-source-authority-match";
        } else if (evidence.terminalOwnerMatch) {
            baseReason = endpointToken + "-terminal-owner-match";
        } else if (evidence.frequencyRoleMatch) {
            baseReason = endpointToken + "-terminal-frequency-match";
        } else {
            baseReason = endpointToken + "-terminal-majority-match";
        }
    } else if (!evidence.terminalOwnerAvailable &&
               !evidence.frequencyRoleMatch &&
               !evidence.sourceAuthorityMatch) {
        baseReason = endpointToken + "-terminal-authority-unavailable";
    } else if (evidence.terminalOwnerAvailable &&
               !evidence.terminalOwnerMatch) {
        baseReason = endpointToken + "-terminal-owner-mismatch";
    } else {
        baseReason = endpointToken + "-terminal-insufficient-evidence";
    }

    std::string reason =
        baseReason +
        TerminalDecisionVoteSuffix(evidence, evidence.accepted);
    if (!evidence.candidateOwner.empty()) {
        reason += ":" + evidence.candidateOwner;
    }
    reason += TerminalAuthorityFactSuffix(
        terminalAuthority,
        baseReason.find("unavailable") != std::string::npos);
    reason += AirportFrequencyEvidenceSuffix(frequencyEvidence);
    if (!evidence.sourceAuthorityProof.empty()) {
        reason += ":source=" + NormalizeCallsign(evidence.sourceAuthorityProof);
    }
    return reason;
}

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

BrainAuthorityRelevanceDecisionPreview
BuildBrainAuthorityRelevanceDecisionPreview(
    const AuthorityRelevanceSnapshot& authorityRelevance) {
    return BuildBrainAuthorityRelevanceDecisionPreviewInternal(authorityRelevance);
}

AuthorityRelevanceSnapshot BuildBrainOwnedAuthorityRelevanceSnapshot(
    AuthorityRelevanceSnapshot authorityRelevance,
    const BrainAuthorityRelevanceDecisionPreview& preview) {
    if (!HasAuthorityRelevanceEvidenceLedger(authorityRelevance)) {
        return authorityRelevance;
    }

    // Preserve route_sector's old survivor construction strictly as
    // compatibility data. From this point on, relevantAuthorities is the
    // brain-owned live projection built from evidence decisions.
    if (!authorityRelevance.liveRelevantAuthoritiesBrainOwned &&
        authorityRelevance.compatibilityRelevantAuthorities.empty()) {
        authorityRelevance.compatibilityRelevantAuthorities =
            authorityRelevance.relevantAuthorities;
    }

    const auto compatibilityRelevantAuthorities =
        authorityRelevance.compatibilityRelevantAuthorities;
    authorityRelevance.relevantAuthorities.clear();
    authorityRelevance.relevantAuthorities.reserve(
        compatibilityRelevantAuthorities.size());

    for (const auto& relevant : compatibilityRelevantAuthorities) {
        const auto acceptedByBrain = std::any_of(
            preview.decisions.begin(),
            preview.decisions.end(),
            [&](const auto& decision) {
                return decision.decision == kAuthorityRelevancePreviewSurvivor &&
                       PreviewDecisionMatchesRelevantAuthority(decision, relevant);
            });
        if (acceptedByBrain) {
            authorityRelevance.relevantAuthorities.push_back(relevant);
        }
    }

    authorityRelevance.compatibilityRelevantAuthorityCount =
        static_cast<int>(
            authorityRelevance.compatibilityRelevantAuthorities.size());
    authorityRelevance.liveRelevantAuthoritiesBrainOwned = true;
    authorityRelevance.relevantAuthoritiesCompatibilityOnly = true;
    return authorityRelevance;
}

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
    const auto currentRouteCenterRoots =
        BuildCurrentRouteCenterRoots(input);

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
        station.sourceEvidenceId =
            "radio-reachable:" + CandidateStableKey(candidate);
        station.sourceEvidenceType = "radio-reachable-controller";
        station.sourceEvidenceDomain = "controller-relevance";
        station.sourceEvidenceLinkStatus = "linked";

        if (role == StationRole::Other ||
            candidate.group == RadioReachableFacilityGroup::Atis) {
            RecordCandidateDecision(
                input,
                candidate,
                BrainOwnedCandidateDecision::Rejected,
                "facility-not-ui-relevant",
                station,
                DisplayRelation::Hidden,
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
                DisplayRelation::Hidden,
                &output.completions);
            continue;
        }

        bool accepted = false;
        DisplayRelation completionRelation = DisplayRelation::Unknown;
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
                if (routeMatch.matched) {
                    const auto relation = routeMatch.displayRelation;
                    station.polygonKey = routeMatch.polygonKey;
                    station.sectorActive =
                        relation == DisplayRelation::CurrentPolygon ||
                        station.tuned;
                    station.hasRouteEntryDistance =
                        relation == DisplayRelation::NextPolygon &&
                        routeMatch.hasRouteEntryDistance;
                    station.routeEntryDistanceNm =
                        station.hasRouteEntryDistance
                            ? routeMatch.routeEntryDistanceNm
                            : 0.0;
                    centerCandidates.push_back({station});
                    accepted = true;
                    completionRelation = relation;
                    reason = routeMatch.reason;
                } else {
                    completionRelation = DisplayRelation::Hidden;
                    if (routeMatch.hasRouteMetadata && station.tuned) {
                        reason = kCenterTunedOffRouteNotRouteOwnedPolicy;
                    } else {
                        reason = routeMatch.hasRouteMetadata
                                     ? routeMatch.reason
                                     : "center-route-authority-unavailable-radio-only-blocked";
                    }
                }
            } else {
                completionRelation = DisplayRelation::Hidden;
                reason = "center-not-needed-for-phase";
            }
        } else if (includeDepartureGroups && localRole &&
                   ControllerMatchesAirport(candidate.callsign, departureTokens)) {
            const auto frequencyEvidence = ResolveAirportFrequencyEvidence(
                input,
                candidate,
                BrainAirportFrequencyEndpoint::Departure,
                role);
            station.polygonKey = input.currentPolygonKey;
            AppendStationUnique(
                station,
                &output.departureBoard,
                &departureKeys);
            accepted = true;
            completionRelation = DisplayRelation::CurrentPolygon;
            reason = std::string("departure-airport-match") +
                     AirportFrequencyEvidenceSuffix(frequencyEvidence);
        } else if (includeDepartureGroups && appDepRole) {
            const auto frequencyEvidence = ResolveAirportFrequencyEvidence(
                input,
                candidate,
                BrainAirportFrequencyEndpoint::Departure,
                role);
            const auto evidence = BuildTerminalDecisionEvidence(
                input,
                candidate,
                input.departureTerminalAuthority,
                frequencyEvidence,
                currentRouteCenterRoots);
            if (evidence.accepted) {
                station.polygonKey = input.currentPolygonKey;
                AppendStationUnique(
                    station,
                    &output.departureBoard,
                    &departureKeys);
                accepted = true;
                completionRelation = DisplayRelation::CurrentPolygon;
                reason = TerminalDecisionReason(
                    "departure",
                    evidence,
                    input.departureTerminalAuthority,
                    frequencyEvidence);
            } else {
                completionRelation = DisplayRelation::Hidden;
                reason = TerminalDecisionReason(
                    "departure",
                    evidence,
                    input.departureTerminalAuthority,
                    frequencyEvidence);
            }
        } else if (includeArrivalGroups &&
                   localRole &&
                   ControllerMatchesAirport(candidate.callsign, arrivalTokens)) {
            const auto frequencyEvidence = ResolveAirportFrequencyEvidence(
                input,
                candidate,
                BrainAirportFrequencyEndpoint::Arrival,
                role);
            station.polygonKey = input.arrivalPolygonKey;
            AppendStationUnique(
                station,
                &output.arrivalBoard,
                &arrivalKeys);
            accepted = true;
            completionRelation = DisplayRelation::ArrivalPrep;
            reason = std::string("arrival-airport-match") +
                     AirportFrequencyEvidenceSuffix(frequencyEvidence);
        } else if (includeArrivalGroups && appDepRole) {
            const auto frequencyEvidence = ResolveAirportFrequencyEvidence(
                input,
                candidate,
                BrainAirportFrequencyEndpoint::Arrival,
                role);
            const auto evidence = BuildTerminalDecisionEvidence(
                input,
                candidate,
                input.arrivalTerminalAuthority,
                frequencyEvidence,
                currentRouteCenterRoots);
            if (evidence.accepted) {
                station.polygonKey = input.arrivalPolygonKey;
                AppendStationUnique(
                    station,
                    &output.arrivalBoard,
                    &arrivalKeys);
                accepted = true;
                completionRelation = DisplayRelation::ArrivalPrep;
                reason = TerminalDecisionReason(
                    "arrival",
                    evidence,
                    input.arrivalTerminalAuthority,
                    frequencyEvidence);
            } else {
                completionRelation = DisplayRelation::Hidden;
                reason = TerminalDecisionReason(
                    "arrival",
                    evidence,
                    input.arrivalTerminalAuthority,
                    frequencyEvidence);
            }
        } else {
            completionRelation = DisplayRelation::Hidden;
            reason = "phase-or-airport-filter-rejected";
        }

        RecordCandidateDecision(
            input,
            candidate,
            accepted ? BrainOwnedCandidateDecision::Accepted
                     : BrainOwnedCandidateDecision::Rejected,
            reason,
            station,
            completionRelation,
            &output.completions);
    }

    AppendSelectedCenterStations(
        centerCandidates,
        &output.enrouteBoard,
        &enrouteKeys);
    return output;
}

BrainControllerRelevanceWorkerInput BuildBrainOwnedControllerRelevanceInput(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedControllerRelevanceInputRequest& request) {
    BrainControllerRelevanceWorkerInput input;
    input.workflowStage = request.workflowStage;
    input.radioBoardHash = request.radioSnapshot.stableHash;
    input.routePolygonHash = state.routePolygonHash;
    input.currentPolygonIndex = state.currentPolygonIndex;
    input.currentPolygonKey = state.currentPolygonKey;
    input.nextPolygonKey = state.nextPolygonKey;
    input.arrivalPolygonKey = state.arrivalPolygonKey;
    input.routeProgressDistanceNm = state.routeProgressDistanceNm;
    input.departureIcao = request.departureIcao;
    input.arrivalIcao = request.arrivalIcao;
    input.departureTerminalAuthorityHash =
        state.departureTerminalAuthorityHash;
    input.departureTerminalAuthority =
        state.departureTerminalAuthority;
    input.arrivalTerminalAuthorityHash =
        state.arrivalTerminalAuthorityHash;
    input.arrivalTerminalAuthority =
        state.arrivalTerminalAuthority;
    input.airportFrequencyHash =
        state.airportFrequencyHash;
    input.airportFrequencies =
        state.airportFrequencies;
    input.authorityRelevanceHash = request.authorityRelevanceHash;
    input.authorityRelevance = request.authorityRelevance;
    input.radioTuningHash = HashRadioTuningIdentity(request.radios);
    input.radios = request.radios;
    input.currentSectors = state.routePolygonSnapshot.currentSectors;
    input.nextSectors = state.routePolygonSnapshot.nextSectors;
    input.candidates = request.radioSnapshot.candidates;
    return input;
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
        state->lastDepartureTerminalAuthorityHash ==
            input.departureTerminalAuthorityHash &&
        state->lastArrivalTerminalAuthorityHash ==
            input.arrivalTerminalAuthorityHash &&
        state->lastAirportFrequencyHash ==
            input.airportFrequencyHash &&
        state->lastAuthorityRelevanceHash ==
            input.authorityRelevanceHash &&
        state->lastRadioTuningHash == input.radioTuningHash &&
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
    state->lastDepartureTerminalAuthorityHash =
        input.departureTerminalAuthorityHash;
    state->lastArrivalTerminalAuthorityHash =
        input.arrivalTerminalAuthorityHash;
    state->lastAirportFrequencyHash =
        input.airportFrequencyHash;
    state->lastAuthorityRelevanceHash =
        input.authorityRelevanceHash;
    state->lastRadioTuningHash = input.radioTuningHash;

    state->candidateCompletions.clear();
    for (const auto& completion : output.relevance.completions) {
        RecordBrainOwnedCandidateCompletion(state, completion);
    }
    state->candidatesComplete = true;
    state->lastIdleReason.clear();
    return output;
}

}  // namespace xvatsim::brain
