#include "XVatsim/brain/BrainWorkflow.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace xvatsim::brain::workflow {

namespace brain = ::xvatsim::brain;

namespace {

double ToRadians(double degrees) {
    return degrees * 3.14159265358979323846 / 180.0;
}

double GreatCircleDistanceNm(
    double latitudeDegA,
    double longitudeDegA,
    double latitudeDegB,
    double longitudeDegB) {
    const auto latitudeRadA = ToRadians(latitudeDegA);
    const auto latitudeRadB = ToRadians(latitudeDegB);
    const auto deltaLatitude = ToRadians(latitudeDegB - latitudeDegA);
    const auto deltaLongitude = ToRadians(longitudeDegB - longitudeDegA);

    const auto sinLatitude = std::sin(deltaLatitude / 2.0);
    const auto sinLongitude = std::sin(deltaLongitude / 2.0);
    const auto a = sinLatitude * sinLatitude +
                   std::cos(latitudeRadA) * std::cos(latitudeRadB) *
                       sinLongitude * sinLongitude;
    const auto c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return 3440.065 * c;
}

std::string NormalizeIcao(std::string airportIcao) {
    std::string normalized;
    normalized.reserve(airportIcao.size());
    for (const auto character : airportIcao) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0) {
            continue;
        }

        normalized.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(character))));
    }
    return normalized;
}

std::string NormalizeCallsign(std::string callsign) {
    std::string normalized;
    normalized.reserve(callsign.size());
    for (const auto character : callsign) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0) {
            continue;
        }

        normalized.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(character))));
    }
    return normalized;
}

bool AirportsMatch(const std::string& left, const std::string& right) {
    return !left.empty() && !right.empty() && NormalizeIcao(left) == NormalizeIcao(right);
}

bool HasUsableFiledRoute(const brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    return networkPlanSnapshot.matched &&
           !networkPlanSnapshot.stale &&
           !networkPlanSnapshot.departureIcao.empty() &&
           !networkPlanSnapshot.destinationIcao.empty();
}

bool PlanMatchesFlightContext(
    const FlightContext& flightContext,
    const brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    return flightContext.active &&
           HasUsableFiledRoute(networkPlanSnapshot) &&
           AirportsMatch(flightContext.departureIcao, networkPlanSnapshot.departureIcao) &&
           AirportsMatch(flightContext.destinationIcao, networkPlanSnapshot.destinationIcao);
}

void ApplyMissingAirportCoordinates(
    const std::string& targetIcao,
    const std::string& sourceIcao,
    bool sourceHasCoordinates,
    double sourceLatitudeDeg,
    double sourceLongitudeDeg,
    double* targetLatitudeDeg,
    double* targetLongitudeDeg,
    bool* targetHasCoordinates) {
    if (targetLatitudeDeg == nullptr ||
        targetLongitudeDeg == nullptr ||
        targetHasCoordinates == nullptr ||
        *targetHasCoordinates ||
        !sourceHasCoordinates ||
        !AirportsMatch(targetIcao, sourceIcao)) {
        return;
    }

    *targetLatitudeDeg = sourceLatitudeDeg;
    *targetLongitudeDeg = sourceLongitudeDeg;
    *targetHasCoordinates = true;
}

bool CoordinatesDiffer(
    double leftLatitudeDeg,
    double leftLongitudeDeg,
    double rightLatitudeDeg,
    double rightLongitudeDeg) {
    constexpr double kCoordinateToleranceDeg = 1e-4;
    return std::fabs(leftLatitudeDeg - rightLatitudeDeg) >
               kCoordinateToleranceDeg ||
           std::fabs(leftLongitudeDeg - rightLongitudeDeg) >
               kCoordinateToleranceDeg;
}

