#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "XVatsim/brain/BrainOwnedRuntime.h"

namespace xvatsim::modules::runtime_workers {

constexpr std::size_t kCandidateDecisionDiagnosticRecordMaxChars = 3900;
constexpr std::size_t kCandidateDecisionVotesPerRecord = 12;

struct CandidateDecisionDiagnosticsContext {
    std::string phase;
    std::string planKey;
    std::string route;
    std::string currentPolygon;
    std::string nextPolygon;
    std::string arrivalPolygon;
    int currentPolygonIndex = 0;
    std::uint64_t rawRadioHash = 0;
    std::uint64_t gatedRadioHash = 0;
    std::uint64_t routePolygonHash = 0;
    bool candidatesComplete = false;
};

struct CandidateDecisionDiagnosticRemoval {
    std::string candidateKey;
};

struct CandidateDecisionDiagnosticsBatch {
    bool emit = false;
    bool baseline = false;
    std::uint64_t sessionGeneration = 0;
    std::uint64_t decisionGeneration = 0;
    std::uint64_t decisionDigest = 0;
    CandidateDecisionDiagnosticsContext context;
    std::size_t observedCandidates = 0;
    std::size_t changedCandidates = 0;
    std::size_t removedCandidates = 0;
    std::size_t acceptedCandidates = 0;
    std::size_t rejectedCandidates = 0;
    std::size_t pendingCandidates = 0;
    std::size_t displayedCandidates = 0;
    std::vector<brain::BrainOwnedCandidateCompletion> changedCompletions;
    std::vector<CandidateDecisionDiagnosticRemoval> removals;
};

// Observes immutable, completed Brain decision receipts after the Brain has
// made its controller decision. This class controls diagnostics emission only;
// it cannot alter candidate facts, votes, Brain decisions, or display state.
class CandidateDecisionDiagnosticsReducer {
public:
    CandidateDecisionDiagnosticsBatch Observe(
        const CandidateDecisionDiagnosticsContext& context,
        const std::vector<brain::BrainOwnedCandidateCompletion>& completions);

    void Reset() noexcept;
    std::size_t TrackedCandidateCount() const noexcept;

private:
    struct CandidateState {
        std::uint64_t semanticFingerprint = 0;
        std::string candidateKey;
    };

    bool initialized_ = false;
    std::string sessionKey_;
    std::uint64_t contextFingerprint_ = 0;
    std::uint64_t sessionGeneration_ = 1;
    std::uint64_t decisionGeneration_ = 0;
    std::unordered_map<std::string, CandidateState> candidates_;
};

std::string FormatCandidateDecisionDiagnosticsSummary(
    const CandidateDecisionDiagnosticsBatch& batch);

std::size_t CandidateDecisionDiagnosticReceiptRecordCount(
    const brain::BrainOwnedCandidateCompletion& completion) noexcept;

std::string FormatCandidateDecisionDiagnosticReceiptRecord(
    const CandidateDecisionDiagnosticsContext& context,
    const brain::BrainOwnedCandidateCompletion& completion,
    bool baseline,
    std::size_t recordIndex);

std::string FormatCandidateDecisionDiagnosticRemoval(
    const CandidateDecisionDiagnosticsContext& context,
    const CandidateDecisionDiagnosticRemoval& removal,
    bool baseline);

}  // namespace xvatsim::modules::runtime_workers
