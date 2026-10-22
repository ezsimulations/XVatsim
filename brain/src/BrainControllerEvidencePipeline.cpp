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
    std::string provenance = {},
    std::string deduplicationKey = {},
    bool contributesToScore = true) {
    BrainControllerEvidenceVote vote;
    vote.source = std::move(source);
    vote.score = score > 0 ? 1 : score < 0 ? -1 : 0;
    vote.reason = std::move(reason);
    vote.provenance = std::move(provenance);
    vote.deduplicationKey = std::move(deduplicationKey);
    vote.contributesToScore = contributesToScore;
    return vote;
}

struct OperationalEligibility {
    bool eligible = true;
    bool atis = false;
    std::string reason;
    std::vector<BrainControllerEvidenceVote> facts;
};

OperationalEligibility EvaluateOperationalEligibility(
    const RadioReachableControllerCandidate& candidate) {
    OperationalEligibility result;
    std::string suffix;
    const auto hasSuffix = SplitCallsign(candidate.callsign, nullptr, &suffix);
    result.atis = candidate.atis || (hasSuffix && suffix == "ATIS");

    const auto facilityKnown = candidate.vatsimFacilityProvided ||
                               candidate.vatsimFacility != 0;
    const auto facilityOperational =
        facilityKnown && candidate.vatsimFacility != 0;
    result.facts.push_back(Vote(
        "operational-source-facility",
        !facilityKnown ? 0 : facilityOperational ? 1 : -1,
        !facilityKnown ? "numeric-facility-unknown"
                       : facilityOperational ? "numeric-facility-operational"
                                             : "numeric-facility-observer",
        "facility=" + std::to_string(candidate.vatsimFacility),
        "operational-source-facility",
        false));

    const auto sourcePrimary = candidate.sourcePrimaryFrequency.empty() &&
            !candidate.sourcePrimaryFrequencyProvided
        ? candidate.frequency
        : candidate.sourcePrimaryFrequency;
    const auto primaryDigits = NormalizeFrequency(sourcePrimary);
    const auto primaryGuard = GuardFrequency(sourcePrimary);
    const auto primaryKnown = primaryDigits.size() == 6;
    result.facts.push_back(Vote(
        "operational-source-primary",
        primaryGuard ? -1 : primaryKnown ? 1 : 0,
        primaryGuard ? "explicit-inactive-or-guard-primary"
                     : primaryKnown ? "source-primary-present"
                                    : "source-primary-unknown",
        DisplayFrequency(sourcePrimary),
        "operational-source-primary",
        false));

    const auto observerSuffix = hasSuffix && suffix == "OBS";
    const auto supervisorSuffix = hasSuffix && suffix == "SUP";
    result.facts.push_back(Vote(
        "operational-source-role",
        observerSuffix || supervisorSuffix ? -1 : 1,
        observerSuffix ? "observer-callsign"
                       : supervisorSuffix ? "supervisor-callsign"
                                          : "controller-role-not-observer",
        candidate.callsign,
        "operational-source-role",
        false));

    if (facilityKnown && !facilityOperational) {
        result.eligible = false;
        result.reason = "brain-ineligible-source-facility-observer";
    } else if (observerSuffix) {
        result.eligible = false;
        result.reason = "brain-ineligible-source-observer";
    } else if (supervisorSuffix) {
        result.eligible = false;
        result.reason = "brain-ineligible-source-supervisor";
    } else if (primaryGuard) {
        result.eligible = false;
        result.reason = "brain-ineligible-source-primary-guard";
    }
    return result;
}

