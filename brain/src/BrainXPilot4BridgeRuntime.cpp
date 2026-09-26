#include "XVatsim/brain/BrainXPilot4BridgeRuntime.h"

#include "XVatsim/brain/BrainOwnedRuntime.h"
#include "XVatsim/brain/BrainPdcRuntime.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <limits>
#include <sstream>
#include <utility>

namespace xvatsim::brain {
namespace {

constexpr std::uint64_t kSnapshotResponseTimeoutMicroseconds =
    5ULL * 1'000'000ULL;

std::string NormalizeIdentity(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() &&
           std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
        --last;
    }
    std::string normalized(value.substr(first, last - first));
    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
    return normalized;
}

bool HasIdentityText(std::string_view value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isspace(character) == 0;
    });
}

bool IdentityEquals(std::string_view left, std::string_view right) {
    const auto trim = [](std::string_view value) {
        while (!value.empty() &&
               std::isspace(
                   static_cast<unsigned char>(value.front())) != 0) {
            value.remove_prefix(1);
        }
        while (!value.empty() &&
               std::isspace(
                   static_cast<unsigned char>(value.back())) != 0) {
            value.remove_suffix(1);
        }
        return value;
    };
    left = trim(left);
    right = trim(right);
    return left.size() == right.size() &&
        std::equal(
            left.begin(),
            left.end(),
            right.begin(),
            [](unsigned char leftCharacter, unsigned char rightCharacter) {
                return std::toupper(leftCharacter) ==
                    std::toupper(rightCharacter);
            });
}

void HashCombine(std::uint64_t* seed, std::uint64_t value) {
    *seed ^= value + 0x9e3779b97f4a7c15ULL + (*seed << 6U) + (*seed >> 2U);
}

void HashString(std::uint64_t* seed, const std::string& value) {
    HashCombine(seed, static_cast<std::uint64_t>(value.size()));
    for (const auto character : value) {
        HashCombine(
            seed,
            static_cast<std::uint64_t>(
                static_cast<unsigned char>(character)));
    }
}

std::uint64_t HashControllerSnapshot(
    const BrainXPilot4ControllerSnapshot& snapshot) {
    std::uint64_t hash = 1469598103934665603ULL;
    HashCombine(&hash, snapshot.processEpoch);
    HashCombine(&hash, snapshot.connectionEpoch);
    HashCombine(&hash, snapshot.sourceAvailable ? 1U : 0U);
    HashCombine(&hash, snapshot.networkConnected ? 1U : 0U);
    HashCombine(&hash, snapshot.complete ? 1U : 0U);
    HashCombine(&hash, snapshot.controllers.size());
    for (const auto& controller : snapshot.controllers) {
        HashString(&hash, controller.callsign);
        HashCombine(
            &hash,
            static_cast<std::uint64_t>(
                static_cast<std::int64_t>(controller.frequencyHz)));
        std::uint64_t latitudeBits = 0;
        std::uint64_t longitudeBits = 0;
        static_assert(sizeof(latitudeBits) == sizeof(controller.latitudeDeg));
        std::memcpy(
            &latitudeBits, &controller.latitudeDeg, sizeof(latitudeBits));
        std::memcpy(
            &longitudeBits, &controller.longitudeDeg, sizeof(longitudeBits));
        HashCombine(&hash, latitudeBits);
        HashCombine(&hash, longitudeBits);
        HashCombine(&hash, controller.hasFrequency ? 1U : 0U);
        HashCombine(&hash, controller.hasLocation ? 1U : 0U);
    }
    return hash;
}

bool PublishControllerSnapshot(
    BrainXPilot4BridgeRuntimeState* bridge,
    std::vector<BrainXPilot4ControllerFact> controllers,
    bool sourceAvailable,
    bool networkConnected,
    bool complete) {
    BrainXPilot4ControllerSnapshot next;
    next.sourceAvailable = sourceAvailable;
    next.networkConnected = networkConnected;
    next.complete = complete;
    next.processEpoch = bridge->processEpoch;
    next.connectionEpoch = bridge->connectionEpoch;
    next.generation = bridge->controllerSnapshot != nullptr
        ? bridge->controllerSnapshot->generation + 1U
        : 1U;
    if (next.generation == 0) next.generation = 1;
    next.controllers = std::move(controllers);
    next.stableHash = HashControllerSnapshot(next);
    if (bridge->controllerSnapshot != nullptr &&
        bridge->controllerSnapshot->stableHash == next.stableHash &&
        bridge->controllerSnapshot->sourceAvailable == next.sourceAvailable &&
        bridge->controllerSnapshot->networkConnected == next.networkConnected &&
        bridge->controllerSnapshot->complete == next.complete) {
        return false;
    }
    bridge->controllerSnapshot =
        std::make_shared<const BrainXPilot4ControllerSnapshot>(std::move(next));
    return true;
}

