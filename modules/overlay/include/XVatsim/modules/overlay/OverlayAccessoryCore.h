#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "XVatsim/brain/BrainOwnedRuntime.h"

namespace xvatsim::modules::overlay {

struct AccessoryTextMeasurementContext;

enum class AccessoryFontRole {
    DrawerBody,
    DrawerEntryTitle,
    OrbLabel,
    MetarOrbDetail,
    OrbOpenIndicator,
};

struct AccessoryTextMeasurementInput {
    std::string text;
    AccessoryFontRole role = AccessoryFontRole::DrawerBody;
    float scale = 1.0f;
};

struct AccessoryTextMeasurementResult {
    brain::BrainOwnedAccessoryOperationStatus status =
        brain::BrainOwnedAccessoryOperationStatus::Unavailable;
    int measuredWidth = 0;
    int measuredHeight = 0;
    std::string fontFamily;
    float fontPixelSize = 0.0f;
    bool bold = false;
};

struct AccessoryGdiMeasurementCounters {
    std::uint64_t measurementCalls = 0;
    std::uint64_t bitmapConstructions = 0;
    std::uint64_t graphicsConstructions = 0;
    std::uint64_t fontConstructions = 0;
};

struct AccessoryTextExtent {
    int width = 0;
    int height = 0;
};

struct AccessoryTypographyMetrics {
    brain::BrainOwnedAccessoryOperationStatus status =
        brain::BrainOwnedAccessoryOperationStatus::Unavailable;
    float scale = 1.0f;
    std::uint64_t generation = 0;
    std::array<AccessoryTextExtent, 3> orbLabels{};
    AccessoryTextExtent openIndicator;
    AccessoryTextExtent drawerTitleLine;
    AccessoryTextExtent drawerBodyLine;
};

struct AccessoryRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct AccessoryPoint {
    int x = 0;
    int y = 0;
};

struct AccessoryOrbLayout {
    brain::BrainOwnedAccessoryDrawerId drawer =
        brain::BrainOwnedAccessoryDrawerId::None;
    AccessoryRect designBounds;
    AccessoryPoint designCenter;
    AccessoryRect bounds;
    int diameterPhysicalPixels = 0;
    int labelMeasuredWidth = 0;
    int labelMeasuredHeight = 0;
    int labelAvailableWidth = 0;
    int labelAvailableHeight = 0;
    int openIndicatorMeasuredWidth = 0;
    int openIndicatorMeasuredHeight = 0;
    int openIndicatorAvailableWidth = 0;
    int openIndicatorAvailableHeight = 0;
    bool visible = false;
    bool interactive = false;
    bool labelFits = false;
    bool openIndicatorFits = false;
};

enum class AccessoryAnchorOperation {
    Initialize,
    OrdinaryRefresh,
    OpenDrawer,
    CloseDrawer,
    IntentionalMove,
    ScreenBoundsChanged,
};

struct AccessoryLayoutInput {
    int screenWidth = 1920;
    int screenHeight = 1080;
    int windowLeft = 0;
    int windowTop = 1080;
    float scale = 1.0f;
    float cardAnimationProgress = 1.0f;
    bool drawerOpen = false;
    AccessoryAnchorOperation anchorOperation =
        AccessoryAnchorOperation::OrdinaryRefresh;
    bool savedClosedAnchorValid = false;
    int savedClosedAnchorLeft = 0;
    int savedClosedAnchorTop = 1080;
    const AccessoryTypographyMetrics* typography = nullptr;
};

struct AccessoryLayoutResult {
    brain::BrainOwnedAccessoryOperationStatus status =
        brain::BrainOwnedAccessoryOperationStatus::Unavailable;
    float scale = 1.0f;
    int closedWidth = 0;
    int closedHeight = 0;
    int openWidth = 0;
    int openHeight = 0;
    int closedDesignWidth = 0;
    int closedDesignHeight = 0;
    int openDesignWidth = 0;
    int openDesignHeight = 0;
    int orbDiameterDesignPixels = 0;
    AccessoryRect railDesignBounds;
    AccessoryRect drawerDesignBounds;
    int mainCardToRailGapDesignPixels = 0;
    int interOrbGapDesignPixels = 0;
    int railToDrawerGapDesignPixels = 0;
    int openBottomMarginDesignPixels = 0;
    int resolvedLeft = 0;
    int resolvedTop = 0;
    int restorableClosedAnchorLeft = 0;
    int restorableClosedAnchorTop = 0;
    AccessoryRect closedBounds;
    AccessoryRect openBounds;
    AccessoryRect resolvedBounds;
    bool allPhysicalBoundsInsideScreen = false;
    bool temporaryClampApplied = false;
    bool intentionalMoveApplied = false;
    bool closedDimensionsRestored = false;
    bool closedAnchorRestored = false;
    bool positionSettingsDirty = false;
    bool accessoriesVisible = false;
    bool accessoriesInteractive = false;
    std::vector<AccessoryOrbLayout> orbs;
    AccessoryRect mainCardBounds;
    AccessoryRect railBounds;
    AccessoryRect drawerBounds;
    int drawerContentInset = 0;
    int drawerHeaderTop = 0;
    int drawerHeaderHeight = 0;
    int drawerContentTop = 0;
    int drawerContentBottom = 0;
    int drawerLineHeight = 0;
    int drawerVisibleLineCapacity = 0;
};

struct AccessoryAnchorState {
    bool initialized = false;
    bool drawerOpen = false;
    bool currentAnchorValid = false;
    int currentAnchorLeft = 0;
    int currentAnchorTop = 1080;
    bool savedClosedAnchorValid = false;
    int savedClosedAnchorLeft = 0;
    int savedClosedAnchorTop = 1080;
    bool temporaryOpenClampApplied = false;
    bool intentionalMoveSinceOpen = false;
    std::uint64_t transitionGeneration = 0;
};

struct AccessoryAnchorUpdateInput {
    AccessoryAnchorOperation operation =
        AccessoryAnchorOperation::OrdinaryRefresh;
    AccessoryLayoutInput layout;
};

struct AccessoryAnchorUpdateResult {
    brain::BrainOwnedAccessoryOperationStatus status =
        brain::BrainOwnedAccessoryOperationStatus::Unavailable;
    AccessoryLayoutResult layout;
    bool savedClosedAnchorCaptured = false;
    bool explicitClosedAnchorRestored = false;
    bool intentionalMoveApplied = false;
    bool positionSettingsDirty = false;
    bool temporaryOpenClampApplied = false;
};

struct AccessoryClickFact {
    brain::BrainOwnedAccessoryDrawerId drawer =
        brain::BrainOwnedAccessoryDrawerId::None;
    std::uint64_t requestSequence = 0;
    std::uint64_t startedMicroseconds = 0;
    std::uint64_t dispatchStartedMicroseconds = 0;
    std::array<std::uint64_t, 5> dispatchStageWallMicroseconds{};
};

class AccessoryClickFactQueue {
public:
    static constexpr std::size_t kCapacity = 8;