std::optional<BrainControllerEvidenceVote> XPilot4CorrelationVote(
    const BrainControllerRelevanceWorkerInput& input,
    const RadioReachableControllerCandidate& candidate) {
    const auto& snapshot = input.xpilot4Controllers;
    if (snapshot == nullptr || !snapshot->sourceAvailable ||
        !snapshot->networkConnected || !snapshot->complete) {
        return std::nullopt;
    }

    const auto candidateCallsign = Upper(candidate.callsign);
    const auto candidateFrequency = NormalizeFrequency(candidate.frequency);
    const BrainXPilot4ControllerFact* callsignMatch = nullptr;
    const BrainXPilot4ControllerFact* exactMatch = nullptr;
    for (const auto& controller : snapshot->controllers) {
        if (Upper(controller.callsign) != candidateCallsign) continue;
        if (callsignMatch == nullptr) callsignMatch = &controller;
        if (!controller.hasFrequency || candidateFrequency.empty()) continue;
        const auto controllerKhz = controller.frequencyHz >= 1'000'000
            ? controller.frequencyHz / 1'000
            : controller.frequencyHz;
        if (std::to_string(controllerKhz) == candidateFrequency) {
            exactMatch = &controller;
            break;
        }
    }

    const auto* observed = exactMatch != nullptr ? exactMatch : callsignMatch;
    std::ostringstream provenance;
    provenance << "process=" << snapshot->processEpoch
               << ",connection=" << snapshot->connectionEpoch
               << ",snapshot=" << snapshot->generation;
    if (observed != nullptr) {
        provenance << ",callsign=" << observed->callsign;
        if (observed->hasFrequency) {
            provenance << ",frequencyHz=" << observed->frequencyHz;
        } else {
            provenance << ",frequencyHz=absent";
        }
    }
    return Vote(
        "xpilot-fsd-correlation",
        0,
        exactMatch != nullptr
            ? "correlated-controller-confirmed"
            : callsignMatch != nullptr
                ? "correlated-controller-frequency-differs-or-absent"
                : "controller-not-observed-in-complete-xpilot4-snapshot",
        provenance.str());
}

