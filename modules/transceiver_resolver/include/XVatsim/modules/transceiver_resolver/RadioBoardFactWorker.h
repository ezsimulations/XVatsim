#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "XVatsim/brain/BrainOwnedWorkerTypes.h"

namespace xvatsim::modules::transceiver_resolver {

struct RadioBoardFactIdentity {
    std::uint64_t requestId = 0;
    std::uint64_t lifecycleEpoch = 0;
    std::uint64_t controllerGeneration = 0;
    std::uint64_t controllerContentDigest = 0;
    std::uint64_t requestedSourceGeneration = 0;
    std::uint64_t requestedSourceContentDigest = 0;
    std::uint64_t requestedMonotonicMs = 0;
    bool aircraftValid = false;
    double aircraftLatitudeDeg = 0.0;
    double aircraftLongitudeDeg = 0.0;
    double aircraftAltitudeAglFt = 0.0;
};

struct RadioBoardFactRequest {
    RadioBoardFactIdentity identity;
    brain::BrainRadioRangeWorkerInput input;
};

struct RadioBoardFact {
    RadioBoardFactIdentity identity;
    brain::BrainRadioRangeWorkerOutput output;
    bool completed = false;
    bool failed = false;
    std::string failureReason;
    std::uint64_t completedMonotonicMs = 0;
    std::uint64_t workerWallUs = 0;
    std::uint64_t workerCpuUs = 0;
    bool workerCpuAvailable = false;
    std::uint64_t sourceGeneration = 0;
    std::uint64_t sourceContentDigest = 0;
};

struct RadioBoardFactWorkerSnapshot {
    bool started = false;
    bool running = false;
    bool pending = false;
    bool ready = false;
    bool priorityRequested = false;
    bool prioritySucceeded = false;
    std::uint64_t submissions = 0;
    std::uint64_t replacements = 0;
    std::uint64_t completions = 0;
    std::uint64_t failures = 0;
    std::uint64_t harvests = 0;
    std::uint64_t maximumPendingDepth = 0;
    std::uint64_t workerThreadIdentity = 0;
    std::uint64_t sourceGeneration = 0;
    std::uint64_t sourceContentDigest = 0;
};

class RadioBoardFactWorker {
public:
    RadioBoardFactWorker();
    ~RadioBoardFactWorker();
    RadioBoardFactWorker(const RadioBoardFactWorker&) = delete;
    RadioBoardFactWorker& operator=(const RadioBoardFactWorker&) = delete;

    bool Start();
    void Stop();
    bool StartLatest(RadioBoardFactRequest request);
    bool TryHarvest(RadioBoardFact* fact);
    bool HasWork() const;
    RadioBoardFactWorkerSnapshot Snapshot() const;

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

}  // namespace xvatsim::modules::transceiver_resolver
