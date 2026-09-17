#include "Step6PdcContractProbe.h"

#include "XVatsim/brain/BrainOwnedRuntime.h"
#include "XVatsim/modules/overlay/OverlayAccessoryCore.h"
#include "XVatsim/modules/xpilot_bridge/XPilotBridge.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace xvatsim::tools::step6_pdc_proof {
namespace {

using namespace brain;
using namespace modules::overlay;
using namespace modules::xpilot_bridge;

class ScriptedPrimitiveReader final : public XPilotPrivatePrimitiveReader {
public:
    XPilotPrivateSourceIdentity source{
        true, true, true, true, 7, 11, "org.vatsim.xpilot", "3.0.2",
        "56AEC8CADC0CB36AAF36DC122C330FB782C76FC80190391B8D4A1F717602A8AF"};
    XPilotPrivateSourceIdentity sourceAfter = source;
    XPilotPrivateCapabilities capabilities{
        true, true, true, XPilotPrivateDataKind::Integer,
        XPilotPrivateDataKind::Bytes, XPilotPrivateDataKind::Bytes};
    XPilotPrivateCapabilities capabilitiesAfter = capabilities;
    XPilotPrivateSessionEvidence session{
        XPilotPrivateConnectionStatus::Connected, "N100PC"};
    XPilotPrivateSessionEvidence sessionAfter = session;
    std::array<std::int64_t, 2> sequences{1, 1};
    std::string sender = "KDFW_DEL";
    std::string body =
        "N100PC CLEARED TO KSAN VIA ROUTE CLIMB 5000 SQUAWK 1234";
    bool sequenceReadSucceeds = true;
    bool senderReadSucceeds = true;
    bool bodyReadSucceeds = true;
    std::size_t sourceReads = 0;
    std::size_t capabilityReads = 0;
    std::size_t sessionReads = 0;
    std::size_t sequenceReads = 0;
    std::size_t senderReads = 0;
    std::size_t bodyReads = 0;
    std::vector<std::string> callOrder;

