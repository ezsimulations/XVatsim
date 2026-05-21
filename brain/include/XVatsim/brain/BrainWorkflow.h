#pragma once

#include <string>

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::brain::workflow {

struct FlightContext {
    bool active = false;
    std::string callsign;
    std::string departureIcao;
    double departureLatDeg = 0.0;
    double departureLonDeg = 0.0;
    bool hasDepartureCoordinates = false;
    std::string destinationIcao;
    double destinationLatDeg = 0.0;
    double destinationLonDeg = 0.0;
    bool hasDestinationCoordinates = false;
    std::string routeText;
};

struct WorkflowState {
    FlightContext flightContext;
    bool departureReleasedThisFlight = false;
    bool arrivalAwakeThisFlight = false;
    double airborneSinceSeconds = -1.0;
};

struct WorkflowTuning {
    double arrivalWakeDistanceNm = 200.0;
    double destinationGroundDistanceNm = 5.0;
    double departureConfirmDistanceNm = 10.0;
    double departureReleaseHoldSeconds = 180.0;
};

struct HandoffDecision {
    WorkflowStage stage = WorkflowStage::None;
    std::string reason = "no-flight-context";
};

enum class RecoveryRequestMode {
    AutomaticReconnect,
    Manual,
};

struct RecoveryDecision {
    bool accepted = false;
    WorkflowStage stage = WorkflowStage::None;
    std::string reason = "not-evaluated";
    FlightContext flightContext;
    bool usedPreservedContext = false;
    bool usedFreshNetworkPlan = false;
};

double DistanceToDestinationNm(
    const AircraftStateSnapshot& aircraftState,
    const FlightContext& flightContext);

bool IsInsideArrivalWakeDistance(
    const AircraftStateSnapshot& aircraftState,
    const FlightContext& flightContext,
    const WorkflowTuning& tuning = {});

bool IsOnGroundAtDestination(
    const AircraftStateSnapshot& aircraftState,
    const FlightContext& flightContext,
    const WorkflowTuning& tuning = {});

bool CanConfirmDepartureLocation(
    const AircraftStateSnapshot& aircraftState,
    const FlightPlanSnapshot& flightPlanSnapshot,
    const NetworkPlanSnapshot& networkPlanSnapshot,
    const WorkflowTuning& tuning = {});

RecoveryDecision ResolveCurrentFlightRecovery(
    const AircraftStateSnapshot& aircraftState,
    const FlightPlanSnapshot& flightPlanSnapshot,
    const NetworkPlanSnapshot& networkPlanSnapshot,
    const FlightContext& preservedFlightContext,
    RecoveryRequestMode mode,
    const WorkflowTuning& tuning = {});

HandoffDecision ResolveWorkflowStage(
    const AircraftStateSnapshot& aircraftState,
    const RadioStateSnapshot& radioStateSnapshot,
    bool departureTerminalCoverageKnown,
    bool insideDepartureTerminalCoverage,
    const ModuleBoardSnapshot& departureBoardSnapshot,
    const ModuleBoardSnapshot& enrouteBoardSnapshot,
    double nowSeconds,
    WorkflowState* state,
    const WorkflowTuning& tuning = {});

}  // namespace xvatsim::brain::workflow