bool ApplyAuthoritativeAirportCoordinates(
    const std::string& targetIcao,
    const std::string& sourceIcao,
    bool sourceHasCoordinates,
    double sourceLatitudeDeg,
    double sourceLongitudeDeg,
    double* targetLatitudeDeg,
    double* targetLongitudeDeg,
    bool* targetHasCoordinates) {
    if (targetLatitudeDeg == nullptr ||
        targetLongitudeDeg == nullptr ||
        targetHasCoordinates == nullptr ||
        !sourceHasCoordinates ||
        !AirportsMatch(targetIcao, sourceIcao)) {
        return false;
    }

    if (*targetHasCoordinates &&
        !CoordinatesDiffer(
            *targetLatitudeDeg,
            *targetLongitudeDeg,
            sourceLatitudeDeg,
            sourceLongitudeDeg)) {
        return false;
    }

    *targetLatitudeDeg = sourceLatitudeDeg;
    *targetLongitudeDeg = sourceLongitudeDeg;
    *targetHasCoordinates = true;
    return true;
}

bool ApplyMissingAirportCoordinatesWithChange(
    const std::string& targetIcao,
    const std::string& sourceIcao,
    bool sourceHasCoordinates,
    double sourceLatitudeDeg,
    double sourceLongitudeDeg,
    double* targetLatitudeDeg,
    double* targetLongitudeDeg,
    bool* targetHasCoordinates) {
    if (targetHasCoordinates == nullptr) {
        return false;
    }
    const auto hadCoordinates = *targetHasCoordinates;
    ApplyMissingAirportCoordinates(
        targetIcao,
        sourceIcao,
        sourceHasCoordinates,
        sourceLatitudeDeg,
        sourceLongitudeDeg,
        targetLatitudeDeg,
        targetLongitudeDeg,
        targetHasCoordinates);
    return !hadCoordinates && *targetHasCoordinates;
}

FlightContext BuildLockedFlightContextFromNetworkPlan(
    const brain::PilotIdentitySnapshot& pilotIdentitySnapshot,
    const brain::FlightPlanSnapshot& flightPlanSnapshot,
    const brain::NetworkPlanSnapshot& networkPlanSnapshot,
    const FlightContext& previousContext) {
    FlightContext nextContext;
    nextContext.active = true;
    nextContext.callsign = pilotIdentitySnapshot.callsign;
    nextContext.departureIcao = networkPlanSnapshot.departureIcao;
    nextContext.departureLatDeg = networkPlanSnapshot.departureLatDeg;
    nextContext.departureLonDeg = networkPlanSnapshot.departureLonDeg;
    nextContext.hasDepartureCoordinates =
        networkPlanSnapshot.hasDepartureCoordinates;
    nextContext.destinationIcao = networkPlanSnapshot.destinationIcao;
    nextContext.destinationLatDeg = networkPlanSnapshot.destinationLatDeg;
    nextContext.destinationLonDeg = networkPlanSnapshot.destinationLonDeg;
    nextContext.hasDestinationCoordinates =
        networkPlanSnapshot.hasDestinationCoordinates;
    nextContext.routeText = networkPlanSnapshot.routeText;

    ApplyMissingAirportCoordinates(
        nextContext.departureIcao,
        flightPlanSnapshot.departureIcao,
        flightPlanSnapshot.hasDepartureCoordinates,
        flightPlanSnapshot.departureLatDeg,
        flightPlanSnapshot.departureLonDeg,
        &nextContext.departureLatDeg,
        &nextContext.departureLonDeg,
        &nextContext.hasDepartureCoordinates);
    ApplyMissingAirportCoordinates(
        nextContext.destinationIcao,
        flightPlanSnapshot.destinationIcao,
        flightPlanSnapshot.hasDestinationCoordinates,
        flightPlanSnapshot.destinationLatDeg,
        flightPlanSnapshot.destinationLonDeg,
        &nextContext.destinationLatDeg,
        &nextContext.destinationLonDeg,
        &nextContext.hasDestinationCoordinates);
    ApplyMissingAirportCoordinates(
        nextContext.departureIcao,
        previousContext.departureIcao,
        previousContext.hasDepartureCoordinates,
        previousContext.departureLatDeg,
        previousContext.departureLonDeg,
        &nextContext.departureLatDeg,
        &nextContext.departureLonDeg,
        &nextContext.hasDepartureCoordinates);
    ApplyMissingAirportCoordinates(
        nextContext.destinationIcao,
        previousContext.destinationIcao,
        previousContext.hasDestinationCoordinates,
        previousContext.destinationLatDeg,
        previousContext.destinationLonDeg,
        &nextContext.destinationLatDeg,
        &nextContext.destinationLonDeg,
        &nextContext.hasDestinationCoordinates);
    return nextContext;
}

