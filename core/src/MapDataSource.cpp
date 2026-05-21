#include "XVatsim/core/MapDataSource.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

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

std::vector<std::string> ExtractJsonStringArrayField(
    const std::string& payload,
    const std::string& fieldName) {
    std::vector<std::string> values;
    std::size_t searchIndex = 0;
    for (;;) {
        const auto keyIndex = payload.find('"' + fieldName + '"', searchIndex);
        if (keyIndex == std::string::npos) {
            return values;
        }

        std::size_t afterKeyIndex = keyIndex + fieldName.size() + 2;
        afterKeyIndex = SkipWhitespace(payload, afterKeyIndex);
        if (afterKeyIndex >= payload.size() || payload[afterKeyIndex] != ':') {
            searchIndex = keyIndex + 1;
            continue;
        }

        auto valueIndex = SkipWhitespace(payload, afterKeyIndex + 1);
        if (valueIndex >= payload.size() || payload[valueIndex] != '[') {
            return values;
        }
        ++valueIndex;

        for (;;) {
            valueIndex = SkipWhitespace(payload, valueIndex);
            if (valueIndex >= payload.size()) {
                return {};
            }
            if (payload[valueIndex] == ']') {
                return values;
            }
            if (payload[valueIndex] != '"') {
                return {};
            }

            std::size_t nextIndex = valueIndex;
            auto decoded = DecodeJsonStringAt(payload, valueIndex, &nextIndex);
            if (!decoded.has_value()) {
                return {};
            }
            values.push_back(*decoded);

            valueIndex = SkipWhitespace(payload, nextIndex);
            if (valueIndex >= payload.size()) {
                return {};
            }
            if (payload[valueIndex] == ']') {
                return values;
            }
            if (payload[valueIndex] != ',') {
                return {};
            }
            ++valueIndex;
        }
    }
}

std::optional<std::string> ExtractJsonArrayPayload(
    const std::string& payload,
    const std::string& fieldName) {
    const auto keyIndex = payload.find('"' + fieldName + '"');
    if (keyIndex == std::string::npos) {
        return std::nullopt;
    }

    auto arrayStart = SkipWhitespace(payload, keyIndex + fieldName.size() + 2);
    if (arrayStart >= payload.size() || payload[arrayStart] != ':') {
        return std::nullopt;
    }
    arrayStart = SkipWhitespace(payload, arrayStart + 1);
    if (arrayStart >= payload.size() || payload[arrayStart] != '[') {
        return std::nullopt;
    }

    bool inString = false;
    bool escaped = false;
    int depth = 0;
    for (std::size_t index = arrayStart; index < payload.size(); ++index) {
        const auto character = payload[index];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                inString = false;
            }
            continue;
        }

        if (character == '"') {
            inString = true;
            continue;
        }
        if (character == '[') {
            ++depth;
        } else if (character == ']') {
            --depth;
            if (depth == 0) {
                return payload.substr(arrayStart + 1, index - arrayStart - 1);
            }
        }
    }

    return std::nullopt;
}

std::vector<std::string> ExtractJsonObjectsFromArrayPayload(
    const std::string& arrayPayload) {
    std::vector<std::string> objects;
    for (std::size_t index = 0; index < arrayPayload.size();) {
        index = SkipWhitespace(arrayPayload, index);
        if (index >= arrayPayload.size()) {
            break;
        }
        if (arrayPayload[index] != '{') {
            ++index;
            continue;
        }

        bool inString = false;
        bool escaped = false;
        int depth = 0;
        std::size_t objectEnd = std::string::npos;
        for (std::size_t scanIndex = index; scanIndex < arrayPayload.size(); ++scanIndex) {
            const auto character = arrayPayload[scanIndex];
            if (inString) {
                if (escaped) {
                    escaped = false;
                } else if (character == '\\') {
                    escaped = true;
                } else if (character == '"') {
                    inString = false;
                }
                continue;
            }

            if (character == '"') {
                inString = true;
                continue;
            }
            if (character == '{') {
                ++depth;
            } else if (character == '}') {
                --depth;
                if (depth == 0) {
                    objectEnd = scanIndex;
                    break;
                }
            }
        }

        if (objectEnd == std::string::npos) {
            break;
        }

        objects.push_back(arrayPayload.substr(index, objectEnd - index + 1));
        index = objectEnd + 1;
    }
    return objects;
}

