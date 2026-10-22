#include "XVatsim/modules/runtime_workers/CandidateDecisionDiagnosticsReducer.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace xvatsim::modules::runtime_workers {
namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void HashText(std::uint64_t* hash, std::string_view value) noexcept {
    if (hash == nullptr) return;
    for (const auto character : value) {
        *hash ^= static_cast<unsigned char>(character);
        *hash *= kFnvPrime;
    }
    *hash ^= 0xffU;
    *hash *= kFnvPrime;
}

template <typename Value>
void HashNumber(std::uint64_t* hash, Value value) {
    HashText(hash, std::to_string(value));
}

std::string SanitizeToken(std::string value, std::size_t maxChars) {
    if (value.size() > maxChars) value.resize(maxChars);
    for (auto& character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte <= 0x20U || character == '"' || character == '\'' ||
            character == ',' || character == ';' || character == '|' ||
            character == '(' || character == ')') {
            character = '_';
        }
    }
    return value.empty() ? "none" : value;
}

std::string CandidateIdentity(
    const brain::BrainOwnedCandidateCompletion& completion) {
    std::ostringstream stream;
    stream << completion.callsign << '\x1f' << completion.frequency << '\x1f'
           << static_cast<int>(completion.facilityGroup);
    return stream.str();
}

std::string CandidateDisplayKey(
    const brain::BrainOwnedCandidateCompletion& completion) {
    std::ostringstream stream;
    stream << SanitizeToken(completion.callsign, 48) << "@"
           << SanitizeToken(completion.frequency, 20) << ":"
           << brain::ToString(completion.facilityGroup);
    return stream.str();
}

std::string FinalBrainReason(std::string_view reason) {
    constexpr std::string_view marker = ":brain-reason=";
    const auto position = reason.rfind(marker);
    if (position == std::string_view::npos) {
        return std::string{reason};
    }
    return std::string{reason.substr(position + marker.size())};
}

std::string SemanticVoteProvenance(
    const brain::BrainControllerEvidenceVote& vote) {
    if (vote.source == "frequency-distance") {
        // The exact moving distance is recorded in an emitted receipt. It is
        // not itself a new decision while the ternary vote and reason remain
        // unchanged. A threshold crossing changes the score/reason and emits.
        return {};
    }
    if (vote.source == "vatsim-feed-health") {
        // Global roster record counts can change whenever any controller in
        // the world logs in or out. The score and reason already capture the
        // only semantic health state used by the Brain.
        return {};
    }
    if (vote.source == "vatsim-roster" &&
        vote.reason != "candidate-exact-endpoint-present") {
        // Keep exact endpoint identity evidence, but do not make every scoped
        // candidate look changed when only global declared/retained/rejected
        // counters move.
        return {};
    }
    if (vote.source != "xpilot-fsd-correlation") {
        return vote.provenance;
    }

    // xPilot process, connection, and snapshot generations describe transport
    // churn. Preserve any controller identity and frequency facts that follow
    // them because those are semantic evidence supplied to the Brain.
    std::istringstream tokens{vote.provenance};
    std::string token;
    std::ostringstream retained;
    bool first = true;
    while (std::getline(tokens, token, '_')) {
        if (token.rfind("process=", 0) == 0 ||
            token.rfind("connection=", 0) == 0 ||
            token.rfind("snapshot=", 0) == 0) {
            continue;
        }
        if (!first) retained << '_';
        retained << token;
        first = false;
    }
    return retained.str();
}

