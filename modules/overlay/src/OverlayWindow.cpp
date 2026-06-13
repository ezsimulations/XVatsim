#include "XVatsim/modules/overlay/OverlayWindow.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
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
using Gdiplus::StringFormat;
using Gdiplus::StringFormatFlagsNoWrap;
using Gdiplus::StringTrimmingEllipsisCharacter;
using Gdiplus::UnitPixel;

constexpr int kOverlayMarginLeft = 18;
constexpr int kOverlayMarginTop = 44;
constexpr int kOverlayWidth = 430;
constexpr int kOverlayHeight = 388;

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
constexpr wchar_t kTransitionSoundAlias[] = L"xvatsim_transition";
constexpr std::size_t kMaxRenderTextChars = 128;
constexpr std::size_t kMaxRenderHeaderChars = 32;
constexpr std::size_t kMaxRenderFrequencyChars = 16;
constexpr std::size_t kMaxRenderListLines = 64;
constexpr std::size_t kMaxTextEntryChars = 32;

struct OverlaySections {
    std::string callsignOrStatus;
    std::string badgeText;
    std::string phaseChip;
    std::string headerRightText;
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
    XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);
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
    DrawTextBlock(&graphics, RectF(46.0f, 30.0f, 80.0f, 12.0f), "v1.0.3", &metaFont, muted);
    DrawTextBlock(&graphics, RectF(154.0f, 17.0f, 76.0f, 12.0f), sections.badgeText, &badgeFont, cyan);
    DrawTextBlock(&graphics, RectF(panelRect.GetRight() - 112.0f, 10.0f, 98.0f, 16.0f), sections.phaseChip, &metaFont, muted, Gdiplus::StringAlignmentFar);

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

    return CaptureBitmap(&bitmap);
}

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
    animationProgress_ = 0.0f;
    animationTarget_ = 0.0f;
    lastWakeState_ = false;
    animationLastTimestampSeconds_ = XPLMGetElapsedTime();

    Gdiplus::GdiplusStartupInput startupInput;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &startupInput, nullptr) == Gdiplus::Ok) {
        gdiplusToken_ = static_cast<std::uintptr_t>(token);
    }

    caseTextureDirty_ = true;
    cardTextureDirty_ = true;
    lastCardSignature_ = 0;
}

void OverlayWindow::Destroy() {
    if (window_ != nullptr) {
        XPLMDestroyWindow(window_);
        window_ = nullptr;
    }

    CloseTransitionSoundAlias();

    if (caseTextureId_ != 0U || cardTextureId_ != 0U) {
        GLuint textureIds[2] = {caseTextureId_, cardTextureId_};
        glDeleteTextures(2, textureIds);
        caseTextureId_ = 0;
        cardTextureId_ = 0;
    }

    if (gdiplusToken_ != 0U) {
        Gdiplus::GdiplusShutdown(static_cast<ULONG_PTR>(gdiplusToken_));
        gdiplusToken_ = 0;
    }

    windowVisible_ = false;
    overlayEnabled_ = false;
}

