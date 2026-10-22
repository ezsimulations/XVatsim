#include "CandidateDecisionDiagnosticsProbe.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "XVatsim/modules/runtime_workers/AsyncDiagnosticsWriter.h"
#include "XVatsim/modules/runtime_workers/CandidateDecisionDiagnosticsReducer.h"

namespace xvatsim::tools::candidate_decision_diagnostics {
namespace {

using xvatsim::brain::BrainControllerEvidenceVote;
using xvatsim::brain::BrainOwnedCandidateCompletion;
using xvatsim::brain::BrainOwnedCandidateDecision;
using xvatsim::brain::DisplayRelation;
using xvatsim::brain::RadioReachableFacilityGroup;
using xvatsim::brain::WorkflowStage;
using xvatsim::modules::runtime_workers::CandidateDecisionDiagnosticsBatch;
using xvatsim::modules::runtime_workers::CandidateDecisionDiagnosticsContext;
using xvatsim::modules::runtime_workers::CandidateDecisionDiagnosticsReducer;

BrainControllerEvidenceVote Vote(
    std::string source,
    int score,
    std::string reason,
    std::string provenance = {}) {
    BrainControllerEvidenceVote vote;
    vote.source = std::move(source);
    vote.score = score;
    vote.reason = std::move(reason);
    vote.provenance = std::move(provenance);
    return vote;
}

BrainOwnedCandidateCompletion Completion(std::size_t index) {
    BrainOwnedCandidateCompletion completion;
    completion.radioBoardHash = 10;
    completion.routePolygonHash = 20;
    completion.workflowStage = WorkflowStage::Departure;
    completion.currentPolygonIndex = 1;
    completion.currentPolygonKey = "KZOA";
    completion.matchedPolygonKey = "KZOA";
    completion.callsign = "TEST_" + std::to_string(index) + "_APP";
    completion.frequency =
        "1" + std::to_string(18000 + static_cast<int>(index));
    completion.facilityGroup = RadioReachableFacilityGroup::AppDep;
    completion.displayRelation = DisplayRelation::Hidden;
    completion.decision = BrainOwnedCandidateDecision::Rejected;
    completion.displayed = false;
    completion.positiveVotes = 0;
    completion.negativeVotes = 3;
    completion.neutralVotes = 5;
    completion.evidenceVotes = {
        Vote(
            "frequency-distance",
            -1,
            "outside-airport-radius",
            std::to_string(1000 + index) + ".0nm<=5.0nm"),
        Vote("published-frequency", 0, "airport-frequency-data-unavailable"),
        Vote("route-context", 0, "no-current-center-root-match"),
        Vote(
            "terminal-source",
            -1,
            "terminal-owner-mismatch",
            completion.callsign),
        Vote(
            "vatsim-parse",
            0,
            "live-role-valid-endpoint-relationship-unknown",
            "TEST"),
        Vote(
            "vatspy-parse",
            -1,
            "airport-alias-mismatch",
            "TEST!=SJC"),
        Vote(
            "vatsim-feed-health",
            1,
            "fresh-complete-controller-roster",
            "declared=152,retained=152,rejected=0"),
        Vote(
            "vatsim-roster",
            0,
            "no-direct-endpoint-controller",
            "declared=152,retained=152,rejected=0,truncated=0"),
        Vote("vnas", 0, "incomplete-us-data-no-match"),
        Vote(
            "xpilot-fsd-correlation",
            0,
            "controller-not-observed-in-complete-xpilot4-snapshot",
            "process=100_connection=1_snapshot=1"),
    };
    completion.reason =
        "departure-terminal-evidence:brain-votes=0/3/5:"
        "frequency-distance=-1[outside-airport-radius@1000.0nm<=5.0nm]:"
        "xpilot-fsd-correlation=0[controller-not-observed@process=100_"
        "connection=1_snapshot=1]:brain-final=hide:"
        "brain-reason=brain-ineligible-relation";
    completion.stableKey =
        "10|20|DEP|KZOA|" + completion.callsign + "|" +
        completion.frequency + "|APP_DEP";
    return completion;
}

CandidateDecisionDiagnosticsContext Context() {
    CandidateDecisionDiagnosticsContext context;
    context.phase = "DEP";
    context.planKey = "TEST250|KSJC|KIDA";
    context.route = "KSJC->KIDA";
    context.currentPolygon = "KZOA";
    context.nextPolygon = "KZLC";
    context.arrivalPolygon = "KZLC";
    context.currentPolygonIndex = 1;
    context.rawRadioHash = 10;
    context.gatedRadioHash = 11;
    context.routePolygonHash = 20;
    context.candidatesComplete = true;
    return context;
}

std::size_t FormattedBytes(const CandidateDecisionDiagnosticsBatch& batch) {
    using namespace xvatsim::modules::runtime_workers;
    std::size_t bytes =
        FormatCandidateDecisionDiagnosticsSummary(batch).size() + 1;
    for (const auto& completion : batch.changedCompletions) {
        const auto recordCount =
            CandidateDecisionDiagnosticReceiptRecordCount(completion);
        for (std::size_t recordIndex = 0;
             recordIndex < recordCount;
             ++recordIndex) {
            bytes += FormatCandidateDecisionDiagnosticReceiptRecord(
                         batch.context,
                         completion,
                         batch.baseline,
                         recordIndex)
                         .size() +
                     1;
        }
    }
    for (const auto& removal : batch.removals) {
        bytes += FormatCandidateDecisionDiagnosticRemoval(
                     batch.context, removal, batch.baseline)
                     .size() +
                 1;
    }
    return bytes;
}

bool RecordsAreBoundedAndComplete(
    const CandidateDecisionDiagnosticsBatch& batch,
    std::string* failure) {
    using namespace xvatsim::modules::runtime_workers;
    const auto summary = FormatCandidateDecisionDiagnosticsSummary(batch);
    if (summary.size() > 1024 ||
        summary.find("accepted=") == std::string::npos ||
        summary.find("rejected=") == std::string::npos ||
        summary.find("pending=") == std::string::npos ||
        summary.find("displayed=") == std::string::npos ||
        summary.find("decisionDigest=") == std::string::npos ||
        summary.find("sessionGeneration=") == std::string::npos ||
        summary.find("decisionGeneration=") == std::string::npos) {
        *failure = "summary-record-over-limit";
        return false;
    }
    for (const auto& completion : batch.changedCompletions) {
        std::string joinedReceipts;
        const auto recordCount =
            CandidateDecisionDiagnosticReceiptRecordCount(completion);
        for (std::size_t recordIndex = 0;
             recordIndex < recordCount;
             ++recordIndex) {
            const auto receipt =
                FormatCandidateDecisionDiagnosticReceiptRecord(
                    batch.context,
                    completion,
                    batch.baseline,
                    recordIndex);
            if (receipt.size() > kCandidateDecisionDiagnosticRecordMaxChars ||
                receipt.find("receiptPart=") == std::string::npos ||
                receipt.find("receiptDigest=") == std::string::npos) {
                *failure = "candidate-record-over-limit";
                return false;
            }
            joinedReceipts += receipt;
        }
        for (const auto& vote : completion.evidenceVotes) {
            const auto expected = vote.source + "=" +
                (vote.score > 0 ? "+1" : vote.score < 0 ? "-1" : "0") +
                "(" + vote.reason;
            if (joinedReceipts.find(expected) == std::string::npos) {
                *failure = "missing-vote-" + vote.source;
                return false;
            }
        }
    }
    for (const auto& removal : batch.removals) {
        const auto receipt = FormatCandidateDecisionDiagnosticRemoval(
            batch.context, removal, batch.baseline);
        if (receipt.size() > kCandidateDecisionDiagnosticRecordMaxChars ||
            receipt.find("state=removed") == std::string::npos) {
            *failure = "invalid-removal-record";
            return false;
        }
    }
    return true;
}

}  // namespace

int RunCandidateDecisionDiagnosticsProbe() {
    CandidateDecisionDiagnosticsReducer reducer;
    auto context = Context();
    std::vector<BrainOwnedCandidateCompletion> completions;
    for (std::size_t index = 0; index < 49; ++index) {
        completions.push_back(Completion(index));
    }

    const auto baseline = reducer.Observe(context, completions);
    std::string failure;
    if (!baseline.emit || !baseline.baseline ||
        baseline.changedCandidates != completions.size() ||
        baseline.removedCandidates != 0 ||
        baseline.acceptedCandidates != 0 ||
        baseline.rejectedCandidates != completions.size() ||
        baseline.pendingCandidates != 0 ||
        baseline.displayedCandidates != 0 ||
        reducer.TrackedCandidateCount() != completions.size() ||
        !RecordsAreBoundedAndComplete(baseline, &failure)) {
        std::cerr << "CANDIDATE_DIAGNOSTICS_BASELINE_FAILED " << failure
                  << "\n";
        return 1;
    }
    const auto baselineBytes = FormattedBytes(baseline);
    if (baselineBytes > 125'000) {
        std::cerr << "CANDIDATE_DIAGNOSTICS_BASELINE_VOLUME_FAILED bytes="
                  << baselineBytes << "\n";
        return 1;
    }

    auto oversizedReceipt = Completion(99);
    oversizedReceipt.evidenceVotes.clear();
    for (int index = 0; index < 29; ++index) {
        oversizedReceipt.evidenceVotes.push_back(Vote(
            "evidence-source-" + std::to_string(index),
            index % 3 == 0 ? 1 : index % 3 == 1 ? 0 : -1,
            "bounded-semantic-reason-" + std::to_string(index),
            "material-provenance-" + std::to_string(index)));
    }
    const auto splitRecordCount =
        xvatsim::modules::runtime_workers::
            CandidateDecisionDiagnosticReceiptRecordCount(oversizedReceipt);
    std::string splitRecords;
    for (std::size_t recordIndex = 0;
         recordIndex < splitRecordCount;
         ++recordIndex) {
        const auto record = xvatsim::modules::runtime_workers::
            FormatCandidateDecisionDiagnosticReceiptRecord(
                context,
                oversizedReceipt,
                true,
                recordIndex);
        if (record.size() > xvatsim::modules::runtime_workers::
                                kCandidateDecisionDiagnosticRecordMaxChars) {
            std::cerr << "CANDIDATE_DIAGNOSTICS_SPLIT_LIMIT_FAILED\n";
            return 1;
        }
        splitRecords += record;
    }
    if (splitRecordCount != 3) {
        std::cerr << "CANDIDATE_DIAGNOSTICS_SPLIT_COUNT_FAILED records="
                  << splitRecordCount << "\n";
        return 1;
    }
    for (const auto& vote : oversizedReceipt.evidenceVotes) {
        const auto score =
            vote.score > 0 ? "+1" : vote.score < 0 ? "-1" : "0";
        if (splitRecords.find(vote.source + "=" + score + "(" +
                              vote.reason) == std::string::npos) {
            std::cerr << "CANDIDATE_DIAGNOSTICS_SPLIT_VOTE_FAILED source="
                      << vote.source << "\n";
            return 1;
        }
    }

    std::uint64_t maximumObserveUs = 0;
    std::uint64_t totalObserveUs = 0;
    std::size_t duplicateEmissions = 0;
    for (std::size_t repeat = 0; repeat < 1000; ++repeat) {
        ++context.rawRadioHash;
        ++context.gatedRadioHash;
        ++context.routePolygonHash;
        for (auto& completion : completions) {
            completion.radioBoardHash = context.gatedRadioHash;
            completion.routePolygonHash = context.routePolygonHash;
            completion.stableKey =
                std::to_string(context.gatedRadioHash) + "|" +
                std::to_string(context.routePolygonHash) + "|" +
                completion.callsign;
            completion.evidenceVotes.front().provenance =
                std::to_string(1000 + repeat) + ".0nm<=5.0nm";
            completion.evidenceVotes.back().provenance =
                "process=" + std::to_string(100 + repeat) +
                "_connection=1_snapshot=" + std::to_string(repeat + 2);
            completion.reason =
                "departure-terminal-evidence:brain-votes=0/3/5:"
                "frequency-distance=-1[outside-airport-radius@" +
                std::to_string(1000 + repeat) +
                ".0nm<=5.0nm]:xpilot-fsd-correlation=0[controller-not-"
                "observed@process=" + std::to_string(100 + repeat) +
                "_connection=1_snapshot=" + std::to_string(repeat + 2) +
                "]:brain-final=hide:brain-reason=brain-ineligible-relation";
        }
        const auto started = std::chrono::steady_clock::now();
        const auto duplicate = reducer.Observe(context, completions);
        const auto elapsedUs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started)
                .count());
        maximumObserveUs = std::max(maximumObserveUs, elapsedUs);
        totalObserveUs += elapsedUs;
        if (duplicate.emit) ++duplicateEmissions;
    }
    const auto averageObserveUs = totalObserveUs / 1000;
    if (duplicateEmissions != 0 || maximumObserveUs >= 10'000 ||
        averageObserveUs >= 1'000) {
        std::cerr << "CANDIDATE_DIAGNOSTICS_DEDUP_OR_PERF_FAILED emissions="
                  << duplicateEmissions << " averageUs=" << averageObserveUs
                  << " maximumUs=" << maximumObserveUs << "\n";
        return 1;
    }

    CandidateDecisionDiagnosticsReducer rosterReducer;
    std::vector<BrainOwnedCandidateCompletion> rosterCompletions = {
        Completion(500)};
    const auto rosterBaseline = rosterReducer.Observe(context, rosterCompletions);
    std::size_t rosterCountChurnEmissions = 0;
    for (std::size_t repeat = 0; repeat < 1000; ++repeat) {
        for (auto& vote : rosterCompletions.front().evidenceVotes) {
            if (vote.source == "vatsim-feed-health") {
                vote.provenance =
                    "declared=" + std::to_string(153 + repeat) +
                    ",retained=" + std::to_string(153 + repeat) +
                    ",rejected=0";
            } else if (vote.source == "vatsim-roster") {
                vote.provenance =
                    "declared=" + std::to_string(153 + repeat) +
                    ",retained=" + std::to_string(153 + repeat) +
                    ",rejected=0,truncated=0";
            }
        }
        if (rosterReducer.Observe(context, rosterCompletions).emit) {
            ++rosterCountChurnEmissions;
        }
    }
    for (auto& vote : rosterCompletions.front().evidenceVotes) {
        if (vote.source == "vatsim-roster") {
            vote.score = 0;
            vote.reason = "controller-roster-incomplete";
            vote.provenance =
                "declared=1153,retained=1152,rejected=1,truncated=1";
        }
    }
    const auto rosterSemanticDelta =
        rosterReducer.Observe(context, rosterCompletions);
    if (!rosterBaseline.emit || rosterCountChurnEmissions != 0 ||
        !rosterSemanticDelta.emit ||
        rosterSemanticDelta.changedCandidates != 1) {
        std::cerr << "CANDIDATE_DIAGNOSTICS_ROSTER_CHURN_FAILED"
                  << " emissions=" << rosterCountChurnEmissions
                  << " semanticChanged="
                  << rosterSemanticDelta.changedCandidates << "\n";
        return 1;
    }

    auto& changed = completions[17];
    changed.evidenceVotes.back().score = 1;
    changed.evidenceVotes.back().reason = "correlated-controller-confirmed";
    changed.evidenceVotes.back().provenance +=
        "_callsign=" + changed.callsign + "_frequencyHz=121900000";
    changed.positiveVotes = 1;
    changed.neutralVotes = 4;
    changed.decision = BrainOwnedCandidateDecision::Accepted;
    changed.displayed = true;
    changed.displayRelation = DisplayRelation::CurrentPolygon;
    changed.reason =
        "departure-terminal-evidence:brain-final=display:"
        "brain-reason=brain-vote-accepted";
    const auto delta = reducer.Observe(context, completions);
    if (!delta.emit || delta.baseline || delta.changedCandidates != 1 ||
        delta.removedCandidates != 0 ||
        delta.acceptedCandidates != 1 ||
        delta.rejectedCandidates != completions.size() - 1 ||
        delta.displayedCandidates != 1 ||
        delta.changedCompletions.front().callsign != changed.callsign ||
        !RecordsAreBoundedAndComplete(delta, &failure)) {
        std::cerr << "CANDIDATE_DIAGNOSTICS_DELTA_FAILED " << failure << "\n";
        return 1;
    }

    const auto removedCallsign = completions[4].callsign;
    completions.erase(completions.begin() + 4);
    const auto removal = reducer.Observe(context, completions);
    if (!removal.emit || removal.changedCandidates != 0 ||
        removal.removedCandidates != 1 ||
        removal.removals.front().candidateKey.find(removedCallsign) ==
            std::string::npos ||
        !RecordsAreBoundedAndComplete(removal, &failure)) {
        std::cerr << "CANDIDATE_DIAGNOSTICS_REMOVAL_FAILED " << failure
                  << "\n";
        return 1;
    }

    reducer.Reset();
    const auto resetBaseline = reducer.Observe(context, completions);
    if (!resetBaseline.emit || !resetBaseline.baseline ||
        resetBaseline.changedCandidates != completions.size()) {
        std::cerr << "CANDIDATE_DIAGNOSTICS_RESET_FAILED\n";
        return 1;
    }

    const auto storageProbeDirectory =
        std::filesystem::temp_directory_path() /
        ("xvatsim_diagnostic_bound_probe_" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(storageProbeDirectory);
    const std::string seedLine(120, 'S');
    for (const auto* date : {"2000_01_01", "2000_01_02"}) {
        std::ofstream seed(
            storageProbeDirectory /
                (std::string{"xvatsim_diagnostics_"} + date + ".log"),
            std::ios::binary | std::ios::trunc);
        for (int index = 0; index < 100; ++index) {
            seed << "tick=0 seed=" << index << " " << seedLine << '\n';
        }
    }
    xvatsim::modules::runtime_workers::AsyncDiagnosticsWriter boundedWriter;
    xvatsim::modules::runtime_workers::DiagnosticsWriterOptions writerOptions;
    writerOptions.logDirectory = storageProbeDirectory;
    writerOptions.maximumDateLogBytes = 4096;
    writerOptions.retainedDateLogCount = 3;
    if (!boundedWriter.Start(writerOptions)) {
        std::cerr << "CANDIDATE_DIAGNOSTICS_STORAGE_START_FAILED\n";
        return 1;
    }
    for (int index = 0; index < 300; ++index) {
        const auto line =
            "event=storage-bound-probe sequence=" + std::to_string(index) +
            " payload=" + std::string(160, 'X');
        while (!boundedWriter.TryEnqueueLine(line)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    const auto writerIdle = boundedWriter.WaitUntilIdleForTesting(
        std::chrono::seconds(10));
    const auto writerSnapshot = boundedWriter.Snapshot();
    boundedWriter.Stop();

    std::uintmax_t totalDiagnosticBytes = 0;
    std::size_t datedLogCount = 0;
    bool everyFileBounded = true;
    bool newestRecordPreserved = false;
    bool trimMarkerPresent = false;
    for (const auto& entry :
         std::filesystem::directory_iterator(storageProbeDirectory)) {
        if (!entry.is_regular_file() ||
            entry.path().filename().string().rfind(
                "xvatsim_diagnostics_", 0) != 0 ||
            entry.path().extension() != ".log") {
            continue;
        }
        ++datedLogCount;
        const auto bytes = entry.file_size();
        totalDiagnosticBytes += bytes;
        everyFileBounded = everyFileBounded && bytes <= 4096;
        std::ifstream input(entry.path(), std::ios::binary);
        std::ostringstream content;
        content << input.rdbuf();
        const auto text = content.str();
        newestRecordPreserved = newestRecordPreserved ||
            text.find("sequence=299 ") != std::string::npos;
        trimMarkerPresent = trimMarkerPresent ||
            text.find("event=diagnostic-history-trimmed") !=
                std::string::npos;
    }
    std::error_code cleanupError;
    std::filesystem::remove_all(storageProbeDirectory, cleanupError);
    if (!writerIdle || writerSnapshot.storageFailures != 0 ||
        writerSnapshot.storageTrims == 0 || datedLogCount > 3 ||
        !everyFileBounded || totalDiagnosticBytes > 3 * 4096 ||
        !newestRecordPreserved || !trimMarkerPresent) {
        std::cerr << "CANDIDATE_DIAGNOSTICS_STORAGE_BOUND_FAILED"
                  << " idle=" << (writerIdle ? 1 : 0)
                  << " failures=" << writerSnapshot.storageFailures
                  << " trims=" << writerSnapshot.storageTrims
                  << " files=" << datedLogCount
                  << " bytes=" << totalDiagnosticBytes
                  << " newest=" << (newestRecordPreserved ? 1 : 0)
                  << " marker=" << (trimMarkerPresent ? 1 : 0) << "\n";
        return 1;
    }

    std::cout << "CANDIDATE_DECISION_DIAGNOSTICS_PASSED"
              << " candidates=49"
              << " votesPerCandidate=10"
              << " baselineBytes=" << baselineBytes
              << " transportRepeats=1000"
              << " transportRepeatEmissions=" << duplicateEmissions
              << " rosterCountChurnEmissions="
              << rosterCountChurnEmissions
              << " storageTrims=" << writerSnapshot.storageTrims
              << " maximumDateLogBytes=4096"
              << " retainedDateLogCount=" << datedLogCount
              << " deltaCandidates=" << delta.changedCandidates
              << " removalReceipts=" << removal.removedCandidates
              << " oversizedReceiptParts=" << splitRecordCount
              << " maximumRecordChars="
              << xvatsim::modules::runtime_workers::
                     kCandidateDecisionDiagnosticRecordMaxChars
              << " averageObserveUs=" << averageObserveUs
              << " maximumObserveUs=" << maximumObserveUs << "\n";
    return 0;
}

}  // namespace xvatsim::tools::candidate_decision_diagnostics
