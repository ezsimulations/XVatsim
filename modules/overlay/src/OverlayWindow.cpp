#include "XVatsim/modules/overlay/OverlayWindow.h"

#if defined(XVATSIM_STEP3_OFFLINE_VISUAL_PROOF)
#include "XVatsim/modules/overlay/OverlayVisualProof.h"
#endif

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <mmsystem.h>
#include <gl/GL.h>

#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

namespace xvatsim::modules::overlay {

namespace {

using Gdiplus::Bitmap;
using Gdiplus::Color;
using Gdiplus::Font;
using Gdiplus::FontStyleBold;
using Gdiplus::FontStyleRegular;
using Gdiplus::Graphics;
using Gdiplus::GraphicsPath;
using Gdiplus::LinearGradientBrush;
using Gdiplus::Pen;
using Gdiplus::PointF;
using Gdiplus::Rect;
using Gdiplus::RectF;
using Gdiplus::SolidBrush;
using Gdiplus::StringAlignmentNear;
using Gdiplus::StringAlignmentCenter;
using Gdiplus::StringFormat;
using Gdiplus::StringFormatFlagsNoWrap;
using Gdiplus::StringTrimmingEllipsisCharacter;
using Gdiplus::UnitPixel;

constexpr int kOverlayMarginLeft = 18;
constexpr int kOverlayMarginTop = 44;
constexpr int kOverlayWidth = 430;
constexpr int kOverlayHeight = 374;
constexpr int kOverlayOpenHeight = 506;

constexpr int kCaseWidth = 170;
constexpr int kCaseHeight = 18;
constexpr int kCaseTopInset = 6;
constexpr int kTetherWidth = 12;
constexpr int kTetherHeight = 18;

constexpr int kCardWidth = 388;
constexpr int kCardHeight = 304;
constexpr int kCardTopOffset = 28;
constexpr int kCardLeftInset = 18;
constexpr int kDragRegionHeight = 52;
constexpr int kResizeHotspotPx = 22;
constexpr int kVisibleListRows = 4;
constexpr float kShowDurationSeconds = 0.95f;
constexpr float kHideDurationSeconds = 1.10f;
constexpr float kBringFrontCooldownSeconds = 3.0f;
constexpr wchar_t kTransitionSoundAlias[] = L"xvatsim_transition";
constexpr std::size_t kMaxRenderTextChars = 128;
constexpr std::size_t kMaxRenderHeaderChars = 32;
constexpr std::size_t kMaxRenderFrequencyChars = 16;
constexpr std::size_t kMaxRenderListLines = 64;
constexpr std::size_t kMaxTextEntryChars = 32;

using OverlayClock = std::chrono::steady_clock;

struct OverlayUpdateTiming {
    long long windowCreateUs = 0;
    long long setVisibleUs = 0;
    long long bringFrontUs = 0;
    long long bodyUs = 0;
    long long transitionSoundUs = 0;
    long long otherUs = 0;
    long long totalUs = 0;
    bool windowCreateCalled = false;
    bool setVisibleCalled = false;
    bool bringFrontChecked = false;
    bool bringFrontCalled = false;
    bool bringFrontThrottled = false;
    bool transitionSoundSkipped = false;
};

OverlayUpdateTiming gLastOverlayUpdateTiming;
float gLastBringFrontAttemptTimeSeconds = -1000.0f;

long long ElapsedOverlayUsSince(OverlayClock::time_point started) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               OverlayClock::now() - started)
        .count();
}

void ResetOverlayUpdateTiming() {
    gLastOverlayUpdateTiming = {};
}

long long KnownOverlayUpdateUs(const OverlayUpdateTiming& timing) {
    return timing.windowCreateUs +
           timing.setVisibleUs +
           timing.bringFrontUs +
           timing.bodyUs +
           timing.transitionSoundUs;
}

struct OverlaySections {
    std::string callsignOrStatus;
    std::string badgeText;
    std::string phaseChip;
    std::string headerRightText;
    std::string versionText;
    std::string versionAlternateText;
    bool versionRotateAlternate = false;
    xvatsim::brain::OverlayVersionTone versionTone =
        xvatsim::brain::OverlayVersionTone::Unknown;
    xvatsim::brain::OverlayNoticeSnapshot systemNotice;
    xvatsim::brain::RadioStateSnapshot radioState;
    std::vector<xvatsim::brain::OverlayTextLine> listLines;
    std::string footerPrimary;
    std::string footerSecondary;
    bool showMessageAcknowledge = false;
    bool showMessageRecall = false;
};

struct OverlayLayout {
    int caseLeft = 0;
    int caseTop = 0;
    int caseRight = 0;
    int caseBottom = 0;
    int tetherLeft = 0;
    int tetherTop = 0;
    int tetherRight = 0;
    int tetherBottom = 0;
    int cardLeft = 0;
    int cardTop = 0;
    int cardRight = 0;
    int cardBottom = 0;
    int listTop = 0;
    int listBottom = 0;
};

struct RasterImage {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
};

struct ScreenRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    bool Contains(int x, int y) const {
        return x >= left && x <= right && y <= top && y >= bottom;
    }
};

enum class OverlayMessageAction {
    None,
    Acknowledge,
    Recall,
};

bool StartsWith(const std::string& value, const char* prefix) {
    return value.rfind(prefix, 0) == 0;
}

int ScaleValue(int value, float scale) {
    return std::max(1, static_cast<int>(std::round(static_cast<float>(value) * scale)));
}

void ClampTopLeftToScreen(int width, int height, int* left, int* top) {
    if (left == nullptr || top == nullptr || width <= 0 || height <= 0) {
        return;
    }

    int screenLeft = 0;
    int screenTop = 0;
    int screenRight = 0;
    int screenBottom = 0;
#if defined(XVATSIM_STEP3_OFFLINE_VISUAL_PROOF)
    screenRight = 1920;
    screenTop = 1080;
#else
    XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);
#endif
    if (screenRight <= screenLeft || screenTop <= screenBottom) {
        return;
    }

    const auto screenWidth = screenRight - screenLeft;
    const auto screenHeight = screenTop - screenBottom;
    if (width >= screenWidth) {
        *left = screenLeft;
    } else {
        *left = std::clamp(*left, screenLeft, screenRight - width);
    }

    if (height >= screenHeight) {
        *top = screenTop;
    } else {
        *top = std::clamp(*top, screenBottom + height, screenTop);
    }
}

std::string ToUpper(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
    return value;
}

std::string SanitizeRenderText(std::string value, std::size_t maxChars) {
    const auto nullPosition = value.find('\0');
    if (nullPosition != std::string::npos) {
        value.resize(nullPosition);
    }

    std::string sanitized;
    sanitized.reserve(std::min(value.size(), maxChars));
    bool pendingSpace = false;
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isspace(byte) != 0) {
            pendingSpace = !sanitized.empty();
            continue;
        }
        if (std::iscntrl(byte) != 0) {
            continue;
        }
        if (pendingSpace) {
            sanitized.push_back(' ');
            pendingSpace = false;
        }
        sanitized.push_back(character);
        if (sanitized.size() > maxChars) {
            sanitized.resize(maxChars > 3 ? maxChars - 3 : maxChars);
            if (maxChars > 3) {
                sanitized += "...";
            }
            break;
        }
    }

    return sanitized;
}

std::wstring ToWide(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

std::wstring QuoteMciPath(const std::string& path) {
    const auto widePath = ToWide(path);
    return L"\"" + widePath + L"\"";
}

std::string ExtractCallsign(const std::string& xPilotLine) {
    constexpr char kConnectedPrefix[] = "xPilot connected ";
    constexpr char kLegacyConnectedPrefix[] = "XP xPilot connected ";
    for (const auto* prefix : {kConnectedPrefix, kLegacyConnectedPrefix}) {
        if (StartsWith(xPilotLine, prefix)) {
            const auto callsign = xPilotLine.substr(std::char_traits<char>::length(prefix));
            if (!callsign.empty()) {
                return SanitizeRenderText(callsign, kMaxRenderHeaderChars);
            }
        }
    }

    if (xPilotLine.empty()) {
        return "XVatsim";
    }

    return SanitizeRenderText(xPilotLine, kMaxRenderHeaderChars);
}

std::string ResolveBadgeText(const std::string& xPilotLine) {
    if (xPilotLine.find("connected") != std::string::npos) {
        return "CONNECTED";
    }
    if (xPilotLine.find("loaded") != std::string::npos) {
        return "LOADED";
    }
    return "STANDBY";
}

std::string ResolvePhaseChip(const std::string& title) {
    if (!StartsWith(title, "XVatsim ")) {
        return "SYNC";
    }

    const auto phase = title.substr(8);
    if (phase == "parked") return "PARK";
    if (phase == "taxi") return "TAXI";
    if (phase == "ground roll") return "ROLL";
    if (phase == "departure") return "DEP";
    if (phase == "climb") return "CLB";
    if (phase == "cruise") return "CRZ";
    if (phase == "descent") return "DES";
    if (phase == "approach") return "APP";
    return ToUpper(phase);
}

OverlayLayout ResolveLayout(int windowLeft, int windowTop, float animationProgress, float scale) {
    OverlayLayout layout;
    const auto overlayWidth = ScaleValue(kOverlayWidth, scale);
    const auto caseWidth = ScaleValue(kCaseWidth, scale);
    const auto caseHeight = ScaleValue(kCaseHeight, scale);
    const auto caseTopInset = ScaleValue(kCaseTopInset, scale);
    const auto tetherWidth = ScaleValue(kTetherWidth, scale);
    const auto tetherHeight = ScaleValue(kTetherHeight, scale);
    const auto cardWidth = ScaleValue(kCardWidth, scale);
    const auto cardHeight = ScaleValue(kCardHeight, scale);
    const auto cardTopOffset = ScaleValue(kCardTopOffset, scale);
    const auto cardLeftInset = ScaleValue(kCardLeftInset, scale);

    layout.caseLeft = windowLeft + ((overlayWidth - caseWidth) / 2);
    layout.caseTop = windowTop - caseTopInset;
    layout.caseRight = layout.caseLeft + caseWidth;
    layout.caseBottom = layout.caseTop - caseHeight;

    const auto currentCardHeight =
        (animationProgress <= 0.0f)
            ? 0
            : static_cast<int>(std::round(static_cast<float>(cardHeight) * animationProgress));

    layout.cardLeft = windowLeft + cardLeftInset;
    layout.cardTop = windowTop - cardTopOffset;
    layout.cardRight = layout.cardLeft + cardWidth;
    layout.cardBottom = layout.cardTop - currentCardHeight;

    layout.tetherLeft = windowLeft + ((overlayWidth - tetherWidth) / 2);
    layout.tetherRight = layout.tetherLeft + tetherWidth;
    layout.tetherTop = layout.caseBottom;
    layout.tetherBottom = std::max(
        layout.cardTop,
        layout.tetherTop -
            static_cast<int>(std::round(static_cast<float>(tetherHeight) * animationProgress)));

    layout.listTop = layout.cardTop - ScaleValue(148, scale);
    layout.listBottom = layout.cardBottom + ScaleValue(60, scale);
    return layout;
}

ScreenRect ResolveCardActionRect(
    const OverlayLayout& layout,
    float scale,
    OverlayMessageAction action) {
    int designLeft = 0;
    int designTop = 257;
    int designWidth = 0;
    int designHeight = 22;

    switch (action) {
        case OverlayMessageAction::Acknowledge:
            designLeft = 304;
            designWidth = 54;
            break;
        case OverlayMessageAction::Recall:
            designLeft = 276;
            designWidth = 82;
            break;
        case OverlayMessageAction::None:
        default:
            return {};
    }

    ScreenRect rect;
    rect.left = layout.cardLeft + ScaleValue(designLeft, scale);
    rect.top = layout.cardTop - ScaleValue(designTop, scale);
    rect.right = rect.left + ScaleValue(designWidth, scale);
    rect.bottom = rect.top - ScaleValue(designHeight, scale);
    return rect;
}

ScreenRect ResolveSystemNoticeDismissRect(
    const OverlayLayout& layout,
    float scale) {
    ScreenRect rect;
    rect.left = layout.cardLeft + ScaleValue(300, scale);
    rect.top = layout.cardTop - ScaleValue(250, scale);
    rect.right = rect.left + ScaleValue(58, scale);
    rect.bottom = rect.top - ScaleValue(22, scale);
    return rect;
}

OverlaySections ResolveSections(
    const brain::OverlayViewModel& viewModel,
    bool textEntryActive,
    const std::string& promptLine) {
    OverlaySections sections;

    const auto& lines = viewModel.bodyLines;
    const auto xPilotLine = lines.size() > 0 ? lines[0].text : std::string{};
    sections.callsignOrStatus = ExtractCallsign(xPilotLine);
    sections.badgeText = ResolveBadgeText(xPilotLine);
    sections.phaseChip = ResolvePhaseChip(viewModel.title);
    sections.headerRightText =
        SanitizeRenderText(viewModel.headerRightText, kMaxRenderHeaderChars);
    sections.versionText =
        SanitizeRenderText(viewModel.version.text, kMaxRenderHeaderChars);
    sections.versionAlternateText =
        SanitizeRenderText(viewModel.version.alternateText, kMaxRenderHeaderChars);
    sections.versionRotateAlternate = viewModel.version.rotateAlternate;
    sections.versionTone = viewModel.version.tone;
    if (sections.versionRotateAlternate &&
        !sections.versionAlternateText.empty() &&
#if defined(XVATSIM_STEP3_OFFLINE_VISUAL_PROOF)
        false) {
#else
        static_cast<int>(std::floor(XPLMGetElapsedTime() / 2.0f)) % 2 != 0) {
#endif
        sections.versionText = sections.versionAlternateText;
    }
    sections.systemNotice = viewModel.systemNotice;
    sections.systemNotice.title =
        SanitizeRenderText(sections.systemNotice.title, kMaxRenderTextChars);
    sections.systemNotice.dismissText =
        SanitizeRenderText(sections.systemNotice.dismissText, kMaxRenderHeaderChars);
    for (auto& line : sections.systemNotice.bodyLines) {
        line = SanitizeRenderText(line, kMaxRenderTextChars);
    }
    sections.radioState = viewModel.radioState;
    sections.radioState.com1ActiveFrequency = SanitizeRenderText(
        sections.radioState.com1ActiveFrequency,
        kMaxRenderFrequencyChars);
    sections.radioState.com2ActiveFrequency = SanitizeRenderText(
        sections.radioState.com2ActiveFrequency,
        kMaxRenderFrequencyChars);
    sections.radioState.com1StandbyFrequency = SanitizeRenderText(
        sections.radioState.com1StandbyFrequency,
        kMaxRenderFrequencyChars);
    sections.showMessageAcknowledge = viewModel.showMessageAcknowledge;
    sections.showMessageRecall = viewModel.showMessageRecall;

    auto startIndex = std::min<std::size_t>(1, lines.size());
    auto endIndex = lines.size();
    if (!textEntryActive && endIndex > startIndex) {
        --endIndex;
    }

    for (auto index = startIndex; index < endIndex; ++index) {
        if (sections.listLines.size() >= kMaxRenderListLines) {
            break;
        }
        auto line = lines[index];
        line.text = SanitizeRenderText(line.text, kMaxRenderTextChars);
        if (!line.text.empty()) {
            sections.listLines.push_back(std::move(line));
        }
    }

    const auto footerSource =
        textEntryActive
            ? SanitizeRenderText(promptLine, kMaxRenderTextChars)
            : (lines.size() > 1
                   ? SanitizeRenderText(lines.back().text, kMaxRenderTextChars)
                   : std::string{});

    if (StartsWith(footerSource, "PLAN VATSIM ")) {
        auto routeText = footerSource.substr(12);
        const auto distanceSuffix = routeText.rfind("nm");
        const auto splitPoint =
            distanceSuffix == std::string::npos ? std::string::npos : routeText.rfind(' ', distanceSuffix);
        if (splitPoint != std::string::npos) {
            sections.footerPrimary =
                SanitizeRenderText(
                    routeText.substr(0, splitPoint) + "   " +
                        routeText.substr(splitPoint + 1) + " remaining",
                    kMaxRenderTextChars);
        } else {
            sections.footerPrimary =
                SanitizeRenderText(routeText, kMaxRenderTextChars);
        }
    } else {
        sections.footerPrimary =
            SanitizeRenderText(footerSource, kMaxRenderTextChars);
    }

    return sections;
}

std::size_t HashCombine(std::size_t seed, const std::string& value) {
    return seed ^ (std::hash<std::string>{}(value) + 0x9e3779b9 + (seed << 6U) + (seed >> 2U));
}

std::size_t BuildRenderSignature(
    const OverlaySections& sections,
    int scrollOffset,
    bool textEntryActive,
    float animationTarget) {
    std::size_t signature = 0;
    signature = HashCombine(signature, sections.callsignOrStatus);
    signature = HashCombine(signature, sections.badgeText);
    signature = HashCombine(signature, sections.phaseChip);
    signature = HashCombine(signature, sections.headerRightText);
    signature = HashCombine(signature, sections.versionText);
    signature = HashCombine(signature, sections.versionAlternateText);
    signature ^= static_cast<std::size_t>(
        static_cast<int>(sections.versionTone) * 181);
    signature ^= static_cast<std::size_t>(
        sections.versionRotateAlternate ? 191 : 193);
    signature ^= static_cast<std::size_t>(
        sections.systemNotice.visible ? 197 : 199);
    signature ^= static_cast<std::size_t>(
        static_cast<int>(sections.systemNotice.severity) * 211);
    signature = HashCombine(signature, sections.systemNotice.title);
    signature = HashCombine(signature, sections.systemNotice.dismissText);
    for (const auto& line : sections.systemNotice.bodyLines) {
        signature = HashCombine(signature, line);
    }
    signature = HashCombine(signature, sections.footerPrimary);
    signature = HashCombine(signature, sections.footerSecondary);
    signature ^= static_cast<std::size_t>(sections.showMessageAcknowledge ? 151 : 0);
    signature ^= static_cast<std::size_t>(sections.showMessageRecall ? 157 : 0);
    signature = HashCombine(signature, sections.radioState.com1ActiveFrequency);
    signature = HashCombine(signature, sections.radioState.com2ActiveFrequency);
    signature ^= static_cast<std::size_t>(sections.radioState.standbyAssistEnabled ? 149 : 0);
    signature ^= static_cast<std::size_t>(sections.radioState.com1TxAvailable ? 101 : 0);
    signature ^= static_cast<std::size_t>(sections.radioState.com1RxAvailable ? 103 : 0);
    signature ^= static_cast<std::size_t>(sections.radioState.com2TxAvailable ? 107 : 0);
    signature ^= static_cast<std::size_t>(sections.radioState.com2RxAvailable ? 109 : 0);
    signature ^= static_cast<std::size_t>(sections.radioState.com1TxActive ? 113 : 0);
    signature ^= static_cast<std::size_t>(sections.radioState.com1RxActive ? 127 : 0);
    signature ^= static_cast<std::size_t>(sections.radioState.com2TxActive ? 131 : 0);
    signature ^= static_cast<std::size_t>(sections.radioState.com2RxActive ? 137 : 0);
    signature ^= static_cast<std::size_t>(sections.radioState.modeCActive ? 139 : 0);
    signature ^= static_cast<std::size_t>(scrollOffset + 37);
    signature ^= static_cast<std::size_t>(textEntryActive ? 131 : 17);
    signature ^= static_cast<std::size_t>(animationTarget > 0.0f ? 971 : 311);
    for (const auto& line : sections.listLines) {
        signature = HashCombine(signature, line.text);
        signature ^= static_cast<std::size_t>(static_cast<int>(line.tone) * 23);
    }
    return signature;
}

void PopulateRoundedRectPath(GraphicsPath* path, const RectF& rect, float radius) {
    const auto diameter = radius * 2.0f;
    path->AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
    path->AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270.0f, 90.0f);
    path->AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter, diameter, diameter, 0.0f, 90.0f);
    path->AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90.0f, 90.0f);
    path->CloseFigure();
}

