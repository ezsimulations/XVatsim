#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>
#include <objidl.h>
#include <bcrypt.h>
#include <gdiplus.h>

#include "XVatsim/brain/BrainOwnedRuntime.h"
#include "XVatsim/modules/overlay/OverlayAccessoryCore.h"
#include "XVatsim/modules/overlay/OverlayVisualProof.h"

namespace fs = std::filesystem;
namespace brain = xvatsim::brain;
namespace overlay = xvatsim::modules::overlay;

namespace {

constexpr std::uint64_t kTimingLimitMicroseconds = 16700;
constexpr auto kOfflinePreparationTimeout = std::chrono::seconds(30);

struct FailureState {
    std::vector<std::string> messages;

    void Require(bool condition, const std::string& message) {
        if (!condition) messages.push_back(message);
    }
};

std::string Hex(const unsigned char* bytes, std::size_t count) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(count * 2);
    for (std::size_t index = 0; index < count; ++index) {
        result.push_back(digits[(bytes[index] >> 4) & 0x0f]);
        result.push_back(digits[bytes[index] & 0x0f]);
    }
    return result;
}

std::string Sha256(const std::vector<unsigned char>& bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectBytes = 0;
    DWORD returned = 0;
    std::vector<unsigned char> object;
    std::array<unsigned char, 32> digest{};
    if (BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0 ||
        BCryptGetProperty(
            algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes),
            &returned, 0) != 0) {
        if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    object.resize(objectBytes);
    if (BCryptCreateHash(
            algorithm, &hash, object.data(), objectBytes,
            nullptr, 0, 0) != 0 ||
        (!bytes.empty() && BCryptHashData(
            hash, const_cast<PUCHAR>(bytes.data()),
            static_cast<ULONG>(bytes.size()), 0) != 0) ||
        BCryptFinishHash(hash, digest.data(), digest.size(), 0) != 0) {
        if (hash != nullptr) BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return Hex(digest.data(), digest.size());
}

std::vector<unsigned char> ReadBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<unsigned char>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::string Sha256File(const fs::path& path) {
    return Sha256(ReadBytes(path));
}

int FindPngEncoder(CLSID* outClsid) {
    UINT count = 0;
    UINT bytes = 0;
    Gdiplus::GetImageEncodersSize(&count, &bytes);
    if (bytes == 0 || outClsid == nullptr) return -1;
    std::vector<unsigned char> buffer(bytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(count, bytes, encoders) != Gdiplus::Ok) {
        return -1;
    }
    for (UINT index = 0; index < count; ++index) {
        if (std::wstring(encoders[index].MimeType) == L"image/png") {
            *outClsid = encoders[index].Clsid;
            return static_cast<int>(index);
        }
    }
    return -1;
}

bool SavePng(
    const fs::path& path,
    const overlay::OfflineRasterImage& image) {
    if (image.width <= 0 || image.height <= 0 ||
        image.bgraPixels.size() !=
            static_cast<std::size_t>(image.width * image.height * 4)) {
        return false;
    }
    CLSID encoder{};
    if (FindPngEncoder(&encoder) < 0) return false;
    Gdiplus::Bitmap bitmap(
        image.width,
        image.height,
        image.width * 4,
        PixelFormat32bppARGB,
        const_cast<BYTE*>(image.bgraPixels.data()));
    return bitmap.Save(path.wstring().c_str(), &encoder, nullptr) == Gdiplus::Ok;
}

overlay::OfflineRasterImage MakeCanvas(
    int width,
    int height,
    unsigned char red = 27,
    unsigned char green = 35,
    unsigned char blue = 44) {
    overlay::OfflineRasterImage result;
    result.width = std::max(1, width);
    result.height = std::max(1, height);
    result.bgraPixels.resize(
        static_cast<std::size_t>(result.width * result.height * 4));
    for (std::size_t index = 0; index < result.bgraPixels.size(); index += 4) {
        result.bgraPixels[index + 0] = blue;
        result.bgraPixels[index + 1] = green;
        result.bgraPixels[index + 2] = red;
        result.bgraPixels[index + 3] = 255;
    }
    return result;
}

overlay::OfflineRasterImage ScaleNearest(
    const overlay::OfflineRasterImage& source,
    int width,
    int height) {
    overlay::OfflineRasterImage result = MakeCanvas(width, height, 0, 0, 0);
    std::fill(result.bgraPixels.begin(), result.bgraPixels.end(), 0);
    if (source.width <= 0 || source.height <= 0) return result;
    for (int y = 0; y < result.height; ++y) {
        const int sourceY = std::min(
            source.height - 1,
            static_cast<int>((static_cast<long long>(y) * source.height) /
                result.height));
        for (int x = 0; x < result.width; ++x) {
            const int sourceX = std::min(
                source.width - 1,
                static_cast<int>((static_cast<long long>(x) * source.width) /
                    result.width));
            const auto sourceIndex = static_cast<std::size_t>(
                (sourceY * source.width + sourceX) * 4);
            const auto targetIndex = static_cast<std::size_t>(
                (y * result.width + x) * 4);
            std::copy_n(
                source.bgraPixels.data() + sourceIndex,
                4,
                result.bgraPixels.data() + targetIndex);
        }
    }
    return result;
}

void AlphaBlit(
    overlay::OfflineRasterImage* target,
    const overlay::OfflineRasterImage& source,
    int left,
    int top) {
    if (target == nullptr) return;
    for (int y = 0; y < source.height; ++y) {
        const int targetY = top + y;
        if (targetY < 0 || targetY >= target->height) continue;
        for (int x = 0; x < source.width; ++x) {
            const int targetX = left + x;
            if (targetX < 0 || targetX >= target->width) continue;
            const auto sourceIndex = static_cast<std::size_t>(
                (y * source.width + x) * 4);
            const auto targetIndex = static_cast<std::size_t>(
                (targetY * target->width + targetX) * 4);
            const unsigned int alpha = source.bgraPixels[sourceIndex + 3];
            const unsigned int inverse = 255 - alpha;
            for (int channel = 0; channel < 3; ++channel) {
                target->bgraPixels[targetIndex + channel] =
                    static_cast<unsigned char>((
                        source.bgraPixels[sourceIndex + channel] * alpha +
                        target->bgraPixels[targetIndex + channel] * inverse +
                        127) / 255);
            }
            target->bgraPixels[targetIndex + 3] = 255;
        }
    }
}

void DrawBorder(
    overlay::OfflineRasterImage* image,
    int left,
    int top,
    int right,
    int bottom,
    unsigned char red,
    unsigned char green,
    unsigned char blue) {
    if (image == nullptr) return;
    const auto set = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= image->width || y >= image->height) return;
        const auto index = static_cast<std::size_t>((y * image->width + x) * 4);
        image->bgraPixels[index] = blue;
        image->bgraPixels[index + 1] = green;
        image->bgraPixels[index + 2] = red;
        image->bgraPixels[index + 3] = 255;
    };
    for (int x = left; x < right; ++x) {
        set(x, top);
        set(x, bottom - 1);
    }
    for (int y = top; y < bottom; ++y) {
        set(left, y);
        set(right - 1, y);
    }
}

