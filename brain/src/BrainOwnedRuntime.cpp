#include "XVatsim/brain/BrainOwnedRuntime.h"

#include "XVatsim/brain/BrainOwnedWorkerTypes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace xvatsim::brain {
namespace {

constexpr char kUnicomFallbackFrequency[] = "122.800";
constexpr char kCom1StandbyDatarefName[] =
    "sim/cockpit2/radios/actuators/com1_standby_frequency_hz_833";

const char* WorkflowStageToken(WorkflowStage stage) {
    switch (stage) {
        case WorkflowStage::Departure:
            return "DEP";
        case WorkflowStage::Enroute:
            return "ENR";
        case WorkflowStage::Arrival:
            return "ARR";
        case WorkflowStage::None:
        default:
            return "NONE";
    }
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

std::string NormalizeIcao(std::string airportIcao) {
    std::string normalized;
    normalized.reserve(airportIcao.size());
    for (const auto character : airportIcao) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0) {
            continue;
        }

        normalized.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(character))));
    }
    return normalized;
}

void HashCombine(std::uint64_t* seed, std::uint64_t value) {
    if (seed == nullptr) {
        return;
    }
    *seed ^= value + 0x9e3779b97f4a7c15ULL + (*seed << 6) + (*seed >> 2);
}

void HashCombine(std::uint64_t* seed, const std::string& value) {
    HashCombine(seed, static_cast<std::uint64_t>(value.size()));
    for (const auto character : value) {
        HashCombine(seed, static_cast<std::uint64_t>(
                             static_cast<unsigned char>(character)));
    }
}

void HashCombine(std::uint64_t* seed, bool value) {
    HashCombine(seed, static_cast<std::uint64_t>(value ? 1 : 0));
}

std::string BuildTerminalAuthorityRequestKey(
    const BrainOwnedTerminalAuthorityRefreshInput& input) {
    const auto airportIcao = NormalizeIcao(input.airportIcao);
    if (!input.flightContextActive || airportIcao.empty() ||
        !input.hasAirportCoordinates) {
        return {};
    }

    std::ostringstream stream;
    stream << airportIcao << '|'
           << std::fixed << std::setprecision(5)
           << input.airportLatitudeDeg << '|'
           << input.airportLongitudeDeg;
    return stream.str();
}

std::uint64_t HashTerminalAuthorityFact(
    const BrainTerminalAuthorityWorkerOutput& fact) {
    std::uint64_t hash = 1469598103934665603ULL;
    HashCombine(&hash, fact.available);
    HashCombine(&hash, fact.pending);
    HashCombine(&hash, fact.resolved);
    HashCombine(&hash, fact.stale);
    HashCombine(&hash, NormalizeIcao(fact.airportIcao));
    HashCombine(&hash, fact.source);
    HashCombine(&hash, fact.sourceGeneration);
    for (const auto& ownerToken : fact.ownerTokens) {
        HashCombine(&hash, NormalizeCallsign(ownerToken));
    }
    for (const auto& polygonKey : fact.polygonKeys) {
        HashCombine(&hash, NormalizeCallsign(polygonKey));
    }
    return hash;
}

std::string BuildAirportFrequencyRequestKey(
    const BrainOwnedAirportFrequencyRefreshInput& input) {
    const auto departureIcao = NormalizeIcao(input.departureIcao);
    const auto arrivalIcao = NormalizeIcao(input.arrivalIcao);
    if (!input.flightContextActive ||
        (departureIcao.empty() && arrivalIcao.empty())) {
        return {};
    }
    return departureIcao + "|" + arrivalIcao;
}

std::string NormalizeFrequency(std::string frequency);

std::uint64_t HashAirportFrequencyFact(
    const BrainAirportFrequencyWorkerOutput& fact) {
    std::uint64_t hash = 1469598103934665603ULL;
    HashCombine(&hash, fact.available);
    HashCombine(&hash, fact.pending);
    HashCombine(&hash, fact.resolved);
    HashCombine(&hash, fact.stale);
    HashCombine(&hash, NormalizeIcao(fact.departureIcao));
    HashCombine(&hash, NormalizeIcao(fact.arrivalIcao));
    HashCombine(&hash, fact.source);
    HashCombine(&hash, fact.sourceGeneration);
    const auto hashRecord = [&](const BrainAirportFrequencyRecord& record) {
        HashCombine(&hash, static_cast<std::uint64_t>(record.endpoint));
        HashCombine(&hash, NormalizeIcao(record.airportIcao));
        HashCombine(&hash, static_cast<std::uint64_t>(record.role));
        HashCombine(&hash, NormalizeFrequency(record.frequency));
        HashCombine(&hash, NormalizeCallsign(record.frequencyUse));
        HashCombine(&hash, NormalizeCallsign(record.sectorization));
        HashCombine(&hash, NormalizeCallsign(record.facility));
        HashCombine(&hash, NormalizeCallsign(record.servicedFacility));
    };
    for (const auto& record : fact.departureFrequencies) {
        hashRecord(record);
    }
    for (const auto& record : fact.arrivalFrequencies) {
        hashRecord(record);
    }
    return hash;
}

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

std::string TrimWhitespace(std::string value) {
    value.erase(
        std::remove_if(
            value.begin(),
            value.end(),
            [](unsigned char c) { return std::isspace(c) != 0; }),
        value.end());
    return value;
}

std::string DiagnoseCom1StandbyWriterFrequency(
    const std::string& frequency,
    std::string* normalizedFrequency) {
    if (normalizedFrequency != nullptr) {
        *normalizedFrequency = NormalizeFrequency(frequency);
    }

    if (TrimWhitespace(frequency).empty()) {
        return "empty-frequency";
    }

    const auto normalized = NormalizeFrequency(frequency);
    if (normalized.empty()) {
        return "frequency-parse-failed";
    }

    int channelNumber = 0;
    try {
        channelNumber = std::stoi(normalized);
    } catch (...) {
        return "frequency-parse-failed";
    }

    if (channelNumber < 118000 || channelNumber > 136975) {
        return "frequency-out-of-range";
    }

    return {};
}

bool IsWriterInvalidFrequencyCode(const std::string& code) {
    return code == "empty-frequency" ||
           code == "invalid-frequency" ||
           code == "frequency-parse-failed" ||
           code == "frequency-out-of-range" ||
           code == "unsupported-frequency-channel";
}

std::string WriterFailureDomainForCode(const std::string& code) {
    if (code.empty() || code == "write-succeeded") {
        return {};
    }
    if (code == "no-target") {
        return "planner";
    }
    if (code == "no-write-requested") {
        return "planner";
    }
    if (code == "write-not-allowed" ||
        code == "invalid-target-com") {
        return "brain-side-effect";
    }
    if (IsWriterInvalidFrequencyCode(code)) {
        return "validation";
    }
    if (code == "com1-standby-dataref-missing" ||
        code == "com1-standby-dataref-not-writable") {
        return "dataref";
    }
    if (code == "sim-write-failed") {
        return "sim";
    }
    return "writer";
}

std::string NormalizeDirectCtafGateSource(std::string source) {
    source = NormalizeCallsign(std::move(source));
    if (source == "DEFAULT") {
        return "default";
    }
    if (source == "SETTINGS-STORE" || source == "SETTINGS_STORE" ||
        source == "SETTINGSSTORE") {
        return "settings-store";
    }
    if (source == "HARNESS") {
        return "harness";
    }
    if (source == "UNKNOWN") {
        return "unknown";
    }
    return source.empty() ? std::string("unknown") : std::string("unknown");
}

BrainOwnedStandbyAssistSettingsDiagnostics BuildStandbySettingsDiagnostics(
    const BrainOwnedStandbyAssistPlanInput& input) {
    BrainOwnedStandbyAssistSettingsDiagnostics diagnostics;
    diagnostics.standbyAssistEnabled = input.standbyAssistEnabled;
    diagnostics.directCtafStandbyAssistEnabled =
        input.directCtafStandbyAssistEnabled;
    diagnostics.directCtafGateSource =
        NormalizeDirectCtafGateSource(input.directCtafGateSource);
    diagnostics.directCtafGateEffective =
        input.standbyAssistEnabled &&
        input.directCtafStandbyAssistEnabled;
    return diagnostics;
}

BrainOwnedStandbyAssistWriterResult BuildBaseStandbyWriterResult(
    const BrainOwnedStandbyAssistSideEffectDecision& decision) {
    BrainOwnedStandbyAssistWriterResult result;
    result.writerInputFrequency = decision.targetFrequency;
    result.writerNormalizedFrequency =
        NormalizeFrequency(decision.targetFrequency);
    result.writerTargetCom =
        decision.writerTarget.empty() ? "none" : decision.writerTarget;
    result.writerDatarefName = kCom1StandbyDatarefName;
    result.writerWriteAttempted = decision.writeAttempted;
    result.writerWriteSucceeded = false;
    result.writerResultSource =
        decision.actualSelectedTargetSource.empty()
            ? std::string("none")
            : decision.actualSelectedTargetSource;
    result.writerResultDecisionId =
        decision.sideEffectDecisionId.empty()
            ? std::string("standby-writer-result:unknown")
            : "standby-writer-result:" + decision.sideEffectDecisionId;
    result.writerResultLinkedStandbyDecisionId =
        decision.standbyDecisionId;
    return result;
}

void ApplyStandbyWriterResultCodeDefaults(
    BrainOwnedStandbyAssistWriterResult* result) {
    if (result == nullptr) {
        return;
    }

    if (result->writerResultCode.empty()) {
        result->writerResultCode = "writer-result-unknown";
    }

    result->writerResultKnown =
        result->writerResultCode != "writer-result-unknown";
    result->writerFailureDomain =
        WriterFailureDomainForCode(result->writerResultCode);

    if (result->writerFailureReason.empty() &&
        result->writerResultCode != "write-succeeded") {
        result->writerFailureReason = result->writerResultCode;
    }

    if (result->writerResultCode == "write-succeeded") {
        result->writerResultKnown = true;
        result->writerFailureReason.clear();
        result->writerFailureDomain.clear();
        result->writerDatarefAvailable = true;
        result->writerDatarefWritable = true;
        result->writerValidationPassed = true;
        result->writerWriteAttempted = true;
        result->writerWriteSucceeded = true;
        result->writerWriteBlockedBeforeSimWrite = false;
        result->writerWriteFailedAtSimLayer = false;
        return;
    }

    if (IsWriterInvalidFrequencyCode(result->writerResultCode) ||
        result->writerResultCode == "invalid-target-com" ||
        result->writerResultCode == "write-not-allowed" ||
        result->writerResultCode == "no-target" ||
        result->writerResultCode == "no-write-requested") {
        result->writerValidationPassed =
            !IsWriterInvalidFrequencyCode(result->writerResultCode) &&
            result->writerResultCode != "invalid-target-com";
        result->writerWriteSucceeded = false;
        result->writerWriteBlockedBeforeSimWrite = true;
        result->writerWriteFailedAtSimLayer = false;
        return;
    }

    if (result->writerResultCode == "com1-standby-dataref-missing") {
        result->writerDatarefAvailable = false;
        result->writerDatarefWritable = false;
        result->writerValidationPassed = true;
        result->writerWriteSucceeded = false;
        result->writerWriteBlockedBeforeSimWrite = true;
        result->writerWriteFailedAtSimLayer = false;
        return;
    }

    if (result->writerResultCode == "com1-standby-dataref-not-writable") {
        result->writerDatarefAvailable = true;
        result->writerDatarefWritable = false;
        result->writerValidationPassed = true;
        result->writerWriteSucceeded = false;
        result->writerWriteBlockedBeforeSimWrite = true;
        result->writerWriteFailedAtSimLayer = false;
        return;
    }

    if (result->writerResultCode == "sim-write-failed") {
        result->writerDatarefAvailable = true;
        result->writerDatarefWritable = true;
        result->writerValidationPassed = true;
        result->writerWriteAttempted = true;
        result->writerWriteSucceeded = false;
        result->writerWriteBlockedBeforeSimWrite = false;
        result->writerWriteFailedAtSimLayer = true;
        return;
    }

    result->writerWriteSucceeded = false;
}

bool StationRequiresCompletion(const BoardStationSnapshot& station) {
    (void)station;
    return true;
}

bool IsCtafOrUnicom(const BoardStationSnapshot& station) {
    return station.role == StationRole::Ctaf ||
           station.role == StationRole::Unicom;
}

bool IsLiveRouteCenterStation(const FinalDisplayStationSnapshot& station) {
    return station.role == StationRole::Center &&
           !station.offline &&
           !station.frequency.empty();
}

bool HasLiveRouteCenters(const FinalDisplaySnapshot& board) {
    return std::any_of(
        board.stations.begin(),
        board.stations.end(),
        IsLiveRouteCenterStation);
}

bool IsBlockedControllerFrequency(const std::string& frequency) {
    const auto normalizedFrequency = NormalizeFrequency(frequency);
    return normalizedFrequency == "121500" || normalizedFrequency == "199998";
}

bool IsCom1TunedToFrequency(
    const RadioStateSnapshot& radios,
    const std::string& frequency) {
    const auto normalizedTarget = NormalizeFrequency(frequency);
    if (normalizedTarget.empty()) {
        return false;
    }

    return NormalizeFrequency(radios.com1ActiveFrequency) == normalizedTarget;
}

