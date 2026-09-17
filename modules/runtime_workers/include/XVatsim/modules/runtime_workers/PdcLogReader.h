#pragma once

#include "XVatsim/brain/BrainPdcLogTypes.h"

#include <filesystem>
#include <string_view>

namespace xvatsim::modules::runtime_workers {

bool ParseXPilotNetworkLogLine(
    std::string_view line,
    std::uint64_t beginOffset,
    std::uint64_t endOffset,
    brain::BrainPdcLogRecordFact* fact);

class PdcLogReader {
public:
    explicit PdcLogReader(std::filesystem::path directoryOverride = {});

    brain::BrainPdcLogFact Read(
        const brain::BrainPdcLogRequest& request) const noexcept;

private:
    std::filesystem::path directoryOverride_;
};

}  // namespace xvatsim::modules::runtime_workers
