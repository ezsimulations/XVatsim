#include "XVatsim/modules/route_sector/RouteSectorResolver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <windows.h>
#include <winhttp.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

#include "XVatsim/core/MapDataSource.h"
#include "XVatsim/core/RouteGrammar.h"
#include "XVatsim/core/RouteTraversal.h"

namespace xvatsim::modules::route_sector {

namespace {

constexpr wchar_t kUserAgent[] = L"XVatsim/1.0.0";
constexpr wchar_t kVatsimMapDataManifestUrl[] =
    L"https://api.vatsim.net/api/map_data";
constexpr wchar_t kTraconBoundaryUrl[] =
    L"https://github.com/vatsimnetwork/simaware-tracon-project/releases/latest/download/TRACONBoundaries.geojson";
constexpr long long kRefreshCadenceSeconds = 21600;
constexpr long long kFailureBackoffSeconds = 600;
constexpr long long kInProgressCacheGraceSeconds = 300;
constexpr long long kAirborneSnapshotCadenceSeconds = 10;
constexpr long long kGroundSnapshotCadenceSeconds = 60;
constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusNm = 3440.065;
constexpr double kSnapshotMovementThresholdNm = 2.0;
constexpr std::size_t kAirportCoverageCacheLimit = 32;
constexpr std::size_t kRouteSectorSanityLimit = 30;
constexpr std::size_t kDiagnosticListLimit = 12;
constexpr std::size_t kDiagnosticTextLimit = 96;
constexpr std::size_t kDiagnosticRouteTextLimit = 512;
constexpr std::size_t kDiagnosticLogLineLimit = 4096;

struct WinHttpHandle {
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) : handle(handle) {}
    ~WinHttpHandle() {
        if (handle != nullptr) {
            WinHttpCloseHandle(handle);
        }
    }

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    HINTERNET handle = nullptr;
};

struct ParsedUrl {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    bool secure = true;
};

struct GeoPoint {
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
};

struct Vector3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct SectorPolygon {
    std::vector<GeoPoint> ring;
};

struct SectorFeature {
    std::string label;
    std::unordered_set<std::string> tokens;
    std::vector<SectorPolygon> polygons;
};

struct ControllerAuthorityCatalog {
    std::unordered_map<std::string, std::vector<std::string>> prefixesByKey;
    std::unordered_map<std::string, std::vector<std::string>> callsignPatternsByKey;
};

struct RouteResolveDiagnostics {
    std::vector<std::string> rawTokens;
    std::vector<std::string> resolvedTokens;
    std::vector<std::string> expandedTokens;
    std::vector<std::string> recognizedProcedureTokens;
    std::vector<std::string> procedureMetadataSources;
    std::vector<std::string> procedureRecordKinds;
    std::vector<std::string> procedureRunwayRecords;
    std::vector<std::string> procedureCatalogAuthorities;
    std::vector<std::string> procedureCatalogFixes;
    std::vector<std::string> procedureBoundaryFixes;
    std::vector<std::string> procedureOrderedFixes;
    std::vector<std::string> procedureSyntheticWaypoints;
    std::vector<std::string> procedureSyntheticSources;
    std::vector<std::string> procedureApplicationStates;
    std::vector<std::string> procedureApplicationBlocks;
    std::vector<std::string> procedureAppliedFixSequences;
    std::vector<std::string> procedureCatalogTransitions;
    std::vector<std::string> procedureSupportDirections;
    std::vector<std::string> procedureTransitionLinks;
    std::vector<std::string> procedureTransitionMisses;
    std::vector<std::string> procedureAnchorLinks;
    std::vector<std::string> procedureContextOnlyTokens;
    std::vector<std::string> ignoredTokens;
    std::vector<std::string> unsupportedTokens;
    std::vector<std::string> unresolvedTokens;
};

bool TokenCanActAsPoint(const xvatsim::core::route::ParsedRouteToken& token);
bool TokenCanActAsAirway(const xvatsim::core::route::ParsedRouteToken& token);
std::optional<std::size_t> FindNextMeaningfulTokenIndex(
    const std::vector<xvatsim::core::route::ParsedRouteToken>& parsedTokens,
    std::size_t startIndex);
std::optional<std::size_t> FindNextAnchorTokenIndex(
    const std::vector<xvatsim::core::route::ParsedRouteToken>& parsedTokens,
    std::size_t startIndex,
    RouteResolveDiagnostics* diagnostics);

struct AirwayNode {
    std::string ident;
    std::string region;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    int navDataType = 0;
};

struct AirwayEdge {
    std::size_t toNodeIndex = 0;
    double distanceNm = 0.0;
};

struct AirwayGraph {
    std::vector<AirwayNode> nodes;
    std::unordered_map<std::string, std::vector<std::size_t>> nodeIndicesByIdent;
    std::unordered_map<std::string, std::size_t> nodeIndexByExactKey;
    std::unordered_map<std::string, std::unordered_map<std::size_t, std::vector<AirwayEdge>>>
        adjacencyByAirway;
};

struct ResolvedRoutePoint {
    std::string ident;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    std::optional<std::size_t> graphNodeIndex;
};

bool ResolveRoutePointTokenWithRouteContext(
    const std::string& token,
    const std::optional<GeoPoint>& referencePoint,
    const std::optional<std::string>& nextPointToken,
    const std::optional<GeoPoint>& destinationPoint,
    ResolvedRoutePoint* outPoint);

bool ResolveRoutePointTokenWithAirwayEntryContext(
    const std::string& token,
    const std::optional<GeoPoint>& referencePoint,
    const std::string& airwayToken,
    const std::string& airwayEndToken,
    ResolvedRoutePoint* outPoint);

long long CurrentTickSeconds() {
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::optional<ParsedUrl> ParseUrl(const std::wstring& url) {
    URL_COMPONENTS components = {};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
        return std::nullopt;
    }

    ParsedUrl parsed;
    parsed.host.assign(components.lpszHostName, components.dwHostNameLength);
    parsed.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0) {
        parsed.path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    parsed.port = components.nPort;
    parsed.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    if (parsed.host.empty() || parsed.path.empty()) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<std::wstring> QueryRedirectLocation(
    HINTERNET request,
    const std::wstring& currentUrl) {
    DWORD locationSizeBytes = 0;
    if (WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_LOCATION,
            WINHTTP_HEADER_NAME_BY_INDEX,
            WINHTTP_NO_OUTPUT_BUFFER,
            &locationSizeBytes,
            WINHTTP_NO_HEADER_INDEX)) {
        return std::nullopt;
    }

    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || locationSizeBytes == 0) {
        return std::nullopt;
    }

    std::wstring location(locationSizeBytes / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_LOCATION,
            WINHTTP_HEADER_NAME_BY_INDEX,
            location.data(),
            &locationSizeBytes,
            WINHTTP_NO_HEADER_INDEX)) {
        return std::nullopt;
    }

    location.resize(locationSizeBytes / sizeof(wchar_t));
    while (!location.empty() && location.back() == L'\0') {
        location.pop_back();
    }
    if (location.empty()) {
        return std::nullopt;
    }

    if (location.rfind(L"http://", 0) == 0 || location.rfind(L"https://", 0) == 0) {
        return location;
    }

    const auto parsedCurrent = ParseUrl(currentUrl);
    if (!parsedCurrent.has_value()) {
        return std::nullopt;
    }

    if (!location.empty() && location.front() == L'/') {
        return std::wstring(parsedCurrent->secure ? L"https://" : L"http://") +
               parsedCurrent->host +
               location;
    }

    return std::nullopt;
}

std::string DownloadHttpsPayload(const std::wstring& initialUrl) {
    std::wstring url = initialUrl;
    for (int redirectCount = 0; redirectCount < 6; ++redirectCount) {
        const auto parsed = ParseUrl(url);
        if (!parsed.has_value()) {
            return {};
        }

        WinHttpHandle session(WinHttpOpen(
            kUserAgent,
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0));
        if (session.handle == nullptr) {
            return {};
        }
        WinHttpSetTimeouts(session.handle, 5000, 5000, 10000, 15000);

        WinHttpHandle connection(WinHttpConnect(
            session.handle,
            parsed->host.c_str(),
            parsed->port,
            0));
        if (connection.handle == nullptr) {
            return {};
        }

        WinHttpHandle request(WinHttpOpenRequest(
            connection.handle,
            L"GET",
            parsed->path.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            parsed->secure ? WINHTTP_FLAG_SECURE : 0));
        if (request.handle == nullptr) {
            return {};
        }

        if (!WinHttpSendRequest(
                request.handle,
                WINHTTP_NO_ADDITIONAL_HEADERS,
                0,
                WINHTTP_NO_REQUEST_DATA,
                0,
                0,
                0)) {
            return {};
        }

        if (!WinHttpReceiveResponse(request.handle, nullptr)) {
            return {};
        }

        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        if (!WinHttpQueryHeaders(
                request.handle,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &statusCode,
                &statusCodeSize,
                WINHTTP_NO_HEADER_INDEX)) {
            return {};
        }

        if (statusCode == 301 || statusCode == 302 || statusCode == 303 ||
            statusCode == 307 || statusCode == 308) {
            const auto redirectUrl = QueryRedirectLocation(request.handle, url);
            if (!redirectUrl.has_value()) {
                return {};
            }
            url = *redirectUrl;
            continue;
        }

        if (statusCode != 200) {
            return {};
        }

        std::string payload;
        for (;;) {
            DWORD availableBytes = 0;
            if (!WinHttpQueryDataAvailable(request.handle, &availableBytes)) {
                return {};
            }

            if (availableBytes == 0) {
                break;
            }

            std::vector<char> buffer(availableBytes);
            DWORD downloadedBytes = 0;
            if (!WinHttpReadData(
                    request.handle,
                    buffer.data(),
                    availableBytes,
                    &downloadedBytes)) {
                return {};
            }

            payload.append(buffer.data(), downloadedBytes);
        }

        return payload;
    }

    return {};
}

std::wstring WidenUrl(const std::string& url) {
    return std::wstring(url.begin(), url.end());
}

xvatsim::core::source_data::MapDataManifest DownloadMapDataManifest() {
    const auto payload = DownloadHttpsPayload(kVatsimMapDataManifestUrl);
    const auto manifest =
        xvatsim::core::source_data::ParseMapDataManifestJson(payload);
    if (manifest.valid) {
        return manifest;
    }

    return xvatsim::core::source_data::BuildFallbackMapDataManifest();
}

std::string DownloadBoundaryPayload(
    const xvatsim::core::source_data::MapDataManifest& manifest) {
    if (!manifest.valid || manifest.firBoundariesGeoJsonUrl.empty()) {
        return {};
    }
    return DownloadHttpsPayload(WidenUrl(manifest.firBoundariesGeoJsonUrl));
}

std::string DownloadVatSpyDataPayload(
    const xvatsim::core::source_data::MapDataManifest& manifest) {
    if (!manifest.valid || manifest.vatspyDatUrl.empty()) {
        return {};
    }
    return DownloadHttpsPayload(WidenUrl(manifest.vatspyDatUrl));
}

std::string DownloadTerminalBoundaryPayload() {
    return DownloadHttpsPayload(kTraconBoundaryUrl);
}

void AddBoundaryIdentifierToken(
    const std::string& value,
    std::unordered_set<std::string>* tokens) {
    if (tokens == nullptr || value.empty()) {
        return;
    }

    std::string normalized;
    normalized.reserve(value.size());
    for (const auto character : value) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0 ||
            character == '_' || character == '-') {
            normalized.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(character))));
            continue;
        }
        if (!normalized.empty() && normalized.back() != '_') {
            normalized.push_back('_');
        }
    }

    while (!normalized.empty() && normalized.back() == '_') {
        normalized.pop_back();
    }
    if (!normalized.empty()) {
        tokens->insert(normalized);
    }
}

std::string ExtractLabel(
    const winrt::Windows::Data::Json::JsonObject& properties,
    std::unordered_set<std::string>* tokens) {
    using namespace winrt::Windows::Data::Json;

    // Center geometry identity comes from boundary identifiers only; controller
    // callsign authority is resolved from the VATSpy catalog.
    static constexpr const wchar_t* kIdentifierKeys[] = {
        L"identifier",
        L"icao",
        L"id"
    };

    std::string label;
    for (const auto key : kIdentifierKeys) {
        if (!properties.HasKey(key)) {
            continue;
        }
        const auto value = properties.GetNamedValue(key);
        if (value.ValueType() != JsonValueType::String) {
            continue;
        }
        const auto text = winrt::to_string(value.GetString());
        if (!text.empty() && label.empty()) {
            label = text;
        }
        AddBoundaryIdentifierToken(text, tokens);
    }

    if (label.empty() && properties.HasKey(L"name")) {
        const auto value = properties.GetNamedValue(L"name");
        if (value.ValueType() == JsonValueType::String) {
            label = winrt::to_string(value.GetString());
        }
    }

    if (label.empty()) {
        label = "SECTOR";
    }
    return label;
}

std::optional<GeoPoint> ParseCoordinatePair(
    const winrt::Windows::Data::Json::JsonArray& coordinates) {
    using namespace winrt::Windows::Data::Json;
    if (coordinates.Size() < 2) {
        return std::nullopt;
    }

    const auto longitude = coordinates.GetAt(0);
    const auto latitude = coordinates.GetAt(1);
    if (longitude.ValueType() != JsonValueType::Number ||
        latitude.ValueType() != JsonValueType::Number) {
        return std::nullopt;
    }

    return GeoPoint{latitude.GetNumber(), longitude.GetNumber()};
}

void ParseRing(
    const winrt::Windows::Data::Json::JsonArray& ringArray,
    SectorPolygon* polygon) {
    if (polygon == nullptr) {
        return;
    }

    for (uint32_t index = 0; index < ringArray.Size(); ++index) {
        const auto pointValue = ringArray.GetAt(index);
        if (pointValue.ValueType() != winrt::Windows::Data::Json::JsonValueType::Array) {
            continue;
        }
        const auto point = ParseCoordinatePair(pointValue.GetArray());
        if (point.has_value()) {
            polygon->ring.push_back(*point);
        }
    }
}

std::vector<SectorFeature> ParseSectorFeatures(const std::string& payload) {
    using namespace winrt;
    using namespace winrt::Windows::Data::Json;

    static const bool kWinRtInitialized = []() {
        try {
            init_apartment();
        } catch (...) {
        }
        return true;
    }();
    (void)kWinRtInitialized;

    std::vector<SectorFeature> features;
    if (payload.empty()) {
        return features;
    }

    try {
        const auto root = JsonObject::Parse(to_hstring(payload));
        const auto featureArray = root.GetNamedArray(L"features", JsonArray{});
        for (uint32_t featureIndex = 0; featureIndex < featureArray.Size(); ++featureIndex) {
            const auto featureObject = featureArray.GetObjectAt(featureIndex);
            const auto properties = featureObject.GetNamedObject(L"properties", JsonObject{});
            const auto geometry = featureObject.GetNamedObject(L"geometry", JsonObject{});

            SectorFeature feature;
            feature.label = ExtractLabel(properties, &feature.tokens);
            if (feature.tokens.empty()) {
                continue;
            }

            const auto geometryType = winrt::to_string(geometry.GetNamedString(L"type", L""));
            const auto coordinates = geometry.GetNamedArray(L"coordinates", JsonArray{});
            if (geometryType == "Polygon") {
                if (coordinates.Size() == 0) {
                    continue;
                }
                SectorPolygon polygon;
                ParseRing(coordinates.GetArrayAt(0), &polygon);
                if (polygon.ring.size() >= 3) {
                    feature.polygons.push_back(std::move(polygon));
                }
            } else if (geometryType == "MultiPolygon") {
                for (uint32_t polygonIndex = 0; polygonIndex < coordinates.Size(); ++polygonIndex) {
                    const auto polygonArray = coordinates.GetArrayAt(polygonIndex);
                    if (polygonArray.Size() == 0) {
                        continue;
                    }
                    SectorPolygon polygon;
                    ParseRing(polygonArray.GetArrayAt(0), &polygon);
                    if (polygon.ring.size() >= 3) {
                        feature.polygons.push_back(std::move(polygon));
                    }
                }
            }

            if (!feature.polygons.empty()) {
                features.push_back(std::move(feature));
            }
        }
    } catch (...) {
        return {};
    }

    return features;
}

std::string NormalizeCompactToken(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const auto character : value) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_') {
            normalized.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(character))));
        }
    }
    return normalized;
}

std::string NormalizeAuthorityCatalogKey(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const auto character : value) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0 ||
            character == '_' || character == '-') {
            normalized.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(character))));
        }
    }
    return normalized;
}

std::vector<std::string> ExtractStringList(
    const winrt::Windows::Data::Json::JsonObject& properties,
    const wchar_t* key) {
    using namespace winrt;
    using namespace winrt::Windows::Data::Json;

    std::vector<std::string> values;
    if (!properties.HasKey(key)) {
        return values;
    }

    const auto value = properties.GetNamedValue(key);
    if (value.ValueType() == JsonValueType::String) {
        const auto text = to_string(value.GetString());
        if (!text.empty()) {
            values.push_back(text);
        }
        return values;
    }

    if (value.ValueType() != JsonValueType::Array) {
        return values;
    }

    const auto array = value.GetArray();
    for (uint32_t index = 0; index < array.Size(); ++index) {
        const auto item = array.GetAt(index);
        if (item.ValueType() != JsonValueType::String) {
            continue;
        }
        const auto text = to_string(item.GetString());
        if (!text.empty()) {
            values.push_back(text);
        }
    }
    return values;
}

void AddTerminalPrefixTokens(
    const std::string& prefix,
    const std::string& suffix,
    std::unordered_set<std::string>* tokens) {
    if (tokens == nullptr) {
        return;
    }

    const auto normalizedPrefix = NormalizeCompactToken(prefix);
    if (normalizedPrefix.empty()) {
        return;
    }

    const auto normalizedSuffix = NormalizeCompactToken(suffix);
    if (!normalizedSuffix.empty()) {
        tokens->insert(normalizedPrefix + "_" + normalizedSuffix);
        return;
    }

    // SimAware treats a missing suffix as approach. In-sim, many VATSIM
    // facilities use the same terminal boundary for APP and DEP.
    tokens->insert(normalizedPrefix + "_APP");
    tokens->insert(normalizedPrefix + "_DEP");
}

void AppendTerminalFeature(
    const winrt::Windows::Data::Json::JsonObject& featureObject,
    std::vector<SectorFeature>* features) {
    using namespace winrt;
    using namespace winrt::Windows::Data::Json;

    if (features == nullptr) {
        return;
    }

    const auto properties = featureObject.GetNamedObject(L"properties", JsonObject{});
    const auto geometry = featureObject.GetNamedObject(L"geometry", JsonObject{});

    SectorFeature feature;
    const auto id = to_string(properties.GetNamedString(L"id", L""));
    const auto name = to_string(properties.GetNamedString(L"name", L""));
    const auto suffix = to_string(properties.GetNamedString(L"suffix", L""));
    feature.label = !id.empty() ? id : (!name.empty() ? name : "TRACON");
    if (!suffix.empty()) {
        feature.label += "_" + NormalizeCompactToken(suffix);
    }

    const auto prefixes = ExtractStringList(properties, L"prefix");
    for (const auto& prefix : prefixes) {
        AddTerminalPrefixTokens(prefix, suffix, &feature.tokens);
    }
    if (prefixes.empty() && !id.empty()) {
        AddTerminalPrefixTokens(id, suffix, &feature.tokens);
    }

    if (feature.tokens.empty()) {
        return;
    }

    const auto geometryType = to_string(geometry.GetNamedString(L"type", L""));
    const auto coordinates = geometry.GetNamedArray(L"coordinates", JsonArray{});
    if (geometryType == "Polygon") {
        if (coordinates.Size() == 0) {
            return;
        }
        SectorPolygon polygon;
        ParseRing(coordinates.GetArrayAt(0), &polygon);
        if (polygon.ring.size() >= 3) {
            feature.polygons.push_back(std::move(polygon));
        }
    } else if (geometryType == "MultiPolygon") {
        for (uint32_t polygonIndex = 0; polygonIndex < coordinates.Size(); ++polygonIndex) {
            const auto polygonArray = coordinates.GetArrayAt(polygonIndex);
            if (polygonArray.Size() == 0) {
                continue;
            }
            SectorPolygon polygon;
            ParseRing(polygonArray.GetArrayAt(0), &polygon);
            if (polygon.ring.size() >= 3) {
                feature.polygons.push_back(std::move(polygon));
            }
        }
    }

    if (!feature.polygons.empty()) {
        features->push_back(std::move(feature));
    }
}

std::vector<SectorFeature> ParseTerminalSectorFeatures(const std::string& payload) {
    using namespace winrt;
    using namespace winrt::Windows::Data::Json;

    static const bool kWinRtInitialized = []() {
        try {
            init_apartment();
        } catch (...) {
        }
        return true;
    }();
    (void)kWinRtInitialized;

    std::vector<SectorFeature> features;
    if (payload.empty()) {
        return features;
    }

    try {
        const auto root = JsonObject::Parse(to_hstring(payload));
        const auto rootType = to_string(root.GetNamedString(L"type", L""));
        if (rootType == "Feature") {
            AppendTerminalFeature(root, &features);
            return features;
        }

        const auto featureArray = root.GetNamedArray(L"features", JsonArray{});
        for (uint32_t featureIndex = 0; featureIndex < featureArray.Size(); ++featureIndex) {
            AppendTerminalFeature(featureArray.GetObjectAt(featureIndex), &features);
        }
    } catch (...) {
        return {};
    }

    return features;
}

void MergeControllerAuthorityPrefixes(
    std::unordered_map<std::string, std::unordered_set<std::string>>* prefixesByKey,
    const std::string& lookupKey,
    const std::vector<std::string>& additions) {
    if (prefixesByKey == nullptr) {
        return;
    }

    const auto normalizedLookupKey = NormalizeAuthorityCatalogKey(lookupKey);
    if (normalizedLookupKey.empty()) {
        return;
    }

    auto& prefixes = (*prefixesByKey)[normalizedLookupKey];
    for (const auto& addition : additions) {
        const auto normalizedAddition = NormalizeAuthorityCatalogKey(addition);
        if (!normalizedAddition.empty()) {
            prefixes.insert(normalizedAddition);
        }
    }
}

void AddCenterActivationPatterns(
    const std::string& activationBase,
    std::unordered_set<std::string>* patterns) {
    if (patterns == nullptr) {
        return;
    }

    const auto normalizedBase = NormalizeAuthorityCatalogKey(activationBase);
    if (normalizedBase.empty()) {
        return;
    }

    patterns->insert(normalizedBase + "_CTR");
    patterns->insert(normalizedBase + "_*_CTR");
    patterns->insert(normalizedBase + "_FSS");
    patterns->insert(normalizedBase + "_*_FSS");
}

void MergeControllerAuthorityPatterns(
    std::unordered_map<std::string, std::unordered_set<std::string>>* patternsByKey,
    const std::string& lookupKey,
    const std::vector<std::string>& activationBases) {
    if (patternsByKey == nullptr) {
        return;
    }

    const auto normalizedLookupKey = NormalizeAuthorityCatalogKey(lookupKey);
    if (normalizedLookupKey.empty()) {
        return;
    }

    auto& patterns = (*patternsByKey)[normalizedLookupKey];
    for (const auto& activationBase : activationBases) {
        AddCenterActivationPatterns(activationBase, &patterns);
    }
}

