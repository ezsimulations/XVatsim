#pragma once

#include <string>

namespace xvatsim::modules::settings_store {

enum class StoredDisplayMode {
    Auto,
    Open,
    Sleep,
};

struct PluginSettings {
    StoredDisplayMode displayMode = StoredDisplayMode::Auto;
    bool standbyAssistEnabled = false;
    bool hasWindowPosition = false;
    int windowLeft = 0;
    int windowTop = 0;
    float overlayOpacity = 1.0f;
    float overlayScale = 1.0f;
    float animationSpeed = 1.0f;
    long long lastUpdateCheckUnixSeconds = 0;
};

class SettingsStore {
public:
    SettingsStore() = default;

    void SetPath(const std::string& path);
    PluginSettings Load() const;
    bool Save(const PluginSettings& settings) const;

private:
    std::string path_{};
};

}  // namespace xvatsim::modules::settings_store