    bool Produce(
        brain::BrainOwnedAccessoryDrawerId drawer,
        std::uint64_t startedMicroseconds,
        AccessoryClickFact* outFact = nullptr);
    bool Consume(AccessoryClickFact* outFact);
    std::size_t DiscardPending();
    std::size_t PendingCount() const;
    std::uint64_t NextSequence() const;
    std::uint64_t ProducedCount() const;
    std::uint64_t DroppedCount() const;
    std::uint64_t ConsumedCount() const;
    std::uint64_t DiscardedCount() const;
    std::size_t MaximumDepth() const;

private:
    std::array<AccessoryClickFact, kCapacity> facts_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    std::uint64_t nextSequence_ = 1;
    std::uint64_t producedCount_ = 0;
    std::uint64_t droppedCount_ = 0;
    std::uint64_t consumedCount_ = 0;
    std::uint64_t discardedCount_ = 0;
    std::size_t maximumDepth_ = 0;
};

struct AccessoryInputDispatchSnapshot {
    bool inFlight = false;
    bool presentationBound = false;
    std::uint64_t inFlightRequestSequence = 0;
    std::uint64_t expectedSelectionGeneration = 0;
    std::uint64_t expectedRenderGeneration = 0;
    brain::BrainOwnedAccessoryDrawerId expectedDrawer =
        brain::BrainOwnedAccessoryDrawerId::None;
    std::uint64_t dispatchNotifications = 0;
    std::uint64_t beginAttempts = 0;
    std::uint64_t requestsBegun = 0;
    std::uint64_t blockedWhileInFlight = 0;
    std::uint64_t presentationsBound = 0;
    std::uint64_t matchingDrawCompletions = 0;
    std::uint64_t exactMatchCompletions = 0;
    std::uint64_t supersededGenerationCompletions = 0;
    std::uint64_t explicitCancellations = 0;
    std::uint64_t selectionSupersededCancellations = 0;
    std::uint64_t mismatchedDrawAttempts = 0;
    std::uint64_t invalidatedInFlight = 0;
    std::uint64_t maximumInFlightMicroseconds = 0;
};

enum class AccessoryInputDispatchDisposition {
    None,
    ExactMatch,
    CompatibleRenderSuperseded,
    SelectionSuperseded,
    ExplicitCancellation,
};

const char* AccessoryInputDispatchDispositionToken(
    AccessoryInputDispatchDisposition disposition);

struct AccessoryInputDispatchCompletion {
    bool terminal = false;
    bool completed = false;
    bool cancelled = false;
    AccessoryInputDispatchDisposition disposition =
        AccessoryInputDispatchDisposition::None;
    AccessoryClickFact fact;
    brain::BrainOwnedAccessoryDrawerAction action =
        brain::BrainOwnedAccessoryDrawerAction::None;
};

class AccessoryInputDispatchCoordinator {
public:
    void RecordDispatchNotification();
    bool TryBegin(
        AccessoryClickFactQueue* queue,
        AccessoryClickFact* outFact);
    bool BindPresentation(
        std::uint64_t requestSequence,
        brain::BrainOwnedAccessoryDrawerAction action,
        std::uint64_t selectionGeneration,
        std::uint64_t renderGeneration);
    AccessoryInputDispatchCompletion CompleteMatchingDraw(
        brain::BrainOwnedAccessoryDrawerId selectedDrawer,
        std::uint64_t selectionGeneration,
        std::uint64_t renderGeneration,
        std::uint64_t completedMicroseconds = 0);
    bool CancelInFlight(
        std::uint64_t requestSequence,
        std::uint64_t completedMicroseconds = 0);
    bool InvalidateInFlight(std::uint64_t completedMicroseconds = 0);
    AccessoryInputDispatchSnapshot Snapshot() const;

private:
    void ReleaseInFlight(std::uint64_t completedMicroseconds);
    AccessoryInputDispatchSnapshot snapshot_{};
    AccessoryClickFact inFlightFact_{};
    brain::BrainOwnedAccessoryDrawerAction inFlightAction_ =
        brain::BrainOwnedAccessoryDrawerAction::None;
};

class AccessoryPublicationFactQueue {
public:
    static constexpr std::size_t kCapacity = 256;
    bool Produce(const brain::BrainOwnedAccessoryPublicationFact& fact);
    bool Consume(brain::BrainOwnedAccessoryPublicationFact* fact);
    std::size_t PendingCount() const;
    std::size_t AvailableCapacity() const;
    std::uint64_t ProducedCount() const;
    std::uint64_t ConsumedCount() const;
    std::uint64_t RejectedCount() const;

private:
    std::array<brain::BrainOwnedAccessoryPublicationFact, kCapacity> facts_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    std::uint64_t lastProducedCommandIdentity_ = 0;
    std::uint64_t lastProducedLifecycleEpoch_ = 0;
    std::uint64_t producedCount_ = 0;
    std::uint64_t consumedCount_ = 0;
    std::uint64_t rejectedCount_ = 0;
};

enum class AccessoryPerformanceCategory : std::size_t {
    RailRasterization = 0,
    DrawerRasterization,
    RailTextureUpload,
    DrawerTextureUpload,
    CompletedAccessoryDraw,
    OpenAction,
    AtomicSwitchAction,
    CloseAction,
    EffectiveScrollAction,
    BrainPresentationDispatchWall,
    MatchingActionDrawWall,
    CombinedActionWall,
    CallbackWait,
    PreparationWait,
    ContainingFrameInterval,
    Count,
};

enum class AccessoryActionTimingClassification : std::size_t {
    SynchronousWallWithinBudget = 0,
    FrameCadenceLimited,
    PreparationLimited,
    SynchronousWallFailure,
    RenderWallFailure,
    LivenessFailure,
    TimingUnavailable,
};

enum class AccessoryDispatchStage : std::size_t {
    BrainDecision = 0,
    BrainProjectionHistoryCopy,
    OverlayLayoutPresentation,
    GdiMeasurementWrapping,
    GenerationBinding,
    Count,
};

struct AccessoryDispatchStageWallTimings {
    std::array<std::uint64_t,
        static_cast<std::size_t>(AccessoryDispatchStage::Count)>
        elapsedMicroseconds{};
};

struct AccessoryActionDispatchTimingInput {
    std::uint64_t dispatchCompletedMicroseconds = 0;
    std::uint64_t dispatchStartedMicroseconds = 0;
    std::uint64_t preparationWaitMicroseconds = 0;
    std::uint64_t preparationRequestedMicroseconds = 0;
    std::uint64_t workerStartedMicroseconds = 0;
    std::uint64_t workerCompletedMicroseconds = 0;
    std::uint64_t workerPublishedMicroseconds = 0;
    std::uint64_t readyCollectedMicroseconds = 0;
    std::uint64_t bindingStartedMicroseconds = 0;
    std::uint64_t workerQueueWaitMicroseconds = 0;
    std::uint64_t workerPreparationMicroseconds = 0;
    std::uint64_t workerPublicationHandoffMicroseconds = 0;
    std::uint64_t publicationToReadyCollectionMicroseconds = 0;
    std::uint64_t readyToBindWaitMicroseconds = 0;
    std::uint64_t preparationUnattributedMicroseconds = 0;
    AccessoryDispatchStageWallTimings stages{};
};

struct AccessoryActionDrawTimingInput {
    std::uint64_t actionDrawWallMicroseconds = 0;
    bool renderWallFailure = false;
    AccessoryPerformanceCategory renderWallFailureCategory =
        AccessoryPerformanceCategory::CompletedAccessoryDraw;
    std::uint64_t renderWallFailureMicroseconds = 0;
};

enum class AccessoryRasterReason : std::size_t {
    Initial = 0,
    Selection,
    EffectiveScaleOrResize,
    TypographyOrLayout,
    ContentGeneration,
    PresentationScroll,
    Count,
};

struct AccessoryActionTimingRecord {
    bool available = false;
    AccessoryPerformanceCategory actionCategory =
        AccessoryPerformanceCategory::OpenAction;
    AccessoryActionTimingClassification classification =
        AccessoryActionTimingClassification::TimingUnavailable;
    std::uint64_t requestSequence = 0;
    std::uint64_t mouseFactAcceptedMicroseconds = 0;
    std::uint64_t dispatchStartedMicroseconds = 0;
    std::uint64_t dispatchCompletedMicroseconds = 0;
    std::uint64_t precedingDrawEnteredMicroseconds = 0;
    std::uint64_t matchingDrawEnteredMicroseconds = 0;
    std::uint64_t matchingDrawCompletedMicroseconds = 0;
    std::uint64_t dispatchWallMicroseconds = 0;
    std::uint64_t queuedBeforeDispatchMicroseconds = 0;
    std::uint64_t callbackWaitMicroseconds = 0;
    std::uint64_t actionDrawWallMicroseconds = 0;
    std::uint64_t matchingDrawCallbackElapsedMicroseconds = 0;
    std::uint64_t combinedActionWallMicroseconds = 0;
    std::uint64_t preparationWaitMicroseconds = 0;
    std::uint64_t preparationRequestedMicroseconds = 0;
    std::uint64_t workerStartedMicroseconds = 0;
    std::uint64_t workerCompletedMicroseconds = 0;
    std::uint64_t workerPublishedMicroseconds = 0;
    std::uint64_t readyCollectedMicroseconds = 0;
    std::uint64_t bindingStartedMicroseconds = 0;
    std::uint64_t workerQueueWaitMicroseconds = 0;
    std::uint64_t workerPreparationMicroseconds = 0;
    std::uint64_t workerPublicationHandoffMicroseconds = 0;
    std::uint64_t publicationToReadyCollectionMicroseconds = 0;
    std::uint64_t readyToBindWaitMicroseconds = 0;
    std::uint64_t preparationUnattributedMicroseconds = 0;
    std::uint64_t totalUnattributedMicroseconds = 0;
    std::uint64_t totalOverlapMicroseconds = 0;
    std::uint64_t drawSampleId = 0;
    bool sharedDrawSample = false;
    std::uint64_t drawSampleFanOut = 0;
    bool renderWallFailure = false;
    AccessoryPerformanceCategory renderWallFailureCategory =
        AccessoryPerformanceCategory::CompletedAccessoryDraw;
    std::uint64_t renderWallFailureMicroseconds = 0;
    AccessoryDispatchStageWallTimings stages{};
    std::uint64_t endToEndMicroseconds = 0;
    std::uint64_t containingFrameIntervalMicroseconds = 0;
    std::uint64_t expectedDrawOrdinal = 0;
    std::uint64_t matchingDrawOrdinal = 0;
    std::uint64_t missedEligibleDraws = 0;
    bool accountingExact = false;
};

struct AccessoryPerformanceSummary {
    std::uint64_t count = 0;
    std::uint64_t p50Microseconds = 0;
    std::uint64_t p95Microseconds = 0;
    std::uint64_t maximumMicroseconds = 0;
};

struct AccessoryPerformanceSnapshot {
    std::uint64_t epoch = 0;
    std::uint64_t measurementRevision = 0;
    bool thresholdFailure = false;
    std::uint64_t firstViolationMicroseconds = 0;
    AccessoryPerformanceCategory firstViolationCategory =
        AccessoryPerformanceCategory::RailRasterization;
    std::uint64_t warningCount = 0;
    std::uint64_t publicationCount = 0;
    std::uint64_t actionsCompleted = 0;
    std::uint64_t synchronousWallWithinBudgetCount = 0;
    std::uint64_t frameCadenceLimitedCount = 0;
    std::uint64_t preparationLimitedCount = 0;
    std::uint64_t synchronousWallFailureCount = 0;
    std::uint64_t renderWallFailureCount = 0;
    std::uint64_t livenessFailureCount = 0;
    std::uint64_t timingUnavailableCount = 0;
    std::uint64_t cadenceContractFailureCount = 0;
    std::uint64_t missedEligibleDraws = 0;
    std::uint64_t uniqueDrawSamples = 0;
    std::uint64_t drawSampleReferences = 0;
    std::uint64_t coalescedActionCount = 0;
    std::uint64_t maximumActionsPerDraw = 0;
    AccessoryActionTimingRecord lastAction{};
    static constexpr std::size_t kViolationRecordCapacity = 8;
    std::array<AccessoryActionTimingRecord, kViolationRecordCapacity>
        firstViolationRecords{};
    std::uint64_t firstViolationRecordCount = 0;
    std::uint64_t droppedViolationRecordCount = 0;
    std::array<std::uint64_t,
        static_cast<std::size_t>(AccessoryRasterReason::Count)>
        railRasterReasons{};
    std::array<std::uint64_t,
        static_cast<std::size_t>(AccessoryRasterReason::Count)>
        drawerRasterReasons{};
    std::array<AccessoryPerformanceSummary,
        static_cast<std::size_t>(AccessoryPerformanceCategory::Count)> categories{};
    std::array<std::array<AccessoryPerformanceSummary,
        static_cast<std::size_t>(AccessoryDispatchStage::Count)>, 3>
        actionStageWall{};
};

class AccessoryPerformanceCollector {
public:
    static constexpr std::uint64_t kThresholdMicroseconds = 16700;
    static constexpr std::size_t kHistogramMaximumMicroseconds = 65534;
    static constexpr std::size_t kHistogramBucketCount = 65536;
    static constexpr std::size_t kPendingActionCapacity = 8;
    static constexpr std::size_t kViolationRecordCapacity =
        AccessoryPerformanceSnapshot::kViolationRecordCapacity;

