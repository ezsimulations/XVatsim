#pragma once

#include <string>

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::brain {

struct PhaseSnapshotPublisherState {
    bool hasDeparture = false;
    bool hasEnroute = false;
    bool hasArrival = false;
    ModuleBoardSnapshot departure;
    ModuleBoardSnapshot enroute;
    ModuleBoardSnapshot arrival;

    void Reset();
};

struct PhaseSnapshotPublishRequest {
    WorkflowStage stage = WorkflowStage::None;
    ModuleBoardSnapshot candidate;
    bool verificationPending = false;
    std::string reason;
};

struct PhaseSnapshotPublishResult {
    ModuleBoardSnapshot snapshot;
    bool storedNewProven = false;
    bool usedLastProven = false;
    bool verificationPending = false;
    std::string statusLine;
};

PhaseSnapshotPublishResult PublishPhaseSnapshot(
    PhaseSnapshotPublisherState* state,
    const PhaseSnapshotPublishRequest& request);

std::string PhaseSnapshotPublisherStateSummary(
    const PhaseSnapshotPublisherState& state);

}  // namespace xvatsim::brain
