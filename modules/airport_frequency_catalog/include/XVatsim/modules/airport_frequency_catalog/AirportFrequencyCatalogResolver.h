#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

#include "XVatsim/brain/BrainOwnedRuntime.h"

namespace xvatsim::modules::airport_frequency_catalog {

class AirportFrequencyCatalogResolver final : public brain::BrainAirportFrequencyWorker {
public:
    AirportFrequencyCatalogResolver() = default;

    brain::BrainAirportFrequencyWorkerOutput ResolveAirportFrequencies(
        const brain::BrainAirportFrequencyWorkerInput& input) override;

    void LoadFrqCsvPayloadForTesting(
        const std::string& frqCsvPayload,
        std::uint64_t sourceGeneration = 1);
    void Reset();

private:
    mutable std::mutex mutex_;
    std::string frqCsvPayload_;
    std::uint64_t sourceGeneration_ = 0;
    std::unordered_map<std::string, brain::BrainAirportFrequencyWorkerOutput>
        airportPairCache_{};
};

}  // namespace xvatsim::modules::airport_frequency_catalog
