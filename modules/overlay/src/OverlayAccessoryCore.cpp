#include "XVatsim/modules/overlay/OverlayAccessoryCore.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <utility>

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

namespace xvatsim::modules::overlay {

struct AccessoryTextMeasurementContext {
    struct ScaleCache {
        float scale = 1.0f;
        std::unique_ptr<Gdiplus::Font> drawerBodyFont;
        std::unique_ptr<Gdiplus::Font> drawerEntryTitleFont;
        std::unique_ptr<Gdiplus::Font> orbLabelFont;
        std::unique_ptr<Gdiplus::Font> orbOpenIndicatorFont;
        AccessoryTypographyMetrics typography;
        bool typographyPrepared = false;
    };

    ULONG_PTR gdiplusToken = 0;
    bool gdiplusStarted = false;
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    std::unique_ptr<Gdiplus::Graphics> graphics;
    std::map<int, ScaleCache> scales;
    AccessoryGdiMeasurementCounters counters;
    std::uint64_t nextTypographyGeneration = 1;
};

namespace {

std::size_t PreparationDrawerIndex(
    brain::BrainOwnedAccessoryDrawerId drawer) {
    switch (drawer) {
        case brain::BrainOwnedAccessoryDrawerId::Metar: return 0;
        case brain::BrainOwnedAccessoryDrawerId::Atis: return 1;
        case brain::BrainOwnedAccessoryDrawerId::Pdc: return 2;
        case brain::BrainOwnedAccessoryDrawerId::None:
        default: return 3;
    }
}

constexpr int kClosedDesignWidth = 430;
constexpr int kClosedDesignHeight = 374;
constexpr int kOpenDesignWidth = 430;
constexpr int kOpenDesignHeight = 506;
constexpr int kOrbDesignDiameter = 52;
constexpr int kMainCardToRailGap = 2;
constexpr int kInterOrbGap = 16;
constexpr int kRailToDrawerGap = 6;
constexpr int kOpenBottomMargin = 12;

constexpr AccessoryRect kMainCardDesignBounds{21, 28, 409, 332};
constexpr AccessoryRect kRailDesignBounds{118, 320, 306, 372};
constexpr AccessoryRect kDrawerDesignBounds{0, 378, 430, 494};

int ScaleDesignValue(int value, float scale) {
    return static_cast<int>(std::lround(static_cast<float>(value) * scale));
}

AccessoryRect ResolveDesignRect(
    const AccessoryRect& design,
    int resolvedLeft,
    int resolvedTop,
    float scale) {
    AccessoryRect result;
    result.left = resolvedLeft + ScaleDesignValue(design.left, scale);
    result.right = resolvedLeft + ScaleDesignValue(design.right, scale);
    result.top = resolvedTop - ScaleDesignValue(design.top, scale);
    result.bottom = resolvedTop - ScaleDesignValue(design.bottom, scale);
    return result;
}

bool RectHasArea(const AccessoryRect& rect) {
    return rect.right > rect.left && rect.top > rect.bottom;
}

bool PointInsideRect(const AccessoryRect& rect, int x, int y) {
    return RectHasArea(rect) && x >= rect.left && x <= rect.right &&
        y >= rect.bottom && y <= rect.top;
}

bool RectInsideScreen(
    const AccessoryRect& rect,
    int screenWidth,
    int screenHeight) {
    return RectHasArea(rect) && rect.left >= 0 && rect.right <= screenWidth &&
        rect.bottom >= 0 && rect.top <= screenHeight;
}

bool IsValidUtf8(const std::string& text) {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7f) {
            ++index;
            continue;
        }
        std::size_t count = 0;
        if (first >= 0xc2 && first <= 0xdf) count = 1;
        else if (first >= 0xe0 && first <= 0xef) count = 2;
        else if (first >= 0xf0 && first <= 0xf4) count = 3;
        else return false;
        if (index + count >= text.size()) return false;
        for (std::size_t offset = 1; offset <= count; ++offset) {
            if ((static_cast<unsigned char>(text[index + offset]) & 0xc0) != 0x80) {
                return false;
            }
        }
        if ((first == 0xe0 && static_cast<unsigned char>(text[index + 1]) < 0xa0) ||
            (first == 0xed && static_cast<unsigned char>(text[index + 1]) >= 0xa0) ||
            (first == 0xf0 && static_cast<unsigned char>(text[index + 1]) < 0x90) ||
            (first == 0xf4 && static_cast<unsigned char>(text[index + 1]) >= 0x90)) {
            return false;
        }
        index += count + 1;
    }
    return true;
}

std::string NormalizeLineEndings(const std::string& text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
            normalized.push_back('\n');
            ++index;
        } else {
            normalized.push_back(text[index]);
        }
    }
    return normalized;
}

std::string LimitUtf8(const std::string& text, std::size_t maximumBytes, bool* limited) {
    if (limited != nullptr) *limited = text.size() > maximumBytes;
    if (text.size() <= maximumBytes) return text;
    const std::string marker = "CONTENT LIMITED";
    if (maximumBytes <= marker.size()) return marker.substr(0, maximumBytes);
    std::size_t retained = maximumBytes - marker.size();
    while (retained > 0 && !IsValidUtf8(text.substr(0, retained))) --retained;
    return text.substr(0, retained) + marker;
}

struct AccessoryFontSpecification {
    const wchar_t* family = L"Segoe UI";
    float designPixelSize = 11.5f;
    int style = Gdiplus::FontStyleRegular;
    bool bold = false;
};

AccessoryFontSpecification FontSpecification(AccessoryFontRole role) {
    switch (role) {
        case AccessoryFontRole::DrawerEntryTitle:
            return {L"Segoe UI", 11.5f, Gdiplus::FontStyleBold, true};
        case AccessoryFontRole::OrbLabel:
            return {L"Segoe UI", 8.0f, Gdiplus::FontStyleBold, true};
        case AccessoryFontRole::OrbOpenIndicator:
            return {L"Segoe UI", 6.5f, Gdiplus::FontStyleBold, true};
        case AccessoryFontRole::DrawerBody:
        default:
            return {L"Segoe UI", 11.5f, Gdiplus::FontStyleRegular, false};
    }
}

int TypographyScaleKey(float scale) {
    return static_cast<int>(std::lround(scale * 1000.0f));
}

AccessoryTextMeasurementContext::ScaleCache* EnsureScaleCache(
    AccessoryTextMeasurementContext* context,
    float scale) {
    if (context == nullptr || context->graphics == nullptr || scale <= 0.0f) {
        return nullptr;
    }
    const int key = TypographyScaleKey(scale);
    const auto existing = context->scales.find(key);
    if (existing != context->scales.end()) return &existing->second;

    AccessoryTextMeasurementContext::ScaleCache cache;
    cache.scale = scale;
    const auto makeFont = [&](AccessoryFontRole role) {
        const auto specification = FontSpecification(role);
        auto font = std::make_unique<Gdiplus::Font>(
            specification.family,
            specification.designPixelSize * scale,
            specification.style,
            Gdiplus::UnitPixel);
        ++context->counters.fontConstructions;
        if (font->GetLastStatus() != Gdiplus::Ok) {
            return std::unique_ptr<Gdiplus::Font>{};
        }
        return font;
    };
    cache.drawerBodyFont = makeFont(AccessoryFontRole::DrawerBody);
    cache.drawerEntryTitleFont = makeFont(AccessoryFontRole::DrawerEntryTitle);
    cache.orbLabelFont = makeFont(AccessoryFontRole::OrbLabel);
    cache.orbOpenIndicatorFont = makeFont(AccessoryFontRole::OrbOpenIndicator);
    if (cache.drawerBodyFont == nullptr ||
        cache.drawerEntryTitleFont == nullptr || cache.orbLabelFont == nullptr ||
        cache.orbOpenIndicatorFont == nullptr) {
        return nullptr;
    }
    return &context->scales.emplace(key, std::move(cache)).first->second;
}

Gdiplus::Font* CachedFont(
    AccessoryTextMeasurementContext::ScaleCache* cache,
    AccessoryFontRole role) {
    if (cache == nullptr) return nullptr;
    switch (role) {
        case AccessoryFontRole::DrawerEntryTitle:
            return cache->drawerEntryTitleFont.get();
        case AccessoryFontRole::OrbLabel:
            return cache->orbLabelFont.get();
        case AccessoryFontRole::OrbOpenIndicator:
            return cache->orbOpenIndicatorFont.get();
        case AccessoryFontRole::DrawerBody:
        default:
            return cache->drawerBodyFont.get();
    }
}

bool Utf8ToWide(const std::string& text, std::wstring* wide) {
    if (wide == nullptr || !IsValidUtf8(text)) return false;
    wide->clear();
    if (text.empty()) return true;
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (required <= 0) return false;
    wide->resize(static_cast<std::size_t>(required));
    return MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        wide->data(),
        required) == required;
}

bool IsWrapWhitespace(wchar_t value) {
    return value == L' ' || value == L'\t';
}

bool WideToUtf8(
    const wchar_t* text,
    int length,
    std::string* utf8) {
    if (utf8 == nullptr || length < 0) return false;
    utf8->clear();
    if (length == 0) return true;
    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text,
        length,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) return false;
    utf8->resize(static_cast<std::size_t>(required));
    return WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text,
        length,
        utf8->data(),
        required,
        nullptr,
        nullptr) == required;
}

bool MeasureWideNoWrap(
    AccessoryTextMeasurementContext* context,
    const wchar_t* text,
    int length,
    float scale,
    AccessoryFontRole fontRole,
    int* measuredWidth,
    int* measuredHeight) {
    if (measuredWidth != nullptr) *measuredWidth = 0;
    if (measuredHeight != nullptr) *measuredHeight = 0;
    if (context == nullptr || text == nullptr || length < 0 || scale <= 0.0f) {
        return false;
    }
    auto* cache = EnsureScaleCache(context, scale);
    auto* font = CachedFont(cache, fontRole);
    if (font == nullptr || context->graphics == nullptr) return false;

    if (measuredHeight != nullptr) {
        *measuredHeight = static_cast<int>(
            std::ceil(font->GetHeight(context->graphics.get())));
    }
    if (length == 0) return true;

    Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
    format.SetFormatFlags(
        format.GetFormatFlags() |
        Gdiplus::StringFormatFlagsNoWrap |
        Gdiplus::StringFormatFlagsMeasureTrailingSpaces);
    Gdiplus::RectF bounds;
    ++context->counters.measurementCalls;
    const auto status = context->graphics->MeasureString(
        text,
        length,
        font,
        Gdiplus::PointF(0.0f, 0.0f),
        &format,
        &bounds);
    if (status != Gdiplus::Ok) return false;
    if (measuredWidth != nullptr) {
        *measuredWidth = static_cast<int>(std::ceil(bounds.Width));
    }
    if (measuredHeight != nullptr) {
        *measuredHeight = static_cast<int>(std::ceil(bounds.Height));
    }
    return true;
}

int PreviousScalarBoundary(const wchar_t* text, int boundary) {
    if (text == nullptr || boundary <= 0) return 0;
    int result = boundary - 1;
    if (result > 0 && text[result] >= 0xdc00 && text[result] <= 0xdfff &&
        text[result - 1] >= 0xd800 && text[result - 1] <= 0xdbff) {
        --result;
    }
    return result;
}

int NextScalarBoundary(const wchar_t* text, int length, int boundary) {
    if (text == nullptr || boundary >= length) return length;
    int result = boundary + 1;
    if (text[boundary] >= 0xd800 && text[boundary] <= 0xdbff &&
        result < length && text[result] >= 0xdc00 && text[result] <= 0xdfff) {
        ++result;
    }
    return result;
}

struct MeasuredLineFit {
    int characters = 0;
    int measuredWidth = 0;
    bool available = false;
};

MeasuredLineFit FitMeasuredWideTextLine(
    AccessoryTextMeasurementContext* context,
    const wchar_t* text,
    int length,
    int contentWidth,
    float scale,
    AccessoryFontRole fontRole) {
    MeasuredLineFit result;
    if (context == nullptr || text == nullptr || length <= 0 ||
        contentWidth <= 0) return result;
    auto* cache = EnsureScaleCache(context, scale);
    auto* font = CachedFont(cache, fontRole);
    if (font == nullptr || context->graphics == nullptr) return result;

    Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
    format.SetFormatFlags(
        format.GetFormatFlags() |
        Gdiplus::StringFormatFlagsMeasureTrailingSpaces);
    const float lineHeight = font->GetHeight(context->graphics.get());
    constexpr int initialWindowCharacters = 256;
    int windowCharacters = std::min(length, initialWindowCharacters);
    int proposedCharacters = 0;
    while (windowCharacters > 0) {
        Gdiplus::RectF bounds;
        INT charactersFitted = 0;
        INT linesFilled = 0;
        ++context->counters.measurementCalls;
        const auto status = context->graphics->MeasureString(
            text,
            windowCharacters,
            font,
            Gdiplus::RectF(
                0.0f,
                0.0f,
                static_cast<float>(contentWidth),
                std::ceil(lineHeight) + 1.0f),
            &format,
            &bounds,
            &charactersFitted,
            &linesFilled);
        if (status != Gdiplus::Ok || charactersFitted <= 0 || linesFilled <= 0) {
            return result;
        }
        proposedCharacters = std::min(charactersFitted, windowCharacters);
        if (proposedCharacters != windowCharacters ||
            windowCharacters == length) {
            break;
        }
        int exactWindowWidth = 0;
        if (!MeasureWideNoWrap(
                context,
                text,
                windowCharacters,
                scale,
                fontRole,
                &exactWindowWidth,
                nullptr)) {
            return result;
        }
        if (exactWindowWidth > contentWidth) break;
        windowCharacters = std::min(length, windowCharacters * 2);
    }

    proposedCharacters = std::clamp(proposedCharacters, 1, length);
    if (proposedCharacters < length && proposedCharacters > 0 &&
        text[proposedCharacters] >= 0xdc00 &&
        text[proposedCharacters] <= 0xdfff &&
        text[proposedCharacters - 1] >= 0xd800 &&
        text[proposedCharacters - 1] <= 0xdbff) {
        --proposedCharacters;
    }

    int exactWidth = 0;
    if (!MeasureWideNoWrap(
            context,
            text,
            proposedCharacters,
            scale,
            fontRole,
            &exactWidth,
            nullptr)) {
        return result;
    }
    while (proposedCharacters > 0 && exactWidth > contentWidth) {
        proposedCharacters = PreviousScalarBoundary(text, proposedCharacters);
        if (!MeasureWideNoWrap(
                context,
                text,
                proposedCharacters,
                scale,
                fontRole,
                &exactWidth,
                nullptr)) {
            return result;
        }
    }
    if (proposedCharacters == 0) {
        proposedCharacters = NextScalarBoundary(text, length, 0);
        if (!MeasureWideNoWrap(
                context,
                text,
                proposedCharacters,
                scale,
                fontRole,
                &exactWidth,
                nullptr) || exactWidth > contentWidth) {
            return result;
        }
    }

    const bool boundaryInsideWord = proposedCharacters < length &&
        !IsWrapWhitespace(text[proposedCharacters - 1]) &&
        !IsWrapWhitespace(text[proposedCharacters]);
    if (boundaryInsideWord) {
        while (proposedCharacters < length) {
            const int candidate =
                NextScalarBoundary(text, length, proposedCharacters);
            int candidateWidth = 0;
            if (!MeasureWideNoWrap(
                    context,
                    text,
                    candidate,
                    scale,
                    fontRole,
                    &candidateWidth,
                    nullptr)) {
                return result;
            }
            if (candidateWidth > contentWidth) break;
            proposedCharacters = candidate;
            exactWidth = candidateWidth;
        }
    }

    result.characters = proposedCharacters;
    result.measuredWidth = exactWidth;
    result.available = true;
    return result;
}

