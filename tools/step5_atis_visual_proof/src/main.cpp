#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>
#include <objidl.h>
#include <bcrypt.h>
#include <gdiplus.h>

#include "XVatsim/brain/BrainOwnedRuntime.h"
#include "XVatsim/modules/overlay/OverlayAccessoryCore.h"
#include "XVatsim/modules/overlay/OverlayVisualProof.h"
#include "XVatsim/modules/vatsim_data_feed/VatsimDataFeedClient.h"

namespace fs = std::filesystem;
namespace brain = xvatsim::brain;
namespace overlay = xvatsim::modules::overlay;
namespace feed = xvatsim::modules::vatsim_data_feed;

namespace {

struct VisualSpec {
    const char* filename;
    const char* variant;
    const char* expectedToken;
};

constexpr VisualSpec kVisuals[]{
    {"01_idle_no_endpoint.png", "idle-no-endpoint", "ATIS IDLE"},
    {"02_confirmed_unavailable.png", "confirmed-unavailable", "NO APPLICABLE ATIS IN CURRENT VATSIM FEED"},
    {"03_source_unknown_missing.png", "source-unknown-missing", "VATSIM ATIS SOURCE STATUS UNKNOWN"},
    {"04_source_unknown_stale.png", "source-unknown-stale", "VATSIM ATIS SOURCE STATUS UNKNOWN"},
    {"05_departure_unread_info_a.png", "departure-unread-a", "DEPARTURE INFORMATION ALPHA"},
    {"06_departure_read_info_a.png", "departure-read-a", "DEPARTURE INFORMATION ALPHA"},
    {"07_departure_unread_no_code.png", "departure-unread-no-code", "DEPARTURE WITHOUT CODE"},
    {"08_departure_read_no_code.png", "departure-read-no-code", "DEPARTURE WITHOUT CODE"},
    {"09_combined_departure_fallback.png", "combined-departure", "COMBINED INFORMATION CHARLIE"},
    {"10_enroute_arrival_unread.png", "enroute-arrival-unread", "ARRIVAL INFORMATION BRAVO"},
    {"11_enroute_arrival_read.png", "enroute-arrival-read", "ARRIVAL INFORMATION BRAVO"},
    {"12_vfr_departure_endpoint.png", "vfr-departure", "VFR DEPARTURE INFORMATION DELTA"},
    {"13_ifr_departure_endpoint.png", "ifr-departure", "IFR DEPARTURE INFORMATION ECHO"},
    {"14_closed_unread_rail.png", "closed-unread", ""},
    {"15_closed_read_rail.png", "closed-read", ""},
    {"16_atis_selected_unread.png", "atis-selected-unread", "SELECTED UNREAD INFORMATION"},
    {"17_atis_selected_read.png", "atis-selected-read", "SELECTED READ INFORMATION"},
    {"18_metar_selected_atis_unread.png", "metar-selected", "METAR"},
    {"19_pdc_selected_atis_unread.png", "pdc-selected", "PDC"},
    {"20_lookup_pending.png", "lookup-pending", "WAITING FOR CURRENT VATSIM FEED"},
    {"21_lookup_combined.png", "lookup-combined", "KABQ COMBINED LOOKUP"},
    {"22_lookup_split.png", "lookup-split", "ARRIVAL"},
    {"23_lookup_one_sided_departure.png", "lookup-departure-only", "KABQ DEPARTURE ONLY"},
    {"24_lookup_one_sided_arrival.png", "lookup-arrival-only", "KABQ ARRIVAL ONLY"},
    {"25_lookup_unavailable.png", "lookup-unavailable", "NO APPLICABLE ATIS IN CURRENT VATSIM FEED"},
    {"26_lookup_source_unknown.png", "lookup-source-unknown", "VATSIM ATIS SOURCE STATUS UNKNOWN"},
    {"27_lookup_expired_primary.png", "lookup-expired", "KDFW PRIMARY AFTER LOOKUP"},
    {"28_lookup_lost_ownership_pdc.png", "lookup-lost-pdc", "PDC"},
    {"29_open_update_new_code.png", "open-update", "UPDATED INFORMATION JULIET"},
    {"30_fresh_recovery.png", "fresh-recovery", "RECOVERED INFORMATION KILO"},
    {"31_suspend_resume_retained.png", "suspend-retained", "VATSIM ATIS SOURCE STATUS UNKNOWN"},
    {"32_disconnect_reconnect_unknown.png", "disconnect-unknown", "VATSIM ATIS SOURCE STATUS UNKNOWN"},
    {"33_history_two_revisions.png", "history-two", "HISTORY REVISION TWO"},
    {"34_history_five_revisions.png", "history-five", "HISTORY REVISION FIVE"},
    {"35_long_text_top.png", "long-top", "LONG ATIS HEADER"},
    {"36_long_text_scrolled.png", "long-scrolled", "LONG BODY SEGMENT"},
    {"37_wrapping_scale_075.png", "scale-075", "SCALE ZERO SEVEN FIVE"},
    {"38_normal_scale_100.png", "scale-100", "SCALE ONE ZERO ZERO"},
    {"39_large_scale_125.png", "scale-125", "SCALE ONE TWO FIVE"},
    {"40_xlarge_scale_150.png", "scale-150", "SCALE ONE FIVE ZERO"},
    {"41_empty_frequency.png", "empty-frequency", "EMPTY FREQUENCY"},
    {"42_empty_information_code.png", "empty-code", "EMPTY INFORMATION CODE"},
    {"43_malformed_nonapplicable.png", "malformed", "NO APPLICABLE ATIS IN CURRENT VATSIM FEED"},
    {"44_controller_text_isolation.png", "controller-isolation", "NO APPLICABLE ATIS IN CURRENT VATSIM FEED"},
    {"45_duplicate_newest_selected.png", "duplicate-newest", "NEWEST DUPLICATE"},
    {"46_equal_time_lexical_selected.png", "equal-time", "EQUAL TIME"},
    {"47_enroute_combined_fallback.png", "enroute-combined", "ENROUTE COMBINED"},
    {"48_manual_only_no_plan_lookup.png", "manual-only", "KABQ MANUAL ONLY"},
};

static_assert(std::size(kVisuals) == 48, "Step 5 visual case count changed");

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
    if (Gdiplus::GetImageEncoders(count, bytes, encoders) != Gdiplus::Ok) {
        return -1;
    }
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
            static_cast<std::size_t>(image.width * image.height * 4)) {
        return false;
    }
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
    image.bgraPixels.resize(
        static_cast<std::size_t>(image.width * image.height * 4));
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
        const int sourceY = std::min(
            source.height - 1,
            static_cast<int>((static_cast<long long>(y) * source.height) /
                             result.height));
        for (int x = 0; x < result.width; ++x) {
            const int sourceX = std::min(
                source.width - 1,
                static_cast<int>((static_cast<long long>(x) * source.width) /
                                 result.width));
            const auto from = static_cast<std::size_t>(
                (sourceY * source.width + sourceX) * 4);
            const auto to = static_cast<std::size_t>(
                (y * result.width + x) * 4);
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
    if (target == nullptr) return;
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            const int tx = left + x;
            const int ty = top + y;
            if (tx < 0 || ty < 0 || tx >= target->width ||
                ty >= target->height) {
                continue;
            }
            const auto from = static_cast<std::size_t>(
                (y * source.width + x) * 4);
            const auto to = static_cast<std::size_t>(
                (ty * target->width + tx) * 4);
            const unsigned alpha = source.bgraPixels[from + 3];
            for (int channel = 0; channel < 3; ++channel) {
                target->bgraPixels[to + channel] =
                    static_cast<unsigned char>((
                        source.bgraPixels[from + channel] * alpha +
                        target->bgraPixels[to + channel] * (255 - alpha) + 127) /
                        255);
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
    view.headerRightText = "ASA551";
    view.version.text = "V2 STEP 5";
    view.version.tone = brain::OverlayVersionTone::Current;
    view.radioState.valid = true;
    view.radioState.com1Powered = true;
    view.radioState.com2Powered = true;
    view.radioState.modeCActive = true;
    view.radioState.com1ActiveFrequency = "127.650";
    view.radioState.com2ActiveFrequency = "134.790";
    view.bodyLines = {
        {"CONNECTED ASA551", brain::OverlayTone::Active},
        {"LAX_CTR 127.650", brain::OverlayTone::Active},
        {"Brain-owned VATSIM ATIS", brain::OverlayTone::Next},
        {"Single accessory presentation path", brain::OverlayTone::Normal},
    };
    return view;
}

std::string EscapeJson(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const unsigned char character : value) {
        switch (character) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped.push_back(static_cast<char>(character)); break;
        }
    }
    return escaped;
}

