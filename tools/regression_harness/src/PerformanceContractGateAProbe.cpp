#include "PerformanceContractGateAProbe.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "XVatsim/brain/BrainOwnedWorkerTypes.h"
#include "XVatsim/modules/route_sector/RouteSectorResolver.h"

namespace xvatsim::tools::performance_contract_gate_a {
namespace {

using Clock = std::chrono::steady_clock;
using Microseconds = std::chrono::microseconds;
using Milliseconds = std::chrono::milliseconds;

long long ElapsedUs(Clock::time_point started) {
    return std::chrono::duration_cast<Microseconds>(Clock::now() - started)
        .count();
}

long long Percentile99(std::vector<long long> values) {
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

bool WaitUntilRunning(
    modules::route_sector::AuthorityRelevanceWorker* worker,
    Milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    while (Clock::now() < deadline) {
        if (worker != nullptr && worker->Snapshot().running) {
            return true;
        }
        std::this_thread::sleep_for(Milliseconds(1));
    }
    return false;
}

bool WaitForFact(
    modules::route_sector::AuthorityRelevanceWorker* worker,
    std::uint64_t expectedRequestId,
    Milliseconds timeout,
    modules::route_sector::AuthorityWorkerFact* fact,
    std::vector<long long>* harvestUs) {
    const auto deadline = Clock::now() + timeout;
    while (Clock::now() < deadline) {
        modules::route_sector::AuthorityWorkerFact candidate;
        const auto started = Clock::now();
        const auto harvested = worker != nullptr && worker->TryHarvest(&candidate);
        if (harvestUs != nullptr) {
            harvestUs->push_back(ElapsedUs(started));
        }
        if (harvested) {
            if (candidate.identity.requestId != expectedRequestId) {
                return false;
            }
            if (fact != nullptr) {
                *fact = std::move(candidate);
            }
            return true;
        }
        std::this_thread::sleep_for(Milliseconds(1));
    }
    return false;
}

modules::route_sector::AuthorityWorkerRequest BuildRequest(
    const modules::route_sector::RouteSectorResolver& resolver,
    const brain::AircraftStateSnapshot& aircraft,
    const brain::RouteSectorSnapshot& route,
    const std::vector<brain::ControllerSnapshot>& controllers,
    const brain::TransceiverResolutionSnapshot& transceivers,
    std::uint64_t requestId,
    std::uint32_t delayMs) {
    modules::route_sector::AuthorityWorkerRequest request;
    request.identity.requestId = requestId;
    request.identity.lifecycleEpoch = 7;
    request.identity.planKey = "KSEA->KDEN|gate-a-proof";
    request.identity.routeDigest =
        brain::HashBrainAuthorityRouteSnapshot(route);
    request.identity.controllerDigest =
        brain::HashBrainAuthorityControllerEvidence(
            controllers,
            true,
            false);
    const auto authorityTransceivers =
        brain::BuildBrainAuthorityTransceiverEvidence(transceivers);
    request.identity.transceiverDigest =
        brain::HashBrainAuthorityTransceiverEvidence(
            authorityTransceivers);
    request.identity.datasetIdentity =
        resolver.GetAuthoritySourceDatasetIdentity();
    request.aircraft = aircraft;
    request.route = std::make_shared<const brain::RouteSectorSnapshot>(route);
    request.controllers = std::make_shared<
        const std::vector<brain::AuthorityControllerSnapshot>>(
            brain::BuildBrainAuthorityControllerEvidence(controllers));
    request.hasControllerEvidence = true;
    request.controllerFeedAvailable = true;
    request.controllerFeedStale = false;
    request.controllerFeedGeneration = 9;
    request.controllerFeedConnectedControllers =
        static_cast<int>(controllers.size());
    request.terminalBoundaryGeneration =
        resolver.GetTerminalBoundaryGeneration();
    request.scheduleReason = "controller-relevance-evidence-ledger";
    request.hasTransceiverEvidence = true;
    request.transceivers = std::make_shared<
        const brain::AuthorityTransceiverEvidenceSnapshot>(
            authorityTransceivers);
    request.dataset = resolver.GetAuthoritySourceDataset();
    request.dispatchedMonotonicMs = 1000;
    request.cooperativeDelayForTestingMs = delayMs;
    return request;
}

}  // namespace

int RunPerformanceContractGateAProbe() {
    std::vector<std::string> failures;
    const auto require = [&](bool condition, std::string failure) {
        if (!condition) {
            failures.push_back(std::move(failure));
        }
    };

    brain::RouteSectorSnapshot route;
    route.available = true;
    route.stale = false;
    route.routeResolved = true;
    route.centerBoundaryGeneration = 3;
    route.authorityCatalogGeneration = 4;
    route.departureIcao = "KSEA";
    route.destinationIcao = "KDEN";
    route.waypoints = {
        {"SEA", 47.4489, -122.3094},
        {"DEN", 39.8561, -104.6737},
    };
    auto enrichedRoute = route;
    enrichedRoute.waypoints.insert(
        enrichedRoute.waypoints.begin() + 1,
        {"BOI", 43.5644, -116.2228});
    require(
        brain::HashBrainAuthorityRouteSnapshot(route) !=
            brain::HashBrainAuthorityRouteSnapshot(enrichedRoute),
        "authority route identity ignored waypoint enrichment");

    brain::TransceiverResolutionSnapshot transceivers;
    transceivers.available = true;
    transceivers.stale = false;
    transceivers.resolutionPath = "gate-a-proof";
    brain::TransceiverControllerEvidenceSnapshot transceiverController;
    transceiverController.callsign = "KZSE_CTR";
    transceiverController.controllerFrequency = "125.600";
    transceiverController.facility = 6;
    transceiverController.actionable = true;
    brain::TransceiverStationEvidenceSnapshot station;
    station.sourceFrequency = "125.600";
    station.latitudeDeg = 47.0;
    station.longitudeDeg = -121.0;
    station.score = 10.0;
    transceiverController.stations.push_back(station);
    transceivers.controllerEvidence.push_back(transceiverController);
    auto changedTransceivers = transceivers;
    changedTransceivers.controllerEvidence.front().stations.front().score = 11.0;
    require(
        brain::HashBrainTransceiverEvidence(transceivers) !=
            brain::HashBrainTransceiverEvidence(changedTransceivers),
        "transceiver evidence identity ignored station evidence");

    brain::BrainAuthorityCompletionValidationInput validation;
    validation.expected = {
        11,
        7,
        "KSEA->KDEN|gate-a-proof",
        101,
        202,
        303,
        404,
    };
    validation.completed = validation.expected;
    validation.currentAircraft.valid = true;
    validation.currentAircraft.latitudeDeg = 47.0;
    validation.currentAircraft.longitudeDeg = -122.0;
    validation.dispatchedAircraft = validation.currentAircraft;
    validation.nowMonotonicMs = 2000;
    validation.completedMonotonicMs = 1900;
    require(
        brain::DecideBrainAuthorityCompletion(validation).accepted,
        "exact completion identity was rejected");
    const auto requireIdentityReject = [&](auto mutate, const char* expectedReason) {
        auto changed = validation;
        mutate(&changed.completed);
        const auto decision = brain::DecideBrainAuthorityCompletion(changed);
        require(
            !decision.accepted && decision.reason == expectedReason,
            std::string("completion identity did not reject ") + expectedReason);
    };
    requireIdentityReject(
        [](auto* value) { ++value->lifecycleEpoch; },
        "lifecycle-epoch-mismatch");
    requireIdentityReject(
        [](auto* value) { value->planKey += "-changed"; },
        "plan-identity-mismatch");
    requireIdentityReject(
        [](auto* value) { ++value->routeDigest; },
        "authority-route-digest-mismatch");
    requireIdentityReject(
        [](auto* value) { ++value->controllerDigest; },
        "controller-digest-mismatch");
    requireIdentityReject(
        [](auto* value) { ++value->transceiverDigest; },
        "transceiver-digest-mismatch");
    requireIdentityReject(
        [](auto* value) { ++value->datasetIdentity; },
        "authority-dataset-identity-mismatch");
    auto oldCompletion = validation;
    oldCompletion.nowMonotonicMs = 8000;
    require(
        brain::DecideBrainAuthorityCompletion(oldCompletion).reason ==
            "completion-age-exceeded",
        "old completion was not rejected");
    auto displacedCompletion = validation;
    displacedCompletion.currentAircraft.longitudeDeg = -120.0;
    require(
        brain::DecideBrainAuthorityCompletion(displacedCompletion).reason ==
            "completion-displacement-exceeded",
        "geographically displaced completion was not rejected");

    modules::route_sector::RouteSectorResolver resolver;
    constexpr const char* kEmptyGeoJson =
        R"json({"type":"FeatureCollection","features":[]})json";
    resolver.LoadBoundaryPayloadsForTesting(
        kEmptyGeoJson,
        kEmptyGeoJson,
        "[FIR]\nTEST|TEST|TEST|\n",
        "{}");
    modules::route_sector::RouteSectorResolver changedDatasetResolver;
    changedDatasetResolver.LoadBoundaryPayloadsForTesting(
        kEmptyGeoJson,
        R"json({"type":"FeatureCollection","features":[],"revision":2})json",
        "[FIR]\nTEST|TEST|TEST|\n",
        "{}");
    require(
        resolver.GetAuthoritySourceDatasetIdentity() != 0 &&
            resolver.GetAuthoritySourceDatasetIdentity() !=
                changedDatasetResolver.GetAuthoritySourceDatasetIdentity(),
        "unified dataset identity did not include terminal content");

    brain::AircraftStateSnapshot aircraft;
    aircraft.valid = true;
    aircraft.latitudeDeg = 47.0;
    aircraft.longitudeDeg = -122.0;
    std::vector<brain::ControllerSnapshot> controllers;
    brain::ControllerSnapshot controller;
    controller.callsign = "KZSE_CTR";
    controller.frequency = "125.600";
    controller.facility = 6;
    controllers.push_back(controller);
    brain::ControllerFeedSnapshot controllerFeed;
    controllerFeed.available = true;
    controllerFeed.stale = false;
    controllerFeed.generation = 9;
    controllerFeed.connectedControllers = 1;
    controllerFeed.controllers = &controllers;

    const auto synchronous =
        resolver.ResolveBrainScheduledAuthorityVerification(
            aircraft,
            controllerFeed,
            route,
            "controller-relevance-evidence-ledger",
            &transceivers);
    const auto synchronousDigest =
        brain::HashBrainAuthorityRelevanceSnapshot(synchronous);

    modules::route_sector::AuthorityRelevanceWorker worker;
    require(worker.Start(), "authority worker startup failed");
    auto parityRequest = BuildRequest(
        resolver,
        aircraft,
        route,
        controllers,
        transceivers,
        1,
        0);
    require(worker.StartLatest(std::move(parityRequest)), "parity dispatch failed");
    modules::route_sector::AuthorityWorkerFact parityFact;
    std::vector<long long> harvestTimings;
    require(
        WaitForFact(
            &worker,
            1,
            Milliseconds(5000),
            &parityFact,
            &harvestTimings),
        "worker parity result did not complete");
    require(
        parityFact.completed && !parityFact.cancelled &&
            parityFact.snapshotLease != nullptr &&
            parityFact.snapshotLease->snapshot != nullptr &&
            parityFact.snapshotDigest == synchronousDigest &&
            parityFact.snapshotDigest ==
                brain::HashBrainAuthorityRelevanceSnapshot(
                    *parityFact.snapshotLease->snapshot),
        "worker output did not match the synchronous authority oracle");

    std::vector<long long> dispatchTimings;
    const auto runDelayed = [&](std::uint64_t requestId, std::uint32_t delayMs) {
        auto request = BuildRequest(
            resolver,
            aircraft,
            route,
            controllers,
            transceivers,
            requestId,
            delayMs);
        const auto dispatchStarted = Clock::now();
        const auto started = worker.StartLatest(std::move(request));
        dispatchTimings.push_back(ElapsedUs(dispatchStarted));
        require(started, "delayed worker dispatch failed");
        modules::route_sector::AuthorityWorkerFact fact;
        const auto harvested = WaitForFact(
            &worker,
            requestId,
            Milliseconds(delayMs + 5000),
            &fact,
            &harvestTimings);
        require(harvested, "delayed worker result did not complete");
        require(
            !harvested ||
                fact.workerElapsedUs >=
                    static_cast<long long>(delayMs) * 900,
            "forced worker delay was not exercised");
    };
    runDelayed(2, 500);
    runDelayed(3, 2000);

    auto runningRequest = BuildRequest(
        resolver,
        aircraft,
        route,
        controllers,
        transceivers,
        100,
        2000);
    const auto runningDispatchStarted = Clock::now();
    require(
        worker.StartLatest(std::move(runningRequest)),
        "latest-only running dispatch failed");
    dispatchTimings.push_back(ElapsedUs(runningDispatchStarted));
    require(
        WaitUntilRunning(&worker, Milliseconds(1000)),
        "latest-only running request did not start");
    for (std::uint64_t requestId = 101; requestId <= 356; ++requestId) {
        auto replacement = BuildRequest(
            resolver,
            aircraft,
            route,
            controllers,
            transceivers,
            requestId,
            requestId == 356 ? 0 : 2000);
        const auto dispatchStarted = Clock::now();
        require(
            worker.StartLatest(std::move(replacement)),
            "latest-only replacement dispatch failed");
        dispatchTimings.push_back(ElapsedUs(dispatchStarted));
    }
    modules::route_sector::AuthorityWorkerFact latestFact;
    require(
        WaitForFact(
            &worker,
            356,
            Milliseconds(5000),
            &latestFact,
            &harvestTimings),
        "latest-only queue did not publish the newest request");
    const auto queueSnapshot = worker.Snapshot();
    require(
        queueSnapshot.maximumPendingDepth == 1,
        "worker pending depth exceeded one");
    require(
        queueSnapshot.cancellations > 0,
        "superseded running work was not cooperatively cancelled");

    // Production bounds controller decoding at 5,000 records. Build that
    // immutable evidence package before timing, as the live fetch thread does,
    // then measure the entire main-thread identity/package/mailbox operation.
    constexpr std::size_t kWorstCaseControllerCount = 5000;
    std::vector<brain::ControllerSnapshot> worstCaseControllers;
    worstCaseControllers.reserve(kWorstCaseControllerCount);
    for (std::size_t index = 0; index < kWorstCaseControllerCount; ++index) {
        brain::ControllerSnapshot fixture;
        fixture.callsign = "K" + std::to_string(index) + "_CTR";
        fixture.frequency = "125.600";
        fixture.facility = 6;
        fixture.visualRangeNm = 400;
        fixture.textAtis.assign(2048, 'A');
        worstCaseControllers.push_back(std::move(fixture));
    }
    const auto worstCaseControllerContentDigest =
        brain::HashBrainAuthorityControllerEvidenceContent(
            worstCaseControllers);
    auto immutableWorstCaseControllers = std::make_shared<
        const std::vector<brain::AuthorityControllerSnapshot>>(
            brain::BuildBrainAuthorityControllerEvidence(
                worstCaseControllers));
    auto immutableWorstCaseRoute = std::make_shared<
        const brain::RouteSectorSnapshot>(enrichedRoute);
    auto worstCaseTransceiverValue =
        std::make_shared<brain::TransceiverResolutionSnapshot>();
    worstCaseTransceiverValue->available = true;
    worstCaseTransceiverValue->stale = false;
    worstCaseTransceiverValue->receivableControllers =
        static_cast<int>(kWorstCaseControllerCount);
    worstCaseTransceiverValue->candidates.reserve(kWorstCaseControllerCount);
    for (std::size_t index = 0; index < kWorstCaseControllerCount; ++index) {
        brain::ReceivableControllerSnapshot candidate;
        candidate.callsign = "K" + std::to_string(index) + "_CTR";
        candidate.frequency = "125.600";
        candidate.distanceNm = static_cast<double>(index % 300);
        worstCaseTransceiverValue->candidates.push_back(std::move(candidate));
    }
    auto immutableWorstCaseTransceivers = std::make_shared<
        const brain::AuthorityTransceiverEvidenceSnapshot>(
            brain::BuildBrainAuthorityTransceiverEvidence(
                *worstCaseTransceiverValue));
    const auto worstCaseRouteDigest =
        brain::HashBrainAuthorityRouteSnapshot(*immutableWorstCaseRoute);
    const auto worstCaseTransceiverDigest =
        brain::HashBrainAuthorityTransceiverEvidence(
            *immutableWorstCaseTransceivers);
    std::vector<long long> packageAndSubmitTimings;
    const auto packageAndSubmit = [&](std::uint64_t requestId,
                                      std::uint32_t delayMs) {
        const auto started = Clock::now();
        modules::route_sector::AuthorityWorkerRequest request;
        request.identity.requestId = requestId;
        request.identity.lifecycleEpoch = 8;
        request.identity.planKey = "KSEA->KDEN|worst-controller-package";
        request.identity.routeDigest = worstCaseRouteDigest;
        request.identity.controllerDigest =
            brain::HashBrainAuthorityControllerEvidenceFromContentDigest(
                worstCaseControllerContentDigest,
                true,
                false);
        request.identity.transceiverDigest = worstCaseTransceiverDigest;
        const auto datasetPublication =
            resolver.GetAuthoritySourceDatasetPublication();
        request.identity.datasetIdentity = datasetPublication.identity;
        request.aircraft = aircraft;
        request.route = immutableWorstCaseRoute;
        request.controllers = immutableWorstCaseControllers;
        request.hasControllerEvidence = true;
        request.controllerFeedAvailable = true;
        request.controllerFeedStale = false;
        request.controllerFeedGeneration = 10;
        request.controllerFeedConnectedControllers =
            static_cast<int>(kWorstCaseControllerCount);
        request.terminalBoundaryGeneration =
            datasetPublication.terminalBoundaryGeneration;
        request.scheduleReason = "controller-relevance-evidence-ledger";
        request.hasTransceiverEvidence = true;
        request.transceivers = immutableWorstCaseTransceivers;
        request.dataset = datasetPublication.dataset;
        request.dispatchedMonotonicMs = 1000;
        request.cooperativeDelayForTestingMs = delayMs;
        const auto submitted = worker.StartLatest(std::move(request));
        packageAndSubmitTimings.push_back(ElapsedUs(started));
        require(submitted, "worst-case package-and-submit dispatch failed");
    };

    packageAndSubmit(1000, 2000);
    require(
        WaitUntilRunning(&worker, Milliseconds(1000)),
        "worst-case package blocker did not start");
    for (std::uint64_t requestId = 1001; requestId <= 1256; ++requestId) {
        packageAndSubmit(requestId, requestId == 1256 ? 0 : 2000);
    }
    modules::route_sector::AuthorityWorkerFact packageFact;
    require(
        WaitForFact(
            &worker,
            1256,
            Milliseconds(10000),
            &packageFact,
            &harvestTimings),
        "worst-case package queue did not publish the newest request");
    const auto packageAndSubmitP99Us =
        Percentile99(packageAndSubmitTimings);
    require(
        packageAndSubmitP99Us < 250,
        "complete worst-case package-and-submit P99 exceeded 250 us");

    const auto retirementCountBefore =
        worker.Snapshot().workerThreadSnapshotRetirements;
    if (packageFact.snapshotLease != nullptr) {
        packageFact.snapshotLease->retired.store(
            true, std::memory_order_release);
        packageFact.snapshotLease.reset();
        worker.NotifyRetirement();
    }
    const auto retirementDeadline = Clock::now() + Milliseconds(1000);
    while (worker.Snapshot().workerThreadSnapshotRetirements <=
               retirementCountBefore &&
           Clock::now() < retirementDeadline) {
        std::this_thread::sleep_for(Milliseconds(1));
    }
    require(
        worker.Snapshot().workerThreadSnapshotRetirements >
            retirementCountBefore,
        "obsolete evidence lease was not retired on the worker thread");

    auto lifecycleRequest = BuildRequest(
        resolver,
        aircraft,
        route,
        controllers,
        transceivers,
        400,
        2000);
    require(
        worker.StartLatest(std::move(lifecycleRequest)),
        "lifecycle delay dispatch failed");
    require(
        WaitUntilRunning(&worker, Milliseconds(1000)),
        "lifecycle request did not start");
    const auto cancelStarted = Clock::now();
    worker.CancelPending();
    const auto cancelUs = ElapsedUs(cancelStarted);
    require(cancelUs < 500, "lifecycle cancellation blocked the caller");
    const auto idleDeadline = Clock::now() + Milliseconds(1000);
    while (worker.IsRunning() && Clock::now() < idleDeadline) {
        std::this_thread::sleep_for(Milliseconds(1));
    }
    require(!worker.IsRunning(), "cooperative lifecycle cancellation did not settle");
    modules::route_sector::AuthorityWorkerFact obsoleteFact;
    require(
        !worker.TryHarvest(&obsoleteFact),
        "cancelled lifecycle work remained publishable");

    const auto dispatchP99Us = Percentile99(dispatchTimings);
    const auto harvestP99Us = Percentile99(harvestTimings);
    require(dispatchP99Us < 250, "authority submission P99 exceeded 250 us");
    require(harvestP99Us < 500, "authority harvest P99 exceeded 500 us");
    for (auto* fact : {&parityFact, &latestFact}) {
        if (fact->snapshotLease != nullptr) {
            fact->snapshotLease->retired.store(
                true, std::memory_order_release);
            fact->snapshotLease.reset();
        }
    }
    worker.NotifyRetirement();
    worker.CancelAndJoin();
    require(!worker.IsRunning(), "authority worker thread did not join");

    if (!failures.empty()) {
        for (const auto& failure : failures) {
            std::cerr << "GATE_A_ASSERTION_FAILED: " << failure << "\n";
        }
        return 1;
    }

    const auto finalSnapshot = worker.Snapshot();
    std::cout << "PERFORMANCE_CONTRACT_GATE_A_PASSED"
              << " syncDigest=" << synchronousDigest
              << " dispatchP99Us=" << dispatchP99Us
              << " packageAndSubmitP99Us=" << packageAndSubmitP99Us
              << " worstCaseControllers=" << kWorstCaseControllerCount
              << " harvestP99Us=" << harvestP99Us
              << " maxPendingDepth=" << queueSnapshot.maximumPendingDepth
              << " starts=" << finalSnapshot.starts
              << " replacements=" << finalSnapshot.replacements
              << " completions=" << finalSnapshot.completions
              << " cancellations=" << finalSnapshot.cancellations
              << " workerSnapshotRetirements="
              << finalSnapshot.workerThreadSnapshotRetirements
              << " cancelUs=" << cancelUs
              << " forcedDelaysMs=500,2000"
              << " stalePublications=0"
              << "\n";
    return 0;
}

}  // namespace xvatsim::tools::performance_contract_gate_a
