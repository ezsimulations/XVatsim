#include "XVatsim/modules/settings_store/SettingsStore.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <system_error>

namespace xvatsim::modules::settings_store {

namespace {

constexpr std::uintmax_t kMaxSettingsFileBytes = 16U * 1024U;
constexpr std::size_t kMaxSettingsLineChars = 256;
constexpr int kMaxSettingsLines = 128;
constexpr int kMinStoredWindowCoordinate = -32768;
constexpr int kMaxStoredWindowCoordinate = 32767;
constexpr float kMinOverlayOpacity = 0.45f;
constexpr float kMaxOverlayOpacity = 1.0f;
constexpr float kMinOverlayScale = 0.85f;
constexpr float kMaxOverlayScale = 1.35f;
constexpr float kMinAnimationSpeed = 0.60f;
constexpr float kMaxAnimationSpeed = 1.60f;

std::string TrimCopy(std::string value) {
    value.erase(
        value.begin(),
        std::find_if(
            value.begin(),
            value.end(),
            [](unsigned char c) { return std::isspace(c) == 0; }));
    value.erase(
        std::find_if(
            value.rbegin(),
            value.rend(),
            [](unsigned char c) { return std::isspace(c) == 0; })
            .base(),
        value.end());
    return value;
}

std::string NormalizeToken(std::string value) {
    value = TrimCopy(value);
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

StoredDisplayMode ParseDisplayMode(const std::string& value) {
    const auto normalizedValue = NormalizeToken(value);
    if (normalizedValue == "open") {
        return StoredDisplayMode::Open;
    }
    if (normalizedValue == "sleep") {
        return StoredDisplayMode::Sleep;
    }
    return StoredDisplayMode::Auto;
}

std::string SerializeDisplayMode(StoredDisplayMode mode) {
    switch (mode) {
        case StoredDisplayMode::Open:
            return "open";
        case StoredDisplayMode::Sleep:
            return "sleep";
        case StoredDisplayMode::Auto:
        default:
            return "auto";
    }
}

bool TryParseInt(const std::string& value, int* outValue) {
    if (outValue == nullptr) {
        return false;
    }

    const auto trimmedValue = TrimCopy(value);
    if (trimmedValue.empty()) {
        return false;
    }

    try {
        std::size_t consumed = 0;
        const auto parsedValue = std::stoi(trimmedValue, &consumed);
        if (consumed != trimmedValue.size()) {
            return false;
        }
        *outValue = parsedValue;
        return true;
    } catch (...) {
        return false;
    }
}

bool TryParseWindowCoordinate(const std::string& value, int* outValue) {
    int parsedValue = 0;
    if (!TryParseInt(value, &parsedValue)) {
        return false;
    }

    if (parsedValue < kMinStoredWindowCoordinate ||
        parsedValue > kMaxStoredWindowCoordinate) {
        return false;
    }

    *outValue = parsedValue;
    return true;
}

bool TryParseFloat(const std::string& value, float* outValue) {
    if (outValue == nullptr) {
        return false;
    }

    const auto trimmedValue = TrimCopy(value);
    if (trimmedValue.empty()) {
        return false;
    }

    try {
        std::size_t consumed = 0;
        const auto parsedValue = std::stof(trimmedValue, &consumed);
        if (consumed != trimmedValue.size() || !std::isfinite(parsedValue)) {
            return false;
        }
        *outValue = parsedValue;
        return true;
    } catch (...) {
        return false;
    }
}

bool TryParseBool(const std::string& value, bool* outValue) {
    if (outValue == nullptr) {
        return false;
    }

    const auto normalizedValue = NormalizeToken(value);
    if (normalizedValue == "1" || normalizedValue == "true" ||
        normalizedValue == "on" || normalizedValue == "yes") {
        *outValue = true;
        return true;
    }

    if (normalizedValue == "0" || normalizedValue == "false" ||
        normalizedValue == "off" || normalizedValue == "no") {
        *outValue = false;
        return true;
    }

    return false;
}

float ClampFloat(float value, float minimumValue, float maximumValue, float fallbackValue) {
    if (!std::isfinite(value)) {
        return fallbackValue;
    }

    return std::clamp(value, minimumValue, maximumValue);
}

PluginSettings NormalizeSettings(PluginSettings settings) {
    settings.overlayOpacity = ClampFloat(
        settings.overlayOpacity,
        kMinOverlayOpacity,
        kMaxOverlayOpacity,
        1.0f);
    settings.overlayScale = ClampFloat(
        settings.overlayScale,
        kMinOverlayScale,
        kMaxOverlayScale,
        1.0f);
    settings.animationSpeed = ClampFloat(
        settings.animationSpeed,
        kMinAnimationSpeed,
        kMaxAnimationSpeed,
        1.0f);

    if (settings.hasWindowPosition) {
        settings.windowLeft = std::clamp(
            settings.windowLeft,
            kMinStoredWindowCoordinate,
            kMaxStoredWindowCoordinate);
        settings.windowTop = std::clamp(
            settings.windowTop,
            kMinStoredWindowCoordinate,
            kMaxStoredWindowCoordinate);
    } else {
        settings.windowLeft = 0;
        settings.windowTop = 0;
    }

    return settings;
}

}  // namespace

void SettingsStore::SetPath(const std::string& path) {
    path_ = path;
}

PluginSettings SettingsStore::Load() const {
    PluginSettings settings;
    if (path_.empty()) {
        return settings;
    }

    const auto settingsPath = std::filesystem::path(path_);
    std::error_code fileStatusError;
    if (std::filesystem::exists(settingsPath, fileStatusError)) {
        const auto fileSize = std::filesystem::file_size(settingsPath, fileStatusError);
        if (!fileStatusError && fileSize > kMaxSettingsFileBytes) {
            return settings;
        }
    }

    std::ifstream input(settingsPath);
    if (!input.is_open()) {
        return settings;
    }

    bool hasWindowLeft = false;
    bool hasWindowTop = false;
    int windowLeft = 0;
    int windowTop = 0;

    std::string line;
    int lineCount = 0;
    while (lineCount < kMaxSettingsLines && std::getline(input, line)) {
        ++lineCount;
        if (line.size() > kMaxSettingsLineChars) {
            continue;
        }

        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        const auto key = NormalizeToken(line.substr(0, separator));
        const auto value = TrimCopy(line.substr(separator + 1));

        if (key == "display_mode") {
            settings.displayMode = ParseDisplayMode(value);
            continue;
        }

        if (key == "standby_assist") {
            TryParseBool(value, &settings.standbyAssistEnabled);
            continue;
        }

        if (key == "window_left") {
            int parsedWindowLeft = 0;
            if (TryParseWindowCoordinate(value, &parsedWindowLeft)) {
                windowLeft = parsedWindowLeft;
                hasWindowLeft = true;
            }
            continue;
        }

        if (key == "window_top") {
            int parsedWindowTop = 0;
            if (TryParseWindowCoordinate(value, &parsedWindowTop)) {
                windowTop = parsedWindowTop;
                hasWindowTop = true;
            }
            continue;
        }

        if (key == "overlay_opacity") {
            TryParseFloat(value, &settings.overlayOpacity);
            continue;
        }

        if (key == "overlay_scale") {
            TryParseFloat(value, &settings.overlayScale);
            continue;
        }

        if (key == "animation_speed") {
            TryParseFloat(value, &settings.animationSpeed);
            continue;
        }
    }

    if (hasWindowLeft && hasWindowTop) {
        settings.hasWindowPosition = true;
        settings.windowLeft = windowLeft;
        settings.windowTop = windowTop;
    }

    return NormalizeSettings(settings);
}

bool SettingsStore::Save(const PluginSettings& settings) const {
    if (path_.empty()) {
        return false;
    }

    try {
        const auto normalizedSettings = NormalizeSettings(settings);
        const auto settingsPath = std::filesystem::path(path_);
        if (settingsPath.has_parent_path()) {
            std::filesystem::create_directories(settingsPath.parent_path());
        }

        auto temporaryPath = settingsPath;
        temporaryPath += ".tmp";

        std::ofstream output(temporaryPath, std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }

        output << "display_mode=" << SerializeDisplayMode(normalizedSettings.displayMode) << "\n";
        output << "standby_assist="
               << (normalizedSettings.standbyAssistEnabled ? "true" : "false") << "\n";
        output << std::fixed << std::setprecision(2);
        output << "overlay_opacity=" << normalizedSettings.overlayOpacity << "\n";
        output << "overlay_scale=" << normalizedSettings.overlayScale << "\n";
        output << "animation_speed=" << normalizedSettings.animationSpeed << "\n";
        if (normalizedSettings.hasWindowPosition) {
            output << "window_left=" << normalizedSettings.windowLeft << "\n";
            output << "window_top=" << normalizedSettings.windowTop << "\n";
        }
        output.flush();
        if (!output.good()) {
            return false;
        }
        output.close();

        std::error_code renameError;
        std::filesystem::rename(temporaryPath, settingsPath, renameError);
        if (renameError) {
            std::error_code copyError;
            std::filesystem::copy_file(
                temporaryPath,
                settingsPath,
                std::filesystem::copy_options::overwrite_existing,
                copyError);
            std::error_code removeError;
            std::filesystem::remove(temporaryPath, removeError);
            if (copyError) {
                return false;
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace xvatsim::modules::settings_store