void OverlayWindow::Update(const brain::OverlayViewModel& viewModel) {
    if (window_ == nullptr) {
        Create();
    }
    viewModel_ = viewModel;
    overlayEnabled_ = true;
    ClampScrollOffset();
    SyncVisibility();
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

void OverlayWindow::SetWindowTopLeft(int left, int top) {
    const auto pendingWidth = ScaleValue(kOverlayWidth, scale_);
    const auto pendingHeight = ScaleValue(kOverlayHeight, scale_);
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
    positionChanged_ = false;
}

bool OverlayWindow::GetWindowTopLeft(int* outLeft, int* outTop) const {
    if (window_ == nullptr || outLeft == nullptr || outTop == nullptr) {
        return false;
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
    viewModel_ = {};
    textEntryActive_ = false;
    textEntryBuffer_.clear();
    pendingSubmittedText_.clear();
    hasPendingSubmittedText_ = false;
    hasPendingAcknowledgeRequest_ = false;
    hasPendingRecallRequest_ = false;
    overlayEnabled_ = false;
    dragging_ = false;
    resizing_ = false;
    activeResizeCorner_ = ResizeCorner::None;
    animationProgress_ = 0.0f;
    animationTarget_ = 0.0f;
    lastWakeState_ = false;
    scrollOffset_ = 0;
    cardTextureDirty_ = true;
    if (window_ != nullptr) {
        XPLMSetWindowIsVisible(window_, 0);
        windowVisible_ = false;
    }
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
        XPLMBringWindowToFront(windowId);
        if (self->textEntryActive_) {
            XPLMTakeKeyboardFocus(windowId);
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
        return 1;
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
    (void)x;
    (void)y;
    (void)mouse;
    auto* self = static_cast<OverlayWindow*>(refcon);
    if (self != nullptr) {
        self->StopDragging();
        self->StopResizing();
    }
    return 1;
}

int OverlayWindow::HandleMouseWheelCallback(XPLMWindowID windowId, int x, int y, int wheel, int clicks, void* refcon) {
    (void)windowId;
    (void)wheel;

    auto* self = static_cast<OverlayWindow*>(refcon);
    if (self == nullptr) {
        return 0;
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
    if (self != nullptr &&
        (self->ResolveResizeCorner(x, y) != ResizeCorner::None ||
         self->IsInDragRegion(x, y) ||
         self->IsInAcknowledgeAction(x, y) ||
         self->IsInRecallAction(x, y))) {
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
        overlayEnabled_ && (viewModel_.visible || !viewModel_.bodyLines.empty() || textEntryActive_);
    if (wantsCardVisible != lastWakeState_) {
        if (!transitionSoundPath_.empty()) {
            PlayTransitionSound();
        }
        lastWakeState_ = wantsCardVisible;
    }
    animationTarget_ = wantsCardVisible ? 1.0f : 0.0f;

    const auto shouldBeVisible = overlayEnabled_ || animationProgress_ > 0.0f || animationTarget_ > 0.0f;
    if (shouldBeVisible != windowVisible_) {
        XPLMSetWindowIsVisible(window_, shouldBeVisible ? 1 : 0);
        windowVisible_ = shouldBeVisible;
    }

    if (shouldBeVisible && !dragging_ && !XPLMIsWindowInFront(window_)) {
        XPLMBringWindowToFront(window_);
    }

    if (textEntryActive_ && shouldBeVisible && XPLMHasKeyboardFocus(window_) == 0) {
        XPLMTakeKeyboardFocus(window_);
    }
}

void OverlayWindow::Draw() {
    if (window_ == nullptr || !windowVisible_) {
        return;
    }

    UpdateAnimationState();
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

    const auto rightDistance = std::max(std::abs(x - layout.cardRight), std::abs(y - layout.cardBottom));
    if (rightDistance <= ScaleValue(kResizeHotspotPx, scale_)) {
        return ResizeCorner::LowerRight;
    }

    const auto leftDistance = std::max(std::abs(x - layout.cardLeft), std::abs(y - layout.cardBottom));
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

void OverlayWindow::ApplyScale(float scale, bool anchorRight) {
    const auto clampedScale = std::clamp(scale, 0.85f, 1.35f);
    if (std::fabs(clampedScale - scale_) < 0.001f) {
        return;
    }

    scale_ = clampedScale;
    scaleChanged_ = true;

    if (window_ == nullptr) {
        return;
    }

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);

    const auto newWidth = ScaleValue(kOverlayWidth, scale_);
    const auto newHeight = ScaleValue(kOverlayHeight, scale_);
    auto newLeft = anchorRight ? (right - newWidth) : left;
    auto newTop = top;
    ClampTopLeftToScreen(newWidth, newHeight, &newLeft, &newTop);
    XPLMSetWindowGeometry(window_, newLeft, newTop, newLeft + newWidth, newTop - newHeight);
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
}

void OverlayWindow::StopDragging() {
    if (dragging_) {
        positionChanged_ = true;
    }
    dragging_ = false;
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
    const auto heightFromTop = std::max(ScaleValue(kOverlayHeight, 0.85f), resizeAnchorTop_ - y);

    const auto widthScale =
        static_cast<float>(activeResizeCorner_ == ResizeCorner::LowerLeft ? widthFromRight : widthFromLeft) /
        static_cast<float>(kOverlayWidth);
    const auto heightScale =
        static_cast<float>(heightFromTop) / static_cast<float>(kOverlayHeight);
    ApplyScale(std::max(widthScale, heightScale), activeResizeCorner_ == ResizeCorner::LowerLeft);
}

void OverlayWindow::StopResizing() {
    resizing_ = false;
    activeResizeCorner_ = ResizeCorner::None;
}

}  // namespace xvatsim::modules::overlay