ControllerAuthorityCatalog ParseControllerAuthorityCatalog(const std::string& payload) {
    enum class Section {
        None,
        Firs,
        Uirs,
    };

    ControllerAuthorityCatalog catalog;
    if (payload.empty()) {
        return catalog;
    }

    std::unordered_map<std::string, std::unordered_set<std::string>> prefixesByKey;
    std::unordered_map<std::string, std::unordered_set<std::string>> patternsByKey;
    Section currentSection = Section::None;

    std::istringstream stream(payload);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty() || line.front() == ';') {
            continue;
        }

        if (line == "[FIRs]") {
            currentSection = Section::Firs;
            continue;
        }
        if (line == "[UIRs]") {
            currentSection = Section::Uirs;
            continue;
        }
        if (!line.empty() && line.front() == '[') {
            currentSection = Section::None;
            continue;
        }
        if (currentSection != Section::Firs && currentSection != Section::Uirs) {
            continue;
        }

        std::vector<std::string> fields;
        std::size_t startIndex = 0;
        while (startIndex <= line.size()) {
            const auto separatorIndex = line.find('|', startIndex);
            if (separatorIndex == std::string::npos) {
                fields.push_back(line.substr(startIndex));
                break;
            }
            fields.push_back(line.substr(startIndex, separatorIndex - startIndex));
            startIndex = separatorIndex + 1;
        }

        if (fields.size() < 4) {
            continue;
        }

        const auto sectorIdentifier = NormalizeAuthorityCatalogKey(fields[0]);
        const auto callsignPrefix = NormalizeAuthorityCatalogKey(fields[2]);
        auto boundaryIdentifier = NormalizeAuthorityCatalogKey(fields[3]);
        if (boundaryIdentifier.empty()) {
            boundaryIdentifier = sectorIdentifier;
        }
        if (boundaryIdentifier.empty()) {
            continue;
        }

        std::vector<std::string> acceptedPrefixes;
        if (!callsignPrefix.empty()) {
            acceptedPrefixes.push_back(callsignPrefix);
        }

        MergeControllerAuthorityPrefixes(
            &prefixesByKey,
            boundaryIdentifier,
            acceptedPrefixes);
        MergeControllerAuthorityPatterns(
            &patternsByKey,
            boundaryIdentifier,
            acceptedPrefixes);
        if (!sectorIdentifier.empty() && sectorIdentifier != boundaryIdentifier) {
            MergeControllerAuthorityPrefixes(
                &prefixesByKey,
                sectorIdentifier,
                acceptedPrefixes);
            MergeControllerAuthorityPatterns(
                &patternsByKey,
                sectorIdentifier,
                acceptedPrefixes);
        }
    }

    for (auto& [key, prefixes] : prefixesByKey) {
        std::vector<std::string> normalizedPrefixes(prefixes.begin(), prefixes.end());
        std::sort(normalizedPrefixes.begin(), normalizedPrefixes.end());
        catalog.prefixesByKey.emplace(std::move(key), std::move(normalizedPrefixes));
    }
    for (auto& [key, patterns] : patternsByKey) {
        std::vector<std::string> normalizedPatterns(patterns.begin(), patterns.end());
        std::sort(normalizedPatterns.begin(), normalizedPatterns.end());
        catalog.callsignPatternsByKey.emplace(std::move(key), std::move(normalizedPatterns));
    }

    return catalog;
}

std::size_t HashPayload(const std::vector<unsigned char>& payload) {
    std::size_t hash = 1469598103934665603ull;
    for (const auto byte : payload) {
        hash ^= static_cast<std::size_t>(byte);
        hash *= 1099511628211ull;
    }
    return hash;
}

const std::vector<SectorFeature>& GetCachedSectorFeatures(
    const std::vector<unsigned char>& payload) {
    static std::mutex cacheMutex;
    static std::size_t cachedHash = 0;
    static std::size_t cachedSize = 0;
    static std::vector<SectorFeature> cachedFeatures;

    const auto payloadHash = HashPayload(payload);
    std::lock_guard<std::mutex> lock(cacheMutex);
    if (payloadHash == cachedHash && payload.size() == cachedSize) {
        return cachedFeatures;
    }

    cachedHash = payloadHash;
    cachedSize = payload.size();
    cachedFeatures = ParseSectorFeatures(
        std::string(
            reinterpret_cast<const char*>(payload.data()),
            payload.size()));
    return cachedFeatures;
}

const std::vector<SectorFeature>& GetCachedTerminalSectorFeatures(
    const std::vector<unsigned char>& payload) {
    static std::mutex cacheMutex;
    static std::size_t cachedHash = 0;
    static std::size_t cachedSize = 0;
    static std::vector<SectorFeature> cachedFeatures;

    const auto payloadHash = HashPayload(payload);
    std::lock_guard<std::mutex> lock(cacheMutex);
    if (payloadHash == cachedHash && payload.size() == cachedSize) {
        return cachedFeatures;
    }

    cachedHash = payloadHash;
    cachedSize = payload.size();
    cachedFeatures = ParseTerminalSectorFeatures(
        std::string(
            reinterpret_cast<const char*>(payload.data()),
            payload.size()));
    return cachedFeatures;
}

const ControllerAuthorityCatalog& GetCachedControllerAuthorityCatalog(
    const std::vector<unsigned char>& payload) {
    static std::mutex cacheMutex;
    static std::size_t cachedHash = 0;
    static std::size_t cachedSize = 0;
    static ControllerAuthorityCatalog cachedCatalog;

    const auto payloadHash = HashPayload(payload);
    std::lock_guard<std::mutex> lock(cacheMutex);
    if (payloadHash == cachedHash && payload.size() == cachedSize) {
        return cachedCatalog;
    }

    cachedHash = payloadHash;
    cachedSize = payload.size();
    cachedCatalog = ParseControllerAuthorityCatalog(
        std::string(
            reinterpret_cast<const char*>(payload.data()),
            payload.size()));
    return cachedCatalog;
}

double ToRadians(double degrees) {
    return degrees * kPi / 180.0;
}

double ToDegrees(double radians) {
    return radians * 180.0 / kPi;
}

double ClampUnit(double value) {
    return std::clamp(value, -1.0, 1.0);
}

double AngularDistanceRad(
    double latitudeDegA,
    double longitudeDegA,
    double latitudeDegB,
    double longitudeDegB) {
    const auto latitudeRadA = ToRadians(latitudeDegA);
    const auto latitudeRadB = ToRadians(latitudeDegB);
    const auto deltaLatitude = ToRadians(latitudeDegB - latitudeDegA);
    const auto deltaLongitude = ToRadians(longitudeDegB - longitudeDegA);

    const auto sinLatitude = std::sin(deltaLatitude / 2.0);
    const auto sinLongitude = std::sin(deltaLongitude / 2.0);
    const auto a = sinLatitude * sinLatitude +
                   std::cos(latitudeRadA) * std::cos(latitudeRadB) *
                       sinLongitude * sinLongitude;
    const auto clampedA = std::clamp(a, 0.0, 1.0);
    return 2.0 * std::atan2(std::sqrt(clampedA), std::sqrt(1.0 - clampedA));
}

double GreatCircleDistanceNm(
    double latitudeDegA,
    double longitudeDegA,
    double latitudeDegB,
    double longitudeDegB) {
    return kEarthRadiusNm *
           AngularDistanceRad(latitudeDegA, longitudeDegA, latitudeDegB, longitudeDegB);
}

double NormalizeLongitudeDeg(double longitudeDeg) {
    while (longitudeDeg > 180.0) {
        longitudeDeg -= 360.0;
    }
    while (longitudeDeg < -180.0) {
        longitudeDeg += 360.0;
    }
    return longitudeDeg;
}

double ShortestLongitudeDeltaDeg(double fromLongitudeDeg, double toLongitudeDeg) {
    auto deltaDeg = toLongitudeDeg - fromLongitudeDeg;
    while (deltaDeg > 180.0) {
        deltaDeg -= 360.0;
    }
    while (deltaDeg < -180.0) {
        deltaDeg += 360.0;
    }
    return deltaDeg;
}

double UnwrapLongitudeRelativeDeg(double referenceLongitudeDeg, double longitudeDeg) {
    return referenceLongitudeDeg +
           ShortestLongitudeDeltaDeg(referenceLongitudeDeg, longitudeDeg);
}

double DotProduct(const Vector3& a, const Vector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 CrossProduct(const Vector3& a, const Vector3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

Vector3 AddVector(const Vector3& a, const Vector3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 ScaleVector(const Vector3& vector, double scale) {
    return {vector.x * scale, vector.y * scale, vector.z * scale};
}

double VectorLength(const Vector3& vector) {
    return std::sqrt(DotProduct(vector, vector));
}

std::optional<Vector3> NormalizeVector(const Vector3& vector) {
    const auto length = VectorLength(vector);
    if (length <= 1e-12) {
        return std::nullopt;
    }
    return ScaleVector(vector, 1.0 / length);
}

Vector3 ToUnitVector(const GeoPoint& point) {
    const auto latitudeRad = ToRadians(point.latitudeDeg);
    const auto longitudeRad = ToRadians(NormalizeLongitudeDeg(point.longitudeDeg));
    const auto cosLatitude = std::cos(latitudeRad);
    return {
        cosLatitude * std::cos(longitudeRad),
        cosLatitude * std::sin(longitudeRad),
        std::sin(latitudeRad),
    };
}

GeoPoint ToGeoPoint(const Vector3& vector) {
    const auto normalized = NormalizeVector(vector);
    if (!normalized.has_value()) {
        return {};
    }

    const auto horizontalLength =
        std::sqrt(normalized->x * normalized->x + normalized->y * normalized->y);
    return {
        ToDegrees(std::atan2(normalized->z, horizontalLength)),
        NormalizeLongitudeDeg(ToDegrees(std::atan2(normalized->y, normalized->x))),
    };
}

double AngularDistanceRad(const Vector3& a, const Vector3& b) {
    return std::acos(ClampUnit(DotProduct(a, b)));
}

bool CrossesAntiMeridian(double longitudeDegA, double longitudeDegB) {
    return std::fabs(
               NormalizeLongitudeDeg(longitudeDegB) -
               NormalizeLongitudeDeg(longitudeDegA)) > 180.0;
}

bool RingCrossesAntiMeridian(const SectorPolygon& polygon) {
    double minLongitudeDeg = std::numeric_limits<double>::max();
    double maxLongitudeDeg = std::numeric_limits<double>::lowest();
    for (std::size_t index = 0; index < polygon.ring.size(); ++index) {
        const auto longitudeDeg = NormalizeLongitudeDeg(polygon.ring[index].longitudeDeg);
        minLongitudeDeg = std::min(minLongitudeDeg, longitudeDeg);
        maxLongitudeDeg = std::max(maxLongitudeDeg, longitudeDeg);
        if (index > 0 &&
            CrossesAntiMeridian(
                polygon.ring[index - 1].longitudeDeg,
                polygon.ring[index].longitudeDeg)) {
            return true;
        }
    }

    return (maxLongitudeDeg - minLongitudeDeg) > 180.0;
}

bool PointInRing(
    const GeoPoint& point,
    const SectorPolygon& polygon) {
    bool inside = false;
    const auto count = polygon.ring.size();
    if (count < 3) {
        return false;
    }

    const auto crossesAntiMeridian = RingCrossesAntiMeridian(polygon);
    const auto pointLongitudeDeg = NormalizeLongitudeDeg(point.longitudeDeg);

    for (std::size_t i = 0, j = count - 1; i < count; j = i++) {
        const auto& pi = polygon.ring[i];
        const auto& pj = polygon.ring[j];
        const auto piLongitudeDeg =
            crossesAntiMeridian
                ? UnwrapLongitudeRelativeDeg(pointLongitudeDeg, pi.longitudeDeg)
                : pi.longitudeDeg;
        const auto pjLongitudeDeg =
            crossesAntiMeridian
                ? UnwrapLongitudeRelativeDeg(pointLongitudeDeg, pj.longitudeDeg)
                : pj.longitudeDeg;
        const auto intersects =
            ((pi.latitudeDeg > point.latitudeDeg) != (pj.latitudeDeg > point.latitudeDeg)) &&
            (pointLongitudeDeg <
             (pjLongitudeDeg - piLongitudeDeg) * (point.latitudeDeg - pi.latitudeDeg) /
                     ((pj.latitudeDeg - pi.latitudeDeg) == 0.0 ? 1e-12 : (pj.latitudeDeg - pi.latitudeDeg)) +
                 piLongitudeDeg);
        if (intersects) {
            inside = !inside;
        }
    }

    return inside;
}

bool PointInFeature(const GeoPoint& point, const SectorFeature& feature) {
    for (const auto& polygon : feature.polygons) {
        if (PointInRing(point, polygon)) {
            return true;
        }
    }
    return false;
}

std::string BuildRouteCacheKey(const brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    std::string key;
    key.reserve(
        networkPlanSnapshot.departureIcao.size() +
        networkPlanSnapshot.destinationIcao.size() +
        networkPlanSnapshot.routeText.size() + 32);
    key.append(networkPlanSnapshot.departureIcao);
    key.push_back('|');
    key.append(networkPlanSnapshot.destinationIcao);
    key.push_back('|');
    key.append(networkPlanSnapshot.routeText);
    return key;
}

std::string BuildAirportCoverageKey(
    const std::string& airportIcao,
    double airportLatitudeDeg,
    double airportLongitudeDeg) {
    std::ostringstream stream;
    stream << airportIcao << "|"
           << airportLatitudeDeg << "|"
           << airportLongitudeDeg;
    return stream.str();
}

std::string ReadTextFile(const std::string& path) {
    std::ifstream stream(path, std::ios::in | std::ios::binary);
    if (!stream.is_open()) {
        return {};
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

HMODULE ResolveXPlaneModule() {
    if (auto* module = GetModuleHandleA("XPLM_64.dll")) {
        return module;
    }
    if (auto* module = GetModuleHandleA("XPLM.dll")) {
        return module;
    }
    return nullptr;
}

void SafeXPlaneDebugString(const std::string& message) {
    using DebugStringFn = void (*)(const char*);
    const auto module = ResolveXPlaneModule();
    if (module == nullptr) {
        return;
    }

    const auto function =
        reinterpret_cast<DebugStringFn>(GetProcAddress(module, "XPLMDebugString"));
    if (function != nullptr) {
        function(message.c_str());
    }
}

std::string GetXPlaneRootPath() {
    using GetSystemPathFn = void (*)(char*);
    char systemPath[512] = {};
    const auto module = ResolveXPlaneModule();
    if (module != nullptr) {
        const auto function =
            reinterpret_cast<GetSystemPathFn>(GetProcAddress(module, "XPLMGetSystemPath"));
        if (function != nullptr) {
            function(systemPath);
        }
    }

    std::string root(systemPath);
    if (!root.empty() && root.back() != '\\' && root.back() != '/') {
        root.push_back('\\');
    }
    return root;
}

std::string TrimAsciiWhitespace(std::string value) {
    const auto notSpace = [](unsigned char ch) {
        return std::isspace(ch) == 0;
    };

    value.erase(
        value.begin(),
        std::find_if(value.begin(), value.end(), notSpace));
    value.erase(
        std::find_if(value.rbegin(), value.rend(), notSpace).base(),
        value.end());
    return value;
}

std::vector<std::string> SplitCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ',')) {
        fields.push_back(TrimAsciiWhitespace(field));
    }
    return fields;
}

void MergeProcedureMetadata(
    const std::string& rawName,
    const std::string& recordType,
    const std::string& rawTransition,
    const std::string& authoritySource,
    std::unordered_map<std::string, xvatsim::core::route::ProcedureCatalogEntry>* proceduresByName) {
    if (proceduresByName == nullptr) {
        return;
    }

    const auto normalizedName =
        xvatsim::core::route::ExtractRouteTokenBase(rawName);
    if (normalizedName.empty()) {
        return;
    }

    const auto normalizedRecordType =
        xvatsim::core::route::NormalizeRouteToken(recordType);
    const auto isSid = normalizedRecordType == "SID";
    const auto isStar = normalizedRecordType == "STAR";
    if (!isSid && !isStar) {
        return;
    }

    auto& entry = (*proceduresByName)[normalizedName];
    entry.hasSid = entry.hasSid || isSid;
    entry.hasStar = entry.hasStar || isStar;
    const auto normalizedAuthority =
        xvatsim::core::route::NormalizeRouteToken(authoritySource);
    if (isSid && !normalizedAuthority.empty()) {
        entry.sidAuthoritySources.insert(normalizedAuthority);
    }
    if (isStar && !normalizedAuthority.empty()) {
        entry.starAuthoritySources.insert(normalizedAuthority);
    }

    if (rawTransition.empty()) {
        return;
    }

    if (xvatsim::core::route::IsRunwayProcedureSegmentToken(rawTransition)) {
        const auto normalizedRunway =
            xvatsim::core::route::ExtractRouteTokenBase(rawTransition);
        if (isSid) {
            entry.hasSidRunwayRecords = true;
            if (!normalizedRunway.empty()) {
                entry.sidRunwayTransitions.insert(normalizedRunway);
            }
        }
        if (isStar) {
            entry.hasStarRunwayRecords = true;
            if (!normalizedRunway.empty()) {
                entry.starRunwayTransitions.insert(normalizedRunway);
            }
        }
        return;
    }

    const auto normalizedTransition =
        xvatsim::core::route::ExtractRouteTokenBase(rawTransition);
    if (normalizedTransition.empty()) {
        return;
    }

    if (isSid) {
        entry.sidTransitions.insert(normalizedTransition);
    }
    if (isStar) {
        entry.starTransitions.insert(normalizedTransition);
    }
}

void MergeProcedureFix(
    const std::string& rawName,
    const std::string& recordType,
    const std::string& rawFix,
    std::unordered_map<std::string, xvatsim::core::route::ProcedureCatalogEntry>* proceduresByName) {
    if (proceduresByName == nullptr) {
        return;
    }

    const auto normalizedName =
        xvatsim::core::route::ExtractRouteTokenBase(rawName);
    if (normalizedName.empty()) {
        return;
    }

    const auto normalizedRecordType =
        xvatsim::core::route::NormalizeRouteToken(recordType);
    const auto isSid = normalizedRecordType == "SID";
    const auto isStar = normalizedRecordType == "STAR";
    if (!isSid && !isStar) {
        return;
    }

    const auto normalizedFix =
        xvatsim::core::route::ExtractRouteTokenBase(rawFix);
    if (normalizedFix.empty() ||
        xvatsim::core::route::IsRouteControlToken(normalizedFix) ||
        xvatsim::core::route::IsRunwayProcedureSegmentToken(rawFix)) {
        return;
    }

    auto& entry = (*proceduresByName)[normalizedName];
    if (isSid) {
        if (entry.sidFixes.insert(normalizedFix).second) {
            entry.sidOrderedFixes.push_back(normalizedFix);
        }
    }
    if (isStar) {
        if (entry.starFixes.insert(normalizedFix).second) {
            entry.starOrderedFixes.push_back(normalizedFix);
        }
    }
}

std::string ResolveProcedureMetadataSourceTag(
    const xvatsim::core::route::ProcedureCatalogEntry& procedureEntry) {
    if (procedureEntry.sourcedFromDepartureAirport &&
        procedureEntry.sourcedFromArrivalAirport) {
        return "BOTH";
    }
    if (procedureEntry.sourcedFromDepartureAirport) {
        return "DEP";
    }
    if (procedureEntry.sourcedFromArrivalAirport) {
        return "ARR";
    }
    return "UNK";
}

const std::string* ResolveSidBoundaryFix(
    const xvatsim::core::route::ProcedureCatalogEntry& procedureEntry) {
    if (procedureEntry.sidOrderedFixes.empty()) {
        return nullptr;
    }
    return &procedureEntry.sidOrderedFixes.back();
}

const std::string* ResolveStarBoundaryFix(
    const xvatsim::core::route::ProcedureCatalogEntry& procedureEntry) {
    if (procedureEntry.starOrderedFixes.empty()) {
        return nullptr;
    }
    return &procedureEntry.starOrderedFixes.front();
}

struct SyntheticProcedureAnchor {
    const std::string* ident = nullptr;
    const char* source = "";
};

SyntheticProcedureAnchor ResolveSidForwardSyntheticAnchor(
    const xvatsim::core::route::ProcedureCatalogEntry& procedureEntry) {
    if (!procedureEntry.hasSid ||
        procedureEntry.hasStar ||
        procedureEntry.hasSidRunwayRecords) {
        return {};
    }

    if (procedureEntry.sidTransitions.size() == 1) {
        return {&(*procedureEntry.sidTransitions.begin()), "TRANSITION"};
    }

    if (const auto* boundaryFix = ResolveSidBoundaryFix(procedureEntry);
        boundaryFix != nullptr) {
        return {boundaryFix, "BOUNDARY"};
    }
    return {};
}

SyntheticProcedureAnchor ResolveStarBackwardSyntheticAnchor(
    const xvatsim::core::route::ProcedureCatalogEntry& procedureEntry) {
    if (!procedureEntry.hasStar ||
        procedureEntry.hasSid ||
        procedureEntry.hasStarRunwayRecords) {
        return {};
    }

    if (procedureEntry.starTransitions.size() == 1) {
        return {&(*procedureEntry.starTransitions.begin()), "TRANSITION"};
    }

    if (const auto* boundaryFix = ResolveStarBoundaryFix(procedureEntry);
        boundaryFix != nullptr) {
        return {boundaryFix, "BOUNDARY"};
    }
    return {};
}

bool TryResolveSyntheticSidOrderedSequence(
    const xvatsim::core::route::ParsedRouteToken& procedureToken,
    const std::vector<xvatsim::core::route::ParsedRouteToken>& parsedTokens,
    std::size_t tokenIndex,
    const xvatsim::core::route::RouteGrammarCatalog& grammarCatalog,
    const std::optional<GeoPoint>& referencePoint,
    std::vector<ResolvedRoutePoint>* outPoints) {
    if (outPoints == nullptr) {
        return false;
    }

    const auto* procedureEntry =
        xvatsim::core::route::LookupProcedureCatalogEntry(
            grammarCatalog,
            procedureToken.normalized);
    if (procedureEntry == nullptr ||
        !procedureEntry->hasSid ||
        procedureEntry->hasStar ||
        procedureEntry->hasSidRunwayRecords ||
        !procedureEntry->sidTransitions.empty() ||
        procedureEntry->sidOrderedFixes.size() < 2) {
        return false;
    }

    const auto nextMeaningfulIndex =
        FindNextMeaningfulTokenIndex(parsedTokens, tokenIndex + 1);
    if (nextMeaningfulIndex.has_value()) {
        const auto& nextToken = parsedTokens[*nextMeaningfulIndex];
        if (!(TokenCanActAsAirway(nextToken) && !TokenCanActAsPoint(nextToken)) &&
            !TokenCanActAsAirway(nextToken)) {
            return false;
        }
    }

    std::optional<std::string> followingAirwayToken;
    std::optional<std::string> followingAirwayEndToken;
    if (nextMeaningfulIndex.has_value() &&
        TokenCanActAsAirway(parsedTokens[*nextMeaningfulIndex])) {
        const auto airwayExitAnchorIndex =
            FindNextAnchorTokenIndex(parsedTokens, *nextMeaningfulIndex + 1, nullptr);
        if (airwayExitAnchorIndex.has_value()) {
            followingAirwayToken = parsedTokens[*nextMeaningfulIndex].normalized;
            followingAirwayEndToken = parsedTokens[*airwayExitAnchorIndex].normalized;
        }
    }

    std::optional<GeoPoint> currentReferencePoint = referencePoint;
    std::vector<ResolvedRoutePoint> resolvedPoints;
    resolvedPoints.reserve(procedureEntry->sidOrderedFixes.size());
    for (std::size_t orderedFixIndex = 0;
         orderedFixIndex < procedureEntry->sidOrderedFixes.size();
         ++orderedFixIndex) {
        const auto& orderedFix = procedureEntry->sidOrderedFixes[orderedFixIndex];
        if (orderedFix.empty()) {
            continue;
        }

        std::optional<std::string> nextOrderedFix;
        for (std::size_t nextFixIndex = orderedFixIndex + 1;
             nextFixIndex < procedureEntry->sidOrderedFixes.size();
             ++nextFixIndex) {
            if (!procedureEntry->sidOrderedFixes[nextFixIndex].empty()) {
                nextOrderedFix = procedureEntry->sidOrderedFixes[nextFixIndex];
                break;
            }
        }

        ResolvedRoutePoint resolvedPoint;
        const auto resolvedWithAirwayEntry =
            !nextOrderedFix.has_value() &&
            followingAirwayToken.has_value() &&
            followingAirwayEndToken.has_value() &&
            ResolveRoutePointTokenWithAirwayEntryContext(
                orderedFix,
                currentReferencePoint,
                *followingAirwayToken,
                *followingAirwayEndToken,
                &resolvedPoint);
        if (!resolvedWithAirwayEntry &&
            !ResolveRoutePointTokenWithRouteContext(
                orderedFix,
                currentReferencePoint,
                nextOrderedFix,
                std::nullopt,
                &resolvedPoint)) {
            return false;
        }

        resolvedPoints.push_back(resolvedPoint);
        currentReferencePoint = GeoPoint{
            resolvedPoint.latitudeDeg,
            resolvedPoint.longitudeDeg,
        };
    }

    if (resolvedPoints.size() < 2) {
        return false;
    }

    *outPoints = std::move(resolvedPoints);
    return true;
}

bool TryResolveSyntheticStarOrderedSequence(
    const xvatsim::core::route::ParsedRouteToken& procedureToken,
    const xvatsim::core::route::RouteGrammarCatalog& grammarCatalog,
    const std::optional<GeoPoint>& referencePoint,
    std::vector<ResolvedRoutePoint>* outPoints) {
    if (outPoints == nullptr) {
        return false;
    }

    const auto* procedureEntry =
        xvatsim::core::route::LookupProcedureCatalogEntry(
            grammarCatalog,
            procedureToken.normalized);
    if (procedureEntry == nullptr ||
        !procedureEntry->hasStar ||
        procedureEntry->hasSid ||
        procedureEntry->hasStarRunwayRecords ||
        !procedureEntry->starTransitions.empty() ||
        procedureEntry->starOrderedFixes.size() < 2) {
        return false;
    }

    std::optional<GeoPoint> currentReferencePoint = referencePoint;
    std::vector<ResolvedRoutePoint> resolvedPoints;
    resolvedPoints.reserve(procedureEntry->starOrderedFixes.size());
    for (std::size_t orderedFixIndex = 0;
         orderedFixIndex < procedureEntry->starOrderedFixes.size();
         ++orderedFixIndex) {
        const auto& orderedFix = procedureEntry->starOrderedFixes[orderedFixIndex];
        if (orderedFix.empty()) {
            continue;
        }

        std::optional<std::string> nextOrderedFix;
        for (std::size_t nextFixIndex = orderedFixIndex + 1;
             nextFixIndex < procedureEntry->starOrderedFixes.size();
             ++nextFixIndex) {
            if (!procedureEntry->starOrderedFixes[nextFixIndex].empty()) {
                nextOrderedFix = procedureEntry->starOrderedFixes[nextFixIndex];
                break;
            }
        }

        ResolvedRoutePoint resolvedPoint;
        if (!ResolveRoutePointTokenWithRouteContext(
                orderedFix,
                currentReferencePoint,
                nextOrderedFix,
                std::nullopt,
                &resolvedPoint)) {
            return false;
        }

        resolvedPoints.push_back(resolvedPoint);
        currentReferencePoint = GeoPoint{
            resolvedPoint.latitudeDeg,
            resolvedPoint.longitudeDeg,
        };
    }

    if (resolvedPoints.size() < 2) {
        return false;
    }

    *outPoints = std::move(resolvedPoints);
    return true;
}

void LoadProcedureMetadataFromProcPayload(
    const std::string& payload,
    std::unordered_map<std::string, xvatsim::core::route::ProcedureCatalogEntry>* proceduresByName) {
    if (payload.empty() || proceduresByName == nullptr) {
        return;
    }

    std::istringstream stream(payload);
    std::string line;
    std::string activeRecordType;
    std::string activeProcedureName;
    while (std::getline(stream, line)) {
        const auto fields = SplitCsvLine(line);
        if (fields.empty()) {
            continue;
        }

        const auto recordType =
            xvatsim::core::route::NormalizeRouteToken(fields[0]);
        if (recordType != "SID" && recordType != "STAR") {
            if (!activeRecordType.empty() &&
                !activeProcedureName.empty() &&
                fields.size() >= 2) {
                MergeProcedureFix(
                    activeProcedureName,
                    activeRecordType,
                    fields[1],
                    proceduresByName);
            }
            continue;
        }

        if (fields.size() < 2) {
            activeRecordType.clear();
            activeProcedureName.clear();
            continue;
        }

        activeRecordType = recordType;
        activeProcedureName = fields[1];

        const auto transition = fields.size() >= 3 ? fields[2] : std::string{};
        MergeProcedureMetadata(fields[1], recordType, transition, "PROC", proceduresByName);
    }
}

void LoadProcedureMetadataFromCifpPayload(
    const std::string& payload,
    const std::string& authoritySource,
    std::unordered_map<std::string, xvatsim::core::route::ProcedureCatalogEntry>* proceduresByName) {
    if (payload.empty() || proceduresByName == nullptr) {
        return;
    }

    std::istringstream stream(payload);
    std::string line;
    while (std::getline(stream, line)) {
        const auto fields = SplitCsvLine(line);
        if (fields.size() < 3) {
            continue;
        }

        const auto separatorIndex = fields[0].find(':');
        if (separatorIndex == std::string::npos) {
            continue;
        }

        const auto recordType =
            xvatsim::core::route::NormalizeRouteToken(
                fields[0].substr(0, separatorIndex));
        if (recordType != "SID" && recordType != "STAR") {
            continue;
        }

        const auto transition = fields.size() >= 4 ? fields[3] : std::string{};
        MergeProcedureMetadata(
            fields[2],
            recordType,
            transition,
            authoritySource,
            proceduresByName);
        if (fields.size() >= 5) {
            MergeProcedureFix(fields[2], recordType, fields[4], proceduresByName);
        }
    }
}

std::unordered_map<std::string, xvatsim::core::route::ProcedureCatalogEntry>
LoadAirportProcedureMetadata(
    const std::string& airportIcao) {
    std::unordered_map<std::string, xvatsim::core::route::ProcedureCatalogEntry>
        proceduresByName;
    const auto normalizedIcao =
        xvatsim::core::route::NormalizeRouteToken(airportIcao);
    if (normalizedIcao.empty()) {
        return proceduresByName;
    }

    const auto root = GetXPlaneRootPath();
    const auto procPayload = ReadTextFile(
        root + "Custom Data\\navdata\\Proc\\" + normalizedIcao + ".txt");
    if (!procPayload.empty()) {
        LoadProcedureMetadataFromProcPayload(procPayload, &proceduresByName);
    }

    const auto customCifpPayload = ReadTextFile(
        root + "Custom Data\\CIFP\\" + normalizedIcao + ".dat");
    if (!customCifpPayload.empty()) {
        LoadProcedureMetadataFromCifpPayload(
            customCifpPayload,
            "CIFP_CUSTOM",
            &proceduresByName);
    }

    const auto defaultCifpPayload = ReadTextFile(
        root + "Resources\\default data\\CIFP\\" + normalizedIcao + ".dat");
    if (!defaultCifpPayload.empty()) {
        LoadProcedureMetadataFromCifpPayload(
            defaultCifpPayload,
            "CIFP_DEFAULT",
            &proceduresByName);
    }

    return proceduresByName;
}

const std::unordered_map<std::string, xvatsim::core::route::ProcedureCatalogEntry>&
GetCachedAirportProcedureMetadata(
    const std::string& airportIcao) {
    static std::mutex cacheMutex;
    static std::unordered_map<
        std::string,
        std::unordered_map<std::string, xvatsim::core::route::ProcedureCatalogEntry>>
        cache;
    static const std::unordered_map<std::string, xvatsim::core::route::ProcedureCatalogEntry>
        empty;

    const auto normalizedIcao =
        xvatsim::core::route::NormalizeRouteToken(airportIcao);
    if (normalizedIcao.empty()) {
        return empty;
    }

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        const auto cached = cache.find(normalizedIcao);
        if (cached != cache.end()) {
            return cached->second;
        }
    }

    auto loadedMetadata = LoadAirportProcedureMetadata(normalizedIcao);

    std::lock_guard<std::mutex> lock(cacheMutex);
    const auto [iterator, _] =
        cache.emplace(normalizedIcao, std::move(loadedMetadata));
    return iterator->second;
}

std::unordered_map<std::string, xvatsim::core::route::ProcedureCatalogEntry>
BuildRouteProcedureCatalog(
    const brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    std::unordered_map<std::string, xvatsim::core::route::ProcedureCatalogEntry>
        proceduresByName;
    const auto mergeOrderedFixes =
        [](const std::vector<std::string>& sourceFixes,
           std::unordered_set<std::string>* targetFixSet,
           std::vector<std::string>* targetOrderedFixes) {
        if (targetFixSet == nullptr || targetOrderedFixes == nullptr) {
            return;
        }

        for (const auto& fix : sourceFixes) {
            if (fix.empty()) {
                continue;
            }
            if (targetFixSet->insert(fix).second) {
                targetOrderedFixes->push_back(fix);
            }
        }

        std::vector<std::string> remainingFixes;
        remainingFixes.reserve(targetFixSet->size());
        for (const auto& fix : *targetFixSet) {
            if (std::find(
                    targetOrderedFixes->begin(),
                    targetOrderedFixes->end(),
                    fix) == targetOrderedFixes->end()) {
                remainingFixes.push_back(fix);
            }
        }
        std::sort(remainingFixes.begin(), remainingFixes.end());
        for (const auto& fix : remainingFixes) {
            targetOrderedFixes->push_back(fix);
        }
    };
    const auto appendAirportProcedures =
        [&](const std::string& airportIcao, bool fromDepartureAirport) {
        const auto& cachedProcedureMetadata =
            GetCachedAirportProcedureMetadata(airportIcao);
        for (const auto& [procedureName, metadata] : cachedProcedureMetadata) {
            auto& entry = proceduresByName[procedureName];
            entry.hasSid = entry.hasSid || metadata.hasSid;
            entry.hasStar = entry.hasStar || metadata.hasStar;
            entry.sourcedFromDepartureAirport =
                entry.sourcedFromDepartureAirport ||
                (fromDepartureAirport || metadata.sourcedFromDepartureAirport);
            entry.sourcedFromArrivalAirport =
                entry.sourcedFromArrivalAirport ||
                (!fromDepartureAirport || metadata.sourcedFromArrivalAirport);
            entry.sidAuthoritySources.insert(
                metadata.sidAuthoritySources.begin(),
                metadata.sidAuthoritySources.end());
            entry.starAuthoritySources.insert(
                metadata.starAuthoritySources.begin(),
                metadata.starAuthoritySources.end());
            entry.sidRunwayTransitions.insert(
                metadata.sidRunwayTransitions.begin(),
                metadata.sidRunwayTransitions.end());
            entry.starRunwayTransitions.insert(
                metadata.starRunwayTransitions.begin(),
                metadata.starRunwayTransitions.end());
            mergeOrderedFixes(
                metadata.sidOrderedFixes,
                &entry.sidFixes,
                &entry.sidOrderedFixes);
            mergeOrderedFixes(
                metadata.starOrderedFixes,
                &entry.starFixes,
                &entry.starOrderedFixes);
            for (const auto& fix : metadata.sidFixes) {
                if (entry.sidFixes.insert(fix).second) {
                    entry.sidOrderedFixes.push_back(fix);
                }
            }
            for (const auto& fix : metadata.starFixes) {
                if (entry.starFixes.insert(fix).second) {
                    entry.starOrderedFixes.push_back(fix);
                }
            }
            entry.sidTransitions.insert(
                metadata.sidTransitions.begin(),
                metadata.sidTransitions.end());
            entry.starTransitions.insert(
                metadata.starTransitions.begin(),
                metadata.starTransitions.end());
        }
    };

    appendAirportProcedures(networkPlanSnapshot.departureIcao, true);
    appendAirportProcedures(networkPlanSnapshot.destinationIcao, false);
    return proceduresByName;
}

std::string LoadAirwayDataPayload() {
    const auto root = GetXPlaneRootPath();

    static constexpr const char* kRelativePaths[] = {
        "Custom Data\\earth_awy.dat",
        "Resources\\default data\\earth_awy.dat",
    };

    for (const auto* relativePath : kRelativePaths) {
        const auto payload = ReadTextFile(root + relativePath);
        if (!payload.empty()) {
            return payload;
        }
    }

    return {};
}

std::string LoadFixDataPayload() {
    const auto root = GetXPlaneRootPath();

    static constexpr const char* kRelativePaths[] = {
        "Custom Data\\earth_fix.dat",
        "Resources\\default data\\earth_fix.dat",
    };

    for (const auto* relativePath : kRelativePaths) {
        const auto payload = ReadTextFile(root + relativePath);
        if (!payload.empty()) {
            return payload;
        }
    }

    return {};
}

std::string LoadNavDataPayload() {
    const auto root = GetXPlaneRootPath();

    static constexpr const char* kRelativePaths[] = {
        "Custom Data\\earth_nav.dat",
        "Resources\\default data\\earth_nav.dat",
    };

    for (const auto* relativePath : kRelativePaths) {
        const auto payload = ReadTextFile(root + relativePath);
        if (!payload.empty()) {
            return payload;
        }
    }

    return {};
}

std::string NormalizeRouteToken(const std::string& token) {
    std::string normalized;
    normalized.reserve(token.size());
    for (const auto character : token) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0) {
            normalized.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(character))));
        }
    }
    return normalized;
}

