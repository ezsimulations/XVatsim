#include "XVatsim/brain/RoutePolygonTransition.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <vector>

namespace xvatsim::brain {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusNm = 3440.065;
constexpr double kSameEntryDistanceToleranceNm = 0.25;

struct RoutePoint {
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
};

struct ProjectedPoint {
    double xNm = 0.0;
    double yNm = 0.0;
};

double ToRadians(double degrees) {
    return degrees * kPi / 180.0;
}

double NormalizeLongitudeDeltaDeg(double deltaDeg) {
    while (deltaDeg > 180.0) {
        deltaDeg -= 360.0;
    }
    while (deltaDeg < -180.0) {
        deltaDeg += 360.0;
    }
    return deltaDeg;
}

double GreatCircleDistanceNm(
    double latitudeDegA,
    double longitudeDegA,
    double latitudeDegB,
    double longitudeDegB) {
    const auto latitudeRadA = ToRadians(latitudeDegA);
    const auto latitudeRadB = ToRadians(latitudeDegB);
    const auto deltaLatitude = ToRadians(latitudeDegB - latitudeDegA);
    const auto deltaLongitude =
        ToRadians(NormalizeLongitudeDeltaDeg(longitudeDegB - longitudeDegA));

    const auto sinLatitude = std::sin(deltaLatitude / 2.0);
    const auto sinLongitude = std::sin(deltaLongitude / 2.0);
    const auto a = sinLatitude * sinLatitude +
                   std::cos(latitudeRadA) * std::cos(latitudeRadB) *
                       sinLongitude * sinLongitude;
    const auto c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return kEarthRadiusNm * c;
}

ProjectedPoint ProjectRelativeTo(
    const RoutePoint& origin,
    const RoutePoint& point) {
    const auto referenceLatitudeRad = ToRadians(origin.latitudeDeg);
    const auto deltaLatitudeRad = ToRadians(point.latitudeDeg - origin.latitudeDeg);
    const auto deltaLongitudeRad =
        ToRadians(NormalizeLongitudeDeltaDeg(point.longitudeDeg - origin.longitudeDeg));

    return {
        deltaLongitudeRad * std::cos(referenceLatitudeRad) * kEarthRadiusNm,
        deltaLatitudeRad * kEarthRadiusNm,
    };
}

bool IsValidPosition(double latitudeDeg, double longitudeDeg) {
    return std::isfinite(latitudeDeg) && std::isfinite(longitudeDeg) &&
           std::fabs(latitudeDeg) <= 90.0 && std::fabs(longitudeDeg) <= 180.0;
}

std::vector<RouteSectorMatchSnapshot> BuildOrderedRoutePolygonSequence(
    const RouteSectorSnapshot& route) {
    std::vector<RouteSectorMatchSnapshot> sequence;
    std::unordered_set<std::string> seen;

    auto addSector = [&](RouteSectorMatchSnapshot sector) {
        if (sector.identifier.empty()) {
            return;
        }
        if (!seen.insert(sector.identifier).second) {
            return;
        }
        sector.entryDistanceNm = std::max(0.0, sector.entryDistanceNm);
        sequence.push_back(std::move(sector));
    };

    for (const auto& sector : route.currentSectors) {
        addSector(sector);
    }
    for (const auto& sector : route.nextSectors) {
        addSector(sector);
    }

    std::sort(
        sequence.begin(),
        sequence.end(),
        [](const auto& left, const auto& right) {
            if (left.entryDistanceNm != right.entryDistanceNm) {
                return left.entryDistanceNm < right.entryDistanceNm;
            }
            return left.identifier < right.identifier;
        });
    return sequence;
}

double ComputeRouteProgressNm(
    const AircraftStateSnapshot& aircraft,
    const std::vector<RouteWaypointSnapshot>& waypoints) {
    if (!aircraft.valid ||
        !IsValidPosition(aircraft.latitudeDeg, aircraft.longitudeDeg) ||
        waypoints.size() < 2) {
        return 0.0;
    }

    const RoutePoint aircraftPoint{aircraft.latitudeDeg, aircraft.longitudeDeg};
    double accumulatedDistanceNm = 0.0;
    double bestProgressNm = 0.0;
    double bestDistanceSquaredNm = std::numeric_limits<double>::max();
    double totalDistanceNm = 0.0;

    for (std::size_t index = 1; index < waypoints.size(); ++index) {
        const RoutePoint start{
            waypoints[index - 1].latitudeDeg,
            waypoints[index - 1].longitudeDeg,
        };
        const RoutePoint end{
            waypoints[index].latitudeDeg,
            waypoints[index].longitudeDeg,
        };
        if (!IsValidPosition(start.latitudeDeg, start.longitudeDeg) ||
            !IsValidPosition(end.latitudeDeg, end.longitudeDeg)) {
            continue;
        }

        const auto segmentDistanceNm = GreatCircleDistanceNm(
            start.latitudeDeg,
            start.longitudeDeg,
            end.latitudeDeg,
            end.longitudeDeg);
        if (segmentDistanceNm <= 0.01) {
            continue;
        }

        const auto projectedAircraft = ProjectRelativeTo(start, aircraftPoint);
        const auto projectedEnd = ProjectRelativeTo(start, end);
        const auto segmentLengthSquared =
            projectedEnd.xNm * projectedEnd.xNm +
            projectedEnd.yNm * projectedEnd.yNm;
        if (segmentLengthSquared <= 0.0001) {
            accumulatedDistanceNm += segmentDistanceNm;
            totalDistanceNm = accumulatedDistanceNm;
            continue;
        }

        const auto unclampedFraction =
            (projectedAircraft.xNm * projectedEnd.xNm +
             projectedAircraft.yNm * projectedEnd.yNm) /
            segmentLengthSquared;
        const auto fraction = std::clamp(unclampedFraction, 0.0, 1.0);
        const auto closestX = projectedEnd.xNm * fraction;
        const auto closestY = projectedEnd.yNm * fraction;
        const auto deltaX = projectedAircraft.xNm - closestX;
        const auto deltaY = projectedAircraft.yNm - closestY;
        const auto distanceSquaredNm = deltaX * deltaX + deltaY * deltaY;

        if (distanceSquaredNm < bestDistanceSquaredNm) {
            bestDistanceSquaredNm = distanceSquaredNm;
            bestProgressNm = accumulatedDistanceNm + segmentDistanceNm * fraction;
        }

        accumulatedDistanceNm += segmentDistanceNm;
        totalDistanceNm = accumulatedDistanceNm;
    }

    return std::clamp(bestProgressNm, 0.0, totalDistanceNm);
}

}  // namespace