bool PublishUnavailableControllerSnapshot(
    BrainXPilot4BridgeRuntimeState* bridge) {
    std::vector<BrainXPilot4ControllerFact> retained;
    if (bridge->controllerSnapshot != nullptr) {
        retained = bridge->controllerSnapshot->controllers;
    }
    return PublishControllerSnapshot(
        bridge, std::move(retained), false, false, false);
}

bool ApplyControllerEventToVector(
    const BrainXPilot4Observation& observation,
    std::vector<BrainXPilot4ControllerFact>* controllers) {
    if (controllers == nullptr || observation.controller.callsign.empty()) {
        return false;
    }
    const auto callsign = NormalizeIdentity(observation.controller.callsign);
    const auto matches = [&](const BrainXPilot4ControllerFact& value) {
        return NormalizeIdentity(value.callsign) == callsign;
    };
    if (observation.kind == BrainXPilot4EventKind::ControllerDeleted) {
        const auto oldSize = controllers->size();
        controllers->erase(
            std::remove_if(controllers->begin(), controllers->end(), matches),
            controllers->end());
        return controllers->size() != oldSize;
    }
    if (observation.kind == BrainXPilot4EventKind::ControllerAdded) {
        controllers->push_back(observation.controller);
        return true;
    }

    bool changed = false;
    for (auto& controller : *controllers) {
        if (!matches(controller)) continue;
        if (observation.kind ==
            BrainXPilot4EventKind::ControllerFrequencyChanged) {
            changed |= !controller.hasFrequency ||
                controller.frequencyHz != observation.controller.frequencyHz;
            controller.frequencyHz = observation.controller.frequencyHz;
            controller.hasFrequency = true;
        } else if (observation.kind ==
                   BrainXPilot4EventKind::ControllerLocationChanged) {
            changed |= !controller.hasLocation ||
                controller.latitudeDeg != observation.controller.latitudeDeg ||
                controller.longitudeDeg != observation.controller.longitudeDeg;
            controller.latitudeDeg = observation.controller.latitudeDeg;
            controller.longitudeDeg = observation.controller.longitudeDeg;
            controller.hasLocation = true;
        }
    }
    if (!changed &&
        std::none_of(controllers->begin(), controllers->end(), matches)) {
        controllers->push_back(observation.controller);
        changed = true;
    }
    return changed;
}

bool ApplyLiveControllerEvent(
    BrainXPilot4BridgeRuntimeState* bridge,
    const BrainXPilot4Observation& observation) {
    std::vector<BrainXPilot4ControllerFact> controllers;
    if (bridge->controllerSnapshot != nullptr) {
        controllers = bridge->controllerSnapshot->controllers;
    }
    if (!ApplyControllerEventToVector(observation, &controllers)) {
        return false;
    }
    ++bridge->counters.controllerEventsApplied;
    return PublishControllerSnapshot(
        bridge,
        std::move(controllers),
        bridge->sourceAvailable,
        bridge->networkConnected,
        bridge->controllerSnapshot != nullptr &&
            bridge->controllerSnapshot->complete);
}

void MarkRecoveryRequired(BrainXPilot4BridgeRuntimeState* bridge) {
    bridge->snapshotRecoveryRequired = true;
    bridge->snapshotRequestPending = false;
    bridge->pendingSnapshotRequestId = 0;
    bridge->snapshotRequestedAtMicroseconds = 0;
    bridge->snapshotBuild = {};
}

bool ObservationBelongsToCurrentSource(
    BrainXPilot4BridgeRuntimeState* bridge,
    const BrainXPilot4Observation& observation) {
    if (observation.kind == BrainXPilot4EventKind::TransportConnected ||
        observation.kind == BrainXPilot4EventKind::TransportDisconnected ||
        observation.kind == BrainXPilot4EventKind::LocalCapacityLoss ||
        (observation.kind == BrainXPilot4EventKind::CorruptEnvelope &&
         observation.processEpoch == 0)) {
        return true;
    }
    if (observation.kind == BrainXPilot4EventKind::Hello) {
        return true;
    }
    if (bridge->processEpoch == 0 ||
        observation.processEpoch != bridge->processEpoch) {
        return false;
    }
    if (observation.connectionEpoch < bridge->connectionEpoch) {
        return false;
    }
    return true;
}

