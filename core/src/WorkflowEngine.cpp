#include "XVatsim/core/WorkflowEngine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

namespace xvatsim::core::workflow {

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

bool AirportsMatch(const std::string& left, const std::string& right) {
    return !left.empty() && !right.empty() && NormalizeIcao(left) == NormalizeIcao(right);
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

std::string BoardStationKey(const brain::BoardStationSnapshot& station) {
    return std::to_string(static_cast<int>(station.role)) + "|" +
           station.callsign + "|" + NormalizeFrequency(station.frequency);
}

void AppendBoardStationsUnique(
    const brain::ModuleBoardSnapshot& source,
    brain::ModuleBoardSnapshot* target) {
    if (target == nullptr) {
        return;
    }

    std::unordered_set<std::string> existingKeys;
    existingKeys.reserve(target->stations.size());
    for (const auto& station : target->stations) {
        existingKeys.insert(BoardStationKey(station));
    }

    for (const auto& station : source.stations) {
        const auto key = BoardStationKey(station);
        if (existingKeys.insert(key).second) {
            target->stations.push_back(station);
        }
    }

    target->available = target->available || source.available;
    target->displayStations = target->displayStations || source.displayStations;
}

brain::ModuleBoardSnapshot CurrentEnrouteCenterBoard(
    const brain::ModuleBoardSnapshot& enrouteBoardSnapshot) {
    auto snapshot = enrouteBoardSnapshot;
    snapshot.stations.clear();
    snapshot.available = false;
    snapshot.displayStations = false;

    for (const auto& station : enrouteBoardSnapshot.stations) {
        if (station.role != brain::StationRole::Center) {
            continue;
        }

        if (!IsLiveRouteCenterStation(station)) {
            continue;
        }

        const auto currentRouteCenter =
            station.sectorActive ||
            (station.hasRouteEntryDistance && station.routeEntryDistanceNm <= 0.5);
        if (!currentRouteCenter && !station.tuned) {
            continue;
        }

        snapshot.stations.push_back(station);
        snapshot.available = true;
        snapshot.displayStations = true;
    }

    return snapshot;
}

brain::ModuleBoardSnapshot OnlineEnrouteDisplayBoard(
    const brain::ModuleBoardSnapshot& enrouteBoardSnapshot) {
    auto snapshot = enrouteBoardSnapshot;
    snapshot.stations.clear();
    snapshot.available = false;
    snapshot.displayStations = false;

    for (const auto& station : enrouteBoardSnapshot.stations) {
        if (!IsLiveRouteCenterStation(station)) {
            continue;
        }

        snapshot.stations.push_back(station);
        snapshot.available = true;
        snapshot.displayStations = true;
    }

    return snapshot;
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

brain::ModuleBoardSnapshot BuildDisplayBoard(
    brain::WorkflowStage workflowStage,
    const brain::ModuleBoardSnapshot& departureBoardSnapshot,
    const brain::ModuleBoardSnapshot& arrivalBoardSnapshot,
    const brain::ModuleBoardSnapshot& enrouteBoardSnapshot) {
    using brain::WorkflowStage;

    if (workflowStage == WorkflowStage::Enroute) {
        return OnlineEnrouteDisplayBoard(enrouteBoardSnapshot);
    }

    if (workflowStage == WorkflowStage::Arrival) {
        auto displayBoard = arrivalBoardSnapshot;
        displayBoard.source = brain::BoardSource::Arrival;
        AppendBoardStationsUnique(OnlineEnrouteDisplayBoard(enrouteBoardSnapshot), &displayBoard);
        displayBoard.displayStations =
            displayBoard.displayStations || !displayBoard.stations.empty();
        return displayBoard;
    }

    if (workflowStage == WorkflowStage::Departure) {
        auto displayBoard = departureBoardSnapshot;
        displayBoard.source = brain::BoardSource::Departure;
        const auto currentCenterBoard = CurrentEnrouteCenterBoard(enrouteBoardSnapshot);
        AppendBoardStationsUnique(currentCenterBoard, &displayBoard);
        displayBoard.displayStations =
            displayBoard.displayStations || !displayBoard.stations.empty();
        return displayBoard;
    }

    return {};
}

}  // namespace xvatsim::core::workflow
