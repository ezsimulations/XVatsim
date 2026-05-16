#pragma once

#include <cstddef>
#include <cstdint>

#include "XVatsim/brain/BrainTypes.h"
#include "XPLMDisplay.h"

namespace xvatsim::modules::overlay {

class OverlayWindow {
public:
    OverlayWindow() = default;
    ~OverlayWindow() = default;

    void Create();
    void Destroy();
    void Update(const brain::OverlayViewModel& viewModel);
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

    XPLMWindowID window_ = nullptr;
    brain::OverlayViewModel viewModel_{};
    bool textEntryActive_ = false;
    bool hasPendingSubmittedText_ = false;
    std::string textEntryBuffer_;
    std::string pendingSubmittedText_;
    bool hasPendingAcknowledgeRequest_ = false;
    bool hasPendingRecallRequest_ = false;
    bool dragging_ = false;
    bool resizing_ = false;
    ResizeCorner activeResizeCorner_ = ResizeCorner::None;
    int dragOffsetX_ = 0;
    int dragOffsetTop_ = 0;
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
    bool caseTextureDirty_ = true;
    bool cardTextureDirty_ = true;
    std::size_t lastCardSignature_ = 0;
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
