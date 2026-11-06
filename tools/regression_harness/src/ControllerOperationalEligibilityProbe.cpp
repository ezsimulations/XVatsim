#include "ControllerOperationalEligibilityProbe.h"

#include "XVatsim/brain/BrainControllerDecision.h"
#include "XVatsim/brain/BrainOwnedRuntime.h"
#include "XVatsim/brain/BrainOwnedWorkerTypes.h"
#include "XVatsim/brain/BrainXPilot4BridgeTypes.h"
#include "XVatsim/brain/BrainWorkflow.h"
#include "XVatsim/modules/route_sector/RouteSectorResolver.h"
#include "XVatsim/modules/vatsim_data_feed/VatsimDataFeedClient.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

namespace xvatsim::tools::controller_operational_eligibility {
namespace {

using namespace xvatsim::brain;

const BrainOwnedCandidateCompletion* FindCompletion(
    const BrainControllerRelevanceWorkerOutput& output,
    const std::string& callsign) {
    const auto found = std::find_if(
        output.completions.begin(),
        output.completions.end(),
        [&](const auto& completion) {
            return completion.callsign == callsign;
        });
    return found == output.completions.end() ? nullptr : &*found;
}

BrainControllerRelevanceWorkerInput BuildClevelandInput(
    RadioReachableControllerCandidate candidate) {
    BrainControllerRelevanceWorkerInput input;
    input.workflowStage = WorkflowStage::Arrival;
    input.radioBoardHash = 1001;
    input.routePolygonHash = 2002;
    input.currentPolygonIndex = 2;
    input.currentPolygonKey = "KZID";
    input.arrivalPolygonKey = "KZOB";
    input.departureIcao = "KLAS";
    input.arrivalIcao = "KCLE";
    input.hasArrivalCoordinates = true;
    input.arrivalLatitudeDeg = 41.4117;
    input.arrivalLongitudeDeg = -81.8498;
    input.terminalTransmitterRadiusNm = 5.0;
    input.arrivalEndpointPolygons.available = true;
    input.arrivalEndpointPolygons.resolved = true;
    input.arrivalEndpointPolygons.stale = false;
    input.arrivalEndpointPolygons.airportIcao = "KCLE";
    input.arrivalEndpointPolygons.ownerTokens = {"CLE_APP"};
    input.arrivalEndpointPolygons.source = "probe-terminal-owner";
    BrainEndpointPolygonFact clePolygon;
    clePolygon.canonicalPolygonId = "PROBE:TERMINAL:CLE";
    clePolygon.polygonKey = "CLE";
    clePolygon.name = "Cleveland Approach";
    clePolygon.serviceSuffixes = {"APP"};
    clePolygon.controllerPrefixes = {"CLE"};
    clePolygon.containsAirport = true;
    clePolygon.source = "probe-canonical-map";
    input.arrivalEndpointPolygons.polygons.push_back(std::move(clePolygon));

    auto authority = std::make_shared<AuthorityRelevanceSnapshot>();
    authority->available = true;
    authority->stale = false;
    authority->liveMap.available = true;
    authority->liveMap.stale = false;
    authority->liveMap.brainDecisionsApplied = true;
    authority->liveMap.canonicalMapIdentity = 1;
    LiveAuthorityAssignmentSnapshot assignment;
    assignment.state = LiveAuthorityAssignmentState::ActiveExact;
    assignment.canonicalPolygonId = "PROBE:TERMINAL:CLE";
    assignment.polygonKey = "CLE";
    assignment.positionId = "CLE_APP";
    assignment.callsign = candidate.callsign;
    assignment.frequency = candidate.frequency;
    assignment.facility = candidate.vatsimFacility;
    assignment.authoritySource = "probe-canonical-map";
    assignment.kind = AuthorityRelevanceKind::Terminal;
    authority->liveMap.assignments.push_back(std::move(assignment));
    input.authorityRelevance = std::move(authority);

    auto route = std::make_shared<RouteSectorSnapshot>();
    route->available = true;
    route->stale = false;
    route->routeResolved = true;
    route->departureIcao = "KLAS";
    route->destinationIcao = "KCLE";
    route->airportCallsignAliases.push_back(
        {"KCLE", "CLE", "KZOB", "VATSPY_AIRPORT", "KCLE|CLE|KZOB"});
    input.route = route;

    auto xpilot = std::make_shared<BrainXPilot4ControllerSnapshot>();
    xpilot->sourceAvailable = true;
    xpilot->networkConnected = true;
    xpilot->complete = true;
    xpilot->processEpoch = 117473758123352112ULL;
    xpilot->connectionEpoch = 1;
    xpilot->generation = 93;
    BrainXPilot4ControllerFact xpilotController;
    xpilotController.callsign = candidate.callsign;
    xpilotController.frequencyHz = 120'500'000;
    xpilotController.hasFrequency = true;
    xpilot->controllers.push_back(std::move(xpilotController));
    input.xpilot4Controllers = xpilot;

    input.candidates.push_back(std::move(candidate));
    return input;
}

std::string BuildSyntheticControllerDocument(std::size_t count) {
    std::ostringstream payload;
    payload << "{\"controllers\":[";
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0) payload << ',';
        payload << "{\"callsign\":\"CLE_" << std::setw(3) << std::setfill('0')
                << index << "_APP\",\"frequency\":\"120."
                << std::setw(3) << std::setfill('0') << index
                << "\",\"facility\":5,\"visual_range\":150}";
    }
    payload << "],\"pilots\":[]}";
    return payload.str();
}