void ConfigureGraphics(Graphics* graphics) {
    graphics->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics->SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
}

void DrawTextBlock(
    Graphics* graphics,
    const RectF& rect,
    const std::string& text,
    Font* font,
    const Color& color,
    Gdiplus::StringAlignment alignment = StringAlignmentNear) {
    if (text.empty()) {
        return;
    }

    SolidBrush brush(color);
    StringFormat format;
    format.SetAlignment(alignment);
    format.SetLineAlignment(StringAlignmentNear);
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    const auto wideText = ToWide(text);
    graphics->DrawString(
        wideText.c_str(),
        static_cast<INT>(wideText.size()),
        font,
        rect,
        &format,
        &brush);
}

void FillRoundedRect(
    Graphics* graphics,
    const RectF& rect,
    float radius,
    const Color& fillColor,
    const Color& borderColor) {
    GraphicsPath path(Gdiplus::FillModeAlternate);
    PopulateRoundedRectPath(&path, rect, radius);
    SolidBrush fillBrush(fillColor);
    Pen borderPen(borderColor, 1.0f);
    graphics->FillPath(&fillBrush, &path);
    graphics->DrawPath(&borderPen, &path);
}

void DrawStatusBox(
    Graphics* graphics,
    const RectF& rect,
    const std::string& label,
    Font* font,
    bool available,
    bool active) {
    const Color disabledFill(30, 88, 96, 106);
    const Color disabledBorder(46, 110, 118, 126);
    const Color disabledText(108, 134, 142, 150);
    const Color readyFill(46, 28, 36, 46);
    const Color readyBorder(82, 174, 190, 205);
    const Color readyText(228, 236, 242, 246);
    const Color activeFill(245, 48, 186, 108);
    const Color activeBorder(255, 120, 244, 184);
    const Color activeText(255, 234, 255, 242);

    const auto fillColor = active ? activeFill : (!available ? disabledFill : readyFill);
    const auto borderColor = active ? activeBorder : (!available ? disabledBorder : readyBorder);
    const auto textColor = active ? activeText : (!available ? disabledText : readyText);

    FillRoundedRect(graphics, rect, 5.0f, fillColor, borderColor);
    SolidBrush brush(textColor);
    StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    const auto wideText = ToWide(label);
    graphics->DrawString(
        wideText.c_str(),
        static_cast<INT>(wideText.size()),
        font,
        rect,
        &format,
        &brush);
}

void DrawActionButton(
    Graphics* graphics,
    const RectF& rect,
    const std::string& label,
    Font* font) {
    DrawStatusBox(graphics, rect, label, font, true, false);
}

Color VersionToneColor(brain::OverlayVersionTone tone) {
    switch (tone) {
        case brain::OverlayVersionTone::Current:
            return Color(224, 172, 242, 205);
        case brain::OverlayVersionTone::UpdateAvailable:
            return Color(236, 247, 212, 130);
        case brain::OverlayVersionTone::Error:
            return Color(236, 255, 142, 142);
        case brain::OverlayVersionTone::Unknown:
        default:
            return Color(180, 186, 196, 206);
    }
}

Color NoticeAccentColor(brain::OverlayNoticeSeverity severity) {
    switch (severity) {
        case brain::OverlayNoticeSeverity::Success:
            return Color(228, 132, 238, 190);
        case brain::OverlayNoticeSeverity::Warning:
            return Color(238, 247, 212, 130);
        case brain::OverlayNoticeSeverity::Error:
            return Color(238, 255, 142, 142);
        case brain::OverlayNoticeSeverity::Info:
        default:
            return Color(228, 174, 218, 244);
    }
}

void DrawSystemNotice(
    Graphics* graphics,
    const RectF& panelRect,
    const brain::OverlayNoticeSnapshot& notice) {
    if (!notice.visible) {
        return;
    }

    Font noticeTitleFont(L"Segoe UI", 13.5f, FontStyleBold, UnitPixel);
    Font noticeBodyFont(L"Segoe UI", 11.5f, FontStyleRegular, UnitPixel);
    Font noticeButtonFont(L"Segoe UI", 10.0f, FontStyleBold, UnitPixel);

    const RectF noticeRect(
        28.0f,
        146.0f,
        panelRect.GetRight() - 56.0f,
        130.0f);
    const auto accent = NoticeAccentColor(notice.severity);
    const Color fill(232, 10, 16, 24);
    const Color bodyColor(232, 226, 235, 242);
    const Color titleColor(246, 244, 248, 252);

    FillRoundedRect(graphics, noticeRect, 8.0f, fill, accent);
    SolidBrush accentBrush(Color(88, accent.GetR(), accent.GetG(), accent.GetB()));
    graphics->FillRectangle(
        &accentBrush,
        noticeRect.X + 1.0f,
        noticeRect.Y + 1.0f,
        5.0f,
        noticeRect.Height - 2.0f);

    DrawTextBlock(
        graphics,
        RectF(44.0f, 156.0f, 274.0f, 18.0f),
        notice.title,
        &noticeTitleFont,
        titleColor);

    auto lineY = 184.0f;
    const auto maxBodyLines =
        std::min<std::size_t>(notice.bodyLines.size(), 4);
    for (std::size_t index = 0; index < maxBodyLines; ++index) {
        DrawTextBlock(
            graphics,
            RectF(44.0f, lineY, 268.0f, 16.0f),
            notice.bodyLines[index],
            &noticeBodyFont,
            bodyColor);
        lineY += 17.0f;
    }

    if (notice.dismissible) {
        DrawActionButton(
            graphics,
            RectF(300.0f, 250.0f, 58.0f, 22.0f),
            notice.dismissText.empty() ? "Close" : notice.dismissText,
            &noticeButtonFont);
    }
}

RasterImage CaptureBitmap(Bitmap* bitmap) {
    RasterImage image;
    image.width = bitmap->GetWidth();
    image.height = bitmap->GetHeight();
    image.pixels.resize(static_cast<std::size_t>(image.width * image.height * 4));

    Gdiplus::BitmapData bitmapData{};
    Rect lockRect(0, 0, image.width, image.height);
    bitmap->LockBits(&lockRect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmapData);
    for (int row = 0; row < image.height; ++row) {
        const auto* source = static_cast<const unsigned char*>(bitmapData.Scan0) + (bitmapData.Stride * row);
        auto* destination = image.pixels.data() + (static_cast<std::size_t>(row) * image.width * 4);
        std::copy(source, source + (image.width * 4), destination);
    }
    bitmap->UnlockBits(&bitmapData);
    return image;
}

RasterImage RenderCaseImage(bool automaticMode) {
    Bitmap bitmap(kCaseWidth, kCaseHeight, PixelFormat32bppARGB);
    Graphics graphics(&bitmap);
    ConfigureGraphics(&graphics);
    graphics.Clear(Color(0, 0, 0, 0));

    const RectF caseRect(0.0f, 0.0f, static_cast<float>(kCaseWidth - 1), static_cast<float>(kCaseHeight - 1));
    GraphicsPath casePath(Gdiplus::FillModeAlternate);
    PopulateRoundedRectPath(&casePath, caseRect, 8.5f);
    LinearGradientBrush caseBrush(
        PointF(0.0f, 0.0f),
        PointF(0.0f, static_cast<float>(kCaseHeight)),
        Color(220, 88, 102, 115),
        Color(205, 35, 44, 54));
    Pen caseBorder(Color(112, 174, 196, 210), 1.0f);
    SolidBrush slotBrush(Color(170, 16, 20, 28));
    Font modeFont(L"Segoe UI", 10.0f, FontStyleBold, UnitPixel);
    const Color modeColor(196, 84, 124, 188);

    graphics.FillPath(&caseBrush, &casePath);
    graphics.DrawPath(&caseBorder, &casePath);
    graphics.FillRectangle(&slotBrush, 36.0f, 6.5f, static_cast<float>(kCaseWidth - 72), 4.0f);
    if (automaticMode) {
        DrawTextBlock(
            &graphics,
            RectF(static_cast<float>(kCaseWidth - 22), 2.5f, 12.0f, 12.0f),
            "A",
            &modeFont,
            modeColor,
            Gdiplus::StringAlignmentFar);
    } else {
        DrawTextBlock(
            &graphics,
            RectF(10.0f, 2.5f, 12.0f, 12.0f),
            "M",
            &modeFont,
            modeColor);
    }

    return CaptureBitmap(&bitmap);
}

