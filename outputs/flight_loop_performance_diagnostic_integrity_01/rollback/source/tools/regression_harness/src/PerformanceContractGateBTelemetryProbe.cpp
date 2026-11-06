#include "PerformanceContractGateBTelemetryProbe.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "XVatsim/modules/runtime_workers/AsyncDiagnosticsWriter.h"

namespace xvatsim::tools::performance_contract_gate_b_telemetry {
namespace {

using modules::runtime_workers::AsyncDiagnosticsWriter;
using modules::runtime_workers::DiagnosticsWriterOptions;
using modules::runtime_workers::RouteEvidenceDisposition;
using modules::runtime_workers::RouteEvidenceRecord;
using modules::runtime_workers::RouteEvidenceRecordType;
using modules::runtime_workers::RouteEvidenceWorkKind;

std::filesystem::path UniqueProbeRoot() {
    const auto token = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() /
           ("xvatsim_gate_b_telemetry_" + std::to_string(token));
}

std::uint64_t Percentile99(std::vector<std::uint64_t> samples) {
    if (samples.empty()) {
        return 0;
    }
    std::sort(samples.begin(), samples.end());
    const auto index = std::min<std::size_t>(
        samples.size() - 1,
        (samples.size() * 99 + 99) / 100 - 1);
    return samples[index];
}

std::string ReadAllLogs(const std::filesystem::path& root) {
    std::ostringstream content;
    std::error_code error;
    if (!std::filesystem::exists(root, error) || error) {
        return {};
    }
    for (std::filesystem::directory_iterator iterator(root, error), end;
         !error && iterator != end;
         iterator.increment(error)) {
        if (!iterator->is_regular_file(error) || error) {
            error.clear();
            continue;
        }
        std::ifstream input(iterator->path());
        content << input.rdbuf();
    }
    return content.str();
}

std::vector<std::uint64_t> RouteEvidenceSequences(
    const std::string& content) {
    std::vector<std::uint64_t> sequences;
    std::istringstream lines(content);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.find("event=route-evidence ") == std::string::npos) {
            continue;
        }
        const auto marker = line.find(" sequence=");
        if (marker == std::string::npos) {
            continue;
        }
        const auto start = marker + std::string{" sequence="}.size();
        const auto end = line.find(' ', start);
        sequences.push_back(static_cast<std::uint64_t>(
            std::stoull(line.substr(start, end - start))));
    }
    return sequences;
}

template <std::size_t Size>
void CopyToken(std::array<char, Size>* destination, std::string_view value) {
    destination->fill('\0');
    const auto count = std::min(value.size(), Size - 1);
    std::copy_n(value.data(), count, destination->data());
}

RouteEvidenceRecord MakeEvidence(
    RouteEvidenceRecordType type,
    RouteEvidenceWorkKind kind,
    std::uint64_t requestId,
    std::uint64_t lifecycleEpoch,
    std::string_view reason) {
    RouteEvidenceRecord evidence;
    evidence.recordType = type;
    evidence.workKind = kind;
    evidence.requestId = requestId;
    evidence.lifecycleEpoch = lifecycleEpoch;
    evidence.networkDigest = 101;
    evidence.sourceIdentity = 102;
    evidence.preflightIdentity = 103;
    evidence.policyIdentity = 104;
    evidence.anchorDigest = 105;
    evidence.fmsObservationIdentity = 106;
    evidence.resultDigest = 107;
    evidence.completeDesiredPackageSubmitUs = 12;
    evidence.mailboxExchangeUs = 2;
    evidence.maximumPendingDepth = 1;
    evidence.workerWallUs = 321;
    evidence.workerCpuUs = 123;
    evidence.workerCpuAvailable = true;
    evidence.completed = type == RouteEvidenceRecordType::Terminal;
    CopyToken(&evidence.planKey, "UAL472|KAAA|KBBB");
    CopyToken(&evidence.reason, reason);
    return evidence;
}

bool WaitForDequeued(
    const AsyncDiagnosticsWriter& writer,
    std::uint64_t minimum,
    std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (writer.Snapshot().dequeued >= minimum) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return writer.Snapshot().dequeued >= minimum;
}

}  // namespace