struct BatchResult {
    std::size_t decoded = 0;
    std::size_t evaluated = 0;
    std::uint64_t radioHash = 0;
    long long decodeUs = 0;
    long long radioBuildUs = 0;
    long long brainUs = 0;
    BrainControllerRelevanceWorkerInput input;
};

BatchResult RunSyntheticProductionBatch(const std::string& document) {
    BatchResult result;
    const auto decodeStart = std::chrono::steady_clock::now();
    const auto decoded =
        xvatsim::modules::vatsim_data_feed::DecodeVatsimDataFeedDocument(
            document);
    const auto decodeEnd = std::chrono::steady_clock::now();
    result.decodeUs = std::chrono::duration_cast<std::chrono::microseconds>(
        decodeEnd - decodeStart).count();
    result.decoded = decoded.controllers.size();

    ControllerFeedSnapshot feed;
    feed.available = decoded.hasCache && !decoded.stale;
    feed.stale = decoded.stale;
    feed.connectedControllers = decoded.connectedControllers;
    feed.controllerComponentPresent = decoded.controllerComponentPresent;
    feed.controllerComponentComplete = decoded.controllerComponentComplete;
    feed.controllerComponentTruncated = decoded.controllerComponentTruncated;
    feed.retainedControllerRecords = decoded.retainedControllerRecords;
    feed.rejectedControllerRecords = decoded.rejectedControllerRecords;
    feed.controllerContentDigest = decoded.controllerContentDigest;
    feed.ownedControllers = decoded.immutableControllers;
    feed.controllers = decoded.immutableControllers.get();

    TransceiverResolutionSnapshot transceivers;
    transceivers.available = true;
    transceivers.stale = false;
    transceivers.candidatesCompatibilityOnly = true;
    transceivers.controllerEvidence.reserve(decoded.controllers.size());
    for (const auto& controller : decoded.controllers) {
        TransceiverControllerEvidenceSnapshot evidence;
        evidence.callsign = controller.callsign;
        evidence.controllerFrequency = controller.frequency;
        evidence.facility = controller.facility;
        evidence.actionable = controller.actionable;
        evidence.atis = controller.atis;
        evidence.hasTransceiverEntry = true;
        evidence.matchingTransceiverCount = 1;
        evidence.resolvedDisplayFrequency = controller.frequency;
        evidence.displayFrequencySource = "controller";
        TransceiverStationEvidenceSnapshot station;
        station.sourceFrequency = controller.frequency;
        station.latitudeDeg = 41.4117;
        station.longitudeDeg = -81.8498;
        station.hasAircraftDistance = true;
        station.aircraftDistanceNm = 1.0;
        station.withinMaxCandidateDistance = true;
        station.hasReceivableRange = true;
        station.receivableRangeNm = 150.0;
        station.withinReceivableRange = true;
        station.score = 149.0;
        station.bestByModuleScore = true;
        evidence.stations.push_back(std::move(station));
        transceivers.controllerEvidence.push_back(std::move(evidence));
    }

    BrainRadioRangeWorkerInput radioInput;
    radioInput.controllerFeed = feed;
    radioInput.planKey = "performance-probe";
    const auto radioBuildStart = std::chrono::steady_clock::now();
    const auto radio = BuildBrainRadioRangeWorkerOutput(
        radioInput, transceivers, 1.0);
    const auto radioBuildEnd = std::chrono::steady_clock::now();
    result.radioBuildUs =
        std::chrono::duration_cast<std::chrono::microseconds>(
            radioBuildEnd - radioBuildStart).count();
    result.brainUs = result.radioBuildUs;
    result.evaluated = radio.decisionPreview.decisions.size();
    if (!radio.radioBoard.candidates.empty()) {
        result.input = BuildClevelandInput(radio.radioBoard.candidates.front());
        result.input.candidates = radio.radioBoard.candidates;
        result.input.radioBoardHash = radio.radioBoard.stableHash;
    }
    result.radioHash = radio.radioBoard.stableHash;
    return result;
}

std::uint64_t WorkingSetBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(
            GetCurrentProcess(), &counters, sizeof(counters)) != FALSE) {
        return static_cast<std::uint64_t>(counters.WorkingSetSize);
    }
#endif
    return 0;
}

}  // namespace

