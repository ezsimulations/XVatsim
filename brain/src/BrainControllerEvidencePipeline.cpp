#include "XVatsim/brain/BrainControllerDecision.h"

#include "XVatsim/brain/BrainOwnedWorkerTypes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace xvatsim::brain {
namespace {

constexpr double kEarthRadiusNm = 3440.065;
constexpr double kCenterRangeNm = 250.0;

std::string Upper(std::string value) {
    value.erase(
        std::remove_if(value.begin(), value.end(), [](unsigned char c) {
            return std::isspace(c) != 0;
        }),
        value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

std::string UpperText(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

std::string NormalizeFrequency(const std::string& value) {
    std::string digits;
    bool decimal = false;
    int decimals = 0;
    for (const auto c : value) {
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            digits.push_back(c);
            if (decimal && decimals < 3) ++decimals;
        } else if (c == '.' && !decimal) {
            decimal = true;
        }
    }
    if (digits.empty()) return {};
    if (decimal) {
        while (decimals++ < 3) digits.push_back('0');
    } else if (digits.size() == 5) {
        digits.push_back('0');
    }
    return digits;
}

std::string DisplayFrequency(const std::string& value) {
    const auto digits = NormalizeFrequency(value);
    if (digits.size() != 6) return value;
    return digits.substr(0, 3) + "." + digits.substr(3);
}

bool GuardFrequency(const std::string& value) {
    const auto frequency = NormalizeFrequency(value);
    return frequency == "121500" || frequency == "199998";
}

bool Tuned(const std::string& frequency, const RadioStateSnapshot& radios) {
    const auto normalized = NormalizeFrequency(frequency);
    return !normalized.empty() &&
           (normalized == NormalizeFrequency(radios.com1ActiveFrequency) ||
            normalized == NormalizeFrequency(radios.com2ActiveFrequency));
}

bool SplitCallsign(
    const std::string& callsign,
    std::string* prefix,
    std::string* suffix) {
    const auto normalized = Upper(callsign);
    const auto separator = normalized.rfind('_');
    if (separator == std::string::npos || separator == 0 ||
        separator + 1 >= normalized.size()) {
        return false;
    }
    if (prefix != nullptr) *prefix = normalized.substr(0, separator);
    if (suffix != nullptr) *suffix = normalized.substr(separator + 1);
    return true;
}

StationRole Role(const RadioReachableControllerCandidate& candidate) {
    switch (candidate.group) {
        case RadioReachableFacilityGroup::Delivery:
            return StationRole::Delivery;
        case RadioReachableFacilityGroup::Ground:
            return StationRole::Ground;
        case RadioReachableFacilityGroup::Tower:
            return StationRole::Tower;
        case RadioReachableFacilityGroup::AppDep: {
            std::string suffix;
            SplitCallsign(candidate.callsign, nullptr, &suffix);
            return suffix == "DEP" ? StationRole::Departure
                                   : StationRole::Approach;
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

bool IsTerminalRole(StationRole role) {
    return role == StationRole::Delivery || role == StationRole::Ground ||
           role == StationRole::Tower || role == StationRole::Departure ||
           role == StationRole::Approach;
}

bool IsAppDep(StationRole role) {
    return role == StationRole::Departure || role == StationRole::Approach;
}

bool RoleMatchesPublished(StationRole candidate, StationRole published) {
    if (IsAppDep(candidate) && IsAppDep(published)) return true;
    return candidate == published;
}

BrainControllerEvidenceVote Vote(
    std::string source,
    int score,
    std::string reason,
    std::string provenance = {}) {
    BrainControllerEvidenceVote vote;
    vote.source = std::move(source);
    vote.score = score > 0 ? 1 : score < 0 ? -1 : 0;
    vote.reason = std::move(reason);
    vote.provenance = std::move(provenance);
    return vote;
}

double Radians(double degrees) {
    return degrees * 3.14159265358979323846 / 180.0;
}

double DistanceNm(double latA, double lonA, double latB, double lonB) {
    const auto dLat = Radians(latB - latA);
    const auto dLon = Radians(lonB - lonA);
    const auto a = std::sin(dLat / 2.0) * std::sin(dLat / 2.0) +
                   std::cos(Radians(latA)) * std::cos(Radians(latB)) *
                       std::sin(dLon / 2.0) * std::sin(dLon / 2.0);
    const auto bounded = std::clamp(a, 0.0, 1.0);
    return kEarthRadiusNm * 2.0 *
           std::atan2(std::sqrt(bounded), std::sqrt(1.0 - bounded));
}

std::optional<double> NearestAirportTransmitter(
    const RadioReachableControllerCandidate& candidate,
    bool hasAirport,
    double airportLat,
    double airportLon) {
    if (!hasAirport || !std::isfinite(airportLat) ||
        !std::isfinite(airportLon)) {
        return std::nullopt;
    }
    double nearest = std::numeric_limits<double>::infinity();
    for (const auto& station : candidate.stationCoordinates) {
        if (!std::isfinite(station.latitudeDeg) ||
            !std::isfinite(station.longitudeDeg) ||
            (station.latitudeDeg == 0.0 && station.longitudeDeg == 0.0)) {
            continue;
        }
        nearest = std::min(
            nearest,
            DistanceNm(
                airportLat,
                airportLon,
                station.latitudeDeg,
                station.longitudeDeg));
    }
    return std::isfinite(nearest) ? std::optional<double>(nearest)
                                  : std::nullopt;
}

std::string Nm(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << value << "nm";
    return out.str();
}

bool UsAirport(const std::string& airport) {
    const auto normalized = Upper(airport);
    return normalized.size() == 4 &&
           (normalized.front() == 'K' || normalized.front() == 'P');
}

bool CallsignRoleValid(
    const RadioReachableControllerCandidate& candidate,
    StationRole role) {
    std::string suffix;
    if (!SplitCallsign(candidate.callsign, nullptr, &suffix)) return false;
    switch (role) {
        case StationRole::Delivery: return suffix == "DEL";
        case StationRole::Ground: return suffix == "GND";
        case StationRole::Tower: return suffix == "TWR";
        case StationRole::Departure: return suffix == "DEP";
        case StationRole::Approach: return suffix == "APP";
        case StationRole::Center: return suffix == "CTR" || suffix == "FSS";
        default: return false;
    }
}

BrainControllerEvidenceVote VatsimVote(
    const RadioReachableControllerCandidate& candidate,
    StationRole role) {
    const auto valid = CallsignRoleValid(candidate, role);
    return Vote(
        "vatsim-parse",
        valid ? 1 : -1,
        valid ? "live-role-valid" : "callsign-role-mismatch",
        Upper(candidate.callsign) + "@" + DisplayFrequency(candidate.frequency));
}

std::vector<std::string> AirportTokens(const std::string& airport) {
    std::vector<std::string> tokens;
    const auto normalized = Upper(airport);
    if (normalized.empty()) return tokens;
    tokens.push_back(normalized);
    if (normalized.size() == 4) tokens.push_back(normalized.substr(1));
    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
    return tokens;
}

std::size_t EditDistance(const std::string& left, const std::string& right) {
    std::vector<std::size_t> previous(right.size() + 1);
    std::vector<std::size_t> current(right.size() + 1);
    for (std::size_t j = 0; j <= right.size(); ++j) previous[j] = j;
    for (std::size_t i = 1; i <= left.size(); ++i) {
        current[0] = i;
        for (std::size_t j = 1; j <= right.size(); ++j) {
            current[j] = std::min({
                previous[j] + 1,
                current[j - 1] + 1,
                previous[j - 1] + (left[i - 1] == right[j - 1] ? 0u : 1u),
            });
        }
        previous.swap(current);
    }
    return previous.back();
}

BrainControllerEvidenceVote TerminalVatsimVote(
    const RadioReachableControllerCandidate& candidate,
    StationRole role,
    const std::string& airport,
    bool routeContextMatch) {
    if (!CallsignRoleValid(candidate, role)) {
        return Vote(
            "vatsim-parse", -1, "callsign-role-mismatch", candidate.callsign);
    }
    std::string prefix;
    SplitCallsign(candidate.callsign, &prefix, nullptr);
    for (const auto& token : AirportTokens(airport)) {
        if (prefix == token ||
            (prefix.size() > token.size() &&
             prefix.compare(0, token.size(), token) == 0 &&
             (prefix[token.size()] == '-' || prefix[token.size()] == '_'))) {
            return Vote(
                "vatsim-parse",
                1,
                "endpoint-callsign-match",
                prefix + "=" + token);
        }
        if (token.size() >= 2 && prefix.size() >= 2 &&
            EditDistance(prefix, token) <= 1) {
            return Vote(
                "vatsim-parse",
                1,
                "endpoint-callsign-partial-match-medium-confidence",
                prefix + "~" + token);
        }
    }
    if (routeContextMatch) {
        return Vote(
            "vatsim-parse",
            0,
            "regional-callsign-needs-route-context",
            prefix);
    }
    return Vote(
        "vatsim-parse",
        0,
        "live-role-valid-endpoint-relationship-unknown",
        prefix);
}

std::size_t SharedPrefix(const std::string& left, const std::string& right) {
    const auto count = std::min(left.size(), right.size());
    std::size_t shared = 0;
    while (shared < count && left[shared] == right[shared]) ++shared;
    return shared;
}

struct VatspyResult {
    BrainControllerEvidenceVote vote;
    bool endpointPositive = false;
};

VatspyResult VatspyVote(
    const BrainControllerRelevanceWorkerInput& input,
    const RadioReachableControllerCandidate& candidate,
    const std::string& airport) {
    VatspyResult result;
    std::string prefix;
    if (!SplitCallsign(candidate.callsign, &prefix, nullptr)) {
        result.vote = Vote("vatspy-parse", -1, "callsign-prefix-unavailable");
        return result;
    }
    if (input.route == nullptr) {
        result.vote = Vote("vatspy-parse", 0, "airport-alias-data-unavailable");
        return result;
    }

    std::vector<const AirportCallsignAliasSnapshot*> endpointAliases;
    for (const auto& alias : input.route->airportCallsignAliases) {
        if (Upper(alias.airportIcao) == Upper(airport)) {
            endpointAliases.push_back(&alias);
        }
    }
    if (endpointAliases.empty()) {
        result.vote = Vote("vatspy-parse", 0, "airport-alias-record-missing");
        return result;
    }

    for (const auto* alias : endpointAliases) {
        const auto expected = Upper(alias->callsignPrefix);
        if (prefix == expected ||
            (prefix.size() > expected.size() &&
             prefix.compare(0, expected.size(), expected) == 0 &&
             (prefix[expected.size()] == '-' || prefix[expected.size()] == '_'))) {
            result.endpointPositive = true;
            result.vote = Vote(
                "vatspy-parse",
                1,
                "exact-airport-alias-match",
                alias->source + ":" + alias->airportIcao + "=" +
                    alias->callsignPrefix);
            return result;
        }
    }

    const AirportCallsignAliasSnapshot* best = nullptr;
    std::size_t bestShared = 0;
    for (const auto* alias : endpointAliases) {
        const auto expected = Upper(alias->callsignPrefix);
        const auto shared = SharedPrefix(prefix, expected);
        if (shared > bestShared) {
            best = alias;
            bestShared = shared;
        }
    }
    if (best != nullptr && bestShared >= 2 &&
        bestShared + 2 >= std::max(prefix.size(), Upper(best->callsignPrefix).size())) {
        result.endpointPositive = true;
        result.vote = Vote(
            "vatspy-parse",
            1,
            "partial-airport-alias-match-medium-confidence",
            best->source + ":" + prefix + "~" + best->callsignPrefix);
        return result;
    }

    result.vote = Vote(
        "vatspy-parse",
        -1,
        "airport-alias-mismatch",
        prefix + "!=" + endpointAliases.front()->callsignPrefix);
    return result;
}

int VnasOwnershipRank(const VnasTerminalEvidenceRecord& record) {
    if (!record.ownershipFactsComplete) return 0;
    if (!record.serviceCompatible) return 1;
    if (record.tdlsDepartureFrequencyMatch) return 4;
    if (!record.matchedTcpTokens.empty() ||
        (record.areaSupportsEndpoint &&
         !record.positionTcpHasEndpointDescendant)) {
        return 3;
    }
    if (record.areaSupportsEndpoint &&
        record.positionTcpHasEndpointDescendant) {
        return 2;
    }
    return 1;
}

const VnasTerminalEvidenceRecord* FindVnasRecord(
    const BrainControllerRelevanceWorkerInput& input,
    const RadioReachableControllerCandidate& candidate,
    const std::string& airport) {
    if (!input.vnasTerminalEvidence) return nullptr;
    for (const auto& record : input.vnasTerminalEvidence->records) {
        if (record.supportsEndpoint &&
            Upper(record.controllerCallsign) == Upper(candidate.callsign) &&
            NormalizeFrequency(record.controllerFrequency) ==
                NormalizeFrequency(candidate.frequency) &&
            Upper(record.airportIcao) == Upper(airport)) {
            return &record;
        }
    }
    return nullptr;
}

int BestOnlineVnasRank(
    const BrainControllerRelevanceWorkerInput& input,
    const std::string& airport) {
    int best = 0;
    for (const auto& candidate : input.candidates) {
        if (!candidate.actionable || candidate.atis ||
            candidate.group != RadioReachableFacilityGroup::AppDep) {
            continue;
        }
        const auto* record = FindVnasRecord(input, candidate, airport);
        if (record != nullptr && record->serviceCompatible) {
            best = std::max(best, VnasOwnershipRank(*record));
        }
    }
    return best;
}

struct VnasEvaluation {
    BrainControllerEvidenceVote vote;
    bool ownershipApplicable = true;
};

VnasEvaluation VnasVote(
    const BrainControllerRelevanceWorkerInput& input,
    const RadioReachableControllerCandidate& candidate,
    const std::string& airport) {
    if (!UsAirport(airport)) {
        return {Vote("vnas", 0, "outside-us-scope", Upper(airport)), true};
    }
    if (!input.vnasTerminalEvidence ||
        !input.vnasTerminalEvidence->enabled) {
        return {Vote("vnas", 0, "disabled"), true};
    }
    const auto& snapshot = *input.vnasTerminalEvidence;
    if (!snapshot.available || snapshot.stale) {
        return {Vote("vnas", 0, "unavailable-or-stale"), true};
    }
    if (const auto* record = FindVnasRecord(input, candidate, airport)) {
        if (record->ownershipFactsComplete && !record->serviceCompatible) {
            return {
                Vote(
                    "vnas",
                    -1,
                    "operation-incompatible",
                    record->proof),
                false,
            };
        }
        const auto rank = VnasOwnershipRank(*record);
        const auto bestRank = BestOnlineVnasRank(input, airport);
        if (rank > 0 && rank < bestRank) {
            return {
                Vote(
                    "vnas",
                    0,
                    "lower-operational-owner-online",
                    record->proof),
                false,
            };
        }
        return {
            Vote("vnas", 1, "endpoint-ownership-match", record->proof),
            true,
        };
    }
    if (snapshot.facilityDataComplete && snapshot.ownershipDataAvailable) {
        return {
            Vote("vnas", -1, "complete-us-data-no-endpoint-match"),
            true,
        };
    }
    return {Vote("vnas", 0, "incomplete-us-data-no-match"), true};
}

BrainControllerEvidenceVote DistanceVote(
    const BrainControllerRelevanceWorkerInput& input,
    const RadioReachableControllerCandidate& candidate,
    bool departure) {
    const auto distance = NearestAirportTransmitter(
        candidate,
        departure ? input.hasDepartureCoordinates : input.hasArrivalCoordinates,
        departure ? input.departureLatitudeDeg : input.arrivalLatitudeDeg,
        departure ? input.departureLongitudeDeg : input.arrivalLongitudeDeg);
    if (!distance.has_value()) {
        return Vote("frequency-distance", 0, "transmitter-or-airport-position-unknown");
    }
    const auto threshold = input.terminalTransmitterRadiusNm > 0.0
                               ? input.terminalTransmitterRadiusNm
                               : 5.0;
    return Vote(
        "frequency-distance",
        *distance <= threshold ? 1 : -1,
        *distance <= threshold ? "within-airport-radius" : "outside-airport-radius",
        Nm(*distance) + "<=" + Nm(threshold));
}

std::string TerminalOwnerToken(const std::string& callsign) {
    std::string prefix;
    std::string suffix;
    if (!SplitCallsign(callsign, &prefix, &suffix) ||
        (suffix != "APP" && suffix != "DEP")) {
        return {};
    }
    const auto first = prefix.find('_');
    const auto owner = first == std::string::npos ? prefix : prefix.substr(0, first);
    return owner.empty() ? std::string{} : owner + "_" + suffix;
}

bool SourceAuthorityMatch(
    const BrainControllerRelevanceWorkerInput& input,
    const RadioReachableControllerCandidate& candidate,
    std::string* proof) {
    if (!input.authorityRelevance || !input.authorityRelevance->available ||
        input.authorityRelevance->stale) {
        return false;
    }
    for (const auto& authority : input.authorityRelevance->relevantAuthorities) {
        if (Upper(authority.callsign) != Upper(candidate.callsign)) continue;
        if (!authority.frequency.empty() &&
            NormalizeFrequency(authority.frequency) !=
                NormalizeFrequency(candidate.frequency)) {
            continue;
        }
        if (proof != nullptr) *proof = authority.proofSource;
        return true;
    }
    return false;
}

BrainControllerEvidenceVote TerminalSourceVote(
    const BrainControllerRelevanceWorkerInput& input,
    const RadioReachableControllerCandidate& candidate,
    const BrainTerminalAuthorityWorkerOutput& authority,
    StationRole role) {
    std::string sourceProof;
    if (SourceAuthorityMatch(input, candidate, &sourceProof)) {
        return Vote("terminal-source", 1, "source-authority-match", sourceProof);
    }
    if (!IsAppDep(role)) {
        return Vote("terminal-source", 0, "not-appdep-authority-scope");
    }
    if (!authority.available || !authority.resolved || authority.stale ||
        authority.ownerTokens.empty()) {
        return Vote("terminal-source", 0, "terminal-authority-unavailable");
    }
    const auto owner = TerminalOwnerToken(candidate.callsign);
    for (const auto& expected : authority.ownerTokens) {
        if (Upper(expected) == owner) {
            return Vote("terminal-source", 1, "terminal-owner-match", owner);
        }
    }
    return Vote(
        "terminal-source", -1, "terminal-owner-mismatch", owner);
}

const std::vector<BrainAirportFrequencyRecord>& FrequencyRecords(
    const BrainControllerRelevanceWorkerInput& input,
    bool departure) {
    return departure ? input.airportFrequencies.departureFrequencies
                     : input.airportFrequencies.arrivalFrequencies;
}

BrainControllerEvidenceVote PublishedFrequencyVote(
    const BrainControllerRelevanceWorkerInput& input,
    const RadioReachableControllerCandidate& candidate,
    StationRole role,
    bool departure) {
    if (!input.airportFrequencies.available ||
        !input.airportFrequencies.resolved || input.airportFrequencies.stale) {
        return Vote("published-frequency", 0, "airport-frequency-data-unavailable");
    }
    bool roleFacts = false;
    for (const auto& record : FrequencyRecords(input, departure)) {
        if (!RoleMatchesPublished(role, record.role)) continue;
        roleFacts = true;
        if (NormalizeFrequency(record.frequency) ==
            NormalizeFrequency(candidate.frequency)) {
            return Vote(
                "published-frequency",
                1,
                "role-frequency-match",
                record.airportIcao + ":" + record.frequency);
        }
    }
    return roleFacts
        ? Vote(
              "published-frequency",
              0,
              "role-frequency-not-confirmed-pseudo-frequency-possible")
        : Vote("published-frequency", 0, "endpoint-role-record-missing");
}

struct EndpointEvaluation {
    bool departure = true;
    BrainControllerDecisionReceipt receipt;
    std::string airport;
};

EndpointEvaluation EvaluateTerminal(
    const BrainControllerRelevanceWorkerInput& input,
    const RadioReachableControllerCandidate& candidate,
    StationRole role,
    bool departure,
    bool phaseApplicable,
    bool routeContextMatch) {
    EndpointEvaluation evaluation;
    evaluation.departure = departure;
    evaluation.airport = departure ? input.departureIcao : input.arrivalIcao;

    const auto vatspy = VatspyVote(input, candidate, evaluation.airport);
    auto distance = DistanceVote(input, candidate, departure);
    auto vnas = VnasVote(input, candidate, evaluation.airport);
    auto terminal = TerminalSourceVote(
        input,
        candidate,
        departure ? input.departureTerminalAuthority
                  : input.arrivalTerminalAuthority,
        role);
    auto published = PublishedFrequencyVote(
        input, candidate, role, departure);
    auto vatsim = TerminalVatsimVote(
        candidate, role, evaluation.airport, routeContextMatch);
    auto routeContext = Vote(
        "route-context",
        routeContextMatch ? 1 : 0,
        routeContextMatch ? "terminal-root-matches-current-center"
                          : "no-current-center-root-match");

    BrainControllerDecisionInput decision;
    decision.actionable = candidate.actionable;
    decision.atis = candidate.atis;
    decision.supportedFacility = IsTerminalRole(role);
    decision.guardFrequency = GuardFrequency(candidate.frequency);
    decision.phaseApplicable = phaseApplicable;
    decision.relationAvailable =
        vatsim.score > 0 || vatspy.endpointPositive || distance.score > 0 ||
        vnas.vote.score > 0 ||
        terminal.score > 0 || published.score > 0 || routeContext.score > 0;
    decision.ownershipApplicable =
        !input.vnasSectorPrecedenceEnabled || vnas.ownershipApplicable;
    decision.supportedTiesDisplay =
        input.terminalRelevanceV2Enabled || routeContextMatch;
    decision.votes = {
        std::move(vatsim),
        vatspy.vote,
        std::move(vnas.vote),
        std::move(distance),
        std::move(terminal),
        std::move(published),
        std::move(routeContext),
    };
    evaluation.receipt = DecideBrainControllerCandidate(decision);
    return evaluation;
}

int ReceiptMargin(const BrainControllerDecisionReceipt& receipt) {
    return receipt.positiveVotes - receipt.negativeVotes;
}

const std::vector<RouteSectorMatchSnapshot>& CurrentSectors(
    const BrainControllerRelevanceWorkerInput& input) {
    return input.route ? input.route->currentSectors : input.currentSectors;
}

const std::vector<RouteSectorMatchSnapshot>& NextSectors(
    const BrainControllerRelevanceWorkerInput& input) {
    return input.route ? input.route->nextSectors : input.nextSectors;
}

bool PrefixMatch(const std::string& rawPrefix, const std::string& rawCallsign) {
    const auto prefix = Upper(rawPrefix);
    const auto callsign = Upper(rawCallsign);
    if (prefix.empty() || callsign.empty()) return false;
    return callsign == prefix ||
           (callsign.size() > prefix.size() &&
            callsign.compare(0, prefix.size(), prefix) == 0 &&
            (callsign[prefix.size()] == '_' || callsign[prefix.size()] == '-'));
}

bool WildcardMatch(std::string pattern, const std::string& value) {
    pattern = Upper(std::move(pattern));
    const auto target = Upper(value);
    std::size_t p = 0, v = 0, star = std::string::npos, retry = 0;
    while (v < target.size()) {
        if (p < pattern.size() && pattern[p] == target[v]) {
            ++p; ++v;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++; retry = v;
        } else if (star != std::string::npos) {
            p = star + 1; v = ++retry;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

bool SectorMatchesController(
    const RouteSectorMatchSnapshot& sector,
    const std::string& callsign,
    std::string* proof) {
    for (const auto& pattern : sector.controllerCallsignPatterns) {
        if ((pattern.find('*') == std::string::npos &&
             Upper(pattern) == Upper(callsign)) ||
            (pattern.find('*') != std::string::npos &&
             WildcardMatch(pattern, callsign))) {
            if (proof) *proof = "pattern:" + Upper(pattern);
            return true;
        }
    }
    for (const auto& prefix : sector.controllerPrefixes) {
        if (PrefixMatch(prefix, callsign)) {
            if (proof) *proof = "prefix:" + Upper(prefix);
            return true;
        }
    }
    for (const auto& token : sector.matchTokens) {
        if (PrefixMatch(token, callsign)) {
            if (proof) *proof = "token:" + Upper(token);
            return true;
        }
    }
    if (PrefixMatch(sector.identifier, callsign)) {
        if (proof) *proof = "sector:" + Upper(sector.identifier);
        return true;
    }
    return false;
}

std::string ControllerRoot(const std::string& callsign) {
    const auto normalized = Upper(callsign);
    const auto separator = normalized.find('_');
    return separator == std::string::npos ? normalized
                                          : normalized.substr(0, separator);
}

std::unordered_set<std::string> CurrentCenterRoots(
    const BrainControllerRelevanceWorkerInput& input) {
    std::unordered_set<std::string> roots;
    for (const auto& candidate : input.candidates) {
        if (candidate.group != RadioReachableFacilityGroup::Center) continue;
        const auto matched = std::any_of(
            CurrentSectors(input).begin(),
            CurrentSectors(input).end(),
            [&](const auto& sector) {
                return SectorMatchesController(sector, candidate.callsign, nullptr);
            });
        if (matched) roots.insert(ControllerRoot(candidate.callsign));
    }
    return roots;
}

void AddSectorAlias(
    std::unordered_set<std::string>* aliases,
    std::string value) {
    if (aliases == nullptr) return;
    value = Upper(std::move(value));
    if (value.empty()) return;
    const auto role = value.rfind('_');
    if (role != std::string::npos) value = value.substr(0, role);
    aliases->insert(value);
    if (value.size() == 4 && value.front() == 'Y') {
        aliases->insert(value.substr(1));
    }
    const auto dash = value.rfind('-');
    if (dash != std::string::npos && dash + 1 < value.size()) {
        aliases->insert(value.substr(dash + 1));
    }
}

std::unordered_set<std::string> SectorAliases(
    const RouteSectorMatchSnapshot& sector) {
    std::unordered_set<std::string> aliases;
    AddSectorAlias(&aliases, sector.identifier);
    for (const auto& value : sector.matchTokens) AddSectorAlias(&aliases, value);
    for (const auto& value : sector.controllerPrefixes) AddSectorAlias(&aliases, value);
    for (const auto& value : sector.controllerCallsignPatterns) {
        if (value.find('*') == std::string::npos) AddSectorAlias(&aliases, value);
    }
    return aliases;
}

struct ExtensionDeclaration {
    std::string sectorToken;
    std::string frequency;
};

std::vector<ExtensionDeclaration> ParseExtensions(const std::string& status) {
    const auto upper = UpperText(status);
    const auto coverage = upper.find("EXTEND") != std::string::npos ||
                          upper.find("COVERING") != std::string::npos ||
                          upper.find("COMBINING") != std::string::npos ||
                          upper.find("CONSOLIDAT") != std::string::npos;
    const auto negated = upper.find("NOT EXTEND") != std::string::npos ||
                         upper.find("NO LONGER EXTEND") != std::string::npos ||
                         upper.find("WITHDRAW") != std::string::npos ||
                         upper.find("CEASE") != std::string::npos ||
                         upper.find("WILL EXTEND") != std::string::npos;
    if (!coverage || negated) return {};

    static const std::regex pair(
        R"(([A-Z][A-Z0-9-]{1,12})\s+((?:11[89]|12[0-9]|13[0-6])(?:\.\d{1,3})?))");
    std::vector<ExtensionDeclaration> declarations;
    for (auto it = std::sregex_iterator(upper.begin(), upper.end(), pair);
         it != std::sregex_iterator(); ++it) {
        ExtensionDeclaration declaration;
        declaration.sectorToken = Upper((*it)[1].str());
        declaration.frequency = DisplayFrequency((*it)[2].str());
        declarations.push_back(std::move(declaration));
    }
    return declarations;
}

const TransceiverStationEvidenceSnapshot* FindExtensionChannel(
    const RadioReachableControllerCandidate& candidate,
    const std::string& frequency) {
    const auto target = NormalizeFrequency(frequency);
    const TransceiverStationEvidenceSnapshot* best = nullptr;
    for (const auto& station : candidate.originalTransceivers) {
        if (NormalizeFrequency(station.sourceFrequency) != target ||
            !station.withinMaxCandidateDistance ||
            (station.hasReceivableRange && !station.withinReceivableRange) ||
            (station.hasAircraftDistance &&
             station.aircraftDistanceNm > kCenterRangeNm)) {
            continue;
        }
        if (best == nullptr ||
            (station.hasAircraftDistance &&
             (!best->hasAircraftDistance ||
              station.aircraftDistanceNm < best->aircraftDistanceNm))) {
            best = &station;
        }
    }
    return best;
}

struct CenterEvaluation {
    bool matched = false;
    bool radioReachable = false;
    std::string frequency;
    std::string polygonKey;
    DisplayRelation relation = DisplayRelation::Hidden;
    bool hasEntryDistance = false;
    double entryDistanceNm = 0.0;
    std::string context;
    BrainControllerDecisionReceipt receipt;
};

CenterEvaluation EvaluateCenter(
    const BrainControllerRelevanceWorkerInput& input,
    const RadioReachableControllerCandidate& candidate,
    bool phaseApplicable) {
    CenterEvaluation evaluation;
    evaluation.frequency = candidate.frequency;
    const auto declarations = ParseExtensions(candidate.textAtis);
    bool extensionRouteMatch = false;
    bool extensionChannelMatch = false;
    std::string extensionProof;

    auto tryExtensions = [&](const std::vector<RouteSectorMatchSnapshot>& sectors,
                             DisplayRelation relation) {
        for (const auto& sector : sectors) {
            const auto aliases = SectorAliases(sector);
            for (const auto& declaration : declarations) {
                if (aliases.find(declaration.sectorToken) == aliases.end()) continue;
                extensionRouteMatch = true;
                const auto* channel =
                    FindExtensionChannel(candidate, declaration.frequency);
                if (channel == nullptr) continue;
                extensionChannelMatch = true;
                evaluation.matched = true;
                evaluation.radioReachable = true;
                evaluation.frequency = DisplayFrequency(channel->sourceFrequency);
                evaluation.polygonKey = !sector.identifier.empty()
                    ? sector.identifier
                    : relation == DisplayRelation::CurrentPolygon
                        ? input.currentPolygonKey
                        : input.nextPolygonKey;
                evaluation.relation = relation;
                evaluation.hasEntryDistance =
                    relation == DisplayRelation::NextPolygon;
                evaluation.entryDistanceNm =
                    evaluation.hasEntryDistance
                        ? std::max(0.0, sector.entryDistanceNm)
                        : 0.0;
                extensionProof = declaration.sectorToken + "@" +
                                 evaluation.frequency;
                evaluation.context = "center-extension-match:" + extensionProof;
                return true;
            }
        }
        return false;
    };

    if (!tryExtensions(CurrentSectors(input), DisplayRelation::CurrentPolygon) &&
        !tryExtensions(NextSectors(input), DisplayRelation::NextPolygon)) {
        auto tryDirect = [&](const std::vector<RouteSectorMatchSnapshot>& sectors,
                             DisplayRelation relation) {
            for (const auto& sector : sectors) {
                std::string proof;
                if (!SectorMatchesController(sector, candidate.callsign, &proof)) {
                    continue;
                }
                evaluation.matched = true;
                evaluation.frequency = candidate.frequency;
                evaluation.polygonKey = !sector.identifier.empty()
                    ? sector.identifier
                    : relation == DisplayRelation::CurrentPolygon
                        ? input.currentPolygonKey
                        : input.nextPolygonKey;
                evaluation.relation = relation;
                evaluation.hasEntryDistance =
                    relation == DisplayRelation::NextPolygon;
                evaluation.entryDistanceNm =
                    evaluation.hasEntryDistance
                        ? std::max(0.0, sector.entryDistanceNm)
                        : 0.0;
                evaluation.context = relation == DisplayRelation::CurrentPolygon
                    ? "center-current-polygon-match:" + proof
                    : "center-next-polygon-match:" + proof;
                return true;
            }
            return false;
        };
        if (!tryDirect(CurrentSectors(input), DisplayRelation::CurrentPolygon)) {
            tryDirect(NextSectors(input), DisplayRelation::NextPolygon);
        }
        evaluation.radioReachable =
            evaluation.matched && candidate.hasDistanceNm &&
            std::isfinite(candidate.distanceNm) &&
            candidate.distanceNm <= kCenterRangeNm;
    }

    const auto routeMetadata = !CurrentSectors(input).empty() ||
                               !NextSectors(input).empty();
    BrainControllerDecisionInput decision;
    decision.actionable = candidate.actionable;
    decision.atis = candidate.atis;
    decision.supportedFacility = true;
    decision.guardFrequency = GuardFrequency(evaluation.frequency);
    decision.phaseApplicable = phaseApplicable;
    decision.relationAvailable = evaluation.matched;
    decision.radioReachable =
        !input.terminalRelevanceV2Enabled || evaluation.radioReachable;
    decision.votes = {
        VatsimVote(candidate, StationRole::Center),
        Vote(
            "vatsim-extension",
            extensionChannelMatch ? 1 : 0,
            extensionChannelMatch
                ? "declared-current-route-extension"
                : extensionRouteMatch
                    ? "declared-extension-channel-unavailable"
                    : declarations.empty()
                        ? "no-valid-extension-declaration"
                        : "extension-not-on-current-route",
            extensionProof),
        Vote(
            "vatspy-parse",
            evaluation.matched ? 1 : routeMetadata ? -1 : 0,
            evaluation.matched ? "route-sector-authority-match"
                               : routeMetadata ? "route-sector-authority-mismatch"
                                               : "route-sector-data-unavailable",
            evaluation.polygonKey),
        Vote(
            "route-geometry",
            evaluation.matched ? 1 : routeMetadata ? -1 : 0,
            evaluation.matched ? "current-or-next-sector-match"
                               : routeMetadata ? "no-current-or-next-match"
                                               : "route-geometry-unavailable",
            evaluation.polygonKey),
        Vote(
            "radio-channel",
            evaluation.radioReachable ? 1
                : (evaluation.matched &&
                   (candidate.hasDistanceNm || extensionRouteMatch)) ? -1 : 0,
            evaluation.radioReachable ? "channel-within-250nm"
                : evaluation.matched ? "channel-outside-or-unavailable"
                                     : "route-relation-unresolved",
            "login=" + DisplayFrequency(candidate.frequency) +
                ",selected=" + DisplayFrequency(evaluation.frequency)),
    };
    evaluation.receipt = DecideBrainControllerCandidate(decision);
    if (evaluation.context.empty()) {
        evaluation.context = routeMetadata ? "center-not-route-polygon-match"
                                           : "center-route-metadata-unavailable";
    }
    return evaluation;
}

std::string CandidateKey(const RadioReachableControllerCandidate& candidate) {
    if (!candidate.stableKey.empty()) return candidate.stableKey;
    return Upper(candidate.callsign) + "|" + NormalizeFrequency(candidate.frequency) +
           "|" + std::to_string(static_cast<int>(candidate.group));
}

BrainOwnedCandidateCompletion MakeCompletion(
    const BrainControllerRelevanceWorkerInput& input,
    const RadioReachableControllerCandidate& candidate,
    const BoardStationSnapshot& station,
    DisplayRelation relation,
    const BrainControllerDecisionReceipt& receipt,
    const std::string& context) {
    auto keyed = candidate;
    keyed.stableKey = CandidateKey(candidate);
    BrainOwnedCandidateCompletion completion;
    completion.radioBoardHash = input.radioBoardHash;
    completion.routePolygonHash = input.routePolygonHash;
    completion.workflowStage = input.workflowStage;
    completion.currentPolygonIndex = input.currentPolygonIndex;
    completion.currentPolygonKey = input.currentPolygonKey;
    completion.matchedPolygonKey = station.polygonKey;
    completion.callsign = candidate.callsign;
    completion.frequency = station.frequency.empty() ? candidate.frequency
                                                      : station.frequency;
    completion.facilityGroup = candidate.group;
    completion.displayRelation = receipt.accepted ? relation : DisplayRelation::Hidden;
    completion.decision = receipt.accepted ? BrainOwnedCandidateDecision::Accepted
                                           : BrainOwnedCandidateDecision::Rejected;
    completion.displayed = false;
    completion.hasRouteEntryDistance = station.hasRouteEntryDistance;
    completion.routeEntryDistanceNm = station.routeEntryDistanceNm;
    completion.positiveVotes = receipt.positiveVotes;
    completion.negativeVotes = receipt.negativeVotes;
    completion.neutralVotes = receipt.neutralVotes;
    completion.evidenceVotes = receipt.votes;
    completion.reason = context + ":" +
                        FormatBrainControllerDecisionReceipt(receipt);
    completion.stableKey = BuildBrainOwnedCandidateCompletionKey(
        input.radioBoardHash,
        input.routePolygonHash,
        input.workflowStage,
        input.currentPolygonKey,
        keyed);
    return completion;
}

void AppendUnique(
    BoardStationSnapshot station,
    const BrainOwnedCandidateCompletion& completion,
    ModuleBoardSnapshot* board,
    std::unordered_set<std::string>* keys) {
    if (board == nullptr || keys == nullptr ||
        completion.decision != BrainOwnedCandidateDecision::Accepted ||
        station.frequency.empty()) {
        return;
    }
    const auto key = std::to_string(static_cast<int>(station.role)) + "|" +
                     Upper(station.callsign) + "|" +
                     NormalizeFrequency(station.frequency);
    if (!keys->insert(key).second) return;
    station.stableCompletionKey = completion.stableKey;
    board->stations.push_back(std::move(station));
    board->available = true;
}

}  // namespace

BrainControllerRelevanceWorkerOutput RunBrainControllerEvidencePipeline(
    const BrainControllerRelevanceWorkerInput& input) {
    BrainControllerRelevanceWorkerOutput output;
    output.available = true;
    output.stale = false;
    output.reason = "brain-controller-evidence-pipeline";
    output.departureBoard.source = BoardSource::Departure;
    output.departureBoard.airportIcao = input.departureIcao;
    output.arrivalBoard.source = BoardSource::Arrival;
    output.arrivalBoard.airportIcao = input.arrivalIcao;
    output.enrouteBoard.source = BoardSource::Enroute;

    std::unordered_set<std::string> departureKeys;
    std::unordered_set<std::string> arrivalKeys;
    std::unordered_set<std::string> enrouteKeys;
    const auto currentCenterRoots = CurrentCenterRoots(input);

    const auto departurePhase = input.workflowStage == WorkflowStage::None ||
                                input.workflowStage == WorkflowStage::Departure;
    const auto arrivalPhase = input.workflowStage == WorkflowStage::None ||
                              input.workflowStage == WorkflowStage::Arrival;
    const auto centerPhase = input.workflowStage == WorkflowStage::None ||
                             input.workflowStage == WorkflowStage::Departure ||
                             input.workflowStage == WorkflowStage::Enroute ||
                             input.workflowStage == WorkflowStage::Arrival;

    output.completions.reserve(input.candidates.size());
    for (const auto& candidate : input.candidates) {
        const auto role = Role(candidate);
        BoardStationSnapshot station;
        station.role = role;
        station.callsign = candidate.callsign;
        station.frequency = candidate.frequency;
        station.tuned = Tuned(station.frequency, input.radios);
        station.online = candidate.actionable;
        station.offline = !candidate.actionable;
        station.sourceEvidenceId = "radio-reachable:" + CandidateKey(candidate);
        station.sourceEvidenceType = "radio-reachable-controller";
        station.sourceEvidenceDomain = "controller-relevance";
        station.sourceEvidenceLinkStatus = "linked";

        if (role == StationRole::Center) {
            const auto evaluation = EvaluateCenter(input, candidate, centerPhase);
            station.frequency = evaluation.frequency;
            station.tuned = Tuned(station.frequency, input.radios);
            station.polygonKey = evaluation.polygonKey;
            station.sectorActive =
                evaluation.relation == DisplayRelation::CurrentPolygon ||
                station.tuned;
            station.hasRouteEntryDistance = evaluation.hasEntryDistance;
            station.routeEntryDistanceNm = evaluation.entryDistanceNm;
            auto completion = MakeCompletion(
                input,
                candidate,
                station,
                evaluation.relation,
                evaluation.receipt,
                evaluation.context);
            if (evaluation.receipt.accepted) {
                AppendUnique(
                    station,
                    completion,
                    &output.enrouteBoard,
                    &enrouteKeys);
            }
            output.completions.push_back(std::move(completion));
            continue;
        }

        if (!IsTerminalRole(role)) {
            BrainControllerDecisionInput decision;
            decision.actionable = candidate.actionable;
            decision.atis = candidate.atis ||
                            candidate.group == RadioReachableFacilityGroup::Atis;
            decision.supportedFacility = false;
            decision.guardFrequency = GuardFrequency(candidate.frequency);
            decision.phaseApplicable = false;
            decision.relationAvailable = false;
            decision.votes = {VatsimVote(candidate, role)};
            const auto receipt = DecideBrainControllerCandidate(decision);
            output.completions.push_back(MakeCompletion(
                input,
                candidate,
                station,
                DisplayRelation::Hidden,
                receipt,
                "facility-not-ui-relevant"));
            continue;
        }

        auto departure = EvaluateTerminal(
            input,
            candidate,
            role,
            true,
            departurePhase,
            currentCenterRoots.find(ControllerRoot(candidate.callsign)) !=
                currentCenterRoots.end());
        auto arrival = EvaluateTerminal(
            input,
            candidate,
            role,
            false,
            arrivalPhase,
            currentCenterRoots.find(ControllerRoot(candidate.callsign)) !=
                currentCenterRoots.end());
        const EndpointEvaluation* selected = nullptr;
        if (departure.receipt.accepted && arrival.receipt.accepted) {
            selected = ReceiptMargin(departure.receipt) >=
                           ReceiptMargin(arrival.receipt)
                ? &departure
                : &arrival;
        } else if (departure.receipt.accepted) {
            selected = &departure;
        } else if (arrival.receipt.accepted) {
            selected = &arrival;
        } else if (departurePhase && !arrivalPhase) {
            selected = &departure;
        } else if (arrivalPhase && !departurePhase) {
            selected = &arrival;
        } else {
            selected = ReceiptMargin(departure.receipt) >=
                           ReceiptMargin(arrival.receipt)
                ? &departure
                : &arrival;
        }

        const auto relation = selected->departure
            ? DisplayRelation::CurrentPolygon
            : DisplayRelation::ArrivalPrep;
        station.polygonKey = selected->departure ? input.currentPolygonKey
                                                 : input.arrivalPolygonKey;
        auto completion = MakeCompletion(
            input,
            candidate,
            station,
            relation,
            selected->receipt,
            std::string(selected->departure ? "departure" : "arrival") +
                "-terminal-evidence");
        if (selected->receipt.accepted) {
            if (selected->departure) {
                AppendUnique(
                    station,
                    completion,
                    &output.departureBoard,
                    &departureKeys);
            } else {
                AppendUnique(
                    station,
                    completion,
                    &output.arrivalBoard,
                    &arrivalKeys);
            }
        }
        output.completions.push_back(std::move(completion));
    }
    return output;
}

}  // namespace xvatsim::brain
