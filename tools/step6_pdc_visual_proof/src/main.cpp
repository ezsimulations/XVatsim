#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
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
#include "XVatsim/modules/xpilot_bridge/XPilotBridge.h"

namespace fs = std::filesystem;
namespace brain = xvatsim::brain;
namespace overlay = xvatsim::modules::overlay;
namespace bridge = xvatsim::modules::xpilot_bridge;

namespace {

struct VisualSpec {
    const char* filename;
    const char* expectedToken;
};

constexpr VisualSpec kVisuals[]{
    {"01_closed_new_1.png", ""},
    {"02_closed_msg_1.png", ""},
    {"03_pre_capture_check.png", ""},
    {"04_captured_drawer_snapshot.png",
     "CAPTURED SNAPSHOT — CHECK XPILOT FOR REVISIONS"},
    {"21_scale_075.png", ""},
    {"22_scale_085.png", ""},
    {"23_scale_100.png", ""},
    {"24_scale_125.png", ""},
    {"25_scale_150.png", ""},
    {"26_clipped_small_screen.png", ""},
    {"27_metar_owns_unread.png", ""},
    {"28_atis_owns_unread.png", ""},
    {"53_metar_then_pdc.png", ""},
    {"54_atis_then_pdc.png", ""},
};
static_assert(std::size(kVisuals) == 14, "focused Step 6 visual count changed");

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
    DWORD resultBytes = 0;
    DWORD digestBytes = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
            nullptr, 0) != 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes),
            &resultBytes, 0) != 0 ||
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&digestBytes), sizeof(digestBytes),
            &resultBytes, 0) != 0) {
        if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    std::vector<unsigned char> object(objectBytes);
    std::vector<unsigned char> digest(digestBytes);
    const bool okay = BCryptCreateHash(algorithm, &hash, object.data(),
            static_cast<ULONG>(object.size()), nullptr, 0, 0) == 0 &&
        BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()),
            static_cast<ULONG>(bytes.size()), 0) == 0 &&
        BCryptFinishHash(hash, digest.data(),
            static_cast<ULONG>(digest.size()), 0) == 0;
    if (hash != nullptr) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return okay ? Hex(digest.data(), digest.size()) : std::string{};
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
    Gdiplus::Bitmap bitmap(image.width, image.height, image.width * 4,
        PixelFormat32bppARGB, const_cast<BYTE*>(image.bgraPixels.data()));
    return bitmap.Save(path.wstring().c_str(), &encoder, nullptr) == Gdiplus::Ok;
}

overlay::OfflineRasterImage Canvas(int width, int height) {
    overlay::OfflineRasterImage image;
    image.width = (std::max)(1, width);
    image.height = (std::max)(1, height);
    image.bgraPixels.resize(static_cast<std::size_t>(image.width * image.height * 4));
    for (std::size_t index = 0; index < image.bgraPixels.size(); index += 4) {
        image.bgraPixels[index] = 44;
        image.bgraPixels[index + 1] = 35;
        image.bgraPixels[index + 2] = 27;
        image.bgraPixels[index + 3] = 255;
    }
    return image;
}

void Blit(overlay::OfflineRasterImage* destination,
          const overlay::OfflineRasterImage& source, int left, int top) {
    if (destination == nullptr) return;
    for (int y = 0; y < source.height; ++y) {
        const int dy = top + y;
        if (dy < 0 || dy >= destination->height) continue;
        for (int x = 0; x < source.width; ++x) {
            const int dx = left + x;
            if (dx < 0 || dx >= destination->width) continue;
            const auto from = static_cast<std::size_t>((y * source.width + x) * 4);
            const auto to = static_cast<std::size_t>((dy * destination->width + dx) * 4);
            std::copy_n(source.bgraPixels.data() + from, 4,
                        destination->bgraPixels.data() + to);
        }
    }
}

class Reader final : public bridge::XPilotPrivatePrimitiveReader {
public:
    bridge::XPilotPrivateSourceIdentity source{
        true, true, true, true, 7, 11, "org.vatsim.xpilot", "3.0.2",
        "56AEC8CADC0CB36AAF36DC122C330FB782C76FC80190391B8D4A1F717602A8AF"};
    bridge::XPilotPrivateCapabilities capabilities{
        true, true, true, bridge::XPilotPrivateDataKind::Integer,
        bridge::XPilotPrivateDataKind::Bytes, bridge::XPilotPrivateDataKind::Bytes};
    bridge::XPilotPrivateSessionEvidence before{
        bridge::XPilotPrivateConnectionStatus::Connected, "N100PC"};
    bridge::XPilotPrivateSessionEvidence after = before;
    std::int64_t sequence = 0;
    std::string sender;
    std::string body;
    int sourceReads = 0;
    int capabilityReads = 0;
    int sessionReads = 0;
    int sequenceReads = 0;
    int senderReads = 0;
    int bodyReads = 0;

    bridge::XPilotPrivateSourceIdentity ReadSourceIdentity() override {
        ++sourceReads;
        return source;
    }
    bridge::XPilotPrivateCapabilities ReadCapabilities() override {
        ++capabilityReads;
        return capabilities;
    }
    bridge::XPilotPrivateSessionEvidence ReadSessionEvidence() override {
        return sessionReads++ == 0 ? before : after;
    }
    bool ReadSequence(std::int64_t* value) override {
        if (value == nullptr) return false;
        ++sequenceReads;
        *value = sequence;
        return true;
    }
    bool ReadSender(std::string* value) override {
        if (value == nullptr) return false;
        ++senderReads;
        *value = sender;
        return true;
    }
    bool ReadBody(std::string* value) override {
        if (value == nullptr) return false;
        ++bodyReads;
        *value = body;
        return true;
    }
};

struct Fixture {
    std::unique_ptr<brain::BrainOwnedRuntimeState> state =
        std::make_unique<brain::BrainOwnedRuntimeState>();
    brain::BrainPdcEvaluationContext context;
    std::uint64_t nowUs = 1'000;
    std::uint64_t clickSequence = 0;

