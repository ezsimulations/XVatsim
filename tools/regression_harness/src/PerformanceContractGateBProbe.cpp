#include "PerformanceContractGateBProbe.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "XVatsim/brain/BrainOwnedWorkerTypes.h"
#include "XVatsim/brain/RoutePolygonTransition.h"
#include "XVatsim/modules/route_sector/RouteSectorResolver.h"

namespace xvatsim::tools::performance_contract_gate_b {
namespace {

using Clock = std::chrono::steady_clock;
using Microseconds = std::chrono::microseconds;
using Milliseconds = std::chrono::milliseconds;
using modules::route_sector::RoutePreparationRequest;
using modules::route_sector::RouteSectorResolver;
using modules::route_sector::RouteWorkerFact;

brain::BrainRouteCompletionIdentity ToBrainIdentity(
    const modules::route_sector::RouteWorkerIdentity& identity) {
    brain::BrainRouteCompletionIdentity output;
    output.requestId = identity.requestId;
    output.lifecycleEpoch = identity.lifecycleEpoch;
    output.planKey = identity.planKey;
    output.networkPlanDigest = identity.networkPlanDigest;
    output.routeSourceDatasetIdentity =
        identity.routeSourceDatasetIdentity;
    output.preflightCandidateIdentity = identity.preflightCandidateIdentity;
    output.routePolicyIdentity = identity.routePolicyIdentity;
    output.routeAnchorDigest = identity.routeAnchorDigest;
    output.expandedFmsObservationIdentity =
        identity.expandedFmsObservationIdentity;
    return output;
}

class InjectedRoutePreparationSource final
    : public modules::route_sector::RoutePreparationSource {
public:
    std::string ReadTextFile(const std::filesystem::path&) const override {
        ++textReadCalls;
        return {};
    }

    std::vector<std::filesystem::path> ListRegularFiles(
        const std::filesystem::path& directory) const override {
        ++listCalls;
        if (directory.filename() == "FMS plans") {
            return visibleFmsFiles;
        }
        return {};
    }

    core::preflight::FmsParseResult LoadFmsPlan(
        const std::filesystem::path& path) const override {
        ++fmsLoadCalls;
        const auto found = fmsResults.find(path.filename().string());
        if (found == fmsResults.end()) {
            core::preflight::FmsParseResult missing;
            missing.message = "injected missing or unreadable file";
            return missing;
        }
        return found->second;
    }

    core::preflight::CacheValidationResult ValidatePreflightCandidate(
        const core::preflight::PreflightRouteCache& candidate,
        const brain::NetworkPlanSnapshot& networkPlan) const override {
        ++preflightValidationCalls;
        return core::preflight::ValidatePreflightRouteCacheForNetworkPlan(
            candidate, networkPlan, false);
    }

    std::vector<std::filesystem::path> visibleFmsFiles;
    std::unordered_map<std::string, core::preflight::FmsParseResult>
        fmsResults;
    mutable std::uint64_t textReadCalls = 0;
    mutable std::uint64_t listCalls = 0;
    mutable std::uint64_t fmsLoadCalls = 0;
    mutable std::uint64_t preflightValidationCalls = 0;
};

core::preflight::FmsParseResult MakeInjectedFmsPlan(
    std::string sourceName,
    std::string marker,
    long long modifiedUnixSeconds,
    std::string departure = "KAAA",
    std::string destination = "KBBB") {
    core::preflight::FmsParseResult result;
    result.ok = true;
    result.message = "injected deterministic FMS plan";
    result.plan.sourcePath =
        (std::filesystem::path("injected") / sourceName).string();
    result.plan.sourceModifiedUnixSeconds = modifiedUnixSeconds;
    result.plan.sourceSizeBytes = 100;
    result.plan.sourceContentHash = "content-" + marker;
    result.plan.cycle = "2609";
    result.plan.departureIcao = std::move(departure);
    result.plan.destinationIcao = std::move(destination);
    result.plan.routeIdentityHash = "route-" + marker;
    result.plan.waypoints = {
        {1, "KAAA", "DRCT", 0.0, 40.0, -120.0},
        {11, "FIXA", "DRCT", 10000.0, 40.0, -115.0},
        {11, std::move(marker), "DRCT", 10000.0, 40.0, -110.0},
        {1, "KBBB", "DRCT", 0.0, 40.0, -100.0},
    };
    return result;
}

bool RouteContainsWaypoint(
    const std::shared_ptr<const brain::RouteSectorSnapshot>& route,
    std::string_view ident) {
    return route != nullptr &&
        std::any_of(
            route->waypoints.begin(),
            route->waypoints.end(),
            [&](const auto& waypoint) { return waypoint.ident == ident; });
}

std::uint64_t ElapsedUs(Clock::time_point started) {
    return static_cast<std::uint64_t>(std::max<long long>(
        0,
        std::chrono::duration_cast<Microseconds>(Clock::now() - started)
            .count()));
}

std::uint64_t Percentile99(std::vector<std::uint64_t> values) {
    if (values.empty()) {
        return 0;
    }
    std::sort(values.begin(), values.end());
    const auto index = std::min<std::size_t>(
        values.size() - 1,
        static_cast<std::size_t>(
            static_cast<double>(values.size() - 1) * 0.99));
    return values[index];
}

const char* BoundaryPayload() {
    return R"json({"type":"FeatureCollection","features":[
      {"type":"Feature","properties":{"identifier":"WEST","tokens":"WEST KZAA"},"geometry":{"type":"Polygon","coordinates":[[[-130,30],[-110,30],[-110,50],[-130,50],[-130,30]]]}},
      {"type":"Feature","properties":{"identifier":"EAST","tokens":"EAST KZBB"},"geometry":{"type":"Polygon","coordinates":[[[-110,30],[-90,30],[-90,50],[-110,50],[-110,30]]]}}
    ]})json";
}

const char* EmptyTerminalPayload() {
    return R"json({"type":"FeatureCollection","features":[]})json";
}

const char* AuthorityPayload() {
    return "[FIRs]\nKZAA|KZAA|Seattle Center|\nKZBB|KZBB|Denver Center|\n";
}

brain::AircraftStateSnapshot Aircraft() {
    brain::AircraftStateSnapshot aircraft;
    aircraft.valid = true;
    aircraft.latitudeDeg = 40.0;
    aircraft.longitudeDeg = -120.0;
    aircraft.onGround = false;
    return aircraft;
}