void ObserveSequence(
    BrainXPilot4BridgeRuntimeState* bridge,
    const BrainXPilot4Observation& observation) {
    if (observation.sourceSequence == 0 ||
        observation.kind == BrainXPilot4EventKind::Hello ||
        observation.kind == BrainXPilot4EventKind::TransportConnected ||
        observation.kind == BrainXPilot4EventKind::TransportDisconnected ||
        observation.kind == BrainXPilot4EventKind::LocalCapacityLoss) {
        return;
    }
    if (bridge->lastSourceSequence != 0 &&
        observation.sourceSequence > bridge->lastSourceSequence + 1U) {
        ++bridge->counters.sequenceGaps;
        if (observation.kind != BrainXPilot4EventKind::CapacityLoss) {
            bridge->counters.capacityLosses +=
                observation.sourceSequence - bridge->lastSourceSequence - 1U;
        }
        MarkRecoveryRequired(bridge);
    }
    bridge->lastSourceSequence =
        std::max(bridge->lastSourceSequence, observation.sourceSequence);
}

bool BindPdcFlight(
    BrainOwnedRuntimeState* state,
    const BrainXPilot4BridgeContext& context) {
    BrainPdcEvaluationContext pdcContext;
    pdcContext.flightContextActive = context.flightContextActive;
    pdcContext.aircraftCallsign = std::string(context.flightCallsign);
    pdcContext.departureIcao = std::string(context.departureIcao);
    pdcContext.destinationIcao = std::string(context.destinationIcao);
    const auto before = state->pdc.semanticGeneration;
    (void)EvaluateBrainOwnedPdcAcquisition(state, pdcContext);
    return state->pdc.semanticGeneration != before;
}

bool FlightContextReadyForPrivateMessage(
    const BrainXPilot4BridgeRuntimeState& bridge,
    const BrainXPilot4BridgeContext& context) {
    return context.flightContextActive && bridge.transportConnected &&
        bridge.sourceAvailable && bridge.sdkCompatible &&
        bridge.networkConnected && HasIdentityText(context.flightCallsign) &&
        HasIdentityText(bridge.callsign) &&
        IdentityEquals(context.flightCallsign, bridge.callsign) &&
        HasIdentityText(context.departureIcao) &&
        HasIdentityText(context.destinationIcao);
}

bool ObservationMatchesCurrentSession(
    const BrainXPilot4BridgeRuntimeState& bridge,
    const BrainXPilot4Observation& observation) {
    return bridge.processEpoch != 0 &&
        observation.processEpoch == bridge.processEpoch &&
        observation.connectionEpoch == bridge.connectionEpoch;
}

bool AdmitPrivateMessage(
    BrainOwnedRuntimeState* state,
    const BrainXPilot4BridgeContext& context,
    const BrainXPilot4Observation& observation,
    std::uint64_t nowMicroseconds) {
    auto& bridge = state->xpilot4Bridge;
    (void)BindPdcFlight(state, context);
    std::ostringstream revision;
    revision << "PDC-XP4-" << state->pdc.productLifecycleEpoch << '-'
             << observation.processEpoch << '-'
             << observation.connectionEpoch << '-'
             << observation.sourceSequence;
    const auto sequence = static_cast<std::int64_t>(
        std::min<std::uint64_t>(
            observation.sourceSequence,
            static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())));
    const bool admitted = AdmitBrainOwnedPdcMessage(
        state,
        observation.sender,
        observation.message,
        revision.str(),
        sequence,
        {},
        nowMicroseconds,
        "xpilot4_plugin_sdk");
    if (admitted) {
        ++bridge.counters.privateMessagesAdmitted;
        bridge.lastPrivateMessageDecision =
            "brain-admitted-current-flight-private-message";
    } else {
        ++bridge.counters.privateMessagesRejected;
        bridge.lastPrivateMessageDecision =
            "brain-rejected-private-message-policy-or-duplicate";
    }
    return admitted;
}

void RecordPendingMessageLoss(
    BrainOwnedRuntimeState* state,
    std::uint64_t count) {
    if (count == 0) return;
    RecordBrainOwnedPdcTransportCapacityLoss(state, count);
}

