#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::modules::route_sector {

class RouteSectorResolver {
public:
    RouteSectorResolver() = default;
    ~RouteSectorResolver();

    brain::RouteSectorSnapshot Resolve(
        const brain::AircraftStateSnapshot& aircraftState,
        const brain::NetworkPlanSnapshot& networkPlanSnapshot) const;
    brain::AirportSectorSnapshot ResolveAirportCoverage(
        const std::string& airportIcao,
        bool hasAirportCoordinates,
        double airportLatitudeDeg,
        double airportLongitudeDeg) const;
    bool CanEvaluateAirportTerminalCoverage(
        const brain::AirportSectorSnapshot& airportCoverageSnapshot) const;
    bool IsInsideAirportTerminalCoverage(
        const brain::AirportSectorSnapshot& airportCoverageSnapshot,
        double aircraftLatitudeDeg,
        double aircraftLongitudeDeg) const;
    void LoadBoundaryPayloadsForTesting(
        const std::string& boundaryGeoJson,
        const std::string& terminalGeoJson,
        const std::string& authorityCatalogDat) const;
    void QueueBoundaryPayloadsForTesting(
        const std::string& boundaryGeoJson,
        const std::string& terminalGeoJson,
        const std::string& authorityCatalogDat) const;
    // Clears per-flight route/airport results while keeping downloaded source payloads.
    void ResetRuntimeState();
    // Clears downloaded sector/source payloads; use only for true data-source replacement.
    void ResetSourceCaches();
    void Reset();

private:
    brain::RouteSectorSnapshot BuildSnapshot(
        const brain::AircraftStateSnapshot& aircraftState,
        const brain::NetworkPlanSnapshot& networkPlanSnapshot) const;
    brain::AirportSectorSnapshot BuildAirportCoverageSnapshot(
        const std::string& airportIcao,
        double airportLatitudeDeg,
        double airportLongitudeDeg) const;
    void StartAsyncBoundaryFetch(long long nowSeconds) const;
    void StageFetchedPayloads(
        std::vector<unsigned char> boundaryPayload,
        std::vector<unsigned char> terminalBoundaryPayload,
        std::vector<unsigned char> authorityCatalogPayload) const;
    void HarvestPendingFetch() const;
    bool IsCenterAuthorityCacheFresh(long long nowSeconds) const;
    bool IsTerminalBoundaryCacheFresh(long long nowSeconds) const;
    bool RefreshBoundariesIfNeeded() const;

    mutable bool hasBoundaryCache_ = false;
    mutable bool hasAuthorityCatalogCache_ = false;
    mutable bool lastFetchSucceeded_ = false;
    mutable long long lastFetchTickSeconds_ = 0;
    mutable long long lastSuccessfulCenterFetchTickSeconds_ = 0;
    mutable long long lastSuccessfulTerminalFetchTickSeconds_ = 0;
    mutable bool hasSnapshotCache_ = false;
    mutable long long lastSnapshotBuildTickSeconds_ = 0;
    mutable double lastSnapshotLatitudeDeg_ = 0.0;
    mutable double lastSnapshotLongitudeDeg_ = 0.0;
    mutable std::string lastSnapshotRouteKey_;
    mutable brain::RouteSectorSnapshot cachedSnapshot_{};
    mutable std::unordered_map<std::string, brain::AirportSectorSnapshot> airportCoverageCache_{};
    mutable std::atomic<bool> fetchInProgress_{false};
    mutable std::mutex fetchMutex_{};
    mutable std::thread fetchThread_{};
    mutable std::vector<unsigned char> boundaryPayload_;
    mutable std::vector<unsigned char> pendingBoundaryPayload_;
    mutable std::vector<unsigned char> terminalBoundaryPayload_;
    mutable std::vector<unsigned char> pendingTerminalBoundaryPayload_;
    mutable std::vector<unsigned char> vatspyPayload_;
    mutable std::vector<unsigned char> pendingVatspyPayload_;
    mutable bool hasPendingPayload_ = false;
    mutable bool hasTerminalBoundaryCache_ = false;
    mutable std::uint64_t centerBoundaryGeneration_ = 0;
    mutable std::uint64_t authorityCatalogGeneration_ = 0;
    mutable std::uint64_t terminalBoundaryGeneration_ = 0;
};

}  // namespace xvatsim::modules::route_sector
