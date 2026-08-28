#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <optional>
#include <string>

#include "XVatsim/brain/BrainTypes.h"
#include "XVatsim/modules/overlay/OverlayAccessoryCore.h"
#include "XPLMDisplay.h"

namespace xvatsim::modules::overlay {

using OverlayAccessoryClickFact = AccessoryClickFact;
using OverlayAccessoryInputDispatchCallback = void (*)(void* refcon);
using OverlayAccessoryPreparationFailureCallback = void (*)(
    AccessoryPreparationWorkerFailure failure,
    void* refcon);

struct OverlayAccessoryIntegrationCounters {
    std::uint64_t clickFactsProduced = 0;
    std::uint64_t clickFactsDropped = 0;
    std::uint64_t railRasterizations = 0;
    std::uint64_t drawerRasterizations = 0;
    std::uint64_t railTextureUploads = 0;
    std::uint64_t drawerTextureUploads = 0;
    std::uint64_t railDraws = 0;
    std::uint64_t drawerDraws = 0;
    std::uint64_t unchangedUpdates = 0;
    std::uint64_t accessoryRenderGeneration = 0;
    AccessoryInputDispatchSnapshot dispatch;
    AccessoryPerformanceSnapshot performance;
};

class OverlayWindow {
public:
    OverlayWindow() = default;
    ~OverlayWindow() = default;

    void Create();
    void Destroy();
    void Update(const brain::OverlayViewModel& viewModel);
    void UpdateAccessory(
        const brain::BrainOwnedAccessoryPresentationHandle& presentation,
        AccessoryDispatchStageWallTimings* acceptedActionStages = nullptr);
    void QueueAccessoryPreparation(
        const brain::BrainOwnedAccessoryPreparationHandle& preparation);
    void StartAccessoryPreparation();
    void StopAccessoryPreparation();
    AccessoryPreparationWorkerCounters GetAccessoryPreparationCounters() const;
    void SetAccessoryInputDispatchCallback(
        OverlayAccessoryInputDispatchCallback callback,
        void* refcon);
    void SetAccessoryPreparationFailureCallback(
        OverlayAccessoryPreparationFailureCallback callback,
        void* refcon);
    bool BeginAccessoryInputDispatch(OverlayAccessoryClickFact* outFact);
    bool BindAccessoryInputDispatch(
        const OverlayAccessoryClickFact& fact,
        brain::BrainOwnedAccessoryDrawerAction action);
    bool CancelAccessoryInputDispatch(std::uint64_t requestSequence);
    std::size_t DiscardPendingAccessoryClickFacts();
    bool ConsumeAccessoryPerformancePublication(
        AccessoryPerformanceSnapshot* outSnapshot);
    static std::uint64_t AccessoryWallClockMicroseconds();
    std::uint64_t GetAccessoryLayoutGeneration() const;
    OverlayAccessoryIntegrationCounters GetAccessoryIntegrationCounters() const;
    void SetAutomaticMode(bool automaticMode);
    void SetTransitionSoundPath(const std::string& transitionSoundPath);
    void SetOpacity(float opacity);
    void SetScale(float scale);
    void SetAnimationSpeed(float speed);
    void BeginTextEntry(const std::string& initialText);
    void CancelTextEntry();
    bool ConsumeSubmittedText(std::string* outText);
    bool ConsumeAcknowledgeRequest();
    bool ConsumeRecallRequest();
    bool ConsumeSystemNoticeDismissRequest();
    void SetWindowTopLeft(int left, int top);
    bool GetWindowTopLeft(int* outLeft, int* outTop) const;
    bool ConsumePositionChanged(int* outLeft, int* outTop);
    bool ConsumeScaleChanged(float* outScale);
    void Hide();

private:
    enum class ResizeCorner {
        None,
        LowerLeft,
        LowerRight,
    };

