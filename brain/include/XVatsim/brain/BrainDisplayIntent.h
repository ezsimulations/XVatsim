#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::brain {

struct BrainDisplayRelationFact {
    std::string callsign;
    std::string frequency;
    DisplayRelation displayRelation = DisplayRelation::Unknown;
    bool hasRouteEntryDistance = false;
    double routeEntryDistanceNm = 0.0;
};

struct BrainDisplayIntentInput {
    WorkflowStage workflowStage = WorkflowStage::None;
    double routeProgressDistanceNm = 0.0;
    std::string currentPolygonKey;
    std::string nextPolygonKey;
    std::string arrivalPolygonKey;
    bool sourceOwnedFallbackStableKeyShadowEnabled = false;
    std::string sourceOwnedFallbackStableKeyShadowGateSource = "default";
    bool sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled = false;
    std::string sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource =
        "default";
    bool sourceOwnedFallbackStableKeyLiveConsumptionEnabled = false;
    std::string sourceOwnedFallbackStableKeyLiveConsumptionGateSource =
        "default";
    RadioStateSnapshot radios;
    ModuleBoardSnapshot departureBoard;
    ModuleBoardSnapshot arrivalBoard;
    ModuleBoardSnapshot enrouteBoard;
    std::vector<BrainDisplayRelationFact> relationFacts;
};

struct BrainDisplayDecisionRecord {
    std::string decisionId;
    std::string completionStableKey;
    std::string sourceDecisionId;
    std::string sourceEvidenceId;
    std::string sourceEvidenceType;
    std::string sourceEvidenceDomain;
    bool sourceEvidenceLinked = false;
    std::string sourceEvidenceLinkStatus;
    std::string sourceEvidenceMissingReason;
    bool sourceDecisionLinked = false;
    bool displayDecisionLinked = false;
    bool capDecisionLinked = false;
    std::string linkageConfidence;
    bool linkageFallbackUsed = false;
    std::string callsign;
    std::string frequency;
    StationRole role = StationRole::Other;
    BoardSource sourceBoard = BoardSource::None;
    WorkflowStage workflowStage = WorkflowStage::None;
    bool acceptedByRelevance = false;
    DisplayRelation acceptedRelation = DisplayRelation::Unknown;
    bool relationFactPresent = false;
    DisplayRelation relationFactValue = DisplayRelation::Unknown;
    bool fallbackRelationUsed = false;
    DisplayRelation fallbackRelationValue = DisplayRelation::Unknown;
    DisplayRelation finalRelation = DisplayRelation::Unknown;
    std::string stationPolygonKey;
    std::string currentPolygonKey;
    std::string nextPolygonKey;
    std::string arrivalPolygonKey;
    bool displayable = false;
    bool duplicateSuppressed = false;
    std::string duplicateKey;
    std::string duplicateKeptDecisionId;
    std::string duplicateDroppedDecisionId;
    bool stageSuppressed = false;
    bool displayedInFinalSnapshot = false;
    std::string decision;
    std::string reason;
    std::string confidenceLevel;
    // Diagnostic-only bounded scoring. Keep numeric scores normalized;
    // hardBlock is a separate category, not an oversized negative score.
    double positiveScore = 0.0;
    double negativeScore = 0.0;
    bool hardBlock = false;
    std::string scoreSummary;
    std::string failSoftRecommendation;
    std::string failSoftReason;
    bool currentHideButFailSoftWouldShowOrWarn = false;
};

struct BrainDisplayDecisionSummary {
    int acceptedCompletionCount = 0;
    int displayDecisionCount = 0;
    int displayedFinalCount = 0;
    int hiddenAfterAcceptCount = 0;
    int filteredAfterAcceptCount = 0;
    int duplicateSuppressedCount = 0;
    int stageSuppressedCount = 0;
    int missingDecisionCount = 0;
};

