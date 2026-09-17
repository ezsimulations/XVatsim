#include "XVatsim/modules/runtime_workers/FlightLoopDiagnosticsReducer.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
#include <sstream>

namespace xvatsim::modules::runtime_workers {
namespace {

constexpr std::array<std::uint64_t, 11> kHistogramUpperBoundsUs = {
    50,
    100,
    250,
    500,
    1'000,
    2'000,
    5'000,
    10'000,
    20'000,
    40'000,
    std::numeric_limits<std::uint64_t>::max(),
};

std::uint64_t PercentileUpperBound(
    const std::array<std::uint64_t, 11>& histogram,
    std::uint64_t sampleCount,
    std::uint64_t maximumCompleteUs,
    std::uint64_t numerator) noexcept {
    if (sampleCount == 0) return 0;
    const auto target = (sampleCount * numerator + 99) / 100;
    std::uint64_t cumulative = 0;
    for (std::size_t index = 0; index < histogram.size(); ++index) {
        cumulative += histogram[index];
        if (cumulative < target) continue;
        const auto bound = kHistogramUpperBoundsUs[index];
        return bound == std::numeric_limits<std::uint64_t>::max()
            ? maximumCompleteUs
            : bound;
    }
    return maximumCompleteUs;
}

}  // namespace

FlightLoopDiagnosticsReducer::FlightLoopDiagnosticsReducer(
    FlightLoopDiagnosticsPolicy policy)
    : policy_(policy) {
    policy_.summaryIntervalSeconds =
        std::max<std::uint64_t>(1, policy_.summaryIntervalSeconds);
    policy_.outlierThresholdUs =
        std::max<std::uint64_t>(1, policy_.outlierThresholdUs);
}

FlightLoopDiagnosticsDecision FlightLoopDiagnosticsReducer::Observe(
    const FlightLoopTimingRecord& timing,
    std::uint64_t nowSeconds) noexcept {
    FlightLoopDiagnosticsDecision decision;
    if (!windowStarted_) {
        windowStarted_ = true;
        windowStartedSeconds_ = nowSeconds;
        firstSequence_ = timing.sequence;
        minimumCompleteUs_ = timing.completeBeforeTelemetryUs;
    }

    lastSequence_ = timing.sequence;
    ++sampleCount_;
    totalCompleteUs_ += timing.completeBeforeTelemetryUs;
    minimumCompleteUs_ =
        std::min(minimumCompleteUs_, timing.completeBeforeTelemetryUs);
    maximumCompleteUs_ =
        std::max(maximumCompleteUs_, timing.completeBeforeTelemetryUs);
    maximumRefreshUs_ = std::max(maximumRefreshUs_, timing.refreshUs);
    maximumDiagnosticsSubmissionUs_ = std::max(
        maximumDiagnosticsSubmissionUs_,
        timing.diagnosticsSubmissionUs);
    if (timing.completeBeforeTelemetryUs >= 1'000) ++over1msCount_;
    if (timing.completeBeforeTelemetryUs >= 5'000) ++over5msCount_;
    if (timing.completeBeforeTelemetryUs >= 10'000) ++over10msCount_;
    if (timing.completeBeforeTelemetryUs >= 40'000) ++over40msCount_;
    if (timing.accessoryPath) ++accessoryPathCount_;
    if (timing.activeCadence) ++activeCadenceCount_;
    const auto bucket = std::find_if(
        kHistogramUpperBoundsUs.begin(),
        kHistogramUpperBoundsUs.end(),
        [&](std::uint64_t upper) {
            return timing.completeBeforeTelemetryUs <= upper;
        });
    const auto bucketIndex = static_cast<std::size_t>(
        std::distance(kHistogramUpperBoundsUs.begin(), bucket));
    ++histogram_[std::min(bucketIndex, histogram_.size() - 1)];

    if (timing.completeBeforeTelemetryUs >= policy_.outlierThresholdUs &&
        (!hasLastOutlierRecord_ ||
         nowSeconds < lastOutlierRecordSeconds_ ||
         nowSeconds - lastOutlierRecordSeconds_ >=
             policy_.outlierMinimumIntervalSeconds)) {
        decision.emitOutlier = true;
        hasLastOutlierRecord_ = true;
        lastOutlierRecordSeconds_ = nowSeconds;
        ++outlierRecordsEmitted_;
    }

    if (nowSeconds >= windowStartedSeconds_ &&
        nowSeconds - windowStartedSeconds_ >=
            policy_.summaryIntervalSeconds) {
        decision.emitSummary = true;
        decision.summary = BuildSummary(nowSeconds);
        ClearWindow();
    }
    return decision;
}

std::optional<FlightLoopTimingSummary> FlightLoopDiagnosticsReducer::Flush(
    std::uint64_t nowSeconds) noexcept {
    if (sampleCount_ == 0) return std::nullopt;
    auto summary = BuildSummary(nowSeconds);
    ClearWindow();
    return summary;
}

void FlightLoopDiagnosticsReducer::Reset() noexcept {
    ClearWindow();
    hasLastOutlierRecord_ = false;
    lastOutlierRecordSeconds_ = 0;
}

FlightLoopTimingSummary FlightLoopDiagnosticsReducer::BuildSummary(
    std::uint64_t nowSeconds) const noexcept {
    FlightLoopTimingSummary summary;
    summary.windowSeconds = nowSeconds >= windowStartedSeconds_
        ? nowSeconds - windowStartedSeconds_
        : 0;
    summary.firstSequence = firstSequence_;
    summary.lastSequence = lastSequence_;
    summary.sampleCount = sampleCount_;
    summary.totalCompleteUs = totalCompleteUs_;
    summary.minimumCompleteUs = minimumCompleteUs_;
    summary.maximumCompleteUs = maximumCompleteUs_;
    summary.p95UpperUs = PercentileUpperBound(
        histogram_, sampleCount_, maximumCompleteUs_, 95);
    summary.p99UpperUs = PercentileUpperBound(
        histogram_, sampleCount_, maximumCompleteUs_, 99);
    summary.maximumRefreshUs = maximumRefreshUs_;
    summary.maximumDiagnosticsSubmissionUs =
        maximumDiagnosticsSubmissionUs_;
    summary.over1msCount = over1msCount_;
    summary.over5msCount = over5msCount_;
    summary.over10msCount = over10msCount_;
    summary.over40msCount = over40msCount_;
    summary.outlierRecordsEmitted = outlierRecordsEmitted_;
    summary.accessoryPathCount = accessoryPathCount_;
    summary.activeCadenceCount = activeCadenceCount_;
    return summary;
}

void FlightLoopDiagnosticsReducer::ClearWindow() noexcept {
    windowStarted_ = false;
    windowStartedSeconds_ = 0;
    firstSequence_ = 0;
    lastSequence_ = 0;
    sampleCount_ = 0;
    totalCompleteUs_ = 0;
    minimumCompleteUs_ = 0;
    maximumCompleteUs_ = 0;
    maximumRefreshUs_ = 0;
    maximumDiagnosticsSubmissionUs_ = 0;
    over1msCount_ = 0;
    over5msCount_ = 0;
    over10msCount_ = 0;
    over40msCount_ = 0;
    outlierRecordsEmitted_ = 0;
    accessoryPathCount_ = 0;
    activeCadenceCount_ = 0;
    histogram_.fill(0);
}

std::string FormatFlightLoopTimingSummary(
    const FlightLoopTimingSummary& summary,
    std::string_view reason) {
    const auto average = summary.sampleCount == 0
        ? 0.0
        : static_cast<double>(summary.totalCompleteUs) /
              static_cast<double>(summary.sampleCount);
    std::ostringstream stream;
    stream << "event=flight-loop-summary"
           << " reason=" << reason
           << " windowSeconds=" << summary.windowSeconds
           << " firstSequence=" << summary.firstSequence
           << " lastSequence=" << summary.lastSequence
           << " samples=" << summary.sampleCount
           << " averageUs=" << std::fixed << std::setprecision(1) << average
           << " minimumUs=" << summary.minimumCompleteUs
           << " p95UpperUs=" << summary.p95UpperUs
           << " p99UpperUs=" << summary.p99UpperUs
           << " maximumUs=" << summary.maximumCompleteUs
           << " refreshMaximumUs=" << summary.maximumRefreshUs
           << " diagnosticsSubmissionMaximumUs="
           << summary.maximumDiagnosticsSubmissionUs
           << " over1ms=" << summary.over1msCount
           << " over5ms=" << summary.over5msCount
           << " over10ms=" << summary.over10msCount
           << " over40ms=" << summary.over40msCount
           << " outlierRecords=" << summary.outlierRecordsEmitted
           << " accessoryPath=" << summary.accessoryPathCount
           << " activeCadence=" << summary.activeCadenceCount;
    return stream.str();
}

}  // namespace xvatsim::modules::runtime_workers
