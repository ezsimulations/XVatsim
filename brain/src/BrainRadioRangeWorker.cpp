#include "XVatsim/brain/BrainOwnedWorkerTypes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace xvatsim::brain {

namespace {

constexpr const char* kPreviewSurvivor = "brain-preview-survivor";
constexpr const char* kPreviewRejectedNonActionable =
    "brain-preview-rejected-non-actionable";
constexpr const char* kPreviewRejectedMissingTransceiver =
    "brain-preview-rejected-missing-transceiver";
constexpr const char* kPreviewRejectedOverMaxDistance =
    "brain-preview-rejected-over-max-distance";
constexpr const char* kPreviewRejectedBeyondReceivableRange =
    "brain-preview-rejected-beyond-receivable-range";
constexpr const char* kPreviewRejectedEmptyFrequency =
    "brain-preview-rejected-empty-frequency";
constexpr const char* kPreviewRejectedGuardFrequency =
    "brain-preview-rejected-guard-frequency";
constexpr const char* kPreviewRejectedNoUsableStation =
    "brain-preview-rejected-no-usable-station";
constexpr const char* kAuthorityStationsPath = "authority-stations";
constexpr const char* kAuthorityPreviewSurvivor =
    "brain-preview-authority-survivor";
constexpr const char* kAuthorityPreviewRejectedNonActionable =
    "brain-preview-authority-rejected-non-actionable";
constexpr const char* kAuthorityPreviewRejectedMissingTransceiver =
    "brain-preview-authority-rejected-missing-transceiver";
constexpr const char* kAuthorityPreviewRejectedEmptyFrequency =
    "brain-preview-authority-rejected-empty-frequency";
constexpr const char* kAuthorityPreviewRejectedGuardFrequency =
    "brain-preview-authority-rejected-guard-frequency";
constexpr const char* kAuthorityPreviewRejectedNoUsableStation =
    "brain-preview-authority-rejected-no-usable-station";
constexpr const char* kAirportCoveragePath = "airport-coverage";
constexpr const char* kAirportCoveragePreviewSurvivor =
    "brain-preview-airport-coverage-survivor";
constexpr const char* kAirportCoveragePreviewRejectedNonActionable =
    "brain-preview-airport-coverage-rejected-non-actionable";
constexpr const char* kAirportCoveragePreviewRejectedMissingTransceiver =
    "brain-preview-airport-coverage-rejected-missing-transceiver";
constexpr const char* kAirportCoveragePreviewRejectedOutOfCoverage =
    "brain-preview-airport-coverage-rejected-out-of-coverage";
constexpr const char* kAirportCoveragePreviewRejectedAllStationsFailed =
    "brain-preview-airport-coverage-rejected-all-stations-failed";
constexpr const char* kAirportCoveragePreviewRejectedEmptyFrequency =
    "brain-preview-airport-coverage-rejected-empty-frequency";
constexpr const char* kAirportCoveragePreviewRejectedGuardFrequency =
    "brain-preview-airport-coverage-rejected-guard-frequency";
constexpr const char* kAirportCoveragePreviewRejectedNoUsableStation =
    "brain-preview-airport-coverage-rejected-no-usable-station";

struct AuthorityDisplayFrequencyResolution {
    std::string frequency;
    std::string unavailableReason;
};

struct AirportCoverageBestStation {
    const TransceiverStationEvidenceSnapshot* station = nullptr;
    int index = -1;
};

TransceiverResolutionSnapshot ApplyBrainOwnedRadioCandidateEnvelope(
    TransceiverResolutionSnapshot snapshot) {
    const auto maxDistanceNm =
        snapshot.maxCandidateDistanceNm > 0.0
            ? snapshot.maxCandidateDistanceNm
            : kBrainOwnedMaxRadioBoardCandidateDistanceNm;
    snapshot.maxCandidateDistanceNm = maxDistanceNm;

    std::vector<ReceivableControllerSnapshot> accepted;
    accepted.reserve(snapshot.candidates.size());
    int distanceRejected = snapshot.distanceRejectedControllers;
    for (auto& candidate : snapshot.candidates) {
        if (std::isfinite(candidate.distanceNm) &&
            candidate.distanceNm > maxDistanceNm) {
            ++distanceRejected;
            continue;
        }
        accepted.push_back(std::move(candidate));
    }

    snapshot.candidates = std::move(accepted);
    snapshot.receivableControllers =
        static_cast<int>(snapshot.candidates.size());
    snapshot.distanceRejectedControllers = distanceRejected;

    if (distanceRejected > 0 &&
        snapshot.statusLine.find("radio-candidate-over-max-distance") ==
            std::string::npos) {
        std::ostringstream stream;
        stream << snapshot.statusLine;
        if (!snapshot.statusLine.empty()) {
            stream << " ";
        }
        stream << "distanceRejected=" << distanceRejected
               << " maxCandidateDistanceNm="
               << static_cast<int>(std::round(maxDistanceNm))
               << " distanceReason=radio-candidate-over-max-distance";
        snapshot.statusLine = stream.str();
    }

    return snapshot;
}

bool EvidenceMatchesOldSurvivor(
    const TransceiverControllerEvidenceSnapshot& evidence,
    const ReceivableControllerSnapshot& survivor) {
    return evidence.callsign == survivor.callsign &&
           evidence.resolvedDisplayFrequency == survivor.frequency;
}

std::string NormalizeFrequencyForPreview(std::string frequency) {
    frequency.erase(
        std::remove_if(
            frequency.begin(),
            frequency.end(),
            [](unsigned char c) { return std::isspace(c) != 0; }),
        frequency.end());

    std::string digits;
    bool sawDecimal = false;
    int decimals = 0;
    for (const auto character : frequency) {
        if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
            digits.push_back(character);
            if (sawDecimal && decimals < 3) {
                ++decimals;
            }
            continue;
        }

        if (character == '.' && !sawDecimal) {
            sawDecimal = true;
        }
    }