    Fixture() {
        brain::InitializeBrainOwnedPdcRuntime(state.get());
        context.flightContextActive = true;
        context.ifrMode = true;
        context.workflowStage = brain::WorkflowStage::Departure;
        context.aircraftCallsign = "N100PC";
        context.departureIcao = "KDFW";
        context.destinationIcao = "KSAN";
        context.rosterAvailable = true;
        context.rosterStale = false;
        context.rosterComplete = true;
        context.senders = {
            {"KDFW_DEL", brain::BrainPdcSenderEvidenceKind::Controller,
             brain::StationRole::Delivery, true},
            {"KDFW_GND", brain::BrainPdcSenderEvidenceKind::Controller,
             brain::StationRole::Ground, true},
            {"N200AA", brain::BrainPdcSenderEvidenceKind::Pilot,
             brain::StationRole::Other, false},
            {"SUPERVISOR", brain::BrainPdcSenderEvidenceKind::AuthoritativeStaff,
             brain::StationRole::Other, false},
        };
    }

    brain::BrainPdcObservationDecision Observe(
        std::int64_t sequence, std::string sender, std::string body,
        bridge::XPilotPrivateConnectionStatus before =
            bridge::XPilotPrivateConnectionStatus::Connected,
        bridge::XPilotPrivateConnectionStatus after =
            bridge::XPilotPrivateConnectionStatus::Connected,
        bool sourceQualified = true, std::uint64_t pluginIdentity = 7) {
        Reader reader;
        reader.source.pluginInstanceIdentity = pluginIdentity;
        reader.source.binaryFingerprintQualified = sourceQualified;
        reader.before.status = before;
        reader.after.status = after;
        reader.before.callsign = before == bridge::XPilotPrivateConnectionStatus::Connected
            ? "N100PC" : "";
        reader.after.callsign = after == bridge::XPilotPrivateConnectionStatus::Connected
            ? "N100PC" : "";
        reader.sequence = sequence;
        reader.sender = std::move(sender);
        reader.body = std::move(body);
        const auto sampled = bridge::SampleXPilotPrivateMessageCombined(&reader);
        auto fact = bridge::ToBrainPdcMechanicalObservation(sampled, nowUs);
        const auto decision = brain::EvaluateBrainOwnedPdcObservation(
            state.get(), fact, context, nowUs);
        nowUs += 1'000;
        return decision;
    }

    void Select(brain::BrainOwnedAccessoryDrawerId drawer) {
        if (state->accessory.activeDrawer == drawer) return;
        ++clickSequence;
        const auto decision = brain::RequestBrainOwnedAccessoryDrawerSelection(
            state.get(), {drawer, clickSequence, nowUs, nowUs, nowUs + 1});
        nowUs += 10;
        if (decision.activeDrawer != drawer) {
            throw std::runtime_error("Brain drawer selection failed");
        }
    }

    void Close() {
        const auto active = state->accessory.activeDrawer;
        if (active == brain::BrainOwnedAccessoryDrawerId::None) return;
        ++clickSequence;
        const auto decision = brain::RequestBrainOwnedAccessoryDrawerSelection(
            state.get(), {active, clickSequence, nowUs, nowUs, nowUs + 1});
        nowUs += 10;
        if (decision.activeDrawer != brain::BrainOwnedAccessoryDrawerId::None) {
            throw std::runtime_error("Brain drawer close failed");
        }
    }

    brain::BrainOwnedAccessoryPresentationHandle Project() {
        return brain::ProjectBrainOwnedAccessoryPresentation(state.get(), nullptr);
    }
};

bool SnapshotContains(const brain::BrainOwnedAccessoryPresentationHandle& command,
                      const std::string& token) {
    if (!command.snapshot) return false;
    if (command.snapshot->drawerTitle.find(token) != std::string::npos ||
        command.snapshot->drawerStateText.find(token) != std::string::npos ||
        command.snapshot->emptyStateText.find(token) != std::string::npos) return true;
    return std::any_of(command.snapshot->entries.begin(), command.snapshot->entries.end(),
        [&](const auto& entry) {
            return entry.title.find(token) != std::string::npos ||
                entry.body.find(token) != std::string::npos;
        });
}

struct PublicationContext {
    overlay::AccessoryVisiblePublicationState visible;
    overlay::AccessoryPublicationFactQueue queue;
    overlay::AccessoryPublicationDiagnosticAccounting diagnostics;
};

struct FrameResult {
    overlay::OfflineRasterImage image;
    std::string visibleText;
    std::uint64_t preparationUs = 0;
    std::uint64_t presentationUs = 0;
    std::uint64_t rasterUs = 0;
    std::uint64_t command = 0;
    std::uint64_t revision = 0;
    int firstVisibleLine = 0;
    std::size_t visibleIdentities = 0;
};