bool IsTrustedHttpsUrl(const std::string& value) {
    return value.rfind("https://", 0) == 0;
}

bool OptionalTrustedHttpsUrl(const std::string& value) {
    return value.empty() || IsTrustedHttpsUrl(value);
}

bool AllTrustedHttpsUrls(const std::vector<std::string>& values) {
    return std::all_of(
        values.begin(),
        values.end(),
        [](const auto& value) { return IsTrustedHttpsUrl(value); });
}

bool HasAnyDynamicVatGlassesUrl(const MapDataManifest& manifest) {
    return !manifest.vatglassesDynamicBaseUrl.empty() ||
           !manifest.vatglassesPositionsUrl.empty() ||
           !manifest.vatglassesAirspaceUrl.empty() ||
           !manifest.vatglassesDynamicOwnershipUrl.empty();
}

bool HasCompleteExplicitDynamicVatGlassesUrls(const MapDataManifest& manifest) {
    return !manifest.vatglassesPositionsUrl.empty() &&
           !manifest.vatglassesAirspaceUrl.empty() &&
           !manifest.vatglassesDynamicOwnershipUrl.empty();
}

bool IsSafeRelativeJsonPath(const std::string& value) {
    if (value.empty()) {
        return true;
    }
    if (value.find("://") != std::string::npos ||
        value.find("..") != std::string::npos ||
        value.front() == '/' ||
        value.front() == '\\') {
        return false;
    }
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) != 0 ||
            character == '_' ||
            character == '-' ||
            character == '.' ||
            character == '/') {
            continue;
        }
        return false;
    }
    return value.size() >= 5 && value.rfind(".json") == value.size() - 5;
}

bool DynamicVatGlassesSourceIsValid(const MapDataManifest& manifest) {
    if (!HasAnyDynamicVatGlassesUrl(manifest)) {
        return true;
    }

    const auto hasTrustedBase = OptionalTrustedHttpsUrl(manifest.vatglassesDynamicBaseUrl);
    const auto hasTrustedExplicitUrls =
        OptionalTrustedHttpsUrl(manifest.vatglassesPositionsUrl) &&
        OptionalTrustedHttpsUrl(manifest.vatglassesAirspaceUrl) &&
        OptionalTrustedHttpsUrl(manifest.vatglassesDynamicOwnershipUrl);
    const auto hasCompleteDynamicSource =
        !manifest.vatglassesDynamicBaseUrl.empty() ||
        HasCompleteExplicitDynamicVatGlassesUrls(manifest);

    return hasTrustedBase &&
           hasTrustedExplicitUrls &&
           hasCompleteDynamicSource &&
           IsSafeRelativeJsonPath(manifest.vatglassesDynamicOwnershipFile);
}

std::string TrimJsonPayload(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool LooksLikeJsonObject(const std::string& payload) {
    const auto trimmed = TrimJsonPayload(payload);
    return trimmed.size() >= 2 && trimmed.front() == '{' && trimmed.back() == '}';
}

std::string NormalizeSourceToken(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const auto character : value) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0) {
            normalized.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(character))));
        } else if (!normalized.empty() && normalized.back() != '_') {
            normalized.push_back('_');
        }
    }
    while (!normalized.empty() && normalized.back() == '_') {
        normalized.pop_back();
    }
    return normalized;
}

