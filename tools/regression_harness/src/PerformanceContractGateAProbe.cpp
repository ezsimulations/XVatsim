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
    std::uint32_t delayMs,
    bool controllerFeedComplete = true) {
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
    (void)transceivers;
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
    request.controllerFeedComplete = controllerFeedComplete;
    request.controllerFeedGeneration = 9;
    request.controllerFeedConnectedControllers =
        static_cast<int>(controllers.size());
    request.terminalBoundaryGeneration =
        resolver.GetTerminalBoundaryGeneration();
    request.scheduleReason = "controller-relevance-evidence-ledger";
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
        [](auto* value) { ++value->dynamicOwnershipDigest; },
        "dynamic-ownership-digest-mismatch");
    requireIdentityReject(
        [](auto* value) { ++value->datasetIdentity; },
        "authority-dataset-identity-mismatch");
    auto incompleteCompletion = validation;
    incompleteCompletion.controllerFeedComplete = false;
    require(
        brain::DecideBrainAuthorityCompletion(incompleteCompletion).reason ==
            "authority-controller-feed-unusable",
        "incomplete controller feed completion was publishable");

    auto continuityCurrent = validation.expected;
    ++continuityCurrent.requestId;
    const auto stableContinuity =
        brain::DecideBrainAuthorityPublicationContinuity(
            {validation.expected, continuityCurrent, true, false, true});
    require(
        stableContinuity.mayReuseLastAccepted &&
            !stableContinuity.evidenceRefreshPending,
        "stable authority proof continuity was rejected");
    ++continuityCurrent.controllerDigest;
    const auto controllerRefreshContinuity =
        brain::DecideBrainAuthorityPublicationContinuity(
            {validation.expected, continuityCurrent, true, false, true});
    require(
        controllerRefreshContinuity.mayReuseLastAccepted &&
            controllerRefreshContinuity.evidenceRefreshPending &&
            controllerRefreshContinuity.controllerRefreshPending &&
            !controllerRefreshContinuity.routeRefreshPending &&
            !controllerRefreshContinuity.dynamicOwnershipRefreshPending,
        "controller-only refresh erased the last proven authority result");
    auto routeRefreshCurrent = validation.expected;
    ++routeRefreshCurrent.routeDigest;
    const auto routeRefreshContinuity =
        brain::DecideBrainAuthorityPublicationContinuity(
            {validation.expected, routeRefreshCurrent, true, false, true});
    require(
        routeRefreshContinuity.mayReuseLastAccepted &&
            routeRefreshContinuity.evidenceRefreshPending &&
            routeRefreshContinuity.routeRefreshPending &&
            !routeRefreshContinuity.controllerRefreshPending &&
            !routeRefreshContinuity.dynamicOwnershipRefreshPending,
        "GTI1098 route refresh erased the proven SDF terminal authority");
    auto ownershipRefreshCurrent = validation.expected;
    ++ownershipRefreshCurrent.dynamicOwnershipDigest;
    const auto ownershipRefreshContinuity =
        brain::DecideBrainAuthorityPublicationContinuity(
            {validation.expected, ownershipRefreshCurrent, true, false, true});
    require(
        ownershipRefreshContinuity.mayReuseLastAccepted &&
            ownershipRefreshContinuity.evidenceRefreshPending &&
            ownershipRefreshContinuity.dynamicOwnershipRefreshPending &&
            !ownershipRefreshContinuity.controllerRefreshPending &&
            !ownershipRefreshContinuity.routeRefreshPending,
        "GTI1098 vNAS churn erased the proven SDF terminal authority");
    auto combinedRefreshCurrent = validation.expected;
    ++combinedRefreshCurrent.controllerDigest;
    ++combinedRefreshCurrent.routeDigest;
    ++combinedRefreshCurrent.dynamicOwnershipDigest;
    const auto combinedRefreshContinuity =
        brain::DecideBrainAuthorityPublicationContinuity(
            {validation.expected, combinedRefreshCurrent, true, false, true});
    require(
        combinedRefreshContinuity.mayReuseLastAccepted &&
            combinedRefreshContinuity.evidenceRefreshPending &&
            combinedRefreshContinuity.controllerRefreshPending &&
            combinedRefreshContinuity.routeRefreshPending &&
            combinedRefreshContinuity.dynamicOwnershipRefreshPending,
        "GTI1098 combined refresh published an unavailable authority gap");
    const auto requireContinuityReject =
        [&](auto mutate, const char* expectedReason) {
            auto changed = validation.expected;
            mutate(&changed);
            const auto decision =
                brain::DecideBrainAuthorityPublicationContinuity(
                    {validation.expected, changed, true, false, true});
            require(
                !decision.mayReuseLastAccepted &&
                    decision.reason == expectedReason,
                std::string("authority continuity did not reject ") +
                    expectedReason);
        };
    requireContinuityReject(
        [](auto* value) { ++value->lifecycleEpoch; },
        "authority-lifecycle-changed");
    requireContinuityReject(
        [](auto* value) { value->planKey += "-changed"; },
        "authority-plan-changed");
    requireContinuityReject(
        [](auto* value) { ++value->datasetIdentity; },
        "authority-dataset-changed");
    requireContinuityReject(
        [](auto* value) { value->routeDigest = 0; },
        "authority-route-identity-unavailable");
    const auto unavailableFeedContinuity =
        brain::DecideBrainAuthorityPublicationContinuity(
            {validation.expected, continuityCurrent, false, true, false});
    require(
        !unavailableFeedContinuity.mayReuseLastAccepted &&
            unavailableFeedContinuity.reason ==
                "authority-controller-feed-unusable",
        "unusable controller feed retained old authority proof");
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
    auto atisOnlyControllers = controllers;
    atisOnlyControllers.front().textAtis = "DESCRIPTIVE TEXT ONLY";
    require(
        brain::HashBrainAuthorityControllerEvidenceContent(controllers) ==
            brain::HashBrainAuthorityControllerEvidenceContent(
                atisOnlyControllers),
        "ATIS descriptive content changed authority-semantic identity");
    auto atisEligibilityControllers = controllers;
    atisEligibilityControllers.front().atis = true;
    require(
        brain::HashBrainAuthorityControllerEvidenceContent(controllers) !=
            brain::HashBrainAuthorityControllerEvidenceContent(
                atisEligibilityControllers),
        "ATIS eligibility state did not change authority-semantic identity");
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
            "controller-relevance-evidence-ledger");
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

    // A baseline continues to completion while one newest roster waits. The
    // follow-up verifies only the two additions and reuses all ten unchanged
    // controller facts from the worker's non-authoritative mechanical cache.
    std::vector<brain::ControllerSnapshot> baselineControllers;
    for (int index = 0; index < 10; ++index) {
        brain::ControllerSnapshot value;
        value.callsign = "DELTA" + std::to_string(index) + "_CTR";
        value.frequency = "12" + std::to_string(index) + ".600";
        value.facility = 6;
        baselineControllers.push_back(std::move(value));
    }
    auto expandedControllers = baselineControllers;
    for (int index = 10; index < 12; ++index) {
        brain::ControllerSnapshot value;
        value.callsign = "DELTA" + std::to_string(index) + "_CTR";
        value.frequency = "13" + std::to_string(index - 10) + ".600";
        value.facility = 6;
        expandedControllers.push_back(std::move(value));
    }
    auto baselineRequest = BuildRequest(
        resolver,
        aircraft,
        route,
        baselineControllers,
        transceivers,
        20,
        500);
    require(
        worker.StartLatest(std::move(baselineRequest)),
        "ten-controller baseline dispatch failed");
    require(
        WaitUntilRunning(&worker, Milliseconds(1000)),
        "ten-controller baseline did not start");
    auto additionRequest = BuildRequest(
        resolver,
        aircraft,
        route,
        expandedControllers,
        transceivers,
        21,
        0);
    require(
        worker.StartLatest(std::move(additionRequest)),
        "two-controller delta dispatch failed");
    modules::route_sector::AuthorityWorkerFact baselineFact;
    modules::route_sector::AuthorityWorkerFact additionFact;
    require(
        WaitForFact(
            &worker,
            20,
            Milliseconds(5000),
            &baselineFact,
            &harvestTimings),
        "ten-controller baseline was cancelled or lost");
    require(
        baselineFact.completed && !baselineFact.cancelled &&
            baselineFact.snapshotLease != nullptr &&
            baselineFact.snapshotLease->snapshot != nullptr &&
            baselineFact.snapshotLease->snapshot->liveMap
                    .verifiedControllerFactCount == 10 &&
            baselineFact.snapshotLease->snapshot->liveMap
                    .reusedControllerFactCount == 0,
        "ten-controller baseline did not verify exactly ten records");
    require(
        WaitForFact(
            &worker,
            21,
            Milliseconds(5000),
            &additionFact,
            &harvestTimings),
        "two-controller follow-up did not complete");
    require(
        additionFact.completed && !additionFact.cancelled &&
            additionFact.snapshotLease != nullptr &&
            additionFact.snapshotLease->snapshot != nullptr &&
            additionFact.snapshotLease->snapshot->liveMap
                    .verifiedControllerFactCount == 2 &&
            additionFact.snapshotLease->snapshot->liveMap
                    .reusedControllerFactCount == 10,
        "ten-plus-two follow-up did not limit verification to two additions");

    std::vector<modules::route_sector::AuthorityWorkerFact>
        incrementalAuditFacts;
    const auto runRoster = [&](const auto& roster,
                               std::uint64_t requestId,
                               bool complete) {
        auto request = BuildRequest(
            resolver,
            aircraft,
            route,
            roster,
            transceivers,
            requestId,
            0,
            complete);
        require(
            worker.StartLatest(std::move(request)),
            "incremental audit dispatch failed");
        modules::route_sector::AuthorityWorkerFact fact;
        require(
            WaitForFact(
                &worker,
                requestId,
                Milliseconds(5000),
                &fact,
                &harvestTimings),
            "incremental audit result did not complete");
        incrementalAuditFacts.push_back(std::move(fact));
        return incrementalAuditFacts.back().snapshotLease != nullptr
            ? incrementalAuditFacts.back().snapshotLease->snapshot
            : std::shared_ptr<
                  const brain::AuthorityRelevanceSnapshot>{};
    };

    auto reorderedControllers = expandedControllers;
    std::reverse(reorderedControllers.begin(), reorderedControllers.end());
    const auto reorderedSnapshot = runRoster(reorderedControllers, 22, true);
    require(
        reorderedSnapshot != nullptr &&
            reorderedSnapshot->liveMap.verifiedControllerFactCount == 0 &&
            reorderedSnapshot->liveMap.reusedControllerFactCount == 12 &&
            reorderedSnapshot->liveMap.removedControllerFactCount == 0,
        "source reordering re-verified unchanged controller facts");

    auto descriptiveAtisControllers = expandedControllers;
    descriptiveAtisControllers.front().textAtis =
        "DESCRIPTIVE SENTENCE CHANGED";
    const auto descriptiveAtisSnapshot =
        runRoster(descriptiveAtisControllers, 23, true);
    require(
        descriptiveAtisSnapshot != nullptr &&
            descriptiveAtisSnapshot->liveMap
                    .verifiedControllerFactCount == 0 &&
            descriptiveAtisSnapshot->liveMap.reusedControllerFactCount == 12,
        "descriptive ATIS churn re-verified authority facts");

    auto incompleteRoster = expandedControllers;
    incompleteRoster.pop_back();
    const auto incompleteSnapshot = runRoster(incompleteRoster, 24, false);
    require(
        incompleteSnapshot != nullptr &&
            incompleteSnapshot->liveMap.removedControllerFactCount == 0 &&
            incompleteSnapshot->liveMap.verifiedControllerFactCount == 0 &&
            incompleteSnapshot->liveMap.reusedControllerFactCount == 11,
        "incomplete roster inferred a controller removal");
    const auto restoredSnapshot = runRoster(expandedControllers, 25, true);
    require(
        restoredSnapshot != nullptr &&
            restoredSnapshot->liveMap.verifiedControllerFactCount == 0 &&
            restoredSnapshot->liveMap.reusedControllerFactCount == 12 &&
            restoredSnapshot->liveMap.removedControllerFactCount == 0,
        "incomplete roster discarded a retained controller fact");

    auto removedRoster = expandedControllers;
    removedRoster.pop_back();
    const auto removedSnapshot = runRoster(removedRoster, 26, true);
    require(
        removedSnapshot != nullptr &&
            removedSnapshot->liveMap.verifiedControllerFactCount == 0 &&
            removedSnapshot->liveMap.reusedControllerFactCount == 11 &&
            removedSnapshot->liveMap.removedControllerFactCount == 1,
        "complete roster did not remove exactly one signed-off controller");

    auto frequencyChangedRoster = removedRoster;
    frequencyChangedRoster.front().frequency = "134.775";
    const auto frequencyChangedSnapshot =
        runRoster(frequencyChangedRoster, 27, true);
    require(
        frequencyChangedSnapshot != nullptr &&
            frequencyChangedSnapshot->liveMap
                    .verifiedControllerFactCount == 1 &&
            frequencyChangedSnapshot->liveMap.reusedControllerFactCount == 10,
        "frequency change did not verify exactly one controller fact");

    auto facilityChangedRoster = frequencyChangedRoster;
    facilityChangedRoster[1].facility = 5;
    const auto facilityChangedSnapshot =
        runRoster(facilityChangedRoster, 28, true);
    require(
        facilityChangedSnapshot != nullptr &&
            facilityChangedSnapshot->liveMap
                    .verifiedControllerFactCount == 1 &&
            facilityChangedSnapshot->liveMap.reusedControllerFactCount == 10,
        "facility change did not verify exactly one controller fact");
    const auto unchangedSnapshot =
        runRoster(facilityChangedRoster, 29, true);
    require(
        unchangedSnapshot != nullptr &&
            unchangedSnapshot->liveMap.verifiedControllerFactCount == 0 &&
            unchangedSnapshot->liveMap.reusedControllerFactCount == 11,
        "unchanged generation performed authority verification");
    const auto auditCounts = [](const auto& snapshot) {
        if (snapshot == nullptr) {
            return std::string("missing");
        }
        return std::to_string(
                   snapshot->liveMap.verifiedControllerFactCount) + "/" +
               std::to_string(
                   snapshot->liveMap.reusedControllerFactCount) + "/" +
               std::to_string(
                   snapshot->liveMap.removedControllerFactCount);
    };
    std::cout << "INCREMENTAL_AUTHORITY_AUDIT"
              << " reorder=" << auditCounts(reorderedSnapshot)
              << " atisText=" << auditCounts(descriptiveAtisSnapshot)
              << " incomplete=" << auditCounts(incompleteSnapshot)
              << " restored=" << auditCounts(restoredSnapshot)
              << " removed=" << auditCounts(removedSnapshot)
              << " frequency=" << auditCounts(frequencyChangedSnapshot)
              << " facility=" << auditCounts(facilityChangedSnapshot)
              << " unchanged=" << auditCounts(unchangedSnapshot)
              << " format=verified/reused/removed\n";

    auto randomizedRoster = facilityChangedRoster;
    int randomizedParityChecks = 0;
    for (int iteration = 0; iteration < 20; ++iteration) {
        switch (iteration % 4) {
            case 0: {
                brain::ControllerSnapshot added;
                added.callsign =
                    "RANDOM" + std::to_string(iteration) + "_CTR";
                added.frequency =
                    "127." + std::to_string(700 + iteration);
                added.facility = 6;
                randomizedRoster.push_back(std::move(added));
                break;
            }
            case 1:
                if (randomizedRoster.size() > 4) {
                    randomizedRoster.erase(randomizedRoster.begin() + 2);
                }
                break;
            case 2:
                randomizedRoster.front().frequency =
                    iteration % 8 == 2 ? "128.100" : "129.100";
                break;
            case 3:
                std::rotate(
                    randomizedRoster.begin(),
                    randomizedRoster.begin() + 1,
                    randomizedRoster.end());
                break;
        }
        const auto incrementalSnapshot = runRoster(
            randomizedRoster,
            static_cast<std::uint64_t>(30 + iteration),
            true);
        modules::route_sector::RouteSectorResolver oracleResolver;
        oracleResolver.LoadBoundaryPayloadsForTesting(
            kEmptyGeoJson,
            kEmptyGeoJson,
            "[FIR]\nTEST|TEST|TEST|\n",
            "{}");
        brain::ControllerFeedSnapshot oracleFeed;
        oracleFeed.available = true;
        oracleFeed.stale = false;
        oracleFeed.controllerComponentComplete = true;
        oracleFeed.generation = 9;
        oracleFeed.connectedControllers =
            static_cast<int>(randomizedRoster.size());
        oracleFeed.controllers = &randomizedRoster;
        const auto oracleSnapshot =
            oracleResolver.ResolveBrainScheduledAuthorityVerification(
                aircraft,
                oracleFeed,
                route,
                "controller-relevance-evidence-ledger");
        const auto parity =
            incrementalSnapshot != nullptr &&
            incrementalSnapshot->liveMap.brainDecisionsApplied &&
            oracleSnapshot.liveMap.brainDecisionsApplied &&
            incrementalSnapshot->liveMap.stableHash ==
                oracleSnapshot.liveMap.stableHash &&
            incrementalSnapshot->liveMap.assignments.size() ==
                oracleSnapshot.liveMap.assignments.size() &&
            incrementalSnapshot->liveMap.conflictedPolygonIds ==
                oracleSnapshot.liveMap.conflictedPolygonIds &&
            incrementalSnapshot->liveMap.unmappedControllerCallsigns ==
                oracleSnapshot.liveMap.unmappedControllerCallsigns;
        require(parity, "randomized incremental map diverged from clean oracle");
        if (parity) {
            ++randomizedParityChecks;
        }
    }
    std::cout << "INCREMENTAL_AUTHORITY_ORACLE_PARITY checks="
              << randomizedParityChecks << "/20\n";

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
    modules::route_sector::AuthorityWorkerFact runningFact;
    modules::route_sector::AuthorityWorkerFact latestFact;
    require(
        WaitForFact(
            &worker,
            100,
            Milliseconds(5000),
            &runningFact,
            &harvestTimings),
        "running authority work was cancelled by ordinary roster churn");
    require(
        runningFact.completed && !runningFact.cancelled,
        "ordinary roster churn did not preserve the running completion");
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
        queueSnapshot.cancellations == 0,
        "ordinary roster churn cancelled active authority work");

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
    const auto worstCaseRouteDigest =
        brain::HashBrainAuthorityRouteSnapshot(*immutableWorstCaseRoute);
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
        const auto datasetPublication =
            resolver.GetAuthoritySourceDatasetPublication();
        request.identity.datasetIdentity = datasetPublication.identity;
        request.aircraft = aircraft;
        request.route = immutableWorstCaseRoute;
        request.controllers = immutableWorstCaseControllers;
        request.hasControllerEvidence = true;
        request.controllerFeedAvailable = true;
        request.controllerFeedStale = false;
        request.controllerFeedComplete = true;
        request.controllerFeedGeneration = 10;
        request.controllerFeedConnectedControllers =
            static_cast<int>(kWorstCaseControllerCount);
        request.terminalBoundaryGeneration =
            datasetPublication.terminalBoundaryGeneration;
        request.scheduleReason = "controller-relevance-evidence-ledger";
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
    modules::route_sector::AuthorityWorkerFact packageBaselineFact;
    modules::route_sector::AuthorityWorkerFact packageFact;
    require(
        WaitForFact(
            &worker,
            1000,
            Milliseconds(15000),
            &packageBaselineFact,
            &harvestTimings),
        "worst-case package baseline did not finish before its delta");
    require(
        packageBaselineFact.completed && !packageBaselineFact.cancelled,
        "worst-case baseline was cancelled by ordinary churn");
    require(
        WaitForFact(
            &worker,
            1256,
            Milliseconds(15000),
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
    for (auto* fact : {
             &parityFact,
             &baselineFact,
             &additionFact,
             &runningFact,
             &latestFact,
             &packageBaselineFact}) {
        if (fact->snapshotLease != nullptr) {
            fact->snapshotLease->retired.store(
                true, std::memory_order_release);
            fact->snapshotLease.reset();
        }
    }
    for (auto& fact : incrementalAuditFacts) {
        if (fact.snapshotLease != nullptr) {
            fact.snapshotLease->retired.store(
                true, std::memory_order_release);
            fact.snapshotLease.reset();
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
              << " authorityContinuityRegression=GTI1098_SDF_D_APP"
              << "\n";
    return 0;
}

}  // namespace xvatsim::tools::performance_contract_gate_a