bool IsDepartureTerminalCandidate(
    const RadioReachableControllerCandidate& candidate) {
    return candidate.group == RadioReachableFacilityGroup::AppDep;
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

bool TerminalAuthorityFactMatchesCandidate(
    const BrainTerminalAuthorityWorkerOutput& fact,
    const RadioReachableControllerCandidate& candidate) {
    if (!fact.resolved || fact.stale || fact.ownerTokens.empty()) {
        return false;
    }

    const auto candidateOwner =
        TerminalServiceTokenFromControllerCallsign(candidate.callsign);
    if (candidateOwner.empty()) {
        return false;
    }

    return std::any_of(
        fact.ownerTokens.begin(),
        fact.ownerTokens.end(),
        [&](const auto& ownerToken) {
            return NormalizeCallsign(ownerToken) == candidateOwner;
        });
}

bool IsRouteCenterCandidate(
    const RadioReachableControllerCandidate& candidate) {
    return candidate.group == RadioReachableFacilityGroup::Center;
}

workflow::WorkflowSignals BuildBrainOwnedWorkflowSignals(
    const BrainOwnedWorkflowSelectionInput& input) {
    workflow::WorkflowSignals signals;

    for (const auto& candidate : input.radioSnapshot.candidates) {
        if (candidate.frequency.empty() ||
            IsBlockedControllerFrequency(candidate.frequency)) {
            continue;
        }

        if (IsRouteCenterCandidate(candidate) &&
            IsCom1TunedToFrequency(input.radios, candidate.frequency)) {
            signals.com1TunedLiveRouteCenter = true;
            continue;
        }

        if (!IsDepartureTerminalCandidate(candidate) ||
            !TerminalAuthorityFactMatchesCandidate(
                input.departureTerminalAuthority,
                candidate)) {
            continue;
        }

        signals.hasLiveDepartureTerminalController = true;
        if (IsCom1TunedToFrequency(input.radios, candidate.frequency)) {
            signals.com1TunedDepartureTerminalController = true;
        }
    }

    return signals;
}

bool IsStandbyEligibleRole(StationRole role) {
    switch (role) {
        case StationRole::Delivery:
        case StationRole::Ground:
        case StationRole::Tower:
        case StationRole::Departure:
        case StationRole::Approach:
        case StationRole::Center:
            return true;
        case StationRole::Atis:
        case StationRole::Ctaf:
        case StationRole::Unicom:
        case StationRole::Other:
        default:
            return false;
    }
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

std::string StandbyAssistWorkflowKey(
    WorkflowStage workflowStage,
    const std::string& planKey,
    const RadioStateSnapshot& radios,
    const FinalDisplayStationSnapshot& targetStation) {
    return planKey + "|" +
           std::to_string(static_cast<int>(workflowStage)) + "|" +
           NormalizeFrequency(radios.com1ActiveFrequency) + "|" +
           NormalizeCallsign(targetStation.callsign) + "|" +
           NormalizeFrequency(targetStation.frequency);
}

std::string StandbyWorkflowStageToken(WorkflowStage stage) {
    switch (stage) {
        case WorkflowStage::Departure:
            return "Departure";
        case WorkflowStage::Enroute:
            return "Enroute";
        case WorkflowStage::Arrival:
            return "Arrival";
        case WorkflowStage::None:
        default:
            return "None";
    }
}

std::string StandbyStationRoleToken(StationRole role) {
    switch (role) {
        case StationRole::Delivery:
            return "DEL";
        case StationRole::Ground:
            return "GND";
        case StationRole::Tower:
            return "TWR";
        case StationRole::Departure:
            return "DEP";
        case StationRole::Approach:
            return "APP";
        case StationRole::Center:
            return "CTR";
        case StationRole::Atis:
            return "ATIS";
        case StationRole::Ctaf:
            return "CTAF";
        case StationRole::Unicom:
            return "UNICOM";
        case StationRole::Other:
        default:
            return "OTHER";
    }
}

StationRole StandbyRoleFromToken(const std::string& role) {
    const auto normalized = NormalizeCallsign(role);
    if (normalized == "DEL" || normalized == "DELIVERY") {
        return StationRole::Delivery;
    }
    if (normalized == "GND" || normalized == "GROUND") {
        return StationRole::Ground;
    }
    if (normalized == "TWR" || normalized == "TOWER") {
        return StationRole::Tower;
    }
    if (normalized == "DEP" || normalized == "DEPARTURE") {
        return StationRole::Departure;
    }
    if (normalized == "APP" || normalized == "APPROACH") {
        return StationRole::Approach;
    }
    if (normalized == "CTR" || normalized == "CENTER") {
        return StationRole::Center;
    }
    if (normalized == "ATIS") {
        return StationRole::Atis;
    }
    if (normalized == "CTAF") {
        return StationRole::Ctaf;
    }
    if (normalized == "UNICOM") {
        return StationRole::Unicom;
    }
    return StationRole::Other;
}

std::string StandbyDisplayRelationToken(DisplayRelation relation) {
    switch (relation) {
        case DisplayRelation::CurrentPolygon:
            return "CURRENT_POLYGON";
        case DisplayRelation::NextPolygon:
            return "NEXT_POLYGON";
        case DisplayRelation::ArrivalPrep:
            return "ARRIVAL_PREP";
        case DisplayRelation::Filtered:
            return "FILTERED";
        case DisplayRelation::Hidden:
            return "HIDDEN";
        case DisplayRelation::Unknown:
        default:
            return "UNKNOWN";
    }
}

bool IsStandbyWorkflowStageSupported(WorkflowStage stage) {
    return stage == WorkflowStage::Departure ||
           stage == WorkflowStage::Arrival ||
           stage == WorkflowStage::Enroute;
}

bool FrequencyMatches(
    const std::string& lhs,
    const std::string& rhs) {
    const auto normalizedLhs = NormalizeFrequency(lhs);
    return !normalizedLhs.empty() &&
           normalizedLhs == NormalizeFrequency(rhs);
}

bool IsCom1ActiveFrequency(
    const std::string& frequency,
    const RadioStateSnapshot& radios) {
    return FrequencyMatches(frequency, radios.com1ActiveFrequency);
}

bool IsCom2ActiveFrequency(
    const std::string& frequency,
    const RadioStateSnapshot& radios) {
    return FrequencyMatches(frequency, radios.com2ActiveFrequency);
}

bool IsCom1StandbyFrequency(
    const std::string& frequency,
    const RadioStateSnapshot& radios) {
    return FrequencyMatches(frequency, radios.com1StandbyFrequency);
}

std::string StandbySubjectKey(
    const std::string& sourceDomain,
    const std::string& role,
    const std::string& callsign,
    const std::string& frequency,
    const std::string& endpoint,
    const std::string& airportIcao) {
    std::ostringstream stream;
    stream << sourceDomain << "|"
           << (endpoint.empty() ? "none" : endpoint) << "|"
           << (airportIcao.empty() ? "none" : NormalizeIcao(airportIcao))
           << "|" << role << "|"
           << (callsign.empty() ? "none" : NormalizeCallsign(callsign))
           << "|"
           << (NormalizeFrequency(frequency).empty()
                   ? "empty"
                   : NormalizeFrequency(frequency));
    return stream.str();
}

std::string StandbyDecisionIdForIndex(
    std::size_t index,
    const std::string& subjectKey) {
    std::ostringstream stream;
    stream << "standby-decision:" << index << ":" << subjectKey;
    return stream.str();
}

bool AdvisoryCandidateVisibleInFinalBoard(
    const FinalDisplaySnapshot& board,
    const BrainOwnedStandbyAssistAdvisoryCandidate& candidate) {
    const auto role = StandbyRoleFromToken(candidate.projectedRole);
    const auto normalizedFrequency =
        NormalizeFrequency(candidate.projectedFrequency);
    const auto normalizedAirport = NormalizeIcao(candidate.airportIcao);
    for (const auto& station : board.stations) {
        if (station.role != role) {
            continue;
        }
        if (!normalizedFrequency.empty() &&
            NormalizeFrequency(station.frequency) != normalizedFrequency) {
            continue;
        }
        if (!normalizedAirport.empty() &&
            NormalizeIcao(station.callsign) != normalizedAirport) {
            continue;
        }
        return true;
    }
    return false;
}

std::optional<std::size_t> FindAdvisoryCandidateFinalBoardIndex(
    const FinalDisplaySnapshot& board,
    const BrainOwnedStandbyAssistAdvisoryCandidate& candidate) {
    const auto role = StandbyRoleFromToken(candidate.projectedRole);
    const auto normalizedFrequency =
        NormalizeFrequency(candidate.projectedFrequency);
    const auto normalizedAirport = NormalizeIcao(candidate.airportIcao);
    for (std::size_t index = 0; index < board.stations.size(); ++index) {
        const auto& station = board.stations[index];
        if (station.role != role) {
            continue;
        }
        if (!normalizedFrequency.empty() &&
            NormalizeFrequency(station.frequency) != normalizedFrequency) {
            continue;
        }
        if (!normalizedAirport.empty() &&
            NormalizeIcao(station.callsign) != normalizedAirport) {
            continue;
        }
        return index;
    }
    return std::nullopt;
}

std::string StandbyAdvisoryCandidateType(
    const BrainOwnedStandbyAssistAdvisoryCandidate& candidate) {
    if (candidate.advisoryDecision == "defer-pending") {
        return "pending-lookup";
    }
    if (candidate.advisoryDecision == "lookup-failed") {
        return "failed-lookup";
    }
    if (candidate.fallbackUsed ||
        candidate.projectedRole == "UNICOM" ||
        candidate.advisoryDecision == "unicom-fallback-display") {
        return "unicom-fallback";
    }
    if (candidate.projectedRole == "CTAF" ||
        candidate.advisoryDecision == "ctaf-display" ||
        candidate.advisoryDecision == "hide-non-displayable") {
        return "direct-ctaf";
    }
    return "unknown";
}

std::string StandbyAdvisoryFrequencyResolutionState(
    const BrainOwnedStandbyAssistAdvisoryCandidate& candidate,
    const std::string& advisoryCandidateType) {
    if (advisoryCandidateType == "pending-lookup") {
        return "pending-lookup";
    }
    if (advisoryCandidateType == "failed-lookup") {
        return "failed-lookup";
    }
    if (NormalizeFrequency(candidate.projectedFrequency).empty() ||
        candidate.advisoryDecision == "hide-non-displayable") {
        return "empty-frequency";
    }
    if (advisoryCandidateType == "unicom-fallback") {
        return "resolved-unicom-fallback";
    }
    if (advisoryCandidateType == "direct-ctaf") {
        return "resolved-direct-ctaf";
    }
    return "unknown";
}

std::string StandbyAdvisoryProductGate(
    const std::string& advisoryCandidateType,
    const std::string& frequencyResolutionState) {
    if (advisoryCandidateType == "unicom-fallback") {
        return "product-decision-required";
    }
    if (frequencyResolutionState == "resolved-direct-ctaf") {
        return "direct-ctaf-preview-only";
    }
    if (frequencyResolutionState == "pending-lookup") {
        return "lookup-pending";
    }
    if (frequencyResolutionState == "failed-lookup") {
        return "lookup-failed";
    }
    if (frequencyResolutionState == "empty-frequency") {
        return "frequency-blocked";
    }
    return "unknown";
}

std::string StandbyAdvisoryWritePolicy(
    const std::string& advisoryCandidateType,
    const std::string& frequencyResolutionState) {
    if (advisoryCandidateType == "unicom-fallback") {
        return "product-gated-no-write";
    }
    if (frequencyResolutionState == "pending-lookup" ||
        frequencyResolutionState == "failed-lookup") {
        return "not-ready-no-write";
    }
    if (frequencyResolutionState == "empty-frequency") {
        return "blocked-no-write";
    }
    return "preview-only-no-write";
}

void ApplyAdvisoryPreviewRecommendation(
    const BrainOwnedStandbyAssistPlanInput& input,
    BrainOwnedStandbyRecommendationDecision* decision) {
    if (decision == nullptr) {
        return;
    }

    const auto stageSupported =
        IsStandbyWorkflowStageSupported(input.workflowStage);
    const auto hasPlannerContext =
        stageSupported && !input.planKey.empty() && input.radios.valid;
    const auto hasFrequency = !NormalizeFrequency(decision->frequency).empty();
    const auto guardFrequency = IsBlockedControllerFrequency(decision->frequency);
    const auto active =
        decision->alreadyCom1Active || decision->alreadyCom2Active;

    if (!stageSupported) {
        decision->previewRecommendation = "preview-stage-deferred";
        decision->previewSkipReason = "skip-stage-deferred";
        return;
    }
    if (!hasPlannerContext) {
        decision->previewRecommendation = "preview-needs-more-evidence";
        decision->previewSkipReason = "needs-more-evidence";
        return;
    }
    if (decision->advisoryCandidateType == "pending-lookup") {
        decision->previewRecommendation = "preview-not-ready";
        decision->previewSkipReason = "skip-pending-lookup";
        return;
    }
    if (decision->advisoryCandidateType == "failed-lookup") {
        decision->previewRecommendation = "preview-not-ready";
        decision->previewSkipReason = "skip-lookup-failed";
        return;
    }
    if (!hasFrequency ||
        decision->advisoryFrequencyResolutionState == "empty-frequency") {
        decision->previewRecommendation = "preview-blocked";
        decision->previewSkipReason = "skip-empty-frequency";
        return;
    }
    if (guardFrequency) {
        decision->previewRecommendation = "preview-blocked";
        decision->previewSkipReason = "skip-guard-frequency";
        return;
    }
    if (decision->advisoryCandidateType == "unicom-fallback") {
        decision->previewRecommendation = "preview-product-gated";
        decision->previewSkipReason = "product-decision-required";
        return;
    }
    if (decision->advisoryCandidateType != "direct-ctaf") {
        decision->previewRecommendation = "preview-not-ready";
        decision->previewSkipReason = "needs-more-evidence";
        return;
    }
    if (active) {
        decision->previewRecommendation = "preview-skip-active";
        decision->previewSkipReason = "skip-active";
        return;
    }
    if (decision->alreadyCom1Standby) {
        decision->previewRecommendation = "preview-skip-already-standby";
        decision->previewSkipReason = "skip-already-standby";
        return;
    }

    decision->previewEligible = true;
    decision->previewRecommendation = "preview-recommend-com1-standby";
    decision->previewSkipReason.clear();
}

void ApplyDirectCtafDryRunReadiness(
    const BrainOwnedStandbyAssistPlanInput& input,
    bool existingControllerTargetSelected,
    BrainOwnedStandbyRecommendationDecision* decision) {
    if (decision == nullptr) {
        return;
    }

    decision->dryRunTargetCom = "none";
    decision->dryRunPromotionClass = "unknown";

    if (decision->sourceDomain != "ctaf-unicom-advisory") {
        decision->dryRunLiveRecommendation = "not-dry-run-candidate";
        decision->dryRunSkipReason = "not-advisory";
        decision->dryRunSafetyGate = "not-advisory";
        decision->dryRunPromotionClass = "unknown";
        return;
    }

    if (decision->advisoryCandidateType == "unicom-fallback") {
        decision->dryRunLiveRecommendation = "dry-run-excluded-unicom";
        decision->dryRunSkipReason = "unicom-excluded";
        decision->dryRunSafetyGate = "unicom-excluded";
        decision->dryRunPromotionClass = "unicom-excluded";
        return;
    }

    decision->dryRunPromotionClass =
        decision->advisoryCandidateType == "direct-ctaf"
            ? "direct-ctaf-only"
            : "unknown";

    const auto stageSupported =
        IsStandbyWorkflowStageSupported(input.workflowStage);
    const auto hasPlannerContext =
        stageSupported && !input.planKey.empty() && input.radios.valid;
    const auto normalizedFrequency = NormalizeFrequency(decision->frequency);
    const auto hasFrequency = !normalizedFrequency.empty();

    if (decision->advisoryCandidateType == "direct-ctaf" && hasFrequency) {
        decision->dryRunTargetCom = "COM1_STANDBY";
        decision->dryRunTargetFrequency = decision->frequency;
    }

    if (!stageSupported) {
        decision->dryRunLiveRecommendation = "dry-run-stage-deferred";
        decision->dryRunSkipReason = "skip-stage-deferred";
        decision->dryRunSafetyGate = "stage-deferred";
        return;
    }
    if (!hasPlannerContext) {
        decision->dryRunLiveRecommendation = "dry-run-needs-more-evidence";
        decision->dryRunSkipReason = "needs-more-evidence";
        decision->dryRunSafetyGate = "needs-more-evidence";
        return;
    }
    if (decision->advisoryCandidateType == "pending-lookup") {
        decision->dryRunLiveRecommendation = "dry-run-not-ready";
        decision->dryRunSkipReason = "skip-pending-lookup";
        decision->dryRunSafetyGate = "frequency-state";
        decision->dryRunBlockedByFrequencyState = true;
        return;
    }
    if (decision->advisoryCandidateType == "failed-lookup") {
        decision->dryRunLiveRecommendation = "dry-run-not-ready";
        decision->dryRunSkipReason = "skip-lookup-failed";
        decision->dryRunSafetyGate = "frequency-state";
        decision->dryRunBlockedByFrequencyState = true;
        return;
    }
    if (decision->advisoryCandidateType != "direct-ctaf") {
        decision->dryRunLiveRecommendation = "dry-run-not-direct-ctaf";
        decision->dryRunSkipReason = "not-direct-ctaf";
        decision->dryRunSafetyGate = "not-direct-ctaf";
        return;
    }
    if (!hasFrequency ||
        decision->advisoryFrequencyResolutionState == "empty-frequency") {
        decision->dryRunLiveRecommendation =
            "dry-run-blocked-frequency-state";
        decision->dryRunSkipReason = "skip-empty-frequency";
        decision->dryRunSafetyGate = "frequency-state";
        decision->dryRunBlockedByFrequencyState = true;
        return;
    }
    if (IsBlockedControllerFrequency(decision->frequency)) {
        decision->dryRunLiveRecommendation =
            "dry-run-blocked-frequency-state";
        decision->dryRunSkipReason = "skip-guard-frequency";
        decision->dryRunSafetyGate = "frequency-state";
        decision->dryRunBlockedByFrequencyState = true;
        return;
    }
    if (decision->alreadyCom1Active || decision->alreadyCom2Active) {
        decision->dryRunLiveRecommendation =
            "dry-run-blocked-active-frequency";
        decision->dryRunSkipReason = "skip-active";
        decision->dryRunSafetyGate = "active-frequency";
        return;
    }
    if (existingControllerTargetSelected) {
        decision->dryRunLiveRecommendation =
            "dry-run-blocked-existing-controller-target";
        decision->dryRunSkipReason = "existing-controller-target";
        decision->dryRunSafetyGate = "existing-controller-target";
        decision->dryRunBlockedByExistingControllerTarget = true;
        decision->dryRunWouldDisplaceControllerTarget = false;
        return;
    }
    if (!input.standbyAssistEnabled) {
        decision->dryRunLiveRecommendation =
            "dry-run-blocked-standby-disabled";
        decision->dryRunSkipReason = "standby-assist-disabled";
        decision->dryRunSafetyGate = "standby-assist-disabled";
        decision->dryRunBlockedByStandbyDisabled = true;
        return;
    }
    if (decision->alreadyCom1Standby) {
        decision->dryRunLiveRecommendation =
            "dry-run-blocked-already-com1-standby";
        decision->dryRunSkipReason = "skip-already-standby";
        decision->dryRunSafetyGate = "already-com1-standby";
        decision->dryRunBlockedByAlreadyCom1Standby = true;
        return;
    }

    decision->dryRunLiveEligible = true;
    decision->dryRunLiveRecommendation = "dry-run-recommend-com1-standby";
    decision->dryRunSkipReason.clear();
    decision->dryRunSafetyGate = "pass";
    decision->dryRunWouldSelectTarget = true;
    decision->dryRunWouldDisplaceControllerTarget = false;
}

void ApplyDirectCtafLivePromotionLedger(
    const BrainOwnedStandbyAssistPlanInput& input,
    bool existingControllerTargetSelected,
    bool selectedDirectCtafTarget,
    BrainOwnedStandbyRecommendationDecision* decision) {
    if (decision == nullptr) {
        return;
    }

    decision->productGateEnabled = input.directCtafStandbyAssistEnabled;
    decision->noControllerTargetAvailable = !existingControllerTargetSelected;
    decision->controllerTargetPreserved = existingControllerTargetSelected;
    decision->actualSelectedTargetSource = "none";
    decision->featureGateRequired = "direct-ctaf-standby-assist";
    decision->featureGateSatisfied =
        input.standbyAssistEnabled &&
        input.directCtafStandbyAssistEnabled;

    if (decision->sourceDomain != "ctaf-unicom-advisory") {
        decision->featureGateRequired = "not-required";
        decision->featureGateSatisfied = true;
        decision->featureGateBlockedReason = "not-advisory";
        decision->livePromotionBlockedReason = "not-advisory";
        return;
    }
    if (selectedDirectCtafTarget) {
        decision->directCtafLivePromotionAllowed = true;
        decision->livePromotionReason = "promoted-direct-ctaf";
        decision->promotedFromDryRun = decision->dryRunLiveEligible;
        decision->actualSelectedTargetSource = "direct-ctaf-advisory";
        decision->actualSelectedTargetFrequency = decision->frequency;
        decision->actualWriteEligible = true;
        decision->targetCom = "COM1_STANDBY";
        decision->eligible = true;
        decision->liveWriteEligible = true;
        decision->skipReason.clear();
        decision->finalRecommendation = "recommend-com1-standby";
        decision->featureGateBlockedReason.clear();
        return;
    }

    if (decision->advisoryCandidateType != "direct-ctaf") {
        if (decision->advisoryCandidateType == "unicom-fallback") {
            decision->livePromotionBlockedReason = "unicom-excluded";
            decision->featureGateSatisfied = false;
            decision->featureGateBlockedReason = "unicom-excluded";
        } else if (decision->advisoryCandidateType == "pending-lookup") {
            decision->livePromotionBlockedReason = "skip-pending-lookup";
            decision->featureGateBlockedReason =
                decision->featureGateSatisfied
                    ? std::string()
                    : std::string("product-gate-disabled");
        } else if (decision->advisoryCandidateType == "failed-lookup") {
            decision->livePromotionBlockedReason = "skip-lookup-failed";
            decision->featureGateBlockedReason =
                decision->featureGateSatisfied
                    ? std::string()
                    : std::string("product-gate-disabled");
        } else {
            decision->livePromotionBlockedReason = "not-direct-ctaf";
            decision->featureGateSatisfied = false;
            decision->featureGateBlockedReason = "not-direct-ctaf";
        }
        return;
    }
    if (!input.standbyAssistEnabled) {
        decision->featureGateBlockedReason = "standby-assist-disabled";
    } else if (!input.directCtafStandbyAssistEnabled) {
        decision->featureGateBlockedReason = "product-gate-disabled";
    } else {
        decision->featureGateBlockedReason.clear();
    }
    if (!input.directCtafStandbyAssistEnabled) {
        decision->livePromotionBlockedReason = "product-gate-disabled";
        return;
    }
    if (existingControllerTargetSelected) {
        decision->livePromotionBlockedReason = "existing-controller-target";
        return;
    }
    if (!decision->dryRunLiveEligible) {
        decision->livePromotionBlockedReason =
            decision->dryRunSkipReason.empty()
                ? decision->dryRunSafetyGate
                : decision->dryRunSkipReason;
        return;
    }
    if (!decision->candidateVisibleInFinalBoard) {
        decision->livePromotionBlockedReason = "not-visible-in-final-board";
        return;
    }

    decision->livePromotionBlockedReason = "not-selected";
}

BrainOwnedStandbyRecommendationDecision BuildDisplayRowStandbyDecision(
    const BrainOwnedStandbyAssistPlanInput& input,
    const FinalDisplayStationSnapshot& station,
    std::size_t boardIndex,
    bool selectedTarget) {
    BrainOwnedStandbyRecommendationDecision decision;
    decision.sourceDomain = "display-row";
    decision.endpoint.clear();
    decision.airportIcao = input.board.airportIcao;
    decision.callsign = station.callsign;
    decision.role = StandbyStationRoleToken(station.role);
    decision.frequency = station.frequency;
    decision.workflowStage = StandbyWorkflowStageToken(input.workflowStage);
    decision.planKey = input.planKey;
    decision.boardIndex = static_cast<int>(boardIndex);
    decision.displayRelation =
        StandbyDisplayRelationToken(station.displayRelation);
    decision.candidateVisibleInFinalBoard = true;
    decision.alreadyCom1Active =
        IsCom1ActiveFrequency(station.frequency, input.radios);
    decision.alreadyCom2Active =
        IsCom2ActiveFrequency(station.frequency, input.radios);
    decision.alreadyCom1Standby =
        IsCom1StandbyFrequency(station.frequency, input.radios);
    decision.targetCom = selectedTarget ? "COM1_STANDBY" : "none";
    decision.previewRecommendation = "not-advisory-preview";
    decision.previewSkipReason = "not-advisory";
    decision.advisoryProductGate = "not-advisory";
    decision.advisoryWritePolicy = "controller-live-policy";
    decision.advisoryFrequencyResolutionState = "not-advisory";
    decision.advisoryCandidateType = "not-advisory";
    decision.dryRunLiveRecommendation = "not-dry-run-candidate";
    decision.dryRunSkipReason = "not-advisory";
    decision.dryRunSafetyGate = "not-advisory";
    decision.dryRunTargetCom = "none";
    decision.dryRunPromotionClass = "unknown";
    decision.productGateEnabled = input.directCtafStandbyAssistEnabled;
    decision.livePromotionBlockedReason = "not-advisory";
    decision.actualSelectedTargetSource =
        selectedTarget ? "controller-display-row" : "none";
    decision.actualSelectedTargetFrequency =
        selectedTarget ? station.frequency : "";
    decision.featureGateRequired = "not-required";
    decision.featureGateSatisfied = true;
    decision.featureGateBlockedReason = "not-advisory";
    decision.subjectKey =
        StandbySubjectKey(
            decision.sourceDomain,
            decision.role,
            decision.callsign,
            decision.frequency,
            decision.endpoint,
            decision.airportIcao);
    decision.standbyDecisionId =
        StandbyDecisionIdForIndex(boardIndex, decision.subjectKey);

    const auto stageSupported =
        IsStandbyWorkflowStageSupported(input.workflowStage);
    const auto hasPlannerContext =
        stageSupported && !input.planKey.empty() && input.radios.valid;
    const auto hasFrequency = !NormalizeFrequency(station.frequency).empty();
    const auto roleEligible = IsStandbyEligibleRole(station.role);
    const auto guardFrequency = IsBlockedControllerFrequency(station.frequency);
    const auto active =
        decision.alreadyCom1Active || decision.alreadyCom2Active;

    if (!stageSupported) {
        decision.skipReason = "skip-stage-deferred";
        decision.finalRecommendation = "skip-stage-deferred";
        decision.confidenceLevel = "medium";
        decision.positiveScore = 0.20;
        decision.negativeScore = 0.70;
        return decision;
    }
    if (!hasPlannerContext) {
        decision.skipReason = "needs-more-evidence";
        decision.finalRecommendation = "needs-more-evidence";
        decision.confidenceLevel = "low";
        decision.positiveScore = 0.10;
        decision.negativeScore = 0.35;
        return decision;
    }
    if (station.offline) {
        decision.skipReason = "needs-more-evidence";
        decision.finalRecommendation = "needs-more-evidence";
        decision.confidenceLevel = "low";
        decision.negativeScore = 0.50;
        return decision;
    }
    if (!hasFrequency) {
        decision.skipReason = "skip-empty-frequency";
        decision.finalRecommendation = "skip-empty-frequency";
        decision.confidenceLevel = "high";
        decision.negativeScore = 0.90;
        decision.hardBlock = true;
        decision.hardBlockReason = "empty-frequency";
        return decision;
    }
    if (guardFrequency) {
        decision.skipReason = "skip-guard-frequency";
        decision.finalRecommendation = "skip-guard-frequency";
        decision.confidenceLevel = "high";
        decision.negativeScore = 0.90;
        decision.hardBlock = true;
        decision.hardBlockReason = "guard-frequency";
        return decision;
    }
    if (!roleEligible) {
        decision.skipReason = "skip-role-not-eligible";
        decision.finalRecommendation = "skip-role-not-eligible";
        decision.confidenceLevel = "high";
        decision.negativeScore = 0.70;
        return decision;
    }

    decision.eligible = true;
    if (active) {
        decision.skipReason = "skip-active";
        decision.finalRecommendation = "skip-active";
        decision.confidenceLevel = "high";
        decision.positiveScore = 0.20;
        decision.negativeScore = 0.80;
        return decision;
    }
    if (selectedTarget && decision.alreadyCom1Standby) {
        decision.skipReason = "skip-already-standby";
        decision.finalRecommendation = "skip-already-standby";
        decision.confidenceLevel = "high";
        decision.positiveScore = 0.85;
        decision.negativeScore = 0.20;
        return decision;
    }
    if (selectedTarget) {
        decision.finalRecommendation = "recommend-com1-standby";
        decision.confidenceLevel = "high";
        decision.positiveScore = 0.90;
        decision.negativeScore = 0.05;
        decision.liveWriteEligible = true;
        decision.actualWriteEligible = true;
        return decision;
    }

    decision.skipReason = "no-target";
    decision.finalRecommendation = "no-target";
    decision.confidenceLevel = "medium";
    decision.positiveScore = 0.45;
    decision.negativeScore = 0.20;
    return decision;
}

BrainOwnedStandbyRecommendationDecision BuildAdvisoryStandbyDecision(
    const BrainOwnedStandbyAssistPlanInput& input,
    const BrainOwnedStandbyAssistAdvisoryCandidate& candidate,
    std::size_t decisionIndex,
    bool existingControllerTargetSelected,
    bool selectedDirectCtafTarget) {
    BrainOwnedStandbyRecommendationDecision decision;
    decision.sourceDomain = "ctaf-unicom-advisory";
    decision.sourceDecisionId = candidate.sourceDecisionId;
    decision.sourceEvidenceId = candidate.sourceEvidenceId;
    decision.endpoint = candidate.endpoint;
    decision.airportIcao = candidate.airportIcao;
    decision.callsign = candidate.airportIcao;
    decision.role = candidate.projectedRole.empty()
        ? "none"
        : candidate.projectedRole;
    decision.frequency = candidate.projectedFrequency;
    decision.workflowStage = StandbyWorkflowStageToken(input.workflowStage);
    decision.planKey = input.planKey;
    decision.boardIndex = -1;
    decision.displayRelation = "UNKNOWN";
    decision.candidateVisibleInFinalBoard =
        AdvisoryCandidateVisibleInFinalBoard(input.board, candidate);
    decision.acceptedByAdvisory = candidate.acceptedByAdvisory;
    decision.advisoryDecision = candidate.advisoryDecision;
    decision.sourceConfidence = candidate.sourceConfidence;
    decision.confidenceLevel = candidate.confidenceLevel.empty()
        ? "unknown"
        : candidate.confidenceLevel;
    decision.fallbackUsed = candidate.fallbackUsed;
    decision.positiveScore = candidate.positiveScore;
    decision.negativeScore = candidate.negativeScore;
    decision.hardBlock = candidate.hardBlock;
    decision.hardBlockReason = candidate.hardBlockReason;
    decision.alreadyCom1Active =
        IsCom1ActiveFrequency(candidate.projectedFrequency, input.radios);
    decision.alreadyCom2Active =
        IsCom2ActiveFrequency(candidate.projectedFrequency, input.radios);
    decision.alreadyCom1Standby =
        IsCom1StandbyFrequency(candidate.projectedFrequency, input.radios);
    decision.targetCom = "none";
    decision.eligible = false;
    decision.liveWriteEligible = false;
    decision.advisoryCandidateType =
        StandbyAdvisoryCandidateType(candidate);
    decision.advisoryFrequencyResolutionState =
        StandbyAdvisoryFrequencyResolutionState(
            candidate,
            decision.advisoryCandidateType);
    decision.advisoryProductGate =
        StandbyAdvisoryProductGate(
            decision.advisoryCandidateType,
            decision.advisoryFrequencyResolutionState);
    decision.advisoryWritePolicy =
        StandbyAdvisoryWritePolicy(
            decision.advisoryCandidateType,
            decision.advisoryFrequencyResolutionState);
    decision.subjectKey =
        StandbySubjectKey(
            decision.sourceDomain,
            decision.role,
            decision.callsign,
            decision.frequency,
            decision.endpoint,
            decision.airportIcao);
    decision.standbyDecisionId =
        StandbyDecisionIdForIndex(decisionIndex, decision.subjectKey);

    if (candidate.advisoryDecision == "defer-pending") {
        decision.skipReason = "skip-pending-lookup";
        decision.finalRecommendation = "skip-pending-lookup";
    } else if (candidate.advisoryDecision == "lookup-failed") {
        decision.skipReason = "skip-lookup-failed";
        decision.finalRecommendation = "skip-lookup-failed";
    } else if (NormalizeFrequency(candidate.projectedFrequency).empty() ||
               candidate.advisoryDecision == "hide-non-displayable") {
        decision.skipReason = "skip-empty-frequency";
        decision.finalRecommendation = "skip-empty-frequency";
        decision.hardBlock = true;
        if (decision.hardBlockReason.empty()) {
            decision.hardBlockReason = candidate.advisoryReason.empty()
                ? "empty-frequency"
                : candidate.advisoryReason;
        }
    } else if (IsBlockedControllerFrequency(candidate.projectedFrequency)) {
        decision.skipReason = "skip-guard-frequency";
        decision.finalRecommendation = "skip-guard-frequency";
        decision.hardBlock = true;
        if (decision.hardBlockReason.empty()) {
            decision.hardBlockReason = "guard-frequency";
        }
    } else {
        decision.skipReason = "skip-role-not-eligible";
        decision.finalRecommendation = "skip-role-not-eligible";
    }

    ApplyAdvisoryPreviewRecommendation(input, &decision);
    ApplyDirectCtafDryRunReadiness(
        input,
        existingControllerTargetSelected,
        &decision);
    ApplyDirectCtafLivePromotionLedger(
        input,
        existingControllerTargetSelected,
        selectedDirectCtafTarget,
        &decision);
    return decision;
}

BrainOwnedStandbyRecommendationSummary BuildStandbyRecommendationSummary(
    const std::vector<BrainOwnedStandbyRecommendationDecision>& decisions,
    bool hasTarget) {
    BrainOwnedStandbyRecommendationSummary summary;
    summary.standbyEvidenceCount = static_cast<int>(decisions.size());
    summary.selectedTargetCount = hasTarget ? 1 : 0;
    for (const auto& decision : decisions) {
        if (decision.sourceDomain == "ctaf-unicom-advisory") {
            ++summary.advisoryCandidateCount;
        } else {
            ++summary.standbyCandidateCount;
        }

        if (decision.finalRecommendation == "skip-empty-frequency") {
            ++summary.skippedEmptyFrequencyCount;
        } else if (decision.finalRecommendation == "skip-pending-lookup") {
            ++summary.skippedPendingLookupCount;
        } else if (decision.finalRecommendation == "skip-lookup-failed") {
            ++summary.skippedLookupFailedCount;
        } else if (decision.finalRecommendation == "skip-guard-frequency") {
            ++summary.skippedGuardFrequencyCount;
        } else if (decision.finalRecommendation == "skip-role-not-eligible") {
            ++summary.skippedRoleNotEligibleCount;
        } else if (decision.finalRecommendation == "skip-active") {
            ++summary.skippedAlreadyActiveCount;
        }
    }
    return summary;
}

std::vector<BrainOwnedStandbyRecommendationDecision> BuildStandbyDecisionLedger(
    const BrainOwnedStandbyAssistPlanInput& input,
    const BrainOwnedStandbyAssistPlanOutput& output) {
    std::vector<BrainOwnedStandbyRecommendationDecision> decisions;
    decisions.reserve(
        output.board.stations.size() +
        input.ctafUnicomAdvisoryCandidates.size());

    for (std::size_t index = 0; index < output.board.stations.size(); ++index) {
        const auto selectedControllerTarget =
            output.actualSelectedTargetSource == "controller-display-row" &&
            output.hasTarget && output.targetStationIndex == index;
        decisions.push_back(
            BuildDisplayRowStandbyDecision(
                input,
                output.board.stations[index],
                index,
                selectedControllerTarget));
    }

    auto decisionIndex = decisions.size();
    const auto existingControllerTargetSelected =
        output.actualSelectedTargetSource == "controller-display-row";
    for (const auto& advisoryCandidate :
         input.ctafUnicomAdvisoryCandidates) {
        const auto selectedDirectCtafTarget =
            output.actualSelectedTargetSource == "direct-ctaf-advisory" &&
            output.targetAdvisorySourceDecisionId ==
                advisoryCandidate.sourceDecisionId;
        decisions.push_back(
            BuildAdvisoryStandbyDecision(
                input,
                advisoryCandidate,
                decisionIndex,
                existingControllerTargetSelected,
                selectedDirectCtafTarget));
        ++decisionIndex;
    }

    return decisions;
}

bool TryPromoteDirectCtafStandbyTarget(
    const BrainOwnedStandbyAssistPlanInput& input,
    BrainOwnedStandbyAssistPlanOutput* output) {
    if (output == nullptr || output->hasTarget ||
        !input.directCtafStandbyAssistEnabled) {
        return false;
    }

    for (const auto& candidate : input.ctafUnicomAdvisoryCandidates) {
        const auto decision =
            BuildAdvisoryStandbyDecision(
                input,
                candidate,
                0,
                false,
                false);
        if (!decision.dryRunLiveEligible ||
            decision.advisoryCandidateType != "direct-ctaf" ||
            decision.advisoryFrequencyResolutionState !=
                "resolved-direct-ctaf") {
            continue;
        }

        const auto boardIndex =
            FindAdvisoryCandidateFinalBoardIndex(output->board, candidate);
        if (!boardIndex.has_value()) {
            continue;
        }

        output->hasTarget = true;
        output->targetStationIndex = *boardIndex;
        output->targetFrequency = candidate.projectedFrequency;
        output->targetAdvisorySourceDecisionId = candidate.sourceDecisionId;
        output->actualSelectedTargetSource = "direct-ctaf-advisory";
        output->actualSelectedTargetFrequency = candidate.projectedFrequency;
        const auto& targetStation = output->board.stations[*boardIndex];
        output->latchKey =
            StandbyAssistWorkflowKey(
                input.workflowStage,
                input.planKey,
                input.radios,
                targetStation);
        const auto normalizedTarget =
            NormalizeFrequency(candidate.projectedFrequency);
        output->targetAlreadyInCom1Standby =
            !normalizedTarget.empty() &&
            NormalizeFrequency(input.radios.com1StandbyFrequency) ==
                normalizedTarget;
        return true;
    }

    return false;
}

bool WouldApplyStandbyMarker(
    const BrainOwnedStandbyAssistPlanOutput& plan,
    bool standbyLoaded) {
    if (!plan.hasTarget || plan.targetStationIndex >= plan.board.stations.size()) {
        return false;
    }
    if (plan.workflowStage == WorkflowStage::Enroute) {
        return false;
    }
    const auto& targetStation = plan.board.stations[plan.targetStationIndex];
    return standbyLoaded && !targetStation.tuned;
}

std::string SelectStandbyNoTargetWriterResultCode(
    const BrainOwnedStandbyAssistPlanOutput& plan,
    std::string* failureReason) {
    const auto setReason = [&](const std::string& reason) {
        if (failureReason != nullptr) {
            *failureReason = reason;
        }
    };

    for (const auto& decision : plan.standbyDecisions) {
        if (decision.finalRecommendation == "skip-empty-frequency") {
            setReason("skip-empty-frequency");
            return "empty-frequency";
        }
    }
    for (const auto& decision : plan.standbyDecisions) {
        if (decision.featureGateBlockedReason == "standby-assist-disabled" ||
            decision.dryRunBlockedByStandbyDisabled) {
            setReason("standby-assist-disabled");
            return "no-write-requested";
        }
    }
    for (const auto& decision : plan.standbyDecisions) {
        if (decision.finalRecommendation == "skip-pending-lookup") {
            setReason("skip-pending-lookup");
            return "no-write-requested";
        }
        if (decision.finalRecommendation == "skip-lookup-failed") {
            setReason("skip-lookup-failed");
            return "no-write-requested";
        }
    }
    for (const auto& decision : plan.standbyDecisions) {
        if (decision.livePromotionBlockedReason == "product-gate-disabled") {
            setReason("product-gate-disabled");
            return "no-write-requested";
        }
        if (decision.livePromotionBlockedReason == "unicom-excluded" ||
            decision.advisoryCandidateType == "unicom-fallback") {
            setReason("unicom-excluded");
            return "no-write-requested";
        }
    }
    for (const auto& decision : plan.standbyDecisions) {
        if (decision.finalRecommendation == "skip-role-not-eligible" &&
            (decision.role == "CTAF" || decision.role == "UNICOM")) {
            setReason("skip-role-not-eligible");
            return "no-write-requested";
        }
    }

    setReason("no-target");
    return "no-target";
}

void AddStandbyWriterSummaryCounters(
    const BrainOwnedStandbyAssistWriterResult& result,
    BrainOwnedStandbyRecommendationSummary* summary) {
    if (summary == nullptr) {
        return;
    }

    ++summary->writerResultCount;
    if (result.writerResultCode == "write-succeeded") {
        ++summary->writerSuccessCount;
    } else if (result.writerWriteAttempted &&
               result.writerResultCode != "writer-result-unknown") {
        ++summary->writerFailureCount;
    }
    if (result.writerWriteBlockedBeforeSimWrite) {
        ++summary->writerBlockedBeforeWriteCount;
    }
    if (!result.writerResultKnown ||
        result.writerResultCode == "writer-result-unknown") {
        ++summary->writerUnknownResultCount;
    }
    if (result.writerResultCode == "com1-standby-dataref-missing") {
        ++summary->writerDatarefMissingCount;
    }
    if (result.writerResultCode == "com1-standby-dataref-not-writable") {
        ++summary->writerDatarefNotWritableCount;
    }
    if (IsWriterInvalidFrequencyCode(result.writerResultCode)) {
        ++summary->writerInvalidFrequencyCount;
    }
    if (result.writerResultCode == "no-target") {
        ++summary->writerNoTargetCount;
    }
    if (result.writerResultCode == "no-write-requested") {
        ++summary->writerNoWriteRequestedCount;
    }
    if (result.writerResultSource == "controller-display-row") {
        ++summary->writerControllerSourceCount;
    } else if (result.writerResultSource == "direct-ctaf-advisory") {
        ++summary->writerDirectCtafSourceCount;
    }
}

BrainOwnedStandbyAssistAdvisoryCandidate BuildStandbyAdvisoryCandidate(
    const BrainOwnedCtafUnicomAdvisoryPreviewDecision& decision) {
    BrainOwnedStandbyAssistAdvisoryCandidate candidate;
    candidate.sourceDecisionId = decision.advisoryDecisionId;
    candidate.sourceEvidenceId = decision.sourceEvidenceId;
    candidate.endpoint = decision.endpoint;
    candidate.airportIcao = decision.airportIcao;
    candidate.advisoryDecision = decision.decision;
    candidate.projectedRole = decision.projectedRole;
    candidate.projectedFrequency = decision.projectedFrequency;
    candidate.acceptedByAdvisory = decision.wouldEmitLiveRow;
    candidate.fallbackUsed = decision.fallbackUsed;
    candidate.sourceConfidence = decision.sourceConfidence;
    candidate.confidenceLevel = decision.confidenceLevel;
    candidate.positiveScore = decision.positiveScore;
    candidate.negativeScore = decision.negativeScore;
    candidate.hardBlock = decision.hardBlock;
    candidate.hardBlockReason = decision.hardBlock ? decision.reason : "";
    candidate.advisoryReason = decision.reason;
    return candidate;
}

std::string StationKey(const BoardStationSnapshot& station) {
    return std::to_string(static_cast<int>(station.role)) + "|" +
           NormalizeCallsign(station.callsign) + "|" +
           NormalizeFrequency(station.frequency);
}

void RemoveCtafStations(ModuleBoardSnapshot* board) {
    if (board == nullptr) {
        return;
    }

    board->stations.erase(
        std::remove_if(
            board->stations.begin(),
            board->stations.end(),
            IsCtafOrUnicom),
        board->stations.end());
    board->available = board->available || !board->stations.empty();
}

int CountCtafStations(const ModuleBoardSnapshot& board) {
    return static_cast<int>(std::count_if(
        board.stations.begin(),
        board.stations.end(),
        IsCtafOrUnicom));
}

bool AppendUniqueStation(
    const BoardStationSnapshot& station,
    ModuleBoardSnapshot* board,
    std::unordered_set<std::string>* keys) {
    if (board == nullptr || keys == nullptr) {
        return false;
    }
    if (!keys->insert(StationKey(station)).second) {
        return false;
    }

    board->stations.push_back(station);
    board->available = true;
    return true;
}

bool CompletionApprovesStation(
    const BrainOwnedCandidateCompletion& completion,
    const BoardStationSnapshot& station) {
    return completion.decision == BrainOwnedCandidateDecision::Accepted &&
           NormalizeCallsign(completion.callsign) ==
               NormalizeCallsign(station.callsign) &&
           NormalizeFrequency(completion.frequency) ==
               NormalizeFrequency(station.frequency);
}

bool CompletionDisplayedInFinalBoard(
    const FinalDisplaySnapshot& board,
    const BrainOwnedCandidateCompletion& completion) {
    return std::any_of(
        board.stations.begin(),
        board.stations.end(),
        [&](const auto& displayedStation) {
            return NormalizeCallsign(displayedStation.callsign) ==
                       NormalizeCallsign(completion.callsign) &&
                   NormalizeFrequency(displayedStation.frequency) ==
                        NormalizeFrequency(completion.frequency);
        });
}

bool IsDisplayIntentRelation(DisplayRelation relation) {
    return relation == DisplayRelation::CurrentPolygon ||
           relation == DisplayRelation::NextPolygon ||
           relation == DisplayRelation::ArrivalPrep;
}

std::vector<BrainDisplayRelationFact> BuildDisplayRelationFacts(
    const std::vector<BrainOwnedCandidateCompletion>& completions) {
    std::vector<BrainDisplayRelationFact> facts;
    facts.reserve(completions.size());
    for (const auto& completion : completions) {
        if (completion.decision != BrainOwnedCandidateDecision::Accepted ||
            completion.callsign.empty() ||
            completion.frequency.empty() ||
            !IsDisplayIntentRelation(completion.displayRelation)) {
            continue;
        }

        BrainDisplayRelationFact fact;
        fact.callsign = completion.callsign;
        fact.frequency = completion.frequency;
        fact.displayRelation = completion.displayRelation;
        fact.hasRouteEntryDistance = completion.hasRouteEntryDistance;
        fact.routeEntryDistanceNm = completion.routeEntryDistanceNm;
        facts.push_back(std::move(fact));
    }
    return facts;
}

bool BuildCtafStationFromLookupFact(
    const BrainOwnedCtafLookupFact& fact,
    const RadioStateSnapshot& radios,
    BoardStationSnapshot* station) {
    if (station == nullptr || fact.airportIcao.empty()) {
        return false;
    }

    *station = {};
    station->callsign = fact.airportIcao;
    if (fact.available) {
        station->role = StationRole::Ctaf;
        station->frequency = fact.frequency;
        station->tuned = FrequencyTuned(fact.frequency, radios);
    } else if (fact.resolved) {
        station->role = StationRole::Unicom;
        station->frequency = kUnicomFallbackFrequency;
        station->tuned = FrequencyTuned(kUnicomFallbackFrequency, radios);
    } else {
        station->role = StationRole::Ctaf;
    }
    return true;
}

std::string CtafEvidenceId(
    const std::string& endpoint,
    const std::string& airportIcao) {
    return "ctaf-unicom:" + endpoint + ":" + NormalizeIcao(airportIcao);
}

BrainOwnedCtafUnicomSourceEvidence BuildCtafUnicomSourceEvidence(
    const std::string& endpoint,
    const BrainOwnedCtafLookupFact& fact) {
    BrainOwnedCtafUnicomSourceEvidence evidence;
    evidence.endpoint = endpoint;
    evidence.airportIcao = NormalizeIcao(fact.airportIcao);
    evidence.evidenceId = CtafEvidenceId(endpoint, evidence.airportIcao);
    evidence.lookupAttempted = fact.lookupAttempted;
    evidence.lookupSkippedReason = fact.lookupSkippedReason;
    evidence.cacheHit = fact.cacheHit;
    evidence.fetchInProgress = fact.fetchInProgress;
    evidence.requestSucceeded = fact.requestSucceeded;
    evidence.statusCodeClass = fact.statusCodeClass;
    evidence.resolved = fact.resolved;
    evidence.available = fact.available;
    evidence.frequency = fact.frequency;
    evidence.lastAttemptAgeSeconds = fact.lastAttemptAgeSeconds;
    evidence.failureCount = fact.failureCount;
    evidence.fallbackEligible = fact.resolved && !fact.available;
    evidence.fallbackFrequency =
        evidence.fallbackEligible ? kUnicomFallbackFrequency : "";
    evidence.pendingReason = fact.pendingReason;

    if (evidence.airportIcao.empty()) {
        evidence.sourceConfidence = "unknown";
        evidence.sourceReason = "missing-airport";
        if (evidence.lookupSkippedReason.empty()) {
            evidence.lookupSkippedReason = "missing-airport";
        }
    } else if (fact.available) {
        evidence.sourceConfidence = "high";
        evidence.sourceReason = "ctaf-available";
    } else if (fact.resolved) {
        evidence.sourceConfidence = "medium";
        evidence.sourceReason = "resolved-no-ctaf";
    } else if (fact.fetchInProgress) {
        evidence.sourceConfidence = "fallback";
        evidence.sourceReason = "fetch-in-progress";
        if (evidence.pendingReason.empty()) {
            evidence.pendingReason = "fetch-in-progress";
        }
    } else if (fact.failureCount > 0) {
        evidence.sourceConfidence = "low";
        evidence.sourceReason = "lookup-failed";
        if (evidence.pendingReason.empty()) {
            evidence.pendingReason = "lookup-failed";
        }
    } else if (fact.lookupAttempted) {
        evidence.sourceConfidence = "fallback";
        evidence.sourceReason = "lookup-pending";
        if (evidence.pendingReason.empty()) {
            evidence.pendingReason = "unresolved-pending";
        }
    } else {
        evidence.sourceConfidence = "unknown";
        evidence.sourceReason = evidence.lookupSkippedReason.empty()
            ? "lookup-not-attempted"
            : evidence.lookupSkippedReason;
    }

    return evidence;
}

const BrainOwnedCtafUnicomSourceEvidence* FindCtafUnicomSourceEvidence(
    const std::vector<BrainOwnedCtafUnicomSourceEvidence>& evidence,
    const std::string& endpoint) {
    const auto found = std::find_if(
        evidence.begin(),
        evidence.end(),
        [&](const auto& record) { return record.endpoint == endpoint; });
    return found == evidence.end() ? nullptr : &(*found);
}

std::string StationRoleToken(StationRole role) {
    switch (role) {
        case StationRole::Ctaf:
            return "CTAF";
        case StationRole::Unicom:
            return "UNICOM";
        default:
            return "none";
    }
}

BrainOwnedCtafUnicomProjectionEvidence BuildCtafUnicomProjectionEvidence(
    const std::string& endpoint,
    const BrainOwnedCtafUnicomSourceEvidence* sourceEvidence,
    bool hasStation,
    const BoardStationSnapshot& station,
    int legacyRowRemovedCount,
    int duplicateSuppressedCount,
    bool liveRowEmitted) {
    BrainOwnedCtafUnicomProjectionEvidence evidence;
    evidence.endpoint = endpoint;
    evidence.sourceEvidenceId =
        sourceEvidence == nullptr ? "" : sourceEvidence->evidenceId;
    const auto airportIcao =
        sourceEvidence == nullptr ? std::string("unknown")
                                  : sourceEvidence->airportIcao;
    evidence.airportIcao =
        sourceEvidence != nullptr
            ? sourceEvidence->airportIcao
            : (hasStation ? NormalizeIcao(station.callsign) : std::string());
    evidence.projectionEvidenceId =
        "ctaf-unicom-projection:" + endpoint + ":" +
        (airportIcao.empty() ? std::string("unknown") : airportIcao);
    evidence.projectedRole = hasStation ? StationRoleToken(station.role) : "none";
    evidence.projectedFrequency = hasStation ? station.frequency : "";
    evidence.fallbackUsed = hasStation && station.role == StationRole::Unicom;
    evidence.unresolvedProjectedEmptyFrequency =
        hasStation && station.role == StationRole::Ctaf &&
        station.frequency.empty() &&
        sourceEvidence != nullptr && !sourceEvidence->resolved;
    evidence.legacyRowRemovedCount = legacyRowRemovedCount;
    evidence.duplicateSuppressedCount = duplicateSuppressedCount;
    evidence.diagnosticCompatibilityProjectionOnly = true;
    evidence.completionBypassCompatibilityOnly = true;
    evidence.completionBypassRetired = true;
    evidence.completionBypassLiveAuthority = false;
    evidence.completionBypassDiagnosticOnly = true;
    evidence.legacyDiagnosticLiveRowEmitted = liveRowEmitted;
    evidence.liveRowEmitted = liveRowEmitted;
    return evidence;
}

BrainOwnedCtafUnicomEvidenceSummary BuildCtafUnicomEvidenceSummary(
    const std::vector<BrainOwnedCtafUnicomSourceEvidence>& sourceEvidence,
    const std::vector<BrainOwnedCtafUnicomProjectionEvidence>& projectionEvidence) {
    BrainOwnedCtafUnicomEvidenceSummary summary;
    summary.sourceEvidenceCount = static_cast<int>(sourceEvidence.size());
    summary.projectionEvidenceCount =
        static_cast<int>(projectionEvidence.size());
    summary.advisoryDecisionCount = 0;
    for (const auto& projection : projectionEvidence) {
        ++summary.historicalCompatibilityRowCount;
        if (projection.liveRowEmitted) {
            ++summary.liveRowEmittedCount;
            ++summary.legacyDiagnosticLiveRowEmittedCount;
        }
        if (projection.diagnosticCompatibilityProjectionOnly) {
            summary.diagnosticCompatibilityProjectionOnly = 1;
        }
        if (projection.completionBypassCompatibilityOnly) {
            summary.completionBypassCompatibilityOnly = 1;
        }
        if (!projection.completionBypassDiagnosticOnly) {
            summary.compatibilityRowsDiagnosticOnly = false;
        }
    }
    return summary;
}

const BrainOwnedCtafUnicomProjectionEvidence* FindCtafUnicomProjectionEvidence(
    const std::vector<BrainOwnedCtafUnicomProjectionEvidence>& evidence,
    const std::string& sourceEvidenceId,
    const std::string& endpoint) {
    const auto found = std::find_if(
        evidence.begin(),
        evidence.end(),
        [&](const auto& record) {
            return record.sourceEvidenceId == sourceEvidenceId ||
                   (!endpoint.empty() && record.endpoint == endpoint);
        });
    return found == evidence.end() ? nullptr : &(*found);
}

BrainOwnedCtafUnicomAdvisoryPreviewDecision
BuildCtafUnicomAdvisoryPreviewDecision(
    const BrainOwnedCtafUnicomSourceEvidence& source,
    const BrainOwnedCtafUnicomProjectionEvidence* projection) {
    BrainOwnedCtafUnicomAdvisoryPreviewDecision decision;
    decision.advisoryDecisionId =
        "ctaf-unicom-preview:" + source.endpoint + ":" + source.airportIcao;
    decision.sourceEvidenceId = source.evidenceId;
    decision.endpoint = source.endpoint;
    decision.airportIcao = source.airportIcao;
    decision.sourceConfidence = source.sourceConfidence;
    decision.confidenceLevel = source.sourceConfidence.empty()
        ? "unknown"
        : source.sourceConfidence;
    decision.projectedRole =
        projection == nullptr ? "none" : projection->projectedRole;
    decision.projectedFrequency =
        projection == nullptr ? "" : projection->projectedFrequency;
    decision.fallbackUsed =
        projection != nullptr && projection->fallbackUsed;
    decision.wouldEmitLiveRow = false;

    if (source.airportIcao.empty()) {
        decision.decision = "reject-invalid-source";
        decision.projectedRole = "none";
        decision.projectedFrequency.clear();
        decision.fallbackUsed = false;
        decision.confidenceLevel = "high";
        decision.negativeScore = 0.80;
        decision.hardBlock = true;
        decision.reason = "missing-airport";
    } else if (source.available && !source.frequency.empty()) {
        decision.decision = "ctaf-display";
        decision.projectedRole = "CTAF";
        decision.projectedFrequency = source.frequency;
        decision.fallbackUsed = false;
        decision.wouldEmitLiveRow = true;
        decision.confidenceLevel = "high";
        decision.positiveScore = 0.90;
        decision.negativeScore = 0.05;
        decision.reason = "available-ctaf-source";
    } else if (source.available && source.frequency.empty()) {
        decision.decision = "hide-non-displayable";
        decision.projectedRole = "CTAF";
        decision.projectedFrequency.clear();
        decision.fallbackUsed = false;
        decision.wouldEmitLiveRow = false;
        decision.confidenceLevel = "high";
        decision.negativeScore = 0.90;
        decision.hardBlock = true;
        decision.reason = "available-ctaf-empty-frequency";
    } else if (source.fallbackEligible) {
        decision.decision = "unicom-fallback-display";
        decision.projectedRole = "UNICOM";
        decision.projectedFrequency = source.fallbackFrequency;
        decision.fallbackUsed = true;
        decision.wouldEmitLiveRow = true;
        decision.confidenceLevel = "medium";
        decision.positiveScore = 0.72;
        decision.negativeScore = 0.10;
        decision.reason = "resolved-no-ctaf-unicom-fallback";
    } else if (source.failureCount > 0 ||
               source.sourceReason == "lookup-failed") {
        decision.decision = "lookup-failed";
        decision.projectedRole = "CTAF";
        decision.projectedFrequency.clear();
        decision.fallbackUsed = false;
        decision.wouldEmitLiveRow = false;
        decision.confidenceLevel = "low";
        decision.positiveScore = 0.10;
        decision.negativeScore = 0.65;
        decision.reason =
            "lookup-failed:failureCount=" +
            std::to_string(source.failureCount);
    } else if (source.lookupAttempted || source.fetchInProgress) {
        decision.decision = "defer-pending";
        decision.projectedRole = "CTAF";
        decision.projectedFrequency.clear();
        decision.fallbackUsed = false;
        decision.wouldEmitLiveRow = false;
        decision.confidenceLevel =
            source.fetchInProgress ? "fallback" : "unknown";
        decision.positiveScore = 0.20;
        decision.negativeScore = 0.25;
        decision.reason = source.pendingReason.empty()
            ? "lookup-pending"
            : source.pendingReason;
    } else {
        decision.decision = "defer-pending";
        decision.projectedRole = "CTAF";
        decision.projectedFrequency.clear();
        decision.fallbackUsed = false;
        decision.wouldEmitLiveRow = false;
        decision.confidenceLevel = "unknown";
        decision.positiveScore = 0.05;
        decision.negativeScore = 0.10;
        decision.reason = source.sourceReason.empty()
            ? "lookup-not-attempted"
            : source.sourceReason;
    }

    if (NormalizeFrequency(decision.projectedFrequency).empty()) {
        decision.wouldEmitLiveRow = false;
    }

    if (projection != nullptr) {
        decision.matchesCurrentProjection =
            decision.projectedRole == projection->projectedRole &&
            decision.projectedFrequency == projection->projectedFrequency &&
            decision.fallbackUsed == projection->fallbackUsed &&
            decision.wouldEmitLiveRow == projection->liveRowEmitted;
    }

    return decision;
}

std::vector<BrainOwnedCtafUnicomAdvisoryPreviewDecision>
BuildCtafUnicomAdvisoryPreviewDecisions(
    const std::vector<BrainOwnedCtafUnicomSourceEvidence>& sourceEvidence,
    const std::vector<BrainOwnedCtafUnicomProjectionEvidence>& projectionEvidence) {
    std::vector<BrainOwnedCtafUnicomAdvisoryPreviewDecision> decisions;
    decisions.reserve(sourceEvidence.size());
    for (const auto& source : sourceEvidence) {
        const auto* projection = FindCtafUnicomProjectionEvidence(
            projectionEvidence,
            source.evidenceId,
            source.endpoint);
        decisions.push_back(
            BuildCtafUnicomAdvisoryPreviewDecision(source, projection));
    }
    return decisions;
}

void ApplyCtafUnicomAdvisoryDiagnosticFaults(
    const BrainOwnedPublisherInput& input,
    std::vector<BrainOwnedCtafUnicomAdvisoryPreviewDecision>* decisions) {
    if (decisions == nullptr) {
        return;
    }

    if (input.omitDepartureCtafUnicomAdvisoryDecisionForDiagnostics ||
        input.omitArrivalCtafUnicomAdvisoryDecisionForDiagnostics) {
        decisions->erase(
            std::remove_if(
                decisions->begin(),
                decisions->end(),
                [&](const auto& decision) {
                    return (input.omitDepartureCtafUnicomAdvisoryDecisionForDiagnostics &&
                            decision.endpoint == "departure") ||
                           (input.omitArrivalCtafUnicomAdvisoryDecisionForDiagnostics &&
                            decision.endpoint == "arrival");
                }),
            decisions->end());
    }

    for (auto& decision : *decisions) {
        const auto incomplete =
            (input.incompleteDepartureCtafUnicomAdvisoryDecisionForDiagnostics &&
             decision.endpoint == "departure") ||
            (input.incompleteArrivalCtafUnicomAdvisoryDecisionForDiagnostics &&
             decision.endpoint == "arrival");
        if (!incomplete) {
            continue;
        }

        decision.decision = "incomplete-advisory-diagnostic";
        decision.projectedFrequency.clear();
        decision.wouldEmitLiveRow = false;
        decision.matchesCurrentProjection = false;
        decision.confidenceLevel = "low";
        decision.positiveScore = 0.0;
        decision.negativeScore = 0.95;
        decision.hardBlock = true;
        decision.reason = "harness-incomplete-advisory";
    }
}

BrainOwnedCtafUnicomAdvisoryPreviewSummary
BuildCtafUnicomAdvisoryPreviewSummary(
    const std::vector<BrainOwnedCtafUnicomSourceEvidence>& sourceEvidence,
    const std::vector<BrainOwnedCtafUnicomProjectionEvidence>& projectionEvidence,
    const std::vector<BrainOwnedCtafUnicomAdvisoryPreviewDecision>& decisions) {
    BrainOwnedCtafUnicomAdvisoryPreviewSummary summary;
    summary.sourceEvidenceCount = static_cast<int>(sourceEvidence.size());
    summary.projectionEvidenceCount =
        static_cast<int>(projectionEvidence.size());
    summary.advisoryPreviewDecisionCount =
        static_cast<int>(decisions.size());
    for (const auto& projection : projectionEvidence) {
        if (projection.diagnosticCompatibilityProjectionOnly) {
            summary.diagnosticCompatibilityProjectionOnly = 1;
        }
        if (projection.completionBypassCompatibilityOnly) {
            summary.completionBypassCompatibilityOnly = 1;
        }
    }
    for (const auto& decision : decisions) {
        if (decision.wouldEmitLiveRow) {
            ++summary.previewWouldEmitLiveRowCount;
        }
        if (decision.matchesCurrentProjection) {
            ++summary.previewMatchesCurrentProjectionCount;
        } else {
            ++summary.previewMismatchCount;
        }
    }
    return summary;
}

bool BuildCtafStationFromAdvisoryDecision(
    const BrainOwnedCtafUnicomAdvisoryPreviewDecision& decision,
    const RadioStateSnapshot& radios,
    BoardStationSnapshot* station) {
    if (station == nullptr ||
        !decision.wouldEmitLiveRow ||
        NormalizeFrequency(decision.projectedFrequency).empty() ||
        decision.airportIcao.empty()) {
        return false;
    }

    StationRole role = StationRole::Other;
    if (decision.projectedRole == "CTAF") {
        role = StationRole::Ctaf;
    } else if (decision.projectedRole == "UNICOM") {
        role = StationRole::Unicom;
    } else {
        return false;
    }

    *station = {};
    station->callsign = decision.airportIcao;
    station->role = role;
    station->frequency = decision.projectedFrequency;
    station->sourceDecisionId = decision.advisoryDecisionId;
    station->sourceEvidenceId = decision.sourceEvidenceId;
    station->sourceEvidenceType = "ctaf-unicom-source-evidence";
    station->sourceEvidenceDomain = "ctaf-unicom-advisory";
    station->sourceEvidenceLinkStatus =
        decision.sourceEvidenceId.empty() ? "missing-from-display-decision"
                                          : "linked";
    station->sourceEvidenceMissingReason =
        decision.sourceEvidenceId.empty()
            ? "ctaf-unicom-advisory-missing-source-evidence-id"
            : "";
    station->tuned = FrequencyTuned(decision.projectedFrequency, radios);
    return true;
}

int AppendCtafUnicomRowsFromAdvisoryDecisions(
    const std::vector<BrainOwnedCtafUnicomAdvisoryPreviewDecision>& decisions,
    const RadioStateSnapshot& radios,
    ModuleBoardSnapshot* departureBoard,
    ModuleBoardSnapshot* arrivalBoard) {
    std::unordered_set<std::string> departureKeys;
    std::unordered_set<std::string> arrivalKeys;
    int liveRows = 0;
    for (const auto& decision : decisions) {
        BoardStationSnapshot station;
        if (!BuildCtafStationFromAdvisoryDecision(
                decision,
                radios,
                &station)) {
            continue;
        }

        if (decision.endpoint == "departure") {
            if (AppendUniqueStation(station, departureBoard, &departureKeys)) {
                ++liveRows;
            }
        } else if (decision.endpoint == "arrival") {
            if (AppendUniqueStation(station, arrivalBoard, &arrivalKeys)) {
                ++liveRows;
            }
        }
    }
    return liveRows;
}

int CountCompatibilityProjectionLiveRows(
    const std::vector<BrainOwnedCtafUnicomProjectionEvidence>& projections) {
    return static_cast<int>(std::count_if(
        projections.begin(),
        projections.end(),
        [](const auto& projection) { return projection.liveRowEmitted; }));
}

BrainOwnedCtafUnicomAdvisoryAuthoritySummary
BuildCtafUnicomAdvisoryAuthoritySummary(
    const std::vector<BrainOwnedCtafUnicomSourceEvidence>& sourceEvidence,
    const std::vector<BrainOwnedCtafUnicomProjectionEvidence>& projectionEvidence,
    const std::vector<BrainOwnedCtafUnicomAdvisoryPreviewDecision>& decisions,
    int liveAdvisoryRowCount,
    bool liveRowsBrainOwned) {
    BrainOwnedCtafUnicomAdvisoryAuthoritySummary summary;
    summary.advisoryAuthority =
        liveRowsBrainOwned ? "brain-evidence" : "none";
    summary.sourceEvidenceCount = static_cast<int>(sourceEvidence.size());
    summary.advisoryPreviewDecisionCount =
        static_cast<int>(decisions.size());
    summary.liveAdvisoryRowCount = liveAdvisoryRowCount;
    summary.brainAdvisoryLiveRowCount = liveAdvisoryRowCount;
    summary.compatibilityProjectionCount =
        static_cast<int>(projectionEvidence.size());
    summary.liveRowsBrainOwned = liveRowsBrainOwned;

    for (const auto& projection : projectionEvidence) {
        if (projection.diagnosticCompatibilityProjectionOnly) {
            summary.diagnosticCompatibilityProjectionOnly = 1;
        }
        if (projection.completionBypassCompatibilityOnly) {
            summary.completionBypassCompatibilityOnly = 1;
        }
        if (projection.completionBypassLiveAuthority) {
            ++summary.liveBypassAuthorityCount;
        }
        if (projection.completionBypassDiagnosticOnly) {
            ++summary.diagnosticBypassRowCount;
        }
    }

    int mismatchCount = 0;
    for (const auto& decision : decisions) {
        if (!decision.matchesCurrentProjection) {
            ++mismatchCount;
        }
    }
    const auto compatibilityLiveRows =
        CountCompatibilityProjectionLiveRows(projectionEvidence);
    mismatchCount += std::abs(compatibilityLiveRows - liveAdvisoryRowCount);
    summary.oldVsBrainMismatchCount = mismatchCount;
    summary.bypassRetirementSafe =
        summary.completionBypassRetired &&
        summary.liveBypassAuthorityCount == 0 &&
        summary.duplicateLiveRowCount == 0;
    summary.noLiveBypassAuthority =
        summary.liveBypassAuthorityCount == 0;
    summary.compatibilityRowsDiagnosticOnly =
        summary.completionBypassRetired &&
        summary.liveBypassAuthorityCount == 0 &&
        summary.diagnosticBypassRowCount ==
            summary.compatibilityProjectionCount;
    summary.liveRowsBrainAdvisoryOwned =
        summary.liveBypassAuthorityCount == 0 &&
        (summary.liveAdvisoryRowCount == 0 || summary.liveRowsBrainOwned);
    summary.legacyBypassFieldsQuarantined =
        summary.completionBypassRetired &&
        summary.noLiveBypassAuthority &&
        summary.compatibilityRowsDiagnosticOnly;
    return summary;
}

const BrainOwnedCtafUnicomAdvisoryPreviewDecision*
FindCtafUnicomAdvisoryPreviewDecision(
    const std::vector<BrainOwnedCtafUnicomAdvisoryPreviewDecision>& decisions,
    const std::string& sourceEvidenceId,
    const std::string& endpoint) {
    const auto found = std::find_if(
        decisions.begin(),
        decisions.end(),
        [&](const auto& decision) {
            return decision.sourceEvidenceId == sourceEvidenceId ||
                   (!endpoint.empty() && decision.endpoint == endpoint);
        });
    return found == decisions.end() ? nullptr : &(*found);
}

const BrainOwnedCtafUnicomSourceEvidence* FindCtafUnicomSourceEvidenceById(
    const std::vector<BrainOwnedCtafUnicomSourceEvidence>& sourceEvidence,
    const std::string& sourceEvidenceId,
    const std::string& endpoint) {
    const auto found = std::find_if(
        sourceEvidence.begin(),
        sourceEvidence.end(),
        [&](const auto& source) {
            return source.evidenceId == sourceEvidenceId ||
                   (!endpoint.empty() && source.endpoint == endpoint);
        });
    return found == sourceEvidence.end() ? nullptr : &(*found);
}

bool CtafAuditPendingLookup(
    const BrainOwnedCtafUnicomSourceEvidence* source,
    const BrainOwnedCtafUnicomAdvisoryPreviewDecision* advisory) {
    return (source != nullptr &&
            (source->fetchInProgress ||
             source->sourceReason == "lookup-pending" ||
             source->sourceReason == "fetch-in-progress")) ||
           (advisory != nullptr && advisory->decision == "defer-pending");
}

bool CtafAuditLookupFailed(
    const BrainOwnedCtafUnicomSourceEvidence* source,
    const BrainOwnedCtafUnicomAdvisoryPreviewDecision* advisory) {
    return (source != nullptr &&
            (source->failureCount > 0 ||
             source->sourceReason == "lookup-failed")) ||
           (advisory != nullptr && advisory->decision == "lookup-failed");
}

void ApplyCtafUnicomRetirementPolicy(
    BrainOwnedCtafUnicomBypassAuditDecision* decision) {
    if (decision == nullptr) {
        return;
    }

    decision->retirementBlockerClass =
        decision->retirementBlockedReason.empty()
            ? std::string("none")
            : decision->retirementBlockedReason;

    const auto hasUsableFrequency =
        !NormalizeFrequency(decision->frequency).empty();
    const auto hasRawBlocker =
        decision->retirementBlockerClass != "none";

    if (decision->missingSourceEvidence ||
        decision->missingAdvisoryDecision) {
        decision->retirementPolicy = "fail-soft-missing-evidence";
        decision->retirementPolicyReason =
            decision->missingSourceEvidence
                ? "missing-source-evidence"
                : "missing-advisory-decision";
        decision->retirementBlockerClass = "missing-evidence";
        decision->missingEvidenceByPolicy = true;
        decision->retirementBlockerResolved = false;
        decision->retirementSafeAfterPolicy = false;
    } else if (
        decision->retirementBlockedReason ==
        "duplicate-compatibility-row") {
        decision->retirementBlockerClass =
            "duplicate-compatibility-row";
        if (decision->advisoryMatchesBypassRow &&
            decision->brainRowHasBypassEquivalent) {
            decision->retirementPolicy =
                "compatibility-duplicate-suppressed";
            decision->retirementPolicyReason =
                "duplicate-compatibility-row-has-brain-equivalent";
            decision->compatibilityDuplicateSuppressed = true;
            decision->duplicateSuppressionReason =
                "legacy-ctaf-unicom-row-removed-before-advisory-projection";
            decision->retirementBlockerResolved = true;
            decision->retirementSafeAfterPolicy = true;
        } else {
            decision->retirementPolicy =
                "compatibility-duplicate-still-blocked";
            decision->retirementPolicyReason =
                "duplicate-compatibility-row-missing-safe-brain-equivalent";
            decision->retirementBlockerResolved = false;
            decision->retirementSafeAfterPolicy = false;
        }
    } else if (decision->pendingLookup) {
        decision->retirementPolicy = "defer-pending-lookup";
        decision->retirementPolicyReason =
            "pending-lookup-no-resolved-frequency";
        decision->retirementBlockerClass = "pending-lookup";
        decision->nonDisplayableByPolicy = true;
        decision->deferredByPolicy = true;
        decision->retirementBlockerResolved = true;
        decision->retirementSafeAfterPolicy = true;
    } else if (decision->lookupFailed) {
        decision->retirementPolicy = "failed-lookup-non-displayable";
        decision->retirementPolicyReason =
            "failed-lookup-no-valid-frequency";
        decision->retirementBlockerClass = "lookup-failed";
        decision->nonDisplayableByPolicy = true;
        decision->failedLookupByPolicy = true;
        decision->retirementBlockerResolved = true;
        decision->retirementSafeAfterPolicy = true;
    } else if (decision->emptyFrequency) {
        decision->retirementPolicy = "empty-frequency-non-displayable";
        decision->retirementPolicyReason =
            "empty-frequency-hard-block";
        decision->retirementBlockerClass = "empty-frequency";
        decision->nonDisplayableByPolicy = true;
        decision->emptyFrequencyByPolicy = true;
        decision->retirementBlockerResolved = true;
        decision->retirementSafeAfterPolicy = true;
    } else if (hasRawBlocker) {
        decision->retirementPolicy = "mismatch-still-blocked";
        decision->retirementPolicyReason =
            decision->retirementBlockedReason;
        decision->retirementBlockerResolved = false;
        decision->retirementSafeAfterPolicy = false;
    } else {
        decision->retirementPolicy = "parity-safe";
        decision->retirementPolicyReason =
            "brain-advisory-equivalent";
        decision->retirementBlockerClass = "none";
        decision->retirementBlockerResolved = false;
        decision->retirementSafeAfterPolicy =
            decision->advisoryMatchesBypassRow &&
            (!decision->bypassRequired ||
             decision->bypassRowHasBrainEquivalent);
    }

    decision->wouldLoseFrequencyIfBypassRemoved =
        decision->bypassRequired &&
        hasUsableFrequency &&
        !decision->brainRowHasBypassEquivalent &&
        !decision->compatibilityDuplicateSuppressed;
    decision->wouldLoseVisibilityIfBypassRemoved =
        decision->bypassRequired &&
        !decision->brainRowHasBypassEquivalent &&
        !decision->nonDisplayableByPolicy &&
        !decision->compatibilityDuplicateSuppressed;

    decision->retirementStillBlocked =
        !decision->retirementSafeAfterPolicy ||
        decision->wouldLoseFrequencyIfBypassRemoved ||
        decision->wouldLoseVisibilityIfBypassRemoved;
    decision->safeToRemoveBypassAfterCleanup =
        !decision->retirementStillBlocked;
    decision->bypassRetirementFallbackWarning =
        decision->missingEvidenceByPolicy &&
        (decision->wouldLoseFrequencyIfBypassRemoved ||
         decision->wouldLoseVisibilityIfBypassRemoved);
    decision->missingEvidenceWarningOnly =
        decision->bypassRetirementFallbackWarning;
    decision->missingEvidenceFallbackPreserved =
        decision->bypassRetirementFallbackWarning;
    decision->advisoryProjectionAuthority =
        decision->advisoryWouldEmitLiveRow;
    if (decision->advisoryProjectionAuthority) {
        decision->liveRowAuthority = "brain-advisory";
        decision->diagnosticLiveRowAuthority = "brain-advisory";
    } else if (decision->bypassRetirementFallbackWarning) {
        decision->liveRowAuthority = "missing-evidence-warning-only";
        decision->diagnosticLiveRowAuthority =
            "missing-evidence-warning-only";
    } else {
        decision->liveRowAuthority = "none";
        decision->diagnosticLiveRowAuthority = "none";
    }
    decision->standbyAuthority =
        decision->standbyConsumesAdvisoryDecision
            ? std::string("brain-advisory")
            : std::string("none");
    decision->bypassRetirementRegressionSafe =
        decision->completionBypassRetired &&
        !decision->completionBypassLiveAuthority &&
        !decision->standbyConsumesBypassRow &&
        (decision->safeToRemoveBypassAfterCleanup ||
         decision->bypassRetirementFallbackWarning);
}

BrainOwnedCtafUnicomBypassAuditDecision BuildCtafUnicomBypassAuditDecision(
    const BrainOwnedCtafUnicomSourceEvidence* source,
    const BrainOwnedCtafUnicomProjectionEvidence* projection,
    const BrainOwnedCtafUnicomAdvisoryPreviewDecision* advisory,
    const std::unordered_set<std::string>& standbyAdvisoryDecisionIds,
    const std::string& advisoryAuthority) {
    BrainOwnedCtafUnicomBypassAuditDecision decision;
    decision.endpoint =
        projection != nullptr
            ? projection->endpoint
            : (advisory != nullptr
                   ? advisory->endpoint
                   : (source != nullptr ? source->endpoint : ""));
    decision.airportIcao =
        source != nullptr
            ? source->airportIcao
            : (advisory != nullptr ? advisory->airportIcao : "");
    decision.callsign = decision.airportIcao;
    decision.sourceEvidenceId =
        source != nullptr
            ? source->evidenceId
            : (advisory != nullptr
                   ? advisory->sourceEvidenceId
                   : (projection != nullptr ? projection->sourceEvidenceId : ""));
    decision.projectionEvidenceId =
        projection == nullptr ? "" : projection->projectionEvidenceId;
    decision.advisoryDecisionId =
        advisory == nullptr ? "" : advisory->advisoryDecisionId;
    decision.role =
        advisory != nullptr
            ? advisory->projectedRole
            : (projection != nullptr ? projection->projectedRole : "none");
    decision.frequency =
        advisory != nullptr
            ? advisory->projectedFrequency
            : (projection != nullptr ? projection->projectedFrequency : "");
    decision.ctafUnicomBypassAuditDecisionId =
        "ctaf-unicom-bypass-audit:" + decision.endpoint + ":" +
        (decision.airportIcao.empty() ? std::string("unknown")
                                      : decision.airportIcao);
    decision.diagnosticCompatibilityOnly =
        projection != nullptr &&
        projection->diagnosticCompatibilityProjectionOnly;
    decision.compatibilityOnly =
        projection != nullptr && projection->completionBypassCompatibilityOnly;
    decision.completionBypassRetired =
        projection == nullptr || projection->completionBypassRetired;
    decision.completionBypassLiveAuthority =
        projection != nullptr && projection->completionBypassLiveAuthority;
    decision.completionBypassDiagnosticOnly =
        projection != nullptr && projection->completionBypassDiagnosticOnly;
    decision.retiredBypassCompatibilityRowCount =
        projection != nullptr && projection->completionBypassDiagnosticOnly &&
                projection->liveRowEmitted
            ? 1
            : 0;
    decision.bypassRequired =
        projection != nullptr && projection->completionBypassCompatibilityOnly &&
        projection->liveRowEmitted;
    decision.diagnosticCompatibilityWouldDisplay =
        projection != nullptr &&
        projection->diagnosticCompatibilityProjectionOnly &&
        projection->legacyDiagnosticLiveRowEmitted;
    if (projection == nullptr) {
        decision.bypassReason = "missing-projection-evidence";
    } else if (decision.bypassRequired) {
        decision.bypassReason =
            projection->completionBypassRetired
                ? "completion-bypass-retired-diagnostic-only"
                : "station-requires-completion-bypass";
    } else {
        decision.bypassReason = "no-live-bypass-row";
    }
    decision.diagnosticCompatibilityReason = decision.bypassReason;
    decision.advisoryAuthority = advisoryAuthority;
    decision.advisoryWouldEmitLiveRow =
        advisory != nullptr && advisory->wouldEmitLiveRow;
    decision.missingAdvisoryDecision =
        decision.bypassRequired && advisory == nullptr;
    decision.missingSourceEvidence = source == nullptr;
    decision.pendingLookup = CtafAuditPendingLookup(source, advisory);
    decision.lookupFailed = CtafAuditLookupFailed(source, advisory);
    decision.emptyFrequency = decision.frequency.empty();
    decision.unicomFallback =
        (advisory != nullptr && advisory->fallbackUsed) ||
        (projection != nullptr && projection->fallbackUsed);
    decision.standbyConsumesAdvisoryDecision =
        advisory != nullptr &&
        standbyAdvisoryDecisionIds.find(advisory->advisoryDecisionId) !=
            standbyAdvisoryDecisionIds.end();
    decision.standbyConsumesBypassRow = false;

    decision.roleMatches =
        projection != nullptr && advisory != nullptr &&
        projection->projectedRole == advisory->projectedRole;
    decision.frequencyMatches =
        projection != nullptr && advisory != nullptr &&
        projection->projectedFrequency == advisory->projectedFrequency;
    decision.endpointMatches =
        projection != nullptr && advisory != nullptr &&
        projection->endpoint == advisory->endpoint;
    decision.airportMatches =
        source != nullptr && advisory != nullptr &&
        source->airportIcao == advisory->airportIcao;
    decision.visibilityMatches =
        projection != nullptr && advisory != nullptr &&
        projection->liveRowEmitted == advisory->wouldEmitLiveRow;
    decision.advisoryMatchesBypassRow =
        decision.roleMatches && decision.frequencyMatches &&
        decision.endpointMatches && decision.airportMatches &&
        decision.visibilityMatches;
    decision.bypassRowHasBrainEquivalent =
        decision.bypassRequired && advisory != nullptr;
    decision.brainRowHasBypassEquivalent =
        decision.advisoryWouldEmitLiveRow && projection != nullptr;

    if (projection != nullptr &&
        (projection->legacyRowRemovedCount > 0 ||
         projection->duplicateSuppressedCount > 0)) {
        decision.mismatchReason = "duplicate-compatibility-row";
    } else if (decision.missingSourceEvidence) {
        decision.mismatchReason = "missing-source-evidence";
    } else if (decision.missingAdvisoryDecision) {
        decision.mismatchReason = "missing-advisory-decision";
    } else if (advisory != nullptr && projection == nullptr) {
        decision.mismatchReason = "missing-bypass-equivalent";
    } else if (projection != nullptr && advisory != nullptr &&
               !decision.advisoryMatchesBypassRow) {
        if (!decision.roleMatches) {
            decision.mismatchReason = "role-mismatch";
        } else if (!decision.frequencyMatches) {
            decision.mismatchReason = "frequency-mismatch";
        } else if (!decision.endpointMatches) {
            decision.mismatchReason = "endpoint-mismatch";
        } else if (!decision.airportMatches) {
            decision.mismatchReason = "airport-mismatch";
        } else if (!decision.visibilityMatches) {
            decision.mismatchReason = "visibility-mismatch";
        }
    }

    if (decision.missingSourceEvidence) {
        decision.retirementBlockedReason = "missing-source-evidence";
    } else if (decision.missingAdvisoryDecision) {
        decision.retirementBlockedReason = "missing-advisory-decision";
    } else if (decision.mismatchReason == "duplicate-compatibility-row") {
        decision.retirementBlockedReason = "duplicate-compatibility-row";
    } else if (decision.pendingLookup) {
        decision.retirementBlockedReason = "pending-lookup";
    } else if (decision.lookupFailed) {
        decision.retirementBlockedReason = "lookup-failed";
    } else if (decision.emptyFrequency) {
        decision.retirementBlockedReason = "empty-frequency";
    } else if (!decision.mismatchReason.empty()) {
        decision.retirementBlockedReason = decision.mismatchReason;
    }

    decision.wouldRetireSafely =
        decision.retirementBlockedReason.empty() &&
        (!decision.bypassRequired || decision.bypassRowHasBrainEquivalent) &&
        (!decision.advisoryWouldEmitLiveRow ||
         decision.brainRowHasBypassEquivalent) &&
        decision.advisoryMatchesBypassRow;
    ApplyCtafUnicomRetirementPolicy(&decision);

    return decision;
}

std::vector<BrainOwnedCtafUnicomBypassAuditDecision>
BuildCtafUnicomBypassAuditDecisions(
    const std::vector<BrainOwnedCtafUnicomSourceEvidence>& sourceEvidence,
    const std::vector<BrainOwnedCtafUnicomProjectionEvidence>& projectionEvidence,
    const std::vector<BrainOwnedCtafUnicomAdvisoryPreviewDecision>& advisoryDecisions,
    const std::vector<BrainOwnedStandbyAssistAdvisoryCandidate>& standbyCandidates,
    const std::string& advisoryAuthority) {
    std::unordered_set<std::string> standbyAdvisoryDecisionIds;
    for (const auto& candidate : standbyCandidates) {
        if (!candidate.sourceDecisionId.empty()) {
            standbyAdvisoryDecisionIds.insert(candidate.sourceDecisionId);
        }
    }

    std::vector<BrainOwnedCtafUnicomBypassAuditDecision> decisions;
    decisions.reserve(
        std::max(projectionEvidence.size(), advisoryDecisions.size()));
    for (const auto& projection : projectionEvidence) {
        const auto* source = FindCtafUnicomSourceEvidenceById(
            sourceEvidence,
            projection.sourceEvidenceId,
            projection.endpoint);
        const auto* advisory = FindCtafUnicomAdvisoryPreviewDecision(
            advisoryDecisions,
            projection.sourceEvidenceId,
            projection.endpoint);
        decisions.push_back(
            BuildCtafUnicomBypassAuditDecision(
                source,
                &projection,
                advisory,
                standbyAdvisoryDecisionIds,
                advisoryAuthority));
    }

    for (const auto& advisory : advisoryDecisions) {
        const auto projectionFound = std::any_of(
            projectionEvidence.begin(),
            projectionEvidence.end(),
            [&](const auto& projection) {
                return projection.sourceEvidenceId == advisory.sourceEvidenceId ||
                       projection.endpoint == advisory.endpoint;
            });
        if (projectionFound) {
            continue;
        }
        const auto* source = FindCtafUnicomSourceEvidenceById(
            sourceEvidence,
            advisory.sourceEvidenceId,
            advisory.endpoint);
        decisions.push_back(
            BuildCtafUnicomBypassAuditDecision(
                source,
                nullptr,
                &advisory,
                standbyAdvisoryDecisionIds,
                advisoryAuthority));
    }
    return decisions;
}

BrainOwnedCtafUnicomBypassAuditSummary BuildCtafUnicomBypassAuditSummary(
    const std::vector<BrainOwnedCtafUnicomBypassAuditDecision>& decisions) {
    BrainOwnedCtafUnicomBypassAuditSummary summary;
    summary.bypassAuditDecisionCount = static_cast<int>(decisions.size());
    summary.ctafUnicomBypassRetirementReady = !decisions.empty();
    for (const auto& decision : decisions) {
        if (decision.bypassRequired) {
            ++summary.bypassRowCount;
        }
        if (decision.advisoryWouldEmitLiveRow) {
            ++summary.brainOwnedAdvisoryRowCount;
        }
        if (decision.bypassRowHasBrainEquivalent &&
            decision.advisoryMatchesBypassRow) {
            ++summary.matchingBrainEquivalentCount;
        }
        if (decision.bypassRequired && !decision.bypassRowHasBrainEquivalent) {
            ++summary.missingBrainEquivalentCount;
        }
        if (!decision.mismatchReason.empty() ||
            (decision.bypassRequired &&
             decision.bypassRowHasBrainEquivalent &&
             !decision.advisoryMatchesBypassRow)) {
            ++summary.mismatchCount;
        }
        if (decision.wouldRetireSafely) {
            ++summary.safeToRetireCount;
        } else {
            ++summary.blockedRetirementCount;
        }
        if (decision.pendingLookup) {
            ++summary.pendingLookupCount;
        }
        if (decision.lookupFailed) {
            ++summary.lookupFailedCount;
        }
        if (decision.emptyFrequency) {
            ++summary.emptyFrequencyCount;
        }
        if (decision.unicomFallback) {
            ++summary.unicomFallbackCount;
        }
        if (decision.standbyConsumesAdvisoryDecision) {
            ++summary.standbyAdvisoryConsumerCount;
        }
        if (decision.standbyConsumesBypassRow) {
            ++summary.standbyBypassConsumerCount;
        }
        if (decision.completionBypassLiveAuthority) {
            ++summary.liveBypassAuthorityCount;
        }
        if (decision.completionBypassDiagnosticOnly) {
            ++summary.diagnosticBypassRowCount;
        }
        if (decision.advisoryProjectionAuthority) {
            ++summary.brainAdvisoryLiveRowCount;
        }
        if (decision.bypassRetirementFallbackWarning) {
            ++summary.missingEvidenceWarningCount;
            ++summary.compatibilityFallbackWarningCount;
        }
        if (decision.missingEvidenceFallbackPreserved) {
            ++summary.missingEvidenceFallbackWarningCount;
        }
        if (decision.pendingLookup && decision.nonDisplayableByPolicy) {
            ++summary.pendingNonDisplayableCount;
        }
        if (decision.lookupFailed && decision.nonDisplayableByPolicy) {
            ++summary.failedLookupNonDisplayableCount;
        }
        if (decision.emptyFrequency && decision.nonDisplayableByPolicy) {
            ++summary.emptyFrequencyNonDisplayableCount;
        }
        summary.retiredBypassCompatibilityRowCount +=
            decision.retiredBypassCompatibilityRowCount;
        if (!decision.retirementPolicy.empty()) {
            ++summary.retirementPolicyDecisionCount;
        }
        if (decision.retirementBlockerClass != "none" &&
            decision.retirementBlockerResolved) {
            ++summary.resolvedBlockerCount;
        }
        if (decision.retirementStillBlocked) {
            ++summary.stillBlockedCount;
        }
        if (decision.nonDisplayableByPolicy) {
            ++summary.policyNonDisplayableCount;
        }
        if (decision.deferredByPolicy) {
            ++summary.policyDeferredCount;
        }
        if (decision.failedLookupByPolicy) {
            ++summary.policyFailedLookupCount;
        }
        if (decision.emptyFrequencyByPolicy) {
            ++summary.policyEmptyFrequencyCount;
        }
        if (decision.compatibilityDuplicateSuppressed) {
            ++summary.duplicateSuppressedCount;
        }
        if (decision.missingEvidenceByPolicy) {
            ++summary.missingEvidencePolicyCount;
        }
        if (decision.wouldLoseFrequencyIfBypassRemoved) {
            ++summary.wouldLoseFrequencyCount;
        }
        if (decision.wouldLoseVisibilityIfBypassRemoved) {
            ++summary.wouldLoseVisibilityCount;
        }
        if (decision.safeToRemoveBypassAfterCleanup) {
            ++summary.bypassRemovalSafeCandidateCount;
        } else {
            ++summary.bypassRemovalStillUnsafeCount;
            summary.ctafUnicomBypassRetirementReady = false;
        }
        if (decision.compatibilityOnly) {
            summary.completionBypassCompatibilityOnly = true;
        }
        if (decision.diagnosticCompatibilityOnly) {
            summary.diagnosticCompatibilityProjectionOnly = true;
        }
    }
    summary.bypassRetirementSafe =
        summary.completionBypassRetired &&
        summary.liveBypassAuthorityCount == 0 &&
        summary.duplicateLiveRowCount == 0 &&
        summary.standbyBypassConsumerCount == 0 &&
        summary.bypassRemovalStillUnsafeCount == 0;
    summary.noLiveBypassAuthority =
        summary.liveBypassAuthorityCount == 0;
    summary.compatibilityRowsDiagnosticOnly =
        summary.completionBypassRetired &&
        summary.liveBypassAuthorityCount == 0 &&
        summary.diagnosticBypassRowCount ==
            summary.bypassAuditDecisionCount;
    summary.liveRowsBrainAdvisoryOwned =
        summary.liveBypassAuthorityCount == 0 &&
        summary.brainOwnedAdvisoryRowCount ==
            summary.brainAdvisoryLiveRowCount;
    summary.standbyRowsAdvisoryOwned =
        summary.standbyBypassConsumerCount == 0;
    summary.legacyBypassFieldsQuarantined =
        summary.completionBypassRetired &&
        summary.noLiveBypassAuthority &&
        summary.compatibilityRowsDiagnosticOnly;
    return summary;
}

bool CtafUnicomAdvisoryIncompleteForAudit(
    const BrainOwnedCtafUnicomSourceEvidence* source,
    const BrainOwnedCtafUnicomProjectionEvidence& projection,
    const BrainOwnedCtafUnicomAdvisoryPreviewDecision* advisory) {
    if (source == nullptr || advisory == nullptr ||
        !projection.liveRowEmitted ||
        NormalizeFrequency(projection.projectedFrequency).empty()) {
        return false;
    }
    return advisory->endpoint.empty() ||
           advisory->airportIcao.empty() ||
           advisory->projectedRole.empty() ||
           advisory->projectedRole == "none" ||
           NormalizeFrequency(advisory->projectedFrequency).empty() ||
           !advisory->wouldEmitLiveRow;
}

std::string CtafUnicomMissingEvidenceCause(
    const BrainOwnedCtafUnicomSourceEvidence* source,
    const BrainOwnedCtafUnicomProjectionEvidence& projection,
    const BrainOwnedCtafUnicomAdvisoryPreviewDecision* advisory,
    bool missingSource,
    bool missingAdvisory,
    bool incompleteAdvisory) {
    if (missingSource) {
        return "missing-source-evidence";
    }
    if (missingAdvisory) {
        return "missing-advisory-decision";
    }
    if (!incompleteAdvisory || advisory == nullptr) {
        return "unknown";
    }
    if (advisory->endpoint != projection.endpoint) {
        return "endpoint-mismatch";
    }
    if (source != nullptr && advisory->airportIcao != source->airportIcao) {
        return "airport-mismatch";
    }
    if (!advisory->projectedFrequency.empty() &&
        NormalizeFrequency(advisory->projectedFrequency).empty()) {
        return "malformed-frequency";
    }
    if (NormalizeFrequency(advisory->projectedFrequency).empty()) {
        return "incomplete-advisory-decision";
    }
    return "unknown";
}

std::string CtafUnicomMissingEvidenceRecoveryHint(
    const std::string& cause) {
    if (cause == "missing-source-evidence") {
        return "restore-ctaf-unicom-source-evidence";
    }
    if (cause == "missing-advisory-decision") {
        return "restore-ctaf-unicom-advisory-decision";
    }
    if (cause == "endpoint-mismatch") {
        return "repair-advisory-endpoint";
    }
    if (cause == "airport-mismatch") {
        return "repair-advisory-airport";
    }
    if (cause == "malformed-frequency") {
        return "repair-advisory-frequency-format";
    }
    if (cause == "incomplete-advisory-decision") {
        return "repair-incomplete-advisory-decision";
    }
    return "inspect-ctaf-unicom-evidence";
}

bool CtafUnicomAuthorityInvariantPreserved(
    const BrainOwnedCtafUnicomAdvisoryAuthoritySummary& authority,
    const BrainOwnedCtafUnicomBypassAuditSummary& bypassSummary) {
    return authority.noLiveBypassAuthority &&
           authority.compatibilityRowsDiagnosticOnly &&
           authority.legacyBypassFieldsQuarantined &&
           bypassSummary.noLiveBypassAuthority &&
           bypassSummary.compatibilityRowsDiagnosticOnly &&
           bypassSummary.legacyBypassFieldsQuarantined &&
           bypassSummary.standbyBypassConsumerCount == 0;
}

BrainOwnedCtafUnicomMissingEvidenceAuditDecision
BuildCtafUnicomMissingEvidenceAuditDecision(
    const BrainOwnedCtafUnicomSourceEvidence* source,
    const BrainOwnedCtafUnicomProjectionEvidence& projection,
    const BrainOwnedCtafUnicomAdvisoryPreviewDecision* advisory,
    const BrainOwnedCtafUnicomAdvisoryAuthoritySummary& authoritySummary,
    const BrainOwnedCtafUnicomBypassAuditSummary& bypassSummary) {
    BrainOwnedCtafUnicomMissingEvidenceAuditDecision decision;
    decision.missingEvidenceEndpoint =
        !projection.endpoint.empty()
            ? projection.endpoint
            : (advisory != nullptr ? advisory->endpoint
                                   : (source != nullptr ? source->endpoint
                                                        : std::string()));
    decision.missingEvidenceAirportIcao =
        source != nullptr
            ? source->airportIcao
            : (advisory != nullptr
                   ? advisory->airportIcao
                   : projection.airportIcao);
    decision.missingEvidenceRole =
        advisory != nullptr && !advisory->projectedRole.empty()
            ? advisory->projectedRole
            : projection.projectedRole;
    decision.missingEvidenceFrequency =
        advisory != nullptr && !advisory->projectedFrequency.empty()
            ? advisory->projectedFrequency
            : projection.projectedFrequency;
    decision.missingSourceEvidence = source == nullptr;
    decision.missingAdvisoryDecision =
        projection.liveRowEmitted && advisory == nullptr;
    decision.incompleteAdvisoryDecision =
        CtafUnicomAdvisoryIncompleteForAudit(source, projection, advisory);
    decision.oldCompatibilityWouldDisplay = projection.liveRowEmitted;
    decision.missingEvidenceCause =
        CtafUnicomMissingEvidenceCause(
            source,
            projection,
            advisory,
            decision.missingSourceEvidence,
            decision.missingAdvisoryDecision,
            decision.incompleteAdvisoryDecision);
    decision.warningOnly = true;
    decision.warningReason = decision.missingEvidenceCause;
    decision.warningLabel = "missing-evidence-warning-only";
    decision.recoveryHint =
        CtafUnicomMissingEvidenceRecoveryHint(
            decision.missingEvidenceCause);
    const auto projectionFrequencyUsable =
        !NormalizeFrequency(projection.projectedFrequency).empty();
    const auto advisoryWouldDisplay =
        advisory != nullptr && advisory->wouldEmitLiveRow &&
        !NormalizeFrequency(advisory->projectedFrequency).empty();
    decision.wouldLoseFrequency =
        projection.liveRowEmitted &&
        projectionFrequencyUsable &&
        !advisoryWouldDisplay;
    decision.wouldLoseVisibility =
        projection.liveRowEmitted &&
        !advisoryWouldDisplay;
    decision.liveAuthorityRestored = false;
    decision.liveCompatibilityFallbackUsed = false;
    decision.standbyConsumesWarning = false;
    decision.standbyWriteBlockedByMissingEvidence =
        decision.warningOnly && !advisoryWouldDisplay;
    decision.authorityInvariantPreserved =
        CtafUnicomAuthorityInvariantPreserved(
            authoritySummary,
            bypassSummary);
    decision.failSoftVisible = decision.warningOnly;
    decision.operatorActionRequired =
        decision.warningOnly &&
        (decision.wouldLoseFrequency ||
         decision.wouldLoseVisibility ||
         decision.missingSourceEvidence ||
         decision.missingAdvisoryDecision ||
         decision.incompleteAdvisoryDecision);
    decision.missingEvidenceAuditDecisionId =
        "ctaf-unicom-missing-evidence-audit:" +
        decision.missingEvidenceEndpoint + ":" +
        (decision.missingEvidenceAirportIcao.empty()
             ? std::string("unknown")
             : decision.missingEvidenceAirportIcao);
    return decision;
}

std::vector<BrainOwnedCtafUnicomMissingEvidenceAuditDecision>
BuildCtafUnicomMissingEvidenceAuditDecisions(
    const std::vector<BrainOwnedCtafUnicomSourceEvidence>& sourceEvidence,
    const std::vector<BrainOwnedCtafUnicomProjectionEvidence>& projectionEvidence,
    const std::vector<BrainOwnedCtafUnicomAdvisoryPreviewDecision>& advisoryDecisions,
    const BrainOwnedCtafUnicomAdvisoryAuthoritySummary& authoritySummary,
    const BrainOwnedCtafUnicomBypassAuditSummary& bypassSummary) {
    std::vector<BrainOwnedCtafUnicomMissingEvidenceAuditDecision> decisions;
    for (const auto& projection : projectionEvidence) {
        const auto* source = FindCtafUnicomSourceEvidenceById(
            sourceEvidence,
            projection.sourceEvidenceId,
            projection.endpoint);
        const auto* advisory = FindCtafUnicomAdvisoryPreviewDecision(
            advisoryDecisions,
            projection.sourceEvidenceId,
            projection.endpoint);
        const auto missingSource = source == nullptr;
        const auto missingAdvisory =
            projection.liveRowEmitted && advisory == nullptr;
        const auto incompleteAdvisory =
            CtafUnicomAdvisoryIncompleteForAudit(
                source,
                projection,
                advisory);
        if (!missingSource && !missingAdvisory && !incompleteAdvisory) {
            continue;
        }
        decisions.push_back(
            BuildCtafUnicomMissingEvidenceAuditDecision(
                source,
                projection,
                advisory,
                authoritySummary,
                bypassSummary));
    }
    return decisions;
}

BrainOwnedCtafUnicomMissingEvidenceAuditSummary
BuildCtafUnicomMissingEvidenceAuditSummary(
    const std::vector<BrainOwnedCtafUnicomMissingEvidenceAuditDecision>& decisions) {
    BrainOwnedCtafUnicomMissingEvidenceAuditSummary summary;
    summary.missingEvidenceAuditCount = static_cast<int>(decisions.size());
    for (const auto& decision : decisions) {
        if (decision.missingSourceEvidence) {
            ++summary.missingSourceEvidenceCount;
        }
        if (decision.missingAdvisoryDecision) {
            ++summary.missingAdvisoryDecisionCount;
        }
        if (decision.incompleteAdvisoryDecision) {
            ++summary.incompleteAdvisoryDecisionCount;
        }
        if (decision.oldCompatibilityWouldDisplay) {
            ++summary.oldCompatibilityWouldDisplayCount;
        }
        if (decision.wouldLoseFrequency) {
            ++summary.wouldLoseFrequencyCount;
        }
        if (decision.wouldLoseVisibility) {
            ++summary.wouldLoseVisibilityCount;
        }
        if (decision.warningOnly) {
            ++summary.warningOnlyCount;
        }
        if (decision.liveAuthorityRestored) {
            ++summary.liveAuthorityRestoredCount;
        }
        if (decision.liveCompatibilityFallbackUsed) {
            ++summary.liveCompatibilityFallbackUsedCount;
        }
        if (decision.standbyConsumesWarning) {
            ++summary.standbyConsumesWarningCount;
        }
        if (decision.authorityInvariantPreserved) {
            ++summary.authorityInvariantPreservedCount;
        }
        if (decision.operatorActionRequired) {
            ++summary.operatorActionRequiredCount;
        }
    }
    return summary;
}

BrainOwnedCtafUnicomLegacyBypassAliasAuditDecision
BuildCtafUnicomLegacyBypassAliasAuditDecision(
    std::string aliasName,
    std::string aliasLocation,
    std::string aliasCategory,
    std::string currentMeaning,
    std::string misleadingRisk,
    std::string recommendedAction,
    std::string migrationTarget,
    bool consumerKnown,
    std::string consumerRisk,
    bool canRenameNow,
    bool canRemoveNow,
    std::string removalBlockedReason,
    bool authorityInvariantProtected,
    bool liveAuthorityImplication) {
    BrainOwnedCtafUnicomLegacyBypassAliasAuditDecision decision;
    decision.aliasName = std::move(aliasName);
    decision.aliasLocation = std::move(aliasLocation);
    decision.aliasCategory = std::move(aliasCategory);
    decision.currentMeaning = std::move(currentMeaning);
    decision.misleadingRisk = std::move(misleadingRisk);
    decision.recommendedAction = std::move(recommendedAction);
    decision.migrationTarget = std::move(migrationTarget);
    decision.consumerKnown = consumerKnown;
    decision.consumerRisk = std::move(consumerRisk);
    decision.canRenameNow = canRenameNow;
    decision.canRemoveNow = canRemoveNow;
    decision.removalBlockedReason = std::move(removalBlockedReason);
    decision.authorityInvariantProtected = authorityInvariantProtected;
    decision.liveAuthorityImplication = liveAuthorityImplication;
    decision.legacyAliasAuditId =
        "ctaf-unicom-legacy-alias:" + decision.aliasName;
    decision.replacementFieldName = decision.migrationTarget;
    decision.replacementFieldPresent =
        !decision.migrationTarget.empty() && decision.migrationTarget != "none";
    decision.legacyFieldStillPresent = true;
    decision.replacementMatchesLegacy = decision.replacementFieldPresent;
    decision.harnessMigratedToReplacement =
        decision.replacementFieldPresent &&
        decision.consumerRisk != "report-only";
    decision.oldAliasDeprecated =
        decision.recommendedAction == "rename-later" ||
        decision.recommendedAction == "remove-later";
    decision.safeToRemoveLegacyLater =
        decision.recommendedAction == "remove-later";
    decision.replacementMigrationComplete =
        !decision.replacementFieldPresent ||
        (decision.replacementMatchesLegacy &&
         (decision.harnessMigratedToReplacement ||
          decision.consumerRisk == "report-only"));
    if (decision.replacementFieldPresent &&
        !decision.replacementMatchesLegacy) {
        decision.replacementMismatchReason = "replacement-mismatch";
    }
    return decision;
}

std::vector<BrainOwnedCtafUnicomLegacyBypassAliasAuditDecision>
BuildCtafUnicomLegacyBypassAliasAuditDecisions() {
    using Decision = BrainOwnedCtafUnicomLegacyBypassAliasAuditDecision;
    std::vector<Decision> decisions;
    decisions.reserve(22);
    auto add = [&](std::string aliasName,
                   std::string aliasLocation,
                   std::string aliasCategory,
                   std::string currentMeaning,
                   std::string misleadingRisk,
                   std::string recommendedAction,
                   std::string migrationTarget,
                   bool consumerKnown,
                   std::string consumerRisk,
                   bool canRenameNow,
                   bool canRemoveNow,
                   std::string removalBlockedReason,
                   bool authorityInvariantProtected,
                   bool liveAuthorityImplication) {
        decisions.push_back(
            BuildCtafUnicomLegacyBypassAliasAuditDecision(
                std::move(aliasName),
                std::move(aliasLocation),
                std::move(aliasCategory),
                std::move(currentMeaning),
                std::move(misleadingRisk),
                std::move(recommendedAction),
                std::move(migrationTarget),
                consumerKnown,
                std::move(consumerRisk),
                canRenameNow,
                canRemoveNow,
                std::move(removalBlockedReason),
                authorityInvariantProtected,
                liveAuthorityImplication));
    };

    add("bypassRequired",
        "brain-runtime;harness-output;harness-expectations",
        "per-decision-field",
        "old-compatibility-projection-would-have-displayed",
        "high",
        "rename-later",
        "diagnosticCompatibilityWouldDisplay",
        true,
        "public-consumer-risk",
        false,
        false,
        "harness-and-report-consumers",
        true,
        true);
    add("compatibilityOnly",
        "brain-runtime;harness-output;harness-expectations",
        "per-decision-field",
        "diagnostic-compatibility-projection-only",
        "high",
        "rename-later",
        "diagnosticCompatibilityOnly",
        true,
        "public-consumer-risk",
        false,
        false,
        "harness-and-report-consumers",
        true,
        true);
    add("compatibility-fallback-warning",
        "brain-runtime-warning-label;reports",
        "warning-label",
        "missing-evidence-warning-only",
        "high",
        "rename-later",
        "missing-evidence-warning-only",
        true,
        "public-consumer-risk",
        false,
        false,
        "saved-report-and-harness-wording",
        true,
        true);
    add("liveRowEmitted",
        "brain-runtime-projection-field",
        "internal-field",
        "historical-projection-would-have-emitted-row",
        "high",
        "rename-later",
        "legacyDiagnosticLiveRowEmitted",
        false,
        "unknown-consumer-risk",
        false,
        false,
        "public-header-field",
        true,
        true);
    add("legacyDiagnosticBypassRows",
        "harness-output;harness-expectations",
        "harness-only",
        "diagnostic-bypass-row-count",
        "low",
        "keep-current-name",
        "none",
        true,
        "harness-only",
        false,
        false,
        "not-blocking",
        true,
        false);
    add("legacyDiagnosticBypassFlag",
        "harness-output;harness-expectations",
        "harness-only",
        "diagnostic-bypass-compatibility-flag",
        "low",
        "keep-current-name",
        "none",
        true,
        "harness-only",
        false,
        false,
        "not-blocking",
        true,
        false);
    add("legacyDiagnosticLiveRows",
        "harness-output;harness-expectations",
        "harness-only",
        "diagnostic-live-row-count-from-historical-projection",
        "low",
        "keep-current-name",
        "none",
        true,
        "harness-only",
        false,
        false,
        "not-blocking",
        true,
        false);
    add("legacyDiagnosticLiveRowEmitted",
        "harness-output;harness-expectations",
        "harness-only",
        "diagnostic-row-emitted-flag-from-historical-projection",
        "low",
        "keep-current-name",
        "none",
        true,
        "harness-only",
        false,
        false,
        "not-blocking",
        true,
        false);
    add("historicalCompatibilityRows",
        "harness-output;harness-expectations",
        "harness-only",
        "historical-compatibility-projection-count",
        "low",
        "keep-current-name",
        "none",
        true,
        "harness-only",
        false,
        false,
        "not-blocking",
        true,
        false);
    add("diagnosticBypassRows",
        "harness-output;harness-expectations",
        "harness-only",
        "diagnostic-only-bypass-row-count",
        "low",
        "keep-current-name",
        "none",
        true,
        "harness-only",
        false,
        false,
        "not-blocking",
        true,
        false);
    add("liveBypassAuthority",
        "harness-output;harness-expectations",
        "authority-invariant-field",
        "count-of-live-bypass-authority-records",
        "low",
        "keep-current-name",
        "none",
        true,
        "harness-only",
        false,
        false,
        "not-blocking",
        true,
        false);
    add("bypassLiveAuthority",
        "harness-output;harness-expectations",
        "per-decision-authority-field",
        "per-decision-live-bypass-authority-flag",
        "low",
        "keep-current-name",
        "none",
        true,
        "harness-only",
        false,
        false,
        "not-blocking",
        true,
        false);
    add("bypassDiagnosticOnly",
        "harness-output;harness-expectations",
        "per-decision-authority-field",
        "per-decision-diagnostic-only-bypass-flag",
        "low",
        "keep-current-name",
        "none",
        true,
        "harness-only",
        false,
        false,
        "not-blocking",
        true,
        false);
    add("bypassReason",
        "brain-runtime;harness-output;harness-expectations",
        "per-decision-reason-field",
        "diagnostic-bypass-or-retirement-reason",
        "medium",
        "rename-later",
        "diagnosticCompatibilityReason",
        true,
        "public-consumer-risk",
        false,
        false,
        "harness-and-report-consumers",
        true,
        true);
    add("completionBypassCompatibilityOnly",
        "brain-runtime-summary-field",
        "internal-field",
        "compatibility-projection-is-diagnostic-only",
        "medium",
        "rename-later",
        "diagnosticCompatibilityProjectionOnly",
        false,
        "unknown-consumer-risk",
        false,
        false,
        "public-header-field",
        true,
        true);
    add("compatibilityRowsDiagnosticOnly",
        "harness-output;harness-expectations",
        "authority-invariant-field",
        "compatibility-rows-have-no-live-authority",
        "low",
        "keep-current-name",
        "none",
        true,
        "harness-only",
        false,
        false,
        "not-blocking",
        true,
        false);
    add("legacyBypassFieldsQuarantined",
        "harness-output;harness-expectations",
        "authority-invariant-field",
        "legacy-bypass-fields-are-diagnostic-only",
        "low",
        "keep-current-name",
        "none",
        true,
        "harness-only",
        false,
        false,
        "not-blocking",
        true,
        false);
    add("liveRowAuthority",
        "harness-output;harness-expectations",
        "authority-invariant-field",
        "actual-live-row-authority-source",
        "low",
        "keep-current-name",
        "none",
        true,
        "harness-only",
        false,
        false,
        "not-blocking",
        true,
        false);
    add("legacyCompatibilityOnly",
        "harness-output;harness-expectations",
        "harness-only",
        "legacy-name-for-compatibility-diagnostic-only-status",
        "medium",
        "rename-later",
        "compatibilityRowsDiagnosticOnly",
        true,
        "harness-only",
        false,
        false,
        "harness-expectations",
        true,
        true);
    add("fallbackWarnings",
        "harness-output;harness-expectations",
        "harness-only",
        "compatibility-fallback-warning-count",
        "medium",
        "rename-later",
        "missingEvidenceWarningCount",
        true,
        "harness-only",
        false,
        false,
        "harness-expectations",
        true,
        true);
    add("missingEvidenceFallback",
        "harness-output;harness-expectations",
        "harness-only",
        "missing-evidence-warning-preserved-flag",
        "medium",
        "rename-later",
        "missingEvidenceWarningOnly",
        true,
        "harness-only",
        false,
        false,
        "harness-expectations",
        true,
        true);
    add("retiredCompatibilityRows",
        "harness-output;harness-expectations",
        "harness-only",
        "retired-diagnostic-compatibility-row-count",
        "low",
        "keep-current-name",
        "none",
        true,
        "harness-only",
        false,
        false,
        "not-blocking",
        true,
        false);

    return decisions;
}

BrainOwnedCtafUnicomLegacyBypassAliasAuditSummary
BuildCtafUnicomLegacyBypassAliasAuditSummary(
    const std::vector<BrainOwnedCtafUnicomLegacyBypassAliasAuditDecision>& decisions) {
    BrainOwnedCtafUnicomLegacyBypassAliasAuditSummary summary;
    summary.aliasAuditCount = static_cast<int>(decisions.size());
    summary.replacementMigrationComplete = true;
    summary.reportOnlyAliasRemovedCount = 3;
    summary.reportOnlyAliasRemovalSafeCount = 3;
    summary.reportOnlyAliasStillFoundCount = 0;
    for (const auto& decision : decisions) {
        if (decision.canRenameNow) {
            ++summary.renameNowCandidateCount;
        }
        if (decision.recommendedAction == "rename-later") {
            ++summary.renameLaterCount;
        }
        if (decision.recommendedAction == "remove-later") {
            ++summary.removeLaterCount;
        }
        if (decision.consumerRisk == "harness-only") {
            ++summary.harnessOnlyAliasCount;
        }
        if (decision.consumerRisk == "report-only") {
            ++summary.reportOnlyAliasCount;
        }
        if (decision.consumerRisk == "public-consumer-risk") {
            ++summary.publicConsumerRiskCount;
        }
        if (decision.consumerRisk == "unknown-consumer-risk") {
            ++summary.unknownConsumerRiskCount;
        }
        if (decision.liveAuthorityImplication) {
            ++summary.liveAuthorityMisleadingAliasCount;
        }
        if (decision.authorityInvariantProtected) {
            ++summary.authorityInvariantProtectedCount;
        }
        if (decision.replacementFieldPresent) {
            ++summary.replacementFieldCount;
        }
        if (decision.legacyFieldStillPresent) {
            ++summary.legacyFieldStillPresentCount;
        }
        if (decision.replacementMatchesLegacy) {
            ++summary.replacementMatchesLegacyCount;
        }
        if (decision.replacementFieldPresent &&
            !decision.replacementMatchesLegacy) {
            ++summary.replacementMismatchCount;
        }
        if (decision.harnessMigratedToReplacement) {
            ++summary.harnessMigratedToReplacementCount;
        }
        if (decision.oldAliasDeprecated) {
            ++summary.deprecatedAliasCount;
        }
        if (decision.safeToRemoveLegacyLater) {
            ++summary.safeToRemoveLegacyLaterCount;
        }
        if (!decision.replacementMigrationComplete) {
            summary.replacementMigrationComplete = false;
        }
    }
    return summary;
}

BrainOwnedCtafUnicomPublicUnknownAliasConsumerAuditDecision
BuildCtafUnicomPublicUnknownAliasConsumerAuditDecision(
    std::string aliasName,
    std::string replacementName,
    std::string definitionLocation,
    std::string emissionLocation,
    int harnessUsageCount,
    int reportUsageCount,
    int runtimeUsageCount,
    int docsUsageCount,
    int pluginUsageCount,
    bool externalConsumerRisk,
    bool unknownConsumerRisk,
    bool removalReadyLater,
    std::string removalBlockedReason,
    std::string nextMigrationAction) {
    BrainOwnedCtafUnicomPublicUnknownAliasConsumerAuditDecision decision;
    decision.consumerAliasName = std::move(aliasName);
    decision.replacementName = std::move(replacementName);
    decision.definitionLocation = std::move(definitionLocation);
    decision.emissionLocation = std::move(emissionLocation);
    decision.harnessUsageCount = harnessUsageCount;
    decision.reportUsageCount = reportUsageCount;
    decision.runtimeUsageCount = runtimeUsageCount;
    decision.docsUsageCount = docsUsageCount;
    decision.pluginUsageCount = pluginUsageCount;
    decision.externalConsumerRisk = externalConsumerRisk;
    decision.unknownConsumerRisk = unknownConsumerRisk;
    decision.replacementEmittedSameScope = true;
    decision.internalConsumersMigrated = true;
    decision.aliasCompatibilityOnly = true;
    decision.removalReadyLater = removalReadyLater;
    decision.removalBlockedReason = std::move(removalBlockedReason);
    decision.nextMigrationAction = std::move(nextMigrationAction);
    return decision;
}

std::vector<BrainOwnedCtafUnicomPublicUnknownAliasConsumerAuditDecision>
BuildCtafUnicomPublicUnknownAliasConsumerAuditDecisions() {
    std::vector<BrainOwnedCtafUnicomPublicUnknownAliasConsumerAuditDecision>
        decisions;
    decisions.reserve(6);
    auto add = [&](std::string aliasName,
                   std::string replacementName,
                   std::string definitionLocation,
                   std::string emissionLocation,
                   int harnessUsageCount,
                   int reportUsageCount,
                   int runtimeUsageCount,
                   int docsUsageCount,
                   int pluginUsageCount,
                   bool externalConsumerRisk,
                   bool unknownConsumerRisk,
                   bool removalReadyLater,
                   std::string removalBlockedReason,
                   std::string nextMigrationAction) {
        decisions.push_back(
            BuildCtafUnicomPublicUnknownAliasConsumerAuditDecision(
                std::move(aliasName),
                std::move(replacementName),
                std::move(definitionLocation),
                std::move(emissionLocation),
                harnessUsageCount,
                reportUsageCount,
                runtimeUsageCount,
                docsUsageCount,
                pluginUsageCount,
                externalConsumerRisk,
                unknownConsumerRisk,
                removalReadyLater,
                std::move(removalBlockedReason),
                std::move(nextMigrationAction)));
    };

    add("bypassRequired",
        "diagnosticCompatibilityWouldDisplay",
        "BrainOwnedCtafUnicomBypassAuditDecision",
        "CtafUnicomBypassAuditDecisionSummaries",
        11,
        6,
        14,
        0,
        0,
        true,
        false,
        true,
        "external-consumer-compatibility-window",
        "remove-after-downstream-expectations-use-replacement");
    add("compatibilityOnly",
        "diagnosticCompatibilityOnly",
        "BrainOwnedCtafUnicomBypassAuditDecision",
        "CtafUnicomBypassAuditDecisionSummaries",
        8,
        6,
        6,
        0,
        0,
        true,
        false,
        true,
        "external-consumer-compatibility-window",
        "remove-after-downstream-expectations-use-replacement");
    add("compatibility-fallback-warning",
        "missing-evidence-warning-only",
        "BrainOwnedRuntime warning label",
        "diagnosticLiveRowAuthority and warningLabel",
        3,
        6,
        3,
        0,
        0,
        true,
        false,
        true,
        "external-consumer-compatibility-window",
        "remove-after-warning-consumers-use-missing-evidence-warning-only");
    add("bypassReason",
        "diagnosticCompatibilityReason",
        "BrainOwnedCtafUnicomBypassAuditDecision",
        "CtafUnicomBypassAuditDecisionSummaries",
        8,
        6,
        7,
        0,
        0,
        true,
        false,
        true,
        "external-consumer-compatibility-window",
        "remove-after-downstream-expectations-use-replacement");
    add("liveRowEmitted",
        "legacyDiagnosticLiveRowEmitted",
        "BrainOwnedCtafUnicomProjectionEvidence",
        "CtafUnicomProjectionEvidenceSummaries",
        1,
        9,
        19,
        0,
        0,
        false,
        true,
        false,
        "unknown-consumer-public-header-field",
        "prove-no-public-header-consumer-before-removal");
    add("completionBypassCompatibilityOnly",
        "diagnosticCompatibilityProjectionOnly",
        "BrainOwnedCtafUnicomProjectionEvidence and summaries",
        "CtafUnicomEvidence/Preview/Authority/Bypass summaries",
        2,
        11,
        21,
        0,
        0,
        false,
        true,
        false,
        "unknown-consumer-public-header-field",
        "prove-no-public-header-consumer-before-removal");
    return decisions;
}

BrainOwnedCtafUnicomPublicUnknownAliasConsumerAuditSummary
BuildCtafUnicomPublicUnknownAliasConsumerAuditSummary(
    const std::vector<BrainOwnedCtafUnicomPublicUnknownAliasConsumerAuditDecision>& decisions) {
    BrainOwnedCtafUnicomPublicUnknownAliasConsumerAuditSummary summary;
    summary.publicUnknownAliasCount = static_cast<int>(decisions.size());
    for (const auto& decision : decisions) {
        if (decision.replacementEmittedSameScope) {
            ++summary.replacementSameScopeCount;
        }
        if (decision.internalConsumersMigrated) {
            ++summary.internalMigratedCount;
        }
        if (decision.aliasCompatibilityOnly) {
            ++summary.compatibilityOnlyAliasCount;
        }
        if (decision.removalReadyLater) {
            ++summary.removalReadyLaterCount;
        } else {
            ++summary.removalBlockedCount;
        }
        if (decision.externalConsumerRisk) {
            ++summary.externalRiskCount;
        }
        if (decision.unknownConsumerRisk) {
            ++summary.unknownRiskCount;
        }
        summary.runtimeUsageCount += decision.runtimeUsageCount;
        summary.harnessLegacyUsageCount += decision.harnessUsageCount;
        summary.reportLegacyUsageCount += decision.reportUsageCount;
    }
    return summary;
}

BrainOwnedCtafUnicomExternalAliasDeprecationDecision
BuildCtafUnicomExternalAliasDeprecationDecision(
    std::string aliasName,
    std::string replacementName,
    std::string aliasRiskClass,
    std::string deprecationStatus,
    bool activeGeneratedAliasPresent,
    bool aliasRemovedFromActiveOutput,
    bool aliasDeprecated,
    bool publicHeaderRiskAliasRetained,
    std::string removalBlockedReason,
    std::string nextMigrationAction) {
    BrainOwnedCtafUnicomExternalAliasDeprecationDecision decision;
    decision.aliasName = std::move(aliasName);
    decision.replacementName = std::move(replacementName);
    decision.aliasRiskClass = std::move(aliasRiskClass);
    decision.deprecationStatus = std::move(deprecationStatus);
    decision.activeGeneratedAliasPresent = activeGeneratedAliasPresent;
    decision.aliasRemovedFromActiveOutput = aliasRemovedFromActiveOutput;
    decision.aliasDeprecated = aliasDeprecated;
    decision.canonicalReplacementPreferred = true;
    decision.replacementUsedByHarness = true;
    decision.replacementCarriesEquivalentMeaning = true;
    decision.authorityInvariantProtected = true;
    decision.liveAuthorityImplication = true;
    decision.publicHeaderRiskAliasRetained = publicHeaderRiskAliasRetained;
    decision.runtimeBehaviorChanged = false;
    decision.removalBlockedReason = std::move(removalBlockedReason);
    decision.nextMigrationAction = std::move(nextMigrationAction);
    return decision;
}

std::vector<BrainOwnedCtafUnicomExternalAliasDeprecationDecision>
BuildCtafUnicomExternalAliasDeprecationDecisions() {
    std::vector<BrainOwnedCtafUnicomExternalAliasDeprecationDecision> decisions;
    decisions.reserve(6);
    auto add = [&](std::string aliasName,
                   std::string replacementName,
                   std::string aliasRiskClass,
                   std::string deprecationStatus,
                   bool activeGeneratedAliasPresent,
                   bool aliasRemovedFromActiveOutput,
                   bool aliasDeprecated,
                   bool publicHeaderRiskAliasRetained,
                   std::string removalBlockedReason,
                   std::string nextMigrationAction) {
        decisions.push_back(
            BuildCtafUnicomExternalAliasDeprecationDecision(
                std::move(aliasName),
                std::move(replacementName),
                std::move(aliasRiskClass),
                std::move(deprecationStatus),
                activeGeneratedAliasPresent,
                aliasRemovedFromActiveOutput,
                aliasDeprecated,
                publicHeaderRiskAliasRetained,
                std::move(removalBlockedReason),
                std::move(nextMigrationAction)));
    };

    add("bypassRequired",
        "diagnosticCompatibilityWouldDisplay",
        "external-risk",
        "deprecated-active-output",
        true,
        false,
        true,
        false,
        "active-output-compatibility-window",
        "remove-after-downstream-output-consumers-confirm-replacement");
    add("compatibilityOnly",
        "diagnosticCompatibilityOnly",
        "external-risk",
        "deprecated-active-output",
        true,
        false,
        true,
        false,
        "active-output-compatibility-window",
        "remove-after-downstream-output-consumers-confirm-replacement");
    add("compatibility-fallback-warning",
        "missing-evidence-warning-only",
        "external-risk",
        "removed-from-active-warning-output",
        false,
        true,
        true,
        false,
        "removed-from-active-generated-warning-output",
        "keep-in-alias-audits-until-historical-consumers-migrate");
    add("bypassReason",
        "diagnosticCompatibilityReason",
        "external-risk",
        "deprecated-active-output",
        true,
        false,
        true,
        false,
        "active-output-compatibility-window",
        "remove-after-downstream-output-consumers-confirm-replacement");
    add("liveRowEmitted",
        "legacyDiagnosticLiveRowEmitted",
        "unknown-public-header-risk",
        "retained-unknown-public-header-risk",
        true,
        false,
        false,
        true,
        "unknown-consumer-public-header-field",
        "prove-no-public-header-consumer-before-removal");
    add("completionBypassCompatibilityOnly",
        "diagnosticCompatibilityProjectionOnly",
        "unknown-public-header-risk",
        "retained-unknown-public-header-risk",
        true,
        false,
        false,
        true,
        "unknown-consumer-public-header-field",
        "prove-no-public-header-consumer-before-removal");
    return decisions;
}

BrainOwnedCtafUnicomExternalAliasDeprecationSummary
BuildCtafUnicomExternalAliasDeprecationSummary(
    const std::vector<BrainOwnedCtafUnicomExternalAliasDeprecationDecision>& decisions) {
    BrainOwnedCtafUnicomExternalAliasDeprecationSummary summary;
    summary.aliasDeprecationDecisionCount = static_cast<int>(decisions.size());
    summary.noLiveBypassAuthority = true;
    for (const auto& decision : decisions) {
        if (decision.aliasRiskClass == "external-risk") {
            ++summary.externalRiskAliasCount;
            if (decision.aliasDeprecated) {
                ++summary.externalAliasDeprecatedCount;
            }
            if (decision.aliasRemovedFromActiveOutput) {
                ++summary.externalAliasRemovedCount;
            }
        }
        if (decision.activeGeneratedAliasPresent) {
            ++summary.activeGeneratedAliasRetainedCount;
        }
        if (decision.canonicalReplacementPreferred) {
            ++summary.canonicalReplacementPreferredCount;
        }
        if (decision.replacementCarriesEquivalentMeaning) {
            ++summary.replacementEquivalentCount;
        }
        if (decision.publicHeaderRiskAliasRetained) {
            ++summary.publicHeaderRiskAliasRetainedCount;
        }
        if (decision.aliasName == "liveRowEmitted" &&
            decision.activeGeneratedAliasPresent) {
            summary.liveRowEmittedRetained = true;
        }
        if (decision.aliasName == "completionBypassCompatibilityOnly" &&
            decision.activeGeneratedAliasPresent) {
            summary.completionBypassCompatibilityOnlyRetained = true;
        }
        if (decision.runtimeBehaviorChanged) {
            summary.runtimeBehaviorChanged = true;
        }
    }
    return summary;
}

BrainOwnedCtafUnicomPublicHeaderAliasRiskClosureDecision
BuildCtafUnicomPublicHeaderAliasRiskClosureDecision(
    std::string publicHeaderAliasName,
    std::string replacementName,
    std::string headerDefinitionLocation,
    std::string runtimeWriteLocation,
    std::string harnessOutputLocation,
    int harnessExpectationUsageCount,
    int pluginUsageCount,
    int moduleUsageCount,
    int docsUsageCount,
    int reportUsageCount,
    bool canDeprecateNow,
    bool canRemoveLater,
    std::string removalBlockedReason,
    std::string recommendedAction,
    std::string nextMigrationStep) {
    BrainOwnedCtafUnicomPublicHeaderAliasRiskClosureDecision decision;
    decision.publicHeaderAliasName = std::move(publicHeaderAliasName);
    decision.deprecatedAliasName = decision.publicHeaderAliasName;
    decision.replacementName = std::move(replacementName);
    decision.headerDefinitionLocation = std::move(headerDefinitionLocation);
    decision.runtimeWriteLocation = std::move(runtimeWriteLocation);
    decision.harnessOutputLocation = std::move(harnessOutputLocation);
    decision.harnessExpectationUsageCount = harnessExpectationUsageCount;
    decision.pluginUsageCount = pluginUsageCount;
    decision.moduleUsageCount = moduleUsageCount;
    decision.docsUsageCount = docsUsageCount;
    decision.reportUsageCount = reportUsageCount;
    decision.replacementSameScope = true;
    decision.replacementMatchesLegacy = true;
    decision.compatibilityOnly = true;
    decision.deprecatedPublicHeaderAliasRetained = true;
    decision.deprecatedAliasStillEmitted = true;
    decision.replacementPreferred = true;
    decision.replacementMatchesDeprecatedAlias = true;
    decision.canDeprecateNow = canDeprecateNow;
    decision.canRemoveLater = canRemoveLater;
    decision.removalBlockedReason = std::move(removalBlockedReason);
    decision.publicHeaderConsumerRisk = true;
    decision.externalConsumerRisk = false;
    decision.recommendedAction = std::move(recommendedAction);
    decision.nextMigrationStep = std::move(nextMigrationStep);
    return decision;
}

std::vector<BrainOwnedCtafUnicomPublicHeaderAliasRiskClosureDecision>
BuildCtafUnicomPublicHeaderAliasRiskClosureDecisions() {
    std::vector<BrainOwnedCtafUnicomPublicHeaderAliasRiskClosureDecision>
        decisions;
    decisions.reserve(2);
    decisions.push_back(
        BuildCtafUnicomPublicHeaderAliasRiskClosureDecision(
            "liveRowEmitted",
            "legacyDiagnosticLiveRowEmitted",
            "BrainOwnedCtafUnicomProjectionEvidence",
            "BuildCtafUnicomProjectionEvidence;BuildCtafUnicomEvidenceSummary;advisory-parity-audits",
            "CtafUnicomProjectionEvidenceSummaries;CtafUnicomEvidenceSummaryText",
            4,
            0,
            0,
            1,
            16,
            true,
            false,
            "public-header-consumer-risk-not-yet-cleared",
            "retain-now-deprecate-next",
            "mark-deprecated-after-public-header-consumer-notice"));
    decisions.push_back(
        BuildCtafUnicomPublicHeaderAliasRiskClosureDecision(
            "completionBypassCompatibilityOnly",
            "diagnosticCompatibilityProjectionOnly",
            "BrainOwnedCtafUnicomProjectionEvidence;BrainOwnedCtafUnicomEvidenceSummary;BrainOwnedCtafUnicomAdvisoryPreviewSummary;BrainOwnedCtafUnicomAdvisoryAuthoritySummary;BrainOwnedCtafUnicomBypassAuditSummary",
            "BuildCtafUnicomProjectionEvidence;CTAF/UNICOM evidence;preview;authority;and-bypass-summaries",
            "CtafUnicomEvidenceSummaryText;CtafUnicomProjectionEvidenceSummaries;CtafUnicomAdvisoryPreviewSummaryText;CtafUnicomAdvisoryAuthoritySummaryText;CtafUnicomBypassAuditSummaryText",
            5,
            0,
            0,
            1,
            18,
            true,
            false,
            "public-header-consumer-risk-not-yet-cleared",
            "retain-now-deprecate-next",
            "mark-deprecated-after-public-header-consumer-notice"));
    return decisions;
}

BrainOwnedCtafUnicomPublicHeaderAliasRiskClosureSummary
BuildCtafUnicomPublicHeaderAliasRiskClosureSummary(
    const std::vector<BrainOwnedCtafUnicomPublicHeaderAliasRiskClosureDecision>& decisions) {
    BrainOwnedCtafUnicomPublicHeaderAliasRiskClosureSummary summary;
    summary.publicHeaderAliasCount = static_cast<int>(decisions.size());
    summary.deprecatedAliasDocumentationPresent = true;
    summary.publicHeaderCompatibilityWindowOpen = true;
    summary.ctafUnicomAliasCleanupClosedExceptCompatibilityWindow = true;
    for (const auto& decision : decisions) {
        if (decision.replacementSameScope) {
            ++summary.replacementSameScopeCount;
        }
        if (decision.replacementMatchesLegacy) {
            ++summary.replacementMatchesLegacyCount;
        }
        if (decision.compatibilityOnly) {
            ++summary.compatibilityOnlyCount;
        }
        if (!decision.deprecatedAliasName.empty()) {
            ++summary.deprecatedPublicHeaderAliasCount;
        }
        if (decision.deprecatedPublicHeaderAliasRetained) {
            ++summary.deprecatedPublicHeaderAliasRetainedCount;
        }
        if (decision.replacementMatchesDeprecatedAlias) {
            ++summary.deprecatedAliasReplacementMatchCount;
        } else if (!decision.deprecatedAliasName.empty()) {
            ++summary.deprecatedAliasReplacementMismatchCount;
        }
        if (decision.canDeprecateNow) {
            ++summary.canDeprecateNowCount;
        }
        if (decision.canRemoveLater) {
            ++summary.canRemoveLaterCount;
        }
        if (!decision.canRemoveLater) {
            ++summary.removalBlockedCount;
        }
        if (!decision.canRemoveLater && !decision.deprecatedAliasName.empty()) {
            ++summary.deprecatedAliasRemovalBlockedCount;
        }
        summary.pluginUsageCount += decision.pluginUsageCount;
        summary.moduleUsageCount += decision.moduleUsageCount;
        summary.harnessLegacyUsageCount +=
            decision.harnessExpectationUsageCount;
        if (decision.publicHeaderConsumerRisk) {
            ++summary.publicHeaderRiskCount;
        }
    }
    return summary;
}

double ResolveCruiseComparisonAltitudeFt(
    const AircraftStateSnapshot& aircraftState,
    double cruiseTargetFt) {
    auto comparisonAltitudeFt = aircraftState.altitudeOperationalFt;
    if (cruiseTargetFt >= 18000.0 && aircraftState.hasAltimeterSetting) {
        comparisonAltitudeFt +=
            (29.92 - aircraftState.altimeterSettingInHg) * 1000.0;
    }
    return comparisonAltitudeFt;
}

double NormalizeCruiseAltitudeFt(double altitudeFt) {
    if (altitudeFt <= 0.0) {
        return 0.0;
    }
    return std::round(altitudeFt / 100.0) * 100.0;
}

std::string FormatCruiseTargetText(double altitudeFt) {
    const auto normalizedAltitudeFt = NormalizeCruiseAltitudeFt(altitudeFt);
    if (normalizedAltitudeFt <= 0.0) {
        return {};
    }

    const auto roundedAltitudeFt = static_cast<int>(normalizedAltitudeFt);
    if (roundedAltitudeFt >= 18000) {
        return "FL" + std::to_string(roundedAltitudeFt / 100);
    }

    return std::to_string(roundedAltitudeFt);
}

bool AircraftWithinCruiseTargetBand(
    const AircraftStateSnapshot& aircraftState,
    double cruiseTargetFt,
    const BrainOwnedCruiseTargetTuning& tuning) {
    return std::fabs(
               ResolveCruiseComparisonAltitudeFt(
                   aircraftState,
                   cruiseTargetFt) -
               cruiseTargetFt) <= tuning.gateToleranceFt;
}

}  // namespace

void ResetBrainOwnedRuntimeState(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    const auto displayOverrideMode = state->displayOverrideMode;
    *state = {};
    state->displayOverrideMode = displayOverrideMode;
}

void ResetBrainOwnedRuntimeCachePreservingFlightContext(
    BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    const auto flightContext = state->flightContext;
    const auto displayOverrideMode = state->displayOverrideMode;
    const auto pendingTextEntryMode = state->pendingTextEntryMode;
    const auto lastAircraftStateSnapshot = state->lastAircraftStateSnapshot;
    const auto lastPilotIdentitySnapshot = state->lastPilotIdentitySnapshot;
    const auto lastFlightPlanSnapshot = state->lastFlightPlanSnapshot;
    const auto lastNetworkPlanSnapshot = state->lastNetworkPlanSnapshot;
    *state = {};
    state->flightContext = flightContext;
    state->displayOverrideMode = displayOverrideMode;
    state->pendingTextEntryMode = pendingTextEntryMode;
    state->lastAircraftStateSnapshot = lastAircraftStateSnapshot;
    state->lastPilotIdentitySnapshot = lastPilotIdentitySnapshot;
    state->lastFlightPlanSnapshot = lastFlightPlanSnapshot;
    state->lastNetworkPlanSnapshot = lastNetworkPlanSnapshot;
}

void ResetBrainOwnedDisplayPublisherState(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->phaseSnapshotPublisherState.Reset();
}

void CommitBrainOwnedLastSampledFacts(
    BrainOwnedRuntimeState* state,
    const AircraftStateSnapshot& aircraftState,
    const PilotIdentitySnapshot& pilotIdentity,
    const FlightPlanSnapshot& flightPlan,
    const NetworkPlanSnapshot& networkPlan) {
    if (state == nullptr) {
        return;
    }
    state->lastAircraftStateSnapshot = aircraftState;
    state->lastPilotIdentitySnapshot = pilotIdentity;
    state->lastFlightPlanSnapshot = flightPlan;
    state->lastNetworkPlanSnapshot = networkPlan;
}

void ClearBrainOwnedLastSampledFacts(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->lastAircraftStateSnapshot = {};
    state->lastPilotIdentitySnapshot = {};
    state->lastFlightPlanSnapshot = {};
    state->lastNetworkPlanSnapshot = {};
}

std::string BuildBrainOwnedPlanIdentityKey(
    std::string callsign,
    std::string departureIcao,
    std::string destinationIcao) {
    callsign = NormalizeCallsign(std::move(callsign));
    departureIcao = NormalizeIcao(std::move(departureIcao));
    destinationIcao = NormalizeIcao(std::move(destinationIcao));

    if (callsign.empty() || departureIcao.empty() || destinationIcao.empty()) {
        return {};
    }

    return callsign + "|" + departureIcao + "|" + destinationIcao;
}

std::string BuildBrainOwnedNetworkPlanIdentityKey(
    const NetworkPlanSnapshot& networkPlanSnapshot) {
    if (networkPlanSnapshot.stale || !networkPlanSnapshot.matched) {
        return {};
    }

    return BuildBrainOwnedPlanIdentityKey(
        networkPlanSnapshot.matchedCallsign,
        networkPlanSnapshot.departureIcao,
        networkPlanSnapshot.destinationIcao);
}

void CommitBrainOwnedFlightContext(
    BrainOwnedRuntimeState* state,
    const workflow::FlightContext& flightContext) {
    if (state == nullptr) {
        return;
    }
    state->flightContext = flightContext;
}

void ClearBrainOwnedFlightContext(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->flightContext = {};
}

void ClearBrainOwnedXPilotConnectionTracking(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->xPilotSessionBoundaryState.lastXPilotConnected = false;
    state->xPilotSessionBoundaryState.lastConnectedPilotCallsign.clear();
}

void ClearBrainOwnedFlightRecoveryRequests(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->xPilotSessionBoundaryState.disconnectedPilotCallsign.clear();
    state->pendingAutomaticFlightRecovery = false;
    state->manualFlightRecoveryRequested = false;
}

void SetBrainOwnedAutomaticFlightRecoveryPending(
    BrainOwnedRuntimeState* state,
    bool pending) {
    if (state == nullptr) {
        return;
    }
    state->pendingAutomaticFlightRecovery = pending;
}

void SetBrainOwnedManualFlightRecoveryRequested(
    BrainOwnedRuntimeState* state,
    bool requested) {
    if (state == nullptr) {
        return;
    }
    state->manualFlightRecoveryRequested = requested;
}

void SetBrainOwnedColdDarkResetApplied(
    BrainOwnedRuntimeState* state,
    bool applied) {
    if (state == nullptr) {
        return;
    }
    state->coldDarkResetApplied = applied;
}

void ClearBrainOwnedAircraftStateInvalidBoundary(
    BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->aircraftStateInvalidBoundaryActive = false;
}

void ApplyBrainOwnedXPilotSessionBoundaryDecision(
    BrainOwnedRuntimeState* state,
    const workflow::XPilotSessionBoundaryDecision& decision) {
    if (state == nullptr) {
        return;
    }
    state->xPilotSessionBoundaryState = decision.nextState;
    if (decision.shouldClearPendingRecoveryRequests) {
        state->pendingAutomaticFlightRecovery = false;
        state->manualFlightRecoveryRequested = false;
    }
    if (decision.shouldQueueAutomaticRecovery) {
        state->pendingAutomaticFlightRecovery = true;
    }
    if (decision.sawXPilotConnectedThisFlight) {
        state->sawXPilotConnectedThisFlight = true;
    }
}

void ApplyBrainOwnedAircraftRuntimeBoundaryDecision(
    BrainOwnedRuntimeState* state,
    const workflow::AircraftRuntimeBoundaryDecision& decision) {
    if (state == nullptr) {
        return;
    }
    state->coldDarkResetApplied = decision.nextColdDarkResetApplied;
    state->aircraftStateInvalidBoundaryActive =
        decision.nextAircraftStateInvalidBoundaryActive;
}

void ResetBrainOwnedStandbyAssistLatch(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->standbyAssistLatchKey.clear();
    state->standbyAssistWriteConsumed = false;
}

void ClearBrainOwnedDiversionOverrideSource(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->diversionOverrideSourceKey.clear();
}

void SetBrainOwnedDiversionOverrideSourceKey(
    BrainOwnedRuntimeState* state,
    const std::string& sourcePlanKey) {
    if (state == nullptr) {
        return;
    }
    state->diversionOverrideSourceKey = sourcePlanKey;
}

BrainOwnedDiversionOverrideDecision DecideBrainOwnedDiversionOverride(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedDiversionOverrideInput& input) {
    BrainOwnedDiversionOverrideDecision decision;
    if (!input.hasOverride) {
        return decision;
    }

    if (input.sourcePlanKey.empty() ||
        state.diversionOverrideSourceKey.empty() ||
        input.sourcePlanKey != state.diversionOverrideSourceKey) {
        decision.clearOverride = true;
        decision.logLine =
            "[XVatsim] Diversion override cleared because source VATSIM flight plan was stale, unmatched, or changed.\n";
        return decision;
    }

    decision.useOverride = true;
    return decision;
}

void ClearBrainOwnedPreflightRouteCacheApplication(
    BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->preflightRouteCacheAppliedPlanKey.clear();
}

void SetBrainOwnedDisplayOverrideMode(
    BrainOwnedRuntimeState* state,
    BrainOwnedDisplayOverrideMode mode) {
    if (state == nullptr) {
        return;
    }
    state->displayOverrideMode = mode;
}

void ClearBrainOwnedManualQuery(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->manualQuerySnapshot = {};
    state->manualQueryVisibleUntilSeconds = 0;
}

void SetBrainOwnedPendingTextEntryMode(
    BrainOwnedRuntimeState* state,
    BrainOwnedTextEntryMode mode) {
    if (state == nullptr) {
        return;
    }
    state->pendingTextEntryMode = mode;
}

void ClearBrainOwnedPendingTextEntryMode(BrainOwnedRuntimeState* state) {
    SetBrainOwnedPendingTextEntryMode(state, BrainOwnedTextEntryMode::None);
}

BrainOwnedTextEntryMode ConsumeBrainOwnedPendingTextEntryMode(
    BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return BrainOwnedTextEntryMode::None;
    }
    const auto mode = state->pendingTextEntryMode;
    state->pendingTextEntryMode = BrainOwnedTextEntryMode::None;
    return mode;
}