std::uint64_t CandidateSemanticFingerprint(
    const brain::BrainOwnedCandidateCompletion& completion) {
    auto hash = kFnvOffset;
    HashNumber(&hash, static_cast<int>(completion.workflowStage));
    HashNumber(&hash, completion.currentPolygonIndex);
    HashText(&hash, completion.currentPolygonKey);
    HashText(&hash, completion.matchedPolygonKey);
    HashText(&hash, completion.callsign);
    HashText(&hash, completion.frequency);
    HashNumber(&hash, static_cast<int>(completion.facilityGroup));
    HashNumber(&hash, static_cast<int>(completion.displayRelation));
    HashNumber(&hash, static_cast<int>(completion.decision));
    HashNumber(&hash, completion.displayed ? 1 : 0);
    HashNumber(&hash, completion.hasRouteEntryDistance ? 1 : 0);
    if (completion.hasRouteEntryDistance && completion.displayed) {
        HashNumber(
            &hash,
            static_cast<long long>(std::llround(completion.routeEntryDistanceNm)));
    }
    HashNumber(&hash, completion.positiveVotes);
    HashNumber(&hash, completion.negativeVotes);
    HashNumber(&hash, completion.neutralVotes);
    for (const auto& vote : completion.evidenceVotes) {
        HashText(&hash, vote.source);
        HashNumber(&hash, vote.score);
        HashText(&hash, vote.reason);
        HashText(&hash, SemanticVoteProvenance(vote));
    }
    HashText(&hash, FinalBrainReason(completion.reason));
    return hash;
}

std::uint64_t ContextSemanticFingerprint(
    const CandidateDecisionDiagnosticsContext& context) {
    auto hash = kFnvOffset;
    HashText(&hash, context.phase);
    HashText(&hash, context.planKey);
    HashText(&hash, context.route);
    HashText(&hash, context.currentPolygon);
    HashText(&hash, context.nextPolygon);
    HashText(&hash, context.arrivalPolygon);
    HashNumber(&hash, context.currentPolygonIndex);
    HashNumber(&hash, context.candidatesComplete ? 1 : 0);
    return hash;
}

std::string SessionKey(const CandidateDecisionDiagnosticsContext& context) {
    return context.phase + "\x1f" + context.planKey;
}

std::string ContextFields(const CandidateDecisionDiagnosticsContext& context) {
    std::ostringstream stream;
    stream << " phase=" << SanitizeToken(context.phase, 16)
           << " plan=" << SanitizeToken(context.planKey, 128)
           << " route=" << SanitizeToken(context.route, 40)
           << " currentPolygon=" << SanitizeToken(context.currentPolygon, 64)
           << " nextPolygon=" << SanitizeToken(context.nextPolygon, 64)
           << " arrivalPolygon=" << SanitizeToken(context.arrivalPolygon, 64)
           << " currentIndex=" << context.currentPolygonIndex;
    return stream.str();
}

std::string FormatDistance(bool available, double distanceNm) {
    if (!available) return "na";
    return std::to_string(static_cast<long long>(std::llround(distanceNm)));
}

std::string ScoreToken(int score) {
    if (score > 0) return "+1";
    if (score < 0) return "-1";
    return "0";
}

std::string CompactVotes(
    const std::vector<brain::BrainControllerEvidenceVote>& votes,
    std::size_t firstVote,
    std::size_t voteCount) {
    if (votes.empty()) return "none";
    std::ostringstream stream;
    const auto end = std::min(votes.size(), firstVote + voteCount);
    for (std::size_t index = firstVote; index < end; ++index) {
        if (index > firstVote) stream << ',';
        const auto& vote = votes[index];
        stream << SanitizeToken(vote.source, 40) << '=' << ScoreToken(vote.score)
               << '(' << SanitizeToken(vote.reason, 96);
        if (!vote.provenance.empty()) {
            stream << '@' << SanitizeToken(vote.provenance, 80);
        }
        stream << ')';
    }
    return stream.str();
}

