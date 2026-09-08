#pragma once

#include <string>

namespace xvatsim::modules::settings_store {

enum class StoredDisplayMode {
    Auto,
    Open,
    Sleep,
};

enum class StoredOperatingMode {
    IFR,
    VFR,
};

enum class StoredOperatingModeLoadStatus {
    Missing,
    Valid,
    Invalid,
    Unavailable,
};

struct PluginSettings {
    StoredOperatingMode operatingMode = StoredOperatingMode::IFR;
    StoredOperatingModeLoadStatus operatingModeLoadStatus =
        StoredOperatingModeLoadStatus::Missing;
    StoredDisplayMode displayMode = StoredDisplayMode::Auto;
    bool standbyAssistEnabled = false;
    bool directCtafStandbyAssistEnabled = false;
    std::string directCtafStandbyAssistGateSource = "default";
    // Safety switch for the endpoint-distance/equal-source terminal relevance
    // policy and corrected 250 NM Center boundary. Set
    // terminal_relevance_v2=false in XVatsim settings to restore the complete
    // pre-change controller-relevance path during controlled online testing.
    bool terminalRelevanceV2Enabled = true;
    // Subordinate rollback switch for vNAS sector/TCP ownership precedence.
    // The vNAS module supplies facts only; the Brain remains the sole display
    // decision-maker. The master switch above also disables this path.
    bool vnasSectorPrecedenceEnabled = true;
    bool sourceOwnedFallbackStableKeyLiveConsumptionEnabled = false;
    std::string sourceOwnedFallbackStableKeyLiveConsumptionGateSource =
        "default";
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