    AccessoryPerformanceCollector();
    void ResetForNewProcess(std::uint64_t epoch);
    void Record(
        AccessoryPerformanceCategory category,
        std::uint64_t elapsedMicroseconds);
    void RecordRasterReason(bool drawer, AccessoryRasterReason reason);
    bool BeginDrawerAction(
        brain::BrainOwnedAccessoryDrawerAction action,
        std::uint64_t requestSequence,
        std::uint64_t startedMicroseconds,
        std::uint64_t expectedSelectionGeneration = 0,
        std::uint64_t expectedRenderGeneration = 0,
        std::uint64_t dispatchCompletedMicroseconds = 0,
        std::uint64_t expectedDrawOrdinal = 0,
        const AccessoryActionDispatchTimingInput& timing = {});
    bool BeginEffectiveScroll(
        std::uint64_t startedMicroseconds,
        std::uint64_t expectedSelectionGeneration = 0,
        std::uint64_t expectedRenderGeneration = 0,
        std::uint64_t dispatchCompletedMicroseconds = 0,
        std::uint64_t expectedDrawOrdinal = 0,
        const AccessoryActionDispatchTimingInput& timing = {});
    std::size_t CompletePendingActions(
        std::uint64_t completedMicroseconds,
        std::uint64_t completedSelectionGeneration = 0,
        std::uint64_t completedRenderGeneration = 0,
        std::uint64_t matchingDrawEnteredMicroseconds = 0,
        std::uint64_t precedingDrawEnteredMicroseconds = 0,
        std::uint64_t matchingDrawOrdinal = 0,
        const AccessoryActionDrawTimingInput& drawTiming = {});
    void DiscardPendingActions();
    bool HasPendingActions() const;
    bool HasMatchingPendingAction(
        std::uint64_t selectionGeneration,
        std::uint64_t renderGeneration,
        std::uint64_t drawOrdinal) const;
    bool ConsumeFirstViolationWarning(
        AccessoryPerformanceCategory* outCategory,
        std::uint64_t* outElapsedMicroseconds);
    bool BeginAggregatePublication(AccessoryPerformanceSnapshot* outSnapshot);
    AccessoryPerformanceSnapshot Snapshot() const;

private:
    struct Histogram {
        std::array<std::uint64_t, kHistogramBucketCount> buckets{};
        std::uint64_t count = 0;
        std::uint64_t maximumMicroseconds = 0;
    };
    struct PendingAction {
        AccessoryPerformanceCategory category =
            AccessoryPerformanceCategory::OpenAction;
        std::uint64_t requestSequence = 0;
        std::uint64_t startedMicroseconds = 0;
        std::uint64_t expectedSelectionGeneration = 0;
        std::uint64_t expectedRenderGeneration = 0;
        std::uint64_t dispatchCompletedMicroseconds = 0;
        std::uint64_t expectedDrawOrdinal = 0;
        std::uint64_t dispatchStartedMicroseconds = 0;
        std::uint64_t preparationWaitMicroseconds = 0;
        std::uint64_t preparationRequestedMicroseconds = 0;
        std::uint64_t workerStartedMicroseconds = 0;
        std::uint64_t workerCompletedMicroseconds = 0;
        std::uint64_t workerPublishedMicroseconds = 0;
        std::uint64_t readyCollectedMicroseconds = 0;
        std::uint64_t bindingStartedMicroseconds = 0;
        std::uint64_t workerQueueWaitMicroseconds = 0;
        std::uint64_t workerPreparationMicroseconds = 0;
        std::uint64_t workerPublicationHandoffMicroseconds = 0;
        std::uint64_t publicationToReadyCollectionMicroseconds = 0;
        std::uint64_t readyToBindWaitMicroseconds = 0;
        std::uint64_t preparationUnattributedMicroseconds = 0;
        AccessoryDispatchStageWallTimings stages{};
    };