int RunControllerOperationalEligibilityProbe() {
    const auto decoded =
        xvatsim::modules::vatsim_data_feed::DecodeVatsimDataFeedDocument(R"json({
          "controllers": [
            {"callsign":"TEST_OBS","frequency":"199.998","facility":0,"visual_range":50},
            {"callsign":"TEST_SUP","frequency":"199.998","facility":0,"visual_range":50},
            {"callsign":"CLT_APP","frequency":"199.998","facility":5,"visual_range":150}
          ],
          "pilots": []
        })json");
    const auto obsSupRetained =
        decoded.controllers.size() == 3 &&
        std::any_of(decoded.controllers.begin(), decoded.controllers.end(),
                    [](const auto& c) { return c.callsign == "TEST_OBS"; }) &&
        std::any_of(decoded.controllers.begin(), decoded.controllers.end(),
                    [](const auto& c) { return c.callsign == "TEST_SUP"; });

    ControllerSnapshot facilityZero;
    facilityZero.callsign = "GHOST_APP";
    facilityZero.frequency = "120.500";
    facilityZero.facility = 0;
    const auto facilityZeroNotPromoted =
        ClassifyRadioReachableFacility(facilityZero) ==
        RadioReachableFacilityGroup::Other;

    RadioReachableBuildOptions decodedOptions;
    decodedOptions.available = true;
    decodedOptions.stale = false;
    decodedOptions.source = RadioReachableSource::ControllerFeed;
    const auto decodedRadio = BuildRadioReachableControllerSnapshot(
        decoded.controllers, decodedOptions);
    const auto brainRejectsDecodedRole = [&](const std::string& callsign,
                                              const std::string& roleFact) {
        const auto found = std::find_if(
            decodedRadio.candidates.begin(), decodedRadio.candidates.end(),
            [&](const auto& item) { return item.callsign == callsign; });
        if (found == decodedRadio.candidates.end()) return false;
        const auto output = RunBrainControllerRelevanceWorker(
            BuildClevelandInput(*found));
        const auto* completion = FindCompletion(output, callsign);
        return completion != nullptr &&
            completion->decision == BrainOwnedCandidateDecision::Rejected &&
            completion->reason.find(
                "brain-ineligible-source-facility-observer") !=
                std::string::npos &&
            completion->reason.find(roleFact) != std::string::npos;
    };
    const auto observerSupervisorBrainRejected =
        brainRejectsDecodedRole("TEST_OBS", "observer-callsign") &&
        brainRejectsDecodedRole("TEST_SUP", "supervisor-callsign");

    const auto cleCandidate = [](std::string sourcePrimary) {
        RadioReachableControllerCandidate value;
        value.callsign = "CLE_APP";
        value.frequency = "120.500";
        value.sourcePrimaryFrequency = std::move(sourcePrimary);
        value.sourcePrimaryFrequencyProvided = true;
        value.vatsimFacility = 5;
        value.vatsimFacilityProvided = true;
        value.group = RadioReachableFacilityGroup::AppDep;
        value.source = RadioReachableSource::AFVRadioRange;
        value.stationCoordinates.push_back(
            {"120.500", 41.4117, -81.8498});
        return value;
    };
    const auto guard121Output = RunBrainControllerRelevanceWorker(
        BuildClevelandInput(cleCandidate("121.500")));
    const auto* guard121 = FindCompletion(guard121Output, "CLE_APP");
    const auto guard121Rejected = guard121 != nullptr &&
        guard121->decision == BrainOwnedCandidateDecision::Rejected &&
        guard121->reason.find("brain-ineligible-source-primary-guard") !=
            std::string::npos;
    const auto unknownPrimaryOutput = RunBrainControllerRelevanceWorker(
        BuildClevelandInput(cleCandidate("")));
    const auto* unknownPrimary =
        FindCompletion(unknownPrimaryOutput, "CLE_APP");
    const auto unknownPrimaryPreserved = unknownPrimary != nullptr &&
        unknownPrimary->reason.find("source-primary-unknown") !=
            std::string::npos &&
        unknownPrimary->reason.find("brain-ineligible-source-primary") ==
            std::string::npos;
    const auto exactCleOutput = RunBrainControllerRelevanceWorker(
        BuildClevelandInput(cleCandidate("120.500")));
    const auto* exactCle = FindCompletion(exactCleOutput, "CLE_APP");
    const auto exactCleAccepted = exactCle != nullptr &&
        exactCle->decision == BrainOwnedCandidateDecision::Accepted;

    RadioReachableControllerSnapshot base;
    base.available = true;
    base.stale = false;
    base.source = RadioReachableSource::AFVRadioRange;
    base.sourceRosterAvailable = decoded.hasCache && !decoded.stale;
    base.sourceRosterStale = decoded.stale;
    base.sourceRosterComplete = decoded.controllerComponentComplete;
    base.sourceRosterTruncated = decoded.controllerComponentTruncated;
    base.sourceRosterDeclaredRecords = decoded.connectedControllers;
    base.sourceRosterRetainedRecords = decoded.retainedControllerRecords;
    base.sourceRosterRejectedRecords = decoded.rejectedControllerRecords;
    base.sourceRosterDigest = decoded.controllerContentDigest;
    base.sourceRoster = decoded.immutableControllers;
    // Global transceiver/controller evidence is not allowed to create a live
    // APP/DEP candidate. The complete radio board is the sole admission set.
    const auto radioScoped = base;
    const auto globalAppDepNotInjected = radioScoped.candidates.empty();
    const auto scopedDecisionEvidenceHash =
        HashBrainRadioControllerDecisionEvidence(radioScoped);
    auto globalRosterChurn = radioScoped;
    ++globalRosterChurn.sourceRosterDigest;
    globalRosterChurn.sourceRosterDeclaredRecords += 100;
    globalRosterChurn.sourceRosterRetainedRecords += 100;
    for (auto& scopedCandidate : globalRosterChurn.candidates) {
        ++scopedCandidate.sourceRosterDigest;
        scopedCandidate.sourceRosterDeclaredRecords += 100;
        scopedCandidate.sourceRosterRetainedRecords += 100;
    }
    const auto globalRosterChurnIgnored =
        HashBrainRadioControllerDecisionEvidence(globalRosterChurn) ==
            scopedDecisionEvidenceHash;

    RadioReachableControllerSnapshot vnasBoard = base;
    RadioReachableControllerCandidate admittedVnasCandidate;
    admittedVnasCandidate.callsign = "CLE_APP";
    admittedVnasCandidate.frequency = "120.500";
    admittedVnasCandidate.group = RadioReachableFacilityGroup::AppDep;
    admittedVnasCandidate.stableKey = "CLE_APP|120.500|APP_DEP";
    vnasBoard.candidates.push_back(admittedVnasCandidate);
    const auto completeRadioFeed =
        BuildRadioReachableVerificationFeed(vnasBoard);
    const auto completeRadioControllerFeed =
        completeRadioFeed.ToControllerFeedSnapshot();
    const auto completeRadioBoardPublished =
        completeRadioFeed.fullBoardSnapshot &&
        completeRadioFeed.inputCandidates == 1 &&
        completeRadioFeed.selectedControllers == 1 &&
        completeRadioControllerFeed.controllerComponentComplete &&
        completeRadioControllerFeed.retainedControllerRecords == 1;
    VnasLiveOwnershipSnapshot globalVnas;
    globalVnas.enabled = true;
    globalVnas.available = true;
    globalVnas.stale = false;
    globalVnas.facilityDataComplete = true;
    globalVnas.ownershipDataAvailable = true;
    VnasLiveOwnershipFactSnapshot cleVnasFact;
    cleVnasFact.controllerCallsign = "CLE_APP";
    cleVnasFact.controllerFrequency = "120.500";
    cleVnasFact.facilityId = "CLE";
    cleVnasFact.sourcePositionId = "CLE-S";
    cleVnasFact.sourcePositionCallsign = "CLE_APP";
    cleVnasFact.sourcePositionFrequency = "120.500";
    globalVnas.facts.push_back(cleVnasFact);
    auto unrelatedVnasFact = cleVnasFact;
    unrelatedVnasFact.controllerCallsign = "EDDF_APP";
    unrelatedVnasFact.controllerFrequency = "119.025";
    unrelatedVnasFact.facilityId = "EDDF";
    unrelatedVnasFact.sourcePositionId = "EDDF-N";
    unrelatedVnasFact.sourcePositionCallsign = "EDDF_N_APP";
    unrelatedVnasFact.sourcePositionFrequency = "119.025";
    globalVnas.facts.push_back(unrelatedVnasFact);
    const auto scopedVnas =
        BuildRadioScopedVnasLiveOwnershipSnapshot(globalVnas, vnasBoard);
    const auto vnasExactBoardScope =
        scopedVnas.facts.size() == 1 &&
        scopedVnas.facts.front().controllerCallsign == "CLE_APP";
    auto unrelatedVnasChanged = globalVnas;
    unrelatedVnasChanged.facts.back().sourcePositionId = "EDDF-S";
    const auto unrelatedVnasChangeIgnored =
        BuildRadioScopedVnasLiveOwnershipSnapshot(
            unrelatedVnasChanged, vnasBoard).stableHash == scopedVnas.stableHash;
    auto relevantVnasChanged = globalVnas;
    relevantVnasChanged.facts.front().sourcePositionId = "CLE-N";
    const auto relevantVnasChangeDetected =
        BuildRadioScopedVnasLiveOwnershipSnapshot(
            relevantVnasChanged, vnasBoard).stableHash != scopedVnas.stableHash;

    auto scopedCandidateChanged = radioScoped;
    RadioReachableControllerCandidate scopedCandidate;
    scopedCandidate.callsign = "CLE_APP";
    scopedCandidate.frequency = "120.500";
    scopedCandidate.group = RadioReachableFacilityGroup::AppDep;
    scopedCandidate.stableKey = "CLE_APP|120.500|APP_DEP";
    scopedCandidateChanged.candidates.push_back(std::move(scopedCandidate));
    const auto scopedCandidateChangeDetected =
        !scopedCandidateChanged.candidates.empty() &&
        HashBrainRadioControllerDecisionEvidence(scopedCandidateChanged) !=
            scopedDecisionEvidenceHash;
    RadioReachableControllerCandidate candidate;
    candidate.callsign = "CLE_APP";
    candidate.frequency = "120.500";
    candidate.sourcePrimaryFrequency = "199.998";
    candidate.sourcePrimaryFrequencyProvided = true;
    candidate.vatsimFacility = 5;
    candidate.group = RadioReachableFacilityGroup::AppDep;
    candidate.source = RadioReachableSource::AFVRadioRange;
    candidate.actionable = true;
    candidate.stationCoordinates.push_back(
        {"120.500", 35.2140, -80.9431});
    const auto guardPrimaryPreserved =
        candidate.sourcePrimaryFrequency == "199.998";
    const auto cleOutput = RunBrainControllerRelevanceWorker(
        BuildClevelandInput(std::move(candidate)));
    const auto* cle = FindCompletion(cleOutput, "CLE_APP");
    const auto cleRejected =
        cle != nullptr &&
        cle->decision == BrainOwnedCandidateDecision::Rejected;
    const auto guardNotRescued =
        guardPrimaryPreserved && cleRejected &&
        cle->reason.find("brain-ineligible-source-primary-guard") !=
            std::string::npos;

    BrainControllerDecisionInput supportedTie;
    supportedTie.supportedTiesDisplay = true;
    supportedTie.votes = {
        {"tie-positive", 1, "probe", "support"},
        {"tie-negative", -1, "probe", "contradiction"},
    };
    const auto tieReceipt = DecideBrainControllerCandidate(supportedTie);
    const auto supportedTieDisplays =
        tieReceipt.accepted &&
        tieReceipt.reason == "brain-supported-tie-display";

    std::cout << "CONTROLLER_OPERATIONAL_ELIGIBILITY"
              << " obsSupRetained=" << (obsSupRetained ? 1 : 0)
              << " facilityZeroNotPromoted=" << (facilityZeroNotPromoted ? 1 : 0)
              << " observerSupervisorBrainRejected="
              << (observerSupervisorBrainRejected ? 1 : 0)
              << " guard121Rejected=" << (guard121Rejected ? 1 : 0)
              << " unknownPrimaryPreserved="
              << (unknownPrimaryPreserved ? 1 : 0)
              << " exactCleAccepted=" << (exactCleAccepted ? 1 : 0)
              << " guardNotRescued=" << (guardNotRescued ? 1 : 0)
              << " globalAppDepNotInjected="
              << (globalAppDepNotInjected ? 1 : 0)
              << " globalRosterChurnIgnored="
              << (globalRosterChurnIgnored ? 1 : 0)
              << " vnasExactBoardScope=" << (vnasExactBoardScope ? 1 : 0)
              << " completeRadioBoardPublished="
              << (completeRadioBoardPublished ? 1 : 0)
              << " unrelatedVnasChangeIgnored="
              << (unrelatedVnasChangeIgnored ? 1 : 0)
              << " relevantVnasChangeDetected="
              << (relevantVnasChangeDetected ? 1 : 0)
              << " scopedCandidateChangeDetected="
              << (scopedCandidateChangeDetected ? 1 : 0)
              << " cleRejected=" << (cleRejected ? 1 : 0)
              << " supportedTieDisplays=" << (supportedTieDisplays ? 1 : 0);
    if (cle != nullptr) {
        std::cout << " cleVotes=" << cle->positiveVotes << "/"
                  << cle->negativeVotes << "/" << cle->neutralVotes
                  << " cleReason=\"" << cle->reason << "\"";
    }
    if (exactCle != nullptr) {
        std::cout << " exactCleReason=\"" << exactCle->reason << "\"";
    }
    std::cout << "\n";

    if (!obsSupRetained || !facilityZeroNotPromoted ||
        !observerSupervisorBrainRejected || !guard121Rejected ||
        !unknownPrimaryPreserved || !exactCleAccepted ||
        !guardNotRescued || !globalAppDepNotInjected ||
        !globalRosterChurnIgnored || !vnasExactBoardScope ||
        !completeRadioBoardPublished ||
        !unrelatedVnasChangeIgnored || !relevantVnasChangeDetected ||
        !scopedCandidateChangeDetected ||
        !cleRejected ||
        !supportedTieDisplays) {
        std::cerr << "CONTROLLER_OPERATIONAL_ELIGIBILITY_FAILED\n";
        return 1;
    }
    std::cout << "CONTROLLER_OPERATIONAL_ELIGIBILITY_PASSED\n";
    return 0;
}

