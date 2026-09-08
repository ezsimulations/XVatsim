#include "RuntimeActivationGateProbe.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "XVatsim/brain/BrainOwnedRuntime.h"
#include "XVatsim/brain/BrainWorkflow.h"

namespace xvatsim::tools::runtime_activation_gate {
namespace {

using Clock = std::chrono::steady_clock;
using Microseconds = std::chrono::microseconds;
using ActivationDecision = brain::BrainOwnedOperationalActivationDecision;
using ActivationReason = brain::BrainOwnedOperationalActivationReason;
using ActivationStage = brain::BrainOwnedOperationalServiceStage;
using SessionAction = brain::workflow::XPilotSessionBoundaryAction;

constexpr std::array<ActivationStage,
                     brain::kBrainOwnedOperationalServiceStageCount>
    kOperationalStages = {
        ActivationStage::VatsimFeed,
        ActivationStage::ControllerSnapshot,
        ActivationStage::FlightPlan,
        ActivationStage::NetworkPlan,
        ActivationStage::Radio,
        ActivationStage::PdcPrivateSource,
        ActivationStage::Atis,
        ActivationStage::Metar,
        ActivationStage::Ctaf,
        ActivationStage::Route,
        ActivationStage::Authority,
        ActivationStage::ControllerRelevance,
        ActivationStage::WorkflowPublication,
        ActivationStage::StandbyAssist,
    };

struct CallbackInput {
    bool aircraftValid = true;
    bool batteryOn = true;
    bool xPilotConnected = false;
    bool replayActive = false;
    bool rapidMovement = false;
    bool forcedOpen = false;
    bool updateNotice = false;
    int fmsEntries = 0;
    int validFmsAirports = 0;
    std::uint64_t pdcObservationSequence = 0;
    std::uint64_t monotonicMs = 0;
    std::string callsign;
};

struct CallbackOutput {
    ActivationDecision activation;
    brain::workflow::XPilotSessionBoundaryDecision session;
};

struct ProductionPathFixture {
    brain::BrainOwnedOperationalActivationState activationState;
    brain::workflow::XPilotSessionBoundaryState sessionState;
    std::uint64_t updateLifecycleCalls = 0;
    std::uint64_t aircraftSamples = 0;
    std::uint64_t xPilotPolls = 0;
    std::uint64_t sessionBoundaryPreservations = 0;
    std::uint64_t asynchronousInvalidations = 0;
    std::uint64_t callsignResets = 0;
    std::uint64_t automaticRecoveryQueues = 0;
    std::uint64_t dormantPresentations = 0;
    std::uint64_t forcedOpenDormantPresentations = 0;
    std::uint64_t updateNoticeDormantPresentations = 0;
    std::uint64_t fmsSamples = 0;
    std::uint64_t fmsEntriesExamined = 0;
    std::uint64_t pdcServiceCalls = 0;
    std::uint64_t pdcCaptures = 0;
    std::uint64_t lastPdcCapturedSequence = 0;
    std::uint64_t firstPdcCaptureMonotonicMs = 0;
    std::vector<std::uint64_t> pdcCapturedSequences;
    std::uint64_t sessionResets = 0;
    std::uint64_t normalShutdowns = 0;
    std::uint64_t enableWakeCallbackRuns = 0;
    bool enableWakeScheduled = false;
    std::array<std::uint64_t,
               brain::kBrainOwnedOperationalServiceStageCount>
        injectedServiceCalls{};