std::vector<std::string> WrapMeasuredText(
    AccessoryTextMeasurementContext* context,
    const std::string& text,
    int contentWidth,
    float scale,
    AccessoryFontRole fontRole,
    bool* measurementAvailable,
    bool* usedWordWrapping,
    bool* usedCharacterFallback,
    int* maximumMeasuredLineWidth) {
    if (measurementAvailable != nullptr) *measurementAvailable = true;
    if (usedWordWrapping != nullptr) *usedWordWrapping = false;
    if (usedCharacterFallback != nullptr) *usedCharacterFallback = false;
    if (maximumMeasuredLineWidth != nullptr) *maximumMeasuredLineWidth = 0;
    std::vector<std::string> lines;
    std::wstring wideText;
    if (!Utf8ToWide(text, &wideText)) {
        if (measurementAvailable != nullptr) *measurementAvailable = false;
        return lines;
    }

    const auto appendLine = [&](std::string line, int measuredWidth) {
        if (maximumMeasuredLineWidth != nullptr) {
            *maximumMeasuredLineWidth = std::max(
                *maximumMeasuredLineWidth,
                measuredWidth);
        }
        lines.push_back(std::move(line));
    };

    std::size_t paragraphStart = 0;
    while (paragraphStart <= wideText.size()) {
        const auto newline = wideText.find(L'\n', paragraphStart);
        const auto paragraphEnd =
            newline == std::wstring::npos ? wideText.size() : newline;
        std::size_t cursor = paragraphStart;
        while (cursor < paragraphEnd) {
            const auto fitted = FitMeasuredWideTextLine(
                context,
                wideText.data() + cursor,
                static_cast<int>(paragraphEnd - cursor),
                contentWidth,
                scale,
                fontRole);
            if (!fitted.available || fitted.characters <= 0 ||
                fitted.characters > static_cast<int>(paragraphEnd - cursor)) {
                if (measurementAvailable != nullptr) *measurementAvailable = false;
                return lines;
            }
            std::string line;
            if (!WideToUtf8(
                    wideText.data() + cursor,
                    fitted.characters,
                    &line)) {
                if (measurementAvailable != nullptr) *measurementAvailable = false;
                return lines;
            }
            const bool continues =
                cursor + static_cast<std::size_t>(fitted.characters) < paragraphEnd;
            if (continues) {
                const bool boundaryInsideWord =
                    !line.empty() &&
                    !IsWrapWhitespace(
                        wideText[cursor + fitted.characters - 1]) &&
                    !IsWrapWhitespace(wideText[cursor + fitted.characters]);
                if (boundaryInsideWord) {
                    if (usedCharacterFallback != nullptr) {
                        *usedCharacterFallback = true;
                    }
                } else if (usedWordWrapping != nullptr) {
                    *usedWordWrapping = true;
                }
            }
            appendLine(line,fitted.measuredWidth);
            cursor += static_cast<std::size_t>(fitted.characters);
        }
        if (cursor == paragraphStart) appendLine({},0);
        if (newline == std::wstring::npos) break;
        paragraphStart = newline + 1;
        if (paragraphStart == wideText.size()) {
            appendLine({},0);
            break;
        }
    }
    if (lines.empty()) appendLine({},0);
    return lines;
}

std::string DrawerToken(brain::BrainOwnedAccessoryDrawerId drawer) {
    switch (drawer) {
        case brain::BrainOwnedAccessoryDrawerId::Metar: return "METAR";
        case brain::BrainOwnedAccessoryDrawerId::Atis: return "ATIS";
        case brain::BrainOwnedAccessoryDrawerId::Pdc: return "PDC";
        case brain::BrainOwnedAccessoryDrawerId::None:
        default: return "NONE";
    }
}

std::string RailSignature(
    const brain::BrainOwnedAccessoryPresentationSnapshot& snapshot,
    const AccessoryLayoutResult& layout) {
    std::ostringstream stream;
    stream << snapshot.selectionGeneration << '|' << DrawerToken(snapshot.activeDrawer)
           << '|' << snapshot.contentGeneration
           << '|' << layout.closedWidth << 'x' << layout.closedHeight
           << '|' << layout.accessoriesVisible << '|' << layout.accessoriesInteractive;
    return stream.str();
}

std::string DrawerSignature(
    const brain::BrainOwnedAccessoryPresentationSnapshot& snapshot,
    const AccessoryLayoutResult& layout,
    int offset) {
    std::ostringstream stream;
    stream << snapshot.snapshotIdentity << '|' << snapshot.historyGeneration
           << '|' << snapshot.contentGeneration
           << '|' << DrawerToken(snapshot.activeDrawer) << '|'
           << layout.openWidth << 'x' << layout.openHeight << '|' << offset;
    return stream.str();
}

void AddCounters(AccessoryRenderCounters* target, const AccessoryRenderCounters& delta) {
    if (target == nullptr) return;
    target->historyVisits += delta.historyVisits;
    target->entryCopies += delta.entryCopies;
    target->wrapVisits += delta.wrapVisits;
    target->mainCardRasterRequests += delta.mainCardRasterRequests;
    target->railRasterRequests += delta.railRasterRequests;
    target->drawerRasterRequests += delta.drawerRasterRequests;
    target->uploadRequests += delta.uploadRequests;
    target->snapshotPublications += delta.snapshotPublications;
}

}  // namespace

bool AccessoryPreparationKey::operator==(
    const AccessoryPreparationKey& other) const {
    return drawer == other.drawer &&
        historyGeneration == other.historyGeneration &&
        contentGeneration == other.contentGeneration &&
        layoutGeneration == other.layoutGeneration &&
        typographyGeneration == other.typographyGeneration &&
        scaleThousandths == other.scaleThousandths &&
        contentWidth == other.contentWidth &&
        visibleLineCapacity == other.visibleLineCapacity;
}

const char* AccessoryPreparationWorkerStateToken(
    AccessoryPreparationWorkerState state) {
    switch (state) {
        case AccessoryPreparationWorkerState::Stopped: return "stopped";
        case AccessoryPreparationWorkerState::Starting: return "starting";
        case AccessoryPreparationWorkerState::Ready: return "ready";
        case AccessoryPreparationWorkerState::Failed: return "failed";
    }
    return "unknown";
}

const char* AccessoryPreparationWorkerFailureToken(
    AccessoryPreparationWorkerFailure failure) {
    switch (failure) {
        case AccessoryPreparationWorkerFailure::None: return "none";
        case AccessoryPreparationWorkerFailure::PriorityAssignment:
            return "priority-assignment";
        case AccessoryPreparationWorkerFailure::TextMeasurementInitialization:
            return "text-measurement-initialization";
    }
    return "unknown";
}

AccessoryPreparationAvailabilityDecision
ResolveAccessoryPreparationAvailability(
    AccessoryPreparationWorkerState state,
    bool pendingAction) {
    AccessoryPreparationAvailabilityDecision result;
    result.ready = state == AccessoryPreparationWorkerState::Ready;
    result.retryStartup = state == AccessoryPreparationWorkerState::Stopped;
    result.cancelPendingAction = pendingAction &&
        state == AccessoryPreparationWorkerState::Failed;
    return result;
}

struct AccessoryPreparationWorker::Implementation {
    explicit Implementation(AccessoryPreparationWorkerHooks configuredHooks)
        : hooks(std::move(configuredHooks)) {}

    mutable std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    bool stopRequested = false;
    std::atomic<AccessoryPreparationWorkerState> lifecycle{
        AccessoryPreparationWorkerState::Stopped};
    std::atomic<AccessoryPreparationWorkerFailure> failure{
        AccessoryPreparationWorkerFailure::None};
    std::atomic<std::uint64_t> readinessCheckCount{0};
    std::atomic<std::uint64_t> readinessContentionCount{0};
    std::atomic<std::uint64_t> publicationCount{0};
    std::atomic<std::uint64_t> maximumPublicationMicroseconds{0};
    std::atomic<std::uint64_t> enqueueAttemptCount{0};
    std::atomic<std::uint64_t> enqueueContentionCount{0};
    std::atomic<std::uint64_t> enqueueSuccessCount{0};
    std::atomic<std::uint64_t> enqueueReplacementCount{0};
    std::atomic<std::uint64_t> maximumEnqueueMicroseconds{0};
    std::atomic<std::uint64_t> requestsRejectedUnavailable{0};
    std::array<std::optional<AccessoryPreparationRequest>, 3> queued;
    std::array<std::optional<AccessoryPreparationKey>, 3> latestKeys;
    std::array<std::shared_ptr<const AccessoryPreparedDrawerPlan>, 3> ready;
    AccessoryPreparationWorkerCounters counters;
    AccessoryPreparationWorkerHooks hooks;

    void RecordMaximumPublication(std::uint64_t elapsed) {
        auto previous = maximumPublicationMicroseconds.load(
            std::memory_order_relaxed);
        while (previous < elapsed &&
               !maximumPublicationMicroseconds.compare_exchange_weak(
                   previous, elapsed, std::memory_order_relaxed)) {}
    }

    void RecordMaximumEnqueue(std::uint64_t elapsed) {
        auto previous = maximumEnqueueMicroseconds.load(
            std::memory_order_relaxed);
        while (previous < elapsed &&
               !maximumEnqueueMicroseconds.compare_exchange_weak(
                   previous, elapsed, std::memory_order_relaxed)) {}
    }

    void FailStartup(AccessoryPreparationWorkerFailure reason) {
        std::lock_guard<std::mutex> lock(mutex);
        for (std::size_t index = 0; index < queued.size(); ++index) {
            if (queued[index].has_value()) {
                ++counters.jobsCancelled;
            }
            queued[index].reset();
            latestKeys[index].reset();
            ready[index].reset();
        }
        failure.store(reason, std::memory_order_release);
        lifecycle.store(AccessoryPreparationWorkerState::Failed,
                        std::memory_order_release);
        counters.failure = reason;
        counters.lifecycleState = AccessoryPreparationWorkerState::Failed;
        ++counters.startupFailureCount;
        if (counters.startupFailureDiagnosticCount == 0) {
            ++counters.startupFailureDiagnosticCount;
        }
        counters.runningWorkerThreads = 0;
    }

    bool IsCurrent(const AccessoryPreparationKey& key) const {
        const auto index = PreparationDrawerIndex(key.drawer);
        return index < latestKeys.size() && latestKeys[index].has_value() &&
            latestKeys[index].value() == key;
    }

    std::size_t QueueDepth() const {
        return static_cast<std::size_t>(std::count_if(
            queued.begin(), queued.end(),
            [](const auto& item) { return item.has_value(); }));
    }

    std::size_t ReadyCount() const {
        return static_cast<std::size_t>(std::count_if(
            ready.begin(), ready.end(),
            [](const auto& item) { return item != nullptr; }));
    }

    bool Cancelled(const AccessoryPreparationKey& key) {
        std::lock_guard<std::mutex> lock(mutex);
        return stopRequested || !IsCurrent(key);
    }