FlightContext BuildRecoveredFlightContext(
    const brain::FlightPlanSnapshot& flightPlanSnapshot,
    const brain::NetworkPlanSnapshot& networkPlanSnapshot,
    const FlightContext& preservedFlightContext,
    bool usePreservedContext) {
    FlightContext recoveredContext = usePreservedContext ? preservedFlightContext : FlightContext{};
    recoveredContext.active = true;
    if (!networkPlanSnapshot.matchedCallsign.empty()) {
        recoveredContext.callsign = networkPlanSnapshot.matchedCallsign;
    }
    recoveredContext.departureIcao = networkPlanSnapshot.departureIcao;
    recoveredContext.destinationIcao = networkPlanSnapshot.destinationIcao;
    recoveredContext.routeText = networkPlanSnapshot.routeText;

    if (networkPlanSnapshot.hasDepartureCoordinates) {
        recoveredContext.departureLatDeg = networkPlanSnapshot.departureLatDeg;
        recoveredContext.departureLonDeg = networkPlanSnapshot.departureLonDeg;
        recoveredContext.hasDepartureCoordinates = true;
    } else {
        recoveredContext.hasDepartureCoordinates =
            usePreservedContext &&
            AirportsMatch(preservedFlightContext.departureIcao, recoveredContext.departureIcao) &&
            preservedFlightContext.hasDepartureCoordinates;
        if (recoveredContext.hasDepartureCoordinates) {
            recoveredContext.departureLatDeg = preservedFlightContext.departureLatDeg;
            recoveredContext.departureLonDeg = preservedFlightContext.departureLonDeg;
        }
    }
    ApplyMissingAirportCoordinates(
        recoveredContext.departureIcao,
        flightPlanSnapshot.departureIcao,
        flightPlanSnapshot.hasDepartureCoordinates,
        flightPlanSnapshot.departureLatDeg,
        flightPlanSnapshot.departureLonDeg,
        &recoveredContext.departureLatDeg,
        &recoveredContext.departureLonDeg,
        &recoveredContext.hasDepartureCoordinates);

    if (networkPlanSnapshot.hasDestinationCoordinates) {
        recoveredContext.destinationLatDeg = networkPlanSnapshot.destinationLatDeg;
        recoveredContext.destinationLonDeg = networkPlanSnapshot.destinationLonDeg;
        recoveredContext.hasDestinationCoordinates = true;
    } else {
        recoveredContext.hasDestinationCoordinates =
            usePreservedContext &&
            AirportsMatch(
                preservedFlightContext.destinationIcao,
                recoveredContext.destinationIcao) &&
            preservedFlightContext.hasDestinationCoordinates;
        if (recoveredContext.hasDestinationCoordinates) {
            recoveredContext.destinationLatDeg = preservedFlightContext.destinationLatDeg;
            recoveredContext.destinationLonDeg = preservedFlightContext.destinationLonDeg;
        }
    }
    ApplyMissingAirportCoordinates(
        recoveredContext.destinationIcao,
        flightPlanSnapshot.destinationIcao,
        flightPlanSnapshot.hasDestinationCoordinates,
        flightPlanSnapshot.destinationLatDeg,
        flightPlanSnapshot.destinationLonDeg,
        &recoveredContext.destinationLatDeg,
        &recoveredContext.destinationLonDeg,
        &recoveredContext.hasDestinationCoordinates);

    return recoveredContext;
}

