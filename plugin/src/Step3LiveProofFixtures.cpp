#include "Step3LiveProofFixtures.h"

#include <iomanip>
#include <sstream>
#include <string>

namespace xvatsim::plugin::step3_live_proof {
namespace {

constexpr char kSyntheticMarker[] =
    "SYNTHETIC STEP 3 PROOF \xE2\x80\x94 NOT LIVE DATA";
constexpr char kFixtureIdentity[] = "XVATSIM_STEP3_LIVE_FIXTURE";
constexpr char kHiddenUpdateKey[] =
    "XVATSIM_STEP3_LIVE_FIXTURE_METAR_HIDDEN_UPDATE";
constexpr char kLimitedKey[] =
    "XVATSIM_STEP3_LIVE_FIXTURE_PDC_CONTENT_LIMITED";

std::string FixtureTitle(const std::string& detail) {
    return std::string{kSyntheticMarker} + " | " + detail;
}

std::string FixtureBody(const std::string& detail) {
    return std::string{kSyntheticMarker} + "\n" + detail;
}

std::string NumberedKey(const char* drawer, int index) {
    std::ostringstream stream;
    stream << kFixtureIdentity << '_' << drawer << '_'
           << std::setw(2) << std::setfill('0') << index;
    return stream.str();
}

brain::BrainOwnedAccessoryHistoryDecision Accept(
    brain::BrainOwnedRuntimeState* state,
    brain::BrainOwnedAccessoryDrawerId drawer,
    const std::string& key,
    const std::string& title,
    const std::string& body,
    Step3LiveProofFixtureSeedResult* result) {
    brain::BrainOwnedAccessoryHistoryEntryInput input;
    input.drawer = drawer;
    input.stableKey = key;
    input.title = title;
    input.body = body;
    const auto decision = brain::AcceptBrainOwnedAccessoryHistoryEntry(
        state, input);
    if (result != nullptr && decision.accepted) {
        ++result->acceptedMutationCount;
        if (decision.contentLimited) {
            ++result->contentLimitedMutationCount;
        }
    }
    return decision;
}

bool SeedMetar(
    brain::BrainOwnedRuntimeState* state,
    Step3LiveProofFixtureSeedResult* result) {
    const auto first = Accept(
        state,
        brain::BrainOwnedAccessoryDrawerId::Metar,
        kHiddenUpdateKey,
        FixtureTitle("METAR HIDDEN UPDATE"),
        FixtureBody("Earlier deterministic METAR fixture version."),
        result);
    std::string normalText;
    for (int index = 0; index < 36; ++index) {
        normalText +=
            "Production wrapping retains readable fixture words in order. ";
    }
    const auto older = Accept(
        state,
        brain::BrainOwnedAccessoryDrawerId::Metar,
        NumberedKey("METAR", 1),
        FixtureTitle("METAR RETAINED HISTORY"),
        FixtureBody(normalText),
        result);
    if (result != nullptr) {
        result->hiddenUpdateGenerationBefore = older.historyGeneration;
    }
    const auto updated = Accept(
        state,
        brain::BrainOwnedAccessoryDrawerId::Metar,
        kHiddenUpdateKey,
        FixtureTitle("METAR HIDDEN UPDATE"),
        FixtureBody(
            "Updated deterministic METAR fixture version; this is the only "
            "current value for the stable key."),
        result);
    if (result != nullptr) {
        result->hiddenUpdateGenerationAfter = updated.historyGeneration;
        result->hiddenUpdateAcceptedSequence = updated.acceptedSequence;
    }
    return first.accepted && older.accepted && updated.accepted &&
        updated.historyGeneration == older.historyGeneration + 1;
}

bool SeedAtis(
    brain::BrainOwnedRuntimeState* state,
    Step3LiveProofFixtureSeedResult* result) {
    const auto retained = Accept(
        state,
        brain::BrainOwnedAccessoryDrawerId::Atis,
        NumberedKey("ATIS", 1),
        FixtureTitle("ATIS UTF-8 RETAINED HISTORY"),
        FixtureBody(
            "UTF-8 proof: \xE9\x9B\xAA \xE2\x98\x83 \xCE\xA9; newest entries "
            "remain above older entries."),
        result);
    std::string unbroken;
    for (int index = 0; index < 1800; ++index) {
        unbroken += (index % 3 == 0) ? "W" : "\xCE\xA9";
    }
    const auto token = Accept(
        state,
        brain::BrainOwnedAccessoryDrawerId::Atis,
        NumberedKey("ATIS", 2),
        FixtureTitle("ATIS UNBROKEN UTF-8 TOKEN"),
        FixtureBody(unbroken),
        result);
    return retained.accepted && token.accepted;
}

bool SeedPdc(
    brain::BrainOwnedRuntimeState* state,
    Step3LiveProofFixtureSeedResult* result) {
    bool accepted = true;
    for (int index = 1; index <= 34; ++index) {
        std::string body = "PDC retained history entry " +
            std::to_string(index) + "\n";
        body += std::string(2100, static_cast<char>('A' + (index % 26)));
        const auto decision = Accept(
            state,
            brain::BrainOwnedAccessoryDrawerId::Pdc,
            NumberedKey("PDC", index),
            FixtureTitle("PDC RETENTION " + std::to_string(index)),
            FixtureBody(body),
            result);
        accepted = accepted && decision.accepted;
    }
    const auto limited = Accept(
        state,
        brain::BrainOwnedAccessoryDrawerId::Pdc,
        kLimitedKey,
        FixtureTitle("PDC CONTENT LIMIT"),
        FixtureBody(
            "Oversized fixture content begins here.\n" +
            std::string(9000, 'L')),
        result);
    return accepted && limited.accepted && limited.contentLimited;
}

}  // namespace

const char* SyntheticStep3ProofMarker() {
    return kSyntheticMarker;
}

const char* Step3FixtureIdentity() {
    return kFixtureIdentity;
}

const char* Step3HiddenUpdateStableKey() {
    return kHiddenUpdateKey;
}

const char* Step3LimitedStableKey() {
    return kLimitedKey;
}

Step3LiveProofFixtureSeedResult SeedStep3LiveProofFixturesOnce(
    brain::BrainOwnedRuntimeState* brainState,
    Step3LiveProofFixtureSession* fixtureSession) {
    Step3LiveProofFixtureSeedResult result;
    if (brainState == nullptr || fixtureSession == nullptr) {
        return result;
    }
    result.status = brain::BrainOwnedAccessoryOperationStatus::Available;
    ++fixtureSession->invocationCount;
    result.invocationCount = fixtureSession->invocationCount;
    if (fixtureSession->attempted) {
        result.seeded = fixtureSession->seeded;
        result.noOp = true;
        return result;
    }

    fixtureSession->attempted = true;
    const bool metarSeeded = SeedMetar(brainState, &result);
    const bool atisSeeded = SeedAtis(brainState, &result);
    const bool pdcSeeded = SeedPdc(brainState, &result);
    fixtureSession->seeded = metarSeeded && atisSeeded && pdcSeeded;
    result.seeded = fixtureSession->seeded;

    for (std::size_t index = 0; index < brainState->accessory.histories.size();
         ++index) {
        const auto& history = brainState->accessory.histories[index];
        result.retainedEntryCounts[index] = history.entries.size();
        result.retainedByteCounts[index] = history.retainedBytes;
        result.historyGenerations[index] = history.generation;
    }
    return result;
}

Step3LiveProofFixtureSeedResult
TrySeedStep3LiveProofFixturesAfterStableRuntimeSample(
    brain::BrainOwnedRuntimeState* brainState,
    Step3LiveProofFixtureSession* fixtureSession,
    const Step3LiveProofFixtureLifecycleInput& input) {
    Step3LiveProofFixtureSeedResult result;
    if (brainState == nullptr || fixtureSession == nullptr) {
        return result;
    }

    result.status = brain::BrainOwnedAccessoryOperationStatus::Available;
    result.seeded = fixtureSession->seeded;
    result.invocationCount = fixtureSession->invocationCount;
    result.triggerDisarmed = !fixtureSession->triggerArmed;
    if (!fixtureSession->triggerArmed) {
        result.noOp = true;
        return result;
    }

    result.triggerEligible = input.aircraftSampleValid &&
        input.aircraftBoundaryApplied && input.xPilotBoundaryApplied &&
        input.runtimeClearBoundariesComplete;
    if (!result.triggerEligible) {
        result.noOp = true;
        result.triggerDeferred = true;
        return result;
    }

    result = SeedStep3LiveProofFixturesOnce(brainState, fixtureSession);
    result.triggerEligible = true;
    if (result.seeded) {
        fixtureSession->triggerArmed = false;
        result.triggerDisarmed = true;
    }
    return result;
}

Step3LiveProofFixtureLifecycleInput
ResolveStep3LiveProofFixtureLifecycleInputAfterRuntimePass(
    const brain::BrainOwnedRuntimeState& brainState,
    std::uint64_t historyClearGenerationBefore) {
    Step3LiveProofFixtureLifecycleInput input;
    const auto stableRuntimeSample =
        brainState.lastAircraftStateSnapshot.valid &&
        !brainState.aircraftStateInvalidBoundaryActive &&
        !brainState.coldDarkResetApplied;
    input.aircraftSampleValid = stableRuntimeSample;
    input.aircraftBoundaryApplied = stableRuntimeSample;
    input.xPilotBoundaryApplied = stableRuntimeSample;
    input.runtimeClearBoundariesComplete = stableRuntimeSample &&
        brainState.accessory.historyClearGeneration ==
            historyClearGenerationBefore;
    return input;
}

Step3LiveProofFixtureHostEvaluation
EvaluateStep3LiveProofFixtureHostAfterRuntimePass(
    brain::BrainOwnedRuntimeState* brainState,
    Step3LiveProofFixtureSession* fixtureSession,
    std::uint64_t historyClearGenerationBefore,
    Step3LiveProofFixtureHostCounters* counters) {
    Step3LiveProofFixtureHostEvaluation evaluation;
    if (brainState == nullptr || fixtureSession == nullptr) {
        return evaluation;
    }
    if (counters != nullptr) {
        ++counters->stateReads;
        ++counters->resolverCalls;
    }
    const auto input =
        ResolveStep3LiveProofFixtureLifecycleInputAfterRuntimePass(
            *brainState, historyClearGenerationBefore);
    if (counters != nullptr) {
        ++counters->lifecycleEvaluations;
    }
    const auto invocationCountBefore = fixtureSession->invocationCount;
    evaluation.seedResult =
        TrySeedStep3LiveProofFixturesAfterStableRuntimeSample(
            brainState, fixtureSession, input);
    if (counters != nullptr &&
        fixtureSession->invocationCount != invocationCountBefore) {
        ++counters->seedAttempts;
    }
    evaluation.detachProxy = evaluation.seedResult.seeded &&
        evaluation.seedResult.triggerDisarmed;
    if (evaluation.detachProxy && counters != nullptr) {
        ++counters->detachments;
    }
    return evaluation;
}

bool Step3LiveProofFixtureProxyRequired(
    const Step3LiveProofFixtureSession* fixtureSession) {
    return fixtureSession != nullptr && fixtureSession->triggerArmed;
}

Step3LiveProofFixtureCallbackRegistrationActions
BeginStep3LiveProofFixtureCallbackRegistration(
    bool proxyRequired,
    Step3LiveProofFixtureCallbackRegistrationState* state) {
    Step3LiveProofFixtureCallbackRegistrationActions actions;
    if (state == nullptr || state->proxyRegistered ||
        state->originalRegistered) {
        return actions;
    }
    if (proxyRequired) {
        state->proxyRegistered = true;
        state->proxyActive = true;
        actions.registerProxy = true;
    } else {
        state->originalRegistered = true;
        actions.registerOriginal = true;
    }
    return actions;
}

Step3LiveProofFixtureCallbackRegistrationActions
CompleteStep3LiveProofFixtureProxySeed(
    Step3LiveProofFixtureCallbackRegistrationState* state) {
    Step3LiveProofFixtureCallbackRegistrationActions actions;
    if (state == nullptr || !state->proxyRegistered ||
        !state->proxyActive) {
        return actions;
    }
    state->proxyActive = false;
    if (!state->originalRegistered) {
        state->originalRegistered = true;
        actions.registerOriginal = true;
    }
    return actions;
}

Step3LiveProofFixtureCallbackRegistrationActions
StopStep3LiveProofFixtureCallbackRegistration(
    Step3LiveProofFixtureCallbackRegistrationState* state) {
    Step3LiveProofFixtureCallbackRegistrationActions actions;
    if (state == nullptr) {
        return actions;
    }
    actions.unregisterOriginal = state->originalRegistered;
    actions.unregisterProxy = state->proxyRegistered;
    *state = Step3LiveProofFixtureCallbackRegistrationState{};
    return actions;
}

}  // namespace xvatsim::plugin::step3_live_proof
