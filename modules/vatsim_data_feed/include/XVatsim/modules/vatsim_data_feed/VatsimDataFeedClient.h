#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::modules::vatsim_data_feed {

struct PilotPlanEntry {
    int cid = 0;
    std::string callsign;
    std::string normalizedCallsign;
    std::string departureIcao;
    std::string destinationIcao;
    double filedCruiseAltitudeFt = 0.0;
    bool hasFiledCruiseAltitude = false;
    std::string routeText;
};

struct VatsimDataFeedSnapshot {
    bool hasCache = false;
    bool fetchInProgress = false;
    bool stale = true;
    std::uint64_t generation = 0;
    int connectedControllers = 0;
    std::vector<xvatsim::brain::ControllerSnapshot> controllers;
    std::vector<PilotPlanEntry> pilotPlans;
};

class VatsimDataFeedClient {
public:
    VatsimDataFeedClient() = default;
    ~VatsimDataFeedClient();

    const VatsimDataFeedSnapshot& Poll();
    void Reset();

private:
    VatsimDataFeedSnapshot FetchSnapshot() const;
    bool StartAsyncFetch(long long nowSeconds);
    void HarvestPendingFetch();
    bool IsCachedSnapshotFresh(long long nowSeconds) const;
    bool RefreshFeedIfNeeded();

    VatsimDataFeedSnapshot cachedSnapshot_{};
    VatsimDataFeedSnapshot emptySnapshot_{};
    VatsimDataFeedSnapshot pendingSnapshot_{};
    bool hasPendingSnapshot_ = false;
    bool lastFetchSucceeded_ = false;
    long long lastFetchTickSeconds_ = 0;
    long long lastSuccessfulFetchTickSeconds_ = 0;
    std::uint64_t lastGeneration_ = 0;
    std::atomic<bool> fetchInProgress_{false};
    std::mutex fetchMutex_{};
    std::thread fetchThread_{};
};

}  // namespace xvatsim::modules::vatsim_data_feed