std::string NormalizeFrequency(std::string frequency) {
    frequency.erase(
        std::remove_if(
            frequency.begin(),
            frequency.end(),
            [](unsigned char c) { return std::isspace(c) != 0; }),
        frequency.end());

    std::string digits;
    bool sawDecimal = false;
    int decimals = 0;
    for (const auto character : frequency) {
        if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
            digits.push_back(character);
            if (sawDecimal && decimals < 3) {
                ++decimals;
            }
            continue;
        }

        if (character == '.' && !sawDecimal) {
            sawDecimal = true;
        }
    }

    if (digits.empty()) {
        return {};
    }

    if (sawDecimal) {
        while (decimals < 3) {
            digits.push_back('0');
            ++decimals;
        }
    } else if (digits.size() == 5) {
        digits.push_back('0');
    }

    return digits;
}

bool IsCom1TunedToFrequency(
    const brain::RadioStateSnapshot& radioStateSnapshot,
    const std::string& frequency) {
    const auto normalizedTarget = NormalizeFrequency(frequency);
    if (normalizedTarget.empty()) {
        return false;
    }

    return NormalizeFrequency(radioStateSnapshot.com1ActiveFrequency) == normalizedTarget;
}

bool IsLiveRouteCenterStation(const brain::BoardStationSnapshot& station) {
    return station.role == brain::StationRole::Center &&
           !station.offline &&
           !station.frequency.empty();
}

bool IsDepartureTerminalRole(brain::StationRole role) {
    return role == brain::StationRole::Approach ||
           role == brain::StationRole::Departure;
}

bool HasLiveDepartureTerminalController(
    const brain::ModuleBoardSnapshot& departureBoardSnapshot) {
    for (const auto& station : departureBoardSnapshot.stations) {
        if (IsDepartureTerminalRole(station.role) &&
            !station.offline &&
            !station.frequency.empty()) {
            return true;
        }
    }

    return false;
}

bool HasCom1TunedDepartureTerminalController(
    const brain::ModuleBoardSnapshot& departureBoardSnapshot,
    const brain::RadioStateSnapshot& radioStateSnapshot) {
    for (const auto& station : departureBoardSnapshot.stations) {
        if (IsDepartureTerminalRole(station.role) &&
            !station.offline &&
            !station.frequency.empty() &&
            IsCom1TunedToFrequency(radioStateSnapshot, station.frequency)) {
            return true;
        }
    }

    return false;
}

bool HasCom1TunedLiveRouteCenter(
    const brain::ModuleBoardSnapshot& enrouteBoardSnapshot,
    const brain::RadioStateSnapshot& radioStateSnapshot) {
    for (const auto& station : enrouteBoardSnapshot.stations) {
        if (IsLiveRouteCenterStation(station) &&
            IsCom1TunedToFrequency(radioStateSnapshot, station.frequency)) {
            return true;
        }
    }

    return false;
}

}  // namespace

double DistanceToDestinationNm(
    const brain::AircraftStateSnapshot& aircraftState,
    const FlightContext& flightContext) {
    if (!aircraftState.valid || !flightContext.active || !flightContext.hasDestinationCoordinates) {
        return -1.0;
    }

    return GreatCircleDistanceNm(
        aircraftState.latitudeDeg,
        aircraftState.longitudeDeg,
        flightContext.destinationLatDeg,
        flightContext.destinationLonDeg);
}

bool IsInsideArrivalWakeDistance(
    const brain::AircraftStateSnapshot& aircraftState,
    const FlightContext& flightContext,
    const WorkflowTuning& tuning) {
    const auto distanceToDestinationNm = DistanceToDestinationNm(aircraftState, flightContext);
    return distanceToDestinationNm >= 0.0 &&
           distanceToDestinationNm <= tuning.arrivalWakeDistanceNm;
}

bool IsOnGroundAtDestination(
    const brain::AircraftStateSnapshot& aircraftState,
    const FlightContext& flightContext,
    const WorkflowTuning& tuning) {
    const auto distanceToDestinationNm = DistanceToDestinationNm(aircraftState, flightContext);
    return aircraftState.onGround &&
           distanceToDestinationNm >= 0.0 &&
           distanceToDestinationNm <= tuning.destinationGroundDistanceNm;
}