bool HasBrainOwnedPendingTextEntry(const BrainOwnedRuntimeState& state) {
    return state.pendingTextEntryMode != BrainOwnedTextEntryMode::None;
}

void ShowBrainOwnedManualQueryLine(
    BrainOwnedRuntimeState* state,
    const std::string& line,
    long long visibleUntilSeconds) {
    if (state == nullptr) {
        return;
    }
    state->manualQuerySnapshot = {};
    if (line.empty()) {
        state->manualQueryVisibleUntilSeconds = 0;
        return;
    }
    state->manualQuerySnapshot.visible = true;
    state->manualQuerySnapshot.line = line;
    state->manualQueryVisibleUntilSeconds = visibleUntilSeconds;
}

void CommitBrainOwnedManualQuerySnapshot(
    BrainOwnedRuntimeState* state,
    ManualQuerySnapshot snapshot,
    long long visibleUntilSeconds) {
    if (state == nullptr) {
        return;
    }
    state->manualQuerySnapshot = std::move(snapshot);
    state->manualQueryVisibleUntilSeconds =
        state->manualQuerySnapshot.visible ? visibleUntilSeconds : 0;
}

void ExpireBrainOwnedManualQuery(
    BrainOwnedRuntimeState* state,
    long long nowSeconds) {
    if (state == nullptr || !state->manualQuerySnapshot.visible) {
        return;
    }
    if (nowSeconds < state->manualQueryVisibleUntilSeconds) {
        return;
    }
    ClearBrainOwnedManualQuery(state);
}

