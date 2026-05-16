#pragma once

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::brain {

class BrainOrchestrator {
public:
    BrainOrchestrator() = default;

    OverlayViewModel BuildOverlayViewModel(
        WorkflowStage workflowStage,
        const AircraftStateSnapshot& aircraftState,
        const XPilotSessionSnapshot& xPilotSessionSnapshot,
        const RadioStateSnapshot& radioStateSnapshot,
        const NetworkPlanSnapshot& networkPlanSnapshot,
        const ControllerFeedSnapshot& controllerFeedSnapshot,
        const TransceiverResolutionSnapshot& transceiverResolutionSnapshot,
        const ModuleBoardSnapshot& activeBoardSnapshot,
        const ManualQuerySnapshot& manualQuerySnapshot) const;
};

}  // namespace xvatsim::brain
