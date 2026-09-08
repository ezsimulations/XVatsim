#include "PerformanceContractGateACalm2Probe.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "XVatsim/brain/BrainOwnedWorkerTypes.h"
#include "XVatsim/modules/route_sector/RouteSectorResolver.h"

namespace xvatsim::tools::performance_contract_gate_a_calm_2 {
namespace {

brain::ControllerSnapshot ControllerFixture() {
    brain::ControllerSnapshot controller;
    controller.callsign = "KZSE_CTR";
    controller.frequency = "125.600";
    controller.facility = 6;
    controller.visualRangeNm = 400;
    controller.actionable = true;
    controller.atis = false;
    controller.textAtis = "SEATTLE CENTER";
    return controller;
}

brain::TransceiverResolutionSnapshot TransceiverFixture() {
    brain::TransceiverResolutionSnapshot snapshot;
    snapshot.available = true;
    snapshot.stale = false;
    snapshot.receivableControllers = 1;
    snapshot.distanceRejectedControllers = 7;
    snapshot.maxCandidateDistanceNm = 350.0;
    snapshot.statusLine = "RX fixture age=1";
    snapshot.resolutionPath = "normal";
    snapshot.candidatesCompatibilityOnly = true;
    snapshot.droppedBeforeBrainControllers = 3;
    brain::ReceivableControllerSnapshot candidate;
    candidate.callsign = "KZSE_CTR";
    candidate.frequency = "125.600";
    candidate.distanceNm = 120.0;
    candidate.score = 80.0;
    candidate.latitudeDeg = 47.0;
    candidate.longitudeDeg = -121.0;
    snapshot.candidates.push_back(candidate);
    snapshot.sourceEvidence.feedCacheExists = true;
    snapshot.sourceEvidence.cacheFresh = true;
    snapshot.sourceEvidence.hasFeedAgeSeconds = true;
    snapshot.sourceEvidence.feedAgeSeconds = 1;
    snapshot.sourceEvidence.fetchAttempted = true;
    snapshot.sourceEvidence.parser.invalidPosition = 2;
    brain::TransceiverControllerEvidenceSnapshot evidence;
    evidence.callsign = candidate.callsign;
    evidence.controllerFrequency = candidate.frequency;
    evidence.facility = 6;
    evidence.actionable = true;
    evidence.hasTransceiverEntry = true;
    evidence.matchingTransceiverCount = 1;
    evidence.pathUnavailableReason = "diagnostic-fixture";
    snapshot.controllerEvidence.push_back(std::move(evidence));
    return snapshot;
}

std::uint64_t AuthorityTransceiverDigest(
    const brain::TransceiverResolutionSnapshot& snapshot) {
    return brain::HashBrainAuthorityTransceiverEvidence(
        brain::BuildBrainAuthorityTransceiverEvidence(snapshot));
}

std::string RelevantAuthoritySignature(
    const brain::AuthorityRelevanceSnapshot& snapshot) {
    std::ostringstream stream;
    stream << (snapshot.available ? 1 : 0) << ":"
           << (snapshot.stale ? 1 : 0);
    for (const auto& authority : snapshot.relevantAuthorities) {
        stream << "|" << authority.callsign
               << ":" << authority.frequency
               << ":" << authority.authorityId
               << ":" << authority.polygonKey
               << ":" << authority.proofSource
               << ":" << static_cast<int>(authority.kind)
               << ":" << (authority.aircraftInside ? 1 : 0)
               << ":" << (authority.routeIntersects ? 1 : 0);
    }
    return stream.str();
}

std::string ControllerDisplaySignature(
    const brain::AuthorityRelevanceSnapshot& authority,
    const brain::RouteSectorSnapshot& route) {
    brain::BrainControllerRelevanceWorkerInput input;
    input.workflowStage = brain::WorkflowStage::Enroute;
    input.radioBoardHash = 101;
    input.routePolygonHash = 202;
    input.currentPolygonIndex = 0;
    input.currentPolygonKey = "CZVR";
    input.currentSectors = route.currentSectors;
    input.nextSectors = route.nextSectors;
    input.departureIcao = "KAAA";
    input.arrivalIcao = "KBBB";
    input.authorityRelevanceHash =
        brain::HashBrainAuthorityRelevanceSnapshot(authority);
    input.authorityRelevance = std::make_shared<
        const brain::AuthorityRelevanceSnapshot>(authority);

    brain::RadioReachableControllerCandidate center;
    center.callsign = "DOG_POOP_CTR";
    center.frequency = "133.700";
    center.vatsimFacility = 6;
    center.group = brain::RadioReachableFacilityGroup::Center;
    center.source = brain::RadioReachableSource::TestHarness;
    center.actionable = true;
    center.hasStationCoordinates = true;
    center.stationLatitudeDeg = 49.0;
    center.stationLongitudeDeg = -123.0;
    center.stableKey = "DOG_POOP_CTR|133700";
    input.candidates.push_back(center);

    const auto output = brain::RunBrainControllerRelevanceWorker(input);
    std::ostringstream stream;
    stream << (output.available ? 1 : 0)
           << ":" << (output.stale ? 1 : 0)
           << ":" << output.reason;
    const auto appendBoard = [&](const brain::ModuleBoardSnapshot& board) {
        stream << "|B:" << static_cast<int>(board.source)
               << ":" << board.airportIcao;
        for (const auto& station : board.stations) {
            stream << ":" << station.callsign
                   << "," << station.frequency
                   << "," << static_cast<int>(station.role)
                   << "," << station.polygonKey
                   << "," << (station.sectorActive ? 1 : 0);
        }
    };
    appendBoard(output.departureBoard);
    appendBoard(output.enrouteBoard);
    appendBoard(output.arrivalBoard);
    for (const auto& completion : output.completions) {
        stream << "|C:" << completion.callsign
               << ":" << completion.frequency
               << ":" << static_cast<int>(completion.decision)
               << ":" << static_cast<int>(completion.displayRelation)
               << ":" << completion.matchedPolygonKey
               << ":" << completion.reason;
    }
    return stream.str();
}

struct ColdStartDeterminismResult {
    bool completed = false;
    std::uint64_t synchronousDigest = 0;
    std::uint64_t workerDigest = 0;
    std::string synchronousRelevant;
    std::string workerRelevant;
    std::string synchronousDisplay;
    std::string workerDisplay;
};

ColdStartDeterminismResult RunColdStartDeterminismVariant(
    const std::vector<brain::ControllerSnapshot>& controllers,
    const brain::TransceiverResolutionSnapshot& transceivers,
    const brain::AircraftStateSnapshot& aircraft,
    const brain::RouteSectorSnapshot& route) {
    ColdStartDeterminismResult result;
    modules::route_sector::RouteSectorResolver resolver;
    constexpr const char* kBoundary =
        R"json({"type":"FeatureCollection","features":[{"type":"Feature","properties":{"identifier":"CZVR","tokens":"CZVR"},"geometry":{"type":"Polygon","coordinates":[[[-126.0,48.0],[-120.0,48.0],[-120.0,51.0],[-126.0,51.0],[-126.0,48.0]]]}}]})json";
    constexpr const char* kEmptyGeoJson =
        R"json({"type":"FeatureCollection","features":[]})json";
    resolver.LoadBoundaryPayloadsForTesting(
        kBoundary,
        kEmptyGeoJson,
        "[FIR]\nCZVR|Vancouver|CZVR|CZVR\n",
        "{}");

    brain::ControllerFeedSnapshot feed;
    feed.available = true;
    feed.stale = false;
    feed.generation = 17;
    feed.connectedControllers = static_cast<int>(controllers.size());
    feed.controllers = &controllers;
    const auto synchronous =
        resolver.ResolveBrainScheduledAuthorityVerification(
            aircraft,
            feed,
            route,
            "controller-relevance-evidence-ledger",
            &transceivers);
    result.synchronousDigest =
        brain::HashBrainAuthorityRelevanceSnapshot(synchronous);
    result.synchronousRelevant = RelevantAuthoritySignature(synchronous);
    result.synchronousDisplay =
        ControllerDisplaySignature(synchronous, route);

    modules::route_sector::AuthorityWorkerRequest request;
    request.identity.requestId = 1;
    request.identity.lifecycleEpoch = 1;
    request.identity.planKey = "KAAA->KBBB|calm-2-determinism";
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
    request.route = std::make_shared<
        const brain::RouteSectorSnapshot>(route);
    request.controllers = std::make_shared<
        const std::vector<brain::AuthorityControllerSnapshot>>(
            brain::BuildBrainAuthorityControllerEvidence(controllers));
    request.hasControllerEvidence = true;
    request.controllerFeedAvailable = true;
    request.controllerFeedStale = false;
    request.controllerFeedGeneration = 17;
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

    modules::route_sector::AuthorityRelevanceWorker worker;
    if (!worker.Start() || !worker.StartLatest(std::move(request))) {
        return result;
    }
    modules::route_sector::AuthorityWorkerFact fact;
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (worker.TryHarvest(&fact)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (fact.completed && !fact.cancelled &&
        fact.snapshotLease != nullptr &&
        fact.snapshotLease->snapshot != nullptr) {
        const auto& workerSnapshot = *fact.snapshotLease->snapshot;
        result.workerDigest = fact.snapshotDigest;
        result.workerRelevant = RelevantAuthoritySignature(workerSnapshot);
        result.workerDisplay =
            ControllerDisplaySignature(workerSnapshot, route);
        result.completed = true;
        fact.snapshotLease->retired.store(true, std::memory_order_release);
        fact.snapshotLease.reset();
        worker.NotifyRetirement();
    }
    worker.CancelAndJoin();
    return result;
}

}  // namespace

int RunPerformanceContractGateACalm2Probe() {
    std::vector<std::string> failures;
    const auto require = [&](bool condition, std::string failure) {
        if (!condition) {
            failures.push_back(std::move(failure));
        }
    };

    const std::vector<brain::ControllerSnapshot> controllers = {
        ControllerFixture(),
        [] {
            auto controller = ControllerFixture();
            controller.callsign = "KPDX_APP";
            controller.frequency = "118.100";
            controller.facility = 5;
            controller.textAtis = "PORTLAND APPROACH";
            return controller;
        }(),
    };
    const auto controllerContentDigest =
        brain::HashBrainAuthorityControllerEvidenceContent(controllers);
    const auto controllerDigest =
        brain::HashBrainAuthorityControllerEvidenceFromContentDigest(
            controllerContentDigest,
            true,
            false);

    auto reorderedControllers = controllers;
    std::reverse(reorderedControllers.begin(), reorderedControllers.end());
    require(
        brain::HashBrainAuthorityControllerEvidenceContent(
            reorderedControllers) == controllerContentDigest,
        "controller input order changed semantic identity");
    auto visualRangeOnly = controllers;
    visualRangeOnly.front().visualRangeNm += 500;
    require(
        brain::HashBrainAuthorityControllerEvidenceContent(visualRangeOnly) ==
            controllerContentDigest,
        "radio-only visual range changed authority identity");
    require(
        brain::HashBrainAuthorityControllerEvidenceFromContentDigest(
            controllerContentDigest,
            true,
            false) == controllerDigest,
        "controller generation/count-free identity was unstable");

    const auto requireControllerChange = [&](auto mutation, const char* field) {
        auto changed = controllers;
        mutation(&changed.front());
        require(
            brain::HashBrainAuthorityControllerEvidenceContent(changed) !=
                controllerContentDigest,
            std::string("controller semantic identity ignored ") + field);
    };
    requireControllerChange(
        [](auto* value) { value->callsign = "KZLC_CTR"; },
        "callsign");
    requireControllerChange(
        [](auto* value) { value->frequency = "132.325"; },
        "frequency");
    requireControllerChange(
        [](auto* value) { value->facility = 5; },
        "facility");
    requireControllerChange(
        [](auto* value) { value->actionable = false; },
        "actionable state");
    requireControllerChange(
        [](auto* value) { value->atis = true; },
        "ATIS state");
    requireControllerChange(
        [](auto* value) { value->textAtis += " OWNERSHIP"; },
        "controller text");
    require(
        brain::HashBrainAuthorityControllerEvidenceFromContentDigest(
            controllerContentDigest,
            false,
            true) != controllerDigest,
        "controller availability/stale state did not invalidate");

    const auto transceivers = TransceiverFixture();
    const auto transceiverDigest = AuthorityTransceiverDigest(transceivers);
    auto volatileRefresh = transceivers;
    volatileRefresh.candidates.front().distanceNm += 25.0;
    volatileRefresh.candidates.front().score -= 25.0;
    volatileRefresh.distanceRejectedControllers += 4;
    volatileRefresh.statusLine = "RX fixture age=6";
    volatileRefresh.resolutionPath = "normal-refresh";
    volatileRefresh.sourceEvidence.feedAgeSeconds += 5;
    volatileRefresh.sourceEvidence.fetchInProgress = true;
    volatileRefresh.sourceEvidence.parser.invalidPosition += 10;
    volatileRefresh.controllerEvidence.front().pathUnavailableReason =
        "changed-radio-diagnostic";
    require(
        brain::HashBrainTransceiverEvidence(volatileRefresh) !=
            brain::HashBrainTransceiverEvidence(transceivers),
        "fixture did not exercise volatile legacy transceiver fields");
    require(
        AuthorityTransceiverDigest(volatileRefresh) == transceiverDigest,
        "volatile radio diagnostics changed authority identity");

    auto reorderedTransceivers = transceivers;
    auto secondCandidate = reorderedTransceivers.candidates.front();
    secondCandidate.callsign = "KPDX_APP";
    secondCandidate.frequency = "118.100";
    secondCandidate.latitudeDeg = 45.6;
    secondCandidate.longitudeDeg = -122.6;
    reorderedTransceivers.candidates.push_back(secondCandidate);
    reorderedTransceivers.receivableControllers = 2;
    const auto twoCandidateDigest =
        AuthorityTransceiverDigest(reorderedTransceivers);
    std::reverse(
        reorderedTransceivers.candidates.begin(),
        reorderedTransceivers.candidates.end());
    require(
        AuthorityTransceiverDigest(reorderedTransceivers) ==
            twoCandidateDigest,
        "transceiver candidate input order changed semantic identity");

    const auto requireTransceiverChange =
        [&](auto mutation, const char* field) {
            auto changed = transceivers;
            mutation(&changed);
            require(
                AuthorityTransceiverDigest(changed) != transceiverDigest,
                std::string("transceiver semantic identity ignored ") +
                    field);
        };
    requireTransceiverChange(
        [](auto* value) {
            value->candidates.push_back(value->candidates.front());
            value->candidates.back().callsign = "KPDX_APP";
            value->receivableControllers = 2;
        },
        "membership");
    requireTransceiverChange(
        [](auto* value) { value->candidates.front().callsign = "KZLC_CTR"; },
        "callsign");
    requireTransceiverChange(
        [](auto* value) { value->candidates.front().frequency = "132.325"; },
        "frequency");
    requireTransceiverChange(
        [](auto* value) { value->candidates.front().latitudeDeg += 0.1; },
        "latitude geometry");
    requireTransceiverChange(
        [](auto* value) { value->candidates.front().longitudeDeg += 0.1; },
        "longitude geometry");
    requireTransceiverChange(
        [](auto* value) { value->available = false; },
        "availability");
    requireTransceiverChange(
        [](auto* value) { value->stale = true; },
        "stale state");

    brain::BrainOwnedRuntimeState runtime;
    brain::BrainOwnedRadioBoardCommitInput initialCommit;
    initialCommit.nowSeconds = 100;
    initialCommit.controllerGeneration = 1;
    initialCommit.transceiverSnapshot = transceivers;
    initialCommit.radioSnapshot.stableHash = 10;
    (void)brain::CommitBrainOwnedRadioBoardRefresh(
        &runtime,
        std::move(initialCommit));
    const auto* initialProjection = runtime.authorityTransceiverEvidence.get();
    const auto initialObservation = runtime.transceiverObservationGeneration;

    brain::BrainOwnedRadioBoardCommitInput unchangedCommit;
    unchangedCommit.nowSeconds = 105;
    unchangedCommit.controllerGeneration = 2;
    unchangedCommit.transceiverSnapshot = volatileRefresh;
    unchangedCommit.radioSnapshot.stableHash = 11;
    (void)brain::CommitBrainOwnedRadioBoardRefresh(
        &runtime,
        std::move(unchangedCommit));
    require(
        runtime.authorityTransceiverEvidence.get() == initialProjection &&
            runtime.authorityTransceiverEvidenceDigest == transceiverDigest,
        "unchanged five-second refresh replaced semantic authority evidence");
    require(
        runtime.transceiverObservationGeneration == initialObservation + 1,
        "unchanged refresh observation was not counted");

    auto changedGeometry = transceivers;
    changedGeometry.candidates.front().latitudeDeg += 0.2;
    brain::BrainOwnedRadioBoardCommitInput changedCommit;
    changedCommit.nowSeconds = 110;
    changedCommit.controllerGeneration = 3;
    changedCommit.transceiverSnapshot = changedGeometry;
    changedCommit.radioSnapshot.stableHash = 12;
    (void)brain::CommitBrainOwnedRadioBoardRefresh(
        &runtime,
        std::move(changedCommit));
    require(
        runtime.authorityTransceiverEvidence.get() != initialProjection &&
            runtime.authorityTransceiverEvidenceDigest != transceiverDigest,
        "real transceiver geometry change retained obsolete projection");

    // The plugin's dispatch gate compares these semantic digests plus route,
    // dataset, plan, and lifecycle identity. Simulate twelve unchanged
    // five-second observations: none may create a changed-input request.
    std::uint64_t expectedControllerDigest = controllerDigest;
    std::uint64_t expectedTransceiverDigest = transceiverDigest;
    int changedInputRequests = 0;
    for (int refresh = 1; refresh <= 12; ++refresh) {
        auto radioRefresh = volatileRefresh;
        radioRefresh.sourceEvidence.feedAgeSeconds = 1 + refresh * 5;
        radioRefresh.candidates.front().distanceNm += refresh;
        radioRefresh.candidates.front().score -= refresh;
        const auto refreshedControllerDigest =
            brain::HashBrainAuthorityControllerEvidenceFromContentDigest(
                controllerContentDigest,
                true,
                false);
        const auto refreshedTransceiverDigest =
            AuthorityTransceiverDigest(radioRefresh);
        if (refreshedControllerDigest != expectedControllerDigest ||
            refreshedTransceiverDigest != expectedTransceiverDigest) {
            ++changedInputRequests;
            expectedControllerDigest = refreshedControllerDigest;
            expectedTransceiverDigest = refreshedTransceiverDigest;
        }
    }
    require(
        changedInputRequests == 0,
        "unchanged five-second radio observations scheduled authority work");

    brain::AircraftStateSnapshot authorityAircraft;
    authorityAircraft.valid = true;
    authorityAircraft.latitudeDeg = 49.0;
    authorityAircraft.longitudeDeg = -125.0;
    brain::RouteSectorSnapshot authorityRoute;
    authorityRoute.available = true;
    authorityRoute.stale = false;
    authorityRoute.routeResolved = true;
    authorityRoute.centerBoundaryGeneration = 1;
    authorityRoute.authorityCatalogGeneration = 1;
    authorityRoute.departureIcao = "KAAA";
    authorityRoute.destinationIcao = "KBBB";
    authorityRoute.waypoints = {
        {"START", 49.0, -125.0},
        {"END", 49.0, -121.0},
    };
    brain::RouteSectorMatchSnapshot currentSector;
    currentSector.identifier = "CZVR";
    currentSector.matchTokens = {"CZVR"};
    currentSector.controllerCallsignPatterns = {"CZVR"};
    currentSector.controllerPrefixes = {"CZVR"};
    currentSector.centerCoverage = true;
    authorityRoute.currentSectors.push_back(currentSector);

    std::vector<brain::ControllerSnapshot> determinismControllers;
    auto centerController = ControllerFixture();
    centerController.callsign = "DOG_POOP_CTR";
    centerController.frequency = "133.700";
    centerController.textAtis = "UNMATCHED CENTER";
    determinismControllers.push_back(centerController);
    determinismControllers.push_back(controllers.back());
    auto determinismTransceivers = TransceiverFixture();
    determinismTransceivers.candidates.front().callsign = "DOG_POOP_CTR";
    determinismTransceivers.candidates.front().frequency = "133.700";
    determinismTransceivers.candidates.front().score = 90.0;
    determinismTransceivers.candidates.front().latitudeDeg = 49.0;
    determinismTransceivers.candidates.front().longitudeDeg = -123.0;
    auto appCandidate = determinismTransceivers.candidates.front();
    appCandidate.callsign = "KPDX_APP";
    appCandidate.frequency = "118.100";
    appCandidate.score = 25.0;
    appCandidate.latitudeDeg = 45.6;
    appCandidate.longitudeDeg = -122.6;
    determinismTransceivers.candidates.push_back(appCandidate);
    determinismTransceivers.receivableControllers = 2;

    struct DeterminismVariant {
        const char* name = nullptr;
        std::vector<brain::ControllerSnapshot> controllers;
        brain::TransceiverResolutionSnapshot transceivers;
    };
    std::vector<DeterminismVariant> variants;
    variants.push_back(
        {"base", determinismControllers, determinismTransceivers});
    auto scoreOnly = determinismTransceivers;
    scoreOnly.candidates.front().score = -500.0;
    scoreOnly.candidates.back().score = 900.0;
    variants.push_back(
        {"score-only", determinismControllers, std::move(scoreOnly)});
    auto visualOnlyControllers = determinismControllers;
    visualOnlyControllers.front().visualRangeNm += 900;
    variants.push_back(
        {"visual-range-only",
         std::move(visualOnlyControllers),
         determinismTransceivers});
    auto controllerOrder = determinismControllers;
    std::reverse(controllerOrder.begin(), controllerOrder.end());
    variants.push_back(
        {"controller-order", std::move(controllerOrder), determinismTransceivers});
    auto transceiverOrder = determinismTransceivers;
    std::reverse(
        transceiverOrder.candidates.begin(),
        transceiverOrder.candidates.end());
    variants.push_back(
        {"transceiver-order",
         determinismControllers,
         std::move(transceiverOrder)});

    const auto baseControllerIdentity =
        brain::HashBrainAuthorityControllerEvidence(
            determinismControllers,
            true,
            false);
    const auto baseTransceiverIdentity =
        AuthorityTransceiverDigest(determinismTransceivers);
    std::vector<ColdStartDeterminismResult> determinismResults;
    for (const auto& variant : variants) {
        require(
            brain::HashBrainAuthorityControllerEvidence(
                variant.controllers,
                true,
                false) == baseControllerIdentity,
            std::string("cold-start controller identity changed for ") +
                variant.name);
        require(
            AuthorityTransceiverDigest(variant.transceivers) ==
                baseTransceiverIdentity,
            std::string("cold-start transceiver identity changed for ") +
                variant.name);
        determinismResults.push_back(
            RunColdStartDeterminismVariant(
                variant.controllers,
                variant.transceivers,
                authorityAircraft,
                authorityRoute));
    }
    const auto& baseDeterminism = determinismResults.front();
    require(
        baseDeterminism.completed &&
            baseDeterminism.synchronousDigest == baseDeterminism.workerDigest,
        "base cold-start synchronous/worker authority output diverged");
    require(
        baseDeterminism.synchronousRelevant.find("DOG_POOP_CTR") !=
            std::string::npos,
        "cold-start fixture did not produce a real relevant authority");
    require(
        baseDeterminism.synchronousDisplay.find("DOG_POOP_CTR") !=
            std::string::npos,
        "cold-start fixture did not exercise downstream controller display");
    for (std::size_t index = 0; index < variants.size(); ++index) {
        const auto& result = determinismResults[index];
        require(
            result.completed,
            std::string("cold-start worker did not complete for ") +
                variants[index].name);
        require(
            result.synchronousDigest == baseDeterminism.synchronousDigest &&
                result.workerDigest == baseDeterminism.workerDigest,
            std::string("equal semantic input changed full authority output for ") +
                variants[index].name);
        require(
            result.synchronousRelevant ==
                    baseDeterminism.synchronousRelevant &&
                result.workerRelevant == baseDeterminism.workerRelevant,
            std::string("equal semantic input changed relevant authorities for ") +
                variants[index].name);
        require(
            result.synchronousDisplay ==
                    baseDeterminism.synchronousDisplay &&
                result.workerDisplay == baseDeterminism.workerDisplay,
            std::string("equal semantic input changed controller display for ") +
                variants[index].name);
        require(
            result.synchronousDigest == result.workerDigest &&
                result.synchronousRelevant == result.workerRelevant &&
                result.synchronousDisplay == result.workerDisplay,
            std::string("fresh synchronous/worker paths diverged for ") +
                variants[index].name);
    }

    if (!failures.empty()) {
        for (const auto& failure : failures) {
            std::cerr << "GATE_A_CALM_2_FAILURE " << failure << "\n";
        }
        return 1;
    }

    std::cout
        << "GATE_A_CALM_2_PROBE unchangedFiveSecondRequests="
        << changedInputRequests
        << " controllerGenerationOnlyStable=1"
        << " volatileRadioStable=1"
        << " membershipInvalidates=1"
        << " geometryInvalidates=1"
        << " frequencyInvalidates=1"
        << " staleInvalidates=1"
        << " stableProjectionReused=1"
        << " coldStartVariants=" << variants.size()
        << " coldStartOutputDeterministic=1"
        << " downstreamDisplayDeterministic=1\n";
    std::cout << "PERFORMANCE_CONTRACT_GATE_A_CALM_2_PASSED\n";
    return 0;
}

}  // namespace xvatsim::tools::performance_contract_gate_a_calm_2
