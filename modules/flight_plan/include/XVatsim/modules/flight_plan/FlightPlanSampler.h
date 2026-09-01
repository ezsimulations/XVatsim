#pragma once

#include <optional>
#include <string>

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::modules::flight_plan {

namespace detail {

constexpr int kMaximumFmsEntries = 512;

struct FmsAirportEndpoint {
    int index = -1;
    std::string airportIcao;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
};

struct FmsAirportEndpoints {
    std::optional<FmsAirportEndpoint> departure;
    std::optional<FmsAirportEndpoint> destination;
    int entriesRead = 0;
};

using FmsAirportEndpointReader =
    bool (*)(int index, FmsAirportEndpoint* endpoint, void* context);

// Reads each visited entry at most once. The first valid airport is the
// departure and the last valid airport is the destination, matching the
// historical forward scan without traversing interior entries unnecessarily.
FmsAirportEndpoints FindFmsAirportEndpointsBidirectionally(
    int entryCount,
    FmsAirportEndpointReader reader,
    void* context);

}  // namespace detail

class FlightPlanSampler {
public:
    FlightPlanSampler() = default;

    brain::FlightPlanSnapshot Sample(const brain::AircraftStateSnapshot& aircraftState) const;
    void Reset();

private:
    mutable bool hasSampleCache_ = false;
    mutable brain::FlightPlanSnapshot cachedSnapshot_{};
    mutable long long lastSampleTickSeconds_ = 0;
    mutable double lastSampleLatitudeDeg_ = 0.0;
    mutable double lastSampleLongitudeDeg_ = 0.0;
};

}  // namespace xvatsim::modules::flight_plan