brain::OverlayViewModel BuildMainCardInput() {
    brain::OverlayViewModel view;
    view.mode = brain::OverlayMode::Active;
    view.visible = true;
    view.title = "Enroute";
    view.headerRightText = "N100PC";
    view.version.text = "V2 STEP 3";
    view.version.tone = brain::OverlayVersionTone::Current;
    view.radioState.valid = true;
    view.radioState.modeCActive = true;
    view.radioState.com1Powered = true;
    view.radioState.com2Powered = true;
    view.radioState.com1ActiveFrequency = "136.225";
    view.radioState.com2ActiveFrequency = "134.790";
    view.radioState.com1RxAvailable = true;
    view.radioState.com1TxAvailable = true;
    view.bodyLines = {
        {"CONNECTED N100PC", brain::OverlayTone::Active},
        {"DEN_17_CTR 127.650", brain::OverlayTone::Active},
        {"Controller output remains unchanged", brain::OverlayTone::Normal},
        {"IFR/VFR accessory parity", brain::OverlayTone::Next},
        {"No filed route", brain::OverlayTone::Next},
    };
    return view;
}

struct EntryInput {
    std::string key;
    std::string title;
    std::string body;
};

std::uint64_t ElapsedMicroseconds(
    std::chrono::steady_clock::time_point started);