void ClearPendingPrivateMessages(
    BrainOwnedRuntimeState* state,
    std::string_view reason) {
    auto& bridge = state->xpilot4Bridge;
    const auto count = static_cast<std::uint64_t>(
        bridge.pendingPrivateMessages.size());
    if (count == 0) return;
    bridge.counters.privateMessagesRejected += count;
    bridge.counters.privateMessagesDiscardedOnSessionChange += count;
    bridge.pendingPrivateMessages.clear();
    bridge.lastPrivateMessageDecision = std::string(reason);
}

void DeferPrivateMessage(
    BrainOwnedRuntimeState* state,
    const BrainXPilot4Observation& observation,
    std::uint64_t nowMicroseconds) {
    auto& bridge = state->xpilot4Bridge;
    if (bridge.pendingPrivateMessages.size() >=
        kXPilot4MaximumPendingPrivateMessages) {
        bridge.pendingPrivateMessages.erase(
            bridge.pendingPrivateMessages.begin());
        ++bridge.counters.privateMessagesRejected;
        ++bridge.counters.privateMessagesCapacityEvicted;
        RecordPendingMessageLoss(state, 1);
    }
    BrainXPilot4PendingPrivateMessage pending;
    pending.observation = observation;
    pending.deferredAtMicroseconds = nowMicroseconds;
    bridge.pendingPrivateMessages.push_back(std::move(pending));
    ++bridge.counters.privateMessagesDeferred;
    bridge.lastPrivateMessageDecision =
        "brain-deferred-private-message-awaiting-flight-context";
}

bool EvaluatePrivateMessage(
    BrainOwnedRuntimeState* state,
    const BrainXPilot4BridgeContext& context,
    const BrainXPilot4Observation& observation,
    std::uint64_t nowMicroseconds) {
    auto& bridge = state->xpilot4Bridge;
    ++bridge.counters.privateMessagesEvaluated;
    if (!ObservationMatchesCurrentSession(bridge, observation)) {
        ++bridge.counters.privateMessagesRejected;
        bridge.lastPrivateMessageDecision =
            "brain-rejected-private-message-session-mismatch";
        return false;
    }
    if (!FlightContextReadyForPrivateMessage(bridge, context)) {
        DeferPrivateMessage(state, observation, nowMicroseconds);
        return false;
    }
    return AdmitPrivateMessage(state, context, observation, nowMicroseconds);
}

bool ReevaluatePendingPrivateMessages(
    BrainOwnedRuntimeState* state,
    const BrainXPilot4BridgeContext& context,
    std::uint64_t nowMicroseconds) {
    auto& bridge = state->xpilot4Bridge;
    if (bridge.pendingPrivateMessages.empty()) return false;

    bool presentationChanged = false;
    const auto discardExpiredOrStaleFront = [&]() {
        while (!bridge.pendingPrivateMessages.empty()) {
            const auto& pending = bridge.pendingPrivateMessages.front();
            const bool expired =
                nowMicroseconds >= pending.deferredAtMicroseconds &&
                nowMicroseconds - pending.deferredAtMicroseconds >=
                    kXPilot4PendingPrivateMessageLifetimeMicroseconds;
            const bool stale = !ObservationMatchesCurrentSession(
                bridge, pending.observation);
            if (!expired && !stale) break;
            ++bridge.counters.privateMessagesRejected;
            if (expired) {
                ++bridge.counters.privateMessagesExpired;
                bridge.lastPrivateMessageDecision =
                    "brain-expired-private-message-without-flight-context";
                RecordPendingMessageLoss(state, 1);
            } else {
                ++bridge.counters.privateMessagesDiscardedOnSessionChange;
                bridge.lastPrivateMessageDecision =
                    "brain-discarded-private-message-after-session-change";
            }
            bridge.pendingPrivateMessages.erase(
                bridge.pendingPrivateMessages.begin());
        }
    };
    discardExpiredOrStaleFront();
    if (bridge.pendingPrivateMessages.empty() ||
        !FlightContextReadyForPrivateMessage(bridge, context)) {
        return false;
    }

    auto pendingMessages = std::move(bridge.pendingPrivateMessages);
    bridge.pendingPrivateMessages.clear();
    for (auto& pending : pendingMessages) {
        const bool expired = nowMicroseconds >= pending.deferredAtMicroseconds &&
            nowMicroseconds - pending.deferredAtMicroseconds >=
                kXPilot4PendingPrivateMessageLifetimeMicroseconds;
        if (expired) {
            ++bridge.counters.privateMessagesRejected;
            ++bridge.counters.privateMessagesExpired;
            bridge.lastPrivateMessageDecision =
                "brain-expired-private-message-without-flight-context";
            RecordPendingMessageLoss(state, 1);
            continue;
        }
        if (!ObservationMatchesCurrentSession(
                bridge, pending.observation)) {
            ++bridge.counters.privateMessagesRejected;
            ++bridge.counters.privateMessagesDiscardedOnSessionChange;
            bridge.lastPrivateMessageDecision =
                "brain-discarded-private-message-after-session-change";
            continue;
        }
        presentationChanged |= AdmitPrivateMessage(
            state, context, pending.observation, nowMicroseconds);
    }
    return presentationChanged;
}

