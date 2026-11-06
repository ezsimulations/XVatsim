#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

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

    bool Start();
    void Stop();
    bool StartCheck(const UpdateCheckRequest& request);
    std::optional<UpdateCheckResult> HarvestResult();
    bool InProgress() const;

private:
    void WorkerMain();

    mutable std::mutex mutex_{};
    std::condition_variable wake_{};
    std::thread worker_{};
    bool started_ = false;
    bool stopping_ = false;
    bool inProgress_ = false;
    bool resultReady_ = false;
    std::optional<UpdateCheckRequest> pendingRequest_{};
    UpdateCheckResult result_{};
};

}  // namespace xvatsim::modules::update_checker
