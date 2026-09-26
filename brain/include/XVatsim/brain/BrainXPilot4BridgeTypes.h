#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace xvatsim::brain {

inline constexpr std::uint16_t kXPilot4BridgeProtocolVersion = 1;
inline constexpr std::size_t kXPilot4MaximumControllerSnapshotEntries = 4096;

enum class BrainXPilot4EventKind : std::uint16_t {
    Hello = 1,
    SourceAvailable = 2,
    SourceUnavailable = 3,
    ConnectionStateChanged = 4,
    IncomingPrivateMessage = 5,
    ControllerSnapshotStarted = 6,
    ControllerSnapshotEntry = 7,
    ControllerSnapshotCompleted = 8,
    ControllerAdded = 9,
    ControllerDeleted = 10,
    ControllerFrequencyChanged = 11,
    ControllerLocationChanged = 12,
    CapacityLoss = 13,
    CorruptEnvelope = 14,
    SessionEnded = 15,
    TransportConnected = 1000,
    TransportDisconnected = 1001,
    LocalCapacityLoss = 1002,
};

struct BrainXPilot4ControllerFact {
    std::string callsign;
    int frequencyHz = 0;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    bool hasFrequency = false;
    bool hasLocation = false;
};

struct BrainXPilot4Observation {
    BrainXPilot4EventKind kind = BrainXPilot4EventKind::TransportDisconnected;
    std::uint16_t protocolVersion = kXPilot4BridgeProtocolVersion;
    std::uint64_t processEpoch = 0;
    std::uint64_t connectionEpoch = 0;
    std::uint64_t sourceSequence = 0;
    std::int64_t receivedUnixMicroseconds = 0;
    bool complete = true;
    bool compatible = false;
    bool connected = false;
    std::string companionVersion;
    std::string apiVersion;
    std::string sourceKind = "xpilot4_plugin_sdk";
    std::string sourceFamily = "xpilot_fsd";
    std::string reason;
    std::string callsign;
    std::string sender;
    std::string message;
    BrainXPilot4ControllerFact controller;
    std::uint64_t snapshotRequestId = 0;
    std::uint32_t snapshotEntryCount = 0;
    std::uint64_t lostCount = 0;
    std::uint64_t firstLostSequence = 0;
    std::uint64_t lastLostSequence = 0;
};

struct BrainXPilot4ControllerSnapshot {
    bool sourceAvailable = false;
    bool networkConnected = false;
    bool complete = false;
    std::uint64_t processEpoch = 0;
    std::uint64_t connectionEpoch = 0;
    std::uint64_t generation = 0;
    std::uint64_t stableHash = 0;
    std::vector<BrainXPilot4ControllerFact> controllers;
};

enum class BrainXPilot4CommandKind : std::uint16_t {
    RequestControllerSnapshot = 100,
};

struct BrainXPilot4Command {
    BrainXPilot4CommandKind kind =
        BrainXPilot4CommandKind::RequestControllerSnapshot;
    std::uint64_t requestId = 0;
};

struct BrainXPilot4TransportSnapshot {
    bool running = false;
    bool connected = false;
    std::uint64_t connectAttempts = 0;
    std::uint64_t connections = 0;
    std::uint64_t disconnects = 0;
    std::uint64_t framesReceived = 0;
    std::uint64_t observationsPublished = 0;
    std::uint64_t commandsSubmitted = 0;
    std::uint64_t commandsWritten = 0;
    std::uint64_t corruptFrames = 0;
    std::uint64_t localCapacityLoss = 0;
    std::uint64_t maximumIncomingDepth = 0;
    std::uint64_t maximumOutgoingDepth = 0;
    std::uint64_t maximumHarvestMicroseconds = 0;
};

class BrainXPilot4Transport {
public:
    virtual ~BrainXPilot4Transport() = default;
    virtual bool TryHarvestXPilot4Observation(
        BrainXPilot4Observation* observation) = 0;
    virtual bool TrySubmitXPilot4Command(
        const BrainXPilot4Command& command) = 0;
    virtual BrainXPilot4TransportSnapshot XPilot4TransportSnapshot() const = 0;
};

}  // namespace xvatsim::brain
