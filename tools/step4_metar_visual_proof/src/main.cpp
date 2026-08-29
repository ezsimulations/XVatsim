#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <windows.h>
#include <objidl.h>
#include <bcrypt.h>
#include <gdiplus.h>

#include "XVatsim/brain/BrainMetarRuntime.h"
#include "XVatsim/brain/BrainOwnedRuntime.h"
#include "XVatsim/modules/overlay/OverlayAccessoryCore.h"
#include "XVatsim/modules/overlay/OverlayVisualProof.h"

namespace fs = std::filesystem;
namespace brain = xvatsim::brain;
namespace overlay = xvatsim::modules::overlay;

namespace {

struct VisualSpec {
    std::string filename;
    std::string variant;
};

const VisualSpec kVisuals[]{
    {"01_unknown_no_primary.png", "unknown"},
    {"02_primary_pending.png", "pending"},
    {"03_primary_unavailable.png", "unavailable"},
    {"04_primary_stale.png", "stale"},
    {"05_primary_vfr.png", "vfr"},
    {"06_primary_mvfr.png", "mvfr"},
    {"07_primary_ifr.png", "ifr"},
    {"08_primary_lifr.png", "lifr"},
    {"09_selected_no_open.png", "selected"},
    {"10_lookup_pending.png", "lookup-pending"},
    {"11_lookup_spotlight.png", "lookup-spotlight"},
    {"12_lookup_failure.png", "lookup-failure"},
    {"13_primary_preempts_spotlight.png", "preempted"},
    {"14_main_card_zero_difference.png", "main-card"},
    {"17_second_kabq_spotlight.png", "lookup-spotlight"},
    {"18_returned_kdfw_primary.png", "vfr"},
    {"19_atis_owns_spotlight_expiry.png", "atis-ownership"},
    {"20_pdc_owns_spotlight_expiry.png", "pdc-ownership"},
    {"21_metar_closed_no_delayed_reopen.png", "metar-closed"},
    {"22_normal_scale_cockpit_orb_crop.png", "orb-crop"},
    {"23_neutral_before_primary_acceptance.png", "unknown"},
    {"24_automatic_kdfw_vfr_no_click.png", "vfr"},
    {"25_automatic_kdfw_mvfr_transition.png", "mvfr"},
    {"26_automatic_ksan_primary_transition.png", "ksan-ifr"},
    {"27_closed_drawer_automatic_update.png", "vfr"},
    {"28_atis_selected_during_metar_update.png", "atis-ownership"},
    {"29_pdc_selected_during_metar_update.png", "pdc-ownership"},
    {"30_kabq_spotlight_unchanged_kdfw_orb.png", "lookup-spotlight"},
    {"31_identical_primary_zero_rail_difference.png", "vfr"},
    {"32_actual_rail_before_pixel_comparison.png", "unknown"},
    {"33_actual_rail_after_pixel_comparison.png", "vfr"},
};

constexpr std::uint64_t kAcceptedMainCardSignature =
    8'949'928'878'432'326'300ULL;

std::uint64_t ElapsedUs(std::chrono::steady_clock::time_point started) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count());
}

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

std::vector<unsigned char> ReadBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

std::string Sha256(const fs::path& path) {
    const auto bytes = ReadBytes(path);
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectBytes = 0;
    DWORD returned = 0;
    std::array<unsigned char, 32> digest{};
    if (BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0 ||
        BCryptGetProperty(
            algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes),
            &returned, 0) != 0) {
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    std::vector<unsigned char> object(objectBytes);
    const bool failed = BCryptCreateHash(
            algorithm, &hash, object.data(), objectBytes, nullptr, 0, 0) != 0 ||
        (!bytes.empty() && BCryptHashData(
            hash, const_cast<PUCHAR>(bytes.data()),
            static_cast<ULONG>(bytes.size()), 0) != 0) ||
        BCryptFinishHash(hash, digest.data(), digest.size(), 0) != 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return failed ? std::string{} : Hex(digest.data(), digest.size());
}

