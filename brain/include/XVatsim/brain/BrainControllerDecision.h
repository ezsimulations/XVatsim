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
    std::string ineligibleReason;
    std::vector<BrainControllerEvidenceVote> votes;
};

struct BrainControllerDecisionReceipt {
    bool accepted = false;
    int positiveVotes = 0;
    int negativeVotes = 0;
    int neutralVotes = 0;
    int rawPositiveFacts = 0;
    int rawNegativeFacts = 0;
    int rawNeutralFacts = 0;
    TerminalEndpointRelationship terminalRelationship =
        TerminalEndpointRelationship::NotApplicable;
    std::string terminalRelationshipProof;
    std::string reason;
    std::vector<BrainControllerEvidenceVote> votes;
};

// The Brain is the only owner of candidate acceptance. Evidence producers may
// populate the input facts and ternary votes, but they cannot pre-filter them.
BrainControllerDecisionReceipt DecideBrainControllerCandidate(
    const BrainControllerDecisionInput& input);

// APP/DEP endpoint membership is relationship-based. The evidence votes in
// the input are preserved only as non-authoritative diagnostics; neither their
// totals nor a tie may create an endpoint row.
BrainControllerDecisionReceipt DecideBrainTerminalControllerCandidate(
    const BrainControllerDecisionInput& input,
    TerminalEndpointRelationship relationship,
    std::string relationshipProof,
    bool relationshipBoardEligible = true,
    std::string relationshipIneligibleReason = {});

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
