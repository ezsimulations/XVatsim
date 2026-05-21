#include "XVatsim/brain/PhaseSnapshotPublisher.h"

#include <sstream>

namespace xvatsim::brain {
namespace {

const char* ToToken(WorkflowStage stage) {
    switch (stage) {
    case WorkflowStage::None:
        return "NONE";
    case WorkflowStage::Departure:
        return "DEPARTURE";
    case WorkflowStage::Enroute:
        return "ENROUTE";
    case WorkflowStage::Arrival:
        return "ARRIVAL";
    }
    return "UNKNOWN";
}

bool IsDisplayableBoard(const ModuleBoardSnapshot& snapshot) {
    return snapshot.available && snapshot.displayStations &&
           !snapshot.stations.empty();
}

ModuleBoardSnapshot* MutableSlotForStage(
    PhaseSnapshotPublisherState* state,
    WorkflowStage stage) {
    if (state == nullptr) {
        return nullptr;
    }
    switch (stage) {
    case WorkflowStage::Departure:
        return &state->departure;
    case WorkflowStage::Enroute:
        return &state->enroute;
    case WorkflowStage::Arrival:
        return &state->arrival;
    case WorkflowStage::None:
        break;
    }
    return nullptr;
}

const ModuleBoardSnapshot* SlotForStage(
    const PhaseSnapshotPublisherState& state,
    WorkflowStage stage) {
    switch (stage) {
    case WorkflowStage::Departure:
        return state.hasDeparture ? &state.departure : nullptr;
    case WorkflowStage::Enroute:
        return state.hasEnroute ? &state.enroute : nullptr;
    case WorkflowStage::Arrival:
        return state.hasArrival ? &state.arrival : nullptr;
    case WorkflowStage::None:
        break;
    }
    return nullptr;
}

void MarkSlotProven(PhaseSnapshotPublisherState* state, WorkflowStage stage) {
    if (state == nullptr) {
        return;
    }
    switch (stage) {
    case WorkflowStage::Departure:
        state->hasDeparture = true;
        return;
    case WorkflowStage::Enroute:
        state->hasEnroute = true;
        return;
    case WorkflowStage::Arrival:
        state->hasArrival = true;
        return;
    case WorkflowStage::None:
        return;
    }
}

void AppendStatus(
    std::ostringstream* stream,
    const char* key,
    bool value) {
    if (stream == nullptr) {
        return;
    }
    *stream << "," << key << "=" << (value ? 1 : 0);
}

}  // namespace

void PhaseSnapshotPublisherState::Reset() {
    hasDeparture = false;
    hasEnroute = false;
    hasArrival = false;
    departure = {};
    enroute = {};
    arrival = {};
}

PhaseSnapshotPublishResult PublishPhaseSnapshot(
    PhaseSnapshotPublisherState* state,
    const PhaseSnapshotPublishRequest& request) {
    PhaseSnapshotPublishResult result;
    result.snapshot = request.candidate;
    result.verificationPending = request.verificationPending;

    const auto candidateIsDisplayable =
        IsDisplayableBoard(request.candidate);
    if (state != nullptr && request.stage != WorkflowStage::None &&
        candidateIsDisplayable) {
        if (auto* slot = MutableSlotForStage(state, request.stage)) {
            *slot = request.candidate;
            MarkSlotProven(state, request.stage);
            result.storedNewProven = true;
        }
    } else if (state != nullptr && request.verificationPending) {
        if (const auto* lastProven = SlotForStage(*state, request.stage)) {
            result.snapshot = *lastProven;
            result.usedLastProven = true;
        }
    }

    std::ostringstream status;
    status << "PHASE_PUBLISH stage=" << ToToken(request.stage)
           << ",stations=" << result.snapshot.stations.size();
    AppendStatus(&status, "candidateDisplay", candidateIsDisplayable);
    AppendStatus(&status, "pending", request.verificationPending);
    AppendStatus(&status, "stored", result.storedNewProven);
    AppendStatus(&status, "reused", result.usedLastProven);
    if (!request.reason.empty()) {
        status << ",reason=" << request.reason;
    }
    result.statusLine = status.str();
    return result;
}

std::string PhaseSnapshotPublisherStateSummary(
    const PhaseSnapshotPublisherState& state) {
    std::ostringstream stream;
    stream << "departure=" << (state.hasDeparture ? state.departure.stations.size() : 0)
           << ",enroute=" << (state.hasEnroute ? state.enroute.stations.size() : 0)
           << ",arrival=" << (state.hasArrival ? state.arrival.stations.size() : 0);
    return stream.str();
}

}  // namespace xvatsim::brain
