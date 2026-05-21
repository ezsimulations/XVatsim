#pragma once

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::brain {

class BrainOrchestrator {
public:
    BrainOrchestrator() = default;

    static OverlayViewModel BuildOverlayViewModel(
        WorkflowStage workflowStage,
        const AircraftStateSnapshot& aircraftState,
        const XPilotSessionSnapshot& xPilotSessionSnapshot,
        const RadioStateSnapshot& radioStateSnapshot,
        const NetworkPlanSnapshot& networkPlanSnapshot,
        const ControllerFeedSnapshot& controllerFeedSnapshot,
        const TransceiverResolutionSnapshot& transceiverResolutionSnapshot,
        const FinalDisplaySnapshot& finalDisplaySnapshot,
        const ManualQuerySnapshot& manualQuerySnapshot);
};

}  // namespace xvatsim::brain