    static void DrawWindowCallback(XPLMWindowID windowId, void* refcon);
    static int HandleMouseClickCallback(
        XPLMWindowID windowId,
        int x,
        int y,
        XPLMMouseStatus mouse,
        void* refcon);
    static int HandleRightClickCallback(
        XPLMWindowID windowId,
        int x,
        int y,
        XPLMMouseStatus mouse,
        void* refcon);
    static int HandleMouseWheelCallback(XPLMWindowID windowId, int x, int y, int wheel, int clicks, void* refcon);
    static XPLMCursorStatus HandleCursorCallback(XPLMWindowID windowId, int x, int y, void* refcon);
    static void HandleKeyCallback(
        XPLMWindowID windowId,
        char key,
        XPLMKeyFlags flags,
        char virtualKey,
        void* refcon,
        int losingFocus);

    void SyncVisibility();
    void Draw();
    void HandleTextEntryKey(char key, char virtualKey, int losingFocus);
    std::string ResolvePromptLine() const;
    bool IsInDragRegion(int x, int y) const;
    bool IsInOverlayRegion(int x, int y) const;
    bool IsInAcknowledgeAction(int x, int y) const;
    bool IsInRecallAction(int x, int y) const;
    bool IsInSystemNoticeDismissAction(int x, int y) const;
    ResizeCorner ResolveResizeCorner(int x, int y) const;
    void ClampScrollOffset();
    void UpdateAnimationState();
    void PlayTransitionSound();
    void CloseTransitionSoundAlias();
    void ApplyScale(float scale, bool anchorRight);
    void StartDragging(int x, int y);
    void ContinueDragging(int x, int y);
    void StopDragging();
    void StartResizing(int x, int y, ResizeCorner corner);
    void ContinueResizing(int x, int y);
    void StopResizing();
    void RefreshAccessoryLayout(
        AccessoryAnchorOperation operation =
            AccessoryAnchorOperation::OrdinaryRefresh,
        bool hasExactAnchor = false,
        int exactLeft = 0,
        int exactTop = 0);
    void ApplyAccessoryWindowGeometry(bool drawerOpen);
    bool IsInAccessoryVisibleRegion(int x, int y) const;
    void RenderAccessoryTexturesIfNeeded(
        AccessoryActionDrawTimingInput* actionDrawTiming = nullptr);
    void MarkAccessoryRailTextureDirty(AccessoryRasterReason reason);
    void MarkAccessoryDrawerTextureDirty(AccessoryRasterReason reason);
    void QueueAccessoryClick(brain::BrainOwnedAccessoryDrawerId drawer);
    void RecordAccessoryPerformance(
        AccessoryPerformanceCategory category,
        std::uint64_t elapsedMicroseconds);
    void PublishFirstAccessoryPerformanceWarningIfNeeded();
    void RetryPendingAccessoryPreparations();
    bool PublishReadyAccessoryPreparation();
    AccessoryPreparationKey BuildAccessoryPreparationKey(
        brain::BrainOwnedAccessoryDrawerId drawer,
        std::uint64_t historyGeneration,
        std::uint64_t contentGeneration) const;