bool CanConfirmDepartureLocation(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::FlightPlanSnapshot& flightPlanSnapshot,
    const brain::NetworkPlanSnapshot& networkPlanSnapshot,
    const WorkflowTuning& tuning) {
    if (!networkPlanSnapshot.matched || networkPlanSnapshot.departureIcao.empty()) {
        return false;
    }

    if (!aircraftState.valid) {
        return false;
    }

    if (AirportsMatch(flightPlanSnapshot.currentAirportIcao, networkPlanSnapshot.departureIcao)) {
        return true;
    }

    if (networkPlanSnapshot.hasDepartureCoordinates) {
        const auto distanceNm = GreatCircleDistanceNm(
            aircraftState.latitudeDeg,
            aircraftState.longitudeDeg,
            networkPlanSnapshot.departureLatDeg,
            networkPlanSnapshot.departureLonDeg);
        if (distanceNm <= tuning.departureConfirmDistanceNm) {
            return true;
        }
    }

    return false;
}

RecoveryDecision ResolveCurrentFlightRecovery(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::FlightPlanSnapshot& flightPlanSnapshot,
    const brain::NetworkPlanSnapshot& networkPlanSnapshot,
    const FlightContext& preservedFlightContext,
    RecoveryRequestMode mode,
    const WorkflowTuning& tuning) {
    RecoveryDecision decision;
    decision.reason =
        mode == RecoveryRequestMode::Manual
            ? "manual-recovery-not-evaluated"
            : "automatic-recovery-not-evaluated";

    if (!aircraftState.valid) {
        decision.reason = "aircraft-state-invalid";
        return decision;
    }
    if (!aircraftState.batteryOn) {
        decision.reason = "battery-off";
        return decision;
    }
    if (!networkPlanSnapshot.matched || networkPlanSnapshot.stale) {
        decision.reason = "plan-unavailable";
        return decision;
    }
    if (!HasUsableFiledRoute(networkPlanSnapshot)) {
        decision.reason = "plan-route-missing";
        return decision;
    }

    const auto usePreservedContext =
        PlanMatchesFlightContext(preservedFlightContext, networkPlanSnapshot);
    if (preservedFlightContext.active &&
        !preservedFlightContext.callsign.empty() &&
        !networkPlanSnapshot.matchedCallsign.empty() &&
        NormalizeCallsign(preservedFlightContext.callsign) !=
            NormalizeCallsign(networkPlanSnapshot.matchedCallsign)) {
        decision.reason = "callsign-changed";
        return decision;
    }
    if (preservedFlightContext.active && !usePreservedContext) {
        decision.reason = "route-changed";
        return decision;
    }

    auto recoveredContext = BuildRecoveredFlightContext(
        flightPlanSnapshot,
        networkPlanSnapshot,
        preservedFlightContext,
        usePreservedContext);

    if (IsOnGroundAtDestination(aircraftState, recoveredContext, tuning)) {
        decision.accepted = true;
        decision.stage = brain::WorkflowStage::Arrival;
        decision.reason = "recovery-destination-ground";
    } else if (aircraftState.onGround) {
        if (!CanConfirmDepartureLocation(
                aircraftState,
                flightPlanSnapshot,
                networkPlanSnapshot,
                tuning)) {
            decision.reason = "ground-not-at-route-endpoint";
            return decision;
        }

        decision.accepted = true;
        decision.stage = brain::WorkflowStage::Departure;
        decision.reason = "recovery-departure-ground";
    } else if (IsInsideArrivalWakeDistance(aircraftState, recoveredContext, tuning)) {
        decision.accepted = true;
        decision.stage = brain::WorkflowStage::Arrival;
        decision.reason = "recovery-arrival-distance";
    } else {
        decision.accepted = true;
        decision.stage = brain::WorkflowStage::Enroute;
        decision.reason = "recovery-enroute-airborne";
    }

    decision.flightContext = std::move(recoveredContext);
    decision.usedPreservedContext = usePreservedContext;
    decision.usedFreshNetworkPlan = true;
    return decision;
}