    if (digits.empty()) {
        return {};
    }

    if (sawDecimal) {
        while (decimals < 3) {
            digits.push_back('0');
            ++decimals;
        }
    } else if (digits.size() == 5) {
        digits.push_back('0');
    }

    return digits;
}

bool IsPreviewGuardFrequency(const std::string& frequency) {
    const auto normalizedFrequency = NormalizeFrequencyForPreview(frequency);
    return normalizedFrequency == "121500" || normalizedFrequency == "199998";
}

AuthorityDisplayFrequencyResolution ResolveAuthorityPreviewDisplayFrequency(
    const std::string& controllerFrequency,
    const std::string& stationFrequency) {
    AuthorityDisplayFrequencyResolution resolution;
    const auto controllerHasFrequency = !controllerFrequency.empty();
    const auto stationHasFrequency = !stationFrequency.empty();
    const auto controllerFrequencyGuard =
        controllerHasFrequency && IsPreviewGuardFrequency(controllerFrequency);
    const auto stationFrequencyGuard =
        stationHasFrequency && IsPreviewGuardFrequency(stationFrequency);

    if (controllerHasFrequency && !controllerFrequencyGuard) {
        resolution.frequency = controllerFrequency;
        return resolution;
    }

    if (stationHasFrequency && !stationFrequencyGuard) {
        resolution.frequency = stationFrequency;
        return resolution;
    }

    if (!controllerHasFrequency && !stationHasFrequency) {
        resolution.unavailableReason = "both-empty";
    } else if (controllerFrequencyGuard && stationFrequencyGuard) {
        resolution.unavailableReason = "both-guard";
    } else if (controllerFrequencyGuard && !stationHasFrequency) {
        resolution.unavailableReason = "controller-guard-transceiver-empty";
    } else if (!controllerHasFrequency && stationFrequencyGuard) {
        resolution.unavailableReason = "controller-empty-transceiver-guard";
    } else {
        resolution.unavailableReason = "no-display-frequency";
    }
    return resolution;
}

bool AuthorityPreviewDecisionMatchesOldSurvivor(
    const BrainAuthorityStationsPreviewDecision& decision,
    const ReceivableControllerSnapshot& survivor) {
    return decision.hasStation &&
           decision.callsign == survivor.callsign &&
           decision.frequency == survivor.frequency &&
           decision.latitudeDeg == survivor.latitudeDeg &&
           decision.longitudeDeg == survivor.longitudeDeg;
}