void BeginSnapshot(
    BrainXPilot4BridgeRuntimeState* bridge,
    const BrainXPilot4Observation& observation) {
    bridge->snapshotBuild = {};
    bridge->snapshotBuild.active = true;
    bridge->snapshotBuild.requestId = observation.snapshotRequestId;
    bridge->snapshotBuild.expectedEntries = observation.snapshotEntryCount;
    const auto reserveCount = std::min<std::size_t>(
        observation.snapshotEntryCount,
        kXPilot4MaximumControllerSnapshotEntries);
    bridge->snapshotBuild.entries.reserve(reserveCount);
}

bool FinishSnapshot(BrainXPilot4BridgeRuntimeState* bridge) {
    auto build = std::move(bridge->snapshotBuild);
    bridge->snapshotBuild = {};
    bridge->snapshotRequestPending = false;
    bridge->pendingSnapshotRequestId = 0;
    bridge->snapshotRequestedAtMicroseconds = 0;
    if (!build.active || build.incomplete ||
        build.entries.size() != build.expectedEntries) {
        ++bridge->counters.incompleteSnapshots;
        return false;
    }
    auto controllers = std::move(build.entries);
    for (const auto& event : build.postSnapshotEvents) {
        (void)ApplyControllerEventToVector(event, &controllers);
    }
    const bool changed = PublishControllerSnapshot(
        bridge,
        std::move(controllers),
        bridge->sourceAvailable,
        bridge->networkConnected,
        true);
    bridge->snapshotRecoveryRequired = false;
    ++bridge->counters.snapshotsPublished;
    return changed;
}

}  // namespace

void InitializeBrainOwnedXPilot4BridgeRuntime(BrainOwnedRuntimeState* state) {
    if (state == nullptr || state->xpilot4Bridge.initialized) return;
    state->xpilot4Bridge = {};
    state->xpilot4Bridge.initialized = true;
}

void ResetBrainOwnedXPilot4BridgeRuntime(BrainOwnedRuntimeState* state) {
    if (state == nullptr) return;
    const bool initialized = state->xpilot4Bridge.initialized;
    state->xpilot4Bridge = {};
    state->xpilot4Bridge.initialized = initialized;
    SetBrainOwnedPdcSourceAvailability(
        state, BrainPdcEvidenceSource::XPilot4PluginSdk, false);
}

void StopBrainOwnedXPilot4BridgeRuntime(BrainOwnedRuntimeState* state) {
    if (state == nullptr) return;
    state->xpilot4Bridge = {};
    SetBrainOwnedPdcSourceAvailability(
        state, BrainPdcEvidenceSource::XPilot4PluginSdk, false);
}

