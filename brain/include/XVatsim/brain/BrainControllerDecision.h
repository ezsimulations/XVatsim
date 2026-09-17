#pragma once

#include <string>
#include <vector>

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::brain {

struct BrainControllerDecisionInput {
    bool actionable = true;
    bool atis = false;
    bool supportedFacility = true;
    bool guardFrequency = false;
    bool phaseApplicable = true;
    bool relationAvailable = true;
    bool radioReachable = true;
    bool ownershipApplicable = true;
    bool supportedTiesDisplay = true;
    std::vector<BrainControllerEvidenceVote> votes;
};

struct BrainControllerDecisionReceipt {
    bool accepted = false;
    int positiveVotes = 0;
    int negativeVotes = 0;
    int neutralVotes = 0;
    std::string reason;
    std::vector<BrainControllerEvidenceVote> votes;
};

// The Brain is the only owner of candidate acceptance. Evidence producers may
// populate the input facts and ternary votes, but they cannot pre-filter them.
BrainControllerDecisionReceipt DecideBrainControllerCandidate(
    const BrainControllerDecisionInput& input);

std::string FormatBrainControllerDecisionReceipt(
    const BrainControllerDecisionReceipt& receipt);

struct BrainControllerRelevanceWorkerInput;
struct BrainControllerRelevanceWorkerOutput;

// Fresh V2.0.1-baseline evidence pipeline. Every candidate reaches this
// function; workers contribute facts and the Brain produces the sole display
// decision plus an auditable receipt.
BrainControllerRelevanceWorkerOutput RunBrainControllerEvidencePipeline(
    const BrainControllerRelevanceWorkerInput& input);

}  // namespace xvatsim::brain