XPilotSessionBoundaryDecision ResolveXPilotSessionBoundary(
    const XPilotSessionBoundaryInput& input) {
    XPilotSessionBoundaryDecision decision;
    decision.nextState = input.state;

    auto connectedCallsign =
        NormalizeCallsign(
            input.pilotIdentity.normalizedCallsign.empty()
                ? input.pilotIdentity.callsign
                : input.pilotIdentity.normalizedCallsign);
    if (connectedCallsign.empty()) {
        connectedCallsign = NormalizeCallsign(input.xPilotSession.callsign);
    }

    if (input.state.lastXPilotConnected && !input.xPilotSession.connected) {
        decision.action = XPilotSessionBoundaryAction::ResetForDisconnect;
        decision.nextState.disconnectedPilotCallsign =
            input.state.lastConnectedPilotCallsign;
        decision.nextState.lastXPilotConnected = false;
        decision.shouldPreserveFlightStateForDisconnect = true;
        decision.shouldClearPendingRecoveryRequests = true;
        return decision;
    }

    if (!input.xPilotSession.connected) {
        decision.nextState.lastXPilotConnected = false;
        return decision;
    }

    const auto reconnectCallsignChanged =
        !input.state.disconnectedPilotCallsign.empty() &&
        !connectedCallsign.empty() &&
        connectedCallsign != input.state.disconnectedPilotCallsign;
    if (reconnectCallsignChanged) {
        decision.action = XPilotSessionBoundaryAction::ResetForCallsignChange;
        decision.nextState = {};
        decision.shouldResetFlightScopedState = true;
        decision.resetReason = "pilot callsign changed after reconnect";
        return decision;
    }

    const auto callsignChanged =
        input.state.lastXPilotConnected &&
        !connectedCallsign.empty() &&
        !input.state.lastConnectedPilotCallsign.empty() &&
        connectedCallsign != input.state.lastConnectedPilotCallsign;
    if (callsignChanged) {
        decision.action = XPilotSessionBoundaryAction::ResetForCallsignChange;
        decision.nextState = {};
        decision.shouldResetFlightScopedState = true;
        decision.resetReason = "pilot callsign changed";
        return decision;
    }

    if (!input.state.disconnectedPilotCallsign.empty()) {
        decision.shouldQueueAutomaticRecovery = true;
        decision.nextState.disconnectedPilotCallsign.clear();
        decision.logLine =
            "[XVatsim] xPilot reconnect detected; waiting for fresh matched plan to recover current flight.\n";
    }

    decision.nextState.lastXPilotConnected = true;
    if (!connectedCallsign.empty()) {
        decision.nextState.lastConnectedPilotCallsign = connectedCallsign;
    }
    decision.sawXPilotConnectedThisFlight = true;
    return decision;
}