brain::NetworkPlanSnapshot NetworkPlan(std::string route = "DCT") {
    brain::NetworkPlanSnapshot plan;
    plan.feedAvailable = true;
    plan.stale = false;
    plan.matched = true;
    plan.cid = 1234567;
    plan.matchedCallsign = "UAL472";
    plan.departureIcao = "KAAA";
    plan.departureLatDeg = 40.0;
    plan.departureLonDeg = -120.0;
    plan.hasDepartureCoordinates = true;
    plan.destinationIcao = "KBBB";
    plan.destinationLatDeg = 40.0;
    plan.destinationLonDeg = -100.0;
    plan.hasDestinationCoordinates = true;
    plan.routeText = std::move(route);
    return plan;
}

bool SameSector(
    const brain::RouteSectorMatchSnapshot& left,
    const brain::RouteSectorMatchSnapshot& right) {
    return left.identifier == right.identifier &&
           left.entryDistanceNm == right.entryDistanceNm &&
           left.matchTokens == right.matchTokens &&
           left.controllerCallsignPatterns ==
               right.controllerCallsignPatterns &&
           left.controllerPrefixes == right.controllerPrefixes &&
           left.centerCoverage == right.centerCoverage &&
           left.terminalCoverage == right.terminalCoverage;
}

bool SameRoute(
    const brain::RouteSectorSnapshot& left,
    const brain::RouteSectorSnapshot& right) {
    if (left.available != right.available || left.stale != right.stale ||
        left.routeResolved != right.routeResolved ||
        left.statusLine != right.statusLine ||
        left.diagnosticCacheStatus != right.diagnosticCacheStatus ||
        left.diagnosticReason != right.diagnosticReason ||
        left.centerBoundaryGeneration != right.centerBoundaryGeneration ||
        left.authorityCatalogGeneration !=
            right.authorityCatalogGeneration ||
        left.departureIcao != right.departureIcao ||
        left.destinationIcao != right.destinationIcao ||
        left.waypoints.size() != right.waypoints.size() ||
        left.currentSectors.size() != right.currentSectors.size() ||
        left.nextSectors.size() != right.nextSectors.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.waypoints.size(); ++index) {
        if (left.waypoints[index].ident != right.waypoints[index].ident ||
            left.waypoints[index].latitudeDeg !=
                right.waypoints[index].latitudeDeg ||
            left.waypoints[index].longitudeDeg !=
                right.waypoints[index].longitudeDeg) {
            return false;
        }
    }
    for (std::size_t index = 0; index < left.currentSectors.size(); ++index) {
        if (!SameSector(left.currentSectors[index], right.currentSectors[index])) {
            return false;
        }
    }
    for (std::size_t index = 0; index < left.nextSectors.size(); ++index) {
        if (!SameSector(left.nextSectors[index], right.nextSectors[index])) {
            return false;
        }
    }
    return brain::HashBrainAuthorityRouteSnapshot(left) ==
        brain::HashBrainAuthorityRouteSnapshot(right);
}

RoutePreparationRequest BuildRequest(
    const RouteSectorResolver& resolver,
    const brain::AircraftStateSnapshot& aircraft,
    const brain::NetworkPlanSnapshot& plan,
    std::uint64_t requestId,
    std::uint64_t lifecycleEpoch = 1,
    std::uint32_t delayMs = 0) {
    RoutePreparationRequest request;
    request.identity.requestId = requestId;
    request.identity.lifecycleEpoch = lifecycleEpoch;
    request.identity.planKey = plan.departureIcao + "->" +
        plan.destinationIcao;
    request.identity.networkPlanDigest =
        modules::route_sector::HashRouteNetworkPlan(plan);
    auto publication = resolver.GetRouteSourceDatasetPublication();
    request.identity.routeSourceDatasetIdentity = publication.identity;
    request.identity.preflightCandidateIdentity =
        modules::route_sector::HashRoutePreflightCandidate(
            nullptr, "no-candidate");
    request.identity.routePolicyIdentity =
        modules::route_sector::RoutePreparationPolicyIdentity();
    request.identity.routeAnchorDigest =
        modules::route_sector::HashRouteAnchor(aircraft, plan);
    request.identity.expandedFmsObservationIdentity = 1;
    request.routeAnchor = aircraft;
    request.networkPlan =
        std::make_shared<const brain::NetworkPlanSnapshot>(plan);
    request.routeSourceDataset = publication.dataset;
    request.preflightValidationReason = "no-candidate";
    request.cooperativeDelayForTestingMs = delayMs;
    return request;
}

bool WaitForFact(
    modules::route_sector::RoutePreparationWorker* worker,
    std::uint64_t requestId,
    Milliseconds timeout,
    RouteWorkerFact* output,
    std::vector<std::uint64_t>* harvestSamples = nullptr) {
    const auto deadline = Clock::now() + timeout;
    while (Clock::now() < deadline) {
        RouteWorkerFact fact;
        const auto started = Clock::now();
        const auto harvested = worker != nullptr && worker->TryHarvest(&fact);
        if (harvestSamples != nullptr) {
            harvestSamples->push_back(ElapsedUs(started));
        }
        if (harvested) {
            if (fact.identity.requestId != requestId) {
                if (fact.routeLease != nullptr) {
                    fact.routeLease->retired.store(
                        true, std::memory_order_release);
                    worker->NotifyRetirement();
                }
                continue;
            }
            if (output != nullptr) {
                *output = std::move(fact);
            }
            return true;
        }
        std::this_thread::sleep_for(Milliseconds(1));
    }
    return false;
}

void RetireFact(
    modules::route_sector::RoutePreparationWorker* worker,
    RouteWorkerFact* fact) {
    if (fact != nullptr && fact->routeLease != nullptr) {
        fact->routeLease->retired.store(true, std::memory_order_release);
        fact->routeLease.reset();
    }
    if (worker != nullptr) {
        worker->NotifyRetirement();
    }
}

}  // namespace