FrameResult Render(Fixture* fixture,
                   const brain::BrainOwnedAccessoryPresentationHandle& command,
                   float scale, int scrollClicks, int screenWidth, int screenHeight,
                   std::uint64_t generation, PublicationContext* publication) {
    if (fixture == nullptr || command.snapshot == nullptr || publication == nullptr) {
        throw std::runtime_error("visual seam input unavailable");
    }
    FrameResult result;
    result.command = command.snapshot->commandIdentity;
    result.revision = command.snapshot->selectedDrawerContentRevision;
    auto* measurement = overlay::InitializeAccessoryTextMeasurement();
    if (measurement == nullptr) throw std::runtime_error("text measurement unavailable");
    const bool drawerOpen = command.snapshot->activeDrawer !=
        brain::BrainOwnedAccessoryDrawerId::None;
    overlay::AccessoryPreparationWorker worker;
    if (drawerOpen && !worker.Start(GetCurrentThreadId())) {
        overlay::ShutdownAccessoryTextMeasurement(measurement);
        throw std::runtime_error("generic preparation worker failed to start");
    }
    const auto typography = overlay::PrepareAccessoryTypography(measurement, scale);
    overlay::AccessoryLayoutInput layoutInput;
    layoutInput.screenWidth = screenWidth;
    layoutInput.screenHeight = screenHeight;
    layoutInput.windowLeft = 80;
    layoutInput.windowTop = screenHeight - 80;
    layoutInput.scale = scale;
    layoutInput.cardAnimationProgress = 1.0f;
    layoutInput.drawerOpen = drawerOpen;
    layoutInput.typography = &typography;
    const auto layout = overlay::ResolveAccessoryLayout(layoutInput);
    if (layout.status != brain::BrainOwnedAccessoryOperationStatus::Available) {
        worker.Stop();
        overlay::ShutdownAccessoryTextMeasurement(measurement);
        throw std::runtime_error("layout unavailable");
    }

    std::shared_ptr<const overlay::AccessoryPreparedDrawerPlan> prepared;
    if (drawerOpen) {
        overlay::AccessoryPreparationKeyInput key;
        key.drawer = command.snapshot->activeDrawer;
        key.layoutGeneration = generation;
        key.commandIdentity = command.snapshot->commandIdentity;
        key.lifecycleEpoch = command.snapshot->lifecycleEpoch;
        key.selectedDrawerContentRevision =
            command.snapshot->selectedDrawerContentRevision;
        key.typographyGeneration = typography.generation;
        key.scaleThousandths = static_cast<int>(std::lround(scale * 1'000.0f));
        key.contentWidth = (std::max)(1, layout.drawerBounds.right -
            layout.drawerBounds.left - 2 * layout.drawerContentInset);
        key.visibleLineCapacity = layout.drawerVisibleLineCapacity;
        overlay::AccessoryPreparationRequest request;
        request.key = overlay::BuildAccessoryPreparationKeyForCommand(key);
        request.snapshot = command.snapshot;
        request.layout = layout;
        request.requestedMicroseconds = generation * 1'000;
        const auto started = std::chrono::steady_clock::now();
        const auto deadline = started + std::chrono::seconds(2);
        bool requested = false;
        while (!requested && std::chrono::steady_clock::now() < deadline) {
            requested = worker.Request(request);
            if (!requested) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        while (requested && !prepared && std::chrono::steady_clock::now() < deadline) {
            prepared = worker.TryTakeReady(request.key, nullptr);
            if (!prepared) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        result.preparationUs = ElapsedUs(started);
        if (!prepared || prepared->snapshot.get() != command.snapshot.get()) {
            worker.Stop();
            overlay::ShutdownAccessoryTextMeasurement(measurement);
            throw std::runtime_error("exact snapshot was not prepared");
        }
    }

    overlay::AccessoryPresentationState presenter;
    overlay::AccessoryPresentationUpdateInput update;
    update.presentation = command;
    update.layout = layout;
    update.mainCardProductionSignature = "step6-pdc-visual-production-seam";
    update.measurementContext = measurement;
    update.preparedPlan = prepared;
    update.mechanicalLayoutGeneration = generation;
    const auto presentationStarted = std::chrono::steady_clock::now();
    const auto committed = overlay::UpdateAccessoryPresentation(&presenter, update);
    result.presentationUs = ElapsedUs(presentationStarted);
    if (committed.preparationPending || committed.publishedSnapshotCount != 1 ||
        presenter.activeSnapshot.get() != command.snapshot.get()) {
        worker.Stop();
        overlay::ShutdownAccessoryTextMeasurement(measurement);
        throw std::runtime_error("exact command did not commit");
    }
    if (scrollClicks != 0 && drawerOpen) {
        overlay::AccessoryPresentationScrollInput scroll;
        scroll.layout = layout;
        scroll.pointerX = (layout.drawerBounds.left + layout.drawerBounds.right) / 2;
        scroll.pointerY = (layout.drawerBounds.top + layout.drawerBounds.bottom) / 2;
        scroll.wheelClicks = scrollClicks;
        const auto routed = overlay::ScrollAccessoryPresentation(&presenter, scroll);
        if (!routed.handled || routed.scope != overlay::AccessoryWheelScope::Drawer) {
            worker.Stop();
            overlay::ShutdownAccessoryTextMeasurement(measurement);
            throw std::runtime_error("production wheel routing failed");
        }
    }

    const auto rasterStarted = std::chrono::steady_clock::now();
    const auto rail = overlay::RenderProductionAccessoryRailForOfflineProof(
        layout, *command.snapshot);
    const auto drawerPlan = overlay::BuildAccessoryDrawerRenderPlan(presenter, layout);
    const auto drawer = overlay::RenderProductionAccessoryDrawerForOfflineProof(
        layout, presenter);
    result.rasterUs = ElapsedUs(rasterStarted);
    result.firstVisibleLine = drawerPlan.firstVisibleLine;
    for (const auto& line : drawerPlan.visibleLines) {
        if (!result.visibleText.empty()) result.visibleText.push_back('\n');
        result.visibleText += line.text;
    }
    const auto& bounds = layout.resolvedBounds;
    result.image = Canvas(bounds.right - bounds.left, bounds.top - bounds.bottom);
    Blit(&result.image, rail, layout.railBounds.left - bounds.left,
         bounds.top - layout.railBounds.top);
    Blit(&result.image, drawer, layout.drawerBounds.left - bounds.left,
         bounds.top - layout.drawerBounds.top);

    overlay::AccessoryVisiblePublicationKey visibleKey;
    visibleKey.commandIdentity = command.snapshot->commandIdentity;
    visibleKey.lifecycleEpoch = command.snapshot->lifecycleEpoch;
    visibleKey.railRevision = command.snapshot->railPresentationRevision;
    visibleKey.drawerRevision = command.snapshot->selectedDrawerContentRevision;
    visibleKey.drawerOpen = drawerOpen;
    const auto began = publication->visible.Observe(
        {visibleKey, true, true, committed.delta.uploadRequests != 0,
         generation * 1'000 + 100, true});
    const auto terminal = publication->visible.CompleteFirstFrame(
        visibleKey, true, true, generation * 1'000 + 200);
    brain::BrainOwnedAccessoryPublicationFact fact;
    overlay::ApplyAccessoryVisiblePublicationTerminal(terminal, &fact);
    fact.activeDrawerRendered = command.snapshot->activeDrawer;
    fact.originatingClickSequence = command.snapshot->originatingClickSequence;
    fact.originatingClickAcceptedMicroseconds =
        command.snapshot->originatingClickAcceptedMicroseconds;
    fact.originatingMouseCallbackExitedMicroseconds =
        command.snapshot->originatingMouseCallbackExitedMicroseconds;
    fact.issueToCommitMicroseconds = 100;
    fact.pdcVisibleRevisionIdentities =
        overlay::CollectVisiblePdcRevisionIdentities(drawerPlan);
    result.visibleIdentities = fact.pdcVisibleRevisionIdentities.size();
    overlay::ApplyAccessoryPresentationRevisionDiagnostic(command.snapshot.get(), &fact);
    if (!began.started || !terminal.terminal || !publication->queue.Produce(fact)) {
        worker.Stop();
        overlay::ShutdownAccessoryTextMeasurement(measurement);
        throw std::runtime_error("publication fact was not queued");
    }
    brain::BrainOwnedAccessoryPublicationFact consumed;
    if (!publication->queue.Consume(&consumed)) {
        worker.Stop();
        overlay::ShutdownAccessoryTextMeasurement(measurement);
        throw std::runtime_error("publication fact was not consumed");
    }
    const auto decision = brain::ConsumeBrainOwnedAccessoryPublicationFact(
        fixture->state.get(), consumed);
    const auto diagnostic = overlay::SerializeAccessoryPublicationDiagnostic(
        consumed, decision, &publication->diagnostics);
    if (!decision.consumed || !decision.commandTerminalAccepted ||
        !decision.visibleAttemptTerminalAccepted ||
        diagnostic.find("event=accessory-publication-terminal") == std::string::npos) {
        worker.Stop();
        overlay::ShutdownAccessoryTextMeasurement(measurement);
        throw std::runtime_error("Brain publication acknowledgement failed");
    }
    worker.Stop();
    overlay::ShutdownAccessoryTextMeasurement(measurement);
    return result;
}

struct Configuration {
    float scale = 1.0f;
    int scrollClicks = 0;
    int screenWidth = 1920;
    int screenHeight = 1080;
    bool acknowledgeBeforeFinal = false;
    bool closeAfterAcknowledgement = false;
    brain::BrainOwnedAccessoryDrawerId drawer =
        brain::BrainOwnedAccessoryDrawerId::Pdc;
};

std::string Clearance(const std::string& token) {
    return "N100PC CLEARED TO KSAN VIA ROUTE CLIMB 5000 SQUAWK 1234 " + token;
}

void RequirePerformance(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

void RunPerformanceProof(const fs::path& outputDirectory) {
    std::uint64_t maximumSourceCallbackUs = 0;
    std::uint64_t maximumBrainEvaluationUs = 0;
    std::uint64_t maximumClickCallbackUs = 0;
    std::uint64_t maximumClickSequence = 0;

    auto sample = [&](Reader* reader, const bridge::XPilotPrivateObservationRequest& request,
                      std::uint64_t observedUs) {
        const auto value = bridge::SampleXPilotPrivateMessageCombined(reader, request);
        maximumSourceCallbackUs = (std::max)(
            maximumSourceCallbackUs, value.elapsedMicroseconds);
        return bridge::ToBrainPdcMechanicalObservation(value, observedUs);
    };
    auto evaluate = [&](Fixture* fixture,
                        const brain::BrainPdcMechanicalObservation& observation,
                        std::uint64_t nowUs) {
        const auto started = std::chrono::steady_clock::now();
        const auto decision = brain::EvaluateBrainOwnedPdcObservation(
            fixture->state.get(), observation, fixture->context, nowUs);
        maximumBrainEvaluationUs = (std::max)(
            maximumBrainEvaluationUs, ElapsedUs(started));
        return decision;
    };

    overlay::AccessoryClickFactQueue clickQueue;
    for (std::uint64_t sequence = 1; sequence <= 1'000; ++sequence) {
        const auto started = std::chrono::steady_clock::now();
        overlay::AccessoryClickFact captured;
        const auto produced = clickQueue.Produce(
            sequence % 3 == 0 ? brain::BrainOwnedAccessoryDrawerId::Metar
                              : sequence % 3 == 1
                                  ? brain::BrainOwnedAccessoryDrawerId::Atis
                                  : brain::BrainOwnedAccessoryDrawerId::Pdc,
            sequence * 1'000, &captured);
        const auto exited = produced && clickQueue.MarkMouseCallbackExited(
            captured.requestSequence, sequence * 1'000 + 1);
        const auto elapsed = ElapsedUs(started);
        if (sequence == 1 || elapsed > maximumClickCallbackUs) {
            maximumClickCallbackUs = elapsed;
            maximumClickSequence = sequence;
        }
        RequirePerformance(produced, "1,000-click production failed");
        RequirePerformance(exited, "1,000-click callback exit failed");
        overlay::AccessoryClickFact consumed;
        RequirePerformance(clickQueue.Consume(&consumed),
            "1,000-click consumption failed");
        RequirePerformance(consumed.requestSequence == captured.requestSequence,
            "1,000-click FIFO identity mismatch");
    }
    RequirePerformance(clickQueue.ProducedCount() == 1'000 &&
        clickQueue.ConsumedCount() == 1'000 && clickQueue.PendingCount() == 0 &&
        clickQueue.DroppedCount() == 0 && maximumClickCallbackUs <= 500,
        "1,000-click exact/timing contract failed");

    auto queueFixture = std::make_unique<Fixture>();
    bridge::XPilotPrivateObservationQueue observationQueue;
    std::uint64_t observationTerminal = 0;
    std::uint64_t observationRejected = 0;
    for (std::int64_t base = 1; base <= 1'000; base += 10) {
        for (std::int64_t offset = 0; offset < 10; ++offset) {
            Reader reader;
            reader.sequence = base + offset;
            reader.sender = "KDFW_DEL";
            reader.body = Clearance("BURST " + std::to_string(base + offset));
            const auto fact = sample(&reader, {},
                static_cast<std::uint64_t>(base + offset) * 1'000);
            if (!observationQueue.Produce(fact)) {
                brain::RecordBrainOwnedPdcTransportCapacityLoss(
                    queueFixture->state.get(), 1);
            }
        }
        brain::BrainPdcMechanicalObservation fact;
        RequirePerformance(observationQueue.Consume(&fact),
            "capacity campaign initial drain failed");
        auto decision = evaluate(queueFixture.get(), fact,
            static_cast<std::uint64_t>(base) * 1'000);
        observationTerminal += decision.evaluated;
        observationRejected += decision.evaluated && !decision.mechanicallyAccepted;
        RequirePerformance(observationQueue.ServiceRetained(),
            "retained observation did not obtain capacity");
        while (observationQueue.Consume(&fact)) {
            decision = evaluate(queueFixture.get(), fact,
                static_cast<std::uint64_t>(fact.sequenceAfter) * 1'000);
            observationTerminal += decision.evaluated;
            observationRejected += decision.evaluated && !decision.mechanicallyAccepted;
        }
    }
    const auto queueCounters = observationQueue.Counters();
    RequirePerformance(queueCounters.produced == 1'000 &&
        queueCounters.queued == 900 && queueCounters.retained == 100 &&
        queueCounters.consumed == 900 && queueCounters.capacityLoss == 100 &&
        observationTerminal == 1 && observationRejected == 0 &&
        queueFixture->state->pdc.counters.admittedMessages == 1 &&
        queueFixture->state->pdc.counters.postCaptureEarlyNoops == 899 &&
        queueFixture->state->pdc.counters.capacityLosses == 1 &&
        queueFixture->state->pdc.captureComplete,
        "one-shot observation queue ledger mismatch");

    auto connectedFixture = std::make_unique<Fixture>();
    Reader connectedReader;
    connectedReader.sequence = 1;
    connectedReader.sender = "KDFW_DEL";
    connectedReader.body = Clearance("CONNECTED FAST PATH");
    auto connectedFact = sample(&connectedReader, {}, 1'000);
    RequirePerformance(evaluate(connectedFixture.get(), connectedFact, 1'000).admitted,
        "connected fast-path seed was not admitted");
    bridge::XPilotPrivateObservationRequest connectedRequest;
    connectedRequest.fastPathToken = {true,
        connectedReader.source.pluginInstanceIdentity,
        connectedReader.source.capabilityGeneration, 1};
    const auto connectedSenderReads = connectedReader.senderReads;
    const auto connectedBodyReads = connectedReader.bodyReads;
    const auto connectedSemantic =
        connectedFixture->state->accessory.semanticPresentationGeneration;
    for (std::uint64_t cycle = 0; cycle < 100'000; ++cycle) {
        connectedFact = sample(&connectedReader, connectedRequest, 2'000 + cycle);
        RequirePerformance(connectedFact.sequenceOnlyFastPath &&
            !connectedFact.senderBodyRead,
            "connected duplicate left sequence-only fast path");
        const auto decision = evaluate(
            connectedFixture.get(), connectedFact, 2'000 + cycle);
        RequirePerformance(!decision.evaluated && !decision.productChanged,
            "post-capture observation created product work");
    }
    RequirePerformance(connectedReader.senderReads == connectedSenderReads &&
        connectedReader.bodyReads == connectedBodyReads &&
        connectedFixture->state->pdc.counters.postCaptureEarlyNoops == 100'000 &&
        connectedFixture->state->accessory.semanticPresentationGeneration ==
            connectedSemantic,
        "100,000 post-capture observations performed semantic/string work");
    const auto connectedSenderReadDelta =
        connectedReader.senderReads - connectedSenderReads;
    const auto connectedBodyReadDelta =
        connectedReader.bodyReads - connectedBodyReads;

    auto disconnectedFixture = std::make_unique<Fixture>();
    Reader disconnectedReader;
    disconnectedReader.before = {
        bridge::XPilotPrivateConnectionStatus::Disconnected, ""};
    disconnectedReader.after = disconnectedReader.before;
    disconnectedReader.sequence = 1;
    disconnectedReader.sender = "KDFW_DEL";
    disconnectedReader.body = Clearance("DISCARDED BEFORE CAPTURE");
    auto disconnectedFact = sample(&disconnectedReader, {}, 1'000);
    const auto disconnectedDecision = evaluate(
        disconnectedFixture.get(), disconnectedFact, 1'000);
    RequirePerformance(disconnectedDecision.evaluated &&
        !disconnectedDecision.provisionalRetained &&
        !disconnectedFixture->state->pdc.capturedArtifact.has_value() &&
        disconnectedFixture->state->pdc.retainedBytes == 0 &&
        disconnectedFixture->state->pdc.counters.provisionalRetained == 0,
        "disconnected pre-capture content was retained");

    Reader stoppedReader;
    stoppedReader.sequence = 2;
    stoppedReader.sender = "KDFW_DEL";
    stoppedReader.body = Clearance("MUST NOT BE SAMPLED");
    const auto stoppedSenderReads = stoppedReader.senderReads;
    const auto stoppedBodyReads = stoppedReader.bodyReads;
    std::uint64_t stoppedAcquisitionDecisions = 0;
    for (std::uint64_t cycle = 0; cycle < 100'000; ++cycle) {
        const auto started = std::chrono::steady_clock::now();
        const auto acquisition = brain::EvaluateBrainOwnedPdcAcquisition(
            connectedFixture->state.get(), connectedFixture->context);
        maximumBrainEvaluationUs = (std::max)(
            maximumBrainEvaluationUs, ElapsedUs(started));
        RequirePerformance(acquisition.evaluated && acquisition.captureComplete &&
            !acquisition.samplePrivatePayload && acquisition.clearQueuedFacts,
            "Brain did not command a complete post-capture payload-read stop");
        ++stoppedAcquisitionDecisions;
    }
    RequirePerformance(stoppedReader.senderReads == stoppedSenderReads &&
        stoppedReader.bodyReads == stoppedBodyReads,
        "post-capture acquisition path sampled sender/body content");

    auto boundedFixture = std::make_unique<Fixture>();
    Reader fullReader;
    fullReader.sequence = 1;
    fullReader.sender = "KDFW_DEL";
    fullReader.body.assign(4'096, 'X');
    const auto prefix = Clearance("FULL BOUNDED INPUT ");
    std::copy(prefix.begin(), prefix.end(), fullReader.body.begin());
    const auto fullFact = sample(&fullReader, {}, 50'000);
    const auto fullDecision = evaluate(boundedFixture.get(), fullFact, 50'000);
    RequirePerformance(fullDecision.captureCompleted &&
        maximumBrainEvaluationUs <= 5'000,
        "full bounded one-shot Brain evaluation exceeded contract");
    const auto& bounded = boundedFixture->state->pdc;
    RequirePerformance(bounded.captureComplete &&
        bounded.capturedArtifact.has_value() &&
        bounded.retainedBytes <= 65'536 &&
        brain::BrainOwnedPdcUnreadCount(bounded) == 1,
        "one-item Brain memory bound failed");

    auto lifecycleFixture = std::make_unique<Fixture>();
    RequirePerformance(lifecycleFixture->Observe(
        1, "KDFW_DEL", Clearance("LIFECYCLE SNAPSHOT")).captureCompleted,
        "lifecycle capture seed failed");
    const auto lifecycleRevision =
        lifecycleFixture->state->pdc.capturedArtifact->revisionIdentity;
    const auto lifecycleEpoch = lifecycleFixture->state->pdc.productLifecycleEpoch;
    brain::MarkBrainOwnedPdcSourceUnavailable(lifecycleFixture->state.get());
    brain::SuspendBrainOwnedPdcRuntime(lifecycleFixture->state.get());
    const auto suspendedAcquisition = brain::EvaluateBrainOwnedPdcAcquisition(
        lifecycleFixture->state.get(), lifecycleFixture->context);
    brain::ResumeBrainOwnedPdcRuntime(lifecycleFixture->state.get());
    const auto resumedAcquisition = brain::EvaluateBrainOwnedPdcAcquisition(
        lifecycleFixture->state.get(), lifecycleFixture->context);
    RequirePerformance(!suspendedAcquisition.samplePrivatePayload &&
        resumedAcquisition.captureComplete &&
        !resumedAcquisition.samplePrivatePayload &&
        lifecycleFixture->state->pdc.capturedArtifact.has_value() &&
        lifecycleFixture->state->pdc.capturedArtifact->revisionIdentity ==
            lifecycleRevision,
        "disconnect or administrative suspend altered/re-armed the capture");
    lifecycleFixture->context.destinationIcao = "KLAX";
    const auto rearmedAcquisition = brain::EvaluateBrainOwnedPdcAcquisition(
        lifecycleFixture->state.get(), lifecycleFixture->context);
    RequirePerformance(rearmedAcquisition.acquisitionArmed &&
        rearmedAcquisition.samplePrivatePayload &&
        !lifecycleFixture->state->pdc.captureComplete &&
        !lifecycleFixture->state->pdc.capturedArtifact.has_value() &&
        lifecycleFixture->state->pdc.productLifecycleEpoch == lifecycleEpoch + 1,
        "new flight identity did not clear and re-arm one-shot capture");

    auto progressedFixture = std::make_unique<Fixture>();
    progressedFixture->context.workflowStage = brain::WorkflowStage::Enroute;
    const auto progressedAcquisition = brain::EvaluateBrainOwnedPdcAcquisition(
        progressedFixture->state.get(), progressedFixture->context);
    RequirePerformance(
        !progressedFixture->state->pdc.acquisitionClosed &&
        progressedAcquisition.acquisitionArmed &&
        progressedAcquisition.contextReady &&
        progressedAcquisition.samplePrivatePayload,
        "workflow stage changed waiting-message eligibility");

    const auto settledCommand = boundedFixture->Project();
    const auto settledSemantic =
        boundedFixture->state->accessory.semanticPresentationGeneration;
    const auto settledPdcMutations = boundedFixture->state->pdc.counters.semanticMutations;
    std::uint64_t settledServiceRequests = 0;
    for (std::uint64_t frame = 0; frame < 100'000; ++frame) {
        const auto cadence = overlay::ResolveAccessoryFlightLoopCadence(false);
        settledServiceRequests += cadence.nextCycle;
    }
    RequirePerformance(settledCommand.snapshot &&
        boundedFixture->state->accessory.semanticPresentationGeneration ==
            settledSemantic &&
        boundedFixture->state->pdc.counters.semanticMutations == settledPdcMutations &&
        settledServiceRequests == 0,
        "settled warm idle created PDC work");
    RequirePerformance(maximumSourceCallbackUs <= 500,
        "source observation callback exceeded 500 microseconds");

    std::ostringstream report;
    report << "metric\tvalue\tcontract\n"
           << "click_produced\t" << clickQueue.ProducedCount() << "\t1000\n"
           << "click_consumed\t" << clickQueue.ConsumedCount() << "\t1000\n"
           << "click_pending\t" << clickQueue.PendingCount() << "\t0\n"
           << "click_dropped\t" << clickQueue.DroppedCount() << "\t0\n"
           << "maximum_click_callback_us\t" << maximumClickCallbackUs << "\t<=500\n"
           << "maximum_click_sequence\t" << maximumClickSequence << "\tdiagnostic\n"
           << "observations_produced\t" << queueCounters.produced << "\t1000\n"
           << "observations_queued\t" << queueCounters.queued << "\t900\n"
           << "observations_retained\t" << queueCounters.retained << "\t100\n"
           << "observations_consumed\t" << queueCounters.consumed << "\t900\n"
           << "observations_capacity_loss\t" << queueCounters.capacityLoss << "\t100\n"
           << "observations_terminal\t" << observationTerminal << "\t1\n"
           << "observations_rejected\t" << observationRejected << "\t0\n"
           << "brain_admitted\t" << queueFixture->state->pdc.counters.admittedMessages
           << "\t1\n"
           << "brain_gap_events\t" << queueFixture->state->pdc.counters.gapEvents
           << "\t0\n"
           << "brain_capacity_losses\t" << queueFixture->state->pdc.counters.capacityLosses
           << "\t1\n"
           << "maximum_source_callback_us\t" << maximumSourceCallbackUs << "\t<=500\n"
           << "maximum_brain_evaluation_us\t" << maximumBrainEvaluationUs << "\t<=5000\n"
           << "post_capture_observation_cycles\t100000\t100000\n"
           << "post_capture_observation_sender_reads\t"
           << connectedSenderReadDelta << "\t0\n"
           << "post_capture_observation_body_reads\t"
           << connectedBodyReadDelta << "\t0\n"
           << "post_capture_acquisition_decisions\t"
           << stoppedAcquisitionDecisions << "\t100000\n"
           << "post_capture_payload_sender_reads\t"
           << (stoppedReader.senderReads - stoppedSenderReads) << "\t0\n"
           << "post_capture_payload_body_reads\t"
           << (stoppedReader.bodyReads - stoppedBodyReads) << "\t0\n"
           << "settled_idle_cycles\t100000\t100000\n"
           << "settled_idle_service_requests\t" << settledServiceRequests << "\t0\n"
           << "retained_artifacts\t"
           << (bounded.capturedArtifact.has_value() ? 1 : 0) << "\t1\n"
           << "retained_bytes\t" << bounded.retainedBytes << "\t<=65536\n"
           << "unread_messages\t" << brain::BrainOwnedPdcUnreadCount(bounded)
           << "\t1\n"
           << "temporary_disconnect_capture_preserved\t1\t1\n"
           << "administrative_resume_sampling_stopped\t1\t1\n"
           << "flight_identity_change_rearmed\t1\t1\n"
           << "workflow_neutral_acquisition\t1\t1\n"
           << "observation_queue_capacity\t"
           << bridge::XPilotPrivateObservationQueue::kCapacity << "\t8\n"
           << "click_queue_capacity\t" << overlay::AccessoryClickFactQueue::kCapacity
           << "\t64\n";
    std::ofstream file(outputDirectory / "pdc_performance_proof.tsv", std::ios::binary);
    file << report.str();
    RequirePerformance(file.good(), "performance proof persistence failed");
}

Configuration Configure(std::size_t index, Fixture* fixture) {
    Configuration configuration;
    auto submit = [&](std::int64_t sequence, const std::string& sender,
                      const std::string& body) {
        return fixture->Observe(sequence, sender, body);
    };
    const auto strict = [&](const std::string& token) {
        return submit(1, "KDFW_DEL", Clearance(token));
    };

    switch (index) {
        case 1:
            strict("ONE SHOT NEW");
            configuration.drawer = brain::BrainOwnedAccessoryDrawerId::None;
            break;
        case 2:
            strict("ONE SHOT VIEWED");
            configuration.drawer = brain::BrainOwnedAccessoryDrawerId::Pdc;
            configuration.acknowledgeBeforeFinal = true;
            configuration.closeAfterAcknowledgement = true;
            break;
        case 3:
            brain::RecordBrainOwnedPdcTransportCapacityLoss(
                fixture->state.get(), 1);
            configuration.drawer = brain::BrainOwnedAccessoryDrawerId::None;
            break;
        case 4:
            strict("NEUTRAL SYNTHETIC CLEARANCE");
            configuration.drawer = brain::BrainOwnedAccessoryDrawerId::Pdc;
            break;
        case 5:
        case 6:
        case 7:
        case 8:
        case 9: {
            static constexpr std::array<float, 5> scales{
                0.75f, 0.85f, 1.0f, 1.25f, 1.5f};
            configuration.scale = scales[index - 5];
            strict("SUPPORTED SCALE CONTROL");
            configuration.drawer = brain::BrainOwnedAccessoryDrawerId::Pdc;
            break;
        }
        case 10:
            strict("CLIPPED GEOMETRY CONTROL");
            configuration.screenWidth = 900;
            configuration.screenHeight = 600;
            configuration.drawer = brain::BrainOwnedAccessoryDrawerId::Pdc;
            break;
        case 11:
            configuration.drawer = brain::BrainOwnedAccessoryDrawerId::Metar;
            break;
        case 12:
            configuration.drawer = brain::BrainOwnedAccessoryDrawerId::Atis;
            break;
        case 13:
            strict("METAR CROSS DRAWER CONTROL");
            configuration.drawer = brain::BrainOwnedAccessoryDrawerId::Metar;
            break;
        case 14:
            strict("ATIS CROSS DRAWER CONTROL");
            configuration.drawer = brain::BrainOwnedAccessoryDrawerId::Atis;
            break;
        default:
            throw std::runtime_error("focused visual case is unmapped");
    }
    return configuration;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: XVatsimStep6PdcVisualProof <output-directory>\n";
        return 2;
    }
    const fs::path outputDirectory = argv[1];
    fs::create_directories(outputDirectory);
    try {
        RunPerformanceProof(outputDirectory);
    } catch (const std::exception& error) {
        std::cerr << "Step 6 PDC performance proof failed: " << error.what() << '\n';
        return 1;
    }
    Gdiplus::GdiplusStartupInput startupInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &startupInput, nullptr) != Gdiplus::Ok) {
        std::cerr << "GDI+ startup failed\n";
        return 1;
    }

    bool failed = false;
    std::size_t images = 0;
    std::uint64_t generation = 1;
    std::ostringstream performance;
    performance << "file,preparation_us,presentation_us,raster_us,command,revision,first_visible_line,visible_identities\n";
    std::ostringstream cases;
    cases << "file\texpected_token\tcommand\trevision\tfirst_visible_line\tvisible_identities\n";
    std::ostringstream sums;
    std::map<std::string, std::string> hashOwners;
    std::ostringstream equivalence;
    equivalence << "file\tpixel_equivalent_to\treason\n";

    for (std::size_t index = 1; index <= std::size(kVisuals); ++index) {
        const auto& visual = kVisuals[index - 1];
        try {
            auto fixture = std::make_unique<Fixture>();
            PublicationContext publication;
            const auto configuration = Configure(index, fixture.get());
            fixture->Select(configuration.drawer);
            if (configuration.acknowledgeBeforeFinal) {
                const auto unread = fixture->Project();
                (void)Render(fixture.get(), unread, configuration.scale, 0,
                    configuration.screenWidth, configuration.screenHeight,
                    generation++, &publication);
                if (configuration.closeAfterAcknowledgement) fixture->Close();
            }
            if (index == 13 || index == 14) {
                fixture->Select(brain::BrainOwnedAccessoryDrawerId::Pdc);
            }
            const auto projectionStarted = std::chrono::steady_clock::now();
            const auto command = fixture->Project();
            const auto projectionUs = ElapsedUs(projectionStarted);
            if (!command.snapshot ||
                (visual.expectedToken[0] != '\0' &&
                  !SnapshotContains(command, visual.expectedToken))) {
                std::ostringstream detail;
                detail << "Brain projection lacks required visual token; drawer="
                       << (command.snapshot ? static_cast<int>(command.snapshot->activeDrawer) : -1)
                       << " entries=" << (command.snapshot ? command.snapshot->entries.size() : 0);
                if (command.snapshot && !command.snapshot->entries.empty()) {
                    detail << " firstTitle=" << command.snapshot->entries.front().title;
                }
                throw std::runtime_error(detail.str());
            }
            if (index >= 1 && index <= 3) {
                static constexpr std::array<const char*, 3> expected{
                    "NEW", "IDLE", "CHECK"};
                const auto pdc = std::find_if(
                    command.snapshot->orbs.begin(), command.snapshot->orbs.end(),
                    [](const auto& orb) {
                        return orb.drawer == brain::BrainOwnedAccessoryDrawerId::Pdc;
                    });
                if (pdc == command.snapshot->orbs.end() ||
                    !pdc->airportIcao.empty() ||
                    pdc->categoryText != expected[index - 1]) {
                    throw std::runtime_error(
                        "focused Brain-owned PDC rail projection mismatch");
                }
            }
            if (index == 4 &&
                (command.snapshot->drawerTitle != "PDC — KDFW" ||
                 command.snapshot->entries.size() != 2 ||
                 command.snapshot->entries[0].stableKey !=
                    "pdc-captured-snapshot-warning" ||
                 command.snapshot->entries[0].title !=
                    "CAPTURED SNAPSHOT — CHECK XPILOT FOR REVISIONS")) {
                throw std::runtime_error(
                    "focused one-shot PDC drawer structure mismatch");
            }
            if (index == 1) {
                auto withoutStatus =
                    std::make_shared<brain::BrainOwnedAccessoryPresentationSnapshot>(
                        *command.snapshot);
                const auto pdc = std::find_if(
                    withoutStatus->orbs.begin(), withoutStatus->orbs.end(),
                    [](const auto& orb) {
                        return orb.drawer == brain::BrainOwnedAccessoryDrawerId::Pdc;
                    });
                if (pdc == withoutStatus->orbs.end()) {
                    throw std::runtime_error("PDC A/B control is absent");
                }
                pdc->categoryText.clear();
                auto* measurement = overlay::InitializeAccessoryTextMeasurement();
                if (measurement == nullptr) {
                    throw std::runtime_error("PDC A/B text measurement failed");
                }
                const auto typography = overlay::PrepareAccessoryTypography(
                    measurement, configuration.scale);
                overlay::AccessoryLayoutInput layoutInput;
                layoutInput.screenWidth = configuration.screenWidth;
                layoutInput.screenHeight = configuration.screenHeight;
                layoutInput.windowLeft = 80;
                layoutInput.windowTop = configuration.screenHeight - 80;
                layoutInput.scale = configuration.scale;
                layoutInput.cardAnimationProgress = 1.0f;
                layoutInput.drawerOpen = false;
                layoutInput.typography = &typography;
                const auto layout = overlay::ResolveAccessoryLayout(layoutInput);
                const auto absentRail =
                    overlay::RenderProductionAccessoryRailForOfflineProof(
                        layout, *withoutStatus);
                const auto presentRail =
                    overlay::RenderProductionAccessoryRailForOfflineProof(
                        layout, *command.snapshot);
                overlay::ShutdownAccessoryTextMeasurement(measurement);
                if (absentRail.bgraPixels == presentRail.bgraPixels) {
                    throw std::runtime_error(
                        "production rail ignored Brain-supplied PDC categoryText");
                }
            }
            if (index >= 5 && index <= 10) {
                const auto pdc = std::find_if(
                    command.snapshot->orbs.begin(), command.snapshot->orbs.end(),
                    [](const auto& orb) {
                        return orb.drawer == brain::BrainOwnedAccessoryDrawerId::Pdc;
                    });
                if (pdc == command.snapshot->orbs.end() || !pdc->selected ||
                    pdc->selectedIndicator != "OPEN" || !pdc->airportIcao.empty()) {
                    throw std::runtime_error(
                        "selected PDC OPEN/empty-airport control failed");
                }
            }
            if ((index == 11 && command.snapshot->activeDrawer !=
                    brain::BrainOwnedAccessoryDrawerId::Metar) ||
                (index == 12 && command.snapshot->activeDrawer !=
                    brain::BrainOwnedAccessoryDrawerId::Atis) ||
                ((index == 13 || index == 14) &&
                    command.snapshot->activeDrawer !=
                        brain::BrainOwnedAccessoryDrawerId::Pdc)) {
                throw std::runtime_error(
                    "METAR/ATIS or cross-drawer ownership control failed");
            }
            const auto frame = Render(fixture.get(), command, configuration.scale,
                configuration.scrollClicks, configuration.screenWidth,
                configuration.screenHeight, generation++, &publication);
            if (visual.expectedToken[0] != '\0' &&
                command.snapshot->activeDrawer == brain::BrainOwnedAccessoryDrawerId::Pdc &&
                frame.visibleText.find(visual.expectedToken) == std::string::npos) {
                throw std::runtime_error("rendered viewport lacks required visual token");
            }
            const auto path = outputDirectory / visual.filename;
            if (!SavePng(path, frame.image)) {
                throw std::runtime_error("PNG persistence failed");
            }
            const auto hash = Sha256(path);
            if (hash.empty()) throw std::runtime_error("PNG SHA-256 failed");
            if (const auto same = hashOwners.find(hash); same != hashOwners.end()) {
                equivalence << visual.filename << '\t' << same->second
                            << "\tdistinct source condition with identical truthful product visual\n";
            } else {
                hashOwners.emplace(hash, visual.filename);
            }
            ++images;
            sums << hash << "  " << visual.filename << '\n';
            performance << visual.filename << ',' << projectionUs << ','
                        << frame.preparationUs << ',' << frame.presentationUs << ','
                        << frame.rasterUs << ',' << frame.command << ',' << frame.revision
                        << ',' << frame.firstVisibleLine << ',' << frame.visibleIdentities << '\n';
            cases << visual.filename << '\t' << visual.expectedToken << '\t'
                  << frame.command << '\t' << frame.revision << '\t'
                  << frame.firstVisibleLine << '\t' << frame.visibleIdentities << '\n';
        } catch (const std::exception& error) {
            std::cerr << visual.filename << ": " << error.what() << '\n';
            failed = true;
        }
    }

    { std::ofstream file(outputDirectory / "performance.csv", std::ios::binary); file << performance.str(); }
    { std::ofstream file(outputDirectory / "cases.tsv", std::ios::binary); file << cases.str(); }
    { std::ofstream file(outputDirectory / "visual_equivalence.tsv", std::ios::binary); file << equivalence.str(); }
    { std::ofstream file(outputDirectory / "SHA256SUMS.txt", std::ios::binary); file << sums.str(); }
    Gdiplus::GdiplusShutdown(gdiplusToken);
    if (failed || images != std::size(kVisuals)) {
        std::cerr << "Step 6 PDC visual proof failed\n";
        return 1;
    }
    std::cout << "Step 6 PDC visual proof wrote 14 focused deterministic images through "
                 "sampler, Brain, preparation, renderer, publication, and diagnostic seams\n";
    return 0;
}