FlightContextUpdateOutput UpdateFlightContextFromNetworkPlan(
    const FlightContextUpdateInput& input) {
    FlightContextUpdateOutput output;
    output.flightContext = input.currentContext;

    if (!input.pilotIdentity.connected ||
        !input.networkPlan.matched ||
        input.networkPlan.stale) {
        return output;
    }

    const auto callsignChanged =
        input.currentContext.active &&
        !input.pilotIdentity.callsign.empty() &&
        input.currentContext.callsign != input.pilotIdentity.callsign;
    const auto routeChanged =
        input.currentContext.active &&
        (!AirportsMatch(
             input.currentContext.departureIcao,
             input.networkPlan.departureIcao) ||
         !AirportsMatch(
             input.currentContext.destinationIcao,
             input.networkPlan.destinationIcao));

    if (!input.currentContext.active || callsignChanged || routeChanged) {
        if (!CanConfirmDepartureLocation(
                input.aircraftState,
                input.flightPlan,
                input.networkPlan,
                input.tuning)) {
            return output;
        }

        output.flightContext =
            BuildLockedFlightContextFromNetworkPlan(
                input.pilotIdentity,
                input.flightPlan,
                input.networkPlan,
                input.currentContext);
        output.changed = true;
        output.lockedNewContext = true;
        output.shouldResetFlightScopedState = true;
        output.shouldInvalidatePresentation = true;
        return output;
    }

    auto nextContext = input.currentContext;
    bool changed = false;

    if (!input.pilotIdentity.callsign.empty() &&
        nextContext.callsign != input.pilotIdentity.callsign) {
        nextContext.callsign = input.pilotIdentity.callsign;
        changed = true;
    }

    if (!input.networkPlan.routeText.empty() &&
        nextContext.routeText != input.networkPlan.routeText) {
        nextContext.routeText = input.networkPlan.routeText;
        changed = true;
    }

    changed =
        ApplyAuthoritativeAirportCoordinates(
            nextContext.departureIcao,
            input.networkPlan.departureIcao,
            input.networkPlan.hasDepartureCoordinates,
            input.networkPlan.departureLatDeg,
            input.networkPlan.departureLonDeg,
            &nextContext.departureLatDeg,
            &nextContext.departureLonDeg,
            &nextContext.hasDepartureCoordinates) ||
        changed;
    changed =
        ApplyMissingAirportCoordinatesWithChange(
            nextContext.departureIcao,
            input.flightPlan.departureIcao,
            input.flightPlan.hasDepartureCoordinates,
            input.flightPlan.departureLatDeg,
            input.flightPlan.departureLonDeg,
            &nextContext.departureLatDeg,
            &nextContext.departureLonDeg,
            &nextContext.hasDepartureCoordinates) ||
        changed;

    changed =
        ApplyAuthoritativeAirportCoordinates(
            nextContext.destinationIcao,
            input.networkPlan.destinationIcao,
            input.networkPlan.hasDestinationCoordinates,
            input.networkPlan.destinationLatDeg,
            input.networkPlan.destinationLonDeg,
            &nextContext.destinationLatDeg,
            &nextContext.destinationLonDeg,
            &nextContext.hasDestinationCoordinates) ||
        changed;
    changed =
        ApplyMissingAirportCoordinatesWithChange(
            nextContext.destinationIcao,
            input.flightPlan.destinationIcao,
            input.flightPlan.hasDestinationCoordinates,
            input.flightPlan.destinationLatDeg,
            input.flightPlan.destinationLonDeg,
            &nextContext.destinationLatDeg,
            &nextContext.destinationLonDeg,
            &nextContext.hasDestinationCoordinates) ||
        changed;

    if (!changed) {
        return output;
    }

    output.flightContext = std::move(nextContext);
    output.changed = true;
    output.metadataChanged = true;
    output.shouldInvalidatePresentation = true;
    return output;
}

FlightContextRetargetOutput RetargetFlightContextToNetworkPlan(
    const FlightContextRetargetInput& input) {
    FlightContextRetargetOutput output;
    output.flightContext = input.currentContext;

    if (!input.currentContext.active) {
        return output;
    }

    auto nextContext = input.currentContext;
    nextContext.callsign =
        input.pilotIdentity.callsign.empty()
            ? input.currentContext.callsign
            : input.pilotIdentity.callsign;
    nextContext.routeText = input.networkPlan.routeText;

    if (!input.networkPlan.departureIcao.empty()) {
        nextContext.departureIcao = input.networkPlan.departureIcao;
    }
    if (input.networkPlan.hasDepartureCoordinates) {
        nextContext.departureLatDeg = input.networkPlan.departureLatDeg;
        nextContext.departureLonDeg = input.networkPlan.departureLonDeg;
        nextContext.hasDepartureCoordinates = true;
    } else {
        ApplyMissingAirportCoordinates(
            nextContext.departureIcao,
            input.flightPlan.departureIcao,
            input.flightPlan.hasDepartureCoordinates,
            input.flightPlan.departureLatDeg,
            input.flightPlan.departureLonDeg,
            &nextContext.departureLatDeg,
            &nextContext.departureLonDeg,
            &nextContext.hasDepartureCoordinates);
    }

    nextContext.destinationIcao = input.networkPlan.destinationIcao;
    nextContext.destinationLatDeg = input.networkPlan.destinationLatDeg;
    nextContext.destinationLonDeg = input.networkPlan.destinationLonDeg;
    nextContext.hasDestinationCoordinates =
        input.networkPlan.hasDestinationCoordinates;
    ApplyMissingAirportCoordinates(
        nextContext.destinationIcao,
        input.flightPlan.destinationIcao,
        input.flightPlan.hasDestinationCoordinates,
        input.flightPlan.destinationLatDeg,
        input.flightPlan.destinationLonDeg,
        &nextContext.destinationLatDeg,
        &nextContext.destinationLonDeg,
        &nextContext.hasDestinationCoordinates);

    output.flightContext = std::move(nextContext);
    output.retargeted = true;
    output.shouldResetArrivalWake = true;
    output.shouldResetEnrouteInitialDisplayHold = true;
    output.shouldInvalidatePresentation = true;
    return output;
}