int RunPerformanceContractGateBProbe() {
    std::vector<std::string> failures;
    const auto require = [&](bool condition, std::string failure) {
        if (!condition) {
            failures.push_back(std::move(failure));
        }
    };

    RouteSectorResolver resolver;
    resolver.LoadBoundaryPayloadsForTesting(
        BoundaryPayload(), EmptyTerminalPayload(), AuthorityPayload(), "{}");
    const auto aircraft = Aircraft();
    const auto plan = NetworkPlan();
    auto publication = resolver.GetRouteSourceDatasetPublication();
    require(publication.dataset != nullptr && publication.identity != 0,
            "immutable route dataset publication was unavailable");
    require(publication.available && !publication.stale &&
                publication.publicationGeneration != 0,
            "initial route-source publication was not exactly fresh");
    auto initialPublication = publication;
    resolver.QueueBoundaryPayloadsForTesting(
        BoundaryPayload(), EmptyTerminalPayload(), AuthorityPayload(), "{}");
    (void)resolver.TryHarvestSourcePublications();
    auto sameContentPublication =
        resolver.GetRouteSourceDatasetPublication();
    require(sameContentPublication.identity == initialPublication.identity &&
                sameContentPublication.publicationGeneration ==
                    initialPublication.publicationGeneration &&
                sameContentPublication.centerBoundaryGeneration ==
                    initialPublication.centerBoundaryGeneration &&
                sameContentPublication.authorityCatalogGeneration ==
                    initialPublication.authorityCatalogGeneration,
            "same-content source refresh created semantic churn");
    resolver.QueueBoundaryPayloadsForTesting("", "", "", "");
    (void)resolver.TryHarvestSourcePublications();
    auto failedPublication =
        resolver.GetRouteSourceDatasetPublication();
    require(failedPublication.dataset != nullptr &&
                failedPublication.available && failedPublication.stale &&
                failedPublication.contentIdentity ==
                    initialPublication.contentIdentity &&
                failedPublication.identity != initialPublication.identity,
            "failed source refresh did not publish exact stale semantics");
    resolver.QueueBoundaryPayloadsForTesting(
        BoundaryPayload(), EmptyTerminalPayload(), AuthorityPayload(), "{}");
    (void)resolver.TryHarvestSourcePublications();
    publication = resolver.GetRouteSourceDatasetPublication();
    require(publication.available && !publication.stale &&
                publication.contentIdentity ==
                    initialPublication.contentIdentity &&
                publication.identity != failedPublication.identity &&
                publication.centerBoundaryGeneration ==
                    initialPublication.centerBoundaryGeneration &&
                publication.authorityCatalogGeneration ==
                    initialPublication.authorityCatalogGeneration,
            "same-content recovery did not restore freshness without generation churn");
    initialPublication.dataset.reset();
    sameContentPublication.dataset.reset();
    failedPublication.dataset.reset();

    modules::route_sector::RouteCoordinatorDecisionInput coordinatorInput;
    coordinatorInput.sourceUsable = true;
    coordinatorInput.retryDue = true;
    auto coordinatorDecision =
        modules::route_sector::DecideRouteCoordinatorAction(coordinatorInput);
    require(coordinatorDecision.action ==
                modules::route_sector::RouteCoordinatorAction::
                    ObserveExpandedFms &&
                coordinatorDecision.failClosed,
            "production coordinator did not require initial FMS observation");
    coordinatorInput.fmsObservationReady = true;
    coordinatorDecision =
        modules::route_sector::DecideRouteCoordinatorAction(coordinatorInput);
    require(coordinatorDecision.action ==
                modules::route_sector::RouteCoordinatorAction::PrepareRoute,
            "production coordinator did not dispatch initial route work");
    brain::BrainOwnedRuntimeState unresolvedState;
    brain::BrainOwnedRoutePolygonRefreshInput unresolvedRefresh;
    unresolvedRefresh.aircraft = aircraft;
    unresolvedRefresh.routeRuntimeKey = "b3.1-unresolved-retry";
    unresolvedRefresh.nowSeconds = 1;
    unresolvedRefresh.pendingRetrySeconds = 2;
    auto unresolvedRoute = std::make_shared<brain::RouteSectorSnapshot>();
    unresolvedRoute->available = true;
    unresolvedRoute->stale = false;
    unresolvedRoute->routeResolved = false;
    (void)brain::CommitBrainOwnedRoutePolygonRefresh(
        &unresolvedState,
        unresolvedRefresh,
        brain::BuildBrainRoutePolygonWorkerOutput(unresolvedRoute));
    unresolvedRefresh.nowSeconds = 4;
    const auto unresolvedRetry = brain::BeginBrainOwnedRoutePolygonRefresh(
        &unresolvedState, unresolvedRefresh);
    require(unresolvedRetry.needsWorker,
            "Brain did not request a bounded unresolved-route retry");
    coordinatorInput.acceptedCurrent = true;
    coordinatorInput.brainNeedsWorker = unresolvedRetry.needsWorker;
    coordinatorDecision =
        modules::route_sector::DecideRouteCoordinatorAction(coordinatorInput);
    require(coordinatorDecision.action ==
                modules::route_sector::RouteCoordinatorAction::PrepareRoute &&
                coordinatorDecision.failClosed,
            "production coordinator ignored unresolved Brain retry work");
    coordinatorInput.brainNeedsWorker = false;
    coordinatorInput.fmsObservationDue = true;
    coordinatorDecision =
        modules::route_sector::DecideRouteCoordinatorAction(coordinatorInput);
    require(coordinatorDecision.action ==
                modules::route_sector::RouteCoordinatorAction::
                    ObserveExpandedFms &&
                !coordinatorDecision.failClosed,
            "production coordinator did not schedule a quiet FMS observation");

    auto noDepartureCoordinates = plan;
    noDepartureCoordinates.hasDepartureCoordinates = false;
    modules::route_sector::RouteAnchorSelectionState anchorState;
    const auto fallbackPlanDigest =
        modules::route_sector::HashRouteNetworkPlan(noDepartureCoordinates);
    const auto stickyAnchor = modules::route_sector::SelectStickyRouteAnchor(
        &anchorState,
        aircraft,
        noDepartureCoordinates,
        fallbackPlanDigest);
    auto movedAircraft = aircraft;
    movedAircraft.latitudeDeg += 2.0;
    movedAircraft.longitudeDeg += 2.0;
    const auto movedStickyAnchor =
        modules::route_sector::SelectStickyRouteAnchor(
            &anchorState,
            movedAircraft,
            noDepartureCoordinates,
            fallbackPlanDigest);
    require(modules::route_sector::HashRouteAnchor(
                stickyAnchor, noDepartureCoordinates) ==
                modules::route_sector::HashRouteAnchor(
                    movedStickyAnchor, noDepartureCoordinates),
            "ordinary aircraft movement churned the fallback route anchor");
    auto replacedFallbackPlan = noDepartureCoordinates;
    replacedFallbackPlan.routeText += " REPLACED";
    const auto replacementDigest =
        modules::route_sector::HashRouteNetworkPlan(replacedFallbackPlan);
    const auto replacementAnchor =
        modules::route_sector::SelectStickyRouteAnchor(
            &anchorState,
            movedAircraft,
            replacedFallbackPlan,
            replacementDigest);
    require(replacementAnchor.latitudeDeg == movedAircraft.latitudeDeg &&
                replacementAnchor.longitudeDeg == movedAircraft.longitudeDeg,
            "material route replacement did not recapture the fallback anchor");
    modules::route_sector::RouteAnchorSelectionState invalidAnchorState;
    auto invalidAircraft = aircraft;
    invalidAircraft.valid = false;
    const auto invalidAnchor = modules::route_sector::SelectStickyRouteAnchor(
        &invalidAnchorState,
        invalidAircraft,
        noDepartureCoordinates,
        fallbackPlanDigest);
    const auto recoveredAnchor = modules::route_sector::SelectStickyRouteAnchor(
        &invalidAnchorState,
        aircraft,
        noDepartureCoordinates,
        fallbackPlanDigest);
    require(!invalidAnchor.valid && recoveredAnchor.valid &&
                recoveredAnchor.latitudeDeg == aircraft.latitudeDeg &&
                recoveredAnchor.longitudeDeg == aircraft.longitudeDeg,
            "first valid aircraft position did not recover the sticky fallback anchor");

    const auto oracle =
        resolver.ResolveSynchronousRouteOracleForTesting(aircraft, plan);
    modules::route_sector::RoutePreparationEngine engine;
    auto directRequest = BuildRequest(resolver, aircraft, plan, 1);
    const auto prepared = engine.Prepare(directRequest);
    require(prepared.route != nullptr,
            "worker-owned route engine produced no immutable result");
    require(prepared.route != nullptr && SameRoute(oracle, *prepared.route),
            "frozen synchronous oracle and route engine differed");

    auto changedPlan = plan;
    changedPlan.routeText = "DCT CHANGED";
    auto changedIdentity = BuildRequest(
        resolver, aircraft, changedPlan, 2).identity;
    require(!modules::route_sector::SameRouteWorkerSemanticIdentity(
                directRequest.identity, changedIdentity),
            "material route edit did not change semantic identity");
    auto controllerNeutralIdentity = directRequest.identity;
    controllerNeutralIdentity.requestId = 999;
    require(modules::route_sector::SameRouteWorkerSemanticIdentity(
                directRequest.identity, controllerNeutralIdentity),
            "request id polluted semantic route identity");

    brain::BrainRouteCompletionValidationInput exactCompletion;
    exactCompletion.eligible = ToBrainIdentity(directRequest.identity);
    exactCompletion.desired = exactCompletion.eligible;
    exactCompletion.desired.requestId = 0;
    exactCompletion.completed = exactCompletion.eligible;
    require(brain::DecideBrainRouteCompletion(exactCompletion).accepted,
            "Brain rejected an exact route completion identity");
    const auto requireStaleIdentity =
        [&](brain::BrainRouteCompletionValidationInput variant,
            std::string failure) {
            require(
                !brain::DecideBrainRouteCompletion(variant).accepted,
                std::move(failure));
        };
    auto staleCompletion = exactCompletion;
    ++staleCompletion.completed.requestId;
    requireStaleIdentity(
        staleCompletion, "Brain accepted a stale route request id");
    staleCompletion = exactCompletion;
    ++staleCompletion.completed.lifecycleEpoch;
    requireStaleIdentity(
        staleCompletion, "Brain accepted a stale route lifecycle");
    staleCompletion = exactCompletion;
    staleCompletion.completed.planKey += "-stale";
    requireStaleIdentity(
        staleCompletion, "Brain accepted a stale route plan identity");
    staleCompletion = exactCompletion;
    ++staleCompletion.completed.networkPlanDigest;
    requireStaleIdentity(
        staleCompletion, "Brain accepted a stale network-plan digest");
    staleCompletion = exactCompletion;
    ++staleCompletion.completed.routeSourceDatasetIdentity;
    requireStaleIdentity(
        staleCompletion, "Brain accepted a stale route-source identity");
    staleCompletion = exactCompletion;
    ++staleCompletion.completed.preflightCandidateIdentity;
    requireStaleIdentity(
        staleCompletion, "Brain accepted a stale preflight identity");
    staleCompletion = exactCompletion;
    ++staleCompletion.completed.routePolicyIdentity;
    requireStaleIdentity(
        staleCompletion, "Brain accepted a stale route policy");
    staleCompletion = exactCompletion;
    ++staleCompletion.completed.routeAnchorDigest;
    requireStaleIdentity(
        staleCompletion, "Brain accepted a stale route anchor");
    staleCompletion = exactCompletion;
    ++staleCompletion.completed.expandedFmsObservationIdentity;
    requireStaleIdentity(
        staleCompletion, "Brain accepted a stale expanded-FMS observation");
    staleCompletion = exactCompletion;
    ++staleCompletion.desired.networkPlanDigest;
    requireStaleIdentity(
        staleCompletion, "Brain accepted a completion obsolete to desired input");

    std::uint64_t lifecycleReplayStaleRejections = 0;
    std::uint64_t lifecycleReplayCoordinatorPasses = 0;
    auto priorLifecycleIdentity = exactCompletion.completed;
    for (std::uint64_t boundary = 0; boundary < 1000; ++boundary) {
        auto currentIdentity = exactCompletion.desired;
        currentIdentity.requestId = 10'000 + boundary;
        currentIdentity.lifecycleEpoch = 2 + boundary;
        switch (boundary % 5) {
            case 0:  // Plugin Admin disable.
                currentIdentity.planKey += "|disabled";
                break;
            case 1:  // Plugin Admin re-enable.
                currentIdentity.planKey += "|reenabled";
                break;
            case 2:  // Network disconnect.
                currentIdentity.networkPlanDigest += boundary + 1;
                break;
            case 3:  // Reconnect or route-plan replacement.
                currentIdentity.planKey += "|replacement-" +
                    std::to_string(boundary);
                break;
            default:  // Source replacement or session reset.
                currentIdentity.routeSourceDatasetIdentity += boundary + 1;
                break;
        }
        brain::BrainRouteCompletionValidationInput replayValidation;
        replayValidation.eligible = currentIdentity;
        replayValidation.desired = currentIdentity;
        replayValidation.completed = priorLifecycleIdentity;
        if (!brain::DecideBrainRouteCompletion(replayValidation).accepted) {
            ++lifecycleReplayStaleRejections;
        }

        modules::route_sector::RouteCoordinatorDecisionInput replayCoordinator;
        const auto disconnectedOrDisabled = boundary % 5 == 0 ||
            boundary % 5 == 2;
        replayCoordinator.sourceUsable = !disconnectedOrDisabled;
        replayCoordinator.fmsObservationReady = true;
        replayCoordinator.retryDue = true;
        const auto replayDecision =
            modules::route_sector::DecideRouteCoordinatorAction(
                replayCoordinator);
        const auto coordinatorCorrect = disconnectedOrDisabled
            ? replayDecision.action ==
                      modules::route_sector::RouteCoordinatorAction::None &&
                  replayDecision.failClosed
            : replayDecision.action ==
                      modules::route_sector::RouteCoordinatorAction::
                          PrepareRoute &&
                  replayDecision.failClosed;
        if (coordinatorCorrect) {
            ++lifecycleReplayCoordinatorPasses;
        }
        priorLifecycleIdentity = currentIdentity;
    }
    require(lifecycleReplayStaleRejections == 1000 &&
                lifecycleReplayCoordinatorPasses == 1000,
            "accelerated lifecycle replay admitted stale work or chose an unsafe coordinator action");

    // Exercise expanded-FMS behavior entirely through the injected source.
    // This fixture deliberately supplies no live X-Plane files.
    auto injectedSource =
        std::make_shared<InjectedRoutePreparationSource>();
    const auto missingPath =
        std::filesystem::path("injected/KAAAKBBB_MISSING.fms");
    const auto malformedPath =
        std::filesystem::path("injected/KAAAKBBB_MALFORMED.fms");
    const auto alphaPath =
        std::filesystem::path("injected/KAAAKBBB_ALPHA.fms");
    const auto betaPath =
        std::filesystem::path("injected/KAAAKBBB_BETA.fms");
    const auto newerPath =
        std::filesystem::path("injected/KAAAKBBB_NEWER.fms");
    injectedSource->fmsResults.emplace(
        malformedPath.filename().string(),
        core::preflight::FmsParseResult{
            false, {}, "injected malformed FMS file"});
    injectedSource->fmsResults.emplace(
        alphaPath.filename().string(),
        MakeInjectedFmsPlan(
            alphaPath.filename().string(), "ALPHA", 100));
    injectedSource->fmsResults.emplace(
        betaPath.filename().string(),
        MakeInjectedFmsPlan(
            betaPath.filename().string(), "BETA", 100));
    injectedSource->fmsResults.emplace(
        newerPath.filename().string(),
        MakeInjectedFmsPlan(
            newerPath.filename().string(), "NEWER", 101));
    modules::route_sector::RoutePreparationEngine injectedEngine(
        "X:/Injected-X-Plane", injectedSource);
    const auto expandedPlan = NetworkPlan("FIXA");

    injectedSource->visibleFmsFiles = {missingPath, malformedPath};
    auto noValidFms = injectedEngine.Prepare(
        BuildRequest(resolver, aircraft, expandedPlan, 3));
    require(noValidFms.route != nullptr &&
                noValidFms.route->diagnosticCacheStatus !=
                    "expanded-fms-route-build",
            "missing/unreadable/malformed FMS input was accepted");
    const auto noMatchObservation =
        injectedEngine.ObserveExpandedFms(expandedPlan);
    const auto unchangedNoMatchObservation =
        injectedEngine.ObserveExpandedFms(expandedPlan);
    require(noMatchObservation.identity != 0 &&
                noMatchObservation.identity ==
                    unchangedNoMatchObservation.identity &&
                !noMatchObservation.hasMatchingPlan,
            "unchanged missing FMS observation was not quiet/deterministic");

    // The late appearance of two equal-time matches must enrich the route and
    // choose the lexically later filename, exactly matching frozen behavior.
    injectedSource->visibleFmsFiles = {
        missingPath, malformedPath, alphaPath, betaPath};
    auto equalTimeFms = injectedEngine.Prepare(
        BuildRequest(resolver, aircraft, expandedPlan, 4));
    require(equalTimeFms.route != nullptr &&
                equalTimeFms.route->diagnosticCacheStatus ==
                    "expanded-fms-route-build" &&
                RouteContainsWaypoint(equalTimeFms.route, "BETA") &&
                !RouteContainsWaypoint(equalTimeFms.route, "ALPHA"),
            "equal-time expanded-FMS filename tie-break was not exact");
    const auto matchingObservation =
        injectedEngine.ObserveExpandedFms(expandedPlan);
    require(matchingObservation.identity != noMatchObservation.identity &&
                matchingObservation.hasMatchingPlan,
            "late expanded-FMS appearance did not change observation identity");

    // Modification time has priority over filename ordering.
    injectedSource->visibleFmsFiles.push_back(newerPath);
    auto newestFms = injectedEngine.Prepare(
        BuildRequest(resolver, aircraft, expandedPlan, 5));
    require(newestFms.route != nullptr &&
                RouteContainsWaypoint(newestFms.route, "NEWER") &&
                !RouteContainsWaypoint(newestFms.route, "BETA"),
            "expanded-FMS modification-time tie-break was not exact");
    const auto newerObservation =
        injectedEngine.ObserveExpandedFms(expandedPlan);
    require(newerObservation.identity != matchingObservation.identity &&
                newerObservation.hasMatchingPlan,
            "newer expanded-FMS selection did not change observation identity");

    // An exact preflight candidate remains higher priority than discovery.
    auto preflightResult = MakeInjectedFmsPlan(
        "KAAAKBBB_PREFLIGHT.fms", "PREFLIGHT", 50);
    auto preflightRequest =
        BuildRequest(resolver, aircraft, expandedPlan, 6);
    preflightRequest.preflightCandidate =
        std::make_shared<const core::preflight::PreflightRouteCache>(
            core::preflight::BuildPreflightRouteCache(
                preflightResult.plan));
    preflightRequest.identity.preflightCandidateIdentity =
        modules::route_sector::HashRoutePreflightCandidate(
            preflightRequest.preflightCandidate.get(), "candidate");
    const auto preflightPreferred =
        injectedEngine.Prepare(preflightRequest);
    require(preflightPreferred.route != nullptr &&
                preflightPreferred.route->diagnosticCacheStatus ==
                    "preflight-route-cache-build" &&
                RouteContainsWaypoint(
                    preflightPreferred.route, "PREFLIGHT") &&
                !RouteContainsWaypoint(preflightPreferred.route, "NEWER"),
            "valid preflight cache did not retain exact source priority");
    require(injectedSource->listCalls > 0 &&
                injectedSource->fmsLoadCalls > 0 &&
                injectedSource->preflightValidationCalls > 0,
            "injected route source interface was not used end-to-end");

    modules::route_sector::RoutePreparationWorker observationWorker;
    require(observationWorker.Start("", injectedSource, false),
            "injected FMS observation worker startup failed");
    injectedSource->visibleFmsFiles = {alphaPath, betaPath};
    auto observationRequest = BuildRequest(
        resolver, aircraft, expandedPlan, 7);
    observationRequest.kind =
        RoutePreparationRequest::Kind::ObserveExpandedFms;
    require(observationWorker.StartLatest(std::move(observationRequest)),
            "production observation request was rejected");
    RouteWorkerFact observationFact;
    require(WaitForFact(
                &observationWorker,
                7,
                Milliseconds(5000),
                &observationFact),
            "production observation request timed out");
    const auto firstWorkerObservation =
        observationFact.expandedFmsObservationIdentity;
    require(observationFact.completed && observationFact.expandedFmsMatch &&
                firstWorkerObservation != 0 &&
                observationFact.routeLease == nullptr,
            "production observation performed route work or returned no identity");
    require(
        observationFact.expandedFmsObservationWorkerWallUs > 0,
        "production observation worker wall time was not captured");
    require(
        observationFact.expandedFmsObservationWorkerCpuAvailable,
        "production observation worker CPU time was not available");
    RetireFact(&observationWorker, &observationFact);
    auto unchangedObservationRequest = BuildRequest(
        resolver, aircraft, expandedPlan, 8);
    unchangedObservationRequest.kind =
        RoutePreparationRequest::Kind::ObserveExpandedFms;
    require(observationWorker.StartLatest(
                std::move(unchangedObservationRequest)),
            "unchanged production observation request was rejected");
    RouteWorkerFact unchangedObservationFact;
    require(WaitForFact(
                &observationWorker,
                8,
                Milliseconds(5000),
                &unchangedObservationFact) &&
                unchangedObservationFact.expandedFmsObservationIdentity ==
                    firstWorkerObservation,
            "unchanged production FMS observation caused semantic churn");
    require(
        unchangedObservationFact.expandedFmsObservationWorkerWallUs > 0 &&
            unchangedObservationFact.
                expandedFmsObservationWorkerCpuAvailable,
        "unchanged observation timing was not carried in the completion fact");
    RetireFact(&observationWorker, &unchangedObservationFact);
    injectedSource->visibleFmsFiles.push_back(newerPath);
    auto changedObservationRequest = BuildRequest(
        resolver, aircraft, expandedPlan, 9);
    changedObservationRequest.kind =
        RoutePreparationRequest::Kind::ObserveExpandedFms;
    require(observationWorker.StartLatest(
                std::move(changedObservationRequest)),
            "late-FMS production observation request was rejected");
    RouteWorkerFact changedObservationFact;
    require(WaitForFact(
                &observationWorker,
                9,
                Milliseconds(5000),
                &changedObservationFact) &&
                changedObservationFact.expandedFmsObservationIdentity !=
                    firstWorkerObservation,
            "production coordinator could not observe late FMS enrichment");
    require(
        changedObservationFact.expandedFmsObservationWorkerWallUs > 0 &&
            changedObservationFact.expandedFmsObservationWorkerCpuAvailable,
        "changed observation timing was not carried in the completion fact");
    const auto maximumObservationWorkerWallUs = std::max({
        observationFact.expandedFmsObservationWorkerWallUs,
        unchangedObservationFact.expandedFmsObservationWorkerWallUs,
        changedObservationFact.expandedFmsObservationWorkerWallUs,
    });
    const auto maximumObservationWorkerCpuUs = std::max({
        observationFact.expandedFmsObservationWorkerCpuUs,
        unchangedObservationFact.expandedFmsObservationWorkerCpuUs,
        changedObservationFact.expandedFmsObservationWorkerCpuUs,
    });
    RetireFact(&observationWorker, &changedObservationFact);
    const auto observationSnapshot = observationWorker.Snapshot();
    require(observationSnapshot.fmsObservations == 3 &&
                observationSnapshot.fmsObservationChanges == 2,
            "production FMS observation counters were not quiet/exact");
    observationWorker.CancelAndJoin();

    modules::route_sector::RoutePreparationWorker worker;
    require(worker.Start(""), "route worker startup failed");
    require(worker.ObserveSourceDataset(publication.dataset),
            "initial route-source observation was rejected");
    // Parity is now established. Release the harness's extra references so
    // the source-retirement stress measures production ownership only.
    directRequest.routeSourceDataset.reset();
    preflightRequest.routeSourceDataset.reset();
    publication.dataset.reset();

    const auto cancellationCountBeforeRace =
        worker.Snapshot().cancellations;
    auto raceRequest = BuildRequest(resolver, aircraft, plan, 900);
    raceRequest.workerStartRaceDelayForTestingMs = 100;
    require(worker.StartLatest(std::move(raceRequest)),
            "cancellation-race request was rejected");
    const auto raceStartDeadline = Clock::now() + Milliseconds(1000);
    while (!worker.Snapshot().running && Clock::now() < raceStartDeadline) {
        std::this_thread::yield();
    }
    require(worker.Snapshot().running,
            "cancellation-race seam did not reach running state");
    require(worker.StartLatest(BuildRequest(
                resolver, aircraft, plan, 901)),
            "cancellation-race replacement was rejected");
    RouteWorkerFact raceReplacementFact;
    require(WaitForFact(
                &worker,
                901,
                Milliseconds(5000),
                &raceReplacementFact) &&
                raceReplacementFact.completed &&
                !raceReplacementFact.cancelled,
            "sequence cancellation race delayed or lost the replacement");
    RetireFact(&worker, &raceReplacementFact);
    require(worker.Snapshot().cancellations >
                cancellationCountBeforeRace,
            "obsolete running request escaped sequence cancellation");

    std::vector<std::uint64_t> submitUs;
    std::vector<std::uint64_t> harvestUs;
    for (std::uint64_t requestId = 10; requestId < 110; ++requestId) {
        const auto started = Clock::now();
        auto request = BuildRequest(resolver, aircraft, plan, requestId);
        const auto submitted = worker.StartLatest(std::move(request));
        submitUs.push_back(ElapsedUs(started));
        require(submitted, "sequential package-and-submit was rejected");
        RouteWorkerFact fact;
        require(WaitForFact(
                    &worker, requestId, Milliseconds(5000), &fact, &harvestUs),
                "sequential route completion timed out");
        require(fact.completed && !fact.cancelled && !fact.failed &&
                    fact.routeLease != nullptr &&
                    SameRoute(oracle, *fact.routeLease->route),
                "sequential worker result failed exact oracle parity");
        RetireFact(&worker, &fact);
    }

    auto forcedFailure = BuildRequest(resolver, aircraft, plan, 150);
    forcedFailure.forceFailureForTesting = true;
    require(worker.StartLatest(std::move(forcedFailure)),
            "forced worker-failure request was rejected");
    RouteWorkerFact failureFact;
    require(WaitForFact(
                &worker, 150, Milliseconds(5000), &failureFact, &harvestUs),
            "forced worker failure did not publish a bounded fact");
    require(failureFact.failed && !failureFact.completed &&
                failureFact.routeLease == nullptr &&
                failureFact.reason.find("route-worker-exception:") == 0,
            "worker exception was not converted to a typed failure fact");
    RetireFact(&worker, &failureFact);
    require(worker.StartLatest(
                BuildRequest(resolver, aircraft, plan, 151)),
            "worker did not accept work after a caught exception");
    RouteWorkerFact recoveredFact;
    require(WaitForFact(
                &worker, 151, Milliseconds(5000), &recoveredFact, &harvestUs),
            "worker did not recover after a caught exception");
    require(recoveredFact.completed && recoveredFact.routeLease != nullptr &&
                SameRoute(oracle, *recoveredFact.routeLease->route),
            "post-exception worker result lost exact parity");
    RetireFact(&worker, &recoveredFact);

    auto slowA = BuildRequest(resolver, aircraft, plan, 200, 2, 2000);
    auto slowBPlan = plan;
    slowBPlan.routeText = "DCT B";
    auto slowB = BuildRequest(resolver, aircraft, slowBPlan, 201, 2, 500);
    auto latestPlan = plan;
    latestPlan.routeText = "DCT C";
    auto latest = BuildRequest(resolver, aircraft, latestPlan, 202, 2, 0);
    const auto delayedSubmitStarted = Clock::now();
    require(worker.StartLatest(std::move(slowA)),
            "forced two-second request submission failed");
    require(ElapsedUs(delayedSubmitStarted) <= 1000,
            "forced delay blocked route submission");
    std::this_thread::sleep_for(Milliseconds(5));
    require(worker.StartLatest(std::move(slowB)),
            "replacement B submission failed");
    require(worker.StartLatest(std::move(latest)),
            "latest C submission failed");
    RouteWorkerFact latestFact;
    require(WaitForFact(
                &worker, 202, Milliseconds(5000), &latestFact, &harvestUs),
            "latest-only A/B/C completion timed out");
    require(latestFact.completed && !latestFact.cancelled &&
                !latestFact.failed,
            "latest-only A/B/C did not publish C");
    RetireFact(&worker, &latestFact);

    std::uint64_t acceptedRapid = 0;
    std::uint64_t rejectedRapid = 0;
    std::uint64_t maximumRapidSubmitUs = 0;
    for (std::uint64_t index = 0; index < 10'000; ++index) {
        auto rapidPlan = plan;
        rapidPlan.routeText = "DCT R" + std::to_string(index);
        auto request = BuildRequest(
            resolver, aircraft, rapidPlan, 1000 + index, 3, 20);
        const auto started = Clock::now();
        if (worker.StartLatest(std::move(request))) {
            ++acceptedRapid;
        } else {
            ++rejectedRapid;
        }
        maximumRapidSubmitUs =
            std::max(maximumRapidSubmitUs, ElapsedUs(started));
    }
    const auto finalRapidId = 20'999ULL;
    bool finalSubmitted = false;
    const auto finalSubmitDeadline = Clock::now() + Milliseconds(5000);
    while (!finalSubmitted && Clock::now() < finalSubmitDeadline) {
        auto finalPlan = plan;
        finalPlan.routeText = "DCT R9999";
        finalSubmitted = worker.StartLatest(BuildRequest(
            resolver, aircraft, finalPlan, finalRapidId, 3, 0));
        if (!finalSubmitted) {
            std::this_thread::sleep_for(Milliseconds(1));
        }
    }
    RouteWorkerFact rapidFact;
    require(finalSubmitted &&
                WaitForFact(
                    &worker,
                    finalRapidId,
                    Milliseconds(5000),
                    &rapidFact,
                    &harvestUs),
            "10,000-change stress did not publish the exact latest route");
    RetireFact(&worker, &rapidFact);
    require(maximumRapidSubmitUs <= 1000,
            "rapid semantic replacement blocked the caller");

    std::uint64_t lastSourceIdentity = publication.identity;
    std::uint64_t sourceReplacements = 0;
    for (std::uint64_t epoch = 4; epoch < 1004; ++epoch) {
        if ((epoch - 4) % 100 == 0) {
            auto replacementPayload = std::string(BoundaryPayload());
            replacementPayload.append(
                static_cast<std::size_t>(sourceReplacements + 1), ' ');
            resolver.LoadBoundaryPayloadsForTesting(
                replacementPayload,
                EmptyTerminalPayload(),
                AuthorityPayload(),
                "{}");
            const auto replacement =
                resolver.GetRouteSourceDatasetPublication();
            require(replacement.dataset != nullptr &&
                        replacement.identity != lastSourceIdentity,
                    "route-source replacement did not change identity");
            bool observed = false;
            const auto observeDeadline = Clock::now() + Milliseconds(1000);
            while (!observed && Clock::now() < observeDeadline) {
                observed = worker.ObserveSourceDataset(replacement.dataset);
                if (!observed) {
                    std::this_thread::sleep_for(Milliseconds(1));
                }
            }
            require(observed,
                    "route-source replacement observation did not drain");
            lastSourceIdentity = replacement.identity;
            ++sourceReplacements;
        }
        auto request = BuildRequest(
            resolver, aircraft, plan, 30'000 + epoch, epoch, 10);
        (void)worker.StartLatest(std::move(request));
        worker.CancelPending();
    }

    auto largeRoute = std::make_shared<brain::RouteSectorSnapshot>();
    largeRoute->available = true;
    largeRoute->stale = false;
    largeRoute->routeResolved = true;
    for (int index = 0; index < 512; ++index) {
        largeRoute->waypoints.push_back({
            "W" + std::to_string(index),
            40.0,
            -120.0 + static_cast<double>(index) * 0.01,
        });
    }
    brain::RouteSectorMatchSnapshot current;
    current.identifier = "WEST";
    current.entryDistanceNm = 0.0;
    largeRoute->currentSectors.push_back(current);
    for (int index = 1; index <= 64; ++index) {
        brain::RouteSectorMatchSnapshot next;
        next.identifier = "S" + std::to_string(index);
        next.entryDistanceNm = static_cast<double>(index) * 10.0;
        largeRoute->nextSectors.push_back(std::move(next));
    }
    std::shared_ptr<const brain::RouteSectorSnapshot> immutableLarge = largeRoute;
    std::vector<std::uint64_t> transitionUs;
    transitionUs.reserve(2000);
    for (int iteration = 0; iteration < 2000; ++iteration) {
        brain::RoutePolygonTransitionWorkerInput input;
        input.aircraft = aircraft;
        input.route = immutableLarge;
        input.previousPolygonKey = "WEST";
        const auto started = Clock::now();
        const auto transition =
            brain::RunRoutePolygonTransitionWorker(input);
        transitionUs.push_back(ElapsedUs(started));
        require(!transition.changed && transition.route == immutableLarge,
                "unchanged transition copied or replaced the immutable route");
    }

    brain::BrainOwnedRuntimeState transitionState;
    brain::BrainOwnedRoutePolygonRefreshInput transitionRefresh;
    transitionRefresh.aircraft = aircraft;
    transitionRefresh.aircraft.longitudeDeg = -120.0;
    transitionRefresh.routeRuntimeKey = "gate-b-transition-retirement";
    transitionRefresh.nowSeconds = 1;
    transitionRefresh.pendingRetrySeconds = 1;
    auto committedTransition = brain::CommitBrainOwnedRoutePolygonRefresh(
        &transitionState,
        transitionRefresh,
        brain::BuildBrainRoutePolygonWorkerOutput(immutableLarge));
    transitionRefresh.aircraft.longitudeDeg = -119.0;
    transitionRefresh.nowSeconds = 2;
    auto appliedTransition = brain::BeginBrainOwnedRoutePolygonRefresh(
        &transitionState, transitionRefresh);
    require(appliedTransition.transitionChanged &&
                appliedTransition.retiredRoute != nullptr &&
                appliedTransition.retiredRoute !=
                    appliedTransition.route.route,
            "real polygon transition did not return replaced immutable route");
    require(worker.RetireSharedRoute(&appliedTransition.retiredRoute) &&
                appliedTransition.retiredRoute == nullptr,
            "transition route retirement handoff was not bounded/nonblocking");

    const auto waitForIdle = [&]() {
        const auto deadline = Clock::now() + Milliseconds(5000);
        while (Clock::now() < deadline) {
            const auto snapshot = worker.Snapshot();
            if (!snapshot.running && !snapshot.pending &&
                !snapshot.completed && snapshot.outstandingLeases == 0) {
                return true;
            }
            RouteWorkerFact abandoned;
            if (worker.TryHarvest(&abandoned)) {
                RetireFact(&worker, &abandoned);
            }
            worker.NotifyRetirement();
            std::this_thread::sleep_for(Milliseconds(1));
        }
        return false;
    };
    worker.CancelPending();
    require(waitForIdle(), "route worker did not return to idle baseline");
    const auto datasetDrainDeadline = Clock::now() + Milliseconds(5000);
    while (worker.Snapshot().retainedSourceDatasets > 1 &&
           Clock::now() < datasetDrainDeadline) {
        worker.NotifyRetirement();
        std::this_thread::sleep_for(Milliseconds(1));
    }
    const auto beforeJoin = worker.Snapshot();
    worker.CancelAndJoin();
    const auto afterJoin = worker.Snapshot();

    const auto submitP99 = Percentile99(submitUs);
    const auto harvestP99 = Percentile99(harvestUs);
    const auto transitionP99 = Percentile99(transitionUs);
    const auto submitMax = submitUs.empty()
        ? 0
        : *std::max_element(submitUs.begin(), submitUs.end());
    const auto harvestMax = harvestUs.empty()
        ? 0
        : *std::max_element(harvestUs.begin(), harvestUs.end());
    const auto transitionMax = transitionUs.empty()
        ? 0
        : *std::max_element(transitionUs.begin(), transitionUs.end());
    require(submitP99 <= 250 && submitMax <= 1000,
            "complete route package-and-submit timing gate failed");
    require(harvestP99 <= 250 && harvestMax <= 1000,
            "route harvest timing gate failed");
    require(transitionP99 <= 250 && transitionMax <= 1000,
            "unchanged transition timing gate failed");
    require(beforeJoin.maximumPendingDepth <= 1,
            "route worker pending depth exceeded one");
    require(beforeJoin.requestNodesInUse == 0 &&
                beforeJoin.outstandingLeases == 0,
            "route worker object counts did not return to idle baseline");
    require(beforeJoin.retainedSourceDatasets <= 1 &&
                beforeJoin.sourceDatasetObservations ==
                    sourceReplacements + 1,
            "obsolete route-source datasets did not retire to current-only");
    require(beforeJoin.peakRetainedSourceDatasets <= 6,
            "route-source retirement ledger exceeded its explicit bound");
    require(!afterJoin.started && !afterJoin.running &&
                !afterJoin.pending && !afterJoin.completed &&
                afterJoin.retainedSourceDatasets == 0,
            "route worker remained active after join");
    require(beforeJoin.leasesCreated == beforeJoin.leasesRetired &&
                beforeJoin.leasesRetired ==
                    beforeJoin.leasesDestroyedOnWorker,
            "route lease retirement was not exact and worker-owned");

    std::cout << "GATE_B_PROBE"
              << " parity=" << (prepared.route != nullptr &&
                                  SameRoute(oracle, *prepared.route) ? 1 : 0)
              << " submitP99Us=" << submitP99
              << " submitMaxUs=" << submitMax
              << " harvestP99Us=" << harvestP99
              << " harvestMaxUs=" << harvestMax
              << " transitionP99Us=" << transitionP99
              << " transitionMaxUs=" << transitionMax
              << " rapidAccepted=" << acceptedRapid
              << " rapidRejectedBounded=" << rejectedRapid
              << " rapidSubmitMaxUs=" << maximumRapidSubmitUs
              << " starts=" << beforeJoin.starts
              << " replacements=" << beforeJoin.replacements
              << " cancellations=" << beforeJoin.cancellations
              << " maxPendingDepth=" << beforeJoin.maximumPendingDepth
              << " requestNodesPeak=" << beforeJoin.requestNodesPeak
              << " leasesCreated=" << beforeJoin.leasesCreated
              << " leasesRetired=" << beforeJoin.leasesRetired
              << " leasesDestroyedOnWorker="
              << beforeJoin.leasesDestroyedOnWorker
              << " outstandingLeases=" << beforeJoin.outstandingLeases
              << " peakRetainedRouteBytes="
              << beforeJoin.peakRetainedRouteBytes
              << " sourceReplacements=" << sourceReplacements
              << " sourceDatasetObservations="
              << beforeJoin.sourceDatasetObservations
              << " sourceObservationRejections="
              << beforeJoin.sourceDatasetObservationRejections
              << " retainedSourceDatasets="
              << beforeJoin.retainedSourceDatasets
              << " peakRetainedSourceDatasets="
              << beforeJoin.peakRetainedSourceDatasets
              << " lifecycleReplayStaleRejections="
              << lifecycleReplayStaleRejections
              << " lifecycleReplayCoordinatorPasses="
              << lifecycleReplayCoordinatorPasses
              << " injectedSourceLists=" << injectedSource->listCalls
              << " injectedFmsLoads=" << injectedSource->fmsLoadCalls
              << " injectedPreflightValidations="
              << injectedSource->preflightValidationCalls
              << " observationWorkerWallMaxUs="
              << maximumObservationWorkerWallUs
              << " observationWorkerCpuMaxUs="
              << maximumObservationWorkerCpuUs
              << " observationWorkerCpuAvailable=1\n";

    if (!failures.empty()) {
        for (const auto& failure : failures) {
            std::cerr << "GATE_B_ASSERTION_FAILED: " << failure << "\n";
        }
        return 1;
    }
    std::cout << "PERFORMANCE_CONTRACT_GATE_B_PASSED\n";
    return 0;
}

}  // namespace xvatsim::tools::performance_contract_gate_b