    XPilotPrivateSourceIdentity ReadSourceIdentity() override {
        callOrder.push_back("source");
        return sourceReads++ == 0 ? source : sourceAfter;
    }
    XPilotPrivateCapabilities ReadCapabilities() override {
        callOrder.push_back("capability");
        return capabilityReads++ == 0 ? capabilities : capabilitiesAfter;
    }
    XPilotPrivateSessionEvidence ReadSessionEvidence() override {
        callOrder.push_back("session");
        return sessionReads++ == 0 ? session : sessionAfter;
    }
    bool ReadSequence(std::int64_t* value) override {
        callOrder.push_back("sequence");
        if (!sequenceReadSucceeds || value == nullptr) return false;
        *value = sequences[sequenceReads < sequences.size() ? sequenceReads : 1];
        ++sequenceReads;
        return true;
    }
    bool ReadSender(std::string* value) override {
        callOrder.push_back("sender");
        ++senderReads;
        if (!senderReadSucceeds || value == nullptr) return false;
        *value = sender;
        return true;
    }
    bool ReadBody(std::string* value) override {
        callOrder.push_back("body");
        ++bodyReads;
        if (!bodyReadSucceeds || value == nullptr) return false;
        *value = body;
        return true;
    }
};

bool HasIssue(const XPilotPrivateCombinedObservation& value,
              XPilotPrivateMechanicalIssue issue) {
    return (value.mechanicalIssueMask & XPilotPrivateIssueBit(issue)) != 0;
}

int ProbeIndex(const std::string& name) {
    constexpr std::string_view prefix = "v2_step6_pdc_private_messages_";
    if (name.rfind(prefix.data(), 0) != 0) return -1;
    const auto suffix = std::string_view(name).substr(prefix.size());
    for (std::size_t index = 0; index < kStep6ProbeMetadata.size(); ++index) {
        if (kStep6ProbeMetadata[index].scenarioSuffix == suffix) {
            return static_cast<int>(index + 1);
        }
    }
    return -1;
}

bool MechanicalProbe(int index, std::string* reason) {
    ScriptedPrimitiveReader reader;
    if (index == 2) reader.capabilities.sequenceKind = XPilotPrivateDataKind::Bytes;
    if (index == 4) reader.sequences = {1, 2};
    if (index == 5) {
        reader.session.status = XPilotPrivateConnectionStatus::Disconnected;
        reader.sessionAfter.status = XPilotPrivateConnectionStatus::Connected;
    }
    if (index == 8) {
        reader.source.binaryFingerprintQualified = false;
        reader.sourceAfter.binaryFingerprintQualified = false;
    }
    XPilotPrivateObservationRequest request;
    if (index == 7) request.fastPathToken = {true, 7, 11, 1};
    const auto value = SampleXPilotPrivateMessageCombined(&reader, request);
    bool pass = false;
    switch (index) {
        case 1: pass = value.sourceQualified && value.capabilitiesQualified &&
            value.tupleMechanicallyComplete; break;
        case 2: pass = !value.tupleMechanicallyComplete && HasIssue(
            value, XPilotPrivateMechanicalIssue::SequenceTypeMismatch); break;
        case 3: pass = value.tupleMechanicallyComplete && value.senderBodyRead &&
            reader.callOrder == std::vector<std::string>{"source", "capability",
                "session", "sequence", "sender", "body", "sequence",
                "session", "capability", "source"}; break;
        case 4: pass = !value.tupleMechanicallyComplete && HasIssue(
            value, XPilotPrivateMechanicalIssue::SequenceTorn); break;
        case 5: pass = HasIssue(value, XPilotPrivateMechanicalIssue::StatusChanged); break;
        case 6: {
            const auto rejectedWithoutDisposition = [](ScriptedPrimitiveReader* input) {
                const auto sampled = SampleXPilotPrivateMessageCombined(input, {});
                auto state = std::make_unique<BrainOwnedRuntimeState>();
                InitializeBrainOwnedPdcRuntime(state.get());
                BrainPdcEvaluationContext context;
                context.flightContextActive = true;
                context.ifrMode = true;
                context.workflowStage = WorkflowStage::Departure;
                context.aircraftCallsign = "N100PC";
                context.departureIcao = "KDFW";
                context.destinationIcao = "KSAN";
                context.rosterAvailable = true;
                context.rosterStale = false;
                context.rosterComplete = true;
                context.senders.push_back({"KDFW_DEL",
                    BrainPdcSenderEvidenceKind::Controller,
                    StationRole::Delivery, true});
                const auto decision = EvaluateBrainOwnedPdcObservation(
                    state.get(), ToBrainPdcMechanicalObservation(sampled, 1'000),
                    context, 1'000);
                return !sampled.tupleMechanicallyComplete &&
                    !decision.mechanicallyAccepted &&
                    !state->pdc.hasConnectedDisposition &&
                    !state->pdc.capturedArtifact.has_value() &&
                    state->pdc.retainedBytes == 0;
            };

            ScriptedPrimitiveReader emptyBody;
            emptyBody.body.clear();
            if (!rejectedWithoutDisposition(&emptyBody)) {
                if (reason != nullptr) *reason =
                    "empty body was captured or dispositioned";
                return false;
            }
            ScriptedPrimitiveReader failedBodyRead;
            failedBodyRead.bodyReadSucceeds = false;
            if (!rejectedWithoutDisposition(&failedBodyRead)) {
                if (reason != nullptr) *reason =
                    "failed body read was captured or dispositioned";
                return false;
            }
            ScriptedPrimitiveReader invalidUtf8;
            invalidUtf8.body = std::string("BAD") + static_cast<char>(0xC3) + '(';
            if (!rejectedWithoutDisposition(&invalidUtf8)) {
                if (reason != nullptr) *reason =
                    "invalid UTF-8 body was captured or dispositioned";
                return false;
            }
            ScriptedPrimitiveReader unsafeControl;
            unsafeControl.body = std::string("BAD") + static_cast<char>(0x01);
            if (!rejectedWithoutDisposition(&unsafeControl)) {
                if (reason != nullptr) *reason =
                    "unsafe control body was captured or dispositioned";
                return false;
            }
            ScriptedPrimitiveReader oversizedBody;
            oversizedBody.body.assign(4097, 'B');
            if (!rejectedWithoutDisposition(&oversizedBody)) {
                if (reason != nullptr) *reason =
                    "oversized body was captured or dispositioned";
                return false;
            }
            ScriptedPrimitiveReader oversizedSender;
            oversizedSender.sender.assign(65, 'S');
            if (!rejectedWithoutDisposition(&oversizedSender)) {
                if (reason != nullptr) *reason =
                    "oversized sender was captured or dispositioned";
                return false;
            }

            ScriptedPrimitiveReader emptySender;
            emptySender.sender.clear();
            const auto sampled = SampleXPilotPrivateMessageCombined(&emptySender, {});
            auto state = std::make_unique<BrainOwnedRuntimeState>();
            InitializeBrainOwnedPdcRuntime(state.get());
            BrainPdcEvaluationContext context;
            context.flightContextActive = true;
            context.ifrMode = true;
            context.workflowStage = WorkflowStage::Departure;
            context.aircraftCallsign = "N100PC";
            context.departureIcao = "KDFW";
            context.destinationIcao = "KSAN";
            const auto decision = EvaluateBrainOwnedPdcObservation(
                state.get(), ToBrainPdcMechanicalObservation(sampled, 1'000),
                context, 1'000);
            pass = sampled.tupleMechanicallyComplete &&
                !HasIssue(sampled, XPilotPrivateMechanicalIssue::SenderEmpty) &&
                decision.captureCompleted && state->pdc.captureComplete &&
                state->pdc.capturedArtifact.has_value() &&
                state->pdc.capturedArtifact->sender.empty();
            if (!pass && reason != nullptr) {
                *reason = "empty sender did not remain mechanically eligible";
            }
            return pass;
        }
        case 7: {
            ScriptedPrimitiveReader zero;
            zero.sequences = {0, 0};
            const auto sampled = SampleXPilotPrivateMessageCombined(&zero, {});
            auto state = std::make_unique<BrainOwnedRuntimeState>();
            InitializeBrainOwnedPdcRuntime(state.get());
            BrainPdcEvaluationContext context;
            context.flightContextActive = true;
            context.ifrMode = true;
            context.workflowStage = WorkflowStage::Departure;
            context.aircraftCallsign = "N100PC";
            context.departureIcao = "KDFW";
            context.destinationIcao = "KSAN";
            context.rosterAvailable = true;
            context.rosterStale = false;
            context.rosterComplete = true;
            const auto decision = EvaluateBrainOwnedPdcObservation(
                state.get(), ToBrainPdcMechanicalObservation(sampled, 1'000),
                context, 1'000);
            pass = sampled.tupleMechanicallyComplete &&
                !sampled.senderBodyRead && zero.senderReads == 0 &&
                zero.bodyReads == 0 && decision.mechanicallyAccepted &&
                !decision.captureCompleted && !state->pdc.hasConnectedDisposition &&
                !state->pdc.capturedArtifact.has_value();
            break;
        }
        case 8: pass = !value.sourceQualified && HasIssue(
            value, XPilotPrivateMechanicalIssue::BinaryFingerprintMismatch); break;
        default: break;
    }
    if (!pass && reason != nullptr) *reason = "mechanical sampler contract failed";
    return pass;
}

BrainPdcMechanicalObservation Observation(
    std::int64_t sequence, BrainPdcSourceConnectionStatus status,
    std::string sender = "KDFW_DEL",
    std::string body =
        "N100PC CLEARED TO KSAN VIA ROUTE CLIMB 5000 SQUAWK 1234") {
    BrainPdcMechanicalObservation value;
    value.sourceQualified = true;
    value.capabilitiesQualified = true;
    value.tupleMechanicallyComplete = true;
    value.senderBodyRead = sequence > 0;
    value.pluginInstanceIdentity = 7;
    value.capabilityGeneration = 11;
    value.sequenceBefore = sequence;
    value.sequenceAfter = sequence;
    value.statusBefore = status;
    value.statusAfter = status;
    value.callsignBefore = status == BrainPdcSourceConnectionStatus::Connected
        ? "N100PC" : "";
    value.callsignAfter = value.callsignBefore;
    value.sender = std::move(sender);
    value.body = std::move(body);
    return value;
}

struct Step6Fixture {
    std::unique_ptr<BrainOwnedRuntimeState> state =
        std::make_unique<BrainOwnedRuntimeState>();
    BrainPdcEvaluationContext context;
    AccessoryPresentationState presenter;
    AccessoryVisiblePublicationState visibility;
    std::unique_ptr<AccessoryPublicationFactQueue> publications =
        std::make_unique<AccessoryPublicationFactQueue>();
    std::uint64_t nowUs = 1'000;
    std::uint64_t clickSequence = 0;

    Step6Fixture() {
        InitializeBrainOwnedPdcRuntime(state.get());
        context.flightContextActive = true;
        context.ifrMode = true;
        context.workflowStage = WorkflowStage::Departure;
        context.aircraftCallsign = "N100PC";
        context.departureIcao = "KDFW";
        context.destinationIcao = "KSAN";
        context.rosterAvailable = true;
        context.rosterStale = false;
        context.rosterComplete = true;
        context.senders.push_back({"KDFW_DEL",
            BrainPdcSenderEvidenceKind::Controller, StationRole::Delivery, true});
    }
    BrainPdcObservationDecision Submit(BrainPdcMechanicalObservation value) {
        value.observedMonotonicMicroseconds = nowUs;
        const auto decision = EvaluateBrainOwnedPdcObservation(
            state.get(), value, context, nowUs);
        nowUs += 1'000;
        return decision;
    }
    void EnsurePdcSelected() {
        if (state->accessory.activeDrawer == BrainOwnedAccessoryDrawerId::Pdc) return;
        ++clickSequence;
        const auto entered = nowUs++;
        const auto exited = nowUs++;
        (void)RequestBrainOwnedAccessoryDrawerSelection(state.get(),
            {BrainOwnedAccessoryDrawerId::Pdc, clickSequence, entered, entered, exited});
    }
};

struct SeamResult {
    bool passed = false;
    BrainOwnedAccessoryPresentationHandle command;
    AccessoryDrawerRenderPlan render;
    BrainOwnedAccessoryPublicationDecision publication;
    std::vector<std::string> visibleIdentities;
};

SeamResult PublishPdc(Step6Fixture* fixture, std::string* reason) {
    SeamResult result;
    if (fixture == nullptr) return result;
    fixture->EnsurePdcSelected();
    BrainOwnedAccessoryProjectionCounters projection;
    result.command = ProjectBrainOwnedAccessoryPresentation(
        fixture->state.get(), &projection);
    if (result.command.snapshot == nullptr ||
        result.command.snapshot->activeDrawer != BrainOwnedAccessoryDrawerId::Pdc) {
        if (reason != nullptr) *reason = "Brain command projection failed";
        return result;
    }
    auto* measurement = InitializeAccessoryTextMeasurement();
    if (measurement == nullptr) {
        if (reason != nullptr) *reason = "production text measurement unavailable";
        return result;
    }
    const auto typography = PrepareAccessoryTypography(measurement, 1.0f);
    AccessoryLayoutInput layoutInput;
    layoutInput.screenWidth = 1280;
    layoutInput.screenHeight = 900;
    layoutInput.windowTop = 900;
    layoutInput.scale = 1.0f;
    layoutInput.cardAnimationProgress = 1.0f;
    layoutInput.drawerOpen = true;
    layoutInput.typography = &typography;
    const auto layout = ResolveAccessoryLayout(layoutInput);
    AccessoryPreparationWorker worker;
    if (!worker.Start(1)) {
        ShutdownAccessoryTextMeasurement(measurement);
        if (reason != nullptr) *reason = "production preparation worker failed to start";
        return result;
    }
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(2);
    while (worker.State() == AccessoryPreparationWorkerState::Starting &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    AccessoryPreparationKeyInput key;
    key.drawer = BrainOwnedAccessoryDrawerId::Pdc;
    key.layoutGeneration = 1;
    key.commandIdentity = result.command.snapshot->commandIdentity;
    key.lifecycleEpoch = result.command.snapshot->lifecycleEpoch;
    key.selectedDrawerContentRevision =
        result.command.snapshot->selectedDrawerContentRevision;
    key.typographyGeneration = typography.generation;
    key.scaleThousandths = 1000;
    key.contentWidth = (std::max)(1, layout.drawerBounds.right -
        layout.drawerBounds.left - 2 * layout.drawerContentInset);
    key.visibleLineCapacity = layout.drawerVisibleLineCapacity;
    AccessoryPreparationRequest request;
    request.key = BuildAccessoryPreparationKeyForCommand(key);
    request.snapshot = result.command.snapshot;
    request.layout = layout;
    request.requestedMicroseconds = fixture->nowUs++;
    bool requestAccepted = false;
    const auto requestDeadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(2);
    do {
        requestAccepted = worker.Request(request);
        if (!requestAccepted) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    } while (!requestAccepted &&
             std::chrono::steady_clock::now() < requestDeadline);
    if (!requestAccepted) {
        ShutdownAccessoryTextMeasurement(measurement);
        if (reason != nullptr) *reason = "production preparation request failed";
        return result;
    }
    std::shared_ptr<const AccessoryPreparedDrawerPlan> plan;
    while (plan == nullptr && std::chrono::steady_clock::now() < deadline) {
        plan = worker.TryTakeReady(request.key, nullptr);
        if (plan == nullptr) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    AccessoryPresentationUpdateInput update;
    update.presentation = result.command;
    update.layout = layout;
    update.mainCardProductionSignature = "step6-pdc-production-seam";
    update.measurementContext = measurement;
    update.preparedPlan = plan;
    update.mechanicalLayoutGeneration = 1;
    const auto committed = UpdateAccessoryPresentation(&fixture->presenter, update);
    result.render = BuildAccessoryDrawerRenderPlan(fixture->presenter, layout);
    result.visibleIdentities = CollectVisiblePdcRevisionIdentities(result.render);
    AccessoryVisiblePublicationKey visibleKey;
    visibleKey.commandIdentity = result.command.snapshot->commandIdentity;
    visibleKey.lifecycleEpoch = result.command.snapshot->lifecycleEpoch;
    visibleKey.railRevision = result.command.snapshot->railPresentationRevision;
    visibleKey.drawerRevision = result.command.snapshot->selectedDrawerContentRevision;
    visibleKey.drawerOpen = true;
    const auto began = fixture->visibility.Observe({visibleKey, true, true,
        committed.delta.uploadRequests != 0, fixture->nowUs++, true});
    const auto terminal = fixture->visibility.CompleteFirstFrame(
        visibleKey, true, true, fixture->nowUs++);
    BrainOwnedAccessoryPublicationFact fact;
    ApplyAccessoryVisiblePublicationTerminal(terminal, &fact);
    fact.activeDrawerRendered = BrainOwnedAccessoryDrawerId::Pdc;
    fact.pdcVisibleRevisionIdentities = result.visibleIdentities;
    fact.originatingClickSequence = result.command.snapshot->originatingClickSequence;
    fact.clickTimingApplicable = fact.originatingClickSequence != 0;
    fact.clickToTerminalMicroseconds = 100;
    fact.commandElapsedMicroseconds = 100;
    const bool queued = fixture->publications->Produce(fact);
    BrainOwnedAccessoryPublicationFact delivered;
    const bool consumed = fixture->publications->Consume(&delivered);
    if (consumed) result.publication = ConsumeBrainOwnedAccessoryPublicationFact(
        fixture->state.get(), delivered);
    ShutdownAccessoryTextMeasurement(measurement);
    result.passed = plan != nullptr && !committed.preparationPending &&
        committed.publishedSnapshotCount == 1 &&
        result.render.status == BrainOwnedAccessoryOperationStatus::Available &&
        began.started && terminal.terminal && queued && consumed &&
        result.publication.consumed;
    if (!result.passed && reason != nullptr) {
        *reason = "production preparation/render/publication seam failed";
    }
    return result;
}

bool Check(bool value, const char* message, std::string* reason) {
    if (!value && reason != nullptr) *reason = message;
    return value;
}

XPilotPrivatePlatformPrimitives QualifiedQualificationPrimitives() {
    XPilotPrivatePlatformPrimitives value;
    value.pluginPresent = true;
    value.pluginInstanceIdentity = 7;
    value.capabilityGeneration = 11;
    value.signature = "org.vatsim.xpilot";
    value.pluginPath = "synthetic-exact-xpilot.xpl";
    value.xplaneSystemPath = "synthetic-xplane-root";
    value.versionDataRefPresent = true;
    value.versionDataKind = XPilotPrivateDataKind::Bytes;
    value.versionValue = "3.0.2";
    value.sequencePresent = true;
    value.senderPresent = true;
    value.bodyPresent = true;
    value.sequenceKind = XPilotPrivateDataKind::Integer;
    value.senderKind = XPilotPrivateDataKind::Bytes;
    value.bodyKind = XPilotPrivateDataKind::Bytes;
    return value;
}

XPilotPrivateFileInspection ExactQualificationFile() {
    XPilotPrivateFileInspection value;
    value.pathPresent = true;
    value.canonicalizationSucceeded = true;
    value.fileReadable = true;
    value.regularFile = true;
    value.actualSize = 4'990'976;
    value.sha256 =
        "56AEC8CADC0CB36AAF36DC122C330FB782C76FC80190391B8D4A1F717602A8AF";
    value.canonicalPath = "synthetic-canonical-xpilot.xpl";
    value.hashAttempts = 1;
    return value;
}

XPilotPrivateCombinedObservation SampleFromQualification(
    const XPilotPrivateQualificationResult& qualification,
    std::int64_t sequence = 1,
    std::string sender = "SYNTH_DEL",
    std::string body = "SYNTHETIC CLEARED ROUTE CLIMB 5000 SQUAWK 1234") {
    ScriptedPrimitiveReader reader;
    reader.source = qualification.source;
    reader.sourceAfter = qualification.source;
    reader.capabilities = qualification.capabilities;
    reader.capabilitiesAfter = qualification.capabilities;
    reader.sequences = {sequence, sequence};
    reader.sender = std::move(sender);
    reader.body = std::move(body);
    return SampleXPilotPrivateMessageCombined(&reader, {});
}

bool QualificationProbe(int index, std::string* reason) {
    auto primitives = QualifiedQualificationPrimitives();
    auto file = ExactQualificationFile();
    switch (index) {
        case 50: {
            file.sha256.assign(64, '0');
            const auto qualification =
                EvaluateXPilotPrivateQualification(primitives, file);
            const auto sampled = SampleFromQualification(
                qualification, 7, "PRIVATE_SENDER", "PRIVATE_BODY_TOKEN");
            auto state = std::make_unique<BrainOwnedRuntimeState>();
            InitializeBrainOwnedPdcRuntime(state.get());
            BrainPdcEvaluationContext context;
            const auto decision = EvaluateBrainOwnedPdcObservation(state.get(),
                ToBrainPdcMechanicalObservation(sampled, 1'000), context, 1'000);
            const auto event = FormatXPilotPrivateQualificationEvent(
                sampled, state->pdc.lastObservedSequence,
                state->pdc.connectedDispositionedSequence);
            return Check(sampled.sequenceBefore == 7 && sampled.sequenceAfter == 7 &&
                sampled.senderByteCount != 0 && sampled.bodyByteCount != 0 &&
                !decision.mechanicallyAccepted &&
                state->pdc.lastObservedSequence == 0 &&
                state->pdc.connectedDispositionedSequence == 0 &&
                event.find("rawSequenceBefore=7") != std::string::npos &&
                event.find("brainObservedSequence=0") != std::string::npos &&
                event.find("PRIVATE_SENDER") == std::string::npos &&
                event.find("PRIVATE_BODY_TOKEN") == std::string::npos,
                "raw and Brain sequence domains were not serialized truthfully",
                reason);
        }
        case 51: {
            constexpr std::array<std::uint64_t, 29> bits{{
                1ULL << 0U, 1ULL << 1U, 1ULL << 2U, 1ULL << 3U,
                1ULL << 4U, 1ULL << 5U, 1ULL << 6U, 1ULL << 7U,
                1ULL << 8U, 1ULL << 9U, 1ULL << 10U, 1ULL << 11U,
                1ULL << 12U, 1ULL << 13U, 1ULL << 14U, 1ULL << 15U,
                1ULL << 16U, 1ULL << 17U, 1ULL << 18U, 1ULL << 19U,
                1ULL << 20U, 1ULL << 21U, 1ULL << 22U, 1ULL << 23U,
                1ULL << 24U, 1ULL << 25U, 1ULL << 26U, 1ULL << 27U,
                1ULL << 28U}};
            for (const auto bit : bits) {
                const auto names = XPilotPrivateMechanicalIssueNames(bit);
                if (names.empty() || names == "none" || names == "unknown") {
                    return Check(false, "mechanical issue name was not stable", reason);
                }
            }
            auto qualification =
                EvaluateXPilotPrivateQualification(primitives, file);
            auto sampled = SampleFromQualification(qualification);
            sampled.mechanicalIssueMask = 1ULL << 3U;
            const auto event = FormatXPilotPrivateQualificationEvent(sampled, 0, 0);
            return Check(event.find("fatalMask=0x8") != std::string::npos &&
                event.find("binary-fingerprint-mismatch") != std::string::npos &&
                event.find("SYNTH_DEL") == std::string::npos &&
                event.find("SYNTHETIC CLEARED") == std::string::npos,
                "qualification issue serialization was incomplete or unsafe", reason);
        }
        case 52: {
            const auto runtime =
                EvaluateXPilotPrivateQualification(primitives, file);
            const auto preflight =
                EvaluateXPilotPrivateQualification(primitives, file);
            return Check(runtime.fatalIssueMask == preflight.fatalIssueMask &&
                runtime.advisoryIssueMask == preflight.advisoryIssueMask &&
                runtime.source.binaryFingerprintQualified ==
                    preflight.source.binaryFingerprintQualified &&
                runtime.capabilityFatalIssueMask ==
                    preflight.capabilityFatalIssueMask,
                "runtime and preflight policy results diverged", reason);
        }
        case 53: {
            std::array<XPilotPrivatePlatformPrimitives, 4> variants{
                primitives, primitives, primitives, primitives};
            variants[0].versionDataRefPresent = false;
            variants[0].versionDataKind = XPilotPrivateDataKind::Unknown;
            variants[0].versionValue.clear();
            variants[1].versionValue.clear();
            variants[2].versionDataKind = XPilotPrivateDataKind::Unknown;
            variants[3].versionValue = "xPilot release 3.0.2";
            for (const auto& variant : variants) {
                const auto value = EvaluateXPilotPrivateQualification(variant, file);
                if (!value.source.binaryFingerprintQualified ||
                    value.sourceFatalIssueMask != 0 ||
                    value.advisoryIssueMask == 0) {
                    return Check(false,
                        "supplementary version metadata vetoed exact fingerprint",
                        reason);
                }
            }
            return true;
        }
        case 54: {
            auto wrongSize = file;
            --wrongSize.actualSize;
            auto wrongHash = file;
            wrongHash.sha256.assign(64, 'F');
            const auto sizeResult =
                EvaluateXPilotPrivateQualification(primitives, wrongSize);
            const auto hashResult =
                EvaluateXPilotPrivateQualification(primitives, wrongHash);
            return Check(!sizeResult.source.binaryFingerprintQualified &&
                !hashResult.source.binaryFingerprintQualified &&
                (sizeResult.sourceFatalIssueMask & XPilotPrivateIssueBit(
                    XPilotPrivateMechanicalIssue::BinarySizeMismatch)) != 0 &&
                (hashResult.sourceFatalIssueMask & XPilotPrivateIssueBit(
                    XPilotPrivateMechanicalIssue::BinaryFingerprintMismatch)) != 0,
                "wrong binary fingerprint did not fail closed", reason);
        }
        case 55: {
            const auto inspected = InspectXPilotPrivatePluginFile(
                "C:/X-Plane 12/Resources/plugins/xPilot/win_x64/xPilot.xpl", "");
            return Check(inspected.pathPresent && inspected.canonicalizationSucceeded &&
                inspected.fileReadable && inspected.regularFile &&
                inspected.actualSize == 4'990'976 && inspected.hashAttempts == 1 &&
                inspected.sha256 ==
                    "56AEC8CADC0CB36AAF36DC122C330FB782C76FC80190391B8D4A1F717602A8AF",
                "absolute plugin path did not qualify exact target once", reason);
        }
        case 56: {
            const auto absolute = InspectXPilotPrivatePluginFile(
                "C:/X-Plane 12/Resources/plugins/xPilot/win_x64/xPilot.xpl", "");
            const auto relative = InspectXPilotPrivatePluginFile(
                "Resources/plugins/xPilot/win_x64/xPilot.xpl", "C:/X-Plane 12");
            return Check(relative.canonicalizationSucceeded && relative.fileReadable &&
                relative.hashAttempts == 1 &&
                std::filesystem::equivalent(
                    std::filesystem::u8path(absolute.canonicalPath),
                    std::filesystem::u8path(relative.canonicalPath)) &&
                absolute.actualSize == relative.actualSize &&
                absolute.sha256 == relative.sha256,
                "relative plugin path did not resolve to exact X-Plane target", reason);
        }
        case 57: {
            const auto missing = InspectXPilotPrivatePluginFile(
                "Resources/plugins/xPilot/win_x64/does-not-exist.xpl",
                "C:/X-Plane 12");
            const auto directory = InspectXPilotPrivatePluginFile(
                "C:/X-Plane 12/Resources/plugins/xPilot/win_x64", "");
            auto unreadable = file;
            unreadable.fileReadable = false;
            const auto missingResult =
                EvaluateXPilotPrivateQualification(primitives, missing);
            const auto directoryResult =
                EvaluateXPilotPrivateQualification(primitives, directory);
            const auto unreadableResult =
                EvaluateXPilotPrivateQualification(primitives, unreadable);
            return Check(missing.hashAttempts == 0 && directory.hashAttempts == 0 &&
                (missingResult.sourceFatalIssueMask & XPilotPrivateIssueBit(
                    XPilotPrivateMechanicalIssue::PluginPathCanonicalizationFailed)) != 0 &&
                (directoryResult.sourceFatalIssueMask & XPilotPrivateIssueBit(
                    XPilotPrivateMechanicalIssue::PluginTargetNotFile)) != 0 &&
                (unreadableResult.sourceFatalIssueMask & XPilotPrivateIssueBit(
                    XPilotPrivateMechanicalIssue::PluginFileUnreadable)) != 0,
                "file failure matrix did not return bounded fatal reasons", reason);
        }
        case 58: {
            struct Variant { int field; bool missing; XPilotPrivateMechanicalIssue issue; };
            constexpr std::array<Variant, 6> variants{{
                {0, true, XPilotPrivateMechanicalIssue::CapabilityMissing},
                {1, true, XPilotPrivateMechanicalIssue::CapabilityMissing},
                {2, true, XPilotPrivateMechanicalIssue::CapabilityMissing},
                {0, false, XPilotPrivateMechanicalIssue::SequenceTypeMismatch},
                {1, false, XPilotPrivateMechanicalIssue::SenderTypeMismatch},
                {2, false, XPilotPrivateMechanicalIssue::BodyTypeMismatch}}};
            for (const auto& variant : variants) {
                auto changed = primitives;
                if (variant.field == 0) {
                    changed.sequencePresent = !variant.missing;
                    changed.sequenceKind = variant.missing
                        ? XPilotPrivateDataKind::Integer : XPilotPrivateDataKind::Bytes;
                } else if (variant.field == 1) {
                    changed.senderPresent = !variant.missing;
                    changed.senderKind = variant.missing
                        ? XPilotPrivateDataKind::Bytes : XPilotPrivateDataKind::Integer;
                } else {
                    changed.bodyPresent = !variant.missing;
                    changed.bodyKind = variant.missing
                        ? XPilotPrivateDataKind::Bytes : XPilotPrivateDataKind::Integer;
                }
                const auto value = EvaluateXPilotPrivateQualification(changed, file);
                if ((value.capabilityFatalIssueMask &
                     XPilotPrivateIssueBit(variant.issue)) == 0) {
                    return Check(false, "capability failure bit was not independent", reason);
                }
            }
            return true;
        }
        case 59: {
            const auto qualification =
                EvaluateXPilotPrivateQualification(primitives, file);
            ScriptedPrimitiveReader reader;
            reader.source = qualification.source;
            reader.sourceAfter = qualification.source;
            reader.sourceAfter.canonicalPluginPath = "changed-path.xpl";
            reader.capabilities = qualification.capabilities;
            reader.capabilitiesAfter = qualification.capabilities;
            const auto sampled = SampleXPilotPrivateMessageCombined(&reader, {});
            return Check(!sampled.tupleMechanicallyComplete && HasIssue(
                sampled, XPilotPrivateMechanicalIssue::SourceChanged),
                "source snapshot change was not rejected", reason);
        }
        case 60: {
            primitives.versionDataRefPresent = false;
            primitives.versionDataKind = XPilotPrivateDataKind::Unknown;
            primitives.versionValue.clear();
            const auto qualification =
                EvaluateXPilotPrivateQualification(primitives, file);
            const auto sampled = SampleFromQualification(qualification, 1,
                "ACARS", "WAITING MESSAGE ALREADY PRESENT AT STARTUP");
            XPilotPrivateObservationQueue queue;
            const auto produced = queue.Produce(
                ToBrainPdcMechanicalObservation(sampled, 1'000));
            BrainPdcMechanicalObservation delivered;
            const auto consumed = queue.Consume(&delivered);
            auto fixture = std::make_unique<Step6Fixture>();
            fixture->context.rosterAvailable = false;
            fixture->context.rosterStale = true;
            fixture->context.rosterComplete = false;
            fixture->context.senders.clear();
            const auto decision = consumed ? fixture->Submit(delivered)
                                           : BrainPdcObservationDecision{};
            return Check(produced && consumed && decision.mechanicallyAccepted &&
                decision.captureCompleted && decision.admittedCount == 1 &&
                fixture->state->pdc.captureComplete &&
                fixture->state->pdc.capturedArtifact.has_value() &&
                fixture->state->pdc.capturedArtifact->sender == "ACARS" &&
                fixture->state->pdc.capturedArtifact->body ==
                    "WAITING MESSAGE ALREADY PRESENT AT STARTUP" &&
                queue.Counters().produced == 1 && queue.Counters().consumed == 1 &&
                queue.Pending() == 0,
                "positive startup tuple did not bridge exactly once", reason);
        }
        case 61: {
            file.sha256.assign(64, '0');
            const auto qualification =
                EvaluateXPilotPrivateQualification(primitives, file);
            auto sampled = SampleFromQualification(
                qualification, 1, "SECRET_SENDER", "PRIVATE_TEXT_TOKEN");
            XPilotPrivateQualificationEventLatch latch;
            const bool first = latch.Observe(sampled);
            const bool repeated = latch.Observe(sampled);
            sampled.sourceAfter.capabilityGeneration = 12;
            sampled.sourceBefore.capabilityGeneration = 12;
            const bool transition = latch.Observe(sampled);
            const auto event = FormatXPilotPrivateQualificationEvent(sampled, 0, 0);
            return Check(first && !repeated && transition && latch.Emitted() == 2 &&
                event.find("SECRET_SENDER") == std::string::npos &&
                event.find("PRIVATE_TEXT_TOKEN") == std::string::npos,
                "qualification event was not latched or private-safe", reason);
        }
        default: return Check(false, "unmapped qualification probe", reason);
    }
}

bool ProductProbe(int index, std::string* reason) {
    if (index == 44) {
        auto state = std::make_unique<BrainOwnedRuntimeState>();
        (void)RequestBrainOwnedAccessoryDrawerSelection(state.get(),
            {BrainOwnedAccessoryDrawerId::Pdc, 1, 1, 1, 2});
        const auto command = ProjectBrainOwnedAccessoryPresentation(state.get(), nullptr);
        return Check(command.snapshot != nullptr && command.snapshot->emptyStateText ==
            "PDC/private-message data is not enabled in Step 3.",
            "uninitialized compatibility placeholder changed", reason);
    }
    auto fixture = std::make_unique<Step6Fixture>();
    auto& state = *fixture->state;
    const auto connected1 = Observation(1, BrainPdcSourceConnectionStatus::Connected);

    switch (index) {
        case 9: {
            const auto discarded = fixture->Submit(Observation(
                1, BrainPdcSourceConnectionStatus::Disconnected));
            const auto accepted = fixture->Submit(connected1);
            return Check(!discarded.provisionalRetained && accepted.captureCompleted &&
                state.pdc.capturedArtifact.has_value() &&
                state.pdc.capturedArtifact->body == connected1.body &&
                state.pdc.preCaptureUncertain && state.pdc.retainedBytes != 0,
                "already-waiting stable strict PDC was not captured exactly once",
                reason);
        }
        case 10: {
            auto transitional = connected1;
            transitional.statusBefore = BrainPdcSourceConnectionStatus::Disconnected;
            const auto discarded = fixture->Submit(transitional);
            const auto accepted = fixture->Submit(connected1);
            return Check(!discarded.provisionalRetained && accepted.captureCompleted &&
                state.pdc.capturedArtifact.has_value() &&
                state.pdc.counters.provisionalRetained == 0,
                "transitional candidate was retained or stable PDC was not captured",
                reason);
        }
        case 11: {
            const auto discarded = fixture->Submit(Observation(
                1, BrainPdcSourceConnectionStatus::Disconnected));
            return Check(!discarded.provisionalRetained &&
                !state.pdc.capturedArtifact.has_value() &&
                state.pdc.retainedBytes == 0 &&
                state.pdc.connectedDispositionedSequence == 0,
                "cold disconnected content was retained", reason);
        }
        case 12: {
            const auto accepted = fixture->Submit(connected1);
            return Check(accepted.captureCompleted &&
                state.pdc.capturedArtifact.has_value() &&
                state.pdc.capturedArtifact->sourceSequence == 1,
                "cold connected first positive was primed away", reason);
        }
        case 13: {
            fixture->Submit(connected1);
            const auto revision = state.pdc.capturedArtifact->revisionIdentity;
            const auto duplicate = fixture->Submit(connected1);
            fixture->Submit(Observation(1, BrainPdcSourceConnectionStatus::Disconnected));
            const auto reconnect = fixture->Submit(connected1);
            const auto acquisition = EvaluateBrainOwnedPdcAcquisition(
                fixture->state.get(), fixture->context);
            return Check(!duplicate.evaluated && !reconnect.evaluated &&
                acquisition.captureComplete &&
                !acquisition.samplePrivatePayload &&
                state.pdc.capturedArtifact.has_value() &&
                state.pdc.capturedArtifact->revisionIdentity == revision &&
                state.pdc.counters.postCaptureEarlyNoops == 3,
                "temporary disconnect/reconnect altered the captured artifact", reason);
        }
        case 14:
        case 15: {
            const int count = index == 14 ? 2 : 3;
            for (int sequence = 1; sequence <= count; ++sequence) {
                fixture->Submit(Observation(sequence,
                    BrainPdcSourceConnectionStatus::Disconnected, "KDFW_DEL",
                    "MESSAGE " + std::to_string(sequence)));
            }
            const bool noRawHistory = !state.pdc.capturedArtifact.has_value() &&
                state.pdc.retainedBytes == 0 &&
                state.pdc.counters.provisionalRetained == 0;
            const auto admitted = fixture->Submit(Observation(count,
                BrainPdcSourceConnectionStatus::Connected, "KDFW_DEL",
                "N100PC CLEARED TO KSAN VIA ROUTE CLIMB 5000 SQUAWK 1234"));
            return Check(noRawHistory && admitted.captureCompleted &&
                state.pdc.capturedArtifact.has_value() &&
                state.pdc.counters.admittedMessages == 1,
                "disconnected advances retained raw history or produced multiple artifacts",
                reason);
        }
        case 16: {
            fixture->Submit(Observation(1,
                BrainPdcSourceConnectionStatus::Disconnected, "KDFW_DEL",
                "UNSEEN MESSAGE"));
            const auto gap = fixture->Submit(Observation(3,
                BrainPdcSourceConnectionStatus::Connected, "ACARS",
                "WAITING MESSAGE AFTER SEQUENCE DISCONTINUITY"));
            const auto command = ProjectBrainOwnedAccessoryPresentation(
                fixture->state.get(), nullptr);
            const auto pdc = std::find_if(command.snapshot->orbs.begin(),
                command.snapshot->orbs.end(), [](const auto& orb) {
                    return orb.drawer == BrainOwnedAccessoryDrawerId::Pdc;
                });
            return Check(gap.gapDetected && gap.captureCompleted &&
                state.pdc.preCaptureUncertain && state.pdc.captureComplete &&
                state.pdc.counters.gapEvents == 1 &&
                pdc != command.snapshot->orbs.end() &&
                pdc->categoryText == "NEW",
                "discontinuous waiting message did not capture with internal uncertainty",
                reason);
        }
        case 17: {
            fixture->Submit(Observation(5, BrainPdcSourceConnectionStatus::Disconnected,
                "KDFW_DEL", "UNRETAINED DISCONNECTED MESSAGE"));
            const auto epoch = state.pdc.sourceEpoch;
            auto replacement = connected1;
            replacement.pluginInstanceIdentity = 8;
            const auto captured = fixture->Submit(replacement);
            return Check(captured.captureCompleted &&
                state.pdc.sourceEpoch == epoch + 1 &&
                state.pdc.counters.sourceEpochChanges == 1 &&
                state.pdc.preCaptureUncertain &&
                state.pdc.capturedArtifact.has_value() &&
                state.pdc.capturedArtifact->sourceSequence == 1,
                "source replacement did not preserve uncertainty and capture once",
                reason);
        }
        case 18:
        case 47: {
            XPilotPrivateObservationQueue queue;
            for (int sequence = 1; sequence <= 9; ++sequence) {
                if (!queue.Produce(Observation(sequence,
                        BrainPdcSourceConnectionStatus::Connected))) {
                    return Check(false, "ninth retained fact rejected", reason);
                }
            }
            const bool tenth = queue.Produce(Observation(
                10, BrainPdcSourceConnectionStatus::Connected));
            BrainPdcMechanicalObservation fact;
            std::size_t consumed = 0;
            while (queue.Consume(&fact)) ++consumed;
            const bool serviced = queue.ServiceRetained();
            while (queue.Consume(&fact)) ++consumed;
            const auto& counters = queue.Counters();
            return Check(!tenth && serviced && consumed == 9 &&
                counters.produced == 10 && counters.retained == 1 &&
                counters.capacityLoss == 1 && counters.consumed == 9,
                "queue/retained accounting did not reconcile", reason);
        }
        case 19: {
            fixture->Submit(connected1);
            const auto revision = state.pdc.capturedArtifact->revisionIdentity;
            XPilotPrivateObservationQueue queue;
            for (int sequence = 1; sequence <= 9; ++sequence) {
                queue.Produce(Observation(sequence + 1,
                    BrainPdcSourceConnectionStatus::Connected, "KDFW_DEL",
                    "LATER PRIVATE CONTENT " + std::to_string(sequence)));
            }
            BrainPdcMechanicalObservation fact;
            while (queue.Consume(&fact)) fixture->Submit(fact);
            (void)queue.ServiceRetained();
            while (queue.Consume(&fact)) fixture->Submit(fact);
            return Check(state.pdc.capturedArtifact.has_value() &&
                state.pdc.capturedArtifact->revisionIdentity == revision &&
                state.pdc.counters.postCaptureEarlyNoops == 9 &&
                queue.Counters().capacityLoss == 0,
                "later queued private content altered the terminal capture", reason);
        }
        case 20:
        case 21: {
            fixture->context.flightContextActive = false;
            fixture->context.departureIcao.clear();
            fixture->context.rosterAvailable = false;
            fixture->context.rosterStale = true;
            fixture->context.rosterComplete = false;
            const auto waiting = Observation(1,
                BrainPdcSourceConnectionStatus::Connected, "ACARS",
                "WAITING MESSAGE BEFORE FLIGHT BINDING");
            const auto unavailable = fixture->Submit(waiting);
            const bool remainedEligible = !unavailable.evaluated &&
                !state.pdc.capturedArtifact.has_value() &&
                state.pdc.retainedBytes == 0 &&
                !state.pdc.hasConnectedDisposition &&
                state.pdc.connectedDispositionedSequence == 0;
            if (index == 20) return Check(remainedEligible,
                "incomplete binding dispositioned or retained the waiting sequence",
                reason);
            fixture->context.flightContextActive = true;
            fixture->context.departureIcao = "KDFW";
            fixture->context.workflowStage = WorkflowStage::None;
            const auto captured = fixture->Submit(waiting);
            return Check(remainedEligible && captured.captureCompleted &&
                state.pdc.capturedArtifact.has_value() &&
                state.pdc.capturedArtifact->body == waiting.body &&
                state.pdc.hasConnectedDisposition &&
                state.pdc.connectedDispositionedSequence == 1,
                "same waiting sequence did not capture after flight binding became ready",
                reason);
        }
        case 22: {
            fixture->Submit(connected1);
            fixture->EnsurePdcSelected();
            const auto command = ProjectBrainOwnedAccessoryPresentation(
                fixture->state.get(), nullptr);
            return Check(command.snapshot != nullptr &&
                command.snapshot->drawerTitle == "PDC / PRIVATE MESSAGES" &&
                command.snapshot->entries.size() == 1 &&
                command.snapshot->entries[0].body == connected1.body,
                "strict PDC did not atomically project one flight-bound artifact",
                reason);
        }
        case 23: {
            fixture->context.rosterAvailable = false;
            fixture->context.rosterStale = true;
            fixture->context.rosterComplete = false;
            const auto waiting = Observation(1,
                BrainPdcSourceConnectionStatus::Connected, "ACARS",
                "CONTACT GROUND 121.8");
            const auto captured = fixture->Submit(waiting);
            fixture->EnsurePdcSelected();
            const auto command = ProjectBrainOwnedAccessoryPresentation(
                fixture->state.get(), nullptr);
            return Check(captured.captureCompleted && command.snapshot != nullptr &&
                command.snapshot->entries.size() == 1 &&
                command.snapshot->entries[0].body == waiting.body &&
                state.pdc.captureComplete && state.pdc.capturedArtifact.has_value() &&
                state.pdc.capturedArtifact->sender == "ACARS" &&
                BrainOwnedPdcUnreadCount(state.pdc) == 1,
                "ACARS waiting message required sender or body semantics", reason);
        }
        case 24: {
            const auto waiting = Observation(1,
                BrainPdcSourceConnectionStatus::Connected, "SUP123",
                "SYSTEM MAINTENANCE NOTICE");
            const auto captured = fixture->Submit(waiting);
            return Check(captured.captureCompleted &&
                state.pdc.capturedArtifact.has_value() &&
                state.pdc.capturedArtifact->body == waiting.body &&
                state.pdc.capturedArtifact->acceptanceReason ==
                    "one-shot-waiting-message-captured",
                "administrative-style body semantics blocked one-shot capture",
                reason);
        }
        case 29:
        case 46: {
            const auto generation = state.accessory.semanticPresentationGeneration;
            return Check(!state.pdc.capturedArtifact.has_value() &&
                state.accessory.semanticPresentationGeneration == generation,
                "non-private channel created private work", reason);
        }
        case 25: {
            ScriptedPrimitiveReader reader;
            reader.sender.clear();
            reader.body = "WAITING MESSAGE WITH EMPTY SENDER";
            const auto sampled = SampleXPilotPrivateMessageCombined(&reader, {});
            const auto captured = fixture->Submit(
                ToBrainPdcMechanicalObservation(sampled, fixture->nowUs));
            return Check(sampled.tupleMechanicallyComplete &&
                !HasIssue(sampled, XPilotPrivateMechanicalIssue::SenderEmpty) &&
                captured.captureCompleted && state.pdc.capturedArtifact.has_value() &&
                state.pdc.capturedArtifact->sender.empty() &&
                state.pdc.capturedArtifact->body == reader.body,
                "successfully read empty sender invalidated a non-empty body", reason);
        }
        case 26: {
            auto ifr = std::make_unique<Step6Fixture>();
            auto vfr = std::make_unique<Step6Fixture>();
            vfr->context.ifrMode = false;
            const auto waiting = Observation(1,
                BrainPdcSourceConnectionStatus::Connected, "UNKNOWN",
                "MODE-NEUTRAL WAITING MESSAGE");
            const auto ifrDecision = ifr->Submit(waiting);
            const auto vfrDecision = vfr->Submit(waiting);
            return Check(ifrDecision.captureCompleted && vfrDecision.captureCompleted &&
                ifr->state->pdc.capturedArtifact.has_value() &&
                vfr->state->pdc.capturedArtifact.has_value() &&
                ifr->state->pdc.capturedArtifact->body ==
                    vfr->state->pdc.capturedArtifact->body,
                "VFR and IFR contexts were not equally eligible", reason);
        }
        case 27: {
            fixture->context.rosterAvailable = false;
            fixture->context.rosterStale = true;
            fixture->context.rosterComplete = false;
            fixture->context.senders.clear();
            const auto captured = fixture->Submit(Observation(
                1, BrainPdcSourceConnectionStatus::Connected,
                "UNLISTED", "NO CONTROLLER ROSTER REQUIRED"));
            return Check(captured.captureCompleted &&
                state.pdc.capturedArtifact.has_value() &&
                state.pdc.capturedArtifact->sender == "UNLISTED",
                "controller roster remained an admission requirement", reason);
        }
        case 28: {
            constexpr std::array<WorkflowStage, 4> stages{{
                WorkflowStage::None, WorkflowStage::Departure,
                WorkflowStage::Enroute, WorkflowStage::Arrival}};
            for (const auto stage : stages) {
                auto candidate = std::make_unique<Step6Fixture>();
                candidate->context.workflowStage = stage;
                const auto captured = candidate->Submit(Observation(
                    1, BrainPdcSourceConnectionStatus::Connected,
                    "ACARS", "WORKFLOW-NEUTRAL WAITING MESSAGE"));
                if (!captured.captureCompleted ||
                    !candidate->state->pdc.capturedArtifact.has_value()) {
                    return Check(false,
                        "workflow stage changed waiting-message eligibility", reason);
                }
            }
            return true;
        }
        case 30: {
            const auto idleCommand = ProjectBrainOwnedAccessoryPresentation(
                fixture->state.get(), nullptr);
            const auto idle = std::find_if(idleCommand.snapshot->orbs.begin(),
                idleCommand.snapshot->orbs.end(), [](const auto& orb) {
                    return orb.drawer == BrainOwnedAccessoryDrawerId::Pdc;
                });
            auto unavailableFixture = std::make_unique<Step6Fixture>();
            MarkBrainOwnedPdcSourceUnavailable(unavailableFixture->state.get());
            const auto unavailableCommand = ProjectBrainOwnedAccessoryPresentation(
                unavailableFixture->state.get(), nullptr);
            const auto unavailable = std::find_if(
                unavailableCommand.snapshot->orbs.begin(),
                unavailableCommand.snapshot->orbs.end(), [](const auto& orb) {
                    return orb.drawer == BrainOwnedAccessoryDrawerId::Pdc;
                });
            fixture->Submit(connected1);
            const auto closedCommand = ProjectBrainOwnedAccessoryPresentation(
                fixture->state.get(), nullptr);
            const auto closed = std::find_if(closedCommand.snapshot->orbs.begin(),
                closedCommand.snapshot->orbs.end(), [](const auto& orb) {
                    return orb.drawer == BrainOwnedAccessoryDrawerId::Pdc;
                });
            fixture->EnsurePdcSelected();
            const auto openCommand = ProjectBrainOwnedAccessoryPresentation(
                fixture->state.get(), nullptr);
            const auto open = std::find_if(openCommand.snapshot->orbs.begin(),
                openCommand.snapshot->orbs.end(), [](const auto& orb) {
                    return orb.drawer == BrainOwnedAccessoryDrawerId::Pdc;
                });
            ++fixture->clickSequence;
            (void)RequestBrainOwnedAccessoryDrawerSelection(fixture->state.get(),
                {BrainOwnedAccessoryDrawerId::Pdc, fixture->clickSequence,
                 fixture->nowUs++, fixture->nowUs, fixture->nowUs + 1});
            const auto restoredCommand = ProjectBrainOwnedAccessoryPresentation(
                fixture->state.get(), nullptr);
            const auto restored = std::find_if(
                restoredCommand.snapshot->orbs.begin(),
                restoredCommand.snapshot->orbs.end(), [](const auto& orb) {
                    return orb.drawer == BrainOwnedAccessoryDrawerId::Pdc;
                });
            return Check(idle != idleCommand.snapshot->orbs.end() &&
                idle->categoryText == "IDLE" &&
                unavailable != unavailableCommand.snapshot->orbs.end() &&
                unavailable->categoryText == "SOURCE" &&
                closed != closedCommand.snapshot->orbs.end() &&
                closed->label == "PDC" && closed->airportIcao.empty() &&
                closed->categoryText == "NEW" &&
                closed->tone == BrainOwnedAccessoryOrbPresentation::Tone::Amber &&
                open != openCommand.snapshot->orbs.end() &&
                open->selected && open->selectedIndicator == "OPEN" &&
                restored != restoredCommand.snapshot->orbs.end() &&
                restored->categoryText == "IDLE",
                "one-shot PDC ORB state or OPEN precedence failed", reason);
        }
        case 31: {
            fixture->Submit(connected1);
            const auto seam = PublishPdc(fixture.get(), reason);
            ++fixture->clickSequence;
            (void)RequestBrainOwnedAccessoryDrawerSelection(fixture->state.get(),
                {BrainOwnedAccessoryDrawerId::Pdc, fixture->clickSequence,
                 fixture->nowUs++, fixture->nowUs, fixture->nowUs + 1});
            const auto closed = ProjectBrainOwnedAccessoryPresentation(
                fixture->state.get(), nullptr);
            const auto pdc = std::find_if(closed.snapshot->orbs.begin(),
                closed.snapshot->orbs.end(), [](const auto& orb) {
                    return orb.drawer == BrainOwnedAccessoryDrawerId::Pdc;
                });
            return seam.passed && Check(
                seam.command.snapshot->drawerTitle ==
                    "PDC / PRIVATE MESSAGES" &&
                seam.command.snapshot->entries.size() == 1 &&
                seam.command.snapshot->entries[0].body == connected1.body &&
                pdc != closed.snapshot->orbs.end() &&
                pdc->categoryText == "IDLE",
                "one-shot PDC drawer contract failed", reason);
        }
        case 32: {
            const auto first = connected1;
            fixture->Submit(first);
            for (int sequence = 2; sequence <= 40; ++sequence) {
                fixture->Submit(Observation(sequence,
                    BrainPdcSourceConnectionStatus::Connected, "KDFW_DEL",
                    "N100PC CLEARED TO KSAN VIA AMENDMENT " +
                        std::to_string(sequence) + " CLIMB 5000 SQUAWK 1234"));
            }
            fixture->EnsurePdcSelected();
            const auto command = ProjectBrainOwnedAccessoryPresentation(
                fixture->state.get(), nullptr);
            return Check(command.snapshot != nullptr &&
                command.snapshot->entries.size() == 1 &&
                command.snapshot->entries[0].body == first.body,
                "later PDC input altered the one-shot artifact", reason);
        }
        case 33: {
            const auto first = Observation(1,
                BrainPdcSourceConnectionStatus::Connected, "N200PC", "FIRST");
            const auto accepted = fixture->Submit(first);
            fixture->Submit(Observation(2, BrainPdcSourceConnectionStatus::Connected,
                "KDFW_DEL", "SECOND"));
            fixture->Submit(Observation(3, BrainPdcSourceConnectionStatus::Connected,
                "N200PC", "THIRD"));
            fixture->Submit(Observation(4, BrainPdcSourceConnectionStatus::Connected,
                "KDFW_DEL",
                "N100PC CLEARED TO KSAN VIA ROUTE CLIMB 5000 SQUAWK 1234"));
            const auto seam = PublishPdc(fixture.get(), reason);
            return seam.passed && Check(accepted.captureCompleted &&
                seam.command.snapshot->entries.size() == 1 &&
                seam.command.snapshot->entries[0].body == first.body &&
                state.pdc.capturedArtifact.has_value() &&
                state.pdc.capturedArtifact->sourceSequence == 1 &&
                state.pdc.counters.postCaptureEarlyNoops == 3,
                "later sender or body semantics altered the first capture", reason);
        }
        case 34: {
            const auto first = Observation(1,
                BrainPdcSourceConnectionStatus::Connected, "PILOT1", "MESSAGE 1");
            const auto accepted = fixture->Submit(first);
            for (int sequence = 1; sequence <= 40; ++sequence) {
                if (sequence == 1) continue;
                const auto sender = "PILOT" + std::to_string(sequence % 10);
                fixture->Submit(Observation(sequence,
                    BrainPdcSourceConnectionStatus::Connected, sender,
                    "MESSAGE " + std::to_string(sequence)));
            }
            fixture->EnsurePdcSelected();
            const auto command = ProjectBrainOwnedAccessoryPresentation(
                fixture->state.get(), nullptr);
            return Check(accepted.captureCompleted && command.snapshot != nullptr &&
                command.snapshot->entries.size() == 1 &&
                command.snapshot->entries[0].body == first.body &&
                BrainOwnedPdcUnreadCount(state.pdc) == 1 &&
                state.pdc.counters.admittedMessages == 1 &&
                state.pdc.counters.postCaptureEarlyNoops == 39,
                "one-shot capture became a private-message history or unread counter",
                reason);
        }
        case 35: {
            fixture->Submit(Observation(1, BrainPdcSourceConnectionStatus::Connected,
                "KDFW_DEL", "N100PC CLEARED — UTF-8 ✓ CLIMB 5000 SQUAWK 1234 " +
                    std::string(700, 'W')));
            const auto seam = PublishPdc(fixture.get(), reason);
            return seam.passed && Check(seam.render.totalLineCount >
                seam.render.visibleLineCapacity, "UTF-8 layout not scrollable", reason);
        }
        case 36:
            fixture->EnsurePdcSelected(); {
                const auto before = state.accessory.scrollResetGeneration;
                fixture->Submit(connected1);
                return Check(state.accessory.scrollResetGeneration == before + 1,
                    "owned content did not reset viewport once", reason);
            }
        case 37:
        case 41: {
            (void)RequestBrainOwnedAccessoryDrawerSelection(fixture->state.get(),
                {BrainOwnedAccessoryDrawerId::Atis, 1, 1, 1, 2});
            const auto selection = state.accessory.selectionGeneration;
            const auto scroll = state.accessory.scrollResetGeneration;
            fixture->Submit(connected1);
            return Check(state.accessory.activeDrawer == BrainOwnedAccessoryDrawerId::Atis &&
                state.accessory.selectionGeneration == selection &&
                state.accessory.scrollResetGeneration == scroll,
                "hidden update stole drawer/viewport", reason);
        }
        case 38: {
            fixture->Submit(connected1);
            const auto revision = state.pdc.capturedArtifact->revisionIdentity;
            const auto seam = PublishPdc(fixture.get(), reason);
            const auto occurrences = std::count(
                seam.visibleIdentities.begin(), seam.visibleIdentities.end(), revision);
            return seam.passed && Check(occurrences == 1 &&
                seam.visibleIdentities.size() <= 16,
                "captured revision was not published once within the bounded identity set",
                reason);
        }
        case 39: {
            fixture->Submit(connected1);
            const auto revision = state.pdc.capturedArtifact->revisionIdentity;
            const auto before = BrainOwnedPdcUnreadCount(state.pdc);
            const auto seam = PublishPdc(fixture.get(), reason);
            const auto after = BrainOwnedPdcUnreadCount(state.pdc);
            return seam.passed && Check(before == 1 && after == 0 &&
                std::count(seam.visibleIdentities.begin(),
                    seam.visibleIdentities.end(), revision) == 1 &&
                state.pdc.counters.unreadAcknowledged == 1,
                "visible acknowledgement did not target only the captured revision",
                reason);
        }
        case 40: {
            fixture->Submit(connected1);
            const auto revision = state.pdc.capturedArtifact->revisionIdentity;
            const auto semantic = state.accessory.semanticPresentationGeneration;
            bool stopped = true;
            for (int cycle = 0; cycle < 100'000; ++cycle) {
                const auto acquisition = EvaluateBrainOwnedPdcAcquisition(
                    fixture->state.get(), fixture->context);
                stopped &= acquisition.captureComplete &&
                    !acquisition.samplePrivatePayload &&
                    acquisition.clearQueuedFacts;
            }
            return Check(state.accessory.semanticPresentationGeneration == semantic &&
                stopped &&
                state.pdc.capturedArtifact.has_value() &&
                state.pdc.capturedArtifact->revisionIdentity == revision &&
                state.pdc.counters.payloadReadsStopped == 100'000,
                "stopped acquisition created work or permitted private sampling",
                reason);
        }
        case 42: {
            fixture->Submit(connected1);
            const auto revision = state.pdc.capturedArtifact->revisionIdentity;
            SuspendBrainOwnedPdcRuntime(fixture->state.get());
            const auto ignored = fixture->Submit(Observation(
                2, BrainPdcSourceConnectionStatus::Connected));
            ResumeBrainOwnedPdcRuntime(fixture->state.get());
            const auto later = fixture->Submit(Observation(2,
                BrainPdcSourceConnectionStatus::Connected, "KDFW_DEL", "TWO"));
            const auto acquisition = EvaluateBrainOwnedPdcAcquisition(
                fixture->state.get(), fixture->context);
            return Check(!ignored.evaluated && !later.evaluated &&
                acquisition.captureComplete &&
                !acquisition.samplePrivatePayload &&
                state.pdc.capturedArtifact.has_value() &&
                state.pdc.capturedArtifact->revisionIdentity == revision &&
                state.pdc.captureComplete,
                "suspend/resume altered or re-armed the captured artifact", reason);
        }
        case 43:
            fixture->Submit(Observation(1, BrainPdcSourceConnectionStatus::Disconnected));
            ++state.accessory.lifecycleEpoch;
            ResetBrainOwnedPdcProductState(fixture->state.get(), false); {
                const auto accepted = fixture->Submit(connected1);
                return Check(accepted.captureCompleted &&
                    state.pdc.capturedArtifact.has_value() &&
                    state.pdc.capturedArtifact->productLifecycleEpoch == 2 &&
                    state.pdc.counters.provisionalRetained == 0,
                    "discarded pre-capture content contaminated the new lifecycle",
                    reason);
            }
        case 45: {
            auto raw = std::make_unique<BrainOwnedRuntimeState>();
            const auto before = raw->accessory.semanticPresentationGeneration;
            InitializeBrainOwnedPdcRuntime(raw.get());
            const auto command = ProjectBrainOwnedAccessoryPresentation(raw.get(), nullptr);
            const auto pdc = std::find_if(command.snapshot->orbs.begin(),
                command.snapshot->orbs.end(), [](const auto& orb) {
                    return orb.drawer == BrainOwnedAccessoryDrawerId::Pdc;
                });
            return Check(raw->pdc.initialized &&
                raw->accessory.semanticPresentationGeneration == before + 1 &&
                pdc != command.snapshot->orbs.end() && pdc->categoryText == "IDLE",
                "initialization projection failed", reason);
        }
        case 48: {
            const auto captured = fixture->Submit(Observation(
                1, BrainPdcSourceConnectionStatus::Connected,
                "SECRET_SENDER", "PRIVATE BODY MUST NOT APPEAR"));
            const auto summary = BrainOwnedPdcDiagnosticSummary(state.pdc);
            return Check(summary.find("SECRET_SENDER") == std::string::npos &&
                summary.find("PRIVATE BODY") == std::string::npos &&
                summary.find("messages=1") != std::string::npos &&
                captured.captureCompleted && state.pdc.retainedBytes != 0,
                "diagnostic leaked private content", reason);
        }
        case 49:
            fixture->Submit(connected1);
            return Check(state.pdc.initialized && state.pdc.captureComplete &&
                state.pdc.capturedArtifact.has_value() &&
                state.pdc.pluginInstanceIdentity == 7,
                "fixture-off owner inactive", reason);
        default: return Check(false, "unmapped frozen Step 6 probe", reason);
    }
}

}  // namespace

int RunStep6PdcContractProbe(const std::string& scenarioName) {
    const auto index = ProbeIndex(scenarioName);
    if (index < 1) {
        std::cerr << "STEP6_SCENARIO_CONFIGURATION_ERROR: " << scenarioName
                  << ": unknown Step 6 probe\n";
        return 2;
    }
    std::string reason;
    const bool passed = index <= 8
        ? MechanicalProbe(index, &reason)
        : (index >= 50 ? QualificationProbe(index, &reason)
                       : ProductProbe(index, &reason));
    if (!passed) {
        std::cerr << "STEP6_ASSERTION_FAILED: " << scenarioName << ": "
                  << reason << "\n";
        return 1;
    }
    std::cout << "Scenario passed: " << scenarioName << "\n";
    return 0;
}

}  // namespace xvatsim::tools::step6_pdc_proof
