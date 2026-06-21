#include "XVatsim/brain/PhaseSnapshotPublisher.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace xvatsim::brain {
namespace {

const char* ToToken(WorkflowStage stage) {
    switch (stage) {
    case WorkflowStage::None:
        return "NONE";
    case WorkflowStage::Departure:
        return "DEPARTURE";
    case WorkflowStage::Enroute:
        return "ENROUTE";
    case WorkflowStage::Arrival:
        return "ARRIVAL";
    }
    return "UNKNOWN";
}

bool IsDisplayableBoard(const FinalDisplaySnapshot& snapshot) {
    return snapshot.available && !snapshot.stations.empty();
}

FinalDisplaySnapshot* MutableSlotForStage(
    PhaseSnapshotPublisherState* state,
    WorkflowStage stage) {
    if (state == nullptr) {
        return nullptr;
    }
    switch (stage) {
    case WorkflowStage::Departure:
        return &state->departure;
    case WorkflowStage::Enroute:
        return &state->enroute;
    case WorkflowStage::Arrival:
        return &state->arrival;
    case WorkflowStage::None:
        break;
    }
    return nullptr;
}

const FinalDisplaySnapshot* SlotForStage(
    const PhaseSnapshotPublisherState& state,
    WorkflowStage stage) {
    switch (stage) {
    case WorkflowStage::Departure:
        return state.hasDeparture ? &state.departure : nullptr;
    case WorkflowStage::Enroute:
        return state.hasEnroute ? &state.enroute : nullptr;
    case WorkflowStage::Arrival:
        return state.hasArrival ? &state.arrival : nullptr;
    case WorkflowStage::None:
        break;
    }
    return nullptr;
}

PhaseSnapshotPublisherSlotMetadata* MutableMetadataForStage(
    PhaseSnapshotPublisherState* state,
    WorkflowStage stage) {
    if (state == nullptr) {
        return nullptr;
    }
    switch (stage) {
    case WorkflowStage::Departure:
        return &state->departureMetadata;
    case WorkflowStage::Enroute:
        return &state->enrouteMetadata;
    case WorkflowStage::Arrival:
        return &state->arrivalMetadata;
    case WorkflowStage::None:
        break;
    }
    return nullptr;
}

const PhaseSnapshotPublisherSlotMetadata* MetadataForStage(
    const PhaseSnapshotPublisherState& state,
    WorkflowStage stage) {
    switch (stage) {
    case WorkflowStage::Departure:
        return state.hasDeparture ? &state.departureMetadata : nullptr;
    case WorkflowStage::Enroute:
        return state.hasEnroute ? &state.enrouteMetadata : nullptr;
    case WorkflowStage::Arrival:
        return state.hasArrival ? &state.arrivalMetadata : nullptr;
    case WorkflowStage::None:
        break;
    }
    return nullptr;
}

const FinalDisplaySnapshot* FirstOtherStageSlot(
    const PhaseSnapshotPublisherState& state,
    WorkflowStage stage,
    WorkflowStage* slotStage,
    const PhaseSnapshotPublisherSlotMetadata** metadata) {
    const auto tryStage =
        [&](WorkflowStage candidateStage) -> const FinalDisplaySnapshot* {
        if (candidateStage == stage) {
            return nullptr;
        }
        if (const auto* snapshot = SlotForStage(state, candidateStage)) {
            if (slotStage != nullptr) {
                *slotStage = candidateStage;
            }
            if (metadata != nullptr) {
                *metadata = MetadataForStage(state, candidateStage);
            }
            return snapshot;
        }
        return nullptr;
    };

    if (const auto* snapshot = tryStage(WorkflowStage::Departure)) {
        return snapshot;
    }
    if (const auto* snapshot = tryStage(WorkflowStage::Enroute)) {
        return snapshot;
    }
    return tryStage(WorkflowStage::Arrival);
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

std::string StationSubjectKey(const FinalDisplayStationSnapshot& station) {
    return std::to_string(static_cast<int>(station.role)) + "|" +
           station.callsign + "|" + station.frequency;
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

std::string GeneratedStableKeyForStation(
    const FinalDisplaySnapshot& snapshot,
    const FinalDisplayStationSnapshot& station) {
    std::ostringstream stream;
    stream << "row|" << BoardSourceToken(snapshot.source) << "|"
           << snapshot.airportIcao << "|"
           << static_cast<int>(station.role) << "|" << station.callsign
           << "|" << station.frequency;
    return stream.str();
}

StableKeyDerivation DeriveStableKey(
    const FinalDisplaySnapshot& snapshot,
    const FinalDisplayStationSnapshot& station) {
    StableKeyDerivation derived;
    if (!station.stableCompletionKey.empty()) {
        derived.key = station.stableCompletionKey;
        derived.source = "final-row";
        derived.status = "stable";
        derived.reason = "explicit-final-row-stable-key";
        return derived;
    }
    if (!station.sourceEvidenceId.empty()) {
        derived.key = "source-evidence|" + station.sourceEvidenceId;
        derived.source = "source-evidence";
        derived.status = "stable";
        derived.reason = "source-evidence-id";
        return derived;
    }
    if (!station.displayDecisionId.empty()) {
        derived.key = "display-decision|" + station.displayDecisionId;
        derived.source = "display-decision";
        derived.status = "fallback-derived";
        derived.reason = "display-decision-id";
        return derived;
    }
    if (station.sourceEvidenceLinkStatus == "synthetic-row") {
        derived.key = GeneratedStableKeyForStation(snapshot, station);
        derived.source = "synthetic-fixture";
        derived.status = "synthetic";
        derived.reason = "synthetic-row-generated-key";
    } else if (station.sourceEvidenceLinkStatus == "legacy-row") {
        derived.key = GeneratedStableKeyForStation(snapshot, station);
        derived.source = "legacy";
        derived.status = "fallback-derived";
        derived.reason = "legacy-row-generated-key";
    } else if (!station.callsign.empty() && !station.frequency.empty()) {
        derived.key = GeneratedStableKeyForStation(snapshot, station);
        derived.source = "generated-fallback";
        derived.status = "fallback-derived";
        derived.reason = "callsign-role-frequency-endpoint-generated-key";
    } else {
        derived.source = "unavailable";
        derived.status = "missing";
        derived.reason = "insufficient-row-identity";
        return derived;
    }
    derived.includesCallsign = true;
    derived.includesRole = true;
    derived.includesFrequency = true;
    derived.includesEndpoint = true;
    derived.includesAirport = true;
    return derived;
}

std::string SnapshotKeyForBoard(const FinalDisplaySnapshot& snapshot) {
    std::ostringstream stream;
    stream << BoardSourceToken(snapshot.source) << "|airport="
           << snapshot.airportIcao << "|rows=" << snapshot.stations.size();
    for (const auto& station : snapshot.stations) {
        stream << "|" << StationSubjectKey(station);
    }
    return stream.str();
}

std::string SnapshotKeyForRequest(
    const PhaseSnapshotPublishRequest& request) {
    if (!request.currentSnapshotKey.empty()) {
        return request.currentSnapshotKey;
    }
    return SnapshotKeyForBoard(request.candidate);
}

std::string SnapshotKeyForPrevious(
    const FinalDisplaySnapshot& snapshot,
    const PhaseSnapshotPublisherSlotMetadata* metadata) {
    if (metadata != nullptr && !metadata->snapshotKey.empty()) {
        return metadata->snapshotKey;
    }
    return SnapshotKeyForBoard(snapshot);
}

std::string EffectiveProductPlanKey(
    const PhaseSnapshotPublishRequest& request) {
    if (!request.productPlanKey.empty()) {
        return request.productPlanKey;
    }
    return request.currentPlanKey;
}

std::string ProductPlanKeySourceForRequest(
    const PhaseSnapshotPublishRequest& request) {
    if (!request.productPlanKeySource.empty()) {
        return request.productPlanKeySource;
    }
    if (!EffectiveProductPlanKey(request).empty()) {
        return "unknown";
    }
    return "unavailable";
}

std::string ProductPlanMissingReasonForRequest(
    const PhaseSnapshotPublishRequest& request) {
    if (!EffectiveProductPlanKey(request).empty()) {
        return "";
    }
    if (!request.productPlanKeyMissingReason.empty()) {
        return request.productPlanKeyMissingReason;
    }
    return "product-plan-key-not-provided";
}

bool ProductPlanAvailableForRequest(
    const PhaseSnapshotPublishRequest& request) {
    if (request.productPlanKeyAvailable) {
        return true;
    }
    return !EffectiveProductPlanKey(request).empty();
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

PhaseSnapshotPublisherSlotMetadata BuildCurrentMetadata(
    const PhaseSnapshotPublishRequest& request) {
    PhaseSnapshotPublisherSlotMetadata metadata;
    metadata.workflowStage = request.stage;
    metadata.planKey = EffectiveProductPlanKey(request);
    metadata.snapshotKey = SnapshotKeyForRequest(request);
    metadata.stale = request.currentSnapshotStale;
    metadata.productPlanKey = EffectiveProductPlanKey(request);
    metadata.productPlanKeyAvailable =
        ProductPlanAvailableForRequest(request);
    metadata.productPlanKeySource = ProductPlanKeySourceForRequest(request);
    metadata.productPlanKeyMissingReason =
        ProductPlanMissingReasonForRequest(request);
    return metadata;
}

const FinalDisplayStationSnapshot* FindCurrentStationForPrevious(
    const FinalDisplaySnapshot& current,
    const FinalDisplayStationSnapshot& previous,
    int* index) {
    for (std::size_t i = 0; i < current.stations.size(); ++i) {
        const auto& station = current.stations[i];
        if (station.callsign == previous.callsign) {
            if (index != nullptr) {
                *index = static_cast<int>(i);
            }
            return &station;
        }
    }
    return nullptr;
}

void FillStationFields(
    const FinalDisplaySnapshot& snapshot,
    const FinalDisplayStationSnapshot& station,
    const PhaseSnapshotPublishRequest& request,
    PhaseSnapshotReuseDecisionRecord* record) {
    if (record == nullptr) {
        return;
    }
    record->displayDecisionId = station.displayDecisionId;
    record->capDecisionId = station.overlayCapDecisionId;
    record->sourceDecisionId = station.sourceDecisionId;
    record->sourceEvidenceId = station.sourceEvidenceId;
    record->subjectKey = StationSubjectKey(station);
    record->callsign = station.callsign;
    record->role = station.role;
    record->frequency = station.frequency;
    record->endpoint = BoardSourceToken(snapshot.source);
    record->airportIcao = snapshot.airportIcao;
    record->sourceEvidenceLinked = !station.sourceEvidenceId.empty();
    if (!station.sourceEvidenceLinkStatus.empty()) {
        record->sourceEvidenceLinkStatus = station.sourceEvidenceLinkStatus;
    } else {
        record->sourceEvidenceLinkStatus =
            record->sourceEvidenceLinked ? "linked" : "unavailable";
    }
    record->confidenceLevel =
        record->sourceEvidenceLinked ? "high" : "none";
    record->fallbackUsed = !record->sourceEvidenceLinked;

    const auto stableKey = DeriveStableKey(snapshot, station);
    record->stableCompletionKey = stableKey.key;
    record->stableCompletionKeyPresent = !stableKey.key.empty();
    record->stableCompletionKeySource = stableKey.source;
    record->stableCompletionKeyStatus = stableKey.status;
    record->keyDerivationReason = stableKey.reason;
    record->keyIncludesCallsign = stableKey.includesCallsign;
    record->keyIncludesRole = stableKey.includesRole;
    record->keyIncludesFrequency = stableKey.includesFrequency;
    record->keyIncludesEndpoint = stableKey.includesEndpoint;
    record->keyIncludesAirport = stableKey.includesAirport;
    record->keyMatchesDisplayDecision = !record->displayDecisionId.empty();
    record->keyMatchesCapDecision = !record->capDecisionId.empty();
    record->keyMatchesPhaseReuseDecision = true;
    record->keyAuditWarning = stableKey.status != "stable";
    record->keyAuditWarningReason =
        record->keyAuditWarning ? stableKey.status : "";
    record->currentBehaviorKey = stableKey.key;
    record->currentBehaviorKeySource = stableKey.source;
    record->sourceOwnedStableCompletionKey =
        station.sourceOwnedStableCompletionKey;
    record->generatedFallbackStableCompletionKey =
        station.generatedFallbackStableCompletionKey;
    record->sourceOwnedKeyPresent =
        station.sourceOwnedStableCompletionKeyPresent;
    record->sourceOwnedKeyMigrationReady =
        station.sourceOwnedKeyMigrationReady;
    record->behaviorConsumerEnabled =
        station.sourceOwnedKeyBehaviorConsumerEnabled;
    record->dryRunDedupeGroupCurrent = StationSubjectKey(station);
    record->dryRunDedupeGroupSourceOwned =
        station.sourceOwnedStableCompletionKey.empty()
            ? "<none>"
            : station.sourceOwnedStableCompletionKey;
    record->dryRunPhaseReuseMatchCurrent = true;
    record->dryRunPhaseReuseMatchSourceOwned =
        station.sourceOwnedStableCompletionKeyPresent;
    if (!record->sourceOwnedKeyPresent) {
        record->dryRunBlockedReason = "source-owned-key-missing";
    } else if (!record->sourceOwnedKeyMigrationReady) {
        record->dryRunBlockedReason =
            station.sourceOwnedKeyPlanContextAvailable
                ? "source-owned-key-not-migration-ready"
                : "missing-plan-context";
    }
    record->dryRunSafeForOptIn =
        record->sourceOwnedKeyPresent &&
        record->sourceOwnedKeyMigrationReady &&
        !record->behaviorConsumerEnabled;
    record->sourceOwnedFallbackShadowGateEnabled =
        request.sourceOwnedFallbackStableKeyShadowEnabled;
    record->sourceOwnedFallbackShadowGateSource =
        NormalizeSourceOwnedFallbackShadowGateSource(
            request.sourceOwnedFallbackStableKeyShadowEnabled,
            request.sourceOwnedFallbackStableKeyShadowGateSource);
    record->shadowBehaviorConsumerEnabled =
        record->behaviorConsumerEnabled;
    record->shadowFinalBoardHashCurrent = "phase-publisher-current";
    record->shadowFinalBoardHashSourceOwned =
        record->sourceOwnedFallbackShadowGateEnabled
            ? "phase-publisher-current"
            : "<skipped>";
    if (!record->sourceOwnedFallbackShadowGateEnabled) {
        record->shadowRecomputeSkippedReason = "shadow-gate-disabled";
        record->shadowFinalBoardHashMatches = true;
        record->shadowRowOrderingMatches = true;
        record->shadowDedupeGroupsMatch = true;
        record->shadowDuplicateSuppressionMatches = true;
        record->shadowCompletionIdentityMatches = true;
        record->shadowPhaseReuseMatches = true;
        record->shadowOverlayCapMatches = true;
        record->shadowMoreAtcMatches = true;
    } else {
        record->shadowRecomputeAttempted = true;
        record->shadowDedupeGroupsMatch =
            !record->dryRunDedupeGroupWouldChange;
        record->shadowDuplicateSuppressionMatches =
            !record->dryRunDuplicateSuppressionWouldChange;
        record->shadowCompletionIdentityMatches =
            !record->dryRunCompletionIdentityWouldChange;
        record->shadowPhaseReuseMatches =
            !record->dryRunPhaseReuseWouldChange;
        record->shadowRowOrderingMatches =
            !record->dryRunRowOrderingWouldChange;
        record->shadowOverlayCapMatches =
            !record->dryRunOverlayCapWouldChange;
        record->shadowMoreAtcMatches =
            !record->dryRunMoreAtcWouldChange;
        record->shadowFinalBoardHashMatches =
            record->shadowDedupeGroupsMatch &&
            record->shadowDuplicateSuppressionMatches &&
            record->shadowCompletionIdentityMatches &&
            record->shadowPhaseReuseMatches &&
            record->shadowRowOrderingMatches &&
            record->shadowOverlayCapMatches &&
            record->shadowMoreAtcMatches;
        record->shadowMissingPlanContextBlocked =
            record->dryRunBlockedReason == "missing-plan-context";
        record->shadowDriftDetected =
            record->dryRunDriftDetected ||
            !record->shadowFinalBoardHashMatches;
        if (record->shadowDriftDetected) {
            record->shadowDriftReason =
                record->dryRunDriftReason.empty()
                    ? "source-owned-shadow-phase-reuse-drift"
                    : record->dryRunDriftReason;
        } else if (record->shadowMissingPlanContextBlocked) {
            record->shadowDriftReason = "missing-plan-context";
        }
        record->shadowSafeForFutureLiveOptIn =
            record->dryRunSafeForOptIn &&
            !record->shadowBehaviorConsumerEnabled &&
            !record->shadowMissingPlanContextBlocked &&
            !record->shadowDriftDetected &&
            record->shadowFinalBoardHashMatches;
    }
}

void RefreshSourceOwnedFallbackShadowDiagnostics(
    PhaseSnapshotReuseDecisionRecord* record) {
    if (record == nullptr) {
        return;
    }
    record->shadowRecomputeAttempted = false;
    record->shadowRecomputeSkippedReason.clear();
    record->shadowBehaviorConsumerEnabled = record->behaviorConsumerEnabled;
    record->shadowFinalBoardHashCurrent = "phase-publisher-current";
    record->shadowFinalBoardHashSourceOwned =
        record->sourceOwnedFallbackShadowGateEnabled
            ? "phase-publisher-current"
            : "<skipped>";
    record->shadowFinalBoardHashMatches = false;
    record->shadowRowOrderingMatches = false;
    record->shadowDedupeGroupsMatch = false;
    record->shadowDuplicateSuppressionMatches = false;
    record->shadowCompletionIdentityMatches = false;
    record->shadowPhaseReuseMatches = false;
    record->shadowOverlayCapMatches = false;
    record->shadowMoreAtcMatches = false;
    record->shadowMissingPlanContextBlocked = false;
    record->shadowDriftDetected = false;
    record->shadowDriftReason.clear();
    record->shadowSafeForFutureLiveOptIn = false;

    if (!record->sourceOwnedFallbackShadowGateEnabled) {
        record->shadowRecomputeSkippedReason = "shadow-gate-disabled";
        record->shadowFinalBoardHashMatches = true;
        record->shadowRowOrderingMatches = true;
        record->shadowDedupeGroupsMatch = true;
        record->shadowDuplicateSuppressionMatches = true;
        record->shadowCompletionIdentityMatches = true;
        record->shadowPhaseReuseMatches = true;
        record->shadowOverlayCapMatches = true;
        record->shadowMoreAtcMatches = true;
        return;
    }

    record->shadowRecomputeAttempted = true;
    record->shadowDedupeGroupsMatch =
        !record->dryRunDedupeGroupWouldChange;
    record->shadowDuplicateSuppressionMatches =
        !record->dryRunDuplicateSuppressionWouldChange;
    record->shadowCompletionIdentityMatches =
        !record->dryRunCompletionIdentityWouldChange;
    record->shadowPhaseReuseMatches =
        !record->dryRunPhaseReuseWouldChange;
    record->shadowRowOrderingMatches =
        !record->dryRunRowOrderingWouldChange;
    record->shadowOverlayCapMatches =
        !record->dryRunOverlayCapWouldChange;
    record->shadowMoreAtcMatches =
        !record->dryRunMoreAtcWouldChange;
    record->shadowFinalBoardHashMatches =
        record->shadowDedupeGroupsMatch &&
        record->shadowDuplicateSuppressionMatches &&
        record->shadowCompletionIdentityMatches &&
        record->shadowPhaseReuseMatches &&
        record->shadowRowOrderingMatches &&
        record->shadowOverlayCapMatches &&
        record->shadowMoreAtcMatches;
    record->shadowMissingPlanContextBlocked =
        record->dryRunBlockedReason == "missing-plan-context";
    record->shadowDriftDetected =
        record->dryRunDriftDetected ||
        !record->shadowFinalBoardHashMatches;
    if (record->shadowDriftDetected) {
        record->shadowDriftReason =
            record->dryRunDriftReason.empty()
                ? "source-owned-shadow-phase-reuse-drift"
                : record->dryRunDriftReason;
    } else if (record->shadowMissingPlanContextBlocked) {
        record->shadowDriftReason = "missing-plan-context";
    }
    record->shadowSafeForFutureLiveOptIn =
        record->dryRunSafeForOptIn &&
        !record->shadowBehaviorConsumerEnabled &&
        !record->shadowMissingPlanContextBlocked &&
        !record->shadowDriftDetected &&
        record->shadowFinalBoardHashMatches;
}

bool PhaseSourceOwnedFallbackShadowParityClean(
    const PhaseSnapshotReuseDecisionRecord& record) {
    return record.shadowFinalBoardHashMatches &&
           record.shadowRowOrderingMatches &&
           record.shadowDedupeGroupsMatch &&
           record.shadowDuplicateSuppressionMatches &&
           record.shadowCompletionIdentityMatches &&
           record.shadowPhaseReuseMatches &&
           record.shadowOverlayCapMatches &&
           record.shadowMoreAtcMatches;
}

void RefreshSourceOwnedFallbackLiveConsumptionReadiness(
    const PhaseSnapshotPublishRequest& request,
    PhaseSnapshotReuseDecisionRecord* record) {
    if (record == nullptr) {
        return;
    }

    record->liveConsumptionProposalGateArmed =
        request.sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled;
    record->liveConsumptionProposalGateSource =
        NormalizeSourceOwnedFallbackLiveConsumptionProposalGateSource(
            request.sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled,
            request
                .sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource);
    record->liveConsumptionShadowParityClean =
        PhaseSourceOwnedFallbackShadowParityClean(*record);
    record->liveConsumptionPlanContextAvailable =
        !record->shadowMissingPlanContextBlocked;
    record->liveConsumptionReadyForFutureOptIn = false;
    record->liveConsumptionBlockedReason.clear();
    record->liveConsumptionBehaviorEnabled = false;

    if (!record->liveConsumptionProposalGateArmed) {
        record->liveConsumptionBlockedReason =
            "live-consumption-proposal-gate-not-armed";
    } else if (record->shadowBehaviorConsumerEnabled) {
        record->liveConsumptionBlockedReason =
            "behavior-consumer-already-enabled";
    } else if (!record->sourceOwnedFallbackShadowGateEnabled) {
        record->liveConsumptionBlockedReason = "shadow-gate-disabled";
    } else if (!record->shadowRecomputeAttempted) {
        record->liveConsumptionBlockedReason = "shadow-parity-not-attempted";
    } else if (!record->liveConsumptionPlanContextAvailable) {
        record->liveConsumptionBlockedReason = "missing-plan-context";
    } else if (record->shadowDriftDetected) {
        record->liveConsumptionBlockedReason =
            record->shadowDriftReason.empty()
                ? "shadow-drift-detected"
                : record->shadowDriftReason;
    } else if (!record->liveConsumptionShadowParityClean) {
        record->liveConsumptionBlockedReason = "shadow-parity-mismatch";
    } else if (!record->shadowSafeForFutureLiveOptIn) {
        record->liveConsumptionBlockedReason =
            "shadow-not-safe-for-future-live-opt-in";
    } else {
        record->liveConsumptionReadyForFutureOptIn = true;
    }
}

void RefreshSourceOwnedFallbackLiveConsumptionDecision(
    const PhaseSnapshotPublishRequest& request,
    PhaseSnapshotReuseDecisionRecord* record) {
    if (record == nullptr) {
        return;
    }

    record->gatedLiveConsumptionDecisionId =
        "source-owned-fallback-live-consumption|phase|" +
        record->phaseReuseDecisionId;
    record->liveConsumptionGateArmed =
        request.sourceOwnedFallbackStableKeyLiveConsumptionEnabled;
    record->liveConsumptionGateSource =
        NormalizeSourceOwnedFallbackLiveConsumptionGateSource(
            request.sourceOwnedFallbackStableKeyLiveConsumptionEnabled,
            request
                .sourceOwnedFallbackStableKeyLiveConsumptionGateSource);
    record->liveConsumptionAllowed = false;
    record->gatedLiveConsumptionBlockedReason.clear();
    record->liveConsumptionDecisionBehaviorChanged = false;

    if (!record->liveConsumptionGateArmed) {
        record->gatedLiveConsumptionBlockedReason =
            "live-consumption-gate-not-armed";
    } else if (!record->liveConsumptionProposalGateArmed) {
        record->gatedLiveConsumptionBlockedReason =
            "live-consumption-proposal-gate-not-armed";
    } else if (!record->sourceOwnedFallbackShadowGateEnabled) {
        record->gatedLiveConsumptionBlockedReason =
            "shadow-gate-disabled";
    } else if (!record->shadowRecomputeAttempted) {
        record->gatedLiveConsumptionBlockedReason =
            "shadow-parity-not-attempted";
    } else if (!record->liveConsumptionPlanContextAvailable) {
        record->gatedLiveConsumptionBlockedReason =
            "missing-plan-context";
    } else if (!record->sourceOwnedKeyPresent) {
        record->gatedLiveConsumptionBlockedReason =
            "source-owned-key-missing";
    } else if (!record->sourceOwnedKeyMigrationReady) {
        record->gatedLiveConsumptionBlockedReason =
            "source-owned-key-not-migration-ready";
    } else if (record->shadowDriftDetected) {
        record->gatedLiveConsumptionBlockedReason =
            record->shadowDriftReason.empty()
                ? "shadow-drift-detected"
                : record->shadowDriftReason;
    } else if (!record->shadowFinalBoardHashMatches) {
        record->gatedLiveConsumptionBlockedReason =
            "final-board-hash-mismatch";
    } else if (!record->shadowRowOrderingMatches) {
        record->gatedLiveConsumptionBlockedReason =
            "row-ordering-mismatch";
    } else if (!record->shadowDedupeGroupsMatch) {
        record->gatedLiveConsumptionBlockedReason =
            "dedupe-group-mismatch";
    } else if (!record->shadowDuplicateSuppressionMatches) {
        record->gatedLiveConsumptionBlockedReason =
            "duplicate-suppression-mismatch";
    } else if (!record->shadowCompletionIdentityMatches) {
        record->gatedLiveConsumptionBlockedReason =
            "completion-identity-mismatch";
    } else if (!record->shadowPhaseReuseMatches) {
        record->gatedLiveConsumptionBlockedReason =
            "phase-reuse-mismatch";
    } else if (!record->shadowOverlayCapMatches) {
        record->gatedLiveConsumptionBlockedReason =
            "overlay-cap-mismatch";
    } else if (!record->shadowMoreAtcMatches) {
        record->gatedLiveConsumptionBlockedReason =
            "more-atc-mismatch";
    } else if (!record->shadowSafeForFutureLiveOptIn) {
        record->gatedLiveConsumptionBlockedReason =
            "shadow-not-safe-for-future-live-opt-in";
    } else if (!record->liveConsumptionReadyForFutureOptIn) {
        record->gatedLiveConsumptionBlockedReason =
            "readiness-not-ready-for-future-live-consumption";
    } else {
        record->liveConsumptionAllowed = true;
    }

    if (record->liveConsumptionAllowed) {
        record->liveConsumptionConsumedKeyType = "source-owned";
        record->liveConsumptionDefaultModeProtected = false;
    } else if (!record->generatedFallbackStableCompletionKey.empty()) {
        record->liveConsumptionConsumedKeyType = "generated-fallback";
        record->liveConsumptionDefaultModeProtected =
            !record->liveConsumptionGateArmed &&
            !record->liveConsumptionDecisionBehaviorChanged;
    } else {
        record->liveConsumptionConsumedKeyType = "none";
        record->liveConsumptionDefaultModeProtected = false;
    }
}

PhaseSnapshotReuseDecisionRecord BuildFreshRecord(
    const PhaseSnapshotPublishRequest& request,
    const FinalDisplaySnapshot& current,
    const FinalDisplayStationSnapshot& station,
    int index,
    int sequence) {
    PhaseSnapshotReuseDecisionRecord record;
    FillStationFields(current, station, request, &record);
    record.phaseReuseDecisionId =
        "phase-reuse|" + std::to_string(sequence) + "|" + record.subjectKey;
    record.currentWorkflowStage = request.stage;
    record.currentPlanKey = EffectiveProductPlanKey(request);
    record.currentSnapshotKey = SnapshotKeyForRequest(request);
    record.currentBoardIndex = index;
    record.freshCurrentEvidenceAvailable = true;
    record.freshCurrentEvidenceAccepted = true;
    record.reuseAllowed = false;
    record.reuseBlockedReason = "fresh-current-evidence";
    record.reuseDecision = "fresh-current-row";
    return record;
}

std::string PlanMismatchDiagnosticSource(
    const PhaseSnapshotPublisherSlotMetadata* previousMetadata,
    const PhaseSnapshotPublishRequest& request) {
    const auto currentSource = ProductPlanKeySourceForRequest(request);
    const auto previousSource =
        previousMetadata != nullptr ? previousMetadata->productPlanKeySource : "";
    if (currentSource == "live-product" || previousSource == "live-product") {
        return "live-product";
    }
    if (currentSource == "harness" || previousSource == "harness") {
        return "harness-probe";
    }
    if (currentSource == "unavailable" || previousSource == "unavailable") {
        return "unavailable";
    }
    return "unknown";
}

void ApplyPlanContext(
    const PhaseSnapshotPublishRequest& request,
    const PhaseSnapshotPublisherSlotMetadata* previousMetadata,
    PhaseSnapshotReuseDecisionRecord* record) {
    if (record == nullptr) {
        return;
    }

    record->currentProductPlanKey = EffectiveProductPlanKey(request);
    record->currentPlanKey = record->currentProductPlanKey;
    record->productPlanKey = record->currentProductPlanKey;
    record->productPlanKeyAvailable =
        ProductPlanAvailableForRequest(request);
    record->productPlanKeySource = ProductPlanKeySourceForRequest(request);
    record->productPlanKeyMissingReason =
        ProductPlanMissingReasonForRequest(request);
    record->previousProductPlanKey =
        previousMetadata != nullptr ? previousMetadata->productPlanKey : "";
    record->previousPlanKey = record->previousProductPlanKey;
    record->planMismatchDiagnosticSource =
        PlanMismatchDiagnosticSource(previousMetadata, request);
    record->phaseReusePlanContextLinked =
        record->productPlanKeyAvailable ||
        (previousMetadata != nullptr &&
         previousMetadata->productPlanKeyAvailable);

    const auto currentSource = record->productPlanKeySource;
    const auto previousSource =
        previousMetadata != nullptr
            ? previousMetadata->productPlanKeySource
            : std::string{};
    const bool currentHarness = currentSource == "harness";
    const bool previousHarness = previousSource == "harness";

    if (!record->productPlanKeyAvailable) {
        record->planContinuityStatus = "missing-current-plan";
        record->planContinuityKnown = false;
        return;
    }
    if (previousMetadata == nullptr ||
        !previousMetadata->productPlanKeyAvailable) {
        record->planContinuityStatus = "missing-previous-plan";
        record->planContinuityKnown = false;
        return;
    }
    if (currentHarness || previousHarness) {
        if (record->previousProductPlanKey != record->currentProductPlanKey) {
            record->planContinuityStatus = "harness-probe";
        } else {
            record->planContinuityStatus = "same-plan";
        }
        record->planContinuityKnown = true;
        return;
    }
    if (record->previousProductPlanKey == record->currentProductPlanKey) {
        record->planContinuityStatus = "same-plan";
    } else {
        record->planContinuityStatus = "changed-plan";
    }
    record->planContinuityKnown = true;
}

PhaseSnapshotReuseDecisionRecord BuildPreviousRecord(
    const PhaseSnapshotPublishRequest& request,
    const FinalDisplaySnapshot& previous,
    const PhaseSnapshotPublisherSlotMetadata* previousMetadata,
    const FinalDisplayStationSnapshot& station,
    int previousIndex,
    int currentIndex,
    int sequence,
    const std::string& reuseDecision,
    const std::string& blockedReason) {
    PhaseSnapshotReuseDecisionRecord record;
    FillStationFields(previous, station, request, &record);
    record.phaseReuseDecisionId =
        "phase-reuse|" + std::to_string(sequence) + "|" + record.subjectKey;
    record.previousWorkflowStage =
        previousMetadata != nullptr
            ? previousMetadata->workflowStage
            : request.stage;
    record.currentWorkflowStage = request.stage;
    record.previousPlanKey =
        previousMetadata != nullptr ? previousMetadata->planKey : "";
    record.currentPlanKey = EffectiveProductPlanKey(request);
    record.previousSnapshotKey =
        SnapshotKeyForPrevious(previous, previousMetadata);
    record.currentSnapshotKey = SnapshotKeyForRequest(request);
    record.previousBoardIndex = previousIndex;
    record.currentBoardIndex = currentIndex;
    record.reuseCandidate = true;
    record.freshCurrentEvidenceIncomplete =
        request.verificationPending || previous.stations.empty();
    record.reuseDecision = reuseDecision;
    record.reuseBlockedReason = blockedReason;
    record.reuseAllowed = reuseDecision == "reused-last-proven-row";
    record.reusedFromPreviousSnapshot = record.reuseAllowed;
    record.reusedBecauseCurrentIncomplete =
        record.reuseAllowed && request.verificationPending;
    record.displacedByFreshEvidence =
        reuseDecision == "displaced-by-fresh-current-row";
    record.staleReuseBlocked = reuseDecision == "blocked-stale-reuse";
    ApplyPlanContext(request, previousMetadata, &record);
    return record;
}

void PushFreshCurrentRecords(
    const PhaseSnapshotPublishRequest& request,
    const FinalDisplaySnapshot& current,
    std::vector<PhaseSnapshotReuseDecisionRecord>* records,
    int* sequence) {
    if (records == nullptr || sequence == nullptr) {
        return;
    }
    for (std::size_t index = 0; index < current.stations.size(); ++index) {
        records->push_back(
            BuildFreshRecord(
                request,
                current,
                current.stations[index],
                static_cast<int>(index),
                (*sequence)++));
        ApplyPlanContext(request, nullptr, &records->back());
    }
}

std::string BlockReasonForPreviousAgainstCurrent(
    const PhaseSnapshotPublishRequest& request,
    const PhaseSnapshotPublisherSlotMetadata* previousMetadata,
    const FinalDisplayStationSnapshot* currentStation,
    const FinalDisplayStationSnapshot& previousStation,
    std::string* decision) {
    if (decision == nullptr) {
        return "unknown";
    }
    if (previousMetadata != nullptr && previousMetadata->stale) {
        *decision = "blocked-stale-reuse";
        return "previous-snapshot-stale";
    }
    if (previousMetadata != nullptr &&
        !previousMetadata->planKey.empty() &&
        !EffectiveProductPlanKey(request).empty() &&
        previousMetadata->planKey != EffectiveProductPlanKey(request)) {
        *decision = "blocked-plan-mismatch";
        return "plan-mismatch";
    }
    if (currentStation != nullptr &&
        currentStation->role != previousStation.role) {
        *decision = "blocked-role-mismatch";
        return "role-mismatch";
    }
    if (currentStation != nullptr &&
        currentStation->frequency != previousStation.frequency) {
        *decision = "blocked-frequency-mismatch";
        return "frequency-mismatch";
    }
    *decision = "displaced-by-fresh-current-row";
    return "fresh-current-evidence-accepted";
}

void PushPreviousDisplacementRecords(
    const PhaseSnapshotPublishRequest& request,
    const FinalDisplaySnapshot& previous,
    const PhaseSnapshotPublisherSlotMetadata* previousMetadata,
    const FinalDisplaySnapshot& current,
    std::vector<PhaseSnapshotReuseDecisionRecord>* records,
    int* sequence) {
    if (records == nullptr || sequence == nullptr) {
        return;
    }
    for (std::size_t index = 0; index < previous.stations.size(); ++index) {
        int currentIndex = -1;
        const auto* currentStation =
            FindCurrentStationForPrevious(
                current,
                previous.stations[index],
                &currentIndex);
        std::string decision;
        const auto reason =
            BlockReasonForPreviousAgainstCurrent(
                request,
                previousMetadata,
                currentStation,
                previous.stations[index],
                &decision);
        auto record =
            BuildPreviousRecord(
                request,
                previous,
                previousMetadata,
                previous.stations[index],
                static_cast<int>(index),
                currentIndex,
                (*sequence)++,
                decision,
                reason);
        record.freshCurrentEvidenceAvailable = true;
        record.freshCurrentEvidenceAccepted = true;
        record.freshCurrentEvidenceIncomplete = false;
        if (currentStation != nullptr) {
            const auto currentStableKey =
                DeriveStableKey(current, *currentStation).key;
            const bool currentBehaviorWouldMatch =
                currentStation->role == previous.stations[index].role &&
                currentStation->frequency == previous.stations[index].frequency;
            const bool sourceOwnedWouldMatch =
                !record.sourceOwnedStableCompletionKey.empty() &&
                record.sourceOwnedStableCompletionKey ==
                    currentStation->sourceOwnedStableCompletionKey;
            record.dryRunPhaseReuseMatchCurrent = currentBehaviorWouldMatch;
            record.dryRunPhaseReuseMatchSourceOwned =
                sourceOwnedWouldMatch;
            if (record.sourceOwnedKeyPresent &&
                record.sourceOwnedKeyMigrationReady &&
                currentBehaviorWouldMatch != sourceOwnedWouldMatch) {
                record.dryRunPhaseReuseWouldChange = true;
                record.dryRunDriftDetected = true;
                record.dryRunDriftReason =
                    "source-owned-phase-reuse-match-drift";
                record.dryRunBlockedReason = "dry-run-drift-detected";
                record.dryRunSafeForOptIn = false;
            }
            record.keyContinuityKnown = true;
            record.keyChangedAcrossReuse =
                currentStableKey != record.stableCompletionKey;
            record.unsafeSameKeyAcrossChangedFacts =
                currentStableKey == record.stableCompletionKey &&
                (currentStation->role != previous.stations[index].role ||
                 currentStation->frequency != previous.stations[index].frequency);
            if (record.keyChangedAcrossReuse) {
                record.keyAuditWarning = true;
                record.keyAuditWarningReason = "changed-across-reuse";
            } else if (record.unsafeSameKeyAcrossChangedFacts) {
                record.keyAuditWarning = true;
                record.keyAuditWarningReason = "unsafe-same-key";
            }
        } else {
            record.dryRunPhaseReuseMatchCurrent = false;
            record.dryRunPhaseReuseMatchSourceOwned = false;
        }
        records->push_back(std::move(record));
    }
}

void PushReusedRecords(
    const PhaseSnapshotPublishRequest& request,
    const FinalDisplaySnapshot& previous,
    const PhaseSnapshotPublisherSlotMetadata* previousMetadata,
    std::vector<PhaseSnapshotReuseDecisionRecord>* records,
    int* sequence) {
    if (records == nullptr || sequence == nullptr) {
        return;
    }
    for (std::size_t index = 0; index < previous.stations.size(); ++index) {
        auto record = BuildPreviousRecord(
                request,
                previous,
                previousMetadata,
                previous.stations[index],
                static_cast<int>(index),
                static_cast<int>(index),
                (*sequence)++,
                "reused-last-proven-row",
                "");
        record.keyContinuityKnown = true;
        record.keyChangedAcrossReuse = false;
        records->push_back(std::move(record));
    }
}

void PushStageMismatchRecords(
    const PhaseSnapshotPublishRequest& request,
    const FinalDisplaySnapshot& previous,
    const PhaseSnapshotPublisherSlotMetadata* previousMetadata,
    std::vector<PhaseSnapshotReuseDecisionRecord>* records,
    int* sequence) {
    if (records == nullptr || sequence == nullptr) {
        return;
    }
    for (std::size_t index = 0; index < previous.stations.size(); ++index) {
        auto record =
            BuildPreviousRecord(
                request,
                previous,
                previousMetadata,
                previous.stations[index],
                static_cast<int>(index),
                -1,
                (*sequence)++,
                "blocked-stage-mismatch",
                "stage-mismatch");
        record.freshCurrentEvidenceIncomplete = true;
        records->push_back(std::move(record));
    }
}

void PushNoReuseCandidateRecord(
    const PhaseSnapshotPublishRequest& request,
    std::vector<PhaseSnapshotReuseDecisionRecord>* records,
    int* sequence) {
    if (records == nullptr || sequence == nullptr) {
        return;
    }
    PhaseSnapshotReuseDecisionRecord record;
    record.phaseReuseDecisionId =
        "phase-reuse|" + std::to_string((*sequence)++) + "|no-candidate";
    record.subjectKey = "no-reuse-candidate";
    record.currentWorkflowStage = request.stage;
    record.currentPlanKey = EffectiveProductPlanKey(request);
    record.currentSnapshotKey = SnapshotKeyForRequest(request);
    record.freshCurrentEvidenceIncomplete = request.verificationPending;
    record.reuseDecision = "no-reuse-candidate";
    record.reuseBlockedReason = "no-last-proven-snapshot";
    record.sourceEvidenceLinkStatus = "unavailable";
    record.confidenceLevel = "none";
    record.fallbackUsed = true;
    ApplyPlanContext(request, nullptr, &record);
    records->push_back(std::move(record));
}

PhaseSnapshotReuseSummary BuildReuseSummary(
    const std::vector<PhaseSnapshotReuseDecisionRecord>& records) {
    PhaseSnapshotReuseSummary summary;
    summary.phaseReuseDecisionCount = static_cast<int>(records.size());
    summary.phaseReuseLedgerBrainOwned = true;
    summary.displayBehaviorChanged = false;
    for (const auto& record : records) {
        if (record.reuseDecision == "fresh-current-row") {
            ++summary.freshCurrentRowCount;
        } else if (record.reuseDecision == "reused-last-proven-row") {
            ++summary.reusedLastProvenRowCount;
        } else if (record.reuseDecision ==
                   "displaced-by-fresh-current-row") {
            ++summary.displacedByFreshEvidenceCount;
        } else if (record.reuseDecision == "blocked-stale-reuse") {
            ++summary.blockedReuseCount;
            ++summary.staleReuseBlockedCount;
        } else if (record.reuseDecision == "blocked-plan-mismatch") {
            ++summary.blockedReuseCount;
            ++summary.planMismatchBlockedCount;
        } else if (record.reuseDecision == "blocked-stage-mismatch") {
            ++summary.blockedReuseCount;
            ++summary.stageMismatchBlockedCount;
        } else if (record.reuseDecision == "blocked-frequency-mismatch") {
            ++summary.blockedReuseCount;
            ++summary.frequencyMismatchBlockedCount;
        } else if (record.reuseDecision == "blocked-role-mismatch") {
            ++summary.blockedReuseCount;
            ++summary.roleMismatchBlockedCount;
        } else if (record.reuseDecision == "no-reuse-candidate") {
            ++summary.noReuseCandidateCount;
        }
    }
    return summary;
}

PhaseSnapshotPlanContextSummary BuildPlanContextSummary(
    const std::vector<PhaseSnapshotReuseDecisionRecord>& records) {
    PhaseSnapshotPlanContextSummary summary;
    summary.phasePlanContextDecisionCount = static_cast<int>(records.size());
    summary.phasePlanContextBrainOwned = true;
    summary.publishBehaviorChanged = false;
    for (const auto& record : records) {
        if (record.productPlanKeyAvailable) {
            ++summary.productPlanKeyAvailableCount;
        } else {
            ++summary.productPlanKeyMissingCount;
        }
        if (record.productPlanKeySource == "live-product") {
            ++summary.liveProductPlanContextCount;
        }
        if (record.productPlanKeySource == "harness" ||
            record.planMismatchDiagnosticSource == "harness-probe") {
            ++summary.harnessPlanProbeCount;
        }
        if (record.productPlanKeySource == "unavailable" ||
            record.planContinuityStatus == "missing-current-plan" ||
            record.planContinuityStatus == "missing-previous-plan") {
            ++summary.missingPlanContextCount;
        }
        if (record.planContinuityKnown) {
            ++summary.planContinuityKnownCount;
        } else {
            ++summary.planContinuityUnknownCount;
        }
        if (record.planContinuityStatus == "changed-plan" &&
            record.planMismatchDiagnosticSource == "live-product") {
            ++summary.livePlanMismatchCount;
        }
        if (record.planContinuityStatus == "harness-probe" ||
            (record.reuseDecision == "blocked-plan-mismatch" &&
             record.planMismatchDiagnosticSource == "harness-probe")) {
            ++summary.harnessPlanMismatchCount;
        }
    }
    return summary;
}

void MarkDuplicatePhaseStableKeys(
    std::vector<PhaseSnapshotReuseDecisionRecord>* records) {
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

PhaseSnapshotStableKeyAuditSummary BuildPhaseStableKeySummary(
    const std::vector<PhaseSnapshotReuseDecisionRecord>& records) {
    PhaseSnapshotStableKeyAuditSummary summary;
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
        if (!record.capDecisionId.empty()) {
            ++summary.keyLedgerLinkedCapCount;
        }
        if (!record.phaseReuseDecisionId.empty()) {
            ++summary.keyLedgerLinkedPhaseReuseCount;
        }
    }
    return summary;
}

PhaseSnapshotStableKeyConsumerDryRunSummary
BuildPhaseStableKeyConsumerDryRunSummary(
    const std::vector<PhaseSnapshotReuseDecisionRecord>& records) {
    PhaseSnapshotStableKeyConsumerDryRunSummary summary;
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

PhaseSnapshotSourceOwnedFallbackStableKeyShadowSummary
BuildPhaseSourceOwnedFallbackShadowSummary(
    const std::vector<PhaseSnapshotReuseDecisionRecord>& records) {
    PhaseSnapshotSourceOwnedFallbackStableKeyShadowSummary summary;
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

PhaseSnapshotSourceOwnedFallbackStableKeyLiveConsumptionReadinessSummary
BuildPhaseSourceOwnedFallbackLiveConsumptionReadinessSummary(
    const std::vector<PhaseSnapshotReuseDecisionRecord>& records) {
    PhaseSnapshotSourceOwnedFallbackStableKeyLiveConsumptionReadinessSummary
        summary;
    summary.readinessDecisionCount = static_cast<int>(records.size());
    summary.behaviorChanged = false;
    for (const auto& record : records) {
        if (record.liveConsumptionProposalGateArmed) {
            ++summary.proposalGateArmedCount;
        }
        if (record.liveConsumptionShadowParityClean) {
            ++summary.shadowParityCleanCount;
        }
        if (record.liveConsumptionPlanContextAvailable) {
            ++summary.planContextAvailableCount;
        }
        if (!record.liveConsumptionPlanContextAvailable) {
            ++summary.missingPlanBlockedCount;
        }
        if (record.shadowDriftDetected) {
            ++summary.driftBlockedCount;
        }
        if (record.liveConsumptionBlockedReason == "shadow-gate-disabled" ||
            record.liveConsumptionBlockedReason ==
                "shadow-parity-not-attempted") {
            ++summary.shadowNotAttemptedBlockedCount;
        }
        if (!record.liveConsumptionReadyForFutureOptIn) {
            ++summary.readinessBlockedCount;
        }
        if (record.liveConsumptionReadyForFutureOptIn) {
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

PhaseSnapshotSourceOwnedFallbackStableKeyLiveConsumptionSummary
BuildPhaseSourceOwnedFallbackLiveConsumptionSummary(
    const std::vector<PhaseSnapshotReuseDecisionRecord>& records) {
    PhaseSnapshotSourceOwnedFallbackStableKeyLiveConsumptionSummary summary;
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
        if (record.liveConsumptionConsumedKeyType == "source-owned") {
            ++summary.sourceOwnedConsumedCount;
        } else if (
            record.liveConsumptionConsumedKeyType == "generated-fallback") {
            ++summary.generatedFallbackConsumedCount;
        }
        if (record.gatedLiveConsumptionBlockedReason ==
            "missing-plan-context") {
            ++summary.missingPlanBlockedCount;
        }
        if (record.gatedLiveConsumptionBlockedReason ==
            "shadow-gate-disabled") {
            ++summary.shadowGateOffBlockedCount;
        }
        if (record.gatedLiveConsumptionBlockedReason ==
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
        if (record.gatedLiveConsumptionBlockedReason ==
            "source-owned-key-missing") {
            ++summary.missingSourceOwnedKeyBlockedCount;
        }
        if (record.gatedLiveConsumptionBlockedReason ==
            "source-owned-key-not-migration-ready") {
            ++summary.migrationNotReadyBlockedCount;
        }
        if (record.liveConsumptionDefaultModeProtected) {
            ++summary.defaultModeProtectedCount;
        }
        if (record.liveConsumptionDecisionBehaviorChanged) {
            summary.behaviorChanged = true;
        }
    }
    return summary;
}

void BuildReuseLedger(
    const PhaseSnapshotPublisherState* stateBeforePublish,
    const PhaseSnapshotPublishRequest& request,
    bool candidateIsDisplayable,
    const FinalDisplaySnapshot* previousForStage,
    const PhaseSnapshotPublisherSlotMetadata* previousMetadata,
    PhaseSnapshotPublishResult* result) {
    if (result == nullptr) {
        return;
    }

    std::vector<PhaseSnapshotReuseDecisionRecord> records;
    int sequence = 0;

    if (candidateIsDisplayable) {
        PushFreshCurrentRecords(request, request.candidate, &records, &sequence);
        if (previousForStage != nullptr) {
            PushPreviousDisplacementRecords(
                request,
                *previousForStage,
                previousMetadata,
                request.candidate,
                &records,
                &sequence);
        }
    } else if (result->usedLastProven && previousForStage != nullptr) {
        PushReusedRecords(
            request,
            *previousForStage,
            previousMetadata,
            &records,
            &sequence);
    } else if (stateBeforePublish != nullptr && request.verificationPending) {
        WorkflowStage otherStage = WorkflowStage::None;
        const PhaseSnapshotPublisherSlotMetadata* otherMetadata = nullptr;
        if (const auto* otherSnapshot =
                FirstOtherStageSlot(
                    *stateBeforePublish,
                    request.stage,
                    &otherStage,
                    &otherMetadata)) {
            (void)otherStage;
            PushStageMismatchRecords(
                request,
                *otherSnapshot,
                otherMetadata,
                &records,
                &sequence);
        }
    }

    if (records.empty()) {
        PushNoReuseCandidateRecord(request, &records, &sequence);
    }

    MarkDuplicatePhaseStableKeys(&records);
    for (auto& record : records) {
        RefreshSourceOwnedFallbackShadowDiagnostics(&record);
        RefreshSourceOwnedFallbackLiveConsumptionReadiness(request, &record);
        RefreshSourceOwnedFallbackLiveConsumptionDecision(request, &record);
    }
    result->phaseReuseDecisions = std::move(records);
    result->phaseReuseSummary =
        BuildReuseSummary(result->phaseReuseDecisions);
    result->phasePlanContextSummary =
        BuildPlanContextSummary(result->phaseReuseDecisions);
    result->phaseStableKeyAuditSummary =
        BuildPhaseStableKeySummary(result->phaseReuseDecisions);
    result->phaseStableKeyConsumerDryRunSummary =
        BuildPhaseStableKeyConsumerDryRunSummary(result->phaseReuseDecisions);
    result->phaseSourceOwnedFallbackStableKeyShadowSummary =
        BuildPhaseSourceOwnedFallbackShadowSummary(
            result->phaseReuseDecisions);
    result
        ->phaseSourceOwnedFallbackStableKeyLiveConsumptionReadinessSummary =
        BuildPhaseSourceOwnedFallbackLiveConsumptionReadinessSummary(
            result->phaseReuseDecisions);
    result->phaseSourceOwnedFallbackStableKeyLiveConsumptionSummary =
        BuildPhaseSourceOwnedFallbackLiveConsumptionSummary(
            result->phaseReuseDecisions);
}

void MarkSlotProven(PhaseSnapshotPublisherState* state, WorkflowStage stage) {
    if (state == nullptr) {
        return;
    }
    switch (stage) {
    case WorkflowStage::Departure:
        state->hasDeparture = true;
        return;
    case WorkflowStage::Enroute:
        state->hasEnroute = true;
        return;
    case WorkflowStage::Arrival:
        state->hasArrival = true;
        return;
    case WorkflowStage::None:
        return;
    }
}

void AppendStatus(
    std::ostringstream* stream,
    const char* key,
    bool value) {
    if (stream == nullptr) {
        return;
    }
    *stream << "," << key << "=" << (value ? 1 : 0);
}

}  // namespace

void PhaseSnapshotPublisherState::Reset() {
    hasDeparture = false;
    hasEnroute = false;
    hasArrival = false;
    departure = {};
    enroute = {};
    arrival = {};
    departureMetadata = {};
    enrouteMetadata = {};
    arrivalMetadata = {};
}

PhaseSnapshotPublishResult PublishPhaseSnapshot(
    PhaseSnapshotPublisherState* state,
    const PhaseSnapshotPublishRequest& request) {
    PhaseSnapshotPublishResult result;
    result.snapshot = request.candidate;
    result.verificationPending = request.verificationPending;

    PhaseSnapshotPublisherState stateBeforePublish;
    const PhaseSnapshotPublisherState* stateBeforePublishPtr = nullptr;
    FinalDisplaySnapshot previousForStage;
    bool hasPreviousForStage = false;
    PhaseSnapshotPublisherSlotMetadata previousMetadata;
    const PhaseSnapshotPublisherSlotMetadata* previousMetadataPtr = nullptr;
    if (state != nullptr) {
        stateBeforePublish = *state;
        stateBeforePublishPtr = &stateBeforePublish;
        if (const auto* previous = SlotForStage(*state, request.stage)) {
            previousForStage = *previous;
            hasPreviousForStage = true;
        }
        if (const auto* metadata = MetadataForStage(*state, request.stage)) {
            previousMetadata = *metadata;
            previousMetadataPtr = &previousMetadata;
        }
    }

    const auto candidateIsDisplayable =
        IsDisplayableBoard(request.candidate);
    if (state != nullptr && request.stage != WorkflowStage::None &&
        candidateIsDisplayable) {
        if (auto* slot = MutableSlotForStage(state, request.stage)) {
            *slot = request.candidate;
            if (auto* metadata = MutableMetadataForStage(state, request.stage)) {
                *metadata = BuildCurrentMetadata(request);
            }
            MarkSlotProven(state, request.stage);
            result.storedNewProven = true;
        }
    } else if (state != nullptr && request.verificationPending) {
        if (const auto* lastProven = SlotForStage(*state, request.stage)) {
            result.snapshot = *lastProven;
            result.usedLastProven = true;
        }
    }

    BuildReuseLedger(
        stateBeforePublishPtr,
        request,
        candidateIsDisplayable,
        hasPreviousForStage ? &previousForStage : nullptr,
        previousMetadataPtr,
        &result);

    std::ostringstream status;
    status << "PHASE_PUBLISH stage=" << ToToken(request.stage)
           << ",stations=" << result.snapshot.stations.size();
    AppendStatus(&status, "candidateDisplay", candidateIsDisplayable);
    AppendStatus(&status, "pending", request.verificationPending);
    AppendStatus(&status, "stored", result.storedNewProven);
    AppendStatus(&status, "reused", result.usedLastProven);
    if (!request.reason.empty()) {
        status << ",reason=" << request.reason;
    }
    result.statusLine = status.str();
    return result;
}

std::string PhaseSnapshotPublisherStateSummary(
    const PhaseSnapshotPublisherState& state) {
    std::ostringstream stream;
    stream << "departure=" << (state.hasDeparture ? state.departure.stations.size() : 0)
           << ",enroute=" << (state.hasEnroute ? state.enroute.stations.size() : 0)
           << ",arrival=" << (state.hasArrival ? state.arrival.stations.size() : 0);
    return stream.str();
}

}  // namespace xvatsim::brain