std::string BuildReceipt(
    const CandidateDecisionDiagnosticsContext& context,
    const brain::BrainOwnedCandidateCompletion& completion,
    bool baseline,
    std::size_t recordIndex,
    std::size_t recordCount) {
    const auto receiptDigest = CandidateSemanticFingerprint(completion);
    std::ostringstream stream;
    stream << "event=candidate-vote-receipt"
           << " baseline=" << (baseline ? 1 : 0)
           << " receiptPart=" << (recordIndex + 1) << '/' << recordCount
           << " receiptDigest=" << std::hex << receiptDigest << std::dec
           << ContextFields(context)
           << " candidate=" << CandidateDisplayKey(completion)
           << " callsign=" << SanitizeToken(completion.callsign, 48)
           << " frequency=" << SanitizeToken(completion.frequency, 20)
           << " facility=" << brain::ToString(completion.facilityGroup)
           << " decision=" << brain::ToString(completion.decision)
           << " display=" << (completion.displayed ? "displayed" : "hidden")
           << " relation=" << brain::ToString(completion.displayRelation)
           << " matchedPolygon="
           << SanitizeToken(completion.matchedPolygonKey, 64)
           << " entryNm=" << FormatDistance(
                  completion.hasRouteEntryDistance,
                  completion.routeEntryDistanceNm)
           << " scoreTotals=" << completion.positiveVotes << '/'
           << completion.negativeVotes << '/' << completion.neutralVotes
           << " votes=" << CompactVotes(
                  completion.evidenceVotes,
                  recordIndex * kCandidateDecisionVotesPerRecord,
                  kCandidateDecisionVotesPerRecord)
           << " brainReason="
           << SanitizeToken(FinalBrainReason(completion.reason), 160);
    return stream.str();
}

}  // namespace

CandidateDecisionDiagnosticsBatch CandidateDecisionDiagnosticsReducer::Observe(
    const CandidateDecisionDiagnosticsContext& context,
    const std::vector<brain::BrainOwnedCandidateCompletion>& completions) {
    CandidateDecisionDiagnosticsBatch batch;
    if (!context.candidatesComplete) return batch;

    batch.context = context;
    batch.observedCandidates = completions.size();
    ++decisionGeneration_;

    const auto sessionKey = SessionKey(context);
    if (!initialized_ || sessionKey != sessionKey_) {
        if (initialized_) ++sessionGeneration_;
        initialized_ = true;
        sessionKey_ = sessionKey;
        contextFingerprint_ = 0;
        candidates_.clear();
        batch.baseline = true;
    }
    batch.sessionGeneration = sessionGeneration_;
    batch.decisionGeneration = decisionGeneration_;

    const auto nextContextFingerprint = ContextSemanticFingerprint(context);
    const auto contextChanged = nextContextFingerprint != contextFingerprint_;

    std::unordered_map<std::string, CandidateState> current;
    current.reserve(completions.size());
    std::unordered_map<std::string, std::size_t> duplicateCounts;
    duplicateCounts.reserve(completions.size());

    for (const auto& completion : completions) {
        switch (completion.decision) {
            case brain::BrainOwnedCandidateDecision::Accepted:
                ++batch.acceptedCandidates;
                break;
            case brain::BrainOwnedCandidateDecision::Rejected:
                ++batch.rejectedCandidates;
                break;
            case brain::BrainOwnedCandidateDecision::Pending:
            case brain::BrainOwnedCandidateDecision::NeedsVerification:
                ++batch.pendingCandidates;
                break;
        }
        if (completion.displayed) ++batch.displayedCandidates;
        const auto identity = CandidateIdentity(completion);
        const auto occurrence = duplicateCounts[identity]++;
        const auto storageKey = identity + "\x1f" + std::to_string(occurrence);
        CandidateState state;
        state.semanticFingerprint = CandidateSemanticFingerprint(completion);
        state.candidateKey = CandidateDisplayKey(completion);
        current.emplace(storageKey, state);
        if (batch.decisionDigest == 0) batch.decisionDigest = kFnvOffset;
        HashText(&batch.decisionDigest, storageKey);
        HashNumber(&batch.decisionDigest, state.semanticFingerprint);

        const auto previous = candidates_.find(storageKey);
        if (batch.baseline || previous == candidates_.end() ||
            previous->second.semanticFingerprint != state.semanticFingerprint) {
            batch.changedCompletions.push_back(completion);
        }
    }

    for (const auto& previous : candidates_) {
        if (current.find(previous.first) != current.end()) continue;
        batch.removals.push_back({previous.second.candidateKey});
    }
    std::sort(
        batch.removals.begin(),
        batch.removals.end(),
        [](const auto& left, const auto& right) {
            return left.candidateKey < right.candidateKey;
        });

    batch.changedCandidates = batch.changedCompletions.size();
    batch.removedCandidates = batch.removals.size();
    batch.emit = batch.baseline || contextChanged ||
                 batch.changedCandidates > 0 || batch.removedCandidates > 0;

    contextFingerprint_ = nextContextFingerprint;
    candidates_ = std::move(current);
    return batch;
}