bool AuthorityPreviewSurvivorHasOldMatch(
    const ReceivableControllerSnapshot& survivor,
    std::vector<BrainAuthorityStationsPreviewDecision>* decisions) {
    if (decisions == nullptr) {
        return false;
    }

    for (auto& decision : *decisions) {
        if (decision.decision != kAuthorityPreviewSurvivor ||
            decision.matchesOldSurvivor) {
            continue;
        }
        if (AuthorityPreviewDecisionMatchesOldSurvivor(decision, survivor)) {
            decision.matchesOldSurvivor = true;
            return true;
        }
    }
    return false;
}

void SortAuthorityStationCandidates(
    std::vector<ReceivableControllerSnapshot>* candidates) {
    if (candidates == nullptr) {
        return;
    }

    std::sort(
        candidates->begin(),
        candidates->end(),
        [](const auto& left, const auto& right) {
            if (left.callsign != right.callsign) {
                return left.callsign < right.callsign;
            }
            if (left.frequency != right.frequency) {
                return left.frequency < right.frequency;
            }
            if (left.latitudeDeg != right.latitudeDeg) {
                return left.latitudeDeg < right.latitudeDeg;
            }
            return left.longitudeDeg < right.longitudeDeg;
        });
}

void SortAirportCoverageCandidates(
    std::vector<ReceivableControllerSnapshot>* candidates) {
    if (candidates == nullptr) {
        return;
    }

    std::sort(
        candidates->begin(),
        candidates->end(),
        [](const auto& left, const auto& right) {
            if (left.score == right.score) {
                return left.distanceNm < right.distanceNm;
            }
            return left.score > right.score;
        });
}

BrainAuthorityStationsPreviewDecision BuildAuthorityControllerRejection(
    const TransceiverControllerEvidenceSnapshot& evidence,
    const char* decisionText,
    const char* reasonText) {
    BrainAuthorityStationsPreviewDecision decision;
    decision.callsign = evidence.callsign;
    decision.frequency = evidence.resolvedDisplayFrequency;
    decision.decision = decisionText;
    decision.reason = reasonText;
    return decision;
}

BrainAuthorityStationsPreviewDecision BuildAuthorityStationPreviewDecision(
    const TransceiverControllerEvidenceSnapshot& evidence,
    const TransceiverStationEvidenceSnapshot& station,
    int stationIndex) {
    BrainAuthorityStationsPreviewDecision decision;
    decision.callsign = evidence.callsign;
    decision.hasStation = true;
    decision.stationIndex = stationIndex;
    decision.latitudeDeg = station.latitudeDeg;
    decision.longitudeDeg = station.longitudeDeg;

    const auto displayResolution = ResolveAuthorityPreviewDisplayFrequency(
        evidence.controllerFrequency,
        station.sourceFrequency);
    decision.frequency = displayResolution.frequency;

    if (displayResolution.unavailableReason.find("guard") !=
        std::string::npos) {
        decision.decision = kAuthorityPreviewRejectedGuardFrequency;
        decision.reason = "guard-frequency";
        return decision;
    }

    if (displayResolution.unavailableReason.find("empty") !=
        std::string::npos ||
        displayResolution.frequency.empty()) {
        decision.decision = kAuthorityPreviewRejectedEmptyFrequency;
        decision.reason = "empty-frequency";
        return decision;
    }

    decision.decision = kAuthorityPreviewSurvivor;
    decision.reason = "usable-authority-station-evidence";
    return decision;
}

bool AirportCoveragePreviewDecisionMatchesOldSurvivor(
    const BrainAirportCoveragePreviewDecision& decision,
    const ReceivableControllerSnapshot& survivor) {
    return decision.hasStation &&
           decision.callsign == survivor.callsign &&
           decision.frequency == survivor.frequency &&
           decision.latitudeDeg == survivor.latitudeDeg &&
           decision.longitudeDeg == survivor.longitudeDeg;
}

bool AirportCoveragePreviewSurvivorHasOldMatch(
    const ReceivableControllerSnapshot& survivor,
    std::vector<BrainAirportCoveragePreviewDecision>* decisions) {
    if (decisions == nullptr) {
        return false;
    }

    for (auto& decision : *decisions) {
        if (decision.decision != kAirportCoveragePreviewSurvivor ||
            decision.matchesOldSurvivor) {
            continue;
        }
        if (AirportCoveragePreviewDecisionMatchesOldSurvivor(
                decision,
                survivor)) {
            decision.matchesOldSurvivor = true;
            return true;
        }
    }
    return false;
}