    std::shared_ptr<const AccessoryPreparedDrawerPlan> Prepare(
        AccessoryTextMeasurementContext* context,
        const AccessoryPreparationRequest& request) {
        if (context == nullptr || request.snapshot == nullptr ||
            request.snapshot->status !=
                brain::BrainOwnedAccessoryOperationStatus::Available) {
            return nullptr;
        }
        auto plan = std::make_shared<AccessoryPreparedDrawerPlan>();
        plan->key = request.key;
        plan->workerThreadIdentity = GetCurrentThreadId();
        plan->layout.status = brain::BrainOwnedAccessoryOperationStatus::Available;
        plan->layout.drawer = request.key.drawer;
        plan->layout.visibleLineCapacity =
            request.layout.drawerVisibleLineCapacity;
        plan->layout.allWrappedLinesFit = true;
        plan->layout.totalScrollableLineCount = 1;
        for (const auto& entry : request.snapshot->entries) {
            if (Cancelled(request.key)) {
                return nullptr;
            }
            const int contentWidth = std::max(1,
                request.layout.drawerBounds.right -
                    request.layout.drawerBounds.left -
                    (2 * request.layout.drawerContentInset));
            AccessoryTextLayoutInput titleInput;
            titleInput.text = entry.title;
            titleInput.contentWidth = contentWidth;
            titleInput.maxRetainedBytes = static_cast<int>(entry.title.size() + 1);
            titleInput.scale = request.layout.scale;
            titleInput.fontRole = AccessoryFontRole::DrawerEntryTitle;
            const auto titleStarted = std::chrono::steady_clock::now();
            const auto title = BuildAccessoryTextLayout(context, titleInput);
            plan->maximumContiguousSliceMicroseconds = std::max(
                plan->maximumContiguousSliceMicroseconds,
                static_cast<std::uint64_t>(std::chrono::duration_cast<
                    std::chrono::microseconds>(std::chrono::steady_clock::now() -
                        titleStarted).count()));
            if (Cancelled(request.key)) return nullptr;
            AccessoryTextLayoutInput bodyInput = titleInput;
            bodyInput.text = entry.body;
            bodyInput.maxRetainedBytes = static_cast<int>(entry.body.size() + 1);
            bodyInput.fontRole = AccessoryFontRole::DrawerBody;
            const auto bodyStarted = std::chrono::steady_clock::now();
            const auto body = BuildAccessoryTextLayout(context, bodyInput);
            plan->maximumContiguousSliceMicroseconds = std::max(
                plan->maximumContiguousSliceMicroseconds,
                static_cast<std::uint64_t>(std::chrono::duration_cast<
                    std::chrono::microseconds>(std::chrono::steady_clock::now() -
                        bodyStarted).count()));
            if (title.status != brain::BrainOwnedAccessoryOperationStatus::Available ||
                body.status != brain::BrainOwnedAccessoryOperationStatus::Available) {
                return nullptr;
            }
            plan->layout.renderedEntryKeys.push_back(entry.stableKey);
            plan->layout.renderedEntryTitles.push_back(entry.title);
            plan->layout.renderedEntryBodies.push_back(entry.body);
            plan->layout.renderedTitleLines.push_back(title.lines);
            plan->layout.renderedBodyLines.push_back(body.lines);
            plan->layout.renderedAcceptedSequences.push_back(entry.acceptedSequence);
            plan->layout.totalScrollableLineCount +=
                static_cast<int>(title.lines.size() + body.lines.size());
            plan->layout.allWrappedLinesFit =
                plan->layout.allWrappedLinesFit && title.allLinesFit && body.allLinesFit;
            if (Cancelled(request.key)) return nullptr;
        }
        if (Cancelled(request.key)) {
            return nullptr;
        }
        plan->layout.maximumOffset = std::max(
            0,
            plan->layout.totalScrollableLineCount -
                plan->layout.visibleLineCapacity);
        plan->layout.finalMarker =
            "END OF " + DrawerToken(request.key.drawer) + " HISTORY";
        plan->layout.finalMarkerBelongsToSelectedHistory = true;
        return plan;
    }

    void Run() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            counters.workerThreadIdentity = GetCurrentThreadId();
            counters.runningWorkerThreads = 1;
            counters.priorityRequested = true;
        }
        const bool prioritySucceeded = hooks.assignBelowNormalPriority
            ? hooks.assignBelowNormalPriority()
            : SetThreadPriority(
                  GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL) != 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            counters.prioritySucceeded = prioritySucceeded;
        }
        if (!prioritySucceeded) {
            FailStartup(AccessoryPreparationWorkerFailure::PriorityAssignment);
            return;
        }
        auto* context = hooks.initializeTextMeasurement
            ? hooks.initializeTextMeasurement()
            : InitializeAccessoryTextMeasurement();
        if (context == nullptr) {
            FailStartup(
                AccessoryPreparationWorkerFailure::TextMeasurementInitialization);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            failure.store(AccessoryPreparationWorkerFailure::None,
                          std::memory_order_release);
            lifecycle.store(AccessoryPreparationWorkerState::Ready,
                            std::memory_order_release);
            counters.failure = AccessoryPreparationWorkerFailure::None;
            counters.lifecycleState = AccessoryPreparationWorkerState::Ready;
            ++counters.startupSuccessCount;
        }
        for (;;) {
            AccessoryPreparationRequest request;
            {
                std::unique_lock<std::mutex> lock(mutex);
                ++counters.workerSleepCount;
                wake.wait(lock, [&] {
                    return stopRequested || QueueDepth() != 0;
                });
                ++counters.workerWakeCount;
                if (stopRequested) {
                    break;
                }
                std::size_t selected = queued.size();
                for (std::size_t index = 0; index < queued.size(); ++index) {
                    if (queued[index].has_value()) {
                        selected = index;
                        break;
                    }
                }
                if (selected >= queued.size()) {
                    continue;
                }
                request = std::move(queued[selected].value());
                queued[selected].reset();
                ++counters.jobsStarted;
            }
            const auto started = std::chrono::steady_clock::now();
            const auto prepared = Prepare(context, request);
            const auto elapsed = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - started).count());
            std::lock_guard<std::mutex> lock(mutex);
            if (hooks.beforeReadyPublication) {
                hooks.beforeReadyPublication();
            }
            counters.maximumContiguousExecutionMicroseconds = std::max(
                counters.maximumContiguousExecutionMicroseconds,
                prepared != nullptr
                    ? prepared->maximumContiguousSliceMicroseconds
                    : elapsed);
            const auto index = PreparationDrawerIndex(request.key.drawer);
            if (index < counters.totalPreparationMicroseconds.size()) {
                counters.totalPreparationMicroseconds[index] += elapsed;
            }
            if (stopRequested || !IsCurrent(request.key)) {
                ++counters.jobsCancelled;
                ++counters.staleResultsRejected;
                continue;
            }
            if (prepared == nullptr) {
                ++counters.jobsCancelled;
                continue;
            }
            const_cast<AccessoryPreparedDrawerPlan*>(prepared.get())
                ->preparationMicroseconds = elapsed;
            ready[index] = prepared;
            ++counters.jobsCompleted;
            counters.maximumReadyCacheCount = std::max<std::uint64_t>(
                counters.maximumReadyCacheCount, ReadyCount());
        }
        if (hooks.shutdownTextMeasurement) {
            hooks.shutdownTextMeasurement(context);
        } else {
            ShutdownAccessoryTextMeasurement(context);
        }
        std::lock_guard<std::mutex> lock(mutex);
        counters.runningWorkerThreads = 0;
        lifecycle.store(AccessoryPreparationWorkerState::Stopped,
                        std::memory_order_release);
        counters.lifecycleState = AccessoryPreparationWorkerState::Stopped;
    }
};

AccessoryPreparationWorker::AccessoryPreparationWorker(
    AccessoryPreparationWorkerHooks hooks)
    : implementation_(std::make_unique<Implementation>(std::move(hooks))) {}

AccessoryPreparationWorker::~AccessoryPreparationWorker() {
    Stop();
}

bool AccessoryPreparationWorker::Start(std::uint64_t mainThreadIdentity) {
    auto& impl = *implementation_;
    const auto current = impl.lifecycle.load(std::memory_order_acquire);
    if (current == AccessoryPreparationWorkerState::Ready) {
        return true;
    }
    if (current == AccessoryPreparationWorkerState::Failed) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl.mutex);
    const auto lockedState = impl.lifecycle.load(std::memory_order_acquire);
    if (lockedState == AccessoryPreparationWorkerState::Ready) return true;
    if (lockedState == AccessoryPreparationWorkerState::Failed) return false;
    if (lockedState == AccessoryPreparationWorkerState::Starting) return true;
    impl.stopRequested = false;
    impl.queued = {};
    impl.latestKeys = {};
    impl.ready = {};
    impl.counters.mainThreadIdentity = mainThreadIdentity;
    impl.failure.store(AccessoryPreparationWorkerFailure::None,
                       std::memory_order_release);
    impl.lifecycle.store(AccessoryPreparationWorkerState::Starting,
                         std::memory_order_release);
    impl.counters.failure = AccessoryPreparationWorkerFailure::None;
    impl.counters.lifecycleState = AccessoryPreparationWorkerState::Starting;
    ++impl.counters.startupAttemptCount;
    impl.worker = std::thread([&impl] { impl.Run(); });
    return true;
}

void AccessoryPreparationWorker::Stop() {
    auto& impl = *implementation_;
    {
        std::lock_guard<std::mutex> lock(impl.mutex);
        if (impl.lifecycle.load(std::memory_order_acquire) ==
                AccessoryPreparationWorkerState::Stopped &&
            !impl.worker.joinable()) {
            return;
        }
        impl.stopRequested = true;
        for (auto& item : impl.queued) {
            if (item.has_value()) {
                ++impl.counters.jobsCancelled;
            }
            item.reset();
        }
        impl.ready = {};
    }
    impl.wake.notify_one();
    if (impl.worker.joinable()) {
        impl.worker.join();
    }
    std::lock_guard<std::mutex> lock(impl.mutex);
    impl.lifecycle.store(AccessoryPreparationWorkerState::Stopped,
                         std::memory_order_release);
    impl.counters.lifecycleState = AccessoryPreparationWorkerState::Stopped;
    impl.counters.runningWorkerThreads = 0;
}

bool AccessoryPreparationWorker::Request(
    const AccessoryPreparationRequest& request) {
    const auto started = std::chrono::steady_clock::now();
    const auto index = PreparationDrawerIndex(request.key.drawer);
    if (index >= 3 || request.snapshot == nullptr) {
        return false;
    }
    auto& impl = *implementation_;
    impl.enqueueAttemptCount.fetch_add(1, std::memory_order_relaxed);
    const auto finish = [&impl, started]() {
        const auto elapsed = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started).count());
        impl.RecordMaximumEnqueue(elapsed);
    };
    const auto initialState = impl.lifecycle.load(std::memory_order_acquire);
    if (initialState != AccessoryPreparationWorkerState::Starting &&
        initialState != AccessoryPreparationWorkerState::Ready) {
        impl.requestsRejectedUnavailable.fetch_add(1,
                                                   std::memory_order_relaxed);
        finish();
        return false;
    }
    std::unique_lock<std::mutex> lock(impl.mutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        impl.enqueueContentionCount.fetch_add(1, std::memory_order_relaxed);
        finish();
        return false;
    }
    const auto lockedState = impl.lifecycle.load(std::memory_order_acquire);
    if ((lockedState != AccessoryPreparationWorkerState::Starting &&
         lockedState != AccessoryPreparationWorkerState::Ready) ||
        impl.stopRequested) {
        impl.requestsRejectedUnavailable.fetch_add(1,
                                                   std::memory_order_relaxed);
        lock.unlock();
        finish();
        return false;
    }
    const bool replaced = impl.queued[index].has_value();
    ++impl.counters.jobsRequested;
    if (replaced) ++impl.counters.jobsReplaced;
    impl.latestKeys[index] = request.key;
    impl.queued[index] = request;
    if (impl.ready[index] != nullptr &&
        !(impl.ready[index]->key == request.key)) {
        impl.ready[index].reset();
    }
    impl.counters.maximumQueueDepth = std::max<std::uint64_t>(
        impl.counters.maximumQueueDepth, impl.QueueDepth());
    impl.enqueueSuccessCount.fetch_add(1, std::memory_order_relaxed);
    if (replaced) {
        impl.enqueueReplacementCount.fetch_add(1, std::memory_order_relaxed);
    }
    lock.unlock();
    finish();
    impl.wake.notify_one();
    return true;
}

std::shared_ptr<const AccessoryPreparedDrawerPlan>
AccessoryPreparationWorker::TryTakeReady(
    const AccessoryPreparationKey& key,
    std::uint64_t* publicationMicroseconds) {
    const auto started = std::chrono::steady_clock::now();
    std::shared_ptr<const AccessoryPreparedDrawerPlan> result;
    auto& impl = *implementation_;
    impl.readinessCheckCount.fetch_add(1, std::memory_order_relaxed);
    const auto index = PreparationDrawerIndex(key.drawer);
    if (index < 3 && impl.lifecycle.load(std::memory_order_acquire) ==
            AccessoryPreparationWorkerState::Ready) {
        std::unique_lock<std::mutex> lock(impl.mutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            impl.readinessContentionCount.fetch_add(
                1, std::memory_order_relaxed);
        } else
        if (impl.ready[index] != nullptr && impl.ready[index]->key == key) {
            result = impl.ready[index];
            impl.publicationCount.fetch_add(1, std::memory_order_relaxed);
        }
    }
    const auto elapsed = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count());
    if (publicationMicroseconds != nullptr) {
        *publicationMicroseconds = elapsed;
    }
    impl.RecordMaximumPublication(elapsed);
    return result;
}

void AccessoryPreparationWorker::CancelAll() {
    auto& impl = *implementation_;
    std::lock_guard<std::mutex> lock(impl.mutex);
    for (std::size_t index = 0; index < impl.queued.size(); ++index) {
        if (impl.queued[index].has_value()) {
            ++impl.counters.jobsCancelled;
        }
        impl.queued[index].reset();
        impl.latestKeys[index].reset();
        impl.ready[index].reset();
    }
}

bool AccessoryPreparationWorker::Running() const {
    const auto& impl = *implementation_;
    return impl.lifecycle.load(std::memory_order_acquire) ==
        AccessoryPreparationWorkerState::Ready;
}

AccessoryPreparationWorkerState AccessoryPreparationWorker::State() const {
    return implementation_->lifecycle.load(std::memory_order_acquire);
}

AccessoryPreparationWorkerFailure AccessoryPreparationWorker::Failure() const {
    return implementation_->failure.load(std::memory_order_acquire);
}

AccessoryPreparationWorkerCounters
AccessoryPreparationWorker::SnapshotCounters() const {
    const auto& impl = *implementation_;
    std::lock_guard<std::mutex> lock(impl.mutex);
    auto snapshot = impl.counters;
    snapshot.lifecycleState = impl.lifecycle.load(std::memory_order_acquire);
    snapshot.failure = impl.failure.load(std::memory_order_acquire);
    snapshot.readinessCheckCount = impl.readinessCheckCount.load(
        std::memory_order_relaxed);
    snapshot.readinessContentionCount = impl.readinessContentionCount.load(
        std::memory_order_relaxed);
    snapshot.publicationCount = impl.publicationCount.load(
        std::memory_order_relaxed);
    snapshot.maximumPublicationMicroseconds =
        impl.maximumPublicationMicroseconds.load(std::memory_order_relaxed);
    snapshot.enqueueAttemptCount = impl.enqueueAttemptCount.load(
        std::memory_order_relaxed);
    snapshot.enqueueContentionCount = impl.enqueueContentionCount.load(
        std::memory_order_relaxed);
    snapshot.enqueueSuccessCount = impl.enqueueSuccessCount.load(
        std::memory_order_relaxed);
    snapshot.enqueueReplacementCount = impl.enqueueReplacementCount.load(
        std::memory_order_relaxed);
    snapshot.maximumEnqueueMicroseconds =
        impl.maximumEnqueueMicroseconds.load(std::memory_order_relaxed);
    snapshot.requestsRejectedUnavailable =
        impl.requestsRejectedUnavailable.load(std::memory_order_relaxed);
    return snapshot;
}

