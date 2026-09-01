#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "XVatsim/brain/BrainTypes.h"
#include "XVatsim/core/PreflightRouteCache.h"

namespace xvatsim::modules::route_sector {

struct AuthoritySourceDataset;
class AuthorityRelevanceEngine;
struct AuthoritySnapshotLease;

struct AuthorityDatasetPublication {
    const AuthoritySourceDataset* dataset = nullptr;
    std::uint64_t identity = 0;
    std::uint64_t terminalBoundaryGeneration = 0;
};

struct AuthorityWorkerIdentity {
    std::uint64_t requestId = 0;
    std::uint64_t lifecycleEpoch = 0;
    std::string planKey;
    std::uint64_t routeDigest = 0;
    std::uint64_t controllerDigest = 0;
    std::uint64_t transceiverDigest = 0;
    std::uint64_t datasetIdentity = 0;
};

struct AuthorityWorkerRequest {
    AuthorityWorkerIdentity identity;
    brain::AircraftStateSnapshot aircraft;
    std::shared_ptr<const brain::RouteSectorSnapshot> route;
    std::shared_ptr<const std::vector<brain::AuthorityControllerSnapshot>>
        controllers;
    bool hasControllerEvidence = false;
    bool controllerFeedAvailable = false;
    bool controllerFeedStale = true;
    std::uint64_t controllerFeedGeneration = 0;
    int controllerFeedConnectedControllers = 0;
    std::uint64_t terminalBoundaryGeneration = 0;
    std::string scheduleReason;
    bool hasTransceiverEvidence = true;
    std::shared_ptr<const brain::AuthorityTransceiverEvidenceSnapshot>
        transceivers;
    // The resolver retains every published immutable dataset until teardown,
    // which is ordered after the authority worker. This raw handle keeps the
    // flight-loop read and mailbox package lock-free.
    const AuthoritySourceDataset* dataset = nullptr;
    long long dispatchedMonotonicMs = 0;
    // Deterministic proof seam. Production requests leave this at zero.
    std::uint32_t cooperativeDelayForTestingMs = 0;
    // Intrusive mailbox metadata. Owned exclusively by AuthorityRelevanceWorker.
    std::uint64_t mailboxSequence = 0;
    AuthorityWorkerRequest* retirementNext = nullptr;
};

// The worker retains every lease until the main thread marks it retired.
// Consequently, releasing accepted or rejected evidence on the main thread
// cannot run the large snapshot destructor there.
struct AuthoritySnapshotLease {
    std::shared_ptr<const brain::AuthorityRelevanceSnapshot> snapshot;
    std::atomic<bool> retired{false};
};

struct AuthorityWorkerFact {
    AuthorityWorkerIdentity identity;
    brain::AircraftStateSnapshot dispatchedAircraft;
    std::shared_ptr<AuthoritySnapshotLease> snapshotLease;
    bool completed = false;
    bool cancelled = false;
    std::uint64_t snapshotDigest = 0;
    long long workerElapsedUs = 0;
    long long completedMonotonicMs = 0;
    std::string reason;
    AuthorityWorkerFact* retirementNext = nullptr;
};

struct AuthorityWorkerSnapshot {
    bool running = false;
    bool pending = false;
    bool completed = false;
    std::uint64_t starts = 0;
    std::uint64_t replacements = 0;
    std::uint64_t completions = 0;
    std::uint64_t cancellations = 0;
    std::uint64_t maximumPendingDepth = 0;
    std::uint64_t workerThreadSnapshotRetirements = 0;
};

class AuthorityRelevanceWorker {
public:
    AuthorityRelevanceWorker();
    ~AuthorityRelevanceWorker();
    AuthorityRelevanceWorker(const AuthorityRelevanceWorker&) = delete;
    AuthorityRelevanceWorker& operator=(const AuthorityRelevanceWorker&) = delete;

    // Lifecycle-only initialization. Call before registering the flight loop;
    // request submission never creates or joins a thread.
    bool Start();
    bool StartLatest(AuthorityWorkerRequest request);
    bool TryHarvest(AuthorityWorkerFact* fact);
    bool IsRunning() const;
    // Wakes the worker after the caller atomically marks a snapshot lease
    // retired. This operation never waits.
    void NotifyRetirement();
    void CancelPending();
    void CancelAndJoin();
    AuthorityWorkerSnapshot Snapshot() const;

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

class RouteSectorResolver {
public:
    RouteSectorResolver();
    ~RouteSectorResolver();