struct BrainDisplayFailSoftPreviewSummary {
    int failSoftPreviewCount = 0;
    int recommendKeepDisplayCount = 0;
    int recommendKeepHideCount = 0;
    int recommendDisplayWithWarningCount = 0;
    int recommendStageDeferCount = 0;
    int recommendLowerPriorityDisplayCount = 0;
    int recommendHardBlockHideCount = 0;
    int recommendNeedsMoreEvidenceCount = 0;
    int currentHideButFailSoftWouldShowOrWarnCount = 0;
};

struct BrainDisplayOverlayCapDecisionRecord {
    std::string overlayCapDecisionId;
    std::string sourceDecisionId;
    std::string sourceEvidenceId;
    std::string sourceEvidenceType;
    std::string sourceEvidenceDomain;
    bool sourceEvidenceLinked = false;
    std::string sourceEvidenceLinkStatus;
    std::string sourceEvidenceMissingReason;
    bool sourceDecisionLinked = false;
    bool displayDecisionLinked = false;
    bool capDecisionLinked = false;
    std::string linkageConfidence;
    bool linkageFallbackUsed = false;
    std::string displayDecisionId;
    std::string subjectKey;
    std::string callsign;
    StationRole role = StationRole::Other;
    std::string frequency;
    std::string endpoint;
    std::string airportIcao;
    DisplayRelation displayRelation = DisplayRelation::Unknown;
    WorkflowStage workflowStage = WorkflowStage::None;
    int boardIndexBeforeCap = -1;
    int boardIndexAfterCap = -1;
    int capLimit = 0;
    bool visibleBeforeCap = false;
    bool visibleAfterCap = false;
    bool cappedByOverlayLimit = false;
    std::string capReason;
    bool contributesToMoreAtcCount = false;
    int moreAtcCountBeforeRow = 0;
    int moreAtcCountAfterRow = 0;
    int retainedVisibleRowCount = 0;
    int cappedHiddenRowCount = 0;
    std::string finalDisplayOutcome;
    std::string confidenceLevel;
    bool fallbackUsed = false;
    bool hardBlock = false;
    std::string hardBlockReason;
    std::string stableCompletionKey;
};

struct BrainDisplayOverlayCapSummary {
    int overlayCapDecisionCount = 0;
    int capLimit = 0;
    int candidateBeforeCapCount = 0;
    int visibleAfterCapCount = 0;
    int cappedHiddenCount = 0;
    int moreAtcCount = 0;
    int contributesToMoreAtcCount = 0;
    int nonCappedHiddenCount = 0;
    int duplicateHiddenCount = 0;
    int stageDeferredHiddenCount = 0;
    bool capLedgerBrainOwned = false;
    bool overlayCapBehaviorChanged = false;
};

struct BrainDisplaySourceLinkSummary {
    int displaySourceLinkDecisionCount = 0;
    int displaySourceLinkedCount = 0;
    int displaySourceMissingCount = 0;
    int capSourceLinkedCount = 0;
    int capSourceMissingCount = 0;
    int syntheticRowCount = 0;
    int legacyRowCount = 0;
    int unknownSourceLinkCount = 0;
    bool sourceLinkageBrainOwned = false;
    bool displayBehaviorChanged = false;
};

