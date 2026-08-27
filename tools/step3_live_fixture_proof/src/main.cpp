#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "Step3LiveProofFixtures.h"

namespace brain = xvatsim::brain;
namespace fixture = xvatsim::plugin::step3_live_proof;

namespace {

struct ProofState {
    int assertions = 0;
    std::vector<std::string> failures;

    void Require(bool condition, const std::string& message) {
        ++assertions;
        if (!condition) {
            failures.push_back(message);
        }
    }
};

std::string HistoryDigest(const brain::BrainOwnedRuntimeState& state) {
    std::ostringstream stream;
    for (std::size_t historyIndex = 0;
         historyIndex < state.accessory.histories.size(); ++historyIndex) {
        const auto& history = state.accessory.histories[historyIndex];
        stream << historyIndex << ':' << history.generation << ':'
               << history.nextAcceptedSequence << ':' << history.retainedBytes
               << ':' << history.entries.size() << '|';
        for (const auto& entry : history.entries) {
            stream << entry.stableKey << ':' << entry.acceptedSequence << ':'
                   << entry.retainedBytes << ':' << entry.contentLimited << ':'
                   << entry.title << ':' << entry.body << '|';
        }
    }
    return stream.str();
}

bool Contains(const std::string& value, const std::string& token) {
    return value.find(token) != std::string::npos;
}

bool StartsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

bool NewestFirst(const brain::BrainOwnedAccessoryHistory& history) {
    for (std::size_t index = 1; index < history.entries.size(); ++index) {
        if (history.entries[index - 1].acceptedSequence <=
            history.entries[index].acceptedSequence) {
            return false;
        }
    }
    return true;
}

std::size_t CountKey(
    const brain::BrainOwnedAccessoryHistory& history,
    const std::string& key) {
    return static_cast<std::size_t>(std::count_if(
        history.entries.begin(), history.entries.end(),
        [&](const brain::BrainOwnedAccessoryHistoryEntry& entry) {
            return entry.stableKey == key;
        }));
}

bool EveryEntryMarked(
    const brain::BrainOwnedAccessoryHistory& history,
    const std::string& marker) {
    return std::all_of(
        history.entries.begin(), history.entries.end(),
        [&](const brain::BrainOwnedAccessoryHistoryEntry& entry) {
            return Contains(entry.title, marker) &&
                Contains(entry.body, marker);
        });
}

bool HistoriesEmpty(const brain::BrainOwnedRuntimeState& state) {
    return std::all_of(
        state.accessory.histories.begin(),
        state.accessory.histories.end(),
        [](const brain::BrainOwnedAccessoryHistory& history) {
            return history.entries.empty() && history.retainedBytes == 0;
        });
}

fixture::Step3LiveProofFixtureLifecycleInput StableRuntimeInput() {
    fixture::Step3LiveProofFixtureLifecycleInput input;
    input.aircraftSampleValid = true;
    input.aircraftBoundaryApplied = true;
    input.xPilotBoundaryApplied = true;
    input.runtimeClearBoundariesComplete = true;
    return input;
}

brain::workflow::AircraftRuntimeBoundaryDecision ResolveAircraftBoundary(
    brain::BrainOwnedRuntimeState* state,
    bool valid,
    bool batteryOn) {
    brain::workflow::AircraftRuntimeBoundaryInput input;
    input.aircraftState.valid = valid;
    input.aircraftState.batteryOn = batteryOn;
    input.coldDarkResetApplied = state->coldDarkResetApplied;
    input.aircraftStateInvalidBoundaryActive =
        state->aircraftStateInvalidBoundaryActive;
    return brain::workflow::ResolveAircraftRuntimeBoundary(input);
}

void ApplyAircraftBoundary(
    brain::BrainOwnedRuntimeState* state,
    const brain::workflow::AircraftRuntimeBoundaryDecision& decision) {
    if (decision.shouldResetForInvalidAircraftState) {
        brain::CloseBrainOwnedAccessoryForInvalidAircraft(state);
    }
    if (decision.shouldResetSessionRuntimeCaches) {
        brain::ResetBrainOwnedAccessoryForConfirmedColdDark(state);
    }
    brain::ApplyBrainOwnedAircraftRuntimeBoundaryDecision(state, decision);
}

brain::workflow::XPilotSessionBoundaryDecision ApplyXPilotBoundary(
    brain::BrainOwnedRuntimeState* state,
    bool connected,
    const std::string& callsign) {
    brain::workflow::XPilotSessionBoundaryInput input;
    input.xPilotSession.loaded = true;
    input.xPilotSession.connected = connected;
    input.xPilotSession.callsign = callsign;
    input.pilotIdentity.callsign = callsign;
    input.pilotIdentity.normalizedCallsign = callsign;
    input.state = state->xPilotSessionBoundaryState;
    const auto decision = brain::workflow::ResolveXPilotSessionBoundary(input);
    if (decision.shouldResetFlightScopedState) {
        brain::ResetBrainOwnedAccessoryForCallsignChange(
            state,
            input.state.lastConnectedPilotCallsign,
            callsign);
    }
    brain::ApplyBrainOwnedXPilotSessionBoundaryDecision(state, decision);
    return decision;
}

bool KeysBelongTo(
    const brain::BrainOwnedAccessoryHistory& history,
    const std::string& identity,
    const std::string& drawerToken) {
    return std::all_of(
        history.entries.begin(), history.entries.end(),
        [&](const brain::BrainOwnedAccessoryHistoryEntry& entry) {
            return StartsWith(entry.stableKey, identity) &&
                Contains(entry.stableKey, drawerToken);
        });
}

void VerifySeededState(
    const brain::BrainOwnedRuntimeState& state,
    const fixture::Step3LiveProofFixtureSeedResult& first,
    ProofState* proof) {
    const auto marker = std::string{fixture::SyntheticStep3ProofMarker()};
    const auto identity = std::string{fixture::Step3FixtureIdentity()};
    const auto& metar = state.accessory.histories[0];
    const auto& atis = state.accessory.histories[1];
    const auto& pdc = state.accessory.histories[2];

    proof->Require(first.status ==
        brain::BrainOwnedAccessoryOperationStatus::Available,
        "first invocation status unavailable");
    proof->Require(first.seeded && !first.noOp && first.invocationCount == 1,
        "first invocation did not seed exactly once");
    proof->Require(first.acceptedMutationCount == 40,
        "unexpected accepted fixture mutation count");
    proof->Require(first.contentLimitedMutationCount == 1,
        "content-limited mutation count is not one");
    proof->Require(state.accessory.activeDrawer ==
        brain::BrainOwnedAccessoryDrawerId::None,
        "fixture seeding opened a drawer");
    proof->Require(state.accessory.cachedPresentationSnapshot == nullptr,
        "fixture seeding projected a presentation while hidden");

    proof->Require(metar.entries.size() == 2 && atis.entries.size() == 2,
        "METAR or ATIS retained count is incorrect");
    proof->Require(pdc.entries.size() < 32 && pdc.entries.size() > 1,
        "PDC byte retention did not evict beyond the count bound");
    proof->Require(pdc.retainedBytes <= 65536,
        "PDC retained bytes exceed the approved budget");
    proof->Require(NewestFirst(metar) && NewestFirst(atis) && NewestFirst(pdc),
        "fixture histories are not newest-first by accepted sequence");
    proof->Require(
        EveryEntryMarked(metar, marker) && EveryEntryMarked(atis, marker) &&
        EveryEntryMarked(pdc, marker),
        "a retained fixture title or body lacks the synthetic marker");
    proof->Require(
        KeysBelongTo(metar, identity, "METAR") &&
        KeysBelongTo(atis, identity, "ATIS") &&
        KeysBelongTo(pdc, identity, "PDC"),
        "fixture histories are not isolated by drawer-specific keys");

    const auto hiddenKey = std::string{fixture::Step3HiddenUpdateStableKey()};
    proof->Require(CountKey(metar, hiddenKey) == 1,
        "hidden stable-key update retained a duplicate");
    proof->Require(!metar.entries.empty() &&
        metar.entries.front().stableKey == hiddenKey &&
        Contains(metar.entries.front().body, "Updated deterministic METAR"),
        "hidden stable-key update is not the newest current entry");
    proof->Require(
        first.hiddenUpdateGenerationBefore + 1 ==
            first.hiddenUpdateGenerationAfter &&
        first.hiddenUpdateGenerationAfter == metar.generation &&
        first.hiddenUpdateAcceptedSequence ==
            metar.entries.front().acceptedSequence,
        "hidden update generation or sequence is incorrect");

    const auto limitedKey = std::string{fixture::Step3LimitedStableKey()};
    proof->Require(!pdc.entries.empty() &&
        pdc.entries.front().stableKey == limitedKey &&
        pdc.entries.front().contentLimited &&
        Contains(pdc.entries.front().body, "CONTENT LIMITED"),
        "visible CONTENT LIMITED fixture entry was not retained at the top");
    proof->Require(Contains(atis.entries.front().body, "\xCE\xA9") &&
        Contains(atis.entries.back().body, "\xE9\x9B\xAA"),
        "UTF-8 fixture content was not retained");
}

}  // namespace