    CallbackOutput Run(const CallbackInput& input) {
        ++updateLifecycleCalls;
        ++aircraftSamples;

        brain::workflow::XPilotSessionBoundaryDecision session;
        if (input.aircraftValid) {
            ++xPilotPolls;
            brain::workflow::XPilotSessionBoundaryInput sessionInput;
            sessionInput.state = sessionState;
            sessionInput.xPilotSession.loaded = true;
            sessionInput.xPilotSession.connected = input.xPilotConnected;
            sessionInput.xPilotSession.callsign = input.callsign;
            sessionInput.pilotIdentity.connected = input.xPilotConnected;
            sessionInput.pilotIdentity.callsign = input.callsign;
            sessionInput.pilotIdentity.normalizedCallsign = input.callsign;
            session = brain::workflow::ResolveXPilotSessionBoundary(
                sessionInput);
            if (session.shouldPreserveFlightStateForDisconnect) {
                ++sessionBoundaryPreservations;
                ++asynchronousInvalidations;
            }
            if (session.shouldResetFlightScopedState) {
                ++callsignResets;
            }
            if (session.shouldQueueAutomaticRecovery) {
                ++automaticRecoveryQueues;
            }
            sessionState = session.nextState;
        }

        brain::BrainOwnedOperationalActivationInput activationInput;
        activationInput.aircraftStateValid = input.aircraftValid;
        activationInput.batteryOn = input.batteryOn;
        activationInput.xPilotConnected = input.xPilotConnected;
        auto activation = brain::DecideBrainOwnedOperationalActivation(
            activationState, activationInput);
        brain::CommitBrainOwnedOperationalActivationDecision(
            &activationState, activation);

        if (!activation.operational || session.action != SessionAction::None) {
            ++dormantPresentations;
            if (input.forcedOpen) {
                ++forcedOpenDormantPresentations;
            }
            if (input.updateNotice) {
                ++updateNoticeDormantPresentations;
            }
            return {activation, session};
        }

        for (const auto stage : kOperationalStages) {
            brain::RecordBrainOwnedOperationalServiceCall(
                &activationState, activation, stage);
            ++injectedServiceCalls[static_cast<std::size_t>(stage)];
            if (stage == ActivationStage::FlightPlan) {
                ++fmsSamples;
                fmsEntriesExamined += static_cast<std::uint64_t>(
                    std::max(0, input.fmsEntries));
            }
            if (stage == ActivationStage::PdcPrivateSource) {
                ++pdcServiceCalls;
                if (input.pdcObservationSequence != 0 &&
                    input.pdcObservationSequence !=
                        lastPdcCapturedSequence) {
                    if (pdcCaptures == 0) {
                        firstPdcCaptureMonotonicMs = input.monotonicMs;
                    }
                    lastPdcCapturedSequence =
                        input.pdcObservationSequence;
                    pdcCapturedSequences.push_back(
                        input.pdcObservationSequence);
                    ++pdcCaptures;
                }
            }
        }
        return {activation, session};
    }

    void RequestPluginEnableWake() {
        brain::RecordBrainOwnedOperationalEnableWakeRequest(
            &activationState);
        enableWakeScheduled = true;
    }

    CallbackOutput RunPluginEnableWake(const CallbackInput& input) {
        if (enableWakeScheduled) {
            ++enableWakeCallbackRuns;
            enableWakeScheduled = false;
        }
        return Run(input);
    }

    void SuspendForPluginAdmin() {
        enableWakeScheduled = false;
        brain::SetBrainOwnedOperationalActivationSuspended(&activationState);
    }

    void ResetSession() {
        ++sessionResets;
        sessionState = {};
    }