std::string ExtractRouteTokenBase(const std::string& token) {
    const auto annotationSeparator = token.find('/');
    return NormalizeRouteToken(
        annotationSeparator == std::string::npos
            ? token
            : token.substr(0, annotationSeparator));
}

bool IsRouteControlToken(const std::string& token) {
    return token.empty() || token == "DCT";
}

bool ParseCoordinateComponent(
    const std::string& digits,
    int degreeDigits,
    double* outValueDeg) {
    if (outValueDeg == nullptr) {
        return false;
    }
    if (digits.size() != static_cast<std::size_t>(degreeDigits) &&
        digits.size() != static_cast<std::size_t>(degreeDigits + 2)) {
        return false;
    }

    const auto degreesText = digits.substr(0, degreeDigits);
    const auto minutesText =
        digits.size() == static_cast<std::size_t>(degreeDigits + 2)
            ? digits.substr(degreeDigits, 2)
            : std::string{};

    for (const auto character : degreesText) {
        if (std::isdigit(static_cast<unsigned char>(character)) == 0) {
            return false;
        }
    }
    for (const auto character : minutesText) {
        if (std::isdigit(static_cast<unsigned char>(character)) == 0) {
            return false;
        }
    }

    const auto degrees = std::stoi(degreesText);
    const auto minutes = minutesText.empty() ? 0 : std::stoi(minutesText);
    if (minutes >= 60) {
        return false;
    }

    *outValueDeg = static_cast<double>(degrees) +
                   static_cast<double>(minutes) / 60.0;
    return true;
}

bool ResolveCoordinateToken(
    const std::string& token,
    double* outLatitudeDeg,
    double* outLongitudeDeg) {
    if (token.size() < 7) {
        return false;
    }

    const auto latitudeHemisphereIndex = token.find_first_of("NS");
    if (latitudeHemisphereIndex == std::string::npos ||
        latitudeHemisphereIndex == 0 ||
        latitudeHemisphereIndex >= token.size() - 2) {
        return false;
    }

    const auto longitudeHemisphereIndex =
        token.find_first_of("EW", latitudeHemisphereIndex + 2);
    if (longitudeHemisphereIndex == std::string::npos ||
        longitudeHemisphereIndex != token.size() - 1 ||
        longitudeHemisphereIndex <= latitudeHemisphereIndex + 1) {
        return false;
    }

    const auto latitudeDigits = token.substr(0, latitudeHemisphereIndex);
    const auto longitudeDigits = token.substr(
        latitudeHemisphereIndex + 1,
        longitudeHemisphereIndex - latitudeHemisphereIndex - 1);
    if ((latitudeDigits.size() != 2 && latitudeDigits.size() != 4) ||
        (longitudeDigits.size() != 3 && longitudeDigits.size() != 5)) {
        return false;
    }

    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    if (!ParseCoordinateComponent(latitudeDigits, 2, &latitudeDeg) ||
        !ParseCoordinateComponent(longitudeDigits, 3, &longitudeDeg)) {
        return false;
    }

    const auto latitudeHemisphere = token[latitudeHemisphereIndex];
    const auto longitudeHemisphere = token[longitudeHemisphereIndex];
    if (latitudeHemisphere == 'S') {
        latitudeDeg = -latitudeDeg;
    } else if (latitudeHemisphere != 'N') {
        return false;
    }

    if (longitudeHemisphere == 'W') {
        longitudeDeg = -longitudeDeg;
    } else if (longitudeHemisphere != 'E') {
        return false;
    }

    if (std::fabs(latitudeDeg) > 90.0 || std::fabs(longitudeDeg) > 180.0) {
        return false;
    }

    if (outLatitudeDeg != nullptr) {
        *outLatitudeDeg = latitudeDeg;
    }
    if (outLongitudeDeg != nullptr) {
        *outLongitudeDeg = longitudeDeg;
    }
    return true;
}

std::vector<std::string> SplitAirwayNames(const std::string& airwayNamesToken) {
    std::vector<std::string> airwayNames;
    std::string current;
    for (const auto character : airwayNamesToken) {
        if (character == '-') {
            const auto normalized = NormalizeRouteToken(current);
            if (!normalized.empty()) {
                airwayNames.push_back(normalized);
            }
            current.clear();
            continue;
        }
        current.push_back(character);
    }

    const auto normalized = NormalizeRouteToken(current);
    if (!normalized.empty()) {
        airwayNames.push_back(normalized);
    }
    return airwayNames;
}

std::string BuildExactNavNodeKey(
    const std::string& ident,
    const std::string& region,
    int navDataType) {
    return ident + "|" + region + "|" + std::to_string(navDataType);
}

std::string BuildExactNavNodeKey(const AirwayNode& node) {
    return BuildExactNavNodeKey(node.ident, node.region, node.navDataType);
}

void AddGraphNode(
    const std::string& ident,
    const std::string& region,
    int navDataType,
    double latitudeDeg,
    double longitudeDeg,
    AirwayGraph* graph) {
    if (graph == nullptr || ident.empty()) {
        return;
    }

    const auto exactKey = BuildExactNavNodeKey(ident, region, navDataType);
    if (graph->nodeIndexByExactKey.find(exactKey) != graph->nodeIndexByExactKey.end()) {
        return;
    }

    const auto nodeIndex = graph->nodes.size();
    graph->nodes.push_back({
        ident,
        region,
        latitudeDeg,
        longitudeDeg,
        navDataType,
    });
    graph->nodeIndicesByIdent[ident].push_back(nodeIndex);
    graph->nodeIndexByExactKey[exactKey] = nodeIndex;
}

void LoadFixNodes(AirwayGraph* graph) {
    if (graph == nullptr) {
        return;
    }

    std::istringstream stream(LoadFixDataPayload());
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == 'I') {
            continue;
        }

        std::istringstream lineStream(line);
        std::vector<std::string> fields;
        std::string field;
        while (lineStream >> field) {
            fields.push_back(field);
        }

        if (fields.size() < 5) {
            continue;
        }

        try {
            const auto latitudeDeg = std::stod(fields[0]);
            const auto longitudeDeg = std::stod(fields[1]);
            const auto ident = NormalizeRouteToken(fields[2]);
            const auto region = NormalizeRouteToken(fields[4]);
            AddGraphNode(ident, region, 11, latitudeDeg, longitudeDeg, graph);
        } catch (...) {
            continue;
        }
    }
}

bool IsSupportedNavDataType(int navDataType) {
    return navDataType == 2 || navDataType == 3;
}

void LoadNavNodes(AirwayGraph* graph) {
    if (graph == nullptr) {
        return;
    }

    std::istringstream stream(LoadNavDataPayload());
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == 'I') {
            continue;
        }

        std::istringstream lineStream(line);
        std::vector<std::string> fields;
        std::string field;
        while (lineStream >> field) {
            fields.push_back(field);
        }

        if (fields.size() < 10) {
            continue;
        }

        try {
            const auto navDataType = std::stoi(fields[0]);
            if (!IsSupportedNavDataType(navDataType)) {
                continue;
            }

            const auto latitudeDeg = std::stod(fields[1]);
            const auto longitudeDeg = std::stod(fields[2]);
            const auto ident = NormalizeRouteToken(fields[7]);
            const auto region = NormalizeRouteToken(fields[9]);
            AddGraphNode(ident, region, navDataType, latitudeDeg, longitudeDeg, graph);
        } catch (...) {
            continue;
        }
    }
}

std::optional<std::size_t> FindExactGraphNodeIndex(
    const AirwayGraph& graph,
    const std::string& ident,
    const std::string& region,
    int navDataType) {
    const auto exactKey = BuildExactNavNodeKey(ident, region, navDataType);
    const auto it = graph.nodeIndexByExactKey.find(exactKey);
    if (it == graph.nodeIndexByExactKey.end()) {
        return std::nullopt;
    }
    return it->second;
}

const AirwayGraph& GetAirwayGraph() {
    static std::mutex cacheMutex;
    static bool cacheInitialized = false;
    static AirwayGraph cache;

    std::lock_guard<std::mutex> lock(cacheMutex);
    if (cacheInitialized) {
        return cache;
    }

    LoadFixNodes(&cache);
    LoadNavNodes(&cache);

    std::istringstream stream(LoadAirwayDataPayload());
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == 'I') {
            continue;
        }

        std::istringstream lineStream(line);
        std::vector<std::string> fields;
        std::string field;
        while (lineStream >> field) {
            fields.push_back(field);
        }

        if (fields.size() < 11) {
            continue;
        }

        const auto startIdent = NormalizeRouteToken(fields[0]);
        const auto startRegion = NormalizeRouteToken(fields[1]);
        const auto endIdent = NormalizeRouteToken(fields[3]);
        const auto endRegion = NormalizeRouteToken(fields[4]);
        const auto airwayNames = SplitAirwayNames(fields.back());
        if (startIdent.empty() || endIdent.empty() || airwayNames.empty()) {
            continue;
        }

        int startNavDataType = 0;
        int endNavDataType = 0;
        try {
            startNavDataType = std::stoi(fields[2]);
            endNavDataType = std::stoi(fields[5]);
        } catch (...) {
            continue;
        }

        const auto startNodeIndex = FindExactGraphNodeIndex(
            cache,
            startIdent,
            startRegion,
            startNavDataType);
        const auto endNodeIndex = FindExactGraphNodeIndex(
            cache,
            endIdent,
            endRegion,
            endNavDataType);
        if (!startNodeIndex.has_value() || !endNodeIndex.has_value()) {
            continue;
        }

        const auto edgeDistanceNm = GreatCircleDistanceNm(
            cache.nodes[*startNodeIndex].latitudeDeg,
            cache.nodes[*startNodeIndex].longitudeDeg,
            cache.nodes[*endNodeIndex].latitudeDeg,
            cache.nodes[*endNodeIndex].longitudeDeg);

        const auto direction = fields[6];
        const auto addForward = direction != "B";
        const auto addBackward = direction == "N" || direction == "B";

        for (const auto& airwayName : airwayNames) {
            auto& adjacency = cache.adjacencyByAirway[airwayName];
            if (addForward) {
                adjacency[*startNodeIndex].push_back({*endNodeIndex, edgeDistanceNm});
            }
            if (addBackward) {
                adjacency[*endNodeIndex].push_back({*startNodeIndex, edgeDistanceNm});
            }
        }
    }

    cacheInitialized = true;
    return cache;
}

