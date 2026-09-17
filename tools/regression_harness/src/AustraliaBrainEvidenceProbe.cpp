#include "AustraliaBrainEvidenceProbe.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "XVatsim/brain/BrainControllerDecision.h"
#include "XVatsim/brain/BrainOwnedWorkerTypes.h"
#include "XVatsim/core/ControllerAuthority.h"

namespace xvatsim::tools::australia_brain_evidence {
namespace {

using namespace xvatsim::brain;

bool HasVote(
    const BrainOwnedCandidateCompletion& completion,
    const std::string& source,
    int score) {
    return std::any_of(
        completion.evidenceVotes.begin(),
        completion.evidenceVotes.end(),
        [&](const auto& vote) {
            return vote.source == source && vote.score == score;
        });
}

bool HasAllTerminalSources(const BrainOwnedCandidateCompletion& completion) {
    const std::vector<std::string> sources = {
        "vatsim-parse",
        "vatspy-parse",
        "vnas",
        "frequency-distance",
        "terminal-source",
        "published-frequency",
    };
    return std::all_of(sources.begin(), sources.end(), [&](const auto& source) {
        return std::any_of(
            completion.evidenceVotes.begin(),
            completion.evidenceVotes.end(),
            [&](const auto& vote) {
                return vote.source == source &&
                       vote.score >= -1 && vote.score <= 1;
            });
    });
}

RadioReachableControllerCandidate TerminalCandidate(
    std::string callsign,
    std::string frequency,
    RadioReachableFacilityGroup group,
    double stationLatitude,
    double stationLongitude) {
    RadioReachableControllerCandidate candidate;
    candidate.callsign = std::move(callsign);
    candidate.frequency = std::move(frequency);
    candidate.group = group;
    candidate.actionable = true;
    candidate.stationCoordinates.push_back(
        {candidate.frequency, stationLatitude, stationLongitude});
    return candidate;
}

std::shared_ptr<RouteSectorSnapshot> MelbourneRoute() {
    auto route = std::make_shared<RouteSectorSnapshot>();
    route->available = true;
    route->stale = false;
    route->routeResolved = true;
    route->departureIcao = "YMML";
    route->destinationIcao = "YSSY";
    route->airportCallsignAliases = {
        {"YMML", "ML", "YBLA", "VATSPY_AIRPORT", "YMML|Melbourne|...|ML|YBLA|0"},
        {"YMML", "ML-S", "YBLA", "VATSPY_AIRPORT", "YMML|Melbourne|...|ML-S|YBLA|0"},
        {"YMML", "ML-R", "YBLA", "VATSPY_AIRPORT", "YMML|Melbourne|...|ML-R|YBLA|0"},
        {"YSSY", "SY", "YSYD", "VATSPY_AIRPORT", "YSSY|Sydney|...|SY|YSYD|0"},
    };
    RouteSectorMatchSnapshot current;
    current.identifier = "YBLA";
    current.controllerPrefixes = {"ML-BLA"};
    current.entryDistanceNm = 0.0;
    route->currentSectors.push_back(current);
    RouteSectorMatchSnapshot next;
    next.identifier = "YGUN";
    next.controllerPrefixes = {"ML-GUN"};
    next.entryDistanceNm = 25.0;
    route->nextSectors.push_back(next);
    return route;
}

bool CheckVatspyParser() {
    const std::string payload =
        "[Airports]\n"
        "YMML|Melbourne|-37.6733|144.8433|ML|YBLA|0\n"
        "YMML|Melbourne South|-37.6733|144.8433|ML-S|YBLA|0\n"
        "YMML|Melbourne Radar|-37.6733|144.8433|ML-R|YBLA|0\n"
        "YSSY|Sydney|-33.9461|151.1772|SY|YSYD|0\n"
        "[FIRs]\n"
        "YBLA|Brisbane Low|ML-BLA|YBLA\n";
    const auto catalog =
        xvatsim::core::authority::CompileVatSpyAuthorityCatalog(payload);
    const auto count = std::count_if(
        catalog.airportCallsignAliases.begin(),
        catalog.airportCallsignAliases.end(),
        [](const auto& alias) { return alias.airportIcao == "YMML"; });
    return count == 3 && std::any_of(
        catalog.airportCallsignAliases.begin(),
        catalog.airportCallsignAliases.end(),
        [](const auto& alias) {
            return alias.airportIcao == "YMML" &&
                   alias.callsignPrefix == "ML" &&
                   alias.boundaryId == "YBLA";
        });
}

bool CheckTerminalVotes() {
    BrainControllerRelevanceWorkerInput input;
    input.workflowStage = WorkflowStage::Departure;
    input.radioBoardHash = 11;
    input.routePolygonHash = 22;
    input.currentPolygonKey = "YBLA";
    input.arrivalPolygonKey = "YSYD";
    input.departureIcao = "YMML";
    input.arrivalIcao = "YSSY";
    input.hasDepartureCoordinates = true;
    input.departureLatitudeDeg = -37.6733;
    input.departureLongitudeDeg = 144.8433;
    input.hasArrivalCoordinates = true;
    input.arrivalLatitudeDeg = -33.9461;
    input.arrivalLongitudeDeg = 151.1772;
    input.terminalTransmitterRadiusNm = 5.0;
    input.route = MelbourneRoute();
    input.candidates = {
        TerminalCandidate("ML_GND", "121.700", RadioReachableFacilityGroup::Ground,
                          -37.6733, 144.8433),
        TerminalCandidate("ML_TWR", "120.500", RadioReachableFacilityGroup::Tower,
                          -37.6733, 144.8433),
        TerminalCandidate("ML_APP", "132.000", RadioReachableFacilityGroup::AppDep,
                          -37.6733, 144.8433),
        TerminalCandidate("MLL_GND", "121.800", RadioReachableFacilityGroup::Ground,
                          -37.6733, 144.8433),
        TerminalCandidate("XYT_GND", "122.100", RadioReachableFacilityGroup::Ground,
                          -33.9461, 151.1772),
    };

    const auto output = RunBrainControllerRelevanceWorker(input);
    if (output.completions.size() != input.candidates.size() ||
        output.departureBoard.stations.size() != 4) {
        return false;
    }
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& completion = output.completions[i];
        if (completion.decision != BrainOwnedCandidateDecision::Accepted ||
            !HasAllTerminalSources(completion) ||
            !HasVote(completion, "vatspy-parse", 1) ||
            !HasVote(completion, "frequency-distance", 1) ||
            !HasVote(completion, "vnas", 0)) {
            return false;
        }
    }
    const auto& unrelated = output.completions.back();
    return unrelated.decision == BrainOwnedCandidateDecision::Rejected &&
           HasAllTerminalSources(unrelated) &&
           HasVote(unrelated, "vatspy-parse", -1) &&
           HasVote(unrelated, "frequency-distance", -1);
}

RadioReachableControllerCandidate MelbourneCenter(std::string status) {
    RadioReachableControllerCandidate candidate;
    candidate.callsign = "ML-GUN_CTR";
    candidate.frequency = "133.150";
    candidate.group = RadioReachableFacilityGroup::Center;
    candidate.actionable = true;
    candidate.textAtis = std::move(status);
    candidate.hasDistanceNm = true;
    candidate.distanceNm = 80.0;
    TransceiverStationEvidenceSnapshot login;
    login.sourceFrequency = "133.150";
    login.hasAircraftDistance = true;
    login.aircraftDistanceNm = 80.0;
    login.withinMaxCandidateDistance = true;
    login.hasReceivableRange = true;
    login.withinReceivableRange = true;
    TransceiverStationEvidenceSnapshot extension = login;
    extension.sourceFrequency = "132.200";
    extension.aircraftDistanceNm = 45.0;
    candidate.originalTransceivers = {login, extension};
    return candidate;
}

bool CheckCenterExtension() {
    BrainControllerRelevanceWorkerInput input;
    input.workflowStage = WorkflowStage::Enroute;
    input.radioBoardHash = 31;
    input.routePolygonHash = 32;
    input.currentPolygonKey = "YBLA";
    input.nextPolygonKey = "YGUN";
    input.route = MelbourneRoute();
    input.candidates = {MelbourneCenter(
        "Extending SNO 124.0, BLA 132.2, ARL 130.9, MUN 132.6 and MNN 130.1")};
    const auto extended = RunBrainControllerRelevanceWorker(input);
    if (extended.enrouteBoard.stations.size() != 1 ||
        extended.completions.size() != 1) {
        return false;
    }
    const auto& station = extended.enrouteBoard.stations.front();
    const auto& completion = extended.completions.front();
    if (station.frequency != "132.200" || station.polygonKey != "YBLA" ||
        station.hasRouteEntryDistance ||
        completion.displayRelation != DisplayRelation::CurrentPolygon ||
        completion.frequency != "132.200" ||
        !HasVote(completion, "vatsim-extension", 1) ||
        !HasVote(completion, "radio-channel", 1)) {
        return false;
    }

    input.candidates = {MelbourneCenter(
        "No longer extending BLA 132.2; extension withdrawn")};
    const auto withdrawn = RunBrainControllerRelevanceWorker(input);
    return withdrawn.enrouteBoard.stations.size() == 1 &&
           withdrawn.enrouteBoard.stations.front().frequency == "133.150" &&
           withdrawn.enrouteBoard.stations.front().polygonKey == "YGUN" &&
           withdrawn.enrouteBoard.stations.front().hasRouteEntryDistance &&
           withdrawn.completions.front().displayRelation ==
               DisplayRelation::NextPolygon &&
           HasVote(withdrawn.completions.front(), "vatsim-extension", 0);
}

bool CheckBrainOwnsDecision() {
    BrainControllerDecisionInput input;
    input.votes = {
        {"vatspy-parse", 4, "improper-weight", "test"},
        {"vatspy-parse", -7, "contradiction", "test"},
        {"vatsim-parse", 9, "improper-weight", "test"},
    };
    const auto receipt = DecideBrainControllerCandidate(input);
    return receipt.accepted && receipt.positiveVotes == 1 &&
           receipt.negativeVotes == 0 && receipt.neutralVotes == 1 &&
           std::any_of(receipt.votes.begin(), receipt.votes.end(), [](const auto& vote) {
               return vote.source == "vatspy-parse" && vote.score == 0;
           });
}

}  // namespace

int RunAustraliaBrainEvidenceProbe() {
    const bool vatspy = CheckVatspyParser();
    const bool terminal = CheckTerminalVotes();
    const bool center = CheckCenterExtension();
    const bool brain = CheckBrainOwnsDecision();
    if (!vatspy || !terminal || !center || !brain) {
        std::cerr << "AUSTRALIA_BRAIN_EVIDENCE_FAILED"
                  << " vatspy=" << vatspy
                  << " terminal=" << terminal
                  << " center=" << center
                  << " brain=" << brain << "\n";
        return 1;
    }
    std::cout << "AUSTRALIA_BRAIN_EVIDENCE_PASSED"
              << " aliases=ML,ML-S,ML-R"
              << " terminalScores=complete"
              << " extension=BLA@132.200"
              << " next=YGUN@133.150"
              << " brainOnly=1\n";
    return 0;
}

}  // namespace xvatsim::tools::australia_brain_evidence