int main() {
    ProofState proof;

    brain::BrainOwnedRuntimeState invalidState;
    fixture::Step3LiveProofFixtureSession invalidSession;
    const auto invalidBoundary = ResolveAircraftBoundary(
        &invalidState, false, false);
    ApplyAircraftBoundary(&invalidState, invalidBoundary);
    fixture::Step3LiveProofFixtureLifecycleInput invalidInput;
    invalidInput.aircraftBoundaryApplied = true;
    const auto invalidTrigger =
        fixture::TrySeedStep3LiveProofFixturesAfterStableRuntimeSample(
            &invalidState, &invalidSession, invalidInput);
    proof.Require(invalidBoundary.aircraftStateInvalid,
        "invalid aircraft did not enter the production invalid boundary");
    proof.Require(invalidTrigger.noOp && invalidTrigger.triggerDeferred &&
        !invalidTrigger.triggerEligible && !invalidTrigger.seeded &&
        invalidSession.triggerArmed && invalidSession.invocationCount == 0 &&
        HistoriesEmpty(invalidState),
        "invalid aircraft sample seeded or disarmed the fixture trigger");

    brain::BrainOwnedRuntimeState state;
    fixture::Step3LiveProofFixtureSession session;

    proof.Require(HistoriesEmpty(state),
        "fresh process state was not empty");
    const auto coldDarkBoundary = ResolveAircraftBoundary(&state, true, false);
    ApplyAircraftBoundary(&state, coldDarkBoundary);
    fixture::Step3LiveProofFixtureLifecycleInput coldDarkInput;
    coldDarkInput.aircraftSampleValid = true;
    coldDarkInput.aircraftBoundaryApplied = true;
    const auto coldDarkTrigger =
        fixture::TrySeedStep3LiveProofFixturesAfterStableRuntimeSample(
            &state, &session, coldDarkInput);
    proof.Require(coldDarkBoundary.coldDarkBoundaryActive &&
        coldDarkBoundary.shouldResetSessionRuntimeCaches,
        "initial battery-off sample did not apply the production cold-dark clear");
    proof.Require(coldDarkTrigger.noOp && coldDarkTrigger.triggerDeferred &&
        session.triggerArmed && session.invocationCount == 0 &&
        HistoriesEmpty(state),
        "initial cold-dark processing did not precede fixture seeding");

    const auto batteryOnBoundary = ResolveAircraftBoundary(&state, true, true);
    ApplyAircraftBoundary(&state, batteryOnBoundary);
    const auto initialXPilotBoundary = ApplyXPilotBoundary(&state, false, "");
    proof.Require(!batteryOnBoundary.aircraftStateInvalid &&
        !batteryOnBoundary.coldDarkBoundaryActive &&
        !initialXPilotBoundary.shouldResetFlightScopedState,
        "stable startup boundary unexpectedly requested a clear");
    state.lastAircraftStateSnapshot.valid = true;
    state.lastAircraftStateSnapshot.batteryOn = true;
    const auto resolvedStableInput =
        fixture::ResolveStep3LiveProofFixtureLifecycleInputAfterRuntimePass(
            state, state.accessory.historyClearGeneration);
    proof.Require(resolvedStableInput.aircraftSampleValid &&
        resolvedStableInput.aircraftBoundaryApplied &&
        resolvedStableInput.xPilotBoundaryApplied &&
        resolvedStableInput.runtimeClearBoundariesComplete,
        "production host gate did not recognize the post-boundary stable sample");
    const auto first =
        fixture::TrySeedStep3LiveProofFixturesAfterStableRuntimeSample(
            &state, &session, resolvedStableInput);
    VerifySeededState(state, first, &proof);
    proof.Require(first.triggerEligible && first.triggerDisarmed &&
        !session.triggerArmed && session.attempted && session.seeded,
        "successful stable-runtime seed did not permanently disarm the trigger");

    const auto seededDigest = HistoryDigest(state);
    const auto seededState = state;
    const auto seededSession = session;
    const auto generationsBeforeDisable = std::array<std::uint64_t, 3>{
        state.accessory.histories[0].generation,
        state.accessory.histories[1].generation,
        state.accessory.histories[2].generation};
    const auto disable = brain::DisableBrainOwnedAccessoryRuntime(&state);
    const auto enable = brain::EnableBrainOwnedAccessoryRuntime(&state);
    proof.Require(
        disable.status == brain::BrainOwnedAccessoryOperationStatus::Available &&
        enable.status == brain::BrainOwnedAccessoryOperationStatus::Available,
        "disable/enable production lifecycle failed");
    proof.Require(HistoryDigest(state) == seededDigest,
        "disable/enable altered fixture histories");

    const auto second =
        fixture::TrySeedStep3LiveProofFixturesAfterStableRuntimeSample(
            &state, &session, StableRuntimeInput());
    proof.Require(second.noOp && second.seeded && second.triggerDisarmed &&
        second.invocationCount == 1,
        "post-enable runtime update was not a complete fixture no-op");
    proof.Require(second.acceptedMutationCount == 0 &&
        second.contentLimitedMutationCount == 0,
        "post-enable runtime update accepted fixture mutations");
    proof.Require(HistoryDigest(state) == seededDigest,
        "post-enable runtime update duplicated, reordered, or replaced history");
    proof.Require(
        generationsBeforeDisable == std::array<std::uint64_t, 3>{
            state.accessory.histories[0].generation,
            state.accessory.histories[1].generation,
            state.accessory.histories[2].generation},
        "disable/enable or post-enable update advanced generations");

    for (int update = 0; update < 1000; ++update) {
        const auto repeated =
            fixture::TrySeedStep3LiveProofFixturesAfterStableRuntimeSample(
                &state, &session, StableRuntimeInput());
        if (!repeated.noOp || !repeated.triggerDisarmed ||
            repeated.invocationCount != 1 || !repeated.seeded) {
            proof.failures.push_back(
                "a repeated runtime update was not a complete fixture no-op");
            break;
        }
    }
    ++proof.assertions;
    proof.Require(HistoryDigest(state) == seededDigest &&
        session.invocationCount == 1,
        "repeated runtime updates changed history or reseeded");

    brain::BrainOwnedAccessorySelectionRequest openMetar;
    openMetar.drawer = brain::BrainOwnedAccessoryDrawerId::Metar;
    openMetar.requestSequence = 1;
    const auto opened = brain::RequestBrainOwnedAccessoryDrawerSelection(
        &state, openMetar);
    brain::BrainOwnedAccessoryProjectionCounters projectionCounters;
    const auto presentation = brain::ProjectBrainOwnedAccessoryPresentation(
        &state, 1, &projectionCounters);
    proof.Require(opened.action == brain::BrainOwnedAccessoryDrawerAction::Opened,
        "production selection path did not open METAR");
    proof.Require(presentation.snapshot != nullptr &&
        !presentation.snapshot->entries.empty() &&
        presentation.snapshot->entries.front().stableKey ==
            fixture::Step3HiddenUpdateStableKey() &&
        Contains(presentation.snapshot->entries.front().body,
            "Updated deterministic METAR"),
        "first open did not project the latest hidden-cache generation");
    proof.Require(projectionCounters.historyVisits == 2 &&
        projectionCounters.entriesCopied == 2 &&
        projectionCounters.snapshotBuilds == 1,
        "first-open production projection counters are incorrect");

    auto laterColdDarkState = seededState;
    auto laterColdDarkSession = seededSession;
    const auto laterColdDarkBoundary = ResolveAircraftBoundary(
        &laterColdDarkState, true, false);
    const auto laterClearGenerationBefore =
        laterColdDarkState.accessory.historyClearGeneration;
    ApplyAircraftBoundary(&laterColdDarkState, laterColdDarkBoundary);
    const auto resolvedClearingInput =
        fixture::ResolveStep3LiveProofFixtureLifecycleInputAfterRuntimePass(
            laterColdDarkState, laterClearGenerationBefore);
    const auto laterColdDarkNoOp =
        fixture::TrySeedStep3LiveProofFixturesAfterStableRuntimeSample(
            &laterColdDarkState, &laterColdDarkSession, coldDarkInput);
    const auto laterBatteryOnBoundary = ResolveAircraftBoundary(
        &laterColdDarkState, true, true);
    ApplyAircraftBoundary(&laterColdDarkState, laterBatteryOnBoundary);
    const auto laterColdDarkStableNoOp =
        fixture::TrySeedStep3LiveProofFixturesAfterStableRuntimeSample(
            &laterColdDarkState,
            &laterColdDarkSession,
            StableRuntimeInput());
    proof.Require(laterColdDarkBoundary.shouldResetSessionRuntimeCaches &&
        HistoriesEmpty(laterColdDarkState),
        "genuine later cold-dark boundary did not clear fixture histories");
    proof.Require(!resolvedClearingInput.runtimeClearBoundariesComplete,
        "production host gate did not detect a same-pass history clear");
    proof.Require(laterColdDarkNoOp.noOp &&
        laterColdDarkStableNoOp.noOp &&
        laterColdDarkSession.invocationCount == 1 &&
        HistoriesEmpty(laterColdDarkState),
        "later cold-dark clear was reseeded");

    auto sessionResetState = seededState;
    auto sessionResetSession = seededSession;
    brain::ResetBrainOwnedAccessoryForSessionReset(&sessionResetState);
    const auto sessionResetNoOp =
        fixture::TrySeedStep3LiveProofFixturesAfterStableRuntimeSample(
            &sessionResetState,
            &sessionResetSession,
            StableRuntimeInput());
    proof.Require(HistoriesEmpty(sessionResetState) && sessionResetNoOp.noOp &&
        sessionResetSession.invocationCount == 1,
        "session reset did not remain cleared without reseeding");

    auto callsignState = seededState;
    auto callsignSession = seededSession;
    brain::ResetBrainOwnedAccessoryForCallsignChange(
        &callsignState, "N100PC", "N844PC");
    const auto callsignNoOp =
        fixture::TrySeedStep3LiveProofFixturesAfterStableRuntimeSample(
            &callsignState, &callsignSession, StableRuntimeInput());
    proof.Require(HistoriesEmpty(callsignState) && callsignNoOp.noOp &&
        callsignState.accessory.callsignIdentity == "N844PC" &&
        callsignSession.invocationCount == 1,
        "callsign change did not clear fixture history permanently");

    auto stoppedState = seededState;
    brain::StopBrainOwnedAccessoryRuntime(&stoppedState);
    proof.Require(HistoriesEmpty(stoppedState),
        "plugin stop did not clear fixture histories");

    brain::BrainOwnedRuntimeState batteryOnState;
    fixture::Step3LiveProofFixtureSession batteryOnSession;
    const auto immediateBatteryOnBoundary = ResolveAircraftBoundary(
        &batteryOnState, true, true);
    ApplyAircraftBoundary(&batteryOnState, immediateBatteryOnBoundary);
    ApplyXPilotBoundary(&batteryOnState, true, "N844PC");
    const auto immediateBatteryOnSeed =
        fixture::TrySeedStep3LiveProofFixturesAfterStableRuntimeSample(
            &batteryOnState, &batteryOnSession, StableRuntimeInput());
    proof.Require(immediateBatteryOnSeed.seeded &&
        immediateBatteryOnSeed.triggerDisarmed &&
        !HistoriesEmpty(batteryOnState),
        "startup with battery already on did not seed correctly");

    brain::BrainOwnedRuntimeState restartedState;
    fixture::Step3LiveProofFixtureSession restartedSession;
    const auto restarted =
        fixture::TrySeedStep3LiveProofFixturesAfterStableRuntimeSample(
            &restartedState, &restartedSession, StableRuntimeInput());
    proof.Require(restarted.seeded && !restarted.noOp &&
        restarted.invocationCount == 1 && restarted.triggerDisarmed,
        "new process state did not permit one fresh seed");

    brain::BrainOwnedRuntimeState hostState;
    fixture::Step3LiveProofFixtureSession hostSession;
    fixture::Step3LiveProofFixtureHostCounters hostCounters;
    fixture::Step3LiveProofFixtureCallbackRegistrationState
        callbackRegistrationState;
    std::uint64_t originalCallbackExecutions = 0;
    std::uint64_t originalRefconMatches = 0;
    std::uint64_t originalCadenceMatches = 0;
    std::uint64_t proxyRegistrations = 0;
    std::uint64_t originalRegistrations = 0;
    std::uint64_t proxyUnregistrations = 0;
    std::uint64_t originalUnregistrations = 0;
    std::uint64_t unregistersInsideCallback = 0;
    bool insideFlightLoopCallback = false;
    int originalRefconIdentity = 0;
    constexpr float originalCadence = 0.25F;
    const auto applyRegistrationActions = [&](const auto& actions) {
        proxyRegistrations += actions.registerProxy ? 1 : 0;
        originalRegistrations += actions.registerOriginal ? 1 : 0;
        proxyUnregistrations += actions.unregisterProxy ? 1 : 0;
        originalUnregistrations += actions.unregisterOriginal ? 1 : 0;
        if (insideFlightLoopCallback &&
            (actions.unregisterProxy || actions.unregisterOriginal)) {
            ++unregistersInsideCallback;
        }
    };
    const auto simulateOriginalProductionCallback = [&](void* refcon) {
        ++originalCallbackExecutions;
        if (refcon == &originalRefconIdentity) {
            ++originalRefconMatches;
        }
        hostState.lastAircraftStateSnapshot.valid = true;
        hostState.lastAircraftStateSnapshot.batteryOn = true;
        hostState.aircraftStateInvalidBoundaryActive = false;
        hostState.coldDarkResetApplied = false;
        const auto returnedCadence = originalCadence;
        if (returnedCadence == originalCadence) {
            ++originalCadenceMatches;
        }
        return returnedCadence;
    };
    proof.Require(fixture::Step3LiveProofFixtureProxyRequired(&hostSession),
        "fresh fixture host did not require its one-shot proxy");
    const auto initialRegistration =
        fixture::BeginStep3LiveProofFixtureCallbackRegistration(
            true, &callbackRegistrationState);
    applyRegistrationActions(initialRegistration);
    proof.Require(initialRegistration.registerProxy &&
        !initialRegistration.registerOriginal &&
        callbackRegistrationState.proxyRegistered &&
        callbackRegistrationState.proxyActive &&
        !callbackRegistrationState.originalRegistered &&
        proxyRegistrations == 1 && originalRegistrations == 0,
        "initial fixture host did not register one active proxy only");
    const auto hostClearGenerationBefore =
        hostState.accessory.historyClearGeneration;
    insideFlightLoopCallback = true;
    const auto firstReturnedCadence =
        simulateOriginalProductionCallback(&originalRefconIdentity);
    const auto hostEvaluation =
        fixture::EvaluateStep3LiveProofFixtureHostAfterRuntimePass(
            &hostState,
            &hostSession,
            hostClearGenerationBefore,
            &hostCounters);
    const auto selfDetachRegistration =
        fixture::CompleteStep3LiveProofFixtureProxySeed(
            &callbackRegistrationState);
    applyRegistrationActions(selfDetachRegistration);
    insideFlightLoopCallback = false;
    proof.Require(hostEvaluation.detachProxy && hostEvaluation.seedResult.seeded &&
        !fixture::Step3LiveProofFixtureProxyRequired(&hostSession),
        "successful host seed did not self-detach the fixture proxy");
    proof.Require(selfDetachRegistration.registerOriginal &&
        !selfDetachRegistration.unregisterProxy &&
        !selfDetachRegistration.unregisterOriginal &&
        callbackRegistrationState.proxyRegistered &&
        !callbackRegistrationState.proxyActive &&
        callbackRegistrationState.originalRegistered &&
        proxyRegistrations == 1 && originalRegistrations == 1,
        "self-detachment did not retain an inactive registered proxy and "
        "restore the original callback exactly once");
    proof.Require(originalCallbackExecutions == 1 &&
        hostCounters.lifecycleEvaluations == 1 &&
        hostCounters.stateReads == 1 && hostCounters.resolverCalls == 1 &&
        hostCounters.seedAttempts == 1 && hostCounters.detachments == 1,
        "one-shot host accounting is incorrect before detachment");
    proof.Require(firstReturnedCadence == originalCadence &&
        originalRefconMatches == 1 && originalCadenceMatches == 1,
        "one-shot proxy did not preserve the original callback cadence/refcon");
    const auto postDetachCounters = hostCounters;
    const auto postDetachDigest = HistoryDigest(hostState);
    const auto postDetachInvocationCount = hostSession.invocationCount;
    for (int callback = 0; callback < 10000; ++callback) {
        simulateOriginalProductionCallback(&originalRefconIdentity);
    }
    proof.Require(originalCallbackExecutions == 10001,
        "original callback did not execute once for each simulated invocation");
    proof.Require(originalRefconMatches == 10001 &&
        originalCadenceMatches == 10001,
        "detached original callback did not retain its cadence/refcon");
    proof.Require(hostCounters.lifecycleEvaluations ==
        postDetachCounters.lifecycleEvaluations,
        "post-seed callbacks performed fixture lifecycle evaluations");
    proof.Require(hostCounters.stateReads == postDetachCounters.stateReads,
        "post-seed callbacks performed fixture state reads");
    proof.Require(hostCounters.resolverCalls == postDetachCounters.resolverCalls,
        "post-seed callbacks invoked the fixture lifecycle resolver");
    proof.Require(hostCounters.seedAttempts == postDetachCounters.seedAttempts,
        "post-seed callbacks attempted fixture seeding");
    proof.Require(HistoryDigest(hostState) == postDetachDigest,
        "post-seed callbacks mutated fixture history");
    proof.Require(hostSession.invocationCount == postDetachInvocationCount &&
        hostSession.invocationCount == 1,
        "post-seed callbacks reseeded the fixture session");

    const auto firstDisable =
        fixture::StopStep3LiveProofFixtureCallbackRegistration(
            &callbackRegistrationState);
    applyRegistrationActions(firstDisable);
    proof.Require(firstDisable.unregisterOriginal &&
        firstDisable.unregisterProxy &&
        proxyUnregistrations == 1 && originalUnregistrations == 1 &&
        !callbackRegistrationState.proxyRegistered &&
        !callbackRegistrationState.proxyActive &&
        !callbackRegistrationState.originalRegistered,
        "first disable did not explicitly unregister both callback records");

    const auto reenableRegistration =
        fixture::BeginStep3LiveProofFixtureCallbackRegistration(
            fixture::Step3LiveProofFixtureProxyRequired(&hostSession),
            &callbackRegistrationState);
    applyRegistrationActions(reenableRegistration);
    proof.Require(!reenableRegistration.registerProxy &&
        reenableRegistration.registerOriginal &&
        !callbackRegistrationState.proxyRegistered &&
        !callbackRegistrationState.proxyActive &&
        callbackRegistrationState.originalRegistered &&
        proxyRegistrations == 1 && originalRegistrations == 2,
        "re-enable registered anything other than the original callback");

    const auto secondDisable =
        fixture::StopStep3LiveProofFixtureCallbackRegistration(
            &callbackRegistrationState);
    applyRegistrationActions(secondDisable);
    proof.Require(secondDisable.unregisterOriginal &&
        !secondDisable.unregisterProxy &&
        proxyUnregistrations == 1 && originalUnregistrations == 2 &&
        !callbackRegistrationState.proxyRegistered &&
        !callbackRegistrationState.proxyActive &&
        !callbackRegistrationState.originalRegistered,
        "second disable did not unregister only the original callback");

    const auto finalStop =
        fixture::StopStep3LiveProofFixtureCallbackRegistration(
            &callbackRegistrationState);
    applyRegistrationActions(finalStop);
    proof.Require(!finalStop.unregisterOriginal &&
        !finalStop.unregisterProxy &&
        proxyUnregistrations == 1 && originalUnregistrations == 2,
        "final plugin stop did not leave the callback registry empty");
    proof.Require(unregistersInsideCallback == 0,
        "an unregister operation was attempted inside a flight-loop callback");

    std::cout << "STEP3_LIVE_FIXTURE_PROOF\n"
              << "fixtureIdentity=" << fixture::Step3FixtureIdentity() << '\n'
              << "syntheticMarker=" << fixture::SyntheticStep3ProofMarker() << '\n'
              << "firstInvocationSeeded=" << first.seeded << '\n'
              << "firstAcceptedMutations=" << first.acceptedMutationCount << '\n'
              << "postEnableRuntimeNoOp=" << second.noOp << '\n'
              << "initialColdDarkDeferred=" << coldDarkTrigger.triggerDeferred << '\n'
              << "invalidAircraftDeferred=" << invalidTrigger.triggerDeferred << '\n'
              << "triggerDisarmedAfterSeed=" << first.triggerDisarmed << '\n'
              << "disableEnablePreserved=true\n"
              << "historyCounts="
              << first.retainedEntryCounts[0] << ','
              << first.retainedEntryCounts[1] << ','
              << first.retainedEntryCounts[2] << '\n'
              << "historyGenerations="
              << first.historyGenerations[0] << ','
              << first.historyGenerations[1] << ','
              << first.historyGenerations[2] << '\n'
              << "historyRetainedBytes="
              << first.retainedByteCounts[0] << ','
              << first.retainedByteCounts[1] << ','
              << first.retainedByteCounts[2] << '\n'
              << "hiddenUpdateGeneration="
              << first.hiddenUpdateGenerationBefore << "->"
              << first.hiddenUpdateGenerationAfter << '\n'
              << "hiddenSeedPresentationProjection=false\n"
              << "hiddenSeedOverlayDependencyLinked=false\n"
              << "hiddenSeedOverlayCallPathAvailable=false\n"
              << "fixtureLinkDependencies=XVatsimStep3LiveProofFixtures,XVatsimBrain\n"
              << "fixtureFileOrNetworkSources=false\n"
              << "fixtureCallbackCountAdded=0\n"
              << "fixtureExistingFlightLoopWrapped=true\n"
              << "fixtureFlightLoopPollingAdded=false\n"
              << "fixtureStartupTrigger=post-aircraft-post-xpilot-post-clear-boundaries\n"
              << "laterColdDarkReseeded=false\n"
              << "sessionResetReseeded=false\n"
              << "callsignChangeReseeded=false\n"
              << "fixtureSessionStorage=process-memory-only\n"
              << "processRestartFreshSeed=" << restarted.seeded << '\n'
              << "postSeedOriginalCallbacks=10000\n"
              << "postSeedOriginalRefconMatches=10000\n"
              << "postSeedOriginalCadenceMatches=10000\n"
              << "postSeedLifecycleEvaluations=0\n"
              << "postSeedFixtureStateReads=0\n"
              << "postSeedResolverCalls=0\n"
              << "postSeedSeedAttempts=0\n"
              << "postSeedHistoryMutations=0\n"
              << "postSeedReseeds=0\n"
              << "initialProxyRegistrations=1\n"
              << "originalCallbackRestorations=1\n"
              << "proxyInactiveButRegisteredAfterSeed=true\n"
              << "firstDisableProxyUnregistrations=1\n"
              << "firstDisableOriginalUnregistrations=1\n"
              << "registeredCallbacksAfterFirstDisable=0\n"
              << "reenableProxyRegistrations=0\n"
              << "reenableOriginalRegistrations=1\n"
              << "secondDisableProxyUnregistrations=0\n"
              << "secondDisableOriginalUnregistrations=1\n"
              << "registeredCallbacksAfterFinalStop=0\n"
              << "unregistersInsideFlightLoopCallback=0\n"
              << "assertions=" << proof.assertions << '\n'
              << "failures=" << proof.failures.size() << '\n';
    for (const auto& failure : proof.failures) {
        std::cout << "FAILURE: " << failure << '\n';
    }
    std::cout << "status="
              << (proof.failures.empty() ? "PASSED" : "FAILED") << '\n';
    return proof.failures.empty() ? 0 : 1;
}