bool AirportCoverageStationBetterForPreview(
    const TransceiverStationEvidenceSnapshot& candidate,
    const TransceiverStationEvidenceSnapshot& currentBest) {
    if (candidate.score != currentBest.score) {
        return candidate.score > currentBest.score;
    }
    return candidate.aircraftDistanceNm < currentBest.aircraftDistanceNm;
}

AirportCoverageBestStation FindBestAirportCoveragePreviewStation(
    const TransceiverControllerEvidenceSnapshot& evidence) {
    AirportCoverageBestStation best;
    for (std::size_t index = 0; index < evidence.stations.size(); ++index) {
        const auto& station = evidence.stations[index];
        if (!station.withinReceivableRange) {
            continue;
        }
        if (best.station == nullptr ||
            AirportCoverageStationBetterForPreview(station, *best.station)) {
            best.station = &station;
            best.index = static_cast<int>(index);
        }
    }
    return best;
}

BrainAirportCoveragePreviewDecision BuildAirportCoverageControllerRejection(
    const TransceiverControllerEvidenceSnapshot& evidence,
    const char* decisionText,
    const char* reasonText) {
    BrainAirportCoveragePreviewDecision decision;
    decision.callsign = evidence.callsign;
    decision.frequency = evidence.resolvedDisplayFrequency;
    decision.decision = decisionText;
    decision.reason = reasonText;
    return decision;
}

BrainAirportCoveragePreviewDecision BuildAirportCoverageStationPreviewDecision(
    const TransceiverControllerEvidenceSnapshot& evidence,
    const TransceiverStationEvidenceSnapshot& station,
    int stationIndex) {
    BrainAirportCoveragePreviewDecision decision;
    decision.callsign = evidence.callsign;
    decision.hasStation = true;
    decision.stationIndex = stationIndex;
    decision.distanceNm = station.aircraftDistanceNm;
    decision.score = station.score;
    decision.latitudeDeg = station.latitudeDeg;
    decision.longitudeDeg = station.longitudeDeg;

    const auto displayResolution = ResolveAuthorityPreviewDisplayFrequency(
        evidence.controllerFrequency,
        station.sourceFrequency);
    decision.frequency = displayResolution.frequency;

    if (displayResolution.unavailableReason.find("guard") !=
        std::string::npos) {
        decision.decision = kAirportCoveragePreviewRejectedGuardFrequency;
        decision.reason = "guard-frequency";
        return decision;
    }

    if (displayResolution.unavailableReason.find("empty") !=
        std::string::npos ||
        displayResolution.frequency.empty()) {
        decision.decision = kAirportCoveragePreviewRejectedEmptyFrequency;
        decision.reason = "empty-frequency";
        return decision;
    }

    decision.decision = kAirportCoveragePreviewSurvivor;
    decision.reason = "usable-airport-coverage-station-evidence";
    return decision;
}

bool EvidenceMatchesAnyOldSurvivor(
    const TransceiverControllerEvidenceSnapshot& evidence,
    const std::vector<ReceivableControllerSnapshot>& survivors) {
    return std::any_of(
        survivors.begin(),
        survivors.end(),
        [&](const auto& survivor) {
            return EvidenceMatchesOldSurvivor(evidence, survivor);
        });
}

bool SurvivorHasPreviewMatch(
    const ReceivableControllerSnapshot& survivor,
    const std::vector<BrainRadioRangePreviewDecision>& decisions) {
    return std::any_of(
        decisions.begin(),
        decisions.end(),
        [&](const auto& decision) {
            return decision.matchesOldSurvivor &&
                   decision.callsign == survivor.callsign &&
                   decision.frequency == survivor.frequency &&
                   decision.decision == kPreviewSurvivor;
        });
}

bool HasOverMaxStation(
    const TransceiverControllerEvidenceSnapshot& evidence) {
    return std::any_of(
        evidence.stations.begin(),
        evidence.stations.end(),
        [](const auto& station) {
            return !station.withinMaxCandidateDistance;
        });
}

bool HasStationWithinMaxDistance(
    const TransceiverControllerEvidenceSnapshot& evidence) {
    return std::any_of(
        evidence.stations.begin(),
        evidence.stations.end(),
        [](const auto& station) {
            return station.withinMaxCandidateDistance;
        });
}