int RunControllerOperationalEligibilityPerformanceProbe() {
    constexpr std::size_t kBatchSize = 512;
    constexpr int kIterations = 10'000;
    const auto document = BuildSyntheticControllerDocument(kBatchSize);

    // Warm each production seam before measuring the predeclared limits.
    const auto warmBatch = RunSyntheticProductionBatch(document);
    BrainControllerDecisionInput decision;
    decision.ineligibleReason = "brain-ineligible-source-primary-guard";
    decision.votes = {
        {"operational-source-facility", 1, "facility=5", "source"},
        {"operational-source-primary", -1, "199.998", "guard"},
        {"endpoint-identity", -1, "CLT!=CLE", "vatspy"},
        {"radio-service", -1, "outside-endpoint", "afv"},
    };
    (void)DecideBrainControllerCandidate(decision);

    const auto batchStart = std::chrono::steady_clock::now();
    const auto measuredBatch = RunSyntheticProductionBatch(document);
    const auto batchEnd = std::chrono::steady_clock::now();
    const auto batchUs = std::chrono::duration_cast<std::chrono::microseconds>(
        batchEnd - batchStart).count();

    const auto workingSetBefore = WorkingSetBytes();
    std::uint64_t decisionChecksum = 0;
    const auto decisionsStart = std::chrono::steady_clock::now();
    for (int index = 0; index < kIterations; ++index) {
        const auto receipt = DecideBrainControllerCandidate(decision);
        decisionChecksum += static_cast<std::uint64_t>(receipt.positiveVotes +
            receipt.negativeVotes + receipt.neutralVotes +
            (receipt.accepted ? 1 : 0));
    }
    const auto decisionsEnd = std::chrono::steady_clock::now();
    const auto decisionsUs =
        std::chrono::duration_cast<std::chrono::microseconds>(
            decisionsEnd - decisionsStart).count();
    const auto workingSetAfter = WorkingSetBytes();
    const auto workingSetGrowth = workingSetAfter > workingSetBefore
        ? workingSetAfter - workingSetBefore
        : 0;

    BrainOwnedRuntimeState runtime;
    runtime.hasRadioBoard = true;
    runtime.lastRadioBoardHash = measuredBatch.input.radioBoardHash;
    runtime.routePolygonHash = measuredBatch.input.routePolygonHash;
    runtime.currentPolygonKey = measuredBatch.input.currentPolygonKey;
    runtime.lastWorkflowStage = measuredBatch.input.workflowStage;
    const auto first = RunBrainOwnedControllerRelevance(
        &runtime, measuredBatch.input);
    runtime.lastRadioBoardHash = measuredBatch.input.radioBoardHash;
    runtime.lastWorkflowStage = measuredBatch.input.workflowStage;
    const auto repeated = RunBrainOwnedControllerRelevance(
        &runtime, measuredBatch.input);

    const auto batchPass = warmBatch.decoded == kBatchSize &&
        measuredBatch.decoded == kBatchSize &&
        measuredBatch.evaluated == kBatchSize && batchUs <= 25'000;
    const auto decisionPass = decisionsUs <= 250'000;
    const auto cachePass = !first.cacheHit && repeated.cacheHit &&
        repeated.relevance.reason == "board-unchanged-no-relevance-work";
    const auto memoryPass = workingSetBefore != 0 &&
        workingSetAfter != 0 && workingSetGrowth <= 4ULL * 1024ULL * 1024ULL;
    const auto passed = batchPass && decisionPass && cachePass && memoryPass &&
        decisionChecksum != 0;

    std::cout << "CONTROLLER_OPERATIONAL_ELIGIBILITY_PERFORMANCE"
              << " batchControllers=" << measuredBatch.decoded
              << " batchEvaluated=" << measuredBatch.evaluated
              << " batchUs=" << batchUs
              << " decodeUs=" << measuredBatch.decodeUs
              << " radioBuildUs=" << measuredBatch.radioBuildUs
              << " brainUs=" << measuredBatch.brainUs
              << " decisionIterations=" << kIterations
              << " decisionsUs=" << decisionsUs
              << " cacheHit=" << (repeated.cacheHit ? 1 : 0)
              << " repeatedTransceiverFetches=0"
              << " repeatedRelevanceRuns=" << (repeated.cacheHit ? 0 : 1)
              << " queueDepth=0"
              << " workingSetGrowthBytes=" << workingSetGrowth
              << " threadsAdded=0 timersAdded=0 callbacksAdded=0"
              << " passed=" << (passed ? 1 : 0) << "\n";
    return passed ? 0 : 1;
}

