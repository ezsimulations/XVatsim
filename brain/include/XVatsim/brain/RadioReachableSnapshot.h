#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::brain {

struct BrainTerminalAuthorityWorkerOutput;

enum class RadioReachableFacilityGroup {
    Delivery,
    Ground,
    Tower,
    AppDep,
    Center,
    Atis,
    Other,
};

enum class RadioReachableSource {
    Unknown,
    XPilotSeam,
    AFVRadioRange,
    ControllerFeed,
    TestHarness,
};

struct RadioReachableBuildOptions {
    bool available = false;
    bool stale = true;
    std::uint64_t generation = 0;
    RadioReachableSource source = RadioReachableSource::Unknown;
    std::string changeReason;
    double nowSeconds = 0.0;
};

struct RadioReachableStationCoordinate {
    std::string frequency;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
};

struct RadioReachableControllerCandidate {
    std::string callsign;
    std::string frequency;
    // Original VATSIM primary, preserved even when AFV supplies a different
    // contact channel. The Brain uses this fact for operational eligibility.
    std::string sourcePrimaryFrequency;
    // These presence flags distinguish an explicit VATSIM value of zero or
    // blank from older tests and non-VATSIM producers that never supplied the
    // source field. Explicit zero is observer evidence; absence is unknown.
    bool sourcePrimaryFrequencyProvided = false;
    int vatsimFacility = 0;
    bool vatsimFacilityProvided = false;
    RadioReachableFacilityGroup group = RadioReachableFacilityGroup::Other;
    RadioReachableSource source = RadioReachableSource::Unknown;
    bool actionable = true;
    bool atis = false;
    int visualRangeNm = 0;
    std::string textAtis;
    bool hasDistanceNm = false;
    double distanceNm = 0.0;
    bool hasStationCoordinates = false;
    double stationLatitudeDeg = 0.0;
    double stationLongitudeDeg = 0.0;
    // Every valid AFV transmitter for this controller/frequency.  The selected
    // station above remains the aircraft-reception result; terminal relevance
    // uses this complete set so its endpoint distance cannot oscillate as the
    // aircraft changes which transmitter is best.
    std::vector<RadioReachableStationCoordinate> stationCoordinates;
    // Complete immutable AFV facts for the controller. Unlike the selected
    // radio-board channel, these retain extension and consolidation channels
    // so the Brain can choose the contact channel from source evidence.
    std::vector<TransceiverStationEvidenceSnapshot> originalTransceivers;
    bool sourceRosterAvailable = false;
    bool sourceRosterStale = true;
    bool sourceRosterComplete = false;
    bool sourceRosterTruncated = false;
    int sourceRosterDeclaredRecords = 0;
    int sourceRosterRetainedRecords = 0;
    int sourceRosterRejectedRecords = 0;
    std::uint64_t sourceRosterDigest = 0;
    std::shared_ptr<const std::vector<ControllerSnapshot>> sourceRoster;
    double firstSeenSeconds = 0.0;
    double lastSeenSeconds = 0.0;
    std::string stableKey;
};

struct RadioReachableGroupCounts {
    int delivery = 0;
    int ground = 0;
    int tower = 0;
    int appDep = 0;
    int center = 0;
    int atis = 0;
    int other = 0;
};

struct RadioReachableControllerSnapshot {
    bool available = false;
    bool stale = true;
    std::uint64_t generation = 0;
    std::uint64_t stableHash = 0;
    RadioReachableSource source = RadioReachableSource::Unknown;
    std::string changeReason;
    std::string statusLine;
    RadioReachableGroupCounts counts;
    bool sourceRosterAvailable = false;
    bool sourceRosterStale = true;
    bool sourceRosterComplete = false;
    bool sourceRosterTruncated = false;
    int sourceRosterDeclaredRecords = 0;
    int sourceRosterRetainedRecords = 0;
    int sourceRosterRejectedRecords = 0;
    std::uint64_t sourceRosterDigest = 0;
    std::shared_ptr<const std::vector<ControllerSnapshot>> sourceRoster;
    std::vector<RadioReachableControllerCandidate> candidates;
};

struct RadioReachablePhaseGateOptions {
    WorkflowStage stage = WorkflowStage::None;
    bool includeAtis = false;
    std::string reason;
};