RasterImage RenderCardImage(
    const OverlaySections& sections,
    int scrollOffset) {
    Bitmap bitmap(kCardWidth, kCardHeight, PixelFormat32bppARGB);
    Graphics graphics(&bitmap);
    ConfigureGraphics(&graphics);
    graphics.Clear(Color(0, 0, 0, 0));

    const RectF shadowRect(12.0f, 12.0f, static_cast<float>(kCardWidth - 1), static_cast<float>(kCardHeight - 1));
    GraphicsPath shadowPath(Gdiplus::FillModeAlternate);
    PopulateRoundedRectPath(&shadowPath, shadowRect, 18.0f);
    SolidBrush shadowBrush(Color(46, 0, 0, 0));
    graphics.FillPath(&shadowBrush, &shadowPath);

    const RectF panelRect(0.0f, 0.0f, static_cast<float>(kCardWidth - 13), static_cast<float>(kCardHeight - 13));
    GraphicsPath panelPath(Gdiplus::FillModeAlternate);
    PopulateRoundedRectPath(&panelPath, panelRect, 18.0f);
    LinearGradientBrush panelBrush(
        PointF(panelRect.X, panelRect.Y),
        PointF(panelRect.GetRight(), panelRect.GetBottom()),
        Color(188, 26, 36, 48),
        Color(158, 9, 14, 22));
    Pen panelBorder(Color(102, 170, 194, 210), 1.0f);
    graphics.FillPath(&panelBrush, &panelPath);
    graphics.DrawPath(&panelBorder, &panelPath);

    LinearGradientBrush headerGlow(
        PointF(0.0f, 0.0f),
        PointF(0.0f, 40.0f),
        Color(54, 187, 223, 242),
        Color(0, 187, 223, 242));
    graphics.FillRectangle(&headerGlow, 1.0f, 1.0f, panelRect.Width - 2.0f, 40.0f);

    Font brandFont(L"Segoe UI", 17.0f, FontStyleBold, UnitPixel);
    Font badgeFont(L"Segoe UI", 8.5f, FontStyleBold, UnitPixel);
    Font metaFont(L"Segoe UI", 10.5f, FontStyleRegular, UnitPixel);
    Font versionFont(L"Segoe UI", 10.5f, FontStyleBold, UnitPixel);
    Font callsignFont(L"Segoe UI", 17.0f, FontStyleBold, UnitPixel);
    Font radioLabelFont(L"Segoe UI", 11.0f, FontStyleBold, UnitPixel);
    Font radioFrequencyFont(L"Segoe UI", 12.0f, FontStyleBold, UnitPixel);
    Font statusBoxFont(L"Segoe UI", 9.0f, FontStyleBold, UnitPixel);
    Font rowFont(L"Segoe UI", 15.5f, FontStyleBold, UnitPixel);
    Font routeFont(L"Segoe UI", 16.5f, FontStyleBold, UnitPixel);
    Font footerFont(L"Segoe UI", 12.0f, FontStyleRegular, UnitPixel);

    const Color white(238, 241, 246, 250);
    const Color muted(180, 186, 196, 206);
    const Color cyan(188, 222, 244);
    const Color green(172, 242, 205);
    const Color yellow(247, 212, 130);
    const Color softWhite(214, 223, 232, 240);
    const Color connectedFill(62, 76, 118, 120);
    const Color divider(54, 172, 190, 205);

    SolidBrush greenDot(Color(84, 74, 199, 139));
    SolidBrush cyanDot(Color(124, 164, 234, 244));
    SolidBrush yellowDot(Color(132, 247, 212, 130));
    graphics.FillEllipse(&greenDot, 18.0f, 20.0f, 11.0f, 11.0f);
    graphics.FillEllipse(&cyanDot, 20.0f, 100.0f, 6.0f, 6.0f);
    graphics.FillEllipse(&yellowDot, 20.0f, 237.0f, 6.0f, 6.0f);

    const RectF badgeRect(136.0f, 12.0f, 106.0f, 23.0f);
    GraphicsPath badgePath(Gdiplus::FillModeAlternate);
    PopulateRoundedRectPath(&badgePath, badgeRect, 7.0f);
    SolidBrush badgeBrush(connectedFill);
    graphics.FillPath(&badgeBrush, &badgePath);

    Pen dividerPen(divider, 1.0f);
    graphics.DrawLine(&dividerPen, 20.0f, 82.0f, panelRect.GetRight() - 20.0f, 82.0f);
    graphics.DrawLine(&dividerPen, 20.0f, 138.0f, panelRect.GetRight() - 20.0f, 138.0f);
    graphics.DrawLine(&dividerPen, 20.0f, 246.0f, panelRect.GetRight() - 20.0f, 246.0f);

    DrawTextBlock(&graphics, RectF(46.0f, 8.0f, 118.0f, 24.0f), "XVatsim", &brandFont, white);
    DrawTextBlock(&graphics, RectF(154.0f, 17.0f, 76.0f, 12.0f), sections.badgeText, &badgeFont, cyan);
    DrawTextBlock(&graphics, RectF(panelRect.GetRight() - 112.0f, 10.0f, 98.0f, 16.0f), sections.phaseChip, &metaFont, muted, Gdiplus::StringAlignmentFar);
    DrawTextBlock(
        &graphics,
        RectF(panelRect.GetRight() - 112.0f, 28.0f, 98.0f, 15.0f),
        sections.versionText,
        &versionFont,
        VersionToneColor(sections.versionTone),
        Gdiplus::StringAlignmentFar);

    DrawTextBlock(&graphics, RectF(20.0f, 48.0f, 226.0f, 22.0f), sections.callsignOrStatus, &callsignFont, cyan);
    DrawTextBlock(&graphics, RectF(panelRect.GetRight() - 68.0f, 51.0f, 52.0f, 16.0f), sections.headerRightText, &metaFont, muted, Gdiplus::StringAlignmentFar);

    const auto modeCColor = sections.radioState.modeCActive ? green : white;
    DrawTextBlock(
        &graphics,
        RectF(46.0f, 88.0f, 176.0f, 14.0f),
        sections.radioState.modeCActive ? "MODE C *Active*" : "MODE C",
        &radioLabelFont,
        modeCColor);
    DrawTextBlock(
        &graphics,
        RectF(panelRect.GetRight() - 118.0f, 88.0f, 94.0f, 14.0f),
        sections.radioState.standbyAssistEnabled ? "ASST ON" : "ASST OFF",
        &radioLabelFont,
        sections.radioState.standbyAssistEnabled ? green : yellow,
        Gdiplus::StringAlignmentFar);

    DrawTextBlock(&graphics, RectF(46.0f, 104.0f, 48.0f, 16.0f), "COM1", &radioLabelFont, softWhite);
    DrawTextBlock(
        &graphics,
        RectF(94.0f, 103.0f, 126.0f, 18.0f),
        sections.radioState.com1ActiveFrequency.empty() ? "---.---" : sections.radioState.com1ActiveFrequency,
        &radioFrequencyFont,
        sections.radioState.com1Powered ? white : muted);
    DrawStatusBox(
        &graphics,
        RectF(250.0f, 101.0f, 38.0f, 18.0f),
        "TX",
        &statusBoxFont,
        sections.radioState.com1TxAvailable,
        sections.radioState.com1TxActive);
    DrawStatusBox(
        &graphics,
        RectF(294.0f, 101.0f, 38.0f, 18.0f),
        "RX",
        &statusBoxFont,
        sections.radioState.com1RxAvailable,
        sections.radioState.com1RxActive);

    DrawTextBlock(&graphics, RectF(46.0f, 122.0f, 48.0f, 16.0f), "COM2", &radioLabelFont, softWhite);
    DrawTextBlock(
        &graphics,
        RectF(94.0f, 121.0f, 126.0f, 18.0f),
        sections.radioState.com2ActiveFrequency.empty() ? "---.---" : sections.radioState.com2ActiveFrequency,
        &radioFrequencyFont,
        sections.radioState.com2Powered ? white : muted);
    DrawStatusBox(
        &graphics,
        RectF(250.0f, 119.0f, 38.0f, 18.0f),
        "TX",
        &statusBoxFont,
        sections.radioState.com2TxAvailable,
        sections.radioState.com2TxActive);
    DrawStatusBox(
        &graphics,
        RectF(294.0f, 119.0f, 38.0f, 18.0f),
        "RX",
        &statusBoxFont,
        sections.radioState.com2RxAvailable,
        sections.radioState.com2RxActive);

    const auto firstVisible = std::clamp(
        scrollOffset,
        0,
        std::max(0, static_cast<int>(sections.listLines.size()) - kVisibleListRows));
    const auto lastVisible = std::min(static_cast<int>(sections.listLines.size()), firstVisible + kVisibleListRows);

    auto rowY = 150.0f;
    for (auto index = firstVisible; index < lastVisible; ++index) {
        const auto& line = sections.listLines[static_cast<std::size_t>(index)];
        Color toneColor = white;
        if (line.tone == brain::OverlayTone::Active) {
            toneColor = green;
        } else if (line.tone == brain::OverlayTone::Next) {
            toneColor = yellow;
        }
        DrawTextBlock(&graphics, RectF(46.0f, rowY, 308.0f, 20.0f), line.text, &rowFont, toneColor);
        rowY += 22.0f;
    }

    if (firstVisible > 0) {
        DrawTextBlock(&graphics, RectF(panelRect.GetRight() - 90.0f, 142.0f, 76.0f, 16.0f), "more above", &metaFont, muted, Gdiplus::StringAlignmentFar);
    }
    if (lastVisible < static_cast<int>(sections.listLines.size())) {
        DrawTextBlock(&graphics, RectF(panelRect.GetRight() - 90.0f, 228.0f, 76.0f, 16.0f), "more below", &metaFont, muted, Gdiplus::StringAlignmentFar);
    }

    const auto routeFooterWidth =
        sections.showMessageAcknowledge ? 246.0f :
        (sections.showMessageRecall ? 220.0f : 304.0f);
    DrawTextBlock(&graphics, RectF(46.0f, 258.0f, routeFooterWidth, 22.0f), sections.footerPrimary, &routeFont, cyan);

    if (sections.showMessageAcknowledge) {
        DrawActionButton(&graphics, RectF(304.0f, 257.0f, 54.0f, 22.0f), "ACK", &footerFont);
    } else if (sections.showMessageRecall) {
        DrawActionButton(&graphics, RectF(276.0f, 257.0f, 82.0f, 22.0f), "RECALL", &footerFont);
    }

    DrawSystemNotice(&graphics, panelRect, sections.systemNotice);

    return CaptureBitmap(&bitmap);
}

std::wstring AccessoryUtf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const auto required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), wide.data(), required) != required) {
        return {};
    }
    return wide;
}

void DrawAccessoryText(
    Graphics* graphics,
    const RectF& rect,
    const std::string& text,
    Font* font,
    const Color& color,
    Gdiplus::StringAlignment alignment = StringAlignmentNear) {
    if (graphics == nullptr || font == nullptr || text.empty()) {
        return;
    }
    const auto wide = AccessoryUtf8ToWide(text);
    if (wide.empty()) {
        return;
    }
    SolidBrush brush(color);
    StringFormat format;
    format.SetAlignment(alignment);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    graphics->DrawString(
        wide.c_str(), static_cast<INT>(wide.size()), font, rect, &format, &brush);
}

RasterImage RenderAccessoryRailImage(
    const AccessoryLayoutResult& layout,
    const brain::BrainOwnedAccessoryPresentationSnapshot& snapshot) {
    const auto width = std::max(1, layout.railBounds.right - layout.railBounds.left);
    const auto height = std::max(1, layout.railBounds.top - layout.railBounds.bottom);
    Bitmap bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(&bitmap);
    ConfigureGraphics(&graphics);
    graphics.Clear(Color(0, 0, 0, 0));

    Font labelFont(
        L"Segoe UI", std::max(1.0f, 8.0f * layout.scale),
        FontStyleBold, UnitPixel);
    Font indicatorFont(
        L"Segoe UI", std::max(1.0f, 6.5f * layout.scale),
        FontStyleBold, UnitPixel);
    Font metarDetailFont(
        L"Segoe UI", std::max(1.0f, 10.0f * layout.scale),
        FontStyleBold, UnitPixel);
    const Color neutralFill(226, 39, 50, 62);
    const Color neutralBorder(224, 122, 146, 164);
    const Color selectedFill(238, 48, 67, 82);
    const Color selectedBorder(255, 234, 244, 250);
    const Color labelColor(246, 236, 242, 246);
    const Color indicatorColor(255, 250, 222, 138);

    for (std::size_t index = 0;
         index < layout.orbs.size() && index < snapshot.orbs.size();
         ++index) {
        const auto& orb = layout.orbs[index];
        const auto& presentation = snapshot.orbs[index];
        const auto left = static_cast<float>(orb.bounds.left - layout.railBounds.left);
        const auto top = static_cast<float>(layout.railBounds.top - orb.bounds.top);
        const auto diameter = static_cast<float>(orb.diameterPhysicalPixels);
        const RectF circle(left + 1.0f, top + 1.0f,
                           std::max(1.0f, diameter - 2.0f),
                           std::max(1.0f, diameter - 2.0f));
        Color toneFill = neutralFill;
        Color toneBorder = neutralBorder;
        using Tone = brain::BrainOwnedAccessoryOrbPresentation::Tone;
        switch (presentation.tone) {
            case Tone::Green:
                toneFill = Color(235, 35, 112, 67);
                toneBorder = Color(255, 95, 224, 132);
                break;
            case Tone::Blue:
                toneFill = Color(235, 37, 83, 148);
                toneBorder = Color(255, 91, 163, 255);
                break;
            case Tone::Red:
                toneFill = Color(235, 142, 43, 50);
                toneBorder = Color(255, 255, 103, 111);
                break;
            case Tone::Magenta:
                toneFill = Color(235, 122, 42, 139);
                toneBorder = Color(255, 235, 105, 255);
                break;
            case Tone::Gray:
                toneFill = Color(226, 55, 61, 68);
                toneBorder = Color(236, 145, 153, 160);
                break;
            case Tone::Neutral:
            default:
                break;
        }
        if (presentation.tone == Tone::Neutral && presentation.selected) {
            toneFill = selectedFill;
            toneBorder = selectedBorder;
        }
        SolidBrush fill(toneFill);
        Pen border(
            presentation.selected ? selectedBorder : toneBorder,
            presentation.selected ? std::max(2.0f, 2.0f * layout.scale) : 1.0f);
        graphics.FillEllipse(&fill, circle);
        graphics.DrawEllipse(&border, circle);

        const bool detailedMetar =
            presentation.drawer == brain::BrainOwnedAccessoryDrawerId::Metar &&
            !presentation.airportIcao.empty() &&
            !presentation.categoryText.empty();
        const auto labelHeight = std::max(8.0f, 11.0f * layout.scale);
        if (detailedMetar) {
            DrawAccessoryText(
                &graphics,
                RectF(left + 2.0f * layout.scale, top + diameter * 0.20f,
                      diameter - 4.0f * layout.scale,
                      std::max(10.0f, 13.0f * layout.scale)),
                presentation.airportIcao,
                &metarDetailFont, labelColor, StringAlignmentCenter);
            DrawAccessoryText(
                &graphics,
                RectF(left + 2.0f * layout.scale, top + diameter * 0.52f,
                      diameter - 4.0f * layout.scale,
                      std::max(10.0f, 13.0f * layout.scale)),
                presentation.categoryText,
                &metarDetailFont, labelColor, StringAlignmentCenter);
        } else {
            const bool neutralMetar =
                presentation.drawer ==
                    brain::BrainOwnedAccessoryDrawerId::Metar;
            const auto labelTop = top + (neutralMetar
                ? diameter * 0.40f
                : presentation.selected
                    ? diameter * 0.25f : diameter * 0.35f);
            DrawAccessoryText(
                &graphics,
                RectF(left, labelTop, diameter, labelHeight),
                presentation.label,
                &labelFont,
                labelColor,
                StringAlignmentCenter);
            if (!neutralMetar && presentation.selected &&
                !presentation.selectedIndicator.empty()) {
                DrawAccessoryText(
                    &graphics,
                    RectF(left, top + diameter * 0.56f, diameter,
                          std::max(8.0f, 11.0f * layout.scale)),
                    presentation.selectedIndicator,
                    &indicatorFont,
                    indicatorColor,
                    StringAlignmentCenter);
            }
        }
    }
    return CaptureBitmap(&bitmap);
}

RasterImage RenderAccessoryDrawerImage(
    const AccessoryLayoutResult& layout,
    const AccessoryPresentationState& state) {
    const auto width = std::max(1, layout.drawerBounds.right - layout.drawerBounds.left);
    const auto height = std::max(1, layout.drawerBounds.top - layout.drawerBounds.bottom);
    Bitmap bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(&bitmap);
    ConfigureGraphics(&graphics);
    graphics.Clear(Color(0, 0, 0, 0));

    const RectF panel(1.0f, 1.0f, static_cast<float>(width - 2),
                      static_cast<float>(height - 2));
    FillRoundedRect(
        &graphics, panel, std::max(7.0f, 10.0f * layout.scale),
        Color(242, 31, 43, 54), Color(226, 104, 132, 151));

    if (state.activeSnapshot == nullptr) {
        return CaptureBitmap(&bitmap);
    }
    const auto& snapshot = *state.activeSnapshot;
    const auto renderPlan = BuildAccessoryDrawerRenderPlan(state, layout);
    Font titleFont(
        L"Segoe UI", std::max(1.0f, 11.5f * layout.scale),
        FontStyleBold, UnitPixel);
    Font bodyFont(
        L"Segoe UI", std::max(1.0f, 11.5f * layout.scale),
        FontStyleRegular, UnitPixel);
    Font markerFont(
        L"Segoe UI", std::max(1.0f, 9.0f * layout.scale),
        FontStyleBold, UnitPixel);
    const auto inset = static_cast<float>(layout.drawerContentInset);
    const auto titleHeight = static_cast<float>(layout.drawerHeaderHeight);
    DrawAccessoryText(
        &graphics,
        RectF(inset, static_cast<float>(layout.drawerHeaderTop),
              static_cast<float>(width) - (inset * 2.0f), titleHeight),
        snapshot.drawerTitle,
        &titleFont,
        Color(255, 230, 240, 247));

    const auto lineHeight = static_cast<float>(layout.drawerLineHeight);
    auto y = static_cast<float>(layout.drawerContentTop);
    for (const auto& line : renderPlan.visibleLines) {
        DrawAccessoryText(
            &graphics,
            RectF(inset, y, static_cast<float>(width) - (inset * 2.0f), lineHeight),
            line.text,
            line.finalMarker ? &markerFont : (line.title ? &titleFont : &bodyFont),
            line.finalMarker ? Color(238, 245, 207, 118) :
                (line.title
                    ? Color(245, 210, 228, 238)
                    : Color(232, 178, 197, 209)));
        y += lineHeight;
    }
    return CaptureBitmap(&bitmap);
}

#if defined(XVATSIM_STEP3_OFFLINE_VISUAL_PROOF)

}  // namespace

OfflineRasterImage RenderProductionMainCardForOfflineProof(
    const brain::OverlayViewModel& viewModel,
    int scrollOffset) {
    auto image = RenderCardImage(
        ResolveSections(viewModel, false, {}), scrollOffset);
    return {image.width, image.height, std::move(image.pixels)};
}

std::size_t BuildProductionMainCardSignatureForOfflineProof(
    const brain::OverlayViewModel& viewModel,
    int scrollOffset) {
    const auto sections = ResolveSections(viewModel, false, {});
    return BuildRenderSignature(sections, scrollOffset, false, 1.0f);
}

OfflineRasterImage RenderProductionAccessoryRailForOfflineProof(
    const AccessoryLayoutResult& layout,
    const brain::BrainOwnedAccessoryPresentationSnapshot& snapshot) {
    auto image = RenderAccessoryRailImage(layout, snapshot);
    return {image.width, image.height, std::move(image.pixels)};
}

OfflineRasterImage RenderProductionAccessoryDrawerForOfflineProof(
    const AccessoryLayoutResult& layout,
    const AccessoryPresentationState& state) {
    auto image = RenderAccessoryDrawerImage(layout, state);
    return {image.width, image.height, std::move(image.pixels)};
}

#else

void EnsureTexture(unsigned int* textureId) {
    if (*textureId != 0U) {
        return;
    }

    int generatedId = 0;
    XPLMGenerateTextureNumbers(&generatedId, 1);
    *textureId = static_cast<unsigned int>(generatedId);
}

void UploadTexture(unsigned int* textureId, const RasterImage& image) {
    EnsureTexture(textureId);
    XPLMBindTexture2d(static_cast<int>(*textureId), 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        image.width,
        image.height,
        0,
        GL_BGRA,
        GL_UNSIGNED_BYTE,
        image.pixels.data());
}

