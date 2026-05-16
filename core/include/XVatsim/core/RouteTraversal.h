#pragma once

#include <string>
#include <vector>

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::core::route {

struct GeoPoint {
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
};

struct SectorPolygon {
    std::vector<GeoPoint> ring;
};

struct SectorFeature {
    std::string label;
    std::vector<std::string> tokens;
    std::vector<std::string> controllerCallsignPatterns;
    std::vector<std::string> controllerPrefixes;
    std::vector<SectorPolygon> polygons;
};

enum class TraversalMode {
    Exact,
    Sampled,
};

struct TraversalTuning {
    TraversalMode mode = TraversalMode::Exact;
    double routeSampleStepNm = 25.0;
    std::size_t routeSectorSanityLimit = 30;
};

brain::RouteSectorSnapshot BuildRouteSectorSnapshotFromWaypoints(
    const std::vector<brain::RouteWaypointSnapshot>& waypoints,
    const std::vector<SectorFeature>& features,
    const TraversalTuning& tuning = {});

}  // namespace xvatsim::core::route
