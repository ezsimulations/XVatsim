#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "XVatsim/brain/BrainOwnedRuntime.h"

namespace xvatsim::plugin::step3_live_proof {

struct Step3LiveProofFixtureSession {
    bool triggerArmed = true;
    bool attempted = false;
    bool seeded = false;
    std::uint64_t invocationCount = 0;
};

struct Step3LiveProofFixtureLifecycleInput {
    bool aircraftSampleValid = false;
    bool aircraftBoundaryApplied = false;
    bool xPilotBoundaryApplied = false;
    bool runtimeClearBoundariesComplete = false;
};

struct Step3LiveProofFixtureSeedResult {
    brain::BrainOwnedAccessoryOperationStatus status =
        brain::BrainOwnedAccessoryOperationStatus::Unavailable;
    bool seeded = false;
    bool noOp = false;
    bool triggerEligible = false;
    bool triggerDeferred = false;
    bool triggerDisarmed = false;
    std::uint64_t invocationCount = 0;
    std::size_t acceptedMutationCount = 0;
    std::size_t contentLimitedMutationCount = 0;
    std::uint64_t hiddenUpdateGenerationBefore = 0;
    std::uint64_t hiddenUpdateGenerationAfter = 0;
    std::uint64_t hiddenUpdateAcceptedSequence = 0;
    std::array<std::size_t, 3> retainedEntryCounts{};
    std::array<std::size_t, 3> retainedByteCounts{};
    std::array<std::uint64_t, 3> historyGenerations{};
};

struct Step3LiveProofFixtureHostCounters {
    std::uint64_t lifecycleEvaluations = 0;
    std::uint64_t stateReads = 0;
    std::uint64_t resolverCalls = 0;
    std::uint64_t seedAttempts = 0;
    std::uint64_t detachments = 0;
};

struct Step3LiveProofFixtureHostEvaluation {
    Step3LiveProofFixtureSeedResult seedResult;
    bool detachProxy = false;
};

struct Step3LiveProofFixtureCallbackRegistrationState {
    bool proxyRegistered = false;
    bool proxyActive = false;
    bool originalRegistered = false;
};

struct Step3LiveProofFixtureCallbackRegistrationActions {
    bool registerProxy = false;
    bool registerOriginal = false;
    bool unregisterProxy = false;
    bool unregisterOriginal = false;
};

const char* SyntheticStep3ProofMarker();
const char* Step3FixtureIdentity();
const char* Step3HiddenUpdateStableKey();
const char* Step3LimitedStableKey();

Step3LiveProofFixtureSeedResult SeedStep3LiveProofFixturesOnce(
    brain::BrainOwnedRuntimeState* brainState,
    Step3LiveProofFixtureSession* fixtureSession);

Step3LiveProofFixtureSeedResult
TrySeedStep3LiveProofFixturesAfterStableRuntimeSample(
    brain::BrainOwnedRuntimeState* brainState,
    Step3LiveProofFixtureSession* fixtureSession,
    const Step3LiveProofFixtureLifecycleInput& input);

Step3LiveProofFixtureLifecycleInput
ResolveStep3LiveProofFixtureLifecycleInputAfterRuntimePass(
    const brain::BrainOwnedRuntimeState& brainState,
    std::uint64_t historyClearGenerationBefore);

Step3LiveProofFixtureHostEvaluation
EvaluateStep3LiveProofFixtureHostAfterRuntimePass(
    brain::BrainOwnedRuntimeState* brainState,
    Step3LiveProofFixtureSession* fixtureSession,
    std::uint64_t historyClearGenerationBefore,
    Step3LiveProofFixtureHostCounters* counters);

bool Step3LiveProofFixtureProxyRequired(
    const Step3LiveProofFixtureSession* fixtureSession);

Step3LiveProofFixtureCallbackRegistrationActions
BeginStep3LiveProofFixtureCallbackRegistration(
    bool proxyRequired,
    Step3LiveProofFixtureCallbackRegistrationState* state);

Step3LiveProofFixtureCallbackRegistrationActions
CompleteStep3LiveProofFixtureProxySeed(
    Step3LiveProofFixtureCallbackRegistrationState* state);

Step3LiveProofFixtureCallbackRegistrationActions
StopStep3LiveProofFixtureCallbackRegistration(
    Step3LiveProofFixtureCallbackRegistrationState* state);

}  // namespace xvatsim::plugin::step3_live_proof

#if defined(XVATSIM_STEP3_LIVE_PROOF_PLUGIN_HOST)
#include "XPLMProcessing.h"

