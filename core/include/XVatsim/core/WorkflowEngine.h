#pragma once

#include "XVatsim/brain/BrainWorkflow.h"

namespace xvatsim::core::workflow {

using ::xvatsim::brain::workflow::CanConfirmDepartureLocation;
using ::xvatsim::brain::workflow::DistanceToDestinationNm;
using ::xvatsim::brain::workflow::FlightContext;
using ::xvatsim::brain::workflow::HandoffDecision;
using ::xvatsim::brain::workflow::IsInsideArrivalWakeDistance;
using ::xvatsim::brain::workflow::IsOnGroundAtDestination;
using ::xvatsim::brain::workflow::RecoveryDecision;
using ::xvatsim::brain::workflow::RecoveryRequestMode;
using ::xvatsim::brain::workflow::ResolveCurrentFlightRecovery;
using ::xvatsim::brain::workflow::ResolveWorkflowStage;
using ::xvatsim::brain::workflow::WorkflowState;
using ::xvatsim::brain::workflow::WorkflowTuning;

}  // namespace xvatsim::core::workflow