void CandidateDecisionDiagnosticsReducer::Reset() noexcept {
    ++sessionGeneration_;
    initialized_ = false;
    sessionKey_.clear();
    contextFingerprint_ = 0;
    decisionGeneration_ = 0;
    candidates_.clear();
}

std::size_t CandidateDecisionDiagnosticsReducer::TrackedCandidateCount() const
    noexcept {
    return candidates_.size();
}

std::string FormatCandidateDecisionDiagnosticsSummary(
    const CandidateDecisionDiagnosticsBatch& batch) {
    std::ostringstream stream;
    stream << "event=candidate-decision-summary"
           << " baseline=" << (batch.baseline ? 1 : 0)
           << " sessionGeneration=" << batch.sessionGeneration
           << " decisionGeneration=" << batch.decisionGeneration
           << ContextFields(batch.context)
           << " rawRadioHash=" << batch.context.rawRadioHash
           << " gatedRadioHash=" << batch.context.gatedRadioHash
           << " routeHash=" << batch.context.routePolygonHash
           << " candidatesComplete="
           << (batch.context.candidatesComplete ? 1 : 0)
           << " observed=" << batch.observedCandidates
           << " accepted=" << batch.acceptedCandidates
           << " rejected=" << batch.rejectedCandidates
           << " pending=" << batch.pendingCandidates
           << " displayed=" << batch.displayedCandidates
           << " changed=" << batch.changedCandidates
           << " removed=" << batch.removedCandidates
           << " decisionDigest=" << std::hex << batch.decisionDigest;
    return stream.str();
}

std::size_t CandidateDecisionDiagnosticReceiptRecordCount(
    const brain::BrainOwnedCandidateCompletion& completion) noexcept {
    return std::max<std::size_t>(
        1,
        (completion.evidenceVotes.size() +
         kCandidateDecisionVotesPerRecord - 1) /
            kCandidateDecisionVotesPerRecord);
}

std::string FormatCandidateDecisionDiagnosticReceiptRecord(
    const CandidateDecisionDiagnosticsContext& context,
    const brain::BrainOwnedCandidateCompletion& completion,
    bool baseline,
    std::size_t recordIndex) {
    const auto recordCount =
        CandidateDecisionDiagnosticReceiptRecordCount(completion);
    return BuildReceipt(
        context,
        completion,
        baseline,
        std::min(recordIndex, recordCount - 1),
        recordCount);
}

std::string FormatCandidateDecisionDiagnosticRemoval(
    const CandidateDecisionDiagnosticsContext& context,
    const CandidateDecisionDiagnosticRemoval& removal,
    bool baseline) {
    std::ostringstream stream;
    stream << "event=candidate-vote-receipt"
           << " baseline=" << (baseline ? 1 : 0)
           << ContextFields(context)
           << " state=removed"
           << " candidate=" << SanitizeToken(removal.candidateKey, 160);
    return stream.str();
}

}  // namespace xvatsim::modules::runtime_workers