void ResetBrainOwnedControllerMessageState(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->controllerMessageState = {};
}

void ClearBrainOwnedControllerMessage(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->controllerMessageState.visible = false;
}

void RecallBrainOwnedControllerMessage(BrainOwnedRuntimeState* state) {
    if (state == nullptr ||
        !state->controllerMessageState.cachedAvailable) {
        return;
    }
    state->controllerMessageState.visible = true;
}

void UpdateBrainOwnedControllerMessageState(
    BrainOwnedRuntimeState* state,
    const XPilotPrivateMessageSnapshot& messageSnapshot,
    bool controllerMessageUiEnabled) {
    if (state == nullptr) {
        return;
    }
    if (!controllerMessageUiEnabled || !messageSnapshot.loaded) {
        ResetBrainOwnedControllerMessageState(state);
        return;
    }

    auto& pendingMessage = state->controllerMessageState;
    if (!pendingMessage.primed) {
        pendingMessage.primed = true;
        pendingMessage.lastSequence = messageSnapshot.sequence;
        return;
    }

    if (messageSnapshot.sequence < pendingMessage.lastSequence) {
        pendingMessage.lastSequence = messageSnapshot.sequence;
        pendingMessage.visible = false;
        pendingMessage.cachedAvailable = false;
        pendingMessage.from.clear();
        pendingMessage.body.clear();
        return;
    }

    if (messageSnapshot.sequence == pendingMessage.lastSequence) {
        return;
    }

    pendingMessage.lastSequence = messageSnapshot.sequence;
    if (!messageSnapshot.available) {
        return;
    }

    pendingMessage.cachedAvailable = true;
    pendingMessage.visible = true;
    pendingMessage.from = messageSnapshot.from;
    pendingMessage.body = messageSnapshot.body;
}

