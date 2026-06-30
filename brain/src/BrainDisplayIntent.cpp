#include "XVatsim/brain/BrainDisplayIntent.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace xvatsim::brain {
namespace {

constexpr double kCurrentPolygonDistanceToleranceNm = 0.5;
// Mirrors BrainOrchestrator's overlay renderer limit. This diagnostic ledger
// must explain the current cap behavior without feeding display decisions.
constexpr int kOverlayCapDiagnosticLimit = 40;

void HashCombine(std::uint64_t* seed, std::uint64_t value) {
    if (seed == nullptr) {
        return;
    }
    *seed ^= value + 0x9e3779b97f4a7c15ULL + (*seed << 6) + (*seed >> 2);
}

void HashCombine(std::uint64_t* seed, const std::string& value) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : value) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    HashCombine(seed, hash);
}

void HashCombine(std::uint64_t* seed, double value) {
    const auto quantized =
        static_cast<std::uint64_t>(std::llround(std::max(0.0, value) * 10.0));
    HashCombine(seed, quantized);
}

std::string NormalizeKey(std::string value) {
    value.erase(
        std::remove_if(
            value.begin(),
            value.end(),
            [](unsigned char ch) { return std::isspace(ch) != 0; }),
        value.end());
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return value;
}

std::string NormalizeFrequency(std::string frequency) {
    frequency.erase(
        std::remove_if(
            frequency.begin(),
            frequency.end(),
            [](unsigned char ch) { return std::isspace(ch) != 0; }),
        frequency.end());

    std::string digits;
    bool sawDecimal = false;
    int decimals = 0;
    for (const auto ch : frequency) {
        if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            digits.push_back(ch);
            if (sawDecimal && decimals < 3) {
                ++decimals;
            }
            continue;
        }
        if (ch == '.' && !sawDecimal) {
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

bool KeysEqual(const std::string& left, const std::string& right) {
    const auto normalizedLeft = NormalizeKey(left);
    const auto normalizedRight = NormalizeKey(right);
    return !normalizedLeft.empty() && normalizedLeft == normalizedRight;
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

bool IsCenter(const BoardStationSnapshot& station) {
    return station.role == StationRole::Center;
}

bool IsCenter(const FinalDisplayStationSnapshot& station) {
    return station.role == StationRole::Center;
}

bool IsDisplayableStation(const BoardStationSnapshot& station) {
    return !station.offline && !station.frequency.empty();
}

bool IsDisplayableStation(const FinalDisplayStationSnapshot& station) {
    return !station.offline && !station.frequency.empty();
}

std::string StationKey(const FinalDisplayStationSnapshot& station) {
    return std::to_string(static_cast<int>(station.role)) + "|" +
           NormalizeKey(station.callsign) + "|" +
           NormalizeFrequency(station.frequency);
}

bool StationIsAcceptedRelevanceSubject(const BoardStationSnapshot& station) {
    return station.role != StationRole::Ctaf &&
           station.role != StationRole::Unicom;
}

std::string BoardSourceToken(BoardSource source) {
    switch (source) {
        case BoardSource::Departure:
            return "departure";
        case BoardSource::Arrival:
            return "arrival";
        case BoardSource::Enroute:
            return "enroute";
        case BoardSource::None:
        default:
            return "none";
    }
}

std::string WorkflowStageToken(WorkflowStage stage) {
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

std::string DecisionIdForStation(
    BoardSource source,
    const BoardStationSnapshot& station,
    int sequence) {
    std::ostringstream stream;
    stream << BoardSourceToken(source) << "|"
           << static_cast<int>(station.role) << "|"
           << NormalizeKey(station.callsign) << "|"
           << NormalizeFrequency(station.frequency) << "|"
           << sequence;
    return stream.str();
}

void MarkDuplicateDecision(
    BrainDisplayDecisionRecord* decision,
    const FinalDisplayStationSnapshot& station,
    const std::unordered_map<std::string, std::string>* keptDecisionIdsByKey) {
    if (decision == nullptr) {
        return;
    }
    decision->duplicateSuppressed = true;
    decision->duplicateKey = StationKey(station);
    decision->duplicateDroppedDecisionId = decision->decisionId;
    if (keptDecisionIdsByKey != nullptr) {
        const auto found = keptDecisionIdsByKey->find(decision->duplicateKey);
        if (found != keptDecisionIdsByKey->end()) {
            decision->duplicateKeptDecisionId = found->second;
        }
    }
}

std::string ScoreWinner(const BrainDisplayDecisionRecord& decision) {
    if (decision.hardBlock) {
        return "blocked";
    }
    if (decision.positiveScore > decision.negativeScore) {
        return "positive";
    }
    if (decision.negativeScore > decision.positiveScore) {
        return "negative";
    }
    return "balanced";
}

std::string FormatDisplayScore(double score) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << score;
    return stream.str();
}

void RefreshScoreSummary(BrainDisplayDecisionRecord* decision) {
    if (decision == nullptr) {
        return;
    }
    std::ostringstream stream;
    stream << "confidence=" << decision->confidenceLevel
           << "/positive=" << FormatDisplayScore(decision->positiveScore)
           << "/negative=" << FormatDisplayScore(decision->negativeScore)
           << "/hardBlock=" << (decision->hardBlock ? 1 : 0)
           << "/winner=" << ScoreWinner(*decision);
    if (!decision->failSoftRecommendation.empty()) {
        stream << "/recommendation=" << decision->failSoftRecommendation;
    }
    decision->scoreSummary = stream.str();
}

void MarkFailSoftRecommendation(
    BrainDisplayDecisionRecord* decision,
    const std::string& recommendation,
    const std::string& reason) {
    if (decision == nullptr) {
        return;
    }
    decision->failSoftRecommendation = recommendation;
    decision->failSoftReason = reason;
    decision->currentHideButFailSoftWouldShowOrWarn =
        !decision->displayedInFinalSnapshot &&
        (recommendation == "prefer-display-with-warning" ||
         recommendation == "prefer-lower-priority-display");
    RefreshScoreSummary(decision);
}

bool DecisionHasExplicitHighNegative(
    const BrainDisplayDecisionRecord& decision) {
    return decision.hardBlock ||
           (decision.confidenceLevel == "high" &&
            decision.negativeScore >= 0.80 &&
            decision.negativeScore > decision.positiveScore);
}

void ApplyFailSoftRecommendation(BrainDisplayDecisionRecord* decision) {
    if (decision == nullptr) {
        return;
    }

    if (decision->decision == "display-accepted" ||
        decision->decision == "display-format-only") {
        MarkFailSoftRecommendation(
            decision,
            "keep-current-display",
            "current-display-already-keeps-candidate");
        return;
    }

    if (decision->decision == "display-rejected-non-displayable") {
        MarkFailSoftRecommendation(
            decision,
            "hard-block-hide",
            decision->reason == "empty-frequency"
                ? "empty-frequency-impossible-to-render"
                : "offline-row-impossible-to-render");
        return;
    }

    if (decision->decision == "display-rejected-duplicate") {
        if (!decision->relationFactPresent) {
            MarkFailSoftRecommendation(
                decision,
                "keep-current-hide",
                "identical-role-callsign-frequency-duplicate");
        } else {
            MarkFailSoftRecommendation(
                decision,
                "prefer-lower-priority-display",
                "duplicate-has-unique-relation-evidence");
        }
        return;
    }

    if (decision->decision == "display-deferred-by-stage") {
        MarkFailSoftRecommendation(
            decision,
            "prefer-stage-defer",
            "stage-policy-defer-with-explicit-reason");
        return;
    }

    if (decision->decision == "display-rejected-filtered") {
        if (decision->acceptedByRelevance &&
            !DecisionHasExplicitHighNegative(*decision)) {
            MarkFailSoftRecommendation(
                decision,
                "prefer-display-with-warning",
                "filtered-accepted-row-needs-fail-soft-warning");
        } else {
            MarkFailSoftRecommendation(
                decision,
                "keep-current-hide",
                "filtered-row-has-strong-negative-evidence");
        }
        return;
    }

    if (decision->decision == "display-rejected-unknown") {
        MarkFailSoftRecommendation(
            decision,
            "needs-more-evidence",
            "unknown-relation-is-not-automatic-hide");
        return;
    }

    if (decision->decision ==
        "display-rejected-center-fallback-hidden") {
        if (decision->hardBlock) {
            MarkFailSoftRecommendation(
                decision,
                "hard-block-hide",
                "hard-blocked-hidden-center");
            return;
        }
        if (decision->acceptedByRelevance &&
            decision->reason == "fallback-hidden" &&
            !DecisionHasExplicitHighNegative(*decision)) {
            MarkFailSoftRecommendation(
                decision,
                "prefer-display-with-warning",
                "fallback-hidden-accepted-row-fail-soft");
            return;
        }
        if (decision->acceptedByRelevance &&
            decision->reason == "relation-fact-hidden" &&
            !DecisionHasExplicitHighNegative(*decision)) {
            MarkFailSoftRecommendation(
                decision,
                "needs-more-evidence",
                "hidden-relation-fact-is-not-high-confidence-negative");
            return;
        }
        MarkFailSoftRecommendation(
            decision,
            "keep-current-hide",
            "hidden-center-has-strong-negative-evidence");
        return;
    }

    if (decision->displayedInFinalSnapshot) {
        MarkFailSoftRecommendation(
            decision,
            "keep-current-display",
            "current-final-snapshot-displays-candidate");
        return;
    }

    MarkFailSoftRecommendation(
        decision,
        "needs-more-evidence",
        "no-specific-fail-soft-rule");
}

void ApplyFailSoftPreview(BrainDisplayIntentOutput* output) {
    if (output == nullptr) {
        return;
    }

    BrainDisplayFailSoftPreviewSummary summary;
    for (auto& decision : output->displayDecisions) {
        ApplyFailSoftRecommendation(&decision);
        ++summary.failSoftPreviewCount;
        if (decision.failSoftRecommendation == "keep-current-display") {
            ++summary.recommendKeepDisplayCount;
        } else if (decision.failSoftRecommendation == "keep-current-hide") {
            ++summary.recommendKeepHideCount;
        } else if (decision.failSoftRecommendation ==
                   "prefer-display-with-warning") {
            ++summary.recommendDisplayWithWarningCount;
        } else if (decision.failSoftRecommendation ==
                   "prefer-stage-defer") {
            ++summary.recommendStageDeferCount;
        } else if (decision.failSoftRecommendation ==
                   "prefer-lower-priority-display") {
            ++summary.recommendLowerPriorityDisplayCount;
        } else if (decision.failSoftRecommendation == "hard-block-hide") {
            ++summary.recommendHardBlockHideCount;
        } else if (decision.failSoftRecommendation ==
                   "needs-more-evidence") {
            ++summary.recommendNeedsMoreEvidenceCount;
        }
        if (decision.currentHideButFailSoftWouldShowOrWarn) {
            ++summary.currentHideButFailSoftWouldShowOrWarnCount;
        }
    }
    output->failSoftPreviewSummary = summary;
}

std::string FormatDistanceAnnotation(double distanceNm) {
    const auto rounded =
        static_cast<int>(std::round(std::max(0.0, distanceNm)));
    return std::to_string(rounded) + "nm";
}

FinalDisplayStationSnapshot ToFinalDisplayStation(
    const BoardStationSnapshot& station,
    const RadioStateSnapshot& radios) {
    FinalDisplayStationSnapshot displayStation;
    displayStation.role = station.role;
    displayStation.callsign = station.callsign;
    displayStation.frequency = station.frequency;
    displayStation.sourceEvidenceId = station.sourceEvidenceId;
    displayStation.sourceEvidenceType = station.sourceEvidenceType;
    displayStation.sourceEvidenceDomain = station.sourceEvidenceDomain;
    displayStation.sourceDecisionId = station.sourceDecisionId;
    displayStation.sourceEvidenceLinkStatus = station.sourceEvidenceLinkStatus;
    displayStation.sourceEvidenceMissingReason =
        station.sourceEvidenceMissingReason;
    displayStation.stableCompletionKey = station.stableCompletionKey;
    displayStation.tuned =
        radios.valid && IsCom1TunedToFrequency(radios, station.frequency);
    displayStation.sectorActive = station.sectorActive;
    displayStation.online = station.online;
    displayStation.offline = station.offline;
    displayStation.hasRouteEntryDistance = station.hasRouteEntryDistance;
    displayStation.routeEntryDistanceNm = station.routeEntryDistanceNm;
    displayStation.polygonKey = station.polygonKey;
    return displayStation;
}

bool AppendUnique(
    const FinalDisplayStationSnapshot& station,
    FinalDisplaySnapshot* board,
    std::unordered_set<std::string>* keys) {
    if (board == nullptr || keys == nullptr) {
        return false;
    }
    if (keys->insert(StationKey(station)).second) {
        board->stations.push_back(station);
        board->available = true;
        return true;
    }
    return false;
}

const BrainDisplayRelationFact* FindRelationFact(
    const BoardStationSnapshot& station,
    const std::vector<BrainDisplayRelationFact>& relationFacts) {
    const auto stationCallsign = NormalizeKey(station.callsign);
    const auto stationFrequency = NormalizeFrequency(station.frequency);
    if (stationCallsign.empty() || stationFrequency.empty()) {
        return nullptr;
    }

    const auto found = std::find_if(
        relationFacts.begin(),
        relationFacts.end(),
        [&](const auto& fact) {
            return NormalizeKey(fact.callsign) == stationCallsign &&
                   NormalizeFrequency(fact.frequency) == stationFrequency;
        });
    return found == relationFacts.end() ? nullptr : &*found;
}

bool IsFinalDisplayRelation(DisplayRelation relation) {
    return relation == DisplayRelation::CurrentPolygon ||
           relation == DisplayRelation::NextPolygon ||
           relation == DisplayRelation::ArrivalPrep;
}

void ApplySourceLinkageFromStation(
    const BoardStationSnapshot& station,
    BrainDisplayDecisionRecord* decision);
void ApplyStableKeyFromStation(
    const BoardStationSnapshot& station,
    BoardSource sourceBoard,
    const BrainDisplayIntentInput& input,
    BrainDisplayDecisionRecord* decision);

BrainDisplayDecisionRecord BuildBaseDecision(
    const BoardStationSnapshot& station,
    BoardSource sourceBoard,
    const BrainDisplayIntentInput& input,
    int sequence) {
    BrainDisplayDecisionRecord decision;
    decision.decisionId = DecisionIdForStation(sourceBoard, station, sequence);
    decision.callsign = station.callsign;
    decision.frequency = station.frequency;
    decision.role = station.role;
    decision.sourceBoard = sourceBoard;
    decision.workflowStage = input.workflowStage;
    decision.acceptedByRelevance = StationIsAcceptedRelevanceSubject(station);
    decision.stationPolygonKey = station.polygonKey;
    decision.currentPolygonKey = input.currentPolygonKey;
    decision.nextPolygonKey = input.nextPolygonKey;
    decision.arrivalPolygonKey = input.arrivalPolygonKey;
    decision.displayable = IsDisplayableStation(station);
    ApplySourceLinkageFromStation(station, &decision);
    ApplyStableKeyFromStation(station, sourceBoard, input, &decision);
    if (const auto* relationFact =
            FindRelationFact(station, input.relationFacts)) {
        decision.relationFactPresent = true;
        decision.relationFactValue = relationFact->displayRelation;
        decision.acceptedRelation = relationFact->displayRelation;
    }
    return decision;
}

void MarkDecisionOutcome(
    BrainDisplayDecisionRecord* decision,
    const std::string& outcome,
    const std::string& reason,
    const std::string& confidence,
    double positiveScore,
    double negativeScore) {
    if (decision == nullptr) {
        return;
    }
    decision->decision = outcome;
    decision->reason = reason;
    decision->confidenceLevel = confidence;
    decision->positiveScore = positiveScore;
    decision->negativeScore = negativeScore;
    RefreshScoreSummary(decision);
}

bool DisplayRowsContainStation(
    const FinalDisplaySnapshot& board,
    const BrainDisplayDecisionRecord& decision) {
    return std::any_of(
        board.stations.begin(),
        board.stations.end(),
        [&](const auto& station) {
            return static_cast<int>(station.role) ==
                       static_cast<int>(decision.role) &&
                   NormalizeKey(station.callsign) ==
                       NormalizeKey(decision.callsign) &&
                   NormalizeFrequency(station.frequency) ==
                       NormalizeFrequency(decision.frequency);
        });
}

bool DecisionMatchesStation(
    const BrainDisplayDecisionRecord& decision,
    const FinalDisplayStationSnapshot& station) {
    return static_cast<int>(station.role) == static_cast<int>(decision.role) &&
           NormalizeKey(station.callsign) == NormalizeKey(decision.callsign) &&
           NormalizeFrequency(station.frequency) ==
               NormalizeFrequency(decision.frequency);
}

std::string DecisionStationKey(const BrainDisplayDecisionRecord& decision) {
    return std::to_string(static_cast<int>(decision.role)) + "|" +
           NormalizeKey(decision.callsign) + "|" +
           NormalizeFrequency(decision.frequency);
}

std::string StationKeyFromParts(
    StationRole role,
    const std::string& callsign,
    const std::string& frequency,
    const std::string& endpoint,
    const std::string& airportIcao) {
    std::ostringstream stream;
    stream << "row|" << endpoint << "|" << airportIcao << "|"
           << static_cast<int>(role) << "|" << NormalizeKey(callsign)
           << "|" << NormalizeFrequency(frequency);
    return stream.str();
}

struct StableKeyDerivation {
    std::string key;
    std::string source;
    std::string status;
    std::string reason;
    bool includesCallsign = false;
    bool includesRole = false;
    bool includesFrequency = false;
    bool includesEndpoint = false;
    bool includesAirport = false;
};

struct SourceOwnedPlanContext {
    std::string value;
    bool available = false;
    std::string source;
};

struct SourceOwnedFallbackGeometryDiagnostic {
    std::string sourceOwnedStableCompletionKey;
    bool sourceOwnedStableCompletionKeyPresent = false;
    std::string sourceOwnedStableCompletionKeySource;
    std::string sourceOwnedStableCompletionKeyShape;
    std::string generatedFallbackStableCompletionKey;
    bool sourceOwnedMatchesGeneratedFallback = false;
    std::string sourceOwnedKeyMismatchReason;
    std::string sourceOwnedKeyPlanContext;
    bool sourceOwnedKeyPlanContextAvailable = false;
    std::string sourceOwnedKeyPlanContextSource;
    bool sourceOwnedKeyMigrationReady = false;
    bool sourceOwnedKeyBehaviorConsumerEnabled = false;
};

std::string PlanContextPart(const std::string& value) {
    const auto normalized = NormalizeKey(value);
    return normalized.empty() ? "none" : normalized;
}

SourceOwnedPlanContext BuildSourceOwnedPlanContext(
    const BrainDisplayIntentInput& input) {
    SourceOwnedPlanContext context;
    const auto hasPolygonContext =
        !input.currentPolygonKey.empty() ||
        !input.nextPolygonKey.empty() ||
        !input.arrivalPolygonKey.empty();
    if (!hasPolygonContext) {
        context.value = "plan-context-unavailable";
        context.available = false;
        context.source = "unavailable";
        return context;
    }

    context.available = true;
    context.source = "display-intent-polygon-context";
    context.value = "current=" + PlanContextPart(input.currentPolygonKey) +
                    ";next=" + PlanContextPart(input.nextPolygonKey) +
                    ";arrival=" + PlanContextPart(input.arrivalPolygonKey) +
                    ";stage=" + WorkflowStageToken(input.workflowStage);
    return context;
}

std::string SourceOwnedFallbackGeometryKey(
    StationRole role,
    const std::string& callsign,
    const std::string& frequency,
    const std::string& planContext) {
    std::ostringstream stream;
    stream << "source-owned:fallback-polygon-geometry|"
           << NormalizeKey(callsign) << "|" << static_cast<int>(role)
           << "|" << NormalizeFrequency(frequency) << "|" << planContext;
    return stream.str();
}

SourceOwnedFallbackGeometryDiagnostic BuildSourceOwnedFallbackGeometryDiagnostic(
    const BrainDisplayIntentInput& input,
    StationRole role,
    const std::string& callsign,
    const std::string& frequency,
    const std::string& generatedFallbackKey) {
    SourceOwnedFallbackGeometryDiagnostic diagnostic;
    const auto planContext = BuildSourceOwnedPlanContext(input);
    diagnostic.generatedFallbackStableCompletionKey = generatedFallbackKey;
    diagnostic.sourceOwnedStableCompletionKeyShape =
        "source-owned:fallback-polygon-geometry|callsign|role|frequency|plan-context";
    diagnostic.sourceOwnedKeyPlanContext = planContext.value;
    diagnostic.sourceOwnedKeyPlanContextAvailable = planContext.available;
    diagnostic.sourceOwnedKeyPlanContextSource = planContext.source;
    diagnostic.sourceOwnedKeyBehaviorConsumerEnabled = false;
    if (!NormalizeKey(callsign).empty() &&
        !NormalizeFrequency(frequency).empty()) {
        diagnostic.sourceOwnedStableCompletionKey =
            SourceOwnedFallbackGeometryKey(
                role,
                callsign,
                frequency,
                planContext.value);
        diagnostic.sourceOwnedStableCompletionKeyPresent = true;
        diagnostic.sourceOwnedStableCompletionKeySource = "source-owned";
    }
    diagnostic.sourceOwnedMatchesGeneratedFallback =
        diagnostic.sourceOwnedStableCompletionKeyPresent &&
        !diagnostic.generatedFallbackStableCompletionKey.empty();
    if (!diagnostic.sourceOwnedMatchesGeneratedFallback) {
        diagnostic.sourceOwnedKeyMismatchReason =
            diagnostic.sourceOwnedStableCompletionKeyPresent
                ? "generated-fallback-key-missing"
                : "source-owned-key-missing";
    }
    diagnostic.sourceOwnedKeyMigrationReady =
        diagnostic.sourceOwnedMatchesGeneratedFallback &&
        diagnostic.sourceOwnedKeyPlanContextAvailable &&
        !diagnostic.sourceOwnedKeyBehaviorConsumerEnabled;
    return diagnostic;
}

std::string NormalizeSourceOwnedFallbackShadowGateSource(
    bool enabled,
    const std::string& source) {
    if (source == "default" || source == "settings-store" ||
        source == "harness") {
        return source;
    }
    if (source.empty()) {
        return enabled ? "harness" : "default";
    }
    return "unknown";
}

std::string NormalizeSourceOwnedFallbackLiveConsumptionProposalGateSource(
    bool enabled,
    const std::string& source) {
    return NormalizeSourceOwnedFallbackShadowGateSource(enabled, source);
}

std::string NormalizeSourceOwnedFallbackLiveConsumptionGateSource(
    bool enabled,
    const std::string& source) {
    return NormalizeSourceOwnedFallbackShadowGateSource(enabled, source);
}

void ApplySourceOwnedFallbackGeometryDiagnostic(
    const SourceOwnedFallbackGeometryDiagnostic& diagnostic,
    BrainDisplayStableKeyAuditRecord* record) {
    if (record == nullptr) {
        return;
    }
    record->sourceOwnedStableCompletionKey =
        diagnostic.sourceOwnedStableCompletionKey;
    record->sourceOwnedStableCompletionKeyPresent =
        diagnostic.sourceOwnedStableCompletionKeyPresent;
    record->sourceOwnedStableCompletionKeySource =
        diagnostic.sourceOwnedStableCompletionKeySource;
    record->sourceOwnedStableCompletionKeyShape =
        diagnostic.sourceOwnedStableCompletionKeyShape;
    record->generatedFallbackStableCompletionKey =
        diagnostic.generatedFallbackStableCompletionKey;
    record->sourceOwnedMatchesGeneratedFallback =
        diagnostic.sourceOwnedMatchesGeneratedFallback;
    record->sourceOwnedKeyMismatchReason =
        diagnostic.sourceOwnedKeyMismatchReason;
    record->sourceOwnedKeyPlanContext = diagnostic.sourceOwnedKeyPlanContext;
    record->sourceOwnedKeyPlanContextAvailable =
        diagnostic.sourceOwnedKeyPlanContextAvailable;
    record->sourceOwnedKeyPlanContextSource =
        diagnostic.sourceOwnedKeyPlanContextSource;
    record->sourceOwnedKeyMigrationReady =
        diagnostic.sourceOwnedKeyMigrationReady;
    record->sourceOwnedKeyBehaviorConsumerEnabled =
        diagnostic.sourceOwnedKeyBehaviorConsumerEnabled;
}

void ApplySourceOwnedFallbackGeometryDiagnostic(
    const SourceOwnedFallbackGeometryDiagnostic& diagnostic,
    FinalDisplayStationSnapshot* station) {
    if (station == nullptr) {
        return;
    }
    station->sourceOwnedStableCompletionKey =
        diagnostic.sourceOwnedStableCompletionKey;
    station->generatedFallbackStableCompletionKey =
        diagnostic.generatedFallbackStableCompletionKey;
    station->sourceOwnedStableCompletionKeyPresent =
        diagnostic.sourceOwnedStableCompletionKeyPresent;
    station->sourceOwnedKeyMigrationReady =
        diagnostic.sourceOwnedKeyMigrationReady;
    station->sourceOwnedKeyPlanContextAvailable =
        diagnostic.sourceOwnedKeyPlanContextAvailable;
    station->sourceOwnedKeyBehaviorConsumerEnabled =
        diagnostic.sourceOwnedKeyBehaviorConsumerEnabled;
}

StableKeyDerivation DeriveStableKey(
    const std::string& explicitStableKey,
    const std::string& sourceEvidenceId,
    const std::string& sourceDecisionId,
    const std::string& sourceEvidenceLinkStatus,
    StationRole role,
    const std::string& callsign,
    const std::string& frequency,
    const std::string& endpoint,
    const std::string& airportIcao) {
    StableKeyDerivation derived;
    if (!explicitStableKey.empty()) {
        derived.key = explicitStableKey;
        derived.source = "final-row";
        derived.status = "stable";
        derived.reason = "explicit-stable-completion-key";
        return derived;
    }
    if (!sourceEvidenceId.empty()) {
        derived.key = "source-evidence|" + sourceEvidenceId;
        derived.source = "source-evidence";
        derived.status = "stable";
        derived.reason = "source-evidence-id";
        return derived;
    }
    if (!sourceDecisionId.empty()) {
        derived.key = "display-decision|" + sourceDecisionId;
        derived.source = "display-decision";
        derived.status = "fallback-derived";
        derived.reason = "source-decision-without-source-evidence";
        return derived;
    }
    if (sourceEvidenceLinkStatus == "synthetic-row") {
        derived.key =
            StationKeyFromParts(role, callsign, frequency, endpoint, airportIcao);
        derived.source = "synthetic-fixture";
        derived.status = "synthetic";
        derived.reason = "synthetic-row-generated-key";
        derived.includesCallsign = true;
        derived.includesRole = true;
        derived.includesFrequency = true;
        derived.includesEndpoint = true;
        derived.includesAirport = true;
        return derived;
    }
    if (sourceEvidenceLinkStatus == "legacy-row") {
        derived.key =
            StationKeyFromParts(role, callsign, frequency, endpoint, airportIcao);
        derived.source = "legacy";
        derived.status = "fallback-derived";
        derived.reason = "legacy-row-generated-key";
        derived.includesCallsign = true;
        derived.includesRole = true;
        derived.includesFrequency = true;
        derived.includesEndpoint = true;
        derived.includesAirport = true;
        return derived;
    }
    if (!callsign.empty() && !frequency.empty()) {
        derived.key =
            StationKeyFromParts(role, callsign, frequency, endpoint, airportIcao);
        derived.source = "generated-fallback";
        derived.status = "fallback-derived";
        derived.reason = "callsign-role-frequency-endpoint-generated-key";
        derived.includesCallsign = true;
        derived.includesRole = true;
        derived.includesFrequency = true;
        derived.includesEndpoint = true;
        derived.includesAirport = true;
        return derived;
    }
    derived.source = "unavailable";
    derived.status = "missing";
    derived.reason = "insufficient-row-identity";
    return derived;
}

std::string SourceLinkStatusForStation(const BoardStationSnapshot& station) {
    if (!station.sourceEvidenceLinkStatus.empty()) {
        return station.sourceEvidenceLinkStatus;
    }
    if (!station.sourceEvidenceId.empty()) {
        return "linked";
    }
    if (!station.sourceDecisionId.empty()) {
        return "missing-from-display-decision";
    }
    return "unavailable";
}

std::string SourceLinkMissingReasonForStation(
    const BoardStationSnapshot& station,
    const std::string& status) {
    if (!station.sourceEvidenceMissingReason.empty()) {
        return station.sourceEvidenceMissingReason;
    }
    if (status == "linked") {
        return {};
    }
    if (status == "missing-from-display-decision") {
        return "source-decision-present-without-source-evidence-id";
    }
    if (status == "synthetic-row") {
        return "synthetic-row-no-upstream-source-evidence";
    }
    if (status == "legacy-row") {
        return "legacy-row-source-evidence-unavailable";
    }
    if (status == "unknown") {
        return "source-evidence-linkage-unknown";
    }
    return "source-evidence-not-provided";
}

void ApplyStableKeyFromStation(
    const BoardStationSnapshot& station,
    BoardSource sourceBoard,
    const BrainDisplayIntentInput& input,
    BrainDisplayDecisionRecord* decision) {
    if (decision == nullptr) {
        return;
    }

    const auto endpoint = BoardSourceToken(sourceBoard);
    std::string airportIcao;
    switch (sourceBoard) {
        case BoardSource::Departure:
            airportIcao = input.departureBoard.airportIcao;
            break;
        case BoardSource::Arrival:
            airportIcao = input.arrivalBoard.airportIcao;
            break;
        case BoardSource::Enroute:
            airportIcao = input.enrouteBoard.airportIcao;
            break;
        case BoardSource::None:
        default:
            break;
    }
    const auto derived = DeriveStableKey(
        station.stableCompletionKey,
        station.sourceEvidenceId,
        station.sourceDecisionId,
        station.sourceEvidenceLinkStatus,
        station.role,
        station.callsign,
        station.frequency,
        endpoint,
        airportIcao);
    decision->completionStableKey = derived.key;
}

void ApplySourceLinkageFromStation(
    const BoardStationSnapshot& station,
    BrainDisplayDecisionRecord* decision) {
    if (decision == nullptr) {
        return;
    }

    decision->sourceDecisionId = station.sourceDecisionId;
    decision->sourceEvidenceId = station.sourceEvidenceId;
    decision->sourceEvidenceType = station.sourceEvidenceType;
    decision->sourceEvidenceDomain = station.sourceEvidenceDomain;
    decision->sourceEvidenceLinkStatus = SourceLinkStatusForStation(station);
    decision->sourceEvidenceLinked =
        decision->sourceEvidenceLinkStatus == "linked" &&
        !decision->sourceEvidenceId.empty();
    decision->sourceEvidenceMissingReason = SourceLinkMissingReasonForStation(
        station,
        decision->sourceEvidenceLinkStatus);
    decision->sourceDecisionLinked = !decision->sourceDecisionId.empty();
    decision->displayDecisionLinked = !decision->decisionId.empty();
    decision->capDecisionLinked = false;
    decision->linkageConfidence =
        decision->sourceEvidenceLinked
            ? "high"
            : (decision->sourceDecisionLinked ? "medium" : "none");
    decision->linkageFallbackUsed = !decision->sourceEvidenceLinked;
}

std::string AirportIcaoForSource(
    const BrainDisplayIntentInput& input,
    BoardSource source,
    const FinalDisplaySnapshot& finalDisplay) {
    switch (source) {
        case BoardSource::Departure:
            return input.departureBoard.airportIcao.empty()
                       ? finalDisplay.airportIcao
                       : input.departureBoard.airportIcao;
        case BoardSource::Arrival:
            return input.arrivalBoard.airportIcao.empty()
                       ? finalDisplay.airportIcao
                       : input.arrivalBoard.airportIcao;
        case BoardSource::Enroute:
            return input.enrouteBoard.airportIcao.empty()
                       ? finalDisplay.airportIcao
                       : input.enrouteBoard.airportIcao;
        case BoardSource::None:
        default:
            return finalDisplay.airportIcao;
    }
}

std::string OverlayHiddenOutcome(const BrainDisplayDecisionRecord& decision) {
    if (decision.decision == "display-rejected-non-displayable") {
        return "hidden-non-displayable";
    }
    if (decision.duplicateSuppressed ||
        decision.decision == "display-rejected-duplicate") {
        return "hidden-duplicate";
    }
    if (decision.stageSuppressed ||
        decision.decision == "display-deferred-by-stage") {
        return "hidden-stage-deferred";
    }
    return "hidden-other";
}

std::string OverlayHiddenReason(const BrainDisplayDecisionRecord& decision) {
    const auto outcome = OverlayHiddenOutcome(decision);
    if (outcome == "hidden-non-displayable") {
        return "non-displayable-before-cap";
    }
    if (outcome == "hidden-duplicate") {
        return "duplicate-suppressed-before-cap";
    }
    if (outcome == "hidden-stage-deferred") {
        return "stage-deferred-before-cap";
    }
    return decision.reason.empty() ? "hidden-before-cap" : decision.reason;
}

void PopulateOverlayRecordFromDecision(
    const BrainDisplayDecisionRecord& decision,
    const BrainDisplayIntentInput& input,
    const FinalDisplaySnapshot& finalDisplay,
    BrainDisplayOverlayCapDecisionRecord* record) {
    if (record == nullptr) {
        return;
    }

    record->sourceDecisionId =
        decision.sourceDecisionId.empty() ? decision.decisionId
                                          : decision.sourceDecisionId;
    record->sourceEvidenceId = decision.sourceEvidenceId;
    record->sourceEvidenceType = decision.sourceEvidenceType;
    record->sourceEvidenceDomain = decision.sourceEvidenceDomain;
    record->sourceEvidenceLinked = decision.sourceEvidenceLinked;
    record->sourceEvidenceLinkStatus = decision.sourceEvidenceLinkStatus;
    record->sourceEvidenceMissingReason = decision.sourceEvidenceMissingReason;
    record->sourceDecisionLinked = decision.sourceDecisionLinked;
    record->displayDecisionLinked = true;
    record->capDecisionLinked = true;
    record->linkageConfidence = decision.linkageConfidence;
    record->linkageFallbackUsed = decision.linkageFallbackUsed;
    record->displayDecisionId = decision.decisionId;
    record->subjectKey = DecisionStationKey(decision);
    record->callsign = decision.callsign;
    record->role = decision.role;
    record->frequency = decision.frequency;
    record->endpoint = BoardSourceToken(decision.sourceBoard);
    record->airportIcao =
        AirportIcaoForSource(input, decision.sourceBoard, finalDisplay);
    record->displayRelation = decision.finalRelation;
    record->workflowStage = decision.workflowStage;
    record->confidenceLevel = decision.confidenceLevel;
    record->fallbackUsed = decision.fallbackRelationUsed;
    record->hardBlock = decision.hardBlock;
    record->hardBlockReason = decision.hardBlock ? decision.reason : "";
    record->stableCompletionKey = decision.completionStableKey;
}

void BuildOverlayCapLedger(
    const BrainDisplayIntentInput& input,
    BrainDisplayIntentOutput* output) {
    if (output == nullptr) {
        return;
    }

    BrainDisplayOverlayCapSummary summary;
    summary.capLimit = kOverlayCapDiagnosticLimit;
    summary.candidateBeforeCapCount =
        static_cast<int>(output->finalDisplay.stations.size());
    summary.visibleAfterCapCount = std::min(
        summary.candidateBeforeCapCount,
        kOverlayCapDiagnosticLimit);
    summary.cappedHiddenCount =
        std::max(0, summary.candidateBeforeCapCount - summary.visibleAfterCapCount);
    summary.moreAtcCount = summary.cappedHiddenCount;
    summary.capLedgerBrainOwned = true;
    summary.overlayCapBehaviorChanged = false;

    output->overlayCapDecisions.clear();
    output->overlayCapDecisions.reserve(
        output->finalDisplay.stations.size() + output->displayDecisions.size());

    std::unordered_set<std::string> matchedDecisionIds;
    int retainedVisibleRows = 0;
    int cappedHiddenRows = 0;
    int moreAtcCount = 0;
    int sequence = 0;

    for (std::size_t index = 0; index < output->finalDisplay.stations.size();
         ++index) {
        auto& station = output->finalDisplay.stations[index];
        const BrainDisplayDecisionRecord* matchedDecision = nullptr;
        for (const auto& decision : output->displayDecisions) {
            if (matchedDecisionIds.find(decision.decisionId) !=
                matchedDecisionIds.end()) {
                continue;
            }
            if (!decision.displayedInFinalSnapshot ||
                decision.duplicateSuppressed ||
                !DecisionMatchesStation(decision, station)) {
                continue;
            }
            matchedDecision = &decision;
            matchedDecisionIds.insert(decision.decisionId);
            break;
        }

        BrainDisplayOverlayCapDecisionRecord record;
        record.overlayCapDecisionId =
            "overlay-cap|" + std::to_string(sequence++);
        record.subjectKey = StationKey(station);
        record.callsign = station.callsign;
        record.role = station.role;
        record.frequency = station.frequency;
        record.endpoint = BoardSourceToken(output->finalDisplay.source);
        record.airportIcao = output->finalDisplay.airportIcao;
        record.displayRelation = station.displayRelation;
        record.workflowStage = input.workflowStage;
        record.boardIndexBeforeCap = static_cast<int>(index);
        record.capLimit = kOverlayCapDiagnosticLimit;
        record.visibleBeforeCap = true;
        record.moreAtcCountBeforeRow = moreAtcCount;

        if (matchedDecision != nullptr) {
            PopulateOverlayRecordFromDecision(
                *matchedDecision,
                input,
                output->finalDisplay,
                &record);
            record.subjectKey = StationKey(station);
            record.displayRelation = station.displayRelation;
        } else {
            record.stableCompletionKey = station.stableCompletionKey;
        }

        if (static_cast<int>(index) < kOverlayCapDiagnosticLimit) {
            record.boardIndexAfterCap = retainedVisibleRows;
            record.visibleAfterCap = true;
            record.capReason = "within-overlay-limit";
            record.finalDisplayOutcome = "visible";
            ++retainedVisibleRows;
        } else {
            record.visibleAfterCap = false;
            record.cappedByOverlayLimit = true;
            record.capReason = "overlay-row-limit";
            record.contributesToMoreAtcCount = true;
            record.finalDisplayOutcome = "hidden-overlay-cap";
            ++cappedHiddenRows;
            ++moreAtcCount;
            ++summary.contributesToMoreAtcCount;
        }

        record.moreAtcCountAfterRow = moreAtcCount;
        record.retainedVisibleRowCount = retainedVisibleRows;
        record.cappedHiddenRowCount = cappedHiddenRows;
        station.overlayCapDecisionId = record.overlayCapDecisionId;
        if (matchedDecision != nullptr) {
            station.displayDecisionId = matchedDecision->decisionId;
        }
        output->overlayCapDecisions.push_back(std::move(record));
    }

    for (const auto& decision : output->displayDecisions) {
        if (matchedDecisionIds.find(decision.decisionId) !=
            matchedDecisionIds.end()) {
            continue;
        }
        if (decision.displayedInFinalSnapshot && !decision.duplicateSuppressed) {
            continue;
        }

        BrainDisplayOverlayCapDecisionRecord record;
        record.overlayCapDecisionId =
            "overlay-cap|" + std::to_string(sequence++);
        PopulateOverlayRecordFromDecision(
            decision,
            input,
            output->finalDisplay,
            &record);
        record.capLimit = kOverlayCapDiagnosticLimit;
        record.visibleBeforeCap = false;
        record.visibleAfterCap = false;
        record.cappedByOverlayLimit = false;
        record.capReason = OverlayHiddenReason(decision);
        record.moreAtcCountBeforeRow = moreAtcCount;
        record.moreAtcCountAfterRow = moreAtcCount;
        record.retainedVisibleRowCount = retainedVisibleRows;
        record.cappedHiddenRowCount = cappedHiddenRows;
        record.finalDisplayOutcome = OverlayHiddenOutcome(decision);
        ++summary.nonCappedHiddenCount;
        if (record.finalDisplayOutcome == "hidden-duplicate") {
            ++summary.duplicateHiddenCount;
        }
        if (record.finalDisplayOutcome == "hidden-stage-deferred") {
            ++summary.stageDeferredHiddenCount;
        }
        output->overlayCapDecisions.push_back(std::move(record));
    }

    summary.overlayCapDecisionCount =
        static_cast<int>(output->overlayCapDecisions.size());
    output->overlayCapSummary = summary;
}

void BuildSourceLinkSummary(BrainDisplayIntentOutput* output) {
    if (output == nullptr) {
        return;
    }

    BrainDisplaySourceLinkSummary summary;
    summary.displaySourceLinkDecisionCount =
        static_cast<int>(output->displayDecisions.size());
    summary.sourceLinkageBrainOwned = true;
    summary.displayBehaviorChanged = false;

    for (const auto& decision : output->displayDecisions) {
        if (decision.sourceEvidenceLinked) {
            ++summary.displaySourceLinkedCount;
        } else {
            ++summary.displaySourceMissingCount;
        }
        if (decision.sourceEvidenceLinkStatus == "synthetic-row") {
            ++summary.syntheticRowCount;
        } else if (decision.sourceEvidenceLinkStatus == "legacy-row") {
            ++summary.legacyRowCount;
        } else if (decision.sourceEvidenceLinkStatus == "unknown") {
            ++summary.unknownSourceLinkCount;
        }
    }

    for (const auto& decision : output->overlayCapDecisions) {
        if (decision.sourceEvidenceLinked) {
            ++summary.capSourceLinkedCount;
        } else {
            ++summary.capSourceMissingCount;
        }
    }

    output->sourceLinkSummary = summary;
}

BrainDisplayStableKeyAuditRecord BuildStableKeyAuditFromDecision(
    const BrainDisplayDecisionRecord& decision,
    const BrainDisplayIntentInput& input,
    const FinalDisplaySnapshot& finalDisplay,
    int index) {
    BrainDisplayStableKeyAuditRecord record;
    record.stableKeyAuditDecisionId =
        "stable-key|display|" + std::to_string(index);
    record.displayDecisionId = decision.decisionId;
    record.sourceEvidenceId = decision.sourceEvidenceId;
    record.sourceDecisionId = decision.sourceDecisionId;
    record.subjectKey = DecisionStationKey(decision);

    const auto endpoint = BoardSourceToken(decision.sourceBoard);
    const auto airportIcao =
        AirportIcaoForSource(input, decision.sourceBoard, finalDisplay);
    const auto derived = DeriveStableKey(
        {},
        decision.sourceEvidenceId,
        decision.sourceDecisionId,
        decision.sourceEvidenceLinkStatus,
        decision.role,
        decision.callsign,
        decision.frequency,
        endpoint,
        airportIcao);
    record.stableCompletionKey = derived.key;
    record.stableCompletionKeyPresent = !derived.key.empty();
    record.stableCompletionKeySource = derived.source;
    record.stableCompletionKeyStatus = derived.status;
    record.keyDerivationReason = derived.reason;
    record.keyIncludesCallsign = derived.includesCallsign;
    record.keyIncludesRole = derived.includesRole;
    record.keyIncludesFrequency = derived.includesFrequency;
    record.keyIncludesEndpoint = derived.includesEndpoint;
    record.keyIncludesAirport = derived.includesAirport;
    record.keyMatchesDisplayDecision =
        !decision.completionStableKey.empty() &&
        decision.completionStableKey == record.stableCompletionKey;
    if (derived.source == "generated-fallback" &&
        decision.fallbackRelationUsed && decision.role == StationRole::Center) {
        ApplySourceOwnedFallbackGeometryDiagnostic(
            BuildSourceOwnedFallbackGeometryDiagnostic(
                input,
                decision.role,
                decision.callsign,
                decision.frequency,
                derived.key),
            &record);
    }
    record.keyAuditWarning =
        record.stableCompletionKeyStatus != "stable";
    record.keyAuditWarningReason =
        record.keyAuditWarning ? record.stableCompletionKeyStatus : "";
    return record;
}

BrainDisplayStableKeyAuditRecord BuildStableKeyAuditFromFinalRow(
    const FinalDisplayStationSnapshot& station,
    const FinalDisplaySnapshot& finalDisplay,
    int index) {
    BrainDisplayStableKeyAuditRecord record;
    record.stableKeyAuditDecisionId =
        "stable-key|final-row|" + std::to_string(index);
    record.displayDecisionId = station.displayDecisionId;
    record.overlayCapDecisionId = station.overlayCapDecisionId;
    record.sourceEvidenceId = station.sourceEvidenceId;
    record.sourceDecisionId = station.sourceDecisionId;
    record.subjectKey = StationKey(station);

    const auto derived = DeriveStableKey(
        station.stableCompletionKey,
        station.sourceEvidenceId,
        station.sourceDecisionId,
        station.sourceEvidenceLinkStatus,
        station.role,
        station.callsign,
        station.frequency,
        BoardSourceToken(finalDisplay.source),
        finalDisplay.airportIcao);
    record.stableCompletionKey = derived.key;
    record.stableCompletionKeyPresent = !derived.key.empty();
    record.stableCompletionKeySource = derived.source;
    record.stableCompletionKeyStatus = derived.status;
    record.keyDerivationReason = derived.reason;
    record.keyIncludesCallsign = derived.includesCallsign;
    record.keyIncludesRole = derived.includesRole;
    record.keyIncludesFrequency = derived.includesFrequency;
    record.keyIncludesEndpoint = derived.includesEndpoint;
    record.keyIncludesAirport = derived.includesAirport;
    record.keyMatchesDisplayDecision = !station.displayDecisionId.empty();
    record.keyMatchesCapDecision = !station.overlayCapDecisionId.empty();
    record.keyAuditWarning =
        record.stableCompletionKeyStatus != "stable";
    record.keyAuditWarningReason =
        record.keyAuditWarning ? record.stableCompletionKeyStatus : "";
    return record;
}

void MarkDuplicateStableKeys(
    std::vector<BrainDisplayStableKeyAuditRecord>* records) {
    if (records == nullptr) {
        return;
    }
    std::unordered_map<std::string, int> keyCounts;
    for (const auto& record : *records) {
        if (!record.stableCompletionKey.empty()) {
            ++keyCounts[record.stableCompletionKey];
        }
    }
    for (auto& record : *records) {
        if (record.stableCompletionKey.empty()) {
            continue;
        }
        const auto found = keyCounts.find(record.stableCompletionKey);
        if (found == keyCounts.end() || found->second < 2) {
            continue;
        }
        record.duplicateKeyDetected = true;
        record.duplicateKeyGroup = record.stableCompletionKey;
        record.stableCompletionKeyStatus = "duplicated";
        record.keyAuditWarning = true;
        record.keyAuditWarningReason = "duplicate-stable-key";
    }
}

BrainDisplayStableKeyAuditSummary BuildStableKeyAuditSummary(
    const std::vector<BrainDisplayStableKeyAuditRecord>& records) {
    BrainDisplayStableKeyAuditSummary summary;
    summary.stableKeyAuditDecisionCount = static_cast<int>(records.size());
    summary.stableKeyAuditBrainOwned = true;
    summary.displayBehaviorChanged = false;
    for (const auto& record : records) {
        if (record.stableCompletionKeyPresent) {
            ++summary.stableKeyPresentCount;
        } else {
            ++summary.stableKeyMissingCount;
        }
        if (record.stableCompletionKeySource == "generated-fallback" ||
            record.stableCompletionKeyStatus == "fallback-derived") {
            ++summary.fallbackDerivedKeyCount;
        }
        if (record.stableCompletionKeyStatus == "synthetic") {
            ++summary.syntheticKeyCount;
        }
        if (record.stableCompletionKeySource == "legacy") {
            ++summary.legacyKeyCount;
        }
        if (record.duplicateKeyDetected) {
            ++summary.duplicatedKeyCount;
        }
        if (record.keyChangedAcrossReuse) {
            ++summary.changedAcrossReuseCount;
        }
        if (record.unsafeSameKeyAcrossChangedFacts) {
            ++summary.unsafeSameKeyCount;
        }
        if (!record.displayDecisionId.empty()) {
            ++summary.keyLedgerLinkedDisplayCount;
        }
        if (!record.overlayCapDecisionId.empty()) {
            ++summary.keyLedgerLinkedCapCount;
        }
        if (!record.phaseReuseDecisionId.empty()) {
            ++summary.keyLedgerLinkedPhaseReuseCount;
        }
    }
    return summary;
}

BrainDisplaySourceOwnedStableKeySummary BuildSourceOwnedStableKeySummary(
    const std::vector<BrainDisplayStableKeyAuditRecord>& records) {
    BrainDisplaySourceOwnedStableKeySummary summary;
    summary.behaviorChanged = false;
    for (const auto& record : records) {
        const auto sourceOwnedDecision =
            record.sourceOwnedStableCompletionKeyPresent ||
            !record.generatedFallbackStableCompletionKey.empty() ||
            !record.sourceOwnedStableCompletionKeyShape.empty();
        if (!sourceOwnedDecision) {
            continue;
        }
        ++summary.sourceOwnedStableKeyDecisionCount;
        if (record.sourceOwnedStableCompletionKeyPresent) {
            ++summary.sourceOwnedStableKeyPresentCount;
        }
        if (!record.generatedFallbackStableCompletionKey.empty()) {
            ++summary.generatedFallbackKeyPresentCount;
        }
        if (record.sourceOwnedMatchesGeneratedFallback) {
            ++summary.sourceOwnedMatchesFallbackCount;
        } else {
            ++summary.sourceOwnedMismatchCount;
        }
        if (record.sourceOwnedKeyPlanContextAvailable) {
            ++summary.planContextAvailableCount;
        } else {
            ++summary.planContextMissingCount;
        }
        if (record.sourceOwnedKeyMigrationReady) {
            ++summary.migrationReadyCount;
        }
        if (record.sourceOwnedKeyBehaviorConsumerEnabled) {
            ++summary.behaviorConsumerEnabledCount;
        }
    }
    return summary;
}

BrainDisplayStableKeyConsumerDryRunRecord
BuildStableKeyConsumerDryRunRecord(
    const BrainDisplayStableKeyAuditRecord& keyRecord,
    const BrainDisplayDecisionRecord& decision,
    const BrainDisplayIntentInput& input,
    const FinalDisplaySnapshot& finalDisplay,
    int index) {
    BrainDisplayStableKeyConsumerDryRunRecord record;
    record.dryRunStableKeyConsumerDecisionId =
        "stable-key-consumer-dry-run|display|" + std::to_string(index);
    record.subjectKey = DecisionStationKey(decision);
    record.callsign = decision.callsign;
    record.role = decision.role;
    record.frequency = decision.frequency;
    record.endpoint = BoardSourceToken(decision.sourceBoard);
    record.airportIcao =
        AirportIcaoForSource(input, decision.sourceBoard, finalDisplay);
    record.currentBehaviorKey =
        decision.completionStableKey.empty()
            ? keyRecord.stableCompletionKey
            : decision.completionStableKey;
    record.sourceOwnedStableCompletionKey =
        keyRecord.sourceOwnedStableCompletionKey;
    record.generatedFallbackStableCompletionKey =
        keyRecord.generatedFallbackStableCompletionKey;
    record.currentBehaviorKeySource =
        keyRecord.stableCompletionKeySource.empty()
            ? "unknown"
            : keyRecord.stableCompletionKeySource;
    record.sourceOwnedKeyPresent =
        keyRecord.sourceOwnedStableCompletionKeyPresent;
    record.sourceOwnedKeyMigrationReady =
        keyRecord.sourceOwnedKeyMigrationReady;
    record.behaviorConsumerEnabled =
        keyRecord.sourceOwnedKeyBehaviorConsumerEnabled;
    record.dryRunDedupeGroupCurrent = DecisionStationKey(decision);
    record.dryRunDedupeGroupSourceOwned =
        keyRecord.sourceOwnedStableCompletionKey.empty()
            ? "<none>"
            : keyRecord.sourceOwnedStableCompletionKey;
    record.dryRunPhaseReuseMatchCurrent = true;
    record.dryRunPhaseReuseMatchSourceOwned = record.sourceOwnedKeyPresent;

    if (!record.sourceOwnedKeyPresent) {
        record.dryRunBlockedReason = "source-owned-key-missing";
    } else if (!record.sourceOwnedKeyMigrationReady) {
        record.dryRunBlockedReason =
            keyRecord.sourceOwnedKeyPlanContextAvailable
                ? "source-owned-key-not-migration-ready"
                : "missing-plan-context";
    }
    return record;
}

void ApplyStableKeyConsumerDryRunGroupParity(
    std::vector<BrainDisplayStableKeyConsumerDryRunRecord>* records) {
    if (records == nullptr) {
        return;
    }

    std::unordered_map<std::string, std::vector<int>> currentGroups;
    std::unordered_map<std::string, std::vector<int>> sourceOwnedGroups;
    for (std::size_t index = 0; index < records->size(); ++index) {
        auto& record = (*records)[index];
        currentGroups[record.dryRunDedupeGroupCurrent].push_back(
            static_cast<int>(index));
        sourceOwnedGroups[record.dryRunDedupeGroupSourceOwned].push_back(
            static_cast<int>(index));
    }

    for (std::size_t index = 0; index < records->size(); ++index) {
        auto& record = (*records)[index];
        const auto currentIt =
            currentGroups.find(record.dryRunDedupeGroupCurrent);
        const auto sourceIt =
            sourceOwnedGroups.find(record.dryRunDedupeGroupSourceOwned);
        const bool groupWouldChange =
            currentIt == currentGroups.end() ||
            sourceIt == sourceOwnedGroups.end() ||
            currentIt->second != sourceIt->second;
        record.dryRunDedupeGroupWouldChange = groupWouldChange;
        record.dryRunDuplicateSuppressionWouldChange = groupWouldChange;
        record.dryRunCompletionIdentityWouldChange = groupWouldChange;
        record.dryRunRowOrderingWouldChange = groupWouldChange;
        record.dryRunOverlayCapWouldChange = groupWouldChange;
        record.dryRunMoreAtcWouldChange = groupWouldChange;
        record.dryRunDriftDetected = groupWouldChange;
        if (groupWouldChange) {
            record.dryRunDriftReason = "source-owned-dedupe-group-drift";
            record.dryRunBlockedReason = "dry-run-drift-detected";
        }
        record.dryRunSafeForOptIn =
            record.sourceOwnedKeyPresent &&
            record.sourceOwnedKeyMigrationReady &&
            !record.behaviorConsumerEnabled &&
            !record.dryRunDriftDetected;
    }
}

BrainDisplayStableKeyConsumerDryRunSummary
BuildStableKeyConsumerDryRunSummary(
    const std::vector<BrainDisplayStableKeyConsumerDryRunRecord>& records) {
    BrainDisplayStableKeyConsumerDryRunSummary summary;
    summary.dryRunStableKeyConsumerDecisionCount =
        static_cast<int>(records.size());
    summary.displayBehaviorChanged = false;
    for (const auto& record : records) {
        if (record.sourceOwnedKeyPresent) {
            ++summary.sourceOwnedKeyPresentCount;
        }
        if (record.sourceOwnedKeyMigrationReady) {
            ++summary.migrationReadyCount;
        }
        if (record.dryRunDedupeGroupWouldChange) {
            ++summary.dedupeGroupWouldChangeCount;
        }
        if (record.dryRunDuplicateSuppressionWouldChange) {
            ++summary.duplicateSuppressionWouldChangeCount;
        }
        if (record.dryRunCompletionIdentityWouldChange) {
            ++summary.completionIdentityWouldChangeCount;
        }
        if (record.dryRunPhaseReuseWouldChange) {
            ++summary.phaseReuseWouldChangeCount;
        }
        if (record.dryRunRowOrderingWouldChange) {
            ++summary.rowOrderingWouldChangeCount;
        }
        if (record.dryRunOverlayCapWouldChange) {
            ++summary.overlayCapWouldChangeCount;
        }
        if (record.dryRunMoreAtcWouldChange) {
            ++summary.moreAtcWouldChangeCount;
        }
        if (record.dryRunDriftDetected) {
            ++summary.driftDetectedCount;
        }
        if (record.dryRunSafeForOptIn) {
            ++summary.safeForOptInCount;
        }
        if (record.behaviorConsumerEnabled) {
            ++summary.behaviorConsumerEnabledCount;
        }
    }
    return summary;
}

void BuildStableKeyConsumerDryRunLedger(
    const BrainDisplayIntentInput& input,
    BrainDisplayIntentOutput* output) {
    if (output == nullptr) {
        return;
    }

    std::unordered_map<std::string, const BrainDisplayDecisionRecord*>
        decisionsById;
    for (const auto& decision : output->displayDecisions) {
        if (!decision.decisionId.empty()) {
            decisionsById.emplace(decision.decisionId, &decision);
        }
    }

    output->stableKeyConsumerDryRunDecisions.clear();
    int index = 0;
    for (const auto& keyRecord : output->stableKeyAuditDecisions) {
        if (!keyRecord.sourceOwnedStableCompletionKeyPresent &&
            keyRecord.generatedFallbackStableCompletionKey.empty()) {
            continue;
        }
        const auto decisionIt = decisionsById.find(keyRecord.displayDecisionId);
        if (decisionIt == decisionsById.end()) {
            continue;
        }
        output->stableKeyConsumerDryRunDecisions.push_back(
            BuildStableKeyConsumerDryRunRecord(
                keyRecord,
                *decisionIt->second,
                input,
                output->finalDisplay,
                index++));
    }

    ApplyStableKeyConsumerDryRunGroupParity(
        &output->stableKeyConsumerDryRunDecisions);
    output->stableKeyConsumerDryRunSummary =
        BuildStableKeyConsumerDryRunSummary(
            output->stableKeyConsumerDryRunDecisions);
}

BrainDisplaySourceOwnedFallbackStableKeyShadowRecord
BuildSourceOwnedFallbackShadowRecord(
    const BrainDisplayIntentInput& input,
    const BrainDisplayIntentOutput& output,
    const BrainDisplayStableKeyConsumerDryRunRecord& dryRun,
    int index) {
    BrainDisplaySourceOwnedFallbackStableKeyShadowRecord record;
    record.shadowDecisionId =
        "source-owned-fallback-shadow|display|" + std::to_string(index);
    record.dryRunStableKeyConsumerDecisionId =
        dryRun.dryRunStableKeyConsumerDecisionId;
    record.subjectKey = dryRun.subjectKey;
    record.callsign = dryRun.callsign;
    record.role = dryRun.role;
    record.frequency = dryRun.frequency;
    record.endpoint = dryRun.endpoint;
    record.airportIcao = dryRun.airportIcao;
    record.sourceOwnedFallbackShadowGateEnabled =
        input.sourceOwnedFallbackStableKeyShadowEnabled;
    record.sourceOwnedFallbackShadowGateSource =
        NormalizeSourceOwnedFallbackShadowGateSource(
            input.sourceOwnedFallbackStableKeyShadowEnabled,
            input.sourceOwnedFallbackStableKeyShadowGateSource);
    record.shadowBehaviorConsumerEnabled = dryRun.behaviorConsumerEnabled;
    record.shadowFinalBoardHashCurrent = std::to_string(output.stableHash);
    record.shadowFinalBoardHashSourceOwned =
        record.sourceOwnedFallbackShadowGateEnabled
            ? std::to_string(output.stableHash)
            : std::string{"<skipped>"};

    if (!record.sourceOwnedFallbackShadowGateEnabled) {
        record.shadowRecomputeSkippedReason = "shadow-gate-disabled";
        record.shadowFinalBoardHashMatches = true;
        record.shadowRowOrderingMatches = true;
        record.shadowDedupeGroupsMatch = true;
        record.shadowDuplicateSuppressionMatches = true;
        record.shadowCompletionIdentityMatches = true;
        record.shadowPhaseReuseMatches = true;
        record.shadowOverlayCapMatches = true;
        record.shadowMoreAtcMatches = true;
        return record;
    }

    record.shadowRecomputeAttempted = true;
    record.shadowDedupeGroupsMatch = !dryRun.dryRunDedupeGroupWouldChange;
    record.shadowDuplicateSuppressionMatches =
        !dryRun.dryRunDuplicateSuppressionWouldChange;
    record.shadowCompletionIdentityMatches =
        !dryRun.dryRunCompletionIdentityWouldChange;
    record.shadowPhaseReuseMatches = !dryRun.dryRunPhaseReuseWouldChange;
    record.shadowRowOrderingMatches = !dryRun.dryRunRowOrderingWouldChange;
    record.shadowOverlayCapMatches = !dryRun.dryRunOverlayCapWouldChange;
    record.shadowMoreAtcMatches = !dryRun.dryRunMoreAtcWouldChange;
    record.shadowFinalBoardHashMatches =
        record.shadowDedupeGroupsMatch &&
        record.shadowDuplicateSuppressionMatches &&
        record.shadowCompletionIdentityMatches &&
        record.shadowRowOrderingMatches &&
        record.shadowOverlayCapMatches &&
        record.shadowMoreAtcMatches;
    record.shadowMissingPlanContextBlocked =
        dryRun.dryRunBlockedReason == "missing-plan-context";
    record.shadowDriftDetected =
        dryRun.dryRunDriftDetected ||
        !record.shadowFinalBoardHashMatches ||
        !record.shadowPhaseReuseMatches;
    if (record.shadowDriftDetected) {
        record.shadowDriftReason =
            dryRun.dryRunDriftReason.empty()
                ? "source-owned-shadow-output-drift"
                : dryRun.dryRunDriftReason;
    } else if (record.shadowMissingPlanContextBlocked) {
        record.shadowDriftReason = "missing-plan-context";
    }
    record.shadowSafeForFutureLiveOptIn =
        dryRun.dryRunSafeForOptIn &&
        !record.shadowBehaviorConsumerEnabled &&
        !record.shadowMissingPlanContextBlocked &&
        !record.shadowDriftDetected &&
        record.shadowFinalBoardHashMatches &&
        record.shadowPhaseReuseMatches;
    return record;
}

BrainDisplaySourceOwnedFallbackStableKeyShadowSummary
BuildSourceOwnedFallbackShadowSummary(
    const std::vector<BrainDisplaySourceOwnedFallbackStableKeyShadowRecord>&
        records) {
    BrainDisplaySourceOwnedFallbackStableKeyShadowSummary summary;
    summary.shadowDecisionCount = static_cast<int>(records.size());
    summary.behaviorChanged = false;
    for (const auto& record : records) {
        if (record.sourceOwnedFallbackShadowGateEnabled) {
            ++summary.shadowGateEnabledCount;
        }
        if (record.shadowRecomputeAttempted) {
            ++summary.shadowRecomputeAttemptedCount;
        } else {
            ++summary.shadowRecomputeSkippedCount;
        }
        if (!record.shadowFinalBoardHashMatches) {
            ++summary.shadowHashMismatchCount;
        }
        if (!record.shadowRowOrderingMatches) {
            ++summary.shadowRowOrderingMismatchCount;
        }
        if (!record.shadowDedupeGroupsMatch) {
            ++summary.shadowDedupeMismatchCount;
        }
        if (!record.shadowDuplicateSuppressionMatches) {
            ++summary.shadowDuplicateSuppressionMismatchCount;
        }
        if (!record.shadowCompletionIdentityMatches) {
            ++summary.shadowCompletionIdentityMismatchCount;
        }
        if (!record.shadowPhaseReuseMatches) {
            ++summary.shadowPhaseReuseMismatchCount;
        }
        if (!record.shadowOverlayCapMatches) {
            ++summary.shadowOverlayCapMismatchCount;
        }
        if (!record.shadowMoreAtcMatches) {
            ++summary.shadowMoreAtcMismatchCount;
        }
        if (record.shadowMissingPlanContextBlocked) {
            ++summary.shadowMissingPlanBlockedCount;
        }
        if (record.shadowDriftDetected) {
            ++summary.shadowDriftDetectedCount;
        }
        if (record.shadowSafeForFutureLiveOptIn) {
            ++summary.shadowSafeForFutureLiveOptInCount;
        }
        if (record.shadowBehaviorConsumerEnabled) {
            ++summary.shadowBehaviorConsumerEnabledCount;
        }
    }
    return summary;
}

void BuildSourceOwnedFallbackShadowLedger(
    const BrainDisplayIntentInput& input,
    BrainDisplayIntentOutput* output) {
    if (output == nullptr) {
        return;
    }

    output->sourceOwnedFallbackStableKeyShadowDecisions.clear();
    output->sourceOwnedFallbackStableKeyShadowDecisions.reserve(
        output->stableKeyConsumerDryRunDecisions.size());

    int index = 0;
    for (const auto& dryRun : output->stableKeyConsumerDryRunDecisions) {
        output->sourceOwnedFallbackStableKeyShadowDecisions.push_back(
            BuildSourceOwnedFallbackShadowRecord(
                input,
                *output,
                dryRun,
                index++));
    }

    output->sourceOwnedFallbackStableKeyShadowSummary =
        BuildSourceOwnedFallbackShadowSummary(
            output->sourceOwnedFallbackStableKeyShadowDecisions);
}

bool SourceOwnedFallbackShadowParityClean(
    const BrainDisplaySourceOwnedFallbackStableKeyShadowRecord& shadow) {
    return shadow.shadowFinalBoardHashMatches &&
           shadow.shadowRowOrderingMatches &&
           shadow.shadowDedupeGroupsMatch &&
           shadow.shadowDuplicateSuppressionMatches &&
           shadow.shadowCompletionIdentityMatches &&
           shadow.shadowPhaseReuseMatches &&
           shadow.shadowOverlayCapMatches &&
           shadow.shadowMoreAtcMatches;
}

BrainDisplaySourceOwnedFallbackStableKeyLiveConsumptionReadinessRecord
BuildSourceOwnedFallbackLiveConsumptionReadinessRecord(
    const BrainDisplayIntentInput& input,
    const BrainDisplaySourceOwnedFallbackStableKeyShadowRecord& shadow,
    int index) {
    BrainDisplaySourceOwnedFallbackStableKeyLiveConsumptionReadinessRecord
        record;
    record.readinessDecisionId =
        "source-owned-fallback-live-consumption-readiness|display|" +
        std::to_string(index);
    record.shadowDecisionId = shadow.shadowDecisionId;
    record.dryRunStableKeyConsumerDecisionId =
        shadow.dryRunStableKeyConsumerDecisionId;
    record.subjectKey = shadow.subjectKey;
    record.callsign = shadow.callsign;
    record.role = shadow.role;
    record.frequency = shadow.frequency;
    record.endpoint = shadow.endpoint;
    record.airportIcao = shadow.airportIcao;
    record.proposalGateArmed =
        input.sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled;
    record.proposalGateSource =
        NormalizeSourceOwnedFallbackLiveConsumptionProposalGateSource(
            input.sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled,
            input.sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource);
    record.shadowGateEnabled = shadow.sourceOwnedFallbackShadowGateEnabled;
    record.shadowRecomputeAttempted = shadow.shadowRecomputeAttempted;
    record.shadowParityClean =
        SourceOwnedFallbackShadowParityClean(shadow);
    record.planContextAvailable = !shadow.shadowMissingPlanContextBlocked;
    record.shadowDriftDetected = shadow.shadowDriftDetected;
    record.shadowBehaviorConsumerEnabled =
        shadow.shadowBehaviorConsumerEnabled;
    record.liveConsumptionBehaviorEnabled = false;

    if (!record.proposalGateArmed) {
        record.blockedReason = "live-consumption-proposal-gate-not-armed";
    } else if (record.shadowBehaviorConsumerEnabled) {
        record.blockedReason = "behavior-consumer-already-enabled";
    } else if (!record.shadowGateEnabled) {
        record.blockedReason = "shadow-gate-disabled";
    } else if (!record.shadowRecomputeAttempted) {
        record.blockedReason = "shadow-parity-not-attempted";
    } else if (!record.planContextAvailable) {
        record.blockedReason = "missing-plan-context";
    } else if (record.shadowDriftDetected) {
        record.blockedReason =
            shadow.shadowDriftReason.empty()
                ? "shadow-drift-detected"
                : shadow.shadowDriftReason;
    } else if (!record.shadowParityClean) {
        record.blockedReason = "shadow-parity-mismatch";
    } else if (!shadow.shadowSafeForFutureLiveOptIn) {
        record.blockedReason = "shadow-not-safe-for-future-live-opt-in";
    } else {
        record.readyForFutureLiveConsumption = true;
    }

    return record;
}

BrainDisplaySourceOwnedFallbackStableKeyLiveConsumptionReadinessSummary
BuildSourceOwnedFallbackLiveConsumptionReadinessSummary(
    const std::vector<
        BrainDisplaySourceOwnedFallbackStableKeyLiveConsumptionReadinessRecord>&
        records) {
    BrainDisplaySourceOwnedFallbackStableKeyLiveConsumptionReadinessSummary
        summary;
    summary.readinessDecisionCount = static_cast<int>(records.size());
    summary.behaviorChanged = false;
    for (const auto& record : records) {
        if (record.proposalGateArmed) {
            ++summary.proposalGateArmedCount;
        }
        if (record.shadowParityClean) {
            ++summary.shadowParityCleanCount;
        }
        if (record.planContextAvailable) {
            ++summary.planContextAvailableCount;
        }
        if (!record.planContextAvailable) {
            ++summary.missingPlanBlockedCount;
        }
        if (record.shadowDriftDetected) {
            ++summary.driftBlockedCount;
        }
        if (record.blockedReason == "shadow-gate-disabled" ||
            record.blockedReason == "shadow-parity-not-attempted") {
            ++summary.shadowNotAttemptedBlockedCount;
        }
        if (!record.readyForFutureLiveConsumption) {
            ++summary.readinessBlockedCount;
        }
        if (record.readyForFutureLiveConsumption) {
            ++summary.readyForFutureLiveConsumptionCount;
        }
        if (record.liveConsumptionBehaviorEnabled) {
            ++summary.liveConsumptionBehaviorEnabledCount;
        }
        if (record.shadowBehaviorConsumerEnabled) {
            ++summary.shadowBehaviorConsumerEnabledCount;
        }
    }
    return summary;
}

void BuildSourceOwnedFallbackLiveConsumptionReadinessLedger(
    const BrainDisplayIntentInput& input,
    BrainDisplayIntentOutput* output) {
    if (output == nullptr) {
        return;
    }

    output->sourceOwnedFallbackStableKeyLiveConsumptionReadinessDecisions
        .clear();
    output->sourceOwnedFallbackStableKeyLiveConsumptionReadinessDecisions
        .reserve(output->sourceOwnedFallbackStableKeyShadowDecisions.size());

    int index = 0;
    for (const auto& shadow :
         output->sourceOwnedFallbackStableKeyShadowDecisions) {
        output->sourceOwnedFallbackStableKeyLiveConsumptionReadinessDecisions
            .push_back(
                BuildSourceOwnedFallbackLiveConsumptionReadinessRecord(
                    input,
                    shadow,
                    index++));
    }

    output->sourceOwnedFallbackStableKeyLiveConsumptionReadinessSummary =
        BuildSourceOwnedFallbackLiveConsumptionReadinessSummary(
            output->sourceOwnedFallbackStableKeyLiveConsumptionReadinessDecisions);
}

BrainDisplaySourceOwnedFallbackStableKeyLiveConsumptionRecord
BuildSourceOwnedFallbackLiveConsumptionRecord(
    const BrainDisplayIntentInput& input,
    const BrainDisplayStableKeyConsumerDryRunRecord& dryRun,
    const BrainDisplaySourceOwnedFallbackStableKeyShadowRecord& shadow,
    const BrainDisplaySourceOwnedFallbackStableKeyLiveConsumptionReadinessRecord&
        readiness,
    int index) {
    BrainDisplaySourceOwnedFallbackStableKeyLiveConsumptionRecord record;
    record.liveConsumptionDecisionId =
        "source-owned-fallback-live-consumption|display|" +
        std::to_string(index);
    record.subjectKey = readiness.subjectKey;
    record.callsign = readiness.callsign;
    record.role = readiness.role;
    record.frequency = readiness.frequency;
    record.endpoint = readiness.endpoint;
    record.airportIcao = readiness.airportIcao;
    record.generatedFallbackStableCompletionKey =
        dryRun.generatedFallbackStableCompletionKey;
    record.sourceOwnedStableCompletionKey =
        dryRun.sourceOwnedStableCompletionKey;
    record.sourceOwnedKeyPresent = dryRun.sourceOwnedKeyPresent;
    record.sourceOwnedKeyMigrationReady =
        dryRun.sourceOwnedKeyMigrationReady;
    record.planContextAvailable = readiness.planContextAvailable;
    record.shadowGateEnabled = readiness.shadowGateEnabled;
    record.shadowRecomputeAttempted = readiness.shadowRecomputeAttempted;
    record.shadowParityClean = readiness.shadowParityClean;
    record.shadowDriftDetected = readiness.shadowDriftDetected;
    record.shadowFinalBoardHashMatches =
        shadow.shadowFinalBoardHashMatches;
    record.shadowRowOrderingMatches = shadow.shadowRowOrderingMatches;
    record.shadowDedupeGroupsMatch = shadow.shadowDedupeGroupsMatch;
    record.shadowDuplicateSuppressionMatches =
        shadow.shadowDuplicateSuppressionMatches;
    record.shadowCompletionIdentityMatches =
        shadow.shadowCompletionIdentityMatches;
    record.shadowPhaseReuseMatches = shadow.shadowPhaseReuseMatches;
    record.shadowOverlayCapMatches = shadow.shadowOverlayCapMatches;
    record.shadowMoreAtcMatches = shadow.shadowMoreAtcMatches;
    record.proposalGateArmed = readiness.proposalGateArmed;
    record.liveConsumptionGateArmed =
        input.sourceOwnedFallbackStableKeyLiveConsumptionEnabled;
    record.liveConsumptionGateSource =
        NormalizeSourceOwnedFallbackLiveConsumptionGateSource(
            input.sourceOwnedFallbackStableKeyLiveConsumptionEnabled,
            input.sourceOwnedFallbackStableKeyLiveConsumptionGateSource);
    record.behaviorChanged = false;

    if (!record.liveConsumptionGateArmed) {
        record.liveConsumptionBlockedReason =
            "live-consumption-gate-not-armed";
    } else if (!record.proposalGateArmed) {
        record.liveConsumptionBlockedReason =
            "live-consumption-proposal-gate-not-armed";
    } else if (!record.shadowGateEnabled) {
        record.liveConsumptionBlockedReason = "shadow-gate-disabled";
    } else if (!record.shadowRecomputeAttempted) {
        record.liveConsumptionBlockedReason =
            "shadow-parity-not-attempted";
    } else if (!record.planContextAvailable) {
        record.liveConsumptionBlockedReason = "missing-plan-context";
    } else if (!record.sourceOwnedKeyPresent) {
        record.liveConsumptionBlockedReason = "source-owned-key-missing";
    } else if (!record.sourceOwnedKeyMigrationReady) {
        record.liveConsumptionBlockedReason =
            "source-owned-key-not-migration-ready";
    } else if (record.shadowDriftDetected) {
        record.liveConsumptionBlockedReason =
            shadow.shadowDriftReason.empty()
                ? "shadow-drift-detected"
                : shadow.shadowDriftReason;
    } else if (!record.shadowFinalBoardHashMatches) {
        record.liveConsumptionBlockedReason =
            "final-board-hash-mismatch";
    } else if (!record.shadowRowOrderingMatches) {
        record.liveConsumptionBlockedReason = "row-ordering-mismatch";
    } else if (!record.shadowDedupeGroupsMatch) {
        record.liveConsumptionBlockedReason = "dedupe-group-mismatch";
    } else if (!record.shadowDuplicateSuppressionMatches) {
        record.liveConsumptionBlockedReason =
            "duplicate-suppression-mismatch";
    } else if (!record.shadowCompletionIdentityMatches) {
        record.liveConsumptionBlockedReason =
            "completion-identity-mismatch";
    } else if (!record.shadowPhaseReuseMatches) {
        record.liveConsumptionBlockedReason = "phase-reuse-mismatch";
    } else if (!record.shadowOverlayCapMatches) {
        record.liveConsumptionBlockedReason = "overlay-cap-mismatch";
    } else if (!record.shadowMoreAtcMatches) {
        record.liveConsumptionBlockedReason = "more-atc-mismatch";
    } else if (!shadow.shadowSafeForFutureLiveOptIn) {
        record.liveConsumptionBlockedReason =
            "shadow-not-safe-for-future-live-opt-in";
    } else if (!readiness.readyForFutureLiveConsumption) {
        record.liveConsumptionBlockedReason =
            "readiness-not-ready-for-future-live-consumption";
    } else {
        record.liveConsumptionAllowed = true;
    }

    if (record.liveConsumptionAllowed) {
        record.consumedKeyType = "source-owned";
        record.defaultModeProtected = false;
    } else if (!record.generatedFallbackStableCompletionKey.empty()) {
        record.consumedKeyType = "generated-fallback";
        record.defaultModeProtected =
            !record.liveConsumptionGateArmed && !record.behaviorChanged;
    } else {
        record.consumedKeyType = "none";
        record.defaultModeProtected = false;
    }

    return record;
}

BrainDisplaySourceOwnedFallbackStableKeyLiveConsumptionSummary
BuildSourceOwnedFallbackLiveConsumptionSummary(
    const std::vector<
        BrainDisplaySourceOwnedFallbackStableKeyLiveConsumptionRecord>&
        records) {
    BrainDisplaySourceOwnedFallbackStableKeyLiveConsumptionSummary summary;
    summary.liveConsumptionDecisionCount = static_cast<int>(records.size());
    for (const auto& record : records) {
        if (record.liveConsumptionGateArmed) {
            ++summary.liveConsumptionGateArmedCount;
        }
        if (record.liveConsumptionAllowed) {
            ++summary.liveConsumptionAllowedCount;
        } else {
            ++summary.liveConsumptionBlockedCount;
        }
        if (record.consumedKeyType == "source-owned") {
            ++summary.sourceOwnedConsumedCount;
        } else if (record.consumedKeyType == "generated-fallback") {
            ++summary.generatedFallbackConsumedCount;
        }
        if (record.liveConsumptionBlockedReason == "missing-plan-context") {
            ++summary.missingPlanBlockedCount;
        }
        if (record.liveConsumptionBlockedReason == "shadow-gate-disabled") {
            ++summary.shadowGateOffBlockedCount;
        }
        if (record.liveConsumptionBlockedReason ==
            "shadow-parity-not-attempted") {
            ++summary.shadowParityNotAttemptedBlockedCount;
        }
        if (record.shadowDriftDetected) {
            ++summary.shadowDriftBlockedCount;
        }
        if (!record.shadowFinalBoardHashMatches) {
            ++summary.hashMismatchBlockedCount;
        }
        if (!record.shadowRowOrderingMatches) {
            ++summary.rowOrderingMismatchBlockedCount;
        }
        if (!record.shadowDedupeGroupsMatch) {
            ++summary.dedupeMismatchBlockedCount;
        }
        if (!record.shadowDuplicateSuppressionMatches) {
            ++summary.duplicateSuppressionMismatchBlockedCount;
        }
        if (!record.shadowCompletionIdentityMatches) {
            ++summary.completionIdentityMismatchBlockedCount;
        }
        if (!record.shadowPhaseReuseMatches) {
            ++summary.phaseReuseMismatchBlockedCount;
        }
        if (!record.shadowOverlayCapMatches) {
            ++summary.overlayCapMismatchBlockedCount;
        }
        if (!record.shadowMoreAtcMatches) {
            ++summary.moreAtcMismatchBlockedCount;
        }
        if (record.liveConsumptionBlockedReason ==
            "source-owned-key-missing") {
            ++summary.missingSourceOwnedKeyBlockedCount;
        }
        if (record.liveConsumptionBlockedReason ==
            "source-owned-key-not-migration-ready") {
            ++summary.migrationNotReadyBlockedCount;
        }
        if (record.defaultModeProtected) {
            ++summary.defaultModeProtectedCount;
        }
        if (record.behaviorChanged) {
            summary.behaviorChanged = true;
        }
    }
    return summary;
}

void BuildSourceOwnedFallbackLiveConsumptionLedger(
    const BrainDisplayIntentInput& input,
    BrainDisplayIntentOutput* output) {
    if (output == nullptr) {
        return;
    }

    std::unordered_map<
        std::string,
        const BrainDisplayStableKeyConsumerDryRunRecord*>
        dryRunsById;
    for (const auto& dryRun : output->stableKeyConsumerDryRunDecisions) {
        dryRunsById.emplace(dryRun.dryRunStableKeyConsumerDecisionId,
                            &dryRun);
    }

    std::unordered_map<
        std::string,
        const BrainDisplaySourceOwnedFallbackStableKeyShadowRecord*>
        shadowsById;
    for (const auto& shadow :
         output->sourceOwnedFallbackStableKeyShadowDecisions) {
        shadowsById.emplace(shadow.shadowDecisionId, &shadow);
    }

    output->sourceOwnedFallbackStableKeyLiveConsumptionDecisions.clear();
    output->sourceOwnedFallbackStableKeyLiveConsumptionDecisions.reserve(
        output
            ->sourceOwnedFallbackStableKeyLiveConsumptionReadinessDecisions
            .size());

    int index = 0;
    for (const auto& readiness :
         output
             ->sourceOwnedFallbackStableKeyLiveConsumptionReadinessDecisions) {
        const auto dryRunIt =
            dryRunsById.find(readiness.dryRunStableKeyConsumerDecisionId);
        const auto shadowIt = shadowsById.find(readiness.shadowDecisionId);
        if (dryRunIt == dryRunsById.end() ||
            shadowIt == shadowsById.end()) {
            continue;
        }
        output->sourceOwnedFallbackStableKeyLiveConsumptionDecisions
            .push_back(BuildSourceOwnedFallbackLiveConsumptionRecord(
                input,
                *dryRunIt->second,
                *shadowIt->second,
                readiness,
                index++));
    }

    output->sourceOwnedFallbackStableKeyLiveConsumptionSummary =
        BuildSourceOwnedFallbackLiveConsumptionSummary(
            output->sourceOwnedFallbackStableKeyLiveConsumptionDecisions);
}

BrainDisplayUpstreamStableKeySourceAuditRecord BuildUpstreamStableKeySourceRecord(
    int index,
    std::string sourceClass,
    std::string producerName,
    bool producesDisplayRows,
    bool producesCompletionRows,
    bool producesEvidenceRows,
    bool stableKeyProvided,
    std::string stableKeyFieldName,
    std::string stableKeySource,
    bool fallbackKeyUsedDownstream,
    bool missingKeyRisk,
    bool duplicateKeyRisk,
    bool reuseContinuityRisk,
    bool dedupeRisk,
    std::string recommendedStableKeyOwner,
    std::string recommendedStableKeyShape,
    std::string migrationPriority,
    std::string migrationBlockedReason,
    bool behaviorChangeRequiredForMigration) {
    BrainDisplayUpstreamStableKeySourceAuditRecord record;
    record.upstreamStableKeyAuditId =
        "upstream-stable-key|" + std::to_string(index);
    record.sourceClass = std::move(sourceClass);
    record.producerName = std::move(producerName);
    record.producesDisplayRows = producesDisplayRows;
    record.producesCompletionRows = producesCompletionRows;
    record.producesEvidenceRows = producesEvidenceRows;
    record.stableKeyProvided = stableKeyProvided;
    record.stableKeyFieldName = std::move(stableKeyFieldName);
    record.stableKeySource = std::move(stableKeySource);
    record.fallbackKeyUsedDownstream = fallbackKeyUsedDownstream;
    record.missingKeyRisk = missingKeyRisk;
    record.duplicateKeyRisk = duplicateKeyRisk;
    record.reuseContinuityRisk = reuseContinuityRisk;
    record.dedupeRisk = dedupeRisk;
    record.recommendedStableKeyOwner =
        std::move(recommendedStableKeyOwner);
    record.recommendedStableKeyShape =
        std::move(recommendedStableKeyShape);
    record.migrationPriority = std::move(migrationPriority);
    record.migrationBlockedReason = std::move(migrationBlockedReason);
    record.behaviorChangeRequiredForMigration =
        behaviorChangeRequiredForMigration;
    return record;
}

BrainDisplayUpstreamStableKeySourceAuditSummary
BuildUpstreamStableKeySourceAuditSummary(
    const std::vector<BrainDisplayUpstreamStableKeySourceAuditRecord>&
        records) {
    BrainDisplayUpstreamStableKeySourceAuditSummary summary;
    summary.upstreamStableKeyAuditCount =
        static_cast<int>(records.size());
    summary.stableKeySourceAuditBrainOwned = true;
    for (const auto& record : records) {
        if (record.stableKeySource == "source-owned") {
            ++summary.sourceOwnedKeyCount;
        } else if (record.stableKeySource == "evidence-id") {
            ++summary.evidenceIdKeyCount;
        } else if (record.stableKeySource == "decision-id") {
            ++summary.decisionIdKeyCount;
        } else if (record.stableKeySource == "generated-fallback") {
            ++summary.fallbackKeySourceCount;
        } else if (record.stableKeySource == "synthetic-fixture") {
            ++summary.syntheticKeySourceCount;
        } else if (record.stableKeySource == "legacy") {
            ++summary.legacyKeySourceCount;
        } else if (record.stableKeySource == "missing") {
            ++summary.missingKeySourceCount;
        } else {
            ++summary.unknownKeySourceCount;
        }

        if (record.migrationPriority == "high") {
            ++summary.highPriorityMigrationCount;
        } else if (record.migrationPriority == "medium") {
            ++summary.mediumPriorityMigrationCount;
        } else if (record.migrationPriority == "low") {
            ++summary.lowPriorityMigrationCount;
        }
        if (record.dedupeRisk) {
            ++summary.dedupeRiskCount;
        }
        if (record.reuseContinuityRisk) {
            ++summary.reuseContinuityRiskCount;
        }
        if (record.behaviorChangeRequiredForMigration) {
            ++summary.migrationRequiresBehaviorChangeCount;
        }
    }
    return summary;
}

void BuildUpstreamStableKeySourceAuditLedger(
    BrainDisplayIntentOutput* output) {
    if (output == nullptr) {
        return;
    }

    std::vector<BrainDisplayUpstreamStableKeySourceAuditRecord> records;
    records.reserve(8);
    auto addRecord =
        [&](std::string sourceClass,
            std::string producerName,
            bool producesDisplayRows,
            bool producesCompletionRows,
            bool producesEvidenceRows,
            bool stableKeyProvided,
            std::string stableKeyFieldName,
            std::string stableKeySource,
            bool fallbackKeyUsedDownstream,
            bool missingKeyRisk,
            bool duplicateKeyRisk,
            bool reuseContinuityRisk,
            bool dedupeRisk,
            std::string recommendedStableKeyOwner,
            std::string recommendedStableKeyShape,
            std::string migrationPriority,
            std::string migrationBlockedReason,
            bool behaviorChangeRequiredForMigration) {
            records.push_back(BuildUpstreamStableKeySourceRecord(
                static_cast<int>(records.size()),
                std::move(sourceClass),
                std::move(producerName),
                producesDisplayRows,
                producesCompletionRows,
                producesEvidenceRows,
                stableKeyProvided,
                std::move(stableKeyFieldName),
                std::move(stableKeySource),
                fallbackKeyUsedDownstream,
                missingKeyRisk,
                duplicateKeyRisk,
                reuseContinuityRisk,
                dedupeRisk,
                std::move(recommendedStableKeyOwner),
                std::move(recommendedStableKeyShape),
                std::move(migrationPriority),
                std::move(migrationBlockedReason),
                behaviorChangeRequiredForMigration));
        };

    addRecord(
        "controller-relevance-completion",
        "BrainControllerRelevanceWorker",
        true,
        true,
        true,
        true,
        "sourceEvidenceId",
        "evidence-id",
        false,
        false,
        false,
        false,
        false,
        "BrainControllerRelevanceWorker",
        "radio-reachable:<candidate-stable-key>",
        "low",
        "explicit-stableCompletionKey-field-not-yet-emitted",
        false);
    addRecord(
        "transceiver-resolver",
        "TransceiverResolutionSnapshot",
        false,
        false,
        true,
        false,
        "",
        "missing",
        false,
        true,
        true,
        true,
        true,
        "TransceiverResolver",
        "transceiver:<callsign>|<facility>|<frequency>|<source-generation>",
        "medium",
        "resolver-evidence-does-not-yet-expose-stable-row-identity",
        false);
    addRecord(
        "route-sector-authority-relevance",
        "RouteSectorResolver/AuthorityRelevance",
        false,
        true,
        true,
        false,
        "",
        "missing",
        false,
        true,
        false,
        true,
        false,
        "RouteSectorResolver-authority-evidence",
        "route-sector:<plan-key>|<sector-id>|<callsign>|<frequency>",
        "medium",
        "route-sector-evidence-currently-exposes-context-not-row-key",
        false);
    addRecord(
        "ctaf-unicom-advisory",
        "BrainOwnedRuntime CTAF/UNICOM advisory projection",
        true,
        true,
        true,
        true,
        "sourceEvidenceId",
        "evidence-id",
        false,
        false,
        false,
        false,
        false,
        "BrainOwnedRuntime CTAF/UNICOM advisory decision",
        "ctaf-unicom:<endpoint>:<airport-icao>",
        "none",
        "",
        false);
    addRecord(
        "duplicated-atis-frequency-proof",
        "Transceiver/route authority proof diagnostics",
        false,
        false,
        true,
        false,
        "",
        "missing",
        false,
        true,
        true,
        false,
        true,
        "proof-evidence-producer",
        "proof:<domain>|<callsign>|<frequency>|<reason>",
        "low",
        "proof-rows-are-diagnostic-only-today",
        false);
    addRecord(
        "fallback-polygon-geometry-inference",
        "BrainDisplayIntent fallback relation inference",
        true,
        true,
        false,
        true,
        "sourceOwnedStableCompletionKey",
        "source-owned",
        true,
        false,
        true,
        true,
        true,
        "upstream-controller-or-route-evidence",
        "source-owned:<producer>|<callsign>|<role>|<frequency>|<plan-context>",
        "high",
        "fallback-relation-rows-lack-source-owned-stableCompletionKey",
        false);
    addRecord(
        "synthetic-fixture-row",
        "Regression harness display fixture",
        true,
        true,
        false,
        true,
        "sourceEvidenceLinkStatus",
        "synthetic-fixture",
        true,
        false,
        false,
        false,
        false,
        "harness-fixture",
        "fixture:<scenario>|<callsign>|<frequency>",
        "none",
        "",
        false);
    addRecord(
        "legacy-diagnostic-row",
        "Legacy compatibility diagnostic projection",
        true,
        true,
        false,
        true,
        "sourceEvidenceLinkStatus",
        "legacy",
        true,
        false,
        false,
        true,
        false,
        "legacy-compatibility-producer",
        "legacy:<compatibility-domain>|<callsign>|<frequency>",
        "none",
        "diagnostic-only-compatibility-window",
        false);

    output->upstreamStableKeySourceAuditSummary =
        BuildUpstreamStableKeySourceAuditSummary(records);
    output->upstreamStableKeySourceAuditDecisions = std::move(records);
}

void BuildStableKeyAuditLedger(
    const BrainDisplayIntentInput& input,
    BrainDisplayIntentOutput* output) {
    if (output == nullptr) {
        return;
    }

    std::unordered_map<std::string, const BrainDisplayOverlayCapDecisionRecord*>
        capByDisplayDecisionId;
    for (const auto& cap : output->overlayCapDecisions) {
        if (!cap.displayDecisionId.empty()) {
            capByDisplayDecisionId.emplace(cap.displayDecisionId, &cap);
        }
    }

    output->stableKeyAuditDecisions.clear();
    output->stableKeyAuditDecisions.reserve(
        output->displayDecisions.size() + output->finalDisplay.stations.size());

    int index = 0;
    for (const auto& decision : output->displayDecisions) {
        auto record = BuildStableKeyAuditFromDecision(
            decision,
            input,
            output->finalDisplay,
            index++);
        const auto capIt = capByDisplayDecisionId.find(decision.decisionId);
        if (capIt != capByDisplayDecisionId.end()) {
            record.overlayCapDecisionId = capIt->second->overlayCapDecisionId;
            record.keyMatchesCapDecision =
                capIt->second->stableCompletionKey == record.stableCompletionKey;
        }
        if (record.sourceOwnedStableCompletionKeyPresent ||
            !record.generatedFallbackStableCompletionKey.empty()) {
            for (auto& station : output->finalDisplay.stations) {
                if (station.displayDecisionId == decision.decisionId) {
                    SourceOwnedFallbackGeometryDiagnostic diagnostic;
                    diagnostic.sourceOwnedStableCompletionKey =
                        record.sourceOwnedStableCompletionKey;
                    diagnostic.sourceOwnedStableCompletionKeyPresent =
                        record.sourceOwnedStableCompletionKeyPresent;
                    diagnostic.generatedFallbackStableCompletionKey =
                        record.generatedFallbackStableCompletionKey;
                    diagnostic.sourceOwnedKeyMigrationReady =
                        record.sourceOwnedKeyMigrationReady;
                    diagnostic.sourceOwnedKeyPlanContextAvailable =
                        record.sourceOwnedKeyPlanContextAvailable;
                    diagnostic.sourceOwnedKeyBehaviorConsumerEnabled =
                        record.sourceOwnedKeyBehaviorConsumerEnabled;
                    ApplySourceOwnedFallbackGeometryDiagnostic(
                        diagnostic,
                        &station);
                    break;
                }
            }
        }
        output->stableKeyAuditDecisions.push_back(std::move(record));
    }

    for (std::size_t rowIndex = 0;
         rowIndex < output->finalDisplay.stations.size();
         ++rowIndex) {
        const auto& station = output->finalDisplay.stations[rowIndex];
        if (!station.displayDecisionId.empty()) {
            continue;
        }
        output->stableKeyAuditDecisions.push_back(
            BuildStableKeyAuditFromFinalRow(
                station,
                output->finalDisplay,
                static_cast<int>(rowIndex)));
    }

    MarkDuplicateStableKeys(&output->stableKeyAuditDecisions);
    output->stableKeyAuditSummary =
        BuildStableKeyAuditSummary(output->stableKeyAuditDecisions);
    output->sourceOwnedStableKeySummary =
        BuildSourceOwnedStableKeySummary(output->stableKeyAuditDecisions);
}

void FinalizeDisplayDecisionLedger(BrainDisplayIntentOutput* output) {
    if (output == nullptr) {
        return;
    }

    BrainDisplayDecisionSummary summary;
    summary.displayedFinalCount =
        static_cast<int>(output->finalDisplay.stations.size());
    for (auto& decision : output->displayDecisions) {
        decision.displayedInFinalSnapshot =
            !decision.duplicateSuppressed &&
            DisplayRowsContainStation(output->finalDisplay, decision);
        if (decision.acceptedByRelevance) {
            ++summary.acceptedCompletionCount;
        }
        if (decision.duplicateSuppressed) {
            ++summary.duplicateSuppressedCount;
        }
        if (decision.stageSuppressed) {
            ++summary.stageSuppressedCount;
        }
        if (decision.acceptedByRelevance &&
            !decision.displayedInFinalSnapshot) {
            ++summary.hiddenAfterAcceptCount;
            if (decision.finalRelation == DisplayRelation::Filtered) {
                ++summary.filteredAfterAcceptCount;
            }
            if (decision.decision == "display-accepted" &&
                decision.sourceBoard == BoardSource::Enroute &&
                decision.workflowStage == WorkflowStage::Departure &&
                decision.finalRelation == DisplayRelation::ArrivalPrep) {
                decision.stageSuppressed = true;
                ++summary.stageSuppressedCount;
                MarkDecisionOutcome(
                    &decision,
                    "display-deferred-by-stage",
                    "stage=Departure/source=enroute/relation=ARRIVAL_PREP-excluded",
                    "medium",
                    0.55,
                    0.70);
            }
        }
    }
    summary.displayDecisionCount =
        static_cast<int>(output->displayDecisions.size());
    summary.missingDecisionCount = std::max(
        0,
        summary.acceptedCompletionCount - summary.displayDecisionCount);
    output->displayDecisionSummary = summary;
}

FinalDisplayStationSnapshot BuildFactDisplayStation(
    const BoardStationSnapshot& sourceStation,
    const BrainDisplayIntentInput& input,
    const std::vector<BrainDisplayRelationFact>& relationFacts) {
    auto station = ToFinalDisplayStation(sourceStation, input.radios);
    station.next = false;
    station.standby = false;

    const auto* relationFact = FindRelationFact(sourceStation, relationFacts);
    if (relationFact == nullptr ||
        !IsFinalDisplayRelation(relationFact->displayRelation)) {
        return station;
    }

    station.displayRelation = relationFact->displayRelation;
    station.next =
        relationFact->displayRelation == DisplayRelation::NextPolygon ||
        relationFact->displayRelation == DisplayRelation::ArrivalPrep;
    station.hasRouteEntryDistance = relationFact->hasRouteEntryDistance;
    station.routeEntryDistanceNm =
        relationFact->hasRouteEntryDistance
            ? std::max(0.0, relationFact->routeEntryDistanceNm)
            : 0.0;
    return station;
}

DisplayRelation InferCenterRelation(
    const BoardStationSnapshot& station,
    const BrainDisplayIntentInput& input) {
    const auto hasRouteContext =
        !input.currentPolygonKey.empty() ||
        !input.nextPolygonKey.empty() ||
        !input.arrivalPolygonKey.empty();
    if (!hasRouteContext && !station.hasRouteEntryDistance) {
        return DisplayRelation::Hidden;
    }

    if ((input.radios.valid &&
         IsCom1TunedToFrequency(input.radios, station.frequency)) ||
        KeysEqual(station.polygonKey, input.currentPolygonKey)) {
        return DisplayRelation::CurrentPolygon;
    }
    if (KeysEqual(station.polygonKey, input.nextPolygonKey)) {
        return DisplayRelation::NextPolygon;
    }
    if (KeysEqual(station.polygonKey, input.arrivalPolygonKey)) {
        return DisplayRelation::ArrivalPrep;
    }
    if (!station.polygonKey.empty()) {
        return DisplayRelation::Hidden;
    }
    if (station.hasRouteEntryDistance) {
        return station.routeEntryDistanceNm <=
                       input.routeProgressDistanceNm +
                           kCurrentPolygonDistanceToleranceNm
                   ? DisplayRelation::CurrentPolygon
                   : DisplayRelation::NextPolygon;
    }
    return DisplayRelation::Hidden;
}

bool ShouldDisplayDepartureEnrouteCenter(
    const FinalDisplayStationSnapshot& station) {
    return station.displayRelation == DisplayRelation::CurrentPolygon ||
           station.displayRelation == DisplayRelation::NextPolygon;
}

int CenterRelationRank(DisplayRelation relation) {
    switch (relation) {
        case DisplayRelation::CurrentPolygon:
            return 0;
        case DisplayRelation::NextPolygon:
            return 1;
        case DisplayRelation::ArrivalPrep:
            return 1;
        default:
            return 2;
    }
}

int DepartureFrequencyRank(const FinalDisplayStationSnapshot& station) {
    switch (station.role) {
        case StationRole::Delivery:
            return 0;
        case StationRole::Ground:
            return 1;
        case StationRole::Tower:
            return 2;
        case StationRole::Approach:
        case StationRole::Departure:
            return 3;
        case StationRole::Center:
            return 4 + CenterRelationRank(station.displayRelation);
        case StationRole::Ctaf:
        case StationRole::Unicom:
            return 7;
        case StationRole::Atis:
            return 8;
        case StationRole::Other:
        default:
            return 9;
    }
}

int ArrivalFrequencyRank(const FinalDisplayStationSnapshot& station) {
    switch (station.role) {
        case StationRole::Center:
            // Arrival handoff boards keep every displayed center ahead of terminal rows;
            // relation rank preserves current center before next/arrival center.
            return CenterRelationRank(station.displayRelation);
        case StationRole::Approach:
        case StationRole::Departure:
            return 3;
        case StationRole::Tower:
            return 4;
        case StationRole::Ground:
            return 5;
        case StationRole::Delivery:
            return 6;
        case StationRole::Ctaf:
        case StationRole::Unicom:
            return 7;
        case StationRole::Atis:
            return 8;
        case StationRole::Other:
        default:
            return 9;
    }
}

int FrequencyIntentRank(
    WorkflowStage stage,
    const FinalDisplayStationSnapshot& station) {
    if (stage == WorkflowStage::Arrival) {
        return ArrivalFrequencyRank(station);
    }
    return DepartureFrequencyRank(station);
}

void SortFrequencyIntent(WorkflowStage stage, FinalDisplaySnapshot* board) {
    if (board == nullptr) {
        return;
    }

    std::stable_sort(
        board->stations.begin(),
        board->stations.end(),
        [stage](const auto& left, const auto& right) {
            const auto leftRank = FrequencyIntentRank(stage, left);
            const auto rightRank = FrequencyIntentRank(stage, right);
            if (leftRank != rightRank) {
                return leftRank < rightRank;
            }
            if (left.role == StationRole::Center &&
                right.role == StationRole::Center &&
                left.displayRelation != right.displayRelation) {
                return CenterRelationRank(left.displayRelation) <
                       CenterRelationRank(right.displayRelation);
            }
            if (left.hasRouteEntryDistance != right.hasRouteEntryDistance) {
                return left.hasRouteEntryDistance && !right.hasRouteEntryDistance;
            }
            if (left.hasRouteEntryDistance && right.hasRouteEntryDistance &&
                left.routeEntryDistanceNm != right.routeEntryDistanceNm) {
                return left.routeEntryDistanceNm < right.routeEntryDistanceNm;
            }
            if (left.frequency != right.frequency) {
                return left.frequency < right.frequency;
            }
            return left.callsign < right.callsign;
        });
}

FinalDisplayStationSnapshot BuildFinalDisplayStation(
    const BoardStationSnapshot& sourceStation,
    DisplayRelation relation,
    const BrainDisplayIntentInput& input) {
    auto station = ToFinalDisplayStation(sourceStation, input.radios);
    station.displayRelation = relation;
    station.next = false;
    station.standby = false;

    if (!IsCenter(station)) {
        return station;
    }

    if (relation == DisplayRelation::CurrentPolygon ||
        relation == DisplayRelation::ArrivalPrep) {
        station.sectorActive = true;
        station.next = false;
        station.annotation.clear();
        station.hasRouteEntryDistance = false;
        station.routeEntryDistanceNm = 0.0;
        return station;
    }

    if (relation == DisplayRelation::NextPolygon) {
        const auto routeEntryDistanceNm =
            station.hasRouteEntryDistance
                ? std::max(0.0, station.routeEntryDistanceNm)
                : 0.0;
        const auto remainingDistanceNm =
            station.hasRouteEntryDistance
                ? std::max(
                      0.0,
                      routeEntryDistanceNm - input.routeProgressDistanceNm)
                : 0.0;
        station.sectorActive = false;
        station.next = true;
        station.annotation = FormatDistanceAnnotation(remainingDistanceNm);
        station.hasRouteEntryDistance = true;
        station.routeEntryDistanceNm = routeEntryDistanceNm;
        return station;
    }

    return station;
}

void AddBoardStations(
    const ModuleBoardSnapshot& source,
    const BrainDisplayIntentInput& input,
    FinalDisplaySnapshot* target,
    std::unordered_set<std::string>* keys,
    std::unordered_map<std::string, std::string>* keptDecisionIdsByKey,
    std::vector<BrainDisplayDecisionRecord>* decisions,
    BoardSource sourceBoard,
    int* decisionSequence) {
    if (target == nullptr || keys == nullptr) {
        return;
    }
    for (const auto& station : source.stations) {
        auto decision = BuildBaseDecision(
            station,
            sourceBoard,
            input,
            decisionSequence == nullptr ? 0 : (*decisionSequence)++);
        if (!IsDisplayableStation(station)) {
            decision.hardBlock = true;
            MarkDecisionOutcome(
                &decision,
                "display-rejected-non-displayable",
                station.frequency.empty() ? "empty-frequency" : "offline",
                "high",
                0.0,
                0.90);
            if (decisions != nullptr) {
                decisions->push_back(std::move(decision));
            }
            continue;
        }
        auto displayStation =
            BuildFactDisplayStation(station, input, input.relationFacts);
        displayStation.displayDecisionId = decision.decisionId;
        displayStation.stableCompletionKey = decision.completionStableKey;
        decision.finalRelation = displayStation.displayRelation;
        const auto duplicateKey = StationKey(displayStation);
        const auto inserted = AppendUnique(displayStation, target, keys);
        if (inserted) {
            if (keptDecisionIdsByKey != nullptr) {
                (*keptDecisionIdsByKey)[duplicateKey] = decision.decisionId;
            }
            MarkDecisionOutcome(
                &decision,
                decision.relationFactPresent
                    ? "display-accepted"
                    : "display-format-only",
                decision.relationFactPresent
                    ? "relation-fact-applied"
                    : "no-relation-fact-displayable",
                decision.relationFactPresent ? "high" : "medium",
                decision.relationFactPresent ? 0.90 : 0.65,
                0.0);
        } else {
            MarkDuplicateDecision(
                &decision,
                displayStation,
                keptDecisionIdsByKey);
            MarkDecisionOutcome(
                &decision,
                "display-rejected-duplicate",
                "duplicate-station-key",
                "medium",
                0.0,
                0.65);
        }
        if (decisions != nullptr) {
            decisions->push_back(std::move(decision));
        }
    }
}

void AddStageDeferredBoardStations(
    const ModuleBoardSnapshot& source,
    BoardSource sourceBoard,
    const BrainDisplayIntentInput& input,
    std::vector<BrainDisplayDecisionRecord>* decisions,
    int* decisionSequence) {
    if (decisions == nullptr) {
        return;
    }
    for (const auto& station : source.stations) {
        auto decision = BuildBaseDecision(
            station,
            sourceBoard,
            input,
            decisionSequence == nullptr ? 0 : (*decisionSequence)++);
        decision.stageSuppressed = true;
        decision.finalRelation =
            decision.relationFactPresent
                ? decision.relationFactValue
                : DisplayRelation::Unknown;
        MarkDecisionOutcome(
            &decision,
            "display-deferred-by-stage",
            "stage=" + WorkflowStageToken(input.workflowStage) +
                "/source=" + BoardSourceToken(sourceBoard) +
                "/board-not-selected",
            "medium",
            0.0,
            0.65);
        decisions->push_back(std::move(decision));
    }
}

void AddFinalDisplayStations(
    const FinalDisplaySnapshot& source,
    FinalDisplaySnapshot* target,
    std::unordered_set<std::string>* keys) {
    if (target == nullptr || keys == nullptr) {
        return;
    }
    for (const auto& station : source.stations) {
        if (!IsDisplayableStation(station)) {
            continue;
        }
        AppendUnique(station, target, keys);
    }
}

FinalDisplaySnapshot MakeDisplayShell(
    const ModuleBoardSnapshot& source,
    BoardSource boardSource) {
    FinalDisplaySnapshot display;
    display.source = boardSource;
    display.airportIcao = source.airportIcao;
    return display;
}

FinalDisplaySnapshot BuildDisplayBoard(
    const BrainDisplayIntentInput& input,
    const FinalDisplaySnapshot& enrouteBoard,
    std::vector<BrainDisplayDecisionRecord>* decisions,
    int* decisionSequence) {
    FinalDisplaySnapshot display;
    std::unordered_set<std::string> keys;
    std::unordered_map<std::string, std::string> keptDecisionIdsByKey;

    if (input.workflowStage == WorkflowStage::Arrival) {
        display = MakeDisplayShell(input.arrivalBoard, BoardSource::Arrival);
        keys.reserve(input.arrivalBoard.stations.size() + enrouteBoard.stations.size());
        AddBoardStations(
            input.arrivalBoard,
            input,
            &display,
            &keys,
            &keptDecisionIdsByKey,
            decisions,
            BoardSource::Arrival,
            decisionSequence);
        AddFinalDisplayStations(enrouteBoard, &display, &keys);
        AddStageDeferredBoardStations(
            input.departureBoard,
            BoardSource::Departure,
            input,
            decisions,
            decisionSequence);
        return display;
    }

    if (input.workflowStage == WorkflowStage::Departure) {
        display = MakeDisplayShell(input.departureBoard, BoardSource::Departure);
        keys.reserve(input.departureBoard.stations.size() + enrouteBoard.stations.size());
        AddBoardStations(
            input.departureBoard,
            input,
            &display,
            &keys,
            &keptDecisionIdsByKey,
            decisions,
            BoardSource::Departure,
            decisionSequence);
        for (const auto& station : enrouteBoard.stations) {
            if (ShouldDisplayDepartureEnrouteCenter(station) &&
                IsDisplayableStation(station)) {
                AppendUnique(station, &display, &keys);
            }
        }
        AddStageDeferredBoardStations(
            input.arrivalBoard,
            BoardSource::Arrival,
            input,
            decisions,
            decisionSequence);
        return display;
    }

    if (input.workflowStage == WorkflowStage::Enroute) {
        AddStageDeferredBoardStations(
            input.departureBoard,
            BoardSource::Departure,
            input,
            decisions,
            decisionSequence);
        AddStageDeferredBoardStations(
            input.arrivalBoard,
            BoardSource::Arrival,
            input,
            decisions,
            decisionSequence);
        return enrouteBoard;
    }

    return display;
}

void SortEnrouteStations(FinalDisplaySnapshot* board) {
    if (board == nullptr) {
        return;
    }
    std::stable_sort(
        board->stations.begin(),
        board->stations.end(),
        [](const auto& left, const auto& right) {
            if (left.displayRelation != right.displayRelation) {
                const auto rank = [](DisplayRelation relation) {
                    switch (relation) {
                        case DisplayRelation::CurrentPolygon:
                            return 0;
                        case DisplayRelation::NextPolygon:
                            return 1;
                        case DisplayRelation::ArrivalPrep:
                            return 2;
                        default:
                            return 3;
                    }
                };
                return rank(left.displayRelation) < rank(right.displayRelation);
            }
            if (left.hasRouteEntryDistance != right.hasRouteEntryDistance) {
                return left.hasRouteEntryDistance && !right.hasRouteEntryDistance;
            }
            if (left.hasRouteEntryDistance && right.hasRouteEntryDistance &&
                left.routeEntryDistanceNm != right.routeEntryDistanceNm) {
                return left.routeEntryDistanceNm < right.routeEntryDistanceNm;
            }
            if (left.tuned != right.tuned) {
                return left.tuned && !right.tuned;
            }
            return left.callsign < right.callsign;
        });
}

template <typename StationSnapshot>
void AddDiagnostic(
    const StationSnapshot& station,
    DisplayRelation relation,
    const char* action,
    std::vector<std::string>* diagnostics) {
    if (diagnostics == nullptr) {
        return;
    }
    std::ostringstream stream;
    stream << action
           << ":callsign=" << station.callsign
           << ",freq=" << station.frequency
           << ",role=" << static_cast<int>(station.role)
           << ",polygon=" << station.polygonKey
           << ",relation=" << ToString(relation);
    if (station.hasRouteEntryDistance) {
        stream << ",distanceNm="
               << static_cast<int>(std::round(station.routeEntryDistanceNm));
    }
    diagnostics->push_back(stream.str());
}

void HashStation(std::uint64_t* hash, const FinalDisplayStationSnapshot& station) {
    HashCombine(hash, static_cast<std::uint64_t>(station.role));
    HashCombine(hash, station.callsign);
    HashCombine(hash, station.frequency);
    HashCombine(hash, station.annotation);
    HashCombine(hash, station.polygonKey);
    HashCombine(hash, static_cast<std::uint64_t>(station.displayRelation));
    HashCombine(hash, static_cast<std::uint64_t>(station.tuned ? 1u : 0u));
    HashCombine(hash, static_cast<std::uint64_t>(station.next ? 1u : 0u));
    HashCombine(hash, static_cast<std::uint64_t>(station.standby ? 1u : 0u));
    HashCombine(hash, static_cast<std::uint64_t>(station.sectorActive ? 1u : 0u));
    HashCombine(hash, static_cast<std::uint64_t>(station.online ? 1u : 0u));
    HashCombine(hash, static_cast<std::uint64_t>(station.offline ? 1u : 0u));
    HashCombine(hash, static_cast<std::uint64_t>(station.hasRouteEntryDistance ? 1u : 0u));
    if (station.hasRouteEntryDistance) {
        HashCombine(hash, station.routeEntryDistanceNm);
    }
}

std::uint64_t HashBoard(const FinalDisplaySnapshot& board) {
    std::uint64_t hash = 0;
    HashCombine(&hash, static_cast<std::uint64_t>(board.source));
    HashCombine(&hash, board.airportIcao);
    HashCombine(&hash, static_cast<std::uint64_t>(board.stations.size()));
    for (const auto& station : board.stations) {
        HashStation(&hash, station);
    }
    return hash;
}

}  // namespace

const char* ToString(DisplayRelation relation) {
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

BrainDisplayIntentOutput RunBrainDisplayIntentWorker(
    const BrainDisplayIntentInput& input) {
    BrainDisplayIntentOutput output;
    output.reason = "brain-display-intent";
    output.departureBoard = input.departureBoard;
    output.arrivalBoard = input.arrivalBoard;
    output.enrouteBoard = input.enrouteBoard;

    auto displayEnrouteBoard =
        MakeDisplayShell(input.enrouteBoard, BoardSource::Enroute);
    std::unordered_set<std::string> enrouteKeys;
    std::unordered_map<std::string, std::string> enrouteDecisionIdsByKey;
    int decisionSequence = 0;
    for (const auto& station : input.enrouteBoard.stations) {
        auto decision = BuildBaseDecision(
            station,
            BoardSource::Enroute,
            input,
            decisionSequence++);
        if (!IsCenter(station)) {
            MarkDecisionOutcome(
                &decision,
                "display-deferred-by-stage",
                "enroute-board-non-center-not-rendered",
                "medium",
                0.0,
                0.65);
            output.displayDecisions.push_back(std::move(decision));
            continue;
        }
        if (!IsDisplayableStation(station)) {
            decision.finalRelation = DisplayRelation::Hidden;
            decision.hardBlock = true;
            MarkDecisionOutcome(
                &decision,
                "display-rejected-non-displayable",
                station.frequency.empty() ? "empty-frequency" : "offline",
                "high",
                0.0,
                0.90);
            output.displayDecisions.push_back(std::move(decision));
            ++output.hidden;
            AddDiagnostic(
                station,
                DisplayRelation::Hidden,
                "hidden",
                &output.diagnostics);
            continue;
        }
        auto relationStation = station;
        auto relation = InferCenterRelation(relationStation, input);
        decision.fallbackRelationValue = relation;
        const auto* relationFact =
            FindRelationFact(station, input.relationFacts);
        if (relationFact != nullptr &&
            IsFinalDisplayRelation(relationFact->displayRelation)) {
            relation = relationFact->displayRelation;
            if (relationFact->hasRouteEntryDistance) {
                relationStation.hasRouteEntryDistance = true;
                relationStation.routeEntryDistanceNm =
                    std::max(0.0, relationFact->routeEntryDistanceNm);
            }
        } else {
            decision.fallbackRelationUsed = true;
        }
        decision.finalRelation = relation;
        if (relation == DisplayRelation::Hidden ||
            relation == DisplayRelation::Filtered ||
            relation == DisplayRelation::Unknown) {
            auto ledgerRelation = relation;
            if (relationFact != nullptr &&
                (relationFact->displayRelation == DisplayRelation::Filtered ||
                 relationFact->displayRelation == DisplayRelation::Hidden ||
                 relationFact->displayRelation == DisplayRelation::Unknown)) {
                ledgerRelation = relationFact->displayRelation;
            }
            decision.finalRelation = ledgerRelation;
            MarkDecisionOutcome(
                &decision,
                ledgerRelation == DisplayRelation::Hidden
                    ? "display-rejected-center-fallback-hidden"
                    : (ledgerRelation == DisplayRelation::Filtered
                           ? "display-rejected-filtered"
                           : "display-rejected-unknown"),
                ledgerRelation == DisplayRelation::Hidden
                    ? (relationFact != nullptr &&
                               relationFact->displayRelation ==
                                   DisplayRelation::Hidden
                           ? "relation-fact-hidden"
                           : "fallback-hidden")
                    : (ledgerRelation == DisplayRelation::Filtered
                           ? "relation-fact-filtered"
                           : "relation-fact-unknown"),
                relationFact == nullptr && ledgerRelation == relation &&
                        decision.fallbackRelationUsed
                    ? "fallback"
                    : "medium",
                0,
                relationFact == nullptr &&
                        ledgerRelation == DisplayRelation::Hidden
                    ? 0.15
                    : 0.65);
            output.displayDecisions.push_back(std::move(decision));
            ++output.hidden;
            AddDiagnostic(station, relation, "hidden", &output.diagnostics);
            continue;
        }

        auto displayStation =
            BuildFinalDisplayStation(relationStation, relation, input);
        displayStation.displayDecisionId = decision.decisionId;
        displayStation.stableCompletionKey = decision.completionStableKey;
        const auto duplicateKey = StationKey(displayStation);
        const auto inserted =
            AppendUnique(displayStation, &displayEnrouteBoard, &enrouteKeys);
        if (inserted) {
            enrouteDecisionIdsByKey[duplicateKey] = decision.decisionId;
            MarkDecisionOutcome(
                &decision,
                "display-accepted",
                decision.relationFactPresent
                    ? "relation-fact-applied"
                    : "fallback-relation-displayable",
                decision.relationFactPresent ? "high" : "fallback",
                decision.relationFactPresent ? 0.90 : 0.15,
                0.0);
        } else {
            MarkDuplicateDecision(
                &decision,
                displayStation,
                &enrouteDecisionIdsByKey);
            MarkDecisionOutcome(
                &decision,
                "display-rejected-duplicate",
                "duplicate-station-key",
                "medium",
                0.0,
                0.65);
        }
        output.displayDecisions.push_back(std::move(decision));
        ++output.displayed;
        AddDiagnostic(
            displayStation,
            relation,
            "display",
            &output.diagnostics);
    }

    SortEnrouteStations(&displayEnrouteBoard);

    output.finalDisplay = BuildDisplayBoard(
        input,
        displayEnrouteBoard,
        &output.displayDecisions,
        &decisionSequence);
    SortFrequencyIntent(input.workflowStage, &output.finalDisplay);
    output.stableHash = HashBoard(output.finalDisplay);
    FinalizeDisplayDecisionLedger(&output);
    ApplyFailSoftPreview(&output);
    BuildOverlayCapLedger(input, &output);
    BuildSourceLinkSummary(&output);
    BuildStableKeyAuditLedger(input, &output);
    BuildStableKeyConsumerDryRunLedger(input, &output);
    BuildSourceOwnedFallbackShadowLedger(input, &output);
    BuildSourceOwnedFallbackLiveConsumptionReadinessLedger(input, &output);
    BuildSourceOwnedFallbackLiveConsumptionLedger(input, &output);
    BuildUpstreamStableKeySourceAuditLedger(&output);

    return output;
}

}  // namespace xvatsim::brain
