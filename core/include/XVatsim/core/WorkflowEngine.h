#pragma once

#include <string>

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::core::workflow {

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
    brain::WorkflowStage stage = brain::WorkflowStage::None;
    std::string reason = "no-flight-context";
};

enum class RecoveryRequestMode {
    AutomaticReconnect,
    Manual,
};

struct RecoveryDecision {
    bool accepted = false;
    brain::WorkflowStage stage = brain::WorkflowStage::None;
    std::string reason = "not-evaluated";
    FlightContext flightContext;
    bool usedPreservedContext = false;
    bool usedFreshNetworkPlan = false;
};

double DistanceToDestinationNm(
    const brain::AircraftStateSnapshot& aircraftState,
    const FlightContext& flightContext);

bool IsInsideArrivalWakeDistance(
    const brain::AircraftStateSnapshot& aircraftState,
    const FlightContext& flightContext,
    const WorkflowTuning& tuning = {});

bool IsOnGroundAtDestination(
    const brain::AircraftStateSnapshot& aircraftState,
    const FlightContext& flightContext,
    const WorkflowTuning& tuning = {});

bool CanConfirmDepartureLocation(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::FlightPlanSnapshot& flightPlanSnapshot,
    const brain::NetworkPlanSnapshot& networkPlanSnapshot,
    const WorkflowTuning& tuning = {});

RecoveryDecision ResolveCurrentFlightRecovery(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::FlightPlanSnapshot& flightPlanSnapshot,
    const brain::NetworkPlanSnapshot& networkPlanSnapshot,
    const FlightContext& preservedFlightContext,
    RecoveryRequestMode mode,
    const WorkflowTuning& tuning = {});

HandoffDecision ResolveWorkflowStage(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::RadioStateSnapshot& radioStateSnapshot,
    bool departureTerminalCoverageKnown,
    bool insideDepartureTerminalCoverage,
    const brain::ModuleBoardSnapshot& departureBoardSnapshot,
    const brain::ModuleBoardSnapshot& enrouteBoardSnapshot,
    double nowSeconds,
    WorkflowState* state,
    const WorkflowTuning& tuning = {});

}  // namespace xvatsim::core::workflow