xvatsim::core::route::RouteGrammarCatalog BuildRouteGrammarCatalog(
    const AirwayGraph& graph,
    const std::unordered_map<std::string, xvatsim::core::route::ProcedureCatalogEntry>*
        proceduresByName = nullptr) {
    xvatsim::core::route::RouteGrammarCatalog catalog;
    catalog.pointIdents.reserve(graph.nodeIndicesByIdent.size());
    for (const auto& [ident, _] : graph.nodeIndicesByIdent) {
        if (!ident.empty()) {
            catalog.pointIdents.insert(ident);
        }
    }

    catalog.airwayNames.reserve(graph.adjacencyByAirway.size());
    for (const auto& [airwayName, _] : graph.adjacencyByAirway) {
        if (!airwayName.empty()) {
            catalog.airwayNames.insert(airwayName);
        }
    }

    if (proceduresByName != nullptr) {
        catalog.proceduresByName = *proceduresByName;
        catalog.procedureNames.reserve(proceduresByName->size());
        for (const auto& [procedureName, _] : *proceduresByName) {
            if (!procedureName.empty()) {
                catalog.procedureNames.insert(procedureName);
            }
        }
    }

    return catalog;
}

bool ResolveRoutePointCandidates(
    const std::string& token,
    std::vector<ResolvedRoutePoint>* outCandidates) {
    if (outCandidates == nullptr || token.empty()) {
        return false;
    }

    outCandidates->clear();

    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    if (ResolveCoordinateToken(token, &latitudeDeg, &longitudeDeg)) {
        outCandidates->push_back({
            token,
            latitudeDeg,
            longitudeDeg,
            std::nullopt,
        });
        return true;
    }

    const auto& graph = GetAirwayGraph();
    const auto nodeIndicesIt = graph.nodeIndicesByIdent.find(token);
    if (nodeIndicesIt == graph.nodeIndicesByIdent.end() ||
        nodeIndicesIt->second.empty()) {
        return false;
    }

    outCandidates->reserve(nodeIndicesIt->second.size());
    for (const auto nodeIndex : nodeIndicesIt->second) {
        const auto& node = graph.nodes[nodeIndex];
        outCandidates->push_back({
            node.ident,
            node.latitudeDeg,
            node.longitudeDeg,
            nodeIndex,
        });
    }

    return !outCandidates->empty();
}

std::string BuildResolvedRoutePointTieBreakKey(const ResolvedRoutePoint& point) {
    return point.ident + "|" +
           std::to_string(point.latitudeDeg) + "|" +
           std::to_string(point.longitudeDeg);
}

bool ResolveRoutePointTokenWithRouteContext(
    const std::string& token,
    const std::optional<GeoPoint>& referencePoint,
    const std::optional<std::string>& nextPointToken,
    const std::optional<GeoPoint>& destinationPoint,
    ResolvedRoutePoint* outPoint) {
    if (outPoint == nullptr || token.empty()) {
        return false;
    }

    std::vector<ResolvedRoutePoint> candidates;
    if (!ResolveRoutePointCandidates(token, &candidates)) {
        return false;
    }

    if (candidates.size() == 1 && !nextPointToken.has_value() &&
        !destinationPoint.has_value()) {
        *outPoint = candidates.front();
        return true;
    }

    std::vector<ResolvedRoutePoint> nextCandidates;
    if (nextPointToken.has_value() && !nextPointToken->empty()) {
        ResolveRoutePointCandidates(*nextPointToken, &nextCandidates);
    }

    const auto& graph = GetAirwayGraph();
    std::optional<std::string> uniqueNextRegion;
    if (!nextCandidates.empty()) {
        std::unordered_set<std::string> nextRegions;
        for (const auto& nextCandidate : nextCandidates) {
            if (!nextCandidate.graphNodeIndex.has_value()) {
                continue;
            }

            const auto& region = graph.nodes[*nextCandidate.graphNodeIndex].region;
            if (!region.empty()) {
                nextRegions.insert(region);
            }
        }
        if (nextRegions.size() == 1) {
            uniqueNextRegion = *nextRegions.begin();
        }
    }

    const auto matchesUniqueNextRegion = [&](const ResolvedRoutePoint& candidate) {
        return uniqueNextRegion.has_value() &&
               candidate.graphNodeIndex.has_value() &&
               graph.nodes[*candidate.graphNodeIndex].region == *uniqueNextRegion;
    };

    const auto scoreCandidate = [&](const ResolvedRoutePoint& candidate) {
        double scoreNm = 0.0;
        if (referencePoint.has_value()) {
            scoreNm += GreatCircleDistanceNm(
                referencePoint->latitudeDeg,
                referencePoint->longitudeDeg,
                candidate.latitudeDeg,
                candidate.longitudeDeg);
        }

        if (!nextCandidates.empty()) {
            double bestNextDistanceNm = std::numeric_limits<double>::max();
            for (const auto& nextCandidate : nextCandidates) {
                bestNextDistanceNm = std::min(
                    bestNextDistanceNm,
                    GreatCircleDistanceNm(
                        candidate.latitudeDeg,
                        candidate.longitudeDeg,
                        nextCandidate.latitudeDeg,
                        nextCandidate.longitudeDeg));
            }
            scoreNm += bestNextDistanceNm;
        } else if (destinationPoint.has_value()) {
            scoreNm += GreatCircleDistanceNm(
                candidate.latitudeDeg,
                candidate.longitudeDeg,
                destinationPoint->latitudeDeg,
                destinationPoint->longitudeDeg);
        }

        return scoreNm;
    };

    const auto bestCandidateIt = std::min_element(
        candidates.begin(),
        candidates.end(),
        [&](const ResolvedRoutePoint& left, const ResolvedRoutePoint& right) {
            const auto leftRegionMatch = matchesUniqueNextRegion(left);
            const auto rightRegionMatch = matchesUniqueNextRegion(right);
            if (leftRegionMatch != rightRegionMatch) {
                return leftRegionMatch;
            }

            const auto leftScoreNm = scoreCandidate(left);
            const auto rightScoreNm = scoreCandidate(right);
            if (std::fabs(leftScoreNm - rightScoreNm) > 1e-6) {
                return leftScoreNm < rightScoreNm;
            }
            return BuildResolvedRoutePointTieBreakKey(left) <
                   BuildResolvedRoutePointTieBreakKey(right);
        });

    if (bestCandidateIt == candidates.end()) {
        return false;
    }

    *outPoint = *bestCandidateIt;
    return true;
}

bool FindShortestAirwayDistanceFromNode(
    std::size_t startNodeIndex,
    const std::unordered_set<std::size_t>& endNodeIndices,
    const std::string& airwayToken,
    double* outDistanceNm) {
    if (outDistanceNm == nullptr || endNodeIndices.empty()) {
        return false;
    }

    const auto& graph = GetAirwayGraph();
    if (startNodeIndex >= graph.nodes.size()) {
        return false;
    }

    const auto adjacencyIt = graph.adjacencyByAirway.find(airwayToken);
    if (adjacencyIt == graph.adjacencyByAirway.end()) {
        return false;
    }

    const auto& adjacency = adjacencyIt->second;
    if (adjacency.find(startNodeIndex) == adjacency.end()) {
        return false;
    }

    std::vector<double> bestDistanceByNode(
        graph.nodes.size(),
        std::numeric_limits<double>::max());

    struct QueueEntry {
        double distanceNm = 0.0;
        std::size_t nodeIndex = 0;
        bool operator>(const QueueEntry& other) const {
            return distanceNm > other.distanceNm;
        }
    };

    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> queue;
    bestDistanceByNode[startNodeIndex] = 0.0;
    queue.push({0.0, startNodeIndex});

    while (!queue.empty()) {
        const auto current = queue.top();
        queue.pop();
        if (current.distanceNm > bestDistanceByNode[current.nodeIndex]) {
            continue;
        }

        if (current.nodeIndex != startNodeIndex &&
            endNodeIndices.find(current.nodeIndex) != endNodeIndices.end()) {
            *outDistanceNm = current.distanceNm;
            return true;
        }

        const auto edgeListIt = adjacency.find(current.nodeIndex);
        if (edgeListIt == adjacency.end()) {
            continue;
        }

        for (const auto& edge : edgeListIt->second) {
            const auto candidateDistanceNm = current.distanceNm + edge.distanceNm;
            if (candidateDistanceNm >= bestDistanceByNode[edge.toNodeIndex]) {
                continue;
            }

            bestDistanceByNode[edge.toNodeIndex] = candidateDistanceNm;
            queue.push({candidateDistanceNm, edge.toNodeIndex});
        }
    }

    return false;
}

bool ResolveRoutePointTokenWithAirwayEntryContext(
    const std::string& token,
    const std::optional<GeoPoint>& referencePoint,
    const std::string& airwayToken,
    const std::string& airwayEndToken,
    ResolvedRoutePoint* outPoint) {
    if (outPoint == nullptr ||
        token.empty() ||
        airwayToken.empty() ||
        airwayEndToken.empty()) {
        return false;
    }

    std::vector<ResolvedRoutePoint> candidates;
    if (!ResolveRoutePointCandidates(token, &candidates)) {
        return false;
    }

    std::vector<ResolvedRoutePoint> endCandidates;
    if (!ResolveRoutePointCandidates(airwayEndToken, &endCandidates)) {
        return false;
    }

    std::unordered_set<std::size_t> endNodeIndices;
    for (const auto& endCandidate : endCandidates) {
        if (endCandidate.graphNodeIndex.has_value()) {
            endNodeIndices.insert(*endCandidate.graphNodeIndex);
        }
    }
    if (endNodeIndices.empty()) {
        return false;
    }

    std::optional<ResolvedRoutePoint> bestCandidate;
    double bestScoreNm = std::numeric_limits<double>::max();
    for (const auto& candidate : candidates) {
        if (!candidate.graphNodeIndex.has_value()) {
            continue;
        }

        double airwayDistanceNm = 0.0;
        if (!FindShortestAirwayDistanceFromNode(
                *candidate.graphNodeIndex,
                endNodeIndices,
                airwayToken,
                &airwayDistanceNm)) {
            continue;
        }

        double scoreNm = airwayDistanceNm;
        if (referencePoint.has_value()) {
            scoreNm += GreatCircleDistanceNm(
                referencePoint->latitudeDeg,
                referencePoint->longitudeDeg,
                candidate.latitudeDeg,
                candidate.longitudeDeg);
        }

        if (!bestCandidate.has_value() ||
            scoreNm + 1e-6 < bestScoreNm ||
            (std::fabs(scoreNm - bestScoreNm) <= 1e-6 &&
             BuildResolvedRoutePointTieBreakKey(candidate) <
                 BuildResolvedRoutePointTieBreakKey(*bestCandidate))) {
            bestScoreNm = scoreNm;
            bestCandidate = candidate;
        }
    }

    if (!bestCandidate.has_value()) {
        return false;
    }

    *outPoint = *bestCandidate;
    return true;
}

bool ExpandAirwaySegment(
    const ResolvedRoutePoint& startWaypoint,
    const std::string& airwayToken,
    const ResolvedRoutePoint& endWaypoint,
    std::vector<ResolvedRoutePoint>* outExpandedSegment,
    ResolvedRoutePoint* outResolvedEndWaypoint) {
    if (outExpandedSegment == nullptr) {
        return false;
    }

    outExpandedSegment->clear();
    if (startWaypoint.ident.empty() || endWaypoint.ident.empty()) {
        return false;
    }

    const auto& graph = GetAirwayGraph();
    const auto adjacencyIt = graph.adjacencyByAirway.find(airwayToken);
    if (adjacencyIt == graph.adjacencyByAirway.end()) {
        return false;
    }

    const auto startNodesIt = graph.nodeIndicesByIdent.find(startWaypoint.ident);
    const auto endNodesIt = graph.nodeIndicesByIdent.find(endWaypoint.ident);
    if (startNodesIt == graph.nodeIndicesByIdent.end() || endNodesIt == graph.nodeIndicesByIdent.end()) {
        return false;
    }

    const auto& adjacency = adjacencyIt->second;
    std::vector<std::size_t> startNodes;
    if (startWaypoint.graphNodeIndex.has_value()) {
        if (adjacency.find(*startWaypoint.graphNodeIndex) == adjacency.end()) {
            return false;
        }
        startNodes.push_back(*startWaypoint.graphNodeIndex);
    } else {
        for (const auto nodeIndex : startNodesIt->second) {
            if (adjacency.find(nodeIndex) != adjacency.end()) {
                startNodes.push_back(nodeIndex);
            }
        }
    }

    std::unordered_set<std::size_t> endNodes;
    for (const auto nodeIndex : endNodesIt->second) {
        endNodes.insert(nodeIndex);
    }

    if (startNodes.empty() || endNodes.empty()) {
        return false;
    }

    const auto nodeCount = graph.nodes.size();
    std::vector<double> bestDistanceByNode(nodeCount, std::numeric_limits<double>::max());
    std::vector<std::size_t> previousNodeByNode(nodeCount, std::numeric_limits<std::size_t>::max());

    struct QueueEntry {
        double distanceNm = 0.0;
        std::size_t nodeIndex = 0;
        bool operator>(const QueueEntry& other) const {
            return distanceNm > other.distanceNm;
        }
    };

    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> queue;
    for (const auto startNodeIndex : startNodes) {
        const auto& node = graph.nodes[startNodeIndex];
        const auto anchorDistanceNm = GreatCircleDistanceNm(
            startWaypoint.latitudeDeg,
            startWaypoint.longitudeDeg,
            node.latitudeDeg,
            node.longitudeDeg);
        if (anchorDistanceNm < bestDistanceByNode[startNodeIndex]) {
            bestDistanceByNode[startNodeIndex] = anchorDistanceNm;
            queue.push({anchorDistanceNm, startNodeIndex});
        }
    }

    std::size_t bestEndNodeIndex = std::numeric_limits<std::size_t>::max();
    double bestEndDistanceNm = std::numeric_limits<double>::max();
    while (!queue.empty()) {
        const auto current = queue.top();
        queue.pop();
        if (current.distanceNm > bestDistanceByNode[current.nodeIndex]) {
            continue;
        }

        if (endNodes.find(current.nodeIndex) != endNodes.end()) {
            const auto candidateDistanceNm = current.distanceNm;
            if (candidateDistanceNm + 1e-6 < bestEndDistanceNm ||
                (std::fabs(candidateDistanceNm - bestEndDistanceNm) <= 1e-6 &&
                 (bestEndNodeIndex == std::numeric_limits<std::size_t>::max() ||
                  BuildExactNavNodeKey(graph.nodes[current.nodeIndex]) <
                      BuildExactNavNodeKey(graph.nodes[bestEndNodeIndex])))) {
                bestEndDistanceNm = candidateDistanceNm;
                bestEndNodeIndex = current.nodeIndex;
            }
        }

        const auto edgeListIt = adjacency.find(current.nodeIndex);
        if (edgeListIt == adjacency.end()) {
            continue;
        }

        for (const auto& edge : edgeListIt->second) {
            const auto candidateDistanceNm = current.distanceNm + edge.distanceNm;
            if (candidateDistanceNm >= bestDistanceByNode[edge.toNodeIndex]) {
                continue;
            }

            bestDistanceByNode[edge.toNodeIndex] = candidateDistanceNm;
            previousNodeByNode[edge.toNodeIndex] = current.nodeIndex;
            queue.push({candidateDistanceNm, edge.toNodeIndex});
        }
    }

    if (bestEndNodeIndex == std::numeric_limits<std::size_t>::max()) {
        return false;
    }

    std::vector<std::size_t> nodePath;
    for (auto nodeIndex = bestEndNodeIndex;
         nodeIndex != std::numeric_limits<std::size_t>::max();
         nodeIndex = previousNodeByNode[nodeIndex]) {
        nodePath.push_back(nodeIndex);
    }
    std::reverse(nodePath.begin(), nodePath.end());
    if (nodePath.size() < 2) {
        return false;
    }

    for (std::size_t index = 1; index + 1 < nodePath.size(); ++index) {
        const auto& node = graph.nodes[nodePath[index]];
        outExpandedSegment->push_back({
            node.ident,
            node.latitudeDeg,
            node.longitudeDeg,
            nodePath[index],
        });
    }

    if (outResolvedEndWaypoint != nullptr) {
        const auto& node = graph.nodes[bestEndNodeIndex];
        *outResolvedEndWaypoint = {
            node.ident,
            node.latitudeDeg,
            node.longitudeDeg,
            bestEndNodeIndex,
        };
    }

    return true;
}

std::vector<brain::RouteWaypointSnapshot> CompactRouteWaypoints(
    const std::vector<brain::RouteWaypointSnapshot>& waypoints) {
    std::vector<brain::RouteWaypointSnapshot> compacted;
    compacted.reserve(waypoints.size());
    for (const auto& waypoint : waypoints) {
        if (!compacted.empty()) {
            const auto distanceNm = GreatCircleDistanceNm(
                compacted.back().latitudeDeg,
                compacted.back().longitudeDeg,
                waypoint.latitudeDeg,
                waypoint.longitudeDeg);
            if (distanceNm < 1.0) {
                continue;
            }
        }
        compacted.push_back(waypoint);
    }
    return compacted;
}

double DistanceFromPointToRouteSegmentNm(
    const GeoPoint& point,
    const brain::RouteWaypointSnapshot& segmentStart,
    const brain::RouteWaypointSnapshot& segmentEnd) {
    const GeoPoint startPoint{segmentStart.latitudeDeg, segmentStart.longitudeDeg};
    const GeoPoint endPoint{segmentEnd.latitudeDeg, segmentEnd.longitudeDeg};
    const auto startVector = ToUnitVector(startPoint);
    const auto endVector = ToUnitVector(endPoint);
    const auto pointVector = ToUnitVector(point);

    const auto segmentAngularDistanceRad =
        AngularDistanceRad(startVector, endVector);
    if (segmentAngularDistanceRad <= 1e-10) {
        return GreatCircleDistanceNm(
            point.latitudeDeg,
            point.longitudeDeg,
            startPoint.latitudeDeg,
            startPoint.longitudeDeg);
    }

    const auto normal = NormalizeVector(CrossProduct(startVector, endVector));
    if (!normal.has_value()) {
        const auto startDistanceNm = GreatCircleDistanceNm(
            point.latitudeDeg,
            point.longitudeDeg,
            startPoint.latitudeDeg,
            startPoint.longitudeDeg);
        const auto endDistanceNm = GreatCircleDistanceNm(
            point.latitudeDeg,
            point.longitudeDeg,
            endPoint.latitudeDeg,
            endPoint.longitudeDeg);
        return std::min(startDistanceNm, endDistanceNm);
    }

    const auto projectedVector = NormalizeVector(AddVector(
        pointVector,
        ScaleVector(*normal, -DotProduct(pointVector, *normal))));
    if (projectedVector.has_value()) {
        const auto startToProjectionRad =
            AngularDistanceRad(startVector, *projectedVector);
        const auto projectionToEndRad =
            AngularDistanceRad(*projectedVector, endVector);
        const auto arcToleranceRad =
            std::max(1e-7, segmentAngularDistanceRad * 1e-6);
        const auto projectionIsOnSegment =
            std::fabs(
                (startToProjectionRad + projectionToEndRad) -
                segmentAngularDistanceRad) <= arcToleranceRad;
        if (projectionIsOnSegment) {
            return kEarthRadiusNm *
                   AngularDistanceRad(pointVector, *projectedVector);
        }
    }

    const auto startDistanceNm = GreatCircleDistanceNm(
        point.latitudeDeg,
        point.longitudeDeg,
        startPoint.latitudeDeg,
        startPoint.longitudeDeg);
    const auto endDistanceNm = GreatCircleDistanceNm(
        point.latitudeDeg,
        point.longitudeDeg,
        endPoint.latitudeDeg,
        endPoint.longitudeDeg);
    return std::min(startDistanceNm, endDistanceNm);
}

bool TokenCanActAsPoint(const xvatsim::core::route::ParsedRouteToken& token) {
    using xvatsim::core::route::RouteTokenKind;
    return token.kind == RouteTokenKind::Point ||
           token.kind == RouteTokenKind::Coordinate ||
           (token.kind == RouteTokenKind::Ambiguous && token.matchesPointCatalog);
}

bool TokenCanActAsAirway(const xvatsim::core::route::ParsedRouteToken& token) {
    using xvatsim::core::route::RouteTokenKind;
    return token.kind == RouteTokenKind::Airway ||
           (token.kind == RouteTokenKind::Ambiguous && token.matchesAirwayCatalog);
}

std::optional<std::size_t> FindNextAnchorTokenIndex(
    const std::vector<xvatsim::core::route::ParsedRouteToken>& parsedTokens,
    std::size_t startIndex,
    RouteResolveDiagnostics* diagnostics) {
    using xvatsim::core::route::RouteTokenKind;

    for (std::size_t index = startIndex; index < parsedTokens.size(); ++index) {
        const auto& token = parsedTokens[index];
        if (token.kind == RouteTokenKind::Control ||
            token.kind == RouteTokenKind::Empty) {
            continue;
        }
        if (token.kind == RouteTokenKind::Procedure) {
            continue;
        }
        if (token.kind == RouteTokenKind::Unknown) {
            if (diagnostics != nullptr) {
                diagnostics->unsupportedTokens.push_back(token.normalized);
            }
            continue;
        }
        if (TokenCanActAsPoint(token)) {
            return index;
        }
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<std::size_t> FindPreviousAnchorTokenIndex(
    const std::vector<xvatsim::core::route::ParsedRouteToken>& parsedTokens,
    std::size_t startIndex,
    RouteResolveDiagnostics* diagnostics) {
    using xvatsim::core::route::RouteTokenKind;

    if (startIndex == 0 || parsedTokens.empty()) {
        return std::nullopt;
    }

    for (std::size_t index = startIndex; index-- > 0;) {
        const auto& token = parsedTokens[index];
        if (token.kind == RouteTokenKind::Control ||
            token.kind == RouteTokenKind::Empty) {
            continue;
        }
        if (token.kind == RouteTokenKind::Procedure) {
            continue;
        }
        if (token.kind == RouteTokenKind::Unknown) {
            if (diagnostics != nullptr) {
                diagnostics->unsupportedTokens.push_back(token.normalized);
            }
            continue;
        }
        if (TokenCanActAsPoint(token)) {
            return index;
        }
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<std::size_t> FindNextMeaningfulTokenIndex(
    const std::vector<xvatsim::core::route::ParsedRouteToken>& parsedTokens,
    std::size_t startIndex) {
    using xvatsim::core::route::RouteTokenKind;

    for (std::size_t index = startIndex; index < parsedTokens.size(); ++index) {
        const auto& token = parsedTokens[index];
        if (token.kind == RouteTokenKind::Control ||
            token.kind == RouteTokenKind::Empty) {
            continue;
        }
        return index;
    }

    return std::nullopt;
}

void AppendProcedureTransitionLink(
    std::string_view procedureSide,
    const std::string& procedureName,
    const std::string& transitionName,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty() ||
        transitionName.empty()) {
        return;
    }

    diagnostics->procedureTransitionLinks.push_back(
        std::string(procedureSide) + ":" + procedureName + ":" + transitionName);
}

void AppendProcedureTransitionMiss(
    std::string_view procedureSide,
    const std::string& procedureName,
    const std::string& transitionName,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty()) {
        return;
    }

    diagnostics->procedureTransitionMisses.push_back(
        std::string(procedureSide) + ":" + procedureName + ":" +
        (transitionName.empty() ? "<none>" : transitionName));
}

void AppendProcedureAnchorLink(
    std::string_view procedureSide,
    const std::string& procedureName,
    const std::string& anchorName,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty() ||
        anchorName.empty()) {
        return;
    }

    diagnostics->procedureAnchorLinks.push_back(
        std::string(procedureSide) + ":" + procedureName + ":" + anchorName);
}

void AppendProcedureContextOnly(
    std::string_view procedureSide,
    const std::string& procedureName,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty()) {
        return;
    }

    diagnostics->procedureContextOnlyTokens.push_back(
        std::string(procedureSide) + ":" + procedureName);
}