    static AccessoryPerformanceSummary Summarize(const Histogram& histogram);
    bool BeginAction(
        AccessoryPerformanceCategory category,
        std::uint64_t requestSequence,
        std::uint64_t startedMicroseconds,
        std::uint64_t expectedSelectionGeneration,
        std::uint64_t expectedRenderGeneration,
        std::uint64_t dispatchCompletedMicroseconds,
        std::uint64_t expectedDrawOrdinal,
        const AccessoryActionDispatchTimingInput& timing);

    std::array<Histogram,
        static_cast<std::size_t>(AccessoryPerformanceCategory::Count)> histograms_{};
    std::array<PendingAction, kPendingActionCapacity> pendingActions_{};
    std::size_t pendingActionCount_ = 0;
    std::uint64_t nextSyntheticActionSequence_ = 1;
    std::uint64_t epoch_ = 1;
    std::uint64_t measurementRevision_ = 0;
    std::uint64_t publishedRevision_ = 0;
    bool hasPublished_ = false;
    bool thresholdFailure_ = false;
    bool warningPending_ = false;
    bool warningConsumed_ = false;
    std::uint64_t firstViolationMicroseconds_ = 0;
    AccessoryPerformanceCategory firstViolationCategory_ =
        AccessoryPerformanceCategory::RailRasterization;
    std::uint64_t warningCount_ = 0;
    std::uint64_t publicationCount_ = 0;
    std::uint64_t actionsCompleted_ = 0;
    std::uint64_t synchronousWallWithinBudgetCount_ = 0;
    std::uint64_t frameCadenceLimitedCount_ = 0;
    std::uint64_t preparationLimitedCount_ = 0;
    std::uint64_t synchronousWallFailureCount_ = 0;
    std::uint64_t renderWallFailureCount_ = 0;
    std::uint64_t livenessFailureCount_ = 0;
    std::uint64_t timingUnavailableCount_ = 0;
    std::uint64_t cadenceContractFailureCount_ = 0;
    std::uint64_t missedEligibleDraws_ = 0;
    std::uint64_t uniqueDrawSamples_ = 0;
    std::uint64_t drawSampleReferences_ = 0;
    std::uint64_t coalescedActionCount_ = 0;
    std::uint64_t maximumActionsPerDraw_ = 0;
    std::uint64_t nextDrawSampleId_ = 1;
    AccessoryActionTimingRecord lastAction_{};
    std::array<AccessoryActionTimingRecord, kViolationRecordCapacity>
        firstViolationRecords_{};
    std::size_t firstViolationRecordCount_ = 0;
    std::uint64_t droppedViolationRecordCount_ = 0;
    std::array<std::array<Histogram,
        static_cast<std::size_t>(AccessoryDispatchStage::Count)>, 3>
        actionStageWallHistograms_{};
    std::array<std::uint64_t,
        static_cast<std::size_t>(AccessoryRasterReason::Count)>
        railRasterReasons_{};
    std::array<std::uint64_t,
        static_cast<std::size_t>(AccessoryRasterReason::Count)>
        drawerRasterReasons_{};
};

const char* AccessoryPerformanceCategoryToken(
    AccessoryPerformanceCategory category);
const char* AccessoryActionTimingClassificationToken(
    AccessoryActionTimingClassification classification);
const char* AccessoryDispatchStageToken(AccessoryDispatchStage stage);
const char* AccessoryRasterReasonToken(AccessoryRasterReason reason);

struct AccessoryHitTestResult {
    brain::BrainOwnedAccessoryOperationStatus status =
        brain::BrainOwnedAccessoryOperationStatus::Unavailable;
    bool handled = false;
    brain::BrainOwnedAccessoryDrawerId drawer =
        brain::BrainOwnedAccessoryDrawerId::None;
};

struct AccessoryTextLayoutInput {
    std::string text;
    int contentWidth = 0;
    int maxRetainedBytes = 0;
    float scale = 1.0f;
    AccessoryFontRole fontRole = AccessoryFontRole::DrawerBody;
};

struct AccessoryTextLayoutResult {
    brain::BrainOwnedAccessoryOperationStatus status =
        brain::BrainOwnedAccessoryOperationStatus::Unavailable;
    std::vector<std::string> lines;
    bool allLinesFit = false;
    bool contentLimited = false;
    bool validUtf8 = false;
    bool endedAtValidUtf8Boundary = false;
    bool contentLimitedMarkerVisible = false;
    bool usedWordWrapping = false;
    bool usedCharacterFallback = false;
    bool measurementAvailable = false;
    int maximumMeasuredLineWidth = 0;
    std::size_t retainedBytes = 0;
    std::string reconstructedText;
    std::string finalMarker;
    std::uint64_t elapsedMicroseconds = 0;
};

struct AccessoryHistoryLayoutResult {
    brain::BrainOwnedAccessoryOperationStatus status =
        brain::BrainOwnedAccessoryOperationStatus::Unavailable;
    brain::BrainOwnedAccessoryDrawerId drawer =
        brain::BrainOwnedAccessoryDrawerId::None;
    std::vector<std::string> renderedEntryKeys;
    std::vector<std::string> renderedEntryTitles;
    std::vector<std::string> renderedEntryBodies;
    std::vector<std::vector<std::string>> renderedTitleLines;
    std::vector<std::vector<std::string>> renderedBodyLines;
    std::vector<std::uint64_t> renderedAcceptedSequences;
    int maximumOffset = 0;
    int visibleLineCapacity = 0;
    int totalScrollableLineCount = 0;
    bool allWrappedLinesFit = false;
    std::string finalMarker;
    bool finalMarkerBelongsToSelectedHistory = false;
};

struct AccessoryPreparationKey {
    brain::BrainOwnedAccessoryDrawerId drawer =
        brain::BrainOwnedAccessoryDrawerId::None;
    std::uint64_t historyGeneration = 0;
    std::uint64_t contentGeneration = 0;
    std::uint64_t layoutGeneration = 0;
    std::uint64_t commandIdentity = 0;
    std::uint64_t lifecycleEpoch = 0;
    std::uint64_t drawerContentRevision = 0;
    std::uint64_t typographyGeneration = 0;
    int scaleThousandths = 1000;
    int contentWidth = 0;
    int visibleLineCapacity = 0;