std::string Record(
    const std::string& callsign,
    const std::string& frequency,
    const std::string& code,
    const std::vector<std::string>& lines,
    const std::string& updated = "2026-08-30T12:00:00Z") {
    std::ostringstream json;
    json << "{\"callsign\":\"" << EscapeJson(callsign)
         << "\",\"frequency\":\"" << EscapeJson(frequency)
         << "\",\"atis_code\":\"" << EscapeJson(code)
         << "\",\"text_atis\":[";
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index != 0) json << ',';
        json << '"' << EscapeJson(lines[index]) << '"';
    }
    json << "],\"last_updated\":\"" << EscapeJson(updated) << "\"}";
    return json.str();
}

std::string Document(
    const std::vector<std::string>& records,
    const std::string& controllers = "[]") {
    std::ostringstream json;
    json << "{\"controllers\":" << controllers
         << ",\"pilots\":[],\"atis\":[";
    for (std::size_t index = 0; index < records.size(); ++index) {
        if (index != 0) json << ',';
        json << records[index];
    }
    json << "]}";
    return json.str();
}

std::string PrimaryDocument(
    const std::string& callsign,
    const std::string& code,
    const std::string& text,
    const std::string& frequency = "123.775") {
    return Document({Record(callsign, frequency, code, {text})});
}

struct AtisFixture {
    brain::BrainOwnedRuntimeState state;
    feed::VatsimDataFeedSnapshot feedSnapshot;
    brain::BrainOwnedAtisCycleInput input;

    AtisFixture() {
        brain::EnableBrainOwnedAccessoryRuntime(&state);
        input.pluginEnabled = true;
        input.xpilotConnected = true;
        input.workflowStage = brain::WorkflowStage::Departure;
        input.operatingMode = brain::BrainOwnedOperatingMode::IFR;
        input.flightContext.active = true;
        input.flightContext.callsign = "ASA551";
        input.flightContext.departureIcao = "KDFW";
        input.flightContext.destinationIcao = "KSAN";
        input.monotonicMs = 1'000;
    }

    void BindFeed() {
        input.feedHasCache = feedSnapshot.hasCache;
        input.feedStale = feedSnapshot.stale;
        input.feedFetchInProgress = feedSnapshot.fetchInProgress;
        input.feedGeneration = feedSnapshot.generation;
        input.atisRootPresent = feedSnapshot.atisRootPresent;
        input.atisRootArray = feedSnapshot.atisRootArray;
        input.atisComponentComplete = feedSnapshot.atisComponentComplete;
        input.atisMechanicalIssueMask =
            feedSnapshot.atisMechanicalIssueMask;
        input.atisRecords = &feedSnapshot.atisRecords;
    }