AccessoryTextMeasurementContext* InitializeAccessoryTextMeasurement() {
    auto context = std::make_unique<AccessoryTextMeasurementContext>();
    Gdiplus::GdiplusStartupInput startupInput;
    if (Gdiplus::GdiplusStartup(
            &context->gdiplusToken,
            &startupInput,
            nullptr) != Gdiplus::Ok) {
        return nullptr;
    }
    context->gdiplusStarted = true;
    context->bitmap = std::make_unique<Gdiplus::Bitmap>(8, 8, PixelFormat32bppARGB);
    ++context->counters.bitmapConstructions;
    if (context->bitmap->GetLastStatus() != Gdiplus::Ok) {
        ShutdownAccessoryTextMeasurement(context.release());
        return nullptr;
    }
    context->graphics = std::make_unique<Gdiplus::Graphics>(context->bitmap.get());
    ++context->counters.graphicsConstructions;
    if (context->graphics->GetLastStatus() != Gdiplus::Ok) {
        ShutdownAccessoryTextMeasurement(context.release());
        return nullptr;
    }
    context->graphics->SetTextRenderingHint(
        Gdiplus::TextRenderingHintAntiAliasGridFit);
    context->graphics->SetPageUnit(Gdiplus::UnitPixel);
    return context.release();
}

void ShutdownAccessoryTextMeasurement(AccessoryTextMeasurementContext* context) {
    if (context == nullptr) return;
    context->scales.clear();
    context->graphics.reset();
    context->bitmap.reset();
    if (context->gdiplusStarted) {
        Gdiplus::GdiplusShutdown(context->gdiplusToken);
        context->gdiplusStarted = false;
    }
    delete context;
}

AccessoryGdiMeasurementCounters GetAccessoryGdiMeasurementCounters(
    const AccessoryTextMeasurementContext* context) {
    return context == nullptr
        ? AccessoryGdiMeasurementCounters{}
        : context->counters;
}

AccessoryTextMeasurementResult MeasureAccessoryText(
    AccessoryTextMeasurementContext* context,
    const AccessoryTextMeasurementInput& input) {
    AccessoryTextMeasurementResult result;
    if (context == nullptr || input.scale <= 0.0f) return result;
    std::wstring wide;
    if (!Utf8ToWide(input.text, &wide)) return result;

    const auto specification = FontSpecification(input.role);
    auto* cache = EnsureScaleCache(context, input.scale);
    auto* font = CachedFont(cache, input.role);
    if (cache == nullptr || font == nullptr || context->graphics == nullptr) {
        return result;
    }
    result.fontFamily = "Segoe UI";
    result.fontPixelSize = specification.designPixelSize * input.scale;
    result.bold = specification.bold;
    if (!MeasureWideNoWrap(
            context,
            wide.data(),
            static_cast<int>(wide.size()),
            input.scale,
            input.role,
            &result.measuredWidth,
            &result.measuredHeight)) {
        return AccessoryTextMeasurementResult{};
    }
    result.status = brain::BrainOwnedAccessoryOperationStatus::Available;
    return result;
}

AccessoryTypographyMetrics PrepareAccessoryTypography(
    AccessoryTextMeasurementContext* context,
    float scale) {
    AccessoryTypographyMetrics unavailable;
    if (context == nullptr || scale <= 0.0f) return unavailable;
    auto* cache = EnsureScaleCache(context, scale);
    if (cache == nullptr) return unavailable;
    if (cache->typographyPrepared) return cache->typography;

    AccessoryTypographyMetrics metrics;
    metrics.scale = scale;
    const char* labels[]{"METAR", "ATIS", "PDC"};
    for (std::size_t index = 0; index < 3; ++index) {
        AccessoryTextMeasurementInput input;
        input.text = labels[index];
        input.role = AccessoryFontRole::OrbLabel;
        input.scale = scale;
        const auto measured = MeasureAccessoryText(context, input);
        if (measured.status != brain::BrainOwnedAccessoryOperationStatus::Available) {
            return unavailable;
        }
        metrics.orbLabels[index] = {
            measured.measuredWidth,
            measured.measuredHeight};
    }
    AccessoryTextMeasurementInput indicatorInput;
    indicatorInput.text = "OPEN";
    indicatorInput.role = AccessoryFontRole::OrbOpenIndicator;
    indicatorInput.scale = scale;
    const auto indicator = MeasureAccessoryText(context, indicatorInput);
    if (indicator.status != brain::BrainOwnedAccessoryOperationStatus::Available) {
        return unavailable;
    }
    metrics.openIndicator = {
        indicator.measuredWidth,
        indicator.measuredHeight};
    AccessoryTextMeasurementInput titleLineInput;
    titleLineInput.text = "Ag";
    titleLineInput.role = AccessoryFontRole::DrawerEntryTitle;
    titleLineInput.scale = scale;
    const auto titleLine = MeasureAccessoryText(context, titleLineInput);
    AccessoryTextMeasurementInput bodyLineInput = titleLineInput;
    bodyLineInput.role = AccessoryFontRole::DrawerBody;
    const auto bodyLine = MeasureAccessoryText(context, bodyLineInput);
    if (titleLine.status != brain::BrainOwnedAccessoryOperationStatus::Available ||
        bodyLine.status != brain::BrainOwnedAccessoryOperationStatus::Available) {
        return unavailable;
    }
    metrics.drawerTitleLine = {
        titleLine.measuredWidth,
        titleLine.measuredHeight};
    metrics.drawerBodyLine = {
        bodyLine.measuredWidth,
        bodyLine.measuredHeight};
    metrics.status = brain::BrainOwnedAccessoryOperationStatus::Available;
    metrics.generation = context->nextTypographyGeneration++;
    cache->typography = metrics;
    cache->typographyPrepared = true;
    return cache->typography;
}

AccessoryLayoutResult ResolveAccessoryLayout(const AccessoryLayoutInput& input) {
    AccessoryLayoutResult result;
    if (input.screenWidth <= 0 || input.screenHeight <= 0 || input.scale <= 0.0f) return result;

    result.status = brain::BrainOwnedAccessoryOperationStatus::Available;
    result.scale = input.scale;
    result.closedDesignWidth = kClosedDesignWidth;
    result.closedDesignHeight = kClosedDesignHeight;
    result.openDesignWidth = kOpenDesignWidth;
    result.openDesignHeight = kOpenDesignHeight;
    result.orbDiameterDesignPixels = kOrbDesignDiameter;
    result.railDesignBounds = kRailDesignBounds;
    result.drawerDesignBounds = kDrawerDesignBounds;
    result.mainCardToRailGapDesignPixels = kMainCardToRailGap;
    result.interOrbGapDesignPixels = kInterOrbGap;
    result.railToDrawerGapDesignPixels = kRailToDrawerGap;
    result.openBottomMarginDesignPixels = kOpenBottomMargin;
    result.closedWidth = ScaleDesignValue(kClosedDesignWidth, input.scale);
    result.closedHeight = ScaleDesignValue(kClosedDesignHeight, input.scale);
    result.openWidth = ScaleDesignValue(kOpenDesignWidth, input.scale);
    result.openHeight = ScaleDesignValue(kOpenDesignHeight, input.scale);

    const int activeWidth = input.drawerOpen ? result.openWidth : result.closedWidth;
    const int activeHeight = input.drawerOpen ? result.openHeight : result.closedHeight;
    int requestedLeft = input.windowLeft;
    int requestedTop = input.windowTop;
    const bool restoreSavedClosedAnchor =
        input.anchorOperation == AccessoryAnchorOperation::CloseDrawer &&
        !input.drawerOpen && input.savedClosedAnchorValid;
    if (restoreSavedClosedAnchor) {
        requestedLeft = input.savedClosedAnchorLeft;
        requestedTop = input.savedClosedAnchorTop;
        result.closedDimensionsRestored = true;
        result.closedAnchorRestored = true;
    }
    const int maximumLeft = std::max(0, input.screenWidth - activeWidth);
    const int minimumTop = std::min(activeHeight, input.screenHeight);
    result.resolvedLeft = std::clamp(requestedLeft, 0, maximumLeft);
    result.resolvedTop = std::clamp(requestedTop, minimumTop, input.screenHeight);
    const bool clampApplied = result.resolvedLeft != requestedLeft || result.resolvedTop != requestedTop;
    const bool intentionalMove =
        input.anchorOperation == AccessoryAnchorOperation::IntentionalMove;
    result.intentionalMoveApplied = intentionalMove;
    result.positionSettingsDirty = intentionalMove;
    result.temporaryClampApplied = clampApplied && !intentionalMove;
    result.restorableClosedAnchorLeft = input.savedClosedAnchorLeft;
    result.restorableClosedAnchorTop = input.savedClosedAnchorTop;
    const bool updateClosedAnchorFromResolved =
        intentionalMove || restoreSavedClosedAnchor ||
        (!input.drawerOpen &&
         (input.anchorOperation == AccessoryAnchorOperation::Initialize ||
          input.anchorOperation == AccessoryAnchorOperation::OrdinaryRefresh ||
          input.anchorOperation == AccessoryAnchorOperation::ScreenBoundsChanged));
    if (updateClosedAnchorFromResolved) {
        result.restorableClosedAnchorLeft = result.resolvedLeft;
        result.restorableClosedAnchorTop = result.resolvedTop;
    }

    result.closedBounds = {result.resolvedLeft, result.resolvedTop,
        result.resolvedLeft + result.closedWidth, result.resolvedTop - result.closedHeight};
    result.openBounds = {result.resolvedLeft, result.resolvedTop,
        result.resolvedLeft + result.openWidth, result.resolvedTop - result.openHeight};
    result.resolvedBounds = input.drawerOpen ? result.openBounds : result.closedBounds;
    result.mainCardBounds = ResolveDesignRect(
        kMainCardDesignBounds, result.resolvedLeft, result.resolvedTop, input.scale);
    result.railBounds = ResolveDesignRect(
        kRailDesignBounds, result.resolvedLeft, result.resolvedTop, input.scale);
    if (input.drawerOpen) {
        result.drawerBounds = ResolveDesignRect(
            kDrawerDesignBounds, result.resolvedLeft, result.resolvedTop, input.scale);
        const int drawerHeight = result.drawerBounds.top - result.drawerBounds.bottom;
        result.drawerContentInset = std::max(
            ScaleDesignValue(8, input.scale),
            ScaleDesignValue(12, input.scale));
        result.drawerHeaderTop = std::max(
            ScaleDesignValue(3, input.scale),
            ScaleDesignValue(5, input.scale));
        const bool drawerTypographyAvailable = input.typography != nullptr &&
            input.typography->status ==
                brain::BrainOwnedAccessoryOperationStatus::Available &&
            TypographyScaleKey(input.typography->scale) ==
                TypographyScaleKey(input.scale);
        const int measuredHeaderHeight = drawerTypographyAvailable
            ? input.typography->drawerTitleLine.height
            : 0;
        result.drawerHeaderHeight = std::max(
            ScaleDesignValue(18, input.scale), measuredHeaderHeight);
        result.drawerContentTop = result.drawerHeaderTop +
            result.drawerHeaderHeight + ScaleDesignValue(2, input.scale);
        result.drawerContentBottom = drawerHeight - std::max(
            ScaleDesignValue(5, input.scale),
            ScaleDesignValue(7, input.scale));
        result.drawerLineHeight = ScaleDesignValue(13, input.scale);
        if (drawerTypographyAvailable && result.drawerLineHeight > 0) {
            result.drawerVisibleLineCapacity = std::max(
                1,
                (result.drawerContentBottom - result.drawerContentTop) /
                    result.drawerLineHeight);
        }
    }

    result.accessoriesVisible = input.cardAnimationProgress >= 1.0f;
    result.accessoriesInteractive = result.accessoriesVisible;
    const brain::BrainOwnedAccessoryDrawerId drawers[]{
        brain::BrainOwnedAccessoryDrawerId::Metar,
        brain::BrainOwnedAccessoryDrawerId::Atis,
        brain::BrainOwnedAccessoryDrawerId::Pdc};
    const int designLeft[]{118, 186, 254};
    const int designCenterX[]{144, 212, 280};
    for (std::size_t index = 0; index < 3; ++index) {
        AccessoryOrbLayout orb;
        orb.drawer = drawers[index];
        orb.designBounds = {designLeft[index], 320, designLeft[index] + 52, 372};
        orb.designCenter = {designCenterX[index], 346};
        orb.diameterPhysicalPixels = ScaleDesignValue(kOrbDesignDiameter, input.scale);
        const int centerX = result.resolvedLeft + ScaleDesignValue(orb.designCenter.x, input.scale);
        const int centerY = result.resolvedTop - ScaleDesignValue(orb.designCenter.y, input.scale);
        orb.bounds.left = centerX - orb.diameterPhysicalPixels / 2;
        orb.bounds.right = orb.bounds.left + orb.diameterPhysicalPixels;
        orb.bounds.bottom = centerY - orb.diameterPhysicalPixels / 2;
        orb.bounds.top = orb.bounds.bottom + orb.diameterPhysicalPixels;
        orb.visible = result.accessoriesVisible;
        orb.interactive = result.accessoriesInteractive;
        orb.labelAvailableWidth = std::max(
            0,
            orb.diameterPhysicalPixels - ScaleDesignValue(8, input.scale));
        orb.labelAvailableHeight = ScaleDesignValue(14, input.scale);
        orb.openIndicatorAvailableWidth = std::max(
            0,
            orb.diameterPhysicalPixels - ScaleDesignValue(12, input.scale));
        orb.openIndicatorAvailableHeight = ScaleDesignValue(10, input.scale);
        const bool typographyAvailable = input.typography != nullptr &&
            input.typography->status ==
                brain::BrainOwnedAccessoryOperationStatus::Available &&
            TypographyScaleKey(input.typography->scale) ==
                TypographyScaleKey(input.scale);
        const auto labelMeasurement = typographyAvailable
            ? input.typography->orbLabels[index]
            : AccessoryTextExtent{};
        const auto indicatorMeasurement = typographyAvailable
            ? input.typography->openIndicator
            : AccessoryTextExtent{};
        orb.labelMeasuredWidth = labelMeasurement.width;
        orb.labelMeasuredHeight = labelMeasurement.height;
        orb.openIndicatorMeasuredWidth = indicatorMeasurement.width;
        orb.openIndicatorMeasuredHeight = indicatorMeasurement.height;
        orb.labelFits =
            typographyAvailable &&
            orb.labelMeasuredWidth <= orb.labelAvailableWidth &&
            orb.labelMeasuredHeight <= orb.labelAvailableHeight;
        orb.openIndicatorFits =
            typographyAvailable &&
            orb.openIndicatorMeasuredWidth <= orb.openIndicatorAvailableWidth &&
            orb.openIndicatorMeasuredHeight <= orb.openIndicatorAvailableHeight;
        result.orbs.push_back(orb);
    }

    bool inside = RectInsideScreen(result.resolvedBounds, input.screenWidth, input.screenHeight) &&
        RectInsideScreen(result.mainCardBounds, input.screenWidth, input.screenHeight) &&
        RectInsideScreen(result.railBounds, input.screenWidth, input.screenHeight);
    if (input.drawerOpen) {
        inside = inside && RectInsideScreen(result.drawerBounds, input.screenWidth, input.screenHeight);
    }
    for (const auto& orb : result.orbs) {
        inside = inside && RectInsideScreen(orb.bounds, input.screenWidth, input.screenHeight);
    }
    result.allPhysicalBoundsInsideScreen = inside;
    return result;
}

