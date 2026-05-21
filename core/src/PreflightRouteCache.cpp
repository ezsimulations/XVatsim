#include "XVatsim/core/PreflightRouteCache.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>

namespace xvatsim::core::preflight {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

std::vector<std::string> SplitWhitespace(const std::string& input) {
    std::stringstream stream(input);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

std::optional<double> ParseDouble(const std::string& value) {
    try {
        std::size_t parsedChars = 0;
        const auto parsed = std::stod(value, &parsedChars);
        if (parsedChars != value.size()) {
            return std::nullopt;
        }
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<int> ParseInt(const std::string& value) {
    try {
        std::size_t parsedChars = 0;
        const auto parsed = std::stoi(value, &parsedChars);
        if (parsedChars != value.size()) {
            return std::nullopt;
        }
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

std::string HashBytesHex(std::string_view text) {
    std::uint64_t hash = kFnvOffset;
    for (const auto ch : text) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= kFnvPrime;
    }

    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << hash;
    return stream.str();
}

std::string BuildRouteIdentityHash(const FmsPlan& plan) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(6);
    stream << ToUpperCopy(plan.departureIcao) << "|"
           << ToUpperCopy(plan.destinationIcao) << "|"
           << ToUpperCopy(plan.cycle);
    for (const auto& waypoint : plan.waypoints) {
        stream << "|"
               << ToUpperCopy(waypoint.ident) << "@"
               << waypoint.latitudeDeg << ","
               << waypoint.longitudeDeg;
    }
    return HashBytesHex(stream.str());
}

bool IsKnownMetadataKey(const std::string& key) {
    static const std::vector<std::string> kKeys = {
        "DEPRWY",
        "SID",
        "SIDTRANS",
        "DESRWY",
        "STAR",
        "STARTRANS",
        "APP",
        "APPTRANS",
        "APPCH",
        "APPCHTRANS",
        "RTE",
    };
    return std::find(kKeys.begin(), kKeys.end(), key) != kKeys.end();
}

bool LooksLikeWaypointRow(const std::vector<std::string>& tokens) {
    return tokens.size() >= 6 && ParseInt(tokens[0]).has_value() &&
           ParseDouble(tokens[tokens.size() - 1]).has_value() &&
           ParseDouble(tokens[tokens.size() - 2]).has_value();
}

bool ParseWaypointRow(const std::vector<std::string>& tokens, FmsWaypoint* outWaypoint) {
    if (outWaypoint == nullptr || !LooksLikeWaypointRow(tokens)) {
        return false;
    }

    const auto type = ParseInt(tokens[0]);
    const auto altitude = ParseDouble(tokens[tokens.size() - 3]);
    const auto latitude = ParseDouble(tokens[tokens.size() - 2]);
    const auto longitude = ParseDouble(tokens[tokens.size() - 1]);
    if (!type.has_value() || !altitude.has_value() ||
        !latitude.has_value() || !longitude.has_value()) {
        return false;
    }

    FmsWaypoint waypoint;
    waypoint.type = *type;
    waypoint.ident = ToUpperCopy(tokens[1]);
    waypoint.via = tokens.size() >= 6 ? ToUpperCopy(tokens[2]) : "DRCT";
    waypoint.altitudeFt = *altitude;
    waypoint.latitudeDeg = *latitude;
    waypoint.longitudeDeg = *longitude;
    *outWaypoint = std::move(waypoint);
    return true;
}

bool HasRequiredGeometry(const FmsPlan& plan, std::string* outError) {
    if (plan.departureIcao.empty()) {
        if (outError != nullptr) {
            *outError = "The selected FMS file does not include a departure airport.";
        }
        return false;
    }
    if (plan.destinationIcao.empty()) {
        if (outError != nullptr) {
            *outError = "The selected FMS file does not include a destination airport.";
        }
        return false;
    }
    if (plan.waypoints.size() < 2) {
        if (outError != nullptr) {
            *outError = "The selected FMS file does not include enough route waypoints.";
        }
        return false;
    }

    for (const auto& waypoint : plan.waypoints) {
        if (waypoint.ident.empty() ||
            !std::isfinite(waypoint.latitudeDeg) ||
            !std::isfinite(waypoint.longitudeDeg) ||
            waypoint.latitudeDeg < -90.0 ||
            waypoint.latitudeDeg > 90.0 ||
            waypoint.longitudeDeg < -180.0 ||
            waypoint.longitudeDeg > 180.0) {
            if (outError != nullptr) {
                *outError = "The selected FMS file has invalid waypoint geometry.";
            }
            return false;
        }
    }
    return true;
}

std::string JsonEscape(const std::string& value) {
    std::ostringstream stream;
    for (const auto ch : value) {
        switch (ch) {
            case '\\':
                stream << "\\\\";
                break;
            case '"':
                stream << "\\\"";
                break;
            case '\n':
                stream << "\\n";
                break;
            case '\r':
                stream << "\\r";
                break;
            case '\t':
                stream << "\\t";
                break;
            default:
                stream << ch;
                break;
        }
    }
    return stream.str();
}

std::string JsonString(const std::string& value) {
    return "\"" + JsonEscape(value) + "\"";
}

std::optional<std::string> ExtractJsonString(
    const std::string& json,
    const std::string& key) {
    const auto marker = "\"" + key + "\"";
    const auto keyIndex = json.find(marker);
    if (keyIndex == std::string::npos) {
        return std::nullopt;
    }
    const auto colonIndex = json.find(':', keyIndex + marker.size());
    if (colonIndex == std::string::npos) {
        return std::nullopt;
    }
    auto valueIndex = json.find('"', colonIndex + 1);
    if (valueIndex == std::string::npos) {
        return std::nullopt;
    }
    ++valueIndex;

    std::string value;
    bool escaped = false;
    for (std::size_t index = valueIndex; index < json.size(); ++index) {
        const auto ch = json[index];
        if (escaped) {
            switch (ch) {
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                default:
                    value.push_back(ch);
                    break;
            }
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            return value;
        }
        value.push_back(ch);
    }
    return std::nullopt;
}

std::optional<long long> ExtractJsonInteger(
    const std::string& json,
    const std::string& key) {
    const auto marker = "\"" + key + "\"";
    const auto keyIndex = json.find(marker);
    if (keyIndex == std::string::npos) {
        return std::nullopt;
    }
    const auto colonIndex = json.find(':', keyIndex + marker.size());
    if (colonIndex == std::string::npos) {
        return std::nullopt;
    }
    auto valueIndex = colonIndex + 1;
    while (valueIndex < json.size() &&
           std::isspace(static_cast<unsigned char>(json[valueIndex])) != 0) {
        ++valueIndex;
    }
    auto endIndex = valueIndex;
    while (endIndex < json.size() &&
           (std::isdigit(static_cast<unsigned char>(json[endIndex])) != 0 ||
            json[endIndex] == '-')) {
        ++endIndex;
    }
    if (endIndex == valueIndex) {
        return std::nullopt;
    }
    try {
        return std::stoll(json.substr(valueIndex, endIndex - valueIndex));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<double> ExtractJsonDouble(
    const std::string& json,
    const std::string& key) {
    const auto marker = "\"" + key + "\"";
    const auto keyIndex = json.find(marker);
    if (keyIndex == std::string::npos) {
        return std::nullopt;
    }
    const auto colonIndex = json.find(':', keyIndex + marker.size());
    if (colonIndex == std::string::npos) {
        return std::nullopt;
    }
    auto valueIndex = colonIndex + 1;
    while (valueIndex < json.size() &&
           std::isspace(static_cast<unsigned char>(json[valueIndex])) != 0) {
        ++valueIndex;
    }
    auto endIndex = valueIndex;
    while (endIndex < json.size() &&
           (std::isdigit(static_cast<unsigned char>(json[endIndex])) != 0 ||
            json[endIndex] == '-' ||
            json[endIndex] == '+' ||
            json[endIndex] == '.' ||
            json[endIndex] == 'e' ||
            json[endIndex] == 'E')) {
        ++endIndex;
    }
    if (endIndex == valueIndex) {
        return std::nullopt;
    }
    return ParseDouble(json.substr(valueIndex, endIndex - valueIndex));
}

std::vector<FmsWaypoint> ExtractWaypointArray(const std::string& json) {
    std::vector<FmsWaypoint> waypoints;
    const std::string marker = "\"waypoints\"";
    const auto keyIndex = json.find(marker);
    if (keyIndex == std::string::npos) {
        return waypoints;
    }
    const auto arrayStart = json.find('[', keyIndex + marker.size());
    if (arrayStart == std::string::npos) {
        return waypoints;
    }
    const auto arrayEnd = json.find(']', arrayStart + 1);
    if (arrayEnd == std::string::npos || arrayEnd <= arrayStart) {
        return waypoints;
    }

    std::size_t cursor = arrayStart + 1;
    while (cursor < arrayEnd) {
        const auto objectStart = json.find('{', cursor);
        if (objectStart == std::string::npos || objectStart >= arrayEnd) {
            break;
        }
        const auto objectEnd = json.find('}', objectStart + 1);
        if (objectEnd == std::string::npos || objectEnd > arrayEnd) {
            break;
        }
        const auto objectJson = json.substr(objectStart, objectEnd - objectStart + 1);
        FmsWaypoint waypoint;
        waypoint.type = static_cast<int>(ExtractJsonInteger(objectJson, "type").value_or(0));
        waypoint.ident = ExtractJsonString(objectJson, "ident").value_or({});
        waypoint.via = ExtractJsonString(objectJson, "via").value_or({});
        waypoint.altitudeFt = ExtractJsonDouble(objectJson, "altitude_ft").value_or(0.0);
        waypoint.latitudeDeg = ExtractJsonDouble(objectJson, "lat").value_or(0.0);
        waypoint.longitudeDeg = ExtractJsonDouble(objectJson, "lon").value_or(0.0);
        if (!waypoint.ident.empty()) {
            waypoints.push_back(std::move(waypoint));
        }
        cursor = objectEnd + 1;
    }
    return waypoints;
}

std::unordered_map<std::string, std::string> ExtractMetadataObject(const std::string& json) {
    std::unordered_map<std::string, std::string> metadata;
    const std::string marker = "\"metadata\"";
    const auto keyIndex = json.find(marker);
    if (keyIndex == std::string::npos) {
        return metadata;
    }
    const auto objectStart = json.find('{', keyIndex + marker.size());
    if (objectStart == std::string::npos) {
        return metadata;
    }
    const auto objectEnd = json.find('}', objectStart + 1);
    if (objectEnd == std::string::npos || objectEnd <= objectStart) {
        return metadata;
    }
    const auto objectJson = json.substr(objectStart + 1, objectEnd - objectStart - 1);
    std::size_t cursor = 0;
    while (cursor < objectJson.size()) {
        const auto keyStart = objectJson.find('"', cursor);
        if (keyStart == std::string::npos) {
            break;
        }
        const auto keyEnd = objectJson.find('"', keyStart + 1);
        if (keyEnd == std::string::npos) {
            break;
        }
        const auto key = objectJson.substr(keyStart + 1, keyEnd - keyStart - 1);
        const auto colon = objectJson.find(':', keyEnd + 1);
        if (colon == std::string::npos) {
            break;
        }
        const auto valueStart = objectJson.find('"', colon + 1);
        if (valueStart == std::string::npos) {
            break;
        }
        const auto valueEnd = objectJson.find('"', valueStart + 1);
        if (valueEnd == std::string::npos) {
            break;
        }
        metadata[key] = objectJson.substr(valueStart + 1, valueEnd - valueStart - 1);
        cursor = valueEnd + 1;
    }
    return metadata;
}

std::string BuildFileReadError(const std::filesystem::path& path) {
    return "Unable to read " + path.string() + ".";
}

}  // namespace

std::string Trim(std::string value) {
    const auto notSpace = [](unsigned char ch) {
        return std::isspace(ch) == 0;
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string ToUpperCopy(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
    return value;
}

std::string ComputeContentHashHex(const std::string& content) {
    return HashBytesHex(content);
}

long long GetFileModifiedUnixSeconds(const std::filesystem::path& path) {
    std::error_code ec;
    const auto modified = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return 0;
    }
    const auto systemTime =
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            modified - decltype(modified)::clock::now() + std::chrono::system_clock::now());
    return static_cast<long long>(std::chrono::system_clock::to_time_t(systemTime));
}

FmsParseResult ParseFmsPlanText(
    const std::string& content,
    const std::filesystem::path& sourcePath,
    long long modifiedUnixSeconds,
    std::uintmax_t sourceSizeBytes) {
    FmsParseResult result;
    result.plan.sourcePath = sourcePath.string();
    result.plan.sourceModifiedUnixSeconds = modifiedUnixSeconds;
    result.plan.sourceSizeBytes = sourceSizeBytes;
    result.plan.sourceContentHash = ComputeContentHashHex(content);

    std::stringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        line = Trim(line);
        if (line.empty()) {
            continue;
        }

        const auto tokens = SplitWhitespace(line);
        if (tokens.empty()) {
            continue;
        }

        const auto key = ToUpperCopy(tokens[0]);
        if (key == "CYCLE" && tokens.size() >= 2) {
            result.plan.cycle = tokens[1];
            continue;
        }
        if (key == "ADEP" && tokens.size() >= 2) {
            result.plan.departureIcao = ToUpperCopy(tokens[1]);
            continue;
        }
        if (key == "ADES" && tokens.size() >= 2) {
            result.plan.destinationIcao = ToUpperCopy(tokens[1]);
            continue;
        }
        if (IsKnownMetadataKey(key) && tokens.size() >= 2) {
            result.plan.metadata[key] = ToUpperCopy(tokens[1]);
            continue;
        }

        FmsWaypoint waypoint;
        if (ParseWaypointRow(tokens, &waypoint)) {
            result.plan.waypoints.push_back(std::move(waypoint));
        }
    }

    std::string validationError;
    if (!HasRequiredGeometry(result.plan, &validationError)) {
        result.ok = false;
        result.message = validationError;
        return result;
    }

    if (result.plan.cycle.empty()) {
        result.plan.cycle = "UNKNOWN";
    }
    result.plan.routeIdentityHash = BuildRouteIdentityHash(result.plan);
    result.ok = true;
    result.message = "FMS route parsed.";
    return result;
}

FmsParseResult LoadFmsPlanFile(const std::filesystem::path& sourcePath) {
    std::ifstream file(sourcePath, std::ios::binary);
    if (!file) {
        FmsParseResult result;
        result.ok = false;
        result.message = BuildFileReadError(sourcePath);
        return result;
    }

    const std::string content{
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()};
    std::error_code ec;
    const auto size = std::filesystem::file_size(sourcePath, ec);
    return ParseFmsPlanText(
        content,
        sourcePath,
        GetFileModifiedUnixSeconds(sourcePath),
        ec ? 0 : size);
}

PreflightRouteCache BuildPreflightRouteCache(const FmsPlan& plan) {
    PreflightRouteCache cache;
    cache.createdUnixSeconds = static_cast<long long>(std::time(nullptr));
    cache.plan = plan;
    cache.airacIdentity = plan.cycle;
    return cache;
}

std::string SerializePreflightRouteCacheJson(const PreflightRouteCache& cache) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(6);
    stream << "{\n";
    stream << "  \"schema_version\": " << cache.schemaVersion << ",\n";
    stream << "  \"cache_builder_version\": " << JsonString(cache.builderVersion) << ",\n";
    stream << "  \"xvatsim_compatibility_version\": "
           << JsonString(cache.compatibilityVersion) << ",\n";
    stream << "  \"created_unix\": " << cache.createdUnixSeconds << ",\n";
    stream << "  \"selected_fms_path\": " << JsonString(cache.plan.sourcePath) << ",\n";
    stream << "  \"selected_fms_modified_unix\": "
           << cache.plan.sourceModifiedUnixSeconds << ",\n";
    stream << "  \"selected_fms_size\": " << cache.plan.sourceSizeBytes << ",\n";
    stream << "  \"selected_fms_hash\": "
           << JsonString(cache.plan.sourceContentHash) << ",\n";
    stream << "  \"cycle\": " << JsonString(cache.plan.cycle) << ",\n";
    stream << "  \"departure_icao\": "
           << JsonString(ToUpperCopy(cache.plan.departureIcao)) << ",\n";
    stream << "  \"destination_icao\": "
           << JsonString(ToUpperCopy(cache.plan.destinationIcao)) << ",\n";
    stream << "  \"route_identity_hash\": "
           << JsonString(cache.plan.routeIdentityHash) << ",\n";
    stream << "  \"authority_source_registry_hash\": "
           << JsonString(cache.authoritySourceRegistryHash) << ",\n";
    stream << "  \"boundary_data_hash\": " << JsonString(cache.boundaryDataHash) << ",\n";
    stream << "  \"airac_identity\": " << JsonString(cache.airacIdentity) << ",\n";
    stream << "  \"metadata\": {";
    bool firstMetadata = true;
    for (const auto& [key, value] : cache.plan.metadata) {
        if (!firstMetadata) {
            stream << ",";
        }
        firstMetadata = false;
        stream << "\n    " << JsonString(key) << ": " << JsonString(value);
    }
    if (!firstMetadata) {
        stream << "\n  ";
    }
    stream << "},\n";
    stream << "  \"waypoints\": [\n";
    for (std::size_t index = 0; index < cache.plan.waypoints.size(); ++index) {
        const auto& waypoint = cache.plan.waypoints[index];
        stream << "    {"
               << "\"type\": " << waypoint.type << ", "
               << "\"ident\": " << JsonString(ToUpperCopy(waypoint.ident)) << ", "
               << "\"via\": " << JsonString(ToUpperCopy(waypoint.via)) << ", "
               << "\"altitude_ft\": " << waypoint.altitudeFt << ", "
               << "\"lat\": " << waypoint.latitudeDeg << ", "
               << "\"lon\": " << waypoint.longitudeDeg << "}";
        if (index + 1 < cache.plan.waypoints.size()) {
            stream << ",";
        }
        stream << "\n";
    }
    stream << "  ],\n";
    stream << "  \"route_authority_polygon_ids\": [";
    for (std::size_t index = 0; index < cache.routeAuthorityPolygonIds.size(); ++index) {
        if (index > 0) {
            stream << ", ";
        }
        stream << JsonString(cache.routeAuthorityPolygonIds[index]);
    }
    stream << "]\n";
    stream << "}\n";
    return stream.str();
}

bool WritePreflightRouteCacheFile(
    const PreflightRouteCache& cache,
    const std::filesystem::path& cachePath,
    std::string* outError) {
    std::error_code ec;
    std::filesystem::create_directories(cachePath.parent_path(), ec);
    if (ec) {
        if (outError != nullptr) {
            *outError = "Unable to create cache folder: " + cachePath.parent_path().string();
        }
        return false;
    }

    std::ofstream file(cachePath, std::ios::binary | std::ios::trunc);
    if (!file) {
        if (outError != nullptr) {
            *outError = "Unable to write preflight cache: " + cachePath.string();
        }
        return false;
    }

    file << SerializePreflightRouteCacheJson(cache);
    if (!file) {
        if (outError != nullptr) {
            *outError = "The preflight cache could not be written completely.";
        }
        return false;
    }
    return true;
}

bool LoadPreflightRouteCacheFile(
    const std::filesystem::path& cachePath,
    PreflightRouteCache* outCache,
    std::string* outError) {
    if (outCache == nullptr) {
        return false;
    }

    std::ifstream file(cachePath, std::ios::binary);
    if (!file) {
        if (outError != nullptr) {
            *outError = "Preflight route cache not found.";
        }
        return false;
    }
    const std::string json{
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()};

    PreflightRouteCache cache;
    cache.schemaVersion =
        static_cast<int>(ExtractJsonInteger(json, "schema_version").value_or(0));
    cache.builderVersion =
        ExtractJsonString(json, "cache_builder_version").value_or({});
    cache.compatibilityVersion =
        ExtractJsonString(json, "xvatsim_compatibility_version").value_or({});
    cache.createdUnixSeconds =
        ExtractJsonInteger(json, "created_unix").value_or(0);
    cache.plan.sourcePath =
        ExtractJsonString(json, "selected_fms_path").value_or({});
    cache.plan.sourceModifiedUnixSeconds =
        ExtractJsonInteger(json, "selected_fms_modified_unix").value_or(0);
    cache.plan.sourceSizeBytes =
        static_cast<std::uintmax_t>(ExtractJsonInteger(json, "selected_fms_size").value_or(0));
    cache.plan.sourceContentHash =
        ExtractJsonString(json, "selected_fms_hash").value_or({});
    cache.plan.cycle =
        ExtractJsonString(json, "cycle").value_or({});
    cache.plan.departureIcao =
        ToUpperCopy(ExtractJsonString(json, "departure_icao").value_or({}));
    cache.plan.destinationIcao =
        ToUpperCopy(ExtractJsonString(json, "destination_icao").value_or({}));
    cache.plan.routeIdentityHash =
        ExtractJsonString(json, "route_identity_hash").value_or({});
    cache.authoritySourceRegistryHash =
        ExtractJsonString(json, "authority_source_registry_hash").value_or({});
    cache.boundaryDataHash =
        ExtractJsonString(json, "boundary_data_hash").value_or({});
    cache.airacIdentity =
        ExtractJsonString(json, "airac_identity").value_or({});
    cache.plan.metadata = ExtractMetadataObject(json);
    cache.plan.waypoints = ExtractWaypointArray(json);

    std::string validationError;
    if (cache.schemaVersion == 0 ||
        cache.plan.routeIdentityHash.empty() ||
        !HasRequiredGeometry(cache.plan, &validationError)) {
        if (outError != nullptr) {
            *outError = validationError.empty()
                            ? "Preflight route cache is incomplete or invalid."
                            : validationError;
        }
        return false;
    }

    *outCache = std::move(cache);
    return true;
}

CacheValidationResult ValidatePreflightRouteCacheForNetworkPlan(
    const PreflightRouteCache& cache,
    const brain::NetworkPlanSnapshot& networkPlanSnapshot,
    bool verifySourceFile) {
    CacheValidationResult result;
    if (cache.schemaVersion != kPreflightRouteCacheSchemaVersion) {
        result.reason = "unsupported cache schema";
        return result;
    }
    if (cache.compatibilityVersion != kPreflightCompatibilityVersion) {
        result.reason = "unsupported cache compatibility version";
        return result;
    }
    if (!networkPlanSnapshot.matched || networkPlanSnapshot.stale) {
        result.reason = "live VATSIM plan is not ready";
        return result;
    }
    if (ToUpperCopy(cache.plan.departureIcao) !=
        ToUpperCopy(networkPlanSnapshot.departureIcao)) {
        result.reason = "departure does not match live VATSIM plan";
        return result;
    }
    if (ToUpperCopy(cache.plan.destinationIcao) !=
        ToUpperCopy(networkPlanSnapshot.destinationIcao)) {
        result.reason = "destination does not match live VATSIM plan";
        return result;
    }

    std::string validationError;
    if (!HasRequiredGeometry(cache.plan, &validationError)) {
        result.reason = validationError;
        return result;
    }

    if (verifySourceFile) {
        const std::filesystem::path sourcePath(cache.plan.sourcePath);
        std::error_code ec;
        if (sourcePath.empty() || !std::filesystem::exists(sourcePath, ec)) {
            result.reason = "selected FMS file is missing";
            return result;
        }
        const auto currentModified = GetFileModifiedUnixSeconds(sourcePath);
        const auto currentSize = std::filesystem::file_size(sourcePath, ec);
        bool sourceNeedsRouteIdentityRevalidation =
            ec || currentModified != cache.plan.sourceModifiedUnixSeconds ||
            currentSize != cache.plan.sourceSizeBytes;

        if (!sourceNeedsRouteIdentityRevalidation) {
            std::ifstream file(sourcePath, std::ios::binary);
            if (!file) {
                result.reason = "selected FMS file cannot be read";
                return result;
            }
            const std::string content{
                std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>()};
            sourceNeedsRouteIdentityRevalidation =
                ComputeContentHashHex(content) != cache.plan.sourceContentHash;
        }

        if (sourceNeedsRouteIdentityRevalidation) {
            const auto currentPlan = LoadFmsPlanFile(sourcePath);
            if (!currentPlan.ok) {
                result.reason =
                    "selected FMS file changed and cannot be revalidated";
                return result;
            }
            if (ToUpperCopy(currentPlan.plan.departureIcao) !=
                    ToUpperCopy(cache.plan.departureIcao) ||
                ToUpperCopy(currentPlan.plan.destinationIcao) !=
                    ToUpperCopy(cache.plan.destinationIcao) ||
                currentPlan.plan.routeIdentityHash != cache.plan.routeIdentityHash) {
                result.reason =
                    "selected FMS route identity changed after cache build";
                return result;
            }

            result.accepted = true;
            result.reason =
                "selected FMS file changed but route identity still matches";
            return result;
        }
    }

    result.accepted = true;
    result.reason = "cache matches live VATSIM departure and destination";
    return result;
}

std::vector<brain::RouteWaypointSnapshot> BuildRouteWaypointsFromCache(
    const PreflightRouteCache& cache) {
    std::vector<brain::RouteWaypointSnapshot> waypoints;
    waypoints.reserve(cache.plan.waypoints.size());
    for (const auto& waypoint : cache.plan.waypoints) {
        brain::RouteWaypointSnapshot routeWaypoint;
        routeWaypoint.ident = ToUpperCopy(waypoint.ident);
        routeWaypoint.latitudeDeg = waypoint.latitudeDeg;
        routeWaypoint.longitudeDeg = waypoint.longitudeDeg;
        waypoints.push_back(std::move(routeWaypoint));
    }
    return waypoints;
}

std::filesystem::path BuildCachePathBesideExecutable(
    const std::filesystem::path& executablePath) {
    return executablePath.parent_path() / kPreflightRouteCacheFileName;
}

}  // namespace xvatsim::core::preflight