    void Decode(const std::string& document, std::uint64_t generation = 1) {
        feedSnapshot = feed::DecodeVatsimDataFeedDocument(document);
        feedSnapshot.generation = generation;
        BindFeed();
    }

    brain::BrainOwnedAtisCycleDecision Cycle(long long advanceMs = 0) {
        input.monotonicMs += advanceMs;
        BindFeed();
        return brain::RunBrainOwnedAtisCycle(&state, input);
    }

    brain::BrainOwnedAccessoryPresentationHandle Project() {
        return brain::ProjectBrainOwnedAccessoryPresentation(&state, nullptr);
    }

    void Select(
        brain::BrainOwnedAccessoryDrawerId drawer,
        std::uint64_t sequence = 1) {
        brain::BrainOwnedAccessorySelectionRequest request;
        request.drawer = drawer;
        request.requestSequence = sequence;
        request.clickAcceptedMicroseconds = sequence * 1'000;
        request.mouseCallbackEnteredMicroseconds =
            request.clickAcceptedMicroseconds;
        request.mouseCallbackExitedMicroseconds =
            request.clickAcceptedMicroseconds + 1;
        const auto decision =
            brain::RequestBrainOwnedAccessoryDrawerSelection(&state, request);
        if (decision.activeDrawer != drawer) {
            throw std::runtime_error("Brain drawer selection was not accepted");
        }
    }

    void CloseActive(std::uint64_t sequence = 99) {
        const auto active = state.accessory.activeDrawer;
        if (active == brain::BrainOwnedAccessoryDrawerId::None) return;
        brain::BrainOwnedAccessorySelectionRequest request;
        request.drawer = active;
        request.requestSequence = sequence;
        request.clickAcceptedMicroseconds = sequence * 1'000;
        request.mouseCallbackEnteredMicroseconds =
            request.clickAcceptedMicroseconds;
        request.mouseCallbackExitedMicroseconds =
            request.clickAcceptedMicroseconds + 1;
        const auto decision =
            brain::RequestBrainOwnedAccessoryDrawerSelection(&state, request);
        if (decision.activeDrawer !=
            brain::BrainOwnedAccessoryDrawerId::None) {
            throw std::runtime_error("Brain drawer close was not accepted");
        }
    }

    void Lookup(const std::string& airport) {
        brain::BrainOwnedTextEntryFact fact;
        fact.mode = brain::BrainOwnedTextEntryMode::AtisAirportLookup;
        fact.text = airport;
        fact.monotonicMs = ++input.monotonicMs;
        const auto decision = brain::CommitBrainOwnedAtisTextEntryFact(
            &state, fact);
        if (!decision.accepted) {
            throw std::runtime_error("Brain ATIS lookup was not accepted");
        }
    }
};

const brain::BrainOwnedAccessoryOrbPresentation* AtisOrb(
    const brain::BrainOwnedAccessoryPresentationHandle& command) {
    if (!command.snapshot) return nullptr;
    for (const auto& orb : command.snapshot->orbs) {
        if (orb.drawer == brain::BrainOwnedAccessoryDrawerId::Atis) {
            return &orb;
        }
    }
    return nullptr;
}

bool SnapshotContains(
    const brain::BrainOwnedAccessoryPresentationHandle& command,
    const std::string& token) {
    if (!command.snapshot) return false;
    if (command.snapshot->drawerTitle.find(token) != std::string::npos ||
        command.snapshot->drawerStateText.find(token) != std::string::npos ||
        command.snapshot->emptyStateText.find(token) != std::string::npos) {
        return true;
    }
    return std::any_of(
        command.snapshot->entries.begin(), command.snapshot->entries.end(),
        [&](const auto& entry) {
            return entry.title.find(token) != std::string::npos ||
                entry.body.find(token) != std::string::npos;
        });
}

struct FrameResult {
    overlay::OfflineRasterImage composite;
    std::uint64_t projectionUs = 0;
    std::uint64_t preparationUs = 0;
    std::uint64_t presentationUs = 0;
    std::uint64_t rasterUs = 0;
    bool brainAccepted = false;
    bool diagnosticSerialized = false;
    std::size_t visibleLines = 0;
    std::size_t totalLines = 0;
    int drawerOffset = 0;
    std::uint64_t commandIdentity = 0;
    std::uint64_t atisRevision = 0;
    std::string visibleText;
};

struct PublicationContext {
    overlay::AccessoryVisiblePublicationState visible;
    overlay::AccessoryPublicationFactQueue queue;
    overlay::AccessoryPublicationDiagnosticAccounting diagnostics;
};