    bool operator==(const AccessoryPreparationKey& other) const;
};

struct AccessoryPreparedDrawerPlan {
    AccessoryPreparationKey key;
    AccessoryHistoryLayoutResult layout;
    std::uint64_t requestedMicroseconds = 0;
    std::uint64_t workerStartedMicroseconds = 0;
    std::uint64_t workerCompletedMicroseconds = 0;
    std::uint64_t publishedMicroseconds = 0;
    std::uint64_t preparationMicroseconds = 0;
    std::uint64_t maximumContiguousSliceMicroseconds = 0;
    std::uint64_t workerThreadIdentity = 0;
};

struct AccessoryDeferredTimingInput {
    std::uint64_t waitStartedMicroseconds = 0;
    std::uint64_t bindStartedMicroseconds = 0;
    std::uint64_t workerStartedMicroseconds = 0;
    std::uint64_t workerCompletedMicroseconds = 0;
    std::uint64_t publishedMicroseconds = 0;
    std::uint64_t readyCollectedMicroseconds = 0;
};

struct AccessoryDeferredTimingBreakdown {
    std::uint64_t totalMicroseconds = 0;
    std::uint64_t workerQueueWaitMicroseconds = 0;
    std::uint64_t workerPreparationMicroseconds = 0;
    std::uint64_t workerPublicationHandoffMicroseconds = 0;
    std::uint64_t publicationToReadyCollectionMicroseconds = 0;
    std::uint64_t readyToBindWaitMicroseconds = 0;
    std::uint64_t unattributedMicroseconds = 0;
    bool exact = false;
};

AccessoryDeferredTimingBreakdown ResolveAccessoryDeferredTiming(
    const AccessoryDeferredTimingInput& input);

struct AccessoryPreparationRequest {
    AccessoryPreparationKey key;
    std::shared_ptr<const brain::BrainOwnedAccessoryPreparationSnapshot> snapshot;
    AccessoryLayoutResult layout;
    std::uint64_t requestedMicroseconds = 0;
};

enum class AccessoryPreparationWorkerState {
    Stopped,
    Starting,
    Ready,
    Failed,
};

enum class AccessoryPreparationWorkerFailure {
    None,
    PriorityAssignment,
    TextMeasurementInitialization,
};

const char* AccessoryPreparationWorkerStateToken(
    AccessoryPreparationWorkerState state);
const char* AccessoryPreparationWorkerFailureToken(
    AccessoryPreparationWorkerFailure failure);

struct AccessoryPreparationAvailabilityDecision {
    bool ready = false;
    bool retryStartup = false;
    bool cancelPendingAction = false;
};

AccessoryPreparationAvailabilityDecision
ResolveAccessoryPreparationAvailability(
    AccessoryPreparationWorkerState state,
    bool pendingAction);

struct AccessoryPreparationWorkerHooks {
    std::function<bool()> assignBelowNormalPriority;
    std::function<AccessoryTextMeasurementContext*()> initializeTextMeasurement;
    std::function<void(AccessoryTextMeasurementContext*)> shutdownTextMeasurement;
    std::function<void()> beforeReadyPublication;
};

struct AccessoryPreparationWorkerCounters {
    std::uint64_t enqueueAttemptCount = 0;
    std::uint64_t enqueueContentionCount = 0;
    std::uint64_t enqueueSuccessCount = 0;
    std::uint64_t enqueueReplacementCount = 0;
    std::uint64_t maximumEnqueueMicroseconds = 0;
    std::uint64_t jobsRequested = 0;
    std::uint64_t jobsReplaced = 0;
    std::uint64_t jobsStarted = 0;
    std::uint64_t jobsCompleted = 0;
    std::uint64_t jobsCancelled = 0;
    std::uint64_t staleResultsRejected = 0;
    std::uint64_t workerWakeCount = 0;
    std::uint64_t workerSleepCount = 0;
    std::uint64_t maximumQueueDepth = 0;
    std::uint64_t maximumReadyCacheCount = 0;
    std::uint64_t maximumContiguousExecutionMicroseconds = 0;
    std::array<std::uint64_t, 3> totalPreparationMicroseconds{};
    std::uint64_t publicationCount = 0;
    std::uint64_t readinessCheckCount = 0;
    std::uint64_t readinessContentionCount = 0;
    std::uint64_t maximumPublicationMicroseconds = 0;
    std::uint64_t startupAttemptCount = 0;
    std::uint64_t startupSuccessCount = 0;
    std::uint64_t startupFailureCount = 0;
    std::uint64_t startupFailureDiagnosticCount = 0;
    std::uint64_t requestsRejectedUnavailable = 0;
    bool priorityRequested = false;
    bool prioritySucceeded = false;
    AccessoryPreparationWorkerState lifecycleState =
        AccessoryPreparationWorkerState::Stopped;
    AccessoryPreparationWorkerFailure failure =
        AccessoryPreparationWorkerFailure::None;
    std::uint64_t workerThreadIdentity = 0;
    std::uint64_t mainThreadIdentity = 0;
    std::uint64_t prohibitedAccessCount = 0;
    std::uint64_t runningWorkerThreads = 0;
};

class AccessoryPreparationWorker {
public:
    explicit AccessoryPreparationWorker(
        AccessoryPreparationWorkerHooks hooks = {});
    ~AccessoryPreparationWorker();
    AccessoryPreparationWorker(const AccessoryPreparationWorker&) = delete;
    AccessoryPreparationWorker& operator=(const AccessoryPreparationWorker&) = delete;