int FindPngEncoder(CLSID* encoder) {
    UINT count = 0;
    UINT bytes = 0;
    Gdiplus::GetImageEncodersSize(&count, &bytes);
    if (bytes == 0 || encoder == nullptr) return -1;
    std::vector<unsigned char> storage(bytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
    if (Gdiplus::GetImageEncoders(count, bytes, encoders) != Gdiplus::Ok) return -1;
    for (UINT index = 0; index < count; ++index) {
        if (std::wstring(encoders[index].MimeType) == L"image/png") {
            *encoder = encoders[index].Clsid;
            return static_cast<int>(index);
        }
    }
    return -1;
}

bool SavePng(const fs::path& path, const overlay::OfflineRasterImage& image) {
    if (image.width <= 0 || image.height <= 0 ||
        image.bgraPixels.size() !=
            static_cast<std::size_t>(image.width * image.height * 4)) return false;
    CLSID encoder{};
    if (FindPngEncoder(&encoder) < 0) return false;
    Gdiplus::Bitmap bitmap(
        image.width, image.height, image.width * 4, PixelFormat32bppARGB,
        const_cast<BYTE*>(image.bgraPixels.data()));
    return bitmap.Save(path.wstring().c_str(), &encoder, nullptr) == Gdiplus::Ok;
}

overlay::OfflineRasterImage Canvas(int width, int height) {
    overlay::OfflineRasterImage image;
    image.width = std::max(1, width);
    image.height = std::max(1, height);
    image.bgraPixels.resize(static_cast<std::size_t>(image.width * image.height * 4));
    for (std::size_t index = 0; index < image.bgraPixels.size(); index += 4) {
        image.bgraPixels[index] = 44;
        image.bgraPixels[index + 1] = 35;
        image.bgraPixels[index + 2] = 27;
        image.bgraPixels[index + 3] = 255;
    }
    return image;
}

overlay::OfflineRasterImage ScaleNearest(
    const overlay::OfflineRasterImage& source,
    int width,
    int height) {
    auto result = Canvas(width, height);
    std::fill(result.bgraPixels.begin(), result.bgraPixels.end(), 0);
    for (int y = 0; y < result.height; ++y) {
        const int sourceY = std::min(source.height - 1,
            static_cast<int>((static_cast<long long>(y) * source.height) / result.height));
        for (int x = 0; x < result.width; ++x) {
            const int sourceX = std::min(source.width - 1,
                static_cast<int>((static_cast<long long>(x) * source.width) / result.width));
            const auto from = static_cast<std::size_t>((sourceY * source.width + sourceX) * 4);
            const auto to = static_cast<std::size_t>((y * result.width + x) * 4);
            std::copy_n(source.bgraPixels.data() + from, 4,
                        result.bgraPixels.data() + to);
        }
    }
    return result;
}

void Blit(
    overlay::OfflineRasterImage* target,
    const overlay::OfflineRasterImage& source,
    int left,
    int top) {
    if (!target) return;
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            const int tx = left + x;
            const int ty = top + y;
            if (tx < 0 || ty < 0 || tx >= target->width || ty >= target->height) continue;
            const auto from = static_cast<std::size_t>((y * source.width + x) * 4);
            const auto to = static_cast<std::size_t>((ty * target->width + tx) * 4);
            const unsigned alpha = source.bgraPixels[from + 3];
            for (int channel = 0; channel < 3; ++channel) {
                target->bgraPixels[to + channel] = static_cast<unsigned char>((
                    source.bgraPixels[from + channel] * alpha +
                    target->bgraPixels[to + channel] * (255 - alpha) + 127) / 255);
            }
            target->bgraPixels[to + 3] = 255;
        }
    }
}

brain::OverlayViewModel MainCard() {
    brain::OverlayViewModel view;
    view.mode = brain::OverlayMode::Active;
    view.visible = true;
    view.title = "Enroute";
    view.headerRightText = "N123XV";
    view.version.text = "V2 STEP 4";
    view.version.tone = brain::OverlayVersionTone::Current;
    view.radioState.valid = true;
    view.radioState.com1Powered = true;
    view.radioState.com2Powered = true;
    view.radioState.modeCActive = true;
    view.radioState.com1ActiveFrequency = "127.650";
    view.radioState.com2ActiveFrequency = "134.790";
    view.bodyLines = {
        {"CONNECTED N123XV", brain::OverlayTone::Active},
        {"LAX_CTR 127.650", brain::OverlayTone::Active},
        {"Controller workflow remains unchanged", brain::OverlayTone::Normal},
        {"Brain-approved METAR accessory", brain::OverlayTone::Next},
    };
    return view;
}