void AppendXPilot4CorrelationVote(
    const BrainControllerRelevanceWorkerInput& input,
    const RadioReachableControllerCandidate& candidate,
    std::vector<BrainControllerEvidenceVote>* votes) {
    if (votes == nullptr) return;
    auto correlation = XPilot4CorrelationVote(input, candidate);
    if (correlation.has_value()) {
        votes->push_back(std::move(*correlation));
    }
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
    // Check every canonical token before considering resemblance. For US
    // airports, for example, SDF is an exact token for KSDF even though it is
    // also one edit away from the four-letter token.
    for (const auto& token : AirportTokens(airport)) {
        if (prefix == token ||
            (prefix.size() > token.size() &&
             prefix.compare(0, token.size(), token) == 0 &&
             (prefix[token.size()] == '-' || prefix[token.size()] == '_'))) {
            return Vote(
                "vatsim-parse",
                1,
                "endpoint-callsign-match",
                prefix + "=" + token,
                "vatsim-endpoint-identity");
        }
    }
    for (const auto& token : AirportTokens(airport)) {
        if (token.size() >= 2 && prefix.size() >= 2 &&
            EditDistance(prefix, token) <= 1) {
            return Vote(
                "vatsim-parse",
                0,
                "endpoint-callsign-fuzzy-clue-diagnostic-only",
                prefix + "~" + token,
                "vatsim-fuzzy-callsign-clue",
                false);
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

bool CallsignHasExactEndpointPrefix(
    const std::string& callsign,
    const std::string& airport) {
    std::string prefix;
    if (!SplitCallsign(callsign, &prefix, nullptr)) return false;
    for (const auto& token : AirportTokens(airport)) {
        if (prefix == token ||
            (prefix.size() > token.size() &&
             prefix.compare(0, token.size(), token) == 0 &&
             (prefix[token.size()] == '-' || prefix[token.size()] == '_'))) {
            return true;
        }
    }
    return false;
}

BrainControllerEvidenceVote VatsimRosterVote(
    const RadioReachableControllerCandidate& candidate,
    const std::string& airport,
    bool claimScopedNeutrality) {
    std::ostringstream provenance;
    provenance << "declared=" << candidate.sourceRosterDeclaredRecords
               << ",retained=" << candidate.sourceRosterRetainedRecords
               << ",rejected=" << candidate.sourceRosterRejectedRecords
               << ",truncated=" << (candidate.sourceRosterTruncated ? 1 : 0);
    if (!candidate.sourceRosterAvailable || candidate.sourceRosterStale ||
        candidate.sourceRoster == nullptr) {
        return Vote(
            "vatsim-roster", 0, "controller-roster-unavailable-or-stale",
            provenance.str());
    }
    if (!candidate.sourceRosterComplete || candidate.sourceRosterTruncated) {
        return Vote(
            "vatsim-roster", 0, "controller-roster-incomplete",
            provenance.str());
    }
    if (CallsignHasExactEndpointPrefix(candidate.callsign, airport)) {
        return Vote(
            "vatsim-roster", 1, "candidate-exact-endpoint-present",
            candidate.callsign + "@" + DisplayFrequency(
                candidate.sourcePrimaryFrequency),
            "vatsim-endpoint-identity");
    }

    bool anotherDirectEndpoint = false;
    for (const auto& controller : *candidate.sourceRoster) {
        std::string suffix;
        if (controller.facility == 0 ||
            GuardFrequency(controller.frequency) ||
            !SplitCallsign(controller.callsign, nullptr, &suffix) ||
            (suffix != "APP" && suffix != "DEP") ||
            !CallsignHasExactEndpointPrefix(controller.callsign, airport)) {
            continue;
        }
        anotherDirectEndpoint = true;
        break;
    }
    return anotherDirectEndpoint
        ? Vote(
              "vatsim-roster", 0,
              "different-direct-endpoint-controller-present",
              provenance.str())
        : Vote(
              "vatsim-roster", claimScopedNeutrality ? 0 : -1,
              "no-direct-endpoint-controller",
              provenance.str(),
              "vatsim-roster-endpoint-absence");
}

BrainControllerEvidenceVote VatsimFeedHealthFact(
    const RadioReachableControllerCandidate& candidate) {
    const auto complete = candidate.sourceRosterAvailable &&
        !candidate.sourceRosterStale && candidate.sourceRosterComplete &&
        !candidate.sourceRosterTruncated;
    std::ostringstream provenance;
    provenance << "declared=" << candidate.sourceRosterDeclaredRecords
               << ",retained=" << candidate.sourceRosterRetainedRecords
               << ",rejected=" << candidate.sourceRosterRejectedRecords;
    return Vote(
        "vatsim-feed-health",
        complete ? 1 : 0,
        complete ? "fresh-complete-controller-roster"
                 : "controller-roster-health-unknown",
        provenance.str(),
        "vatsim-feed-health",
        false);
}

std::size_t SharedPrefix(const std::string& left, const std::string& right) {
    const auto count = std::min(left.size(), right.size());
    std::size_t shared = 0;
    while (shared < count && left[shared] == right[shared]) ++shared;
    return shared;
}

bool OneCharacterPrefixVariant(
    const std::string& left,
    const std::string& right) {
    if (left.size() < 2 || right.size() < 2 ||
        (left.size() + 1 != right.size() &&
         right.size() + 1 != left.size())) {
        return false;
    }
    const auto& shorter = left.size() < right.size() ? left : right;
    const auto& longer = left.size() < right.size() ? right : left;
    return longer.compare(0, shorter.size(), shorter) == 0;
}

struct VatspyResult {
    BrainControllerEvidenceVote vote;
    std::optional<BrainControllerEvidenceVote> callsignClue;
    bool endpointPositive = false;
};

VatspyResult VatspyVote(
    const BrainControllerRelevanceWorkerInput& input,
    const RadioReachableControllerCandidate& candidate,
    const std::string& airport,
    bool claimScopedNeutrality) {
    VatspyResult result;
    std::string prefix;
    if (!SplitCallsign(candidate.callsign, &prefix, nullptr)) {
        result.vote = Vote(
            "vatspy-parse",
            claimScopedNeutrality ? 0 : -1,
            "callsign-prefix-unavailable");
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
                    alias->callsignPrefix,
                "vatspy-endpoint-identity");
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
        const auto expected = Upper(best->callsignPrefix);
        if (OneCharacterPrefixVariant(prefix, expected)) {
            result.endpointPositive = true;
            result.vote = Vote(
                "vatspy-parse",
                1,
                "near-exact-airport-alias-prefix-match",
                best->source + ":" + prefix + "~" + best->callsignPrefix,
                "vatspy-endpoint-identity");
            return result;
        }
        result.vote = Vote(
            "vatspy-parse",
            claimScopedNeutrality ? 0 : -1,
            "airport-alias-mismatch",
            best->source + ":" + prefix + "!=" + best->callsignPrefix,
            "vatspy-endpoint-identity");
        result.callsignClue = Vote(
            "vatspy-callsign-clue",
            0,
            "airport-alias-fuzzy-clue-diagnostic-only",
            best->source + ":" + prefix + "~" + best->callsignPrefix,
            "vatspy-fuzzy-callsign-clue",
            false);
        return result;
    }

    result.vote = Vote(
        "vatspy-parse",
        claimScopedNeutrality ? 0 : -1,
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
        const auto operational = EvaluateOperationalEligibility(candidate);
        if (!operational.eligible || operational.atis ||
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

BrainControllerEvidenceVote SourceAuthorityVote(
    const BrainControllerRelevanceWorkerInput& input,
    const RadioReachableControllerCandidate& candidate) {
    std::string sourceProof;
    if (SourceAuthorityMatch(input, candidate, &sourceProof)) {
        return Vote(
            "source-authority", 1, "source-authority-match", sourceProof);
    }
    if (!input.authorityRelevance || !input.authorityRelevance->available ||
        input.authorityRelevance->stale) {
        return Vote(
            "source-authority", 0, "source-authority-unavailable");
    }
    return Vote(
        "source-authority", 0, "no-source-authority-match");
}

BrainControllerEvidenceVote TerminalOwnerVote(
    const RadioReachableControllerCandidate& candidate,
    const BrainTerminalAuthorityWorkerOutput& authority,
    StationRole role,
    bool claimScopedNeutrality) {
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
        "terminal-source",
        claimScopedNeutrality ? 0 : -1,
        "terminal-owner-mismatch",
        owner);
}

BrainControllerEvidenceVote TerminalPolygonVote(
    const RadioReachableControllerCandidate& candidate,
    const BrainTerminalAuthorityWorkerOutput& authority,
    StationRole role,
    bool enabled) {
    if (!enabled) {
        return Vote("endpoint-polygon", 0, "endpoint-polygon-gate-disabled");
    }
    if (!IsAppDep(role)) {
        return Vote("endpoint-polygon", 0, "not-appdep-polygon-scope");
    }
    if (!authority.available || !authority.resolved || authority.stale) {
        return Vote("endpoint-polygon", 0, "endpoint-polygon-unavailable");
    }

    std::string callsignPrefix;
    std::string callsignSuffix;
    if (!SplitCallsign(candidate.callsign, &callsignPrefix, &callsignSuffix)) {
        return Vote("endpoint-polygon", 0, "controller-position-unparseable");
    }
    callsignPrefix = Upper(callsignPrefix);
    callsignSuffix = Upper(callsignSuffix);

    for (const auto& polygon : authority.polygons) {
        const auto sourceSuffix = Upper(polygon.serviceSuffix);
        if (!sourceSuffix.empty() && sourceSuffix != callsignSuffix) {
            continue;
        }
        const auto prefixMatch = std::any_of(
            polygon.controllerPrefixes.begin(),
            polygon.controllerPrefixes.end(),
            [&](const auto& sourcePrefix) {
                const auto expected = Upper(sourcePrefix);
                return callsignPrefix == expected ||
                       (callsignPrefix.size() > expected.size() &&
                        callsignPrefix.compare(0, expected.size(), expected) == 0 &&
                        (callsignPrefix[expected.size()] == '_' ||
                         callsignPrefix[expected.size()] == '-'));
            });
        if (!prefixMatch) {
            continue;
        }
        const auto owner = TerminalOwnerToken(candidate.callsign);
        const auto directOwner = std::any_of(
            authority.ownerTokens.begin(),
            authority.ownerTokens.end(),
            [&](const auto& expected) { return Upper(expected) == owner; });
        if (polygon.containsAirport && directOwner) {
            return Vote(
                "endpoint-polygon",
                1,
                "exact-source-direct-endpoint-polygon",
                authority.airportIcao + ":" + polygon.polygonKey,
                "simaware-endpoint-polygon");
        }
        const auto distinctEndpointPolygon = std::none_of(
            authority.polygonKeys.begin(),
            authority.polygonKeys.end(),
            [&](const auto& anchorKey) {
                return Upper(anchorKey) == Upper(polygon.polygonKey);
            });
        if (polygon.intersectsEndpointAnchor && distinctEndpointPolygon) {
            return Vote(
                "endpoint-polygon",
                1,
                "exact-source-nearby-endpoint-polygon",
                authority.airportIcao + ":" + polygon.polygonKey,
                "simaware-endpoint-polygon");
        }
    }
    return Vote(
        "endpoint-polygon",
        0,
        authority.polygons.empty()
            ? "endpoint-polygon-package-empty"
            : "no-exact-source-position-match");
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
                0,
                "real-world-frequency-match-diagnostic-only",
                record.airportIcao + ":" + record.frequency,
                "published-frequency",
                false);
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

    auto operational = EvaluateOperationalEligibility(candidate);
    const auto claimScopedNeutrality = input.terminalRelevanceV2Enabled;
    const auto vatspy = VatspyVote(
        input, candidate, evaluation.airport, claimScopedNeutrality);
    auto roster = VatsimRosterVote(
        candidate, evaluation.airport, claimScopedNeutrality);
    auto feedHealth = VatsimFeedHealthFact(candidate);
    auto distance = DistanceVote(input, candidate, departure);
    auto vnas = VnasVote(input, candidate, evaluation.airport);
    auto sourceAuthority = SourceAuthorityVote(input, candidate);
    auto terminalOwner = TerminalOwnerVote(
        candidate,
        departure ? input.departureTerminalAuthority
                  : input.arrivalTerminalAuthority,
        role,
        claimScopedNeutrality);
    auto terminalPolygon = TerminalPolygonVote(
        candidate,
        departure ? input.departureTerminalAuthority
                  : input.arrivalTerminalAuthority,
        role,
        input.terminalRelevanceV2Enabled);
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
    decision.actionable = true;
    decision.atis = operational.atis;
    const auto facilityKnown = candidate.vatsimFacilityProvided ||
                               candidate.vatsimFacility != 0;
    decision.supportedFacility = IsTerminalRole(role) &&
        (!facilityKnown || candidate.vatsimFacility != 0);
    decision.guardFrequency = false;
    decision.phaseApplicable = phaseApplicable;
    decision.ineligibleReason = operational.reason;
    decision.relationAvailable =
        vatsim.score > 0 || vatspy.endpointPositive || distance.score > 0 ||
        vnas.vote.score > 0 ||
        sourceAuthority.score > 0 || terminalOwner.score > 0 ||
        terminalPolygon.score > 0 ||
        published.score > 0 || routeContext.score > 0;
    decision.ownershipApplicable =
        !input.vnasSectorPrecedenceEnabled || vnas.ownershipApplicable;
    decision.supportedTiesDisplay =
        input.terminalRelevanceV2Enabled || routeContextMatch;
    decision.votes = std::move(operational.facts);
    decision.votes.insert(
        decision.votes.end(), {
        std::move(feedHealth),
        std::move(vatsim),
        std::move(roster),
        vatspy.vote,
        std::move(vnas.vote),
        std::move(distance),
        std::move(sourceAuthority),
        std::move(terminalOwner),
        std::move(terminalPolygon),
        std::move(published),
        std::move(routeContext),
    });
    if (vatspy.callsignClue.has_value()) {
        decision.votes.push_back(std::move(*vatspy.callsignClue));
    }
    AppendXPilot4CorrelationVote(input, candidate, &decision.votes);
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
    auto operational = EvaluateOperationalEligibility(candidate);
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
    decision.actionable = true;
    decision.atis = operational.atis;
    const auto facilityKnown = candidate.vatsimFacilityProvided ||
                               candidate.vatsimFacility != 0;
    decision.supportedFacility =
        !facilityKnown || candidate.vatsimFacility != 0;
    decision.guardFrequency = false;
    decision.phaseApplicable = phaseApplicable;
    decision.ineligibleReason = operational.reason;
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
    decision.votes.insert(
        decision.votes.begin(),
        operational.facts.begin(),
        operational.facts.end());
    AppendXPilot4CorrelationVote(input, candidate, &decision.votes);
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
        station.online = false;
        station.offline = false;
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
                station.online = true;
                station.offline = false;
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
            auto operational = EvaluateOperationalEligibility(candidate);
            BrainControllerDecisionInput decision;
            decision.actionable = true;
            decision.atis = operational.atis ||
                            candidate.group == RadioReachableFacilityGroup::Atis;
            decision.supportedFacility = false;
            decision.guardFrequency = false;
            decision.phaseApplicable = false;
            decision.relationAvailable = false;
            decision.ineligibleReason = operational.reason;
            decision.votes = std::move(operational.facts);
            decision.votes.push_back(VatsimVote(candidate, role));
            AppendXPilot4CorrelationVote(input, candidate, &decision.votes);
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
            station.online = true;
            station.offline = false;
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