    bool Start(std::uint64_t mainThreadIdentity);
    void Stop();
    bool Request(const AccessoryPreparationRequest& request);
    std::shared_ptr<const AccessoryPreparedDrawerPlan> TryTakeReady(
        const AccessoryPreparationKey& key,
        std::uint64_t* publicationMicroseconds = nullptr);
    void CancelAll();
    bool Running() const;
    AccessoryPreparationWorkerState State() const;
    AccessoryPreparationWorkerFailure Failure() const;
    std::uint64_t ReadySequence() const;
    AccessoryPreparationWorkerCounters SnapshotCounters() const;

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

struct AccessoryWheelInput {
    AccessoryLayoutResult layout;
    int pointerX = 0;
    int pointerY = 0;
    int wheelClicks = 0;
    int drawerOffset = 0;
    int drawerMaximumOffset = 0;
    int mainCardOffset = 0;
    int mainCardMaximumOffset = 0;
};

enum class AccessoryWheelScope {
    Unhandled,
    Drawer,
    MainCard,
};

struct AccessoryWheelResult {
    brain::BrainOwnedAccessoryOperationStatus status =
        brain::BrainOwnedAccessoryOperationStatus::Unavailable;
    AccessoryWheelScope scope = AccessoryWheelScope::Unhandled;
    bool handled = false;
    bool drawerChanged = false;
    bool mainCardChanged = false;
    int drawerOffset = 0;
    int mainCardOffset = 0;
    bool accessoryRasterRequested = false;
};

struct AccessoryRenderCounters {
    std::uint64_t historyVisits = 0;
    std::uint64_t entryCopies = 0;
    std::uint64_t wrapVisits = 0;
    std::uint64_t mainCardRasterRequests = 0;
    std::uint64_t railRasterRequests = 0;
    std::uint64_t drawerRasterRequests = 0;
    std::uint64_t uploadRequests = 0;
    std::uint64_t snapshotPublications = 0;
};

struct AccessoryPresentationState {
    std::shared_ptr<const brain::BrainOwnedAccessoryPresentationSnapshot>
        activeSnapshot;
    std::uint64_t activeSnapshotIdentity = 0;
    std::uint64_t selectionGeneration = 0;
    std::uint64_t historyGeneration = 0;
    std::uint64_t contentGeneration = 0;
    std::uint64_t layoutGeneration = 0;
    std::uint64_t commandIdentity = 0;
    std::uint64_t lifecycleEpoch = 0;
    std::uint64_t railPresentationRevision = 0;
    std::uint64_t drawerContentRevision = 0;
    std::string railRenderSignature;
    std::string drawerRenderSignature;
    std::string mainCardProductionSignature;
    std::shared_ptr<const AccessoryPreparedDrawerPlan> preparedPlan;
    bool cachedPlanAvailable = false;
    int drawerOffset = 0;
    int drawerMaximumOffset = 0;
    std::string finalHistoryMarker;
    AccessoryRenderCounters counters;
};

struct AccessoryDrawerRenderLine {
    std::string text;
    bool title = false;
    bool finalMarker = false;
};

struct AccessoryDrawerRenderPlan {
    brain::BrainOwnedAccessoryOperationStatus status =
        brain::BrainOwnedAccessoryOperationStatus::Unavailable;
    int visibleLineCapacity = 0;
    int firstVisibleLine = 0;
    int totalLineCount = 0;
    bool everyLineFitsPixelGeometry = false;
    bool finalMarkerVisible = false;
    std::vector<AccessoryDrawerRenderLine> visibleLines;
};

struct AccessoryPresentationUpdateInput {
    brain::BrainOwnedAccessoryPresentationHandle presentation;
    AccessoryLayoutResult layout;
    std::string mainCardProductionSignature;
    AccessoryTextMeasurementContext* measurementContext = nullptr;
    std::shared_ptr<const AccessoryPreparedDrawerPlan> preparedPlan;
    bool collectAcceptedActionStageTiming = false;
};

struct AccessoryPresentationUpdateResult {
    brain::BrainOwnedAccessoryOperationStatus status =
        brain::BrainOwnedAccessoryOperationStatus::Unavailable;
    bool snapshotChanged = false;
    bool selectionChanged = false;
    bool historyChanged = false;
    bool layoutChanged = false;
    bool railAppearanceChanged = false;
    bool cachedPlanBuilt = false;
    bool preparationPending = false;
    bool drawerOffsetReset = false;
    bool mainCardUnchanged = false;
    int publishedSnapshotCount = 0;
    brain::BrainOwnedAccessoryDrawerId publishedDrawer =
        brain::BrainOwnedAccessoryDrawerId::None;
    bool intermediateNonePublished = false;
    std::uint64_t gdiMeasurementWrappingWallMicroseconds = 0;
    AccessoryRenderCounters delta;
};

struct AccessoryPresentationScrollResult {
    brain::BrainOwnedAccessoryOperationStatus status =
        brain::BrainOwnedAccessoryOperationStatus::Unavailable;
    AccessoryWheelScope scope = AccessoryWheelScope::Unhandled;
    bool handled = false;
    bool changed = false;
    bool reachedFinalMarker = false;
    int previousOffset = 0;
    int drawerOffset = 0;
    int drawerMaximumOffset = 0;
    AccessoryRenderCounters delta;
};

struct AccessoryPresentationScrollInput {
    AccessoryLayoutResult layout;
    int pointerX = 0;
    int pointerY = 0;
    int wheelClicks = 0;
};

struct AccessoryPresentationWarmResult {
    brain::BrainOwnedAccessoryOperationStatus status =
        brain::BrainOwnedAccessoryOperationStatus::Unavailable;
    int iterations = 0;
    AccessoryRenderCounters delta;
};

AccessoryTextMeasurementContext* InitializeAccessoryTextMeasurement();
void ShutdownAccessoryTextMeasurement(AccessoryTextMeasurementContext* context);
AccessoryTypographyMetrics PrepareAccessoryTypography(
    AccessoryTextMeasurementContext* context,
    float scale);
AccessoryGdiMeasurementCounters GetAccessoryGdiMeasurementCounters(
    const AccessoryTextMeasurementContext* context);
AccessoryLayoutResult ResolveAccessoryLayout(const AccessoryLayoutInput& input);
AccessoryAnchorUpdateResult UpdateAccessoryAnchorState(
    AccessoryAnchorState* state,
    const AccessoryAnchorUpdateInput& input);
AccessoryTextMeasurementResult MeasureAccessoryText(
    AccessoryTextMeasurementContext* context,
    const AccessoryTextMeasurementInput& input);
AccessoryHitTestResult HitTestAccessoryOrb(
    const AccessoryLayoutResult& layout,
    int x,
    int y);
AccessoryTextLayoutResult BuildAccessoryTextLayout(
    AccessoryTextMeasurementContext* context,
    const AccessoryTextLayoutInput& input);
AccessoryHistoryLayoutResult BuildAccessoryHistoryLayout(
    AccessoryTextMeasurementContext* context,
    const brain::BrainOwnedAccessoryPresentationSnapshot& snapshot,
    const AccessoryLayoutResult& layout);
AccessoryWheelResult ApplyAccessoryWheel(const AccessoryWheelInput& input);
AccessoryDrawerRenderPlan BuildAccessoryDrawerRenderPlan(
    const AccessoryPresentationState& state,
    const AccessoryLayoutResult& layout);
AccessoryPresentationUpdateResult UpdateAccessoryPresentation(
    AccessoryPresentationState* state,
    const AccessoryPresentationUpdateInput& input);
AccessoryPresentationScrollResult ScrollAccessoryPresentation(
    AccessoryPresentationState* state,
    const AccessoryPresentationScrollInput& input);
AccessoryPresentationWarmResult RunUnchangedAccessoryPresentationUpdates(
    AccessoryPresentationState* state,
    const AccessoryPresentationUpdateInput& input,
    int iterations);

}  // namespace xvatsim::modules::overlay