void DrawSolidQuad(int left, int top, int right, int bottom, float red, float green, float blue, float alpha) {
    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    glColor4f(red, green, blue, alpha);
    glBegin(GL_QUADS);
    glVertex2i(left, top);
    glVertex2i(right, top);
    glVertex2i(right, bottom);
    glVertex2i(left, bottom);
    glEnd();
}

void DrawTexturedQuad(
    unsigned int textureId,
    int left,
    int top,
    int right,
    int bottom,
    float textureTop,
    float textureBottom,
    float alpha) {
    if (textureId == 0U || right <= left || top <= bottom) {
        return;
    }

    XPLMSetGraphicsState(0, 1, 0, 0, 1, 0, 0);
    XPLMBindTexture2d(static_cast<int>(textureId), 0);
    glColor4f(1.0f, 1.0f, 1.0f, alpha);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, textureTop);
    glVertex2i(left, top);
    glTexCoord2f(1.0f, textureTop);
    glVertex2i(right, top);
    glTexCoord2f(1.0f, textureBottom);
    glVertex2i(right, bottom);
    glTexCoord2f(0.0f, textureBottom);
    glVertex2i(left, bottom);
    glEnd();
}

}  // namespace

std::string DescribeLastOverlayUpdateTiming() {
    const auto& timing = gLastOverlayUpdateTiming;
    std::ostringstream stream;
    stream << "createUs=" << timing.windowCreateUs
           << ",setVisUs=" << timing.setVisibleUs
           << ",frontUs=" << timing.bringFrontUs
           << ",bodyUs=" << timing.bodyUs
           << ",soundUs=" << timing.transitionSoundUs
           << ",otherUs=" << timing.otherUs
           << ",wc=" << (timing.windowCreateCalled ? 1 : 0)
           << ",sv=" << (timing.setVisibleCalled ? 1 : 0)
           << ",fc=" << (timing.bringFrontChecked ? 1 : 0)
           << ",fb=" << (timing.bringFrontCalled ? 1 : 0)
           << ",ft=" << (timing.bringFrontThrottled ? 1 : 0)
           << ",ss=" << (timing.transitionSoundSkipped ? 1 : 0);
    return stream.str();
}

void OverlayWindow::Create() {
    if (window_ != nullptr) {
        return;
    }

    int screenLeft = 0;
    int screenTop = 0;
    int screenRight = 0;
    int screenBottom = 0;
    XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);

    XPLMCreateWindow_t params{};
    params.structSize = sizeof(params);
    params.left = screenLeft + kOverlayMarginLeft;
    params.top = screenTop - kOverlayMarginTop;
    params.right = params.left + ScaleValue(kOverlayWidth, scale_);
    params.bottom = params.top - ScaleValue(kOverlayHeight, scale_);
    ClampTopLeftToScreen(
        params.right - params.left,
        params.top - params.bottom,
        &params.left,
        &params.top);
    params.right = params.left + ScaleValue(kOverlayWidth, scale_);
    params.bottom = params.top - ScaleValue(kOverlayHeight, scale_);
    params.visible = 0;
    params.drawWindowFunc = DrawWindowCallback;
    params.handleMouseClickFunc = HandleMouseClickCallback;
    params.handleKeyFunc = HandleKeyCallback;
    params.handleCursorFunc = HandleCursorCallback;
    params.handleMouseWheelFunc = HandleMouseWheelCallback;
    params.refcon = this;
    params.decorateAsFloatingWindow = xplm_WindowDecorationSelfDecorated;
    params.layer = xplm_WindowLayerFlightOverlay;
    params.handleRightClickFunc = HandleRightClickCallback;

    window_ = XPLMCreateWindowEx(&params);
    if (window_ == nullptr) {
        return;
    }
    XPLMSetWindowPositioningMode(window_, xplm_WindowPositionFree, -1);
    if (hasPendingWindowTopLeft_) {
        const auto width = params.right - params.left;
        const auto height = params.top - params.bottom;
        ClampTopLeftToScreen(width, height, &pendingWindowLeft_, &pendingWindowTop_);
        XPLMSetWindowGeometry(
            window_,
            pendingWindowLeft_,
            pendingWindowTop_,
            pendingWindowLeft_ + width,
            pendingWindowTop_ - height);
        hasPendingWindowTopLeft_ = false;
    }
    windowVisible_ = false;
    overlayEnabled_ = false;
    accessoryIntegrationCounters_ = {};
    accessoryPerformance_ = std::make_unique<AccessoryPerformanceCollector>();
    accessoryPerformance_->ResetForNewProcess(accessoryPerformanceEpoch_++);
    animationProgress_ = 0.0f;
    animationTarget_ = 0.0f;
    lastWakeState_ = false;
    animationLastTimestampSeconds_ = XPLMGetElapsedTime();

    Gdiplus::GdiplusStartupInput startupInput;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &startupInput, nullptr) == Gdiplus::Ok) {
        gdiplusToken_ = static_cast<std::uintptr_t>(token);
    }

    accessoryMeasurementContext_ = InitializeAccessoryTextMeasurement();
    accessoryTypography_ = PrepareAccessoryTypography(
        accessoryMeasurementContext_, scale_);
    StartAccessoryPreparation();

    caseTextureDirty_ = true;
    cardTextureDirty_ = true;
    accessoryRailTextureDirty_ = true;
    accessoryDrawerTextureDirty_ = true;
    accessoryRailRasterReason_ = AccessoryRasterReason::Initial;
    accessoryDrawerRasterReason_ = AccessoryRasterReason::Initial;
    accessoryDrawCallbackOrdinal_ = 0;
    accessoryLastDrawCallbackEnteredMicroseconds_ = 0;
    lastCardSignature_ = 0;
    RefreshAccessoryLayout(AccessoryAnchorOperation::Initialize);
}

void OverlayWindow::Destroy() {
    StopAccessoryPreparation();
    if (window_ != nullptr) {
        XPLMDestroyWindow(window_);
        window_ = nullptr;
    }

    CloseTransitionSoundAlias();

    if (caseTextureId_ != 0U || cardTextureId_ != 0U ||
        accessoryRailTextureId_ != 0U || accessoryDrawerTextureId_ != 0U) {
        GLuint textureIds[4] = {
            caseTextureId_, cardTextureId_, accessoryRailTextureId_,
            accessoryDrawerTextureId_};
        glDeleteTextures(4, textureIds);
        caseTextureId_ = 0;
        cardTextureId_ = 0;
        accessoryRailTextureId_ = 0;
        accessoryDrawerTextureId_ = 0;
    }

    if (accessoryMeasurementContext_ != nullptr) {
        ShutdownAccessoryTextMeasurement(accessoryMeasurementContext_);
        accessoryMeasurementContext_ = nullptr;
    }

    if (gdiplusToken_ != 0U) {
        Gdiplus::GdiplusShutdown(static_cast<ULONG_PTR>(gdiplusToken_));
        gdiplusToken_ = 0;
    }

    windowVisible_ = false;
    overlayEnabled_ = false;
    accessoryPresentation_ = {};
    accessoryUpdateInput_ = {};
    accessoryPresentationHandle_ = {};
    accessoryLayout_ = {};
    accessoryDrawerOpen_ = false;
    accessoryAnchorState_ = {};
    dragMoved_ = false;
    accessoryClickQueue_ = {};
    accessoryInputDispatcher_ = {};
    accessoryPerformance_.reset();
    accessoryDrawCallbackOrdinal_ = 0;
    accessoryLastDrawCallbackEnteredMicroseconds_ = 0;
}

AccessoryPreparationKey OverlayWindow::BuildAccessoryPreparationKey(
    brain::BrainOwnedAccessoryDrawerId drawer,
    std::uint64_t historyGeneration,
    std::uint64_t contentGeneration) const {
    AccessoryPreparationKey key;
    key.drawer = drawer;
    key.historyGeneration = historyGeneration;
    key.contentGeneration = contentGeneration;
    key.layoutGeneration = accessoryLayoutGeneration_;
    key.typographyGeneration = accessoryTypography_.generation;
    key.scaleThousandths = static_cast<int>(std::lround(scale_ * 1000.0f));
    key.contentWidth = std::max(1,
        accessoryLayout_.drawerBounds.right - accessoryLayout_.drawerBounds.left -
            (2 * accessoryLayout_.drawerContentInset));
    key.visibleLineCapacity = accessoryLayout_.drawerVisibleLineCapacity;
    return key;
}

void OverlayWindow::StartAccessoryPreparation() {
    auto state = accessoryPreparationWorker_.State();
    if (state == AccessoryPreparationWorkerState::Stopped) {
        accessoryPreparationWorker_.Start(GetCurrentThreadId());
        accessoryPreparationRequested_.fill(false);
        state = accessoryPreparationWorker_.State();
    }
    if (state == AccessoryPreparationWorkerState::Failed &&
        !accessoryPreparationFailureDiagnosticEmitted_ &&
        accessoryPreparationFailureCallback_ != nullptr) {
        accessoryPreparationFailureDiagnosticEmitted_ = true;
        accessoryPreparationFailureCallback_(
            accessoryPreparationWorker_.Failure(),
            accessoryPreparationFailureRefcon_);
    }
}

void OverlayWindow::StopAccessoryPreparation() {
    ClearDeferredAccessoryInputBinding(true);
    accessoryPreparationWorker_.Stop();
    accessoryPreparationFailureDiagnosticEmitted_ = false;
    accessoryPreparationRequested_.fill(false);
    accessoryPendingPreparationRequests_ = {};
    accessoryUpdateInput_.preparedPlan.reset();
    accessoryPreparationWaitStartedMicroseconds_ = 0;
}

void OverlayWindow::ClearDeferredAccessoryInputBinding(bool cancelInFlight) {
    if (accessoryDeferredBindingPending_ && cancelInFlight) {
        CancelAccessoryInputDispatch(
            accessoryDeferredBindingFact_.requestSequence);
    }
    accessoryDeferredBindingPending_ = false;
    accessoryDeferredBindingFact_ = {};
    accessoryDeferredBindingAction_ =
        brain::BrainOwnedAccessoryDrawerAction::None;
    accessoryPreparationWaitStartedMicroseconds_ = 0;
}

void OverlayWindow::NotifyNextAccessoryInputIfPending() {
    if (accessoryClickQueue_.PendingCount() == 0 ||
        accessoryInputDispatchCallback_ == nullptr) {
        return;
    }
    accessoryInputDispatcher_.RecordDispatchNotification();
    accessoryInputDispatchCallback_(accessoryInputDispatchRefcon_);
}

void OverlayWindow::SetAccessoryPreparationFailureCallback(
    OverlayAccessoryPreparationFailureCallback callback,
    void* refcon) {
    accessoryPreparationFailureCallback_ = callback;
    accessoryPreparationFailureRefcon_ = refcon;
}

AccessoryPreparationWorkerCounters
OverlayWindow::GetAccessoryPreparationCounters() const {
    return accessoryPreparationWorker_.SnapshotCounters();
}

void OverlayWindow::QueueAccessoryPreparation(
    const brain::BrainOwnedAccessoryPreparationHandle& preparation) {
    if (preparation.snapshot == nullptr ||
        preparation.snapshot->status !=
            brain::BrainOwnedAccessoryOperationStatus::Available ||
        preparation.snapshot->drawer ==
            brain::BrainOwnedAccessoryDrawerId::None) return;
    StartAccessoryPreparation();
    const auto workerState = accessoryPreparationWorker_.State();
    if (workerState != AccessoryPreparationWorkerState::Starting &&
        workerState != AccessoryPreparationWorkerState::Ready) return;
    const auto key = BuildAccessoryPreparationKey(
        preparation.snapshot->drawer, preparation.historyGeneration,
        preparation.contentGeneration);
    const auto drawerIndex = preparation.snapshot->drawer ==
            brain::BrainOwnedAccessoryDrawerId::Metar ? 0U :
        preparation.snapshot->drawer == brain::BrainOwnedAccessoryDrawerId::Atis
            ? 1U : 2U;
    if (accessoryPreparationRequested_[drawerIndex] &&
        accessoryRequestedPreparationKeys_[drawerIndex] == key) return;
    AccessoryPreparationRequest request;
    request.key = key;
    request.snapshot = preparation.snapshot;
    request.layout = accessoryLayout_;
    if (accessoryPreparationWorker_.Request(request)) {
        accessoryPreparationRequested_[drawerIndex] = true;
        accessoryRequestedPreparationKeys_[drawerIndex] = key;
        accessoryPendingPreparationRequests_[drawerIndex].reset();
    } else if (accessoryPreparationWorker_.State() ==
                   AccessoryPreparationWorkerState::Starting ||
               accessoryPreparationWorker_.State() ==
                   AccessoryPreparationWorkerState::Ready) {
        accessoryPendingPreparationRequests_[drawerIndex] = std::move(request);
    }
}

void OverlayWindow::RetryPendingAccessoryPreparations() {
    for (std::size_t index = 0;
         index < accessoryPendingPreparationRequests_.size(); ++index) {
        auto& pending = accessoryPendingPreparationRequests_[index];
        if (!pending.has_value()) continue;
        if (!accessoryPreparationWorker_.Request(*pending)) continue;
        accessoryPreparationRequested_[index] = true;
        accessoryRequestedPreparationKeys_[index] = pending->key;
        pending.reset();
    }
}

bool OverlayWindow::PublishReadyAccessoryPreparation() {
    StartAccessoryPreparation();
    const auto preparationAvailability =
        ResolveAccessoryPreparationAvailability(
            accessoryPreparationWorker_.State(),
            accessoryDeferredBindingPending_);
    if (preparationAvailability.cancelPendingAction) {
        ClearDeferredAccessoryInputBinding(true);
        NotifyNextAccessoryInputIfPending();
        accessoryUpdateInput_.preparedPlan.reset();
        return false;
    }
    RetryPendingAccessoryPreparations();
    const auto& pending = accessoryUpdateInput_.presentation;
    if (pending.snapshot == nullptr || pending.snapshot->activeDrawer ==
        brain::BrainOwnedAccessoryDrawerId::None) return false;
    if (accessoryPresentation_.selectionGeneration ==
            pending.selectionGeneration &&
        accessoryPresentation_.historyGeneration == pending.historyGeneration &&
        accessoryPresentation_.contentGeneration == pending.contentGeneration &&
        accessoryPresentation_.layoutGeneration == pending.layoutGeneration) {
        return false;
    }
    const auto key = BuildAccessoryPreparationKey(
        pending.snapshot->activeDrawer, pending.historyGeneration,
        pending.contentGeneration);
    std::uint64_t publicationUs = 0;
    const auto ready = accessoryPreparationWorker_.TryTakeReady(
        key, &publicationUs);
    if (ready == nullptr) return false;
    if (!accessoryDrawerOpen_) {
        ApplyAccessoryWindowGeometry(true);
        accessoryUpdateInput_.layout = accessoryLayout_;
    }
    accessoryUpdateInput_.preparedPlan = ready;
    const auto update = UpdateAccessoryPresentation(
        &accessoryPresentation_, accessoryUpdateInput_);
    if (update.preparationPending || update.status !=
        brain::BrainOwnedAccessoryOperationStatus::Available) return false;
    if (update.delta.railRasterRequests > 0)
        MarkAccessoryRailTextureDirty(AccessoryRasterReason::Selection);
    if (update.delta.drawerRasterRequests > 0)
        MarkAccessoryDrawerTextureDirty(AccessoryRasterReason::Selection);
    if (update.delta.railRasterRequests > 0 ||
        update.delta.drawerRasterRequests > 0) {
        ++accessoryRenderGeneration_;
        if (accessoryRenderGeneration_ == 0) accessoryRenderGeneration_ = 1;
    }
    accessoryPresentationHandle_ = pending;
    if (accessoryDeferredBindingPending_) {
        const auto fact = accessoryDeferredBindingFact_;
        const auto action = accessoryDeferredBindingAction_;
        ClearDeferredAccessoryInputBinding(false);
        if (!BindAccessoryInputDispatch(fact, action)) {
            CancelAccessoryInputDispatch(fact.requestSequence);
            NotifyNextAccessoryInputIfPending();
        }
    }
    return true;
}