    void Shutdown() {
        ++normalShutdowns;
        brain::SetBrainOwnedOperationalActivationSuspended(&activationState);
    }
};

std::uint64_t ElapsedUs(Clock::time_point started) {
    return static_cast<std::uint64_t>(std::max<long long>(
        0,
        std::chrono::duration_cast<Microseconds>(Clock::now() - started)
            .count()));
}

std::uint64_t Percentile99(std::vector<std::uint64_t> values) {
    if (values.empty()) {
        return 0;
    }
    std::sort(values.begin(), values.end());
    const auto index = std::min<std::size_t>(
        values.size() - 1,
        static_cast<std::size_t>(
            static_cast<double>(values.size() - 1) * 0.99));
    return values[index];
}

bool NoOperationalServices(const ProductionPathFixture& fixture) {
    return std::all_of(
        fixture.injectedServiceCalls.begin(),
        fixture.injectedServiceCalls.end(),
        [](std::uint64_t count) { return count == 0; });
}

}  // namespace

int RunRuntimeActivationGateProbe() {
    std::vector<std::string> failures;
    const auto require = [&](bool condition, std::string message) {
        if (!condition) {
            failures.push_back(std::move(message));
        }
    };

    constexpr std::uint64_t kFormerInitialCallbackBoundaryMs = 10'000;

    ProductionPathFixture enabledConnected;
    CallbackInput enabledConnectedInput;
    enabledConnectedInput.xPilotConnected = true;
    enabledConnectedInput.callsign = "ENABLE1";
    enabledConnectedInput.pdcObservationSequence = 100;
    enabledConnectedInput.monotonicMs = 1;
    enabledConnected.RequestPluginEnableWake();
    require(enabledConnected.pdcServiceCalls == 0 &&
                NoOperationalServices(enabledConnected),
            "Plugin enable performed operational work outside the gate");
    const auto enabledConnectedOutput =
        enabledConnected.RunPluginEnableWake(enabledConnectedInput);
    require(enabledConnectedOutput.activation.operational &&
                enabledConnected.enableWakeCallbackRuns == 1 &&
                enabledConnected.pdcServiceCalls == 1 &&
                enabledConnected.pdcCaptures == 1 &&
                enabledConnected.activationState.enableImmediateWakeRequests ==
                    1 &&
                enabledConnected.activationState.enableImmediateCallbacks == 1 &&
                enabledConnected.activationState
                        .enableImmediateOperationalCallbacks == 1 &&
                enabledConnected.activationState
                        .enableImmediateDormantCallbacks == 0,
            "powered connected enable did not run one immediate gated sample");

    ProductionPathFixture enabledDisconnected;
    CallbackInput enabledDisconnectedInput;
    enabledDisconnectedInput.pdcObservationSequence = 110;
    enabledDisconnected.RequestPluginEnableWake();
    const auto enabledDisconnectedOutput =
        enabledDisconnected.RunPluginEnableWake(enabledDisconnectedInput);
    require(!enabledDisconnectedOutput.activation.operational &&
                enabledDisconnected.pdcServiceCalls == 0 &&
                NoOperationalServices(enabledDisconnected) &&
                enabledDisconnected.activationState
                        .enableImmediateDormantCallbacks == 1,
            "disconnected enable invoked operational or PDC work");

    ProductionPathFixture enabledBatteryOff;
    CallbackInput enabledBatteryOffInput = enabledConnectedInput;
    enabledBatteryOffInput.batteryOn = false;
    enabledBatteryOff.RequestPluginEnableWake();
    const auto enabledBatteryOffOutput =
        enabledBatteryOff.RunPluginEnableWake(enabledBatteryOffInput);
    require(!enabledBatteryOffOutput.activation.operational &&
                enabledBatteryOff.pdcServiceCalls == 0 &&
                NoOperationalServices(enabledBatteryOff) &&
                enabledBatteryOff.activationState
                        .enableImmediateDormantCallbacks == 1,
            "battery-off connected enable invoked operational or PDC work");

    ProductionPathFixture replacementBeforeLegacyBoundary;
    CallbackInput firstPdc = enabledConnectedInput;
    firstPdc.pdcObservationSequence = 120;
    firstPdc.monotonicMs = 1;
    replacementBeforeLegacyBoundary.RequestPluginEnableWake();
    (void)replacementBeforeLegacyBoundary.RunPluginEnableWake(firstPdc);
    CallbackInput replacementPdc = firstPdc;
    replacementPdc.pdcObservationSequence = 121;
    replacementPdc.monotonicMs = 5'000;
    (void)replacementBeforeLegacyBoundary.Run(replacementPdc);
    (void)replacementBeforeLegacyBoundary.Run(replacementPdc);
    require(replacementBeforeLegacyBoundary.pdcCaptures == 2 &&
                replacementBeforeLegacyBoundary.pdcCapturedSequences.size() ==
                    2 &&
                replacementBeforeLegacyBoundary.pdcCapturedSequences[0] ==
                    120 &&
                replacementBeforeLegacyBoundary.pdcCapturedSequences[1] ==
                    121 &&
                replacementBeforeLegacyBoundary.firstPdcCaptureMonotonicMs <
                    kFormerInitialCallbackBoundaryMs,
            "enable-time PDC observation was missed or admitted repeatedly");

    ProductionPathFixture reenabledConnected;
    CallbackInput reenabledConnectedInput = enabledConnectedInput;
    reenabledConnectedInput.pdcObservationSequence = 130;
    reenabledConnected.RequestPluginEnableWake();
    (void)reenabledConnected.RunPluginEnableWake(reenabledConnectedInput);
    reenabledConnected.SuspendForPluginAdmin();
    reenabledConnected.RequestPluginEnableWake();
    const auto connectedReenable =
        reenabledConnected.RunPluginEnableWake(reenabledConnectedInput);
    require(connectedReenable.activation.operational &&
                connectedReenable.activation.activationRisingEdge &&
                reenabledConnected.enableWakeCallbackRuns == 2 &&
                reenabledConnected.pdcServiceCalls == 2 &&
                reenabledConnected.pdcCaptures == 1 &&
                reenabledConnected.activationState.enableImmediateWakeRequests ==
                    2 &&
                reenabledConnected.activationState.enableImmediateCallbacks ==
                    2 &&
                reenabledConnected.activationState
                        .enableImmediateOperationalCallbacks == 2,
            "connected disable/re-enable did not service one gated callback");

    ProductionPathFixture reenabledDisconnected;
    reenabledDisconnected.RequestPluginEnableWake();
    (void)reenabledDisconnected.RunPluginEnableWake(
        enabledDisconnectedInput);
    reenabledDisconnected.SuspendForPluginAdmin();
    reenabledDisconnected.RequestPluginEnableWake();
    (void)reenabledDisconnected.RunPluginEnableWake(
        enabledDisconnectedInput);
    require(reenabledDisconnected.enableWakeCallbackRuns == 2 &&
                reenabledDisconnected.pdcServiceCalls == 0 &&
                NoOperationalServices(reenabledDisconnected) &&
                reenabledDisconnected.activationState
                        .enableImmediateDormantCallbacks == 2,
            "disconnected disable/re-enable invoked operational work");

    ProductionPathFixture dormant;
    CallbackInput invalid;
    invalid.aircraftValid = false;
    auto invalidOutput = dormant.Run(invalid);
    require(!invalidOutput.activation.operational &&
                invalidOutput.activation.reason ==
                    ActivationReason::AircraftStateInvalid,
            "invalid aircraft did not remain dormant");

    CallbackInput batteryOff;
    batteryOff.batteryOn = false;
    auto batteryOffOutput = dormant.Run(batteryOff);
    require(!batteryOffOutput.activation.operational &&
                batteryOffOutput.activation.reason ==
                    ActivationReason::BatteryOff,
            "battery-off callback did not remain dormant");

    CallbackInput neverConnected;
    neverConnected.batteryOn = true;
    neverConnected.xPilotConnected = false;
    for (int index = 0; index < 1000; ++index) {
        neverConnected.replayActive = true;
        neverConnected.rapidMovement = true;
        neverConnected.fmsEntries = 512;
        neverConnected.validFmsAirports = 0;
        const auto output = dormant.Run(neverConnected);
        require(!output.activation.operational,
                "never-connected replay activated operational work");
    }

    CallbackInput singleAirport = neverConnected;
    singleAirport.validFmsAirports = 1;
    (void)dormant.Run(singleAirport);
    CallbackInput forcedOpen = neverConnected;
    forcedOpen.forcedOpen = true;
    (void)dormant.Run(forcedOpen);
    CallbackInput updateNotice = neverConnected;
    updateNotice.updateNotice = true;
    (void)dormant.Run(updateNotice);
    require(NoOperationalServices(dormant) && dormant.fmsSamples == 0 &&
                dormant.fmsEntriesExamined == 0,
            "dormant production coordinator invoked an operational service");
    require(dormant.activationState.dormantOperationalAttemptCount == 0,
            "dormant coordinator recorded an operational attempt");
    require(dormant.forcedOpenDormantPresentations == 1 &&
                dormant.updateNoticeDormantPresentations == 1,
            "forced-open/update notice did not remain presentation-only");

    ProductionPathFixture lifecycle;
    CallbackInput connected;
    connected.xPilotConnected = true;
    connected.callsign = "DAL7510";
    connected.fmsEntries = 512;
    connected.validFmsAirports = 2;
    const auto initialConnection = lifecycle.Run(connected);
    require(initialConnection.activation.operational &&
                initialConnection.activation.activationRisingEdge,
            "late initial xPilot connection did not activate immediately");
    require(lifecycle.fmsSamples == 1,
            "initial activation did not perform a full operational sample");

    CallbackInput disconnected = connected;
    disconnected.xPilotConnected = false;
    const auto falling = lifecycle.Run(disconnected);
    require(!falling.activation.operational &&
                falling.activation.disconnectFallingEdge &&
                falling.session.shouldPreserveFlightStateForDisconnect,
            "connected-to-disconnected edge was not preserved");
    for (int index = 0; index < 100; ++index) {
        (void)lifecycle.Run(disconnected);
    }
    require(lifecycle.sessionBoundaryPreservations == 1 &&
                lifecycle.asynchronousInvalidations == 1 &&
                lifecycle.activationState.disconnectFallingEdges == 1,
            "disconnect preservation or falling edge repeated");
    require(lifecycle.fmsSamples == 1,
            "subsequent disconnected callbacks sampled the FMS");

    const auto sameCallsignReconnect = lifecycle.Run(connected);
    require(sameCallsignReconnect.activation.activationRisingEdge &&
                sameCallsignReconnect.session.shouldQueueAutomaticRecovery &&
                lifecycle.automaticRecoveryQueues == 1 &&
                lifecycle.fmsSamples == 2,
            "same-callsign reconnect did not recover and sample immediately");

    (void)lifecycle.Run(disconnected);
    CallbackInput changedCallsign = connected;
    changedCallsign.callsign = "DAL7511";
    const auto changedReconnect = lifecycle.Run(changedCallsign);
    require(changedReconnect.session.shouldResetFlightScopedState &&
                changedReconnect.session.action ==
                    SessionAction::ResetForCallsignChange &&
                lifecycle.callsignResets == 1,
            "changed-callsign reconnect did not reset exactly once");
    const auto postReset = lifecycle.Run(changedCallsign);
    require(postReset.activation.operational &&
                postReset.session.action == SessionAction::None &&
                lifecycle.fmsSamples == 3,
            "post-callsign-reset operational sample did not resume");

    CallbackInput connectedBatteryOff = changedCallsign;
    connectedBatteryOff.batteryOn = false;
    const auto poweredDown = lifecycle.Run(connectedBatteryOff);
    require(!poweredDown.activation.operational &&
                poweredDown.activation.deactivationFallingEdge &&
                !poweredDown.activation.disconnectFallingEdge,
            "battery-off transition was classified as a disconnect");
    const auto poweredUp = lifecycle.Run(changedCallsign);
    require(poweredUp.activation.activationRisingEdge &&
                lifecycle.fmsSamples == 4,
            "battery-on transition did not reactivate immediately");

    lifecycle.SuspendForPluginAdmin();
    lifecycle.RequestPluginEnableWake();
    (void)lifecycle.RunPluginEnableWake(disconnected);
    require(!lifecycle.activationState.operational &&
                lifecycle.fmsSamples == 4,
            "disabled/re-enabled disconnected path invoked operational work");

    ProductionPathFixture terminalLifecycle;
    (void)terminalLifecycle.Run(disconnected);
    terminalLifecycle.ResetSession();
    (void)terminalLifecycle.Run(disconnected);
    terminalLifecycle.Shutdown();
    require(terminalLifecycle.sessionResets == 1 &&
                terminalLifecycle.normalShutdowns == 1 &&
                !terminalLifecycle.activationState.operational &&
                terminalLifecycle.fmsSamples == 0 &&
                NoOperationalServices(terminalLifecycle),
            "session reset or normal shutdown activated operational work");

    ProductionPathFixture timedDormant;
    CallbackInput timedInput;
    timedInput.replayActive = true;
    timedInput.rapidMovement = true;
    timedInput.fmsEntries = 512;
    std::vector<std::uint64_t> dormantUs;
    dormantUs.reserve(50'000);
    for (int iteration = 0; iteration < 50'000; ++iteration) {
        const auto started = Clock::now();
        (void)timedDormant.Run(timedInput);
        dormantUs.push_back(ElapsedUs(started));
    }
    const auto dormantP99Us = Percentile99(dormantUs);
    const auto dormantMaxUs = dormantUs.empty()
        ? 0
        : *std::max_element(dormantUs.begin(), dormantUs.end());
    require(dormantP99Us <= 250,
            "dormant production-path coordinator P99 exceeded 250 us");
    require(dormantMaxUs <= 3000,
            "dormant production-path coordinator exceeded 3 ms");
    require(NoOperationalServices(timedDormant) &&
                timedDormant.fmsSamples == 0 &&
                timedDormant.activationState.dormantOperationalAttemptCount == 0,
            "timed dormant run invoked operational work");

    bool stageAccountingExact = true;
    for (std::size_t index = 0;
         index < brain::kBrainOwnedOperationalServiceStageCount;
         ++index) {
        stageAccountingExact = stageAccountingExact &&
            lifecycle.activationState.operationalServiceCalls[index] ==
                lifecycle.injectedServiceCalls[index] &&
            lifecycle.activationState.dormantOperationalAttempts[index] == 0;
    }
    require(stageAccountingExact,
            "operational stage accounting was not exact");

    ProductionPathFixture lifecycleStress;
    CallbackInput stressConnected;
    stressConnected.xPilotConnected = true;
    stressConnected.callsign = "STRESS1";
    stressConnected.fmsEntries = 512;
    CallbackInput stressDisconnected = stressConnected;
    stressDisconnected.xPilotConnected = false;
    (void)lifecycleStress.Run(stressConnected);
    constexpr std::uint64_t kLifecycleStressCycles = 1000;
    for (std::uint64_t cycle = 0; cycle < kLifecycleStressCycles; ++cycle) {
        (void)lifecycleStress.Run(stressDisconnected);
        (void)lifecycleStress.Run(stressDisconnected);
        (void)lifecycleStress.Run(stressDisconnected);
        (void)lifecycleStress.Run(stressConnected);
    }
    require(lifecycleStress.activationState.disconnectFallingEdges ==
                kLifecycleStressCycles &&
                lifecycleStress.sessionBoundaryPreservations ==
                    kLifecycleStressCycles &&
                lifecycleStress.asynchronousInvalidations ==
                    kLifecycleStressCycles &&
                lifecycleStress.automaticRecoveryQueues ==
                    kLifecycleStressCycles &&
                lifecycleStress.activationState.activationRisingEdges ==
                    kLifecycleStressCycles + 1 &&
                lifecycleStress.fmsSamples == kLifecycleStressCycles + 1 &&
                lifecycleStress.callsignResets == 0 &&
                lifecycleStress.activationState.dormantOperationalAttemptCount ==
                    0,
            "lifecycle stress repeated or lost a transition");

    std::cout << "RUNTIME_ACTIVATION_GATE_PROBE"
              << " enableConnectedPdcServices="
              << enabledConnected.pdcServiceCalls
              << " enableConnectedPdcCaptures="
              << enabledConnected.pdcCaptures
              << " enableDisconnectedPdcServices="
              << enabledDisconnected.pdcServiceCalls
              << " enableBatteryOffPdcServices="
              << enabledBatteryOff.pdcServiceCalls
              << " replacementBeforeLegacyBoundaryCaptures="
              << replacementBeforeLegacyBoundary.pdcCaptures
              << " connectedReenablePdcServices="
              << reenabledConnected.pdcServiceCalls
              << " connectedReenablePdcCaptures="
              << reenabledConnected.pdcCaptures
              << " disconnectedReenablePdcServices="
              << reenabledDisconnected.pdcServiceCalls
              << " dormantCallbacks="
              << timedDormant.activationState.dormantCallbacks
              << " dormantP99Us=" << dormantP99Us
              << " dormantMaxUs=" << dormantMaxUs
              << " dormantOperationalAttempts="
              << timedDormant.activationState.dormantOperationalAttemptCount
              << " fmsSamplesWhileDormant=" << timedDormant.fmsSamples
              << " activationRisingEdges="
              << lifecycle.activationState.activationRisingEdges
              << " disconnectFallingEdges="
              << lifecycle.activationState.disconnectFallingEdges
              << " sessionBoundaryPreservations="
              << lifecycle.sessionBoundaryPreservations
              << " asynchronousInvalidations="
              << lifecycle.asynchronousInvalidations
              << " automaticRecoveryQueues="
              << lifecycle.automaticRecoveryQueues
              << " callsignResets=" << lifecycle.callsignResets
              << " sessionResets=" << terminalLifecycle.sessionResets
              << " normalShutdowns=" << terminalLifecycle.normalShutdowns
              << " operationalFmsSamples=" << lifecycle.fmsSamples
              << " lifecycleStressCycles=" << kLifecycleStressCycles
              << " lifecycleStressRecoveries="
              << lifecycleStress.automaticRecoveryQueues
              << " lifecycleStressInvalidations="
              << lifecycleStress.asynchronousInvalidations
              << " stageAccountingExact="
              << (stageAccountingExact ? 1 : 0);
    for (std::size_t index = 0;
         index < brain::kBrainOwnedOperationalServiceStageCount;
         ++index) {
        const auto stage = static_cast<ActivationStage>(index);
        std::cout << " stage-" << brain::ToString(stage)
                  << "-connected="
                  << lifecycle.injectedServiceCalls[index]
                  << " stage-" << brain::ToString(stage)
                  << "-stress="
                  << lifecycleStress.injectedServiceCalls[index]
                  << " stage-" << brain::ToString(stage)
                  << "-dormant-attempts="
                  << dormant.activationState.dormantOperationalAttempts[index] +
                         timedDormant.activationState
                             .dormantOperationalAttempts[index] +
                         lifecycle.activationState
                             .dormantOperationalAttempts[index] +
                         lifecycleStress.activationState
                             .dormantOperationalAttempts[index] +
                         terminalLifecycle.activationState
                             .dormantOperationalAttempts[index];
    }
    std::cout << "\n";

    if (!failures.empty()) {
        for (const auto& failure : failures) {
            std::cerr << "RUNTIME_ACTIVATION_GATE_ASSERTION_FAILED: "
                      << failure << "\n";
        }
        return 1;
    }
    std::cout << "PERFORMANCE_CONTRACT_RUNTIME_ACTIVATION_GATE_PASSED\n";
    return 0;
}

}  // namespace xvatsim::tools::runtime_activation_gate
