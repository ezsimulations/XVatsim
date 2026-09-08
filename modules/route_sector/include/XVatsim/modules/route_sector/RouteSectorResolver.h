#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "XVatsim/brain/BrainTypes.h"
#include "XVatsim/core/PreflightRouteCache.h"

namespace xvatsim::modules::route_sector {

struct AuthoritySourceDataset;
class AuthorityRelevanceEngine;
struct AuthoritySnapshotLease;
struct RouteSourceDataset;
struct RouteDatasetPublicationState;
class RoutePreparationEngine;
struct RouteSnapshotLease;

class RoutePreparationSource {
public:
    virtual ~RoutePreparationSource() = default;
    virtual std::string ReadTextFile(
        const std::filesystem::path& path) const = 0;
    virtual std::vector<std::filesystem::path> ListRegularFiles(
        const std::filesystem::path& directory) const = 0;
    virtual core::preflight::FmsParseResult LoadFmsPlan(
        const std::filesystem::path& path) const = 0;
    virtual core::preflight::CacheValidationResult ValidatePreflightCandidate(
        const core::preflight::PreflightRouteCache& candidate,
        const brain::NetworkPlanSnapshot& networkPlan) const = 0;
};

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

struct RouteDatasetPublication {
    std::shared_ptr<const RouteSourceDataset> dataset;
    std::uint64_t identity = 0;
    std::uint64_t contentIdentity = 0;
    std::uint64_t publicationGeneration = 0;
    std::uint64_t centerBoundaryGeneration = 0;
    std::uint64_t authorityCatalogGeneration = 0;
    bool available = false;
    bool stale = true;
};

struct RouteWorkerIdentity {
    std::uint64_t requestId = 0;
    std::uint64_t lifecycleEpoch = 0;
    std::string planKey;
    std::uint64_t networkPlanDigest = 0;
    std::uint64_t routeSourceDatasetIdentity = 0;
    std::uint64_t preflightCandidateIdentity = 0;
    std::uint64_t routePolicyIdentity = 0;
    std::uint64_t routeAnchorDigest = 0;
    std::uint64_t expandedFmsObservationIdentity = 0;
};

struct RouteAnchorSelectionState {
    bool captured = false;
    std::uint64_t networkPlanDigest = 0;
    brain::AircraftStateSnapshot anchor;
};

brain::AircraftStateSnapshot SelectStickyRouteAnchor(
    RouteAnchorSelectionState* state,
    const brain::AircraftStateSnapshot& currentAircraft,
    const brain::NetworkPlanSnapshot& networkPlan,
    std::uint64_t networkPlanDigest);

enum class RouteCoordinatorAction {
    None,
    ObserveExpandedFms,
    PrepareRoute,
};

struct RouteCoordinatorDecisionInput {
    bool sourceUsable = false;
    bool fmsObservationReady = false;
    bool acceptedCurrent = false;
    bool brainNeedsWorker = false;
    bool hasEligibleRequest = false;
    bool workerBusy = false;
    bool retryDue = false;
    bool fmsObservationDue = false;
};

struct RouteCoordinatorDecision {
    RouteCoordinatorAction action = RouteCoordinatorAction::None;
    bool failClosed = false;
    const char* reason = "route-coordinator-idle";
};

RouteCoordinatorDecision DecideRouteCoordinatorAction(
    const RouteCoordinatorDecisionInput& input);

struct RouteWorkerPhaseTimings {
    long long sourceAcquisitionUs = 0;
    long long navigationInitializationUs = 0;
    long long preflightValidationUs = 0;
    long long fmsDiscoveryUs = 0;
    long long procedureMetadataUs = 0;
    long long grammarParsingUs = 0;
    long long waypointResolutionUs = 0;
    long long traversalPreparationUs = 0;
    long long polygonTraversalUs = 0;
    long long controllerPrefixUs = 0;
    long long outputFinalizationUs = 0;
    long long totalUs = 0;
};

// A request is current only while its immutable mailbox sequence remains the
// newest published sequence. Unlike a shared bool, a new request cannot be
// accidentally "uncancelled" by the worker while it starts.
class RouteCancellationToken {
public:
    RouteCancellationToken() = default;
    RouteCancellationToken(
        const std::atomic<std::uint64_t>* latestMailboxSequence,
        std::uint64_t requestMailboxSequence,
        const std::atomic<bool>* stopping)
        : latestMailboxSequence_(latestMailboxSequence),
          requestMailboxSequence_(requestMailboxSequence),
          stopping_(stopping) {}