brain::BrainMetarParsedObservation Parse(const std::string& station, const std::string& raw) {
    return brain::ParseBrainOwnedMetarReport(station, raw, 1'787'860'800);
}

void AddHistory(
    brain::BrainOwnedRuntimeState* state,
    const std::string& key,
    const std::string& title,
    const std::string& raw,
    std::int64_t chronology) {
    brain::BrainOwnedAccessoryHistoryEntryInput entry;
    entry.drawer = brain::BrainOwnedAccessoryDrawerId::Metar;
    entry.stableKey = key;
    entry.title = title;
    entry.body = raw;
    entry.chronological = true;
    entry.chronologyKey = chronology;
    (void)brain::AcceptBrainOwnedAccessoryHistoryEntry(state, entry);
}

brain::BrainOwnedRuntimeState BuildState(const std::string& variant) {
    brain::BrainOwnedRuntimeState state;
    brain::EnableBrainOwnedAccessoryRuntime(&state);
    state.metar.initialized = true;
    const bool ksan = variant == "ksan-ifr";
    const bool pending = variant == "pending" || variant == "atis-pending" ||
        variant == "pdc-pending";
    state.metar.primaryAirportIcao = variant == "unknown" ? "" :
        (ksan ? "KSAN" : "KDFW");
    state.metar.visibleState = variant == "unknown"
        ? brain::BrainMetarVisibleState::Unavailable
        : pending
            ? brain::BrainMetarVisibleState::Pending
            : variant == "unavailable"
                ? brain::BrainMetarVisibleState::Unavailable
            : brain::BrainMetarVisibleState::Fresh;
    state.metar.sourceHealth = pending
        ? brain::BrainMetarSourceHealth::Pending
        : variant == "unavailable"
            ? brain::BrainMetarSourceHealth::Failed
            : brain::BrainMetarSourceHealth::Healthy;
    std::string raw = "KDFW 271952Z 18010KT 10SM FEW050";
    if (variant == "mvfr") raw = "KDFW 271952Z 18010KT 4SM BKN020";
    if (variant == "ifr") raw = "KDFW 271952Z 18010KT 2SM BKN008";
    if (ksan) raw = "KSAN 271952Z 24009KT 2SM BKN008";
    if (variant == "lifr" || variant == "preempted") {
        raw = "SPECI KDFW 271954Z 18010KT M1/4SM VV003";
    }
    if (variant != "unknown" && !pending &&
        variant != "unavailable") {
        state.metar.primaryObservation = Parse(ksan ? "KSAN" : "KDFW", raw);
        state.metar.primaryContentFingerprint = brain::FingerprintBrainMetarContent(raw);
        state.metar.visibleFetchAgeMinutes = 2;
        if (variant == "stale") state.metar.visibleState = brain::BrainMetarVisibleState::Stale;
        AddHistory(&state, std::string("METAR|") + (ksan ? "KSAN|" : "KDFW|") +
            std::to_string(state.metar.primaryObservation.observationUnixSeconds),
            std::string(ksan ? "KSAN" : "KDFW") +
                " · AUTOMATIC TARGET · 1952Z", raw,
            state.metar.primaryObservation.observationUnixSeconds);
    }
    if (variant == "lookup-pending" ||
        variant == "lookup-spotlight" || variant == "lookup-failure" ||
        variant == "preempted") {
        AddHistory(&state, "METAR|KSAN|1787852820",
                   "KSAN · AUTOMATIC TARGET · 1747Z",
                   "KSAN 271747Z 24009KT 10SM FEW050", 1'787'852'820);
        AddHistory(&state, "METAR|KABQ|1787860380",
                   "KABQ · PILOT REQUEST · 1953Z",
                   "KABQ 271953Z 18012KT 4SM BKN020", 1'787'860'380);
    }
    if (variant == "lookup-pending") {
        state.metar.pendingLookupIcao = "KABQ";
        state.metar.transientPresentation =
            brain::BrainMetarTransientPresentation::LookupPending;
    } else if (variant == "lookup-spotlight") {
        state.metar.pendingLookupIcao = "KABQ";
        state.metar.lookupObservation = Parse(
            "KABQ", "KABQ 271953Z 18012KT 4SM BKN020");
        state.metar.transientPresentation =
            brain::BrainMetarTransientPresentation::LookupSpotlight;
    } else if (variant == "lookup-failure") {
        state.metar.pendingLookupIcao = "KABQ";
        state.metar.transientPresentation =
            brain::BrainMetarTransientPresentation::LookupFailure;
    }
    state.metar.presentationGeneration = 1;
    brain::BrainOwnedAccessorySelectionRequest request;
    request.drawer = (variant == "atis-ownership" || variant == "atis-pending")
        ? brain::BrainOwnedAccessoryDrawerId::Atis
        : (variant == "pdc-ownership" || variant == "pdc-pending")
            ? brain::BrainOwnedAccessoryDrawerId::Pdc
            : brain::BrainOwnedAccessoryDrawerId::Metar;
    request.requestSequence = 1;
    (void)brain::RequestBrainOwnedAccessoryDrawerSelection(&state, request);
    if (variant == "metar-closed") {
        request.requestSequence = 2;
        (void)brain::RequestBrainOwnedAccessoryDrawerSelection(&state, request);
    }
    return state;
}

void CloseActiveDrawer(brain::BrainOwnedRuntimeState* state) {
    if (state == nullptr || state->accessory.activeDrawer ==
            brain::BrainOwnedAccessoryDrawerId::None) {
        return;
    }
    brain::BrainOwnedAccessorySelectionRequest close;
    close.drawer = state->accessory.activeDrawer;
    close.requestSequence = state->accessory.lastConsumedClickSequence + 1;
    (void)brain::RequestBrainOwnedAccessoryDrawerSelection(state, close);
}

const brain::BrainOwnedAccessoryOrbPresentation* MetarOrb(
    const brain::BrainOwnedAccessoryPresentationHandle& presentation) {
    if (!presentation.snapshot) return nullptr;
    for (const auto& orb : presentation.snapshot->orbs) {
        if (orb.drawer == brain::BrainOwnedAccessoryDrawerId::Metar) {
            return &orb;
        }
    }
    return nullptr;
}

bool ValidateExactOrbStrings(
    brain::BrainOwnedRuntimeState* state,
    const std::string& variant,
    std::uint64_t generation) {
    const auto presentation = brain::ProjectBrainOwnedAccessoryPresentation(
        state, generation, nullptr);
    const auto* orb = MetarOrb(presentation);
    if (orb == nullptr) return false;
    const bool neutral = variant == "unknown" || variant == "pending" ||
        variant == "unavailable" || variant == "stale";
    if (neutral) {
        return orb->label == "METAR" && orb->airportIcao.empty() &&
            orb->categoryText.empty() && orb->stateText.empty() &&
            orb->selectedIndicator.empty() && orb->neutral &&
            orb->tone ==
                brain::BrainOwnedAccessoryOrbPresentation::Tone::Gray;
    }
    std::string expectedCategory = "VFR";
    auto expectedTone = brain::BrainOwnedAccessoryOrbPresentation::Tone::Green;
    if (variant == "mvfr") {
        expectedCategory = "MVFR";
        expectedTone = brain::BrainOwnedAccessoryOrbPresentation::Tone::Blue;
    } else if (variant == "ifr") {
        expectedCategory = "IFR";
        expectedTone = brain::BrainOwnedAccessoryOrbPresentation::Tone::Red;
    } else if (variant == "lifr" || variant == "preempted") {
        expectedCategory = "LIFR";
        expectedTone = brain::BrainOwnedAccessoryOrbPresentation::Tone::Magenta;
    }
    const auto expectedAirport = variant == "ksan-ifr" ? "KSAN" : "KDFW";
    if (variant == "ksan-ifr") {
        expectedCategory = "IFR";
        expectedTone = brain::BrainOwnedAccessoryOrbPresentation::Tone::Red;
    }
    return orb->label.empty() && orb->airportIcao == expectedAirport &&
        orb->categoryText == expectedCategory && orb->stateText.empty() &&
        orb->selectedIndicator.empty() && !orb->neutral &&
        orb->tone == expectedTone;
}

struct RenderResult {
    overlay::OfflineRasterImage composite;
    std::uint64_t projectionUs = 0;
    std::uint64_t wrapUs = 0;
    std::uint64_t updateUs = 0;
    std::uint64_t rasterUs = 0;
    std::size_t mainCardSignature = 0;
};

RenderResult Render(
    brain::BrainOwnedRuntimeState* state,
    overlay::AccessoryTextMeasurementContext* measurement,
    std::uint64_t generation) {
    RenderResult result;
    const auto mainCard = MainCard();
    result.mainCardSignature =
        overlay::BuildProductionMainCardSignatureForOfflineProof(mainCard, 0);
    const auto typography = overlay::PrepareAccessoryTypography(measurement, 1.0f);
    overlay::AccessoryLayoutInput layoutInput;
    layoutInput.screenWidth = 1920;
    layoutInput.screenHeight = 1080;
    layoutInput.windowLeft = 120;
    layoutInput.windowTop = 940;
    layoutInput.scale = 1.0f;
    layoutInput.cardAnimationProgress = 1.0f;
    layoutInput.drawerOpen = false;
    layoutInput.typography = &typography;
    const auto layout = overlay::ResolveAccessoryLayout(layoutInput);

    auto started = std::chrono::steady_clock::now();
    const auto presentation = brain::ProjectBrainOwnedAccessoryPresentation(
        state, generation, nullptr);
    result.projectionUs = ElapsedUs(started);
    if (!presentation.snapshot) throw std::runtime_error("presentation unavailable");

    layoutInput.drawerOpen = presentation.snapshot->activeDrawer !=
        brain::BrainOwnedAccessoryDrawerId::None;
    const auto resolvedLayout = overlay::ResolveAccessoryLayout(layoutInput);

    auto prepared = std::make_shared<overlay::AccessoryPreparedDrawerPlan>();
    prepared->key.drawer = presentation.snapshot->activeDrawer;
    prepared->key.historyGeneration = presentation.historyGeneration;
    prepared->key.contentGeneration = presentation.contentGeneration;
    prepared->key.layoutGeneration = generation;
    prepared->key.commandIdentity = presentation.commandIdentity;
    prepared->key.lifecycleEpoch = presentation.lifecycleEpoch;
    prepared->key.drawerContentRevision = presentation.drawerContentRevision;
    prepared->key.typographyGeneration = typography.generation;
    prepared->key.scaleThousandths = 1000;
    prepared->key.contentWidth = std::max(
        1, resolvedLayout.drawerBounds.right - resolvedLayout.drawerBounds.left -
            2 * resolvedLayout.drawerContentInset);
    prepared->key.visibleLineCapacity = resolvedLayout.drawerVisibleLineCapacity;
    started = std::chrono::steady_clock::now();
    prepared->layout = overlay::BuildAccessoryHistoryLayout(
        measurement, *presentation.snapshot, resolvedLayout);
    result.wrapUs = ElapsedUs(started);

    overlay::AccessoryPresentationState presenter;
    overlay::AccessoryPresentationUpdateInput update;
    update.presentation = presentation;
    update.layout = resolvedLayout;
    update.mainCardProductionSignature = std::to_string(result.mainCardSignature);
    update.measurementContext = measurement;
    update.preparedPlan = prepared;
    started = std::chrono::steady_clock::now();
    (void)overlay::UpdateAccessoryPresentation(&presenter, update);
    result.updateUs = ElapsedUs(started);

    started = std::chrono::steady_clock::now();
    const auto card = overlay::RenderProductionMainCardForOfflineProof(mainCard, 0);
    const auto rail = overlay::RenderProductionAccessoryRailForOfflineProof(
        resolvedLayout, *presentation.snapshot);
    const auto drawer = overlay::RenderProductionAccessoryDrawerForOfflineProof(
        resolvedLayout, presenter);
    result.rasterUs = ElapsedUs(started);

    const auto& bounds = resolvedLayout.resolvedBounds;
    result.composite = Canvas(bounds.right - bounds.left, bounds.top - bounds.bottom);
    const auto scaledCard = ScaleNearest(
        card,
        resolvedLayout.mainCardBounds.right - resolvedLayout.mainCardBounds.left,
        resolvedLayout.mainCardBounds.top - resolvedLayout.mainCardBounds.bottom);
    Blit(&result.composite, scaledCard,
         resolvedLayout.mainCardBounds.left - bounds.left,
         bounds.top - resolvedLayout.mainCardBounds.top);
    Blit(&result.composite, rail,
         resolvedLayout.railBounds.left - bounds.left,
         bounds.top - resolvedLayout.railBounds.top);
    Blit(&result.composite, drawer,
         resolvedLayout.drawerBounds.left - bounds.left,
         bounds.top - resolvedLayout.drawerBounds.top);
    return result;
}

overlay::OfflineRasterImage RenderMinimalOrbStateMatrix(
    overlay::AccessoryTextMeasurementContext* measurement,
    std::uint64_t* generation,
    bool* exactStringsValid) {
    const std::string variants[]{"unknown", "vfr", "mvfr", "ifr", "lifr"};
    std::vector<overlay::OfflineRasterImage> rails;
    const auto typography = overlay::PrepareAccessoryTypography(measurement, 1.0f);
    for (const auto& variant : variants) {
        auto state = BuildState(variant);
        const auto currentGeneration = (*generation)++;
        *exactStringsValid = *exactStringsValid &&
            ValidateExactOrbStrings(&state, variant, currentGeneration);
        overlay::AccessoryLayoutInput layoutInput;
        layoutInput.screenWidth = 1920;
        layoutInput.screenHeight = 1080;
        layoutInput.windowLeft = 120;
        layoutInput.windowTop = 940;
        layoutInput.scale = 1.0f;
        layoutInput.cardAnimationProgress = 1.0f;
        layoutInput.drawerOpen = false;
        layoutInput.typography = &typography;
        const auto layout = overlay::ResolveAccessoryLayout(layoutInput);
        const auto presentation = brain::ProjectBrainOwnedAccessoryPresentation(
            &state, currentGeneration, nullptr);
        rails.push_back(overlay::RenderProductionAccessoryRailForOfflineProof(
            layout, *presentation.snapshot));
    }
    const int gap = 12;
    int width = gap;
    int height = 0;
    for (const auto& rail : rails) {
        width += rail.width + gap;
        height = std::max(height, rail.height + 2 * gap);
    }
    auto matrix = Canvas(width, height);
    int left = gap;
    for (const auto& rail : rails) {
        Blit(&matrix, rail, left, gap);
        left += rail.width + gap;
    }
    return matrix;
}

overlay::OfflineRasterImage RenderTwoLineOrbScaleMatrix(
    overlay::AccessoryTextMeasurementContext* measurement,
    std::uint64_t* generation,
    bool* exactStringsValid) {
    const float scales[]{0.85f, 1.0f, 1.35f};
    std::vector<overlay::OfflineRasterImage> rails;
    for (const auto scale : scales) {
        auto state = BuildState("vfr");
        const auto currentGeneration = (*generation)++;
        *exactStringsValid = *exactStringsValid &&
            ValidateExactOrbStrings(&state, "vfr", currentGeneration);
        const auto typography = overlay::PrepareAccessoryTypography(
            measurement, scale);
        overlay::AccessoryLayoutInput input;
        input.screenWidth = 1920;
        input.screenHeight = 1080;
        input.windowLeft = 120;
        input.windowTop = 940;
        input.scale = scale;
        input.cardAnimationProgress = 1.0f;
        input.drawerOpen = false;
        input.typography = &typography;
        const auto layout = overlay::ResolveAccessoryLayout(input);
        const auto presentation = brain::ProjectBrainOwnedAccessoryPresentation(
            &state, currentGeneration, nullptr);
        rails.push_back(overlay::RenderProductionAccessoryRailForOfflineProof(
            layout, *presentation.snapshot));
    }
    constexpr int gap = 12;
    int width = gap;
    int height = 0;
    for (const auto& rail : rails) {
        width += rail.width + gap;
        height = std::max(height, rail.height + 2 * gap);
    }
    auto matrix = Canvas(width, height);
    int left = gap;
    for (const auto& rail : rails) {
        Blit(&matrix, rail, left, gap);
        left += rail.width + gap;
    }
    return matrix;
}

std::shared_ptr<const overlay::AccessoryPreparedDrawerPlan> PreparePlan(
    overlay::AccessoryTextMeasurementContext* measurement,
    const brain::BrainOwnedAccessoryPresentationHandle& presentation,
    const overlay::AccessoryLayoutResult& layout) {
    if (!presentation.snapshot || presentation.snapshot->activeDrawer ==
            brain::BrainOwnedAccessoryDrawerId::None) {
        return {};
    }
    auto plan = std::make_shared<overlay::AccessoryPreparedDrawerPlan>();
    plan->key.drawer = presentation.snapshot->activeDrawer;
    plan->key.historyGeneration = presentation.historyGeneration;
    plan->key.contentGeneration = presentation.contentGeneration;
    plan->key.layoutGeneration = presentation.layoutGeneration;
    plan->key.commandIdentity = presentation.commandIdentity;
    plan->key.lifecycleEpoch = presentation.lifecycleEpoch;
    plan->key.drawerContentRevision = presentation.drawerContentRevision;
    plan->key.scaleThousandths = 1000;
    plan->key.contentWidth = std::max(
        1, layout.drawerBounds.right - layout.drawerBounds.left -
            (2 * layout.drawerContentInset));
    plan->key.visibleLineCapacity = layout.drawerVisibleLineCapacity;
    plan->layout = overlay::BuildAccessoryHistoryLayout(
        measurement, *presentation.snapshot, layout);
    return plan;
}

bool ValidatePublicationTransition(
    overlay::AccessoryTextMeasurementContext* measurement,
    const std::string& name,
    const std::string& beforeVariant,
    const std::string& afterVariant,
    bool drawerOpen,
    std::uint64_t expectedRail,
    std::uint64_t expectedDrawer,
    std::ostringstream* report,
    std::uint64_t* generation) {
    auto beforeState = BuildState(beforeVariant);
    auto afterState = BuildState(afterVariant);
    if (!drawerOpen) {
        for (auto* state : {&beforeState, &afterState}) {
            if (state->accessory.activeDrawer !=
                    brain::BrainOwnedAccessoryDrawerId::None) {
                brain::BrainOwnedAccessorySelectionRequest close;
                close.drawer = state->accessory.activeDrawer;
                close.requestSequence = 2;
                (void)brain::RequestBrainOwnedAccessoryDrawerSelection(
                    state, close);
            }
        }
    }
    overlay::AccessoryLayoutInput input;
    input.screenWidth = 1920;
    input.screenHeight = 1080;
    input.windowLeft = 120;
    input.windowTop = 940;
    input.scale = 1.0f;
    input.cardAnimationProgress = 1.0f;
    input.drawerOpen = drawerOpen;
    const auto typography = overlay::PrepareAccessoryTypography(measurement, 1.0f);
    input.typography = &typography;
    const auto layout = overlay::ResolveAccessoryLayout(input);
    auto before = brain::ProjectBrainOwnedAccessoryPresentation(
        &beforeState, (*generation)++, nullptr);
    auto after = brain::ProjectBrainOwnedAccessoryPresentation(
        &afterState, (*generation)++, nullptr);
    auto authoritativeAfter = std::make_shared<
        brain::BrainOwnedAccessoryPresentationSnapshot>(*after.snapshot);
    authoritativeAfter->commandIdentity = before.commandIdentity + 1;
    authoritativeAfter->lifecycleEpoch = before.lifecycleEpoch;
    authoritativeAfter->railPresentationRevision =
        before.railPresentationRevision + (expectedRail == 0 ? 0 : 1);
    authoritativeAfter->drawerContentRevision =
        before.drawerContentRevision + (expectedDrawer == 0 ? 0 : 1);
    after.snapshot = std::move(authoritativeAfter);
    after.commandIdentity = after.snapshot->commandIdentity;
    after.lifecycleEpoch = after.snapshot->lifecycleEpoch;
    after.railPresentationRevision =
        after.snapshot->railPresentationRevision;
    after.drawerContentRevision = after.snapshot->drawerContentRevision;
    after.contentGeneration = before.contentGeneration + 1;
    after.historyGeneration = before.historyGeneration + 1;
    after.layoutGeneration = before.layoutGeneration;
    overlay::AccessoryPresentationState presenter;
    overlay::AccessoryPresentationUpdateInput update;
    update.layout = layout;
    update.mainCardProductionSignature =
        std::to_string(kAcceptedMainCardSignature);
    update.measurementContext = measurement;
    update.presentation = before;
    update.preparedPlan = PreparePlan(measurement, before, layout);
    (void)overlay::UpdateAccessoryPresentation(&presenter, update);
    update.presentation = after;
    update.preparedPlan = PreparePlan(measurement, after, layout);
    const auto result = overlay::UpdateAccessoryPresentation(&presenter, update);
    const auto expectedUpload = expectedRail + expectedDrawer;
    if (report != nullptr) {
        *report << name << ',' << result.delta.railRasterRequests << ','
                << result.delta.uploadRequests << ','
                << result.delta.drawerRasterRequests << ','
                << (result.railAppearanceChanged ? "true" : "false") << '\n';
    }
    return result.delta.railRasterRequests == expectedRail &&
        result.delta.drawerRasterRequests == expectedDrawer &&
        result.delta.uploadRequests == expectedUpload;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: XVatsimStep4MetarVisualProof <output-directory>\n";
        return 2;
    }
    const fs::path outputDirectory = argv[1];
    fs::create_directories(outputDirectory);
    Gdiplus::GdiplusStartupInput startupInput;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &startupInput, nullptr) != Gdiplus::Ok) {
        std::cerr << "GDI+ startup failed\n";
        return 1;
    }
    auto* measurement = overlay::InitializeAccessoryTextMeasurement();
    if (!measurement) {
        Gdiplus::GdiplusShutdown(token);
        std::cerr << "text measurement startup failed\n";
        return 1;
    }

    std::ostringstream performance;
    performance << "file,projection_us,wrapping_us,presentation_us,raster_us,main_card_signature\n";
    std::size_t baselineMainCardSignature = 0;
    bool failed = false;
    bool exactStringsValid = true;
    std::uint64_t generation = 1;
    for (const auto& visual : kVisuals) {
        auto state = BuildState(visual.variant);
        const auto proofNumber = std::stoi(visual.filename.substr(0, 2));
        if (proofNumber == 23 || proofNumber == 24 || proofNumber == 25 ||
            proofNumber == 26 || proofNumber == 27 || proofNumber == 31 ||
            proofNumber == 32 || proofNumber == 33) {
            CloseActiveDrawer(&state);
        }
        const auto currentGeneration = generation++;
        exactStringsValid = exactStringsValid &&
            ValidateExactOrbStrings(
                &state, visual.variant, currentGeneration);
        const auto rendered = Render(&state, measurement, currentGeneration);
        if (baselineMainCardSignature == 0) {
            baselineMainCardSignature = rendered.mainCardSignature;
        }
        if (rendered.mainCardSignature != baselineMainCardSignature ||
            rendered.mainCardSignature != kAcceptedMainCardSignature ||
            !SavePng(outputDirectory / visual.filename, rendered.composite)) {
            failed = true;
        }
        performance << visual.filename << ',' << rendered.projectionUs << ','
                    << rendered.wrapUs << ',' << rendered.updateUs << ','
                    << rendered.rasterUs << ',' << rendered.mainCardSignature << '\n';
    }
    const auto matrix = RenderMinimalOrbStateMatrix(
        measurement, &generation, &exactStringsValid);
    if (!SavePng(
            outputDirectory / "15_minimal_orb_state_matrix.png", matrix)) {
        failed = true;
    }
    performance << "15_minimal_orb_state_matrix.png,0,0,0,0,"
                << baselineMainCardSignature << '\n';
    const auto scaleMatrix = RenderTwoLineOrbScaleMatrix(
        measurement, &generation, &exactStringsValid);
    if (!SavePng(
            outputDirectory / "16_two_line_orb_scale_matrix.png",
            scaleMatrix)) {
        failed = true;
    }
    performance << "16_two_line_orb_scale_matrix.png,0,0,0,0,"
                << baselineMainCardSignature << '\n';
    std::ostringstream publication;
    publication << "transition,rail_rasters,uploads,drawer_rasters,rail_appearance_changed\n";
    failed = !ValidatePublicationTransition(
        measurement, "neutral_to_kdfw_vfr_closed", "unknown", "vfr", false,
        1, 0, &publication, &generation) || failed;
    failed = !ValidatePublicationTransition(
        measurement, "kdfw_vfr_to_mvfr_closed", "vfr", "mvfr", false,
        1, 0, &publication, &generation) || failed;
    failed = !ValidatePublicationTransition(
        measurement, "kdfw_to_ksan_closed", "vfr", "ksan-ifr", false,
        1, 0, &publication, &generation) || failed;
    failed = !ValidatePublicationTransition(
        measurement, "success_to_neutral_closed", "vfr", "unavailable", false,
        1, 0, &publication, &generation) || failed;
    failed = !ValidatePublicationTransition(
        measurement, "metar_open_primary_change", "pending", "vfr", true,
        1, 1, &publication, &generation) || failed;
    failed = !ValidatePublicationTransition(
        measurement, "atis_owns_primary_change", "atis-pending", "atis-ownership", true,
        1, 1, &publication, &generation) || failed;
    failed = !ValidatePublicationTransition(
        measurement, "pdc_owns_primary_change", "pdc-pending", "pdc-ownership", true,
        1, 1, &publication, &generation) || failed;
    failed = !ValidatePublicationTransition(
        measurement, "lookup_spotlight_unchanged_primary", "vfr", "lookup-spotlight", true,
        0, 1, &publication, &generation) || failed;
    failed = !ValidatePublicationTransition(
        measurement, "identical_primary_content_only", "vfr", "vfr", false,
        0, 0, &publication, &generation) || failed;
    failed = failed || !exactStringsValid;
    overlay::ShutdownAccessoryTextMeasurement(measurement);
    Gdiplus::GdiplusShutdown(token);

    {
        std::ofstream output(outputDirectory / "performance.csv", std::ios::binary);
        output << performance.str();
    }
    {
        std::ofstream output(
            outputDirectory / "publication_counts.csv", std::ios::binary);
        output << publication.str();
    }
    std::ostringstream sums;
    for (const auto& visual : kVisuals) {
        sums << Sha256(outputDirectory / visual.filename) << "  "
             << visual.filename << '\n';
    }
    sums << Sha256(outputDirectory / "15_minimal_orb_state_matrix.png")
         << "  15_minimal_orb_state_matrix.png\n";
    sums << Sha256(outputDirectory / "16_two_line_orb_scale_matrix.png")
         << "  16_two_line_orb_scale_matrix.png\n";
    sums << Sha256(outputDirectory / "performance.csv")
         << "  performance.csv\n";
    sums << Sha256(outputDirectory / "publication_counts.csv")
         << "  publication_counts.csv\n";
    {
        std::ofstream output(outputDirectory / "SHA256SUMS.txt", std::ios::binary);
        output << sums.str();
    }
    if (failed) {
        std::cerr << "Step 4 visual proof failed\n";
        return 1;
    }
    std::cout << "Step 4 ORB-publication visual proof wrote 33 deterministic images"
              << " with exact ORB strings, transition counts, and unchanged main-card signature\n";
    return 0;
}