struct BrainDisplayStableKeyAuditRecord {
    std::string stableKeyAuditDecisionId;
    std::string displayDecisionId;
    std::string overlayCapDecisionId;
    std::string phaseReuseDecisionId;
    std::string sourceEvidenceId;
    std::string sourceDecisionId;
    std::string subjectKey;
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

struct BrainDisplayStableKeyAuditSummary {
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

struct BrainDisplaySourceOwnedStableKeySummary {
    int sourceOwnedStableKeyDecisionCount = 0;
    int sourceOwnedStableKeyPresentCount = 0;
    int generatedFallbackKeyPresentCount = 0;
    int sourceOwnedMatchesFallbackCount = 0;
    int sourceOwnedMismatchCount = 0;
    int planContextAvailableCount = 0;
    int planContextMissingCount = 0;
    int migrationReadyCount = 0;
    int behaviorConsumerEnabledCount = 0;
    bool behaviorChanged = false;
};

struct BrainDisplayStableKeyConsumerDryRunRecord {
    std::string dryRunStableKeyConsumerDecisionId;
    std::string subjectKey;
    std::string callsign;
    StationRole role = StationRole::Other;
    std::string frequency;
    std::string endpoint;
    std::string airportIcao;
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
};

struct BrainDisplayStableKeyConsumerDryRunSummary {
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

struct BrainDisplaySourceOwnedFallbackStableKeyShadowRecord {
    std::string shadowDecisionId;
    std::string dryRunStableKeyConsumerDecisionId;
    std::string subjectKey;
    std::string callsign;
    StationRole role = StationRole::Other;
    std::string frequency;
    std::string endpoint;
    std::string airportIcao;
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
};

struct BrainDisplaySourceOwnedFallbackStableKeyShadowSummary {
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

struct BrainDisplaySourceOwnedFallbackStableKeyLiveConsumptionReadinessRecord {
    std::string readinessDecisionId;
    std::string shadowDecisionId;
    std::string dryRunStableKeyConsumerDecisionId;
    std::string subjectKey;
    std::string callsign;
    StationRole role = StationRole::Other;
    std::string frequency;
    std::string endpoint;
    std::string airportIcao;
    bool proposalGateArmed = false;
    std::string proposalGateSource = "default";
    bool shadowGateEnabled = false;
    bool shadowRecomputeAttempted = false;
    bool shadowParityClean = false;
    bool planContextAvailable = false;
    bool shadowDriftDetected = false;
    std::string blockedReason;
    bool readyForFutureLiveConsumption = false;
    bool liveConsumptionBehaviorEnabled = false;
    bool shadowBehaviorConsumerEnabled = false;
};

struct BrainDisplaySourceOwnedFallbackStableKeyLiveConsumptionReadinessSummary {
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

struct BrainDisplaySourceOwnedFallbackStableKeyLiveConsumptionRecord {
    std::string liveConsumptionDecisionId;
    std::string subjectKey;
    std::string callsign;
    StationRole role = StationRole::Other;
    std::string frequency;
    std::string endpoint;
    std::string airportIcao;
    std::string generatedFallbackStableCompletionKey;
    std::string sourceOwnedStableCompletionKey;
    bool sourceOwnedKeyPresent = false;
    bool sourceOwnedKeyMigrationReady = false;
    bool planContextAvailable = false;
    bool shadowGateEnabled = false;
    bool shadowRecomputeAttempted = false;
    bool shadowParityClean = false;
    bool shadowDriftDetected = false;
    bool shadowFinalBoardHashMatches = false;
    bool shadowRowOrderingMatches = false;
    bool shadowDedupeGroupsMatch = false;
    bool shadowDuplicateSuppressionMatches = false;
    bool shadowCompletionIdentityMatches = false;
    bool shadowPhaseReuseMatches = false;
    bool shadowOverlayCapMatches = false;
    bool shadowMoreAtcMatches = false;
    bool proposalGateArmed = false;
    bool liveConsumptionGateArmed = false;
    std::string liveConsumptionGateSource = "default";
    bool liveConsumptionAllowed = false;
    std::string liveConsumptionBlockedReason;
    std::string consumedKeyType = "generated-fallback";
    bool behaviorChanged = false;
    bool defaultModeProtected = true;
};

struct BrainDisplaySourceOwnedFallbackStableKeyLiveConsumptionSummary {
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

struct BrainDisplayUpstreamStableKeySourceAuditRecord {
    std::string upstreamStableKeyAuditId;
    std::string sourceClass;
    std::string producerName;
    bool producesDisplayRows = false;
    bool producesCompletionRows = false;
    bool producesEvidenceRows = false;
    bool stableKeyProvided = false;
    std::string stableKeyFieldName;
    std::string stableKeySource;
    bool fallbackKeyUsedDownstream = false;
    bool missingKeyRisk = false;
    bool duplicateKeyRisk = false;
    bool reuseContinuityRisk = false;
    bool dedupeRisk = false;
    std::string recommendedStableKeyOwner;
    std::string recommendedStableKeyShape;
    std::string migrationPriority;
    std::string migrationBlockedReason;
    bool behaviorChangeRequiredForMigration = false;
};

struct BrainDisplayUpstreamStableKeySourceAuditSummary {
    int upstreamStableKeyAuditCount = 0;
    int sourceOwnedKeyCount = 0;
    int evidenceIdKeyCount = 0;
    int decisionIdKeyCount = 0;
    int fallbackKeySourceCount = 0;
    int syntheticKeySourceCount = 0;
    int legacyKeySourceCount = 0;
    int missingKeySourceCount = 0;
    int unknownKeySourceCount = 0;
    int highPriorityMigrationCount = 0;
    int mediumPriorityMigrationCount = 0;
    int lowPriorityMigrationCount = 0;
    int dedupeRiskCount = 0;
    int reuseContinuityRiskCount = 0;
    int migrationRequiresBehaviorChangeCount = 0;
    bool stableKeySourceAuditBrainOwned = false;
};

struct BrainDisplayIntentOutput {
    ModuleBoardSnapshot departureBoard;
    ModuleBoardSnapshot arrivalBoard;
    ModuleBoardSnapshot enrouteBoard;
    FinalDisplaySnapshot finalDisplay;
    std::vector<std::string> diagnostics;
    std::string reason;
    std::uint64_t stableHash = 0;
    int displayed = 0;
    int hidden = 0;
    int filtered = 0;
    std::vector<BrainDisplayDecisionRecord> displayDecisions;
    BrainDisplayDecisionSummary displayDecisionSummary;
    BrainDisplayFailSoftPreviewSummary failSoftPreviewSummary;
    std::vector<BrainDisplayOverlayCapDecisionRecord> overlayCapDecisions;
    BrainDisplayOverlayCapSummary overlayCapSummary;
    BrainDisplaySourceLinkSummary sourceLinkSummary;
    std::vector<BrainDisplayStableKeyAuditRecord> stableKeyAuditDecisions;
    BrainDisplayStableKeyAuditSummary stableKeyAuditSummary;
    BrainDisplaySourceOwnedStableKeySummary sourceOwnedStableKeySummary;
    std::vector<BrainDisplayStableKeyConsumerDryRunRecord>
        stableKeyConsumerDryRunDecisions;
    BrainDisplayStableKeyConsumerDryRunSummary stableKeyConsumerDryRunSummary;
    std::vector<BrainDisplaySourceOwnedFallbackStableKeyShadowRecord>
        sourceOwnedFallbackStableKeyShadowDecisions;
    BrainDisplaySourceOwnedFallbackStableKeyShadowSummary
        sourceOwnedFallbackStableKeyShadowSummary;
    std::vector<
        BrainDisplaySourceOwnedFallbackStableKeyLiveConsumptionReadinessRecord>
        sourceOwnedFallbackStableKeyLiveConsumptionReadinessDecisions;
    BrainDisplaySourceOwnedFallbackStableKeyLiveConsumptionReadinessSummary
        sourceOwnedFallbackStableKeyLiveConsumptionReadinessSummary;
    std::vector<BrainDisplaySourceOwnedFallbackStableKeyLiveConsumptionRecord>
        sourceOwnedFallbackStableKeyLiveConsumptionDecisions;
    BrainDisplaySourceOwnedFallbackStableKeyLiveConsumptionSummary
        sourceOwnedFallbackStableKeyLiveConsumptionSummary;
    std::vector<BrainDisplayUpstreamStableKeySourceAuditRecord>
        upstreamStableKeySourceAuditDecisions;
    BrainDisplayUpstreamStableKeySourceAuditSummary
        upstreamStableKeySourceAuditSummary;
};

const char* ToString(DisplayRelation relation);

BrainDisplayIntentOutput RunBrainDisplayIntentWorker(
    const BrainDisplayIntentInput& input);

}  // namespace xvatsim::brain
