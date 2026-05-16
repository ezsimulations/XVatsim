#include "XVatsim/core/MapDataSource.h"

#include <cctype>
#include <optional>
#include <string>

namespace xvatsim::core::source_data {

namespace {

constexpr const char* kFallbackCommitHash = "fallback-master";
constexpr const char* kFallbackFirBoundariesGeoJsonUrl =
    "https://raw.githubusercontent.com/vatsimnetwork/vatspy-data-project/master/Boundaries.geojson";
constexpr const char* kFallbackFirBoundariesDatUrl =
    "https://raw.githubusercontent.com/vatsimnetwork/vatspy-data-project/master/FIRBoundaries.dat";
constexpr const char* kFallbackVatspyDatUrl =
    "https://raw.githubusercontent.com/vatsimnetwork/vatspy-data-project/master/VATSpy.dat";
constexpr const char* kFallbackSimAwareTraconGeoJsonUrl =
    "https://github.com/vatsimnetwork/simaware-tracon-project/releases/latest/download/TRACONBoundaries.geojson";

std::size_t SkipWhitespace(const std::string& value, std::size_t index) {
    while (index < value.size() &&
           std::isspace(static_cast<unsigned char>(value[index])) != 0) {
        ++index;
    }
    return index;
}

std::optional<std::string> DecodeJsonStringAt(
    const std::string& value,
    std::size_t quoteIndex,
    std::size_t* outNextIndex) {
    if (quoteIndex >= value.size() || value[quoteIndex] != '"') {
        return std::nullopt;
    }

    std::string decoded;
    for (std::size_t index = quoteIndex + 1; index < value.size(); ++index) {
        const auto character = value[index];
        if (character == '"') {
            if (outNextIndex != nullptr) {
                *outNextIndex = index + 1;
            }
            return decoded;
        }

        if (character != '\\') {
            decoded.push_back(character);
            continue;
        }

        if (index + 1 >= value.size()) {
            return std::nullopt;
        }

        const auto escaped = value[++index];
        switch (escaped) {
            case '"':
            case '\\':
            case '/':
                decoded.push_back(escaped);
                break;
            case 'b':
                decoded.push_back('\b');
                break;
            case 'f':
                decoded.push_back('\f');
                break;
            case 'n':
                decoded.push_back('\n');
                break;
            case 'r':
                decoded.push_back('\r');
                break;
            case 't':
                decoded.push_back('\t');
                break;
            default:
                return std::nullopt;
        }
    }

    return std::nullopt;
}

std::optional<std::string> ExtractJsonStringField(
    const std::string& payload,
    const std::string& fieldName) {
    std::size_t searchIndex = 0;
    for (;;) {
        const auto keyIndex = payload.find('"' + fieldName + '"', searchIndex);
        if (keyIndex == std::string::npos) {
            return std::nullopt;
        }

        std::size_t afterKeyIndex = keyIndex + fieldName.size() + 2;
        afterKeyIndex = SkipWhitespace(payload, afterKeyIndex);
        if (afterKeyIndex >= payload.size() || payload[afterKeyIndex] != ':') {
            searchIndex = keyIndex + 1;
            continue;
        }

        auto valueIndex = SkipWhitespace(payload, afterKeyIndex + 1);
        if (valueIndex >= payload.size() || payload[valueIndex] != '"') {
            return std::nullopt;
        }

        return DecodeJsonStringAt(payload, valueIndex, nullptr);
    }
}

bool IsTrustedHttpsUrl(const std::string& value) {
    return value.rfind("https://", 0) == 0;
}

bool OptionalTrustedHttpsUrl(const std::string& value) {
    return value.empty() || IsTrustedHttpsUrl(value);
}

}  // namespace

MapDataManifest ParseMapDataManifestJson(const std::string& payload) {
    MapDataManifest manifest;
    if (payload.empty()) {
        return manifest;
    }

    manifest.currentCommitHash =
        ExtractJsonStringField(payload, "current_commit_hash").value_or("");
    manifest.firBoundariesDatUrl =
        ExtractJsonStringField(payload, "fir_boundaries_dat_url").value_or("");
    manifest.firBoundariesGeoJsonUrl =
        ExtractJsonStringField(payload, "fir_boundaries_geojson_url").value_or("");
    manifest.vatspyDatUrl =
        ExtractJsonStringField(payload, "vatspy_dat_url").value_or("");
    manifest.simawareTraconGeoJsonUrl =
        ExtractJsonStringField(payload, "simaware_tracon_geojson_url")
            .value_or(kFallbackSimAwareTraconGeoJsonUrl);
    manifest.vatglassesOwnershipUrl =
        ExtractJsonStringField(payload, "vatglasses_ownership_url").value_or("");

    manifest.valid =
        !manifest.currentCommitHash.empty() &&
        IsTrustedHttpsUrl(manifest.firBoundariesDatUrl) &&
        IsTrustedHttpsUrl(manifest.firBoundariesGeoJsonUrl) &&
        IsTrustedHttpsUrl(manifest.vatspyDatUrl) &&
        IsTrustedHttpsUrl(manifest.simawareTraconGeoJsonUrl) &&
        OptionalTrustedHttpsUrl(manifest.vatglassesOwnershipUrl);
    return manifest;
}

MapDataManifest BuildFallbackMapDataManifest() {
    MapDataManifest manifest;
    manifest.valid = true;
    manifest.currentCommitHash = kFallbackCommitHash;
    manifest.firBoundariesDatUrl = kFallbackFirBoundariesDatUrl;
    manifest.firBoundariesGeoJsonUrl = kFallbackFirBoundariesGeoJsonUrl;
    manifest.vatspyDatUrl = kFallbackVatspyDatUrl;
    manifest.simawareTraconGeoJsonUrl = kFallbackSimAwareTraconGeoJsonUrl;
    return manifest;
}

}  // namespace xvatsim::core::source_data
