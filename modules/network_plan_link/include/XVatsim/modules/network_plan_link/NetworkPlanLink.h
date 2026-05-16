#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "XVatsim/brain/BrainTypes.h"
#include "XVatsim/modules/vatsim_data_feed/VatsimDataFeedClient.h"

namespace xvatsim::modules::network_plan_link {

class NetworkPlanLink {
public:
    NetworkPlanLink() = default;

    brain::NetworkPlanSnapshot Poll(
        const brain::PilotIdentitySnapshot& pilotIdentitySnapshot,
        const xvatsim::modules::vatsim_data_feed::VatsimDataFeedSnapshot& feedSnapshot) const;
    void Reset();

private:
    struct AirportCoordinateCacheEntry {
        bool resolved = false;
        double latitudeDeg = 0.0;
        double longitudeDeg = 0.0;
    };

    brain::NetworkPlanSnapshot MatchPilotPlan(
        const brain::PilotIdentitySnapshot& pilotIdentitySnapshot,
        const std::vector<xvatsim::modules::vatsim_data_feed::PilotPlanEntry>& cachedPilotPlans) const;
    bool ResolveAirportCoordinatesCached(
        const std::string& airportIcao,
        double* outLatitudeDeg,
        double* outLongitudeDeg) const;

    mutable bool hasPlanCache_ = false;
    mutable bool cachedPlanFromFreshFeed_ = false;
    mutable std::uint64_t lastFeedGeneration_ = 0;
    mutable std::string lastNormalizedCallsign_{};
    mutable brain::NetworkPlanSnapshot cachedPlanSnapshot_{};
    mutable std::unordered_map<std::string, AirportCoordinateCacheEntry> airportCoordinateCache_{};
};

}  // namespace xvatsim::modules::network_plan_link