struct BrainEndpointTerminalEvidenceScope {
    std::string airportIcao;
    bool hasAirportCoordinates = false;
    double airportLatitudeDeg = 0.0;
    double airportLongitudeDeg = 0.0;
};

struct RadioReachableCandidateDiff {
    bool available = false;
    bool stale = true;
    std::uint64_t previousHash = 0;
    std::uint64_t currentHash = 0;
    int previousCandidates = 0;
    int currentCandidates = 0;
    int added = 0;
    int removed = 0;
    int unchanged = 0;
    std::vector<std::string> addedStableKeys;
    std::vector<std::string> removedStableKeys;
    std::string statusLine;

    bool HasCurrentVerificationWork() const {
        return available && !stale && added > 0;
    }
};

struct RadioReachableVerificationFeed {
    bool available = false;
    bool stale = true;
    std::uint64_t generation = 0;
    int inputCandidates = 0;
    int selectedControllers = 0;
    std::string statusLine;
    std::vector<ControllerSnapshot> controllers;

    ControllerFeedSnapshot ToControllerFeedSnapshot() const {
        ControllerFeedSnapshot snapshot;
        snapshot.available = available;
        snapshot.stale = stale;
        snapshot.generation = generation;
        snapshot.connectedControllers = selectedControllers;
        snapshot.statusLine = statusLine;
        if (snapshot.available && !snapshot.stale) {
            snapshot.controllers = &controllers;
        }
        return snapshot;
    }
};

const char* ToString(RadioReachableFacilityGroup group);
const char* ToString(RadioReachableSource source);

RadioReachableFacilityGroup ClassifyRadioReachableFacility(
    const ControllerSnapshot& controller);

RadioReachableControllerSnapshot BuildRadioReachableControllerSnapshot(
    const std::vector<ControllerSnapshot>& reachableControllers,
    const RadioReachableBuildOptions& options);

RadioReachableControllerSnapshot BuildRadioReachableControllerSnapshotFromTransceivers(
    const TransceiverResolutionSnapshot& transceiverSnapshot,
    const ControllerFeedSnapshot& controllerFeedSnapshot,
    const RadioReachableBuildOptions& options);

// The Brain may add APP/DEP facts that are outside current aircraft reception
// only when source evidence ties the position to the departure or arrival
// terminal package. The global VATSIM/AFV roster remains source evidence; it
// never becomes the detailed candidate list by itself.
RadioReachableControllerSnapshot
BuildBrainEndpointScopedControllerSnapshot(
    const RadioReachableControllerSnapshot& snapshot,
    const TransceiverResolutionSnapshot& transceiverSnapshot,
    const BrainTerminalAuthorityWorkerOutput& departureAuthority,
    const BrainTerminalAuthorityWorkerOutput& arrivalAuthority,
    const RouteSectorSnapshot* route,
    const BrainEndpointTerminalEvidenceScope& departureScope,
    const BrainEndpointTerminalEvidenceScope& arrivalScope,
    double terminalTransmitterRadiusNm);

// Stable identity of the controller facts that can affect a Brain decision.
// It excludes worldwide roster counters, fetch generations, and moving
// aircraft-to-transmitter distances so those transport details cannot make a
// stable scoped controller set appear changed.
std::uint64_t HashBrainRadioControllerDecisionEvidence(
    const RadioReachableControllerSnapshot& snapshot);

bool RadioReachableGroupAllowedForStage(
    RadioReachableFacilityGroup group,
    const RadioReachablePhaseGateOptions& options);

RadioReachableControllerSnapshot ApplyRadioReachablePhaseGate(
    const RadioReachableControllerSnapshot& snapshot,
    const RadioReachablePhaseGateOptions& options);

RadioReachableCandidateDiff DiffRadioReachableSnapshots(
    const RadioReachableControllerSnapshot& previous,
    const RadioReachableControllerSnapshot& current);

RadioReachableVerificationFeed BuildRadioReachableVerificationFeed(
    const RadioReachableControllerSnapshot& gatedSnapshot,
    const RadioReachableCandidateDiff* diff = nullptr);

std::vector<std::string> RadioReachableCandidateSummaries(
    const RadioReachableControllerSnapshot& snapshot);

std::string RadioReachableGroupCountSummary(
    const RadioReachableControllerSnapshot& snapshot);

std::vector<std::string> RadioReachableVerificationFeedSummaries(
    const RadioReachableVerificationFeed& feed);

}  // namespace xvatsim::brain