void RemoveProcedureContextOnly(
    std::string_view procedureSide,
    const std::string& procedureName,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty()) {
        return;
    }

    const auto target = std::string(procedureSide) + ":" + procedureName;
    diagnostics->procedureContextOnlyTokens.erase(
        std::remove(
            diagnostics->procedureContextOnlyTokens.begin(),
            diagnostics->procedureContextOnlyTokens.end(),
            target),
        diagnostics->procedureContextOnlyTokens.end());
}

void RemoveProcedureTransitionMisses(
    std::string_view procedureSide,
    const std::string& procedureName,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty()) {
        return;
    }

    const auto prefix = std::string(procedureSide) + ":" + procedureName + ":";
    diagnostics->procedureTransitionMisses.erase(
        std::remove_if(
            diagnostics->procedureTransitionMisses.begin(),
            diagnostics->procedureTransitionMisses.end(),
            [&](const std::string& value) {
                return value.rfind(prefix, 0) == 0;
            }),
        diagnostics->procedureTransitionMisses.end());
}

bool HasProcedureSyntheticWaypoint(
    std::string_view procedureSide,
    const std::string& procedureName,
    const RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty()) {
        return false;
    }

    const auto prefix = std::string(procedureSide) + ":" + procedureName + ":";
    return std::any_of(
        diagnostics->procedureSyntheticWaypoints.begin(),
        diagnostics->procedureSyntheticWaypoints.end(),
        [&](const std::string& value) {
            return value.rfind(prefix, 0) == 0;
        });
}

bool HasProcedureApplicationState(
    std::string_view procedureSide,
    const std::string& procedureName,
    const RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty()) {
        return false;
    }

    const auto prefix = std::string(procedureSide) + ":" + procedureName + ":";
    return std::any_of(
        diagnostics->procedureApplicationStates.begin(),
        diagnostics->procedureApplicationStates.end(),
        [&](const std::string& value) {
            return value.rfind(prefix, 0) == 0;
        });
}

bool HasAppliedProcedureApplicationState(
    std::string_view procedureSide,
    const std::string& procedureName,
    const RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty()) {
        return false;
    }

    const auto prefix =
        std::string(procedureSide) + ":" + procedureName + ":APPLIED";
    return std::any_of(
        diagnostics->procedureApplicationStates.begin(),
        diagnostics->procedureApplicationStates.end(),
        [&](const std::string& value) {
            return value.rfind(prefix, 0) == 0;
        });
}

void DeduplicatePreserveOrder(std::vector<std::string>* values) {
    if (values == nullptr) {
        return;
    }

    std::unordered_set<std::string> seen;
    std::vector<std::string> deduplicated;
    deduplicated.reserve(values->size());
    for (const auto& value : *values) {
        if (seen.insert(value).second) {
            deduplicated.push_back(value);
        }
    }
    *values = std::move(deduplicated);
}

void AppendProcedureRecordKind(
    std::string_view procedureSide,
    bool hasRunwayRecords,
    bool hasEnrouteTransitions,
    const std::string& procedureName,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty()) {
        return;
    }

    std::string recordKind = "BASE";
    if (hasRunwayRecords && hasEnrouteTransitions) {
        recordKind = "BOTH";
    } else if (hasRunwayRecords) {
        recordKind = "RUNWAY";
    } else if (hasEnrouteTransitions) {
        recordKind = "ENROUTE";
    }

    diagnostics->procedureRecordKinds.push_back(
        std::string(procedureSide) + ":" + recordKind + ":" + procedureName);
}

void AppendProcedureRunwayRecords(
    std::string_view procedureSide,
    const std::unordered_set<std::string>& runwayTokens,
    const std::string& procedureName,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty() ||
        runwayTokens.empty()) {
        return;
    }

    std::vector<std::string> sortedRunways(runwayTokens.begin(), runwayTokens.end());
    std::sort(sortedRunways.begin(), sortedRunways.end());
    for (const auto& runwayToken : sortedRunways) {
        diagnostics->procedureRunwayRecords.push_back(
            std::string(procedureSide) + ":" + procedureName + ":" + runwayToken);
    }
}

void AppendProcedureCatalogTransitions(
    std::string_view procedureSide,
    const std::unordered_set<std::string>& transitionTokens,
    const std::string& procedureName,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty() ||
        transitionTokens.empty()) {
        return;
    }

    std::vector<std::string> sortedTransitions(
        transitionTokens.begin(),
        transitionTokens.end());
    std::sort(sortedTransitions.begin(), sortedTransitions.end());
    for (const auto& transitionToken : sortedTransitions) {
        diagnostics->procedureCatalogTransitions.push_back(
            std::string(procedureSide) + ":" + procedureName + ":" + transitionToken);
    }
}

void AppendProcedureCatalogAuthorities(
    std::string_view procedureSide,
    const std::unordered_set<std::string>& authorityTokens,
    const std::string& procedureName,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty() ||
        authorityTokens.empty()) {
        return;
    }

    std::vector<std::string> sortedAuthorities(
        authorityTokens.begin(),
        authorityTokens.end());
    std::sort(sortedAuthorities.begin(), sortedAuthorities.end());
    for (const auto& authorityToken : sortedAuthorities) {
        diagnostics->procedureCatalogAuthorities.push_back(
            std::string(procedureSide) + ":" + procedureName + ":" + authorityToken);
    }
}

void AppendProcedureCatalogFixes(
    std::string_view procedureSide,
    const std::unordered_set<std::string>& fixTokens,
    const std::string& procedureName,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty() ||
        fixTokens.empty()) {
        return;
    }

    std::vector<std::string> sortedFixes(fixTokens.begin(), fixTokens.end());
    std::sort(sortedFixes.begin(), sortedFixes.end());
    for (const auto& fixToken : sortedFixes) {
        diagnostics->procedureCatalogFixes.push_back(
            std::string(procedureSide) + ":" + procedureName + ":" + fixToken);
    }
}

void AppendProcedureBoundaryFix(
    std::string_view procedureSide,
    const std::string* boundaryFix,
    const std::string& procedureName,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty() ||
        boundaryFix == nullptr ||
        boundaryFix->empty()) {
        return;
    }

    diagnostics->procedureBoundaryFixes.push_back(
        std::string(procedureSide) + ":" + procedureName + ":" + *boundaryFix);
}

void AppendProcedureOrderedFixes(
    std::string_view procedureSide,
    const std::vector<std::string>& orderedFixes,
    const std::string& procedureName,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty() ||
        orderedFixes.empty()) {
        return;
    }

    std::ostringstream stream;
    stream << procedureSide << ":" << procedureName << ":";
    for (std::size_t index = 0; index < orderedFixes.size(); ++index) {
        if (orderedFixes[index].empty()) {
            continue;
        }
        if (index > 0) {
            stream << ">";
        }
        stream << orderedFixes[index];
    }

    const auto encoded = stream.str();
    if (!encoded.empty() && encoded.back() != ':') {
        diagnostics->procedureOrderedFixes.push_back(encoded);
    }
}

void AppendProcedureSyntheticWaypoint(
    std::string_view procedureSide,
    const std::string& procedureName,
    const std::string& waypointIdent,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty() ||
        waypointIdent.empty()) {
        return;
    }

    diagnostics->procedureSyntheticWaypoints.push_back(
        std::string(procedureSide) + ":" + procedureName + ":" + waypointIdent);
}

void AppendProcedureSyntheticSource(
    std::string_view procedureSide,
    const std::string& procedureName,
    std::string_view sourceKind,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty() ||
        sourceKind.empty()) {
        return;
    }

    diagnostics->procedureSyntheticSources.push_back(
        std::string(procedureSide) + ":" + procedureName + ":" +
        std::string(sourceKind));
}

const std::string* FindProcedureSyntheticSource(
    std::string_view procedureSide,
    const std::string& procedureName,
    const RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty()) {
        return nullptr;
    }

    const auto prefix = std::string(procedureSide) + ":" + procedureName + ":";
    for (const auto& value : diagnostics->procedureSyntheticSources) {
        if (value.rfind(prefix, 0) == 0) {
            return &value;
        }
    }
    return nullptr;
}

void AppendProcedureApplicationState(
    std::string_view procedureSide,
    const std::string& procedureName,
    std::string_view state,
    std::string_view detail,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty() ||
        state.empty()) {
        return;
    }

    std::string encoded =
        std::string(procedureSide) + ":" + procedureName + ":" + std::string(state);
    if (!detail.empty()) {
        encoded += ":";
        encoded += std::string(detail);
    }
    diagnostics->procedureApplicationStates.push_back(std::move(encoded));
}

void AppendProcedureApplicationBlock(
    std::string_view procedureSide,
    const std::string& procedureName,
    std::string_view reason,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty() ||
        reason.empty()) {
        return;
    }

    diagnostics->procedureApplicationBlocks.push_back(
        std::string(procedureSide) + ":" + procedureName + ":" +
        std::string(reason));
}

std::string JoinIdents(const std::vector<std::string>& idents) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < idents.size(); ++index) {
        if (idents[index].empty()) {
            continue;
        }
        if (stream.tellp() > 0) {
            stream << ">";
        }
        stream << idents[index];
    }
    return stream.str();
}

void AppendProcedureAppliedFixSequence(
    std::string_view procedureSide,
    const std::string& procedureName,
    const std::vector<std::string>& orderedFixes,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty()) {
        return;
    }

    const auto encodedFixes = JoinIdents(orderedFixes);
    if (encodedFixes.empty()) {
        return;
    }

    diagnostics->procedureAppliedFixSequences.push_back(
        std::string(procedureSide) + ":" + procedureName + ":" + encodedFixes);
}

void RemoveProcedureApplicationStates(
    std::string_view procedureSide,
    const std::string& procedureName,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty()) {
        return;
    }

    const auto prefix = std::string(procedureSide) + ":" + procedureName + ":";
    diagnostics->procedureApplicationStates.erase(
        std::remove_if(
            diagnostics->procedureApplicationStates.begin(),
            diagnostics->procedureApplicationStates.end(),
            [&](const std::string& value) {
                return value.rfind(prefix, 0) == 0;
            }),
        diagnostics->procedureApplicationStates.end());
}

void RemoveProcedureApplicationBlocks(
    std::string_view procedureSide,
    const std::string& procedureName,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr ||
        procedureSide.empty() ||
        procedureName.empty()) {
        return;
    }

    const auto prefix = std::string(procedureSide) + ":" + procedureName + ":";
    diagnostics->procedureApplicationBlocks.erase(
        std::remove_if(
            diagnostics->procedureApplicationBlocks.begin(),
            diagnostics->procedureApplicationBlocks.end(),
            [&](const std::string& value) {
                return value.rfind(prefix, 0) == 0;
            }),
        diagnostics->procedureApplicationBlocks.end());
}

void AppendProcedureSupportDirection(
    bool hasForwardAnchor,
    bool hasBackwardAnchor,
    const std::string& procedureName,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr || procedureName.empty()) {
        return;
    }

    std::string direction = "NONE";
    if (hasForwardAnchor && hasBackwardAnchor) {
        direction = "BOTH";
    } else if (hasForwardAnchor) {
        direction = "FORWARD";
    } else if (hasBackwardAnchor) {
        direction = "BACKWARD";
    }

    diagnostics->procedureSupportDirections.push_back(
        direction + ":" + procedureName);
}

void RecordProcedureMetadata(
    const xvatsim::core::route::ParsedRouteToken& procedureToken,
    std::size_t tokenIndex,
    const std::vector<xvatsim::core::route::ParsedRouteToken>& parsedTokens,
    const xvatsim::core::route::RouteGrammarCatalog& grammarCatalog,
    RouteResolveDiagnostics* diagnostics) {
    if (diagnostics == nullptr) {
        return;
    }

    diagnostics->recognizedProcedureTokens.push_back(procedureToken.normalized);
    const auto* procedureEntry =
        xvatsim::core::route::LookupProcedureCatalogEntry(
            grammarCatalog,
            procedureToken.normalized);
    if (procedureEntry == nullptr) {
        return;
    }
    const auto* sidBoundaryFix = ResolveSidBoundaryFix(*procedureEntry);
    const auto* starBoundaryFix = ResolveStarBoundaryFix(*procedureEntry);
    diagnostics->procedureMetadataSources.push_back(
        ResolveProcedureMetadataSourceTag(*procedureEntry) + ":" +
        procedureToken.normalized);
    if (procedureEntry->hasSid) {
        AppendProcedureRecordKind(
            "SID",
            procedureEntry->hasSidRunwayRecords,
            !procedureEntry->sidTransitions.empty(),
            procedureToken.normalized,
            diagnostics);
        AppendProcedureRunwayRecords(
            "SID",
            procedureEntry->sidRunwayTransitions,
            procedureToken.normalized,
            diagnostics);
        AppendProcedureCatalogAuthorities(
            "SID",
            procedureEntry->sidAuthoritySources,
            procedureToken.normalized,
            diagnostics);
        AppendProcedureCatalogFixes(
            "SID",
            procedureEntry->sidFixes,
            procedureToken.normalized,
            diagnostics);
        AppendProcedureOrderedFixes(
            "SID",
            procedureEntry->sidOrderedFixes,
            procedureToken.normalized,
            diagnostics);
        AppendProcedureBoundaryFix(
            "SID",
            sidBoundaryFix,
            procedureToken.normalized,
            diagnostics);
        AppendProcedureCatalogTransitions(
            "SID",
            procedureEntry->sidTransitions,
            procedureToken.normalized,
            diagnostics);
    }
    if (procedureEntry->hasStar) {
        AppendProcedureRecordKind(
            "STAR",
            procedureEntry->hasStarRunwayRecords,
            !procedureEntry->starTransitions.empty(),
            procedureToken.normalized,
            diagnostics);
        AppendProcedureRunwayRecords(
            "STAR",
            procedureEntry->starRunwayTransitions,
            procedureToken.normalized,
            diagnostics);
        AppendProcedureCatalogAuthorities(
            "STAR",
            procedureEntry->starAuthoritySources,
            procedureToken.normalized,
            diagnostics);
        AppendProcedureCatalogFixes(
            "STAR",
            procedureEntry->starFixes,
            procedureToken.normalized,
            diagnostics);
        AppendProcedureOrderedFixes(
            "STAR",
            procedureEntry->starOrderedFixes,
            procedureToken.normalized,
            diagnostics);
        AppendProcedureBoundaryFix(
            "STAR",
            starBoundaryFix,
            procedureToken.normalized,
            diagnostics);
        AppendProcedureCatalogTransitions(
            "STAR",
            procedureEntry->starTransitions,
            procedureToken.normalized,
            diagnostics);
    }
    if (HasProcedureSyntheticWaypoint("SID", procedureToken.normalized, diagnostics)) {
        RemoveProcedureContextOnly("SID", procedureToken.normalized, diagnostics);
        RemoveProcedureTransitionMisses("SID", procedureToken.normalized, diagnostics);
    }
    if (HasProcedureSyntheticWaypoint("STAR", procedureToken.normalized, diagnostics)) {
        RemoveProcedureContextOnly("STAR", procedureToken.normalized, diagnostics);
        RemoveProcedureTransitionMisses("STAR", procedureToken.normalized, diagnostics);
    }

    const auto nextAnchorIndex =
        FindNextAnchorTokenIndex(parsedTokens, tokenIndex + 1, nullptr);
    const auto previousAnchorIndex =
        FindPreviousAnchorTokenIndex(parsedTokens, tokenIndex, nullptr);
    const auto hasForwardAnchor =
        nextAnchorIndex.has_value() &&
        !parsedTokens[*nextAnchorIndex].normalized.empty();
    const auto hasBackwardAnchor =
        previousAnchorIndex.has_value() &&
        !parsedTokens[*previousAnchorIndex].normalized.empty();
    bool hasForwardProcedureSupport = false;
    bool hasBackwardProcedureSupport = false;
    if (procedureEntry->hasSid && hasForwardAnchor) {
        const auto& nextAnchorToken = parsedTokens[*nextAnchorIndex];
        if (!procedureEntry->sidTransitions.empty()) {
            hasForwardProcedureSupport =
                procedureEntry->sidTransitions.find(nextAnchorToken.normalized) !=
                procedureEntry->sidTransitions.end();
        } else if (sidBoundaryFix != nullptr) {
            hasForwardProcedureSupport = nextAnchorToken.normalized == *sidBoundaryFix;
        }
    }
    if (procedureEntry->hasStar && hasBackwardAnchor) {
        const auto& previousAnchorToken = parsedTokens[*previousAnchorIndex];
        if (!procedureEntry->starTransitions.empty()) {
            hasBackwardProcedureSupport =
                procedureEntry->starTransitions.find(previousAnchorToken.normalized) !=
                procedureEntry->starTransitions.end();
        } else if (starBoundaryFix != nullptr) {
            hasBackwardProcedureSupport = previousAnchorToken.normalized == *starBoundaryFix;
        }
    }
    AppendProcedureSupportDirection(
        hasForwardProcedureSupport,
        hasBackwardProcedureSupport,
        procedureToken.normalized,
        diagnostics);

    if (procedureEntry->hasSid) {
        if (!procedureEntry->sidTransitions.empty()) {
            if (nextAnchorIndex.has_value()) {
                const auto& nextAnchorToken = parsedTokens[*nextAnchorIndex];
                if (procedureEntry->sidTransitions.find(nextAnchorToken.normalized) !=
                    procedureEntry->sidTransitions.end()) {
                    AppendProcedureTransitionLink(
                        "SID",
                        procedureToken.normalized,
                        nextAnchorToken.normalized,
                        diagnostics);
                } else {
                    AppendProcedureTransitionMiss(
                        "SID",
                        procedureToken.normalized,
                        nextAnchorToken.normalized,
                        diagnostics);
                }
            } else {
                AppendProcedureTransitionMiss(
                    "SID",
                    procedureToken.normalized,
                    {},
                    diagnostics);
            }
        } else if (nextAnchorIndex.has_value()) {
            const auto& nextAnchorToken = parsedTokens[*nextAnchorIndex];
            if (sidBoundaryFix != nullptr &&
                !nextAnchorToken.normalized.empty() &&
                nextAnchorToken.normalized == *sidBoundaryFix) {
                AppendProcedureAnchorLink(
                    "SID",
                    procedureToken.normalized,
                    nextAnchorToken.normalized,
                    diagnostics);
            } else {
                AppendProcedureContextOnly(
                    "SID",
                    procedureToken.normalized,
                    diagnostics);
            }
        } else {
            AppendProcedureContextOnly(
                "SID",
                procedureToken.normalized,
                diagnostics);
        }
    }

    if (procedureEntry->hasStar) {
        if (!procedureEntry->starTransitions.empty()) {
            if (previousAnchorIndex.has_value()) {
                const auto& previousAnchorToken = parsedTokens[*previousAnchorIndex];
                if (procedureEntry->starTransitions.find(previousAnchorToken.normalized) !=
                    procedureEntry->starTransitions.end()) {
                    AppendProcedureTransitionLink(
                        "STAR",
                        procedureToken.normalized,
                        previousAnchorToken.normalized,
                        diagnostics);
                } else {
                    AppendProcedureTransitionMiss(
                        "STAR",
                        procedureToken.normalized,
                        previousAnchorToken.normalized,
                        diagnostics);
                }
            } else {
                AppendProcedureTransitionMiss(
                    "STAR",
                    procedureToken.normalized,
                    {},
                    diagnostics);
            }
        } else if (previousAnchorIndex.has_value()) {
            const auto& previousAnchorToken = parsedTokens[*previousAnchorIndex];
            if (starBoundaryFix != nullptr &&
                !previousAnchorToken.normalized.empty() &&
                previousAnchorToken.normalized == *starBoundaryFix) {
                AppendProcedureAnchorLink(
                    "STAR",
                    procedureToken.normalized,
                    previousAnchorToken.normalized,
                    diagnostics);
            } else {
                AppendProcedureContextOnly(
                    "STAR",
                    procedureToken.normalized,
                    diagnostics);
            }
        } else {
            AppendProcedureContextOnly(
                "STAR",
                procedureToken.normalized,
                diagnostics);
        }
    }

    if (HasProcedureSyntheticWaypoint("SID", procedureToken.normalized, diagnostics)) {
        RemoveProcedureContextOnly("SID", procedureToken.normalized, diagnostics);
        RemoveProcedureTransitionMisses("SID", procedureToken.normalized, diagnostics);
    }
    if (HasProcedureSyntheticWaypoint("STAR", procedureToken.normalized, diagnostics)) {
        RemoveProcedureContextOnly("STAR", procedureToken.normalized, diagnostics);
        RemoveProcedureTransitionMisses("STAR", procedureToken.normalized, diagnostics);
    }
    if (HasProcedureApplicationState("SID", procedureToken.normalized, diagnostics)) {
        RemoveProcedureContextOnly("SID", procedureToken.normalized, diagnostics);
        RemoveProcedureTransitionMisses("SID", procedureToken.normalized, diagnostics);
    }
    if (HasProcedureApplicationState("STAR", procedureToken.normalized, diagnostics)) {
        RemoveProcedureContextOnly("STAR", procedureToken.normalized, diagnostics);
        RemoveProcedureTransitionMisses("STAR", procedureToken.normalized, diagnostics);
    }

    if (procedureEntry->hasSid) {
        if (HasProcedureApplicationState(
                "SID",
                procedureToken.normalized,
                diagnostics)) {
            // Preserve previously-applied procedure states such as
            // ordered-sequence insertion.
        } else if (const auto* syntheticSource =
                FindProcedureSyntheticSource("SID", procedureToken.normalized, diagnostics);
            syntheticSource != nullptr) {
            const auto detail = syntheticSource->substr(
                std::string("SID:").size() + procedureToken.normalized.size() + 1);
            AppendProcedureApplicationState(
                "SID",
                procedureToken.normalized,
                "APPLIED",
                detail,
                diagnostics);
        } else {
            AppendProcedureApplicationState(
                "SID",
                procedureToken.normalized,
                "RECOGNIZED_ONLY",
                {},
                diagnostics);
        }
    }
    if (procedureEntry->hasStar) {
        if (HasProcedureApplicationState(
                "STAR",
                procedureToken.normalized,
                diagnostics)) {
            // Preserve previously-applied procedure states such as
            // ordered-sequence insertion.
        } else if (const auto* syntheticSource =
                FindProcedureSyntheticSource("STAR", procedureToken.normalized, diagnostics);
            syntheticSource != nullptr) {
            const auto detail = syntheticSource->substr(
                std::string("STAR:").size() + procedureToken.normalized.size() + 1);
            AppendProcedureApplicationState(
                "STAR",
                procedureToken.normalized,
                "APPLIED",
                detail,
                diagnostics);
        } else {
            AppendProcedureApplicationState(
                "STAR",
                procedureToken.normalized,
                "RECOGNIZED_ONLY",
                {},
                diagnostics);
        }
    }

    if (procedureEntry->hasSid &&
        !HasAppliedProcedureApplicationState(
            "SID",
            procedureToken.normalized,
            diagnostics)) {
        std::string blocker = "INSUFFICIENT_CONTEXT";
        if (hasForwardProcedureSupport) {
            blocker = "NOT_NEEDED";
        } else if (procedureEntry->hasStar) {
            blocker = "DUAL_ROLE";
        } else if (procedureEntry->hasSidRunwayRecords) {
            blocker = "RUNWAY_DEPENDENT";
        } else if (procedureEntry->sidTransitions.size() > 1) {
            blocker = "MULTI_TRANSITION";
        } else if (!procedureEntry->sidTransitions.empty()) {
            blocker = "UNMATCHED_TRANSITION";
        } else if (procedureEntry->sidOrderedFixes.empty()) {
            blocker = "NO_PROVABLE_PATH";
        }

        AppendProcedureApplicationBlock(
            "SID",
            procedureToken.normalized,
            blocker,
            diagnostics);
    }

    if (procedureEntry->hasStar &&
        !HasAppliedProcedureApplicationState(
            "STAR",
            procedureToken.normalized,
            diagnostics)) {
        std::string blocker = "INSUFFICIENT_CONTEXT";
        if (hasBackwardProcedureSupport) {
            blocker = "NOT_NEEDED";
        } else if (procedureEntry->hasSid) {
            blocker = "DUAL_ROLE";
        } else if (procedureEntry->hasStarRunwayRecords) {
            blocker = "RUNWAY_DEPENDENT";
        } else if (procedureEntry->starTransitions.size() > 1) {
            blocker = "MULTI_TRANSITION";
        } else if (!procedureEntry->starTransitions.empty()) {
            blocker = "UNMATCHED_TRANSITION";
        } else if (procedureEntry->starOrderedFixes.empty()) {
            blocker = "NO_PROVABLE_PATH";
        }

        AppendProcedureApplicationBlock(
            "STAR",
            procedureToken.normalized,
            blocker,
            diagnostics);
    }
}