std::string NormalizeRegistrySource(std::string source) {
    source = NormalizeSourceToken(std::move(source));
    if (source == "VATGLASSES" ||
        source == "VATGLASSES_STATIC_OWNER" ||
        source == "VATGLASSES_DYNAMIC_OWNER") {
        return "VATGLASSES";
    }
    if (source == "VATGLASSES_DYNAMIC_DIRECTORY" ||
        source == "VATGLASSES_DIRECTORY" ||
        source == "DYNAMIC_DIRECTORY") {
        return "VATGLASSES_DYNAMIC_DIRECTORY";
    }
    if (source == "SPECIAL" ||
        source == "SPECIAL_SECTOR" ||
        source == "SPECIAL_SECTORS" ||
        source == "SECTOR_DATA" ||
        source == "SPECIAL_SECTOR_DATA") {
        return "SPECIAL_SECTOR_DATA";
    }
    if (source == "SIMAWARE" ||
        source == "TRACON" ||
        source == "TERMINAL" ||
        source == "TERMINAL_AUTHORITY" ||
        source == "TERMINAL_AUTHORITY_DATA" ||
        source == "SIMAWARE_TRACON") {
        return "TERMINAL_AUTHORITY";
    }
    return {};
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
    manifest.vatglassesDynamicBaseUrl =
        ExtractJsonStringField(payload, "vatglasses_dynamic_base_url").value_or("");
    manifest.vatglassesPositionsUrl =
        ExtractJsonStringField(payload, "vatglasses_positions_url").value_or("");
    manifest.vatglassesAirspaceUrl =
        ExtractJsonStringField(payload, "vatglasses_airspace_url").value_or("");
    manifest.vatglassesDynamicOwnershipUrl =
        ExtractJsonStringField(payload, "vatglasses_dynamic_ownership_url").value_or("");
    manifest.vatglassesDynamicOwnershipFile =
        ExtractJsonStringField(payload, "vatglasses_dynamic_ownership_file").value_or("");
    manifest.specialSectorDataUrl =
        ExtractJsonStringField(payload, "special_sector_data_url").value_or("");
    manifest.specialSectorDataUrls =
        ExtractJsonStringArrayField(payload, "special_sector_data_urls");
    manifest.terminalAuthorityDataUrl =
        ExtractJsonStringField(payload, "terminal_authority_data_url").value_or("");
    manifest.terminalAuthorityDataUrls =
        ExtractJsonStringArrayField(payload, "terminal_authority_data_urls");
    manifest.authoritySourceRegistryUrl =
        ExtractJsonStringField(payload, "authority_source_registry_url").value_or("");
    manifest.authoritySourceRegistryUrls =
        ExtractJsonStringArrayField(payload, "authority_source_registry_urls");

    manifest.valid =
        !manifest.currentCommitHash.empty() &&
        IsTrustedHttpsUrl(manifest.firBoundariesDatUrl) &&
        IsTrustedHttpsUrl(manifest.firBoundariesGeoJsonUrl) &&
        IsTrustedHttpsUrl(manifest.vatspyDatUrl) &&
        IsTrustedHttpsUrl(manifest.simawareTraconGeoJsonUrl) &&
        OptionalTrustedHttpsUrl(manifest.vatglassesOwnershipUrl) &&
        OptionalTrustedHttpsUrl(manifest.specialSectorDataUrl) &&
        AllTrustedHttpsUrls(manifest.specialSectorDataUrls) &&
        OptionalTrustedHttpsUrl(manifest.terminalAuthorityDataUrl) &&
        AllTrustedHttpsUrls(manifest.terminalAuthorityDataUrls) &&
        OptionalTrustedHttpsUrl(manifest.authoritySourceRegistryUrl) &&
        AllTrustedHttpsUrls(manifest.authoritySourceRegistryUrls) &&
        DynamicVatGlassesSourceIsValid(manifest);
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

std::string BuildVatGlassesDynamicSourcePayload(
    const std::string& positionsJson,
    const std::string& airspaceJson,
    const std::string& ownershipJson) {
    const auto positionsPayload = TrimJsonPayload(positionsJson);
    const auto airspacePayload = TrimJsonPayload(airspaceJson);
    const auto ownershipPayload = TrimJsonPayload(ownershipJson);
    if (!LooksLikeJsonObject(positionsPayload) ||
        !LooksLikeJsonObject(airspacePayload) ||
        !LooksLikeJsonObject(ownershipPayload)) {
        return {};
    }

    return std::string("{\"positions\":") + positionsPayload +
           ",\"airspace\":" + airspacePayload +
           ",\"ownership\":" + ownershipPayload +
           "}";
}

std::string BuildAuthoritySourcePackagePayload(
    const std::string& primarySourceJson,
    const std::string& supplementalSourceJson) {
    return BuildAuthoritySourcePackagePayload(
        primarySourceJson,
        std::vector<std::string>{supplementalSourceJson});
}

std::string BuildAuthoritySourcePackagePayload(
    const std::string& primarySourceJson,
    const std::vector<std::string>& supplementalSourceJsons) {
    std::vector<std::string> packages;
    const auto primaryPayload = TrimJsonPayload(primarySourceJson);
    if (LooksLikeJsonObject(primaryPayload)) {
        packages.push_back(primaryPayload);
    }

    for (const auto& supplementalSourceJson : supplementalSourceJsons) {
        const auto supplementalPayload = TrimJsonPayload(supplementalSourceJson);
        if (LooksLikeJsonObject(supplementalPayload)) {
            packages.push_back(supplementalPayload);
        }
    }

    if (packages.empty()) {
        return {};
    }
    if (packages.size() == 1) {
        return packages.front();
    }

    std::string payload = "{\"source_packages\":[";
    for (std::size_t index = 0; index < packages.size(); ++index) {
        if (index > 0) {
            payload += ",";
        }
        payload += packages[index];
    }
    payload += "]}";

    return payload;
}

std::vector<AuthoritySourceRegistryEntry> ParseAuthoritySourceRegistryJson(
    const std::string& payload) {
    std::vector<AuthoritySourceRegistryEntry> entries;
    if (payload.empty()) {
        return entries;
    }

    auto arrayPayload = ExtractJsonArrayPayload(payload, "authority_sources");
    if (!arrayPayload.has_value()) {
        arrayPayload = ExtractJsonArrayPayload(payload, "source_registry");
    }
    if (!arrayPayload.has_value()) {
        arrayPayload = ExtractJsonArrayPayload(payload, "sources");
    }
    if (!arrayPayload.has_value()) {
        return entries;
    }

    for (const auto& objectPayload : ExtractJsonObjectsFromArrayPayload(*arrayPayload)) {
        auto source = NormalizeRegistrySource(
            ExtractJsonStringField(objectPayload, "source").value_or(""));
        auto url = ExtractJsonStringField(objectPayload, "url").value_or("");
        if (url.empty()) {
            url = ExtractJsonStringField(objectPayload, "data_url").value_or("");
        }
        if (url.empty()) {
            url = ExtractJsonStringField(objectPayload, "source_url").value_or("");
        }
        auto positionsUrl =
            ExtractJsonStringField(objectPayload, "positions_url").value_or("");
        auto airspaceUrl =
            ExtractJsonStringField(objectPayload, "airspace_url").value_or("");
        auto ownershipUrl =
            ExtractJsonStringField(objectPayload, "ownership_url").value_or("");

        const auto hasStaticUrl =
            source != "VATGLASSES_DYNAMIC_DIRECTORY" &&
            IsTrustedHttpsUrl(url);
        const auto hasDynamicUrls =
            source == "VATGLASSES_DYNAMIC_DIRECTORY" &&
            IsTrustedHttpsUrl(positionsUrl) &&
            IsTrustedHttpsUrl(airspaceUrl) &&
            IsTrustedHttpsUrl(ownershipUrl);
        if (source.empty() || (!hasStaticUrl && !hasDynamicUrls)) {
            continue;
        }

        AuthoritySourceRegistryEntry entry;
        entry.source = std::move(source);
        entry.url = std::move(url);
        entry.positionsUrl = std::move(positionsUrl);
        entry.airspaceUrl = std::move(airspaceUrl);
        entry.ownershipUrl = std::move(ownershipUrl);
        entry.regions = ExtractJsonStringArrayField(objectPayload, "regions");
        if (entry.regions.empty()) {
            entry.regions = ExtractJsonStringArrayField(objectPayload, "route_keys");
        }
        if (entry.regions.empty()) {
            entry.regions = ExtractJsonStringArrayField(objectPayload, "firs");
        }
        entries.push_back(std::move(entry));
    }

    std::sort(
        entries.begin(),
        entries.end(),
        [](const auto& left, const auto& right) {
            if (left.source != right.source) {
                return left.source < right.source;
            }
            return left.url < right.url;
        });
    entries.erase(
        std::unique(
            entries.begin(),
            entries.end(),
            [](const auto& left, const auto& right) {
                return left.source == right.source &&
                       left.url == right.url &&
                       left.positionsUrl == right.positionsUrl &&
                       left.airspaceUrl == right.airspaceUrl &&
                       left.ownershipUrl == right.ownershipUrl;
            }),
        entries.end());
    return entries;
}

}  // namespace xvatsim::core::source_data