int RunPerformanceContractGateBTelemetryProbe() {
    std::vector<std::string> failures;
    const auto require = [&](bool condition, std::string failure) {
        if (!condition) {
            failures.push_back(std::move(failure));
        }
    };

    const auto probeRoot = UniqueProbeRoot();
    std::error_code error;
    std::filesystem::create_directories(probeRoot, error);
    require(!error, "failed to create telemetry probe root");

    std::uint64_t producerP99Us = 0;
    std::uint64_t producerMaxUs = 0;
    std::uint64_t writtenEvidence = 0;
    std::uint64_t maximumEvidenceDepth = 0;
    {
        AsyncDiagnosticsWriter writer;
        DiagnosticsWriterOptions options;
        options.logDirectory = probeRoot / "contention-delay-lifecycle";
        options.routineQueueCapacity = 32;
        options.criticalQueueCapacity = 8;
        options.flightLoopTimingQueueCapacity = 32;
        // The delayed writer cannot consume this lane while the complete
        // 99-record lifecycle fixture is submitted, so the last accepted
        // record fills the fixed-capacity ring exactly without loss.
        options.routeEvidenceQueueCapacity = 99;
        options.artificialWorkerDelayForTesting = std::chrono::seconds(2);
        options.artificialWorkerDelayRecordCountForTesting = 1;
        options.artificialQueueLockHoldForTesting =
            std::chrono::milliseconds(100);
        require(writer.Start(options), "telemetry writer failed to start");
        require(
            writer.TryEnqueueLine("event=route-evidence-delay-anchor"),
            "delay anchor was not accepted");
        require(
            WaitForDequeued(writer, 1, std::chrono::seconds(2)),
            "writer did not enter its forced delay");

        std::atomic<bool> contentionStarted{false};
        std::thread contender([&]() {
            contentionStarted.store(true, std::memory_order_release);
            (void)writer.TryEnqueueLine(
                "event=route-evidence-routine-contention");
        });
        while (!contentionStarted.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        (void)writer.TryEnqueueLine(
            "event=route-evidence-routine-contention-probe");

        std::vector<RouteEvidenceRecord> records;
        records.reserve(100);
        auto disable = MakeEvidence(
            RouteEvidenceRecordType::Lifecycle,
            RouteEvidenceWorkKind::None,
            0,
            4,
            "plugin-admin-disable");
        disable.disposition = RouteEvidenceDisposition::Cancelled;
        records.push_back(disable);
        for (std::uint64_t requestId = 1; requestId <= 32; ++requestId) {
            const auto kind = requestId % 5 == 0
                ? RouteEvidenceWorkKind::RoutePreparation
                : RouteEvidenceWorkKind::ExpandedFmsObservation;
            auto dispatch = MakeEvidence(
                RouteEvidenceRecordType::Dispatch,
                kind,
                requestId,
                4,
                requestId == 1
                    ? "expanded-fms-observation-required"
                    : "expanded-fms-quiet-observation-due");
            records.push_back(dispatch);
            auto terminal = MakeEvidence(
                RouteEvidenceRecordType::Terminal,
                kind,
                requestId,
                4,
                kind == RouteEvidenceWorkKind::ExpandedFmsObservation
                    ? "expanded-fms-match"
                    : "route-key-changed");
            records.push_back(terminal);
            auto disposition = MakeEvidence(
                RouteEvidenceRecordType::Disposition,
                kind,
                requestId,
                4,
                kind == RouteEvidenceWorkKind::ExpandedFmsObservation
                    ? "expanded-fms-observation-unchanged"
                    : "exact-route-completion-identity");
            disposition.disposition =
                kind == RouteEvidenceWorkKind::ExpandedFmsObservation
                ? RouteEvidenceDisposition::Observed
                : RouteEvidenceDisposition::Published;
            disposition.rebuildDecisionApplicable =
                kind == RouteEvidenceWorkKind::ExpandedFmsObservation;
            disposition.rebuildDecision = false;
            records.push_back(disposition);
        }
        auto disconnect = MakeEvidence(
            RouteEvidenceRecordType::Lifecycle,
            RouteEvidenceWorkKind::None,
            0,
            5,
            "network-plan-unavailable");
        disconnect.disposition = RouteEvidenceDisposition::Cancelled;
        records.push_back(disconnect);
        auto shutdown = MakeEvidence(
            RouteEvidenceRecordType::Lifecycle,
            RouteEvidenceWorkKind::None,
            0,
            6,
            "plugin-stop");
        shutdown.disposition = RouteEvidenceDisposition::Cancelled;
        records.push_back(shutdown);

        std::vector<std::uint64_t> producerSamples;
        producerSamples.reserve(records.size());
        for (auto record : records) {
            const auto started = std::chrono::steady_clock::now();
            const auto accepted = writer.TryEnqueueRouteEvidence(record);
            producerSamples.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - started)
                    .count()));
            require(accepted, "route-evidence record was not accepted");
        }
        contender.join();

        producerP99Us = Percentile99(producerSamples);
        producerMaxUs = producerSamples.empty()
            ? 0
            : *std::max_element(
                  producerSamples.begin(), producerSamples.end());
        require(
            producerP99Us < 250 && producerMaxUs <= 1000,
            "route-evidence producer exceeded its nonblocking budget");
        require(
            writer.WaitUntilIdle(std::chrono::seconds(20)),
            "route-evidence lane did not drain after forced delay");
        const auto snapshot = writer.Snapshot();
        writtenEvidence = snapshot.writtenRouteEvidence;
        maximumEvidenceDepth = snapshot.maximumRouteEvidenceQueueDepth;
        require(
            snapshot.submittedRouteEvidence == records.size() &&
                snapshot.dequeuedRouteEvidence == records.size() &&
                snapshot.writtenRouteEvidence == records.size(),
            "route-evidence submitted/dequeued/written accounting differed");
        require(
            snapshot.droppedRouteEvidenceFull == 0 &&
                snapshot.rejectedRouteEvidenceNotRunning == 0,
            "route-evidence lane lost a record");
        require(
            snapshot.droppedRoutineContention > 0,
            "forced routine-writer contention was not observed");
        require(
            snapshot.lastSubmittedRouteEvidenceSequence == records.size() &&
                snapshot.lastWrittenRouteEvidenceSequence == records.size(),
            "route-evidence sequence accounting was not exact");
        require(
            snapshot.maximumRouteEvidenceQueueDepth ==
                options.routeEvidenceQueueCapacity,
            "route-evidence queue did not reach its exact fixed capacity");
        require(
            snapshot.maximumRouteEvidenceProducerMicroseconds <= 1000,
            "route-evidence internal producer maximum exceeded 1 ms");
        writer.Stop();
        require(!writer.Snapshot().running, "telemetry writer did not stop");

        auto afterStop = MakeEvidence(
            RouteEvidenceRecordType::Lifecycle,
            RouteEvidenceWorkKind::None,
            0,
            7,
            "session-runtime-reset");
        require(
            !writer.TryEnqueueRouteEvidence(afterStop) &&
                writer.Snapshot().rejectedRouteEvidenceNotRunning == 1,
            "post-stop route evidence was not rejected and counted");

        const auto logs = ReadAllLogs(options.logDirectory);
        const auto sequences = RouteEvidenceSequences(logs);
        require(
            sequences.size() == records.size(),
            "persisted route-evidence record count differed");
        for (std::size_t index = 0; index < sequences.size(); ++index) {
            require(
                sequences[index] == index + 1,
                "persisted route-evidence sequence contained a gap");
        }
        require(
            logs.find("workerWallUs=321 workerCpuStatus=available ") !=
                    std::string::npos &&
                logs.find("rebuildDecision=0 ") != std::string::npos &&
                logs.find("reason=plugin-admin-disable") !=
                    std::string::npos &&
                logs.find("reason=network-plan-unavailable") !=
                    std::string::npos &&
                logs.find("reason=plugin-stop") != std::string::npos,
            "route-evidence payload omitted timing, rebuild, or lifecycle data");
    }

    {
        AsyncDiagnosticsWriter writer;
        DiagnosticsWriterOptions options;
        options.logDirectory = probeRoot / "shutdown-drain";
        options.routineQueueCapacity = 8;
        options.criticalQueueCapacity = 4;
        options.flightLoopTimingQueueCapacity = 8;
        options.routeEvidenceQueueCapacity = 32;
        options.artificialWorkerDelayForTesting =
            std::chrono::milliseconds(20);
        options.artificialWorkerDelayRecordCountForTesting = 8;
        require(writer.Start(options), "shutdown-drain writer failed to start");
        for (std::uint64_t requestId = 1; requestId <= 8; ++requestId) {
            require(
                writer.TryEnqueueRouteEvidence(MakeEvidence(
                    RouteEvidenceRecordType::Terminal,
                    RouteEvidenceWorkKind::ExpandedFmsObservation,
                    requestId,
                    9,
                    "expanded-fms-match")),
                "shutdown-drain route evidence was not accepted");
        }
        writer.Stop();
        const auto stopped = writer.Snapshot();
        require(
            stopped.submittedRouteEvidence == 8 &&
                stopped.dequeuedRouteEvidence == 8 &&
                stopped.writtenRouteEvidence == 8 &&
                stopped.lastWrittenRouteEvidenceSequence == 8 &&
                stopped.droppedRouteEvidenceFull == 0,
            "shutdown did not drain every accepted route-evidence record");
        const auto sequences =
            RouteEvidenceSequences(ReadAllLogs(options.logDirectory));
        require(
            sequences.size() == 8 && sequences.front() == 1 &&
                sequences.back() == 8,
            "shutdown-drain persistence sequence was incomplete");
    }

    std::filesystem::remove_all(probeRoot, error);
    require(!error, "failed to clean telemetry probe root");

    std::cout << "GATE_B_TELEMETRY_PROBE"
              << " routeEvidenceWritten=" << writtenEvidence
              << " producerP99Us=" << producerP99Us
              << " producerMaxUs=" << producerMaxUs
              << " maxDepth=" << maximumEvidenceDepth
              << " fullDrops=0 notRunningDuringSession=0 sequenceGaps=0"
              << " shutdownDrain=8/8\n";
    if (!failures.empty()) {
        for (const auto& failure : failures) {
            std::cerr << "GATE_B_TELEMETRY_ASSERTION_FAILED: "
                      << failure << "\n";
        }
        return 1;
    }
    std::cout << "PERFORMANCE_CONTRACT_GATE_B_TELEMETRY_PASSED\n";
    return 0;
}

}  // namespace xvatsim::tools::performance_contract_gate_b_telemetry