bool TryResolveSyntheticSidWaypoint(
    const xvatsim::core::route::ParsedRouteToken& procedureToken,
    const std::vector<xvatsim::core::route::ParsedRouteToken>& parsedTokens,
    std::size_t tokenIndex,
    const xvatsim::core::route::RouteGrammarCatalog& grammarCatalog,
    const std::optional<GeoPoint>& referencePoint,
    ResolvedRoutePoint* outPoint,
    RouteResolveDiagnostics* diagnostics) {
    if (outPoint == nullptr) {
        return false;
    }

    const auto* procedureEntry =
        xvatsim::core::route::LookupProcedureCatalogEntry(
            grammarCatalog,
            procedureToken.normalized);
    if (procedureEntry == nullptr) {
        return false;
    }

    const auto nextMeaningfulIndex =
        FindNextMeaningfulTokenIndex(parsedTokens, tokenIndex + 1);
    if (!nextMeaningfulIndex.has_value() ||
        !TokenCanActAsAirway(parsedTokens[*nextMeaningfulIndex])) {
        return false;
    }

    const auto syntheticAnchor =
        ResolveSidForwardSyntheticAnchor(*procedureEntry);
    if (syntheticAnchor.ident == nullptr || syntheticAnchor.ident->empty()) {
        return false;
    }

    const auto airwayExitAnchorIndex =
        FindNextAnchorTokenIndex(parsedTokens, *nextMeaningfulIndex + 1, nullptr);
    if (!airwayExitAnchorIndex.has_value() ||
        !ResolveRoutePointTokenWithAirwayEntryContext(
            *syntheticAnchor.ident,
            referencePoint,
            parsedTokens[*nextMeaningfulIndex].normalized,
            parsedTokens[*airwayExitAnchorIndex].normalized,
            outPoint)) {
        return false;
    }

    AppendProcedureSyntheticWaypoint(
        "SID",
        procedureToken.normalized,
        outPoint->ident,
        diagnostics);
    AppendProcedureSyntheticSource(
        "SID",
        procedureToken.normalized,
        syntheticAnchor.source,
        diagnostics);
    RemoveProcedureContextOnly(
        "SID",
        procedureToken.normalized,
        diagnostics);
    RemoveProcedureTransitionMisses(
        "SID",
        procedureToken.normalized,
        diagnostics);
    return true;
}

bool TryResolveSyntheticStarWaypoint(
    const xvatsim::core::route::ParsedRouteToken& procedureToken,
    const xvatsim::core::route::RouteGrammarCatalog& grammarCatalog,
    const std::optional<GeoPoint>& referencePoint,
    ResolvedRoutePoint* outPoint,
    RouteResolveDiagnostics* diagnostics) {
    (void)diagnostics;
    if (outPoint == nullptr) {
        return false;
    }

    const auto* procedureEntry =
        xvatsim::core::route::LookupProcedureCatalogEntry(
            grammarCatalog,
            procedureToken.normalized);
    if (procedureEntry == nullptr) {
        return false;
    }

    const auto syntheticAnchor =
        ResolveStarBackwardSyntheticAnchor(*procedureEntry);
    if (syntheticAnchor.ident == nullptr || syntheticAnchor.ident->empty()) {
        return false;
    }

    *outPoint = {
        *syntheticAnchor.ident,
        0.0,
        0.0,
        std::nullopt,
    };

    return true;
}

std::vector<brain::RouteWaypointSnapshot> ResolveRouteWaypoints(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::NetworkPlanSnapshot& networkPlanSnapshot,
    RouteResolveDiagnostics* diagnostics) {
    std::vector<ResolvedRoutePoint> resolvedFiledRoute;
    std::optional<GeoPoint> referencePoint;
    if (!networkPlanSnapshot.hasDestinationCoordinates) {
        return {};
    }

    if (networkPlanSnapshot.hasDepartureCoordinates) {
        resolvedFiledRoute.push_back({
            networkPlanSnapshot.departureIcao.empty() ? "DEP" : networkPlanSnapshot.departureIcao,
            networkPlanSnapshot.departureLatDeg,
            networkPlanSnapshot.departureLonDeg,
            std::nullopt,
        });
        referencePoint = GeoPoint{
            networkPlanSnapshot.departureLatDeg,
            networkPlanSnapshot.departureLonDeg,
        };
    } else if (aircraftState.valid) {
        referencePoint = GeoPoint{
            aircraftState.latitudeDeg,
            aircraftState.longitudeDeg,
        };
    }

    const auto proceduresByName =
        BuildRouteProcedureCatalog(networkPlanSnapshot);
    const auto grammarCatalog =
        BuildRouteGrammarCatalog(GetAirwayGraph(), &proceduresByName);
    const auto parsedTokens =
        xvatsim::core::route::ParseRouteTokens(
            networkPlanSnapshot.routeText,
            &grammarCatalog);
    for (const auto& parsedToken : parsedTokens) {
        if (diagnostics != nullptr && !parsedToken.rawNormalized.empty()) {
            diagnostics->rawTokens.push_back(parsedToken.rawNormalized);
        }
    }

    for (std::size_t tokenIndex = 0; tokenIndex < parsedTokens.size(); ++tokenIndex) {
        const auto& parsedToken = parsedTokens[tokenIndex];
        if (parsedToken.kind == xvatsim::core::route::RouteTokenKind::Control ||
            parsedToken.kind == xvatsim::core::route::RouteTokenKind::Empty ||
            parsedToken.normalized.empty()) {
            continue;
        }

        if (parsedToken.kind == xvatsim::core::route::RouteTokenKind::Procedure) {
            RecordProcedureMetadata(
                parsedToken,
                tokenIndex,
                parsedTokens,
                grammarCatalog,
                diagnostics);
            std::vector<ResolvedRoutePoint> syntheticSidSequence;
            if (TryResolveSyntheticSidOrderedSequence(
                    parsedToken,
                    parsedTokens,
                    tokenIndex,
                    grammarCatalog,
                    referencePoint,
                    &syntheticSidSequence)) {
                std::vector<std::string> appliedFixIdents;
                appliedFixIdents.reserve(syntheticSidSequence.size());
                for (const auto& waypoint : syntheticSidSequence) {
                    resolvedFiledRoute.push_back(waypoint);
                    appliedFixIdents.push_back(waypoint.ident);
                }
                const auto& lastWaypoint = syntheticSidSequence.back();
                referencePoint = GeoPoint{
                    lastWaypoint.latitudeDeg,
                    lastWaypoint.longitudeDeg,
                };
                RemoveProcedureContextOnly(
                    "SID",
                    parsedToken.normalized,
                    diagnostics);
                RemoveProcedureTransitionMisses(
                    "SID",
                    parsedToken.normalized,
                    diagnostics);
                RemoveProcedureApplicationStates(
                    "SID",
                    parsedToken.normalized,
                    diagnostics);
                RemoveProcedureApplicationBlocks(
                    "SID",
                    parsedToken.normalized,
                    diagnostics);
                AppendProcedureApplicationState(
                    "SID",
                    parsedToken.normalized,
                    "APPLIED",
                    "ORDERED_FIXES",
                    diagnostics);
                AppendProcedureAppliedFixSequence(
                    "SID",
                    parsedToken.normalized,
                    appliedFixIdents,
                    diagnostics);
                continue;
            }
            ResolvedRoutePoint syntheticSidWaypoint;
            if (TryResolveSyntheticSidWaypoint(
                    parsedToken,
                    parsedTokens,
                    tokenIndex,
                    grammarCatalog,
                    referencePoint,
                    &syntheticSidWaypoint,
                    diagnostics)) {
                resolvedFiledRoute.push_back(syntheticSidWaypoint);
                referencePoint = GeoPoint{
                    syntheticSidWaypoint.latitudeDeg,
                    syntheticSidWaypoint.longitudeDeg,
                };
                RemoveProcedureContextOnly(
                    "SID",
                    parsedToken.normalized,
                    diagnostics);
            }
            if (HasProcedureSyntheticWaypoint("SID", parsedToken.normalized, diagnostics)) {
                RemoveProcedureContextOnly(
                    "SID",
                    parsedToken.normalized,
                    diagnostics);
                RemoveProcedureTransitionMisses(
                    "SID",
                    parsedToken.normalized,
                    diagnostics);
                RemoveProcedureApplicationStates(
                    "SID",
                    parsedToken.normalized,
                    diagnostics);
                RemoveProcedureApplicationBlocks(
                    "SID",
                    parsedToken.normalized,
                    diagnostics);
                if (const auto* syntheticSource =
                        FindProcedureSyntheticSource("SID", parsedToken.normalized, diagnostics);
                    syntheticSource != nullptr) {
                    const auto detail = syntheticSource->substr(
                        std::string("SID:").size() + parsedToken.normalized.size() + 1);
                    AppendProcedureApplicationState(
                        "SID",
                        parsedToken.normalized,
                        "APPLIED",
                        detail,
                        diagnostics);
                }
            }
            if (HasProcedureSyntheticWaypoint("STAR", parsedToken.normalized, diagnostics)) {
                RemoveProcedureContextOnly(
                    "STAR",
                    parsedToken.normalized,
                    diagnostics);
                RemoveProcedureTransitionMisses(
                    "STAR",
                    parsedToken.normalized,
                    diagnostics);
            }
            continue;
        }
        if (parsedToken.kind == xvatsim::core::route::RouteTokenKind::Unknown) {
            if (diagnostics != nullptr) {
                diagnostics->unsupportedTokens.push_back(parsedToken.normalized);
            }
            continue;
        }

        if (TokenCanActAsAirway(parsedToken)) {
            if (!resolvedFiledRoute.empty()) {
                const auto nextAnchorIndex =
                    FindNextAnchorTokenIndex(parsedTokens, tokenIndex + 1, diagnostics);
                if (nextAnchorIndex.has_value() &&
                    !parsedTokens[*nextAnchorIndex].normalized.empty()) {
                    const auto& nextAnchorToken = parsedTokens[*nextAnchorIndex];
                    const ResolvedRoutePoint nextAnchorWaypoint{
                        nextAnchorToken.normalized,
                        0.0,
                        0.0,
                        std::nullopt,
                    };
                    std::vector<ResolvedRoutePoint> expandedSegment;
                    ResolvedRoutePoint resolvedAirwayEndWaypoint;
                    if (ExpandAirwaySegment(
                            resolvedFiledRoute.back(),
                            parsedToken.normalized,
                            nextAnchorWaypoint,
                            &expandedSegment,
                            &resolvedAirwayEndWaypoint)) {
                        if (diagnostics != nullptr) {
                            diagnostics->expandedTokens.push_back(parsedToken.normalized);
                            diagnostics->resolvedTokens.push_back(nextAnchorToken.normalized);
                        }
                        for (const auto& waypoint : expandedSegment) {
                            resolvedFiledRoute.push_back(waypoint);
                        }
                        resolvedFiledRoute.push_back(resolvedAirwayEndWaypoint);
                        referencePoint = GeoPoint{
                            resolvedAirwayEndWaypoint.latitudeDeg,
                            resolvedAirwayEndWaypoint.longitudeDeg,
                        };
                        tokenIndex = *nextAnchorIndex;
                        continue;
                    }
                } else {
                    const auto nextMeaningfulIndex =
                        FindNextMeaningfulTokenIndex(parsedTokens, tokenIndex + 1);
                    if (nextMeaningfulIndex.has_value() &&
                        parsedTokens[*nextMeaningfulIndex].kind ==
                            xvatsim::core::route::RouteTokenKind::Procedure) {
                        const auto& nextProcedureToken =
                            parsedTokens[*nextMeaningfulIndex];
                        std::vector<ResolvedRoutePoint> syntheticStarSequence;
                        if (TryResolveSyntheticStarOrderedSequence(
                                nextProcedureToken,
                                grammarCatalog,
                                referencePoint,
                                &syntheticStarSequence)) {
                            std::vector<ResolvedRoutePoint> expandedSegment;
                            ResolvedRoutePoint resolvedAirwayEndWaypoint;
                            if (ExpandAirwaySegment(
                                    resolvedFiledRoute.back(),
                                    parsedToken.normalized,
                                    syntheticStarSequence.front(),
                                    &expandedSegment,
                                    &resolvedAirwayEndWaypoint)) {
                                syntheticStarSequence.front() = resolvedAirwayEndWaypoint;
                                bool rebuiltOrderedSequence = true;
                                const auto* procedureEntry =
                                    xvatsim::core::route::LookupProcedureCatalogEntry(
                                        grammarCatalog,
                                        nextProcedureToken.normalized);
                                if (procedureEntry != nullptr &&
                                    procedureEntry->starOrderedFixes.size() ==
                                        syntheticStarSequence.size() &&
                                    !procedureEntry->starOrderedFixes.empty()) {
                                    std::optional<GeoPoint> sequenceReferencePoint = GeoPoint{
                                        resolvedAirwayEndWaypoint.latitudeDeg,
                                        resolvedAirwayEndWaypoint.longitudeDeg,
                                    };
                                    for (std::size_t sequenceIndex = 1;
                                         sequenceIndex < procedureEntry->starOrderedFixes.size();
                                         ++sequenceIndex) {
                                        std::optional<std::string> nextOrderedFix;
                                        for (std::size_t nextFixIndex = sequenceIndex + 1;
                                             nextFixIndex < procedureEntry->starOrderedFixes.size();
                                             ++nextFixIndex) {
                                            if (!procedureEntry->starOrderedFixes[nextFixIndex].empty()) {
                                                nextOrderedFix =
                                                    procedureEntry->starOrderedFixes[nextFixIndex];
                                                break;
                                            }
                                        }

                                        ResolvedRoutePoint rebuiltPoint;
                                        if (!ResolveRoutePointTokenWithRouteContext(
                                                procedureEntry->starOrderedFixes[sequenceIndex],
                                                sequenceReferencePoint,
                                                nextOrderedFix,
                                                GeoPoint{
                                                    networkPlanSnapshot.destinationLatDeg,
                                                    networkPlanSnapshot.destinationLonDeg,
                                                },
                                                &rebuiltPoint)) {
                                            rebuiltOrderedSequence = false;
                                            break;
                                        }
                                        syntheticStarSequence[sequenceIndex] = rebuiltPoint;
                                        sequenceReferencePoint = GeoPoint{
                                            rebuiltPoint.latitudeDeg,
                                            rebuiltPoint.longitudeDeg,
                                        };
                                    }
                                }
                                if (rebuiltOrderedSequence && diagnostics != nullptr) {
                                    std::vector<std::string> appliedFixIdents;
                                    appliedFixIdents.reserve(
                                        syntheticStarSequence.size());
                                    diagnostics->expandedTokens.push_back(
                                        parsedToken.normalized);
                                    for (const auto& waypoint :
                                         syntheticStarSequence) {
                                        appliedFixIdents.push_back(
                                            waypoint.ident);
                                    }
                                    RemoveProcedureContextOnly(
                                        "STAR",
                                        nextProcedureToken.normalized,
                                        diagnostics);
                                    RemoveProcedureTransitionMisses(
                                        "STAR",
                                        nextProcedureToken.normalized,
                                        diagnostics);
                                    RemoveProcedureApplicationStates(
                                        "STAR",
                                        nextProcedureToken.normalized,
                                        diagnostics);
                                    AppendProcedureApplicationState(
                                        "STAR",
                                        nextProcedureToken.normalized,
                                        "APPLIED",
                                        "ORDERED_FIXES",
                                        diagnostics);
                                    AppendProcedureAppliedFixSequence(
                                        "STAR",
                                        nextProcedureToken.normalized,
                                        appliedFixIdents,
                                        diagnostics);
                                }
                                if (rebuiltOrderedSequence) {
                                    for (const auto& waypoint : expandedSegment) {
                                        resolvedFiledRoute.push_back(waypoint);
                                    }
                                    for (const auto& waypoint :
                                         syntheticStarSequence) {
                                        resolvedFiledRoute.push_back(waypoint);
                                    }
                                    const auto& lastWaypoint =
                                        syntheticStarSequence.back();
                                    referencePoint = GeoPoint{
                                        lastWaypoint.latitudeDeg,
                                        lastWaypoint.longitudeDeg,
                                    };
                                    continue;
                                }
                            }
                        }
                        ResolvedRoutePoint syntheticStarWaypoint;
                        if (TryResolveSyntheticStarWaypoint(
                                nextProcedureToken,
                                grammarCatalog,
                                referencePoint,
                                &syntheticStarWaypoint,
                                diagnostics)) {
                            std::vector<ResolvedRoutePoint> expandedSegment;
                            ResolvedRoutePoint resolvedAirwayEndWaypoint;
                            if (ExpandAirwaySegment(
                                    resolvedFiledRoute.back(),
                                    parsedToken.normalized,
                                    syntheticStarWaypoint,
                                    &expandedSegment,
                                    &resolvedAirwayEndWaypoint)) {
                                if (diagnostics != nullptr) {
                                    diagnostics->expandedTokens.push_back(
                                        parsedToken.normalized);
                                    const auto* procedureEntry =
                                        xvatsim::core::route::LookupProcedureCatalogEntry(
                                            grammarCatalog,
                                            nextProcedureToken.normalized);
                                    if (procedureEntry != nullptr) {
                                        const auto syntheticAnchor =
                                            ResolveStarBackwardSyntheticAnchor(
                                                *procedureEntry);
                                        if (syntheticAnchor.ident != nullptr) {
                                            AppendProcedureSyntheticWaypoint(
                                                "STAR",
                                                nextProcedureToken.normalized,
                                                resolvedAirwayEndWaypoint.ident,
                                                diagnostics);
                                            AppendProcedureSyntheticSource(
                                                "STAR",
                                                nextProcedureToken.normalized,
                                                syntheticAnchor.source,
                                                diagnostics);
                                            RemoveProcedureContextOnly(
                                                "STAR",
                                                nextProcedureToken.normalized,
                                                diagnostics);
                                            RemoveProcedureTransitionMisses(
                                                "STAR",
                                                nextProcedureToken.normalized,
                                                diagnostics);
                                        }
                                    }
                                }
                                for (const auto& waypoint : expandedSegment) {
                                    resolvedFiledRoute.push_back(waypoint);
                                }
                                resolvedFiledRoute.push_back(resolvedAirwayEndWaypoint);
                                referencePoint = GeoPoint{
                                    resolvedAirwayEndWaypoint.latitudeDeg,
                                    resolvedAirwayEndWaypoint.longitudeDeg,
                                };
                                continue;
                            }
                        }
                    }
                }
            }

            if (parsedToken.kind == xvatsim::core::route::RouteTokenKind::Airway) {
                if (diagnostics != nullptr) {
                    diagnostics->unresolvedTokens.push_back(parsedToken.normalized);
                }
                continue;
            }
        }

        if (!TokenCanActAsPoint(parsedToken)) {
            if (diagnostics != nullptr) {
                diagnostics->unresolvedTokens.push_back(parsedToken.normalized);
            }
            continue;
        }

        ResolvedRoutePoint resolvedPoint;
        bool pointResolved = false;
        const auto nextMeaningfulIndex =
            FindNextMeaningfulTokenIndex(parsedTokens, tokenIndex + 1);
        if (nextMeaningfulIndex.has_value() &&
            TokenCanActAsAirway(parsedTokens[*nextMeaningfulIndex])) {
            const auto airwayExitAnchorIndex =
                FindNextAnchorTokenIndex(parsedTokens, *nextMeaningfulIndex + 1, nullptr);
            if (airwayExitAnchorIndex.has_value()) {
                pointResolved = ResolveRoutePointTokenWithAirwayEntryContext(
                    parsedToken.normalized,
                    referencePoint,
                    parsedTokens[*nextMeaningfulIndex].normalized,
                    parsedTokens[*airwayExitAnchorIndex].normalized,
                    &resolvedPoint);
            }
        }
        std::optional<std::string> nextPointToken;
        const auto nextAnchorIndex =
            FindNextAnchorTokenIndex(parsedTokens, tokenIndex + 1, nullptr);
        if (nextAnchorIndex.has_value()) {
            nextPointToken = parsedTokens[*nextAnchorIndex].normalized;
        }
        const std::optional<GeoPoint> destinationPoint = GeoPoint{
            networkPlanSnapshot.destinationLatDeg,
            networkPlanSnapshot.destinationLonDeg,
        };
        if (!pointResolved && !ResolveRoutePointTokenWithRouteContext(
                parsedToken.normalized,
                referencePoint,
                nextPointToken,
                destinationPoint,
                &resolvedPoint)) {
            if (diagnostics != nullptr) {
                diagnostics->unresolvedTokens.push_back(parsedToken.normalized);
            }
            continue;
        }

        if (diagnostics != nullptr) {
            diagnostics->resolvedTokens.push_back(parsedToken.normalized);
        }
        resolvedFiledRoute.push_back(resolvedPoint);
        referencePoint = GeoPoint{
            resolvedPoint.latitudeDeg,
            resolvedPoint.longitudeDeg,
        };
    }

    if (diagnostics != nullptr) {
        DeduplicatePreserveOrder(&diagnostics->ignoredTokens);
    }

    resolvedFiledRoute.push_back({
        networkPlanSnapshot.destinationIcao.empty() ? "DEST" : networkPlanSnapshot.destinationIcao,
        networkPlanSnapshot.destinationLatDeg,
        networkPlanSnapshot.destinationLonDeg,
        std::nullopt,
    });

    std::vector<brain::RouteWaypointSnapshot> filedRoute;
    filedRoute.reserve(resolvedFiledRoute.size());
    for (const auto& waypoint : resolvedFiledRoute) {
        filedRoute.push_back({
            waypoint.ident,
            waypoint.latitudeDeg,
            waypoint.longitudeDeg,
        });
    }
    filedRoute = CompactRouteWaypoints(filedRoute);

    std::vector<brain::RouteWaypointSnapshot> remainingRoute;
    remainingRoute.push_back({
        "ACFT",
        aircraftState.latitudeDeg,
        aircraftState.longitudeDeg,
    });

    if (filedRoute.size() < 2) {
        remainingRoute.push_back({
            networkPlanSnapshot.destinationIcao.empty() ? "DEST" : networkPlanSnapshot.destinationIcao,
            networkPlanSnapshot.destinationLatDeg,
            networkPlanSnapshot.destinationLonDeg,
        });
        return CompactRouteWaypoints(remainingRoute);
    }

    const GeoPoint aircraftPoint{aircraftState.latitudeDeg, aircraftState.longitudeDeg};
    std::size_t bestSegmentStartIndex = 0;
    double bestDistanceNm = std::numeric_limits<double>::max();
    for (std::size_t index = 0; index + 1 < filedRoute.size(); ++index) {
        const auto distanceNm = DistanceFromPointToRouteSegmentNm(
            aircraftPoint,
            filedRoute[index],
            filedRoute[index + 1]);
        if (distanceNm < bestDistanceNm) {
            bestDistanceNm = distanceNm;
            bestSegmentStartIndex = index;
        }
    }

    const auto firstRemainingWaypointIndex =
        std::min(bestSegmentStartIndex + 1, filedRoute.size() - 1);
    for (std::size_t index = firstRemainingWaypointIndex; index < filedRoute.size(); ++index) {
        remainingRoute.push_back(filedRoute[index]);
    }

    return CompactRouteWaypoints(remainingRoute);
}

