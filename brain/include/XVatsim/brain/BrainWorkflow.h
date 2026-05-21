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

struct WorkflowSignals {
    bool departureTerminalCoverageKnown = false;
    bool insideDepartureTerminalCoverage = false;
    bool hasLiveDepartureTerminalController = false;
    bool com1TunedDepartureTerminalController = false;
    bool com1TunedLiveRouteCenter = false;
};

enum class RecoveryRequestMode {
    AutomaticReconnect,
    Manual,
};

enum class XPilotSessionBoundaryAction {
    None,
    ResetForDisconnect,
    ResetForCallsignChange,
};

struct XPilotSessionBoundaryState {
    bool lastXPilotConnected = false;
    std::string lastConnectedPilotCallsign;
    std::string disconnectedPilotCallsign;
};

struct XPilotSessionBoundaryInput {
    XPilotSessionSnapshot xPilotSession;
    PilotIdentitySnapshot pilotIdentity;
    XPilotSessionBoundaryState state;
};

struct XPilotSessionBoundaryDecision {
    XPilotSessionBoundaryAction action = XPilotSessionBoundaryAction::None;
    XPilotSessionBoundaryState nextState;
    bool shouldPreserveFlightStateForDisconnect = false;
    bool shouldResetFlightScopedState = false;
    bool shouldClearPendingRecoveryRequests = false;
    bool shouldQueueAutomaticRecovery = false;
    bool sawXPilotConnectedThisFlight = false;
    std::string resetReason;
    std::string logLine;
};

struct AircraftRuntimeBoundaryInput {
    AircraftStateSnapshot aircraftState;
    bool coldDarkResetApplied = false;
    bool aircraftStateInvalidBoundaryActive = false;
};

struct AircraftRuntimeBoundaryDecision {
    bool aircraftStateInvalid = false;
    bool coldDarkBoundaryActive = false;
    bool shouldResetForInvalidAircraftState = false;
    bool shouldResetSessionRuntimeCaches = false;
    bool shouldResetPresentationState = false;
    bool nextColdDarkResetApplied = false;
    bool nextAircraftStateInvalidBoundaryActive = false;
    std::string logLine;
};

struct RecoveryDecision {
    bool accepted = false;
    WorkflowStage stage = WorkflowStage::None;
    std::string reason = "not-evaluated";
    FlightContext flightContext;
    bool usedPreservedContext = false;
    bool usedFreshNetworkPlan = false;
};

struct FlightContextUpdateInput {
    FlightContext currentContext;
    AircraftStateSnapshot aircraftState;
    PilotIdentitySnapshot pilotIdentity;
    FlightPlanSnapshot flightPlan;
    NetworkPlanSnapshot networkPlan;
    WorkflowTuning tuning;
};

struct FlightContextUpdateOutput {
    FlightContext flightContext;
    bool changed = false;
    bool lockedNewContext = false;
    bool metadataChanged = false;
    bool shouldResetFlightScopedState = false;
    bool shouldInvalidatePresentation = false;
};

struct FlightContextRetargetInput {
    FlightContext currentContext;
    PilotIdentitySnapshot pilotIdentity;
    FlightPlanSnapshot flightPlan;
    NetworkPlanSnapshot networkPlan;
};

struct FlightContextRetargetOutput {
    FlightContext flightContext;
    bool retargeted = false;
    bool shouldResetArrivalWake = false;
    bool shouldResetEnrouteInitialDisplayHold = false;
    bool shouldInvalidatePresentation = false;
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

XPilotSessionBoundaryDecision ResolveXPilotSessionBoundary(
    const XPilotSessionBoundaryInput& input);

AircraftRuntimeBoundaryDecision ResolveAircraftRuntimeBoundary(
    const AircraftRuntimeBoundaryInput& input);

FlightContextUpdateOutput UpdateFlightContextFromNetworkPlan(
    const FlightContextUpdateInput& input);

FlightContextRetargetOutput RetargetFlightContextToNetworkPlan(
    const FlightContextRetargetInput& input);

HandoffDecision ResolveWorkflowStageFromSignals(
    const AircraftStateSnapshot& aircraftState,
    const RadioStateSnapshot& radioStateSnapshot,
    const WorkflowSignals& signals,
    double nowSeconds,
    WorkflowState* state,
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