BrainXPilot4BridgeServiceResult ServiceBrainOwnedXPilot4Bridge(
    BrainOwnedRuntimeState* state,
    const BrainXPilot4BridgeContext& context,
    BrainXPilot4Transport* transport,
    std::uint64_t nowMicroseconds,
    std::size_t maximumObservations) {
    BrainXPilot4BridgeServiceResult result;
    const auto started = std::chrono::steady_clock::now();
    if (state == nullptr || transport == nullptr || maximumObservations == 0) {
        result.reason = "xpilot4-bridge-unavailable";
        return result;
    }
    InitializeBrainOwnedXPilot4BridgeRuntime(state);
    auto& bridge = state->xpilot4Bridge;
    const auto pdcGenerationBefore = state->pdc.semanticGeneration;
    const auto controllerHashBefore = bridge.controllerSnapshot != nullptr
        ? bridge.controllerSnapshot->stableHash
        : 0;

    BrainXPilot4Observation observation;
    while (result.observationsConsumed < maximumObservations &&
           transport->TryHarvestXPilot4Observation(&observation)) {
        ++result.observationsConsumed;
        ++bridge.counters.observationsConsumed;
        if (!ObservationBelongsToCurrentSource(&bridge, observation)) {
            ++bridge.counters.staleObservations;
            continue;
        }
        if (observation.sourceSequence != 0 &&
            observation.kind != BrainXPilot4EventKind::Hello &&
            bridge.lastSourceSequence != 0 &&
            observation.sourceSequence <= bridge.lastSourceSequence) {
            ++bridge.counters.staleObservations;
            continue;
        }
        ObserveSequence(&bridge, observation);

        switch (observation.kind) {
            case BrainXPilot4EventKind::TransportConnected:
                bridge.transportConnected = true;
                bridge.reason = "xpilot4-local-transport-connected";
                result.healthChanged = true;
                break;
            case BrainXPilot4EventKind::TransportDisconnected:
                ClearPendingPrivateMessages(
                    state,
                    "brain-discarded-private-message-transport-disconnected");
                bridge.transportConnected = false;
                bridge.sourceAvailable = false;
                bridge.networkConnected = false;
                bridge.reason = "xpilot4-local-transport-disconnected";
                MarkRecoveryRequired(&bridge);
                result.controllerEvidenceChanged |=
                    PublishUnavailableControllerSnapshot(&bridge);
                SetBrainOwnedPdcSourceAvailability(
                    state, BrainPdcEvidenceSource::XPilot4PluginSdk, false);
                result.healthChanged = true;
                break;
            case BrainXPilot4EventKind::Hello: {
                const bool processChanged = bridge.processEpoch != 0 &&
                    bridge.processEpoch != observation.processEpoch;
                const bool firstProcess = bridge.processEpoch == 0;
                bridge.protocolVersion = observation.protocolVersion;
                bridge.processEpoch = observation.processEpoch;
                bridge.connectionEpoch = observation.connectionEpoch;
                if (firstProcess || processChanged) {
                    bridge.lastSourceSequence = 0;
                }
                bridge.sdkCompatible = observation.compatible &&
                    observation.protocolVersion == kXPilot4BridgeProtocolVersion;
                bridge.companionVersion = observation.companionVersion;
                bridge.apiVersion = observation.apiVersion;
                bridge.sourceKind = observation.sourceKind;
                bridge.sourceFamily = observation.sourceFamily;
                bridge.reason = observation.reason;
                if (processChanged) {
                    ClearPendingPrivateMessages(
                        state,
                        "brain-discarded-private-message-process-changed");
                    MarkRecoveryRequired(&bridge);
                    result.controllerEvidenceChanged |=
                        PublishUnavailableControllerSnapshot(&bridge);
                }
                result.healthChanged = true;
                break;
            }
            case BrainXPilot4EventKind::SourceAvailable:
                bridge.sourceAvailable = bridge.sdkCompatible;
                bridge.apiVersion = observation.apiVersion;
                bridge.reason = observation.reason;
                SetBrainOwnedPdcSourceAvailability(
                    state,
                    BrainPdcEvidenceSource::XPilot4PluginSdk,
                    bridge.sourceAvailable && bridge.networkConnected);
                result.healthChanged = true;
                break;
            case BrainXPilot4EventKind::SourceUnavailable:
                ClearPendingPrivateMessages(
                    state,
                    "brain-discarded-private-message-source-unavailable");
                bridge.sourceAvailable = false;
                bridge.sdkCompatible = false;
                bridge.networkConnected = false;
                bridge.reason = observation.reason;
                MarkRecoveryRequired(&bridge);
                result.controllerEvidenceChanged |=
                    PublishUnavailableControllerSnapshot(&bridge);
                SetBrainOwnedPdcSourceAvailability(
                    state, BrainPdcEvidenceSource::XPilot4PluginSdk, false);
                result.healthChanged = true;
                break;
            case BrainXPilot4EventKind::ConnectionStateChanged:
                if (!observation.connected ||
                    (bridge.connectionEpoch != 0 &&
                     bridge.connectionEpoch != observation.connectionEpoch)) {
                    ClearPendingPrivateMessages(
                        state,
                        "brain-discarded-private-message-connection-changed");
                }
                bridge.connectionEpoch = observation.connectionEpoch;
                bridge.networkConnected = observation.connected;
                bridge.callsign = observation.connected
                    ? observation.callsign
                    : std::string{};
                bridge.reason = observation.connected
                    ? "xpilot4-network-connected"
                    : "xpilot4-network-disconnected";
                MarkRecoveryRequired(&bridge);
                SetBrainOwnedPdcSourceAvailability(
                    state,
                    BrainPdcEvidenceSource::XPilot4PluginSdk,
                    bridge.sourceAvailable && bridge.sdkCompatible &&
                        bridge.networkConnected);
                if (!observation.connected) {
                    result.controllerEvidenceChanged |=
                        PublishUnavailableControllerSnapshot(&bridge);
                }
                result.healthChanged = true;
                break;
            case BrainXPilot4EventKind::IncomingPrivateMessage:
                result.presentationChanged |= EvaluatePrivateMessage(
                    state, context, observation, nowMicroseconds);
                break;
            case BrainXPilot4EventKind::ControllerSnapshotStarted:
                if (bridge.snapshotRequestPending &&
                    observation.snapshotRequestId ==
                        bridge.pendingSnapshotRequestId) {
                    BeginSnapshot(&bridge, observation);
                } else {
                    ++bridge.counters.observationsRejected;
                }
                break;
            case BrainXPilot4EventKind::ControllerSnapshotEntry:
                if (!bridge.snapshotBuild.active ||
                    observation.snapshotRequestId !=
                        bridge.snapshotBuild.requestId) {
                    ++bridge.counters.observationsRejected;
                    break;
                }
                if (bridge.snapshotBuild.entries.size() >=
                    kXPilot4MaximumControllerSnapshotEntries) {
                    bridge.snapshotBuild.incomplete = true;
                    ++bridge.counters.capacityLosses;
                } else {
                    bridge.snapshotBuild.entries.push_back(
                        observation.controller);
                }
                break;
            case BrainXPilot4EventKind::ControllerSnapshotCompleted:
                if (!bridge.snapshotBuild.active ||
                    observation.snapshotRequestId !=
                        bridge.snapshotBuild.requestId ||
                    observation.snapshotEntryCount !=
                        bridge.snapshotBuild.expectedEntries) {
                    ++bridge.counters.incompleteSnapshots;
                    MarkRecoveryRequired(&bridge);
                } else {
                    result.controllerEvidenceChanged |= FinishSnapshot(&bridge);
                }
                break;
            case BrainXPilot4EventKind::ControllerAdded:
            case BrainXPilot4EventKind::ControllerDeleted:
            case BrainXPilot4EventKind::ControllerFrequencyChanged:
            case BrainXPilot4EventKind::ControllerLocationChanged:
                if (bridge.snapshotBuild.active) {
                    bridge.snapshotBuild.postSnapshotEvents.push_back(observation);
                }
                result.controllerEvidenceChanged |=
                    ApplyLiveControllerEvent(&bridge, observation);
                break;
            case BrainXPilot4EventKind::CapacityLoss:
            case BrainXPilot4EventKind::LocalCapacityLoss:
                bridge.counters.capacityLosses +=
                    std::max<std::uint64_t>(1, observation.lostCount);
                RecordBrainOwnedPdcTransportCapacityLoss(
                    state,
                    std::max<std::uint64_t>(1, observation.lostCount));
                MarkRecoveryRequired(&bridge);
                result.healthChanged = true;
                break;
            case BrainXPilot4EventKind::CorruptEnvelope:
                ++bridge.counters.corruptEnvelopes;
                MarkRecoveryRequired(&bridge);
                result.healthChanged = true;
                break;
            case BrainXPilot4EventKind::SessionEnded:
                ClearPendingPrivateMessages(
                    state,
                    "brain-discarded-private-message-session-ended");
                bridge.transportConnected = false;
                bridge.sourceAvailable = false;
                bridge.networkConnected = false;
                bridge.reason = "xpilot4-session-ended";
                MarkRecoveryRequired(&bridge);
                result.controllerEvidenceChanged |=
                    PublishUnavailableControllerSnapshot(&bridge);
                SetBrainOwnedPdcSourceAvailability(
                    state, BrainPdcEvidenceSource::XPilot4PluginSdk, false);
                result.healthChanged = true;
                break;
            default:
                ++bridge.counters.observationsRejected;
                break;
        }
    }

    result.presentationChanged |= ReevaluatePendingPrivateMessages(
        state, context, nowMicroseconds);

    if (bridge.snapshotRequestPending &&
        bridge.snapshotRequestedAtMicroseconds != 0 &&
        nowMicroseconds > bridge.snapshotRequestedAtMicroseconds &&
        nowMicroseconds - bridge.snapshotRequestedAtMicroseconds >=
            kSnapshotResponseTimeoutMicroseconds) {
        ++bridge.counters.incompleteSnapshots;
        bridge.reason = "xpilot4-controller-snapshot-timeout";
        MarkRecoveryRequired(&bridge);
        result.healthChanged = true;
    }

    SetBrainOwnedPdcSourceAvailability(
        state,
        BrainPdcEvidenceSource::XPilot4PluginSdk,
        bridge.transportConnected && bridge.sourceAvailable &&
            bridge.sdkCompatible && bridge.networkConnected);

    if (bridge.transportConnected && bridge.sourceAvailable &&
        bridge.sdkCompatible && bridge.networkConnected &&
        bridge.snapshotRecoveryRequired &&
        !bridge.snapshotRequestPending && !bridge.snapshotBuild.active) {
        BrainXPilot4Command command;
        command.requestId = bridge.nextSnapshotRequestId;
        if (transport->TrySubmitXPilot4Command(command)) {
            bridge.snapshotRequestPending = true;
            bridge.pendingSnapshotRequestId = command.requestId;
            bridge.snapshotRequestedAtMicroseconds =
                std::max<std::uint64_t>(1, nowMicroseconds);
            ++bridge.nextSnapshotRequestId;
            if (bridge.nextSnapshotRequestId == 0) {
                bridge.nextSnapshotRequestId = 1;
            }
            ++bridge.counters.snapshotRequests;
            result.commandSubmitted = true;
        } else {
            ++bridge.counters.snapshotRequestFailures;
        }
    }

    result.presentationChanged |=
        state->pdc.semanticGeneration != pdcGenerationBefore;
    const auto controllerHashAfter = bridge.controllerSnapshot != nullptr
        ? bridge.controllerSnapshot->stableHash
        : 0;
    result.controllerEvidenceChanged |=
        controllerHashAfter != controllerHashBefore;
    result.reason = bridge.reason;
    const auto elapsed = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
    bridge.counters.maximumServiceMicroseconds =
        std::max(bridge.counters.maximumServiceMicroseconds, elapsed);
    return result;
}

