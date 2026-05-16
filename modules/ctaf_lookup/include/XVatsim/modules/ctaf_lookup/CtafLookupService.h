#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::modules::ctaf_lookup {

struct CtafLookupEntry {
    bool resolved = false;
    bool available = false;
    std::string frequency;
    long long lastAttemptTickSeconds = 0;
    int failureCount = 0;
};

class CtafLookupService {
public:
    CtafLookupService() = default;
    ~CtafLookupService();

    CtafLookupEntry Lookup(const std::string& airportIcao);
    brain::ManualQuerySnapshot RunManualCtafQuery(const std::string& commandText);
    void Reset();

private:
    CtafLookupEntry LookupSync(const std::string& airportIcao);
    bool StartAsyncFetch(const std::string& airportIcao, long long requestTickSeconds);
    void HarvestPendingFetch();

    std::unordered_map<std::string, CtafLookupEntry> cache_{};
    std::string pendingFetchAirportIcao_{};
    CtafLookupEntry pendingFetchEntry_{};
    bool hasPendingFetchEntry_ = false;
    std::atomic<bool> fetchInProgress_{false};
    std::mutex fetchMutex_{};
    std::thread fetchThread_{};
};

}  // namespace xvatsim::modules::ctaf_lookup
