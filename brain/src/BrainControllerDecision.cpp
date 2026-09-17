#include "XVatsim/brain/BrainControllerDecision.h"

#include <algorithm>
#include <map>
#include <sstream>

namespace xvatsim::brain {
namespace {

int Ternary(int score) {
    return score > 0 ? 1 : (score < 0 ? -1 : 0);
}

std::string JoinReason(const std::string& left, const std::string& right) {
    if (left.empty()) return right;
    if (right.empty()) return left;
    return left + "+" + right;
}

}  // namespace

BrainControllerDecisionReceipt DecideBrainControllerCandidate(
    const BrainControllerDecisionInput& input) {
    BrainControllerDecisionReceipt receipt;

    // One source family gets one vote. Repeated facts from a family are folded
    // together; contradictory values become neutral and remain diagnosable.
    std::map<std::string, BrainControllerEvidenceVote> bySource;
    for (const auto& raw : input.votes) {
        auto vote = raw;
        vote.score = Ternary(vote.score);
        if (vote.source.empty()) vote.source = "unknown";
        const auto found = bySource.find(vote.source);
        if (found == bySource.end()) {
            bySource.emplace(vote.source, std::move(vote));
            continue;
        }
        auto& existing = found->second;
        existing.provenance = JoinReason(existing.provenance, vote.provenance);
        existing.reason = JoinReason(existing.reason, vote.reason);
        if (existing.score != vote.score) {
            existing.score = 0;
            existing.reason = JoinReason(existing.reason, "conflicting-source-facts");
        }
    }

    for (auto& [source, vote] : bySource) {
        if (vote.score > 0) ++receipt.positiveVotes;
        else if (vote.score < 0) ++receipt.negativeVotes;
        else ++receipt.neutralVotes;
        receipt.votes.push_back(std::move(vote));
    }

    if (!input.actionable) receipt.reason = "brain-ineligible-offline";
    else if (input.atis) receipt.reason = "brain-ineligible-atis";
    else if (!input.supportedFacility) receipt.reason = "brain-ineligible-facility";
    else if (input.guardFrequency) receipt.reason = "brain-ineligible-guard";
    else if (!input.phaseApplicable) receipt.reason = "brain-ineligible-phase";
    else if (!input.ownershipApplicable)
        receipt.reason = "brain-ineligible-operational-ownership";
    else if (!input.relationAvailable) receipt.reason = "brain-ineligible-relation";
    else if (!input.radioReachable) receipt.reason = "brain-ineligible-radio-range";
    else if (receipt.positiveVotes == 0) receipt.reason = "brain-no-positive-evidence";
    else if (!input.supportedTiesDisplay &&
             receipt.negativeVotes == receipt.positiveVotes)
        receipt.reason = "brain-legacy-tie-hidden";
    else if (receipt.negativeVotes > receipt.positiveVotes)
        receipt.reason = "brain-negative-majority";
    else {
        receipt.accepted = true;
        receipt.reason = receipt.positiveVotes == receipt.negativeVotes
            ? "brain-supported-tie-display"
            : "brain-positive-majority-display";
    }
    return receipt;
}

std::string FormatBrainControllerDecisionReceipt(
    const BrainControllerDecisionReceipt& receipt) {
    std::ostringstream out;
    out << "brain-votes=" << receipt.positiveVotes << "/"
        << receipt.negativeVotes << "/" << receipt.neutralVotes;
    for (const auto& vote : receipt.votes) {
        out << ":" << vote.source << "="
            << (vote.score > 0 ? "+1" : vote.score < 0 ? "-1" : "0")
            << "[" << vote.reason;
        if (!vote.provenance.empty()) out << "@" << vote.provenance;
        out << "]";
    }
    out << ":brain-final=" << (receipt.accepted ? "display" : "hide")
        << ":brain-reason=" << receipt.reason;
    return out.str();
}

}  // namespace xvatsim::brain
