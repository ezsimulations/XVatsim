#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "XVatsim/modules/runtime_workers/AsyncDiagnosticsWriter.h"

namespace xvatsim::modules::runtime_workers {

struct FlightLoopDiagnosticsPolicy {
    std::uint64_t summaryIntervalSeconds = 60;
    std::uint64_t outlierThresholdUs = 10'000;
    std::uint64_t outlierMinimumIntervalSeconds = 5;
};

struct FlightLoopTimingSummary {
    std::uint64_t windowSeconds = 0;
    std::uint64_t firstSequence = 0;
    std::uint64_t lastSequence = 0;
    std::uint64_t sampleCount = 0;
    std::uint64_t totalCompleteUs = 0;
    std::uint64_t minimumCompleteUs = 0;
    std::uint64_t maximumCompleteUs = 0;
    std::uint64_t p95UpperUs = 0;
    std::uint64_t p99UpperUs = 0;
    std::uint64_t maximumRefreshUs = 0;
    std::uint64_t maximumDiagnosticsSubmissionUs = 0;
    std::uint64_t over1msCount = 0;
    std::uint64_t over5msCount = 0;
    std::uint64_t over10msCount = 0;
    std::uint64_t over40msCount = 0;
    std::uint64_t outlierRecordsEmitted = 0;
    std::uint64_t accessoryPathCount = 0;
    std::uint64_t activeCadenceCount = 0;
};

struct FlightLoopDiagnosticsDecision {
    bool emitOutlier = false;
    bool emitSummary = false;
    FlightLoopTimingSummary summary;
};

// Reduces high-rate callback timings into a one-minute statistical receipt.
// Individual records are retained only for rate-limited outliers. The reducer
// owns diagnostics policy only; it does not affect flight-loop scheduling.
class FlightLoopDiagnosticsReducer {
public:
    explicit FlightLoopDiagnosticsReducer(
        FlightLoopDiagnosticsPolicy policy = {});

    FlightLoopDiagnosticsDecision Observe(
        const FlightLoopTimingRecord& timing,
        std::uint64_t nowSeconds) noexcept;

    std::optional<FlightLoopTimingSummary> Flush(
        std::uint64_t nowSeconds) noexcept;

    void Reset() noexcept;

private:
    static constexpr std::size_t kHistogramBucketCount = 11;

    FlightLoopTimingSummary BuildSummary(std::uint64_t nowSeconds) const noexcept;
    void ClearWindow() noexcept;

    FlightLoopDiagnosticsPolicy policy_;
    bool windowStarted_ = false;
    std::uint64_t windowStartedSeconds_ = 0;
    std::uint64_t firstSequence_ = 0;
    std::uint64_t lastSequence_ = 0;
    std::uint64_t sampleCount_ = 0;
    std::uint64_t totalCompleteUs_ = 0;
    std::uint64_t minimumCompleteUs_ = 0;
    std::uint64_t maximumCompleteUs_ = 0;
    std::uint64_t maximumRefreshUs_ = 0;
    std::uint64_t maximumDiagnosticsSubmissionUs_ = 0;
    std::uint64_t over1msCount_ = 0;
    std::uint64_t over5msCount_ = 0;
    std::uint64_t over10msCount_ = 0;
    std::uint64_t over40msCount_ = 0;
    std::uint64_t outlierRecordsEmitted_ = 0;
    std::uint64_t accessoryPathCount_ = 0;
    std::uint64_t activeCadenceCount_ = 0;
    std::array<std::uint64_t, kHistogramBucketCount> histogram_{};
    bool hasLastOutlierRecord_ = false;
    std::uint64_t lastOutlierRecordSeconds_ = 0;
};

std::string FormatFlightLoopTimingSummary(
    const FlightLoopTimingSummary& summary,
    std::string_view reason);

}  // namespace xvatsim::modules::runtime_workers