bool HasBeyondReceivableRangeStation(
    const TransceiverControllerEvidenceSnapshot& evidence) {
    return std::any_of(
        evidence.stations.begin(),
        evidence.stations.end(),
        [](const auto& station) {
            return station.withinMaxCandidateDistance &&
                   !station.withinReceivableRange;
        });
}

bool StationUsableForBrainRadioRange(
    const TransceiverStationEvidenceSnapshot& station) {
    return station.withinMaxCandidateDistance &&
           station.withinReceivableRange;
}

bool StationBetterForBrainRadioRange(
    const TransceiverStationEvidenceSnapshot& candidate,
    const TransceiverStationEvidenceSnapshot& currentBest) {
    if (candidate.score != currentBest.score) {
        return candidate.score > currentBest.score;
    }
    return candidate.aircraftDistanceNm < currentBest.aircraftDistanceNm;
}

const TransceiverStationEvidenceSnapshot* FindBestBrainUsableStation(
    const TransceiverControllerEvidenceSnapshot& evidence) {
    const TransceiverStationEvidenceSnapshot* best = nullptr;
    for (const auto& station : evidence.stations) {
        if (!StationUsableForBrainRadioRange(station)) {
            continue;
        }
        if (best == nullptr ||
            StationBetterForBrainRadioRange(station, *best)) {
            best = &station;
        }
    }
    return best;
}

BrainRadioRangePreviewDecision BuildPreviewDecision(
    const TransceiverControllerEvidenceSnapshot& evidence,
    const std::vector<ReceivableControllerSnapshot>& oldSurvivors) {
    BrainRadioRangePreviewDecision decision;
    decision.callsign = evidence.callsign;
    decision.frequency = evidence.resolvedDisplayFrequency;
    decision.matchesOldSurvivor =
        EvidenceMatchesAnyOldSurvivor(evidence, oldSurvivors);

    if (!evidence.actionable) {
        decision.decision = kPreviewRejectedNonActionable;
        decision.reason = "controller-not-actionable";
        return decision;
    }

    if (!evidence.hasTransceiverEntry) {
        decision.decision = kPreviewRejectedMissingTransceiver;
        decision.reason = "missing-transceiver";
        return decision;
    }

    const auto* bestStation = FindBestBrainUsableStation(evidence);
    if (bestStation == nullptr) {
        if (!HasStationWithinMaxDistance(evidence) &&
            HasOverMaxStation(evidence)) {
            decision.decision = kPreviewRejectedOverMaxDistance;
            decision.reason = "station-over-max-distance";
            return decision;
        }

        if (HasBeyondReceivableRangeStation(evidence)) {
            decision.decision = kPreviewRejectedBeyondReceivableRange;
            decision.reason = "station-beyond-receivable-range";
            return decision;
        }

        if (HasOverMaxStation(evidence)) {
            decision.decision = kPreviewRejectedOverMaxDistance;
            decision.reason = "station-over-max-distance";
            return decision;
        }

        decision.decision = kPreviewRejectedNoUsableStation;
        decision.reason = "no-usable-station";
        return decision;
    }

    if (evidence.displayFrequencyUnavailableReason.find("guard") !=
        std::string::npos) {
        decision.decision = kPreviewRejectedGuardFrequency;
        decision.reason = "guard-frequency";
        return decision;
    }

    if (evidence.displayFrequencyUnavailableReason.find("empty") !=
        std::string::npos) {
        decision.decision = kPreviewRejectedEmptyFrequency;
        decision.reason = "empty-frequency";
        return decision;
    }

    if (evidence.resolvedDisplayFrequency.empty()) {
        decision.decision = kPreviewRejectedEmptyFrequency;
        decision.reason = "empty-frequency";
        return decision;
    }

    decision.decision = kPreviewSurvivor;
    decision.reason = "usable-station-evidence";
    decision.hasStation = true;
    decision.distanceNm = bestStation->aircraftDistanceNm;
    decision.score = bestStation->score;
    decision.latitudeDeg = bestStation->latitudeDeg;
    decision.longitudeDeg = bestStation->longitudeDeg;
    return decision;
}