namespace xvatsim::plugin::step3_live_proof {

inline brain::BrainOwnedRuntimeState* gStep3FixtureHostBrainState = nullptr;
inline Step3LiveProofFixtureSession* gStep3FixtureHostSession = nullptr;
inline XPLMFlightLoop_f gStep3FixtureWrappedFlightLoop = nullptr;
inline void* gStep3FixtureWrappedRefcon = nullptr;
inline Step3LiveProofFixtureCallbackRegistrationState
    gStep3FixtureCallbackRegistrationState;
inline Step3LiveProofFixtureHostCounters gStep3FixtureHostCounters;

inline Step3LiveProofFixtureSeedResult ArmStep3LiveProofFixtureHost(
    brain::BrainOwnedRuntimeState* brainState,
    Step3LiveProofFixtureSession* fixtureSession) {
    gStep3FixtureHostBrainState = brainState;
    gStep3FixtureHostSession = fixtureSession;
    Step3LiveProofFixtureSeedResult result;
    result.status = brain::BrainOwnedAccessoryOperationStatus::Available;
    result.seeded = brainState != nullptr && fixtureSession != nullptr;
    result.noOp = true;
    result.triggerDeferred = result.seeded;
    return result;
}

inline float Step3LiveProofFixtureFlightLoopProxy(
    float elapsedSinceLastCall,
    float elapsedTimeSinceLastFlightLoop,
    int counter,
    void* refcon) {
    (void)refcon;
    if (gStep3FixtureWrappedFlightLoop == nullptr) {
        return 0.0F;
    }

    ++gStep3FixtureHostCounters.stateReads;
    const auto historyClearGenerationBefore =
        gStep3FixtureHostBrainState == nullptr
            ? 0
            : gStep3FixtureHostBrainState->accessory.historyClearGeneration;
    const auto nextInterval = gStep3FixtureWrappedFlightLoop(
        elapsedSinceLastCall,
        elapsedTimeSinceLastFlightLoop,
        counter,
        gStep3FixtureWrappedRefcon);

    if (gStep3FixtureHostBrainState == nullptr ||
        gStep3FixtureHostSession == nullptr) {
        return nextInterval;
    }
    const auto evaluation =
        EvaluateStep3LiveProofFixtureHostAfterRuntimePass(
            gStep3FixtureHostBrainState,
            gStep3FixtureHostSession,
            historyClearGenerationBefore,
            &gStep3FixtureHostCounters);
    if (evaluation.detachProxy) {
        const auto actions = CompleteStep3LiveProofFixtureProxySeed(
            &gStep3FixtureCallbackRegistrationState);
        if (actions.registerOriginal) {
            XPLMRegisterFlightLoopCallback(
                gStep3FixtureWrappedFlightLoop,
                nextInterval,
                gStep3FixtureWrappedRefcon);
        }
        return 0.0F;
    }
    return nextInterval;
}

inline void RegisterStep3LiveProofFixtureFlightLoop(
    XPLMFlightLoop_f callback,
    float interval,
    void* refcon) {
    gStep3FixtureWrappedFlightLoop = callback;
    gStep3FixtureWrappedRefcon = refcon;
    const auto actions = BeginStep3LiveProofFixtureCallbackRegistration(
        Step3LiveProofFixtureProxyRequired(gStep3FixtureHostSession),
        &gStep3FixtureCallbackRegistrationState);
    if (actions.registerOriginal) {
        XPLMRegisterFlightLoopCallback(callback, interval, refcon);
        return;
    }
    if (actions.registerProxy) {
        XPLMRegisterFlightLoopCallback(
            Step3LiveProofFixtureFlightLoopProxy, interval, nullptr);
    }
}

inline void UnregisterStep3LiveProofFixtureFlightLoop(
    XPLMFlightLoop_f callback,
    void* refcon) {
    (void)callback;
    (void)refcon;
    const auto actions = StopStep3LiveProofFixtureCallbackRegistration(
        &gStep3FixtureCallbackRegistrationState);
    if (actions.unregisterOriginal &&
        gStep3FixtureWrappedFlightLoop != nullptr) {
        XPLMUnregisterFlightLoopCallback(
            gStep3FixtureWrappedFlightLoop,
            gStep3FixtureWrappedRefcon);
    }
    if (actions.unregisterProxy) {
        XPLMUnregisterFlightLoopCallback(
            Step3LiveProofFixtureFlightLoopProxy, nullptr);
    }
    gStep3FixtureWrappedFlightLoop = nullptr;
    gStep3FixtureWrappedRefcon = nullptr;
}

}  // namespace xvatsim::plugin::step3_live_proof

#define SeedStep3LiveProofFixturesOnce \
    ArmStep3LiveProofFixtureHost
#define XPLMRegisterFlightLoopCallback(callback, interval, refcon) \
    xvatsim::plugin::step3_live_proof::RegisterStep3LiveProofFixtureFlightLoop( \
        callback, interval, refcon)
#define XPLMUnregisterFlightLoopCallback(callback, refcon) \
    xvatsim::plugin::step3_live_proof::UnregisterStep3LiveProofFixtureFlightLoop( \
        callback, refcon)
#endif