RoutePolygonTransitionWorkerOutput RunRoutePolygonTransitionWorker(
    const RoutePolygonTransitionWorkerInput& input) {
    RoutePolygonTransitionWorkerOutput output;
    output.previousPolygonKey = input.previousPolygonKey;
    output.route = input.route;
    output.available = input.route.available;
    output.stale = input.route.stale;
    output.routeResolved = input.route.routeResolved;

    if (!input.route.available || input.route.stale || !input.route.routeResolved) {
        output.reason = "route-polygon-transition-route-unavailable";
        return output;
    }

    const auto sequence = BuildOrderedRoutePolygonSequence(input.route);
    if (sequence.empty()) {
        output.reason = "route-polygon-transition-no-sequence";
        return output;
    }

    output.progressDistanceNm =
        ComputeRouteProgressNm(input.aircraft, input.route.waypoints);
    const auto transitionToleranceNm =
        std::max(0.0, input.transitionToleranceNm);

    std::size_t selectedIndex = 0;
    for (std::size_t index = 0; index < sequence.size(); ++index) {
        if (sequence[index].entryDistanceNm <=
            output.progressDistanceNm + transitionToleranceNm) {
            selectedIndex = index;
        } else {
            break;
        }
    }

    const auto selectedEntryDistanceNm = sequence[selectedIndex].entryDistanceNm;
    std::vector<RouteSectorMatchSnapshot> currentSectors;
    std::vector<RouteSectorMatchSnapshot> nextSectors;
    for (const auto& sector : sequence) {
        if (std::fabs(sector.entryDistanceNm - selectedEntryDistanceNm) <=
            kSameEntryDistanceToleranceNm) {
            currentSectors.push_back(sector);
            continue;
        }
        if (sector.entryDistanceNm > selectedEntryDistanceNm +
            kSameEntryDistanceToleranceNm) {
            nextSectors.push_back(sector);
        }
    }

    if (currentSectors.empty()) {
        currentSectors.push_back(sequence[selectedIndex]);
    }

    output.currentPolygonIndex = static_cast<int>(selectedIndex) + 1;
    output.currentPolygonKey = currentSectors.front().identifier;
    output.nextPolygonKey =
        nextSectors.empty() ? std::string{} : nextSectors.front().identifier;
    output.finalRoutePolygonKey = sequence.back().identifier;
    output.enteredFinalRoutePolygon =
        !output.currentPolygonKey.empty() &&
        output.currentPolygonKey == output.finalRoutePolygonKey;
    output.changed =
        !input.previousPolygonKey.empty() &&
        output.currentPolygonKey != input.previousPolygonKey;
    output.shouldWakeUi = output.changed && !output.enteredFinalRoutePolygon;

    output.route.currentSectors = std::move(currentSectors);
    output.route.nextSectors = std::move(nextSectors);

    if (output.changed) {
        output.reason = output.enteredFinalRoutePolygon
                            ? "route-polygon-transition-final"
                            : "route-polygon-transition";
    } else {
        output.reason = "route-polygon-position-unchanged";
    }
    return output;
}

}  // namespace xvatsim::brain