    XPLMWindowID window_ = nullptr;
    brain::OverlayViewModel viewModel_{};
    bool textEntryActive_ = false;
    bool hasPendingSubmittedText_ = false;
    std::string textEntryBuffer_;
    std::string pendingSubmittedText_;
    bool hasPendingAcknowledgeRequest_ = false;
    bool hasPendingRecallRequest_ = false;
    bool hasPendingSystemNoticeDismissRequest_ = false;
    bool dragging_ = false;
    bool resizing_ = false;
    ResizeCorner activeResizeCorner_ = ResizeCorner::None;
    int dragOffsetX_ = 0;
    int dragOffsetTop_ = 0;
    bool dragMoved_ = false;
    int resizeAnchorTop_ = 0;
    int resizeAnchorLeft_ = 0;
    int resizeAnchorRight_ = 0;
    bool windowVisible_ = false;
    bool overlayEnabled_ = true;
    int scrollOffset_ = 0;
    float animationProgress_ = 0.0f;
    float animationTarget_ = 0.0f;
    float animationLastTimestampSeconds_ = -1.0f;
    std::uintptr_t gdiplusToken_ = 0;
    unsigned int caseTextureId_ = 0;
    unsigned int cardTextureId_ = 0;
    unsigned int accessoryRailTextureId_ = 0;
    unsigned int accessoryDrawerTextureId_ = 0;
    bool caseTextureDirty_ = true;
    bool cardTextureDirty_ = true;
    std::size_t lastCardSignature_ = 0;
    AccessoryTextMeasurementContext* accessoryMeasurementContext_ = nullptr;
    AccessoryTypographyMetrics accessoryTypography_{};
    AccessoryLayoutResult accessoryLayout_{};
    AccessoryPresentationState accessoryPresentation_{};
    AccessoryPresentationUpdateInput accessoryUpdateInput_{};
    AccessoryPreparationWorker accessoryPreparationWorker_{};
    OverlayAccessoryPreparationFailureCallback
        accessoryPreparationFailureCallback_ = nullptr;
    void* accessoryPreparationFailureRefcon_ = nullptr;
    bool accessoryPreparationFailureDiagnosticEmitted_ = false;
    std::array<AccessoryPreparationKey, 3> accessoryRequestedPreparationKeys_{};
    std::array<bool, 3> accessoryPreparationRequested_{};
    std::array<std::optional<AccessoryPreparationRequest>, 3>
        accessoryPendingPreparationRequests_{};
    std::uint64_t accessoryPreparationWaitStartedMicroseconds_ = 0;
    bool accessoryDeferredBindingPending_ = false;
    OverlayAccessoryClickFact accessoryDeferredBindingFact_{};
    brain::BrainOwnedAccessoryDrawerAction accessoryDeferredBindingAction_ =
        brain::BrainOwnedAccessoryDrawerAction::None;
    brain::BrainOwnedAccessoryPresentationHandle accessoryPresentationHandle_{};
    std::string mainCardProductionSignature_ = "0";
    bool accessoryRailTextureDirty_ = true;
    bool accessoryDrawerTextureDirty_ = true;
    AccessoryRasterReason accessoryRailRasterReason_ =
        AccessoryRasterReason::Initial;
    AccessoryRasterReason accessoryDrawerRasterReason_ =
        AccessoryRasterReason::Initial;
    bool accessoryDrawerOpen_ = false;
    AccessoryAnchorState accessoryAnchorState_{};
    int accessoryLastScreenLeft_ = 0;
    int accessoryLastScreenTop_ = 0;
    int accessoryLastScreenRight_ = 0;
    int accessoryLastScreenBottom_ = 0;
    float accessoryLastAnimationProgress_ = -1.0f;
    std::uint64_t accessoryLayoutGeneration_ = 1;
    std::uint64_t accessoryRenderGeneration_ = 1;
    std::uint64_t accessoryDrawCallbackOrdinal_ = 0;
    std::uint64_t accessoryLastDrawCallbackEnteredMicroseconds_ = 0;
    AccessoryClickFactQueue accessoryClickQueue_{};
    AccessoryInputDispatchCoordinator accessoryInputDispatcher_{};
    OverlayAccessoryInputDispatchCallback accessoryInputDispatchCallback_ = nullptr;
    void* accessoryInputDispatchRefcon_ = nullptr;
    std::unique_ptr<AccessoryPerformanceCollector> accessoryPerformance_;
    std::uint64_t accessoryPerformanceEpoch_ = 1;
    OverlayAccessoryIntegrationCounters accessoryIntegrationCounters_{};
    bool positionChanged_ = false;
    bool scaleChanged_ = false;
    bool automaticMode_ = true;
    std::string transitionSoundPath_;
    bool transitionSoundLoaded_ = false;
    bool lastWakeState_ = false;
    float opacity_ = 1.0f;
    float scale_ = 1.0f;
    float animationSpeed_ = 1.0f;
    bool hasPendingWindowTopLeft_ = false;
    int pendingWindowLeft_ = 0;
    int pendingWindowTop_ = 0;
};

}  // namespace xvatsim::modules::overlay