void OverlayWindow::Update(const brain::OverlayViewModel& viewModel) {
    ResetOverlayUpdateTiming();
    const auto totalStarted = OverlayClock::now();
    if (window_ == nullptr) {
        const auto createStarted = OverlayClock::now();
        Create();
        gLastOverlayUpdateTiming.windowCreateCalled = true;
        gLastOverlayUpdateTiming.windowCreateUs +=
            ElapsedOverlayUsSince(createStarted);
    }
    const auto bodyStarted = OverlayClock::now();
    viewModel_ = viewModel;
    overlayEnabled_ = true;
    ClampScrollOffset();
    gLastOverlayUpdateTiming.bodyUs += ElapsedOverlayUsSince(bodyStarted);
    SyncVisibility();
    gLastOverlayUpdateTiming.totalUs = ElapsedOverlayUsSince(totalStarted);
    const auto knownUs = KnownOverlayUpdateUs(gLastOverlayUpdateTiming);
    gLastOverlayUpdateTiming.otherUs =
        gLastOverlayUpdateTiming.totalUs > knownUs
            ? gLastOverlayUpdateTiming.totalUs - knownUs
            : 0;
}

void OverlayWindow::UpdateAccessory(
    const brain::BrainOwnedAccessoryPresentationHandle& presentation,
    AccessoryDispatchStageWallTimings* acceptedActionStages) {
    const auto acceptedActionUpdateStarted = acceptedActionStages != nullptr
        ? AccessoryWallClockMicroseconds()
        : 0;
    if (presentation.snapshot == nullptr ||
        presentation.snapshot->status !=
            brain::BrainOwnedAccessoryOperationStatus::Available) {
        return;
    }
    if (window_ == nullptr) {
        Create();
    }
    if (window_ == nullptr || accessoryMeasurementContext_ == nullptr) {
        return;
    }
    StartAccessoryPreparation();
    const auto preparationAvailability =
        ResolveAccessoryPreparationAvailability(
            accessoryPreparationWorker_.State(),
            accessoryDeferredBindingPending_);
    if (preparationAvailability.cancelPendingAction ||
        accessoryPreparationWorker_.State() ==
            AccessoryPreparationWorkerState::Failed) {
        accessoryPreparationWaitStartedMicroseconds_ = 0;
        accessoryUpdateInput_.preparedPlan.reset();
        if (accessoryDeferredBindingPending_) {
            CancelAccessoryInputDispatch(
                accessoryDeferredBindingFact_.requestSequence);
            accessoryDeferredBindingPending_ = false;
            accessoryDeferredBindingFact_ = {};
            accessoryDeferredBindingAction_ =
                brain::BrainOwnedAccessoryDrawerAction::None;
        }
        return;
    }

    const auto drawerOpen = presentation.snapshot->activeDrawer !=
        brain::BrainOwnedAccessoryDrawerId::None;
    const bool needsPreparedTransition = drawerOpen &&
        (accessoryPresentation_.selectionGeneration !=
             presentation.selectionGeneration ||
         accessoryPresentation_.historyGeneration !=
             presentation.historyGeneration ||
         accessoryPresentation_.contentGeneration !=
             presentation.contentGeneration ||
         accessoryPresentation_.layoutGeneration !=
             presentation.layoutGeneration);
    if (needsPreparedTransition) {
        const auto key = BuildAccessoryPreparationKey(
            presentation.snapshot->activeDrawer, presentation.historyGeneration,
            presentation.contentGeneration);
        std::uint64_t publicationUs = 0;
        const auto ready = accessoryPreparationWorker_.TryTakeReady(
            key, &publicationUs);
        if (ready == nullptr) {
            accessoryUpdateInput_.presentation = presentation;
            accessoryUpdateInput_.layout = accessoryLayout_;
            accessoryUpdateInput_.mainCardProductionSignature =
                mainCardProductionSignature_;
            accessoryUpdateInput_.measurementContext =
                accessoryMeasurementContext_;
            if (accessoryPreparationWaitStartedMicroseconds_ == 0)
                accessoryPreparationWaitStartedMicroseconds_ =
                    AccessoryWallClockMicroseconds();
            return;
        }
        accessoryUpdateInput_.preparedPlan = ready;
    }
    bool layoutInputsChanged = false;
    if (drawerOpen == accessoryDrawerOpen_ &&
        accessoryLayout_.status ==
            brain::BrainOwnedAccessoryOperationStatus::Available) {
        int screenLeft = 0;
        int screenTop = 0;
        int screenRight = 0;
        int screenBottom = 0;
        XPLMGetScreenBoundsGlobal(
            &screenLeft, &screenTop, &screenRight, &screenBottom);
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
        XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);
        layoutInputsChanged =
            screenLeft != accessoryLastScreenLeft_ ||
            screenTop != accessoryLastScreenTop_ ||
            screenRight != accessoryLastScreenRight_ ||
            screenBottom != accessoryLastScreenBottom_ ||
            left != accessoryLayout_.resolvedBounds.left ||
            top != accessoryLayout_.resolvedBounds.top ||
            right != accessoryLayout_.resolvedBounds.right ||
            bottom != accessoryLayout_.resolvedBounds.bottom;
    }
    if (drawerOpen != accessoryDrawerOpen_) {
        ApplyAccessoryWindowGeometry(drawerOpen);
        layoutInputsChanged = true;
    } else if (layoutInputsChanged ||
               accessoryLayout_.status !=
                   brain::BrainOwnedAccessoryOperationStatus::Available) {
        RefreshAccessoryLayout();
        if (layoutInputsChanged &&
            accessoryLayout_.status ==
                brain::BrainOwnedAccessoryOperationStatus::Available) {
            int currentLeft = 0;
            int currentTop = 0;
            int currentRight = 0;
            int currentBottom = 0;
            XPLMGetWindowGeometry(
                window_, &currentLeft, &currentTop,
                &currentRight, &currentBottom);
            const auto& bounds = accessoryLayout_.resolvedBounds;
            if (currentLeft != bounds.left || currentTop != bounds.top ||
                currentRight != bounds.right || currentBottom != bounds.bottom) {
                XPLMSetWindowGeometry(
                    window_, bounds.left, bounds.top,
                    bounds.right, bounds.bottom);
            }
        }
    }

    const bool selectionChanged =
        accessoryUpdateInput_.presentation.snapshot == nullptr ||
        accessoryUpdateInput_.presentation.selectionGeneration !=
            presentation.selectionGeneration;
    const bool historyChanged =
        accessoryUpdateInput_.presentation.snapshot == nullptr ||
        accessoryUpdateInput_.presentation.historyGeneration !=
            presentation.historyGeneration;
    const bool contentChanged =
        accessoryUpdateInput_.presentation.snapshot == nullptr ||
        accessoryUpdateInput_.presentation.contentGeneration !=
            presentation.contentGeneration;
    const bool presentationInputsChanged =
        accessoryUpdateInput_.presentation.snapshot == nullptr ||
        accessoryUpdateInput_.presentation.snapshot.get() !=
            presentation.snapshot.get() ||
        accessoryUpdateInput_.presentation.selectionGeneration !=
            presentation.selectionGeneration ||
        accessoryUpdateInput_.presentation.historyGeneration !=
            presentation.historyGeneration ||
        accessoryUpdateInput_.presentation.contentGeneration !=
            presentation.contentGeneration ||
        accessoryUpdateInput_.presentation.layoutGeneration !=
            presentation.layoutGeneration;
    if (presentationInputsChanged) {
        accessoryUpdateInput_.presentation = presentation;
    }
    const auto& cachedLayout = accessoryUpdateInput_.layout;
    const bool resolvedLayoutChanged =
        cachedLayout.status !=
            brain::BrainOwnedAccessoryOperationStatus::Available ||
        std::fabs(cachedLayout.scale - accessoryLayout_.scale) >= 0.001f ||
        cachedLayout.closedWidth != accessoryLayout_.closedWidth ||
        cachedLayout.closedHeight != accessoryLayout_.closedHeight ||
        cachedLayout.openWidth != accessoryLayout_.openWidth ||
        cachedLayout.openHeight != accessoryLayout_.openHeight ||
        cachedLayout.accessoriesVisible != accessoryLayout_.accessoriesVisible ||
        cachedLayout.accessoriesInteractive != accessoryLayout_.accessoriesInteractive ||
        cachedLayout.railBounds.right - cachedLayout.railBounds.left !=
            accessoryLayout_.railBounds.right - accessoryLayout_.railBounds.left ||
        cachedLayout.railBounds.top - cachedLayout.railBounds.bottom !=
            accessoryLayout_.railBounds.top - accessoryLayout_.railBounds.bottom ||
        cachedLayout.drawerBounds.right - cachedLayout.drawerBounds.left !=
            accessoryLayout_.drawerBounds.right - accessoryLayout_.drawerBounds.left ||
        cachedLayout.drawerBounds.top - cachedLayout.drawerBounds.bottom !=
            accessoryLayout_.drawerBounds.top - accessoryLayout_.drawerBounds.bottom ||
        cachedLayout.drawerContentInset != accessoryLayout_.drawerContentInset ||
        cachedLayout.drawerHeaderTop != accessoryLayout_.drawerHeaderTop ||
        cachedLayout.drawerHeaderHeight != accessoryLayout_.drawerHeaderHeight ||
        cachedLayout.drawerContentTop != accessoryLayout_.drawerContentTop ||
        cachedLayout.drawerContentBottom != accessoryLayout_.drawerContentBottom ||
        cachedLayout.drawerLineHeight != accessoryLayout_.drawerLineHeight ||
        cachedLayout.drawerVisibleLineCapacity !=
            accessoryLayout_.drawerVisibleLineCapacity;
    if (resolvedLayoutChanged) {
        accessoryUpdateInput_.layout = accessoryLayout_;
    }
    if (accessoryUpdateInput_.mainCardProductionSignature !=
        mainCardProductionSignature_) {
        accessoryUpdateInput_.mainCardProductionSignature =
            mainCardProductionSignature_;
    }
    accessoryUpdateInput_.measurementContext = accessoryMeasurementContext_;
    accessoryUpdateInput_.collectAcceptedActionStageTiming =
        acceptedActionStages != nullptr;
    if (drawerOpen) {
        const auto key = BuildAccessoryPreparationKey(
            presentation.snapshot->activeDrawer, presentation.historyGeneration,
            presentation.contentGeneration);
        if (accessoryPresentation_.selectionGeneration ==
                presentation.selectionGeneration &&
            accessoryPresentation_.historyGeneration ==
                presentation.historyGeneration &&
            accessoryPresentation_.contentGeneration ==
                presentation.contentGeneration &&
            accessoryPresentation_.layoutGeneration ==
                presentation.layoutGeneration) {
            accessoryUpdateInput_.preparedPlan =
                accessoryPresentation_.preparedPlan;
        } else {
            std::uint64_t publicationUs = 0;
            accessoryUpdateInput_.preparedPlan =
                accessoryPreparationWorker_.TryTakeReady(key, &publicationUs);
        }
        if (accessoryUpdateInput_.preparedPlan == nullptr) {
            if (accessoryPreparationWaitStartedMicroseconds_ == 0)
                accessoryPreparationWaitStartedMicroseconds_ =
                    AccessoryWallClockMicroseconds();
            return;
        }
    } else {
        accessoryUpdateInput_.preparedPlan.reset();
        accessoryPreparationWaitStartedMicroseconds_ = 0;
    }
    const auto update = UpdateAccessoryPresentation(
        &accessoryPresentation_, accessoryUpdateInput_);
    if (update.status != brain::BrainOwnedAccessoryOperationStatus::Available) {
        return;
    }
    if (update.preparationPending) return;
    accessoryPresentationHandle_ = presentation;
    if (update.delta.railRasterRequests > 0) {
        MarkAccessoryRailTextureDirty(
            selectionChanged
                ? AccessoryRasterReason::Selection
                : (historyChanged
                    ? AccessoryRasterReason::ContentGeneration
                    : AccessoryRasterReason::TypographyOrLayout));
    }
    if (update.delta.drawerRasterRequests > 0) {
        MarkAccessoryDrawerTextureDirty(
            selectionChanged
                ? AccessoryRasterReason::Selection
                : (historyChanged
                    ? AccessoryRasterReason::ContentGeneration
                    : AccessoryRasterReason::TypographyOrLayout));
    }
    if (update.delta.railRasterRequests > 0 ||
        update.delta.drawerRasterRequests > 0) {
        ++accessoryRenderGeneration_;
        if (accessoryRenderGeneration_ == 0) {
            accessoryRenderGeneration_ = 1;
        }
    }
    if (update.delta.historyVisits == 0 && update.delta.entryCopies == 0 &&
        update.delta.wrapVisits == 0 && update.delta.railRasterRequests == 0 &&
        update.delta.drawerRasterRequests == 0 && update.delta.uploadRequests == 0) {
        ++accessoryIntegrationCounters_.unchangedUpdates;
    }
    SyncVisibility();
    if (acceptedActionStages != nullptr) {
        const auto updateWall =
            AccessoryWallClockMicroseconds() - acceptedActionUpdateStarted;
        const auto gdiWall =
            update.gdiMeasurementWrappingWallMicroseconds;
        acceptedActionStages->elapsedMicroseconds[static_cast<std::size_t>(
            AccessoryDispatchStage::GdiMeasurementWrapping)] = gdiWall;
        acceptedActionStages->elapsedMicroseconds[static_cast<std::size_t>(
            AccessoryDispatchStage::OverlayLayoutPresentation)] =
                updateWall >= gdiWall ? updateWall - gdiWall : 0;
    }
}

void OverlayWindow::SetAccessoryInputDispatchCallback(
    OverlayAccessoryInputDispatchCallback callback,
    void* refcon) {
    accessoryInputDispatchCallback_ = callback;
    accessoryInputDispatchRefcon_ = refcon;
}

bool OverlayWindow::BeginAccessoryInputDispatch(
    OverlayAccessoryClickFact* outFact) {
    const auto began =
        accessoryInputDispatcher_.TryBegin(&accessoryClickQueue_, outFact);
    if (began && outFact != nullptr) {
        outFact->dispatchStartedMicroseconds = AccessoryWallClockMicroseconds();
    }
    return began;
}

bool OverlayWindow::BindAccessoryInputDispatch(
    const OverlayAccessoryClickFact& fact,
    brain::BrainOwnedAccessoryDrawerAction action) {
    const bool presentationPending =
        accessoryUpdateInput_.presentation.snapshot != nullptr &&
        (accessoryPresentation_.selectionGeneration !=
             accessoryUpdateInput_.presentation.selectionGeneration ||
         accessoryPresentation_.historyGeneration !=
             accessoryUpdateInput_.presentation.historyGeneration ||
         accessoryPresentation_.contentGeneration !=
             accessoryUpdateInput_.presentation.contentGeneration);
    if (presentationPending) {
        accessoryDeferredBindingPending_ = true;
        accessoryDeferredBindingFact_ = fact;
        accessoryDeferredBindingAction_ = action;
        return true;
    }
    const auto bindingStarted = AccessoryWallClockMicroseconds();
    if (!accessoryInputDispatcher_.BindPresentation(
            fact.requestSequence,
            action,
            accessoryPresentation_.selectionGeneration,
            accessoryRenderGeneration_)) {
        return false;
    }
    const auto dispatchCompletedMicroseconds = AccessoryWallClockMicroseconds();
    AccessoryActionDispatchTimingInput timing;
    timing.dispatchCompletedMicroseconds = dispatchCompletedMicroseconds;
    timing.dispatchStartedMicroseconds = fact.dispatchStartedMicroseconds;
    if (accessoryPreparationWaitStartedMicroseconds_ != 0 &&
        dispatchCompletedMicroseconds >= accessoryPreparationWaitStartedMicroseconds_) {
        timing.preparationWaitMicroseconds =
            dispatchCompletedMicroseconds - accessoryPreparationWaitStartedMicroseconds_;
        accessoryPreparationWaitStartedMicroseconds_ = 0;
    }
    timing.stages.elapsedMicroseconds = fact.dispatchStageWallMicroseconds;
    timing.stages.elapsedMicroseconds[static_cast<std::size_t>(
        AccessoryDispatchStage::GenerationBinding)] =
            dispatchCompletedMicroseconds - bindingStarted;
    return accessoryPerformance_ != nullptr &&
        accessoryPerformance_->BeginDrawerAction(
            action,
            fact.requestSequence,
            fact.startedMicroseconds,
            accessoryPresentation_.selectionGeneration,
            accessoryRenderGeneration_,
            dispatchCompletedMicroseconds,
            accessoryDrawCallbackOrdinal_ + 1,
            timing);
}

bool OverlayWindow::CancelAccessoryInputDispatch(
    std::uint64_t requestSequence) {
    return accessoryInputDispatcher_.CancelInFlight(
        requestSequence, AccessoryWallClockMicroseconds());
}