BrainRadioRangeDecisionPreview BuildBrainRadioRangeDecisionPreview(
    const TransceiverResolutionSnapshot& transceivers) {
    BrainRadioRangeDecisionPreview preview;
    preview.decisions.reserve(transceivers.controllerEvidence.size());
    preview.summary.liveCandidatesBrainOwned =
        !transceivers.controllerEvidence.empty();
    preview.summary.evidenceControllerCount =
        static_cast<int>(transceivers.controllerEvidence.size());
    preview.summary.oldSurvivorCount =
        static_cast<int>(transceivers.candidates.size());
    preview.summary.resolverCandidatesCompatibilityOnly =
        transceivers.candidatesCompatibilityOnly;
    preview.summary.droppedBeforeBrainControllers =
        transceivers.droppedBeforeBrainControllers;

    for (const auto& evidence : transceivers.controllerEvidence) {
        auto decision = BuildPreviewDecision(evidence, transceivers.candidates);
        if (decision.decision == kPreviewSurvivor) {
            ++preview.summary.previewSurvivorCount;
        } else {
            ++preview.summary.previewRejectedCount;
        }
        preview.decisions.push_back(std::move(decision));
    }

    for (const auto& survivor : transceivers.candidates) {
        if (!SurvivorHasPreviewMatch(survivor, preview.decisions)) {
            ++preview.summary.oldSurvivorMismatchCount;
        }
    }
    for (const auto& decision : preview.decisions) {
        if (decision.decision == kPreviewSurvivor &&
            !decision.matchesOldSurvivor) {
            ++preview.summary.oldSurvivorMismatchCount;
        }
    }

    return preview;
}

TransceiverResolutionSnapshot BuildBrainOwnedRadioCandidateSnapshot(
    TransceiverResolutionSnapshot transceivers,
    const BrainRadioRangeDecisionPreview& preview) {
    if (!preview.summary.liveCandidatesBrainOwned) {
        return transceivers;
    }

    transceivers.candidates.clear();
    for (const auto& decision : preview.decisions) {
        if (decision.decision != kPreviewSurvivor ||
            decision.frequency.empty() ||
            !decision.hasStation) {
            continue;
        }

        ReceivableControllerSnapshot candidate;
        candidate.callsign = decision.callsign;
        candidate.frequency = decision.frequency;
        candidate.distanceNm = decision.distanceNm;
        candidate.score = decision.score;
        candidate.latitudeDeg = decision.latitudeDeg;
        candidate.longitudeDeg = decision.longitudeDeg;
        transceivers.candidates.push_back(std::move(candidate));
    }
    transceivers.receivableControllers =
        static_cast<int>(transceivers.candidates.size());
    return transceivers;
}

}  // namespace

BrainRadioRangeWorkerOutput BuildBrainRadioRangeWorkerOutput(
    const BrainRadioRangeWorkerInput& input,
    const TransceiverResolutionSnapshot& transceivers,
    double nowSeconds) {
    BrainRadioRangeWorkerOutput output;
    output.transceivers =
        ApplyBrainOwnedRadioCandidateEnvelope(transceivers);
    output.decisionPreview =
        BuildBrainRadioRangeDecisionPreview(output.transceivers);
    const auto radioAuthorityTransceivers =
        BuildBrainOwnedRadioCandidateSnapshot(
            output.transceivers,
            output.decisionPreview);

    RadioReachableBuildOptions options;
    options.available = radioAuthorityTransceivers.available;
    options.stale = radioAuthorityTransceivers.stale;
    options.generation = input.controllerFeed.generation;
    options.source = RadioReachableSource::AFVRadioRange;
    options.changeReason =
        output.decisionPreview.summary.liveCandidatesBrainOwned
            ? "brain-radio-range-worker-evidence-authority"
            : "brain-radio-range-worker";
    options.nowSeconds = nowSeconds;
    output.radioBoard =
        BuildRadioReachableControllerSnapshotFromTransceivers(
            radioAuthorityTransceivers,
            input.controllerFeed,
            options);
    output.available = output.radioBoard.available;
    output.stale = output.radioBoard.stale;
    output.reason = output.radioBoard.statusLine;
    return output;
}