std::vector<std::size_t> ResolveContainingFeatures(
    const GeoPoint& point,
    const std::vector<SectorFeature>& features) {
    std::vector<std::size_t> matches;
    for (std::size_t index = 0; index < features.size(); ++index) {
        if (PointInFeature(point, features[index])) {
            matches.push_back(index);
        }
    }
    return matches;
}

std::string FormatDistanceNm(double distanceNm) {
    return std::to_string(static_cast<int>(std::round(std::max(0.0, distanceNm)))) + "nm";
}

std::string SanitizeDiagnosticText(std::string_view value, std::size_t maxChars) {
    std::string sanitized;
    sanitized.reserve(std::min(value.size(), maxChars));
    for (const auto character : value) {
        if (sanitized.size() >= maxChars) {
            break;
        }

        const auto byte = static_cast<unsigned char>(character);
        if (std::iscntrl(byte) != 0) {
            sanitized.push_back(' ');
            continue;
        }

        if (character == '"') {
            sanitized.push_back('\'');
            continue;
        }

        if (character == '\\') {
            sanitized.push_back('/');
            continue;
        }

        sanitized.push_back(static_cast<char>(byte));
    }

    if (value.size() > maxChars && sanitized.size() >= 3) {
        sanitized.resize(maxChars - 3);
        sanitized += "...";
    }

    return sanitized;
}

std::string SummarizeStrings(const std::vector<std::string>& values) {
    if (values.empty()) {
        return "none";
    }

    std::ostringstream stream;
    const auto countToLog = std::min<std::size_t>(values.size(), kDiagnosticListLimit);
    for (std::size_t index = 0; index < countToLog; ++index) {
        if (index > 0) {
            stream << ",";
        }
        stream << SanitizeDiagnosticText(values[index], kDiagnosticTextLimit);
    }
    if (values.size() > countToLog) {
        stream << ",+" << (values.size() - countToLog);
    }
    return stream.str();
}

std::string SummarizeWaypointIdents(
    const std::vector<brain::RouteWaypointSnapshot>& waypoints) {
    std::vector<std::string> idents;
    idents.reserve(waypoints.size());
    for (const auto& waypoint : waypoints) {
        idents.push_back(waypoint.ident);
    }
    return SummarizeStrings(idents);
}

std::string SummarizeSectorIdentifiers(
    const std::vector<brain::RouteSectorMatchSnapshot>& sectors) {
    std::vector<std::string> identifiers;
    identifiers.reserve(sectors.size());
    for (const auto& sector : sectors) {
        identifiers.push_back(sector.identifier);
    }
    return SummarizeStrings(identifiers);
}

std::vector<std::string> ExtractAuthorityGapIdentifiers(
    const std::vector<brain::RouteSectorMatchSnapshot>& sectors,
    std::string_view label) {
    std::vector<std::string> gaps;
    for (const auto& sector : sectors) {
        if (!sector.controllerCallsignPatterns.empty() ||
            !sector.controllerPrefixes.empty()) {
            continue;
        }
        gaps.push_back(std::string(label) + ":" + sector.identifier);
    }
    return gaps;
}

std::string SummarizeAuthorityGapIdentifiers(
    const std::vector<brain::RouteSectorMatchSnapshot>& currentSectors,
    const std::vector<brain::RouteSectorMatchSnapshot>& nextSectors) {
    auto gaps = ExtractAuthorityGapIdentifiers(currentSectors, "current");
    auto nextGaps = ExtractAuthorityGapIdentifiers(nextSectors, "next");
    gaps.insert(gaps.end(), nextGaps.begin(), nextGaps.end());
    return SummarizeStrings(gaps);
}

std::vector<std::string> ResolveAuthoritativeControllerPrefixes(
    const std::string& identifier,
    const std::unordered_set<std::string>& matchTokens,
    const ControllerAuthorityCatalog& catalog) {
    std::unordered_set<std::string> prefixes;

    auto mergePrefixes = [&](const std::string& key) {
        const auto normalizedKey = NormalizeAuthorityCatalogKey(key);
        if (normalizedKey.empty()) {
            return;
        }

        const auto entry = catalog.prefixesByKey.find(normalizedKey);
        if (entry == catalog.prefixesByKey.end()) {
            return;
        }

        prefixes.insert(entry->second.begin(), entry->second.end());
    };

    mergePrefixes(identifier);
    for (const auto& token : matchTokens) {
        mergePrefixes(token);
    }

    std::vector<std::string> resolvedPrefixes(prefixes.begin(), prefixes.end());
    std::sort(resolvedPrefixes.begin(), resolvedPrefixes.end());
    return resolvedPrefixes;
}

std::vector<std::string> ResolveAuthoritativeControllerCallsignPatterns(
    const std::string& identifier,
    const std::unordered_set<std::string>& matchTokens,
    const ControllerAuthorityCatalog& catalog) {
    std::unordered_set<std::string> patterns;

    auto mergePatterns = [&](const std::string& key) {
        const auto normalizedKey = NormalizeAuthorityCatalogKey(key);
        if (normalizedKey.empty()) {
            return;
        }

        const auto entry = catalog.callsignPatternsByKey.find(normalizedKey);
        if (entry == catalog.callsignPatternsByKey.end()) {
            return;
        }

        patterns.insert(entry->second.begin(), entry->second.end());
    };

    mergePatterns(identifier);
    for (const auto& token : matchTokens) {
        mergePatterns(token);
    }

    std::vector<std::string> resolvedPatterns(patterns.begin(), patterns.end());
    std::sort(resolvedPatterns.begin(), resolvedPatterns.end());
    return resolvedPatterns;
}

std::vector<std::string> ResolveAuthoritativeControllerPrefixes(
    const std::string& identifier,
    const std::vector<std::string>& matchTokens,
    const ControllerAuthorityCatalog& catalog) {
    return ResolveAuthoritativeControllerPrefixes(
        identifier,
        std::unordered_set<std::string>(matchTokens.begin(), matchTokens.end()),
        catalog);
}

std::vector<std::string> ResolveAuthoritativeControllerCallsignPatterns(
    const std::string& identifier,
    const std::vector<std::string>& matchTokens,
    const ControllerAuthorityCatalog& catalog) {
    return ResolveAuthoritativeControllerCallsignPatterns(
        identifier,
        std::unordered_set<std::string>(matchTokens.begin(), matchTokens.end()),
        catalog);
}

std::size_t CountAntiMeridianSegments(
    const std::vector<brain::RouteWaypointSnapshot>& waypoints) {
    std::size_t count = 0;
    for (std::size_t index = 1; index < waypoints.size(); ++index) {
        if (CrossesAntiMeridian(
                waypoints[index - 1].longitudeDeg,
                waypoints[index].longitudeDeg)) {
            ++count;
        }
    }
    return count;
}

double RouteDistanceNm(const std::vector<brain::RouteWaypointSnapshot>& waypoints) {
    double distanceNm = 0.0;
    for (std::size_t index = 1; index < waypoints.size(); ++index) {
        distanceNm += GreatCircleDistanceNm(
            waypoints[index - 1].latitudeDeg,
            waypoints[index - 1].longitudeDeg,
            waypoints[index].latitudeDeg,
            waypoints[index].longitudeDeg);
    }
    return distanceNm;
}

void LogRouteDiagnosticsIfChanged(
    const brain::NetworkPlanSnapshot& networkPlanSnapshot,
    const RouteResolveDiagnostics& diagnostics,
    const brain::RouteSectorSnapshot& snapshot,
    double directDistanceNm,
    double routeDistanceNm,
    std::string_view traversalMode,
    bool sanityRejected) {
    static std::mutex logMutex;
    static std::string lastSignature;

    std::ostringstream signatureStream;
    signatureStream << networkPlanSnapshot.departureIcao << "|"
                    << networkPlanSnapshot.destinationIcao << "|"
                    << networkPlanSnapshot.routeText << "|"
                    << snapshot.waypoints.size() << "|"
                    << diagnostics.rawTokens.size() << "|"
                    << diagnostics.resolvedTokens.size() << "|"
                    << diagnostics.expandedTokens.size() << "|"
                    << diagnostics.recognizedProcedureTokens.size() << "|"
                    << diagnostics.procedureMetadataSources.size() << "|"
                    << diagnostics.procedureRecordKinds.size() << "|"
                    << diagnostics.procedureRunwayRecords.size() << "|"
                    << diagnostics.procedureCatalogAuthorities.size() << "|"
                    << diagnostics.procedureCatalogFixes.size() << "|"
                    << diagnostics.procedureBoundaryFixes.size() << "|"
                    << diagnostics.procedureOrderedFixes.size() << "|"
                    << diagnostics.procedureSyntheticWaypoints.size() << "|"
                    << diagnostics.procedureSyntheticSources.size() << "|"
                    << diagnostics.procedureApplicationStates.size() << "|"
                    << diagnostics.procedureApplicationBlocks.size() << "|"
                    << diagnostics.procedureAppliedFixSequences.size() << "|"
                    << diagnostics.procedureCatalogTransitions.size() << "|"
                    << diagnostics.procedureSupportDirections.size() << "|"
                    << diagnostics.procedureTransitionLinks.size() << "|"
                    << diagnostics.procedureTransitionMisses.size() << "|"
                    << diagnostics.procedureAnchorLinks.size() << "|"
                    << diagnostics.procedureContextOnlyTokens.size() << "|"
                    << diagnostics.ignoredTokens.size() << "|"
                    << diagnostics.unsupportedTokens.size() << "|"
                    << diagnostics.unresolvedTokens.size() << "|"
                    << snapshot.currentSectors.size() << "|"
                    << snapshot.nextSectors.size() << "|"
                    << SummarizeSectorIdentifiers(snapshot.currentSectors) << "|"
                    << SummarizeSectorIdentifiers(snapshot.nextSectors) << "|"
                    << SummarizeAuthorityGapIdentifiers(
                           snapshot.currentSectors,
                           snapshot.nextSectors) << "|"
                    << snapshot.statusLine << "|"
                    << sanityRejected;
    const auto signature = signatureStream.str();

    {
        std::lock_guard<std::mutex> lock(logMutex);
        if (signature == lastSignature) {
            return;
        }
        lastSignature = signature;
    }

    std::ostringstream stream;
    stream << "[XVatsim] Route diagnostic: "
           << networkPlanSnapshot.departureIcao << "->"
           << networkPlanSnapshot.destinationIcao
           << " rawRoute=\""
           << (networkPlanSnapshot.routeText.empty()
                   ? "<empty>"
                   : SanitizeDiagnosticText(
                         networkPlanSnapshot.routeText,
                         kDiagnosticRouteTextLimit))
           << "\" rawTokens=" << diagnostics.rawTokens.size()
           << " resolvedTokens=" << diagnostics.resolvedTokens.size()
           << " expandedTokens=" << diagnostics.expandedTokens.size()
           << " procedureTokens=" << diagnostics.recognizedProcedureTokens.size()
           << " procedureSources=" << diagnostics.procedureMetadataSources.size()
           << " procedureRecords=" << diagnostics.procedureRecordKinds.size()
           << " procedureRunways=" << diagnostics.procedureRunwayRecords.size()
           << " procedureAuthorities=" << diagnostics.procedureCatalogAuthorities.size()
           << " procedureCatalogFixes=" << diagnostics.procedureCatalogFixes.size()
           << " procedureBoundaryFixes=" << diagnostics.procedureBoundaryFixes.size()
           << " procedureOrderedFixes=" << diagnostics.procedureOrderedFixes.size()
           << " procedureSyntheticWaypoints=" << diagnostics.procedureSyntheticWaypoints.size()
           << " procedureSyntheticSources=" << diagnostics.procedureSyntheticSources.size()
           << " procedureApplications=" << diagnostics.procedureApplicationStates.size()
           << " procedureApplicationBlocks=" << diagnostics.procedureApplicationBlocks.size()
           << " procedureAppliedFixSequences=" << diagnostics.procedureAppliedFixSequences.size()
           << " procedureCatalogTransitions=" << diagnostics.procedureCatalogTransitions.size()
           << " procedureSupport=" << diagnostics.procedureSupportDirections.size()
           << " procedureLinks=" << diagnostics.procedureTransitionLinks.size()
           << " procedureMisses=" << diagnostics.procedureTransitionMisses.size()
           << " procedureAnchorLinks=" << diagnostics.procedureAnchorLinks.size()
           << " procedureContextOnly=" << diagnostics.procedureContextOnlyTokens.size()
           << " ignoredTokens=" << diagnostics.ignoredTokens.size()
           << " unsupportedTokens=" << diagnostics.unsupportedTokens.size()
           << " unresolvedTokens=" << diagnostics.unresolvedTokens.size()
           << " waypoints=" << snapshot.waypoints.size()
           << " antiMeridianSegments=" << CountAntiMeridianSegments(snapshot.waypoints)
           << " direct=" << FormatDistanceNm(directDistanceNm)
           << " route=" << FormatDistanceNm(routeDistanceNm)
           << " traversal=" << traversalMode
           << " current=" << snapshot.currentSectors.size()
           << " next=" << snapshot.nextSectors.size()
           << " rejected=" << (sanityRejected ? "1" : "0")
           << " waypointList=" << SummarizeWaypointIdents(snapshot.waypoints)
           << " expandedList=" << SummarizeStrings(diagnostics.expandedTokens)
           << " procedureList=" << SummarizeStrings(diagnostics.recognizedProcedureTokens)
           << " procedureSources=" << SummarizeStrings(diagnostics.procedureMetadataSources)
           << " procedureRecords=" << SummarizeStrings(diagnostics.procedureRecordKinds)
           << " procedureRunways=" << SummarizeStrings(diagnostics.procedureRunwayRecords)
           << " procedureAuthorities="
           << SummarizeStrings(diagnostics.procedureCatalogAuthorities)
           << " procedureCatalogFixes="
           << SummarizeStrings(diagnostics.procedureCatalogFixes)
           << " procedureBoundaryFixes="
           << SummarizeStrings(diagnostics.procedureBoundaryFixes)
           << " procedureOrderedFixes="
           << SummarizeStrings(diagnostics.procedureOrderedFixes)
           << " procedureSyntheticWaypoints="
           << SummarizeStrings(diagnostics.procedureSyntheticWaypoints)
           << " procedureSyntheticSources="
           << SummarizeStrings(diagnostics.procedureSyntheticSources)
           << " procedureApplications="
           << SummarizeStrings(diagnostics.procedureApplicationStates)
           << " procedureApplicationBlocks="
           << SummarizeStrings(diagnostics.procedureApplicationBlocks)
           << " procedureAppliedFixSequences="
           << SummarizeStrings(diagnostics.procedureAppliedFixSequences)
           << " procedureCatalogTransitions="
           << SummarizeStrings(diagnostics.procedureCatalogTransitions)
           << " procedureSupport=" << SummarizeStrings(diagnostics.procedureSupportDirections)
           << " procedureLinks=" << SummarizeStrings(diagnostics.procedureTransitionLinks)
           << " procedureMisses=" << SummarizeStrings(diagnostics.procedureTransitionMisses)
           << " procedureAnchorLinks=" << SummarizeStrings(diagnostics.procedureAnchorLinks)
           << " procedureContextOnly=" << SummarizeStrings(diagnostics.procedureContextOnlyTokens)
           << " ignoredList=" << SummarizeStrings(diagnostics.ignoredTokens)
           << " unsupportedList=" << SummarizeStrings(diagnostics.unsupportedTokens)
           << " unresolvedList=" << SummarizeStrings(diagnostics.unresolvedTokens)
           << " currentSectors=" << SummarizeSectorIdentifiers(snapshot.currentSectors)
           << " nextSectors=" << SummarizeSectorIdentifiers(snapshot.nextSectors)
           << " authorityGapSectors="
           << SummarizeAuthorityGapIdentifiers(
                  snapshot.currentSectors,
                  snapshot.nextSectors)
           << " status=\"" << SanitizeDiagnosticText(snapshot.statusLine, 160) << "\""
           << "\n";
    auto line = stream.str();
    if (line.size() > kDiagnosticLogLineLimit) {
        line.resize(kDiagnosticLogLineLimit - 4);
        line += "...\n";
    }
    SafeXPlaneDebugString(line);
}

std::string AppendUnsupportedRouteStatus(
    std::string statusLine,
    const RouteResolveDiagnostics& diagnostics) {
    if (diagnostics.unsupportedTokens.empty()) {
        return statusLine;
    }

    if (!statusLine.empty()) {
        statusLine += " ";
    }
    statusLine += "unsupported ";
    statusLine += std::to_string(diagnostics.unsupportedTokens.size());
    return statusLine;
}

std::size_t CountControllerAuthorityGaps(
    const std::vector<brain::RouteSectorMatchSnapshot>& sectors) {
    std::size_t gapCount = 0;
    for (const auto& sector : sectors) {
        if (sector.controllerCallsignPatterns.empty() &&
            sector.controllerPrefixes.empty()) {
            ++gapCount;
        }
    }
    return gapCount;
}

std::string AppendAuthorityGapStatus(
    std::string statusLine,
    const std::vector<brain::RouteSectorMatchSnapshot>& currentSectors,
    const std::vector<brain::RouteSectorMatchSnapshot>& nextSectors) {
    const auto gapCount =
        CountControllerAuthorityGaps(currentSectors) +
        CountControllerAuthorityGaps(nextSectors);
    if (gapCount == 0) {
        return statusLine;
    }

    if (!statusLine.empty()) {
        statusLine += " ";
    }
    statusLine += "authority-gaps ";
    statusLine += std::to_string(gapCount);
    return statusLine;
}

void AppendStaleSuffixIfNeeded(std::string* statusLine) {
    if (statusLine == nullptr ||
        statusLine->find("(stale)") != std::string::npos) {
        return;
    }

    *statusLine += " (stale)";
}

}  // namespace

RouteSectorResolver::~RouteSectorResolver() {
    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }
}

void RouteSectorResolver::LoadBoundaryPayloadsForTesting(
    const std::string& boundaryGeoJson,
    const std::string& terminalGeoJson,
    const std::string& authorityCatalogDat) const {
    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }

    std::lock_guard<std::mutex> lock(fetchMutex_);
    boundaryPayload_.assign(boundaryGeoJson.begin(), boundaryGeoJson.end());
    terminalBoundaryPayload_.assign(terminalGeoJson.begin(), terminalGeoJson.end());
    vatspyPayload_.assign(authorityCatalogDat.begin(), authorityCatalogDat.end());
    pendingBoundaryPayload_.clear();
    pendingTerminalBoundaryPayload_.clear();
    pendingVatspyPayload_.clear();
    hasBoundaryCache_ = !boundaryPayload_.empty();
    hasAuthorityCatalogCache_ = !vatspyPayload_.empty();
    hasTerminalBoundaryCache_ = !terminalBoundaryPayload_.empty();
    centerBoundaryGeneration_ = hasBoundaryCache_ ? centerBoundaryGeneration_ + 1 : 0;
    authorityCatalogGeneration_ =
        hasAuthorityCatalogCache_ ? authorityCatalogGeneration_ + 1 : 0;
    if (hasBoundaryCache_ && centerBoundaryGeneration_ == 0) {
        centerBoundaryGeneration_ = 1;
    }
    if (hasAuthorityCatalogCache_ && authorityCatalogGeneration_ == 0) {
        authorityCatalogGeneration_ = 1;
    }
    terminalBoundaryGeneration_ =
        hasTerminalBoundaryCache_ ? terminalBoundaryGeneration_ + 1 : 0;
    if (hasTerminalBoundaryCache_ && terminalBoundaryGeneration_ == 0) {
        terminalBoundaryGeneration_ = 1;
    }
    lastFetchSucceeded_ =
        hasBoundaryCache_ && hasAuthorityCatalogCache_ && hasTerminalBoundaryCache_;
    lastFetchTickSeconds_ = CurrentTickSeconds();
    lastSuccessfulCenterFetchTickSeconds_ =
        (hasBoundaryCache_ && hasAuthorityCatalogCache_) ? lastFetchTickSeconds_ : 0;
    lastSuccessfulTerminalFetchTickSeconds_ =
        hasTerminalBoundaryCache_ ? lastFetchTickSeconds_ : 0;
    hasSnapshotCache_ = false;
    cachedSnapshot_ = {};
    airportCoverageCache_.clear();
    hasPendingPayload_ = false;
    fetchInProgress_ = false;
}

void RouteSectorResolver::QueueBoundaryPayloadsForTesting(
    const std::string& boundaryGeoJson,
    const std::string& terminalGeoJson,
    const std::string& authorityCatalogDat) const {
    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }

    StageFetchedPayloads(
        std::vector<unsigned char>(boundaryGeoJson.begin(), boundaryGeoJson.end()),
        std::vector<unsigned char>(terminalGeoJson.begin(), terminalGeoJson.end()),
        std::vector<unsigned char>(authorityCatalogDat.begin(), authorityCatalogDat.end()));
    fetchInProgress_ = false;
}

void RouteSectorResolver::ResetRuntimeState() {
    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }

    std::lock_guard<std::mutex> lock(fetchMutex_);
    lastFetchTickSeconds_ = 0;
    lastFetchSucceeded_ = false;
    lastSuccessfulCenterFetchTickSeconds_ = 0;
    lastSuccessfulTerminalFetchTickSeconds_ = 0;
    hasSnapshotCache_ = false;
    lastSnapshotBuildTickSeconds_ = 0;
    lastSnapshotLatitudeDeg_ = 0.0;
    lastSnapshotLongitudeDeg_ = 0.0;
    lastSnapshotRouteKey_.clear();
    cachedSnapshot_ = {};
    airportCoverageCache_.clear();
    pendingBoundaryPayload_.clear();
    pendingTerminalBoundaryPayload_.clear();
    pendingVatspyPayload_.clear();
    hasPendingPayload_ = false;
    fetchInProgress_ = false;
}

void RouteSectorResolver::ResetSourceCaches() {
    ResetRuntimeState();

    std::lock_guard<std::mutex> lock(fetchMutex_);
    hasBoundaryCache_ = false;
    hasAuthorityCatalogCache_ = false;
    hasTerminalBoundaryCache_ = false;
    boundaryPayload_.clear();
    terminalBoundaryPayload_.clear();
    vatspyPayload_.clear();
    centerBoundaryGeneration_ = 0;
    authorityCatalogGeneration_ = 0;
    terminalBoundaryGeneration_ = 0;
}

void RouteSectorResolver::Reset() {
    ResetRuntimeState();
}