std::size_t OverlayWindow::DiscardPendingAccessoryClickFacts() {
    ClearDeferredAccessoryInputBinding(false);
    accessoryInputDispatcher_.InvalidateInFlight(
        AccessoryWallClockMicroseconds());
    if (accessoryPerformance_ != nullptr) {
        accessoryPerformance_->DiscardPendingActions();
    }
    return accessoryClickQueue_.DiscardPending();
}

bool OverlayWindow::ConsumeAccessoryPerformancePublication(
    AccessoryPerformanceSnapshot* outSnapshot) {
    return accessoryPerformance_ != nullptr &&
        accessoryPerformance_->BeginAggregatePublication(outSnapshot);
}

std::uint64_t OverlayWindow::GetAccessoryLayoutGeneration() const {
    return accessoryLayoutGeneration_;
}

OverlayAccessoryIntegrationCounters
OverlayWindow::GetAccessoryIntegrationCounters() const {
    auto counters = accessoryIntegrationCounters_;
    counters.clickFactsProduced = accessoryClickQueue_.ProducedCount();
    counters.clickFactsDropped = accessoryClickQueue_.DroppedCount();
    counters.clickFactsConsumed = accessoryClickQueue_.ConsumedCount();
    counters.clickFactsDiscarded = accessoryClickQueue_.DiscardedCount();
    counters.clickFactsPending = accessoryClickQueue_.PendingCount();
    counters.maximumClickQueueDepth = accessoryClickQueue_.MaximumDepth();
    counters.accessoryRenderGeneration = accessoryRenderGeneration_;
    counters.dispatch = accessoryInputDispatcher_.Snapshot();
    if (accessoryPerformance_ != nullptr) {
        counters.performance = accessoryPerformance_->Snapshot();
    }
    return counters;
}

void OverlayWindow::SetAutomaticMode(bool automaticMode) {
    if (automaticMode_ == automaticMode) {
        return;
    }

    automaticMode_ = automaticMode;
    caseTextureDirty_ = true;
}

void OverlayWindow::SetTransitionSoundPath(const std::string& transitionSoundPath) {
    if (transitionSoundPath_ == transitionSoundPath) {
        return;
    }

    transitionSoundPath_ = transitionSoundPath;
    transitionSoundLoaded_ = false;
    CloseTransitionSoundAlias();
}

void OverlayWindow::SetOpacity(float opacity) {
    opacity_ = std::clamp(opacity, 0.45f, 1.0f);
}

void OverlayWindow::SetScale(float scale) {
    ApplyScale(scale, false);
}

void OverlayWindow::SetAnimationSpeed(float speed) {
    animationSpeed_ = std::clamp(speed, 0.60f, 1.60f);
}

void OverlayWindow::BeginTextEntry(const std::string& initialText) {
    if (window_ == nullptr) {
        Create();
    }
    textEntryActive_ = true;
    textEntryBuffer_ = SanitizeRenderText(initialText, kMaxTextEntryChars);
    hasPendingSubmittedText_ = false;
    pendingSubmittedText_.clear();
    overlayEnabled_ = true;
    cardTextureDirty_ = true;

    if (window_ != nullptr) {
        XPLMBringWindowToFront(window_);
    }

    ClampScrollOffset();
    SyncVisibility();
}

void OverlayWindow::CancelTextEntry() {
    textEntryActive_ = false;
    textEntryBuffer_.clear();
    cardTextureDirty_ = true;

    if (window_ != nullptr) {
        XPLMTakeKeyboardFocus(nullptr);
    }

    ClampScrollOffset();
    SyncVisibility();
}

bool OverlayWindow::ConsumeSubmittedText(std::string* outText) {
    if (!hasPendingSubmittedText_) {
        return false;
    }

    if (outText != nullptr) {
        *outText = pendingSubmittedText_;
    }

    hasPendingSubmittedText_ = false;
    pendingSubmittedText_.clear();
    return true;
}

bool OverlayWindow::ConsumeAcknowledgeRequest() {
    if (!hasPendingAcknowledgeRequest_) {
        return false;
    }

    hasPendingAcknowledgeRequest_ = false;
    return true;
}

bool OverlayWindow::ConsumeRecallRequest() {
    if (!hasPendingRecallRequest_) {
        return false;
    }

    hasPendingRecallRequest_ = false;
    return true;
}

bool OverlayWindow::ConsumeSystemNoticeDismissRequest() {
    if (!hasPendingSystemNoticeDismissRequest_) {
        return false;
    }

    hasPendingSystemNoticeDismissRequest_ = false;
    return true;
}

void OverlayWindow::SetWindowTopLeft(int left, int top) {
    const auto pendingWidth = ScaleValue(kOverlayWidth, scale_);
    const auto pendingHeight = ScaleValue(
        accessoryDrawerOpen_ ? kOverlayOpenHeight : kOverlayHeight, scale_);
    ClampTopLeftToScreen(pendingWidth, pendingHeight, &left, &top);

    if (window_ == nullptr) {
        hasPendingWindowTopLeft_ = true;
        pendingWindowLeft_ = left;
        pendingWindowTop_ = top;
        return;
    }

    int currentLeft = 0;
    int currentTop = 0;
    int currentRight = 0;
    int currentBottom = 0;
    XPLMGetWindowGeometry(window_, &currentLeft, &currentTop, &currentRight, &currentBottom);

    const auto width = currentRight - currentLeft;
    const auto height = currentTop - currentBottom;
    ClampTopLeftToScreen(width, height, &left, &top);
    XPLMSetWindowGeometry(window_, left, top, left + width, top - height);
    RefreshAccessoryLayout(
        AccessoryAnchorOperation::Initialize,
        true,
        left,
        top);
    positionChanged_ = false;
}

bool OverlayWindow::GetWindowTopLeft(int* outLeft, int* outTop) const {
    if (window_ == nullptr || outLeft == nullptr || outTop == nullptr) {
        return false;
    }

    if (accessoryAnchorState_.initialized &&
        accessoryAnchorState_.currentAnchorValid) {
        *outLeft = accessoryAnchorState_.currentAnchorLeft;
        *outTop = accessoryAnchorState_.currentAnchorTop;
        return true;
    }

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);
    *outLeft = left;
    *outTop = top;
    return true;
}

bool OverlayWindow::ConsumePositionChanged(int* outLeft, int* outTop) {
    if (!positionChanged_) {
        return false;
    }

    positionChanged_ = false;
    return GetWindowTopLeft(outLeft, outTop);
}

bool OverlayWindow::ConsumeScaleChanged(float* outScale) {
    if (!scaleChanged_) {
        return false;
    }

    scaleChanged_ = false;
    if (outScale != nullptr) {
        *outScale = scale_;
    }
    return true;
}

void OverlayWindow::Hide() {
    DiscardPendingAccessoryClickFacts();
    StopAccessoryPreparation();
    ResetOverlayUpdateTiming();
    const auto totalStarted = OverlayClock::now();
    const auto bodyStarted = OverlayClock::now();
    viewModel_ = {};
    textEntryActive_ = false;
    textEntryBuffer_.clear();
    pendingSubmittedText_.clear();
    hasPendingSubmittedText_ = false;
    hasPendingAcknowledgeRequest_ = false;
    hasPendingRecallRequest_ = false;
    hasPendingSystemNoticeDismissRequest_ = false;
    overlayEnabled_ = false;
    dragging_ = false;
    resizing_ = false;
    activeResizeCorner_ = ResizeCorner::None;
    animationProgress_ = 0.0f;
    animationTarget_ = 0.0f;
    lastWakeState_ = false;
    scrollOffset_ = 0;
    cardTextureDirty_ = true;
    gLastOverlayUpdateTiming.bodyUs += ElapsedOverlayUsSince(bodyStarted);
    if (window_ != nullptr) {
        const auto visibleStarted = OverlayClock::now();
        XPLMSetWindowIsVisible(window_, 0);
        gLastOverlayUpdateTiming.setVisibleCalled = true;
        gLastOverlayUpdateTiming.setVisibleUs +=
            ElapsedOverlayUsSince(visibleStarted);
        windowVisible_ = false;
    }
    gLastOverlayUpdateTiming.totalUs = ElapsedOverlayUsSince(totalStarted);
    const auto knownUs = KnownOverlayUpdateUs(gLastOverlayUpdateTiming);
    gLastOverlayUpdateTiming.otherUs =
        gLastOverlayUpdateTiming.totalUs > knownUs
            ? gLastOverlayUpdateTiming.totalUs - knownUs
            : 0;
}

void OverlayWindow::DrawWindowCallback(XPLMWindowID windowId, void* refcon) {
    (void)windowId;
    auto* self = static_cast<OverlayWindow*>(refcon);
    if (self != nullptr) {
        self->Draw();
    }
}

int OverlayWindow::HandleMouseClickCallback(
    XPLMWindowID windowId,
    int x,
    int y,
    XPLMMouseStatus mouse,
    void* refcon) {
    auto* self = static_cast<OverlayWindow*>(refcon);
    if (self == nullptr || windowId == nullptr) {
        return 0;
    }

    if (mouse == xplm_MouseDown) {
        const auto accessoryHit = HitTestAccessoryOrb(
            self->accessoryLayout_, x, y);
        if (accessoryHit.handled) {
            XPLMBringWindowToFront(windowId);
            self->QueueAccessoryClick(accessoryHit.drawer);
            return 1;
        }
        if (!self->IsInOverlayRegion(x, y) &&
            !self->IsInAccessoryVisibleRegion(x, y)) {
            return 0;
        }
        XPLMBringWindowToFront(windowId);
        if (self->textEntryActive_) {
            XPLMTakeKeyboardFocus(windowId);
        }
        if (self->IsInSystemNoticeDismissAction(x, y)) {
            self->hasPendingSystemNoticeDismissRequest_ = true;
            return 1;
        }
        if (self->IsInAcknowledgeAction(x, y)) {
            self->hasPendingAcknowledgeRequest_ = true;
            return 1;
        }
        if (self->IsInRecallAction(x, y)) {
            self->hasPendingRecallRequest_ = true;
            return 1;
        }
        const auto resizeCorner = self->ResolveResizeCorner(x, y);
        if (resizeCorner != ResizeCorner::None) {
            self->StartResizing(x, y, resizeCorner);
        } else if (self->IsInDragRegion(x, y)) {
            self->StartDragging(x, y);
        }
        return 1;
    }

    if (mouse == xplm_MouseDrag) {
        if (self->resizing_) {
            self->ContinueResizing(x, y);
        } else {
            self->ContinueDragging(x, y);
        }
        return (self->resizing_ || self->dragging_) ? 1 : 0;
    }

    if (mouse == xplm_MouseUp) {
        self->StopDragging();
        self->StopResizing();
        return 1;
    }

    return 1;
}

int OverlayWindow::HandleRightClickCallback(
    XPLMWindowID windowId,
    int x,
    int y,
    XPLMMouseStatus mouse,
    void* refcon) {
    (void)windowId;
    (void)mouse;
    auto* self = static_cast<OverlayWindow*>(refcon);
    if (self == nullptr ||
        (!self->IsInOverlayRegion(x, y) &&
         !self->IsInAccessoryVisibleRegion(x, y))) {
        return 0;
    }
    self->StopDragging();
    self->StopResizing();
    return 1;
}

int OverlayWindow::HandleMouseWheelCallback(XPLMWindowID windowId, int x, int y, int wheel, int clicks, void* refcon) {
    (void)windowId;
    (void)wheel;

    auto* self = static_cast<OverlayWindow*>(refcon);
    if (self == nullptr) {
        return 0;
    }

    if (self->accessoryDrawerOpen_) {
        AccessoryPresentationScrollInput input;
        input.layout = self->accessoryLayout_;
        input.pointerX = x;
        input.pointerY = y;
        input.wheelClicks = -clicks;
        AccessoryWheelInput previewInput;
        previewInput.layout = input.layout;
        previewInput.pointerX = input.pointerX;
        previewInput.pointerY = input.pointerY;
        previewInput.wheelClicks = input.wheelClicks;
        previewInput.drawerOffset = self->accessoryPresentation_.drawerOffset;
        previewInput.drawerMaximumOffset =
            self->accessoryPresentation_.drawerMaximumOffset;
        const auto preview = ApplyAccessoryWheel(previewInput);
        const auto actionStarted = preview.drawerChanged
            ? AccessoryWallClockMicroseconds()
            : 0;
        const auto scrolled = ScrollAccessoryPresentation(
            &self->accessoryPresentation_, input);
        if (scrolled.handled) {
            if (scrolled.changed) {
                self->MarkAccessoryDrawerTextureDirty(
                    AccessoryRasterReason::PresentationScroll);
                if (self->accessoryPerformance_ != nullptr) {
                    const auto dispatchCompleted =
                        AccessoryWallClockMicroseconds();
                    AccessoryActionDispatchTimingInput timing;
                    timing.dispatchStartedMicroseconds = actionStarted;
                    timing.dispatchCompletedMicroseconds = dispatchCompleted;
                    self->accessoryPerformance_->BeginEffectiveScroll(
                        actionStarted,
                        self->accessoryPresentation_.selectionGeneration,
                        self->accessoryRenderGeneration_,
                        dispatchCompleted,
                        self->accessoryDrawCallbackOrdinal_ + 1,
                        timing);
                }
            }
            return 1;
        }
    }

    if (!self->IsInOverlayRegion(x, y)) {
        return 0;
    }

    const auto sections = ResolveSections(self->viewModel_, self->textEntryActive_, self->ResolvePromptLine());
    if (sections.listLines.size() > static_cast<std::size_t>(kVisibleListRows) && clicks != 0) {
        const auto previousOffset = self->scrollOffset_;
        self->scrollOffset_ -= clicks;
        self->ClampScrollOffset();
        if (previousOffset != self->scrollOffset_) {
            self->cardTextureDirty_ = true;
        }
    }

    return 1;
}

XPLMCursorStatus OverlayWindow::HandleCursorCallback(XPLMWindowID windowId, int x, int y, void* refcon) {
    (void)windowId;

    auto* self = static_cast<OverlayWindow*>(refcon);
    const auto accessoryHit = self == nullptr
        ? AccessoryHitTestResult{}
        : HitTestAccessoryOrb(self->accessoryLayout_, x, y);
    if (self != nullptr &&
        (accessoryHit.handled ||
         self->ResolveResizeCorner(x, y) != ResizeCorner::None ||
         self->IsInDragRegion(x, y) ||
         self->IsInAcknowledgeAction(x, y) ||
         self->IsInRecallAction(x, y) ||
         self->IsInSystemNoticeDismissAction(x, y))) {
        return xplm_CursorArrow;
    }

    return xplm_CursorDefault;
}

void OverlayWindow::HandleKeyCallback(
    XPLMWindowID windowId,
    char key,
    XPLMKeyFlags flags,
    char virtualKey,
    void* refcon,
    int losingFocus) {
    (void)windowId;
    (void)key;
    (void)virtualKey;

    auto* self = static_cast<OverlayWindow*>(refcon);
    if (self != nullptr && (flags & xplm_UpFlag) == 0) {
        self->HandleTextEntryKey(key, virtualKey, losingFocus);
    }
}

void OverlayWindow::SyncVisibility() {
    if (window_ == nullptr) {
        return;
    }

    const auto wantsCardVisible =
        overlayEnabled_ &&
        (accessoryDrawerOpen_ || viewModel_.visible ||
         !viewModel_.bodyLines.empty() || textEntryActive_);
    if (wantsCardVisible != lastWakeState_) {
        const auto soundStarted = OverlayClock::now();
        if (!transitionSoundPath_.empty()) {
            gLastOverlayUpdateTiming.transitionSoundSkipped = true;
        }
        gLastOverlayUpdateTiming.transitionSoundUs +=
            ElapsedOverlayUsSince(soundStarted);
        lastWakeState_ = wantsCardVisible;
    }
    animationTarget_ = wantsCardVisible ? 1.0f : 0.0f;

    const auto wasWindowVisible = windowVisible_;
    const auto shouldBeVisible = overlayEnabled_ || animationProgress_ > 0.0f || animationTarget_ > 0.0f;
    if (shouldBeVisible != windowVisible_) {
        const auto visibleStarted = OverlayClock::now();
        XPLMSetWindowIsVisible(window_, shouldBeVisible ? 1 : 0);
        gLastOverlayUpdateTiming.setVisibleCalled = true;
        gLastOverlayUpdateTiming.setVisibleUs +=
            ElapsedOverlayUsSince(visibleStarted);
        windowVisible_ = shouldBeVisible;
    }

    const auto becameVisible = shouldBeVisible && !wasWindowVisible;
    const auto needsFrontForInput = textEntryActive_ && shouldBeVisible;
    if (shouldBeVisible && !dragging_ && (becameVisible || needsFrontForInput)) {
        const auto frontStarted = OverlayClock::now();
        gLastOverlayUpdateTiming.bringFrontChecked = true;
        const auto nowSeconds = XPLMGetElapsedTime();
        const auto cooldownElapsed =
            (nowSeconds - gLastBringFrontAttemptTimeSeconds) >=
            kBringFrontCooldownSeconds;
        const auto canAttemptFront = needsFrontForInput || cooldownElapsed;
        if (!canAttemptFront) {
            gLastOverlayUpdateTiming.bringFrontThrottled = true;
        } else {
            gLastBringFrontAttemptTimeSeconds = nowSeconds;
            if (!XPLMIsWindowInFront(window_)) {
                XPLMBringWindowToFront(window_);
                gLastOverlayUpdateTiming.bringFrontCalled = true;
            }
        }
        gLastOverlayUpdateTiming.bringFrontUs +=
            ElapsedOverlayUsSince(frontStarted);
    }

    if (textEntryActive_ && shouldBeVisible && XPLMHasKeyboardFocus(window_) == 0) {
        XPLMTakeKeyboardFocus(window_);
    }
}

