#include "XVatsim/brain/BrainOwnedWorkerTypes.h"

namespace xvatsim::brain {

BrainRadioRangeWorkerOutput BuildBrainRadioRangeWorkerOutput(
    const BrainRadioRangeWorkerInput& input,
    const TransceiverResolutionSnapshot& transceivers,
    double nowSeconds) {
    BrainRadioRangeWorkerOutput output;
    output.transceivers = transceivers;

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
