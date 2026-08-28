#pragma once

#include <cstdint>
#include <string>

#include "XVatsim/brain/BrainOwnedRuntime.h"

namespace xvatsim::brain {

constexpr long long kBrainMetarRefreshMs = 60'000;
constexpr long long kBrainMetarFirstFailureBackoffMs = 120'000;
constexpr long long kBrainMetarSecondFailureBackoffMs = 240'000;
constexpr long long kBrainMetarMaximumFailureBackoffMs = 300'000;
constexpr long long kBrainMetarFreshnessMs = 90 * 60 * 1000;
constexpr long long kBrainMetarLookupPendingMs = 20'000;
constexpr long long kBrainMetarLookupSpotlightMs = 8'000;
constexpr long long kBrainMetarLookupFailureMs = 4'000;

bool NormalizeStrictMetarIcao(
    const std::string& input,
    std::string* normalized);

std::uint64_t FingerprintBrainMetarContent(const std::string& rawMetar);

BrainMetarParsedObservation ParseBrainOwnedMetarReport(
    const std::string& verifiedStationIcao,
    const std::string& rawMetar,
    std::int64_t referenceUtcUnixSeconds);

const char* BrainMetarCategoryToken(BrainMetarFlightCategory category);
const char* BrainMetarVisibleStateToken(BrainMetarVisibleState state);

void ResetBrainOwnedMetarForHardBoundary(BrainOwnedRuntimeState* state);
void SuspendBrainOwnedMetarForXPilotDisconnect(BrainOwnedRuntimeState* state);
void ResumeBrainOwnedMetarAfterXPilotReconnect(BrainOwnedRuntimeState* state);

}  // namespace xvatsim::brain