void OverlayWindow::Draw() {
    if (window_ == nullptr || !windowVisible_) {
        return;
    }
    PublishReadyAccessoryPreparation();

    const auto accessoryDrawCallbackEntered = AccessoryWallClockMicroseconds();
    const auto accessoryPrecedingDrawCallbackEntered =
        accessoryLastDrawCallbackEnteredMicroseconds_;
    accessoryLastDrawCallbackEnteredMicroseconds_ =
        accessoryDrawCallbackEntered;
    ++accessoryDrawCallbackOrdinal_;
    if (accessoryDrawCallbackOrdinal_ == 0) {
        accessoryDrawCallbackOrdinal_ = 1;
    }

    const auto previousAnimationProgress = animationProgress_;
    UpdateAnimationState();
    if (std::fabs(previousAnimationProgress - animationProgress_) >= 0.0001f ||
        std::fabs(accessoryLastAnimationProgress_ - animationProgress_) >= 0.0001f) {
        accessoryLastAnimationProgress_ = animationProgress_;
        RefreshAccessoryLayout();
    }
    if (!overlayEnabled_ && animationProgress_ <= 0.0f && animationTarget_ <= 0.0f) {
        XPLMSetWindowIsVisible(window_, 0);
        windowVisible_ = false;
        return;
    }

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);

    const auto sections = ResolveSections(viewModel_, textEntryActive_, ResolvePromptLine());
    ClampScrollOffset();

    const auto signature = BuildRenderSignature(sections, scrollOffset_, textEntryActive_, animationTarget_);
    if (cardTextureDirty_ || signature != lastCardSignature_) {
        UploadTexture(&cardTextureId_, RenderCardImage(sections, scrollOffset_));
        cardTextureDirty_ = false;
        lastCardSignature_ = signature;
        mainCardProductionSignature_ = std::to_string(signature);
    }

    if (caseTextureDirty_) {
        UploadTexture(&caseTextureId_, RenderCaseImage(automaticMode_));
        caseTextureDirty_ = false;
    }

    const auto layout = ResolveLayout(left, top, animationProgress_, scale_);
    DrawTexturedQuad(
        caseTextureId_,
        layout.caseLeft,
        layout.caseTop,
        layout.caseRight,
        layout.caseBottom,
        0.0f,
        1.0f,
        opacity_);

    if (animationProgress_ > 0.0f) {
        DrawSolidQuad(
            layout.tetherLeft,
            layout.tetherTop,
            layout.tetherRight,
            layout.tetherBottom,
            0.40f,
            0.47f,
            0.54f,
            0.68f * opacity_);
    }

    if (animationProgress_ <= 0.02f) {
        return;
    }

    const auto visibleCardHeight = std::max(1, layout.cardTop - layout.cardBottom);
    const auto textureBottom = static_cast<float>(visibleCardHeight) / static_cast<float>(kCardHeight);
    DrawTexturedQuad(
        cardTextureId_,
        layout.cardLeft,
        layout.cardTop,
        layout.cardRight,
        layout.cardBottom,
        0.0f,
        textureBottom,
        opacity_);

    if (animationProgress_ < 1.0f || !accessoryLayout_.accessoriesVisible) {
        return;
    }
    const bool measuringAccessoryAction =
        accessoryPerformance_ != nullptr &&
        accessoryPerformance_->HasMatchingPendingAction(
            accessoryPresentation_.selectionGeneration,
            accessoryRenderGeneration_,
            accessoryDrawCallbackOrdinal_);
    AccessoryActionDrawTimingInput actionDrawTiming;
    const auto accessoryActionDrawStarted = measuringAccessoryAction
        ? AccessoryWallClockMicroseconds()
        : 0;
    RenderAccessoryTexturesIfNeeded(
        measuringAccessoryAction ? &actionDrawTiming : nullptr);
    const auto accessoryDrawStarted = AccessoryWallClockMicroseconds();
    DrawTexturedQuad(
        accessoryRailTextureId_,
        accessoryLayout_.railBounds.left,
        accessoryLayout_.railBounds.top,
        accessoryLayout_.railBounds.right,
        accessoryLayout_.railBounds.bottom,
        0.0f,
        1.0f,
        opacity_);
    ++accessoryIntegrationCounters_.railDraws;
    if (accessoryDrawerOpen_) {
        DrawTexturedQuad(
            accessoryDrawerTextureId_,
            accessoryLayout_.drawerBounds.left,
            accessoryLayout_.drawerBounds.top,
            accessoryLayout_.drawerBounds.right,
            accessoryLayout_.drawerBounds.bottom,
            0.0f,
            1.0f,
            opacity_);
        ++accessoryIntegrationCounters_.drawerDraws;
    }
    const auto accessoryDrawCompleted = AccessoryWallClockMicroseconds();
    const auto completedDrawWall =
        accessoryDrawCompleted - accessoryDrawStarted;
    RecordAccessoryPerformance(
        AccessoryPerformanceCategory::CompletedAccessoryDraw,
        completedDrawWall);
    if (measuringAccessoryAction &&
        completedDrawWall > AccessoryPerformanceCollector::kThresholdMicroseconds &&
        (!actionDrawTiming.renderWallFailure ||
         completedDrawWall > actionDrawTiming.renderWallFailureMicroseconds)) {
        actionDrawTiming.renderWallFailure = true;
        actionDrawTiming.renderWallFailureCategory =
            AccessoryPerformanceCategory::CompletedAccessoryDraw;
        actionDrawTiming.renderWallFailureMicroseconds = completedDrawWall;
    }
    if (accessoryPerformance_ != nullptr) {
        if (measuringAccessoryAction) {
            actionDrawTiming.actionDrawWallMicroseconds =
                accessoryDrawCompleted - accessoryActionDrawStarted;
        }
        accessoryPerformance_->CompletePendingActions(
            accessoryDrawCompleted,
            accessoryPresentation_.selectionGeneration,
            accessoryRenderGeneration_,
            accessoryDrawCallbackEntered,
            accessoryPrecedingDrawCallbackEntered,
            accessoryDrawCallbackOrdinal_,
            actionDrawTiming);
    }
    const auto completedDispatch =
        accessoryInputDispatcher_.CompleteMatchingDraw(
            accessoryPresentation_.activeSnapshot == nullptr
                ? brain::BrainOwnedAccessoryDrawerId::None
                : accessoryPresentation_.activeSnapshot->activeDrawer,
            accessoryPresentation_.selectionGeneration,
            accessoryRenderGeneration_,
            accessoryDrawCompleted);
    PublishFirstAccessoryPerformanceWarningIfNeeded();
    if (completedDispatch.terminal &&
        accessoryClickQueue_.PendingCount() > 0 &&
        accessoryInputDispatchCallback_ != nullptr) {
        accessoryInputDispatcher_.RecordDispatchNotification();
        accessoryInputDispatchCallback_(accessoryInputDispatchRefcon_);
    }
}

void OverlayWindow::HandleTextEntryKey(char key, char virtualKey, int losingFocus) {
    if (!textEntryActive_) {
        return;
    }

    if (losingFocus != 0) {
        StopDragging();
        return;
    }

    if (virtualKey == XPLM_VK_ESCAPE || key == 27) {
        CancelTextEntry();
        return;
    }

    if (virtualKey == XPLM_VK_RETURN || virtualKey == XPLM_VK_ENTER || key == '\r' || key == '\n') {
        pendingSubmittedText_ = textEntryBuffer_;
        hasPendingSubmittedText_ = !pendingSubmittedText_.empty();
        CancelTextEntry();
        return;
    }

    if (virtualKey == XPLM_VK_BACK || virtualKey == XPLM_VK_DELETE || key == '\b' || virtualKey == 127) {
        if (!textEntryBuffer_.empty()) {
            textEntryBuffer_.pop_back();
            cardTextureDirty_ = true;
        }
        return;
    }

    if (std::isprint(static_cast<unsigned char>(key)) == 0) {
        return;
    }

    if (textEntryBuffer_.size() >= kMaxTextEntryChars) {
        return;
    }

    if (std::isalpha(static_cast<unsigned char>(key)) != 0) {
        key = static_cast<char>(std::toupper(static_cast<unsigned char>(key)));
    }

    textEntryBuffer_.push_back(key);
    cardTextureDirty_ = true;
}

std::string OverlayWindow::ResolvePromptLine() const {
    if (!textEntryActive_) {
        return {};
    }

    return "CMD " + textEntryBuffer_ + "_";
}

bool OverlayWindow::IsInDragRegion(int x, int y) const {
    if (window_ == nullptr || !windowVisible_) {
        return false;
    }

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);
    const auto layout = ResolveLayout(left, top, std::max(animationProgress_, 0.10f), scale_);

    return x >= layout.cardLeft && x <= layout.cardRight &&
           y <= layout.caseTop && y >= (layout.cardTop - ScaleValue(kDragRegionHeight, scale_));
}

bool OverlayWindow::IsInOverlayRegion(int x, int y) const {
    if (window_ == nullptr || !windowVisible_) {
        return false;
    }

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);
    const auto layout = ResolveLayout(left, top, std::max(animationProgress_, 0.10f), scale_);

    const auto overlayLeft = std::min(layout.caseLeft, layout.cardLeft);
    const auto overlayRight = std::max(layout.caseRight, layout.cardRight);
    const auto overlayTop = layout.caseTop;
    const auto overlayBottom = layout.cardBottom;

    return x >= overlayLeft && x <= overlayRight &&
           y <= overlayTop && y >= overlayBottom;
}

bool OverlayWindow::IsInAcknowledgeAction(int x, int y) const {
    if (window_ == nullptr || !windowVisible_ || !viewModel_.showMessageAcknowledge) {
        return false;
    }

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);
    const auto layout = ResolveLayout(left, top, std::max(animationProgress_, 0.10f), scale_);
    return ResolveCardActionRect(layout, scale_, OverlayMessageAction::Acknowledge).Contains(x, y);
}

bool OverlayWindow::IsInRecallAction(int x, int y) const {
    if (window_ == nullptr || !windowVisible_ || !viewModel_.showMessageRecall) {
        return false;
    }

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);
    const auto layout = ResolveLayout(left, top, std::max(animationProgress_, 0.10f), scale_);
    return ResolveCardActionRect(layout, scale_, OverlayMessageAction::Recall).Contains(x, y);
}

bool OverlayWindow::IsInSystemNoticeDismissAction(int x, int y) const {
    if (window_ == nullptr ||
        !windowVisible_ ||
        !viewModel_.systemNotice.visible ||
        !viewModel_.systemNotice.dismissible) {
        return false;
    }

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);
    const auto layout = ResolveLayout(left, top, std::max(animationProgress_, 0.10f), scale_);
    return ResolveSystemNoticeDismissRect(layout, scale_).Contains(x, y);
}

OverlayWindow::ResizeCorner OverlayWindow::ResolveResizeCorner(int x, int y) const {
    if (window_ == nullptr || animationProgress_ <= 0.15f) {
        return ResizeCorner::None;
    }

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);
    const auto layout = ResolveLayout(left, top, animationProgress_, scale_);
    if (layout.cardTop <= layout.cardBottom) {
        return ResizeCorner::None;
    }

    const auto resizeBottom = accessoryDrawerOpen_
        ? accessoryLayout_.resolvedBounds.bottom
        : layout.cardBottom;
    const auto resizeLeft = accessoryDrawerOpen_
        ? accessoryLayout_.resolvedBounds.left
        : layout.cardLeft;
    const auto resizeRight = accessoryDrawerOpen_
        ? accessoryLayout_.resolvedBounds.right
        : layout.cardRight;
    const auto rightDistance = std::max(
        std::abs(x - resizeRight), std::abs(y - resizeBottom));
    if (rightDistance <= ScaleValue(kResizeHotspotPx, scale_)) {
        return ResizeCorner::LowerRight;
    }

    const auto leftDistance = std::max(
        std::abs(x - resizeLeft), std::abs(y - resizeBottom));
    if (leftDistance <= ScaleValue(kResizeHotspotPx, scale_)) {
        return ResizeCorner::LowerLeft;
    }

    return ResizeCorner::None;
}

void OverlayWindow::ClampScrollOffset() {
    const auto sections = ResolveSections(viewModel_, textEntryActive_, ResolvePromptLine());
    const auto maxOffset = std::max(0, static_cast<int>(sections.listLines.size()) - kVisibleListRows);
    scrollOffset_ = std::clamp(scrollOffset_, 0, maxOffset);
}

void OverlayWindow::RefreshAccessoryLayout(
    AccessoryAnchorOperation operation,
    bool hasExactAnchor,
    int exactLeft,
    int exactTop) {
    if (window_ == nullptr || accessoryMeasurementContext_ == nullptr) {
        return;
    }

    int screenLeft = 0;
    int screenTop = 0;
    int screenRight = 0;
    int screenBottom = 0;
    XPLMGetScreenBoundsGlobal(
        &screenLeft, &screenTop, &screenRight, &screenBottom);
    const bool screenBoundsChanged =
        screenLeft != accessoryLastScreenLeft_ ||
        screenTop != accessoryLastScreenTop_ ||
        screenRight != accessoryLastScreenRight_ ||
        screenBottom != accessoryLastScreenBottom_;
    if (screenBoundsChanged) {
        accessoryLastScreenLeft_ = screenLeft;
        accessoryLastScreenTop_ = screenTop;
        accessoryLastScreenRight_ = screenRight;
        accessoryLastScreenBottom_ = screenBottom;
        if (operation == AccessoryAnchorOperation::OrdinaryRefresh &&
            accessoryAnchorState_.initialized) {
            operation = AccessoryAnchorOperation::ScreenBoundsChanged;
        }
    }

    if (accessoryTypography_.status !=
            brain::BrainOwnedAccessoryOperationStatus::Available ||
        std::fabs(accessoryTypography_.scale - scale_) >= 0.001f) {
        accessoryTypography_ = PrepareAccessoryTypography(
            accessoryMeasurementContext_, scale_);
    }

    int left = exactLeft;
    int top = exactTop;
    int right = 0;
    int bottom = 0;
    if (!hasExactAnchor) {
        XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);
    }
    if (!accessoryAnchorState_.initialized &&
        operation == AccessoryAnchorOperation::OrdinaryRefresh) {
        operation = AccessoryAnchorOperation::Initialize;
    }

    AccessoryAnchorState localState = accessoryAnchorState_;
    if (localState.currentAnchorValid) {
        localState.currentAnchorLeft -= screenLeft;
        localState.currentAnchorTop -= screenBottom;
    }
    if (localState.savedClosedAnchorValid) {
        localState.savedClosedAnchorLeft -= screenLeft;
        localState.savedClosedAnchorTop -= screenBottom;
    }

    AccessoryAnchorUpdateInput input;
    input.operation = operation;
    input.layout.screenWidth = std::max(1, screenRight - screenLeft);
    input.layout.screenHeight = std::max(1, screenTop - screenBottom);
    input.layout.windowLeft = left - screenLeft;
    input.layout.windowTop = top - screenBottom;
    input.layout.scale = scale_;
    input.layout.cardAnimationProgress = animationProgress_;
    input.layout.drawerOpen = accessoryDrawerOpen_;
    input.layout.typography = &accessoryTypography_;
    auto update = UpdateAccessoryAnchorState(&localState, input);
    if (update.status != brain::BrainOwnedAccessoryOperationStatus::Available) {
        return;
    }
    auto resolved = std::move(update.layout);

    const auto translateRect = [screenLeft, screenBottom](AccessoryRect* rect) {
        rect->left += screenLeft;
        rect->right += screenLeft;
        rect->top += screenBottom;
        rect->bottom += screenBottom;
    };
    resolved.resolvedLeft += screenLeft;
    resolved.resolvedTop += screenBottom;
    resolved.restorableClosedAnchorLeft += screenLeft;
    resolved.restorableClosedAnchorTop += screenBottom;
    translateRect(&resolved.closedBounds);
    translateRect(&resolved.openBounds);
    translateRect(&resolved.resolvedBounds);
    translateRect(&resolved.mainCardBounds);
    translateRect(&resolved.railBounds);
    if (resolved.drawerBounds.right > resolved.drawerBounds.left) {
        translateRect(&resolved.drawerBounds);
    }
    for (auto& orb : resolved.orbs) {
        translateRect(&orb.bounds);
    }
    if (localState.currentAnchorValid) {
        localState.currentAnchorLeft += screenLeft;
        localState.currentAnchorTop += screenBottom;
    }
    if (localState.savedClosedAnchorValid) {
        localState.savedClosedAnchorLeft += screenLeft;
        localState.savedClosedAnchorTop += screenBottom;
    }
    accessoryAnchorState_ = localState;
    accessoryLayout_ = std::move(resolved);
}