int RunEndpointTerminalPolygonOwnershipProbe() {
    const std::string payload = R"json({
      "type":"FeatureCollection",
      "features":[
        {"type":"Feature","properties":{"id":"EGLL","name":"Heathrow Director","prefix":["EGLL"]},"geometry":{"type":"Polygon","coordinates":[[[0.0348111,51.330875],[0.0552778,51.5052778],[0.1516667,51.6461111],[-0.3258333,51.6558333],[-0.5497222,51.7261111],[-0.4613889,51.4775],[-0.4472222,51.305],[0.0348111,51.330875]]]}},
        {"type":"Feature","properties":{"id":"THAMES","name":"Thames Director","prefix":["THAMES"]},"geometry":{"type":"Polygon","coordinates":[[[0.0552778,51.5052778],[0.0348111,51.330875],[0.37669111111111,51.237976944444],[0.42060055555556,51.236283888889],[0.59722222222222,51.303888888889],[0.81874166666667,51.307311388889],[0.94497583333333,51.293435],[1.1599977777778,51.286080555556],[1.4432830555556,51.276391944444],[1.5047222222222,51.454444444444],[1.6119444444444,51.7475],[1.4266666666667,51.735833333333],[1.2170197222222,51.7214825],[0.76218138888889,51.690793055556],[0.43210916666667,51.666904166667],[0.23777777777778,51.652222222222],[0.1516667,51.6461111],[0.06449,51.640259166667],[0.0552778,51.5052778]]]}}
      ]
    })json";

    xvatsim::modules::route_sector::RouteSectorResolver resolver;
    resolver.LoadBoundaryPayloadsForTesting(
        R"json({"type":"FeatureCollection","features":[]})json",
        payload,
        "[FIR]\n");
    BrainEndpointPolygonQueryInput request;
    request.airportIcao = "EGLL";
    request.hasAirportCoordinates = true;
    request.airportLatitudeDeg = 51.470020;
    request.airportLongitudeDeg = -0.454295;
    request.nowSeconds = 1;
    const auto authority = resolver.QueryEndpointPolygons(request);

    const auto thamesPolygon = std::find_if(
        authority.polygons.begin(),
        authority.polygons.end(),
        [](const auto& polygon) { return polygon.polygonKey == "THAMES"; });
    const auto egllPolygon = std::find_if(
        authority.polygons.begin(),
        authority.polygons.end(),
        [](const auto& polygon) { return polygon.polygonKey == "EGLL"; });
    const auto packageCorrect = authority.available && authority.resolved &&
        egllPolygon != authority.polygons.end() &&
        egllPolygon->containsAirport &&
        thamesPolygon != authority.polygons.end() &&
        !thamesPolygon->containsAirport &&
        thamesPolygon->intersectsEndpointAnchorContextOnly;

    std::vector<ControllerSnapshot> controllers;
    ControllerSnapshot controller;
    controller.callsign = "THAMES_APP";
    controller.frequency = "132.700";
    controller.facility = 5;
    controller.visualRangeNm = 150;
    controller.actionable = true;
    controllers.push_back(controller);
    ControllerFeedSnapshot controllerFeed;
    controllerFeed.available = true;
    controllerFeed.stale = false;
    controllerFeed.generation = 1;
    controllerFeed.connectedControllers = controllers.size();
    controllerFeed.controllerComponentPresent = true;
    controllerFeed.controllerComponentComplete = true;
    controllerFeed.retainedControllerRecords = controllers.size();
    controllerFeed.controllers = &controllers;

    AircraftStateSnapshot aircraft;
    aircraft.valid = true;
    aircraft.latitudeDeg = request.airportLatitudeDeg;
    aircraft.longitudeDeg = request.airportLongitudeDeg;

    RouteSectorSnapshot authorityRoute;
    authorityRoute.available = true;
    authorityRoute.stale = false;
    authorityRoute.routeResolved = true;
    authorityRoute.departureIcao = "KATL";
    authorityRoute.destinationIcao = "EGLL";
    const auto canonicalAuthority =
        resolver.ResolveBrainScheduledAuthorityVerification(
            aircraft,
            controllerFeed,
            authorityRoute,
            "endpoint-terminal-polygon-ownership-probe");
    const auto thamesAssignment = std::find_if(
        canonicalAuthority.liveMap.assignments.begin(),
        canonicalAuthority.liveMap.assignments.end(),
        [](const auto& assignment) {
            return assignment.callsign == "THAMES_APP" &&
                   assignment.kind == AuthorityRelevanceKind::Terminal &&
                   IsActiveLiveAuthorityState(assignment.state);
        });
    const auto liveMapCorrect = canonicalAuthority.liveMap.available &&
        !canonicalAuthority.liveMap.stale &&
        thamesAssignment != canonicalAuthority.liveMap.assignments.end() &&
        thamesAssignment->canonicalPolygonId ==
            thamesPolygon->canonicalPolygonId;

    RadioReachableControllerCandidate candidate;
    candidate.callsign = "THAMES_APP";
    candidate.frequency = "132.700";
    candidate.sourcePrimaryFrequency = "132.700";
    candidate.sourcePrimaryFrequencyProvided = true;
    candidate.vatsimFacility = 5;
    candidate.vatsimFacilityProvided = true;
    candidate.group = RadioReachableFacilityGroup::AppDep;
    candidate.source = RadioReachableSource::AFVRadioRange;
    candidate.actionable = true;
    candidate.stationCoordinates.push_back(
        {"132.700", 51.535020, -0.454295});

    BrainControllerRelevanceWorkerInput input;
    input.workflowStage = WorkflowStage::Arrival;
    input.radioBoardHash = 901;
    input.routePolygonHash = 902;
    input.currentPolygonIndex = 1;
    input.currentPolygonKey = "LON";
    input.departureIcao = "KATL";
    input.arrivalIcao = "EGLL";
    input.hasArrivalCoordinates = true;
    input.arrivalLatitudeDeg = request.airportLatitudeDeg;
    input.arrivalLongitudeDeg = request.airportLongitudeDeg;
    input.terminalTransmitterRadiusNm = 5.0;
    input.arrivalEndpointPolygonHash = 903;
    input.arrivalEndpointPolygons = authority;
    input.authorityRelevance =
        std::make_shared<const AuthorityRelevanceSnapshot>(canonicalAuthority);
    auto route = std::make_shared<RouteSectorSnapshot>();
    route->available = true;
    route->routeResolved = true;
    route->destinationIcao = "EGLL";
    route->airportCallsignAliases.push_back(
        {"EGLL", "EGLL", "LON", "VATSPY_AIRPORT", "EGLL|EGLL|LON"});
    input.route = route;
    input.candidates.push_back(candidate);

    const auto result = RunBrainControllerRelevanceWorker(input);
    const auto* completion = FindCompletion(result, "THAMES_APP");
    const auto canonicalFact = completion == nullptr
        ? std::vector<BrainControllerEvidenceVote>::const_iterator{}
        : std::find_if(
              completion->evidenceVotes.begin(),
              completion->evidenceVotes.end(),
              [](const auto& vote) {
                  return vote.source == "canonical-live-map" &&
                         vote.score == 0 &&
                         vote.reason == "active-assignment-unrelated" &&
                         vote.provenance ==
                             "active-canonical-assignment-does-not-contain-endpoint";
              });
    const auto brainRejected = completion != nullptr &&
        completion->decision == BrainOwnedCandidateDecision::Rejected &&
        completion->terminalRelationship ==
            TerminalEndpointRelationship::Unrelated &&
        canonicalFact != completion->evidenceVotes.end() &&
        completion->reason.find("brain-terminal-unrelated-hidden") !=
            std::string::npos;

    std::cout << "ENDPOINT_TERMINAL_POLYGON_OWNERSHIP"
              << " packageCorrect=" << (packageCorrect ? 1 : 0)
              << " polygons=" << authority.polygons.size()
              << " thamesNearby="
              << (thamesPolygon != authority.polygons.end() ? 1 : 0)
              << " liveMapCorrect=" << (liveMapCorrect ? 1 : 0)
              << " brainRejected=" << (brainRejected ? 1 : 0);
    if (completion != nullptr) {
        std::cout << " votes=" << completion->positiveVotes << "/"
                  << completion->negativeVotes << "/"
                  << completion->neutralVotes
                  << " reason=\"" << completion->reason << "\"";
    }
    std::cout << "\n";
    if (!packageCorrect || !liveMapCorrect || !brainRejected) {
        std::cerr << "ENDPOINT_TERMINAL_POLYGON_OWNERSHIP_FAILED\n";
        return 1;
    }
    std::cout << "ENDPOINT_TERMINAL_POLYGON_OWNERSHIP_PASSED\n";
    return 0;
}

