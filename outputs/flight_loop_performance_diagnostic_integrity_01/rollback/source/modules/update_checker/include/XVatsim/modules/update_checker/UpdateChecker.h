#pragma once

#include <optional>
#include <string>
#include <thread>
#include <mutex>

namespace xvatsim::modules::update_checker {

enum class UpdateCheckSource {
    Automatic,
    Manual,
};

enum class UpdateStatus {
    Unknown,
    Current,
    Available,
    CheckFailed,
    InProgress,
};

struct UpdateCheckRequest {
    std::string installedVersion;
    std::string manifestUrl;
    UpdateCheckSource source = UpdateCheckSource::Automatic;
};

struct UpdateCheckResult {
    UpdateStatus status = UpdateStatus::Unknown;
    UpdateCheckSource source = UpdateCheckSource::Automatic;
    std::string installedVersion;
    std::string latestVersion;
    std::string minimumSupportedVersion;
    std::string packageSha256;
    std::string pluginSha256;
    std::string downloadPageUrl;
    std::string message;
    std::string releaseNotes;
    std::string manifestUrl;
    std::string errorClass;
    bool critical = false;
    bool validManifest = false;
    bool updateAvailable = false;
};

const char* ToString(UpdateCheckSource source);
const char* ToString(UpdateStatus status);

UpdateCheckResult EvaluateUpdateManifestPayload(
    const UpdateCheckRequest& request,
    const std::string& payload);

class UpdateChecker {
public:
    UpdateChecker() = default;
    ~UpdateChecker();

    UpdateChecker(const UpdateChecker&) = delete;
    UpdateChecker& operator=(const UpdateChecker&) = delete;

    bool StartCheck(const UpdateCheckRequest& request);
    std::optional<UpdateCheckResult> HarvestResult();
    bool InProgress() const;

private:
    mutable std::mutex mutex_{};
    std::thread worker_{};
    bool inProgress_ = false;
    bool resultReady_ = false;
    UpdateCheckResult result_{};
};

}  // namespace xvatsim::modules::update_checker