void OverlayWindow::ApplyAccessoryWindowGeometry(bool drawerOpen) {
    if (window_ == nullptr) {
        accessoryDrawerOpen_ = drawerOpen;
        return;
    }
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);
    const auto operation = drawerOpen
        ? AccessoryAnchorOperation::OpenDrawer
        : AccessoryAnchorOperation::CloseDrawer;
    accessoryDrawerOpen_ = drawerOpen;
    RefreshAccessoryLayout(operation, true, left, top);
    if (accessoryLayout_.status !=
        brain::BrainOwnedAccessoryOperationStatus::Available) {
        return;
    }
    const auto& bounds = accessoryLayout_.resolvedBounds;
    XPLMSetWindowGeometry(
        window_, bounds.left, bounds.top, bounds.right, bounds.bottom);
}

bool OverlayWindow::IsInAccessoryVisibleRegion(int x, int y) const {
    if (!accessoryLayout_.accessoriesVisible) {
        return false;
    }
    const auto inside = [x, y](const AccessoryRect& rect) {
        return rect.right > rect.left && rect.top > rect.bottom &&
            x >= rect.left && x <= rect.right &&
            y >= rect.bottom && y <= rect.top;
    };
    return inside(accessoryLayout_.railBounds) ||
        (accessoryDrawerOpen_ && inside(accessoryLayout_.drawerBounds));
}

void OverlayWindow::QueueAccessoryClick(
    brain::BrainOwnedAccessoryDrawerId drawer) {
    if (drawer == brain::BrainOwnedAccessoryDrawerId::None) {
        return;
    }
    const auto produced = accessoryClickQueue_.Produce(
        drawer, AccessoryWallClockMicroseconds(), nullptr);
    accessoryIntegrationCounters_.clickFactsProduced =
        accessoryClickQueue_.ProducedCount();
    accessoryIntegrationCounters_.clickFactsDropped =
        accessoryClickQueue_.DroppedCount();
    if (produced && accessoryInputDispatchCallback_ != nullptr) {
        accessoryInputDispatcher_.RecordDispatchNotification();
        accessoryInputDispatchCallback_(accessoryInputDispatchRefcon_);
    }
}

std::uint64_t OverlayWindow::AccessoryWallClockMicroseconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            OverlayClock::now().time_since_epoch()).count());
}

void OverlayWindow::RecordAccessoryPerformance(
    AccessoryPerformanceCategory category,
    std::uint64_t elapsedMicroseconds) {
    if (accessoryPerformance_ != nullptr) {
        accessoryPerformance_->Record(category, elapsedMicroseconds);
    }
}

void OverlayWindow::MarkAccessoryRailTextureDirty(
    AccessoryRasterReason reason) {
    const auto priority = [](AccessoryRasterReason value) {
        switch (value) {
            case AccessoryRasterReason::Initial: return 5;
            case AccessoryRasterReason::Selection: return 4;
            case AccessoryRasterReason::EffectiveScaleOrResize: return 3;
            case AccessoryRasterReason::ContentGeneration: return 2;
            case AccessoryRasterReason::TypographyOrLayout: return 1;
            case AccessoryRasterReason::Count: default: return 0;
        }
    };
    if (accessoryRailTextureDirty_ &&
        priority(accessoryRailRasterReason_) >= priority(reason)) {
        return;
    }
    accessoryRailTextureDirty_ = true;
    accessoryRailRasterReason_ = reason;
}

void OverlayWindow::MarkAccessoryDrawerTextureDirty(
    AccessoryRasterReason reason) {
    const auto priority = [](AccessoryRasterReason value) {
        switch (value) {
            case AccessoryRasterReason::Initial: return 5;
            case AccessoryRasterReason::Selection: return 4;
            case AccessoryRasterReason::EffectiveScaleOrResize: return 3;
            case AccessoryRasterReason::ContentGeneration: return 2;
            case AccessoryRasterReason::TypographyOrLayout: return 1;
            case AccessoryRasterReason::Count: default: return 0;
        }
    };
    if (accessoryDrawerTextureDirty_ &&
        priority(accessoryDrawerRasterReason_) >= priority(reason)) {
        return;
    }
    accessoryDrawerTextureDirty_ = true;
    accessoryDrawerRasterReason_ = reason;
}

void OverlayWindow::PublishFirstAccessoryPerformanceWarningIfNeeded() {
    if (accessoryPerformance_ == nullptr) {
        return;
    }
    AccessoryPerformanceCategory category;
    std::uint64_t elapsedMicroseconds = 0;
    if (!accessoryPerformance_->ConsumeFirstViolationWarning(
            &category, &elapsedMicroseconds)) {
        return;
    }
    std::ostringstream line;
    line << "[XVatsim] Step 3 performance threshold exceeded category="
         << AccessoryPerformanceCategoryToken(category)
         << " elapsedUs=" << elapsedMicroseconds
         << " thresholdUs="
         << AccessoryPerformanceCollector::kThresholdMicroseconds << "\n";
    XPLMDebugString(line.str().c_str());
}

void OverlayWindow::RenderAccessoryTexturesIfNeeded(
    AccessoryActionDrawTimingInput* actionDrawTiming) {
    if (!accessoryLayout_.accessoriesVisible ||
        accessoryPresentation_.activeSnapshot == nullptr) {
        return;
    }
    const auto recordActionRenderWallViolation = [actionDrawTiming](
        AccessoryPerformanceCategory category,
        std::uint64_t elapsedMicroseconds) {
        if (actionDrawTiming == nullptr ||
            elapsedMicroseconds <=
                AccessoryPerformanceCollector::kThresholdMicroseconds ||
            (actionDrawTiming->renderWallFailure &&
             elapsedMicroseconds <=
                actionDrawTiming->renderWallFailureMicroseconds)) {
            return;
        }
        actionDrawTiming->renderWallFailure = true;
        actionDrawTiming->renderWallFailureCategory = category;
        actionDrawTiming->renderWallFailureMicroseconds = elapsedMicroseconds;
    };
    if (accessoryRailTextureDirty_) {
        const auto rasterStarted = OverlayClock::now();
        const auto image = RenderAccessoryRailImage(
            accessoryLayout_, *accessoryPresentation_.activeSnapshot);
        const auto rasterUs = static_cast<std::uint64_t>(
            ElapsedOverlayUsSince(rasterStarted));
        ++accessoryIntegrationCounters_.railRasterizations;
        RecordAccessoryPerformance(
            AccessoryPerformanceCategory::RailRasterization, rasterUs);
        recordActionRenderWallViolation(
            AccessoryPerformanceCategory::RailRasterization, rasterUs);
        if (accessoryPerformance_ != nullptr) {
            accessoryPerformance_->RecordRasterReason(
                false, accessoryRailRasterReason_);
        }
        const auto uploadStarted = OverlayClock::now();
        UploadTexture(&accessoryRailTextureId_, image);
        const auto uploadUs = static_cast<std::uint64_t>(
            ElapsedOverlayUsSince(uploadStarted));
        ++accessoryIntegrationCounters_.railTextureUploads;
        RecordAccessoryPerformance(
            AccessoryPerformanceCategory::RailTextureUpload, uploadUs);
        recordActionRenderWallViolation(
            AccessoryPerformanceCategory::RailTextureUpload, uploadUs);
        accessoryRailTextureDirty_ = false;
    }
    if (accessoryDrawerOpen_ && accessoryDrawerTextureDirty_) {
        const auto rasterStarted = OverlayClock::now();
        const auto image = RenderAccessoryDrawerImage(
            accessoryLayout_, accessoryPresentation_);
        const auto rasterUs = static_cast<std::uint64_t>(
            ElapsedOverlayUsSince(rasterStarted));
        ++accessoryIntegrationCounters_.drawerRasterizations;
        RecordAccessoryPerformance(
            AccessoryPerformanceCategory::DrawerRasterization, rasterUs);
        recordActionRenderWallViolation(
            AccessoryPerformanceCategory::DrawerRasterization, rasterUs);
        if (accessoryPerformance_ != nullptr) {
            accessoryPerformance_->RecordRasterReason(
                true, accessoryDrawerRasterReason_);
        }
        const auto uploadStarted = OverlayClock::now();
        UploadTexture(&accessoryDrawerTextureId_, image);
        const auto uploadUs = static_cast<std::uint64_t>(
            ElapsedOverlayUsSince(uploadStarted));
        ++accessoryIntegrationCounters_.drawerTextureUploads;
        RecordAccessoryPerformance(
            AccessoryPerformanceCategory::DrawerTextureUpload, uploadUs);
        recordActionRenderWallViolation(
            AccessoryPerformanceCategory::DrawerTextureUpload, uploadUs);
        accessoryDrawerTextureDirty_ = false;
    }
}

void OverlayWindow::ApplyScale(float scale, bool anchorRight) {
    const auto clampedScale = std::clamp(scale, 0.85f, 1.35f);
    if (std::fabs(clampedScale - scale_) < 0.001f) {
        return;
    }

    scale_ = clampedScale;
    scaleChanged_ = true;
    ++accessoryLayoutGeneration_;
    if (accessoryMeasurementContext_ != nullptr) {
        accessoryTypography_ = PrepareAccessoryTypography(
            accessoryMeasurementContext_, scale_);
    }

    if (window_ == nullptr) {
        return;
    }

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);

    const auto newWidth = ScaleValue(kOverlayWidth, scale_);
    const auto newHeight = ScaleValue(
        accessoryDrawerOpen_ ? kOverlayOpenHeight : kOverlayHeight, scale_);
    auto newLeft = anchorRight ? (right - newWidth) : left;
    auto newTop = top;
    ClampTopLeftToScreen(newWidth, newHeight, &newLeft, &newTop);
    XPLMSetWindowGeometry(window_, newLeft, newTop, newLeft + newWidth, newTop - newHeight);
    RefreshAccessoryLayout(
        AccessoryAnchorOperation::IntentionalMove,
        true,
        newLeft,
        newTop);
    MarkAccessoryRailTextureDirty(
        AccessoryRasterReason::EffectiveScaleOrResize);
    MarkAccessoryDrawerTextureDirty(
        AccessoryRasterReason::EffectiveScaleOrResize);
    positionChanged_ = true;
}

void OverlayWindow::UpdateAnimationState() {
    const auto nowSeconds = XPLMGetElapsedTime();
    if (animationLastTimestampSeconds_ < 0.0f) {
        animationLastTimestampSeconds_ = nowSeconds;
    }

    const auto deltaSeconds = std::max(0.0f, nowSeconds - animationLastTimestampSeconds_);
    animationLastTimestampSeconds_ = nowSeconds;

    if (animationProgress_ < animationTarget_) {
        animationProgress_ = std::min(
            animationTarget_,
            animationProgress_ + ((deltaSeconds * animationSpeed_) / kShowDurationSeconds));
        return;
    }

    if (animationProgress_ > animationTarget_) {
        animationProgress_ = std::max(
            animationTarget_,
            animationProgress_ - ((deltaSeconds * animationSpeed_) / kHideDurationSeconds));
    }
}

void OverlayWindow::PlayTransitionSound() {
    if (transitionSoundPath_.empty() || !std::filesystem::exists(transitionSoundPath_)) {
        return;
    }

    if (!transitionSoundLoaded_) {
        CloseTransitionSoundAlias();
        const auto openCommand =
            L"open " + QuoteMciPath(transitionSoundPath_) +
            L" type mpegvideo alias " + std::wstring(kTransitionSoundAlias);
        if (mciSendStringW(openCommand.c_str(), nullptr, 0, nullptr) != 0) {
            return;
        }
        transitionSoundLoaded_ = true;
    }

    const auto seekCommand =
        std::wstring(L"seek ") + kTransitionSoundAlias + L" to start";
    mciSendStringW(seekCommand.c_str(), nullptr, 0, nullptr);

    const auto playCommand =
        std::wstring(L"play ") + kTransitionSoundAlias + L" from 0";
    mciSendStringW(playCommand.c_str(), nullptr, 0, nullptr);
}

void OverlayWindow::CloseTransitionSoundAlias() {
    const auto closeCommand =
        std::wstring(L"close ") + kTransitionSoundAlias;
    mciSendStringW(closeCommand.c_str(), nullptr, 0, nullptr);
    transitionSoundLoaded_ = false;
}

void OverlayWindow::StartDragging(int x, int y) {
    if (window_ == nullptr) {
        return;
    }

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);
    dragging_ = true;
    dragMoved_ = false;
    dragOffsetX_ = x - left;
    dragOffsetTop_ = top - y;
}

void OverlayWindow::ContinueDragging(int x, int y) {
    if (window_ == nullptr || !dragging_ || resizing_) {
        return;
    }

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);

    const auto width = right - left;
    const auto height = top - bottom;
    auto newLeft = x - dragOffsetX_;
    auto newTop = y + dragOffsetTop_;
    ClampTopLeftToScreen(width, height, &newLeft, &newTop);

    XPLMSetWindowGeometry(window_, newLeft, newTop, newLeft + width, newTop - height);
    RefreshAccessoryLayout(
        AccessoryAnchorOperation::IntentionalMove,
        true,
        newLeft,
        newTop);
    dragMoved_ = dragMoved_ || newLeft != left || newTop != top;
}

void OverlayWindow::StopDragging() {
    if (dragging_ && dragMoved_) {
        positionChanged_ = true;
    }
    dragging_ = false;
    dragMoved_ = false;
}

void OverlayWindow::StartResizing(int x, int y, ResizeCorner corner) {
    if (window_ == nullptr || corner == ResizeCorner::None) {
        return;
    }

    StopDragging();

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);

    resizing_ = true;
    activeResizeCorner_ = corner;
    resizeAnchorTop_ = top;
    resizeAnchorLeft_ = left;
    resizeAnchorRight_ = right;
    ContinueResizing(x, y);
}

void OverlayWindow::ContinueResizing(int x, int y) {
    if (window_ == nullptr || !resizing_) {
        return;
    }

    const auto widthFromLeft = std::max(ScaleValue(kOverlayWidth, 0.85f), x - resizeAnchorLeft_);
    const auto widthFromRight = std::max(ScaleValue(kOverlayWidth, 0.85f), resizeAnchorRight_ - x);
    const auto activeDesignHeight = accessoryDrawerOpen_
        ? kOverlayOpenHeight
        : kOverlayHeight;
    const auto heightFromTop = std::max(
        ScaleValue(activeDesignHeight, 0.85f), resizeAnchorTop_ - y);

    const auto widthScale =
        static_cast<float>(activeResizeCorner_ == ResizeCorner::LowerLeft ? widthFromRight : widthFromLeft) /
        static_cast<float>(kOverlayWidth);
    const auto heightScale =
        static_cast<float>(heightFromTop) /
        static_cast<float>(activeDesignHeight);
    ApplyScale(std::max(widthScale, heightScale), activeResizeCorner_ == ResizeCorner::LowerLeft);
}

void OverlayWindow::StopResizing() {
    resizing_ = false;
    activeResizeCorner_ = ResizeCorner::None;
}

#endif

}  // namespace xvatsim::modules::overlay
