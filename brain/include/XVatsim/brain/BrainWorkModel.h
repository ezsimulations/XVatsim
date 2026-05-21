#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::brain {

enum class BrainWorkType {
    BuildRouteScopedMap,
    ResolveDepartureAirportLocal,
    ResolveDepartureTerminal,
    ResolveCurrentCenter,
    ResolveNextCenterWindow,
    ResolveArrivalAirportLocal,
    ResolveArrivalTerminal,
    RunAuthorityFastPath,
    RunAuthorityProof,
    BuildDepartureSnapshot,
    BuildEnrouteSnapshot,
    BuildArrivalSnapshot,
    PublishUiSnapshot,
    Diagnostics,
};

enum class BrainWorkPriority {
    SafetyCurrentPosition = 0,
    DepartureAuthority = 10,
    CurrentEnrouteAuthority = 20,
    NextCenterLookahead = 30,
    ArrivalAuthority = 40,
    EmptyCurrentPolygonRecheck = 50,
    FutureRoutePreparation = 60,
    Diagnostics = 70,
};

enum class BrainWorkReason {
    Unknown,
    NewFlightPlan,
    RouteIdentityChanged,
    SourceGenerationChanged,
    AircraftMovementThreshold,
    CurrentPolygonChanged,
    NextPolygonLookahead,
    ArrivalWakeDistance,
    ControllerFeedRelevantDiff,
    TransceiverFeedRelevantDiff,
    ManualRecovery,
    ReconnectRecovery,
    DiversionOrReroute,
    StageChanged,
    UiRefresh,
    BoardSnapshot,
    EmptyPolygonRecheck,
    FutureRoutePrep,
};

enum class BrainWorkBudget {
    Light,
    Medium,
    Heavy,
};

enum class BrainWorkResultStatus {
    NotRun,
    Completed,
    Deferred,
    Skipped,
    Failed,
};

enum class BrainWorkCacheStatus {
    Unknown,
    NotCacheable,
    Hit,
    Miss,
    Reused,
    Invalidated,
};

struct BrainWorkSourceGenerations {
    std::uint64_t controllerFeed = 0;
    std::uint64_t centerBoundary = 0;
    std::uint64_t authorityCatalog = 0;
    std::uint64_t terminalCoverage = 0;
    std::uint64_t transceiver = 0;
    std::uint64_t routeMap = 0;
};

struct BrainWorkTarget {
    WorkflowStage stage = WorkflowStage::None;
    std::string flightIdentityKey;
    std::string routeHash;
    std::string airportIcao;
    std::string polygonKey;
    int polygonSequence = -1;
    double distanceToTargetNm = 0.0;
};

struct BrainWorkItem {
    BrainWorkType type = BrainWorkType::Diagnostics;
    BrainWorkPriority priority = BrainWorkPriority::Diagnostics;
    BrainWorkReason reason = BrainWorkReason::Unknown;
    BrainWorkBudget budget = BrainWorkBudget::Light;
    BrainWorkTarget target;
    BrainWorkSourceGenerations sourceGenerations;
    std::string cacheKey;
    std::uint64_t enqueueSequence = 0;
};

struct BrainWorkResult {
    BrainWorkType type = BrainWorkType::Diagnostics;
    BrainWorkReason reason = BrainWorkReason::Unknown;
    BrainWorkResultStatus status = BrainWorkResultStatus::NotRun;
    BrainWorkCacheStatus cacheStatus = BrainWorkCacheStatus::Unknown;
    BrainWorkSourceGenerations sourceGenerations;
    std::string cacheKey;
    std::string summary;
    long long durationMs = 0;
};

struct BrainRoutePolygonState {
    std::string polygonKey;
    int sequence = -1;
    double entryDistanceNm = 0.0;
    double exitDistanceNm = 0.0;
    bool hasExitDistance = false;
    bool current = false;
    bool next = false;
    bool arrival = false;
    bool centerCoverage = false;
    bool terminalCoverage = false;
    bool hasSourceOwnership = false;
    bool unresolved = false;
    bool dirty = false;
    std::vector<std::string> matchTokens;
    std::vector<std::string> controllerCallsignPatterns;
    std::vector<std::string> controllerPrefixes;
};

struct RouteAuthorityPlan {
    bool available = false;
    bool stale = true;
    bool routeResolved = false;
    std::string diagnosticCacheStatus;
    std::string diagnosticReason;
    std::string flightIdentityKey;
    std::string routeHash;
    std::string cacheKey;
    std::string previousCacheKey;
    std::string pendingCacheKey;
    std::string departureIcao;
    std::string destinationIcao;
    std::uint64_t centerBoundaryGeneration = 0;
    std::uint64_t authorityCatalogGeneration = 0;
    std::uint64_t routeMapGeneration = 0;
    std::string statusLine;
    bool pendingRebuild = false;
    bool usingLastProvenPlan = false;
    std::vector<BrainRoutePolygonState> polygons;
};

