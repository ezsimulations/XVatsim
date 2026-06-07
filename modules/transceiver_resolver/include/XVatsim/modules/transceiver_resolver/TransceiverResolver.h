#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::modules::transceiver_resolver {

struct CachedTransceiver {
    std::string callsign;
    std::string frequency;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    double heightAglFt = 0.0;
};

class TransceiverResolver {
public:
    TransceiverResolver() = default;
    ~TransceiverResolver();

    brain::TransceiverResolutionSnapshot Resolve(
        const brain::AircraftStateSnapshot& aircraftState,
        const brain::ControllerFeedSnapshot& controllerFeedSnapshot);
    brain::TransceiverResolutionSnapshot ResolveAuthorityStations(
        const brain::ControllerFeedSnapshot& controllerFeedSnapshot);
    brain::TransceiverResolutionSnapshot ResolveAirportCoverage(
        const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
        bool hasAirportCoordinates,
        double airportLatitudeDeg,
        double airportLongitudeDeg);
    void Reset();
    void SeedFeedCacheForTesting(
        std::vector<CachedTransceiver> transceivers,
        long long successfulFetchAgeSeconds,
        bool lastFetchSucceeded);

private:
    bool RefreshFeedIfNeeded();
    bool StartAsyncFetch(long long nowSeconds);
    void HarvestPendingFetch();
    bool IsFeedCacheFresh(long long nowSeconds) const;
    bool IsFeedCacheUsableAsHoldover(long long nowSeconds) const;
    long long FeedCacheAgeSeconds(long long nowSeconds) const;

    std::vector<CachedTransceiver> cachedTransceivers_{};
    std::unordered_map<std::string, std::vector<CachedTransceiver>> indexedTransceivers_{};
    std::vector<CachedTransceiver> pendingTransceivers_{};
    bool hasFeedCache_ = false;
    bool hasPendingFeed_ = false;
    bool lastFetchSucceeded_ = false;
    long long lastFetchTickSeconds_ = 0;
    long long lastSuccessfulFetchTickSeconds_ = 0;
    bool hasResolveCache_ = false;
    brain::TransceiverResolutionSnapshot cachedSnapshot_{};
    bool lastResolveUsedHoldover_ = false;
    long long lastResolveTickSeconds_ = 0;
    double lastResolveLatitudeDeg_ = 0.0;
    double lastResolveLongitudeDeg_ = 0.0;
    double lastResolveAltitudeAglFt_ = 0.0;
    std::size_t lastControllerFeedHash_ = 0;
    bool hasAuthorityStationCache_ = false;
    brain::TransceiverResolutionSnapshot cachedAuthorityStationSnapshot_{};
    long long lastAuthorityStationResolveTickSeconds_ = 0;
    std::size_t lastAuthorityStationControllerFeedHash_ = 0;
    bool hasAirportCoverageCache_ = false;
    brain::TransceiverResolutionSnapshot cachedAirportCoverageSnapshot_{};
    long long lastAirportCoverageResolveTickSeconds_ = 0;
    double lastAirportCoverageLatitudeDeg_ = 0.0;
    double lastAirportCoverageLongitudeDeg_ = 0.0;
    std::size_t lastAirportCoverageControllerFeedHash_ = 0;
    std::atomic<bool> fetchInProgress_{false};
    std::mutex fetchMutex_{};
    std::thread fetchThread_{};
};

}  // namespace xvatsim::modules::transceiver_resolver