std::string BrainOwnedXPilot4BridgeDiagnosticSummary(
    const BrainXPilot4BridgeRuntimeState& state) {
    std::ostringstream stream;
    stream << "transport=" << (state.transportConnected ? 1 : 0)
           << " source=" << (state.sourceAvailable ? 1 : 0)
           << " compatible=" << (state.sdkCompatible ? 1 : 0)
           << " network=" << (state.networkConnected ? 1 : 0)
           << " processEpoch=" << state.processEpoch
           << " connectionEpoch=" << state.connectionEpoch
           << " sequence=" << state.lastSourceSequence
           << " controllerGeneration="
           << (state.controllerSnapshot != nullptr
                   ? state.controllerSnapshot->generation
                   : 0)
           << " controllers="
           << (state.controllerSnapshot != nullptr
                   ? state.controllerSnapshot->controllers.size()
                   : 0)
           << " consumed=" << state.counters.observationsConsumed
           << " rejected=" << state.counters.observationsRejected
           << " gaps=" << state.counters.sequenceGaps
           << " loss=" << state.counters.capacityLosses
           << " snapshots=" << state.counters.snapshotsPublished
           << " incompleteSnapshots=" << state.counters.incompleteSnapshots
           << " snapshotRequestFailures="
           << state.counters.snapshotRequestFailures
           << " messages=" << state.counters.privateMessagesAdmitted
           << "/" << state.counters.privateMessagesEvaluated
           << " messagePending=" << state.pendingPrivateMessages.size()
           << " messageDeferred=" << state.counters.privateMessagesDeferred
           << " messageRejected=" << state.counters.privateMessagesRejected
           << " messageExpired=" << state.counters.privateMessagesExpired
           << " messageCapacityEvicted="
           << state.counters.privateMessagesCapacityEvicted
           << " messageSessionDiscarded="
           << state.counters.privateMessagesDiscardedOnSessionChange
           << " messageDecision=" << state.lastPrivateMessageDecision
           << " maxServiceUs=" << state.counters.maximumServiceMicroseconds
           << " reason=" << state.reason;
    return stream.str();
}

}  // namespace xvatsim::brain