BrainOwnedPreflightRouteCacheDecision BeginBrainOwnedPreflightRouteCacheApplication(
    BrainOwnedRuntimeState* state,
    const BrainOwnedPreflightRouteCacheInput& input) {
    BrainOwnedPreflightRouteCacheDecision decision;
    if (state == nullptr || input.planKey.empty()) {
        return decision;
    }
    if (input.planKey == state->preflightRouteCacheAppliedPlanKey) {
        return decision;
    }

    state->preflightRouteCacheAppliedPlanKey = input.planKey;
    decision.shouldClearRouteResolverCache = true;
    if (!input.hasCandidate) {
        decision.logLine =
            "[XVatsim] Preflight route cache unavailable; using normal route preparation.\n";
        return decision;
    }

    decision.shouldValidateCandidate = true;
    return decision;
}

BrainOwnedPreflightRouteCacheValidationDecision
DecideBrainOwnedPreflightRouteCacheValidation(
    const BrainOwnedPreflightRouteCacheValidationInput& input) {
    BrainOwnedPreflightRouteCacheValidationDecision decision;
    if (!input.accepted) {
        decision.logLine =
            "[XVatsim] Preflight route cache rejected: " + input.reason +
            ". Falling back to normal route preparation.\n";
        return decision;
    }

    decision.shouldApplyRouteResolverCache = true;
    return decision;
}