AccessoryAnchorUpdateResult UpdateAccessoryAnchorState(
    AccessoryAnchorState* state,
    const AccessoryAnchorUpdateInput& input) {
    AccessoryAnchorUpdateResult result;
    if (state == nullptr) {
        return result;
    }

    const auto previous = *state;
    const auto operation = input.operation;
    const bool initialize = !state->initialized ||
        operation == AccessoryAnchorOperation::Initialize;
    if (initialize) {
        state->initialized = true;
        state->drawerOpen = input.layout.drawerOpen;
        state->currentAnchorValid = true;
        state->currentAnchorLeft = input.layout.windowLeft;
        state->currentAnchorTop = input.layout.windowTop;
        state->temporaryOpenClampApplied = false;
        state->intentionalMoveSinceOpen = false;
        if (!state->drawerOpen) {
            state->savedClosedAnchorValid = true;
            state->savedClosedAnchorLeft = state->currentAnchorLeft;
            state->savedClosedAnchorTop = state->currentAnchorTop;
        }
    } else {
        if (operation == AccessoryAnchorOperation::OpenDrawer &&
            !state->drawerOpen) {
            state->currentAnchorValid = true;
            state->currentAnchorLeft = input.layout.windowLeft;
            state->currentAnchorTop = input.layout.windowTop;
            state->savedClosedAnchorValid = true;
            state->savedClosedAnchorLeft = state->currentAnchorLeft;
            state->savedClosedAnchorTop = state->currentAnchorTop;
            state->drawerOpen = true;
            state->temporaryOpenClampApplied = false;
            state->intentionalMoveSinceOpen = false;
            result.savedClosedAnchorCaptured = true;
        } else if (operation == AccessoryAnchorOperation::CloseDrawer) {
            state->drawerOpen = false;
        } else {
            state->drawerOpen = input.layout.drawerOpen;
            state->currentAnchorValid = true;
            state->currentAnchorLeft = input.layout.windowLeft;
            state->currentAnchorTop = input.layout.windowTop;
        }
    }

    AccessoryLayoutInput layoutInput = input.layout;
    layoutInput.drawerOpen = state->drawerOpen;
    layoutInput.anchorOperation = operation;
    layoutInput.windowLeft = state->currentAnchorLeft;
    layoutInput.windowTop = state->currentAnchorTop;
    layoutInput.savedClosedAnchorValid = state->savedClosedAnchorValid;
    layoutInput.savedClosedAnchorLeft = state->savedClosedAnchorLeft;
    layoutInput.savedClosedAnchorTop = state->savedClosedAnchorTop;
    result.layout = ResolveAccessoryLayout(layoutInput);
    result.status = result.layout.status;
    if (result.status != brain::BrainOwnedAccessoryOperationStatus::Available) {
        *state = previous;
        return result;
    }

    state->currentAnchorValid = true;
    state->currentAnchorLeft = result.layout.resolvedLeft;
    state->currentAnchorTop = result.layout.resolvedTop;
    if (operation == AccessoryAnchorOperation::IntentionalMove) {
        state->savedClosedAnchorValid = true;
        state->savedClosedAnchorLeft = result.layout.resolvedLeft;
        state->savedClosedAnchorTop = result.layout.resolvedTop;
        state->intentionalMoveSinceOpen = state->drawerOpen;
        state->temporaryOpenClampApplied = false;
    } else if (operation == AccessoryAnchorOperation::CloseDrawer) {
        state->savedClosedAnchorValid = true;
        state->savedClosedAnchorLeft = result.layout.resolvedLeft;
        state->savedClosedAnchorTop = result.layout.resolvedTop;
        state->temporaryOpenClampApplied = false;
        state->intentionalMoveSinceOpen = false;
        result.explicitClosedAnchorRestored =
            result.layout.closedAnchorRestored;
    } else if (!state->drawerOpen &&
               (operation == AccessoryAnchorOperation::Initialize ||
                operation == AccessoryAnchorOperation::OrdinaryRefresh ||
                operation == AccessoryAnchorOperation::ScreenBoundsChanged)) {
        state->savedClosedAnchorValid = true;
        state->savedClosedAnchorLeft = result.layout.resolvedLeft;
        state->savedClosedAnchorTop = result.layout.resolvedTop;
    }

    if (state->drawerOpen &&
        operation != AccessoryAnchorOperation::IntentionalMove &&
        operation != AccessoryAnchorOperation::OrdinaryRefresh) {
        state->temporaryOpenClampApplied =
            state->temporaryOpenClampApplied ||
            result.layout.temporaryClampApplied;
    }
    result.intentionalMoveApplied = result.layout.intentionalMoveApplied;
    result.positionSettingsDirty = result.layout.positionSettingsDirty;
    result.temporaryOpenClampApplied = state->temporaryOpenClampApplied;

    const bool changed =
        previous.initialized != state->initialized ||
        previous.drawerOpen != state->drawerOpen ||
        previous.currentAnchorValid != state->currentAnchorValid ||
        previous.currentAnchorLeft != state->currentAnchorLeft ||
        previous.currentAnchorTop != state->currentAnchorTop ||
        previous.savedClosedAnchorValid != state->savedClosedAnchorValid ||
        previous.savedClosedAnchorLeft != state->savedClosedAnchorLeft ||
        previous.savedClosedAnchorTop != state->savedClosedAnchorTop ||
        previous.temporaryOpenClampApplied !=
            state->temporaryOpenClampApplied ||
        previous.intentionalMoveSinceOpen != state->intentionalMoveSinceOpen;
    if (changed) {
        ++state->transitionGeneration;
        if (state->transitionGeneration == 0) {
            state->transitionGeneration = 1;
        }
    }
    return result;
}

AccessoryHitTestResult HitTestAccessoryOrb(const AccessoryLayoutResult& layout, int x, int y) {
    AccessoryHitTestResult result;
    if (layout.status != brain::BrainOwnedAccessoryOperationStatus::Available) return result;
    result.status = brain::BrainOwnedAccessoryOperationStatus::Available;
    if (!layout.accessoriesVisible || !layout.accessoriesInteractive) return result;
    for (const auto& orb : layout.orbs) {
        if (!orb.visible || !orb.interactive) continue;
        const int centerX = (orb.bounds.left + orb.bounds.right) / 2;
        const int centerY = (orb.bounds.bottom + orb.bounds.top) / 2;
        const int radius = orb.diameterPhysicalPixels / 2;
        const long long dx = static_cast<long long>(x) - centerX;
        const long long dy = static_cast<long long>(y) - centerY;
        if (dx * dx + dy * dy <= static_cast<long long>(radius) * radius) {
            result.handled = true;
            result.drawer = orb.drawer;
            return result;
        }
    }
    return result;
}

AccessoryTextLayoutResult BuildAccessoryTextLayout(
    AccessoryTextMeasurementContext* context,
    const AccessoryTextLayoutInput& input) {
    AccessoryTextLayoutResult result;
    if (context == nullptr || input.contentWidth <= 0 ||
        input.maxRetainedBytes <= 0 || !IsValidUtf8(input.text)) {
        return result;
    }
    const auto started = std::chrono::steady_clock::now();
    result.status = brain::BrainOwnedAccessoryOperationStatus::Available;
    const auto normalized = NormalizeLineEndings(input.text);
    bool limited = false;
    result.reconstructedText = LimitUtf8(
        normalized, static_cast<std::size_t>(input.maxRetainedBytes), &limited);
    result.lines = WrapMeasuredText(
        context,
        result.reconstructedText,
        input.contentWidth,
        input.scale,
        input.fontRole,
        &result.measurementAvailable,
        &result.usedWordWrapping,
        &result.usedCharacterFallback,
        &result.maximumMeasuredLineWidth);
    result.allLinesFit = result.measurementAvailable && !result.lines.empty() &&
        result.maximumMeasuredLineWidth <= input.contentWidth;
    result.contentLimited = limited;
    result.validUtf8 = IsValidUtf8(result.reconstructedText);
    result.endedAtValidUtf8Boundary = result.validUtf8;
    result.contentLimitedMarkerVisible = limited &&
        result.reconstructedText.find("CONTENT LIMITED") != std::string::npos;
    result.retainedBytes = result.reconstructedText.size();
    result.elapsedMicroseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count());
    return result;
}

AccessoryHistoryLayoutResult BuildAccessoryHistoryLayout(
    AccessoryTextMeasurementContext* context,
    const brain::BrainOwnedAccessoryPresentationSnapshot& snapshot,
    const AccessoryLayoutResult& layout) {
    AccessoryHistoryLayoutResult result;
    const int contentWidth =
        (layout.drawerBounds.right - layout.drawerBounds.left) -
        (layout.drawerContentInset * 2);
    const int visibleLineCount = layout.drawerVisibleLineCapacity;
    if (snapshot.status != brain::BrainOwnedAccessoryOperationStatus::Available ||
        snapshot.activeDrawer == brain::BrainOwnedAccessoryDrawerId::None ||
        context == nullptr ||
        layout.status != brain::BrainOwnedAccessoryOperationStatus::Available ||
        contentWidth <= 0 || visibleLineCount <= 0) return result;
    result.status = brain::BrainOwnedAccessoryOperationStatus::Available;
    result.drawer = snapshot.activeDrawer;
    result.allWrappedLinesFit = true;
    int totalLineCount = 1;
    for (const auto& entry : snapshot.entries) {
        result.renderedEntryKeys.push_back(entry.stableKey);
        result.renderedEntryTitles.push_back(entry.title);
        result.renderedEntryBodies.push_back(entry.body);
        result.renderedAcceptedSequences.push_back(entry.acceptedSequence);
        AccessoryTextLayoutInput titleInput;
        titleInput.text = entry.title;
        titleInput.contentWidth = contentWidth;
        titleInput.maxRetainedBytes = static_cast<int>(entry.title.size() + 1);
        titleInput.scale = layout.scale;
        titleInput.fontRole = AccessoryFontRole::DrawerEntryTitle;
        const auto titleLayout = BuildAccessoryTextLayout(context, titleInput);
        AccessoryTextLayoutInput bodyInput = titleInput;
        bodyInput.text = entry.body;
        bodyInput.maxRetainedBytes = static_cast<int>(entry.body.size() + 1);
        bodyInput.fontRole = AccessoryFontRole::DrawerBody;
        const auto bodyLayout = BuildAccessoryTextLayout(context, bodyInput);
        if (titleLayout.status != brain::BrainOwnedAccessoryOperationStatus::Available ||
            bodyLayout.status != brain::BrainOwnedAccessoryOperationStatus::Available) {
            return AccessoryHistoryLayoutResult{};
        }
        result.allWrappedLinesFit = result.allWrappedLinesFit &&
            titleLayout.allLinesFit && bodyLayout.allLinesFit;
        result.renderedTitleLines.push_back(titleLayout.lines);
        result.renderedBodyLines.push_back(bodyLayout.lines);
        totalLineCount += static_cast<int>(
            titleLayout.lines.size() + bodyLayout.lines.size());
    }
    result.finalMarker = "END OF " + DrawerToken(snapshot.activeDrawer) + " HISTORY";
    result.visibleLineCapacity = visibleLineCount;
    result.totalScrollableLineCount = totalLineCount;
    result.maximumOffset = std::max(0, totalLineCount - visibleLineCount);
    result.finalMarkerBelongsToSelectedHistory = true;
    return result;
}

AccessoryDrawerRenderPlan BuildAccessoryDrawerRenderPlan(
    const AccessoryPresentationState& state,
    const AccessoryLayoutResult& layout) {
    AccessoryDrawerRenderPlan result;
    if (layout.status != brain::BrainOwnedAccessoryOperationStatus::Available ||
        layout.drawerVisibleLineCapacity <= 0 ||
        layout.drawerLineHeight <= 0 ||
        layout.drawerContentBottom <= layout.drawerContentTop ||
        state.activeSnapshot == nullptr) {
        return result;
    }
    result.status = brain::BrainOwnedAccessoryOperationStatus::Available;
    result.visibleLineCapacity = layout.drawerVisibleLineCapacity;
    std::vector<AccessoryDrawerRenderLine> allLines;
    if (state.cachedPlanAvailable && state.preparedPlan != nullptr &&
        !state.preparedPlan->layout.renderedEntryKeys.empty()) {
        const auto& prepared = state.preparedPlan->layout;
        for (std::size_t entry = 0;
             entry < prepared.renderedTitleLines.size() &&
             entry < prepared.renderedBodyLines.size();
             ++entry) {
            for (const auto& line : prepared.renderedTitleLines[entry]) {
                allLines.push_back({line, true, false});
            }
            for (const auto& line : prepared.renderedBodyLines[entry]) {
                allLines.push_back({line, false, false});
            }
        }
        if (!state.finalHistoryMarker.empty()) {
            allLines.push_back({state.finalHistoryMarker, true, true});
        }
    } else if (!state.activeSnapshot->emptyStateText.empty()) {
        allLines.push_back({state.activeSnapshot->emptyStateText, false, false});
    }
    result.totalLineCount = static_cast<int>(allLines.size());
    result.firstVisibleLine = std::clamp(
        state.drawerOffset,
        0,
        std::max(0, result.totalLineCount - result.visibleLineCapacity));
    const auto last = std::min(
        result.totalLineCount,
        result.firstVisibleLine + result.visibleLineCapacity);
    for (int index = result.firstVisibleLine; index < last; ++index) {
        result.visibleLines.push_back(allLines[static_cast<std::size_t>(index)]);
        result.finalMarkerVisible = result.finalMarkerVisible ||
            allLines[static_cast<std::size_t>(index)].finalMarker;
    }
    const int renderableBottom = layout.drawerContentTop +
        (static_cast<int>(result.visibleLines.size()) * layout.drawerLineHeight);
    result.everyLineFitsPixelGeometry =
        renderableBottom <= layout.drawerContentBottom &&
        static_cast<int>(result.visibleLines.size()) <=
            result.visibleLineCapacity;
    return result;
}

