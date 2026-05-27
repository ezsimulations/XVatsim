#include "XVatsim/brain/BrainOwnedWorkerTypes.h"

#include <cmath>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace xvatsim::brain {

namespace {

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

}  // namespace

BrainRadioRangeWorkerOutput BuildBrainRadioRangeWorkerOutput(
    const BrainRadioRangeWorkerInput& input,
    const TransceiverResolutionSnapshot& transceivers,
    double nowSeconds) {
    BrainRadioRangeWorkerOutput output;
    output.transceivers =
        ApplyBrainOwnedRadioCandidateEnvelope(transceivers);

    RadioReachableBuildOptions options;
    options.available = output.transceivers.available;
    options.stale = output.transceivers.stale;
    options.generation = input.controllerFeed.generation;
    options.source = RadioReachableSource::AFVRadioRange;
    options.changeReason = "brain-radio-range-worker";
    options.nowSeconds = nowSeconds;
    output.radioBoard =
        BuildRadioReachableControllerSnapshotFromTransceivers(
            output.transceivers,
            input.controllerFeed,
            options);
    output.available = output.radioBoard.available;
    output.stale = output.radioBoard.stale;
    output.reason = output.radioBoard.statusLine;
    return output;
}

}  // namespace xvatsim::brain