std::string ToString(BrainOwnedCandidateDecision decision) {
    switch (decision) {
        case BrainOwnedCandidateDecision::Pending:
            return "pending";
        case BrainOwnedCandidateDecision::Accepted:
            return "accepted";
        case BrainOwnedCandidateDecision::Rejected:
            return "rejected";
        case BrainOwnedCandidateDecision::NeedsVerification:
            return "needs_verification";
    }
    return "pending";
}

std::string BuildBrainOwnedCandidateCompletionKey(
    std::uint64_t radioBoardHash,
    std::uint64_t routePolygonHash,
    WorkflowStage workflowStage,
    const std::string& currentPolygonKey,
    const RadioReachableControllerCandidate& candidate) {
    std::ostringstream stream;
    stream << radioBoardHash << '|'
           << routePolygonHash << '|'
           << WorkflowStageToken(workflowStage) << '|'
           << currentPolygonKey << '|'
           << candidate.stableKey;
    return stream.str();
}

void RecordBrainOwnedCandidateCompletion(
    BrainOwnedRuntimeState* state,
    BrainOwnedCandidateCompletion completion) {
    if (state == nullptr || completion.stableKey.empty()) {
        return;
    }

    const auto existing = std::find_if(
        state->candidateCompletions.begin(),
        state->candidateCompletions.end(),
        [&completion](const BrainOwnedCandidateCompletion& record) {
            return record.stableKey == completion.stableKey;
        });
    if (existing != state->candidateCompletions.end()) {
        *existing = std::move(completion);
        return;
    }

    state->candidateCompletions.push_back(std::move(completion));
}

BrainOwnedBoardFilterOutput FilterBrainOwnedBoardByAcceptedCompletions(
    const ModuleBoardSnapshot& board,
    const std::vector<BrainOwnedCandidateCompletion>& completions) {
    BrainOwnedBoardFilterOutput output;
    output.board = board;
    output.board.stations.clear();

    for (const auto& station : board.stations) {
        if (!StationRequiresCompletion(station)) {
            output.board.stations.push_back(station);
            continue;
        }

        const auto approved = std::any_of(
            completions.begin(),
            completions.end(),
            [&](const auto& completion) {
                return CompletionApprovesStation(completion, station);
            });
        if (approved) {
            output.board.stations.push_back(station);
        } else {
            ++output.rejectedUnapprovedStations;
        }
    }

    output.board.available =
        output.board.available || !output.board.stations.empty();
    return output;
}

BrainOwnedRadioBoardReuseOutput TryReuseBrainOwnedRadioBoard(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedRadioBoardReuseInput& input) {
    BrainOwnedRadioBoardReuseOutput output;
    output.canReuse =
        state.hasRadioBoard &&
        state.lastControllerGeneration == input.controllerGeneration &&
        (input.nowSeconds - state.lastRadioBoardRefreshSeconds) <
            input.refreshIntervalSeconds;
    if (!output.canReuse) {
        return output;
    }

    output.radioSnapshot = state.radioSnapshot;
    output.reason = "board-unchanged-no-authority-work";
    output.cacheStatus = "clean-runtime-cache-hit";
    return output;
}

BrainOwnedRadioBoardCommitOutput CommitBrainOwnedRadioBoardRefresh(
    BrainOwnedRuntimeState* state,
    const BrainOwnedRadioBoardCommitInput& input) {
    BrainOwnedRadioBoardCommitOutput output;
    output.radioSnapshot = input.radioSnapshot;
    const auto previousRadioSnapshot =
        state != nullptr ? state->radioSnapshot
                         : RadioReachableControllerSnapshot{};
    output.diff =
        DiffRadioReachableSnapshots(
            previousRadioSnapshot,
            input.radioSnapshot);
    output.boardChanged =
        state == nullptr ||
        !state->hasRadioBoard ||
        previousRadioSnapshot.stableHash != input.radioSnapshot.stableHash;
    output.reason =
        output.boardChanged ? "radio-board-changed"
                            : "board-unchanged-no-authority-work";
    output.cacheStatus =
        output.boardChanged ? "clean-runtime-board-refresh"
                            : "radio-board-runtime-idle";

    if (state == nullptr) {
        return output;
    }

    state->hasRadioBoard = true;
    state->lastRadioBoardRefreshSeconds = input.nowSeconds;
    state->lastControllerGeneration = input.controllerGeneration;
    state->transceiverSnapshot = input.transceiverSnapshot;
    state->radioSnapshot = input.radioSnapshot;
    state->radioDiff = output.diff;
    state->lastWakeReason =
        output.boardChanged ? "radio-board-changed" : "radio-board-refresh";
    if (output.boardChanged) {
        state->candidateCompletions.clear();
        state->candidatesComplete = false;
    }
    return output;
}

static BrainOwnedTerminalAuthorityRefreshInput
BuildDepartureTerminalAuthorityRefreshInput(
    const workflow::FlightContext& flightContext,
    long long nowSeconds) {
    BrainOwnedTerminalAuthorityRefreshInput input;
    input.flightContextActive = flightContext.active;
    input.airportIcao = flightContext.departureIcao;
    input.hasAirportCoordinates = flightContext.hasDepartureCoordinates;
    input.airportLatitudeDeg = flightContext.departureLatDeg;
    input.airportLongitudeDeg = flightContext.departureLonDeg;
    input.nowSeconds = nowSeconds;
    return input;
}

static BrainOwnedTerminalAuthorityRefreshInput
BuildArrivalTerminalAuthorityRefreshInput(
    const workflow::FlightContext& flightContext,
    long long nowSeconds) {
    BrainOwnedTerminalAuthorityRefreshInput input;
    input.flightContextActive = flightContext.active;
    input.airportIcao = flightContext.destinationIcao;
    input.hasAirportCoordinates = flightContext.hasDestinationCoordinates;
    input.airportLatitudeDeg = flightContext.destinationLatDeg;
    input.airportLongitudeDeg = flightContext.destinationLonDeg;
    input.nowSeconds = nowSeconds;
    return input;
}

static BrainOwnedAirportFrequencyRefreshInput BuildAirportFrequencyRefreshInput(
    const workflow::FlightContext& flightContext,
    long long nowSeconds) {
    BrainOwnedAirportFrequencyRefreshInput input;
    input.flightContextActive = flightContext.active;
    input.departureIcao = flightContext.departureIcao;
    input.arrivalIcao = flightContext.destinationIcao;
    input.nowSeconds = nowSeconds;
    return input;
}

BrainOwnedTerminalAuthorityRefreshPlan BeginBrainOwnedDepartureTerminalAuthorityRefresh(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedTerminalAuthorityRefreshInput& input) {
    BrainOwnedTerminalAuthorityRefreshPlan plan;
    plan.requestKey = BuildTerminalAuthorityRequestKey(input);
    plan.workerInput.airportIcao = NormalizeIcao(input.airportIcao);
    plan.workerInput.hasAirportCoordinates = input.hasAirportCoordinates;
    plan.workerInput.airportLatitudeDeg = input.airportLatitudeDeg;
    plan.workerInput.airportLongitudeDeg = input.airportLongitudeDeg;
    plan.workerInput.nowSeconds = input.nowSeconds;

    if (!input.flightContextActive) {
        plan.reason = "terminal-authority-no-active-flight";
        plan.cacheStatus = "terminal-authority-idle";
        return plan;
    }
    if (plan.workerInput.airportIcao.empty()) {
        plan.reason = "terminal-authority-missing-airport";
        plan.cacheStatus = "terminal-authority-idle";
        return plan;
    }
    if (!input.hasAirportCoordinates) {
        plan.reason = "terminal-authority-missing-airport-coordinates";
        plan.cacheStatus = "terminal-authority-idle";
        return plan;
    }

    const auto hasReusableFact =
        state.hasDepartureTerminalAuthority &&
        state.departureTerminalAuthorityRequestKey == plan.requestKey &&
        state.departureTerminalAuthority.resolved &&
        !state.departureTerminalAuthority.stale;
    if (hasReusableFact) {
        plan.cachedFact = state.departureTerminalAuthority;
        plan.reason = "departure-terminal-authority-cache-hit";
        plan.cacheStatus = "terminal-authority-cache-hit";
        return plan;
    }

    const auto lastFactPending =
        state.hasDepartureTerminalAuthority &&
        state.departureTerminalAuthorityRequestKey == plan.requestKey &&
        state.departureTerminalAuthority.pending;
    const auto pollCadenceSeconds = lastFactPending ? 1 : 15;
    const auto lastLookup = state.lastDepartureTerminalAuthorityLookupSeconds;
    if (lastLookup > 0 &&
        (input.nowSeconds - lastLookup) < pollCadenceSeconds &&
        state.departureTerminalAuthorityRequestKey == plan.requestKey) {
        plan.cachedFact = state.departureTerminalAuthority;
        plan.reason = lastFactPending ? "departure-terminal-authority-pending"
                                      : "departure-terminal-authority-backoff";
        plan.cacheStatus = "terminal-authority-wait";
        return plan;
    }

    plan.shouldRunWorker = true;
    plan.reason = state.departureTerminalAuthorityRequestKey == plan.requestKey
                      ? "departure-terminal-authority-refresh"
                      : "departure-terminal-authority-new-airport";
    plan.cacheStatus = "terminal-authority-worker-requested";
    return plan;
}