AccessoryWheelResult ApplyAccessoryWheel(const AccessoryWheelInput& input) {
    AccessoryWheelResult result;
    if (input.layout.status != brain::BrainOwnedAccessoryOperationStatus::Available) return result;
    result.status = brain::BrainOwnedAccessoryOperationStatus::Available;
    result.drawerOffset = input.drawerOffset;
    result.mainCardOffset = input.mainCardOffset;
    if (PointInsideRect(input.layout.drawerBounds, input.pointerX, input.pointerY)) {
        result.scope = AccessoryWheelScope::Drawer;
        result.handled = true;
        result.drawerOffset = std::clamp(input.drawerOffset + input.wheelClicks,
            0, std::max(0, input.drawerMaximumOffset));
        result.drawerChanged = result.drawerOffset != input.drawerOffset;
        result.accessoryRasterRequested = result.drawerChanged;
        return result;
    }
    if (PointInsideRect(input.layout.mainCardBounds, input.pointerX, input.pointerY)) {
        result.scope = AccessoryWheelScope::MainCard;
        result.handled = true;
        result.mainCardOffset = std::clamp(input.mainCardOffset + input.wheelClicks,
            0, std::max(0, input.mainCardMaximumOffset));
        result.mainCardChanged = result.mainCardOffset != input.mainCardOffset;
    }
    return result;
}

AccessoryPresentationUpdateResult UpdateAccessoryPresentation(
    AccessoryPresentationState* state,
    const AccessoryPresentationUpdateInput& input) {
    AccessoryPresentationUpdateResult result;
    if (state == nullptr || input.measurementContext == nullptr ||
        input.presentation.snapshot == nullptr ||
        input.presentation.snapshot->status != brain::BrainOwnedAccessoryOperationStatus::Available ||
        input.layout.status != brain::BrainOwnedAccessoryOperationStatus::Available) return result;

    result.status = brain::BrainOwnedAccessoryOperationStatus::Available;
    const auto& snapshot = *input.presentation.snapshot;
    const bool hadSnapshot = state->activeSnapshot != nullptr;
    result.snapshotChanged = state->activeSnapshotIdentity != snapshot.snapshotIdentity;
    result.selectionChanged = !hadSnapshot ||
        state->selectionGeneration != input.presentation.selectionGeneration ||
        state->activeSnapshot->activeDrawer != snapshot.activeDrawer;
    result.historyChanged = !hadSnapshot ||
        state->historyGeneration != input.presentation.historyGeneration;
    const bool contentChanged = !hadSnapshot ||
        state->contentGeneration != input.presentation.contentGeneration;
    result.layoutChanged = !hadSnapshot ||
        state->layoutGeneration != input.presentation.layoutGeneration;
    const bool anyPresentationChange = result.snapshotChanged || result.selectionChanged ||
        result.historyChanged || contentChanged || result.layoutChanged;
    result.mainCardUnchanged = state->mainCardProductionSignature.empty() ||
        state->mainCardProductionSignature == input.mainCardProductionSignature;
    if (!anyPresentationChange &&
        state->mainCardProductionSignature == input.mainCardProductionSignature) return result;

    if (result.selectionChanged || result.layoutChanged || !hadSnapshot) {
        result.delta.railRasterRequests = 1;
    }
    const bool drawerOpen = snapshot.activeDrawer != brain::BrainOwnedAccessoryDrawerId::None;
    if (drawerOpen && anyPresentationChange) {
        const bool planMatches = input.preparedPlan != nullptr &&
            input.preparedPlan->key.drawer == snapshot.activeDrawer &&
            input.preparedPlan->key.historyGeneration ==
                input.presentation.historyGeneration &&
            input.preparedPlan->key.contentGeneration ==
                input.presentation.contentGeneration &&
            input.preparedPlan->key.layoutGeneration ==
                input.presentation.layoutGeneration;
        if (!planMatches) {
            result.preparationPending = true;
            return result;
        }
        result.delta.drawerRasterRequests = 1;
        result.cachedPlanBuilt = true;
        state->preparedPlan = input.preparedPlan;
        const auto& historyLayout = input.preparedPlan->layout;
        state->drawerMaximumOffset = historyLayout.maximumOffset;
        state->finalHistoryMarker = historyLayout.finalMarker;
        state->cachedPlanAvailable = true;
    } else if (!drawerOpen && anyPresentationChange) {
        state->preparedPlan.reset();
        state->cachedPlanAvailable = false;
        state->drawerMaximumOffset = 0;
        state->finalHistoryMarker.clear();
    }

    if (result.selectionChanged) {
        result.drawerOffsetReset = true;
        state->drawerOffset = 0;
    } else if (state->drawerOffset > state->drawerMaximumOffset) {
        state->drawerOffset = state->drawerMaximumOffset;
    }
    result.delta.uploadRequests =
        result.delta.railRasterRequests + result.delta.drawerRasterRequests;
    if (anyPresentationChange) {
        result.publishedSnapshotCount = 1;
        result.publishedDrawer = snapshot.activeDrawer;
        result.delta.snapshotPublications = 1;
    }

    state->activeSnapshot = input.presentation.snapshot;
    state->activeSnapshotIdentity = snapshot.snapshotIdentity;
    state->selectionGeneration = input.presentation.selectionGeneration;
    state->historyGeneration = input.presentation.historyGeneration;
    state->contentGeneration = input.presentation.contentGeneration;
    state->layoutGeneration = input.presentation.layoutGeneration;
    state->mainCardProductionSignature = input.mainCardProductionSignature;
    state->railRenderSignature = RailSignature(snapshot, input.layout);
    state->drawerRenderSignature = drawerOpen
        ? DrawerSignature(snapshot, input.layout, state->drawerOffset)
        : std::string{};
    AddCounters(&state->counters, result.delta);
    return result;
}

AccessoryPresentationScrollResult ScrollAccessoryPresentation(
    AccessoryPresentationState* state,
    const AccessoryPresentationScrollInput& input) {
    AccessoryPresentationScrollResult result;
    if (state == nullptr || state->activeSnapshot == nullptr) return result;
    AccessoryWheelInput wheel;
    wheel.layout = input.layout;
    wheel.pointerX = input.pointerX;
    wheel.pointerY = input.pointerY;
    wheel.wheelClicks = input.wheelClicks;
    wheel.drawerOffset = state->drawerOffset;
    wheel.drawerMaximumOffset = state->drawerMaximumOffset;
    const auto routed = ApplyAccessoryWheel(wheel);
    result.status = routed.status;
    result.scope = routed.scope;
    result.handled = routed.handled;
    result.previousOffset = state->drawerOffset;
    if (routed.scope == AccessoryWheelScope::Drawer && routed.drawerChanged) {
        state->drawerOffset = routed.drawerOffset;
        result.changed = true;
        result.delta.drawerRasterRequests = 1;
        result.delta.uploadRequests = 1;
        state->drawerRenderSignature = DrawerSignature(
            *state->activeSnapshot, input.layout, state->drawerOffset);
        AddCounters(&state->counters, result.delta);
    }
    result.drawerOffset = state->drawerOffset;
    result.drawerMaximumOffset = state->drawerMaximumOffset;
    result.reachedFinalMarker = state->cachedPlanAvailable &&
        state->drawerOffset == state->drawerMaximumOffset;
    return result;
}

AccessoryPresentationWarmResult RunUnchangedAccessoryPresentationUpdates(
    AccessoryPresentationState* state,
    const AccessoryPresentationUpdateInput& input,
    int iterations) {
    AccessoryPresentationWarmResult result;
    if (state == nullptr || iterations < 0) return result;
    result.status = brain::BrainOwnedAccessoryOperationStatus::Available;
    for (int index = 0; index < iterations; ++index) {
        const auto update = UpdateAccessoryPresentation(state, input);
        if (update.status != brain::BrainOwnedAccessoryOperationStatus::Available) {
            result.status = update.status;
            return result;
        }
        AddCounters(&result.delta, update.delta);
        ++result.iterations;
    }
    return result;
}

bool AccessoryClickFactQueue::Produce(
    brain::BrainOwnedAccessoryDrawerId drawer,
    std::uint64_t startedMicroseconds,
    AccessoryClickFact* outFact) {
    if (drawer == brain::BrainOwnedAccessoryDrawerId::None ||
        size_ >= kCapacity) {
        if (size_ >= kCapacity) {
            ++droppedCount_;
        }
        return false;
    }
    AccessoryClickFact fact;
    fact.drawer = drawer;
    fact.requestSequence = nextSequence_++;
    fact.startedMicroseconds = startedMicroseconds;
    const auto tail = (head_ + size_) % kCapacity;
    facts_[tail] = fact;
    ++size_;
    ++producedCount_;
    if (outFact != nullptr) {
        *outFact = fact;
    }
    return true;
}

bool AccessoryClickFactQueue::Consume(AccessoryClickFact* outFact) {
    if (size_ == 0) {
        return false;
    }
    if (outFact != nullptr) {
        *outFact = facts_[head_];
    }
    head_ = (head_ + 1) % kCapacity;
    --size_;
    return true;
}

std::size_t AccessoryClickFactQueue::DiscardPending() {
    const auto discarded = size_;
    discardedCount_ += discarded;
    head_ = 0;
    size_ = 0;
    return discarded;
}

std::size_t AccessoryClickFactQueue::PendingCount() const { return size_; }
std::uint64_t AccessoryClickFactQueue::NextSequence() const { return nextSequence_; }
std::uint64_t AccessoryClickFactQueue::ProducedCount() const { return producedCount_; }
std::uint64_t AccessoryClickFactQueue::DroppedCount() const { return droppedCount_; }
std::uint64_t AccessoryClickFactQueue::DiscardedCount() const { return discardedCount_; }

void AccessoryInputDispatchCoordinator::RecordDispatchNotification() {
    ++snapshot_.dispatchNotifications;
}

bool AccessoryInputDispatchCoordinator::TryBegin(
    AccessoryClickFactQueue* queue,
    AccessoryClickFact* outFact) {
    ++snapshot_.beginAttempts;
    if (snapshot_.inFlight) {
        ++snapshot_.blockedWhileInFlight;
        return false;
    }
    AccessoryClickFact fact;
    if (queue == nullptr || !queue->Consume(&fact)) {
        return false;
    }
    inFlightFact_ = fact;
    inFlightAction_ = brain::BrainOwnedAccessoryDrawerAction::None;
    snapshot_.inFlight = true;
    snapshot_.presentationBound = false;
    snapshot_.inFlightRequestSequence = fact.requestSequence;
    snapshot_.expectedSelectionGeneration = 0;
    snapshot_.expectedRenderGeneration = 0;
    ++snapshot_.requestsBegun;
    if (outFact != nullptr) {
        *outFact = fact;
    }
    return true;
}

bool AccessoryInputDispatchCoordinator::BindPresentation(
    std::uint64_t requestSequence,
    brain::BrainOwnedAccessoryDrawerAction action,
    std::uint64_t selectionGeneration,
    std::uint64_t renderGeneration) {
    if (!snapshot_.inFlight || snapshot_.presentationBound ||
        requestSequence != inFlightFact_.requestSequence ||
        selectionGeneration == 0 || renderGeneration == 0 ||
        (action != brain::BrainOwnedAccessoryDrawerAction::Opened &&
         action != brain::BrainOwnedAccessoryDrawerAction::Switched &&
         action != brain::BrainOwnedAccessoryDrawerAction::Closed)) {
        return false;
    }
    inFlightAction_ = action;
    snapshot_.presentationBound = true;
    snapshot_.expectedSelectionGeneration = selectionGeneration;
    snapshot_.expectedRenderGeneration = renderGeneration;
    ++snapshot_.presentationsBound;
    return true;
}

AccessoryInputDispatchCompletion
AccessoryInputDispatchCoordinator::CompleteMatchingDraw(
    std::uint64_t selectionGeneration,
    std::uint64_t renderGeneration) {
    AccessoryInputDispatchCompletion result;
    if (!snapshot_.inFlight || !snapshot_.presentationBound) {
        return result;
    }
    if (selectionGeneration != snapshot_.expectedSelectionGeneration ||
        renderGeneration != snapshot_.expectedRenderGeneration) {
        ++snapshot_.mismatchedDrawAttempts;
        return result;
    }
    result.completed = true;
    result.fact = inFlightFact_;
    result.action = inFlightAction_;
    ++snapshot_.matchingDrawCompletions;
    snapshot_.inFlight = false;
    snapshot_.presentationBound = false;
    snapshot_.inFlightRequestSequence = 0;
    snapshot_.expectedSelectionGeneration = 0;
    snapshot_.expectedRenderGeneration = 0;
    inFlightFact_ = {};
    inFlightAction_ = brain::BrainOwnedAccessoryDrawerAction::None;
    return result;
}

bool AccessoryInputDispatchCoordinator::CancelInFlight(
    std::uint64_t requestSequence) {
    if (!snapshot_.inFlight ||
        requestSequence != inFlightFact_.requestSequence) {
        return false;
    }
    snapshot_.inFlight = false;
    snapshot_.presentationBound = false;
    snapshot_.inFlightRequestSequence = 0;
    snapshot_.expectedSelectionGeneration = 0;
    snapshot_.expectedRenderGeneration = 0;
    inFlightFact_ = {};
    inFlightAction_ = brain::BrainOwnedAccessoryDrawerAction::None;
    return true;
}

