#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::brain {

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

struct RadioReachableControllerCandidate {
    std::string callsign;
    std::string frequency;
    int vatsimFacility = 0;
    RadioReachableFacilityGroup group = RadioReachableFacilityGroup::Other;
    RadioReachableSource source = RadioReachableSource::Unknown;
    bool actionable = true;
    bool atis = false;
    int visualRangeNm = 0;
    bool hasDistanceNm = false;
    double distanceNm = 0.0;
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
    std::vector<RadioReachableControllerCandidate> candidates;
};

struct RadioReachablePhaseGateOptions {
    WorkflowStage stage = WorkflowStage::None;
    bool includeAtis = false;
    std::string reason;
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