BrainOwnedTerminalAuthorityRefreshPlan BeginBrainOwnedArrivalTerminalAuthorityRefresh(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedTerminalAuthorityRefreshInput& input) {
    BrainOwnedTerminalAuthorityRefreshPlan plan;
    plan.requestKey = BuildTerminalAuthorityRequestKey(input);
    plan.workerInput.airportIcao = NormalizeIcao(input.airportIcao);
    plan.workerInput.hasAirportCoordinates = input.hasAirportCoordinates;
    plan.workerInput.airportLatitudeDeg = input.airportLatitudeDeg;
    plan.workerInput.airportLongitudeDeg = input.airportLongitudeDeg;
    plan.workerInput.nowSeconds = input.nowSeconds;

    if (!input.flightContextActive) {
        plan.reason = "terminal-authority-no-active-flight";
        plan.cacheStatus = "terminal-authority-idle";
        return plan;
    }
    if (plan.workerInput.airportIcao.empty()) {
        plan.reason = "terminal-authority-missing-airport";
        plan.cacheStatus = "terminal-authority-idle";
        return plan;
    }
    if (!input.hasAirportCoordinates) {
        plan.reason = "terminal-authority-missing-airport-coordinates";
        plan.cacheStatus = "terminal-authority-idle";
        return plan;
    }

    const auto hasReusableFact =
        state.hasArrivalTerminalAuthority &&
        state.arrivalTerminalAuthorityRequestKey == plan.requestKey &&
        state.arrivalTerminalAuthority.resolved &&
        !state.arrivalTerminalAuthority.stale;
    if (hasReusableFact) {
        plan.cachedFact = state.arrivalTerminalAuthority;
        plan.reason = "arrival-terminal-authority-cache-hit";
        plan.cacheStatus = "terminal-authority-cache-hit";
        return plan;
    }

    const auto lastFactPending =
        state.hasArrivalTerminalAuthority &&
        state.arrivalTerminalAuthorityRequestKey == plan.requestKey &&
        state.arrivalTerminalAuthority.pending;
    const auto pollCadenceSeconds = lastFactPending ? 1 : 15;
    const auto lastLookup = state.lastArrivalTerminalAuthorityLookupSeconds;
    if (lastLookup > 0 &&
        (input.nowSeconds - lastLookup) < pollCadenceSeconds &&
        state.arrivalTerminalAuthorityRequestKey == plan.requestKey) {
        plan.cachedFact = state.arrivalTerminalAuthority;
        plan.reason = lastFactPending ? "arrival-terminal-authority-pending"
                                      : "arrival-terminal-authority-backoff";
        plan.cacheStatus = "terminal-authority-wait";
        return plan;
    }

    plan.shouldRunWorker = true;
    plan.reason = state.arrivalTerminalAuthorityRequestKey == plan.requestKey
                      ? "arrival-terminal-authority-refresh"
                      : "arrival-terminal-authority-new-airport";
    plan.cacheStatus = "terminal-authority-worker-requested";
    return plan;
}

BrainOwnedAirportFrequencyRefreshPlan BeginBrainOwnedAirportFrequencyRefresh(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedAirportFrequencyRefreshInput& input) {
    BrainOwnedAirportFrequencyRefreshPlan plan;
    plan.requestKey = BuildAirportFrequencyRequestKey(input);
    plan.workerInput.departureIcao = NormalizeIcao(input.departureIcao);
    plan.workerInput.arrivalIcao = NormalizeIcao(input.arrivalIcao);
    plan.workerInput.nowSeconds = input.nowSeconds;

    if (!input.flightContextActive) {
        plan.reason = "airport-frequency-no-active-flight";
        plan.cacheStatus = "airport-frequency-idle";
        return plan;
    }
    if (plan.requestKey.empty()) {
        plan.reason = "airport-frequency-missing-airports";
        plan.cacheStatus = "airport-frequency-idle";
        return plan;
    }

    const auto hasReusableFact =
        state.hasAirportFrequencies &&
        state.airportFrequencyRequestKey == plan.requestKey &&
        state.airportFrequencies.resolved &&
        !state.airportFrequencies.stale;
    if (hasReusableFact) {
        plan.cachedFact = state.airportFrequencies;
        plan.reason = "airport-frequency-cache-hit";
        plan.cacheStatus = "airport-frequency-cache-hit";
        return plan;
    }

    const auto lastFactPending =
        state.hasAirportFrequencies &&
        state.airportFrequencyRequestKey == plan.requestKey &&
        state.airportFrequencies.pending;
    const auto pollCadenceSeconds = lastFactPending ? 1 : 30;
    const auto lastLookup = state.lastAirportFrequencyLookupSeconds;
    if (lastLookup > 0 &&
        (input.nowSeconds - lastLookup) < pollCadenceSeconds &&
        state.airportFrequencyRequestKey == plan.requestKey) {
        plan.cachedFact = state.airportFrequencies;
        plan.reason = lastFactPending ? "airport-frequency-pending"
                                      : "airport-frequency-backoff";
        plan.cacheStatus = "airport-frequency-wait";
        return plan;
    }

    plan.shouldRunWorker = true;
    plan.reason = state.airportFrequencyRequestKey == plan.requestKey
                      ? "airport-frequency-refresh"
                      : "airport-frequency-new-airport-pair";
    plan.cacheStatus = "airport-frequency-worker-requested";
    return plan;
}

void CommitBrainOwnedDepartureTerminalAuthorityRefresh(
    BrainOwnedRuntimeState* state,
    const BrainOwnedTerminalAuthorityRefreshPlan& plan,
    const BrainTerminalAuthorityWorkerOutput& workerOutput) {
    if (state == nullptr || plan.requestKey.empty()) {
        return;
    }

    const auto previousHash = state->departureTerminalAuthorityHash;
    state->hasDepartureTerminalAuthority = true;
    state->departureTerminalAuthority = workerOutput;
    state->departureTerminalAuthority.airportIcao =
        NormalizeIcao(state->departureTerminalAuthority.airportIcao);
    state->departureTerminalAuthorityRequestKey = plan.requestKey;
    state->departureTerminalAuthorityHash =
        HashTerminalAuthorityFact(state->departureTerminalAuthority);
    state->lastDepartureTerminalAuthorityLookupSeconds =
        plan.workerInput.nowSeconds;

    if (previousHash != state->departureTerminalAuthorityHash) {
        state->candidateCompletions.clear();
        state->candidatesComplete = false;
        state->lastIdleReason.clear();
    }
}

void CommitBrainOwnedArrivalTerminalAuthorityRefresh(
    BrainOwnedRuntimeState* state,
    const BrainOwnedTerminalAuthorityRefreshPlan& plan,
    const BrainTerminalAuthorityWorkerOutput& workerOutput) {
    if (state == nullptr || plan.requestKey.empty()) {
        return;
    }

    const auto previousHash = state->arrivalTerminalAuthorityHash;
    state->hasArrivalTerminalAuthority = true;
    state->arrivalTerminalAuthority = workerOutput;
    state->arrivalTerminalAuthority.airportIcao =
        NormalizeIcao(state->arrivalTerminalAuthority.airportIcao);
    state->arrivalTerminalAuthorityRequestKey = plan.requestKey;
    state->arrivalTerminalAuthorityHash =
        HashTerminalAuthorityFact(state->arrivalTerminalAuthority);
    state->lastArrivalTerminalAuthorityLookupSeconds =
        plan.workerInput.nowSeconds;

    if (previousHash != state->arrivalTerminalAuthorityHash) {
        state->candidateCompletions.clear();
        state->candidatesComplete = false;
        state->lastIdleReason.clear();
    }
}

void CommitBrainOwnedAirportFrequencyRefresh(
    BrainOwnedRuntimeState* state,
    const BrainOwnedAirportFrequencyRefreshPlan& plan,
    const BrainAirportFrequencyWorkerOutput& workerOutput) {
    if (state == nullptr || plan.requestKey.empty()) {
        return;
    }

    const auto previousHash = state->airportFrequencyHash;
    state->hasAirportFrequencies = true;
    state->airportFrequencies = workerOutput;
    state->airportFrequencies.departureIcao =
        NormalizeIcao(state->airportFrequencies.departureIcao);
    state->airportFrequencies.arrivalIcao =
        NormalizeIcao(state->airportFrequencies.arrivalIcao);
    state->airportFrequencyRequestKey = plan.requestKey;
    state->airportFrequencyHash =
        HashAirportFrequencyFact(state->airportFrequencies);
    state->lastAirportFrequencyLookupSeconds =
        plan.workerInput.nowSeconds;

    if (previousHash != state->airportFrequencyHash) {
        state->candidateCompletions.clear();
        state->candidatesComplete = false;
        state->lastIdleReason.clear();
    }
}

BrainTerminalAuthorityWorkerOutput RefreshBrainOwnedDepartureTerminalAuthority(
    BrainOwnedRuntimeState* state,
    const workflow::FlightContext& flightContext,
    long long nowSeconds,
    BrainTerminalAuthorityWorker* worker) {
    BrainOwnedRuntimeState emptyState;
    const auto& runtimeState = state != nullptr ? *state : emptyState;
    const auto input =
        BuildDepartureTerminalAuthorityRefreshInput(flightContext, nowSeconds);
    const auto plan =
        BeginBrainOwnedDepartureTerminalAuthorityRefresh(runtimeState, input);
    auto fact = plan.cachedFact;

    if (!plan.shouldRunWorker || worker == nullptr) {
        return fact;
    }

    fact = worker->ResolveAirportTerminalOwner(plan.workerInput);
    CommitBrainOwnedDepartureTerminalAuthorityRefresh(state, plan, fact);
    if (state != nullptr) {
        fact = state->departureTerminalAuthority;
    }
    return fact;
}

BrainTerminalAuthorityWorkerOutput RefreshBrainOwnedArrivalTerminalAuthority(
    BrainOwnedRuntimeState* state,
    const workflow::FlightContext& flightContext,
    long long nowSeconds,
    BrainTerminalAuthorityWorker* worker) {
    BrainOwnedRuntimeState emptyState;
    const auto& runtimeState = state != nullptr ? *state : emptyState;
    const auto input =
        BuildArrivalTerminalAuthorityRefreshInput(flightContext, nowSeconds);
    const auto plan =
        BeginBrainOwnedArrivalTerminalAuthorityRefresh(runtimeState, input);
    auto fact = plan.cachedFact;

    if (!plan.shouldRunWorker || worker == nullptr) {
        return fact;
    }

    fact = worker->ResolveAirportTerminalOwner(plan.workerInput);
    CommitBrainOwnedArrivalTerminalAuthorityRefresh(state, plan, fact);
    if (state != nullptr) {
        fact = state->arrivalTerminalAuthority;
    }
    return fact;
}

BrainAirportFrequencyWorkerOutput RefreshBrainOwnedAirportFrequencies(
    BrainOwnedRuntimeState* state,
    const workflow::FlightContext& flightContext,
    long long nowSeconds,
    BrainAirportFrequencyWorker* worker) {
    BrainOwnedRuntimeState emptyState;
    const auto& runtimeState = state != nullptr ? *state : emptyState;
    const auto input = BuildAirportFrequencyRefreshInput(
        flightContext,
        nowSeconds);
    const auto plan =
        BeginBrainOwnedAirportFrequencyRefresh(runtimeState, input);
    auto fact = plan.cachedFact;

    if (!plan.shouldRunWorker || worker == nullptr) {
        return fact;
    }

    fact = worker->ResolveAirportFrequencies(plan.workerInput);
    CommitBrainOwnedAirportFrequencyRefresh(state, plan, fact);
    if (state != nullptr) {
        fact = state->airportFrequencies;
    }
    return fact;
}

RadioReachableControllerSnapshot RunBrainOwnedRadioPhaseGate(
    BrainOwnedRuntimeState* state,
    const RadioReachableControllerSnapshot& radioSnapshot,
    WorkflowStage workflowStage,
    const std::string& reason) {
    RadioReachablePhaseGateOptions gateOptions;
    gateOptions.stage = workflowStage;
    gateOptions.reason = reason;
    const auto gatedRadioSnapshot =
        ApplyRadioReachablePhaseGate(radioSnapshot, gateOptions);
    if (state != nullptr) {
        state->gatedRadioSnapshot = gatedRadioSnapshot;
    }
    return gatedRadioSnapshot;
}

void CommitBrainOwnedPublishedRuntime(
    BrainOwnedRuntimeState* state,
    const BrainOwnedPublishedRuntimeInput& input) {
    if (state == nullptr) {
        return;
    }
    state->departureBoardSnapshot = input.departureBoard;
    state->arrivalBoardSnapshot = input.arrivalBoard;
    state->enrouteBoardSnapshot = input.enrouteBoard;
    state->finalDisplaySnapshot = input.finalDisplay;
    state->lastWorkflowStage = input.workflowStage;
    state->lastPlanKey = input.planKey;
    state->lastRadioBoardHash = input.gatedRadioSnapshot.stableHash;
}

BrainOwnedOverlayWakeDecision DecideBrainOwnedOverlayWake(
    const BrainOwnedOverlayWakeInput& input) {
    BrainOwnedOverlayWakeDecision decision;
    decision.xPilotDisconnectedAlert =
        input.sawXPilotConnectedThisFlight &&
        !input.xPilotSession.connected;

    bool autoWake = false;
    if (input.manualQueryVisible || decision.xPilotDisconnectedAlert) {
        autoWake = true;
    } else if (!input.xPilotSession.connected) {
        autoWake = false;
    } else if (input.workflowStage == WorkflowStage::Arrival) {
        autoWake = true;
    } else if (input.workflowStage == WorkflowStage::Enroute) {
        autoWake =
            HasLiveRouteCenters(input.finalDisplay) ||
            input.enrouteInitialHoldActive;
    } else if (input.workflowStage == WorkflowStage::Departure) {
        autoWake = true;
    } else {
        autoWake = true;
    }

    const auto controllerMessageWake =
        input.controllerMessageVisible &&
        input.displayOverrideMode != BrainOwnedDisplayOverrideMode::ForcedSleep;
    const auto criticalWake =
        input.manualQueryVisible ||
        input.textEntryActive ||
        decision.xPilotDisconnectedAlert;

    decision.shouldWake = autoWake;
    if (input.displayOverrideMode == BrainOwnedDisplayOverrideMode::ForcedOpen) {
        decision.shouldWake = true;
    } else if (input.displayOverrideMode ==
               BrainOwnedDisplayOverrideMode::ForcedSleep) {
        decision.shouldWake = false;
    }
    if (criticalWake || controllerMessageWake) {
        decision.shouldWake = true;
    }

    decision.hideUntilXpilotConnect =
        input.displayOverrideMode == BrainOwnedDisplayOverrideMode::Auto &&
        !input.manualQueryVisible &&
        !input.textEntryActive &&
        !input.xPilotSession.connected &&
        !input.sawXPilotConnectedThisFlight;

    decision.reason = "enroute-empty";
    if (input.displayOverrideMode == BrainOwnedDisplayOverrideMode::ForcedOpen) {
        decision.reason = "manual-open";
    } else if (
        input.displayOverrideMode == BrainOwnedDisplayOverrideMode::ForcedSleep) {
        decision.reason = "manual-sleep";
    } else if (input.manualQueryVisible) {
        decision.reason = "manual-query";
    } else if (input.textEntryActive) {
        decision.reason = "text-entry";
    } else if (input.controllerMessageVisible) {
        decision.reason = "controller-message";
    } else if (decision.hideUntilXpilotConnect) {
        decision.reason = "xpilot-waiting";
    } else if (decision.xPilotDisconnectedAlert) {
        decision.reason = "xpilot-disconnected";
    } else if (!input.aircraftState.batteryOn) {
        decision.reason = "battery-off";
    } else if (input.workflowStage == WorkflowStage::Departure) {
        decision.reason = "departure-board";
    } else if (input.workflowStage == WorkflowStage::Arrival) {
        decision.reason = "arrival-board";
    } else if (
        input.workflowStage == WorkflowStage::Enroute &&
        !input.finalDisplay.stations.empty()) {
        decision.reason = "enroute-board";
    } else if (input.workflowStage == WorkflowStage::None) {
        decision.reason = "startup";
    }

    return decision;
}

void ResetBrainOwnedEnrouteInitialHold(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }

    state->enrouteInitialHoldStarted = false;
    state->enrouteInitialHoldUntilSeconds = -1.0;
}

BrainOwnedEnrouteInitialHoldOutput UpdateBrainOwnedEnrouteInitialHold(
    BrainOwnedRuntimeState* state,
    const BrainOwnedEnrouteInitialHoldInput& input) {
    BrainOwnedEnrouteInitialHoldOutput output;
    if (state == nullptr) {
        return output;
    }

    if (input.workflowStage == WorkflowStage::Enroute &&
        !state->enrouteInitialHoldStarted) {
        state->enrouteInitialHoldStarted = true;
        state->enrouteInitialHoldUntilSeconds =
            input.nowSeconds + input.holdSeconds;
        output.started = true;
    }

    output.holdUntilSeconds = state->enrouteInitialHoldUntilSeconds;
    output.active =
        state->enrouteInitialHoldUntilSeconds >= input.nowSeconds;
    return output;
}

BrainOwnedFlightPlanSampleDecision DecideBrainOwnedFlightPlanSample(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedFlightPlanSampleInput& input) {
    BrainOwnedFlightPlanSampleDecision decision;
    decision.reason = "sample-required";

    if (input.flightContextActive &&
        state.hasFlightPlanSnapshot &&
        (input.nowSeconds - state.lastFlightPlanSampleSeconds) <
            input.sampleCadenceSeconds) {
        decision.shouldSample = false;
        decision.cachedSnapshot = state.flightPlanSnapshot;
        decision.reason = "active-flight-context-cadence-hit";
    }

    return decision;
}

void CommitBrainOwnedFlightPlanSample(
    BrainOwnedRuntimeState* state,
    const BrainOwnedFlightPlanSampleCommitInput& input) {
    if (state == nullptr) {
        return;
    }

    state->hasFlightPlanSnapshot = true;
    state->flightPlanSnapshot = input.snapshot;
    state->lastFlightPlanSampleSeconds = input.nowSeconds;
}

void ResetBrainOwnedCruiseTarget(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }

    state->cruiseAltitudeReachedThisFlight = false;
    state->cruiseTargetManualOverride = false;
    state->hasActiveCruiseTarget = false;
    state->activeCruiseTargetFt = 0.0;
    state->cruiseGateSatisfiedSinceSeconds = -1.0;
    state->cruiseTargetSourceKey.clear();
}

BrainOwnedCruiseTargetPlanOutput SyncBrainOwnedCruiseTargetFromNetworkPlan(
    BrainOwnedRuntimeState* state,
    const BrainOwnedCruiseTargetPlanInput& input) {
    BrainOwnedCruiseTargetPlanOutput output;
    if (state == nullptr) {
        return output;
    }

    if (state->hasActiveCruiseTarget ||
        !state->cruiseTargetSourceKey.empty()) {
        if (input.planKey.empty() ||
            (!state->cruiseTargetSourceKey.empty() &&
             state->cruiseTargetSourceKey != input.planKey)) {
            ResetBrainOwnedCruiseTarget(state);
            output.changed = true;
            output.logLine =
                "[XVatsim] Cruise target cleared because source VATSIM flight plan was stale, unmatched, or changed.\n";
            return output;
        }
    }

    if (state->cruiseTargetManualOverride ||
        !input.flightContextActive ||
        input.planKey.empty() ||
        !input.networkPlan.hasFiledCruiseAltitude) {
        return output;
    }

    const auto normalizedAltitudeFt =
        NormalizeCruiseAltitudeFt(input.networkPlan.filedCruiseAltitudeFt);
    if (normalizedAltitudeFt <= 0.0) {
        return output;
    }

    output.changed =
        !state->hasActiveCruiseTarget ||
        state->activeCruiseTargetFt != normalizedAltitudeFt ||
        state->cruiseTargetSourceKey != input.planKey;
    state->activeCruiseTargetFt = normalizedAltitudeFt;
    state->hasActiveCruiseTarget = true;
    state->cruiseTargetSourceKey = input.planKey;
    return output;
}

BrainOwnedCruiseTargetCommandOutput ApplyBrainOwnedCruiseTargetCommand(
    BrainOwnedRuntimeState* state,
    const BrainOwnedCruiseTargetCommandInput& input) {
    BrainOwnedCruiseTargetCommandOutput output;
    if (state == nullptr) {
        return output;
    }

    if (!input.flightContextActive) {
        output.statusLine = "CRUISE unavailable without active flight";
        return output;
    }

    if (input.command == BrainOwnedCruiseTargetCommand::CurrentAltitude &&
        !input.aircraftState.valid) {
        output.statusLine = "CRUISE unavailable without aircraft state";
        return output;
    }

    if (input.planKey.empty()) {
        ResetBrainOwnedCruiseTarget(state);
        output.changed = true;
        output.statusLine = "CRUISE unavailable until VATSIM plan matched";
        return output;
    }

    double normalizedAltitudeFt = 0.0;
    if (input.command == BrainOwnedCruiseTargetCommand::CurrentAltitude) {
        normalizedAltitudeFt =
            NormalizeCruiseAltitudeFt(
                ResolveCruiseComparisonAltitudeFt(
                    input.aircraftState,
                    input.aircraftState.altitudeOperationalFt));
    } else {
        if (!input.networkPlan.hasFiledCruiseAltitude) {
            ResetBrainOwnedCruiseTarget(state);
            output.changed = true;
            output.statusLine = "CRUISE filed altitude unavailable";
            return output;
        }
        normalizedAltitudeFt =
            NormalizeCruiseAltitudeFt(input.networkPlan.filedCruiseAltitudeFt);
    }

    if (normalizedAltitudeFt <= 0.0) {
        ResetBrainOwnedCruiseTarget(state);
        output.changed = true;
        output.statusLine =
            input.command == BrainOwnedCruiseTargetCommand::CurrentAltitude
                ? "CRUISE invalid altitude"
                : "CRUISE invalid filed altitude";
        return output;
    }

    state->activeCruiseTargetFt = normalizedAltitudeFt;
    state->hasActiveCruiseTarget = true;
    state->cruiseTargetManualOverride =
        input.command == BrainOwnedCruiseTargetCommand::CurrentAltitude;
    state->cruiseTargetSourceKey = input.planKey;
    if (input.aircraftState.valid) {
        state->cruiseAltitudeReachedThisFlight =
            AircraftWithinCruiseTargetBand(
                input.aircraftState,
                state->activeCruiseTargetFt,
                input.tuning);
        state->cruiseGateSatisfiedSinceSeconds =
            state->cruiseAltitudeReachedThisFlight
                ? input.nowSeconds
                : -1.0;
    } else {
        state->cruiseAltitudeReachedThisFlight = false;
        state->cruiseGateSatisfiedSinceSeconds = -1.0;
    }

    output.accepted = true;
    output.changed = true;
    output.statusLine =
        "CRUISE target " +
        FormatCruiseTargetText(state->activeCruiseTargetFt) +
        (state->cruiseTargetManualOverride ? " current" : " filed");
    return output;
}

void UpdateBrainOwnedCruiseTargetProgress(
    BrainOwnedRuntimeState* state,
    const BrainOwnedCruiseTargetProgressInput& input) {
    if (state == nullptr ||
        !state->hasActiveCruiseTarget ||
        state->cruiseAltitudeReachedThisFlight) {
        return;
    }

    const auto withinCruiseBand =
        AircraftWithinCruiseTargetBand(
            input.aircraftState,
            state->activeCruiseTargetFt,
            input.tuning);
    const auto verticallyStable =
        std::fabs(input.aircraftState.verticalSpeedFpm) <=
        input.tuning.stableVerticalSpeedFpm;

    if (withinCruiseBand && verticallyStable) {
        if (state->cruiseGateSatisfiedSinceSeconds < 0.0) {
            state->cruiseGateSatisfiedSinceSeconds = input.nowSeconds;
        } else if (
            (input.nowSeconds - state->cruiseGateSatisfiedSinceSeconds) >=
            input.tuning.gateDwellSeconds) {
            state->cruiseAltitudeReachedThisFlight = true;
        }
    } else {
        state->cruiseGateSatisfiedSinceSeconds = -1.0;
    }
}

std::string BuildBrainOwnedCruiseTargetHeaderText(
    const BrainOwnedRuntimeState& state) {
    if (!state.hasActiveCruiseTarget) {
        return {};
    }

    return FormatCruiseTargetText(state.activeCruiseTargetFt);
}

void ResetBrainOwnedWorkflowProgress(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }

    state->departureReleasedThisFlight = false;
    state->arrivalAwakeThisFlight = false;
    state->airborneSinceSeconds = -1.0;
}

void ResetBrainOwnedWorkflowArrivalWake(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }

    state->arrivalAwakeThisFlight = false;
}

workflow::WorkflowState BuildBrainOwnedWorkflowState(
    const BrainOwnedRuntimeState& state) {
    workflow::WorkflowState workflowState;
    workflowState.flightContext = state.flightContext;
    workflowState.departureReleasedThisFlight =
        state.departureReleasedThisFlight;
    workflowState.arrivalAwakeThisFlight = state.arrivalAwakeThisFlight;
    workflowState.airborneSinceSeconds = state.airborneSinceSeconds;
    return workflowState;
}

void CommitBrainOwnedWorkflowState(
    BrainOwnedRuntimeState* state,
    const workflow::WorkflowState& workflowState) {
    if (state == nullptr) {
        return;
    }

    state->departureReleasedThisFlight =
        workflowState.departureReleasedThisFlight;
    state->arrivalAwakeThisFlight = workflowState.arrivalAwakeThisFlight;
    state->airborneSinceSeconds = workflowState.airborneSinceSeconds;
    state->flightContext = workflowState.flightContext;
}

BrainOwnedWorkflowSelectionOutput ResolveBrainOwnedWorkflowSelection(
    BrainOwnedRuntimeState* state,
    const BrainOwnedWorkflowSelectionInput& input) {
    BrainOwnedWorkflowSelectionOutput output;
    if (state == nullptr) {
        return output;
    }

    auto workflowState = BuildBrainOwnedWorkflowState(*state);
    output.decision = workflow::ResolveWorkflowStageFromSignals(
        input.aircraft,
        input.radios,
        BuildBrainOwnedWorkflowSignals(input),
        input.nowSeconds,
        &workflowState,
        input.tuning);

    CommitBrainOwnedWorkflowState(state, workflowState);
    return output;
}

void ApplyBrainOwnedWorkflowRecoveryStage(
    BrainOwnedRuntimeState* state,
    WorkflowStage stage,
    double nowSeconds) {
    if (state == nullptr) {
        return;
    }

    switch (stage) {
        case WorkflowStage::Departure:
            state->departureReleasedThisFlight = false;
            state->arrivalAwakeThisFlight = false;
            state->airborneSinceSeconds = -1.0;
            break;
        case WorkflowStage::Enroute:
            state->departureReleasedThisFlight = true;
            state->arrivalAwakeThisFlight = false;
            if (state->airborneSinceSeconds < 0.0) {
                state->airborneSinceSeconds = nowSeconds;
            }
            break;
        case WorkflowStage::Arrival:
            state->departureReleasedThisFlight = true;
            state->arrivalAwakeThisFlight = true;
            if (state->airborneSinceSeconds < 0.0) {
                state->airborneSinceSeconds = nowSeconds;
            }
            break;
        case WorkflowStage::None:
        default:
            break;
    }
}

void SetBrainOwnedXPilotConnectedSeen(
    BrainOwnedRuntimeState* state,
    bool seen) {
    if (state == nullptr) {
        return;
    }

    state->sawXPilotConnectedThisFlight = seen;
}

void MarkBrainOwnedXPilotConnectedIfConnected(
    BrainOwnedRuntimeState* state,
    const XPilotSessionSnapshot& xPilotSession) {
    if (state == nullptr || !xPilotSession.connected) {
        return;
    }

    state->sawXPilotConnectedThisFlight = true;
}

BrainOwnedStandbyAssistPlanOutput BuildBrainOwnedStandbyAssistPlan(
    const BrainOwnedStandbyAssistPlanInput& input) {
    BrainOwnedStandbyAssistPlanOutput output;
    output.workflowStage = input.workflowStage;
    output.board = input.board;
    output.settingsDiagnostics = BuildStandbySettingsDiagnostics(input);
    for (auto& station : output.board.stations) {
        station.standby = false;
        station.next =
            station.displayRelation == DisplayRelation::NextPolygon ||
            station.displayRelation == DisplayRelation::ArrivalPrep;
        if (input.radios.valid) {
            station.tuned = IsCom1TunedToFrequency(input.radios, station.frequency);
        }
    }

    const auto finalize = [&]() {
        output.standbyDecisions = BuildStandbyDecisionLedger(input, output);
        output.targetStandbyDecisionId.clear();
        if (output.hasTarget) {
            for (const auto& decision : output.standbyDecisions) {
                if (output.actualSelectedTargetSource ==
                        "controller-display-row" &&
                    decision.sourceDomain == "display-row" &&
                    decision.boardIndex ==
                        static_cast<int>(output.targetStationIndex)) {
                    output.targetStandbyDecisionId =
                        decision.standbyDecisionId;
                    break;
                }
                if (output.actualSelectedTargetSource ==
                        "direct-ctaf-advisory" &&
                    decision.sourceDomain == "ctaf-unicom-advisory" &&
                    decision.sourceDecisionId ==
                        output.targetAdvisorySourceDecisionId) {
                    output.targetStandbyDecisionId =
                        decision.standbyDecisionId;
                    break;
                }
            }
        }
        output.standbySummary =
            BuildStandbyRecommendationSummary(
                output.standbyDecisions,
                output.hasTarget);
        return output;
    };

    if (input.workflowStage != WorkflowStage::Departure &&
        input.workflowStage != WorkflowStage::Arrival &&
        input.workflowStage != WorkflowStage::Enroute) {
        return finalize();
    }
    if (input.planKey.empty() || !input.radios.valid) {
        return finalize();
    }

    std::vector<std::size_t> orderedEligibleIndices;
    orderedEligibleIndices.reserve(output.board.stations.size());
    for (std::size_t index = 0; index < output.board.stations.size(); ++index) {
        const auto& station = output.board.stations[index];
        if (station.offline ||
            !IsStandbyEligibleRole(station.role) ||
            station.frequency.empty() ||
            IsBlockedControllerFrequency(station.frequency)) {
            continue;
        }
        orderedEligibleIndices.push_back(index);
    }

    if (orderedEligibleIndices.empty()) {
        TryPromoteDirectCtafStandbyTarget(input, &output);
        return finalize();
    }

    std::size_t targetPosition = 0;
    for (std::size_t position = 0; position < orderedEligibleIndices.size();
         ++position) {
        if (output.board.stations[orderedEligibleIndices[position]].tuned) {
            targetPosition = position + 1;
        }
    }

    while (targetPosition < orderedEligibleIndices.size() &&
           output.board.stations[orderedEligibleIndices[targetPosition]].tuned) {
        ++targetPosition;
    }
    if (targetPosition >= orderedEligibleIndices.size()) {
        TryPromoteDirectCtafStandbyTarget(input, &output);
        return finalize();
    }

    const auto targetIndex = orderedEligibleIndices[targetPosition];
    const auto& targetStation = output.board.stations[targetIndex];
    if (targetStation.frequency.empty()) {
        TryPromoteDirectCtafStandbyTarget(input, &output);
        return finalize();
    }

    output.hasTarget = true;
    output.targetStationIndex = targetIndex;
    output.targetFrequency = targetStation.frequency;
    output.actualSelectedTargetSource = "controller-display-row";
    output.actualSelectedTargetFrequency = targetStation.frequency;
    output.latchKey =
        StandbyAssistWorkflowKey(
            input.workflowStage,
            input.planKey,
            input.radios,
            targetStation);
    const auto normalizedTarget = NormalizeFrequency(targetStation.frequency);
    output.targetAlreadyInCom1Standby =
        !normalizedTarget.empty() &&
        NormalizeFrequency(input.radios.com1StandbyFrequency) ==
            normalizedTarget;
    return finalize();
}

