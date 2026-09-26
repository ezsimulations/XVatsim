#pragma once

#include "XVatsim/brain/BrainXPilot4BridgeTypes.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace xvatsim::brain {

struct BrainOwnedRuntimeState;

inline constexpr std::size_t kXPilot4MaximumPendingPrivateMessages = 64;
inline constexpr std::uint64_t kXPilot4PendingPrivateMessageLifetimeMicroseconds =
    5ULL * 60ULL * 1'000'000ULL;

struct BrainXPilot4BridgeCounters {
    std::uint64_t observationsConsumed = 0;
    std::uint64_t observationsRejected = 0;
    std::uint64_t staleObservations = 0;
    std::uint64_t sequenceGaps = 0;
    std::uint64_t capacityLosses = 0;
    std::uint64_t corruptEnvelopes = 0;
    std::uint64_t snapshotRequests = 0;
    std::uint64_t snapshotRequestFailures = 0;
    std::uint64_t snapshotsPublished = 0;
    std::uint64_t incompleteSnapshots = 0;
    std::uint64_t controllerEventsApplied = 0;
    std::uint64_t privateMessagesEvaluated = 0;
    std::uint64_t privateMessagesAdmitted = 0;
    std::uint64_t privateMessagesRejected = 0;
    std::uint64_t privateMessagesDeferred = 0;
    std::uint64_t privateMessagesExpired = 0;
    std::uint64_t privateMessagesCapacityEvicted = 0;
    std::uint64_t privateMessagesDiscardedOnSessionChange = 0;
    std::uint64_t maximumServiceMicroseconds = 0;
};

struct BrainXPilot4SnapshotBuildState {
    bool active = false;
    bool incomplete = false;
    std::uint64_t requestId = 0;
    std::uint32_t expectedEntries = 0;
    std::vector<BrainXPilot4ControllerFact> entries;
    std::vector<BrainXPilot4Observation> postSnapshotEvents;
};

struct BrainXPilot4PendingPrivateMessage {
    BrainXPilot4Observation observation;
    std::uint64_t deferredAtMicroseconds = 0;
};

struct BrainXPilot4BridgeRuntimeState {
    bool initialized = false;
    bool transportConnected = false;
    bool sourceAvailable = false;
    bool sdkCompatible = false;
    bool networkConnected = false;
    bool snapshotRecoveryRequired = true;
    bool snapshotRequestPending = false;
    std::uint16_t protocolVersion = 0;
    std::uint64_t processEpoch = 0;
    std::uint64_t connectionEpoch = 0;
    std::uint64_t lastSourceSequence = 0;
    std::uint64_t nextSnapshotRequestId = 1;
    std::uint64_t pendingSnapshotRequestId = 0;
    std::uint64_t snapshotRequestedAtMicroseconds = 0;
    std::string callsign;
    std::string companionVersion;
    std::string apiVersion;
    std::string sourceKind = "xpilot4_plugin_sdk";
    std::string sourceFamily = "xpilot_fsd";
    std::string reason = "waiting-for-xpilot4-companion";
    std::string lastPrivateMessageDecision = "no-private-message-observed";
    std::shared_ptr<const BrainXPilot4ControllerSnapshot> controllerSnapshot;
    BrainXPilot4SnapshotBuildState snapshotBuild;
    std::vector<BrainXPilot4PendingPrivateMessage> pendingPrivateMessages;
    BrainXPilot4BridgeCounters counters;
};

struct BrainXPilot4BridgeContext {
    bool flightContextActive = false;
    std::string_view flightCallsign;
    std::string_view departureIcao;
    std::string_view destinationIcao;
};

struct BrainXPilot4BridgeServiceResult {
    std::size_t observationsConsumed = 0;
    bool commandSubmitted = false;
    bool presentationChanged = false;
    bool controllerEvidenceChanged = false;
    bool healthChanged = false;
    std::string reason;
};

void InitializeBrainOwnedXPilot4BridgeRuntime(BrainOwnedRuntimeState* state);
void ResetBrainOwnedXPilot4BridgeRuntime(BrainOwnedRuntimeState* state);
void StopBrainOwnedXPilot4BridgeRuntime(BrainOwnedRuntimeState* state);

BrainXPilot4BridgeServiceResult ServiceBrainOwnedXPilot4Bridge(
    BrainOwnedRuntimeState* state,
    const BrainXPilot4BridgeContext& context,
    BrainXPilot4Transport* transport,
    std::uint64_t nowMicroseconds,
    std::size_t maximumObservations = 8);

std::string BrainOwnedXPilot4BridgeDiagnosticSummary(
    const BrainXPilot4BridgeRuntimeState& state);

}  // namespace xvatsim::brain