BrainAuthorityStationsDecisionPreview
BuildBrainAuthorityStationsDecisionPreview(
    const TransceiverResolutionSnapshot& transceivers) {
    BrainAuthorityStationsDecisionPreview preview;
    preview.summary.path = transceivers.resolutionPath;
    preview.summary.liveCandidatesBrainOwned =
        transceivers.resolutionPath == kAuthorityStationsPath &&
        !transceivers.controllerEvidence.empty();
    preview.summary.evidenceControllerCount =
        static_cast<int>(transceivers.controllerEvidence.size());
    preview.summary.oldSurvivorCount =
        static_cast<int>(transceivers.candidates.size());
    preview.summary.resolverCandidatesCompatibilityOnly =
        transceivers.candidatesCompatibilityOnly;
    preview.summary.droppedBeforeBrainControllers =
        transceivers.droppedBeforeBrainControllers;

    if (transceivers.resolutionPath != kAuthorityStationsPath) {
        preview.summary.oldSurvivorMismatchCount =
            preview.summary.oldSurvivorCount;
        return preview;
    }

    for (const auto& evidence : transceivers.controllerEvidence) {
        if (!evidence.actionable) {
            preview.decisions.push_back(
                BuildAuthorityControllerRejection(
                    evidence,
                    kAuthorityPreviewRejectedNonActionable,
                    "controller-not-actionable"));
            continue;
        }

        if (!evidence.hasTransceiverEntry) {
            preview.decisions.push_back(
                BuildAuthorityControllerRejection(
                    evidence,
                    kAuthorityPreviewRejectedMissingTransceiver,
                    "missing-transceiver"));
            continue;
        }

        if (evidence.stations.empty()) {
            preview.decisions.push_back(
                BuildAuthorityControllerRejection(
                    evidence,
                    kAuthorityPreviewRejectedNoUsableStation,
                    "no-usable-station"));
            continue;
        }

        for (std::size_t index = 0; index < evidence.stations.size(); ++index) {
            preview.decisions.push_back(
                BuildAuthorityStationPreviewDecision(
                    evidence,
                    evidence.stations[index],
                    static_cast<int>(index)));
        }
    }

    for (const auto& decision : preview.decisions) {
        if (decision.decision == kAuthorityPreviewSurvivor) {
            ++preview.summary.previewSurvivorCount;
        } else {
            ++preview.summary.previewRejectedCount;
        }
    }

    for (const auto& survivor : transceivers.candidates) {
        if (!AuthorityPreviewSurvivorHasOldMatch(
                survivor,
                &preview.decisions)) {
            ++preview.summary.oldSurvivorMismatchCount;
        }
    }

    for (const auto& decision : preview.decisions) {
        if (decision.decision == kAuthorityPreviewSurvivor &&
            !decision.matchesOldSurvivor) {
            ++preview.summary.oldSurvivorMismatchCount;
        }
    }

    return preview;
}

TransceiverResolutionSnapshot BuildBrainOwnedAuthorityStationsCandidateSnapshot(
    TransceiverResolutionSnapshot transceivers,
    const BrainAuthorityStationsDecisionPreview& preview) {
    if (!preview.summary.liveCandidatesBrainOwned) {
        return transceivers;
    }

    transceivers.candidates.clear();
    for (const auto& decision : preview.decisions) {
        if (decision.decision != kAuthorityPreviewSurvivor ||
            decision.frequency.empty() ||
            !decision.hasStation) {
            continue;
        }

        ReceivableControllerSnapshot candidate;
        candidate.callsign = decision.callsign;
        candidate.frequency = decision.frequency;
        candidate.distanceNm = 0.0;
        candidate.score = 0.0;
        candidate.latitudeDeg = decision.latitudeDeg;
        candidate.longitudeDeg = decision.longitudeDeg;
        transceivers.candidates.push_back(std::move(candidate));
    }

    SortAuthorityStationCandidates(&transceivers.candidates);
    transceivers.receivableControllers =
        static_cast<int>(transceivers.candidates.size());
    transceivers.candidatesCompatibilityOnly = false;
    if (transceivers.available && !transceivers.stale) {
        transceivers.statusLine =
            "AUTHORITY stations " +
            std::to_string(transceivers.receivableControllers) + " located";
    }
    return transceivers;
}