BrainOwnedStandbyAssistSideEffectDecision
DecideBrainOwnedStandbyAssistSideEffect(
    BrainOwnedRuntimeState* state,
    const BrainOwnedStandbyAssistPlanOutput& plan,
    bool standbyAssistEnabled) {
    BrainOwnedStandbyAssistSideEffectDecision decision;
    decision.sideEffectDecisionId =
        plan.hasTarget
            ? "standby-side-effect:" + plan.targetStandbyDecisionId
            : "standby-side-effect:no-target";
    decision.standbyDecisionId = plan.targetStandbyDecisionId;
    decision.standbyAssistEnabled = standbyAssistEnabled;
    decision.latchKey = plan.latchKey;
    decision.targetFrequency = plan.targetFrequency;
    decision.writerTarget = plan.hasTarget ? "COM1_STANDBY" : "none";
    decision.actualSelectedTargetSource = plan.actualSelectedTargetSource;
    decision.actualSelectedTargetFrequency =
        plan.actualSelectedTargetFrequency;
    decision.actualWriteEligible =
        plan.hasTarget && !plan.targetAlreadyInCom1Standby;
    decision.standbySummary = plan.standbySummary;
    if (state == nullptr) {
        ResetBrainOwnedStandbyAssistLatch(state);
        decision.failureReason = "runtime-state-unavailable";
        decision.writerResult =
            BuildBrainOwnedStandbyAssistWriterResultFromCode(
                decision,
                "write-not-allowed");
        decision.writerResult.writerFailureReason =
            decision.failureReason;
        ApplyStandbyWriterResultCodeDefaults(&decision.writerResult);
        return decision;
    }

    if (!plan.hasTarget) {
        ResetBrainOwnedStandbyAssistLatch(state);
        std::string noTargetReason;
        const auto writerCode =
            SelectStandbyNoTargetWriterResultCode(plan, &noTargetReason);
        decision.failureReason = "no-target";
        decision.writerResult =
            BuildBrainOwnedStandbyAssistWriterResultFromCode(
                decision,
                writerCode);
        decision.writerResult.writerFailureReason = noTargetReason;
        ApplyStandbyWriterResultCodeDefaults(&decision.writerResult);
        return decision;
    }

    if (plan.latchKey != state->standbyAssistLatchKey) {
        state->standbyAssistLatchKey = plan.latchKey;
        state->standbyAssistWriteConsumed = false;
    }
    decision.standbySummary.writeDecisionCount = 1;

    if (!standbyAssistEnabled) {
        decision.failureReason = "standby-assist-disabled";
        decision.writerResult =
            BuildBrainOwnedStandbyAssistWriterResultFromCode(
                decision,
                "write-not-allowed");
        decision.writerResult.writerFailureReason =
            decision.failureReason;
        ApplyStandbyWriterResultCodeDefaults(&decision.writerResult);
        return decision;
    }

    decision.standbyLoaded = plan.targetAlreadyInCom1Standby;
    if (decision.standbyLoaded) {
        decision.failureReason = "already-com1-standby";
        decision.writerResult =
            BuildBrainOwnedStandbyAssistWriterResultFromCode(
                decision,
                "write-not-allowed");
        decision.writerResult.writerFailureReason =
            decision.failureReason;
        ApplyStandbyWriterResultCodeDefaults(&decision.writerResult);
    }
    decision.shouldWriteCom1Standby =
        !decision.standbyLoaded && !state->standbyAssistWriteConsumed;
    decision.writeAllowed = decision.shouldWriteCom1Standby;
    decision.writeAttempted = decision.shouldWriteCom1Standby;
    decision.actualWriteAttempted = decision.writeAttempted;
    if (!decision.shouldWriteCom1Standby && !decision.standbyLoaded &&
        decision.failureReason.empty()) {
        decision.failureReason = "latch-already-consumed";
        decision.writerResult =
            BuildBrainOwnedStandbyAssistWriterResultFromCode(
                decision,
                "no-write-requested");
        decision.writerResult.writerFailureReason =
            decision.failureReason;
        ApplyStandbyWriterResultCodeDefaults(&decision.writerResult);
    }
    state->standbyAssistWriteConsumed = true;
    decision.latchConsumed = state->standbyAssistWriteConsumed;
    decision.standbySummary.writeAttemptCount =
        decision.writeAttempted ? 1 : 0;
    return decision;
}

BrainOwnedStandbyAssistWriterResult
BuildBrainOwnedStandbyAssistWriterResultFromCode(
    const BrainOwnedStandbyAssistSideEffectDecision& decision,
    const std::string& writerResultCode) {
    auto result = BuildBaseStandbyWriterResult(decision);
    result.writerResultCode = writerResultCode;
    if (result.writerNormalizedFrequency.empty()) {
        result.writerNormalizedFrequency =
            NormalizeFrequency(result.writerInputFrequency);
    }
    ApplyStandbyWriterResultCodeDefaults(&result);
    return result;
}

BrainOwnedStandbyAssistWriterResult
BuildBrainOwnedStandbyAssistWriterResult(
    const BrainOwnedStandbyAssistSideEffectDecision& decision,
    bool writeSucceeded) {
    auto result = BuildBaseStandbyWriterResult(decision);

    if (!decision.writeAttempted) {
        if (!decision.writerResult.writerResultCode.empty()) {
            result = decision.writerResult;
            ApplyStandbyWriterResultCodeDefaults(&result);
            return result;
        }
        result.writerResultCode =
            decision.failureReason.empty()
                ? std::string("no-write-requested")
                : std::string("write-not-allowed");
        result.writerFailureReason = decision.failureReason;
        ApplyStandbyWriterResultCodeDefaults(&result);
        return result;
    }

    if (decision.writerTarget != "COM1_STANDBY") {
        result.writerResultCode = "invalid-target-com";
        result.writerFailureReason = "invalid-target-com";
        ApplyStandbyWriterResultCodeDefaults(&result);
        return result;
    }

    const auto frequencyFailure =
        DiagnoseCom1StandbyWriterFrequency(
            decision.targetFrequency,
            &result.writerNormalizedFrequency);
    if (!frequencyFailure.empty()) {
        result.writerResultCode = frequencyFailure;
        result.writerFailureReason = frequencyFailure;
        result.writerWriteAttempted = decision.writeAttempted;
        ApplyStandbyWriterResultCodeDefaults(&result);
        return result;
    }

    result.writerValidationPassed = true;
    result.writerResultCode =
        writeSucceeded ? "write-succeeded" : "writer-result-unknown";
    if (!writeSucceeded) {
        result.writerFailureReason = "writer-returned-false";
    }
    result.writerWriteAttempted = decision.writeAttempted;
    result.writerWriteSucceeded = writeSucceeded;
    ApplyStandbyWriterResultCodeDefaults(&result);
    return result;
}

BrainOwnedStandbyAssistSideEffectDecision
CompleteBrainOwnedStandbyAssistSideEffectDecision(
    const BrainOwnedStandbyAssistPlanOutput& plan,
    BrainOwnedStandbyAssistSideEffectDecision decision,
    bool standbyLoaded) {
    auto writerResult =
        BuildBrainOwnedStandbyAssistWriterResult(decision, standbyLoaded);
    return CompleteBrainOwnedStandbyAssistSideEffectDecision(
        plan,
        std::move(decision),
        writerResult);
}

BrainOwnedStandbyAssistSideEffectDecision
CompleteBrainOwnedStandbyAssistSideEffectDecision(
    const BrainOwnedStandbyAssistPlanOutput& plan,
    BrainOwnedStandbyAssistSideEffectDecision decision,
    const BrainOwnedStandbyAssistWriterResult& writerResult) {
    decision.writerResult = writerResult;
    ApplyStandbyWriterResultCodeDefaults(&decision.writerResult);
    if (decision.writerResult.writerFailureReason.empty() &&
        !decision.failureReason.empty() &&
        decision.writerResult.writerResultCode != "write-succeeded") {
        decision.writerResult.writerFailureReason =
            decision.failureReason;
        ApplyStandbyWriterResultCodeDefaults(&decision.writerResult);
    }

    const auto standbyLoaded =
        decision.writeAttempted
            ? decision.writerResult.writerWriteSucceeded
            : decision.standbyLoaded;
    decision.standbyLoaded = standbyLoaded;
    decision.displayStandbyMarkerApplied =
        WouldApplyStandbyMarker(plan, standbyLoaded);
    if (decision.writeAttempted) {
        decision.writeSucceededKnown = true;
        decision.writeSucceeded =
            decision.writerResult.writerWriteSucceeded;
        decision.actualWriteSucceededKnown = true;
        decision.actualWriteSucceeded =
            decision.writerResult.writerWriteSucceeded;
        decision.standbySummary.writeSuccessCount =
            decision.writeSucceeded ? 1 : 0;
        decision.standbySummary.writeFailureCount =
            decision.writeSucceeded ? 0 : 1;
        if (!decision.writeSucceeded && decision.failureReason.empty()) {
            decision.failureReason =
                decision.writerResult.writerFailureReason.empty()
                    ? std::string("writer-returned-false")
                    : decision.writerResult.writerFailureReason;
        }
    }
    AddStandbyWriterSummaryCounters(
        decision.writerResult,
        &decision.standbySummary);
    return decision;
}

FinalDisplaySnapshot ApplyBrainOwnedStandbyAssistResult(
    const BrainOwnedStandbyAssistPlanOutput& plan,
    bool standbyLoaded) {
    auto board = plan.board;
    if (!plan.hasTarget || plan.targetStationIndex >= board.stations.size()) {
        return board;
    }
    if (plan.workflowStage == WorkflowStage::Enroute) {
        return board;
    }

    auto& targetStation = board.stations[plan.targetStationIndex];
    if (targetStation.tuned) {
        return board;
    }
    targetStation.standby = standbyLoaded;
    return board;
}

BrainOwnedPublisherInput BuildBrainOwnedPublisherInputFromFacts(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedPublisherFactInput& facts) {
    BrainOwnedPublisherInput input;
    input.workflowStage = facts.workflowStage;
    input.routeProgressDistanceNm = state.routeProgressDistanceNm;
    input.currentPolygonKey = state.currentPolygonKey;
    input.nextPolygonKey = state.nextPolygonKey;
    input.arrivalPolygonKey = state.arrivalPolygonKey;
    input.radios = facts.radios;
    input.departureBoard = facts.departureBoard;
    input.arrivalBoard = facts.arrivalBoard;
    input.enrouteBoard = facts.enrouteBoard;
    input.completions = facts.completions;
    input.hasDepartureCtafStation =
        BuildCtafStationFromLookupFact(
            facts.departureCtaf,
            facts.radios,
            &input.departureCtafStation);
    input.hasArrivalCtafStation =
        BuildCtafStationFromLookupFact(
            facts.arrivalCtaf,
            facts.radios,
            &input.arrivalCtafStation);
    input.ctafUnicomSourceEvidence.push_back(
        BuildCtafUnicomSourceEvidence("departure", facts.departureCtaf));
    input.ctafUnicomSourceEvidence.push_back(
        BuildCtafUnicomSourceEvidence("arrival", facts.arrivalCtaf));
    input.verificationPending = facts.verificationPending;
    input.publishReason = facts.publishReason;
    input.productPlanKey = facts.productPlanKey;
    input.productPlanKeySource = facts.productPlanKeySource;
    input.productPlanKeyMissingReason = facts.productPlanKeyMissingReason;
    input.sourceOwnedFallbackStableKeyShadowEnabled =
        facts.sourceOwnedFallbackStableKeyShadowEnabled;
    input.sourceOwnedFallbackStableKeyShadowGateSource =
        facts.sourceOwnedFallbackStableKeyShadowGateSource;
    input.sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled =
        facts.sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled;
    input.sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource =
        facts.sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource;
    input.sourceOwnedFallbackStableKeyLiveConsumptionEnabled =
        facts.sourceOwnedFallbackStableKeyLiveConsumptionEnabled;
    input.sourceOwnedFallbackStableKeyLiveConsumptionGateSource =
        facts.sourceOwnedFallbackStableKeyLiveConsumptionGateSource;
    return input;
}

void MarkBrainOwnedDisplayedCompletionsFromFinalDisplay(
    BrainOwnedRuntimeState* state,
    const FinalDisplaySnapshot& finalDisplay) {
    if (state == nullptr) {
        return;
    }

    for (auto& completion : state->candidateCompletions) {
        if (completion.decision != BrainOwnedCandidateDecision::Accepted) {
            completion.displayed = false;
            continue;
        }

        completion.displayed =
            CompletionDisplayedInFinalBoard(finalDisplay, completion);
    }
}

BrainOwnedPublisherOutput RunBrainOwnedPublisher(
    BrainOwnedRuntimeState* state,
    const BrainOwnedPublisherInput& input) {
    BrainOwnedPublisherOutput output;

    const auto departureCompatibilityCtafRowCount =
        CountCtafStations(input.departureBoard);
    const auto arrivalCompatibilityCtafRowCount =
        CountCtafStations(input.arrivalBoard);

    const auto departureFilter =
        FilterBrainOwnedBoardByAcceptedCompletions(
            input.departureBoard,
            input.completions);
    output.departureBoard = departureFilter.board;
    output.rejectedUnapprovedStations +=
        departureFilter.rejectedUnapprovedStations;

    const auto arrivalFilter =
        FilterBrainOwnedBoardByAcceptedCompletions(
            input.arrivalBoard,
            input.completions);
    output.arrivalBoard = arrivalFilter.board;
    output.rejectedUnapprovedStations +=
        arrivalFilter.rejectedUnapprovedStations;

    const auto enrouteFilter =
        FilterBrainOwnedBoardByAcceptedCompletions(
            input.enrouteBoard,
            input.completions);
    output.enrouteBoard = enrouteFilter.board;
    output.rejectedUnapprovedStations +=
        enrouteFilter.rejectedUnapprovedStations;

    output.ctafUnicomSourceEvidence = input.ctafUnicomSourceEvidence;
    RemoveCtafStations(&output.departureBoard);
    RemoveCtafStations(&output.arrivalBoard);

    // Record the old lookup-to-row projection as compatibility/parity evidence.
    // Live CTAF/UNICOM rows are appended from advisory decisions below when
    // source evidence exists.
    const auto* departureSourceEvidence =
        FindCtafUnicomSourceEvidence(output.ctafUnicomSourceEvidence,
                                     "departure");
    const auto* arrivalSourceEvidence =
        FindCtafUnicomSourceEvidence(output.ctafUnicomSourceEvidence,
                                     "arrival");
    if (input.hasDepartureCtafStation) {
        output.ctafUnicomProjectionEvidence.push_back(
            BuildCtafUnicomProjectionEvidence(
                "departure",
                departureSourceEvidence,
                true,
                input.departureCtafStation,
                departureCompatibilityCtafRowCount,
                0,
                true));
    } else {
        output.ctafUnicomProjectionEvidence.push_back(
            BuildCtafUnicomProjectionEvidence(
                "departure",
                departureSourceEvidence,
                false,
                {},
                departureCompatibilityCtafRowCount,
                0,
                false));
    }
    if (input.hasArrivalCtafStation) {
        output.ctafUnicomProjectionEvidence.push_back(
            BuildCtafUnicomProjectionEvidence(
                "arrival",
                arrivalSourceEvidence,
                true,
                input.arrivalCtafStation,
                arrivalCompatibilityCtafRowCount,
                0,
                true));
    } else {
        output.ctafUnicomProjectionEvidence.push_back(
            BuildCtafUnicomProjectionEvidence(
                "arrival",
                arrivalSourceEvidence,
                false,
                {},
                arrivalCompatibilityCtafRowCount,
                0,
                false));
    }
    output.ctafUnicomEvidenceSummary =
        BuildCtafUnicomEvidenceSummary(
            output.ctafUnicomSourceEvidence,
            output.ctafUnicomProjectionEvidence);
    output.ctafUnicomAdvisoryPreviewDecisions =
        BuildCtafUnicomAdvisoryPreviewDecisions(
            output.ctafUnicomSourceEvidence,
            output.ctafUnicomProjectionEvidence);
    ApplyCtafUnicomAdvisoryDiagnosticFaults(
        input,
        &output.ctafUnicomAdvisoryPreviewDecisions);
    output.ctafUnicomStandbyAdvisoryCandidates.clear();
    output.ctafUnicomStandbyAdvisoryCandidates.reserve(
        output.ctafUnicomAdvisoryPreviewDecisions.size());
    for (const auto& decision :
         output.ctafUnicomAdvisoryPreviewDecisions) {
        output.ctafUnicomStandbyAdvisoryCandidates.push_back(
            BuildStandbyAdvisoryCandidate(decision));
    }
    output.ctafUnicomAdvisoryPreviewSummary =
        BuildCtafUnicomAdvisoryPreviewSummary(
            output.ctafUnicomSourceEvidence,
            output.ctafUnicomProjectionEvidence,
            output.ctafUnicomAdvisoryPreviewDecisions);
    const auto liveRowsBrainOwned =
        !output.ctafUnicomSourceEvidence.empty();
    int liveAdvisoryRowCount = 0;
    if (liveRowsBrainOwned) {
        // Brain-owned advisory decisions are now the live row projection source.
        liveAdvisoryRowCount =
            AppendCtafUnicomRowsFromAdvisoryDecisions(
                output.ctafUnicomAdvisoryPreviewDecisions,
                input.radios,
                &output.departureBoard,
                &output.arrivalBoard);
    }
    output.ctafUnicomAdvisoryAuthoritySummary =
        BuildCtafUnicomAdvisoryAuthoritySummary(
            output.ctafUnicomSourceEvidence,
            output.ctafUnicomProjectionEvidence,
            output.ctafUnicomAdvisoryPreviewDecisions,
            liveAdvisoryRowCount,
            liveRowsBrainOwned);
    output.ctafUnicomBypassAuditDecisions =
        BuildCtafUnicomBypassAuditDecisions(
            output.ctafUnicomSourceEvidence,
            output.ctafUnicomProjectionEvidence,
            output.ctafUnicomAdvisoryPreviewDecisions,
            output.ctafUnicomStandbyAdvisoryCandidates,
            output.ctafUnicomAdvisoryAuthoritySummary.advisoryAuthority);
    output.ctafUnicomBypassAuditSummary =
        BuildCtafUnicomBypassAuditSummary(
            output.ctafUnicomBypassAuditDecisions);
    output.ctafUnicomMissingEvidenceAuditDecisions =
        BuildCtafUnicomMissingEvidenceAuditDecisions(
            output.ctafUnicomSourceEvidence,
            output.ctafUnicomProjectionEvidence,
            output.ctafUnicomAdvisoryPreviewDecisions,
            output.ctafUnicomAdvisoryAuthoritySummary,
            output.ctafUnicomBypassAuditSummary);
    output.ctafUnicomMissingEvidenceAuditSummary =
        BuildCtafUnicomMissingEvidenceAuditSummary(
            output.ctafUnicomMissingEvidenceAuditDecisions);
    output.ctafUnicomLegacyBypassAliasAuditDecisions =
        BuildCtafUnicomLegacyBypassAliasAuditDecisions();
    output.ctafUnicomLegacyBypassAliasAuditSummary =
        BuildCtafUnicomLegacyBypassAliasAuditSummary(
            output.ctafUnicomLegacyBypassAliasAuditDecisions);
    output.ctafUnicomPublicUnknownAliasConsumerAuditDecisions =
        BuildCtafUnicomPublicUnknownAliasConsumerAuditDecisions();
    output.ctafUnicomPublicUnknownAliasConsumerAuditSummary =
        BuildCtafUnicomPublicUnknownAliasConsumerAuditSummary(
            output.ctafUnicomPublicUnknownAliasConsumerAuditDecisions);
    output.ctafUnicomExternalAliasDeprecationDecisions =
        BuildCtafUnicomExternalAliasDeprecationDecisions();
    output.ctafUnicomExternalAliasDeprecationSummary =
        BuildCtafUnicomExternalAliasDeprecationSummary(
            output.ctafUnicomExternalAliasDeprecationDecisions);
    output.ctafUnicomPublicHeaderAliasRiskClosureDecisions =
        BuildCtafUnicomPublicHeaderAliasRiskClosureDecisions();
    output.ctafUnicomPublicHeaderAliasRiskClosureSummary =
        BuildCtafUnicomPublicHeaderAliasRiskClosureSummary(
            output.ctafUnicomPublicHeaderAliasRiskClosureDecisions);

    BrainDisplayIntentInput displayIntentInput;
    displayIntentInput.workflowStage = input.workflowStage;
    displayIntentInput.routeProgressDistanceNm =
        input.routeProgressDistanceNm;
    displayIntentInput.currentPolygonKey = input.currentPolygonKey;
    displayIntentInput.nextPolygonKey = input.nextPolygonKey;
    displayIntentInput.arrivalPolygonKey = input.arrivalPolygonKey;
    displayIntentInput.radios = input.radios;
    displayIntentInput.departureBoard = output.departureBoard;
    displayIntentInput.arrivalBoard = output.arrivalBoard;
    displayIntentInput.enrouteBoard = output.enrouteBoard;
    displayIntentInput.relationFacts =
        BuildDisplayRelationFacts(input.completions);
    displayIntentInput.sourceOwnedFallbackStableKeyShadowEnabled =
        input.sourceOwnedFallbackStableKeyShadowEnabled;
    displayIntentInput.sourceOwnedFallbackStableKeyShadowGateSource =
        input.sourceOwnedFallbackStableKeyShadowGateSource;
    displayIntentInput
        .sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled =
        input.sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled;
    displayIntentInput
        .sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource =
        input.sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource;
    displayIntentInput.sourceOwnedFallbackStableKeyLiveConsumptionEnabled =
        input.sourceOwnedFallbackStableKeyLiveConsumptionEnabled;
    displayIntentInput.sourceOwnedFallbackStableKeyLiveConsumptionGateSource =
        input.sourceOwnedFallbackStableKeyLiveConsumptionGateSource;

    output.displayIntent = RunBrainDisplayIntentWorker(displayIntentInput);
    output.departureBoard = output.displayIntent.departureBoard;
    output.arrivalBoard = output.displayIntent.arrivalBoard;
    output.enrouteBoard = output.displayIntent.enrouteBoard;
    output.finalDisplay = output.displayIntent.finalDisplay;

    if (state != nullptr) {
        PhaseSnapshotPublishRequest publishRequest;
        publishRequest.stage = input.workflowStage;
        publishRequest.candidate = output.finalDisplay;
        publishRequest.verificationPending = input.verificationPending;
        publishRequest.reason = input.publishReason;
        publishRequest.currentSnapshotKey =
            std::to_string(output.displayIntent.stableHash);
        publishRequest.currentPlanKey = input.productPlanKey;
        publishRequest.productPlanKey = input.productPlanKey;
        publishRequest.productPlanKeyAvailable =
            !input.productPlanKey.empty();
        publishRequest.productPlanKeySource =
            input.productPlanKey.empty()
                ? (input.productPlanKeySource.empty()
                       ? std::string("unavailable")
                       : input.productPlanKeySource)
                : (input.productPlanKeySource.empty()
                       ? std::string("live-product")
                       : input.productPlanKeySource);
        publishRequest.productPlanKeyMissingReason =
            input.productPlanKey.empty()
                ? (input.productPlanKeyMissingReason.empty()
                       ? std::string("brain-owned-publisher-plan-key-missing")
                       : input.productPlanKeyMissingReason)
                : std::string{};
        publishRequest.sourceOwnedFallbackStableKeyShadowEnabled =
            input.sourceOwnedFallbackStableKeyShadowEnabled;
        publishRequest.sourceOwnedFallbackStableKeyShadowGateSource =
            input.sourceOwnedFallbackStableKeyShadowGateSource;
        publishRequest
            .sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled =
            input.sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled;
        publishRequest
            .sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource =
            input.sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource;
        publishRequest.sourceOwnedFallbackStableKeyLiveConsumptionEnabled =
            input.sourceOwnedFallbackStableKeyLiveConsumptionEnabled;
        publishRequest.sourceOwnedFallbackStableKeyLiveConsumptionGateSource =
            input.sourceOwnedFallbackStableKeyLiveConsumptionGateSource;
        output.phasePublish =
            PublishPhaseSnapshot(
                &state->phaseSnapshotPublisherState,
                publishRequest);
        output.finalDisplay = output.phasePublish.snapshot;
        output.phasePublisherStateSummary =
            PhaseSnapshotPublisherStateSummary(
                state->phaseSnapshotPublisherState);
        state->lastDisplayIntentHash = output.displayIntent.stableHash;
        MarkBrainOwnedDisplayedCompletionsFromFinalDisplay(
            state,
            output.finalDisplay);
    }

    return output;
}

void CommitBrainOwnedPublishedRuntimeFromPublisherOutput(
    BrainOwnedRuntimeState* state,
    WorkflowStage workflowStage,
    const std::string& planKey,
    const RadioReachableControllerSnapshot& gatedRadioSnapshot,
    const BrainOwnedPublisherOutput& publisherOutput,
    const FinalDisplaySnapshot& finalDisplay) {
    BrainOwnedPublishedRuntimeInput input;
    input.workflowStage = workflowStage;
    input.planKey = planKey;
    input.gatedRadioSnapshot = gatedRadioSnapshot;
    input.departureBoard = publisherOutput.departureBoard;
    input.arrivalBoard = publisherOutput.arrivalBoard;
    input.enrouteBoard = publisherOutput.enrouteBoard;
    input.finalDisplay = finalDisplay;
    CommitBrainOwnedPublishedRuntime(state, input);
}

bool BrainOwnedCandidatesCompleteForCurrentBoard(
    const BrainOwnedRuntimeState& state,
    const RadioReachableControllerSnapshot& radioSnapshot,
    WorkflowStage workflowStage,
    const std::string& currentPolygonKey) {
    if (!radioSnapshot.available || radioSnapshot.stale) {
        return false;
    }

    std::unordered_set<std::string> completedKeys;
    completedKeys.reserve(state.candidateCompletions.size());
    for (const auto& completion : state.candidateCompletions) {
        if (completion.radioBoardHash != radioSnapshot.stableHash ||
            completion.routePolygonHash != state.routePolygonHash ||
            completion.workflowStage != workflowStage ||
            completion.currentPolygonKey != currentPolygonKey ||
            completion.decision == BrainOwnedCandidateDecision::Pending) {
            continue;
        }
        completedKeys.insert(completion.stableKey);
    }

    for (const auto& candidate : radioSnapshot.candidates) {
        const auto key = BuildBrainOwnedCandidateCompletionKey(
            radioSnapshot.stableHash,
            state.routePolygonHash,
            workflowStage,
            currentPolygonKey,
            candidate);
        if (completedKeys.find(key) == completedKeys.end()) {
            return false;
        }
    }

    return true;
}

std::string BrainOwnedRuntimeStateSummary(const BrainOwnedRuntimeState& state) {
    std::ostringstream stream;
    stream << "brainOwned"
           << " route=" << (state.hasRoutePolygonSnapshot ? 1 : 0)
           << " routeHash=" << state.routePolygonHash
           << " radio=" << (state.hasRadioBoard ? 1 : 0)
           << " radioHash=" << state.radioSnapshot.stableHash
           << " stage=" << WorkflowStageToken(state.lastWorkflowStage)
           << " polygon=" << state.currentPolygonKey
           << " completions=" << state.candidateCompletions.size()
           << " candidatesComplete=" << (state.candidatesComplete ? 1 : 0)
           << " wake=" << state.lastWakeReason
           << " idle=" << state.lastIdleReason
           << " heavyRequested=" << (state.heavyFallbackRequested ? 1 : 0)
           << " heavyRunning=" << (state.heavyFallbackRunning ? 1 : 0);
    return stream.str();
}

}  // namespace xvatsim::brain