    bool IsCancellationRequested() const {
        return (stopping_ != nullptr &&
                stopping_->load(std::memory_order_acquire)) ||
            latestMailboxSequence_ == nullptr ||
            latestMailboxSequence_->load(std::memory_order_acquire) !=
                requestMailboxSequence_;
    }

private:
    const std::atomic<std::uint64_t>* latestMailboxSequence_ = nullptr;
    std::uint64_t requestMailboxSequence_ = 0;
    const std::atomic<bool>* stopping_ = nullptr;
};

struct RoutePreparationRequest {
    enum class Kind {
        PrepareRoute,
        ObserveExpandedFms,
    };

    Kind kind = Kind::PrepareRoute;
    RouteWorkerIdentity identity;
    brain::AircraftStateSnapshot routeAnchor;
    std::shared_ptr<const brain::NetworkPlanSnapshot> networkPlan;
    std::shared_ptr<const RouteSourceDataset> routeSourceDataset;
    std::shared_ptr<const core::preflight::PreflightRouteCache>
        preflightCandidate;
    std::string preflightValidationReason;
    std::uint32_t cooperativeDelayForTestingMs = 0;
    // Deterministic seam for the former worker-start cancellation race.
    // Production requests leave this at zero.
    std::uint32_t workerStartRaceDelayForTestingMs = 0;
    bool forceFailureForTesting = false;
    std::uint64_t mailboxSequence = 0;
    RoutePreparationRequest* retirementNext = nullptr;
};

struct RoutePreparationResult {
    std::shared_ptr<const brain::RouteSectorSnapshot> route;
    std::uint64_t comprehensiveDigest = 0;
    RouteWorkerPhaseTimings timings;
    std::string reason;
};

struct ExpandedFmsObservationResult {
    std::uint64_t identity = 0;
    bool hasMatchingPlan = false;
    bool cancelled = false;
    std::string reason;
};

class RoutePreparationEngine {
public:
    explicit RoutePreparationEngine(
        std::string xplaneRootPath = {},
        std::shared_ptr<const RoutePreparationSource> source = {},
        bool verifyPreflightSourceFile = true);
    ~RoutePreparationEngine();
    RoutePreparationEngine(const RoutePreparationEngine&) = delete;
    RoutePreparationEngine& operator=(const RoutePreparationEngine&) = delete;

    RoutePreparationResult Prepare(
        const RoutePreparationRequest& request,
        const RouteCancellationToken* cancellation = nullptr);
    ExpandedFmsObservationResult ObserveExpandedFms(
        const brain::NetworkPlanSnapshot& networkPlan,
        const RouteCancellationToken* cancellation = nullptr);
    void Reset();

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

struct RouteSnapshotLease {
    std::shared_ptr<const brain::RouteSectorSnapshot> route;
    std::atomic<bool> retired{false};
    std::uint64_t retainedBytes = 0;
};

struct RouteWorkerFact {
    RoutePreparationRequest::Kind kind =
        RoutePreparationRequest::Kind::PrepareRoute;
    RouteWorkerIdentity identity;
    std::shared_ptr<RouteSnapshotLease> routeLease;
    bool completed = false;
    bool cancelled = false;
    bool failed = false;
    std::uint64_t routeDigest = 0;
    std::uint64_t expandedFmsObservationIdentity = 0;
    bool expandedFmsMatch = false;
    std::uint64_t expandedFmsObservationWorkerWallUs = 0;
    std::uint64_t expandedFmsObservationWorkerCpuUs = 0;
    bool expandedFmsObservationWorkerCpuAvailable = false;
    RouteWorkerPhaseTimings timings;
    long long completedMonotonicMs = 0;
    std::string reason;
    RouteWorkerFact* retirementNext = nullptr;
};

struct RouteWorkerSnapshot {
    bool started = false;
    bool running = false;
    bool pending = false;
    bool completed = false;
    std::uint64_t starts = 0;
    std::uint64_t replacements = 0;
    std::uint64_t completions = 0;
    std::uint64_t cancellations = 0;
    std::uint64_t failures = 0;
    std::uint64_t staleRetirements = 0;
    std::uint64_t maximumPendingDepth = 0;
    std::uint64_t requestNodesInUse = 0;
    std::uint64_t requestNodesPeak = 0;
    std::uint64_t leasesCreated = 0;
    std::uint64_t leasesRetired = 0;
    std::uint64_t leasesDestroyedOnWorker = 0;
    std::uint64_t outstandingLeases = 0;
    std::uint64_t peakRetainedRouteItems = 0;
    std::uint64_t retainedRouteBytes = 0;
    std::uint64_t peakRetainedRouteBytes = 0;
    std::uint64_t sourceDatasetObservations = 0;
    std::uint64_t sourceDatasetObservationRejections = 0;
    std::uint64_t retainedSourceDatasets = 0;
    std::uint64_t peakRetainedSourceDatasets = 0;
    std::uint64_t fmsObservations = 0;
    std::uint64_t fmsObservationChanges = 0;
};

class RoutePreparationWorker {
public:
    RoutePreparationWorker();
    ~RoutePreparationWorker();
    RoutePreparationWorker(const RoutePreparationWorker&) = delete;
    RoutePreparationWorker& operator=(const RoutePreparationWorker&) = delete;