bool AccessoryInputDispatchCoordinator::InvalidateInFlight() {
    if (!snapshot_.inFlight) {
        return false;
    }
    ++snapshot_.invalidatedInFlight;
    snapshot_.inFlight = false;
    snapshot_.presentationBound = false;
    snapshot_.inFlightRequestSequence = 0;
    snapshot_.expectedSelectionGeneration = 0;
    snapshot_.expectedRenderGeneration = 0;
    inFlightFact_ = {};
    inFlightAction_ = brain::BrainOwnedAccessoryDrawerAction::None;
    return true;
}

AccessoryInputDispatchSnapshot
AccessoryInputDispatchCoordinator::Snapshot() const {
    return snapshot_;
}

const char* AccessoryPerformanceCategoryToken(
    AccessoryPerformanceCategory category) {
    switch (category) {
        case AccessoryPerformanceCategory::RailRasterization:
            return "rail-raster";
        case AccessoryPerformanceCategory::DrawerRasterization:
            return "drawer-raster";
        case AccessoryPerformanceCategory::RailTextureUpload:
            return "rail-upload";
        case AccessoryPerformanceCategory::DrawerTextureUpload:
            return "drawer-upload";
        case AccessoryPerformanceCategory::CompletedAccessoryDraw:
            return "completed-draw";
        case AccessoryPerformanceCategory::OpenAction:
            return "open-action";
        case AccessoryPerformanceCategory::AtomicSwitchAction:
            return "switch-action";
        case AccessoryPerformanceCategory::CloseAction:
            return "close-action";
        case AccessoryPerformanceCategory::EffectiveScrollAction:
            return "effective-scroll-action";
        case AccessoryPerformanceCategory::BrainPresentationDispatchWall:
            return "dispatch-wall";
        case AccessoryPerformanceCategory::MatchingActionDrawWall:
            return "action-draw-wall";
        case AccessoryPerformanceCategory::CombinedActionWall:
            return "combined-action-wall";
        case AccessoryPerformanceCategory::CallbackWait:
            return "callback-wait";
        case AccessoryPerformanceCategory::PreparationWait:
            return "preparation-wait";
        case AccessoryPerformanceCategory::ContainingFrameInterval:
            return "frame-interval";
        case AccessoryPerformanceCategory::Count:
        default:
            return "unknown";
    }
}

const char* AccessoryActionTimingClassificationToken(
    AccessoryActionTimingClassification classification) {
    switch (classification) {
        case AccessoryActionTimingClassification::SynchronousWallWithinBudget:
            return "SYNCHRONOUS-WALL-WITHIN-BUDGET";
        case AccessoryActionTimingClassification::FrameCadenceLimited:
            return "FRAME-CADENCE-LIMITED";
        case AccessoryActionTimingClassification::PreparationLimited:
            return "PREPARATION-LIMITED";
        case AccessoryActionTimingClassification::SynchronousWallFailure:
            return "SYNCHRONOUS-WALL-FAILURE";
        case AccessoryActionTimingClassification::RenderWallFailure:
            return "RENDER-WALL-FAILURE";
        case AccessoryActionTimingClassification::TimingUnavailable:
        default:
            return "TIMING-UNAVAILABLE";
    }
}

const char* AccessoryDispatchStageToken(AccessoryDispatchStage stage) {
    switch (stage) {
        case AccessoryDispatchStage::BrainDecision:
            return "brain-decision-wall";
        case AccessoryDispatchStage::BrainProjectionHistoryCopy:
            return "brain-projection-history-copy-wall";
        case AccessoryDispatchStage::OverlayLayoutPresentation:
            return "overlay-layout-presentation-wall";
        case AccessoryDispatchStage::GdiMeasurementWrapping:
            return "gdi-measurement-wrapping-wall";
        case AccessoryDispatchStage::GenerationBinding:
            return "generation-binding-wall";
        case AccessoryDispatchStage::Count:
        default:
            return "unknown-stage";
    }
}

const char* AccessoryRasterReasonToken(AccessoryRasterReason reason) {
    switch (reason) {
        case AccessoryRasterReason::Initial:
            return "initial";
        case AccessoryRasterReason::Selection:
            return "selection-open-close-switch";
        case AccessoryRasterReason::EffectiveScaleOrResize:
            return "scale-or-resize";
        case AccessoryRasterReason::TypographyOrLayout:
            return "typography-or-layout";
        case AccessoryRasterReason::ContentGeneration:
            return "content-generation";
        case AccessoryRasterReason::PresentationScroll:
            return "presentation-scroll";
        case AccessoryRasterReason::Count:
        default:
            return "unknown";
    }
}

namespace {

bool HasHardCeiling(AccessoryPerformanceCategory category) {
    switch (category) {
        case AccessoryPerformanceCategory::RailRasterization:
        case AccessoryPerformanceCategory::DrawerRasterization:
        case AccessoryPerformanceCategory::RailTextureUpload:
        case AccessoryPerformanceCategory::DrawerTextureUpload:
        case AccessoryPerformanceCategory::CompletedAccessoryDraw:
        case AccessoryPerformanceCategory::BrainPresentationDispatchWall:
        case AccessoryPerformanceCategory::MatchingActionDrawWall:
        case AccessoryPerformanceCategory::CombinedActionWall:
            return true;
        case AccessoryPerformanceCategory::OpenAction:
        case AccessoryPerformanceCategory::AtomicSwitchAction:
        case AccessoryPerformanceCategory::CloseAction:
        case AccessoryPerformanceCategory::EffectiveScrollAction:
        case AccessoryPerformanceCategory::CallbackWait:
        case AccessoryPerformanceCategory::PreparationWait:
        case AccessoryPerformanceCategory::ContainingFrameInterval:
        case AccessoryPerformanceCategory::Count:
        default:
            return false;
    }
}

}  // namespace

AccessoryPerformanceCollector::AccessoryPerformanceCollector() {
    ResetForNewProcess(1);
}

void AccessoryPerformanceCollector::ResetForNewProcess(std::uint64_t epoch) {
    for (auto& histogram : histograms_) {
        histogram.buckets.fill(0);
        histogram.count = 0;
        histogram.maximumMicroseconds = 0;
    }
    for (auto& actionStages : actionStageWallHistograms_) {
        for (auto& histogram : actionStages) {
            histogram.buckets.fill(0);
            histogram.count = 0;
            histogram.maximumMicroseconds = 0;
        }
    }
    pendingActions_ = {};
    pendingActionCount_ = 0;
    nextSyntheticActionSequence_ = 1;
    epoch_ = epoch == 0 ? 1 : epoch;
    measurementRevision_ = 0;
    publishedRevision_ = 0;
    hasPublished_ = false;
    thresholdFailure_ = false;
    warningPending_ = false;
    warningConsumed_ = false;
    firstViolationMicroseconds_ = 0;
    firstViolationCategory_ = AccessoryPerformanceCategory::RailRasterization;
    warningCount_ = 0;
    publicationCount_ = 0;
    actionsCompleted_ = 0;
    synchronousWallWithinBudgetCount_ = 0;
    frameCadenceLimitedCount_ = 0;
    preparationLimitedCount_ = 0;
    synchronousWallFailureCount_ = 0;
    renderWallFailureCount_ = 0;
    timingUnavailableCount_ = 0;
    cadenceContractFailureCount_ = 0;
    missedEligibleDraws_ = 0;
    uniqueDrawSamples_ = 0;
    drawSampleReferences_ = 0;
    coalescedActionCount_ = 0;
    maximumActionsPerDraw_ = 0;
    nextDrawSampleId_ = 1;
    lastAction_ = {};
    firstViolationRecords_ = {};
    firstViolationRecordCount_ = 0;
    droppedViolationRecordCount_ = 0;
    railRasterReasons_.fill(0);
    drawerRasterReasons_.fill(0);
}

void AccessoryPerformanceCollector::Record(
    AccessoryPerformanceCategory category,
    std::uint64_t elapsedMicroseconds) {
    const auto categoryIndex = static_cast<std::size_t>(category);
    if (categoryIndex >= histograms_.size()) {
        return;
    }
    auto& histogram = histograms_[categoryIndex];
    const auto bucketIndex = static_cast<std::size_t>(std::min<std::uint64_t>(
        elapsedMicroseconds, kHistogramBucketCount - 1));
    ++histogram.buckets[bucketIndex];
    ++histogram.count;
    histogram.maximumMicroseconds = std::max(
        histogram.maximumMicroseconds, elapsedMicroseconds);
    ++measurementRevision_;
    if (HasHardCeiling(category) &&
        elapsedMicroseconds > kThresholdMicroseconds && !thresholdFailure_) {
        thresholdFailure_ = true;
        warningPending_ = true;
        firstViolationMicroseconds_ = elapsedMicroseconds;
        firstViolationCategory_ = category;
    }
}

void AccessoryPerformanceCollector::RecordRasterReason(
    bool drawer,
    AccessoryRasterReason reason) {
    const auto index = static_cast<std::size_t>(reason);
    if (index >= static_cast<std::size_t>(AccessoryRasterReason::Count)) {
        return;
    }
    if (drawer) {
        ++drawerRasterReasons_[index];
    } else {
        ++railRasterReasons_[index];
    }
    ++measurementRevision_;
}

bool AccessoryPerformanceCollector::BeginAction(
    AccessoryPerformanceCategory category,
    std::uint64_t requestSequence,
    std::uint64_t startedMicroseconds,
    std::uint64_t expectedSelectionGeneration,
    std::uint64_t expectedRenderGeneration,
    std::uint64_t dispatchCompletedMicroseconds,
    std::uint64_t expectedDrawOrdinal,
    const AccessoryActionDispatchTimingInput& timing) {
    if (pendingActionCount_ >= pendingActions_.size() ||
        startedMicroseconds == 0) {
        return false;
    }
    const auto resolvedDispatchCompleted = timing.dispatchCompletedMicroseconds != 0
        ? timing.dispatchCompletedMicroseconds
        : dispatchCompletedMicroseconds;
    pendingActions_[pendingActionCount_++] = {
        category,
        requestSequence,
        startedMicroseconds,
        expectedSelectionGeneration,
        expectedRenderGeneration,
        resolvedDispatchCompleted == 0
            ? startedMicroseconds
            : resolvedDispatchCompleted,
        expectedDrawOrdinal,
        timing.dispatchStartedMicroseconds == 0
            ? startedMicroseconds
            : timing.dispatchStartedMicroseconds,
        timing.preparationWaitMicroseconds,
        timing.stages};
    return true;
}

bool AccessoryPerformanceCollector::BeginDrawerAction(
    brain::BrainOwnedAccessoryDrawerAction action,
    std::uint64_t requestSequence,
    std::uint64_t startedMicroseconds,
    std::uint64_t expectedSelectionGeneration,
    std::uint64_t expectedRenderGeneration,
    std::uint64_t dispatchCompletedMicroseconds,
    std::uint64_t expectedDrawOrdinal,
    const AccessoryActionDispatchTimingInput& timing) {
    AccessoryPerformanceCategory category;
    switch (action) {
        case brain::BrainOwnedAccessoryDrawerAction::Opened:
            category = AccessoryPerformanceCategory::OpenAction;
            break;
        case brain::BrainOwnedAccessoryDrawerAction::Switched:
            category = AccessoryPerformanceCategory::AtomicSwitchAction;
            break;
        case brain::BrainOwnedAccessoryDrawerAction::Closed:
            category = AccessoryPerformanceCategory::CloseAction;
            break;
        case brain::BrainOwnedAccessoryDrawerAction::None:
        case brain::BrainOwnedAccessoryDrawerAction::DuplicateRequestIgnored:
        default:
            return false;
    }
    return BeginAction(
        category,
        requestSequence,
        startedMicroseconds,
        expectedSelectionGeneration,
        expectedRenderGeneration,
        dispatchCompletedMicroseconds,
        expectedDrawOrdinal,
        timing);
}

bool AccessoryPerformanceCollector::BeginEffectiveScroll(
    std::uint64_t startedMicroseconds,
    std::uint64_t expectedSelectionGeneration,
    std::uint64_t expectedRenderGeneration,
    std::uint64_t dispatchCompletedMicroseconds,
    std::uint64_t expectedDrawOrdinal,
    const AccessoryActionDispatchTimingInput& timing) {
    return BeginAction(
        AccessoryPerformanceCategory::EffectiveScrollAction,
        nextSyntheticActionSequence_++,
        startedMicroseconds,
        expectedSelectionGeneration,
        expectedRenderGeneration,
        dispatchCompletedMicroseconds,
        expectedDrawOrdinal,
        timing);
}