    brain::RouteSectorSnapshot Resolve(
        const brain::AircraftStateSnapshot& aircraftState,
        const brain::NetworkPlanSnapshot& networkPlanSnapshot) const;
    brain::AuthorityRelevanceSnapshot ResolveBrainScheduledAuthorityVerification(
        const brain::AircraftStateSnapshot& aircraftState,
        const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
        const brain::RouteSectorSnapshot& routeSectorSnapshot,
        const std::string& scheduleReason,
        const brain::TransceiverResolutionSnapshot* authorityTransceiverSnapshot = nullptr) const;
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
    void LoadBoundaryPayloadsForTesting(
        const std::string& boundaryGeoJson,
        const std::string& terminalGeoJson,
        const std::string& authorityCatalogDat,
        const std::string& ownershipJson) const;
    void QueueBoundaryPayloadsForTesting(
        const std::string& boundaryGeoJson,
        const std::string& terminalGeoJson,
        const std::string& authorityCatalogDat) const;
    void QueueBoundaryPayloadsForTesting(
        const std::string& boundaryGeoJson,
        const std::string& terminalGeoJson,
        const std::string& authorityCatalogDat,
        const std::string& ownershipJson) const;
    void SetPreflightRouteCache(
        const core::preflight::PreflightRouteCache& cache,
        const std::string& validationReason = {});
    void ClearPreflightRouteCache();
    // Clears per-flight route/airport results while keeping downloaded source payloads.
    void ResetRuntimeState();
    // Clears downloaded sector/source payloads; use only for true data-source replacement.
    void ResetSourceCaches();
    void Reset();
    void AgeAuthorityRelevanceCacheForTesting(long long ageSeconds) const;
    const AuthoritySourceDataset* GetAuthoritySourceDataset() const;
    AuthorityDatasetPublication GetAuthoritySourceDatasetPublication() const;
    std::uint64_t GetAuthoritySourceDatasetIdentity() const;
    std::uint64_t GetTerminalBoundaryGeneration() const;

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
        std::vector<unsigned char> authorityCatalogPayload,
        std::vector<unsigned char> ownershipPayload) const;
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
    mutable bool hasAuthorityRelevanceCache_ = false;
    mutable long long lastAuthorityRelevanceBuildTickSeconds_ = 0;
    mutable double lastAuthorityRelevanceLatitudeDeg_ = 0.0;
    mutable double lastAuthorityRelevanceLongitudeDeg_ = 0.0;
    mutable std::size_t lastAuthorityRelevanceSignature_ = 0;
    mutable std::size_t lastAuthorityOperationalScopeSignature_ = 0;
    mutable std::size_t lastAuthorityWatchInputSignature_ = 0;
    mutable std::size_t lastAuthorityRelevanceProgressRouteSignature_ = 0;
    mutable double lastAuthorityRelevanceProgressWindowNm_ = 0.0;
    mutable brain::AuthorityRelevanceSnapshot cachedAuthorityRelevanceSnapshot_{};
    mutable std::size_t authorityProgressRouteSignature_ = 0;
    mutable double authorityProgressWindowNm_ = 0.0;
    mutable long long lastAuthorityProgressTickSeconds_ = 0;
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
    mutable std::vector<unsigned char> ownershipPayload_;
    mutable std::vector<unsigned char> pendingOwnershipPayload_;
    mutable bool hasPendingPayload_ = false;
    mutable bool hasTerminalBoundaryCache_ = false;
    mutable std::uint64_t centerBoundaryGeneration_ = 0;
    mutable std::uint64_t authorityCatalogGeneration_ = 0;
    mutable std::uint64_t terminalBoundaryGeneration_ = 0;
    mutable std::atomic<const AuthoritySourceDataset*>
        publishedAuthorityDataset_{nullptr};
    mutable std::atomic<std::uint64_t>
        publishedTerminalBoundaryGeneration_{0};
    mutable std::optional<core::preflight::PreflightRouteCache> preflightRouteCache_;
    mutable std::string preflightRouteCacheReason_;
    mutable std::shared_ptr<AuthoritySourceDataset> authorityDataset_;
    mutable std::shared_ptr<AuthoritySourceDataset> pendingAuthorityDataset_;
    // Published raw dataset handles remain valid through worker teardown.
    mutable std::vector<std::shared_ptr<const AuthoritySourceDataset>>
        authorityDatasetRetention_;
    mutable std::unique_ptr<AuthorityRelevanceEngine> authorityEngine_;
};

}  // namespace xvatsim::modules::route_sector