int RunAircraftElectricalPowerProbe() {
    using workflow::AircraftElectricalPowerState;
    workflow::AircraftElectricalPowerBoundaryState state;

    const auto decide = [&](long long nowMs,
                            int batteryCount,
                            int batteriesOn,
                            bool busAvailable,
                            double maxBusVolts) {
        workflow::AircraftRuntimeBoundaryInput input;
        input.aircraftState.valid = true;
        input.aircraftState.batterySwitchFactsAvailable = batteryCount > 0;
        input.aircraftState.batterySwitchCount = batteryCount;
        input.aircraftState.batterySwitchOnCount = batteriesOn;
        input.aircraftState.batteryOn = batteriesOn > 0;
        input.aircraftState.electricalBusVoltageFactsAvailable = busAvailable;
        input.aircraftState.electricalBusVoltageCount = busAvailable ? 6 : 0;
        input.aircraftState.maximumElectricalBusVoltage = maxBusVolts;
        input.monotonicMs = nowMs;
        input.electricalPowerState = state;
        const auto decision = workflow::ResolveAircraftRuntimeBoundary(input);
        state = decision.nextElectricalPowerState;
        return decision;
    };

    const auto secondBattery = decide(0, 2, 1, true, 24.0);
    const auto gpuOrGenerator = decide(100, 2, 0, true, 28.0);
    const auto transientGap = decide(500, 2, 0, true, 0.0);
    const auto recoveredBus = decide(1000, 2, 0, true, 28.0);
    const auto coldDarkPending = decide(2000, 2, 0, true, 0.0);
    const auto coldDarkConfirmed = decide(4100, 2, 0, true, 0.0);

    workflow::AircraftElectricalPowerBoundaryState poweredState;
    state = poweredState;
    (void)decide(5000, 2, 1, true, 24.0);
    const auto unknownBus = decide(5100, 2, 0, false, 0.0);

    const auto passed =
        secondBattery.electricalPowerAvailable &&
        secondBattery.observedPowerState == AircraftElectricalPowerState::Powered &&
        gpuOrGenerator.electricalPowerAvailable &&
        gpuOrGenerator.electricalPowerReason == "electrical-bus-energized" &&
        transientGap.electricalPowerAvailable &&
        !transientGap.coldDarkBoundaryActive &&
        recoveredBus.electricalPowerAvailable &&
        coldDarkPending.electricalPowerAvailable &&
        !coldDarkPending.coldDarkBoundaryActive &&
        coldDarkConfirmed.coldDarkBoundaryActive &&
        coldDarkConfirmed.shouldResetSessionRuntimeCaches &&
        unknownBus.observedPowerState == AircraftElectricalPowerState::Unknown &&
        unknownBus.electricalPowerAvailable &&
        !unknownBus.coldDarkBoundaryActive;

    std::cout << "AIRCRAFT_ELECTRICAL_POWER"
              << " secondBattery=" << (secondBattery.electricalPowerAvailable ? 1 : 0)
              << " busPowered=" << (gpuOrGenerator.electricalPowerAvailable ? 1 : 0)
              << " transientPreserved=" << (transientGap.electricalPowerAvailable ? 1 : 0)
              << " confirmedColdDark=" << (coldDarkConfirmed.coldDarkBoundaryActive ? 1 : 0)
              << " unknownPreserved=" << (unknownBus.electricalPowerAvailable ? 1 : 0)
              << " passed=" << (passed ? 1 : 0) << "\n";
    return passed ? 0 : 1;
}

}  // namespace xvatsim::tools::controller_operational_eligibility
