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

const char* TerminalRelationshipToken(
    TerminalEndpointRelationship relationship) {
    switch (relationship) {
        case TerminalEndpointRelationship::DirectEndpoint:
            return "DIRECT_ENDPOINT";
        case TerminalEndpointRelationship::ExplicitParent:
            return "EXPLICIT_PARENT";
        case TerminalEndpointRelationship::NearbyOnly:
            return "NEARBY_ONLY";
        case TerminalEndpointRelationship::Unrelated:
            return "UNRELATED";
        case TerminalEndpointRelationship::Unknown:
            return "UNKNOWN";
        case TerminalEndpointRelationship::NotApplicable:
        default:
            return "NOT_APPLICABLE";
    }
}

BrainControllerDecisionReceipt CollectEvidence(
    const BrainControllerDecisionInput& input,
    bool diagnosticOnly) {
    BrainControllerDecisionReceipt receipt;

    // Every raw fact remains countable. Only facts with the same explicit
    // deduplication key (or the same legacy source key) are folded together.
    std::map<std::string, BrainControllerEvidenceVote> bySource;
    for (const auto& raw : input.votes) {
        auto vote = raw;
        vote.score = Ternary(vote.score);
        if (diagnosticOnly) vote.contributesToScore = false;
        if (vote.source.empty()) vote.source = "unknown";
        if (vote.score > 0) ++receipt.rawPositiveFacts;
        else if (vote.score < 0) ++receipt.rawNegativeFacts;
        else ++receipt.rawNeutralFacts;
        const auto key = vote.deduplicationKey.empty()
            ? vote.source
            : vote.deduplicationKey;
        const auto found = bySource.find(key);
        if (found == bySource.end()) {
            bySource.emplace(key, std::move(vote));
            continue;
        }
        auto& existing = found->second;
        existing.provenance = JoinReason(existing.provenance, vote.provenance);
        existing.reason = JoinReason(existing.reason, vote.reason);
        existing.contributesToScore =
            existing.contributesToScore || vote.contributesToScore;
        if (existing.score != vote.score) {
            existing.score = 0;
            existing.reason = JoinReason(existing.reason, "conflicting-source-facts");
        }
    }

    for (auto& [source, vote] : bySource) {
        if (vote.contributesToScore) {
            if (vote.score > 0) ++receipt.positiveVotes;
            else if (vote.score < 0) ++receipt.negativeVotes;
            else ++receipt.neutralVotes;
        }
        receipt.votes.push_back(std::move(vote));
    }
    return receipt;
}

}  // namespace

BrainControllerDecisionReceipt DecideBrainControllerCandidate(
    const BrainControllerDecisionInput& input) {
    auto receipt = CollectEvidence(input, false);

    if (!input.ineligibleReason.empty()) receipt.reason = input.ineligibleReason;
    else if (!input.actionable) receipt.reason = "brain-ineligible-offline";
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

BrainControllerDecisionReceipt DecideBrainTerminalControllerCandidate(
    const BrainControllerDecisionInput& input,
    TerminalEndpointRelationship relationship,
    std::string relationshipProof,
    bool relationshipBoardEligible,
    std::string relationshipIneligibleReason) {
    // Terminal evidence retains its original polarity so diagnostics can show
    // what each source reported. Every fact is forcibly non-scoring here.
    auto receipt = CollectEvidence(input, true);
    receipt.terminalRelationship = relationship;
    receipt.terminalRelationshipProof = std::move(relationshipProof);

    if (!input.ineligibleReason.empty()) receipt.reason = input.ineligibleReason;
    else if (!input.actionable) receipt.reason = "brain-ineligible-offline";
    else if (input.atis) receipt.reason = "brain-ineligible-atis";
    else if (!input.supportedFacility) receipt.reason = "brain-ineligible-facility";
    else if (input.guardFrequency) receipt.reason = "brain-ineligible-guard";
    else if (!input.phaseApplicable) receipt.reason = "brain-ineligible-phase";
    else if (!relationshipBoardEligible) {
        receipt.reason = relationshipIneligibleReason.empty()
            ? "brain-terminal-relationship-suppressed"
            : std::move(relationshipIneligibleReason);
    } else if (relationship == TerminalEndpointRelationship::DirectEndpoint) {
        receipt.accepted = true;
        receipt.reason = "brain-terminal-direct-endpoint-display";
    } else if (relationship == TerminalEndpointRelationship::ExplicitParent) {
        receipt.accepted = true;
        receipt.reason = "brain-terminal-explicit-parent-display";
    } else if (relationship == TerminalEndpointRelationship::NearbyOnly) {
        receipt.reason = "brain-terminal-nearby-only-hidden";
    } else if (relationship == TerminalEndpointRelationship::Unrelated) {
        receipt.reason = "brain-terminal-unrelated-hidden";
    } else {
        receipt.reason = "brain-terminal-relationship-unknown-hidden";
    }
    return receipt;
}

std::string FormatBrainControllerDecisionReceipt(
    const BrainControllerDecisionReceipt& receipt) {
    std::ostringstream out;
    out << "brain-facts=" << receipt.rawPositiveFacts << "/"
        << receipt.rawNegativeFacts << "/" << receipt.rawNeutralFacts
        << ":brain-votes=" << receipt.positiveVotes << "/"
        << receipt.negativeVotes << "/" << receipt.neutralVotes;
    if (receipt.terminalRelationship !=
        TerminalEndpointRelationship::NotApplicable) {
        out << ":terminal-relationship="
            << TerminalRelationshipToken(receipt.terminalRelationship);
        if (!receipt.terminalRelationshipProof.empty()) {
            out << "[" << receipt.terminalRelationshipProof << "]";
        }
    }
    for (const auto& vote : receipt.votes) {
        out << ":" << vote.source << "="
            << (vote.score > 0 ? "+1" : vote.score < 0 ? "-1" : "0")
            << "[" << vote.reason;
        if (!vote.provenance.empty()) out << "@" << vote.provenance;
        if (!vote.contributesToScore) out << "@fact-only";
        out << "]";
    }
    out << ":brain-final=" << (receipt.accepted ? "display" : "hide")
        << ":brain-reason=" << receipt.reason;
    return out.str();
}

}  // namespace xvatsim::brain
