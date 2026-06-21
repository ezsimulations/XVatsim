#pragma once

#include <string>
#include <vector>

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::brain {

struct PhaseSnapshotPublisherSlotMetadata {
    WorkflowStage workflowStage = WorkflowStage::None;
    std::string planKey;
    std::string snapshotKey;
    bool stale = false;
    std::string productPlanKey;
    bool productPlanKeyAvailable = false;
    std::string productPlanKeySource;
    std::string productPlanKeyMissingReason;
};

struct PhaseSnapshotPublisherState {
    bool hasDeparture = false;
    bool hasEnroute = false;
    bool hasArrival = false;
    FinalDisplaySnapshot departure;
    FinalDisplaySnapshot enroute;
    FinalDisplaySnapshot arrival;
    PhaseSnapshotPublisherSlotMetadata departureMetadata;
    PhaseSnapshotPublisherSlotMetadata enrouteMetadata;
    PhaseSnapshotPublisherSlotMetadata arrivalMetadata;

    void Reset();
};

struct PhaseSnapshotPublishRequest {
    WorkflowStage stage = WorkflowStage::None;
    FinalDisplaySnapshot candidate;
    bool verificationPending = false;
    std::string reason;
    std::string currentPlanKey;
    std::string currentSnapshotKey;
    bool currentSnapshotStale = false;
    std::string productPlanKey;
    bool productPlanKeyAvailable = false;
    std::string productPlanKeySource;
    std::string productPlanKeyMissingReason;
    bool sourceOwnedFallbackStableKeyShadowEnabled = false;
    std::string sourceOwnedFallbackStableKeyShadowGateSource = "default";
    bool sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled = false;
    std::string sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource =
        "default";
    bool sourceOwnedFallbackStableKeyLiveConsumptionEnabled = false;
    std::string sourceOwnedFallbackStableKeyLiveConsumptionGateSource =
        "default";
};

struct PhaseSnapshotReuseDecisionRecord {
    std::string phaseReuseDecisionId;
    std::string displayDecisionId;
    std::string capDecisionId;
    std::string sourceDecisionId;
    std::string sourceEvidenceId;
    std::string subjectKey;
    std::string callsign;
    StationRole role = StationRole::Other;
    std::string frequency;
    std::string endpoint;
    std::string airportIcao;
    WorkflowStage previousWorkflowStage = WorkflowStage::None;
    WorkflowStage currentWorkflowStage = WorkflowStage::None;
    std::string previousPlanKey;
    std::string currentPlanKey;
    std::string productPlanKey;
    bool productPlanKeyAvailable = false;
    std::string productPlanKeySource;
    std::string productPlanKeyMissingReason;
    std::string previousProductPlanKey;
    std::string currentProductPlanKey;
    bool planContinuityKnown = false;
    std::string planContinuityStatus;
    std::string planMismatchDiagnosticSource;
    bool phaseReusePlanContextLinked = false;
    std::string previousSnapshotKey;
    std::string currentSnapshotKey;
    int previousBoardIndex = -1;
    int currentBoardIndex = -1;
    bool reuseCandidate = false;
    bool reusedFromPreviousSnapshot = false;
    bool freshCurrentEvidenceAvailable = false;
    bool freshCurrentEvidenceAccepted = false;
    bool freshCurrentEvidenceIncomplete = false;
    bool reusedBecauseCurrentIncomplete = false;
    bool displacedByFreshEvidence = false;
    bool staleReuseBlocked = false;
    bool reuseAllowed = false;
    std::string reuseBlockedReason;
    std::string reuseDecision;
    bool sourceEvidenceLinked = false;
    std::string sourceEvidenceLinkStatus;
    std::string confidenceLevel;
    bool fallbackUsed = false;
    std::string stableCompletionKey;
    bool stableCompletionKeyPresent = false;
    std::string stableCompletionKeySource;
    std::string stableCompletionKeyStatus;
    std::string keyDerivationReason;
    bool keyIncludesCallsign = false;
    bool keyIncludesRole = false;
    bool keyIncludesFrequency = false;
    bool keyIncludesEndpoint = false;
    bool keyIncludesAirport = false;
    bool keyMatchesDisplayDecision = false;
    bool keyMatchesCapDecision = false;
    bool keyMatchesPhaseReuseDecision = false;
    bool duplicateKeyDetected = false;
    std::string duplicateKeyGroup;
    bool keyContinuityKnown = false;
    bool keyChangedAcrossReuse = false;
    bool unsafeSameKeyAcrossChangedFacts = false;
    bool keyAuditWarning = false;
    std::string keyAuditWarningReason;
    std::string currentBehaviorKey;
    std::string sourceOwnedStableCompletionKey;
    std::string generatedFallbackStableCompletionKey;
    std::string currentBehaviorKeySource;
    bool sourceOwnedKeyPresent = false;
    bool sourceOwnedKeyMigrationReady = false;
    bool behaviorConsumerEnabled = false;
    std::string dryRunDedupeGroupCurrent;
    std::string dryRunDedupeGroupSourceOwned;
    bool dryRunDedupeGroupWouldChange = false;
    bool dryRunDuplicateSuppressionWouldChange = false;
    bool dryRunCompletionIdentityWouldChange = false;
    bool dryRunPhaseReuseMatchCurrent = false;
    bool dryRunPhaseReuseMatchSourceOwned = false;
    bool dryRunPhaseReuseWouldChange = false;
    bool dryRunRowOrderingWouldChange = false;
    bool dryRunOverlayCapWouldChange = false;
    bool dryRunMoreAtcWouldChange = false;
    bool dryRunDriftDetected = false;
    std::string dryRunDriftReason;
    bool dryRunSafeForOptIn = false;
    std::string dryRunBlockedReason;
    bool sourceOwnedFallbackShadowGateEnabled = false;
    std::string sourceOwnedFallbackShadowGateSource = "default";
    bool shadowRecomputeAttempted = false;
    std::string shadowRecomputeSkippedReason;
    bool shadowBehaviorConsumerEnabled = false;
    std::string shadowFinalBoardHashCurrent;
    std::string shadowFinalBoardHashSourceOwned;
    bool shadowFinalBoardHashMatches = false;
    bool shadowRowOrderingMatches = false;
    bool shadowDedupeGroupsMatch = false;
    bool shadowDuplicateSuppressionMatches = false;
    bool shadowCompletionIdentityMatches = false;
    bool shadowPhaseReuseMatches = false;
    bool shadowOverlayCapMatches = false;
    bool shadowMoreAtcMatches = false;
    bool shadowMissingPlanContextBlocked = false;
    bool shadowDriftDetected = false;
    std::string shadowDriftReason;
    bool shadowSafeForFutureLiveOptIn = false;
    bool liveConsumptionProposalGateArmed = false;
    std::string liveConsumptionProposalGateSource = "default";
    bool liveConsumptionShadowParityClean = false;
    bool liveConsumptionPlanContextAvailable = false;
    bool liveConsumptionReadyForFutureOptIn = false;
    std::string liveConsumptionBlockedReason;
    bool liveConsumptionBehaviorEnabled = false;
    std::string gatedLiveConsumptionDecisionId;
    bool liveConsumptionGateArmed = false;
    std::string liveConsumptionGateSource = "default";
    bool liveConsumptionAllowed = false;
    std::string gatedLiveConsumptionBlockedReason;
    std::string liveConsumptionConsumedKeyType = "generated-fallback";
    bool liveConsumptionDecisionBehaviorChanged = false;
    bool liveConsumptionDefaultModeProtected = true;
};

struct PhaseSnapshotReuseSummary {
    int phaseReuseDecisionCount = 0;
    int freshCurrentRowCount = 0;
    int reusedLastProvenRowCount = 0;
    int displacedByFreshEvidenceCount = 0;
    int blockedReuseCount = 0;
    int staleReuseBlockedCount = 0;
    int planMismatchBlockedCount = 0;
    int stageMismatchBlockedCount = 0;
    int frequencyMismatchBlockedCount = 0;
    int roleMismatchBlockedCount = 0;
    int noReuseCandidateCount = 0;
    bool phaseReuseLedgerBrainOwned = false;
    bool displayBehaviorChanged = false;
};

struct PhaseSnapshotPlanContextSummary {
    int phasePlanContextDecisionCount = 0;
    int productPlanKeyAvailableCount = 0;
    int productPlanKeyMissingCount = 0;
    int liveProductPlanContextCount = 0;
    int harnessPlanProbeCount = 0;
    int missingPlanContextCount = 0;
    int planContinuityKnownCount = 0;
    int planContinuityUnknownCount = 0;
    int livePlanMismatchCount = 0;
    int harnessPlanMismatchCount = 0;
    bool phasePlanContextBrainOwned = false;
    bool publishBehaviorChanged = false;
};

struct PhaseSnapshotStableKeyAuditSummary {
    int stableKeyAuditDecisionCount = 0;
    int stableKeyPresentCount = 0;
    int stableKeyMissingCount = 0;
    int fallbackDerivedKeyCount = 0;
    int syntheticKeyCount = 0;
    int legacyKeyCount = 0;
    int duplicatedKeyCount = 0;
    int changedAcrossReuseCount = 0;
    int unsafeSameKeyCount = 0;
    int keyLedgerLinkedDisplayCount = 0;
    int keyLedgerLinkedCapCount = 0;
    int keyLedgerLinkedPhaseReuseCount = 0;
    bool stableKeyAuditBrainOwned = false;
    bool displayBehaviorChanged = false;
};

struct PhaseSnapshotStableKeyConsumerDryRunSummary {
    int dryRunStableKeyConsumerDecisionCount = 0;
    int sourceOwnedKeyPresentCount = 0;
    int migrationReadyCount = 0;
    int dedupeGroupWouldChangeCount = 0;
    int duplicateSuppressionWouldChangeCount = 0;
    int completionIdentityWouldChangeCount = 0;
    int phaseReuseWouldChangeCount = 0;
    int rowOrderingWouldChangeCount = 0;
    int overlayCapWouldChangeCount = 0;
    int moreAtcWouldChangeCount = 0;
    int driftDetectedCount = 0;
    int safeForOptInCount = 0;
    int behaviorConsumerEnabledCount = 0;
    bool displayBehaviorChanged = false;
};

struct PhaseSnapshotSourceOwnedFallbackStableKeyShadowSummary {
    int shadowDecisionCount = 0;
    int shadowGateEnabledCount = 0;
    int shadowRecomputeAttemptedCount = 0;
    int shadowRecomputeSkippedCount = 0;
    int shadowHashMismatchCount = 0;
    int shadowRowOrderingMismatchCount = 0;
    int shadowDedupeMismatchCount = 0;
    int shadowDuplicateSuppressionMismatchCount = 0;
    int shadowCompletionIdentityMismatchCount = 0;
    int shadowPhaseReuseMismatchCount = 0;
    int shadowOverlayCapMismatchCount = 0;
    int shadowMoreAtcMismatchCount = 0;
    int shadowMissingPlanBlockedCount = 0;
    int shadowDriftDetectedCount = 0;
    int shadowSafeForFutureLiveOptInCount = 0;
    int shadowBehaviorConsumerEnabledCount = 0;
    bool behaviorChanged = false;
};

struct PhaseSnapshotSourceOwnedFallbackStableKeyLiveConsumptionReadinessSummary {
    int readinessDecisionCount = 0;
    int proposalGateArmedCount = 0;
    int shadowParityCleanCount = 0;
    int planContextAvailableCount = 0;
    int missingPlanBlockedCount = 0;
    int driftBlockedCount = 0;
    int shadowNotAttemptedBlockedCount = 0;
    int readinessBlockedCount = 0;
    int readyForFutureLiveConsumptionCount = 0;
    int liveConsumptionBehaviorEnabledCount = 0;
    int shadowBehaviorConsumerEnabledCount = 0;
    bool behaviorChanged = false;
};

struct PhaseSnapshotSourceOwnedFallbackStableKeyLiveConsumptionSummary {
    int liveConsumptionDecisionCount = 0;
    int liveConsumptionGateArmedCount = 0;
    int liveConsumptionAllowedCount = 0;
    int liveConsumptionBlockedCount = 0;
    int sourceOwnedConsumedCount = 0;
    int generatedFallbackConsumedCount = 0;
    int missingPlanBlockedCount = 0;
    int shadowGateOffBlockedCount = 0;
    int shadowParityNotAttemptedBlockedCount = 0;
    int shadowDriftBlockedCount = 0;
    int hashMismatchBlockedCount = 0;
    int rowOrderingMismatchBlockedCount = 0;
    int dedupeMismatchBlockedCount = 0;
    int duplicateSuppressionMismatchBlockedCount = 0;
    int completionIdentityMismatchBlockedCount = 0;
    int phaseReuseMismatchBlockedCount = 0;
    int overlayCapMismatchBlockedCount = 0;
    int moreAtcMismatchBlockedCount = 0;
    int missingSourceOwnedKeyBlockedCount = 0;
    int migrationNotReadyBlockedCount = 0;
    int defaultModeProtectedCount = 0;
    bool behaviorChanged = false;
};

struct PhaseSnapshotPublishResult {
    FinalDisplaySnapshot snapshot;
    bool storedNewProven = false;
    bool usedLastProven = false;
    bool verificationPending = false;
    std::string statusLine;
    std::vector<PhaseSnapshotReuseDecisionRecord> phaseReuseDecisions;
    PhaseSnapshotReuseSummary phaseReuseSummary;
    PhaseSnapshotPlanContextSummary phasePlanContextSummary;
    PhaseSnapshotStableKeyAuditSummary phaseStableKeyAuditSummary;
    PhaseSnapshotStableKeyConsumerDryRunSummary
        phaseStableKeyConsumerDryRunSummary;
    PhaseSnapshotSourceOwnedFallbackStableKeyShadowSummary
        phaseSourceOwnedFallbackStableKeyShadowSummary;
    PhaseSnapshotSourceOwnedFallbackStableKeyLiveConsumptionReadinessSummary
        phaseSourceOwnedFallbackStableKeyLiveConsumptionReadinessSummary;
    PhaseSnapshotSourceOwnedFallbackStableKeyLiveConsumptionSummary
        phaseSourceOwnedFallbackStableKeyLiveConsumptionSummary;
};

PhaseSnapshotPublishResult PublishPhaseSnapshot(
    PhaseSnapshotPublisherState* state,
    const PhaseSnapshotPublishRequest& request);

std::string PhaseSnapshotPublisherStateSummary(
    const PhaseSnapshotPublisherState& state);

}  // namespace xvatsim::brain