std::size_t AccessoryPerformanceCollector::CompletePendingActions(
    std::uint64_t completedMicroseconds,
    std::uint64_t completedSelectionGeneration,
    std::uint64_t completedRenderGeneration,
    std::uint64_t matchingDrawEnteredMicroseconds,
    std::uint64_t precedingDrawEnteredMicroseconds,
    std::uint64_t matchingDrawOrdinal,
    const AccessoryActionDrawTimingInput& drawTiming) {
    std::array<bool, kPendingActionCapacity> matches{};
    std::size_t matchedCount = 0;
    for (std::size_t index = 0; index < pendingActionCount_; ++index) {
        const auto& action = pendingActions_[index];
        const bool generationMatches =
            (action.expectedSelectionGeneration == 0 &&
             action.expectedRenderGeneration == 0) ||
            (action.expectedSelectionGeneration == completedSelectionGeneration &&
             action.expectedRenderGeneration == completedRenderGeneration);
        const bool ordinalMatches = action.expectedDrawOrdinal == 0 ||
            matchingDrawOrdinal >= action.expectedDrawOrdinal;
        matches[index] = completedMicroseconds >= action.startedMicroseconds &&
            generationMatches && ordinalMatches;
        matchedCount += matches[index] ? 1 : 0;
    }
    if (matchedCount == 0) return 0;

    const auto drawSampleId = nextDrawSampleId_++;
    ++uniqueDrawSamples_;
    drawSampleReferences_ += matchedCount;
    maximumActionsPerDraw_ = std::max<std::uint64_t>(
        maximumActionsPerDraw_, matchedCount);
    if (matchedCount > 1) coalescedActionCount_ += matchedCount;
    Record(AccessoryPerformanceCategory::MatchingActionDrawWall,
        drawTiming.actionDrawWallMicroseconds);

    std::size_t retained = 0;
    std::size_t completed = 0;
    for (std::size_t index = 0; index < pendingActionCount_; ++index) {
        const auto action = pendingActions_[index];
        if (!matches[index]) {
            pendingActions_[retained++] = action;
            continue;
        }
        const auto drawEntered = matchingDrawEnteredMicroseconds == 0
            ? completedMicroseconds : matchingDrawEnteredMicroseconds;
        const auto dispatchCompleted = action.dispatchCompletedMicroseconds == 0
            ? action.startedMicroseconds : action.dispatchCompletedMicroseconds;
        AccessoryActionTimingRecord timing;
        timing.available = true;
        timing.actionCategory = action.category;
        timing.requestSequence = action.requestSequence;
        timing.mouseFactAcceptedMicroseconds = action.startedMicroseconds;
        timing.dispatchStartedMicroseconds = action.dispatchStartedMicroseconds;
        timing.dispatchCompletedMicroseconds = dispatchCompleted;
        timing.precedingDrawEnteredMicroseconds = precedingDrawEnteredMicroseconds;
        timing.matchingDrawEnteredMicroseconds = drawEntered;
        timing.matchingDrawCompletedMicroseconds = completedMicroseconds;
        timing.expectedDrawOrdinal = action.expectedDrawOrdinal;
        timing.matchingDrawOrdinal = matchingDrawOrdinal;
        timing.preparationWaitMicroseconds = action.preparationWaitMicroseconds;
        timing.drawSampleId = drawSampleId;
        timing.sharedDrawSample = matchedCount > 1;
        timing.drawSampleFanOut = matchedCount;
        timing.queuedBeforeDispatchMicroseconds =
            action.dispatchStartedMicroseconds >= action.startedMicroseconds
            ? action.dispatchStartedMicroseconds - action.startedMicroseconds : 0;
        const auto rawDispatchWall = dispatchCompleted >= action.dispatchStartedMicroseconds
            ? dispatchCompleted - action.dispatchStartedMicroseconds : 0;
        timing.dispatchWallMicroseconds = rawDispatchWall >= timing.preparationWaitMicroseconds
            ? rawDispatchWall - timing.preparationWaitMicroseconds : 0;
        timing.callbackWaitMicroseconds = drawEntered >= dispatchCompleted
            ? drawEntered - dispatchCompleted : 0;
        timing.matchingDrawCallbackElapsedMicroseconds =
            completedMicroseconds >= drawEntered ? completedMicroseconds - drawEntered : 0;
        timing.actionDrawWallMicroseconds = drawTiming.actionDrawWallMicroseconds;
        timing.combinedActionWallMicroseconds =
            timing.dispatchWallMicroseconds + timing.actionDrawWallMicroseconds;
        timing.renderWallFailure = drawTiming.renderWallFailure;
        timing.renderWallFailureCategory = drawTiming.renderWallFailureCategory;
        timing.renderWallFailureMicroseconds = drawTiming.renderWallFailureMicroseconds;
        timing.stages = action.stages;
        timing.endToEndMicroseconds = completedMicroseconds - action.startedMicroseconds;
        timing.containingFrameIntervalMicroseconds =
            precedingDrawEnteredMicroseconds != 0 && drawEntered >= precedingDrawEnteredMicroseconds
            ? drawEntered - precedingDrawEnteredMicroseconds : 0;
        timing.missedEligibleDraws = action.expectedDrawOrdinal != 0 &&
            matchingDrawOrdinal > action.expectedDrawOrdinal
            ? matchingDrawOrdinal - action.expectedDrawOrdinal : 0;
        timing.accountingExact =
            action.dispatchStartedMicroseconds >= action.startedMicroseconds &&
            dispatchCompleted >= action.dispatchStartedMicroseconds &&
            drawEntered >= dispatchCompleted && completedMicroseconds >= drawEntered &&
            timing.endToEndMicroseconds == timing.queuedBeforeDispatchMicroseconds +
                timing.dispatchWallMicroseconds + timing.preparationWaitMicroseconds +
                timing.callbackWaitMicroseconds +
                timing.matchingDrawCallbackElapsedMicroseconds;

        bool stageFailure = false;
        for (const auto elapsed : timing.stages.elapsedMicroseconds) {
            stageFailure = stageFailure || elapsed > kThresholdMicroseconds;
        }
        const bool synchronousPass = !stageFailure &&
            timing.dispatchWallMicroseconds <= kThresholdMicroseconds &&
            timing.actionDrawWallMicroseconds <= kThresholdMicroseconds &&
            timing.combinedActionWallMicroseconds <= kThresholdMicroseconds;
        const bool firstEligibleDraw = timing.missedEligibleDraws == 0;
        const bool waitAccountedByFrame = timing.containingFrameIntervalMicroseconds != 0 &&
            timing.callbackWaitMicroseconds <= timing.containingFrameIntervalMicroseconds + 1000;
        bool violation = false;
        AccessoryPerformanceCategory violationCategory = action.category;
        std::uint64_t violationUs = timing.endToEndMicroseconds;
        if (drawTiming.renderWallFailure) {
            timing.classification = AccessoryActionTimingClassification::RenderWallFailure;
            ++renderWallFailureCount_; violation = true;
            violationCategory = drawTiming.renderWallFailureCategory;
            violationUs = drawTiming.renderWallFailureMicroseconds;
        } else if (!synchronousPass) {
            timing.classification = AccessoryActionTimingClassification::SynchronousWallFailure;
            ++synchronousWallFailureCount_; violation = true;
            violationCategory = timing.combinedActionWallMicroseconds > kThresholdMicroseconds
                ? AccessoryPerformanceCategory::CombinedActionWall
                : AccessoryPerformanceCategory::BrainPresentationDispatchWall;
            violationUs = std::max(timing.dispatchWallMicroseconds,
                timing.combinedActionWallMicroseconds);
        } else if (timing.preparationWaitMicroseconds > 0) {
            timing.classification = AccessoryActionTimingClassification::PreparationLimited;
            ++preparationLimitedCount_;
        } else if (timing.endToEndMicroseconds <= kThresholdMicroseconds &&
                   firstEligibleDraw && timing.accountingExact) {
            timing.classification = AccessoryActionTimingClassification::SynchronousWallWithinBudget;
            ++synchronousWallWithinBudgetCount_;
        } else if (firstEligibleDraw && timing.accountingExact && waitAccountedByFrame) {
            timing.classification = AccessoryActionTimingClassification::FrameCadenceLimited;
            ++frameCadenceLimitedCount_;
        } else {
            timing.classification = AccessoryActionTimingClassification::TimingUnavailable;
            ++timingUnavailableCount_; ++cadenceContractFailureCount_; violation = true;
        }
        if (violation) {
            if (!thresholdFailure_) {
                thresholdFailure_ = true; warningPending_ = true;
                firstViolationCategory_ = violationCategory;
                firstViolationMicroseconds_ = violationUs;
            }
            if (firstViolationRecordCount_ < firstViolationRecords_.size())
                firstViolationRecords_[firstViolationRecordCount_++] = timing;
            else ++droppedViolationRecordCount_;
        }
        missedEligibleDraws_ += timing.missedEligibleDraws;
        ++actionsCompleted_;
        lastAction_ = timing;
        Record(action.category, timing.endToEndMicroseconds);
        Record(AccessoryPerformanceCategory::BrainPresentationDispatchWall,
            timing.dispatchWallMicroseconds);
        Record(AccessoryPerformanceCategory::CombinedActionWall,
            timing.combinedActionWallMicroseconds);
        Record(AccessoryPerformanceCategory::PreparationWait,
            timing.preparationWaitMicroseconds);
        Record(AccessoryPerformanceCategory::CallbackWait, timing.callbackWaitMicroseconds);
        Record(AccessoryPerformanceCategory::ContainingFrameInterval,
            timing.containingFrameIntervalMicroseconds);
        std::size_t actionIndex = action.category == AccessoryPerformanceCategory::OpenAction ? 0 :
            action.category == AccessoryPerformanceCategory::AtomicSwitchAction ? 1 :
            action.category == AccessoryPerformanceCategory::CloseAction ? 2 : 3;
        if (actionIndex < actionStageWallHistograms_.size()) {
            for (std::size_t stage = 0; stage < timing.stages.elapsedMicroseconds.size(); ++stage) {
                auto& histogram = actionStageWallHistograms_[actionIndex][stage];
                const auto elapsed = timing.stages.elapsedMicroseconds[stage];
                const auto bucket = static_cast<std::size_t>(
                    std::min<std::uint64_t>(elapsed, kHistogramBucketCount - 1));
                ++histogram.buckets[bucket]; ++histogram.count;
                histogram.maximumMicroseconds = std::max(histogram.maximumMicroseconds, elapsed);
                ++measurementRevision_;
            }
        }
        ++completed;
    }
    for (std::size_t index = retained; index < pendingActions_.size(); ++index)
        pendingActions_[index] = {};
    pendingActionCount_ = retained;
    return completed;
}

void AccessoryPerformanceCollector::DiscardPendingActions() {
    pendingActions_ = {};
    pendingActionCount_ = 0;
}

bool AccessoryPerformanceCollector::HasPendingActions() const {
    return pendingActionCount_ != 0;
}

bool AccessoryPerformanceCollector::HasMatchingPendingAction(
    std::uint64_t selectionGeneration,
    std::uint64_t renderGeneration,
    std::uint64_t drawOrdinal) const {
    for (std::size_t index = 0; index < pendingActionCount_; ++index) {
        const auto& action = pendingActions_[index];
        const bool generationMatches =
            (action.expectedSelectionGeneration == 0 &&
             action.expectedRenderGeneration == 0) ||
            (action.expectedSelectionGeneration == selectionGeneration &&
             action.expectedRenderGeneration == renderGeneration);
        const bool ordinalMatches = action.expectedDrawOrdinal == 0 ||
            drawOrdinal >= action.expectedDrawOrdinal;
        if (generationMatches && ordinalMatches) {
            return true;
        }
    }
    return false;
}

bool AccessoryPerformanceCollector::ConsumeFirstViolationWarning(
    AccessoryPerformanceCategory* outCategory,
    std::uint64_t* outElapsedMicroseconds) {
    if (!warningPending_ || warningConsumed_) {
        return false;
    }
    warningPending_ = false;
    warningConsumed_ = true;
    ++warningCount_;
    if (outCategory != nullptr) {
        *outCategory = firstViolationCategory_;
    }
    if (outElapsedMicroseconds != nullptr) {
        *outElapsedMicroseconds = firstViolationMicroseconds_;
    }
    return true;
}

AccessoryPerformanceSummary AccessoryPerformanceCollector::Summarize(
    const Histogram& histogram) {
    AccessoryPerformanceSummary summary;
    summary.count = histogram.count;
    summary.maximumMicroseconds = histogram.maximumMicroseconds;
    if (histogram.count == 0) {
        return summary;
    }
    const auto percentile = [&](std::uint64_t numerator) {
        const auto target =
            ((histogram.count * numerator) + 99) / 100;
        std::uint64_t accumulated = 0;
        for (std::size_t index = 0; index < histogram.buckets.size(); ++index) {
            accumulated += histogram.buckets[index];
            if (accumulated >= target) {
                return index == histogram.buckets.size() - 1
                    ? histogram.maximumMicroseconds
                    : static_cast<std::uint64_t>(index);
            }
        }
        return histogram.maximumMicroseconds;
    };
    summary.p50Microseconds = percentile(50);
    summary.p95Microseconds = percentile(95);
    return summary;
}

AccessoryPerformanceSnapshot AccessoryPerformanceCollector::Snapshot() const {
    AccessoryPerformanceSnapshot snapshot;
    snapshot.epoch = epoch_;
    snapshot.measurementRevision = measurementRevision_;
    snapshot.thresholdFailure = thresholdFailure_;
    snapshot.firstViolationMicroseconds = firstViolationMicroseconds_;
    snapshot.firstViolationCategory = firstViolationCategory_;
    snapshot.warningCount = warningCount_;
    snapshot.publicationCount = publicationCount_;
    snapshot.actionsCompleted = actionsCompleted_;
    snapshot.synchronousWallWithinBudgetCount =
        synchronousWallWithinBudgetCount_;
    snapshot.frameCadenceLimitedCount = frameCadenceLimitedCount_;
    snapshot.preparationLimitedCount = preparationLimitedCount_;
    snapshot.synchronousWallFailureCount = synchronousWallFailureCount_;
    snapshot.renderWallFailureCount = renderWallFailureCount_;
    snapshot.timingUnavailableCount = timingUnavailableCount_;
    snapshot.cadenceContractFailureCount = cadenceContractFailureCount_;
    snapshot.missedEligibleDraws = missedEligibleDraws_;
    snapshot.uniqueDrawSamples = uniqueDrawSamples_;
    snapshot.drawSampleReferences = drawSampleReferences_;
    snapshot.coalescedActionCount = coalescedActionCount_;
    snapshot.maximumActionsPerDraw = maximumActionsPerDraw_;
    snapshot.lastAction = lastAction_;
    snapshot.firstViolationRecords = firstViolationRecords_;
    snapshot.firstViolationRecordCount = firstViolationRecordCount_;
    snapshot.droppedViolationRecordCount = droppedViolationRecordCount_;
    snapshot.railRasterReasons = railRasterReasons_;
    snapshot.drawerRasterReasons = drawerRasterReasons_;
    for (std::size_t index = 0; index < histograms_.size(); ++index) {
        snapshot.categories[index] = Summarize(histograms_[index]);
    }
    for (std::size_t actionIndex = 0;
         actionIndex < actionStageWallHistograms_.size(); ++actionIndex) {
        for (std::size_t stageIndex = 0;
             stageIndex < actionStageWallHistograms_[actionIndex].size();
             ++stageIndex) {
            snapshot.actionStageWall[actionIndex][stageIndex] =
                Summarize(actionStageWallHistograms_[actionIndex][stageIndex]);
        }
    }
    return snapshot;
}

bool AccessoryPerformanceCollector::BeginAggregatePublication(
    AccessoryPerformanceSnapshot* outSnapshot) {
    if (hasPublished_ && publishedRevision_ == measurementRevision_) {
        return false;
    }
    hasPublished_ = true;
    publishedRevision_ = measurementRevision_;
    ++publicationCount_;
    if (outSnapshot != nullptr) {
        *outSnapshot = Snapshot();
    }
    return true;
}

}  // namespace xvatsim::modules::overlay
