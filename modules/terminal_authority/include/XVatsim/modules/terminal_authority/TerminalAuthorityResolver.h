#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "XVatsim/brain/BrainOwnedRuntime.h"
#include "XVatsim/core/ControllerAuthority.h"

namespace xvatsim::modules::terminal_authority {

struct TerminalAuthorityCatalog {
    bool available = false;
    std::uint64_t generation = 0;
    std::vector<core::authority::AuthorityPolygon> polygons;
    std::string status;
};

class TerminalAuthorityResolver final : public brain::BrainTerminalAuthorityWorker {
public:
    TerminalAuthorityResolver() = default;
    ~TerminalAuthorityResolver() override;

    brain::BrainTerminalAuthorityWorkerOutput ResolveAirportTerminalOwner(
        const brain::BrainTerminalAuthorityWorkerInput& input) override;

    void LoadPayloadForTesting(const std::string& terminalBoundaryGeoJson);
    void Reset();

private:
    brain::BrainTerminalAuthorityWorkerOutput ResolveFromCatalog(
        const brain::BrainTerminalAuthorityWorkerInput& input,
        const TerminalAuthorityCatalog& catalog) const;
    bool StartAsyncFetch(long long nowSeconds);
    void HarvestPendingFetch();

    mutable std::mutex mutex_;
    TerminalAuthorityCatalog catalog_{};
    TerminalAuthorityCatalog pendingCatalog_{};
    bool hasPendingCatalog_ = false;
    long long lastFetchTickSeconds_ = 0;
    bool lastFetchSucceeded_ = false;
    std::atomic<bool> fetchInProgress_{false};
    std::thread fetchThread_{};
    std::unordered_map<std::string, brain::BrainTerminalAuthorityWorkerOutput>
        airportCache_{};
};

}  // namespace xvatsim::modules::terminal_authority