HandoffDecision ResolveWorkflowStage(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::RadioStateSnapshot& radioStateSnapshot,
    bool departureTerminalCoverageKnown,
    bool insideDepartureTerminalCoverage,
    const brain::ModuleBoardSnapshot& departureBoardSnapshot,
    const brain::ModuleBoardSnapshot& enrouteBoardSnapshot,
    double nowSeconds,
    WorkflowState* state,
    const WorkflowTuning& tuning) {
    HandoffDecision decision;
    if (state == nullptr || !state->flightContext.active) {
        return decision;
    }

    if (IsOnGroundAtDestination(aircraftState, state->flightContext, tuning)) {
        state->arrivalAwakeThisFlight = true;
        decision.stage = brain::WorkflowStage::Arrival;
        decision.reason = "destination-ground";
        return decision;
    }

    if (aircraftState.onGround) {
        state->airborneSinceSeconds = -1.0;
        decision.stage = brain::WorkflowStage::Departure;
        decision.reason = "on-ground";
        return decision;
    }

    if (state->arrivalAwakeThisFlight ||
        IsInsideArrivalWakeDistance(aircraftState, state->flightContext, tuning)) {
        state->arrivalAwakeThisFlight = true;
        decision.stage = brain::WorkflowStage::Arrival;
        decision.reason = "arrival-distance";
        return decision;
    }

    if (state->airborneSinceSeconds < 0.0) {
        state->airborneSinceSeconds = nowSeconds;
    }

    if (state->departureReleasedThisFlight) {
        decision.stage = brain::WorkflowStage::Enroute;
        decision.reason = "enroute-primary";
        return decision;
    }

    if (HasCom1TunedLiveRouteCenter(enrouteBoardSnapshot, radioStateSnapshot)) {
        state->departureReleasedThisFlight = true;
        decision.stage = brain::WorkflowStage::Enroute;
        decision.reason = "center-radio-active";
        return decision;
    }

    const auto hasLiveDepartureTerminalController =
        HasLiveDepartureTerminalController(departureBoardSnapshot);
    if (hasLiveDepartureTerminalController &&
        departureTerminalCoverageKnown &&
        insideDepartureTerminalCoverage) {
        decision.stage = brain::WorkflowStage::Departure;
        decision.reason = "departure-terminal-airspace";
        return decision;
    }

    if (departureTerminalCoverageKnown && !insideDepartureTerminalCoverage) {
        state->departureReleasedThisFlight = true;
        decision.stage = brain::WorkflowStage::Enroute;
        decision.reason = "departure-terminal-exited";
        return decision;
    }

    if ((nowSeconds - state->airborneSinceSeconds) < tuning.departureReleaseHoldSeconds) {
        decision.stage = brain::WorkflowStage::Departure;
        decision.reason = "departure-release-hold";
        return decision;
    }

    if (HasCom1TunedDepartureTerminalController(
            departureBoardSnapshot,
            radioStateSnapshot)) {
        decision.stage = brain::WorkflowStage::Departure;
        decision.reason = "departure-terminal-radio";
        return decision;
    }

    state->departureReleasedThisFlight = true;
    decision.stage = brain::WorkflowStage::Enroute;
    decision.reason = "departure-released";
    return decision;
}

}  // namespace xvatsim::brain::workflow