BrainAirportCoverageDecisionPreview BuildBrainAirportCoverageDecisionPreview(
    const TransceiverResolutionSnapshot& transceivers) {
    BrainAirportCoverageDecisionPreview preview;
    preview.summary.path = transceivers.resolutionPath;
    preview.summary.liveCandidatesBrainOwned =
        transceivers.resolutionPath == kAirportCoveragePath &&
        !transceivers.controllerEvidence.empty();
    preview.summary.evidenceControllerCount =
        static_cast<int>(transceivers.controllerEvidence.size());
    preview.summary.oldSurvivorCount =
        static_cast<int>(transceivers.candidates.size());
    preview.summary.resolverCandidatesCompatibilityOnly =
        transceivers.candidatesCompatibilityOnly;
    preview.summary.droppedBeforeBrainControllers =
        transceivers.droppedBeforeBrainControllers;

    if (transceivers.resolutionPath != kAirportCoveragePath) {
        preview.summary.oldSurvivorMismatchCount =
            preview.summary.oldSurvivorCount;
        return preview;
    }

    for (const auto& evidence : transceivers.controllerEvidence) {
        if (!evidence.actionable) {
            preview.decisions.push_back(
                BuildAirportCoverageControllerRejection(
                    evidence,
                    kAirportCoveragePreviewRejectedNonActionable,
                    "controller-not-actionable"));
            continue;
        }

        if (!evidence.hasTransceiverEntry) {
            preview.decisions.push_back(
                BuildAirportCoverageControllerRejection(
                    evidence,
                    kAirportCoveragePreviewRejectedMissingTransceiver,
                    "missing-transceiver"));
            continue;
        }

        if (evidence.stations.empty()) {
            preview.decisions.push_back(
                BuildAirportCoverageControllerRejection(
                    evidence,
                    kAirportCoveragePreviewRejectedNoUsableStation,
                    "no-usable-station"));
            continue;
        }

        const auto best = FindBestAirportCoveragePreviewStation(evidence);
        if (best.station == nullptr) {
            preview.decisions.push_back(
                BuildAirportCoverageControllerRejection(
                    evidence,
                    evidence.stations.size() > 1
                        ? kAirportCoveragePreviewRejectedAllStationsFailed
                        : kAirportCoveragePreviewRejectedOutOfCoverage,
                    evidence.stations.size() > 1
                        ? "all-stations-failed"
                        : "station-out-of-coverage"));
            continue;
        }

        preview.decisions.push_back(
            BuildAirportCoverageStationPreviewDecision(
                evidence,
                *best.station,
                best.index));
    }

    for (const auto& decision : preview.decisions) {
        if (decision.decision == kAirportCoveragePreviewSurvivor) {
            ++preview.summary.previewSurvivorCount;
        } else {
            ++preview.summary.previewRejectedCount;
        }
    }

    for (const auto& survivor : transceivers.candidates) {
        if (!AirportCoveragePreviewSurvivorHasOldMatch(
                survivor,
                &preview.decisions)) {
            ++preview.summary.oldSurvivorMismatchCount;
        }
    }

    for (const auto& decision : preview.decisions) {
        if (decision.decision == kAirportCoveragePreviewSurvivor &&
            !decision.matchesOldSurvivor) {
            ++preview.summary.oldSurvivorMismatchCount;
        }
    }

    return preview;
}

TransceiverResolutionSnapshot BuildBrainOwnedAirportCoverageCandidateSnapshot(
    TransceiverResolutionSnapshot transceivers,
    const BrainAirportCoverageDecisionPreview& preview) {
    if (!preview.summary.liveCandidatesBrainOwned) {
        return transceivers;
    }

    transceivers.candidates.clear();
    for (const auto& decision : preview.decisions) {
        if (decision.decision != kAirportCoveragePreviewSurvivor ||
            decision.frequency.empty() ||
            !decision.hasStation) {
            continue;
        }

        ReceivableControllerSnapshot candidate;
        candidate.callsign = decision.callsign;
        candidate.frequency = decision.frequency;
        candidate.distanceNm = decision.distanceNm;
        candidate.score = decision.score;
        candidate.latitudeDeg = decision.latitudeDeg;
        candidate.longitudeDeg = decision.longitudeDeg;
        transceivers.candidates.push_back(std::move(candidate));
    }

    SortAirportCoverageCandidates(&transceivers.candidates);
    transceivers.receivableControllers =
        static_cast<int>(transceivers.candidates.size());
    transceivers.candidatesCompatibilityOnly = false;
    if (transceivers.available && !transceivers.stale) {
        transceivers.statusLine =
            "AIRSPACE " +
            std::to_string(transceivers.receivableControllers) +
            " covering";
    }
    return transceivers;
}

}  // namespace xvatsim::brain