brain::RouteSectorSnapshot RouteSectorResolver::Resolve(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::NetworkPlanSnapshot& networkPlanSnapshot) const {
    if (networkPlanSnapshot.stale) {
        brain::RouteSectorSnapshot snapshot;
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.statusLine = networkPlanSnapshot.statusLine.empty()
                                  ? "ROUTE plan stale"
                                  : "ROUTE plan stale: " + networkPlanSnapshot.statusLine;
        return snapshot;
    }

    (void)RefreshBoundariesIfNeeded();
    const auto nowSeconds = CurrentTickSeconds();
    const auto centerAuthorityFresh = IsCenterAuthorityCacheFresh(nowSeconds);
    if (!hasBoundaryCache_) {
        brain::RouteSectorSnapshot snapshot;
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.statusLine = fetchInProgress_ ? "ROUTE sectors pending" : "ROUTE sectors unavailable";
        return snapshot;
    }

    const auto routeKey = BuildRouteCacheKey(networkPlanSnapshot);
    const auto routeSourceChanged =
        hasSnapshotCache_ &&
        (cachedSnapshot_.centerBoundaryGeneration != centerBoundaryGeneration_ ||
         cachedSnapshot_.authorityCatalogGeneration != authorityCatalogGeneration_);
    const auto movedDistanceNm =
        hasSnapshotCache_
            ? GreatCircleDistanceNm(
                  lastSnapshotLatitudeDeg_,
                  lastSnapshotLongitudeDeg_,
                  aircraftState.latitudeDeg,
                  aircraftState.longitudeDeg)
            : std::numeric_limits<double>::max();
    const auto routeChanged =
        !hasSnapshotCache_ || routeSourceChanged || routeKey != lastSnapshotRouteKey_;
    const auto snapshotCadenceSeconds = aircraftState.onGround
                                            ? kGroundSnapshotCadenceSeconds
                                            : kAirborneSnapshotCadenceSeconds;
    const auto cadenceExpired =
        !hasSnapshotCache_ ||
        (nowSeconds - lastSnapshotBuildTickSeconds_) >= snapshotCadenceSeconds;
    const auto movedEnough =
        !hasSnapshotCache_ || movedDistanceNm >= kSnapshotMovementThresholdNm;

    if (!routeChanged && !cadenceExpired && !movedEnough) {
        auto snapshot = cachedSnapshot_;
        snapshot.stale = !centerAuthorityFresh;
        if (snapshot.stale) {
            AppendStaleSuffixIfNeeded(&snapshot.statusLine);
        }
        return snapshot;
    }

    auto snapshot = BuildSnapshot(aircraftState, networkPlanSnapshot);
    snapshot.stale = !centerAuthorityFresh;
    if (snapshot.statusLine.empty()) {
        snapshot.statusLine = snapshot.routeResolved ? "ROUTE sectors active" : "ROUTE pending";
    }
    if (snapshot.stale) {
        AppendStaleSuffixIfNeeded(&snapshot.statusLine);
    }

    cachedSnapshot_ = snapshot;
    hasSnapshotCache_ = true;
    lastSnapshotBuildTickSeconds_ = nowSeconds;
    lastSnapshotLatitudeDeg_ = aircraftState.latitudeDeg;
    lastSnapshotLongitudeDeg_ = aircraftState.longitudeDeg;
    lastSnapshotRouteKey_ = routeKey;
    return snapshot;
}

brain::AirportSectorSnapshot RouteSectorResolver::ResolveAirportCoverage(
    const std::string& airportIcao,
    bool hasAirportCoordinates,
    double airportLatitudeDeg,
    double airportLongitudeDeg) const {
    (void)RefreshBoundariesIfNeeded();
    const auto nowSeconds = CurrentTickSeconds();
    const auto centerAuthorityFresh = IsCenterAuthorityCacheFresh(nowSeconds);
    const auto terminalFresh = IsTerminalBoundaryCacheFresh(nowSeconds);
    brain::AirportSectorSnapshot snapshot;
    snapshot.airportIcao = airportIcao;

    if (!hasAirportCoordinates || airportIcao.empty()) {
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.statusLine = "AIRPORT sectors waiting";
        return snapshot;
    }

    if (!hasBoundaryCache_ && !hasTerminalBoundaryCache_) {
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.statusLine =
            fetchInProgress_ ? "AIRPORT sectors pending" : "AIRPORT sectors unavailable";
        return snapshot;
    }

    const auto coverageKey = BuildAirportCoverageKey(
        airportIcao,
        airportLatitudeDeg,
        airportLongitudeDeg);
    const auto cachedCoverage = airportCoverageCache_.find(coverageKey);
    if (cachedCoverage != airportCoverageCache_.end()) {
        const auto& cachedSnapshot = cachedCoverage->second;
        const auto cachedSourceMatches =
            cachedSnapshot.centerBoundaryGeneration == centerBoundaryGeneration_ &&
            cachedSnapshot.authorityCatalogGeneration == authorityCatalogGeneration_ &&
            cachedSnapshot.terminalCoverageGeneration == terminalBoundaryGeneration_;
        if (cachedSourceMatches) {
            snapshot = cachedSnapshot;
            snapshot.stale = (hasBoundaryCache_ && !centerAuthorityFresh) ||
                             (hasTerminalBoundaryCache_ && !terminalFresh);
            if (snapshot.stale) {
                AppendStaleSuffixIfNeeded(&snapshot.statusLine);
            }
            return snapshot;
        }
        airportCoverageCache_.erase(cachedCoverage);
    }

    snapshot = BuildAirportCoverageSnapshot(
        airportIcao,
        airportLatitudeDeg,
        airportLongitudeDeg);
    snapshot.stale = (hasBoundaryCache_ && !centerAuthorityFresh) ||
                     (hasTerminalBoundaryCache_ && !terminalFresh);
    if (snapshot.statusLine.empty()) {
        snapshot.statusLine = snapshot.coveringSectors.empty()
                                  ? "AIRPORT sectors none"
                                  : "AIRPORT sectors active";
    }
    if (snapshot.stale) {
        AppendStaleSuffixIfNeeded(&snapshot.statusLine);
    }

    if (airportCoverageCache_.size() >= kAirportCoverageCacheLimit) {
        airportCoverageCache_.clear();
    }
    airportCoverageCache_[coverageKey] = snapshot;
    return snapshot;
}

bool RouteSectorResolver::CanEvaluateAirportTerminalCoverage(
    const brain::AirportSectorSnapshot& airportCoverageSnapshot) const {
    if (!airportCoverageSnapshot.hasTerminalCoverageData || !hasTerminalBoundaryCache_) {
        return false;
    }

    (void)RefreshBoundariesIfNeeded();
    if (!IsTerminalBoundaryCacheFresh(CurrentTickSeconds())) {
        return false;
    }

    if (airportCoverageSnapshot.terminalCoverageGeneration == 0 ||
        airportCoverageSnapshot.terminalCoverageGeneration != terminalBoundaryGeneration_) {
        return false;
    }

    return !GetCachedTerminalSectorFeatures(terminalBoundaryPayload_).empty();
}

bool RouteSectorResolver::IsInsideAirportTerminalCoverage(
    const brain::AirportSectorSnapshot& airportCoverageSnapshot,
    double aircraftLatitudeDeg,
    double aircraftLongitudeDeg) const {
    if (!CanEvaluateAirportTerminalCoverage(airportCoverageSnapshot)) {
        return false;
    }

    const auto& terminalFeatures = GetCachedTerminalSectorFeatures(terminalBoundaryPayload_);
    if (terminalFeatures.empty()) {
        return false;
    }

    std::unordered_set<std::string> relevantLabels;
    for (const auto& sector : airportCoverageSnapshot.coveringSectors) {
        const auto featureIt = std::find_if(
            terminalFeatures.begin(),
            terminalFeatures.end(),
            [&](const SectorFeature& feature) { return feature.label == sector.identifier; });
        if (featureIt != terminalFeatures.end()) {
            relevantLabels.insert(sector.identifier);
        }
    }

    if (relevantLabels.empty()) {
        return false;
    }

    const GeoPoint aircraftPoint{aircraftLatitudeDeg, aircraftLongitudeDeg};
    const auto aircraftMatches = ResolveContainingFeatures(aircraftPoint, terminalFeatures);
    for (const auto featureIndex : aircraftMatches) {
        if (relevantLabels.find(terminalFeatures[featureIndex].label) != relevantLabels.end()) {
            return true;
        }
    }

    return false;
}

std::vector<xvatsim::core::route::SectorFeature> ToTraversalFeatures(
    const std::vector<SectorFeature>& features) {
    std::vector<xvatsim::core::route::SectorFeature> traversalFeatures;
    traversalFeatures.reserve(features.size());

    for (const auto& feature : features) {
        xvatsim::core::route::SectorFeature traversalFeature;
        traversalFeature.label = feature.label;
        traversalFeature.tokens.assign(feature.tokens.begin(), feature.tokens.end());
        std::sort(traversalFeature.tokens.begin(), traversalFeature.tokens.end());
        traversalFeature.polygons.reserve(feature.polygons.size());
        for (const auto& polygon : feature.polygons) {
            xvatsim::core::route::SectorPolygon traversalPolygon;
            traversalPolygon.ring.reserve(polygon.ring.size());
            for (const auto& point : polygon.ring) {
                traversalPolygon.ring.push_back({
                    point.latitudeDeg,
                    point.longitudeDeg,
                });
            }
            traversalFeature.polygons.push_back(std::move(traversalPolygon));
        }
        traversalFeatures.push_back(std::move(traversalFeature));
    }

    return traversalFeatures;
}

void PopulateTraversalControllerPrefixes(
    std::vector<brain::RouteSectorMatchSnapshot>* sectors,
    const ControllerAuthorityCatalog& authorityCatalog) {
    if (sectors == nullptr) {
        return;
    }

    for (auto& sector : *sectors) {
        sector.controllerCallsignPatterns =
            ResolveAuthoritativeControllerCallsignPatterns(
                sector.identifier,
                sector.matchTokens,
                authorityCatalog);
        sector.controllerPrefixes =
            ResolveAuthoritativeControllerPrefixes(
                sector.identifier,
                sector.matchTokens,
                authorityCatalog);
    }
}

brain::RouteSectorSnapshot RouteSectorResolver::BuildSnapshot(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::NetworkPlanSnapshot& networkPlanSnapshot) const {
    brain::RouteSectorSnapshot snapshot;
    snapshot.available = true;
    snapshot.centerBoundaryGeneration = hasBoundaryCache_ ? centerBoundaryGeneration_ : 0;
    snapshot.authorityCatalogGeneration =
        hasAuthorityCatalogCache_ ? authorityCatalogGeneration_ : 0;

    if (!aircraftState.valid || !networkPlanSnapshot.hasDestinationCoordinates) {
        snapshot.statusLine = "ROUTE waiting for destination";
        return snapshot;
    }

    const auto& features = GetCachedSectorFeatures(boundaryPayload_);
    const auto& authorityCatalog = GetCachedControllerAuthorityCatalog(vatspyPayload_);
    if (features.empty()) {
        snapshot.statusLine = "ROUTE sectors unavailable";
        return snapshot;
    }

    RouteResolveDiagnostics diagnostics;
    snapshot.waypoints =
        ResolveRouteWaypoints(aircraftState, networkPlanSnapshot, &diagnostics);
    snapshot.routeResolved = snapshot.waypoints.size() >= 2;
    const auto directDistanceNm = GreatCircleDistanceNm(
        aircraftState.latitudeDeg,
        aircraftState.longitudeDeg,
        networkPlanSnapshot.destinationLatDeg,
        networkPlanSnapshot.destinationLonDeg);
    if (!snapshot.routeResolved) {
        snapshot.statusLine =
            AppendUnsupportedRouteStatus("ROUTE unresolved", diagnostics);
        LogRouteDiagnosticsIfChanged(
            networkPlanSnapshot,
            diagnostics,
            snapshot,
            directDistanceNm,
            0.0,
            "none",
            false);
        return snapshot;
    }

    const auto routeDistanceNm = RouteDistanceNm(snapshot.waypoints);
    xvatsim::core::route::TraversalTuning traversalTuning;
    traversalTuning.mode = xvatsim::core::route::TraversalMode::Exact;
    traversalTuning.routeSectorSanityLimit = kRouteSectorSanityLimit;
    auto traversalSnapshot =
        xvatsim::core::route::BuildRouteSectorSnapshotFromWaypoints(
            snapshot.waypoints,
            ToTraversalFeatures(features),
            traversalTuning);

    snapshot.currentSectors = std::move(traversalSnapshot.currentSectors);
    snapshot.nextSectors = std::move(traversalSnapshot.nextSectors);
    snapshot.routeResolved = traversalSnapshot.routeResolved;
    snapshot.statusLine =
        AppendUnsupportedRouteStatus(traversalSnapshot.statusLine, diagnostics);
    PopulateTraversalControllerPrefixes(&snapshot.currentSectors, authorityCatalog);
    PopulateTraversalControllerPrefixes(&snapshot.nextSectors, authorityCatalog);
    snapshot.statusLine = AppendAuthorityGapStatus(
        snapshot.statusLine,
        snapshot.currentSectors,
        snapshot.nextSectors);

    if (!snapshot.routeResolved) {
        LogRouteDiagnosticsIfChanged(
            networkPlanSnapshot,
            diagnostics,
            snapshot,
            directDistanceNm,
            routeDistanceNm,
            "exact",
            true);
        return snapshot;
    }
    LogRouteDiagnosticsIfChanged(
        networkPlanSnapshot,
        diagnostics,
        snapshot,
        directDistanceNm,
        routeDistanceNm,
        "exact",
        false);
    return snapshot;
}

brain::AirportSectorSnapshot RouteSectorResolver::BuildAirportCoverageSnapshot(
    const std::string& airportIcao,
    double airportLatitudeDeg,
    double airportLongitudeDeg) const {
    brain::AirportSectorSnapshot snapshot;
    snapshot.available = true;
    snapshot.airportIcao = airportIcao;
    snapshot.centerBoundaryGeneration = hasBoundaryCache_ ? centerBoundaryGeneration_ : 0;
    snapshot.authorityCatalogGeneration =
        hasAuthorityCatalogCache_ ? authorityCatalogGeneration_ : 0;
    snapshot.terminalCoverageGeneration =
        hasTerminalBoundaryCache_ ? terminalBoundaryGeneration_ : 0;
    const auto& authorityCatalog = GetCachedControllerAuthorityCatalog(vatspyPayload_);

    const GeoPoint airportPoint{airportLatitudeDeg, airportLongitudeDeg};
    std::size_t centerSectorCount = 0;
    std::size_t terminalSectorCount = 0;

    if (hasBoundaryCache_) {
        const auto& features = GetCachedSectorFeatures(boundaryPayload_);
        const auto matches = ResolveContainingFeatures(airportPoint, features);
        for (const auto featureIndex : matches) {
            const auto& feature = features[featureIndex];
            brain::RouteSectorMatchSnapshot sector;
            sector.identifier = feature.label;
            sector.entryDistanceNm = 0.0;
            sector.matchTokens.assign(feature.tokens.begin(), feature.tokens.end());
            std::sort(sector.matchTokens.begin(), sector.matchTokens.end());
            sector.controllerCallsignPatterns =
                ResolveAuthoritativeControllerCallsignPatterns(
                    feature.label,
                    feature.tokens,
                    authorityCatalog);
            sector.controllerPrefixes =
                ResolveAuthoritativeControllerPrefixes(
                    feature.label,
                    feature.tokens,
                    authorityCatalog);
            sector.centerCoverage = true;
            snapshot.coveringSectors.push_back(std::move(sector));
            ++centerSectorCount;
            snapshot.hasCenterCoverageData = true;
        }
    }

    if (hasTerminalBoundaryCache_) {
        const auto& terminalFeatures = GetCachedTerminalSectorFeatures(terminalBoundaryPayload_);
        const auto terminalMatches = ResolveContainingFeatures(airportPoint, terminalFeatures);
        for (const auto featureIndex : terminalMatches) {
            const auto& feature = terminalFeatures[featureIndex];
            brain::RouteSectorMatchSnapshot sector;
            sector.identifier = feature.label;
            sector.entryDistanceNm = 0.0;
            sector.matchTokens.assign(feature.tokens.begin(), feature.tokens.end());
            std::sort(sector.matchTokens.begin(), sector.matchTokens.end());
            sector.terminalCoverage = true;
            snapshot.coveringSectors.push_back(std::move(sector));
            ++terminalSectorCount;
            snapshot.hasTerminalCoverageData = true;
        }
    }

    if (!hasBoundaryCache_ && !hasTerminalBoundaryCache_) {
        snapshot.available = false;
        snapshot.statusLine = "AIRPORT sectors unavailable";
        return snapshot;
    }

    snapshot.statusLine =
        "AIRPORT " + airportIcao + " " +
        std::to_string(centerSectorCount) + " center " +
        std::to_string(terminalSectorCount) + " terminal sectors";
    return snapshot;
}

void RouteSectorResolver::StartAsyncBoundaryFetch(long long nowSeconds) const {
    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }

    lastFetchTickSeconds_ = nowSeconds;
    fetchInProgress_ = true;
    fetchThread_ = std::thread([this]() {
        const auto mapDataManifest = DownloadMapDataManifest();
        const auto payload = DownloadBoundaryPayload(mapDataManifest);
        const auto vatspyPayload = DownloadVatSpyDataPayload(mapDataManifest);
        const auto terminalPayload = DownloadTerminalBoundaryPayload();
        std::vector<unsigned char> boundaryPayload(payload.begin(), payload.end());
        std::vector<unsigned char> authorityCatalogPayload(
            vatspyPayload.begin(),
            vatspyPayload.end());
        std::vector<unsigned char> terminalBoundaryPayload(
            terminalPayload.begin(),
            terminalPayload.end());

        // Warm the polygon caches off the X-Plane thread so the first sector lookup
        // does not pay the GeoJSON parse cost during taxi or departure.
        if (!boundaryPayload.empty()) {
            (void)GetCachedSectorFeatures(boundaryPayload);
        }
        if (!authorityCatalogPayload.empty()) {
            (void)GetCachedControllerAuthorityCatalog(authorityCatalogPayload);
        }
        if (!terminalBoundaryPayload.empty()) {
            (void)GetCachedTerminalSectorFeatures(terminalBoundaryPayload);
        }

        StageFetchedPayloads(
            std::move(boundaryPayload),
            std::move(terminalBoundaryPayload),
            std::move(authorityCatalogPayload));
        fetchInProgress_ = false;
    });
}

void RouteSectorResolver::StageFetchedPayloads(
    std::vector<unsigned char> boundaryPayload,
    std::vector<unsigned char> terminalBoundaryPayload,
    std::vector<unsigned char> authorityCatalogPayload) const {
    std::lock_guard<std::mutex> lock(fetchMutex_);
    pendingBoundaryPayload_.clear();
    pendingVatspyPayload_.clear();
    pendingTerminalBoundaryPayload_.clear();

    if (!boundaryPayload.empty() && !authorityCatalogPayload.empty()) {
        pendingBoundaryPayload_ = std::move(boundaryPayload);
        pendingVatspyPayload_ = std::move(authorityCatalogPayload);
    }
    if (!terminalBoundaryPayload.empty()) {
        pendingTerminalBoundaryPayload_ = std::move(terminalBoundaryPayload);
    }
    hasPendingPayload_ = true;
}

void RouteSectorResolver::HarvestPendingFetch() const {
    if (fetchInProgress_) {
        return;
    }

    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }

    std::lock_guard<std::mutex> lock(fetchMutex_);
    if (!hasPendingPayload_) {
        return;
    }

    hasPendingPayload_ = false;
    const auto hasPendingCenterPackage =
        !pendingBoundaryPayload_.empty() && !pendingVatspyPayload_.empty();
    const auto hasPendingTerminalPackage = !pendingTerminalBoundaryPayload_.empty();
    if (!hasPendingCenterPackage && !hasPendingTerminalPackage) {
        lastFetchSucceeded_ = false;
        return;
    }

    if (hasPendingCenterPackage) {
        boundaryPayload_ = std::move(pendingBoundaryPayload_);
        vatspyPayload_ = std::move(pendingVatspyPayload_);
        hasBoundaryCache_ = true;
        hasAuthorityCatalogCache_ = true;
        ++centerBoundaryGeneration_;
        ++authorityCatalogGeneration_;
        if (centerBoundaryGeneration_ == 0) {
            centerBoundaryGeneration_ = 1;
        }
        if (authorityCatalogGeneration_ == 0) {
            authorityCatalogGeneration_ = 1;
        }
        lastSuccessfulCenterFetchTickSeconds_ = CurrentTickSeconds();
    }
    if (hasPendingTerminalPackage) {
        terminalBoundaryPayload_ = std::move(pendingTerminalBoundaryPayload_);
        hasTerminalBoundaryCache_ = true;
        ++terminalBoundaryGeneration_;
        if (terminalBoundaryGeneration_ == 0) {
            terminalBoundaryGeneration_ = 1;
        }
        lastSuccessfulTerminalFetchTickSeconds_ = CurrentTickSeconds();
    }
    pendingBoundaryPayload_.clear();
    pendingVatspyPayload_.clear();
    pendingTerminalBoundaryPayload_.clear();
    lastFetchSucceeded_ = hasPendingCenterPackage && hasPendingTerminalPackage;
    hasSnapshotCache_ = false;
    lastSnapshotRouteKey_.clear();
    airportCoverageCache_.clear();
}

bool RouteSectorResolver::IsCenterAuthorityCacheFresh(long long nowSeconds) const {
    if (!hasBoundaryCache_ ||
        !hasAuthorityCatalogCache_ ||
        lastSuccessfulCenterFetchTickSeconds_ <= 0) {
        return false;
    }

    const auto cacheAgeSeconds = nowSeconds - lastSuccessfulCenterFetchTickSeconds_;
    if (fetchInProgress_) {
        return cacheAgeSeconds <=
               (kRefreshCadenceSeconds + kInProgressCacheGraceSeconds);
    }

    return cacheAgeSeconds <= kRefreshCadenceSeconds;
}

bool RouteSectorResolver::IsTerminalBoundaryCacheFresh(long long nowSeconds) const {
    if (!hasTerminalBoundaryCache_ || lastSuccessfulTerminalFetchTickSeconds_ <= 0) {
        return false;
    }

    const auto cacheAgeSeconds = nowSeconds - lastSuccessfulTerminalFetchTickSeconds_;
    if (fetchInProgress_) {
        return cacheAgeSeconds <=
               (kRefreshCadenceSeconds + kInProgressCacheGraceSeconds);
    }

    return cacheAgeSeconds <= kRefreshCadenceSeconds;
}

bool RouteSectorResolver::RefreshBoundariesIfNeeded() const {
    const auto nowSeconds = CurrentTickSeconds();
    HarvestPendingFetch();

    const auto cadenceSeconds =
        lastFetchSucceeded_ ? kRefreshCadenceSeconds : kFailureBackoffSeconds;
    if ((hasBoundaryCache_ || hasTerminalBoundaryCache_) &&
        (nowSeconds - lastFetchTickSeconds_) < cadenceSeconds) {
        return lastFetchSucceeded_;
    }

    if (!hasBoundaryCache_ &&
        !hasTerminalBoundaryCache_ &&
        lastFetchTickSeconds_ != 0 &&
        (nowSeconds - lastFetchTickSeconds_) < cadenceSeconds) {
        return false;
    }

    if (!fetchInProgress_) {
        StartAsyncBoundaryFetch(nowSeconds);
    }

    return IsCenterAuthorityCacheFresh(nowSeconds) ||
           IsTerminalBoundaryCacheFresh(nowSeconds);
}

}  // namespace xvatsim::modules::route_sector
