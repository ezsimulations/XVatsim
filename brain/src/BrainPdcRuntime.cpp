#include "XVatsim/brain/BrainPdcRuntime.h"

#include "XVatsim/brain/BrainOwnedRuntime.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace xvatsim::brain {
namespace {

constexpr std::size_t kVisibleLimit = 16;
constexpr std::uint64_t kCrossSourceCorrelationWindowMicroseconds =
    30ULL * 1'000'000ULL;

std::string Trim(std::string value) {
    const auto space = [](unsigned char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    };
    while (!value.empty() && space(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && space(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string Upper(std::string value) {
    for (auto& ch : value) {
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    }
    return value;
}

std::string SenderKey(const std::string& value) { return Upper(Trim(value)); }

bool StableConnected(const BrainPdcMechanicalObservation& value) {
    return value.statusBefore == BrainPdcSourceConnectionStatus::Connected &&
        value.statusAfter == BrainPdcSourceConnectionStatus::Connected &&
        !value.callsignBefore.empty() && value.callsignBefore == value.callsignAfter;
}

bool StableDisconnected(const BrainPdcMechanicalObservation& value) {
    return value.statusBefore == BrainPdcSourceConnectionStatus::Disconnected &&
        value.statusAfter == BrainPdcSourceConnectionStatus::Disconnected &&
        value.callsignBefore == value.callsignAfter;
}

bool FlightBindingReady(
    const BrainPdcEvaluationContext& context,
    const std::string& planIdentity) {
    return context.flightContextActive && !planIdentity.empty() &&
        !Trim(context.departureIcao).empty();
}

std::uint64_t NextEpoch(std::uint64_t value) {
    ++value;
    return value == 0 ? 1 : value;
}

void Mutate(BrainOwnedRuntimeState* state) {
    ++state->pdc.semanticGeneration;
    ++state->pdc.counters.semanticMutations;
    RecordBrainOwnedAccessoryDrawerContentMutation(
        state, BrainOwnedAccessoryDrawerId::Pdc);
}

std::uint64_t ContentDigest(const BrainPdcMechanicalObservation& observation) {
    if (observation.sessionLocalDigest != 0) return observation.sessionLocalDigest;
    std::uint64_t digest = 1469598103934665603ULL;
    const auto append = [&digest](const std::string& value) {
        for (const auto character : value) {
            digest ^= static_cast<unsigned char>(character);
            digest *= 1099511628211ULL;
        }
        digest ^= 0xFFU;
        digest *= 1099511628211ULL;
    };
    append(observation.sender);
    append(observation.body);
    return digest;
}

std::string Revision(
    const BrainPdcRuntimeState& state,
    const BrainPdcMechanicalObservation& observation) {
    std::ostringstream out;
    out << "PDC-R" << state.productLifecycleEpoch << '-' << state.sourceEpoch
        << '-' << observation.sequenceAfter << '-' << std::uppercase << std::hex
        << std::setw(16) << std::setfill('0') << ContentDigest(observation);
    return out.str();
}

std::size_t ArtifactBytes(const BrainPdcCapturedArtifact& value) {
    return value.flightIdentity.size() + value.aircraftCallsign.size() +
        value.departureIcao.size() + value.destinationIcao.size() +
        value.revisionIdentity.size() + value.sender.size() +
        value.normalizedSender.size() + value.body.size() +
        value.acceptanceReason.size() + value.sourceTimeUtc.size() +
        value.sourceKind.size();
}

void BindFlight(
    BrainPdcRuntimeState* pdc,
    const BrainPdcEvaluationContext& context,
    const std::string& planIdentity) {
    pdc->boundPlanIdentity = planIdentity;
    pdc->boundCallsign = Upper(Trim(context.aircraftCallsign));
    pdc->boundDepartureIcao = Upper(Trim(context.departureIcao));
    pdc->boundDestinationIcao = Upper(Trim(context.destinationIcao));
    pdc->boundFlightIdentity = planIdentity + "|pdc-epoch=" +
        std::to_string(pdc->productLifecycleEpoch);
}

}  // namespace

void InitializeBrainOwnedPdcRuntime(BrainOwnedRuntimeState* state) {
    if (state == nullptr || state->pdc.initialized) return;
    BrainOwnedAccessoryVisibleInvalidationBatch batch(state);
    state->pdc = {};
    state->pdc.initialized = true;
    state->pdc.availability = BrainPdcAvailability::SourceNotReady;
    state->pdc.productLifecycleEpoch = 1;
    state->pdc.sourceEpoch = 1;
    Mutate(state);
    RecordBrainOwnedAccessoryLifecycleMutation(state);
}

void ResetBrainOwnedPdcProductState(
    BrainOwnedRuntimeState* state, bool preserveConnectedReplayEvidence) {
    if (state == nullptr) return;
    const auto old = state->pdc;
    state->pdc = {};
    state->pdc.initialized = old.initialized;
    state->pdc.pluginAdminSuspended = old.pluginAdminSuspended;
    state->pdc.availability = old.initialized
        ? BrainPdcAvailability::SourceNotReady : BrainPdcAvailability::Uninitialized;
    state->pdc.productLifecycleEpoch = NextEpoch(old.productLifecycleEpoch);
    state->pdc.sourceEpoch = 1;
    if (preserveConnectedReplayEvidence) {
        state->pdc.sourceQualified = old.sourceQualified;
        state->pdc.sourceAvailable = old.sourceAvailable;
        state->pdc.logSourceAvailable = old.logSourceAvailable;
        state->pdc.xpilot4SourceAvailable = old.xpilot4SourceAvailable;
        state->pdc.pluginInstanceIdentity = old.pluginInstanceIdentity;
        state->pdc.capabilityGeneration = old.capabilityGeneration;
        state->pdc.hasConnectedDisposition = old.hasConnectedDisposition;
        state->pdc.connectedDispositionedSequence =
            old.connectedDispositionedSequence;
        state->pdc.logMonitor.replayCursors = old.logMonitor.replayCursors;
        state->pdc.logMonitor.nextReplayCursorSlot =
            old.logMonitor.nextReplayCursorSlot;
    }
    Mutate(state);
}

void StopBrainOwnedPdcRuntime(BrainOwnedRuntimeState* state) {
    if (state == nullptr) return;
    const auto epoch = NextEpoch(state->pdc.productLifecycleEpoch);
    state->pdc = {};
    state->pdc.productLifecycleEpoch = epoch;
    state->pdc.sourceEpoch = 1;
    Mutate(state);
}

void SuspendBrainOwnedPdcRuntime(BrainOwnedRuntimeState* state) {
    if (state == nullptr || !state->pdc.initialized) return;
    state->pdc.pluginAdminSuspended = true;
    ++state->pdc.logMonitor.connectionEpoch;
    state->pdc.logMonitor.pendingRequestId = 0;
    state->pdc.logMonitor.recoveryRequired = true;
    state->pdc.logMonitor.readingSnapshot = false;
    state->pdc.logMonitor.stagedDirectMessages.clear();
}

void ResumeBrainOwnedPdcRuntime(BrainOwnedRuntimeState* state) {
    if (state == nullptr || !state->pdc.initialized) return;
    state->pdc.pluginAdminSuspended = false;
    state->pdc.logMonitor.recoveryRequired = true;
    state->pdc.logMonitor.nextServiceMicroseconds = 0;
}

void MarkBrainOwnedPdcSourceUnavailable(BrainOwnedRuntimeState* state) {
    SetBrainOwnedPdcSourceAvailability(
        state, BrainPdcEvidenceSource::All, false);
}

void SetBrainOwnedPdcSourceAvailability(
    BrainOwnedRuntimeState* state,
    BrainPdcEvidenceSource source,
    bool available) {
    if (state == nullptr || !state->pdc.initialized) return;
    auto& pdc = state->pdc;
    const auto oldLog = pdc.logSourceAvailable;
    const auto oldXPilot4 = pdc.xpilot4SourceAvailable;
    const auto oldAggregate = pdc.sourceAvailable;
    const auto oldAvailability = pdc.availability;
    if (source == BrainPdcEvidenceSource::All ||
        source == BrainPdcEvidenceSource::XPilot3NetworkLog) {
        pdc.logSourceAvailable = available;
    }
    if (source == BrainPdcEvidenceSource::All ||
        source == BrainPdcEvidenceSource::XPilot4PluginSdk) {
        pdc.xpilot4SourceAvailable = available;
    }
    const bool aggregate =
        pdc.logSourceAvailable || pdc.xpilot4SourceAvailable;
    const auto nextAvailability = aggregate
        ? (BrainOwnedPdcMessageCount(pdc) == 0
               ? BrainPdcAvailability::QualifiedIdle
               : BrainPdcAvailability::Available)
        : BrainPdcAvailability::SourceUnavailable;
    if (oldLog == pdc.logSourceAvailable &&
        oldXPilot4 == pdc.xpilot4SourceAvailable &&
        oldAggregate == aggregate && oldAvailability == nextAvailability) {
        return;
    }
    BrainOwnedAccessoryVisibleInvalidationBatch batch(state);
    if (oldAggregate && !aggregate) {
        pdc.sourceEpoch = NextEpoch(pdc.sourceEpoch);
        pdc.hasConnectedDisposition = false;
        pdc.connectedDispositionedSequence = 0;
        ++pdc.counters.sourceEpochChanges;
    }
    pdc.sourceQualified = aggregate;
    pdc.sourceAvailable = aggregate;
    if (!aggregate) {
        pdc.preCaptureUncertain |= pdc.hasObservedPositiveSequence;
    }
    pdc.availability = nextAvailability;
    Mutate(state);
}

void RecordBrainOwnedPdcTransportCapacityLoss(
    BrainOwnedRuntimeState* state,
    std::uint64_t lostObservationCount) {
    if (state == nullptr || !state->pdc.initialized || lostObservationCount == 0) {
        return;
    }
    state->pdc.counters.capacityLosses += lostObservationCount;
    if (state->pdc.preCaptureUncertain) return;
    BrainOwnedAccessoryVisibleInvalidationBatch batch(state);
    state->pdc.preCaptureUncertain = true;
    Mutate(state);
}

BrainPdcAcquisitionDecision EvaluateBrainOwnedPdcAcquisition(
    BrainOwnedRuntimeState* state,
    const BrainPdcEvaluationContext& context) {
    BrainPdcAcquisitionDecision decision;
    if (state == nullptr || !state->pdc.initialized) {
        decision.reason = "pdc-runtime-uninitialized";
        return decision;
    }
    auto& pdc = state->pdc;
    decision.evaluated = true;
    ++pdc.counters.acquisitionDecisions;
    if (pdc.pluginAdminSuspended) {
        decision.reason = "pdc-runtime-admin-suspended";
        return decision;
    }

    const auto planIdentity = BuildBrainOwnedPlanIdentityKey(
        context.aircraftCallsign,
        context.departureIcao,
        context.destinationIcao);
    const bool contextReady = FlightBindingReady(context, planIdentity);
    bool changed = false;
    if (contextReady && !pdc.boundPlanIdentity.empty() &&
        planIdentity != pdc.boundPlanIdentity) {
        ResetBrainOwnedPdcProductState(state, false);
        BindFlight(&pdc, context, planIdentity);
        changed = true;
    } else if (contextReady && pdc.boundPlanIdentity.empty()) {
        BindFlight(&pdc, context, planIdentity);
        changed = true;
    }

    if (pdc.captureComplete) {
        ++pdc.counters.payloadReadsStopped;
        decision.captureComplete = true;
        decision.clearQueuedFacts = true;
        decision.reason = "one-shot-pdc-capture-complete";
        return decision;
    }
    if (!contextReady) {
        decision.acquisitionArmed = pdc.acquisitionArmed;
        decision.reason = "pdc-flight-context-not-ready";
        if (changed) Mutate(state);
        return decision;
    }
    if (pdc.acquisitionClosed) {
        pdc.acquisitionClosed = false;
        changed = true;
    }
    if (!pdc.acquisitionArmed) {
        pdc.acquisitionArmed = true;
        changed = true;
    }
    decision.acquisitionArmed = true;
    decision.contextReady = true;
    decision.samplePrivatePayload = true;
    decision.reason = "pdc-acquisition-full-sample";
    ++pdc.counters.payloadReadsPermitted;
    if (changed) Mutate(state);
    return decision;
}

BrainPdcObservationDecision EvaluateBrainOwnedPdcObservation(
    BrainOwnedRuntimeState* state,
    const BrainPdcMechanicalObservation& observation,
    const BrainPdcEvaluationContext& context,
    std::uint64_t nowUs) {
    BrainPdcObservationDecision decision;
    if (state == nullptr || !state->pdc.initialized ||
        state->pdc.pluginAdminSuspended) {
        decision.reason = "pdc-runtime-not-observing";
        return decision;
    }
    auto& pdc = state->pdc;
    if (pdc.captureComplete) {
        ++pdc.counters.postCaptureEarlyNoops;
        decision.reason = "one-shot-pdc-already-captured";
        return decision;
    }
    const auto planIdentity = BuildBrainOwnedPlanIdentityKey(
        context.aircraftCallsign,
        context.departureIcao,
        context.destinationIcao);
    if (!pdc.acquisitionArmed || pdc.boundPlanIdentity != planIdentity ||
        !FlightBindingReady(context, planIdentity)) {
        const auto acquisition = EvaluateBrainOwnedPdcAcquisition(state, context);
        if (!acquisition.samplePrivatePayload) {
            decision.reason = acquisition.reason;
            return decision;
        }
    }

    decision.evaluated = true;
    ++pdc.counters.observationsEvaluated;
    if (!observation.sourceQualified || !observation.capabilitiesQualified) {
        ++pdc.counters.observationsRejectedMechanical;
        decision.reason = "private-source-not-qualified";
        MarkBrainOwnedPdcSourceUnavailable(state);
        return decision;
    }
    if (!observation.tupleMechanicallyComplete ||
        observation.sequenceBefore != observation.sequenceAfter ||
        observation.sequenceAfter < 0) {
        ++pdc.counters.observationsRejectedMechanical;
        decision.reason = "private-source-tuple-incomplete";
        return decision;
    }
    decision.mechanicallyAccepted = true;
    BrainOwnedAccessoryVisibleInvalidationBatch batch(state);
    bool changed = false;
    const auto oldAvailability = pdc.availability;
    const bool sourceChanged = pdc.pluginInstanceIdentity != 0 &&
        (pdc.pluginInstanceIdentity != observation.pluginInstanceIdentity ||
         pdc.capabilityGeneration != observation.capabilityGeneration);
    if (sourceChanged) {
        pdc.sourceEpoch = NextEpoch(pdc.sourceEpoch);
        pdc.hasConnectedDisposition = false;
        pdc.connectedDispositionedSequence = 0;
        pdc.preCaptureUncertain |= pdc.hasObservedPositiveSequence;
        ++pdc.counters.sourceEpochChanges;
        changed = true;
    }
    pdc.pluginInstanceIdentity = observation.pluginInstanceIdentity;
    pdc.capabilityGeneration = observation.capabilityGeneration;
    pdc.sourceQualified = true;
    pdc.sourceAvailable = true;
    pdc.lastObservedSequence = observation.sequenceAfter;
    const bool hadPositive = pdc.hasObservedPositiveSequence;
    pdc.hasObservedPositiveSequence |= observation.sequenceAfter > 0;
    const bool connected = StableConnected(observation);
    const bool disconnected = StableDisconnected(observation);
    if (connected) ++pdc.counters.stableConnectedObservations;
    else if (disconnected) ++pdc.counters.stableDisconnectedObservations;
    else {
        ++pdc.counters.transitionalObservations;
        if (observation.callsignBefore != observation.callsignAfter) {
            ++pdc.counters.callsignAmbiguousObservations;
        }
    }

    if (observation.sequenceAfter == 0) {
        if (hadPositive) {
            pdc.sourceEpoch = NextEpoch(pdc.sourceEpoch);
            pdc.hasConnectedDisposition = false;
            pdc.connectedDispositionedSequence = 0;
            pdc.hasObservedPositiveSequence = false;
            pdc.preCaptureUncertain = true;
            ++pdc.counters.rollbackEvents;
            ++pdc.counters.sourceEpochChanges;
        }
        pdc.availability = BrainPdcAvailability::QualifiedIdle;
        decision.reason = hadPositive
            ? "private-source-sequence-zero-after-positive"
            : "private-source-qualified-no-message";
        changed |= oldAvailability != pdc.availability || hadPositive;
    } else if (!connected) {
        pdc.preCaptureUncertain = true;
        pdc.availability = disconnected
            ? BrainPdcAvailability::SourceUnavailable
            : BrainPdcAvailability::SourceNotReady;
        decision.reason = disconnected
            ? "disconnected-candidate-discarded-without-retention"
            : "transitional-candidate-discarded-without-retention";
        changed = true;
    } else if (!observation.senderBodyRead || Trim(observation.body).empty()) {
        ++pdc.counters.observationsRejectedMechanical;
        decision.mechanicallyAccepted = false;
        decision.reason = "new-private-sequence-without-bounded-payload";
    } else {
        if ((!pdc.hasConnectedDisposition && observation.sequenceAfter > 1) ||
            (pdc.hasConnectedDisposition && observation.sequenceAfter >
                pdc.connectedDispositionedSequence + 1)) {
            ++pdc.counters.gapEvents;
            decision.gapDetected = true;
            pdc.preCaptureUncertain = true;
            changed = true;
        } else if (pdc.hasConnectedDisposition && observation.sequenceAfter <
                   pdc.connectedDispositionedSequence) {
            ++pdc.counters.rollbackEvents;
            pdc.sourceEpoch = NextEpoch(pdc.sourceEpoch);
            pdc.preCaptureUncertain = true;
            ++pdc.counters.sourceEpochChanges;
            changed = true;
        }
        BrainPdcCapturedArtifact artifact;
        artifact.productLifecycleEpoch = pdc.productLifecycleEpoch;
        artifact.sourceEpoch = pdc.sourceEpoch;
        artifact.sourceSequence = observation.sequenceAfter;
        artifact.contentDigest = ContentDigest(observation);
        artifact.acceptedMonotonicMicroseconds = nowUs;
        artifact.flightIdentity = pdc.boundFlightIdentity;
        artifact.aircraftCallsign = pdc.boundCallsign;
        artifact.departureIcao = pdc.boundDepartureIcao;
        artifact.destinationIcao = pdc.boundDestinationIcao;
        artifact.revisionIdentity = Revision(pdc, observation);
        artifact.sender = Trim(observation.sender);
        artifact.normalizedSender = SenderKey(observation.sender);
        artifact.body = observation.body;
        artifact.acceptanceReason = "one-shot-waiting-message-captured";
        pdc.retainedBytes = ArtifactBytes(artifact);
        pdc.capturedArtifact = std::move(artifact);
        pdc.hasConnectedDisposition = true;
        pdc.connectedDispositionedSequence = observation.sequenceAfter;
        pdc.captureComplete = true;
        pdc.acquisitionArmed = false;
        pdc.acquisitionClosed = true;
        pdc.availability = BrainPdcAvailability::Available;
        ++pdc.counters.admittedMessages;
        decision.admitted = true;
        decision.captureCompleted = true;
        decision.admittedCount = 1;
        decision.productChanged = true;
        decision.presentationChanged = true;
        decision.reason = "one-shot-waiting-message-captured";
        changed = true;
        if (state->accessory.activeDrawer == BrainOwnedAccessoryDrawerId::Pdc) {
            ++state->accessory.scrollResetGeneration;
        }
    }
    decision.sourceStateChanged = oldAvailability != pdc.availability;
    if (changed) {
        Mutate(state);
        decision.presentationChanged = true;
    }
    return decision;
}

bool ReevaluateBrainOwnedPdcPendingContext(
    BrainOwnedRuntimeState*,
    const BrainPdcEvaluationContext&,
    std::uint64_t) {
    return false;
}

bool AcknowledgeBrainOwnedPdcVisibleEntries(
    BrainOwnedRuntimeState* state,
    const std::vector<std::string>& identities) {
    if (state == nullptr || !state->pdc.initialized) return false;
    const auto end = identities.begin() + std::min(identities.size(), kVisibleLimit);
    BrainOwnedAccessoryVisibleInvalidationBatch batch(state);
    bool changed = false;
    const auto acknowledge = [&](BrainPdcCapturedArtifact* artifact) {
        if (artifact == nullptr || !artifact->unread) return;
        if (std::find(identities.begin(), end, artifact->revisionIdentity) == end) {
            return;
        }
        artifact->unread = false;
        ++state->pdc.counters.unreadAcknowledged;
        changed = true;
    };
    if (state->pdc.capturedArtifact.has_value()) {
        acknowledge(&*state->pdc.capturedArtifact);
    }
    for (auto& artifact : state->pdc.olderArtifacts) acknowledge(&artifact);
    if (changed) Mutate(state);
    return changed;
}

bool AcknowledgeBrainOwnedPdcAllEntries(BrainOwnedRuntimeState* state) {
    if (state == nullptr || !state->pdc.initialized) return false;
    BrainOwnedAccessoryVisibleInvalidationBatch batch(state);
    bool changed = false;
    const auto acknowledge = [&](BrainPdcCapturedArtifact* artifact) {
        if (artifact == nullptr || !artifact->unread) return;
        artifact->unread = false;
        ++state->pdc.counters.unreadAcknowledged;
        changed = true;
    };
    if (state->pdc.capturedArtifact.has_value()) {
        acknowledge(&*state->pdc.capturedArtifact);
    }
    for (auto& artifact : state->pdc.olderArtifacts) acknowledge(&artifact);
    if (changed) Mutate(state);
    return changed;
}

std::size_t BrainOwnedPdcUnreadCount(const BrainPdcRuntimeState& state) {
    const auto olderUnread = std::count_if(
        state.olderArtifacts.begin(),
        state.olderArtifacts.end(),
        [](const auto& artifact) { return artifact.unread; });
    return olderUnread +
        (state.capturedArtifact.has_value() && state.capturedArtifact->unread
             ? 1U
             : 0U);
}

std::size_t BrainOwnedPdcMessageCount(const BrainPdcRuntimeState& state) {
    return state.olderArtifacts.size() +
        (state.capturedArtifact.has_value() ? 1U : 0U);
}

bool AdmitBrainOwnedPdcMessage(
    BrainOwnedRuntimeState* state,
    std::string sender,
    std::string body,
    std::string revisionIdentity,
    std::int64_t sourceSequence,
    std::string sourceTimeUtc,
    std::uint64_t nowMicroseconds,
    std::string sourceKind) {
    if (state == nullptr || !state->pdc.initialized ||
        state->pdc.pluginAdminSuspended || revisionIdentity.empty() ||
        Trim(body).empty() || body.size() > 4096 || sender.size() > 64) {
        if (state != nullptr && state->pdc.initialized) {
            ++state->pdc.counters.rejectedMessages;
        }
        return false;
    }

    auto& pdc = state->pdc;
    auto normalizedSender = SenderKey(sender);
    std::uint64_t contentDigest = 1469598103934665603ULL;
    for (const auto character : normalizedSender + "\xff" + body) {
        contentDigest ^= static_cast<unsigned char>(character);
        contentDigest *= 1099511628211ULL;
    }
    const auto alreadyRetained = [&](const BrainPdcCapturedArtifact& artifact) {
        return artifact.revisionIdentity == revisionIdentity;
    };
    if ((pdc.capturedArtifact.has_value() &&
         alreadyRetained(*pdc.capturedArtifact)) ||
        std::any_of(
            pdc.olderArtifacts.begin(),
            pdc.olderArtifacts.end(),
            alreadyRetained)) {
        ++pdc.counters.connectedDuplicates;
        return false;
    }

    const auto correlatedCrossSource = [&](const BrainPdcCapturedArtifact& artifact) {
        const auto retainedSource = artifact.sourceKind.empty()
            ? std::string("xpilot3_network_log")
            : artifact.sourceKind;
        const auto acceptedDelta = artifact.acceptedMonotonicMicroseconds > nowMicroseconds
            ? artifact.acceptedMonotonicMicroseconds - nowMicroseconds
            : nowMicroseconds - artifact.acceptedMonotonicMicroseconds;
        return retainedSource != sourceKind &&
            artifact.contentDigest == contentDigest &&
            artifact.normalizedSender == normalizedSender &&
            artifact.body == body &&
            acceptedDelta <= kCrossSourceCorrelationWindowMicroseconds;
    };
    if ((pdc.capturedArtifact.has_value() &&
         correlatedCrossSource(*pdc.capturedArtifact)) ||
        std::any_of(
            pdc.olderArtifacts.begin(),
            pdc.olderArtifacts.end(),
            correlatedCrossSource)) {
        ++pdc.counters.connectedDuplicates;
        return false;
    }

    BrainPdcCapturedArtifact artifact;
    artifact.productLifecycleEpoch = pdc.productLifecycleEpoch;
    artifact.sourceEpoch = pdc.sourceEpoch;
    artifact.sourceSequence = sourceSequence;
    artifact.acceptedMonotonicMicroseconds = nowMicroseconds;
    artifact.flightIdentity = pdc.boundFlightIdentity;
    artifact.aircraftCallsign = pdc.boundCallsign;
    artifact.departureIcao = pdc.boundDepartureIcao;
    artifact.destinationIcao = pdc.boundDestinationIcao;
    artifact.revisionIdentity = std::move(revisionIdentity);
    artifact.sender = Trim(std::move(sender));
    artifact.normalizedSender = std::move(normalizedSender);
    artifact.body = std::move(body);
    artifact.sourceTimeUtc = std::move(sourceTimeUtc);
    artifact.sourceKind = std::move(sourceKind);
    artifact.acceptanceReason = "brain-admitted-incoming-direct-message";
    artifact.contentDigest = contentDigest;

    const auto newBytes = ArtifactBytes(artifact);
    if (newBytes > 256U * 1024U) {
        ++pdc.counters.rejectedMessages;
        return false;
    }

    BrainOwnedAccessoryVisibleInvalidationBatch batch(state);
    while (!pdc.olderArtifacts.empty() &&
           (BrainOwnedPdcMessageCount(pdc) >= 32 ||
            pdc.retainedBytes + newBytes > 256U * 1024U)) {
        const auto& oldest = pdc.olderArtifacts.front();
        pdc.retainedBytes -= std::min(pdc.retainedBytes, ArtifactBytes(oldest));
        if (oldest.unread) ++pdc.counters.unreadEvictions;
        pdc.olderArtifacts.erase(pdc.olderArtifacts.begin());
        ++pdc.counters.historyEvictions;
    }
    if (pdc.capturedArtifact.has_value() &&
        pdc.retainedBytes + newBytes > 256U * 1024U) {
        pdc.retainedBytes -= std::min(
            pdc.retainedBytes, ArtifactBytes(*pdc.capturedArtifact));
        if (pdc.capturedArtifact->unread) ++pdc.counters.unreadEvictions;
        pdc.capturedArtifact.reset();
        ++pdc.counters.historyEvictions;
    }
    if (pdc.olderArtifacts.capacity() == 0) pdc.olderArtifacts.reserve(31);
    if (pdc.capturedArtifact.has_value()) {
        pdc.olderArtifacts.push_back(std::move(*pdc.capturedArtifact));
    }
    const bool admittedFromXPilot4 =
        artifact.sourceKind == "xpilot4_plugin_sdk";
    pdc.capturedArtifact = std::move(artifact);
    pdc.retainedBytes += newBytes;
    pdc.captureComplete = true;
    pdc.acquisitionArmed = true;
    pdc.acquisitionClosed = false;
    if (admittedFromXPilot4) {
        pdc.xpilot4SourceAvailable = true;
    } else {
        pdc.logSourceAvailable = true;
    }
    pdc.sourceQualified = true;
    pdc.sourceAvailable = true;
    pdc.availability = BrainPdcAvailability::Available;
    ++pdc.counters.admittedMessages;
    if (state->accessory.activeDrawer == BrainOwnedAccessoryDrawerId::Pdc) {
        ++state->accessory.scrollResetGeneration;
    }
    Mutate(state);
    return true;
}

std::string BrainOwnedPdcDiagnosticSummary(const BrainPdcRuntimeState& state) {
    std::ostringstream out;
    out << "initialized=" << (state.initialized ? 1 : 0)
        << " availability=" << ToString(state.availability)
        << " productEpoch=" << state.productLifecycleEpoch
        << " sourceEpoch=" << state.sourceEpoch
        << " armed=" << (state.acquisitionArmed ? 1 : 0)
        << " closed=" << (state.acquisitionClosed ? 1 : 0)
        << " captureComplete=" << (state.captureComplete ? 1 : 0)
        << " observedSequence=" << state.lastObservedSequence
        << " connectedSequence="
        << (state.hasConnectedDisposition ? state.connectedDispositionedSequence : 0)
        << " messages=" << BrainOwnedPdcMessageCount(state)
        << " unread=" << BrainOwnedPdcUnreadCount(state)
        << " uncertainty=" << (state.preCaptureUncertain ? 1 : 0)
        << " retainedBytes=" << state.retainedBytes
        << " sourceLog=" << (state.logSourceAvailable ? 1 : 0)
        << " sourceXPilot4=" << (state.xpilot4SourceAvailable ? 1 : 0)
        << " logReason=" << state.logMonitor.reason
        << " logRequests=" << state.counters.logRequestsSubmitted
        << " logFacts=" << state.counters.logFactsConsumed
        << " logBytes=" << state.counters.logBytesRead
        << " logExcluded=" << state.counters.logExcludedRecords
        << " logMalformed=" << state.counters.logMalformedRecords;
    return out.str();
}

const char* ToString(BrainPdcAvailability value) {
    switch (value) {
        case BrainPdcAvailability::Uninitialized: return "uninitialized";
        case BrainPdcAvailability::SourceNotReady: return "source-not-ready";
        case BrainPdcAvailability::SourceUnavailable: return "source-unavailable";
        case BrainPdcAvailability::QualifiedIdle: return "qualified-idle";
        case BrainPdcAvailability::Available: return "available";
        default: return "unknown";
    }
}

const char* ToString(BrainPdcClassification value) {
    switch (value) {
        case BrainPdcClassification::ControllerPdc: return "controller-pdc";
        case BrainPdcClassification::ControllerPrivateOperational:
            return "controller-operational";
        case BrainPdcClassification::PilotPrivate: return "pilot-private";
        case BrainPdcClassification::VatsimSystemOrSupervisor: return "vatsim-staff";
        case BrainPdcClassification::UnknownDirectPrivate: return "unknown-private";
        default: return "unknown";
    }
}

}  // namespace xvatsim::brain
