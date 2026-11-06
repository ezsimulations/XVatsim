#include "PerformanceContractGateACalm1Probe.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "XVatsim/modules/runtime_workers/AsyncDiagnosticsWriter.h"
#include "XVatsim/modules/runtime_workers/FlightLoopDiagnosticsReducer.h"

namespace xvatsim::tools::performance_contract_gate_a_calm_1 {
namespace {

using modules::runtime_workers::AsyncDiagnosticsWriter;
using modules::runtime_workers::DiagnosticsImportance;
using modules::runtime_workers::DiagnosticsWriterOptions;
using modules::runtime_workers::FlightLoopDiagnosticsPolicy;
using modules::runtime_workers::FlightLoopDiagnosticsReducer;
using modules::runtime_workers::FlightLoopTimingRecord;

std::uint64_t ThreadIdentity() {
    return static_cast<std::uint64_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
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

std::filesystem::path UniqueProbeRoot() {
    const auto token = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() /
           ("xvatsim_gate_a_calm_1_" + std::to_string(token));
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

std::size_t CountOccurrences(
    const std::string& content,
    const std::string& needle) {
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = content.find(needle, offset)) != std::string::npos) {
        ++count;
        offset += needle.size();
    }
    return count;
}

}  // namespace

int RunPerformanceContractGateACalm1Probe() {
    std::vector<std::string> failures;
    const auto require = [&](bool condition, std::string failure) {
        if (!condition) {
            failures.push_back(std::move(failure));
        }
    };

    const auto probeRoot = UniqueProbeRoot();
    std::error_code cleanupError;
    std::filesystem::create_directories(probeRoot, cleanupError);
    require(!cleanupError, "failed to create probe root");
    const auto callerThread = ThreadIdentity();

    {
        FlightLoopDiagnosticsReducer reducer{
            FlightLoopDiagnosticsPolicy{60, 10'000, 5}};
        const auto observe = [&](std::uint64_t sequence,
                                 std::uint64_t completeUs,
                                 std::uint64_t nowSeconds) {
            FlightLoopTimingRecord timing;
            timing.sequence = sequence;
            timing.completeBeforeTelemetryUs = completeUs;
            timing.refreshUs = completeUs > 10 ? completeUs - 10 : completeUs;
            timing.diagnosticsSubmissionUs = 2;
            return reducer.Observe(timing, nowSeconds);
        };
        require(!observe(1, 100, 1'000).emitOutlier,
                "routine timing emitted an outlier record");
        require(!observe(2, 1'200, 1'001).emitOutlier,
                "sub-threshold timing emitted an outlier record");
        require(!observe(3, 6'000, 1'002).emitOutlier,
                "sub-threshold timing emitted an outlier record");
        require(observe(4, 12'000, 1'003).emitOutlier,
                "first outlier was not retained");
        require(!observe(5, 45'000, 1'004).emitOutlier,
                "outlier rate limit did not suppress a duplicate detail row");
        require(observe(6, 50'000, 1'008).emitOutlier,
                "outlier detail did not resume after the rate limit");
        const auto summaryDecision = observe(7, 200, 1'060);
        require(summaryDecision.emitSummary,
                "one-minute flight-loop summary was not emitted");
        const auto& summary = summaryDecision.summary;
        require(
            summary.sampleCount == 7 &&
                summary.firstSequence == 1 &&
                summary.lastSequence == 7 &&
                summary.totalCompleteUs == 114'500 &&
                summary.minimumCompleteUs == 100 &&
                summary.maximumCompleteUs == 50'000,
            "flight-loop summary totals were incorrect");
        require(
            summary.over1msCount == 5 &&
                summary.over5msCount == 4 &&
                summary.over10msCount == 3 &&
                summary.over40msCount == 2 &&
                summary.outlierRecordsEmitted == 2,
            "flight-loop summary threshold counts were incorrect");
        require(
            summary.p95UpperUs == 50'000 &&
                summary.p99UpperUs == 50'000,
            "flight-loop summary percentile bounds were incorrect");
        const auto formatted =
            modules::runtime_workers::FormatFlightLoopTimingSummary(
                summary, "probe");
        require(
            formatted.find("event=flight-loop-summary") != std::string::npos &&
                formatted.find("samples=7") != std::string::npos &&
                formatted.find("over10ms=3") != std::string::npos,
            "flight-loop summary format omitted required evidence");
        (void)observe(8, 75, 1'061);
        const auto partial = reducer.Flush(1'062);
        require(partial.has_value() && partial->sampleCount == 1,
                "partial flight-loop window was not flushed");
        reducer.Reset();
        require(!reducer.Flush(1'063).has_value(),
                "reset flight-loop reducer retained stale samples");
    }

    std::atomic<std::uint64_t> formatterThread{0};
    {
        AsyncDiagnosticsWriter writer;
        DiagnosticsWriterOptions options;
        options.logDirectory = probeRoot / "normal";
        options.routineQueueCapacity = 32;
        options.criticalQueueCapacity = 8;
        options.flightLoopTimingQueueCapacity = 16;
        require(writer.Start(options), "normal writer failed to start");
        require(
            writer.TryEnqueueLine(
                "event=calm1-normal-line",
                DiagnosticsImportance::Critical),
            "normal line was not accepted");
        require(
            writer.TryEnqueueDeferred([&formatterThread]() {
                formatterThread.store(ThreadIdentity(), std::memory_order_release);
                std::string largePayload(16'384, 'Q');
                return std::string{"event=calm1-deferred-format bytes="} +
                       std::to_string(largePayload.size());
            }),
            "deferred formatter was not accepted");
        FlightLoopTimingRecord timing;
        timing.sequence = 17;
        timing.completeBeforeTelemetryUs = 123;
        timing.refreshUs = 100;
        timing.diagnosticsSubmissionUs = 7;
        require(
            writer.TryEnqueueFlightLoopTiming(timing),
            "flight-loop timing was not accepted");
        require(
            writer.WaitUntilIdleForTesting(std::chrono::seconds(5)),
            "normal writer did not become idle");
        const auto beforeStop = writer.Snapshot();
        require(beforeStop.written == 3, "normal writer did not write 3 records");
        require(
            beforeStop.submittedFlightLoopTiming == 1 &&
                beforeStop.dequeuedFlightLoopTiming == 1 &&
                beforeStop.writtenFlightLoopTiming == 1,
            "normal timing lane accounting was not exact");
        require(
            beforeStop.droppedFlightLoopTimingFull == 0,
            "normal timing lane dropped a record");
        require(
            formatterThread.load(std::memory_order_acquire) != 0 &&
                formatterThread.load(std::memory_order_acquire) != callerThread,
            "large formatter executed on the producer thread");
        require(
            beforeStop.workerThreadIdentity != 0 &&
                beforeStop.workerThreadIdentity != callerThread,
            "writer thread identity was not independent");
#if defined(_WIN32)
        require(
            beforeStop.workerPriorityRequested &&
                beforeStop.workerPrioritySucceeded,
            "below-normal writer priority was not established");
#endif
        writer.Stop();
        require(!writer.Snapshot().running, "normal writer did not stop cleanly");
        const auto logs = ReadAllLogs(options.logDirectory);
        require(
            logs.find("event=calm1-normal-line") != std::string::npos,
            "normal line was not persisted");
        require(
            logs.find("event=calm1-deferred-format bytes=16384") !=
                std::string::npos,
            "deferred formatter output was not persisted");
        require(
            logs.find("event=flight-loop-outlier sequence=17") !=
                std::string::npos,
            "complete callback timing was not persisted");
    }

    std::uint64_t slowP99Us = 0;
    std::uint64_t slowMaximumUs = 0;
    std::uint64_t slowTotalUs = 0;
    std::uint64_t slowDropped = 0;
    std::uint64_t slowMaximumDepth = 0;
    std::uint64_t slowTimingMaximumDepth = 0;
    std::uint64_t slowRoutineContention = 0;
    std::uint64_t slowCriticalContention = 0;
    {
        AsyncDiagnosticsWriter writer;
        DiagnosticsWriterOptions options;
        options.logDirectory = probeRoot / "slow";
        options.routineQueueCapacity = 8;
        options.criticalQueueCapacity = 4;
        options.flightLoopTimingQueueCapacity = 6'000;
        options.artificialWorkerDelayForTesting =
            std::chrono::seconds(2);
        options.artificialWorkerDelayRecordCountForTesting = 1;
        options.artificialQueueLockHoldForTesting =
            std::chrono::milliseconds(100);
        require(writer.Start(options), "slow writer failed to start");
        require(
            writer.TryEnqueueLine("event=slow-storage-anchor"),
            "slow writer anchor was not accepted");
        require(
            WaitForDequeued(writer, 1, std::chrono::seconds(2)),
            "slow writer did not enter the delayed storage path");

        const auto forceContention = [&](DiagnosticsImportance importance) {
            constexpr std::size_t kContenders = 8;
            std::atomic<std::size_t> ready{0};
            std::atomic<bool> go{false};
            std::vector<std::thread> contenders;
            contenders.reserve(kContenders);
            for (std::size_t index = 0; index < kContenders; ++index) {
                contenders.emplace_back([&, index]() {
                    ready.fetch_add(1, std::memory_order_release);
                    while (!go.load(std::memory_order_acquire)) {
                        std::this_thread::yield();
                    }
                    (void)writer.TryEnqueueLine(
                        "event=forced-mailbox-contention index=" +
                            std::to_string(index),
                        importance);
                });
            }
            while (ready.load(std::memory_order_acquire) != kContenders) {
                std::this_thread::yield();
            }
            go.store(true, std::memory_order_release);
            for (auto& contender : contenders) {
                contender.join();
            }
        };
        forceContention(DiagnosticsImportance::Routine);
        forceContention(DiagnosticsImportance::Critical);

        std::vector<std::uint64_t> samples;
        samples.reserve(5'000);
        std::size_t acceptedTiming = 0;
        const auto burstStarted = std::chrono::steady_clock::now();
        for (std::uint64_t index = 0; index < 5'000; ++index) {
            FlightLoopTimingRecord timing;
            timing.sequence = index + 1;
            timing.completeBeforeTelemetryUs = 50;
            const auto started = std::chrono::steady_clock::now();
            if (writer.TryEnqueueFlightLoopTiming(timing)) {
                ++acceptedTiming;
            }
            samples.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - started)
                    .count()));
        }
        slowTotalUs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - burstStarted)
                .count());
        slowP99Us = Percentile99(samples);
        slowMaximumUs = *std::max_element(samples.begin(), samples.end());
        const auto saturated = writer.Snapshot();
        slowDropped = saturated.droppedRoutineFull +
                      saturated.droppedContention;
        slowMaximumDepth = saturated.maximumQueueDepth;
        slowTimingMaximumDepth =
            saturated.maximumFlightLoopTimingQueueDepth;
        slowRoutineContention = saturated.droppedRoutineContention;
        slowCriticalContention = saturated.droppedCriticalContention;
        require(slowP99Us < 250, "slow-storage producer P99 exceeded 250 us");
        require(
            slowTotalUs < 250'000,
            "slow-storage producer burst was delayed by the writer");
        require(
            acceptedTiming == 5'000 &&
                saturated.submittedFlightLoopTiming == 5'000 &&
                saturated.droppedFlightLoopTimingFull == 0,
            "timing lane lost a record during routine/critical contention");
        require(
            slowRoutineContention > 0,
            "forced routine contention was not observed");
        require(
            slowCriticalContention > 0,
            "forced critical contention was not observed");
        require(
            slowMaximumDepth <=
                options.routineQueueCapacity + options.criticalQueueCapacity,
            "slow-storage queue exceeded configured bounds");
        require(
            slowTimingMaximumDepth <= options.flightLoopTimingQueueCapacity,
            "timing lane exceeded its configured bound");
        require(
            writer.WaitUntilIdleForTesting(std::chrono::seconds(20)),
            "slow writer did not drain the timing lane");
        const auto drained = writer.Snapshot();
        require(
            drained.writtenFlightLoopTiming == 5'000,
            "slow writer did not persist every accepted timing record");
        writer.Stop();
        require(!writer.Snapshot().running, "slow writer did not stop cleanly");
        const auto logs = ReadAllLogs(options.logDirectory);
        require(
            CountOccurrences(logs, "event=flight-loop-outlier") == 5'000,
            "slow writer timing sequence was not complete");
        require(
            logs.find("event=flight-loop-outlier sequence=1 ") !=
                    std::string::npos &&
                logs.find("event=flight-loop-outlier sequence=5000 ") !=
                    std::string::npos,
            "slow writer did not preserve timing sequence endpoints");
    }

    std::uint64_t saturatedTimingDropped = 0;
    std::uint64_t saturatedTimingWritten = 0;
    {
        AsyncDiagnosticsWriter writer;
        DiagnosticsWriterOptions options;
        options.logDirectory = probeRoot / "timing-saturation";
        options.routineQueueCapacity = 4;
        options.criticalQueueCapacity = 2;
        options.flightLoopTimingQueueCapacity = 8;
        options.artificialWorkerDelayForTesting = std::chrono::seconds(2);
        options.artificialWorkerDelayRecordCountForTesting = 1;
        require(writer.Start(options), "timing-saturation writer failed to start");
        require(
            writer.TryEnqueueLine("event=timing-saturation-anchor"),
            "timing-saturation anchor was not accepted");
        require(
            WaitForDequeued(writer, 1, std::chrono::seconds(2)),
            "timing-saturation writer did not enter its delay");
        std::size_t accepted = 0;
        for (std::uint64_t sequence = 1; sequence <= 32; ++sequence) {
            FlightLoopTimingRecord timing;
            timing.sequence = sequence;
            if (writer.TryEnqueueFlightLoopTiming(timing)) {
                ++accepted;
            }
        }
        const auto saturated = writer.Snapshot();
        saturatedTimingDropped = saturated.droppedFlightLoopTimingFull;
        require(accepted == 8, "timing saturation accepted beyond fixed capacity");
        require(
            saturatedTimingDropped == 24,
            "timing-full counter was not exact");
        require(
            saturated.droppedContention == 0 &&
                saturated.droppedRoutineContention == 0 &&
                saturated.droppedCriticalContention == 0,
            "timing saturation contaminated mailbox contention counters");
        require(
            saturated.maximumFlightLoopTimingQueueDepth == 8,
            "timing saturation maximum depth was not exact");
        writer.Stop();
        const auto stopped = writer.Snapshot();
        saturatedTimingWritten = stopped.writtenFlightLoopTiming;
        require(
            saturatedTimingWritten == 8,
            "shutdown did not drain every accepted saturated timing record");
    }

    std::uint64_t lifecycleTimingWritten = 0;
    {
        AsyncDiagnosticsWriter writer;
        DiagnosticsWriterOptions options;
        options.logDirectory = probeRoot / "timing-lifecycle";
        options.routineQueueCapacity = 8;
        options.criticalQueueCapacity = 4;
        options.flightLoopTimingQueueCapacity = 16;
        require(writer.Start(options), "timing lifecycle writer failed to start");
        FlightLoopTimingRecord beforeDisable;
        beforeDisable.sequence = 100;
        require(
            writer.TryEnqueueFlightLoopTiming(beforeDisable),
            "pre-disable timing was not accepted");
        require(
            writer.WaitUntilIdleForTesting(std::chrono::seconds(5)),
            "pre-disable timing did not drain");
        // Plugin disable leaves the writer alive but stops the sole timing
        // producer. Re-enable resumes on that same producer and lane.
        FlightLoopTimingRecord afterEnable;
        afterEnable.sequence = 101;
        require(
            writer.TryEnqueueFlightLoopTiming(afterEnable),
            "post-enable timing was not accepted");
        writer.Stop();
        auto stopped = writer.Snapshot();
        lifecycleTimingWritten = stopped.writtenFlightLoopTiming;
        require(
            lifecycleTimingWritten == 2,
            "disable/re-enable shutdown did not drain timing records");
        FlightLoopTimingRecord afterStop;
        afterStop.sequence = 102;
        require(
            !writer.TryEnqueueFlightLoopTiming(afterStop) &&
                writer.Snapshot().rejectedFlightLoopTimingNotRunning == 1,
            "post-shutdown timing was not rejected and counted");

        require(writer.Start(options), "timing lifecycle restart failed");
        FlightLoopTimingRecord restartOne;
        restartOne.sequence = 200;
        FlightLoopTimingRecord restartTwo;
        restartTwo.sequence = 201;
        require(
            writer.TryEnqueueFlightLoopTiming(restartOne) &&
                writer.TryEnqueueFlightLoopTiming(restartTwo),
            "restart timing records were not accepted");
        writer.Stop();
        stopped = writer.Snapshot();
        require(
            stopped.writtenFlightLoopTiming == 2 &&
                stopped.droppedFlightLoopTimingFull == 0,
            "restart shutdown timing accounting was not exact");
        const auto logs = ReadAllLogs(options.logDirectory);
        require(
            logs.find("sequence=100 ") != std::string::npos &&
                logs.find("sequence=101 ") != std::string::npos &&
                logs.find("sequence=200 ") != std::string::npos &&
                logs.find("sequence=201 ") != std::string::npos &&
                logs.find("sequence=102 ") == std::string::npos,
            "timing lifecycle persistence was incorrect");
    }

    std::uint64_t unavailableP99Us = 0;
    std::uint64_t unavailableStorageFailures = 0;
    {
        AsyncDiagnosticsWriter writer;
        DiagnosticsWriterOptions options;
        options.logDirectory = probeRoot / "must-not-exist";
        options.routineQueueCapacity = 32;
        options.criticalQueueCapacity = 4;
        options.forceStorageFailureForTesting = true;
        require(writer.Start(options), "unavailable writer failed to start");
        std::vector<std::uint64_t> samples;
        samples.reserve(5'000);
        for (std::uint64_t index = 0; index < 5'000; ++index) {
            const auto started = std::chrono::steady_clock::now();
            (void)writer.TryEnqueueLine("event=unavailable-storage");
            samples.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - started)
                    .count()));
        }
        unavailableP99Us = Percentile99(samples);
        require(
            writer.WaitUntilIdleForTesting(std::chrono::seconds(5)),
            "unavailable writer did not drain its bounded queue");
        unavailableStorageFailures = writer.Snapshot().storageFailures;
        require(
            unavailableP99Us < 250,
            "unavailable-storage producer P99 exceeded 250 us");
        require(
            unavailableStorageFailures > 0,
            "forced storage failure was not observed by the writer");
        writer.Stop();
        require(
            !std::filesystem::exists(options.logDirectory),
            "forced storage failure touched the filesystem");
    }

    std::uint64_t formattingFailures = 0;
    {
        AsyncDiagnosticsWriter writer;
        DiagnosticsWriterOptions options;
        options.logDirectory = probeRoot / "format-failure";
        options.routineQueueCapacity = 8;
        options.criticalQueueCapacity = 4;
        require(writer.Start(options), "format-failure writer failed to start");
        require(
            writer.TryEnqueueDeferred([]() -> std::string {
                throw std::runtime_error("deterministic-format-failure");
            }),
            "throwing formatter was not accepted");
        require(
            writer.TryEnqueueLine(
                "event=after-format-failure",
                DiagnosticsImportance::Critical),
            "post-failure critical record was not accepted");
        require(
            writer.WaitUntilIdleForTesting(std::chrono::seconds(5)),
            "format-failure writer did not become idle");
        formattingFailures = writer.Snapshot().formattingFailures;
        require(formattingFailures == 1, "formatting failure was not isolated");
        writer.Stop();
        require(
            ReadAllLogs(options.logDirectory).find("event=after-format-failure") !=
                std::string::npos,
            "writer did not continue after a formatting failure");
    }

    std::filesystem::remove_all(probeRoot, cleanupError);
    require(!cleanupError, "probe cleanup failed");

    std::cout << "GATE_A_CALM_1_PROBE"
              << " slowP99Us=" << slowP99Us
              << " slowMaxUs=" << slowMaximumUs
              << " slowBurstUs=" << slowTotalUs
              << " slowDropped=" << slowDropped
              << " slowMaxDepth=" << slowMaximumDepth
              << " slowTimingMaxDepth=" << slowTimingMaximumDepth
              << " routineContention=" << slowRoutineContention
              << " criticalContention=" << slowCriticalContention
              << " saturatedTimingDropped=" << saturatedTimingDropped
              << " saturatedTimingWritten=" << saturatedTimingWritten
              << " lifecycleTimingWritten=" << lifecycleTimingWritten
              << " unavailableP99Us=" << unavailableP99Us
              << " unavailableFailures=" << unavailableStorageFailures
              << " formattingFailures=" << formattingFailures << "\n";

    if (!failures.empty()) {
        for (const auto& failure : failures) {
            std::cerr << "GATE_A_CALM_1_FAILURE: " << failure << "\n";
        }
        return 1;
    }
    std::cout << "PERFORMANCE_CONTRACT_GATE_A_CALM_1_PASSED\n";
    return 0;
}

}  // namespace xvatsim::tools::performance_contract_gate_a_calm_1