FrameResult RenderFrame(
    brain::BrainOwnedRuntimeState* state,
    const brain::BrainOwnedAccessoryPresentationHandle& command,
    float scale,
    int scrollClicks,
    std::uint64_t generation,
    PublicationContext* publication) {
    if (state == nullptr || command.snapshot == nullptr) {
        throw std::runtime_error("visual command unavailable");
    }
    FrameResult result;
    result.commandIdentity = command.snapshot->commandIdentity;
    result.atisRevision = command.snapshot->selectedDrawerContentRevision;

    auto* measurement = overlay::InitializeAccessoryTextMeasurement();
    if (measurement == nullptr) {
        throw std::runtime_error("text measurement unavailable");
    }
    overlay::AccessoryPreparationWorker worker;
    const bool drawerOpen = command.snapshot->activeDrawer !=
        brain::BrainOwnedAccessoryDrawerId::None;
    if (drawerOpen) {
        if (!worker.Start(GetCurrentThreadId())) {
            overlay::ShutdownAccessoryTextMeasurement(measurement);
            throw std::runtime_error("generic preparation worker did not start");
        }
        const auto workerDeadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(2);
        while (worker.State() ==
                   overlay::AccessoryPreparationWorkerState::Starting &&
               std::chrono::steady_clock::now() < workerDeadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (worker.State() != overlay::AccessoryPreparationWorkerState::Ready) {
            worker.Stop();
            overlay::ShutdownAccessoryTextMeasurement(measurement);
            throw std::runtime_error("generic preparation worker was not ready");
        }
    }

    const auto typography =
        overlay::PrepareAccessoryTypography(measurement, scale);
    overlay::AccessoryLayoutInput layoutInput;
    layoutInput.screenWidth = 1920;
    layoutInput.screenHeight = 1080;
    layoutInput.windowLeft = 120;
    layoutInput.windowTop = 940;
    layoutInput.scale = scale;
    layoutInput.cardAnimationProgress = 1.0f;
    layoutInput.drawerOpen = drawerOpen;
    layoutInput.typography = &typography;
    const auto layout = overlay::ResolveAccessoryLayout(layoutInput);
    if (layout.status != brain::BrainOwnedAccessoryOperationStatus::Available) {
        worker.Stop();
        overlay::ShutdownAccessoryTextMeasurement(measurement);
        throw std::runtime_error("visual layout unavailable");
    }

    std::shared_ptr<const overlay::AccessoryPreparedDrawerPlan> prepared;
    if (drawerOpen) {
        overlay::AccessoryPreparationKeyInput keyInput;
        keyInput.drawer = command.snapshot->activeDrawer;
        keyInput.layoutGeneration = generation;
        keyInput.commandIdentity = command.snapshot->commandIdentity;
        keyInput.lifecycleEpoch = command.snapshot->lifecycleEpoch;
        keyInput.selectedDrawerContentRevision =
            command.snapshot->selectedDrawerContentRevision;
        keyInput.typographyGeneration = typography.generation;
        keyInput.scaleThousandths =
            static_cast<int>(std::lround(scale * 1'000.0f));
        keyInput.contentWidth = std::max(
            1, layout.drawerBounds.right - layout.drawerBounds.left -
                (2 * layout.drawerContentInset));
        keyInput.visibleLineCapacity = layout.drawerVisibleLineCapacity;
        overlay::AccessoryPreparationRequest request;
        request.key = overlay::BuildAccessoryPreparationKeyForCommand(keyInput);
        request.snapshot = command.snapshot;
        request.layout = layout;
        request.requestedMicroseconds = generation * 1'000;
        const auto preparationStarted = std::chrono::steady_clock::now();
        if (!worker.Request(request)) {
            worker.Stop();
            overlay::ShutdownAccessoryTextMeasurement(measurement);
            throw std::runtime_error("visual preparation request rejected");
        }
        const auto readyDeadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(2);
        while (!prepared && std::chrono::steady_clock::now() < readyDeadline) {
            prepared = worker.TryTakeReady(request.key, nullptr);
            if (!prepared) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        result.preparationUs = ElapsedUs(preparationStarted);
        if (!prepared || prepared->snapshot.get() != command.snapshot.get() ||
            !(prepared->key == request.key)) {
            worker.Stop();
            overlay::ShutdownAccessoryTextMeasurement(measurement);
            throw std::runtime_error("worker did not return the exact snapshot");
        }
    }

    overlay::AccessoryPresentationState presenter;
    overlay::AccessoryPresentationUpdateInput update;
    update.presentation = command;
    update.layout = layout;
    update.mainCardProductionSignature = "step5-visual-production-seam";
    update.measurementContext = measurement;
    update.preparedPlan = prepared;
    update.mechanicalLayoutGeneration = generation;
    const auto presentationStarted = std::chrono::steady_clock::now();
    const auto committed = overlay::UpdateAccessoryPresentation(
        &presenter, update);
    result.presentationUs = ElapsedUs(presentationStarted);
    if (committed.preparationPending ||
        committed.publishedSnapshotCount != 1 ||
        presenter.activeSnapshot.get() != command.snapshot.get()) {
        worker.Stop();
        overlay::ShutdownAccessoryTextMeasurement(measurement);
        throw std::runtime_error("exact command did not commit");
    }
    if (scrollClicks != 0 &&
        command.snapshot->activeDrawer !=
            brain::BrainOwnedAccessoryDrawerId::None) {
        overlay::AccessoryPresentationScrollInput scroll;
        scroll.layout = layout;
        scroll.pointerX =
            (layout.drawerBounds.left + layout.drawerBounds.right) / 2;
        scroll.pointerY =
            (layout.drawerBounds.top + layout.drawerBounds.bottom) / 2;
        scroll.wheelClicks = scrollClicks;
        const auto routed = overlay::ScrollAccessoryPresentation(
            &presenter, scroll);
        if (!routed.handled || routed.scope != overlay::AccessoryWheelScope::Drawer) {
            worker.Stop();
            overlay::ShutdownAccessoryTextMeasurement(measurement);
            throw std::runtime_error("production drawer wheel routing failed");
        }
    }

    const auto rasterStarted = std::chrono::steady_clock::now();
    const auto mainCard = MainCard();
    const auto card = overlay::RenderProductionMainCardForOfflineProof(
        mainCard, 0);
    const auto rail = overlay::RenderProductionAccessoryRailForOfflineProof(
        layout, *command.snapshot);
    const auto drawerPlan = overlay::BuildAccessoryDrawerRenderPlan(
        presenter, layout);
    const auto drawer = overlay::RenderProductionAccessoryDrawerForOfflineProof(
        layout, presenter);
    result.rasterUs = ElapsedUs(rasterStarted);
    result.visibleLines = drawerPlan.visibleLines.size();
    result.totalLines = drawerPlan.totalLineCount;
    result.drawerOffset = drawerPlan.firstVisibleLine;
    for (const auto& line : drawerPlan.visibleLines) {
        if (!result.visibleText.empty()) result.visibleText.push_back('\n');
        result.visibleText += line.text;
    }

    const auto& bounds = layout.resolvedBounds;
    result.composite = Canvas(
        bounds.right - bounds.left, bounds.top - bounds.bottom);
    const auto scaledCard = ScaleNearest(
        card,
        layout.mainCardBounds.right - layout.mainCardBounds.left,
        layout.mainCardBounds.top - layout.mainCardBounds.bottom);
    Blit(&result.composite, scaledCard,
         layout.mainCardBounds.left - bounds.left,
         bounds.top - layout.mainCardBounds.top);
    Blit(&result.composite, rail,
         layout.railBounds.left - bounds.left,
         bounds.top - layout.railBounds.top);
    Blit(&result.composite, drawer,
         layout.drawerBounds.left - bounds.left,
         bounds.top - layout.drawerBounds.top);

    if (publication == nullptr) {
        worker.Stop();
        overlay::ShutdownAccessoryTextMeasurement(measurement);
        throw std::runtime_error("publication context unavailable");
    }
    overlay::AccessoryVisiblePublicationKey visibleKey;
    visibleKey.commandIdentity = command.snapshot->commandIdentity;
    visibleKey.lifecycleEpoch = command.snapshot->lifecycleEpoch;
    visibleKey.railRevision = command.snapshot->railPresentationRevision;
    visibleKey.drawerRevision =
        command.snapshot->selectedDrawerContentRevision;
    visibleKey.drawerOpen = command.snapshot->activeDrawer !=
        brain::BrainOwnedAccessoryDrawerId::None;
    const auto began = publication->visible.Observe(
        {visibleKey, true, true, committed.delta.uploadRequests != 0,
         generation * 1'000 + 100, true});
    const auto terminal = publication->visible.CompleteFirstFrame(
        visibleKey, true, true, generation * 1'000 + 200);
    brain::BrainOwnedAccessoryPublicationFact fact;
    overlay::ApplyAccessoryVisiblePublicationTerminal(terminal, &fact);
    fact.activeDrawerRendered = command.snapshot->activeDrawer;
    fact.originatingClickSequence =
        command.snapshot->originatingClickSequence;
    fact.originatingClickAcceptedMicroseconds =
        command.snapshot->originatingClickAcceptedMicroseconds;
    fact.originatingMouseCallbackExitedMicroseconds =
        command.snapshot->originatingMouseCallbackExitedMicroseconds;
    fact.issueToCommitMicroseconds = 100;
    if (fact.originatingClickSequence != 0) {
        fact.clickTimingApplicable = true;
        fact.clickToTerminalMicroseconds = 100;
    }
    overlay::ApplyAccessoryPresentationRevisionDiagnostic(
        command.snapshot.get(), &fact);
    if (!began.started || !terminal.terminal ||
        !publication->queue.Produce(fact)) {
        worker.Stop();
        overlay::ShutdownAccessoryTextMeasurement(measurement);
        throw std::runtime_error("visible terminal fact was not queued");
    }
    brain::BrainOwnedAccessoryPublicationFact consumed;
    if (!publication->queue.Consume(&consumed)) {
        worker.Stop();
        overlay::ShutdownAccessoryTextMeasurement(measurement);
        throw std::runtime_error("visible terminal fact was not dequeued");
    }
    const auto decision = brain::ConsumeBrainOwnedAccessoryPublicationFact(
        state, consumed);
    result.brainAccepted = decision.consumed &&
        decision.commandTerminalAccepted &&
        decision.visibleAttemptTerminalAccepted;
    const auto diagnostic = overlay::SerializeAccessoryPublicationDiagnostic(
        consumed, decision, &publication->diagnostics);
    result.diagnosticSerialized = diagnostic.find(
        "event=accessory-publication-terminal") != std::string::npos;

    worker.Stop();
    overlay::ShutdownAccessoryTextMeasurement(measurement);
    return result;
}

struct CaseConfiguration {
    float scale = 1.0f;
    int scrollClicks = 0;
    bool acknowledgeBeforeFinal = false;
};

void UsePrimary(
    AtisFixture* fixture,
    const std::string& callsign,
    const std::string& code,
    const std::string& text,
    const std::string& frequency = "123.775",
    std::uint64_t generation = 1) {
    fixture->Decode(PrimaryDocument(callsign, code, text, frequency), generation);
    (void)fixture->Cycle();
}

CaseConfiguration ConfigureCase(
    const std::string& variant,
    AtisFixture* fixture) {
    CaseConfiguration configuration;
    auto selectAtis = [&] { fixture->Select(brain::BrainOwnedAccessoryDrawerId::Atis); };

    if (variant == "idle-no-endpoint") {
        fixture->input.flightContext = {};
        fixture->Decode(Document({}));
        (void)fixture->Cycle();
        selectAtis();
    } else if (variant == "confirmed-unavailable") {
        fixture->Decode(Document({}));
        (void)fixture->Cycle();
        selectAtis();
    } else if (variant == "source-unknown-missing") {
        fixture->Decode("{\"controllers\":[],\"pilots\":[]}");
        (void)fixture->Cycle();
        selectAtis();
    } else if (variant == "source-unknown-stale") {
        fixture->Decode(PrimaryDocument(
            "KDFW_D_ATIS", "S", "STALE SOURCE MUST BE UNKNOWN"));
        fixture->feedSnapshot.stale = true;
        (void)fixture->Cycle();
        selectAtis();
    } else if (variant == "departure-unread-a" ||
               variant == "departure-read-a") {
        UsePrimary(fixture, "KDFW_D_ATIS", "A",
                   "KDFW DEPARTURE INFORMATION ALPHA");
        selectAtis();
        configuration.acknowledgeBeforeFinal = variant == "departure-read-a";
    } else if (variant == "departure-unread-no-code" ||
               variant == "departure-read-no-code") {
        UsePrimary(fixture, "KDFW_D_ATIS", "",
                   "KDFW DEPARTURE WITHOUT CODE");
        selectAtis();
        configuration.acknowledgeBeforeFinal =
            variant == "departure-read-no-code";
    } else if (variant == "combined-departure") {
        UsePrimary(fixture, "KDFW_ATIS", "C",
                   "KDFW COMBINED INFORMATION CHARLIE");
        selectAtis();
    } else if (variant == "enroute-arrival-unread" ||
               variant == "enroute-arrival-read") {
        fixture->input.workflowStage = brain::WorkflowStage::Enroute;
        UsePrimary(fixture, "KSAN_A_ATIS", "B",
                   "KSAN ARRIVAL INFORMATION BRAVO", "134.800");
        selectAtis();
        configuration.acknowledgeBeforeFinal =
            variant == "enroute-arrival-read";
    } else if (variant == "vfr-departure") {
        fixture->input.operatingMode = brain::BrainOwnedOperatingMode::VFR;
        UsePrimary(fixture, "KDFW_D_ATIS", "D",
                   "KDFW VFR DEPARTURE INFORMATION DELTA");
        selectAtis();
    } else if (variant == "ifr-departure") {
        UsePrimary(fixture, "KDFW_D_ATIS", "E",
                   "KDFW IFR DEPARTURE INFORMATION ECHO");
        selectAtis();
    } else if (variant == "closed-unread" || variant == "closed-read") {
        UsePrimary(fixture, "KDFW_D_ATIS", "F",
                   variant == "closed-unread" ?
                       "CLOSED UNREAD FOXTROT" : "CLOSED READ FOXTROT");
        if (variant == "closed-read") {
            selectAtis();
            configuration.acknowledgeBeforeFinal = true;
        }
    } else if (variant == "atis-selected-unread" ||
               variant == "atis-selected-read") {
        UsePrimary(fixture, "KDFW_D_ATIS", "G",
                   variant == "atis-selected-unread" ?
                       "SELECTED UNREAD INFORMATION" :
                       "SELECTED READ INFORMATION");
        selectAtis();
        configuration.acknowledgeBeforeFinal =
            variant == "atis-selected-read";
    } else if (variant == "metar-selected" || variant == "pdc-selected") {
        UsePrimary(fixture, "KDFW_D_ATIS", "H",
                   "ATIS REMAINS UNREAD ON ANOTHER DRAWER");
        fixture->Select(variant == "metar-selected"
            ? brain::BrainOwnedAccessoryDrawerId::Metar
            : brain::BrainOwnedAccessoryDrawerId::Pdc);
    } else if (variant == "lookup-pending") {
        UsePrimary(fixture, "KDFW_D_ATIS", "A", "KDFW PRIMARY");
        fixture->Lookup("KABQ");
    } else if (variant == "lookup-combined") {
        fixture->Decode(Document({
            Record("KDFW_D_ATIS", "123.775", "A", {"KDFW PRIMARY"}),
            Record("KABQ_ATIS", "118.000", "C", {"KABQ COMBINED LOOKUP"})}));
        (void)fixture->Cycle(); fixture->Lookup("KABQ"); (void)fixture->Cycle();
    } else if (variant == "lookup-split") {
        fixture->Decode(Document({
            Record("KDFW_D_ATIS", "123.775", "A", {"KDFW PRIMARY"}),
            Record("KABQ_D_ATIS", "118.000", "D", {"KABQ DEPARTURE LOOKUP"}),
            Record("KABQ_A_ATIS", "119.000", "E", {"KABQ ARRIVAL LOOKUP"})}));
        (void)fixture->Cycle(); fixture->Lookup("KABQ"); (void)fixture->Cycle();
    } else if (variant == "lookup-departure-only" ||
               variant == "lookup-arrival-only") {
        const bool departure = variant == "lookup-departure-only";
        fixture->Decode(Document({
            Record("KDFW_D_ATIS", "123.775", "A", {"KDFW PRIMARY"}),
            Record(departure ? "KABQ_D_ATIS" : "KABQ_A_ATIS",
                   "118.000", departure ? "D" : "E",
                   {departure ? "KABQ DEPARTURE ONLY" : "KABQ ARRIVAL ONLY"})}));
        (void)fixture->Cycle(); fixture->Lookup("KABQ"); (void)fixture->Cycle();
    } else if (variant == "lookup-unavailable") {
        UsePrimary(fixture, "KDFW_D_ATIS", "A", "KDFW PRIMARY");
        fixture->Lookup("KABQ"); (void)fixture->Cycle();
    } else if (variant == "lookup-source-unknown") {
        fixture->Decode("{\"controllers\":[],\"pilots\":[]}");
        (void)fixture->Cycle(); fixture->Lookup("KABQ");
        (void)fixture->Cycle(20'000);
    } else if (variant == "lookup-expired") {
        fixture->Decode(Document({
            Record("KDFW_D_ATIS", "123.775", "A", {"KDFW PRIMARY AFTER LOOKUP"}),
            Record("KABQ_ATIS", "118.000", "C", {"KABQ LOOKUP"})}));
        (void)fixture->Cycle(); fixture->Lookup("KABQ");
        (void)fixture->Cycle(); (void)fixture->Cycle(8'000);
    } else if (variant == "lookup-lost-pdc") {
        fixture->Decode(Document({
            Record("KDFW_D_ATIS", "123.775", "A", {"KDFW PRIMARY"}),
            Record("KABQ_ATIS", "118.000", "C", {"KABQ LOOKUP"})}));
        (void)fixture->Cycle(); fixture->Lookup("KABQ");
        fixture->Select(brain::BrainOwnedAccessoryDrawerId::Pdc);
        (void)fixture->Cycle();
    } else if (variant == "open-update") {
        UsePrimary(fixture, "KDFW_D_ATIS", "I", "INITIAL INFORMATION INDIA");
        selectAtis();
        fixture->Decode(PrimaryDocument(
            "KDFW_D_ATIS", "J", "UPDATED INFORMATION JULIET"), 2);
        (void)fixture->Cycle(1);
    } else if (variant == "fresh-recovery") {
        fixture->Decode("{\"controllers\":[],\"pilots\":[]}", 1);
        (void)fixture->Cycle();
        fixture->Decode(PrimaryDocument(
            "KDFW_D_ATIS", "K", "RECOVERED INFORMATION KILO"), 2);
        (void)fixture->Cycle(1); selectAtis();
    } else if (variant == "suspend-retained") {
        UsePrimary(fixture, "KDFW_D_ATIS", "L", "SUSPEND RETAINED LIMA");
        const brain::BrainOwnedAsyncWorkerBindings noWorkers;
        (void)brain::SuspendBrainOwnedRuntimeForPluginAdmin(
            &fixture->state, noWorkers);
        (void)brain::ResumeBrainOwnedRuntimeFromPluginAdmin(&fixture->state);
        selectAtis();
    } else if (variant == "disconnect-unknown") {
        UsePrimary(fixture, "KDFW_D_ATIS", "M", "DISCONNECT RETAINED MIKE");
        brain::MarkBrainOwnedAtisSourceUnknownPreservingAcceptedState(
            &fixture->state);
        selectAtis();
    } else if (variant == "history-two" || variant == "history-five") {
        const int count = variant == "history-two" ? 2 : 5;
        for (int index = 1; index <= count; ++index) {
            const std::string word = index == count
                ? (count == 2 ? "HISTORY REVISION TWO" : "HISTORY REVISION FIVE")
                : "HISTORY REVISION " + std::to_string(index);
            fixture->Decode(PrimaryDocument(
                "KDFW_D_ATIS", std::to_string(index), word), index);
            (void)fixture->Cycle(1);
        }
        selectAtis();
    } else if (variant == "long-top" || variant == "long-scrolled") {
        std::vector<std::string> lines{"LONG ATIS HEADER"};
        for (int index = 1; index <= 18; ++index) {
            lines.push_back("LONG BODY SEGMENT " + std::to_string(index) +
                " WITH DETERMINISTIC WRAPPED AIRPORT INFORMATION");
        }
        fixture->Decode(Document({Record(
            "KDFW_D_ATIS", "123.775", "N", lines)}));
        (void)fixture->Cycle(); selectAtis();
        if (variant == "long-scrolled") configuration.scrollClicks = 100;
    } else if (variant.rfind("scale-", 0) == 0) {
        if (variant == "scale-075") configuration.scale = 0.75f;
        if (variant == "scale-125") configuration.scale = 1.25f;
        if (variant == "scale-150") configuration.scale = 1.50f;
        const std::string text = variant == "scale-075" ? "SCALE ZERO SEVEN FIVE" :
            variant == "scale-100" ? "SCALE ONE ZERO ZERO" :
            variant == "scale-125" ? "SCALE ONE TWO FIVE" :
                                     "SCALE ONE FIVE ZERO";
        UsePrimary(fixture, "KDFW_D_ATIS", "S", text);
        selectAtis();
    } else if (variant == "empty-frequency") {
        UsePrimary(fixture, "KDFW_D_ATIS", "T", "EMPTY FREQUENCY", "");
        selectAtis();
    } else if (variant == "empty-code") {
        UsePrimary(fixture, "KDFW_D_ATIS", "", "EMPTY INFORMATION CODE");
        selectAtis();
    } else if (variant == "malformed") {
        UsePrimary(fixture, "KDFW_X_ATIS", "U", "MALFORMED ROLE");
        selectAtis();
    } else if (variant == "controller-isolation") {
        fixture->Decode(Document({},
            "[{\"callsign\":\"KDFW_ATIS\",\"frequency\":\"123.775\","
            "\"facility\":4,\"visual_range\":50,"
            "\"text_atis\":[\"CONTROLLER TEXT MUST NOT OWN ATIS\"]}]") );
        (void)fixture->Cycle(); selectAtis();
    } else if (variant == "duplicate-newest") {
        fixture->Decode(Document({
            Record("KDFW_D_ATIS", "123.000", "V", {"OLDER DUPLICATE"},
                   "2026-08-30T11:00:00Z"),
            Record("KDFW_D_ATIS", "124.000", "W", {"NEWEST DUPLICATE"},
                   "2026-08-30T12:00:00Z")}));
        (void)fixture->Cycle(); selectAtis();
    } else if (variant == "equal-time") {
        fixture->Decode(Document({
            Record("KDFW_D_ATIS", "125.000", "X", {"EQUAL TIME X"}),
            Record("KDFW_D_ATIS", "124.000", "W", {"EQUAL TIME W"})}));
        (void)fixture->Cycle(); selectAtis();
    } else if (variant == "enroute-combined") {
        fixture->input.workflowStage = brain::WorkflowStage::Enroute;
        UsePrimary(fixture, "KSAN_ATIS", "Y", "KSAN ENROUTE COMBINED", "134.800");
        selectAtis();
    } else if (variant == "manual-only") {
        fixture->input.flightContext = {};
        fixture->Decode(Document({Record(
            "KABQ_ATIS", "118.000", "Z", {"KABQ MANUAL ONLY"})}));
        (void)fixture->Cycle(); fixture->Lookup("KABQ"); (void)fixture->Cycle();
    } else {
        throw std::runtime_error("unknown Step 5 visual variant: " + variant);
    }
    return configuration;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: XVatsimStep5AtisVisualProof <output-directory>\n";
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

    bool failed = false;
    std::ostringstream performance;
    performance << "file,projection_us,preparation_us,presentation_us,raster_us,"
                   "visible_lines,total_lines,drawer_offset,command,atis_revision\n";
    std::ostringstream cases;
    cases << "file\tvariant\texpected_token\tcommand\tatis_revision\tbrain_accepted"
             "\tdiagnostic_serialized\n";
    std::ostringstream sums;
    std::ostringstream equivalence;
    equivalence << "file\tpixel_equivalent_to\treason\n";
    std::map<std::string, std::string> hashOwners;
    std::size_t imageCount = 0;
    std::uint64_t generation = 1;

    for (const auto& visual : kVisuals) {
        try {
            auto fixture = std::make_unique<AtisFixture>();
            PublicationContext publication;
            const auto configuration = ConfigureCase(
                visual.variant, fixture.get());
            if (configuration.acknowledgeBeforeFinal) {
                const auto unread = fixture->Project();
                const auto acknowledgement = RenderFrame(
                    &fixture->state, unread, configuration.scale, 0,
                    generation++, &publication);
                if (!acknowledgement.brainAccepted ||
                    !acknowledgement.diagnosticSerialized) {
                    throw std::runtime_error(
                        "pre-render visible acknowledgement failed");
                }
                if (std::string(visual.variant) == "closed-read") {
                    fixture->CloseActive(90);
                }
            }

            const auto projectionStarted = std::chrono::steady_clock::now();
            const auto command = fixture->Project();
            const auto projectionUs = ElapsedUs(projectionStarted);
            if (!command.snapshot || AtisOrb(command) == nullptr) {
                throw std::runtime_error("Brain ATIS projection unavailable");
            }
            if (visual.expectedToken[0] != '\0' &&
                !SnapshotContains(command, visual.expectedToken)) {
                throw std::runtime_error(
                    "projected snapshot lacks required token: " +
                    std::string(visual.expectedToken));
            }
            const auto frame = RenderFrame(
                &fixture->state, command, configuration.scale,
                configuration.scrollClicks, generation++, &publication);
            if (visual.expectedToken[0] != '\0' &&
                command.snapshot->activeDrawer ==
                    brain::BrainOwnedAccessoryDrawerId::Atis &&
                frame.visibleText.find(visual.expectedToken) ==
                    std::string::npos) {
                throw std::runtime_error(
                    "rendered ATIS viewport lacks required token: " +
                    std::string(visual.expectedToken));
            }
            if (!frame.brainAccepted || !frame.diagnosticSerialized ||
                !SavePng(outputDirectory / visual.filename, frame.composite)) {
                throw std::runtime_error(
                    "production render/publication/PNG persistence failed");
            }
            const auto hash = Sha256(outputDirectory / visual.filename);
            if (hash.empty()) {
                throw std::runtime_error("PNG SHA-256 failed");
            }
            const auto existing = hashOwners.find(hash);
            if (existing != hashOwners.end()) {
                equivalence << visual.filename << '\t' << existing->second
                            << "\tdistinct source condition with identical truthful product visual\n";
            } else {
                hashOwners.emplace(hash, visual.filename);
            }
            ++imageCount;
            sums << hash << "  " << visual.filename << '\n';
            performance << visual.filename << ',' << projectionUs << ','
                        << frame.preparationUs << ',' << frame.presentationUs
                        << ',' << frame.rasterUs << ',' << frame.visibleLines
                        << ',' << frame.totalLines << ',' << frame.drawerOffset
                        << ',' << frame.commandIdentity << ','
                        << frame.atisRevision << '\n';
            cases << visual.filename << '\t' << visual.variant << '\t'
                  << visual.expectedToken << '\t' << frame.commandIdentity
                  << '\t' << frame.atisRevision << "\t1\t1\n";
        } catch (const std::exception& error) {
            std::cerr << visual.filename << ": " << error.what() << '\n';
            failed = true;
        }
    }

    {
        std::ofstream output(outputDirectory / "performance.csv", std::ios::binary);
        output << performance.str();
    }
    {
        std::ofstream output(outputDirectory / "cases.tsv", std::ios::binary);
        output << cases.str();
    }
    {
        std::ofstream output(
            outputDirectory / "visual_equivalence.tsv", std::ios::binary);
        output << equivalence.str();
    }
    {
        std::ofstream output(outputDirectory / "SHA256SUMS.txt", std::ios::binary);
        output << sums.str();
    }

    Gdiplus::GdiplusShutdown(token);
    if (failed || imageCount != std::size(kVisuals)) {
        std::cerr << "Step 5 ATIS visual proof failed\n";
        return 1;
    }
    std::cout << "Step 5 ATIS visual proof wrote 48 uniquely named deterministic images "
                 "through decoder, Brain, preparation, renderer, publication, "
                 "and diagnostic seams\n";
    return 0;
}