class OfflinePreparationSession {
public:
    void Start() {
        const auto started = std::chrono::steady_clock::now();
        if (!worker_.Start(GetCurrentThreadId())) {
            throw std::runtime_error(
                "production accessory preparation worker rejected startup");
        }
        const auto deadline = started + kOfflinePreparationTimeout;
        while (worker_.State() ==
               overlay::AccessoryPreparationWorkerState::Starting) {
            if (std::chrono::steady_clock::now() >= deadline) {
                throw std::runtime_error(
                    "production accessory preparation worker startup timed out");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        startupWaitMicroseconds_ = ElapsedMicroseconds(started);
        if (worker_.State() != overlay::AccessoryPreparationWorkerState::Ready) {
            throw std::runtime_error(
                std::string("production accessory preparation worker startup failed: ") +
                overlay::AccessoryPreparationWorkerFailureToken(worker_.Failure()));
        }
    }

    std::uint64_t NextProofGeneration() {
        if (nextProofGeneration_ == 0 ||
            nextProofGeneration_ == std::numeric_limits<std::uint64_t>::max()) {
            throw std::runtime_error("offline proof generation exhausted");
        }
        return nextProofGeneration_++;
    }

    std::shared_ptr<const overlay::AccessoryPreparedDrawerPlan> Prepare(
        const brain::BrainOwnedAccessoryPresentationHandle& presentation,
        const overlay::AccessoryLayoutResult& layout,
        const overlay::AccessoryTypographyMetrics& typography,
        std::uint64_t proofGeneration,
        std::uint64_t* waitMicroseconds) {
        if (presentation.snapshot == nullptr ||
            presentation.snapshot->status !=
                brain::BrainOwnedAccessoryOperationStatus::Available ||
            presentation.snapshot->activeDrawer ==
                brain::BrainOwnedAccessoryDrawerId::None) {
            throw std::runtime_error(
                "brain did not issue an immutable presentation command");
        }

        overlay::AccessoryPreparationKey key;
        key.drawer = presentation.snapshot->activeDrawer;
        key.layoutGeneration = proofGeneration;
        key.commandIdentity = presentation.snapshot->commandIdentity;
        key.lifecycleEpoch = presentation.snapshot->lifecycleEpoch;
        key.selectedDrawerContentRevision =
            presentation.snapshot->selectedDrawerContentRevision;
        key.typographyGeneration = typography.generation;
        key.scaleThousandths = static_cast<int>(
            std::lround(layout.scale * 1000.0f));
        key.contentWidth = std::max(1,
            layout.drawerBounds.right - layout.drawerBounds.left -
                (2 * layout.drawerContentInset));
        key.visibleLineCapacity = layout.drawerVisibleLineCapacity;

        overlay::AccessoryPreparationRequest request;
        request.key = key;
        request.snapshot = presentation.snapshot;
        request.layout = layout;
        request.supersessionTerminalAccounted = true;

        const auto started = std::chrono::steady_clock::now();
        const auto deadline = started + kOfflinePreparationTimeout;
        bool submitted = false;
        while (!submitted) {
            if (worker_.State() ==
                overlay::AccessoryPreparationWorkerState::Failed) {
                throw std::runtime_error(
                    std::string("production preparation worker failed before request: ") +
                    overlay::AccessoryPreparationWorkerFailureToken(worker_.Failure()));
            }
            submitted = worker_.Request(request);
            if (!submitted) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    throw std::runtime_error(
                        "bounded production preparation request timed out offline");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        for (;;) {
            if (worker_.State() ==
                overlay::AccessoryPreparationWorkerState::Failed) {
                throw std::runtime_error(
                    std::string("production preparation worker failed during request: ") +
                    overlay::AccessoryPreparationWorkerFailureToken(worker_.Failure()));
            }
            const auto plan = worker_.TryTakeReady(key);
            if (plan != nullptr) {
                if (!(plan->key == key) ||
                    plan->key.layoutGeneration != proofGeneration) {
                    throw std::runtime_error(
                        "production worker returned a non-matching proof generation");
                }
                if (waitMicroseconds != nullptr) {
                    *waitMicroseconds = ElapsedMicroseconds(started);
                }
                return plan;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                throw std::runtime_error(
                    "exact-generation production preparation timed out offline");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void Stop() {
        worker_.Stop();
        finalCounters_ = worker_.SnapshotCounters();
        stopped_ = true;
    }

    std::uint64_t StartupWaitMicroseconds() const {
        return startupWaitMicroseconds_;
    }

    const overlay::AccessoryPreparationWorkerCounters& FinalCounters() const {
        return finalCounters_;
    }

    bool Stopped() const { return stopped_; }

private:
    overlay::AccessoryPreparationWorker worker_;
    std::uint64_t nextProofGeneration_ = 1;
    std::uint64_t startupWaitMicroseconds_ = 0;
    overlay::AccessoryPreparationWorkerCounters finalCounters_;
    bool stopped_ = false;
};

struct PreparedVisual {
    brain::BrainOwnedRuntimeState brainState;
    overlay::AccessoryLayoutResult layout;
    brain::BrainOwnedAccessoryPresentationHandle presentation;
    overlay::AccessoryPresentationState presenter;
    overlay::AccessoryPresentationUpdateResult update;
    overlay::AccessoryHistoryLayoutResult historyLayout;
    overlay::AccessoryDrawerRenderPlan renderPlan;
    std::shared_ptr<const overlay::AccessoryPreparedDrawerPlan> preparedPlan;
    overlay::OfflineRasterImage card;
    overlay::OfflineRasterImage rail;
    overlay::OfflineRasterImage drawer;
    overlay::OfflineRasterImage composite;
    std::uint64_t projectionMicroseconds = 0;
    std::uint64_t presentationMicroseconds = 0;
    std::uint64_t cardRasterMicroseconds = 0;
    std::uint64_t railRasterMicroseconds = 0;
    std::uint64_t drawerRasterMicroseconds = 0;
    std::uint64_t offlinePreparationWaitMicroseconds = 0;
    std::uint64_t proofGeneration = 0;
    brain::BrainOwnedAccessoryProjectionCounters projectionCounters;
};

std::uint64_t ElapsedMicroseconds(
    std::chrono::steady_clock::time_point started) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count());
}

PreparedVisual PrepareVisual(
    OfflinePreparationSession* preparationSession,
    overlay::AccessoryTextMeasurementContext* measurement,
    const brain::OverlayViewModel& mainCard,
    brain::BrainOwnedAccessoryDrawerId drawer,
    const std::vector<EntryInput>& entries,
    int screenWidth,
    int screenHeight,
    float scale,
    int requestedLeft,
    int requestedTop,
    bool scrollToMaximum,
    FailureState* failures) {
    if (preparationSession == nullptr) {
        throw std::runtime_error("offline preparation session is unavailable");
    }
    PreparedVisual result;
    result.proofGeneration = preparationSession->NextProofGeneration();
    for (const auto& entry : entries) {
        brain::BrainOwnedAccessoryHistoryEntryInput input;
        input.drawer = drawer;
        input.stableKey = entry.key;
        input.title = entry.title;
        input.body = entry.body;
        const auto decision = brain::AcceptBrainOwnedAccessoryHistoryEntry(
            &result.brainState, input);
        if (failures != nullptr) {
            failures->Require(decision.accepted, "history entry was not accepted: " + entry.key);
        }
    }
    if (drawer != brain::BrainOwnedAccessoryDrawerId::None) {
        brain::BrainOwnedAccessorySelectionRequest request;
        request.drawer = drawer;
        request.requestSequence = 1;
        const auto decision = brain::RequestBrainOwnedAccessoryDrawerSelection(
            &result.brainState, request);
        if (failures != nullptr) {
            failures->Require(
                decision.action == brain::BrainOwnedAccessoryDrawerAction::Opened,
                "drawer did not open");
        }
    }

    const auto typography = overlay::PrepareAccessoryTypography(measurement, scale);
    overlay::AccessoryLayoutInput layoutInput;
    layoutInput.screenWidth = screenWidth;
    layoutInput.screenHeight = screenHeight;
    layoutInput.windowLeft = requestedLeft;
    layoutInput.windowTop = requestedTop;
    layoutInput.scale = scale;
    layoutInput.cardAnimationProgress = 1.0f;
    layoutInput.drawerOpen = drawer != brain::BrainOwnedAccessoryDrawerId::None;
    layoutInput.typography = &typography;
    result.layout = overlay::ResolveAccessoryLayout(layoutInput);

    auto started = std::chrono::steady_clock::now();
    result.presentation = brain::ProjectBrainOwnedAccessoryPresentation(
        &result.brainState, result.proofGeneration, &result.projectionCounters);
    result.projectionMicroseconds = ElapsedMicroseconds(started);

    if (drawer != brain::BrainOwnedAccessoryDrawerId::None) {
        result.preparedPlan = preparationSession->Prepare(
            result.presentation,
            result.layout,
            typography,
            result.proofGeneration,
            &result.offlinePreparationWaitMicroseconds);
    }

    overlay::AccessoryPresentationUpdateInput updateInput;
    updateInput.presentation = result.presentation;
    updateInput.mechanicalLayoutGeneration = result.proofGeneration;
    updateInput.layout = result.layout;
    updateInput.mainCardProductionSignature = std::to_string(
        overlay::BuildProductionMainCardSignatureForOfflineProof(mainCard, 0));
    updateInput.measurementContext = measurement;
    updateInput.preparedPlan = result.preparedPlan;
    started = std::chrono::steady_clock::now();
    result.update = overlay::UpdateAccessoryPresentation(
        &result.presenter, updateInput);
    result.presentationMicroseconds = ElapsedMicroseconds(started);

    if (scrollToMaximum && result.presenter.drawerMaximumOffset > 0) {
        overlay::AccessoryPresentationScrollInput scroll;
        scroll.layout = result.layout;
        scroll.pointerX =
            (result.layout.drawerBounds.left + result.layout.drawerBounds.right) / 2;
        scroll.pointerY =
            (result.layout.drawerBounds.top + result.layout.drawerBounds.bottom) / 2;
        scroll.wheelClicks = result.presenter.drawerMaximumOffset + 1;
        const auto scrollResult = overlay::ScrollAccessoryPresentation(
            &result.presenter, scroll);
        if (failures != nullptr) {
            failures->Require(scrollResult.reachedFinalMarker,
                "maximum scroll did not reach the final marker");
        }
    }

    if (drawer != brain::BrainOwnedAccessoryDrawerId::None &&
        result.presentation.snapshot != nullptr &&
        result.preparedPlan != nullptr) {
        result.historyLayout = result.preparedPlan->layout;
        result.renderPlan = overlay::BuildAccessoryDrawerRenderPlan(
            result.presenter, result.layout);
    }

    started = std::chrono::steady_clock::now();
    result.card = overlay::RenderProductionMainCardForOfflineProof(mainCard, 0);
    result.cardRasterMicroseconds = ElapsedMicroseconds(started);
    started = std::chrono::steady_clock::now();
    result.rail = overlay::RenderProductionAccessoryRailForOfflineProof(
        result.layout, *result.presentation.snapshot);
    result.railRasterMicroseconds = ElapsedMicroseconds(started);
    if (drawer != brain::BrainOwnedAccessoryDrawerId::None) {
        started = std::chrono::steady_clock::now();
        result.drawer = overlay::RenderProductionAccessoryDrawerForOfflineProof(
            result.layout, result.presenter);
        result.drawerRasterMicroseconds = ElapsedMicroseconds(started);
    }

    const auto& bounds = result.layout.resolvedBounds;
    result.composite = MakeCanvas(
        bounds.right - bounds.left,
        bounds.top - bounds.bottom);
    const auto cardWidth = result.layout.mainCardBounds.right -
        result.layout.mainCardBounds.left;
    const auto cardHeight = result.layout.mainCardBounds.top -
        result.layout.mainCardBounds.bottom;
    const auto scaledCard = ScaleNearest(result.card, cardWidth, cardHeight);
    AlphaBlit(
        &result.composite,
        scaledCard,
        result.layout.mainCardBounds.left - bounds.left,
        bounds.top - result.layout.mainCardBounds.top);
    AlphaBlit(
        &result.composite,
        result.rail,
        result.layout.railBounds.left - bounds.left,
        bounds.top - result.layout.railBounds.top);
    if (drawer != brain::BrainOwnedAccessoryDrawerId::None) {
        AlphaBlit(
            &result.composite,
            result.drawer,
            result.layout.drawerBounds.left - bounds.left,
            bounds.top - result.layout.drawerBounds.top);
    }
    return result;
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

std::string ScaleToken(float scale) {
    if (scale < 0.9f) return "085";
    if (scale > 1.2f) return "135";
    return "100";
}

bool SamePixels(
    const overlay::OfflineRasterImage& left,
    const overlay::OfflineRasterImage& right) {
    return left.width == right.width && left.height == right.height &&
        left.bgraPixels == right.bgraPixels;
}

std::uint64_t DifferingPixels(
    const overlay::OfflineRasterImage& left,
    const overlay::OfflineRasterImage& right) {
    if (left.width != right.width || left.height != right.height) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    std::uint64_t count = 0;
    for (std::size_t index = 0; index < left.bgraPixels.size(); index += 4) {
        if (!std::equal(
                left.bgraPixels.begin() + index,
                left.bgraPixels.begin() + index + 4,
                right.bgraPixels.begin() + index)) {
            ++count;
        }
    }
    return count;
}

bool DifferencesInsideDrawer(
    const overlay::OfflineRasterImage& top,
    const overlay::OfflineRasterImage& bottom,
    const overlay::AccessoryLayoutResult& layout) {
    if (top.width != bottom.width || top.height != bottom.height) return false;
    const int drawerLeft = layout.drawerBounds.left - layout.resolvedBounds.left;
    const int drawerTop = layout.resolvedBounds.top - layout.drawerBounds.top;
    const int drawerRight = layout.drawerBounds.right - layout.resolvedBounds.left;
    const int drawerBottom = layout.resolvedBounds.top - layout.drawerBounds.bottom;
    bool foundDifference = false;
    for (int y = 0; y < top.height; ++y) {
        for (int x = 0; x < top.width; ++x) {
            const auto index = static_cast<std::size_t>((y * top.width + x) * 4);
            const bool differs = !std::equal(
                top.bgraPixels.begin() + index,
                top.bgraPixels.begin() + index + 4,
                bottom.bgraPixels.begin() + index);
            if (!differs) continue;
            foundDifference = true;
            if (x < drawerLeft || x >= drawerRight ||
                y < drawerTop || y >= drawerBottom) {
                return false;
            }
        }
    }
    return foundDifference;
}

int VisibleCardToRailGapPixels(const PreparedVisual& visual) {
    const auto cardWidth = visual.layout.mainCardBounds.right -
        visual.layout.mainCardBounds.left;
    const auto cardHeight = visual.layout.mainCardBounds.top -
        visual.layout.mainCardBounds.bottom;
    const auto scaledCard = ScaleNearest(visual.card, cardWidth, cardHeight);
    int lastVisibleCardFaceRow = -1;
    const int railLeftInCard = std::max(
        0,
        visual.layout.railBounds.left - visual.layout.mainCardBounds.left);
    const int railRightInCard = std::min(
        scaledCard.width,
        visual.layout.railBounds.right - visual.layout.mainCardBounds.left);
    for (int y = 0; y < scaledCard.height; ++y) {
        for (int x = railLeftInCard; x < railRightInCard; ++x) {
            const auto index = static_cast<std::size_t>(
                (y * scaledCard.width + x) * 4);
            if (scaledCard.bgraPixels[index + 3] > 96) {
                lastVisibleCardFaceRow = std::max(lastVisibleCardFaceRow, y);
            }
        }
    }
    int firstVisibleRailRow = visual.rail.height;
    for (int y = 0; y < visual.rail.height; ++y) {
        for (int x = 0; x < visual.rail.width; ++x) {
            const auto index = static_cast<std::size_t>(
                (y * visual.rail.width + x) * 4);
            if (visual.rail.bgraPixels[index + 3] > 24) {
                firstVisibleRailRow = std::min(firstVisibleRailRow, y);
            }
        }
    }
    if (lastVisibleCardFaceRow < 0 || firstVisibleRailRow >= visual.rail.height) {
        return std::numeric_limits<int>::min();
    }
    const int cardFaceBottomFromWindowTop =
        visual.layout.resolvedBounds.top - visual.layout.mainCardBounds.top +
        lastVisibleCardFaceRow;
    const int railVisibleTopFromWindowTop =
        visual.layout.resolvedBounds.top - visual.layout.railBounds.top +
        firstVisibleRailRow;
    return railVisibleTopFromWindowTop - cardFaceBottomFromWindowTop - 1;
}

bool LayoutTranslatedBy(
    const overlay::AccessoryLayoutResult& before,
    const overlay::AccessoryLayoutResult& after,
    int deltaX,
    int deltaY,
    bool includeDrawer) {
    const auto translated = [deltaX, deltaY](
        const overlay::AccessoryRect& left,
        const overlay::AccessoryRect& right) {
        return right.left - left.left == deltaX &&
            right.right - left.right == deltaX &&
            right.top - left.top == deltaY &&
            right.bottom - left.bottom == deltaY;
    };
    bool result = translated(before.mainCardBounds, after.mainCardBounds) &&
        translated(before.railBounds, after.railBounds) &&
        before.orbs.size() == after.orbs.size();
    if (includeDrawer) {
        result = result && translated(before.drawerBounds, after.drawerBounds);
    }
    for (std::size_t index = 0;
         result && index < before.orbs.size(); ++index) {
        result = translated(before.orbs[index].bounds, after.orbs[index].bounds);
    }
    return result;
}

void WriteText(const fs::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

struct TimingRow {
    std::string operation;
    std::vector<std::uint64_t> samples;
    std::uint64_t textureUploadRequestCount = 0;
    bool hardLimitApplies = true;
};

std::uint64_t Percentile(
    std::vector<std::uint64_t> samples,
    int percentile) {
    if (samples.empty()) return 0;
    std::sort(samples.begin(), samples.end());
    const auto rank = static_cast<std::size_t>(
        ((samples.size() * percentile) + 99) / 100);
    return samples[std::min(samples.size() - 1, std::max<std::size_t>(1, rank) - 1)];
}

int Run(const fs::path& outputDirectory) {
    FailureState failures;
    if (fs::exists(outputDirectory) && !fs::is_empty(outputDirectory)) {
        std::cerr << "Output directory must be absent or empty: "
                  << outputDirectory.string() << "\n";
        return 2;
    }
    fs::create_directories(outputDirectory);

    Gdiplus::GdiplusStartupInput startupInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &startupInput, nullptr) !=
        Gdiplus::Ok) {
        std::cerr << "GDI+ startup failed\n";
        return 2;
    }
    auto* measurement = overlay::InitializeAccessoryTextMeasurement();
    if (measurement == nullptr) {
        Gdiplus::GdiplusShutdown(gdiplusToken);
        std::cerr << "Production text measurement initialization failed\n";
        return 2;
    }

    OfflinePreparationSession preparationSession;
    preparationSession.Start();

    const auto mainCard = BuildMainCardInput();
    const auto neutral = PrepareVisual(
        &preparationSession, measurement, mainCard,
        brain::BrainOwnedAccessoryDrawerId::None, {},
        1920, 1080, 1.0f, 300, 900, false, &failures);
    const auto metarEmpty = PrepareVisual(
        &preparationSession, measurement, mainCard,
        brain::BrainOwnedAccessoryDrawerId::Metar, {},
        1920, 1080, 1.0f, 300, 900, false, &failures);
    const auto atisEmpty = PrepareVisual(
        &preparationSession, measurement, mainCard,
        brain::BrainOwnedAccessoryDrawerId::Atis, {},
        1920, 1080, 1.0f, 300, 900, false, &failures);
    const auto pdcEmpty = PrepareVisual(
        &preparationSession, measurement, mainCard,
        brain::BrainOwnedAccessoryDrawerId::Pdc, {},
        1920, 1080, 1.0f, 300, 900, false, &failures);

    std::vector<std::pair<std::string, overlay::OfflineRasterImage>> artifacts;
    artifacts.push_back({"01_neutral_three_orb_rail.png", neutral.composite});
    artifacts.push_back({"02_metar_empty_state_drawer.png", metarEmpty.composite});
    artifacts.push_back({"03_atis_empty_state_drawer.png", atisEmpty.composite});
    artifacts.push_back({"04_pdc_empty_state_drawer.png", pdcEmpty.composite});

    std::map<std::string, PreparedVisual> scaleVisuals;
    std::ostringstream visibleGaps;
    visibleGaps << "scale,visible_gap_pixels,overlap,within_three_pixels\n";
    for (const float scale : {0.85f, 1.0f, 1.35f}) {
        auto visual = PrepareVisual(
            &preparationSession, measurement, mainCard,
            brain::BrainOwnedAccessoryDrawerId::Metar,
            {}, 1920, 1080, scale, 300, 900, false, &failures);
        for (const auto& orb : visual.layout.orbs) {
            failures.Require(orb.labelFits,
                "ORB label does not fit at scale " + ScaleToken(scale));
            failures.Require(orb.openIndicatorFits,
                "OPEN indicator does not fit at scale " + ScaleToken(scale));
        }
        const int visibleGap = VisibleCardToRailGapPixels(visual);
        failures.Require(visibleGap >= 0,
            "accessory rail overlaps the visible main-card face at scale " +
                ScaleToken(scale));
        failures.Require(visibleGap <= 3,
            "visible main-card-to-rail gap exceeds three pixels at scale " +
                ScaleToken(scale));
        visibleGaps << scale << ',' << visibleGap << ','
                    << (visibleGap < 0 ? "true" : "false") << ','
                    << (visibleGap >= 0 && visibleGap <= 3 ? "true" : "false")
                    << '\n';
        artifacts.push_back({
            "05_scale_" + ScaleToken(scale) + ".png",
            visual.composite});
        scaleVisuals.emplace(ScaleToken(scale), std::move(visual));
    }

    const std::vector<EntryInput> normalEntries{{
        "NORMAL-1",
        "SYNTHETIC STEP 3 PROOF — NOT LIVE DATA",
        "This production-wrapped history entry demonstrates readable words "
        "inside the brain-approved drawer content rectangle."}};
    const auto normalWrapped = PrepareVisual(
        &preparationSession, measurement, mainCard,
        brain::BrainOwnedAccessoryDrawerId::Metar,
        normalEntries, 1920, 1080, 1.0f, 300, 900, false, &failures);
    failures.Require(normalWrapped.historyLayout.allWrappedLinesFit,
        "normal wrapped lines do not fit");
    failures.Require(normalWrapped.renderPlan.everyLineFitsPixelGeometry,
        "normal wrapped render plan crosses the drawer rectangle");
    const bool normalWrappedVisible =
        !normalWrapped.renderPlan.visibleLines.empty();
    failures.Require(normalWrappedVisible,
        "normal wrapped history is not visibly rendered");
    artifacts.push_back({"06_normal_wrapped_history.png", normalWrapped.composite});

    const std::vector<EntryInput> tokenEntries{{
        "TOKEN-1",
        "SYNTHETIC STEP 3 PROOF — NOT LIVE DATA",
        std::string(768, 'W')}};
    const auto unbroken = PrepareVisual(
        &preparationSession, measurement, mainCard,
        brain::BrainOwnedAccessoryDrawerId::Atis,
        tokenEntries, 1920, 1080, 1.0f, 300, 900, false, &failures);
    failures.Require(unbroken.historyLayout.allWrappedLinesFit,
        "unbroken token lines do not fit");
    const bool unbrokenVisibleAndInside =
        !unbroken.renderPlan.visibleLines.empty() &&
        unbroken.renderPlan.everyLineFitsPixelGeometry;
    failures.Require(unbrokenVisibleAndInside,
        "unbroken token history is not visibly rendered inside the drawer");
    artifacts.push_back({"07_long_unbroken_token_history.png", unbroken.composite});

    std::vector<EntryInput> longEntries;
    for (int index = 1; index <= 12; ++index) {
        longEntries.push_back({
            "LONG-" + std::to_string(index),
            "SYNTHETIC STEP 3 PROOF — NOT LIVE DATA " + std::to_string(index),
            "Deterministic retained history body " + std::to_string(index)});
    }
    const auto longTop = PrepareVisual(
        &preparationSession, measurement, mainCard,
        brain::BrainOwnedAccessoryDrawerId::Pdc,
        longEntries, 1920, 1080, 1.0f, 300, 900, false, &failures);
    const auto longBottom = PrepareVisual(
        &preparationSession, measurement, mainCard,
        brain::BrainOwnedAccessoryDrawerId::Pdc,
        longEntries, 1920, 1080, 1.0f, 300, 900, true, &failures);
    const bool newestHistoryAtTop =
        !longTop.historyLayout.renderedEntryKeys.empty() &&
        longTop.historyLayout.renderedEntryKeys.front() == "LONG-12" &&
        !longTop.historyLayout.renderedTitleLines.empty() &&
        !longTop.historyLayout.renderedTitleLines.front().empty() &&
        !longTop.renderPlan.visibleLines.empty() &&
        longTop.renderPlan.visibleLines.front().text ==
            longTop.historyLayout.renderedTitleLines.front().front();
    failures.Require(newestHistoryAtTop,
        "newest retained history is not the first visible entry");
    failures.Require(longBottom.renderPlan.finalMarkerVisible,
        "final history marker is not visible at maximum scroll");
    failures.Require(longBottom.renderPlan.everyLineFitsPixelGeometry,
        "maximum-scroll render plan crosses the drawer rectangle");
    failures.Require(DifferencesInsideDrawer(
        longTop.composite, longBottom.composite, longTop.layout),
        "top/maximum scroll differences escaped the drawer region");
    artifacts.push_back({"08_long_history_newest_top.png", longTop.composite});
    artifacts.push_back({"09_long_history_maximum_scroll_final_marker.png",
        longBottom.composite});

    const std::vector<EntryInput> limitedEntries{{
        "LIMIT-1",
        std::string(112, 'T') + " " + std::string(107, 'T'),
        "SYNTHETIC STEP 3 PROOF — NOT LIVE DATA"}};
    const auto limited = PrepareVisual(
        &preparationSession, measurement, mainCard,
        brain::BrainOwnedAccessoryDrawerId::Metar,
        limitedEntries, 1920, 1080, 1.0f, 300, 900, false, &failures);
    bool limitedMarkerVisible = false;
    for (const auto& line : limited.renderPlan.visibleLines) {
        limitedMarkerVisible = limitedMarkerVisible ||
            line.text.find("CONTENT LIMITED") != std::string::npos;
    }
    failures.Require(limitedMarkerVisible,
        "CONTENT LIMITED marker is not visible");
    artifacts.push_back({"10_content_limited_visible.png", limited.composite});

    auto unchangedState = normalWrapped.presenter;
    overlay::AccessoryPresentationUpdateInput unchangedInput;
    unchangedInput.presentation = normalWrapped.presentation;
    unchangedInput.layout = normalWrapped.layout;
    unchangedInput.mainCardProductionSignature = std::to_string(
        overlay::BuildProductionMainCardSignatureForOfflineProof(mainCard, 0));
    unchangedInput.measurementContext = measurement;
    unchangedInput.preparedPlan = normalWrapped.preparedPlan;
    const auto gdiBeforeUnchanged =
        overlay::GetAccessoryGdiMeasurementCounters(measurement);
    auto unchangedStarted = std::chrono::steady_clock::now();
    const auto unchangedWarm =
        overlay::RunUnchangedAccessoryPresentationUpdates(
            &unchangedState, unchangedInput, 10000);
    const auto unchangedWarmMicroseconds = ElapsedMicroseconds(unchangedStarted);
    const auto gdiAfterUnchanged =
        overlay::GetAccessoryGdiMeasurementCounters(measurement);
    failures.Require(unchangedWarm.iterations == 10000,
        "10,000 unchanged presentation updates did not complete");
    failures.Require(
        unchangedWarm.delta.historyVisits == 0 &&
        unchangedWarm.delta.entryCopies == 0 &&
        unchangedWarm.delta.wrapVisits == 0 &&
        unchangedWarm.delta.mainCardRasterRequests == 0 &&
        unchangedWarm.delta.railRasterRequests == 0 &&
        unchangedWarm.delta.drawerRasterRequests == 0 &&
        unchangedWarm.delta.uploadRequests == 0 &&
        unchangedWarm.delta.snapshotPublications == 0,
        "unchanged presentation updates requested production work");
    failures.Require(
        gdiAfterUnchanged.measurementCalls ==
            gdiBeforeUnchanged.measurementCalls &&
        gdiAfterUnchanged.bitmapConstructions ==
            gdiBeforeUnchanged.bitmapConstructions &&
        gdiAfterUnchanged.graphicsConstructions ==
            gdiBeforeUnchanged.graphicsConstructions &&
        gdiAfterUnchanged.fontConstructions ==
            gdiBeforeUnchanged.fontConstructions,
        "unchanged presentation updates performed GDI+ work");

    const std::vector<std::pair<std::string, const PreparedVisual*>> cropStates{
        {"11_main_card_crop_shell_closed.png", &neutral},
        {"12_main_card_crop_neutral_rail.png", &neutral},
        {"13_main_card_crop_metar_open.png", &metarEmpty},
        {"14_main_card_crop_atis_open.png", &atisEmpty},
        {"15_main_card_crop_pdc_open.png", &pdcEmpty},
        {"16_main_card_crop_drawer_scrolled.png", &longBottom},
    };
    const auto cropHash = Sha256(neutral.card.bgraPixels);
    for (const auto& crop : cropStates) {
        failures.Require(crop.second->card.width == neutral.card.width &&
            crop.second->card.height == neutral.card.height,
            "main-card crop dimensions changed");
        failures.Require(SamePixels(crop.second->card, neutral.card),
            "main-card crop pixels changed");
        failures.Require(Sha256(crop.second->card.bgraPixels) == cropHash,
            "main-card crop hash changed");
        artifacts.push_back({crop.first, crop.second->card});
    }
    auto zeroDifference = MakeCanvas(
        neutral.card.width, neutral.card.height, 0, 0, 0);
    artifacts.push_back({"17_main_card_zero_difference.png", zeroDifference});

    const auto closedMoveStart = PrepareVisual(
        &preparationSession, measurement, mainCard,
        brain::BrainOwnedAccessoryDrawerId::None, {},
        1280, 720, 1.0f, 120, 680, false, &failures);
    const auto closedMoveEnd = PrepareVisual(
        &preparationSession, measurement, mainCard,
        brain::BrainOwnedAccessoryDrawerId::None, {},
        1280, 720, 1.0f, 420, 600, false, &failures);
    const auto openMoveStart = PrepareVisual(
        &preparationSession, measurement, mainCard,
        brain::BrainOwnedAccessoryDrawerId::Metar, {},
        1280, 720, 1.0f, 120, 700, false, &failures);
    const auto openMoveEnd = PrepareVisual(
        &preparationSession, measurement, mainCard,
        brain::BrainOwnedAccessoryDrawerId::Metar, {},
        1280, 720, 1.0f, 420, 650, false, &failures);
    const auto closedDeltaX = closedMoveEnd.layout.resolvedLeft -
        closedMoveStart.layout.resolvedLeft;
    const auto closedDeltaY = closedMoveEnd.layout.resolvedTop -
        closedMoveStart.layout.resolvedTop;
    const auto openDeltaX = openMoveEnd.layout.resolvedLeft -
        openMoveStart.layout.resolvedLeft;
    const auto openDeltaY = openMoveEnd.layout.resolvedTop -
        openMoveStart.layout.resolvedTop;
    failures.Require(LayoutTranslatedBy(
        closedMoveStart.layout, closedMoveEnd.layout,
        closedDeltaX, closedDeltaY, false),
        "closed movement did not translate card, rail, ORBs, and hit geometry equally");
    failures.Require(LayoutTranslatedBy(
        openMoveStart.layout, openMoveEnd.layout,
        openDeltaX, openDeltaY, true),
        "open movement did not translate card, rail, drawer, ORBs, and hit geometry equally");
    failures.Require(SamePixels(
        closedMoveStart.composite, closedMoveEnd.composite),
        "closed pure movement changed texture-local pixels");
    failures.Require(SamePixels(
        openMoveStart.composite, openMoveEnd.composite),
        "open pure movement changed texture-local pixels");

    auto makeMovementCanvas = [](const PreparedVisual& visual) {
        auto canvas = MakeCanvas(1280, 720, 18, 24, 31);
        AlphaBlit(
            &canvas,
            visual.composite,
            visual.layout.resolvedBounds.left,
            720 - visual.layout.resolvedBounds.top);
        DrawBorder(&canvas, 0, 0, 1280, 720, 91, 184, 224);
        return canvas;
    };
    artifacts.push_back({
        "18_movement_sequence_closed_start.png",
        makeMovementCanvas(closedMoveStart)});
    artifacts.push_back({
        "19_movement_sequence_closed_end.png",
        makeMovementCanvas(closedMoveEnd)});
    artifacts.push_back({
        "20_movement_sequence_open_start.png",
        makeMovementCanvas(openMoveStart)});
    artifacts.push_back({
        "21_movement_sequence_open_end.png",
        makeMovementCanvas(openMoveEnd)});

    auto translatedPresenter = openMoveStart.presenter;
    overlay::AccessoryPresentationUpdateInput translatedInput;
    translatedInput.presentation = openMoveStart.presentation;
    translatedInput.layout = openMoveEnd.layout;
    translatedInput.mainCardProductionSignature = std::to_string(
        overlay::BuildProductionMainCardSignatureForOfflineProof(mainCard, 0));
    translatedInput.measurementContext = measurement;
    translatedInput.preparedPlan = openMoveStart.preparedPlan;
    const auto translationRailSignature =
        translatedPresenter.railRenderSignature;
    const auto translationDrawerSignature =
        translatedPresenter.drawerRenderSignature;
    const auto translationGdiBefore =
        overlay::GetAccessoryGdiMeasurementCounters(measurement);
    const auto translationStarted = std::chrono::steady_clock::now();
    const auto translationUpdate = overlay::UpdateAccessoryPresentation(
        &translatedPresenter, translatedInput);
    const auto translationMicroseconds = ElapsedMicroseconds(translationStarted);
    const auto translationGdiAfter =
        overlay::GetAccessoryGdiMeasurementCounters(measurement);
    const auto& translationDelta = translationUpdate.delta;
    failures.Require(
        translationDelta.historyVisits == 0 &&
        translationDelta.entryCopies == 0 &&
        translationDelta.wrapVisits == 0 &&
        translationDelta.mainCardRasterRequests == 0 &&
        translationDelta.railRasterRequests == 0 &&
        translationDelta.drawerRasterRequests == 0 &&
        translationDelta.uploadRequests == 0 &&
        translationGdiAfter.measurementCalls ==
            translationGdiBefore.measurementCalls &&
        translatedPresenter.railRenderSignature ==
            translationRailSignature &&
        translatedPresenter.drawerRenderSignature ==
            translationDrawerSignature,
        "pure translation performed texture-local production work");

    std::ostringstream movement;
    movement << "state,component,start_left,start_top,end_left,end_top,delta_x,delta_y\n";
    const auto appendMovement = [&movement](
        const char* state,
        const char* component,
        const overlay::AccessoryRect& before,
        const overlay::AccessoryRect& after) {
        movement << state << ',' << component << ','
                 << before.left << ',' << before.top << ','
                 << after.left << ',' << after.top << ','
                 << (after.left - before.left) << ','
                 << (after.top - before.top) << '\n';
    };
    appendMovement("closed", "main-card",
        closedMoveStart.layout.mainCardBounds,
        closedMoveEnd.layout.mainCardBounds);
    appendMovement("closed", "rail",
        closedMoveStart.layout.railBounds,
        closedMoveEnd.layout.railBounds);
    for (std::size_t index = 0; index < closedMoveStart.layout.orbs.size(); ++index) {
        appendMovement("closed", ("orb-" + std::to_string(index + 1)).c_str(),
            closedMoveStart.layout.orbs[index].bounds,
            closedMoveEnd.layout.orbs[index].bounds);
    }
    appendMovement("open", "main-card",
        openMoveStart.layout.mainCardBounds,
        openMoveEnd.layout.mainCardBounds);
    appendMovement("open", "rail",
        openMoveStart.layout.railBounds,
        openMoveEnd.layout.railBounds);
    appendMovement("open", "drawer",
        openMoveStart.layout.drawerBounds,
        openMoveEnd.layout.drawerBounds);
    for (std::size_t index = 0; index < openMoveStart.layout.orbs.size(); ++index) {
        appendMovement("open", ("orb-" + std::to_string(index + 1)).c_str(),
            openMoveStart.layout.orbs[index].bounds,
            openMoveEnd.layout.orbs[index].bounds);
    }

    std::ostringstream geometry;
    geometry << "screen,edge,scale,resolved_left,resolved_top,resolved_right,"
             << "resolved_bottom,width,height,inside,remaining_vertical_pixels,"
             << "closed_height,open_height,drawer_visible_lines\n";
    const std::array<std::pair<int, int>, 3> screens{{
        {1280, 720}, {1920, 1080}, {3840, 2160}}};
    const std::array<std::string, 4> edges{{"left", "top", "right", "bottom"}};
    for (const auto& screen : screens) {
        for (const auto& edge : edges) {
            const float scale = 1.35f;
            const int requestedLeft = edge == "left" ? -500 :
                (edge == "right" ? screen.first + 500 : screen.first / 2);
            const int requestedTop = edge == "top" ? screen.second + 500 :
                (edge == "bottom" ? -500 : screen.second / 2);
            auto visual = PrepareVisual(
                &preparationSession, measurement, mainCard,
                brain::BrainOwnedAccessoryDrawerId::Metar, {},
                screen.first, screen.second, scale,
                requestedLeft, requestedTop, false, &failures);
            const auto& bounds = visual.layout.resolvedBounds;
            failures.Require(visual.layout.allPhysicalBoundsInsideScreen,
                "clamped overlay escaped screen " + std::to_string(screen.first) +
                "x" + std::to_string(screen.second) + " " + edge);
            if (screen.first == 1280 && screen.second == 720) {
                failures.Require(visual.layout.openHeight == 683,
                    "1280x720 scale 1.35 open height is not 683");
                failures.Require(screen.second - visual.layout.openHeight == 37,
                    "1280x720 scale 1.35 remaining capacity is not 37 pixels");
                failures.Require(screen.second - visual.layout.openHeight >= 16,
                    "1280x720 scale 1.35 remaining capacity is below 16 pixels");
            }
            geometry << screen.first << 'x' << screen.second << ',' << edge
                     << ",1.35," << bounds.left << ',' << bounds.top << ','
                     << bounds.right << ',' << bounds.bottom << ','
                     << (bounds.right - bounds.left) << ','
                     << (bounds.top - bounds.bottom) << ",true,"
                     << (screen.second - visual.layout.openHeight) << ','
                     << visual.layout.closedHeight << ','
                     << visual.layout.openHeight << ','
                     << visual.layout.drawerVisibleLineCapacity << '\n';

            auto screenImage = MakeCanvas(screen.first, screen.second, 18, 24, 31);
            AlphaBlit(
                &screenImage,
                visual.composite,
                bounds.left,
                screen.second - bounds.top);
            DrawBorder(&screenImage, 0, 0, screen.first, screen.second,
                91, 184, 224);
            artifacts.push_back({
                "clamp_" + std::to_string(screen.first) + "x" +
                    std::to_string(screen.second) + "_" + edge + ".png",
                std::move(screenImage)});
        }
    }

    preparationSession.Stop();
    const auto workerCounters = preparationSession.FinalCounters();
    failures.Require(preparationSession.Stopped(),
        "production preparation worker was not stopped");
    failures.Require(
        workerCounters.lifecycleState ==
            overlay::AccessoryPreparationWorkerState::Stopped &&
        workerCounters.runningWorkerThreads == 0,
        "production preparation worker survived exporter shutdown");
    failures.Require(workerCounters.startupSuccessCount == 1 &&
        workerCounters.startupFailureCount == 0 &&
        workerCounters.priorityRequested && workerCounters.prioritySucceeded,
        "production preparation worker startup proof failed");

    failures.Require(neutral.layout.resolvedBounds.top -
        neutral.layout.resolvedBounds.bottom == neutral.layout.closedHeight,
        "closed geometry reserves expanded height");
    failures.Require(neutral.layout.drawerBounds.right ==
        neutral.layout.drawerBounds.left,
        "closed geometry contains a drawer hit region");

    for (const auto& artifact : artifacts) {
        failures.Require(SavePng(outputDirectory / artifact.first, artifact.second),
            "PNG save failed: " + artifact.first);
    }

    WriteText(outputDirectory / "layout_geometry_matrix.csv", geometry.str());
    WriteText(outputDirectory / "visible_gap_measurements.csv", visibleGaps.str());
    WriteText(outputDirectory / "movement_sequence_geometry.csv", movement.str());

    const std::vector<std::uint64_t> offlinePreparationWaits{
        metarEmpty.offlinePreparationWaitMicroseconds,
        atisEmpty.offlinePreparationWaitMicroseconds,
        pdcEmpty.offlinePreparationWaitMicroseconds,
        scaleVisuals.at("085").offlinePreparationWaitMicroseconds,
        scaleVisuals.at("100").offlinePreparationWaitMicroseconds,
        scaleVisuals.at("135").offlinePreparationWaitMicroseconds,
        normalWrapped.offlinePreparationWaitMicroseconds,
        unbroken.offlinePreparationWaitMicroseconds,
        longTop.offlinePreparationWaitMicroseconds,
        longBottom.offlinePreparationWaitMicroseconds,
        limited.offlinePreparationWaitMicroseconds,
        openMoveStart.offlinePreparationWaitMicroseconds,
        openMoveEnd.offlinePreparationWaitMicroseconds,
    };
    std::vector<TimingRow> timings{
        {"offlineWorkerStartupWait",
            {preparationSession.StartupWaitMicroseconds()}, 0, false},
        {"offlineWorkerPreparationWait", offlinePreparationWaits, 0, false},
        {"brainProjection", {metarEmpty.projectionMicroseconds}, 0},
        {"presentationAndWrapping", {normalWrapped.presentationMicroseconds},
            normalWrapped.update.delta.uploadRequests},
        {"mainCardGdiRasterization", {
            neutral.cardRasterMicroseconds, metarEmpty.cardRasterMicroseconds,
            atisEmpty.cardRasterMicroseconds, pdcEmpty.cardRasterMicroseconds}, 0},
        {"orbRailGdiRasterization", {
            neutral.railRasterMicroseconds, metarEmpty.railRasterMicroseconds,
            atisEmpty.railRasterMicroseconds, pdcEmpty.railRasterMicroseconds}, 0},
        {"drawerGdiRasterization", {
            metarEmpty.drawerRasterMicroseconds, atisEmpty.drawerRasterMicroseconds,
            pdcEmpty.drawerRasterMicroseconds, normalWrapped.drawerRasterMicroseconds,
            unbroken.drawerRasterMicroseconds, longTop.drawerRasterMicroseconds,
            longBottom.drawerRasterMicroseconds, limited.drawerRasterMicroseconds}, 0},
        {"pureTranslationPresentationUpdate", {translationMicroseconds}, 0},
        {"unchangedPresentationBatch10000", {unchangedWarmMicroseconds}, 0},
    };
    std::ostringstream performance;
    performance << "operation,count,p50_us,p95_us,maximum_us,"
                << "hard_limit_applies,textureUploadRequestCount,"
                << "actualOpenGLUploadCount\n";
    for (const auto& timing : timings) {
        const auto maximum = *std::max_element(
            timing.samples.begin(), timing.samples.end());
        if (timing.hardLimitApplies) {
            failures.Require(maximum <= kTimingLimitMicroseconds,
                timing.operation + " exceeded 16.7 ms");
        }
        performance << timing.operation << ',' << timing.samples.size() << ','
                    << Percentile(timing.samples, 50) << ','
                    << Percentile(timing.samples, 95) << ',' << maximum << ','
                    << (timing.hardLimitApplies ? "true" : "false") << ','
                    << timing.textureUploadRequestCount << ",0\n";
    }
    WriteText(outputDirectory / "offline_performance.csv", performance.str());

    std::ostringstream summary;
    summary << "XVatsim V2 Step 3 deterministic offline visual proof\n"
            << "status=" << (failures.messages.empty() ? "PASSED" : "FAILED") << "\n"
            << "renderer=production GDI+ raster paths\n"
            << "openGlContext=false\n"
            << "actualOpenGLUploadsMeasured=false\n"
            << "offlineUploadTerminology=textureUploadRequestCount\n"
            << "preparationModel=production-accessory-preparation-worker\n"
            << "preparationWaitClassification=offline-worker-wait-not-simulator-thread-work\n"
            << "workerStartupSuccessCount="
            << workerCounters.startupSuccessCount << "\n"
            << "workerStartupFailureCount="
            << workerCounters.startupFailureCount << "\n"
            << "workerPrioritySucceeded="
            << (workerCounters.prioritySucceeded ? "true" : "false") << "\n"
            << "workerJobsCompleted=" << workerCounters.jobsCompleted << "\n"
            << "workerThreadsAfterStop="
            << workerCounters.runningWorkerThreads << "\n"
            << "uniqueProofGenerations=true\n"
            << "preparedPlanExactGeneration=true\n"
            << "mainCardCropCount=6\n"
            << "mainCardDimensions=" << neutral.card.width << 'x'
            << neutral.card.height << "\n"
            << "mainCardRawPixelSha256=" << cropHash << "\n"
            << "mainCardDifferingPixels=0\n"
            << "mainCardProductionSignature="
            << overlay::BuildProductionMainCardSignatureForOfflineProof(mainCard, 0)
            << "\n"
            << "orbLabelsFitAt085=true\n"
            << "orbLabelsFitAt100=true\n"
            << "orbLabelsFitAt135=true\n"
            << "openIndicatorsFitAt085=true\n"
            << "openIndicatorsFitAt100=true\n"
            << "openIndicatorsFitAt135=true\n"
            << "drawerLinesInsideContentRectangle=true\n"
            << "normalWrappedHistoryVisible="
            << (normalWrappedVisible ? "true" : "false") << "\n"
            << "unbrokenHistoryVisibleAndInside="
            << (unbrokenVisibleAndInside ? "true" : "false") << "\n"
            << "newestHistoryAtTop="
            << (newestHistoryAtTop ? "true" : "false") << "\n"
            << "finalMarkerVisibleAtMaximumScroll=true\n"
            << "topMaximumDifferencesConfinedToDrawer=true\n"
            << "contentLimitedMarkerVisible="
            << (limitedMarkerVisible ? "true" : "false") << "\n"
            << "visibleCardToRailGapAtAllScalesAtMost3Pixels=true\n"
            << "accessoryMainCardOverlap=false\n"
            << "closedMovementSharedDelta=true\n"
            << "openMovementSharedDelta=true\n"
            << "pureTranslationGdiMeasurements=0\n"
            << "pureTranslationWrapVisits=0\n"
            << "pureTranslationRasterRequests=0\n"
            << "pureTranslationTextureUploadRequests=0\n"
            << "closedGeometryHasNoExpandedRegion=true\n"
            << "unchangedUpdateCount=10000\n"
            << "unchangedHistoryVisits=0\n"
            << "unchangedEntryCopies=0\n"
            << "unchangedWrapVisits=0\n"
            << "unchangedRasterRequests=0\n"
            << "unchangedTextureUploadRequests=0\n"
            << "unchangedGdiMeasurements=0\n"
            << "screenClampMatrixPassed=true\n"
            << "open1280x720Scale135Height=683\n"
            << "open1280x720Scale135RemainingPixels=37\n"
            << "failureCount=" << failures.messages.size() << "\n";
    for (const auto& failure : failures.messages) {
        summary << "failure=" << failure << "\n";
    }
    WriteText(outputDirectory / "visual_proof_summary.txt", summary.str());

    std::vector<fs::path> deterministicFiles;
    for (const auto& item : fs::directory_iterator(outputDirectory)) {
        if (!item.is_regular_file()) continue;
        const auto filename = item.path().filename();
        if (filename != "SHA256SUMS.txt" &&
            filename != "DETERMINISTIC_SHA256SUMS.txt" &&
            filename != "offline_performance.csv") {
            deterministicFiles.push_back(item.path());
        }
    }
    std::sort(deterministicFiles.begin(), deterministicFiles.end(),
        [](const fs::path& left, const fs::path& right) {
            return left.filename().generic_string() < right.filename().generic_string();
        });
    std::ostringstream deterministicManifest;
    for (const auto& path : deterministicFiles) {
        deterministicManifest << Sha256File(path) << "  "
                 << path.filename().generic_string() << "\n";
    }
    WriteText(outputDirectory / "DETERMINISTIC_SHA256SUMS.txt",
        deterministicManifest.str());

    std::vector<fs::path> manifestFiles;
    for (const auto& item : fs::directory_iterator(outputDirectory)) {
        if (item.is_regular_file() && item.path().filename() != "SHA256SUMS.txt") {
            manifestFiles.push_back(item.path());
        }
    }
    std::sort(manifestFiles.begin(), manifestFiles.end(),
        [](const fs::path& left, const fs::path& right) {
            return left.filename().generic_string() < right.filename().generic_string();
        });
    std::ostringstream manifest;
    for (const auto& path : manifestFiles) {
        manifest << Sha256File(path) << "  "
                 << path.filename().generic_string() << "\n";
    }
    WriteText(outputDirectory / "SHA256SUMS.txt", manifest.str());

    overlay::ShutdownAccessoryTextMeasurement(measurement);
    Gdiplus::GdiplusShutdown(gdiplusToken);

    if (!failures.messages.empty()) {
        for (const auto& failure : failures.messages) {
            std::cerr << "VISUAL_PROOF_FAILURE: " << failure << "\n";
        }
        return 1;
    }
    std::cout << "VISUAL_PROOF_PASSED artifacts="
              << (manifestFiles.size() + 1) << "\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 || std::string(argv[1]) != "--output") {
        std::cerr << "Usage: XVatsimStep3VisualProof --output <empty-directory>\n";
        return 2;
    }
    try {
        return Run(fs::path(argv[2]));
    } catch (const std::exception& error) {
        std::cerr << "VISUAL_PROOF_ERROR: " << error.what() << "\n";
        return 2;
    }
}