struct DepartureAuthoritySnapshot {
    bool available = false;
    bool stale = true;
    bool pendingRebuild = false;
    bool usingLastProvenSnapshot = false;
    std::string airportIcao;
    std::string cacheKey;
    std::string previousCacheKey;
    std::string pendingCacheKey;
    std::string diagnosticCacheStatus;
    std::string diagnosticReason;
    std::string statusLine;
    std::uint64_t snapshotGeneration = 0;
    BrainWorkSourceGenerations sourceGenerations;
    AirportSectorSnapshot airportSectorSnapshot;
    ModuleBoardSnapshot boardSnapshot;
};

struct BrainControllerEvidenceSummary {
    std::string callsign;
    std::string frequency;
    std::string polygonKey;
    std::string proofSource;
    std::string proofDetail;
};

struct BrainRejectedCandidateSummary {
    std::string callsign;
    std::string reason;
    std::string source;
    std::string polygonKey;
};

struct BrainDataSnapshot {
    std::string flightIdentityKey;
    WorkflowStage stage = WorkflowStage::None;
    AircraftStateSnapshot aircraftState;
    std::vector<BrainRoutePolygonState> routePolygons;
    std::vector<BrainControllerEvidenceSummary> acceptedControllers;
    std::vector<BrainRejectedCandidateSummary> rejectedCandidates;
    BrainWorkSourceGenerations sourceGenerations;
    std::uint64_t snapshotGeneration = 0;
    bool pending = false;
};

const char* ToString(BrainWorkType type);
const char* ToString(BrainWorkPriority priority);
const char* ToString(BrainWorkReason reason);
const char* ToString(BrainWorkBudget budget);
const char* ToString(BrainWorkResultStatus status);
const char* ToString(BrainWorkCacheStatus status);

bool IsHeavyBrainWork(const BrainWorkItem& item);
bool BrainWorkComesBefore(const BrainWorkItem& left, const BrainWorkItem& right);
void SortBrainWorkQueue(std::vector<BrainWorkItem>* items);
std::string BrainWorkStableId(const BrainWorkItem& item);

RouteAuthorityPlan BuildRouteAuthorityPlanFromRouteSectorSnapshot(
    const NetworkPlanSnapshot& networkPlanSnapshot,
    const RouteSectorSnapshot& routeSectorSnapshot,
    std::uint64_t routeMapGeneration = 0);
RouteAuthorityPlan UpdateRouteAuthorityPlanCache(
    const NetworkPlanSnapshot& networkPlanSnapshot,
    const RouteSectorSnapshot& routeSectorSnapshot,
    RouteAuthorityPlan* activePlan,
    std::string* activeCacheKey,
    std::uint64_t* activeGeneration);
std::vector<std::string> RouteAuthorityPlanPolygonSequence(
    const RouteAuthorityPlan& plan);
std::vector<std::string> RouteAuthorityPlanFlagSummary(
    const RouteAuthorityPlan& plan);
std::vector<std::string> RouteAuthorityPlanSourceSummary(
    const RouteAuthorityPlan& plan);
std::string RouteAuthorityPlanLifecycleSummary(
    const RouteAuthorityPlan& plan);
std::vector<BrainWorkItem> BuildDepartureAuthorityWorkQueue(
    const NetworkPlanSnapshot& networkPlanSnapshot,
    const RouteAuthorityPlan& routeAuthorityPlan,
    const AirportSectorSnapshot& airportSectorSnapshot,
    std::uint64_t enqueueStart = 0);
DepartureAuthoritySnapshot BuildDepartureAuthoritySnapshot(
    const NetworkPlanSnapshot& networkPlanSnapshot,
    const RouteAuthorityPlan& routeAuthorityPlan,
    const AirportSectorSnapshot& airportSectorSnapshot,
    const ModuleBoardSnapshot& boardSnapshot,
    std::uint64_t snapshotGeneration = 0);
DepartureAuthoritySnapshot UpdateDepartureAuthoritySnapshotCache(
    const NetworkPlanSnapshot& networkPlanSnapshot,
    const RouteAuthorityPlan& routeAuthorityPlan,
    const AirportSectorSnapshot& airportSectorSnapshot,
    const ModuleBoardSnapshot& boardSnapshot,
    DepartureAuthoritySnapshot* activeSnapshot,
    std::string* activeCacheKey,
    std::uint64_t* activeGeneration);
std::string DepartureAuthoritySnapshotLifecycleSummary(
    const DepartureAuthoritySnapshot& snapshot);

}  // namespace xvatsim::brain