    bool Start(
        std::string xplaneRootPath,
        std::shared_ptr<const RoutePreparationSource> source = {},
        bool verifyPreflightSourceFile = true);
    bool StartLatest(RoutePreparationRequest request);
    bool TryHarvest(RouteWorkerFact* fact);
    bool IsRunning() const;
    void NotifyRetirement();
    bool RetireSharedRoute(
        std::shared_ptr<const brain::RouteSectorSnapshot>* route);
    bool ObserveSourceDataset(
        std::shared_ptr<const RouteSourceDataset> dataset);
    void CancelPending();
    void CancelAndJoin();
    RouteWorkerSnapshot Snapshot() const;

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

std::uint64_t HashRouteNetworkPlan(
    const brain::NetworkPlanSnapshot& networkPlan);
std::uint64_t HashRoutePreflightCandidate(
    const core::preflight::PreflightRouteCache* candidate,
    std::string_view validationReason = {});
std::uint64_t HashRouteAnchor(
    const brain::AircraftStateSnapshot& aircraft,
    const brain::NetworkPlanSnapshot& networkPlan);
std::uint64_t RoutePreparationPolicyIdentity();
bool SameRouteWorkerSemanticIdentity(
    const RouteWorkerIdentity& left,
    const RouteWorkerIdentity& right);

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
    // Frozen synchronous implementation retained only for Gate B oracle
    // parity fixtures. Production plugin code must not call this entry point.
    brain::RouteSectorSnapshot ResolveSynchronousRouteOracleForTesting(
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
    RouteDatasetPublication GetRouteSourceDatasetPublication() const;
    // Lifecycle/main-thread service points for immutable source publication.
    // Startup may create the fetch thread before flight-loop registration;
    // the harvest call returns immediately while that thread is active.
    void StartSourceRefreshBeforeFlightLoop() const;
    void StopSourceRefreshAfterFlightLoop() const;
    bool TryHarvestSourcePublications() const;
    std::uint64_t GetAuthoritySourceDatasetIdentity() const;
    std::uint64_t GetTerminalBoundaryGeneration() const;

private:
    brain::RouteSectorSnapshot BuildSnapshot(
        const brain::AircraftStateSnapshot& aircraftState,
        const brain::NetworkPlanSnapshot& networkPlanSnapshot) const;
    brain::RouteSectorSnapshot BuildSnapshotFrozenOracle(
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
    mutable std::atomic<long long> lastFetchTickSeconds_{0};
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
    mutable std::atomic<bool> pendingPublicationReady_{false};
    mutable std::atomic<bool> sourceRefreshStarted_{false};
    mutable std::atomic<bool> sourceRefreshStop_{false};
    mutable std::mutex fetchMutex_{};
    mutable std::mutex sourceRefreshWaitMutex_{};
    mutable std::condition_variable sourceRefreshWait_{};
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
    mutable std::shared_ptr<RouteSourceDataset> routeDataset_;
    mutable std::shared_ptr<RouteSourceDataset> pendingRouteDataset_;
    mutable std::uint64_t routePublicationGeneration_ = 0;
    // Published raw dataset handles remain valid through worker teardown.
    mutable std::vector<std::shared_ptr<const AuthoritySourceDataset>>
        authorityDatasetRetention_;
    mutable std::shared_ptr<const RouteDatasetPublicationState>
        publishedRoutePublication_;
    mutable std::unique_ptr<AuthorityRelevanceEngine> authorityEngine_;
    mutable std::unique_ptr<RoutePreparationEngine> synchronousRouteOracleEngine_;
};

}  // namespace xvatsim::modules::route_sector
