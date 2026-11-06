#include "XVatsim/modules/route_sector/RouteSectorResolver.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <windows.h>
#include <winhttp.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

#include "XVatsim/brain/BrainOwnedWorkerTypes.h"
#include "XVatsim/core/ControllerAuthority.h"
#include "XVatsim/core/MapDataSource.h"
#include "XVatsim/core/RouteGrammar.h"
#include "XVatsim/core/RouteTraversal.h"
#include "XVatsim/modules/authority_map/CanonicalAuthorityMapCompiler.h"
#include "XVatsim/modules/authority_map/CanonicalAuthorityMapStore.h"

namespace xvatsim::modules::route_sector {

using CanonicalAuthorityMapDataset =
    xvatsim::core::authority::CanonicalAuthorityMapDataset;

namespace {

constexpr wchar_t kUserAgent[] = L"XVatsim/2.2.0";
constexpr wchar_t kVatsimMapDataManifestUrl[] =
    L"https://api.vatsim.net/api/map_data";
constexpr const char* kPackagedAuthoritySourceRegistryFile =
    "authority_source_registry.json";
constexpr long long kRefreshCadenceSeconds = 21600;
constexpr long long kFailureBackoffSeconds = 600;
constexpr long long kInProgressCacheGraceSeconds = 300;
constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusNm = 3440.065;
constexpr double kGroundSnapshotMovementThresholdNm = 2.0;
constexpr double kAirborneSnapshotMovementThresholdNm = 10.0;
constexpr std::size_t kAirportCoverageCacheLimit = 32;
constexpr std::size_t kRouteSectorSanityLimit = 30;
constexpr std::size_t kDiagnosticListLimit = 12;
constexpr std::size_t kDiagnosticTextLimit = 96;
constexpr std::size_t kDiagnosticRouteTextLimit = 512;
constexpr std::size_t kDiagnosticLogLineLimit = 4096;
constexpr double kAuthorityTerminalRouteScopeToleranceNm = 120.0;
constexpr double kAuthorityNearRouteWindowNm = 200.0;
constexpr double kAuthorityArrivalPrepDistanceNm = 200.0;
constexpr long long kAuthorityEmptyWindowWatchCadenceSeconds = 15;
constexpr long long kAuthorityControlledWindowWatchCadenceSeconds = 60;
constexpr int kVatsimFlightServiceFacility = 1;
constexpr int kVatsimDeliveryFacility = 2;
constexpr int kVatsimGroundFacility = 3;
constexpr int kVatsimTowerFacility = 4;
constexpr int kVatsimApproachFacility = 5;
constexpr int kVatsimCenterFacility = 6;

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
    std::vector<std::vector<GeoPoint>> holes;
};

struct SectorFeature {
    std::string label;
    std::string sourceFeatureId;
    std::string name;
    std::vector<xvatsim::core::authority::AuthorityServiceRole> serviceRoles;
    std::unordered_set<std::string> tokens;
    std::vector<SectorPolygon> polygons;
};

struct ControllerAuthorityCatalog {
    std::unordered_map<std::string, std::vector<std::string>> prefixesByKey;
    std::unordered_map<std::string, std::vector<std::string>> callsignPatternsByKey;
    std::vector<xvatsim::core::authority::AirportCallsignAlias>
        airportCallsignAliases;
};

bool IsAuthorityControllerCandidate(const brain::ControllerSnapshot& controller);
double GreatCircleDistanceNm(
    double latitude1Deg,
    double longitude1Deg,
    double latitude2Deg,
    double longitude2Deg);
double RouteDistanceNm(const std::vector<brain::RouteWaypointSnapshot>& waypoints);
const xvatsim::core::authority::AuthorityPolygon* FindAuthorityPolygonById(
    const xvatsim::core::authority::AuthorityPolygonCatalog& catalog,
    const std::string& polygonId);
std::string AuthorityProofSourceLabel(
    const xvatsim::core::authority::ActiveAuthorityPolygon& activePolygon);
bool RouteCancellationRequested(
    const RouteCancellationToken* cancellation);
std::string AuthorityRelevanceProofDetail(
    const xvatsim::core::authority::RelevantAuthorityPolygon&
        relevantAuthorityPolygon);
void MarkAuthorityEvidenceRelevantSurvivors(
    brain::AuthorityRelevanceSnapshot* snapshot,
    const std::vector<xvatsim::core::authority::RelevantAuthorityPolygon>&
        relevantAuthorityPolygons);
double NormalizeLongitudeDeg(double longitudeDeg);
double UnwrapLongitudeRelativeDeg(double referenceLongitudeDeg, double longitudeDeg);
double DistanceFromPointToAuthorityPolygonNm(
    const xvatsim::core::authority::GeoPoint& point,
    const xvatsim::core::authority::AuthorityPolygon& polygon);
Vector3 AddVector(const Vector3& a, const Vector3& b);
Vector3 ScaleVector(const Vector3& vector, double scale);
Vector3 ToUnitVector(const GeoPoint& point);
GeoPoint ToGeoPoint(const Vector3& vector);
double AngularDistanceRad(const Vector3& a, const Vector3& b);

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
    std::vector<std::string> unresolvedAirwayTokens;
};

bool TokenCanActAsPoint(const xvatsim::core::route::ParsedRouteToken& token);
bool TokenCanActAsAirway(const xvatsim::core::route::ParsedRouteToken& token);
void SafeXPlaneDebugString(const std::string& message);
std::string GetXPlaneRootPath();
std::string AuthorityFamilyKey(std::string key);
std::string SanitizeDiagnosticText(std::string_view value, std::size_t maxChars);
std::string SummarizeStrings(const std::vector<std::string>& values);
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

class LocalRoutePreparationSource final : public RoutePreparationSource {
public:
    explicit LocalRoutePreparationSource(
        bool verifyPreflightSourceFile = true)
        : verifyPreflightSourceFile_(verifyPreflightSourceFile) {}

    std::string ReadTextFile(
        const std::filesystem::path& path) const override {
        std::ifstream stream(path, std::ios::in | std::ios::binary);
        if (!stream.is_open()) {
            return {};
        }
        std::ostringstream buffer;
        buffer << stream.rdbuf();
        return buffer.str();
    }

    std::vector<std::filesystem::path> ListRegularFiles(
        const std::filesystem::path& directory) const override {
        std::vector<std::filesystem::path> paths;
        std::error_code ec;
        if (!std::filesystem::exists(directory, ec) ||
            !std::filesystem::is_directory(directory, ec)) {
            return paths;
        }
        std::filesystem::directory_iterator iterator(directory, ec);
        const std::filesystem::directory_iterator end;
        for (; !ec && iterator != end; iterator.increment(ec)) {
            std::error_code entryError;
            if (iterator->is_regular_file(entryError)) {
                paths.push_back(iterator->path());
            }
        }
        return paths;
    }

    core::preflight::FmsParseResult LoadFmsPlan(
        const std::filesystem::path& path) const override {
        return core::preflight::LoadFmsPlanFile(path);
    }

    core::preflight::CacheValidationResult ValidatePreflightCandidate(
        const core::preflight::PreflightRouteCache& candidate,
        const brain::NetworkPlanSnapshot& networkPlan) const override {
        return core::preflight::ValidatePreflightRouteCacheForNetworkPlan(
            candidate, networkPlan, verifyPreflightSourceFile_);
    }

private:
    bool verifyPreflightSourceFile_ = true;
};

struct RoutePreparationEngineState {
    std::string xplaneRootPath;
    std::shared_ptr<const RoutePreparationSource> source;
    std::unordered_map<
        std::string,
        std::unordered_map<std::string, xvatsim::core::route::ProcedureCatalogEntry>>
        procedureMetadataByAirport;
    AirwayGraph airwayGraph;
    bool airwayGraphInitialized = false;
};

thread_local RoutePreparationEngineState* gActiveRoutePreparationState = nullptr;

class ScopedRoutePreparationState {
public:
    explicit ScopedRoutePreparationState(RoutePreparationEngineState* state)
        : previous_(gActiveRoutePreparationState) {
        gActiveRoutePreparationState = state;
    }

    ~ScopedRoutePreparationState() {
        gActiveRoutePreparationState = previous_;
    }

    ScopedRoutePreparationState(const ScopedRoutePreparationState&) = delete;
    ScopedRoutePreparationState& operator=(
        const ScopedRoutePreparationState&) = delete;

private:
    RoutePreparationEngineState* previous_ = nullptr;
};

RoutePreparationEngineState& ActiveRoutePreparationState() {
    if (gActiveRoutePreparationState == nullptr) {
        throw std::logic_error("route-preparation-state-unavailable");
    }
    return *gActiveRoutePreparationState;
}

struct ResolvedRoutePoint {
    std::string ident;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    std::optional<std::size_t> graphNodeIndex;
};

struct RouteScopedAuthorityPolygon {
    const xvatsim::core::authority::AuthorityPolygon* polygon = nullptr;
    bool aircraftInside = false;
    bool routeIntersects = false;
    double routeEntryDistanceNm = 0.0;
};

struct AuthorityRelevanceWorkScope {
    brain::RouteSectorSnapshot routeSectorSnapshot;
    std::vector<xvatsim::core::authority::GeoPoint> routePoints;
    bool includeDepartureEndpoint = false;
    bool includeDestinationEndpoint = false;
    std::size_t deferredSectorCount = 0;
    double windowNm = kAuthorityNearRouteWindowNm;
    std::string stage = "LOCAL_BOOTSTRAP";
};

struct AuthorityRelevanceScopeArtifacts {
    bool valid = false;
    std::size_t signature = 0;
    xvatsim::core::authority::ControllerAuthorityCatalog controllerAuthorityCatalog;
    xvatsim::core::authority::AuthorityPolygonCatalog authorityPolygonCatalog;
    std::unordered_map<std::string, std::vector<std::size_t>>
        authorityPolygonExactIndexesByKey;
    std::unordered_map<std::string, std::vector<std::size_t>>
        authorityPolygonFamilyIndexesByKey;
    std::unordered_set<std::string> routeAuthorityPolygonKeys;
    std::unordered_set<std::string> routeAuthorityMatchKeys;
    xvatsim::core::authority::ControllerAuthorityCatalog routeScopedControllerAuthorityCatalog;
    xvatsim::core::authority::AuthorityPolygonCatalog routeScopedAuthorityPolygonCatalog;
    std::vector<RouteScopedAuthorityPolygon> routeScopedAuthorityPolygons;
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

std::string ReadLocalTextFile(const std::string& path) {
    std::ifstream stream(path, std::ios::in | std::ios::binary);
    if (!stream.is_open()) {
        return {};
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

std::string ParentDirectory(std::string path) {
    const auto separator = path.find_last_of("\\/");
    if (separator == std::string::npos) {
        return {};
    }
    return path.substr(0, separator + 1);
}

std::string GetCurrentModuleDirectoryPath() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&GetCurrentModuleDirectoryPath),
            &module) ||
        module == nullptr) {
        return {};
    }

    std::vector<char> path(MAX_PATH);
    DWORD size = 0;
    for (;;) {
        size = GetModuleFileNameA(
            module,
            path.data(),
            static_cast<DWORD>(path.size()));
        if (size == 0) {
            return {};
        }
        if (size < path.size() - 1) {
            break;
        }
        path.resize(path.size() * 2);
    }

    return ParentDirectory(std::string(path.data(), size));
}

std::vector<std::string> ResolvePackagedAuthoritySourceRegistryPaths() {
    std::vector<std::string> paths;
    const auto moduleDirectory = GetCurrentModuleDirectoryPath();
    if (!moduleDirectory.empty()) {
        paths.push_back(moduleDirectory + kPackagedAuthoritySourceRegistryFile);
        paths.push_back(
            moduleDirectory +
            std::string("source_data\\") +
            kPackagedAuthoritySourceRegistryFile);
    }

    const auto xplaneRoot = GetXPlaneRootPath();
    if (!xplaneRoot.empty()) {
        paths.push_back(
            xplaneRoot +
            std::string("Resources\\plugins\\XVatsim\\win_x64\\") +
            kPackagedAuthoritySourceRegistryFile);
        paths.push_back(
            xplaneRoot +
            std::string("Resources\\plugins\\XVatsim\\win_x64\\source_data\\") +
            kPackagedAuthoritySourceRegistryFile);
    }

    paths.push_back(
        std::string("assets\\source_data\\") +
        kPackagedAuthoritySourceRegistryFile);
    return paths;
}

std::string LoadPackagedAuthoritySourceRegistryPayload() {
    for (const auto& path : ResolvePackagedAuthoritySourceRegistryPaths()) {
        const auto payload = ReadLocalTextFile(path);
        if (!payload.empty()) {
            return payload;
        }
    }
    return {};
}

std::string JoinUrlPath(std::string baseUrl, const std::string& relativePath) {
    while (!baseUrl.empty() && baseUrl.back() == '/') {
        baseUrl.pop_back();
    }
    std::string path = relativePath;
    while (!path.empty() && path.front() == '/') {
        path.erase(path.begin());
    }
    if (baseUrl.empty() || path.empty()) {
        return {};
    }
    return baseUrl + "/" + path;
}

std::string ResolveVatGlassesPositionsUrl(
    const xvatsim::core::source_data::MapDataManifest& manifest) {
    if (!manifest.vatglassesPositionsUrl.empty()) {
        return manifest.vatglassesPositionsUrl;
    }
    if (manifest.vatglassesDynamicBaseUrl.empty()) {
        return {};
    }
    return JoinUrlPath(manifest.vatglassesDynamicBaseUrl, "positions.json");
}

std::string ResolveVatGlassesAirspaceUrl(
    const xvatsim::core::source_data::MapDataManifest& manifest) {
    if (!manifest.vatglassesAirspaceUrl.empty()) {
        return manifest.vatglassesAirspaceUrl;
    }
    if (manifest.vatglassesDynamicBaseUrl.empty()) {
        return {};
    }
    return JoinUrlPath(manifest.vatglassesDynamicBaseUrl, "airspace.json");
}

std::string ResolveVatGlassesDynamicOwnershipUrl(
    const xvatsim::core::source_data::MapDataManifest& manifest) {
    if (!manifest.vatglassesDynamicOwnershipUrl.empty()) {
        return manifest.vatglassesDynamicOwnershipUrl;
    }
    if (manifest.vatglassesDynamicBaseUrl.empty()) {
        return {};
    }
    const auto ownershipFile =
        manifest.vatglassesDynamicOwnershipFile.empty()
            ? std::string("default.json")
            : manifest.vatglassesDynamicOwnershipFile;
    return JoinUrlPath(
        manifest.vatglassesDynamicBaseUrl,
        JoinUrlPath("ownership", ownershipFile));
}

std::vector<std::string> ResolveSpecialSectorDataUrls(
    const xvatsim::core::source_data::MapDataManifest& manifest) {
    std::vector<std::string> urls;
    auto addUrl = [&urls](const std::string& url) {
        if (url.empty() ||
            std::find(urls.begin(), urls.end(), url) != urls.end()) {
            return;
        }
        urls.push_back(url);
    };

    addUrl(manifest.specialSectorDataUrl);
    for (const auto& url : manifest.specialSectorDataUrls) {
        addUrl(url);
    }
    return urls;
}

std::vector<std::string> ResolveTerminalAuthorityDataUrls(
    const xvatsim::core::source_data::MapDataManifest& manifest) {
    std::vector<std::string> urls;
    auto addUrl = [&urls](const std::string& url) {
        if (url.empty() ||
            std::find(urls.begin(), urls.end(), url) != urls.end()) {
            return;
        }
        urls.push_back(url);
    };

    addUrl(manifest.terminalAuthorityDataUrl);
    for (const auto& url : manifest.terminalAuthorityDataUrls) {
        addUrl(url);
    }
    return urls;
}

std::vector<std::string> ResolveAuthoritySourceRegistryUrls(
    const xvatsim::core::source_data::MapDataManifest& manifest) {
    std::vector<std::string> urls;
    auto addUrl = [&urls](const std::string& url) {
        if (url.empty() ||
            std::find(urls.begin(), urls.end(), url) != urls.end()) {
            return;
        }
        urls.push_back(url);
    };

    addUrl(manifest.authoritySourceRegistryUrl);
    for (const auto& url : manifest.authoritySourceRegistryUrls) {
        addUrl(url);
    }
    return urls;
}

std::vector<std::string> ResolveAuthoritySourceRegistryPayloads(
    const xvatsim::core::source_data::MapDataManifest& manifest) {
    std::vector<std::string> payloads;
    for (const auto& registryUrl : ResolveAuthoritySourceRegistryUrls(manifest)) {
        const auto registryPayload = DownloadHttpsPayload(WidenUrl(registryUrl));
        if (!registryPayload.empty()) {
            payloads.push_back(registryPayload);
        }
    }

    // The packaged registry is the worldwide static-source fallback. It is
    // read once by the async source fetcher, then compiled and cache-warmed
    // off-thread; loading it does not activate a live controller.
    if (payloads.empty()) {
        const auto packagedPayload = LoadPackagedAuthoritySourceRegistryPayload();
        if (!packagedPayload.empty()) {
            payloads.push_back(packagedPayload);
        }
    }
    return payloads;
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

std::string DownloadTerminalBoundaryPayload(
    const xvatsim::core::source_data::MapDataManifest& manifest) {
    if (!manifest.valid || manifest.simawareTraconGeoJsonUrl.empty()) {
        return {};
    }
    return DownloadHttpsPayload(WidenUrl(manifest.simawareTraconGeoJsonUrl));
}

std::string DownloadOwnershipPayload(
    const xvatsim::core::source_data::MapDataManifest& manifest) {
    if (!manifest.valid) {
        return {};
    }

    std::string primaryPayload;
    const auto positionsUrl = ResolveVatGlassesPositionsUrl(manifest);
    const auto airspaceUrl = ResolveVatGlassesAirspaceUrl(manifest);
    const auto dynamicOwnershipUrl = ResolveVatGlassesDynamicOwnershipUrl(manifest);
    if (!positionsUrl.empty() &&
        !airspaceUrl.empty() &&
        !dynamicOwnershipUrl.empty()) {
        const auto positionsPayload = DownloadHttpsPayload(WidenUrl(positionsUrl));
        const auto airspacePayload = DownloadHttpsPayload(WidenUrl(airspaceUrl));
        const auto ownershipPayload = DownloadHttpsPayload(WidenUrl(dynamicOwnershipUrl));
        primaryPayload =
            xvatsim::core::source_data::BuildVatGlassesDynamicSourcePayload(
                positionsPayload,
                airspacePayload,
                ownershipPayload);
    }

    if (primaryPayload.empty() && !manifest.vatglassesOwnershipUrl.empty()) {
        primaryPayload = DownloadHttpsPayload(WidenUrl(manifest.vatglassesOwnershipUrl));
    }

    std::vector<std::string> supplementalPayloads;
    std::unordered_set<std::string> downloadedSupplementalUrls;
    auto downloadSupplementalPayload = [&](const std::string& url) {
        if (url.empty() || !downloadedSupplementalUrls.insert(url).second) {
            return;
        }
        const auto payload = DownloadHttpsPayload(WidenUrl(url));
        if (!payload.empty()) {
            supplementalPayloads.push_back(payload);
        }
    };

    for (const auto& registryPayload : ResolveAuthoritySourceRegistryPayloads(manifest)) {
        for (const auto& entry :
             xvatsim::core::source_data::ParseAuthoritySourceRegistryJson(
                 registryPayload)) {
            if (entry.source == "VATGLASSES_DYNAMIC_DIRECTORY") {
                const auto positionsPayload =
                    DownloadHttpsPayload(WidenUrl(entry.positionsUrl));
                const auto airspacePayload =
                    DownloadHttpsPayload(WidenUrl(entry.airspaceUrl));
                const auto ownershipPayload =
                    DownloadHttpsPayload(WidenUrl(entry.ownershipUrl));
                const auto packagePayload =
                    xvatsim::core::source_data::BuildVatGlassesDynamicSourcePayload(
                        positionsPayload,
                        airspacePayload,
                        ownershipPayload);
                if (!packagePayload.empty()) {
                    supplementalPayloads.push_back(packagePayload);
                }
                continue;
            }
            downloadSupplementalPayload(entry.url);
        }
    }
    for (const auto& url : ResolveSpecialSectorDataUrls(manifest)) {
        downloadSupplementalPayload(url);
    }
    for (const auto& url : ResolveTerminalAuthorityDataUrls(manifest)) {
        downloadSupplementalPayload(url);
    }

    return xvatsim::core::source_data::BuildAuthoritySourcePackagePayload(
        primaryPayload,
        supplementalPayloads);
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

void ParseInteriorRings(
    const winrt::Windows::Data::Json::JsonArray& polygonArray,
    SectorPolygon* polygon) {
    if (polygon == nullptr) {
        return;
    }
    for (uint32_t ringIndex = 1; ringIndex < polygonArray.Size(); ++ringIndex) {
        SectorPolygon hole;
        ParseRing(polygonArray.GetArrayAt(ringIndex), &hole);
        if (hole.ring.size() >= 3) {
            polygon->holes.push_back(std::move(hole.ring));
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
                ParseInteriorRings(coordinates, &polygon);
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
                    ParseInteriorRings(polygonArray, &polygon);
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
    feature.sourceFeatureId = !id.empty() ? id : name;
    feature.name = name;
    feature.label = !id.empty() ? id : (!name.empty() ? name : "TRACON");
    if (!suffix.empty()) {
        feature.label += "_" + NormalizeCompactToken(suffix);
        feature.serviceRoles.push_back(
            NormalizeCompactToken(suffix) == "DEP"
                ? xvatsim::core::authority::AuthorityServiceRole::Departure
                : xvatsim::core::authority::AuthorityServiceRole::Approach);
    } else {
        feature.serviceRoles.push_back(
            xvatsim::core::authority::AuthorityServiceRole::Approach);
        feature.serviceRoles.push_back(
            xvatsim::core::authority::AuthorityServiceRole::Departure);
    }

    const auto prefixes = ExtractStringList(properties, L"prefix");
    for (const auto& prefix : prefixes) {
        AddTerminalPrefixTokens(prefix, suffix, &feature.tokens);
    }
    // A SimAware feature id names the facility/group, not another child
    // position.  When child prefixes exist, keep the group identity on the
    // polygon so a live group callsign can activate it as a parent; do not
    // manufacture that group callsign as a duplicate exact child position.
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
        ParseInteriorRings(coordinates, &polygon);
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
            ParseInteriorRings(polygonArray, &polygon);
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

    patterns->insert(normalizedBase);
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

std::string NormalizeControllerAuthorityPattern(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const auto character : value) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0 ||
            character == '_' || character == '-' || character == '*') {
            normalized.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(character))));
        }
    }
    return normalized;
}

void MergeExplicitAuthorityPatterns(
    std::unordered_map<std::string, std::unordered_set<std::string>>* patternsByKey,
    const std::string& lookupKey,
    const std::vector<std::string>& patterns) {
    if (patternsByKey == nullptr) {
        return;
    }

    const auto normalizedLookupKey = NormalizeAuthorityCatalogKey(lookupKey);
    if (normalizedLookupKey.empty()) {
        return;
    }

    auto& resolvedPatterns = (*patternsByKey)[normalizedLookupKey];
    for (const auto& pattern : patterns) {
        const auto normalizedPattern = NormalizeControllerAuthorityPattern(pattern);
        if (!normalizedPattern.empty()) {
            resolvedPatterns.insert(normalizedPattern);
        }
    }
}

void MergeOwnershipAuthorityRecords(
    std::unordered_map<std::string, std::unordered_set<std::string>>* patternsByKey,
    const std::string& ownershipPayload) {
    if (patternsByKey == nullptr || ownershipPayload.empty()) {
        return;
    }

    auto mergeRecords = [&](
        const std::vector<xvatsim::core::authority::AuthorityPositionSourceRecord>& records) {
        for (const auto& record : records) {
            if (record.kind != xvatsim::core::authority::AuthorityKind::Center) {
                continue;
            }
            MergeExplicitAuthorityPatterns(
                patternsByKey,
                record.polygonKey,
                record.controllerCallsignPatterns);
        }
    };

    mergeRecords(
        xvatsim::core::authority::ParseAuthorityPositionSourceRecordsJson(
            xvatsim::core::authority::AuthoritySource::VatGlasses,
            ownershipPayload));
    mergeRecords(
        xvatsim::core::authority::ParseAuthorityPositionSourceRecordsJson(
            xvatsim::core::authority::AuthoritySource::VatsimRadarExtension,
            ownershipPayload));
    mergeRecords(
        xvatsim::core::authority::ParseAuthorityPositionSourceRecordsJson(
            xvatsim::core::authority::AuthoritySource::SpecialSectorData,
            ownershipPayload));
}

bool SplitTerminalAuthorityToken(
    const std::string& rawToken,
    std::string* outPrefix,
    std::string* outSuffix) {
    const auto token = NormalizeAuthorityCatalogKey(rawToken);
    const auto separatorIndex = token.rfind('_');
    if (separatorIndex == std::string::npos ||
        separatorIndex == 0 ||
        separatorIndex >= token.size() - 1) {
        return false;
    }

    const auto suffix = token.substr(separatorIndex + 1);
    if (suffix != "APP" && suffix != "DEP") {
        return false;
    }

    if (outPrefix != nullptr) {
        *outPrefix = token.substr(0, separatorIndex);
    }
    if (outSuffix != nullptr) {
        *outSuffix = suffix;
    }
    return true;
}

std::vector<std::string> TerminalAuthorityTokens(const SectorFeature& feature) {
    std::vector<std::string> tokens;
    for (const auto& rawToken : feature.tokens) {
        std::string prefix;
        std::string suffix;
        if (!SplitTerminalAuthorityToken(rawToken, &prefix, &suffix)) {
            continue;
        }
        tokens.push_back(prefix + "_" + suffix);
    }
    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
    return tokens;
}

std::vector<std::string> TerminalAuthorityCallsignPatterns(const std::string& token) {
    std::string prefix;
    std::string suffix;
    if (!SplitTerminalAuthorityToken(token, &prefix, &suffix)) {
        return {};
    }

    return {
        prefix + "_" + suffix,
        prefix + "_*_" + suffix,
    };
}

void MergeTerminalAuthorityFeatures(
    std::unordered_map<std::string, std::unordered_set<std::string>>* prefixesByKey,
    std::unordered_map<std::string, std::unordered_set<std::string>>* patternsByKey,
    const std::vector<SectorFeature>& terminalFeatures) {
    if (prefixesByKey == nullptr || patternsByKey == nullptr) {
        return;
    }

    for (const auto& feature : terminalFeatures) {
        const auto featureKey = NormalizeAuthorityCatalogKey(feature.label);
        for (const auto& token : TerminalAuthorityTokens(feature)) {
            std::string prefix;
            std::string suffix;
            if (!SplitTerminalAuthorityToken(token, &prefix, &suffix)) {
                continue;
            }

            const std::vector<std::string> keys = featureKey.empty()
                                                     ? std::vector<std::string>{token}
                                                     : std::vector<std::string>{featureKey, token};
            for (const auto& key : keys) {
                MergeControllerAuthorityPrefixes(prefixesByKey, key, {prefix});
                MergeExplicitAuthorityPatterns(
                    patternsByKey,
                    key,
                    TerminalAuthorityCallsignPatterns(token));
            }
        }
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
    catalog.airportCallsignAliases =
        xvatsim::core::authority::CompileVatSpyAuthorityCatalog(payload)
            .airportCallsignAliases;

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

ControllerAuthorityCatalog ParseControllerAuthorityCatalog(
    const std::string& vatspyPayload,
    const std::string& ownershipPayload,
    const std::vector<SectorFeature>& terminalFeatures = {}) {
    auto catalog = ParseControllerAuthorityCatalog(vatspyPayload);

    std::unordered_map<std::string, std::unordered_set<std::string>> prefixesByKey;
    for (const auto& [key, prefixes] : catalog.prefixesByKey) {
        auto& mergedPrefixes = prefixesByKey[key];
        mergedPrefixes.insert(prefixes.begin(), prefixes.end());
    }

    std::unordered_map<std::string, std::unordered_set<std::string>> patternsByKey;
    for (const auto& [key, patterns] : catalog.callsignPatternsByKey) {
        auto& mergedPatterns = patternsByKey[key];
        mergedPatterns.insert(patterns.begin(), patterns.end());
    }

    MergeTerminalAuthorityFeatures(&prefixesByKey, &patternsByKey, terminalFeatures);
    MergeOwnershipAuthorityRecords(&patternsByKey, ownershipPayload);

    catalog.prefixesByKey.clear();
    for (auto& [key, prefixes] : prefixesByKey) {
        std::vector<std::string> normalizedPrefixes(prefixes.begin(), prefixes.end());
        std::sort(normalizedPrefixes.begin(), normalizedPrefixes.end());
        catalog.prefixesByKey.emplace(std::move(key), std::move(normalizedPrefixes));
    }

    catalog.callsignPatternsByKey.clear();
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

std::size_t HashPayloads(
    const std::vector<unsigned char>& left,
    const std::vector<unsigned char>& right) {
    std::size_t hash = 1469598103934665603ull;
    auto appendByte = [&](unsigned char byte) {
        hash ^= static_cast<std::size_t>(byte);
        hash *= 1099511628211ull;
    };

    for (const auto byte : left) {
        appendByte(byte);
    }
    appendByte(0xff);
    appendByte(0x00);
    for (const auto byte : right) {
        appendByte(byte);
    }
    return hash;
}

std::size_t HashPayloads(
    const std::vector<unsigned char>& left,
    const std::vector<unsigned char>& middle,
    const std::vector<unsigned char>& right) {
    std::size_t hash = 1469598103934665603ull;
    auto appendByte = [&](unsigned char byte) {
        hash ^= static_cast<std::size_t>(byte);
        hash *= 1099511628211ull;
    };

    for (const auto byte : left) {
        appendByte(byte);
    }
    appendByte(0xff);
    appendByte(0x00);
    for (const auto byte : middle) {
        appendByte(byte);
    }
    appendByte(0xfe);
    appendByte(0x00);
    for (const auto byte : right) {
        appendByte(byte);
    }
    return hash;
}

void HashCombine(std::size_t* hash, std::size_t value) {
    if (hash == nullptr) {
        return;
    }
    *hash ^= value + 0x9e3779b97f4a7c15ull + (*hash << 6) + (*hash >> 2);
}

void HashCombineString(std::size_t* hash, const std::string& value) {
    if (hash == nullptr) {
        return;
    }
    for (const auto character : value) {
        *hash ^= static_cast<std::size_t>(static_cast<unsigned char>(character));
        *hash *= 1099511628211ull;
    }
}

void HashCombineDouble(std::size_t* hash, double value) {
    if (hash == nullptr) {
        return;
    }
    const auto rounded = static_cast<long long>(std::llround(value * 1000000.0));
    HashCombine(hash, static_cast<std::size_t>(rounded));
}

void AddAuthorityWorkScopeSignature(
    std::size_t* hash,
    const brain::AircraftStateSnapshot& aircraftState,
    const AuthorityRelevanceWorkScope& workScope,
    std::uint64_t terminalBoundaryGeneration) {
    if (hash == nullptr) {
        return;
    }

    HashCombine(hash, static_cast<std::size_t>(aircraftState.valid ? 1 : 0));
    HashCombine(hash, static_cast<std::size_t>(aircraftState.onGround ? 1 : 0));
    HashCombine(hash, static_cast<std::size_t>(terminalBoundaryGeneration));
    HashCombine(hash, static_cast<std::size_t>(workScope.includeDepartureEndpoint ? 1 : 0));
    HashCombine(hash, static_cast<std::size_t>(workScope.includeDestinationEndpoint ? 1 : 0));
    HashCombine(hash, static_cast<std::size_t>(workScope.deferredSectorCount));
    HashCombineDouble(hash, workScope.windowNm);
    HashCombineString(hash, workScope.stage);
}

std::string PayloadToString(const std::vector<unsigned char>& payload) {
    if (payload.empty()) {
        return {};
    }

    return std::string(
        reinterpret_cast<const char*>(payload.data()),
        payload.size());
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
    const std::vector<unsigned char>& vatspyPayload,
    const std::vector<unsigned char>& terminalPayload,
    const std::vector<unsigned char>& ownershipPayload) {
    static std::mutex cacheMutex;
    static std::size_t cachedHash = 0;
    static std::size_t cachedVatspySize = 0;
    static std::size_t cachedTerminalSize = 0;
    static std::size_t cachedOwnershipSize = 0;
    static ControllerAuthorityCatalog cachedCatalog;

    const auto payloadHash = HashPayloads(vatspyPayload, terminalPayload, ownershipPayload);
    std::lock_guard<std::mutex> lock(cacheMutex);
    if (payloadHash == cachedHash &&
        vatspyPayload.size() == cachedVatspySize &&
        terminalPayload.size() == cachedTerminalSize &&
        ownershipPayload.size() == cachedOwnershipSize) {
        return cachedCatalog;
    }

    cachedHash = payloadHash;
    cachedVatspySize = vatspyPayload.size();
    cachedTerminalSize = terminalPayload.size();
    cachedOwnershipSize = ownershipPayload.size();
    cachedCatalog = ParseControllerAuthorityCatalog(
        PayloadToString(vatspyPayload),
        PayloadToString(ownershipPayload),
        GetCachedTerminalSectorFeatures(terminalPayload));
    return cachedCatalog;
}

const ControllerAuthorityCatalog& GetCachedControllerAuthorityCatalog(
    const std::vector<unsigned char>& vatspyPayload,
    const std::vector<unsigned char>& ownershipPayload) {
    static const std::vector<unsigned char> kEmptyTerminalPayload;
    return GetCachedControllerAuthorityCatalog(
        vatspyPayload,
        kEmptyTerminalPayload,
        ownershipPayload);
}

std::vector<xvatsim::core::authority::AuthorityPositionSourceRecord>
ParseOwnershipAuthoritySourceRecords(const std::string& ownershipPayload) {
    if (ownershipPayload.empty()) {
        return {};
    }

    auto records = xvatsim::core::authority::ParseAuthorityPositionSourceRecordsJson(
        xvatsim::core::authority::AuthoritySource::VatGlasses,
        ownershipPayload);
    auto terminalRecords =
        xvatsim::core::authority::ParseAuthorityPositionSourceRecordsJson(
            xvatsim::core::authority::AuthoritySource::SimAwareTracon,
            ownershipPayload);
    records.insert(
        records.end(),
        std::make_move_iterator(terminalRecords.begin()),
        std::make_move_iterator(terminalRecords.end()));
    auto specialRecords =
        xvatsim::core::authority::ParseAuthorityPositionSourceRecordsJson(
            xvatsim::core::authority::AuthoritySource::SpecialSectorData,
            ownershipPayload);
    records.insert(
        records.end(),
        std::make_move_iterator(specialRecords.begin()),
        std::make_move_iterator(specialRecords.end()));
    return records;
}

std::vector<xvatsim::core::authority::AuthorityPositionSourceRecord>
AdaptSimAwareTerminalPositionSourceRecords(
    const std::vector<SectorFeature>& terminalFeatures) {
    std::vector<xvatsim::core::authority::AuthorityPositionSourceRecord> records;
    std::unordered_set<std::string> insertedIds;

    for (const auto& feature : terminalFeatures) {
        const auto featureKey =
            xvatsim::core::authority::NormalizeAuthorityToken(feature.label);
        for (const auto& token : TerminalAuthorityTokens(feature)) {
            if (!insertedIds.insert(token).second) {
                continue;
            }

            xvatsim::core::authority::AuthorityPositionSourceRecord record;
            record.source = xvatsim::core::authority::AuthoritySource::SimAwareTracon;
            record.kind = xvatsim::core::authority::AuthorityKind::Terminal;
            record.id = token;
            record.name = feature.label;
            record.polygonKey = token;
            record.controllerCallsignPatterns =
                TerminalAuthorityCallsignPatterns(token);
            record.proofSource = "SIMAWARE_TRACON";
            record.proofDetail =
                "terminalBoundary=" + featureKey + ";position=" + token;
            record.sourceRecord = feature.label;
            records.push_back(std::move(record));
        }
    }

    return records;
}

std::vector<xvatsim::core::authority::AuthorityPolygonSourceRecord>
AdaptSimAwareTerminalPolygonSourceRecords(
    const std::vector<SectorFeature>& terminalFeatures) {
    std::vector<xvatsim::core::authority::AuthorityPolygonSourceRecord> records;
    records.reserve(terminalFeatures.size());

    for (const auto& feature : terminalFeatures) {
        const auto tokens = TerminalAuthorityTokens(feature);
        if (tokens.empty()) {
            continue;
        }

        xvatsim::core::authority::AuthorityPolygonSourceRecord record;
        record.source = xvatsim::core::authority::AuthoritySource::SimAwareTracon;
        record.id = feature.sourceFeatureId.empty()
                        ? feature.label
                        : feature.sourceFeatureId;
        record.name = feature.name.empty() ? feature.label : feature.name;
        record.serviceRoles = feature.serviceRoles;
        record.lookupTokens = tokens;
        record.sourceRecord = feature.label;

        std::vector<std::string> roleTokens;
        for (const auto role : record.serviceRoles) {
            roleTokens.push_back(
                role == xvatsim::core::authority::AuthorityServiceRole::Departure
                    ? "DEP"
                    : "APP");
        }
        std::sort(roleTokens.begin(), roleTokens.end());
        roleTokens.erase(
            std::unique(roleTokens.begin(), roleTokens.end()),
            roleTokens.end());
        record.polygonKey = NormalizeAuthorityCatalogKey(record.id);
        for (const auto& role : roleTokens) {
            record.polygonKey += "_" + role;
        }

        for (const auto& token : tokens) {
            std::string prefix;
            std::string suffix;
            if (!SplitTerminalAuthorityToken(token, &prefix, &suffix)) {
                continue;
            }
            record.prefixes.push_back(prefix);
        }
        std::sort(record.prefixes.begin(), record.prefixes.end());
        record.prefixes.erase(
            std::unique(record.prefixes.begin(), record.prefixes.end()),
            record.prefixes.end());

        record.parts.reserve(feature.polygons.size());
        for (std::size_t partIndex = 0;
             partIndex < feature.polygons.size();
             ++partIndex) {
            const auto& polygon = feature.polygons[partIndex];
            xvatsim::core::authority::AuthorityPolygonPart part;
            part.componentId = std::to_string(partIndex);
            part.exterior.points.reserve(polygon.ring.size());
            for (const auto& point : polygon.ring) {
                part.exterior.points.push_back({
                    point.latitudeDeg,
                    point.longitudeDeg,
                });
            }
            part.holes.reserve(polygon.holes.size());
            for (const auto& sourceHole : polygon.holes) {
                xvatsim::core::authority::AuthorityPolygonRing hole;
                hole.points.reserve(sourceHole.size());
                for (const auto& point : sourceHole) {
                    hole.points.push_back({
                        point.latitudeDeg,
                        point.longitudeDeg,
                    });
                }
                part.holes.push_back(std::move(hole));
            }
            record.parts.push_back(std::move(part));
        }

        records.push_back(std::move(record));
    }

    return records;
}

const xvatsim::core::authority::ControllerAuthorityCatalog&
GetCachedCoreControllerAuthorityCatalog(
    const std::vector<unsigned char>& vatspyPayload,
    const std::vector<unsigned char>& terminalPayload,
    const std::vector<unsigned char>& ownershipPayload) {
    static std::mutex cacheMutex;
    static std::size_t cachedHash = 0;
    static std::size_t cachedVatspySize = 0;
    static std::size_t cachedTerminalSize = 0;
    static std::size_t cachedOwnershipSize = 0;
    static xvatsim::core::authority::ControllerAuthorityCatalog cachedCatalog;

    const auto payloadHash = HashPayloads(vatspyPayload, terminalPayload, ownershipPayload);
    std::lock_guard<std::mutex> lock(cacheMutex);
    if (payloadHash == cachedHash &&
        vatspyPayload.size() == cachedVatspySize &&
        terminalPayload.size() == cachedTerminalSize &&
        ownershipPayload.size() == cachedOwnershipSize) {
        return cachedCatalog;
    }

    const auto vatspyCatalog =
        xvatsim::core::authority::CompileVatSpyAuthorityCatalog(
            PayloadToString(vatspyPayload));
    const auto terminalCatalog =
        xvatsim::core::authority::CompileAuthorityPositionCatalog(
            AdaptSimAwareTerminalPositionSourceRecords(
                GetCachedTerminalSectorFeatures(terminalPayload)));
    const auto ownershipCatalog =
        xvatsim::core::authority::CompileAuthorityPositionCatalog(
            ParseOwnershipAuthoritySourceRecords(PayloadToString(ownershipPayload)));

    cachedHash = payloadHash;
    cachedVatspySize = vatspyPayload.size();
    cachedTerminalSize = terminalPayload.size();
    cachedOwnershipSize = ownershipPayload.size();
    cachedCatalog =
        xvatsim::core::authority::MergeControllerAuthorityCatalogs(
            xvatsim::core::authority::MergeControllerAuthorityCatalogs(
                vatspyCatalog,
                terminalCatalog),
            ownershipCatalog);
    return cachedCatalog;
}

std::vector<xvatsim::core::authority::AuthorityPolygonSourceRecord>
BuildCoreAuthorityPolygonRecords(const std::vector<SectorFeature>& features) {
    std::vector<xvatsim::core::authority::AuthorityPolygonSourceRecord> records;
    records.reserve(features.size());

    for (const auto& feature : features) {
        xvatsim::core::authority::AuthorityPolygonSourceRecord record;
        record.source = xvatsim::core::authority::AuthoritySource::VatSpyBoundary;
        record.id = feature.label;
        record.name = feature.label;
        record.lookupTokens.assign(feature.tokens.begin(), feature.tokens.end());
        std::sort(record.lookupTokens.begin(), record.lookupTokens.end());
        record.sourceRecord = feature.label;

        record.parts.reserve(feature.polygons.size());
        for (std::size_t partIndex = 0;
             partIndex < feature.polygons.size();
             ++partIndex) {
            const auto& polygon = feature.polygons[partIndex];
            xvatsim::core::authority::AuthorityPolygonPart part;
            part.componentId = std::to_string(partIndex);
            part.exterior.points.reserve(polygon.ring.size());
            for (const auto& point : polygon.ring) {
                part.exterior.points.push_back({
                    point.latitudeDeg,
                    point.longitudeDeg,
                });
            }
            for (const auto& sourceHole : polygon.holes) {
                xvatsim::core::authority::AuthorityPolygonRing hole;
                hole.points.reserve(sourceHole.size());
                for (const auto& point : sourceHole) {
                    hole.points.push_back({
                        point.latitudeDeg,
                        point.longitudeDeg,
                    });
                }
                part.holes.push_back(std::move(hole));
            }
            record.parts.push_back(std::move(part));
        }

        records.push_back(std::move(record));
    }

    return records;
}

const xvatsim::core::authority::AuthorityPolygonCatalog& GetCachedCoreAuthorityPolygonCatalog(
    const std::vector<unsigned char>& boundaryPayload,
    const std::vector<unsigned char>& terminalPayload,
    const std::vector<unsigned char>& ownershipPayload) {
    static std::mutex cacheMutex;
    static std::size_t cachedHash = 0;
    static std::size_t cachedBoundarySize = 0;
    static std::size_t cachedTerminalSize = 0;
    static std::size_t cachedOwnershipSize = 0;
    static xvatsim::core::authority::AuthorityPolygonCatalog cachedCatalog;

    const auto payloadHash = HashPayloads(boundaryPayload, terminalPayload, ownershipPayload);
    std::lock_guard<std::mutex> lock(cacheMutex);
    if (payloadHash == cachedHash &&
        boundaryPayload.size() == cachedBoundarySize &&
        terminalPayload.size() == cachedTerminalSize &&
        ownershipPayload.size() == cachedOwnershipSize) {
        return cachedCatalog;
    }

    auto records = BuildCoreAuthorityPolygonRecords(
        GetCachedSectorFeatures(boundaryPayload));
    auto terminalRecords =
        AdaptSimAwareTerminalPolygonSourceRecords(
            GetCachedTerminalSectorFeatures(terminalPayload));
    records.insert(
        records.end(),
        std::make_move_iterator(terminalRecords.begin()),
        std::make_move_iterator(terminalRecords.end()));
    auto ownershipRecords =
        xvatsim::core::authority::ParseAuthorityPolygonSourceRecordsJson(
            xvatsim::core::authority::AuthoritySource::VatGlasses,
            PayloadToString(ownershipPayload));
    auto terminalOwnershipRecords =
        xvatsim::core::authority::ParseAuthorityPolygonSourceRecordsJson(
            xvatsim::core::authority::AuthoritySource::SimAwareTracon,
            PayloadToString(ownershipPayload));
    ownershipRecords.insert(
        ownershipRecords.end(),
        std::make_move_iterator(terminalOwnershipRecords.begin()),
        std::make_move_iterator(terminalOwnershipRecords.end()));
    auto specialOwnershipRecords =
        xvatsim::core::authority::ParseAuthorityPolygonSourceRecordsJson(
            xvatsim::core::authority::AuthoritySource::SpecialSectorData,
            PayloadToString(ownershipPayload));
    ownershipRecords.insert(
        ownershipRecords.end(),
        std::make_move_iterator(specialOwnershipRecords.begin()),
        std::make_move_iterator(specialOwnershipRecords.end()));
    records.insert(
        records.end(),
        std::make_move_iterator(ownershipRecords.begin()),
        std::make_move_iterator(ownershipRecords.end()));

    cachedHash = payloadHash;
    cachedBoundarySize = boundaryPayload.size();
    cachedTerminalSize = terminalPayload.size();
    cachedOwnershipSize = ownershipPayload.size();
    cachedCatalog =
        xvatsim::core::authority::CompileAuthorityPolygons(records);
    return cachedCatalog;
}

std::string NormalizeFrequency(std::string frequency) {
    frequency.erase(
        std::remove_if(
            frequency.begin(),
            frequency.end(),
            [](unsigned char c) { return std::isspace(c) != 0; }),
        frequency.end());

    std::string digits;
    bool sawDecimal = false;
    int decimals = 0;
    for (const auto character : frequency) {
        if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
            digits.push_back(character);
            if (sawDecimal && decimals < 3) {
                ++decimals;
            }
            continue;
        }

        if (character == '.' && !sawDecimal) {
            sawDecimal = true;
        }
    }

    if (digits.empty()) {
        return {};
    }

    if (sawDecimal) {
        while (decimals < 3) {
            digits.push_back('0');
            ++decimals;
        }
    } else if (digits.size() == 5) {
        digits.push_back('0');
    }

    return digits;
}

bool IsGuardFrequency(const std::string& frequency) {
    const auto normalizedFrequency = NormalizeFrequency(frequency);
    return normalizedFrequency == "121500" || normalizedFrequency == "199998";
}

bool IsAuthorityControllerCandidate(const brain::ControllerSnapshot& controller) {
    const auto canOwnAirspace =
        controller.facility == kVatsimFlightServiceFacility ||
        controller.facility == kVatsimApproachFacility ||
        controller.facility == kVatsimCenterFacility;
    return controller.actionable &&
           !controller.atis &&
           canOwnAirspace &&
           !controller.callsign.empty() &&
           !IsGuardFrequency(controller.frequency);
}

std::string NormalizeAirportIcao(std::string airportIcao) {
    std::string normalized;
    normalized.reserve(airportIcao.size());
    for (const auto character : airportIcao) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0) {
            continue;
        }
        normalized.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(character))));
    }
    return normalized;
}

std::vector<std::string> BuildAirportLocalTokens(const std::string& airportIcao) {
    std::vector<std::string> tokens;
    const auto normalized = NormalizeAirportIcao(airportIcao);
    if (normalized.empty()) {
        return tokens;
    }

    tokens.push_back(normalized);
    if (normalized.size() == 4) {
        tokens.push_back(normalized.substr(1));
    }
    if (normalized.size() >= 3) {
        tokens.push_back(normalized.substr(normalized.size() - 3));
    }

    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
    return tokens;
}

bool IsTerminalServiceSuffix(const std::string& suffix) {
    return suffix == "APP" || suffix == "DEP";
}

std::string TerminalServiceBase(const std::string& rawToken) {
    const auto token = NormalizeAuthorityCatalogKey(rawToken);
    const auto separator = token.rfind('_');
    if (separator == std::string::npos || separator >= token.size() - 1 ||
        !IsTerminalServiceSuffix(token.substr(separator + 1))) {
        return {};
    }
    return token.substr(0, separator);
}

std::string TerminalServiceRoot(const std::string& rawToken) {
    const auto base = TerminalServiceBase(rawToken);
    const auto separator = base.find('_');
    return separator == std::string::npos ? base : base.substr(0, separator);
}

std::string TerminalServiceToken(
    const std::string& rawBase,
    const std::string& rawSuffix) {
    const auto base = NormalizeAuthorityCatalogKey(rawBase);
    const auto suffix = NormalizeAuthorityCatalogKey(rawSuffix);
    return base.empty() || !IsTerminalServiceSuffix(suffix)
               ? std::string{}
               : base + "_" + suffix;
}

void AppendUniqueTerminalToken(
    std::vector<std::string>* values,
    const std::string& rawValue) {
    if (values == nullptr) {
        return;
    }
    const auto value = NormalizeAuthorityCatalogKey(rawValue);
    if (!value.empty() &&
        std::find(values->begin(), values->end(), value) == values->end()) {
        values->push_back(value);
    }
}

bool TerminalServiceMatchesEndpoint(
    const std::string& rawServiceToken,
    const std::vector<std::string>& endpointTokens,
    const std::string& rawPolygonKey) {
    const auto serviceToken = NormalizeAuthorityCatalogKey(rawServiceToken);
    const auto base = TerminalServiceBase(serviceToken);
    if (base.empty()) {
        return false;
    }
    const auto endpointMatch = std::any_of(
        endpointTokens.begin(),
        endpointTokens.end(),
        [&](const auto& token) {
            return base == token ||
                   (base.size() > token.size() &&
                    base.compare(0, token.size(), token) == 0 &&
                    (base[token.size()] == '_' || base[token.size()] == '-'));
        });
    if (endpointMatch) {
        return true;
    }
    const auto polygonKey = NormalizeAuthorityCatalogKey(rawPolygonKey);
    return !polygonKey.empty() && polygonKey.find('_') == std::string::npos &&
           (base == polygonKey || TerminalServiceRoot(serviceToken) == polygonKey);
}

brain::BrainEndpointPolygonFact BuildCanonicalTerminalPolygonFact(
    const xvatsim::core::authority::AuthorityPolygon& polygon,
    bool containsAirport,
    bool endpointContext) {
    brain::BrainEndpointPolygonFact fact;
    fact.canonicalPolygonId = polygon.id;
    fact.polygonKey = !polygon.sourceFeatureId.empty()
        ? polygon.sourceFeatureId
        : polygon.polygonKey;
    fact.name = polygon.name;
    for (const auto role : polygon.serviceRoles) {
        AppendUniqueTerminalToken(
            &fact.serviceSuffixes,
            role == xvatsim::core::authority::AuthorityServiceRole::Departure
                ? "DEP"
                : "APP");
    }
    fact.controllerPrefixes = polygon.controllerPrefixes;
    if (!polygon.polygonKey.empty() &&
        std::find(
            fact.controllerPrefixes.begin(),
            fact.controllerPrefixes.end(),
            polygon.polygonKey) == fact.controllerPrefixes.end()) {
        fact.controllerPrefixes.push_back(polygon.polygonKey);
    }
    fact.containsAirport = containsAirport;
    fact.intersectsEndpointAnchorContextOnly = endpointContext;
    fact.source = "CANONICAL_AUTHORITY_MAP";
    for (const auto& part : polygon.parts) {
        const auto appendRing = [&](const auto& ring) {
            brain::BrainEndpointPolygonRing factRing;
            factRing.points.reserve(ring.points.size());
            for (const auto& point : ring.points) {
                factRing.points.push_back(
                    {point.latitudeDeg, point.longitudeDeg});
            }
            fact.rings.push_back(std::move(factRing));
        };
        appendRing(part.exterior);
        for (const auto& hole : part.holes) {
            appendRing(hole);
        }
    }
    return fact;
}

bool SplitLocalControllerCallsign(
    const std::string& callsign,
    std::string* outPrefix,
    std::string* outSuffix) {
    const auto normalized =
        xvatsim::core::authority::NormalizeControllerCallsign(callsign);
    const auto separatorIndex = normalized.rfind('_');
    if (separatorIndex == std::string::npos ||
        separatorIndex == 0 ||
        separatorIndex >= normalized.size() - 1) {
        return false;
    }

    if (outPrefix != nullptr) {
        *outPrefix = normalized.substr(0, separatorIndex);
    }
    if (outSuffix != nullptr) {
        *outSuffix = normalized.substr(separatorIndex + 1);
    }
    return true;
}

bool AirportLocalTokenMatchesPrefix(
    const std::vector<std::string>& airportTokens,
    const std::string& controllerPrefix) {
    return std::any_of(
        airportTokens.begin(),
        airportTokens.end(),
        [&](const auto& airportToken) {
            return controllerPrefix == airportToken ||
                   (controllerPrefix.size() > airportToken.size() &&
                    controllerPrefix.compare(0, airportToken.size(), airportToken) == 0 &&
                    controllerPrefix[airportToken.size()] == '_');
        });
}

std::string CanonicalLocalCallsignSuffix(const std::string& suffix) {
    if (suffix == "DEL" || suffix == "CLR" || suffix == "CLNC" || suffix == "CD") {
        return "DEL";
    }
    if (suffix == "GND") {
        return "GND";
    }
    if (suffix == "TWR") {
        return "TWR";
    }
    return {};
}

bool IsAirportLocalControllerCandidate(
    const brain::ControllerSnapshot& controller) {
    return controller.actionable &&
           !controller.atis &&
           !controller.callsign.empty() &&
           !IsGuardFrequency(controller.frequency) &&
           (controller.facility == kVatsimDeliveryFacility ||
            controller.facility == kVatsimGroundFacility ||
            controller.facility == kVatsimTowerFacility);
}

std::string AuthorityKindLabel(xvatsim::core::authority::AuthorityKind kind) {
    switch (kind) {
        case xvatsim::core::authority::AuthorityKind::Terminal:
            return "terminal";
        case xvatsim::core::authority::AuthorityKind::Extension:
            return "extension";
        case xvatsim::core::authority::AuthorityKind::Center:
            return "center";
    }
    return "center";
}

xvatsim::brain::AuthorityRelevanceKind ToBrainAuthorityRelevanceKind(
    xvatsim::core::authority::AuthorityKind kind) {
    switch (kind) {
        case xvatsim::core::authority::AuthorityKind::Terminal:
            return xvatsim::brain::AuthorityRelevanceKind::Terminal;
        case xvatsim::core::authority::AuthorityKind::Extension:
            return xvatsim::brain::AuthorityRelevanceKind::Extension;
        case xvatsim::core::authority::AuthorityKind::Center:
            return xvatsim::brain::AuthorityRelevanceKind::Center;
    }
    return xvatsim::brain::AuthorityRelevanceKind::Center;
}

void AppendAuthorityDiagnostic(
    std::vector<std::string>* diagnostics,
    std::string diagnostic) {
    if (diagnostics == nullptr || diagnostic.empty()) {
        return;
    }
    diagnostics->push_back(std::move(diagnostic));
}

void SortUniqueStrings(std::vector<std::string>* values) {
    if (values == nullptr) {
        return;
    }
    std::sort(values->begin(), values->end());
    values->erase(std::unique(values->begin(), values->end()), values->end());
}

std::string ActiveAuthorityKey(
    const xvatsim::core::authority::ActiveAuthorityPolygon& activePolygon) {
    return activePolygon.callsign + "|" +
           activePolygon.authorityId + "|" +
           activePolygon.polygonId + "|" +
           activePolygon.matchedPattern;
}

void AddRouteAuthorityPolygonKey(
    const brain::RouteSectorMatchSnapshot& sector,
    std::unordered_set<std::string>* routeAuthorityPolygonKeys) {
    if (routeAuthorityPolygonKeys == nullptr) {
        return;
    }

    auto addToken = [&](const std::string& value) {
        const auto normalized =
            xvatsim::core::authority::NormalizeAuthorityToken(value);
        if (!normalized.empty()) {
            routeAuthorityPolygonKeys->insert(normalized);
        }
    };

    addToken(sector.identifier);
    for (const auto& token : sector.matchTokens) {
        addToken(token);
    }
}

std::unordered_set<std::string> BuildRouteAuthorityPolygonKeys(
    const brain::RouteSectorSnapshot& routeSectorSnapshot) {
    std::unordered_set<std::string> routeAuthorityPolygonKeys;
    for (const auto& sector : routeSectorSnapshot.currentSectors) {
        AddRouteAuthorityPolygonKey(sector, &routeAuthorityPolygonKeys);
    }
    for (const auto& sector : routeSectorSnapshot.nextSectors) {
        AddRouteAuthorityPolygonKey(sector, &routeAuthorityPolygonKeys);
    }
    return routeAuthorityPolygonKeys;
}

xvatsim::core::authority::GeoPoint InterpolateAuthorityGeoPoint(
    const xvatsim::core::authority::GeoPoint& start,
    const xvatsim::core::authority::GeoPoint& end,
    double fraction) {
    fraction = std::clamp(fraction, 0.0, 1.0);
    const GeoPoint localStart{start.latitudeDeg, start.longitudeDeg};
    const GeoPoint localEnd{end.latitudeDeg, end.longitudeDeg};
    const auto startVector = ToUnitVector(localStart);
    const auto endVector = ToUnitVector(localEnd);
    const auto angularDistanceRad = AngularDistanceRad(startVector, endVector);
    const auto sinAngularDistance = std::sin(angularDistanceRad);
    if (angularDistanceRad <= 1e-10 || std::fabs(sinAngularDistance) <= 1e-12) {
        return fraction < 0.5 ? start : end;
    }

    const auto startScale =
        std::sin((1.0 - fraction) * angularDistanceRad) / sinAngularDistance;
    const auto endScale =
        std::sin(fraction * angularDistanceRad) / sinAngularDistance;
    const auto interpolated = ToGeoPoint(AddVector(
        ScaleVector(startVector, startScale),
        ScaleVector(endVector, endScale)));
    return {interpolated.latitudeDeg, interpolated.longitudeDeg};
}

std::vector<xvatsim::core::authority::GeoPoint> ClipRoutePointsForAuthorityWindow(
    const std::vector<brain::RouteWaypointSnapshot>& waypoints,
    double windowNm) {
    std::vector<xvatsim::core::authority::GeoPoint> routePoints;
    if (waypoints.empty()) {
        return routePoints;
    }

    routePoints.reserve(waypoints.size());
    routePoints.push_back({waypoints.front().latitudeDeg, waypoints.front().longitudeDeg});
    if (waypoints.size() == 1 || windowNm <= 0.0) {
        return routePoints;
    }

    double accumulatedDistanceNm = 0.0;
    for (std::size_t waypointIndex = 1; waypointIndex < waypoints.size(); ++waypointIndex) {
        const xvatsim::core::authority::GeoPoint start{
            waypoints[waypointIndex - 1].latitudeDeg,
            waypoints[waypointIndex - 1].longitudeDeg,
        };
        const xvatsim::core::authority::GeoPoint end{
            waypoints[waypointIndex].latitudeDeg,
            waypoints[waypointIndex].longitudeDeg,
        };
        const auto segmentDistanceNm = GreatCircleDistanceNm(
            start.latitudeDeg,
            start.longitudeDeg,
            end.latitudeDeg,
            end.longitudeDeg);
        if (segmentDistanceNm <= 1e-6) {
            continue;
        }

        if (accumulatedDistanceNm + segmentDistanceNm <= windowNm) {
            routePoints.push_back(end);
            accumulatedDistanceNm += segmentDistanceNm;
            continue;
        }

        const auto remainingDistanceNm =
            std::max(0.0, windowNm - accumulatedDistanceNm);
        const auto fraction =
            std::clamp(remainingDistanceNm / segmentDistanceNm, 0.0, 1.0);
        if (fraction > 1e-6) {
            routePoints.push_back(InterpolateAuthorityGeoPoint(start, end, fraction));
        }
        break;
    }

    return routePoints;
}

std::vector<brain::RouteWaypointSnapshot> ToRouteWaypointSnapshots(
    const std::vector<xvatsim::core::authority::GeoPoint>& routePoints,
    const std::vector<brain::RouteWaypointSnapshot>& originalWaypoints) {
    std::vector<brain::RouteWaypointSnapshot> waypoints;
    waypoints.reserve(routePoints.size());
    for (std::size_t index = 0; index < routePoints.size(); ++index) {
        brain::RouteWaypointSnapshot waypoint;
        if (index < originalWaypoints.size()) {
            waypoint.ident = originalWaypoints[index].ident;
        } else {
            waypoint.ident = "WINDOW";
        }
        waypoint.latitudeDeg = routePoints[index].latitudeDeg;
        waypoint.longitudeDeg = routePoints[index].longitudeDeg;
        waypoints.push_back(std::move(waypoint));
    }
    return waypoints;
}

std::vector<brain::RouteSectorMatchSnapshot> FilterNextSectorsForAuthorityWindow(
    const std::vector<brain::RouteSectorMatchSnapshot>& nextSectors,
    double windowNm,
    std::size_t* outDeferredSectorCount) {
    std::vector<brain::RouteSectorMatchSnapshot> filtered;
    filtered.reserve(nextSectors.size());
    for (const auto& sector : nextSectors) {
        if (sector.entryDistanceNm <= windowNm) {
            filtered.push_back(sector);
        }
    }

    if (outDeferredSectorCount != nullptr) {
        *outDeferredSectorCount =
            nextSectors.size() > filtered.size() ? nextSectors.size() - filtered.size() : 0;
    }
    return filtered;
}

std::size_t BuildAuthorityProgressRouteSignature(
    const brain::RouteSectorSnapshot& routeSectorSnapshot) {
    std::size_t hash = 1469598103934665603ull;
    HashCombine(&hash, static_cast<std::size_t>(routeSectorSnapshot.available ? 1 : 0));
    HashCombine(&hash, static_cast<std::size_t>(routeSectorSnapshot.routeResolved ? 1 : 0));
    HashCombine(&hash, static_cast<std::size_t>(routeSectorSnapshot.centerBoundaryGeneration));
    HashCombine(&hash, static_cast<std::size_t>(routeSectorSnapshot.authorityCatalogGeneration));
    HashCombineString(&hash, routeSectorSnapshot.departureIcao);
    HashCombineString(&hash, routeSectorSnapshot.destinationIcao);

    auto hashSectorIdentity = [&](const brain::RouteSectorMatchSnapshot& sector) {
        HashCombineString(&hash, sector.identifier);
        for (const auto& token : sector.matchTokens) {
            HashCombineString(&hash, token);
        }
        for (const auto& pattern : sector.controllerCallsignPatterns) {
            HashCombineString(&hash, pattern);
        }
    };
    HashCombine(&hash, 0x5150);
    for (const auto& sector : routeSectorSnapshot.currentSectors) {
        hashSectorIdentity(sector);
    }
    HashCombine(&hash, 0x5151);
    for (const auto& sector : routeSectorSnapshot.nextSectors) {
        hashSectorIdentity(sector);
    }
    return hash;
}

std::size_t BuildAuthorityStructuralScopeRouteSignature(
    const brain::RouteSectorSnapshot& routeSectorSnapshot) {
    std::size_t hash = 1469598103934665603ull;
    HashCombine(&hash, static_cast<std::size_t>(routeSectorSnapshot.available ? 1 : 0));
    HashCombine(&hash, static_cast<std::size_t>(routeSectorSnapshot.routeResolved ? 1 : 0));
    HashCombine(&hash, static_cast<std::size_t>(routeSectorSnapshot.centerBoundaryGeneration));
    HashCombine(&hash, static_cast<std::size_t>(routeSectorSnapshot.authorityCatalogGeneration));
    HashCombineString(&hash, routeSectorSnapshot.departureIcao);
    HashCombineString(&hash, routeSectorSnapshot.destinationIcao);

    auto hashSectorIdentity = [&](const brain::RouteSectorMatchSnapshot& sector) {
        HashCombineString(&hash, sector.identifier);
        for (const auto& token : sector.matchTokens) {
            HashCombineString(&hash, token);
        }
        for (const auto& pattern : sector.controllerCallsignPatterns) {
            HashCombineString(&hash, pattern);
        }
    };
    HashCombine(&hash, 0x5150);
    for (const auto& sector : routeSectorSnapshot.currentSectors) {
        hashSectorIdentity(sector);
    }
    HashCombine(&hash, 0x5151);
    for (const auto& sector : routeSectorSnapshot.nextSectors) {
        hashSectorIdentity(sector);
    }
    return hash;
}

std::size_t BuildAuthorityOperationalScopeSignature(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const AuthorityRelevanceWorkScope& workScope,
    std::uint64_t terminalBoundaryGeneration) {
    std::size_t hash = 1469598103934665603ull;
    HashCombine(&hash, static_cast<std::size_t>(aircraftState.valid ? 1 : 0));
    HashCombine(&hash, static_cast<std::size_t>(aircraftState.onGround ? 1 : 0));
    HashCombine(&hash, static_cast<std::size_t>(controllerFeedSnapshot.available ? 1 : 0));
    HashCombine(&hash, static_cast<std::size_t>(controllerFeedSnapshot.stale ? 1 : 0));
    HashCombine(&hash, static_cast<std::size_t>(controllerFeedSnapshot.generation));
    HashCombine(&hash, static_cast<std::size_t>(terminalBoundaryGeneration));
    HashCombineString(&hash, workScope.stage);
    HashCombineDouble(&hash, workScope.windowNm);
    HashCombine(
        &hash,
        static_cast<std::size_t>(workScope.includeDepartureEndpoint ? 1 : 0));
    HashCombine(
        &hash,
        static_cast<std::size_t>(workScope.includeDestinationEndpoint ? 1 : 0));
    HashCombine(&hash, static_cast<std::size_t>(workScope.deferredSectorCount));

    const auto& routeSectorSnapshot = workScope.routeSectorSnapshot;
    HashCombine(&hash, static_cast<std::size_t>(routeSectorSnapshot.available ? 1 : 0));
    HashCombine(&hash, static_cast<std::size_t>(routeSectorSnapshot.stale ? 1 : 0));
    HashCombine(&hash, static_cast<std::size_t>(routeSectorSnapshot.routeResolved ? 1 : 0));
    HashCombine(&hash, static_cast<std::size_t>(routeSectorSnapshot.centerBoundaryGeneration));
    HashCombine(&hash, static_cast<std::size_t>(routeSectorSnapshot.authorityCatalogGeneration));
    HashCombineString(&hash, routeSectorSnapshot.departureIcao);
    HashCombineString(&hash, routeSectorSnapshot.destinationIcao);

    auto hashSector = [&](const brain::RouteSectorMatchSnapshot& sector) {
        HashCombineString(&hash, sector.identifier);
        for (const auto& token : sector.matchTokens) {
            HashCombineString(&hash, token);
        }
    };
    HashCombine(&hash, 0x1111);
    for (const auto& sector : routeSectorSnapshot.currentSectors) {
        hashSector(sector);
    }
    HashCombine(&hash, 0x2222);
    for (const auto& sector : routeSectorSnapshot.nextSectors) {
        hashSector(sector);
    }
    return hash;
}

bool HasRelevantCenterAuthority(
    const brain::AuthorityRelevanceSnapshot& snapshot) {
    return std::any_of(
        snapshot.relevantAuthorities.begin(),
        snapshot.relevantAuthorities.end(),
        [](const auto& authority) {
            return authority.kind == brain::AuthorityRelevanceKind::Center;
        });
}

AuthorityRelevanceWorkScope BuildAuthorityRelevanceWorkScope(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::RouteSectorSnapshot& routeSectorSnapshot,
    double windowNm) {
    AuthorityRelevanceWorkScope scope;
    scope.routeSectorSnapshot = routeSectorSnapshot;
    const auto remainingRouteDistanceNm = RouteDistanceNm(routeSectorSnapshot.waypoints);
    scope.includeDepartureEndpoint = aircraftState.onGround;
    scope.includeDestinationEndpoint =
        remainingRouteDistanceNm <= kAuthorityArrivalPrepDistanceNm;
    scope.windowNm = scope.includeDestinationEndpoint
                         ? std::max(kAuthorityNearRouteWindowNm, remainingRouteDistanceNm)
                         : std::max(kAuthorityNearRouteWindowNm, windowNm);
    scope.routePoints =
        ClipRoutePointsForAuthorityWindow(routeSectorSnapshot.waypoints, scope.windowNm);
    scope.routeSectorSnapshot.waypoints =
        ToRouteWaypointSnapshots(scope.routePoints, routeSectorSnapshot.waypoints);
    scope.routeSectorSnapshot.nextSectors =
        FilterNextSectorsForAuthorityWindow(
            routeSectorSnapshot.nextSectors,
            scope.windowNm,
            &scope.deferredSectorCount);
    if (scope.includeDestinationEndpoint) {
        scope.stage = "ARRIVAL_PREP";
    } else if (aircraftState.onGround) {
        scope.stage = "LOCAL_BOOTSTRAP";
    } else {
        scope.stage = "NEAR_ROUTE_WINDOW";
    }
    return scope;
}

bool AuthorityPolygonMatchesRouteKeys(
    const xvatsim::core::authority::AuthorityPolygon& polygon,
    const std::unordered_set<std::string>& routeAuthorityPolygonKeys) {
    if (routeAuthorityPolygonKeys.empty()) {
        return false;
    }

    const auto normalizedPolygonKey =
        xvatsim::core::authority::NormalizeAuthorityToken(polygon.polygonKey);
    if (!normalizedPolygonKey.empty() &&
        routeAuthorityPolygonKeys.find(normalizedPolygonKey) !=
            routeAuthorityPolygonKeys.end()) {
        return true;
    }
    for (const auto& lookupKey : polygon.lookupKeys) {
        const auto normalizedLookupKey =
            xvatsim::core::authority::NormalizeAuthorityToken(lookupKey);
        if (!normalizedLookupKey.empty() &&
            routeAuthorityPolygonKeys.find(normalizedLookupKey) !=
                routeAuthorityPolygonKeys.end()) {
            return true;
        }
    }
    return false;
}

bool AuthorityPolygonFamilyMatchesRouteKeys(
    const xvatsim::core::authority::AuthorityPolygon& polygon,
    const std::unordered_set<std::string>& routeAuthorityPolygonKeys) {
    if (AuthorityPolygonMatchesRouteKeys(polygon, routeAuthorityPolygonKeys)) {
        return true;
    }

    auto familyMatchesRoute = [&](const std::string& value) {
        const auto familyKey = AuthorityFamilyKey(value);
        return !familyKey.empty() &&
               routeAuthorityPolygonKeys.find(familyKey) !=
                   routeAuthorityPolygonKeys.end();
    };

    if (familyMatchesRoute(polygon.polygonKey) ||
        familyMatchesRoute(polygon.id)) {
        return true;
    }
    for (const auto& lookupKey : polygon.lookupKeys) {
        if (familyMatchesRoute(lookupKey)) {
            return true;
        }
    }
    return false;
}

void AddNormalizedAuthorityMatchKey(
    const std::string& value,
    std::unordered_set<std::string>* routeAuthorityMatchKeys) {
    if (routeAuthorityMatchKeys == nullptr) {
        return;
    }
    const auto normalized =
        xvatsim::core::authority::NormalizeAuthorityToken(value);
    if (!normalized.empty()) {
        routeAuthorityMatchKeys->insert(normalized);
    }
}

void AddAuthorityScopeKeys(
    const xvatsim::core::authority::AuthorityPolygon& polygon,
    std::unordered_set<std::string>* scopeKeys) {
    if (scopeKeys == nullptr) {
        return;
    }
    AddNormalizedAuthorityMatchKey(polygon.id, scopeKeys);
    AddNormalizedAuthorityMatchKey(polygon.polygonKey, scopeKeys);
    AddNormalizedAuthorityMatchKey(AuthorityFamilyKey(polygon.id), scopeKeys);
    AddNormalizedAuthorityMatchKey(AuthorityFamilyKey(polygon.polygonKey), scopeKeys);
    for (const auto& lookupKey : polygon.lookupKeys) {
        AddNormalizedAuthorityMatchKey(lookupKey, scopeKeys);
        AddNormalizedAuthorityMatchKey(AuthorityFamilyKey(lookupKey), scopeKeys);
    }
}

void AddRouteEndpointAuthorityKeys(
    const brain::RouteSectorSnapshot& routeSectorSnapshot,
    bool includeDepartureEndpoint,
    bool includeDestinationEndpoint,
    std::unordered_set<std::string>* scopeKeys) {
    if (scopeKeys == nullptr) {
        return;
    }
    std::vector<std::string> airports;
    if (includeDepartureEndpoint) {
        airports.push_back(routeSectorSnapshot.departureIcao);
    }
    if (includeDestinationEndpoint) {
        airports.push_back(routeSectorSnapshot.destinationIcao);
    }
    for (const auto& airport : airports) {
        for (const auto& token : BuildAirportLocalTokens(airport)) {
            AddNormalizedAuthorityMatchKey(token, scopeKeys);
        }
    }
}

bool AuthorityPolygonMayContainRoutePoint(
    const xvatsim::core::authority::AuthorityPolygon& polygon,
    const xvatsim::core::authority::GeoPoint& routePoint,
    double paddingDeg) {
    const auto referenceLongitudeDeg = NormalizeLongitudeDeg(routePoint.longitudeDeg);
    for (const auto& part : polygon.parts) {
        const auto& ring = part.exterior;
        if (ring.points.empty()) {
            continue;
        }

        double minLatitudeDeg = std::numeric_limits<double>::max();
        double maxLatitudeDeg = std::numeric_limits<double>::lowest();
        double minLongitudeDeg = std::numeric_limits<double>::max();
        double maxLongitudeDeg = std::numeric_limits<double>::lowest();
        for (const auto& point : ring.points) {
            minLatitudeDeg = std::min(minLatitudeDeg, point.latitudeDeg);
            maxLatitudeDeg = std::max(maxLatitudeDeg, point.latitudeDeg);
            const auto unwrappedLongitudeDeg =
                UnwrapLongitudeRelativeDeg(referenceLongitudeDeg, point.longitudeDeg);
            minLongitudeDeg = std::min(minLongitudeDeg, unwrappedLongitudeDeg);
            maxLongitudeDeg = std::max(maxLongitudeDeg, unwrappedLongitudeDeg);
        }

        if (routePoint.latitudeDeg >= minLatitudeDeg - paddingDeg &&
            routePoint.latitudeDeg <= maxLatitudeDeg + paddingDeg &&
            referenceLongitudeDeg >= minLongitudeDeg - paddingDeg &&
            referenceLongitudeDeg <= maxLongitudeDeg + paddingDeg) {
            return true;
        }
    }
    return false;
}

bool TerminalAuthorityPolygonTouchesRoute(
    const xvatsim::core::authority::AuthorityPolygon& polygon,
    bool hasAircraftPosition,
    const xvatsim::core::authority::GeoPoint& aircraftPosition,
    const std::vector<xvatsim::core::authority::GeoPoint>& routePoints) {
    constexpr double kTerminalRouteScopePaddingDeg = 2.5;
    if (hasAircraftPosition &&
        AuthorityPolygonMayContainRoutePoint(
            polygon,
            aircraftPosition,
            kTerminalRouteScopePaddingDeg) &&
        DistanceFromPointToAuthorityPolygonNm(aircraftPosition, polygon) <=
            kAuthorityTerminalRouteScopeToleranceNm) {
        return true;
    }

    for (const auto& routePoint : routePoints) {
        if (!AuthorityPolygonMayContainRoutePoint(
                polygon,
                routePoint,
                kTerminalRouteScopePaddingDeg)) {
            continue;
        }
        if (DistanceFromPointToAuthorityPolygonNm(routePoint, polygon) <=
            kAuthorityTerminalRouteScopeToleranceNm) {
            return true;
        }
    }
    return false;
}

bool TerminalAuthorityPolygonTouchesAircraft(
    const xvatsim::core::authority::AuthorityPolygon& polygon,
    const xvatsim::core::authority::GeoPoint& aircraftPosition) {
    constexpr double kTerminalRouteScopePaddingDeg = 2.5;
    return AuthorityPolygonMayContainRoutePoint(
               polygon,
               aircraftPosition,
               kTerminalRouteScopePaddingDeg) &&
           DistanceFromPointToAuthorityPolygonNm(aircraftPosition, polygon) <=
               kAuthorityTerminalRouteScopeToleranceNm;
}

std::unordered_set<std::string> BuildRouteAuthorityScopeKeys(
    const xvatsim::core::authority::AuthorityPolygonCatalog& authorityPolygonCatalog,
    const std::unordered_set<std::string>& routeAuthorityPolygonKeys,
    const brain::RouteSectorSnapshot& routeSectorSnapshot,
    bool includeDepartureEndpoint,
    bool includeDestinationEndpoint,
    bool hasAircraftPosition,
    const xvatsim::core::authority::GeoPoint& aircraftPosition,
    const std::vector<xvatsim::core::authority::GeoPoint>& routePoints) {
    (void)routePoints;

    auto scopeKeys = routeAuthorityPolygonKeys;
    if (scopeKeys.empty()) {
        return scopeKeys;
    }
    std::unordered_set<std::string> endpointKeys;
    AddRouteEndpointAuthorityKeys(
        routeSectorSnapshot,
        includeDepartureEndpoint,
        includeDestinationEndpoint,
        &endpointKeys);
    for (const auto& endpointKey : endpointKeys) {
        AddNormalizedAuthorityMatchKey(endpointKey, &scopeKeys);
    }

    for (const auto& polygon : authorityPolygonCatalog.polygons) {
        if (AuthorityPolygonFamilyMatchesRouteKeys(polygon, routeAuthorityPolygonKeys) ||
            AuthorityPolygonMatchesRouteKeys(polygon, endpointKeys)) {
            AddAuthorityScopeKeys(polygon, &scopeKeys);
            continue;
        }
        if (polygon.kind == xvatsim::core::authority::AuthorityKind::Terminal &&
            hasAircraftPosition &&
            TerminalAuthorityPolygonTouchesAircraft(polygon, aircraftPosition)) {
            AddAuthorityScopeKeys(polygon, &scopeKeys);
        }
    }

    return scopeKeys;
}

std::unordered_set<std::string> BuildRouteAuthorityMatchKeys(
    const xvatsim::core::authority::AuthorityPolygonCatalog& authorityPolygonCatalog,
    const std::unordered_set<std::string>& routeAuthorityPolygonKeys) {
    std::unordered_set<std::string> routeAuthorityMatchKeys;
    if (routeAuthorityPolygonKeys.empty()) {
        return routeAuthorityMatchKeys;
    }

    routeAuthorityMatchKeys = routeAuthorityPolygonKeys;
    for (const auto& polygon : authorityPolygonCatalog.polygons) {
        AddAuthorityScopeKeys(polygon, &routeAuthorityMatchKeys);
    }
    return routeAuthorityMatchKeys;
}

xvatsim::core::authority::AuthorityPolygonCatalog
FilterAuthorityPolygonCatalogToRouteKeys(
    const xvatsim::core::authority::AuthorityPolygonCatalog& catalog,
    const std::unordered_set<std::string>& routeAuthorityPolygonKeys) {
    if (routeAuthorityPolygonKeys.empty()) {
        return catalog;
    }

    xvatsim::core::authority::AuthorityPolygonCatalog filtered;
    for (const auto& polygon : catalog.polygons) {
        if (AuthorityPolygonFamilyMatchesRouteKeys(polygon, routeAuthorityPolygonKeys)) {
            filtered.polygons.push_back(polygon);
        }
    }
    for (const auto& gap : catalog.dataGaps) {
        const auto normalizedPolygonKey =
            xvatsim::core::authority::NormalizeAuthorityToken(gap.polygonKey);
        if (!normalizedPolygonKey.empty() &&
            routeAuthorityPolygonKeys.find(normalizedPolygonKey) !=
                routeAuthorityPolygonKeys.end()) {
            filtered.dataGaps.push_back(gap);
        }
    }
    return filtered;
}

bool ControllerAuthorityMatchesScopeKeys(
    const xvatsim::core::authority::ControllerAuthority& authority,
    const std::unordered_set<std::string>& scopeKeys) {
    if (scopeKeys.empty()) {
        return true;
    }

    auto keyMatches = [&](const std::string& rawKey) {
        const auto key = xvatsim::core::authority::NormalizeAuthorityToken(rawKey);
        if (!key.empty() && scopeKeys.find(key) != scopeKeys.end()) {
            return true;
        }
        const auto familyKey = AuthorityFamilyKey(key);
        return !familyKey.empty() && scopeKeys.find(familyKey) != scopeKeys.end();
    };

    if (keyMatches(authority.id) || keyMatches(authority.polygonKey)) {
        return true;
    }
    for (const auto& lookupKey : authority.lookupKeys) {
        if (keyMatches(lookupKey)) {
            return true;
        }
    }
    return false;
}

xvatsim::core::authority::ControllerAuthorityCatalog
FilterControllerAuthorityCatalogToScopeKeys(
    const xvatsim::core::authority::ControllerAuthorityCatalog& catalog,
    const std::unordered_set<std::string>& scopeKeys) {
    if (scopeKeys.empty()) {
        return catalog;
    }

    xvatsim::core::authority::ControllerAuthorityCatalog filtered;
    for (const auto& authority : catalog.authorities) {
        if (ControllerAuthorityMatchesScopeKeys(authority, scopeKeys)) {
            filtered.authorities.push_back(authority);
        }
    }
    for (const auto& gap : catalog.dataGaps) {
        const auto polygonKey =
            xvatsim::core::authority::NormalizeAuthorityToken(gap.polygonKey);
        const auto authorityId =
            xvatsim::core::authority::NormalizeAuthorityToken(gap.authorityId);
        const auto polygonFamilyKey = AuthorityFamilyKey(polygonKey);
        const auto authorityFamilyKey = AuthorityFamilyKey(authorityId);
        if ((!polygonKey.empty() && scopeKeys.find(polygonKey) != scopeKeys.end()) ||
            (!authorityId.empty() && scopeKeys.find(authorityId) != scopeKeys.end()) ||
            (!polygonFamilyKey.empty() &&
             scopeKeys.find(polygonFamilyKey) != scopeKeys.end()) ||
            (!authorityFamilyKey.empty() &&
             scopeKeys.find(authorityFamilyKey) != scopeKeys.end())) {
            filtered.dataGaps.push_back(gap);
        }
    }
    return filtered;
}

bool ControllerCanOwnRouteAuthority(
    const xvatsim::core::authority::ControllerAuthorityCatalog& controllerAuthorityCatalog,
    const std::unordered_set<std::string>& routeAuthorityMatchKeys,
    const brain::ControllerSnapshot& controller) {
    if (routeAuthorityMatchKeys.empty()) {
        return false;
    }

    const auto matches =
        xvatsim::core::authority::ResolveControllerAuthority(
            controllerAuthorityCatalog,
            controller.callsign,
            controller.frequency,
            controller.facility);
    for (const auto& match : matches) {
        const auto normalizedPolygonKey =
            xvatsim::core::authority::NormalizeAuthorityToken(match.polygonKey);
        if (!normalizedPolygonKey.empty() &&
            routeAuthorityMatchKeys.find(normalizedPolygonKey) !=
                routeAuthorityMatchKeys.end()) {
            return true;
        }
    }
    return false;
}

bool AuthorityEvidenceBelongsToRouteAuthorityKeys(
    const xvatsim::core::authority::AuthorityEvidence& evidence,
    const std::unordered_set<std::string>& routeAuthorityMatchKeys) {
    if (routeAuthorityMatchKeys.empty()) {
        return false;
    }

    const auto normalizedPolygonKey =
        xvatsim::core::authority::NormalizeAuthorityToken(evidence.polygonKey);
    return !normalizedPolygonKey.empty() &&
           routeAuthorityMatchKeys.find(normalizedPolygonKey) !=
               routeAuthorityMatchKeys.end();
}

std::vector<xvatsim::core::authority::AuthorityDecision>
FilterAuthorityDecisionsToRouteKeys(
    const std::vector<xvatsim::core::authority::AuthorityDecision>& decisions,
    const std::unordered_set<std::string>& routeAuthorityMatchKeys) {
    std::vector<xvatsim::core::authority::AuthorityDecision> filtered;
    filtered.reserve(decisions.size());
    for (const auto& decision : decisions) {
        if (AuthorityEvidenceBelongsToRouteAuthorityKeys(
                decision.evidence,
                routeAuthorityMatchKeys)) {
            filtered.push_back(decision);
        }
    }
    return filtered;
}

std::vector<xvatsim::core::authority::AuthorityDecision>
FilterTerminalAuthorityRejectionDecisions(
    const std::vector<xvatsim::core::authority::AuthorityDecision>& decisions) {
    std::vector<xvatsim::core::authority::AuthorityDecision> filtered;
    filtered.reserve(decisions.size());
    for (const auto& decision : decisions) {
        if (decision.accepted ||
            decision.evidence.authorityKind !=
                xvatsim::core::authority::AuthorityKind::Terminal ||
            decision.evidence.rejectionReasons.empty()) {
            continue;
        }
        filtered.push_back(decision);
    }
    return filtered;
}

void AppendAuthorityDecisionDiagnostics(
    std::vector<std::string>* diagnostics,
    const std::string& normalizedCallsign,
    const std::vector<xvatsim::core::authority::AuthorityDecision>& routeScopedDecisions) {
    if (diagnostics == nullptr || normalizedCallsign.empty()) {
        return;
    }

    bool appended = false;
    for (const auto& decision : routeScopedDecisions) {
        if (decision.accepted) {
            continue;
        }
        for (const auto& reason : decision.evidence.rejectionReasons) {
            AppendAuthorityDiagnostic(
                diagnostics,
                normalizedCallsign + ":" + reason + ":" +
                    decision.evidence.authorityId + ":" +
                    decision.evidence.polygonKey + ":" +
                    decision.evidence.matchedPattern);
            appended = true;
        }
    }

    if (!appended) {
        AppendAuthorityDiagnostic(
            diagnostics,
            normalizedCallsign + ":unmapped-controller");
    }
}

bool ActivePolygonBelongsToRouteAuthorityKeys(
    const xvatsim::core::authority::ActiveAuthorityPolygon& activePolygon,
    const xvatsim::core::authority::AuthorityPolygonCatalog& authorityPolygonCatalog,
    const std::unordered_set<std::string>& routeAuthorityPolygonKeys) {
    const auto normalizedActiveKey =
        xvatsim::core::authority::NormalizeAuthorityToken(activePolygon.polygonKey);
    if (!normalizedActiveKey.empty() &&
        routeAuthorityPolygonKeys.find(normalizedActiveKey) !=
        routeAuthorityPolygonKeys.end()) {
        return true;
    }

    const auto* polygon = FindAuthorityPolygonById(
        authorityPolygonCatalog,
        activePolygon.polygonId);
    if (polygon == nullptr) {
        return false;
    }

    return AuthorityPolygonMatchesRouteKeys(*polygon, routeAuthorityPolygonKeys);
}

void MarkActiveAuthorityPolygonEvidenceFiltered(
    std::vector<brain::AuthorityPolygonEvidenceSnapshot>* activeEvidence,
    const xvatsim::core::authority::ActiveAuthorityPolygon& activePolygon,
    const std::string& reason,
    std::optional<bool> routeKeyCompatible);

std::vector<xvatsim::core::authority::ActiveAuthorityPolygon>
FilterActivePolygonsToRouteAuthorityKeys(
    const std::vector<xvatsim::core::authority::ActiveAuthorityPolygon>& activePolygons,
    const xvatsim::core::authority::AuthorityPolygonCatalog& authorityPolygonCatalog,
    const std::unordered_set<std::string>& routeAuthorityPolygonKeys,
    std::vector<std::string>* diagnostics,
    std::vector<brain::AuthorityPolygonEvidenceSnapshot>* activeEvidence = nullptr) {
    if (routeAuthorityPolygonKeys.empty()) {
        return activePolygons;
    }

    std::vector<xvatsim::core::authority::ActiveAuthorityPolygon> filtered;
    filtered.reserve(activePolygons.size());
    for (const auto& activePolygon : activePolygons) {
        if (activePolygon.kind == xvatsim::core::authority::AuthorityKind::Terminal) {
            filtered.push_back(activePolygon);
            continue;
        }
        if (!ActivePolygonBelongsToRouteAuthorityKeys(
                activePolygon,
                authorityPolygonCatalog,
                routeAuthorityPolygonKeys)) {
            MarkActiveAuthorityPolygonEvidenceFiltered(
                activeEvidence,
                activePolygon,
                "active-not-relevant",
                false);
            AppendAuthorityDiagnostic(
                diagnostics,
                activePolygon.callsign + ":active-not-relevant:" +
                    activePolygon.authorityId + ":" +
                    activePolygon.polygonId + ":" +
                    AuthorityKindLabel(activePolygon.kind));
            continue;
        }
        filtered.push_back(activePolygon);
    }
    return filtered;
}

std::string SummarizeAuthorityDiagnostics(
    const std::vector<std::string>& diagnostics) {
    return SummarizeStrings(diagnostics);
}

void LogAuthorityDiagnosticsIfChanged(
    const brain::AuthorityRelevanceSnapshot& snapshot) {
    static std::mutex logMutex;
    static std::string lastSignature;

    std::ostringstream signatureStream;
    signatureStream << snapshot.available << "|"
                    << snapshot.stale << "|"
                    << snapshot.statusLine << "|"
                    << snapshot.relevantAuthorities.size() << "|"
                    << SummarizeAuthorityDiagnostics(snapshot.diagnostics);
    const auto signature = signatureStream.str();

    {
        std::lock_guard<std::mutex> lock(logMutex);
        if (signature == lastSignature) {
            return;
        }
        lastSignature = signature;
    }

    std::ostringstream stream;
    stream << "[XVatsim] Authority diagnostic: "
           << SanitizeDiagnosticText(snapshot.statusLine, 192)
           << " relevant=" << snapshot.relevantAuthorities.size()
           << " diagnostics=" << SummarizeAuthorityDiagnostics(snapshot.diagnostics)
           << "\n";
    auto line = stream.str();
    if (line.size() > kDiagnosticLogLineLimit) {
        line.resize(kDiagnosticLogLineLimit - 4);
        line += "...\n";
    }
    SafeXPlaneDebugString(line);
}

std::vector<std::string> SortedAuthorityStrings(
    const std::unordered_set<std::string>& values) {
    std::vector<std::string> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    return sorted;
}

void InitializeAuthorityRelevanceSourceEvidence(
    brain::AuthorityRelevanceSnapshot* snapshot,
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const brain::RouteSectorSnapshot& routeSectorSnapshot,
    const std::string& scheduleReason) {
    if (snapshot == nullptr) {
        return;
    }

    auto& source = snapshot->evidence.source;
    source.scheduled = !scheduleReason.empty();
    source.scheduleReason = scheduleReason;
    source.controllerFeedAvailable = controllerFeedSnapshot.available;
    source.controllerFeedStale = controllerFeedSnapshot.stale;
    source.controllerFeedGeneration = controllerFeedSnapshot.generation;
    if (controllerFeedSnapshot.controllers != nullptr) {
        source.sourceControllerCountKnown = true;
        source.sourceControllerCount =
            static_cast<int>(controllerFeedSnapshot.controllers->size());
    } else if (controllerFeedSnapshot.available && !controllerFeedSnapshot.stale) {
        source.sourceControllerCountKnown = true;
        source.sourceControllerCount = controllerFeedSnapshot.connectedControllers;
    }
    source.routeSnapshotAvailable = routeSectorSnapshot.available;
    source.routeSnapshotStale = routeSectorSnapshot.stale;
    source.routeResolved = routeSectorSnapshot.routeResolved;
}

brain::AuthorityControllerEvidenceSnapshot BuildAuthorityControllerEvidence(
    const brain::ControllerSnapshot& controller,
    bool airportLocalCandidate,
    bool airspaceAuthorityCandidate) {
    brain::AuthorityControllerEvidenceSnapshot evidence;
    evidence.callsign = controller.callsign;
    evidence.frequency = controller.frequency;
    evidence.facility = controller.facility;
    evidence.actionable = controller.actionable;
    evidence.atis = controller.atis;
    evidence.guardFrequency = IsGuardFrequency(controller.frequency);
    evidence.emptyCallsign = controller.callsign.empty();
    evidence.airportLocalCandidate = airportLocalCandidate;
    evidence.airspaceAuthorityCandidate = airspaceAuthorityCandidate;
    evidence.sourceControllerConsidered = true;

    if (!controller.actionable) {
        evidence.evidenceReasons.push_back("non-actionable");
    }
    if (controller.atis) {
        evidence.evidenceReasons.push_back("atis");
    }
    if (evidence.guardFrequency) {
        evidence.evidenceReasons.push_back("guard-frequency");
    }
    if (evidence.emptyCallsign) {
        evidence.evidenceReasons.push_back("empty-callsign");
    }
    if (!airportLocalCandidate && !airspaceAuthorityCandidate) {
        evidence.evidenceReasons.push_back("not-authority-candidate");
    }
    return evidence;
}

void PopulateBasicAuthorityControllerEvidence(
    brain::AuthorityRelevanceSnapshot* snapshot,
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot) {
    if (snapshot == nullptr || !controllerFeedSnapshot.available ||
        controllerFeedSnapshot.stale || controllerFeedSnapshot.controllers == nullptr) {
        return;
    }

    snapshot->evidence.controllerEvidence.clear();
    snapshot->evidence.controllerEvidence.reserve(
        controllerFeedSnapshot.controllers->size());
    for (const auto& controller : *controllerFeedSnapshot.controllers) {
        snapshot->evidence.controllerEvidence.push_back(
            BuildAuthorityControllerEvidence(
                controller,
                IsAirportLocalControllerCandidate(controller),
                IsAuthorityControllerCandidate(controller)));
    }
}

bool AuthorityDecisionEvidenceMatches(
    const xvatsim::core::authority::AuthorityDecision& left,
    const xvatsim::core::authority::AuthorityDecision& right) {
    return left.accepted == right.accepted &&
           left.evidence.authorityId == right.evidence.authorityId &&
           left.evidence.polygonKey == right.evidence.polygonKey &&
           left.evidence.matchedPattern == right.evidence.matchedPattern;
}

bool AuthorityDecisionMatchesAny(
    const xvatsim::core::authority::AuthorityDecision& decision,
    const std::vector<xvatsim::core::authority::AuthorityDecision>& decisions) {
    return std::any_of(
        decisions.begin(),
        decisions.end(),
        [&](const auto& candidate) {
            return AuthorityDecisionEvidenceMatches(decision, candidate);
        });
}

brain::AuthorityDecisionEvidenceSnapshot BuildAuthorityDecisionEvidence(
    const xvatsim::core::authority::AuthorityDecision& decision,
    const std::vector<xvatsim::core::authority::AuthorityDecision>&
        routeScopedDecisions) {
    brain::AuthorityDecisionEvidenceSnapshot evidence;
    evidence.authorityId = decision.evidence.authorityId;
    evidence.authoritySource =
        xvatsim::core::authority::AuthoritySourceLabel(
            decision.evidence.authoritySource);
    evidence.authorityKind = AuthorityKindLabel(decision.evidence.authorityKind);
    evidence.polygonKey = decision.evidence.polygonKey;
    evidence.matchedPattern = decision.evidence.matchedPattern;
    evidence.accepted = decision.accepted;
    evidence.routeScopeMatched =
        AuthorityDecisionMatchesAny(decision, routeScopedDecisions);
    evidence.rejectionReasons = decision.evidence.rejectionReasons;
    return evidence;
}

std::vector<brain::AuthorityDecisionEvidenceSnapshot>
BuildAuthorityDecisionEvidenceList(
    const std::vector<xvatsim::core::authority::AuthorityDecision>& decisions,
    const std::vector<xvatsim::core::authority::AuthorityDecision>&
        routeScopedDecisions) {
    std::vector<brain::AuthorityDecisionEvidenceSnapshot> evidence;
    evidence.reserve(decisions.size());
    for (const auto& decision : decisions) {
        evidence.push_back(
            BuildAuthorityDecisionEvidence(decision, routeScopedDecisions));
    }
    return evidence;
}

std::string StableAuthorityProofEntryKey(
    const std::string& callsign,
    const std::string& frequency,
    int facility,
    const std::string& authorityId,
    const std::string& polygonKey,
    const std::string& matchedPattern) {
    std::ostringstream stream;
    stream << xvatsim::core::authority::NormalizeControllerCallsign(callsign)
           << '|'
           << NormalizeFrequency(frequency)
           << '|'
           << facility
           << '|'
           << xvatsim::core::authority::NormalizeAuthorityToken(authorityId)
           << '|'
           << xvatsim::core::authority::NormalizeAuthorityToken(polygonKey)
           << '|'
           << xvatsim::core::authority::NormalizeAuthorityToken(matchedPattern);
    return stream.str();
}

std::string StableAuthorityRelevantKey(
    const std::string& callsign,
    const std::string& authorityId,
    const std::string& polygonKey,
    const std::string& matchedPattern) {
    std::ostringstream stream;
    stream << xvatsim::core::authority::NormalizeControllerCallsign(callsign)
           << '|'
           << xvatsim::core::authority::NormalizeAuthorityToken(authorityId)
           << '|'
           << xvatsim::core::authority::NormalizeAuthorityToken(polygonKey)
           << '|'
           << xvatsim::core::authority::NormalizeAuthorityToken(matchedPattern);
    return stream.str();
}

void SortUniqueAuthorityProofKeys(std::vector<std::string>* values) {
    if (values == nullptr) {
        return;
    }
    std::sort(values->begin(), values->end());
    values->erase(std::unique(values->begin(), values->end()), values->end());
}

bool SortedVectorIncludes(
    const std::vector<std::string>& superset,
    const std::vector<std::string>& subset) {
    return std::includes(
        superset.begin(),
        superset.end(),
        subset.begin(),
        subset.end());
}

bool IsUsableDirectAuthorityProofController(
    const brain::ControllerSnapshot& controller) {
    return controller.actionable &&
           !controller.atis &&
           !IsGuardFrequency(controller.frequency);
}

void AddCurrentDirectAuthorityProofKeys(
    const brain::ControllerSnapshot& controller,
    const xvatsim::core::authority::ControllerAuthorityCatalog&
        routeScopedControllerAuthorityCatalog,
    const std::unordered_set<std::string>& routeAuthorityMatchKeys,
    std::vector<std::string>* proofEntries,
    std::vector<std::string>* relevantKeys) {
    if (proofEntries == nullptr || relevantKeys == nullptr ||
        !IsUsableDirectAuthorityProofController(controller)) {
        return;
    }

    const auto authorityDecisions =
        xvatsim::core::authority::EvaluateControllerAuthority(
            routeScopedControllerAuthorityCatalog,
            controller.callsign,
            controller.frequency,
            controller.facility);
    const auto routeScopedDecisions =
        FilterAuthorityDecisionsToRouteKeys(
            authorityDecisions,
            routeAuthorityMatchKeys);
    for (const auto& decision : routeScopedDecisions) {
        if (!decision.accepted) {
            continue;
        }
        proofEntries->push_back(
            StableAuthorityProofEntryKey(
                controller.callsign,
                controller.frequency,
                controller.facility,
                decision.evidence.authorityId,
                decision.evidence.polygonKey,
                decision.evidence.matchedPattern));
        relevantKeys->push_back(
            StableAuthorityRelevantKey(
                controller.callsign,
                decision.evidence.authorityId,
                decision.evidence.polygonKey,
                decision.evidence.matchedPattern));
    }
}

std::vector<std::string> BuildCurrentStableAuthorityProofEntries(
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const xvatsim::core::authority::ControllerAuthorityCatalog&
        routeScopedControllerAuthorityCatalog,
    const std::unordered_set<std::string>& routeAuthorityMatchKeys,
    std::vector<std::string>* outRelevantKeys) {
    std::vector<std::string> proofEntries;
    std::vector<std::string> relevantKeys;
    if (!controllerFeedSnapshot.available ||
        controllerFeedSnapshot.stale ||
        controllerFeedSnapshot.controllers == nullptr) {
        if (outRelevantKeys != nullptr) {
            *outRelevantKeys = {};
        }
        return proofEntries;
    }

    for (const auto& controller : *controllerFeedSnapshot.controllers) {
        if (!IsAirportLocalControllerCandidate(controller) &&
            !IsAuthorityControllerCandidate(controller)) {
            continue;
        }
        AddCurrentDirectAuthorityProofKeys(
            controller,
            routeScopedControllerAuthorityCatalog,
            routeAuthorityMatchKeys,
            &proofEntries,
            &relevantKeys);
    }

    SortUniqueAuthorityProofKeys(&proofEntries);
    SortUniqueAuthorityProofKeys(&relevantKeys);
    if (outRelevantKeys != nullptr) {
        *outRelevantKeys = relevantKeys;
    }
    return proofEntries;
}

std::vector<std::string> BuildCachedStableAuthorityProofEntries(
    const brain::AuthorityRelevanceSnapshot& snapshot,
    std::vector<std::string>* outRelevantKeys) {
    std::vector<std::string> proofEntries;
    std::vector<std::string> relevantKeys;
    for (const auto& controllerEvidence : snapshot.evidence.controllerEvidence) {
        if (!controllerEvidence.actionable ||
            controllerEvidence.atis ||
            controllerEvidence.guardFrequency) {
            continue;
        }
        for (const auto& decision : controllerEvidence.authorityDecisions) {
            if (!decision.accepted || !decision.routeScopeMatched) {
                continue;
            }
            proofEntries.push_back(
                StableAuthorityProofEntryKey(
                    controllerEvidence.callsign,
                    controllerEvidence.frequency,
                    controllerEvidence.facility,
                    decision.authorityId,
                    decision.polygonKey,
                    decision.matchedPattern));
            relevantKeys.push_back(
                StableAuthorityRelevantKey(
                    controllerEvidence.callsign,
                    decision.authorityId,
                    decision.polygonKey,
                    decision.matchedPattern));
        }
    }
    SortUniqueAuthorityProofKeys(&proofEntries);
    SortUniqueAuthorityProofKeys(&relevantKeys);
    if (outRelevantKeys != nullptr) {
        *outRelevantKeys = relevantKeys;
    }
    return proofEntries;
}

struct RefreshedAuthorityRelevantSlice {
    std::vector<xvatsim::core::authority::RelevantAuthorityPolygon>
        relevantAuthorityPolygons;
    std::vector<brain::RelevantAuthoritySnapshot> relevantAuthorities;
};

RefreshedAuthorityRelevantSlice BuildCurrentRelevantAuthoritySliceFromLiveMap(
    const std::vector<xvatsim::core::authority::ActiveAuthorityPolygon>&
        activePolygons,
    const xvatsim::core::authority::AuthorityPolygonCatalog&
        authorityPolygonCatalog,
    const brain::LiveAuthorityMapSnapshot& liveMap,
    bool hasAircraftPosition,
    const xvatsim::core::authority::GeoPoint& aircraftPosition,
    const std::vector<xvatsim::core::authority::GeoPoint>& routePoints) {
    RefreshedAuthorityRelevantSlice slice;
    if (!liveMap.available || liveMap.stale || activePolygons.empty()) {
        return slice;
    }

    slice.relevantAuthorityPolygons =
        xvatsim::core::authority::ResolveRelevantAuthorityPolygons(
            activePolygons,
            authorityPolygonCatalog,
            hasAircraftPosition,
            aircraftPosition,
            routePoints);
    slice.relevantAuthorities.reserve(slice.relevantAuthorityPolygons.size());
    for (const auto& relevantAuthorityPolygon :
         slice.relevantAuthorityPolygons) {
        brain::RelevantAuthoritySnapshot relevantAuthority;
        relevantAuthority.callsign =
            relevantAuthorityPolygon.activePolygon.callsign;
        relevantAuthority.authorityId =
            relevantAuthorityPolygon.activePolygon.authorityId;
        relevantAuthority.polygonId =
            relevantAuthorityPolygon.activePolygon.polygonId;
        relevantAuthority.polygonKey =
            relevantAuthorityPolygon.activePolygon.polygonKey;
        relevantAuthority.matchedPattern =
            relevantAuthorityPolygon.activePolygon.matchedPattern;
        relevantAuthority.proofSource =
            AuthorityProofSourceLabel(relevantAuthorityPolygon.activePolygon);
        relevantAuthority.proofDetail =
            AuthorityRelevanceProofDetail(relevantAuthorityPolygon);
        relevantAuthority.kind =
            ToBrainAuthorityRelevanceKind(
                relevantAuthorityPolygon.activePolygon.kind);
        relevantAuthority.aircraftInside =
            relevantAuthorityPolygon.aircraftInside;
        relevantAuthority.routeIntersects =
            relevantAuthorityPolygon.routeIntersects;
        relevantAuthority.routeEntryDistanceNm =
            relevantAuthorityPolygon.routeEntryDistanceNm;

        const auto assignment = std::find_if(
            liveMap.assignments.begin(),
            liveMap.assignments.end(),
            [&](const auto& value) {
                return brain::IsActiveLiveAuthorityState(value.state) &&
                       value.canonicalPolygonId ==
                           relevantAuthority.polygonId &&
                       value.positionId == relevantAuthority.authorityId &&
                       xvatsim::core::authority::NormalizeControllerCallsign(
                           value.callsign) ==
                           xvatsim::core::authority::NormalizeControllerCallsign(
                               relevantAuthority.callsign);
            });
        if (assignment != liveMap.assignments.end()) {
            relevantAuthority.frequency = assignment->frequency;
            relevantAuthority.proofSource = assignment->authoritySource;
        }

        slice.relevantAuthorities.push_back(std::move(relevantAuthority));
    }
    return slice;
}

std::string BuildAuthorityCacheReuseStatusLine(
    const brain::AuthorityRelevanceSnapshot& snapshot,
    const std::vector<std::string>& routeAuthorityKeys) {
    std::ostringstream status;
    status << "AUTHORITY cache reuse routeKeys=" << routeAuthorityKeys.size()
           << " relevant=" << snapshot.relevantAuthorities.size();
    return status.str();
}

std::unordered_set<std::string> BuildAuthorityPolygonIdSet(
    const xvatsim::core::authority::AuthorityPolygonCatalog& catalog) {
    std::unordered_set<std::string> ids;
    ids.reserve(catalog.polygons.size());
    for (const auto& polygon : catalog.polygons) {
        if (!polygon.id.empty()) {
            ids.insert(polygon.id);
        }
    }
    return ids;
}

std::unordered_map<std::string, RouteScopedAuthorityPolygon>
BuildRouteScopedAuthorityPolygonsById(
    const std::vector<RouteScopedAuthorityPolygon>& scopedPolygons) {
    std::unordered_map<std::string, RouteScopedAuthorityPolygon> byId;
    byId.reserve(scopedPolygons.size());
    for (const auto& scopedPolygon : scopedPolygons) {
        if (scopedPolygon.polygon == nullptr || scopedPolygon.polygon->id.empty()) {
            continue;
        }
        byId[scopedPolygon.polygon->id] = scopedPolygon;
    }
    return byId;
}

std::string ActiveAuthorityProofSourceEvidenceLabel(
    const xvatsim::core::authority::ActiveAuthorityPolygon& activePolygon) {
    if (!activePolygon.proofSource.empty()) {
        return activePolygon.proofSource;
    }
    if (activePolygon.matchedPattern.find('*') != std::string::npos) {
        return "CATALOG_PATTERN";
    }
    return "CATALOG_EXACT";
}

brain::AuthorityPolygonEvidenceSnapshot BuildAuthorityPolygonEvidence(
    const xvatsim::core::authority::AuthorityPolygon& polygon,
    const std::unordered_set<std::string>& routeAuthorityPolygonKeys,
    const std::unordered_set<std::string>& routeEndpointAuthorityKeys,
    const std::unordered_set<std::string>& queryScopedPolygonIds,
    const std::unordered_map<std::string, RouteScopedAuthorityPolygon>&
        scopedPolygonsById) {
    brain::AuthorityPolygonEvidenceSnapshot evidence;
    evidence.polygonId = polygon.id;
    evidence.polygonKey = polygon.polygonKey;
    evidence.authoritySource =
        xvatsim::core::authority::AuthoritySourceLabel(polygon.source);
    evidence.authorityKind = AuthorityKindLabel(polygon.kind);
    evidence.routeKeyMatch =
        AuthorityPolygonMatchesRouteKeys(polygon, routeAuthorityPolygonKeys);
    evidence.routeFamilyMatch =
        !evidence.routeKeyMatch &&
        AuthorityPolygonFamilyMatchesRouteKeys(polygon, routeAuthorityPolygonKeys);
    evidence.routeEndpointMatch =
        AuthorityPolygonMatchesRouteKeys(polygon, routeEndpointAuthorityKeys);
    evidence.inQueryScope =
        queryScopedPolygonIds.find(polygon.id) != queryScopedPolygonIds.end();
    evidence.routeKeyCompatible =
        evidence.routeKeyMatch ||
        evidence.routeFamilyMatch ||
        evidence.routeEndpointMatch;
    const auto scopedIt = scopedPolygonsById.find(polygon.id);
    if (scopedIt != scopedPolygonsById.end()) {
        evidence.routeGeometryRelevant =
            scopedIt->second.aircraftInside || scopedIt->second.routeIntersects;
        evidence.aircraftInside = scopedIt->second.aircraftInside;
        evidence.routeIntersects = scopedIt->second.routeIntersects;
        evidence.routeEntryDistanceNm = scopedIt->second.routeEntryDistanceNm;
    }
    if (!evidence.inQueryScope) {
        if (!evidence.routeKeyCompatible) {
            evidence.queryScopeOutReason = "route-key-filtered";
        } else {
            evidence.queryScopeOutReason = "route-geometry-filtered";
        }
    }
    return evidence;
}

brain::AuthorityPolygonEvidenceSnapshot BuildActiveAuthorityPolygonEvidence(
    const xvatsim::core::authority::ActiveAuthorityPolygon& activePolygon,
    const xvatsim::core::authority::AuthorityPolygonCatalog& authorityPolygonCatalog,
    const std::unordered_set<std::string>& routeAuthorityPolygonKeys) {
    brain::AuthorityPolygonEvidenceSnapshot evidence;
    evidence.callsign = activePolygon.callsign;
    evidence.authorityId = activePolygon.authorityId;
    evidence.polygonId = activePolygon.polygonId;
    evidence.polygonKey = activePolygon.polygonKey;
    evidence.matchedPattern = activePolygon.matchedPattern;
    evidence.authoritySource =
        xvatsim::core::authority::AuthoritySourceLabel(
            activePolygon.polygonSource);
    evidence.authorityKind = AuthorityKindLabel(activePolygon.kind);
    evidence.activePolygon = true;
    evidence.activeProofSource =
        ActiveAuthorityProofSourceEvidenceLabel(activePolygon);
    evidence.activeProofDetail = activePolygon.proofDetail;
    evidence.routeKeyMatch =
        ActivePolygonBelongsToRouteAuthorityKeys(
            activePolygon,
            authorityPolygonCatalog,
            routeAuthorityPolygonKeys);
    evidence.routeKeyCompatible = evidence.routeKeyMatch;
    if (!evidence.routeKeyCompatible &&
        activePolygon.kind != xvatsim::core::authority::AuthorityKind::Terminal) {
        evidence.filteredReason = "active-not-relevant";
    }
    return evidence;
}

bool AuthorityPolygonEvidenceMatchesActive(
    const brain::AuthorityPolygonEvidenceSnapshot& evidence,
    const xvatsim::core::authority::ActiveAuthorityPolygon& activePolygon) {
    return evidence.activePolygon &&
           evidence.callsign == activePolygon.callsign &&
           evidence.authorityId == activePolygon.authorityId &&
           evidence.polygonId == activePolygon.polygonId &&
           evidence.polygonKey == activePolygon.polygonKey &&
           evidence.matchedPattern == activePolygon.matchedPattern;
}

void UpsertActiveAuthorityPolygonEvidence(
    std::vector<brain::AuthorityPolygonEvidenceSnapshot>* activeEvidence,
    brain::AuthorityPolygonEvidenceSnapshot evidence) {
    if (activeEvidence == nullptr) {
        return;
    }
    for (auto& existing : *activeEvidence) {
        if (existing.activePolygon &&
            existing.callsign == evidence.callsign &&
            existing.authorityId == evidence.authorityId &&
            existing.polygonId == evidence.polygonId &&
            existing.polygonKey == evidence.polygonKey &&
            existing.matchedPattern == evidence.matchedPattern) {
            const auto existingReason = existing.filteredReason;
            existing = std::move(evidence);
            if (!existingReason.empty() &&
                existing.filteredReason.empty()) {
                existing.filteredReason = existingReason;
            }
            return;
        }
    }
    activeEvidence->push_back(std::move(evidence));
}

void MarkActiveAuthorityPolygonEvidenceFiltered(
    std::vector<brain::AuthorityPolygonEvidenceSnapshot>* activeEvidence,
    const xvatsim::core::authority::ActiveAuthorityPolygon& activePolygon,
    const std::string& reason,
    std::optional<bool> routeKeyCompatible) {
    if (activeEvidence == nullptr) {
        return;
    }
    for (auto& evidence : *activeEvidence) {
        if (!AuthorityPolygonEvidenceMatchesActive(evidence, activePolygon)) {
            continue;
        }
        if (!reason.empty()) {
            evidence.filteredReason = reason;
        }
        if (routeKeyCompatible.has_value()) {
            evidence.routeKeyCompatible = *routeKeyCompatible;
        }
        return;
    }
}

void FinalizeAuthorityRelevanceEvidenceGuardrails(
    brain::AuthorityRelevanceSnapshot* snapshot) {
    if (snapshot == nullptr) {
        return;
    }

    if (snapshot->evidence.source.sourceControllerCountKnown) {
        snapshot->droppedBeforeBrainControllers = std::max(
            0,
            snapshot->evidence.source.sourceControllerCount -
                static_cast<int>(snapshot->evidence.controllerEvidence.size()));
    } else {
        snapshot->droppedBeforeBrainControllers = 0;
    }
}

void ResetAuthorityEvidenceRelevantSurvivors(
    brain::AuthorityRelevanceSnapshot* snapshot) {
    if (snapshot == nullptr) {
        return;
    }
    for (auto& controllerEvidence : snapshot->evidence.controllerEvidence) {
        for (auto& decision : controllerEvidence.authorityDecisions) {
            decision.relevant = false;
        }
        for (auto& polygon : controllerEvidence.activePolygons) {
            polygon.relevant = false;
            if (polygon.filteredReason == "active-not-relevant" ||
                polygon.filteredReason ==
                    "active-not-relevant-geometry") {
                polygon.filteredReason.clear();
            }
        }
    }
    for (auto& polygon : snapshot->evidence.activePolygonEvidence) {
        polygon.relevant = false;
        if (polygon.filteredReason == "active-not-relevant" ||
            polygon.filteredReason ==
                "active-not-relevant-geometry") {
            polygon.filteredReason.clear();
        }
    }
    for (auto& polygon : snapshot->evidence.polygonEvidence) {
        polygon.relevant = false;
        polygon.routeGeometryRelevant = false;
        polygon.aircraftInside = false;
        polygon.routeIntersects = false;
        polygon.routeEntryDistanceNm = 0.0;
    }
}

brain::AuthorityRelevanceSnapshot BuildAuthorityProofReuseSnapshot(
    const brain::AuthorityRelevanceSnapshot& cachedSnapshot,
    const brain::LiveAuthorityMapSnapshot& liveMap,
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const brain::RouteSectorSnapshot& routeSectorSnapshot,
    const AuthorityRelevanceWorkScope& workScope,
    const std::vector<std::string>& routeAuthorityKeys,
    const std::vector<std::string>& routeAuthorityMatchKeys,
    const RefreshedAuthorityRelevantSlice& refreshedSlice,
    const std::string& cacheStatus,
    const std::string& cacheReason,
    std::uint64_t terminalBoundaryGeneration) {
    auto snapshot = cachedSnapshot;
    snapshot.liveMap = liveMap;
    snapshot.controllerFeedGeneration = controllerFeedSnapshot.generation;
    snapshot.centerBoundaryGeneration =
        routeSectorSnapshot.centerBoundaryGeneration;
    snapshot.authorityCatalogGeneration =
        routeSectorSnapshot.authorityCatalogGeneration;
    snapshot.terminalCoverageGeneration = terminalBoundaryGeneration;
    snapshot.diagnosticCacheStatus = cacheStatus;
    snapshot.diagnosticReason = cacheReason;
    snapshot.diagnosticWorkStage = workScope.stage;
    snapshot.diagnosticWindowNm = workScope.windowNm;
    snapshot.diagnosticDeferredSectorCount =
        static_cast<int>(workScope.deferredSectorCount);

    auto& source = snapshot.evidence.source;
    source.controllerFeedAvailable = controllerFeedSnapshot.available;
    source.controllerFeedStale = controllerFeedSnapshot.stale;
    source.controllerFeedGeneration = controllerFeedSnapshot.generation;
    if (controllerFeedSnapshot.controllers != nullptr) {
        source.sourceControllerCountKnown = true;
        source.sourceControllerCount =
            static_cast<int>(controllerFeedSnapshot.controllers->size());
    } else if (controllerFeedSnapshot.available &&
               !controllerFeedSnapshot.stale) {
        source.sourceControllerCountKnown = true;
        source.sourceControllerCount =
            controllerFeedSnapshot.connectedControllers;
    }
    source.routeSnapshotAvailable = routeSectorSnapshot.available;
    source.routeSnapshotStale = routeSectorSnapshot.stale;
    source.routeResolved = routeSectorSnapshot.routeResolved;
    source.workStage = workScope.stage;
    source.workWindowNm = workScope.windowNm;
    source.workDeferredSectorCount =
        static_cast<int>(workScope.deferredSectorCount);
    source.routeAuthorityKeys = routeAuthorityKeys;
    source.routeAuthorityMatchKeys = routeAuthorityMatchKeys;
    source.cacheHit = true;
    source.cacheStatus = cacheStatus;
    source.cacheReason = cacheReason;
    ResetAuthorityEvidenceRelevantSurvivors(&snapshot);
    MarkAuthorityEvidenceRelevantSurvivors(
        &snapshot,
        refreshedSlice.relevantAuthorityPolygons);
    for (auto& assignment : snapshot.liveMap.assignments) {
        assignment.aircraftInside = false;
        assignment.routeIntersects = false;
        assignment.routeEntryDistanceNm = 0.0;
        if (!brain::IsActiveLiveAuthorityState(assignment.state)) {
            continue;
        }
        const auto relevantIt = std::find_if(
            refreshedSlice.relevantAuthorityPolygons.begin(),
            refreshedSlice.relevantAuthorityPolygons.end(),
            [&](const auto& relevant) {
                return relevant.activePolygon.polygonId ==
                           assignment.canonicalPolygonId &&
                       relevant.activePolygon.authorityId ==
                           assignment.positionId &&
                       xvatsim::core::authority::NormalizeControllerCallsign(
                           relevant.activePolygon.callsign) ==
                           xvatsim::core::authority::NormalizeControllerCallsign(
                               assignment.callsign);
            });
        if (relevantIt ==
            refreshedSlice.relevantAuthorityPolygons.end()) {
            continue;
        }
        assignment.aircraftInside = relevantIt->aircraftInside;
        assignment.routeIntersects = relevantIt->routeIntersects;
        assignment.routeEntryDistanceNm = relevantIt->routeEntryDistanceNm;
    }
    snapshot.liveRelevantAuthoritiesBrainOwned = false;
    snapshot.relevantAuthorities = refreshedSlice.relevantAuthorities;
    snapshot.statusLine =
        BuildAuthorityCacheReuseStatusLine(snapshot, routeAuthorityKeys);
    FinalizeAuthorityRelevanceEvidenceGuardrails(&snapshot);
    snapshot =
        brain::BuildBrainOwnedAuthorityRelevanceSnapshot(
            std::move(snapshot),
            {});
    return snapshot;
}

void MarkAuthorityEvidenceRelevantSurvivors(
    brain::AuthorityRelevanceSnapshot* snapshot,
    const std::vector<xvatsim::core::authority::RelevantAuthorityPolygon>&
        relevantAuthorityPolygons) {
    if (snapshot == nullptr) {
        return;
    }

    auto relevantMatchesActive = [&](const std::string& callsign,
                                     const std::string& authorityId,
                                     const std::string& polygonId,
                                     const std::string& matchedPattern) {
        return std::any_of(
            relevantAuthorityPolygons.begin(),
            relevantAuthorityPolygons.end(),
            [&](const auto& relevant) {
                const auto& active = relevant.activePolygon;
                return active.callsign == callsign &&
                       active.authorityId == authorityId &&
                       active.polygonId == polygonId &&
                       active.matchedPattern == matchedPattern;
            });
    };

    auto relevantMatchesDecision = [&](const std::string& callsign,
                                       const brain::AuthorityDecisionEvidenceSnapshot&
                                           decision) {
        return std::any_of(
            relevantAuthorityPolygons.begin(),
            relevantAuthorityPolygons.end(),
            [&](const auto& relevant) {
                const auto& active = relevant.activePolygon;
                return active.callsign == callsign &&
                       active.authorityId == decision.authorityId &&
                       active.polygonKey == decision.polygonKey &&
                       active.matchedPattern == decision.matchedPattern;
            });
    };

    for (auto& controllerEvidence : snapshot->evidence.controllerEvidence) {
        for (auto& decision : controllerEvidence.authorityDecisions) {
            decision.relevant =
                relevantMatchesDecision(controllerEvidence.callsign, decision);
        }
        for (auto& polygon : controllerEvidence.activePolygons) {
            polygon.relevant =
                relevantMatchesActive(
                    polygon.callsign,
                    polygon.authorityId,
                    polygon.polygonId,
                    polygon.matchedPattern);
            if (!polygon.relevant &&
                polygon.filteredReason.empty()) {
                polygon.filteredReason = "active-not-relevant-geometry";
            }
        }
    }

    for (auto& polygon : snapshot->evidence.activePolygonEvidence) {
        polygon.relevant =
            relevantMatchesActive(
                polygon.callsign,
                polygon.authorityId,
                polygon.polygonId,
                polygon.matchedPattern);
        if (!polygon.relevant &&
            polygon.filteredReason.empty()) {
            polygon.filteredReason = "active-not-relevant";
        }
    }

    for (auto& polygon : snapshot->evidence.polygonEvidence) {
        for (const auto& relevant : relevantAuthorityPolygons) {
            if (relevant.activePolygon.polygonId != polygon.polygonId) {
                continue;
            }
            polygon.relevant = true;
            polygon.routeGeometryRelevant = true;
            polygon.aircraftInside = relevant.aircraftInside;
            polygon.routeIntersects = relevant.routeIntersects;
            polygon.routeEntryDistanceNm = relevant.routeEntryDistanceNm;
        }
    }
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

bool RingCrossesAntiMeridian(const std::vector<GeoPoint>& ring) {
    double minLongitudeDeg = std::numeric_limits<double>::max();
    double maxLongitudeDeg = std::numeric_limits<double>::lowest();
    for (std::size_t index = 0; index < ring.size(); ++index) {
        const auto longitudeDeg = NormalizeLongitudeDeg(ring[index].longitudeDeg);
        minLongitudeDeg = std::min(minLongitudeDeg, longitudeDeg);
        maxLongitudeDeg = std::max(maxLongitudeDeg, longitudeDeg);
        if (index > 0 &&
            CrossesAntiMeridian(
                ring[index - 1].longitudeDeg,
                ring[index].longitudeDeg)) {
            return true;
        }
    }

    return (maxLongitudeDeg - minLongitudeDeg) > 180.0;
}

bool PointInRing(
    const GeoPoint& point,
    const std::vector<GeoPoint>& ring) {
    bool inside = false;
    const auto count = ring.size();
    if (count < 3) {
        return false;
    }

    const auto crossesAntiMeridian = RingCrossesAntiMeridian(ring);
    const auto pointLongitudeDeg = NormalizeLongitudeDeg(point.longitudeDeg);

    for (std::size_t i = 0, j = count - 1; i < count; j = i++) {
        const auto& pi = ring[i];
        const auto& pj = ring[j];
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
        if (!PointInRing(point, polygon.ring)) {
            continue;
        }
        const auto insideHole = std::any_of(
            polygon.holes.begin(),
            polygon.holes.end(),
            [&](const auto& hole) { return PointInRing(point, hole); });
        if (!insideHole) {
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
    const auto& source = ActiveRoutePreparationState().source;
    if (source == nullptr) {
        return {};
    }
    return source->ReadTextFile(std::filesystem::path(path));
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

bool IsXPlaneAirwayPayloadNonRecordLine(const std::string& line) {
    const auto trimmed = TrimAsciiWhitespace(line);
    return trimmed.empty() || trimmed == "I" || trimmed == "99";
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

    const auto& root = ActiveRoutePreparationState().xplaneRootPath;
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
    static const std::unordered_map<std::string, xvatsim::core::route::ProcedureCatalogEntry>
        empty;

    const auto normalizedIcao =
        xvatsim::core::route::NormalizeRouteToken(airportIcao);
    if (normalizedIcao.empty()) {
        return empty;
    }

    auto& cache = ActiveRoutePreparationState().procedureMetadataByAirport;
    const auto cached = cache.find(normalizedIcao);
    if (cached != cache.end()) {
        return cached->second;
    }

    auto loadedMetadata = LoadAirportProcedureMetadata(normalizedIcao);

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
    const auto& root = ActiveRoutePreparationState().xplaneRootPath;

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
    const auto& root = ActiveRoutePreparationState().xplaneRootPath;

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
    const auto& root = ActiveRoutePreparationState().xplaneRootPath;

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
    auto& state = ActiveRoutePreparationState();
    if (state.airwayGraphInitialized) {
        return state.airwayGraph;
    }

    LoadFixNodes(&state.airwayGraph);
    LoadNavNodes(&state.airwayGraph);

    std::istringstream stream(LoadAirwayDataPayload());
    std::string line;
    while (std::getline(stream, line)) {
        if (IsXPlaneAirwayPayloadNonRecordLine(line)) {
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
            state.airwayGraph,
            startIdent,
            startRegion,
            startNavDataType);
        const auto endNodeIndex = FindExactGraphNodeIndex(
            state.airwayGraph,
            endIdent,
            endRegion,
            endNavDataType);
        if (!startNodeIndex.has_value() || !endNodeIndex.has_value()) {
            continue;
        }

        const auto edgeDistanceNm = GreatCircleDistanceNm(
            state.airwayGraph.nodes[*startNodeIndex].latitudeDeg,
            state.airwayGraph.nodes[*startNodeIndex].longitudeDeg,
            state.airwayGraph.nodes[*endNodeIndex].latitudeDeg,
            state.airwayGraph.nodes[*endNodeIndex].longitudeDeg);

        const auto direction = fields[6];
        const auto addForward = direction != "B";
        const auto addBackward = direction == "N" || direction == "B";

        for (const auto& airwayName : airwayNames) {
            auto& adjacency =
                state.airwayGraph.adjacencyByAirway[airwayName];
            if (addForward) {
                adjacency[*startNodeIndex].push_back({*endNodeIndex, edgeDistanceNm});
            }
            if (addBackward) {
                adjacency[*endNodeIndex].push_back({*startNodeIndex, edgeDistanceNm});
            }
        }
    }

    state.airwayGraphInitialized = true;
    return state.airwayGraph;
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

double DistanceFromPointToAuthoritySegmentNm(
    const xvatsim::core::authority::GeoPoint& point,
    const xvatsim::core::authority::GeoPoint& segmentStart,
    const xvatsim::core::authority::GeoPoint& segmentEnd) {
    const GeoPoint localPoint{point.latitudeDeg, point.longitudeDeg};
    const brain::RouteWaypointSnapshot localStart{
        {},
        segmentStart.latitudeDeg,
        segmentStart.longitudeDeg,
    };
    const brain::RouteWaypointSnapshot localEnd{
        {},
        segmentEnd.latitudeDeg,
        segmentEnd.longitudeDeg,
    };
    return DistanceFromPointToRouteSegmentNm(localPoint, localStart, localEnd);
}

double DistanceFromPointToAuthorityPolygonNm(
    const xvatsim::core::authority::GeoPoint& point,
    const xvatsim::core::authority::AuthorityPolygon& polygon) {
    if (xvatsim::core::authority::AuthorityPolygonContainsPoint(
            polygon,
            point)) {
        return 0.0;
    }

    double bestDistanceNm = std::numeric_limits<double>::max();
    const auto visitRing = [&](
        const xvatsim::core::authority::AuthorityPolygonRing& ring) {
        if (ring.points.size() < 2) {
            return;
        }

        for (std::size_t index = 0; index < ring.points.size(); ++index) {
            const auto& start = ring.points[index];
            const auto& end = ring.points[(index + 1) % ring.points.size()];
            bestDistanceNm = std::min(
                bestDistanceNm,
                DistanceFromPointToAuthoritySegmentNm(point, start, end));
        }
    };
    for (const auto& part : polygon.parts) {
        visitRing(part.exterior);
        for (const auto& hole : part.holes) {
            visitRing(hole);
        }
    }

    return bestDistanceNm;
}

const xvatsim::core::authority::AuthorityPolygon* FindAuthorityPolygonById(
    const xvatsim::core::authority::AuthorityPolygonCatalog& catalog,
    const std::string& polygonId) {
    for (const auto& polygon : catalog.polygons) {
        if (polygon.id == polygonId) {
            return &polygon;
        }
    }
    return nullptr;
}

bool AuthorityPolygonMatchesAuthorityKey(
    const xvatsim::core::authority::AuthorityPolygon& polygon,
    const std::string& rawAuthorityKey) {
    const auto authorityKey =
        xvatsim::core::authority::NormalizeAuthorityToken(rawAuthorityKey);
    if (authorityKey.empty()) {
        return false;
    }
    if (xvatsim::core::authority::NormalizeAuthorityToken(polygon.polygonKey) ==
        authorityKey) {
        return true;
    }
    if (xvatsim::core::authority::NormalizeAuthorityToken(polygon.id) ==
        authorityKey) {
        return true;
    }
    for (const auto& lookupKey : polygon.lookupKeys) {
        if (xvatsim::core::authority::NormalizeAuthorityToken(lookupKey) ==
            authorityKey) {
            return true;
        }
    }
    return false;
}

std::vector<const xvatsim::core::authority::AuthorityPolygon*>
FindAuthorityPolygonsByAuthorityKey(
    const xvatsim::core::authority::AuthorityPolygonCatalog& catalog,
    const std::string& rawAuthorityKey) {
    std::vector<const xvatsim::core::authority::AuthorityPolygon*> polygons;
    for (const auto& polygon : catalog.polygons) {
        if (AuthorityPolygonMatchesAuthorityKey(polygon, rawAuthorityKey)) {
            polygons.push_back(&polygon);
        }
    }
    return polygons;
}

void AddAuthorityPolygonIndexEntry(
    std::unordered_map<std::string, std::vector<std::size_t>>* index,
    std::string key,
    std::size_t polygonIndex) {
    if (index == nullptr) {
        return;
    }
    key = xvatsim::core::authority::NormalizeAuthorityToken(std::move(key));
    if (key.empty()) {
        return;
    }
    auto& entries = (*index)[key];
    if (std::find(entries.begin(), entries.end(), polygonIndex) == entries.end()) {
        entries.push_back(polygonIndex);
    }
}

std::unordered_map<std::string, std::vector<std::size_t>>
BuildAuthorityPolygonExactIndex(
    const xvatsim::core::authority::AuthorityPolygonCatalog& catalog) {
    std::unordered_map<std::string, std::vector<std::size_t>> index;
    for (std::size_t i = 0; i < catalog.polygons.size(); ++i) {
        const auto& polygon = catalog.polygons[i];
        AddAuthorityPolygonIndexEntry(&index, polygon.polygonKey, i);
        AddAuthorityPolygonIndexEntry(&index, polygon.id, i);
        for (const auto& lookupKey : polygon.lookupKeys) {
            AddAuthorityPolygonIndexEntry(&index, lookupKey, i);
        }
    }
    return index;
}

std::vector<const xvatsim::core::authority::AuthorityPolygon*>
FindAuthorityPolygonsByAuthorityKeyIndexed(
    const xvatsim::core::authority::AuthorityPolygonCatalog& catalog,
    const std::unordered_map<std::string, std::vector<std::size_t>>& index,
    const std::string& rawAuthorityKey) {
    const auto authorityKey =
        xvatsim::core::authority::NormalizeAuthorityToken(rawAuthorityKey);
    if (authorityKey.empty()) {
        return {};
    }
    const auto it = index.find(authorityKey);
    if (it == index.end()) {
        return {};
    }

    std::vector<const xvatsim::core::authority::AuthorityPolygon*> polygons;
    polygons.reserve(it->second.size());
    std::unordered_set<std::string> seenPolygonIds;
    for (const auto polygonIndex : it->second) {
        if (polygonIndex >= catalog.polygons.size()) {
            continue;
        }
        const auto& polygon = catalog.polygons[polygonIndex];
        if (seenPolygonIds.insert(polygon.id).second) {
            polygons.push_back(&polygon);
        }
    }
    return polygons;
}

xvatsim::core::authority::ControllerAuthorityCatalog
FilterControllerAuthorityCatalogToAvailablePolygons(
    const xvatsim::core::authority::ControllerAuthorityCatalog& controllerCatalog,
    const xvatsim::core::authority::AuthorityPolygonCatalog& polygonCatalog) {
    xvatsim::core::authority::ControllerAuthorityCatalog filtered;
    for (const auto& authority : controllerCatalog.authorities) {
        if (!FindAuthorityPolygonsByAuthorityKey(
                 polygonCatalog,
                 authority.polygonKey).empty()) {
            filtered.authorities.push_back(authority);
        }
    }
    for (const auto& gap : controllerCatalog.dataGaps) {
        if (!FindAuthorityPolygonsByAuthorityKey(
                 polygonCatalog,
                 gap.polygonKey).empty()) {
            filtered.dataGaps.push_back(gap);
        }
    }
    return filtered;
}

std::string AuthorityPolygonPrimaryKey(
    const xvatsim::core::authority::AuthorityPolygon& polygon) {
    const auto polygonKey =
        xvatsim::core::authority::NormalizeAuthorityToken(polygon.polygonKey);
    if (!polygonKey.empty()) {
        return polygonKey;
    }
    return xvatsim::core::authority::NormalizeAuthorityToken(polygon.id);
}

std::string AuthorityFamilyKey(std::string key) {
    key = xvatsim::core::authority::NormalizeAuthorityToken(std::move(key));
    const auto separator = key.find('-');
    if (separator == std::string::npos || separator == 0) {
        return key;
    }
    return key.substr(0, separator);
}

std::unordered_map<std::string, std::vector<std::size_t>>
BuildAuthorityPolygonFamilyIndex(
    const xvatsim::core::authority::AuthorityPolygonCatalog& catalog) {
    std::unordered_map<std::string, std::vector<std::size_t>> index;
    for (std::size_t i = 0; i < catalog.polygons.size(); ++i) {
        const auto& polygon = catalog.polygons[i];
        const auto primaryKey = AuthorityPolygonPrimaryKey(polygon);
        AddAuthorityPolygonIndexEntry(&index, primaryKey, i);
        AddAuthorityPolygonIndexEntry(&index, AuthorityFamilyKey(primaryKey), i);
    }
    return index;
}

std::optional<xvatsim::core::authority::GeoPoint>
AuthorityPolygonRepresentativePoint(
    const xvatsim::core::authority::AuthorityPolygon& polygon) {
    const xvatsim::core::authority::AuthorityPolygonRing* bestRing = nullptr;
    for (const auto& part : polygon.parts) {
        const auto& ring = part.exterior;
        if (ring.points.empty()) {
            continue;
        }
        if (bestRing == nullptr || ring.points.size() > bestRing->points.size()) {
            bestRing = &ring;
        }
    }
    if (bestRing == nullptr || bestRing->points.empty()) {
        return std::nullopt;
    }

    const auto referenceLongitudeDeg = bestRing->points.front().longitudeDeg;
    double latitudeSum = 0.0;
    double longitudeSum = 0.0;
    for (const auto& point : bestRing->points) {
        latitudeSum += point.latitudeDeg;
        longitudeSum +=
            UnwrapLongitudeRelativeDeg(referenceLongitudeDeg, point.longitudeDeg);
    }

    const auto pointCount = static_cast<double>(bestRing->points.size());
    return xvatsim::core::authority::GeoPoint{
        latitudeSum / pointCount,
        NormalizeLongitudeDeg(longitudeSum / pointCount),
    };
}

bool IsCenterRouteAuthorityKind(
    xvatsim::core::authority::AuthorityKind kind) {
    return kind == xvatsim::core::authority::AuthorityKind::Center ||
           kind == xvatsim::core::authority::AuthorityKind::Extension;
}
int AuthoritySourceConfidencePriority(
    xvatsim::core::authority::AuthoritySource source) {
    switch (source) {
        case xvatsim::core::authority::AuthoritySource::VatGlasses:
            return 0;
        case xvatsim::core::authority::AuthoritySource::AirportLocal:
            return 0;
        case xvatsim::core::authority::AuthoritySource::SpecialSectorData:
            return 1;
        case xvatsim::core::authority::AuthoritySource::VatsimRadarExtension:
            return 2;
        case xvatsim::core::authority::AuthoritySource::VatSpyBoundary:
            return 3;
        case xvatsim::core::authority::AuthoritySource::VatSpyFir:
            return 4;
        case xvatsim::core::authority::AuthoritySource::VatSpyUir:
            return 5;
        case xvatsim::core::authority::AuthoritySource::SimAwareTracon:
            return 6;
    }
    return 100;
}

std::string FormatAuthorityDistanceNm(double distanceNm) {
    if (!std::isfinite(distanceNm)) {
        return "unknown";
    }
    return std::to_string(static_cast<int>(std::round(std::max(0.0, distanceNm)))) + "nm";
}

std::vector<RouteScopedAuthorityPolygon> BuildRouteScopedAuthorityPolygons(
    const xvatsim::core::authority::AuthorityPolygonCatalog& authorityPolygonCatalog,
    bool hasAircraftPosition,
    const xvatsim::core::authority::GeoPoint& aircraftPosition,
    const std::vector<xvatsim::core::authority::GeoPoint>& routePoints) {
    std::vector<xvatsim::core::authority::ActiveAuthorityPolygon> routeScopeCandidates;
    routeScopeCandidates.reserve(authorityPolygonCatalog.polygons.size());
    for (const auto& polygon : authorityPolygonCatalog.polygons) {
        if (!IsCenterRouteAuthorityKind(polygon.kind)) {
            continue;
        }

        routeScopeCandidates.push_back({
            "ROUTE_SCOPE",
            "ROUTE_SCOPE",
            polygon.id,
            polygon.polygonKey,
            "ROUTE_SCOPE",
            polygon.source,
            polygon.kind,
        });
    }

    const auto relevantPolygons =
        xvatsim::core::authority::ResolveRelevantAuthorityPolygons(
            routeScopeCandidates,
            authorityPolygonCatalog,
            hasAircraftPosition,
            aircraftPosition,
            routePoints);

    std::vector<RouteScopedAuthorityPolygon> scopedPolygons;
    scopedPolygons.reserve(relevantPolygons.size());
    for (const auto& relevantPolygon : relevantPolygons) {
        const auto* polygon = FindAuthorityPolygonById(
            authorityPolygonCatalog,
            relevantPolygon.activePolygon.polygonId);
        if (polygon == nullptr ||
            !IsCenterRouteAuthorityKind(polygon->kind)) {
            continue;
        }

        scopedPolygons.push_back({
            polygon,
            relevantPolygon.aircraftInside,
            relevantPolygon.routeIntersects,
            relevantPolygon.routeEntryDistanceNm,
        });
    }

    std::sort(
        scopedPolygons.begin(),
        scopedPolygons.end(),
        [](const auto& left, const auto& right) {
            if (left.routeIntersects != right.routeIntersects) {
                return left.routeIntersects && !right.routeIntersects;
            }
            if (left.routeEntryDistanceNm != right.routeEntryDistanceNm) {
                return left.routeEntryDistanceNm < right.routeEntryDistanceNm;
            }
            const auto leftPriority =
                AuthoritySourceConfidencePriority(left.polygon->source);
            const auto rightPriority =
                AuthoritySourceConfidencePriority(right.polygon->source);
            if (leftPriority != rightPriority) {
                return leftPriority < rightPriority;
            }
            return left.polygon->id < right.polygon->id;
        });
    return scopedPolygons;
}

xvatsim::core::authority::AuthorityPolygonCatalog
FilterAuthorityPolygonCatalogToRouteGeometry(
    const xvatsim::core::authority::AuthorityPolygonCatalog& authorityPolygonCatalog,
    bool hasAircraftPosition,
    const xvatsim::core::authority::GeoPoint& aircraftPosition,
    const std::vector<xvatsim::core::authority::GeoPoint>& routePoints) {
    if (authorityPolygonCatalog.polygons.empty()) {
        return authorityPolygonCatalog;
    }

    std::vector<xvatsim::core::authority::ActiveAuthorityPolygon> routeScopeCandidates;
    routeScopeCandidates.reserve(authorityPolygonCatalog.polygons.size());
    for (const auto& polygon : authorityPolygonCatalog.polygons) {
        routeScopeCandidates.push_back({
            "ROUTE_SCOPE",
            "ROUTE_SCOPE",
            polygon.id,
            polygon.polygonKey,
            "ROUTE_SCOPE",
            polygon.source,
            polygon.kind,
        });
    }

    const auto relevantPolygons =
        xvatsim::core::authority::ResolveRelevantAuthorityPolygons(
            routeScopeCandidates,
            authorityPolygonCatalog,
            hasAircraftPosition,
            aircraftPosition,
            routePoints);

    std::unordered_set<std::string> relevantPolygonIds;
    std::unordered_set<std::string> relevantPolygonKeys;
    for (const auto& relevantPolygon : relevantPolygons) {
        const auto polygonId = xvatsim::core::authority::NormalizeAuthorityToken(
            relevantPolygon.activePolygon.polygonId);
        const auto polygonKey = xvatsim::core::authority::NormalizeAuthorityToken(
            relevantPolygon.activePolygon.polygonKey);
        if (!polygonId.empty()) {
            relevantPolygonIds.insert(polygonId);
        }
        if (!polygonKey.empty()) {
            relevantPolygonKeys.insert(polygonKey);
        }
    }

    xvatsim::core::authority::AuthorityPolygonCatalog filtered;
    for (const auto& polygon : authorityPolygonCatalog.polygons) {
        const auto polygonId =
            xvatsim::core::authority::NormalizeAuthorityToken(polygon.id);
        if (!polygonId.empty() &&
            relevantPolygonIds.find(polygonId) != relevantPolygonIds.end()) {
            filtered.polygons.push_back(polygon);
        }
    }
    for (const auto& gap : authorityPolygonCatalog.dataGaps) {
        const auto polygonKey =
            xvatsim::core::authority::NormalizeAuthorityToken(gap.polygonKey);
        if (!polygonKey.empty() &&
            relevantPolygonKeys.find(polygonKey) != relevantPolygonKeys.end()) {
            filtered.dataGaps.push_back(gap);
        }
    }
    return filtered;
}

std::size_t BuildAuthorityScopeCacheSignature(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const AuthorityRelevanceWorkScope& workScope,
    std::uint64_t terminalBoundaryGeneration) {
    std::size_t hash = 1469598103934665603ull;
    // Source payload changes are already represented by the route/terminal
    // generation counters, so avoid re-hashing large payload byte arrays in
    // the live authority cadence path.
    (void)controllerFeedSnapshot;
    HashCombine(&hash, BuildAuthorityStructuralScopeRouteSignature(workScope.routeSectorSnapshot));
    // Scope artifacts are route/source structures. Aircraft movement alone
    // should not rebuild them; the live relevance pass handles current
    // position when the route window or authority evidence actually changes.
    HashCombine(&hash, static_cast<std::size_t>(aircraftState.valid ? 1 : 0));
    HashCombine(&hash, static_cast<std::size_t>(aircraftState.onGround ? 1 : 0));
    HashCombine(&hash, static_cast<std::size_t>(terminalBoundaryGeneration));
    HashCombine(&hash, static_cast<std::size_t>(workScope.includeDepartureEndpoint ? 1 : 0));
    HashCombine(&hash, static_cast<std::size_t>(workScope.includeDestinationEndpoint ? 1 : 0));
    HashCombine(&hash, static_cast<std::size_t>(workScope.deferredSectorCount));
    HashCombineDouble(&hash, workScope.windowNm);
    HashCombineString(&hash, workScope.stage);
    return hash;
}

const AuthorityRelevanceScopeArtifacts& ResolveAuthorityRelevanceScopeArtifacts(
    AuthorityRelevanceScopeArtifacts* cachedArtifacts,
    const xvatsim::core::authority::ControllerAuthorityCatalog&
        sourceControllerAuthorityCatalog,
    const xvatsim::core::authority::AuthorityPolygonCatalog&
        sourceAuthorityPolygonCatalog,
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const AuthorityRelevanceWorkScope& workScope,
    std::uint64_t terminalBoundaryGeneration) {
    if (cachedArtifacts == nullptr) {
        static const AuthorityRelevanceScopeArtifacts emptyArtifacts;
        return emptyArtifacts;
    }

    const auto scopeSignature =
        BuildAuthorityScopeCacheSignature(
            aircraftState,
            controllerFeedSnapshot,
            workScope,
            terminalBoundaryGeneration);

    if (cachedArtifacts->valid &&
        cachedArtifacts->signature == scopeSignature) {
        return *cachedArtifacts;
    }

    AuthorityRelevanceScopeArtifacts rebuilt;
    rebuilt.signature = scopeSignature;
    rebuilt.routeAuthorityPolygonKeys =
        BuildRouteAuthorityPolygonKeys(workScope.routeSectorSnapshot);

    // Query scope is an index over the immutable canonical publication. It may
    // never manufacture endpoint geometry or controller ownership.
    rebuilt.controllerAuthorityCatalog = sourceControllerAuthorityCatalog;
    rebuilt.authorityPolygonCatalog = sourceAuthorityPolygonCatalog;
    rebuilt.authorityPolygonExactIndexesByKey =
        BuildAuthorityPolygonExactIndex(rebuilt.authorityPolygonCatalog);
    rebuilt.authorityPolygonFamilyIndexesByKey =
        BuildAuthorityPolygonFamilyIndex(rebuilt.authorityPolygonCatalog);

    const auto routePoints = workScope.routePoints;
    const xvatsim::core::authority::GeoPoint aircraftPosition{
        aircraftState.latitudeDeg,
        aircraftState.longitudeDeg,
    };
    const auto routeAuthorityScopeKeys =
        BuildRouteAuthorityScopeKeys(
            rebuilt.authorityPolygonCatalog,
            rebuilt.routeAuthorityPolygonKeys,
            workScope.routeSectorSnapshot,
            workScope.includeDepartureEndpoint,
            workScope.includeDestinationEndpoint,
            aircraftState.valid,
            aircraftPosition,
            routePoints);
    const auto routeBroadAuthorityPolygonCatalog =
        FilterAuthorityPolygonCatalogToRouteKeys(
            rebuilt.authorityPolygonCatalog,
            routeAuthorityScopeKeys);
    rebuilt.routeScopedAuthorityPolygonCatalog =
        FilterAuthorityPolygonCatalogToRouteGeometry(
            routeBroadAuthorityPolygonCatalog,
            aircraftState.valid,
            aircraftPosition,
            routePoints);
    rebuilt.routeAuthorityMatchKeys =
        BuildRouteAuthorityMatchKeys(
            rebuilt.routeScopedAuthorityPolygonCatalog,
            rebuilt.routeAuthorityPolygonKeys);
    rebuilt.routeScopedControllerAuthorityCatalog =
        FilterControllerAuthorityCatalogToScopeKeys(
            rebuilt.controllerAuthorityCatalog,
            rebuilt.routeAuthorityMatchKeys);
    rebuilt.routeScopedControllerAuthorityCatalog =
        FilterControllerAuthorityCatalogToAvailablePolygons(
            rebuilt.routeScopedControllerAuthorityCatalog,
            rebuilt.routeScopedAuthorityPolygonCatalog);
    rebuilt.routeScopedAuthorityPolygons =
        BuildRouteScopedAuthorityPolygons(
            rebuilt.routeScopedAuthorityPolygonCatalog,
            aircraftState.valid,
            aircraftPosition,
            routePoints);
    rebuilt.valid = true;

    *cachedArtifacts = std::move(rebuilt);
    return *cachedArtifacts;
}

std::string AuthorityProofSourceLabel(
    const xvatsim::core::authority::ActiveAuthorityPolygon& activePolygon) {
    if (!activePolygon.proofSource.empty()) {
        return activePolygon.proofSource;
    }

    if (activePolygon.matchedPattern.find('*') != std::string::npos) {
        return "CATALOG_PATTERN";
    }

    return "CATALOG_EXACT";
}

std::string AuthorityProofDetail(
    const xvatsim::core::authority::ActiveAuthorityPolygon& activePolygon) {
    std::ostringstream stream;
    if (!activePolygon.proofDetail.empty()) {
        stream << activePolygon.proofDetail << ";";
    }
    stream << "pattern=" << activePolygon.matchedPattern
           << ";polygonSource="
           << xvatsim::core::authority::AuthoritySourceLabel(
                  activePolygon.polygonSource)
           << ";polygonId=" << activePolygon.polygonId;
    return stream.str();
}

std::string AuthorityRelevanceProofDetail(
    const xvatsim::core::authority::RelevantAuthorityPolygon& relevantAuthorityPolygon) {
    std::ostringstream stream;
    stream << AuthorityProofDetail(relevantAuthorityPolygon.activePolygon)
           << ";routeRelevance=";
    if (relevantAuthorityPolygon.aircraftInside &&
        relevantAuthorityPolygon.routeIntersects) {
        stream << "aircraft-inside+route-intersects";
    } else if (relevantAuthorityPolygon.aircraftInside) {
        stream << "aircraft-inside";
    } else if (relevantAuthorityPolygon.routeIntersects) {
        stream << "route-intersects";
    } else {
        stream << "not-relevant";
    }
    stream << ";routeEntry="
           << FormatAuthorityDistanceNm(
                  relevantAuthorityPolygon.routeEntryDistanceNm);
    return stream.str();
}

struct CanonicalLiveAuthorityMapBuild {
    brain::LiveAuthorityMapSnapshot map;
    std::vector<xvatsim::core::authority::ActiveAuthorityPolygon>
        activePolygons;
    // Unfiltered mechanical candidates retained for incremental reuse. The
    // Brain may later classify a polygon conflict; that decision must not
    // destroy the reusable controller-to-polygon fact.
    std::vector<xvatsim::core::authority::ActiveAuthorityPolygon>
        candidateActivePolygons;
    std::vector<std::string> diagnostics;
    int candidateControllers = 0;
    int activationDataGaps = 0;
    int verifiedControllerFacts = 0;
    int reusedControllerFacts = 0;
    int removedControllerFacts = 0;
    int verifiedDynamicOwnershipFacts = 0;
    int reusedDynamicOwnershipFacts = 0;
};

struct CanonicalControllerActivationCacheEntry {
    std::uint64_t semanticSignature = 0;
    std::vector<xvatsim::core::authority::AuthorityDecision> decisions;
    xvatsim::core::authority::AuthorityActivationResult activation;
};

struct CanonicalDynamicOwnershipActivationCacheEntry {
    std::uint64_t semanticSignature = 0;
    xvatsim::core::authority::AuthorityActivationResult activation;
    bool usedFacilityAirportGroupFallback = false;
};

struct CanonicalLiveAuthorityIncrementalCache {
    std::uint64_t datasetIdentity = 0;
    std::unordered_map<std::string, CanonicalControllerActivationCacheEntry>
        controllers;
    std::unordered_map<
        std::string,
        CanonicalDynamicOwnershipActivationCacheEntry>
        dynamicOwnership;
};

std::uint64_t CanonicalControllerSemanticSignature(
    const brain::ControllerSnapshot& controller) {
    std::size_t hash = 1469598103934665603ull;
    HashCombineString(
        &hash,
        xvatsim::core::authority::NormalizeControllerCallsign(
            controller.callsign));
    HashCombineString(&hash, NormalizeFrequency(controller.frequency));
    HashCombine(&hash, static_cast<std::size_t>(controller.facility));
    HashCombine(&hash, static_cast<std::size_t>(controller.actionable ? 1 : 0));
    return static_cast<std::uint64_t>(hash);
}

std::uint64_t CanonicalDynamicOwnershipSemanticSignature(
    const brain::VnasLiveOwnershipFactSnapshot& fact,
    const brain::ControllerSnapshot& controller) {
    std::size_t hash = CanonicalControllerSemanticSignature(controller);
    HashCombineString(&hash, fact.controllerCallsign);
    HashCombineString(&hash, NormalizeFrequency(fact.controllerFrequency));
    HashCombineString(&hash, fact.artccId);
    HashCombineString(&hash, fact.facilityId);
    HashCombineString(&hash, fact.livePositionId);
    HashCombineString(&hash, fact.livePositionCallsign);
    HashCombineString(&hash, fact.sourcePositionId);
    HashCombineString(&hash, fact.sourcePositionCallsign);
    HashCombineString(&hash, NormalizeFrequency(fact.sourcePositionFrequency));
    HashCombineString(&hash, fact.tcpToken);
    for (const auto& airport : fact.ownedAirportCodes) {
        HashCombineString(&hash, airport);
    }
    for (const auto& airport : fact.departureAirportCodes) {
        HashCombineString(&hash, airport);
    }
    HashCombine(&hash, static_cast<std::size_t>(fact.ownershipKind));
    HashCombineString(&hash, fact.proof);
    return static_cast<std::uint64_t>(hash);
}

std::string CanonicalDynamicOwnershipCacheKey(
    const brain::VnasLiveOwnershipFactSnapshot& fact) {
    return xvatsim::core::authority::NormalizeControllerCallsign(
               fact.controllerCallsign) +
           "|" + fact.sourcePositionId + "|" + fact.livePositionId + "|" +
           fact.tcpToken;
}

bool TerminalCallsignMatchesPrefixes(
    const xvatsim::core::authority::AuthorityPolygon& polygon,
    const std::vector<std::string>& prefixes,
    const std::string& callsign) {
    if (polygon.kind !=
        xvatsim::core::authority::AuthorityKind::Terminal) {
        return false;
    }
    for (const auto& rawPrefix : prefixes) {
        const auto prefix =
            xvatsim::core::authority::NormalizeAuthorityToken(rawPrefix);
        if (prefix.empty()) {
            continue;
        }
        for (const auto role : polygon.serviceRoles) {
            const auto suffix =
                role ==
                        xvatsim::core::authority::AuthorityServiceRole::Departure
                    ? "DEP"
                    : "APP";
            if (xvatsim::core::authority::CallsignMatchesPattern(
                    prefix + "_" + suffix, callsign) ||
                xvatsim::core::authority::CallsignMatchesPattern(
                    prefix + "_*_" + suffix, callsign)) {
                return true;
            }
        }
    }
    return false;
}

bool IsExplicitTerminalPolygonPosition(
    const xvatsim::core::authority::AuthorityPolygon& polygon,
    const std::string& callsign) {
    return TerminalCallsignMatchesPrefixes(
        polygon, polygon.controllerPrefixes, callsign);
}

bool IsTerminalSourceFeatureGroupPosition(
    const xvatsim::core::authority::AuthorityPolygon& polygon,
    const std::string& callsign) {
    if (polygon.source !=
            xvatsim::core::authority::AuthoritySource::SimAwareTracon ||
        polygon.sourceFeatureId.empty()) {
        return false;
    }
    return TerminalCallsignMatchesPrefixes(
        polygon, {polygon.sourceFeatureId}, callsign);
}

std::uint64_t HashCanonicalLiveAuthorityActivation(
    const brain::LiveAuthorityMapSnapshot& map) {
    std::size_t hash = 1469598103934665603ull;
    HashCombine(&hash, static_cast<std::size_t>(map.available ? 1 : 0));
    HashCombine(&hash, static_cast<std::size_t>(map.stale ? 1 : 0));
    HashCombine(
        &hash,
        static_cast<std::size_t>(map.brainDecisionsApplied ? 1 : 0));
    HashCombine(&hash, static_cast<std::size_t>(map.canonicalMapIdentity));
    HashCombine(&hash, static_cast<std::size_t>(map.polygonDefinitionCount));
    HashCombine(&hash, static_cast<std::size_t>(map.polygonPartCount));
    HashCombine(&hash, static_cast<std::size_t>(map.dataGapCount));
    for (const auto& assignment : map.assignments) {
        HashCombine(&hash, static_cast<std::size_t>(assignment.state));
        HashCombine(
            &hash,
            static_cast<std::size_t>(assignment.candidateState));
        HashCombine(
            &hash,
            static_cast<std::size_t>(
                assignment.explicitTerminalPosition ? 1 : 0));
        HashCombineString(&hash, assignment.canonicalPolygonId);
        HashCombineString(&hash, assignment.polygonKey);
        HashCombineString(&hash, assignment.positionId);
        HashCombineString(&hash, assignment.callsign);
        HashCombineString(&hash, assignment.frequency);
        HashCombine(&hash, static_cast<std::size_t>(assignment.facility));
        HashCombineString(&hash, assignment.authoritySource);
        HashCombine(&hash, static_cast<std::size_t>(assignment.kind));
        HashCombineString(&hash, assignment.matchedPattern);
        HashCombineString(&hash, assignment.ownershipProof);
        for (const auto& airport : assignment.departureAirportCodes) {
            HashCombineString(&hash, airport);
        }
    }
    for (const auto& polygon : map.polygonStates) {
        HashCombine(&hash, static_cast<std::size_t>(polygon.state));
        HashCombineString(&hash, polygon.canonicalPolygonId);
        HashCombineString(&hash, polygon.polygonKey);
        HashCombineString(&hash, polygon.authoritySource);
        HashCombine(&hash, static_cast<std::size_t>(polygon.kind));
        for (const auto& callsign : polygon.controllerCallsigns) {
            HashCombineString(&hash, callsign);
        }
    }
    for (const auto& polygonId : map.conflictedPolygonIds) {
        HashCombineString(&hash, polygonId);
    }
    for (const auto& callsign : map.unmappedControllerCallsigns) {
        HashCombineString(&hash, callsign);
    }
    return static_cast<std::uint64_t>(hash);
}

CanonicalLiveAuthorityMapBuild BuildRadioScopedLiveAuthorityMap(
    const CanonicalAuthorityMapDataset& dataset,
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const brain::VnasLiveOwnershipSnapshot* dynamicOwnership,
    CanonicalLiveAuthorityIncrementalCache* incrementalCache) {
    CanonicalLiveAuthorityMapBuild output;
    if (incrementalCache != nullptr &&
        incrementalCache->datasetIdentity != dataset.identity) {
        *incrementalCache = {};
        incrementalCache->datasetIdentity = dataset.identity;
    }
    output.map.available =
        controllerFeedSnapshot.available && !controllerFeedSnapshot.stale;
    output.map.stale =
        !controllerFeedSnapshot.available || controllerFeedSnapshot.stale;
    output.map.canonicalMapIdentity = dataset.identity;
    output.map.controllerFeedGeneration = controllerFeedSnapshot.generation;
    output.map.centerBoundaryGeneration = dataset.centerBoundaryGeneration;
    output.map.authorityCatalogGeneration =
        dataset.authorityCatalogGeneration;
    output.map.terminalBoundaryGeneration =
        dataset.terminalBoundaryGeneration;
    output.map.dynamicOwnershipGeneration =
        dynamicOwnership != nullptr ? dynamicOwnership->generation : 0;
    output.map.polygonDefinitionCount = dataset.receipt.polygonDefinitions;
    output.map.polygonPartCount = dataset.receipt.polygonParts;
    output.map.dataGapCount = dataset.receipt.dataGaps;

    struct CandidateAssignment {
        xvatsim::core::authority::ActiveAuthorityPolygon active;
        brain::LiveAuthorityAssignmentSnapshot assignment;
        int priority = 0;
    };

    std::unordered_set<std::string> countedControllers;
    std::unordered_set<std::string> mappedControllers;
    std::unordered_set<std::string> decisionDiagnosedControllers;
    std::unordered_map<std::string, const brain::ControllerSnapshot*>
        controllersByCallsign;
    std::unordered_map<std::string, CandidateAssignment> candidates;
    std::unordered_set<std::string> currentControllerCacheKeys;
    auto addCandidate = [&](CandidateAssignment candidate) {
        const auto controllerKey =
            xvatsim::core::authority::NormalizeControllerCallsign(
                candidate.assignment.callsign);
        const auto key = controllerKey + "|" +
                         candidate.assignment.canonicalPolygonId;
        const auto found = candidates.find(key);
        if (found != candidates.end()) {
            if (found->second.priority > candidate.priority) {
                return;
            }
            if (found->second.priority == candidate.priority &&
                found->second.assignment.positionId <=
                    candidate.assignment.positionId) {
                return;
            }
        }
        candidates[key] = std::move(candidate);
        mappedControllers.insert(controllerKey);
    };

    for (const auto& controller : controllerFeedSnapshot.Controllers()) {
        if (!IsAuthorityControllerCandidate(controller)) {
            continue;
        }
        const auto normalizedCallsign =
            xvatsim::core::authority::NormalizeControllerCallsign(
                controller.callsign);
        if (countedControllers.insert(normalizedCallsign).second) {
            ++output.candidateControllers;
            controllersByCallsign[normalizedCallsign] = &controller;
        }
        currentControllerCacheKeys.insert(normalizedCallsign);
        const auto semanticSignature =
            CanonicalControllerSemanticSignature(controller);
        const std::vector<xvatsim::core::authority::AuthorityDecision>*
            authorityDecisions = nullptr;
        const xvatsim::core::authority::AuthorityActivationResult* activation =
            nullptr;
        if (incrementalCache != nullptr) {
            const auto cached =
                incrementalCache->controllers.find(normalizedCallsign);
            if (cached != incrementalCache->controllers.end() &&
                cached->second.semanticSignature == semanticSignature) {
                authorityDecisions = &cached->second.decisions;
                activation = &cached->second.activation;
                ++output.reusedControllerFacts;
            } else {
                CanonicalControllerActivationCacheEntry entry;
                entry.semanticSignature = semanticSignature;
                entry.decisions =
                    xvatsim::core::authority::EvaluateControllerAuthority(
                        dataset.controllerAuthorityCatalog,
                        controller.callsign,
                        controller.frequency,
                        controller.facility);
                entry.activation =
                    xvatsim::core::authority::ActivateAuthorityPolygons(
                        dataset.controllerAuthorityCatalog,
                        dataset.authorityPolygonCatalog,
                        controller.callsign,
                        controller.frequency,
                        controller.facility);
                auto inserted = incrementalCache->controllers.insert_or_assign(
                    normalizedCallsign, std::move(entry));
                authorityDecisions = &inserted.first->second.decisions;
                activation = &inserted.first->second.activation;
                ++output.verifiedControllerFacts;
            }
        } else {
            static const std::vector<xvatsim::core::authority::AuthorityDecision>
                kEmptyDecisions;
            static const xvatsim::core::authority::AuthorityActivationResult
                kEmptyActivation;
            (void)kEmptyDecisions;
            (void)kEmptyActivation;
        }
        std::vector<xvatsim::core::authority::AuthorityDecision>
            uncachedAuthorityDecisions;
        xvatsim::core::authority::AuthorityActivationResult uncachedActivation;
        if (authorityDecisions == nullptr || activation == nullptr) {
            uncachedAuthorityDecisions =
                xvatsim::core::authority::EvaluateControllerAuthority(
                    dataset.controllerAuthorityCatalog,
                    controller.callsign,
                    controller.frequency,
                    controller.facility);
            uncachedActivation =
                xvatsim::core::authority::ActivateAuthorityPolygons(
                    dataset.controllerAuthorityCatalog,
                    dataset.authorityPolygonCatalog,
                    controller.callsign,
                    controller.frequency,
                    controller.facility);
            authorityDecisions = &uncachedAuthorityDecisions;
            activation = &uncachedActivation;
            ++output.verifiedControllerFacts;
        }
        output.activationDataGaps +=
            static_cast<int>(activation->dataGaps.size());
        for (const auto& gap : activation->dataGaps) {
            AppendAuthorityDiagnostic(
                &output.diagnostics,
                normalizedCallsign + ":authority-gap:" + gap.authorityId +
                    ":" + gap.polygonKey + ":" + gap.reason);
        }
        if (activation->activePolygons.empty()) {
            if (activation->dataGaps.empty() && !authorityDecisions->empty()) {
                AppendAuthorityDecisionDiagnostics(
                    &output.diagnostics,
                    normalizedCallsign,
                    *authorityDecisions);
                decisionDiagnosedControllers.insert(normalizedCallsign);
            }
        }

        for (auto active : activation->activePolygons) {
            CandidateAssignment candidate;
            auto assignmentState = output.map.stale
                ? brain::LiveAuthorityAssignmentState::Stale
                : brain::LiveAuthorityAssignmentState::ActiveExact;
            const auto polygon = std::find_if(
                dataset.authorityPolygonCatalog.polygons.begin(),
                dataset.authorityPolygonCatalog.polygons.end(),
                [&](const auto& value) { return value.id == active.polygonId; });
            if (!output.map.stale &&
                polygon != dataset.authorityPolygonCatalog.polygons.end() &&
                IsTerminalSourceFeatureGroupPosition(
                    *polygon, controller.callsign) &&
                !IsExplicitTerminalPolygonPosition(
                    *polygon, controller.callsign)) {
                assignmentState =
                    brain::LiveAuthorityAssignmentState::ActiveParent;
                if (!active.proofDetail.empty()) {
                    active.proofDetail += ";";
                }
                active.proofDetail += "join=source-feature-group";
            }
            candidate.active = active;
            candidate.assignment.state = assignmentState;
            candidate.assignment.candidateState = assignmentState;
            candidate.assignment.explicitTerminalPosition =
                polygon != dataset.authorityPolygonCatalog.polygons.end() &&
                IsExplicitTerminalPolygonPosition(
                    *polygon, controller.callsign);
            candidate.assignment.canonicalPolygonId = active.polygonId;
            candidate.assignment.polygonKey = active.polygonKey;
            candidate.assignment.positionId = active.authorityId;
            candidate.assignment.callsign = controller.callsign;
            candidate.assignment.frequency = controller.frequency;
            candidate.assignment.facility = controller.facility;
            candidate.assignment.authoritySource =
                AuthorityProofSourceLabel(active);
            candidate.assignment.kind =
                ToBrainAuthorityRelevanceKind(active.kind);
            candidate.assignment.matchedPattern = active.matchedPattern;
            candidate.assignment.ownershipProof = active.proofDetail;
            candidate.priority =
                assignmentState ==
                        brain::LiveAuthorityAssignmentState::ActiveExact
                    ? 4
                    : 3;
            addCandidate(std::move(candidate));
        }
    }

    const auto dynamicOwnershipUsable =
        dynamicOwnership != nullptr && dynamicOwnership->enabled &&
        dynamicOwnership->available && !dynamicOwnership->stale;
    std::unordered_set<std::string> currentDynamicOwnershipCacheKeys;
    if (dynamicOwnershipUsable) {
        for (const auto& fact : dynamicOwnership->facts) {
            const auto controllerKey =
                xvatsim::core::authority::NormalizeControllerCallsign(
                    fact.controllerCallsign);
            const auto controllerIt = controllersByCallsign.find(controllerKey);
            if (controllerIt == controllersByCallsign.end() ||
                controllerIt->second == nullptr) {
                continue;
            }
            const auto& controller = *controllerIt->second;
            const auto rosterFrequency = NormalizeFrequency(controller.frequency);
            const auto vnasFrequency =
                NormalizeFrequency(fact.controllerFrequency);
            if (!rosterFrequency.empty() && !vnasFrequency.empty() &&
                rosterFrequency != vnasFrequency) {
                AppendAuthorityDiagnostic(
                    &output.diagnostics,
                    controllerKey +
                        ":vnas-live-ownership-frequency-mismatch");
                continue;
            }

            const auto dynamicCacheKey =
                CanonicalDynamicOwnershipCacheKey(fact);
            currentDynamicOwnershipCacheKeys.insert(dynamicCacheKey);
            const auto dynamicSemanticSignature =
                CanonicalDynamicOwnershipSemanticSignature(fact, controller);
            xvatsim::core::authority::AuthorityActivationResult activation;
            auto usedFacilityAirportGroupFallback = false;
            auto dynamicCacheHit = false;
            if (incrementalCache != nullptr) {
                const auto cached =
                    incrementalCache->dynamicOwnership.find(dynamicCacheKey);
                if (cached != incrementalCache->dynamicOwnership.end() &&
                    cached->second.semanticSignature ==
                        dynamicSemanticSignature) {
                    activation = cached->second.activation;
                    usedFacilityAirportGroupFallback =
                        cached->second.usedFacilityAirportGroupFallback;
                    dynamicCacheHit = true;
                    ++output.reusedDynamicOwnershipFacts;
                }
            }
            if (!dynamicCacheHit) {
                activation =
                    xvatsim::core::authority::ActivateAuthorityPolygons(
                        dataset.controllerAuthorityCatalog,
                        dataset.authorityPolygonCatalog,
                        fact.sourcePositionCallsign,
                        fact.sourcePositionFrequency,
                        controller.facility);
            }
            if (!dynamicCacheHit && activation.activePolygons.empty() &&
                !fact.facilityId.empty() &&
                !fact.ownedAirportCodes.empty()) {
                const auto sourceCallsign =
                    xvatsim::core::authority::NormalizeControllerCallsign(
                        fact.sourcePositionCallsign);
                const auto separator = sourceCallsign.rfind('_');
                const auto serviceSuffix =
                    separator == std::string::npos
                        ? std::string{}
                        : sourceCallsign.substr(separator + 1);
                const auto serviceMatches = [&](const auto& polygon) {
                    if (serviceSuffix != "APP" && serviceSuffix != "DEP") {
                        return false;
                    }
                    return std::any_of(
                        polygon.serviceRoles.begin(),
                        polygon.serviceRoles.end(),
                        [&](const auto role) {
                            return (serviceSuffix == "APP" &&
                                    role == xvatsim::core::authority::
                                                AuthorityServiceRole::Approach) ||
                                   (serviceSuffix == "DEP" &&
                                    role == xvatsim::core::authority::
                                                AuthorityServiceRole::Departure);
                        });
                };
                const auto airportPrefixMatches = [&](const auto& polygon) {
                    return std::any_of(
                        polygon.controllerPrefixes.begin(),
                        polygon.controllerPrefixes.end(),
                        [&](const auto& rawPrefix) {
                            const auto prefix =
                                xvatsim::core::authority::NormalizeAuthorityToken(
                                    rawPrefix);
                            return std::any_of(
                                fact.ownedAirportCodes.begin(),
                                fact.ownedAirportCodes.end(),
                                [&](const auto& rawAirport) {
                                    const auto airport =
                                        xvatsim::core::authority::
                                            NormalizeAuthorityToken(rawAirport);
                                    return prefix == airport ||
                                           (airport.size() == 4 &&
                                            airport.front() == 'K' &&
                                            prefix == airport.substr(1));
                                });
                        });
                };
                const auto facilityKey =
                    xvatsim::core::authority::NormalizeAuthorityToken(
                        fact.facilityId);
                for (const auto& polygon :
                     dataset.authorityPolygonCatalog.polygons) {
                    if (polygon.source !=
                            xvatsim::core::authority::
                                AuthoritySource::SimAwareTracon ||
                        polygon.kind !=
                            xvatsim::core::authority::AuthorityKind::Terminal ||
                        xvatsim::core::authority::NormalizeAuthorityToken(
                            polygon.sourceFeatureId) != facilityKey ||
                        !serviceMatches(polygon) ||
                        !airportPrefixMatches(polygon)) {
                        continue;
                    }
                    xvatsim::core::authority::ActiveAuthorityPolygon active;
                    active.callsign = fact.sourcePositionCallsign;
                    active.authorityId =
                        "VNAS_GROUP:" + facilityKey + ":" +
                        fact.sourcePositionId;
                    active.polygonId = polygon.id;
                    active.polygonKey = polygon.polygonKey;
                    active.matchedPattern =
                        "VNAS_FACILITY_AIRPORT:" + facilityKey;
                    active.polygonSource = polygon.source;
                    active.kind =
                        xvatsim::core::authority::AuthorityKind::Terminal;
                    active.proofSource = "VNAS_LIVE_OWNERSHIP";
                    active.proofDetail =
                        fact.proof + ";join=facility-airport-group";
                    activation.activePolygons.push_back(std::move(active));
                    usedFacilityAirportGroupFallback = true;
                }
            }
            if (!dynamicCacheHit) {
                ++output.verifiedDynamicOwnershipFacts;
                if (incrementalCache != nullptr) {
                    CanonicalDynamicOwnershipActivationCacheEntry entry;
                    entry.semanticSignature = dynamicSemanticSignature;
                    entry.activation = activation;
                    entry.usedFacilityAirportGroupFallback =
                        usedFacilityAirportGroupFallback;
                    incrementalCache->dynamicOwnership.insert_or_assign(
                        dynamicCacheKey, std::move(entry));
                }
            }
            if (activation.activePolygons.empty()) {
                AppendAuthorityDiagnostic(
                    &output.diagnostics,
                    controllerKey + ":vnas-source-position-unmapped:" +
                        fact.sourcePositionId + ":" +
                        fact.sourcePositionCallsign);
                continue;
            }
            for (auto active : activation.activePolygons) {
                const auto activationProof =
                    fact.proof +
                    (usedFacilityAirportGroupFallback
                         ? ";join=facility-airport-group"
                         : "");
                active.callsign = controller.callsign;
                active.proofSource = "VNAS_LIVE_OWNERSHIP";
                active.proofDetail = activationProof;
                CandidateAssignment candidate;
                candidate.active = active;
                candidate.assignment.state =
                    usedFacilityAirportGroupFallback
                        ? brain::LiveAuthorityAssignmentState::ActiveParent
                        : fact.ownershipKind ==
                            brain::VnasLiveOwnershipKind::ParentAssumption
                        ? brain::LiveAuthorityAssignmentState::ActiveParent
                        : fact.ownershipKind ==
                                  brain::VnasLiveOwnershipKind::DynamicAssumption
                            ? brain::LiveAuthorityAssignmentState::ActiveDynamic
                            : brain::LiveAuthorityAssignmentState::ActiveExact;
                candidate.assignment.candidateState = candidate.assignment.state;
                candidate.assignment.canonicalPolygonId = active.polygonId;
                const auto assignmentPolygon = std::find_if(
                    dataset.authorityPolygonCatalog.polygons.begin(),
                    dataset.authorityPolygonCatalog.polygons.end(),
                    [&](const auto& value) {
                        return value.id == active.polygonId;
                    });
                candidate.assignment.explicitTerminalPosition =
                    assignmentPolygon !=
                        dataset.authorityPolygonCatalog.polygons.end() &&
                    IsExplicitTerminalPolygonPosition(
                        *assignmentPolygon, controller.callsign);
                candidate.assignment.polygonKey = active.polygonKey;
                candidate.assignment.positionId = active.authorityId;
                candidate.assignment.callsign = controller.callsign;
                candidate.assignment.frequency = controller.frequency;
                candidate.assignment.facility = controller.facility;
                candidate.assignment.authoritySource =
                    "VNAS_LIVE_OWNERSHIP";
                candidate.assignment.kind =
                    ToBrainAuthorityRelevanceKind(active.kind);
                candidate.assignment.matchedPattern = active.matchedPattern;
                candidate.assignment.ownershipProof = activationProof;
                candidate.assignment.departureAirportCodes =
                    fact.departureAirportCodes;
                candidate.assignment.dynamicOwnershipGeneration =
                    dynamicOwnership->generation;
                candidate.priority =
                    usedFacilityAirportGroupFallback
                        ? 1
                        : fact.ownershipKind ==
                            brain::VnasLiveOwnershipKind::ExactPosition
                        ? 5
                        : fact.ownershipKind ==
                                  brain::VnasLiveOwnershipKind::DynamicAssumption
                            ? 2
                            : 1;
                addCandidate(std::move(candidate));
            }
        }
    }

    if (incrementalCache != nullptr && controllerFeedSnapshot.available &&
        !controllerFeedSnapshot.stale &&
        controllerFeedSnapshot.controllerComponentComplete) {
        for (auto cached = incrementalCache->controllers.begin();
             cached != incrementalCache->controllers.end();) {
            if (currentControllerCacheKeys.find(cached->first) ==
                currentControllerCacheKeys.end()) {
                cached = incrementalCache->controllers.erase(cached);
                ++output.removedControllerFacts;
            } else {
                ++cached;
            }
        }
    }
    if (incrementalCache != nullptr && dynamicOwnershipUsable &&
        dynamicOwnership->facilityDataComplete) {
        for (auto cached = incrementalCache->dynamicOwnership.begin();
             cached != incrementalCache->dynamicOwnership.end();) {
            if (currentDynamicOwnershipCacheKeys.find(cached->first) ==
                currentDynamicOwnershipCacheKeys.end()) {
                cached = incrementalCache->dynamicOwnership.erase(cached);
            } else {
                ++cached;
            }
        }
    }

    for (const auto& callsign : countedControllers) {
        if (mappedControllers.find(callsign) == mappedControllers.end()) {
            output.map.unmappedControllerCallsigns.push_back(callsign);
            if (decisionDiagnosedControllers.find(callsign) ==
                decisionDiagnosedControllers.end()) {
                AppendAuthorityDiagnostic(
                    &output.diagnostics,
                    callsign + ":unmapped-controller");
            }
        }
    }
    for (auto& [key, candidate] : candidates) {
        (void)key;
        output.map.assignments.push_back(candidate.assignment);
        if (brain::IsActiveLiveAuthorityState(candidate.assignment.state)) {
            output.activePolygons.push_back(std::move(candidate.active));
        }
    }
    output.candidateActivePolygons = output.activePolygons;

    std::unordered_map<std::string, std::vector<std::string>> ownersByPolygon;
    std::unordered_map<std::string, brain::LiveAuthorityAssignmentState>
        stateByPolygon;
    const auto statePriority = [](brain::LiveAuthorityAssignmentState state) {
        switch (state) {
            case brain::LiveAuthorityAssignmentState::Conflict:
                return 5;
            case brain::LiveAuthorityAssignmentState::ActiveExact:
                return 4;
            case brain::LiveAuthorityAssignmentState::ActiveDynamic:
                return 3;
            case brain::LiveAuthorityAssignmentState::ActiveParent:
                return 2;
            case brain::LiveAuthorityAssignmentState::Stale:
                return 1;
            case brain::LiveAuthorityAssignmentState::Unmapped:
            case brain::LiveAuthorityAssignmentState::Offline:
                return 0;
        }
        return 0;
    };
    for (const auto& assignment : output.map.assignments) {
        if (assignment.canonicalPolygonId.empty()) {
            continue;
        }
        ownersByPolygon[assignment.canonicalPolygonId].push_back(
            assignment.callsign);
        const auto found = stateByPolygon.find(assignment.canonicalPolygonId);
        if (found == stateByPolygon.end() ||
            statePriority(assignment.state) > statePriority(found->second)) {
            stateByPolygon[assignment.canonicalPolygonId] = assignment.state;
        }
    }
    output.map.polygonStates.reserve(
        dataset.authorityPolygonCatalog.polygons.size());
    for (const auto& polygon : dataset.authorityPolygonCatalog.polygons) {
        brain::LiveAuthorityPolygonStateSnapshot polygonState;
        polygonState.canonicalPolygonId = polygon.id;
        polygonState.polygonKey = polygon.polygonKey;
        polygonState.authoritySource =
            xvatsim::core::authority::AuthoritySourceLabel(polygon.source);
        polygonState.kind = ToBrainAuthorityRelevanceKind(polygon.kind);
        const auto state = stateByPolygon.find(polygon.id);
        polygonState.state = state != stateByPolygon.end()
            ? state->second
            : output.map.stale
                ? brain::LiveAuthorityAssignmentState::Stale
                : brain::LiveAuthorityAssignmentState::Offline;
        const auto owners = ownersByPolygon.find(polygon.id);
        if (owners != ownersByPolygon.end()) {
            polygonState.controllerCallsigns = owners->second;
            SortUniqueStrings(&polygonState.controllerCallsigns);
        }
        output.map.polygonStates.push_back(std::move(polygonState));
    }

    SortUniqueStrings(&output.map.unmappedControllerCallsigns);
    SortUniqueStrings(&output.map.conflictedPolygonIds);
    SortUniqueStrings(&output.diagnostics);
    std::sort(
        output.map.assignments.begin(),
        output.map.assignments.end(),
        [](const auto& left, const auto& right) {
            if (left.canonicalPolygonId != right.canonicalPolygonId) {
                return left.canonicalPolygonId < right.canonicalPolygonId;
            }
            if (left.callsign != right.callsign) {
                return left.callsign < right.callsign;
            }
            if (left.positionId != right.positionId) {
                return left.positionId < right.positionId;
            }
            return left.frequency < right.frequency;
        });
    std::sort(
        output.map.polygonStates.begin(),
        output.map.polygonStates.end(),
        [](const auto& left, const auto& right) {
            return left.canonicalPolygonId < right.canonicalPolygonId;
        });

    output.map.verifiedControllerFactCount =
        static_cast<std::uint64_t>(output.verifiedControllerFacts);
    output.map.reusedControllerFactCount =
        static_cast<std::uint64_t>(output.reusedControllerFacts);
    output.map.removedControllerFactCount =
        static_cast<std::uint64_t>(output.removedControllerFacts);
    output.map.verifiedDynamicOwnershipFactCount =
        static_cast<std::uint64_t>(output.verifiedDynamicOwnershipFacts);
    output.map.reusedDynamicOwnershipFactCount =
        static_cast<std::uint64_t>(output.reusedDynamicOwnershipFacts);
    std::ostringstream status;
    status << "LIVE_AUTHORITY_MAP definitions="
           << output.map.polygonDefinitionCount
           << " assignments=" << output.map.assignments.size()
           << " conflicts=" << output.map.conflictedPolygonIds.size()
           << " unmapped=" << output.map.unmappedControllerCallsigns.size();
    output.map.statusLine = status.str();
    SortUniqueStrings(&output.diagnostics);
    output.map.stableHash = HashCanonicalLiveAuthorityActivation(output.map);
    return output;
}

const char* LiveAuthorityStateToken(
    brain::LiveAuthorityAssignmentState state) {
    switch (state) {
        case brain::LiveAuthorityAssignmentState::ActiveExact:
            return "ACTIVE_EXACT";
        case brain::LiveAuthorityAssignmentState::ActiveDynamic:
            return "ACTIVE_DYNAMIC";
        case brain::LiveAuthorityAssignmentState::ActiveParent:
            return "ACTIVE_PARENT";
        case brain::LiveAuthorityAssignmentState::Conflict:
            return "CONFLICT";
        case brain::LiveAuthorityAssignmentState::Unmapped:
            return "UNMAPPED";
        case brain::LiveAuthorityAssignmentState::Stale:
            return "STALE";
        case brain::LiveAuthorityAssignmentState::Offline:
            return "OFFLINE";
    }
    return "UNKNOWN";
}

const char* AuthorityHealthStateToken(
    xvatsim::core::authority::AuthoritySourceHealthState state) {
    switch (state) {
        case xvatsim::core::authority::AuthoritySourceHealthState::Unavailable:
            return "UNAVAILABLE";
        case xvatsim::core::authority::AuthoritySourceHealthState::Partial:
            return "PARTIAL";
        case xvatsim::core::authority::AuthoritySourceHealthState::Complete:
            return "COMPLETE";
        case xvatsim::core::authority::AuthoritySourceHealthState::Stale:
            return "STALE";
        case xvatsim::core::authority::AuthoritySourceHealthState::Invalid:
            return "INVALID";
    }
    return "UNKNOWN";
}

std::string AuthorityJsonString(std::string_view value) {
    std::ostringstream output;
    output << '"';
    for (const auto character : value) {
        switch (character) {
            case '"':
                output << "\\\"";
                break;
            case '\\':
                output << "\\\\";
                break;
            case '\b':
                output << "\\b";
                break;
            case '\f':
                output << "\\f";
                break;
            case '\n':
                output << "\\n";
                break;
            case '\r':
                output << "\\r";
                break;
            case '\t':
                output << "\\t";
                break;
            default:
                if (static_cast<unsigned char>(character) < 0x20U) {
                    output << "\\u00" << std::hex << std::setw(2)
                           << std::setfill('0')
                           << static_cast<int>(
                                  static_cast<unsigned char>(character))
                           << std::dec << std::setfill(' ');
                } else {
                    output << character;
                }
                break;
        }
    }
    output << '"';
    return output.str();
}

template <typename Values, typename Project>
void WriteAuthorityJsonArray(
    std::ostringstream* stream,
    const Values& values,
    Project project) {
    *stream << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            *stream << ',';
        }
        *stream << AuthorityJsonString(project(values[index]));
    }
    *stream << ']';
}

std::string BuildCanonicalAuthorityDiagnosticGeoJson(
    const CanonicalAuthorityMapDataset& dataset,
    const brain::AuthorityRelevanceSnapshot& snapshot) {
    const auto& liveMap = snapshot.liveMap;
    std::unordered_map<std::string, const brain::LiveAuthorityPolygonStateSnapshot*>
        statesByPolygon;
    for (const auto& state : liveMap.polygonStates) {
        statesByPolygon[state.canonicalPolygonId] = &state;
    }
    std::unordered_map<std::string,
                       std::vector<const brain::LiveAuthorityAssignmentSnapshot*>>
        assignmentsByPolygon;
    for (const auto& assignment : liveMap.assignments) {
        assignmentsByPolygon[assignment.canonicalPolygonId].push_back(
            &assignment);
    }

    std::vector<const xvatsim::core::authority::AuthorityPolygon*> polygons;
    polygons.reserve(dataset.authorityPolygonCatalog.polygons.size());
    for (const auto& polygon : dataset.authorityPolygonCatalog.polygons) {
        polygons.push_back(&polygon);
    }
    std::sort(
        polygons.begin(),
        polygons.end(),
        [](const auto* left, const auto* right) {
            return left->id < right->id;
        });

    std::ostringstream output;
    output << std::setprecision(12);
    output << "{\"type\":\"FeatureCollection\",\"xvatsim\":{"
           << "\"stableHash\":" << liveMap.stableHash
           << ","
           << "\"canonicalMapIdentity\":" << liveMap.canonicalMapIdentity
           << ",\"controllerFeedGeneration\":"
           << liveMap.controllerFeedGeneration
           << ",\"dynamicOwnershipGeneration\":"
           << liveMap.dynamicOwnershipGeneration
           << ",\"available\":" << (liveMap.available ? "true" : "false")
           << ",\"stale\":" << (liveMap.stale ? "true" : "false")
           << ",\"polygonDefinitions\":" << liveMap.polygonDefinitionCount
           << ",\"polygonParts\":" << liveMap.polygonPartCount
           << ",\"dataGaps\":" << liveMap.dataGapCount
           << ",\"unmappedControllers\":";
    WriteAuthorityJsonArray(
        &output,
        liveMap.unmappedControllerCallsigns,
        [](const auto& value) -> std::string_view { return value; });
    output << ",\"conflictedPolygonIds\":";
    WriteAuthorityJsonArray(
        &output,
        liveMap.conflictedPolygonIds,
        [](const auto& value) -> std::string_view { return value; });
    output << ",\"sourceReceipts\":[";
    for (std::size_t index = 0; index < dataset.receipt.sources.size(); ++index) {
        const auto& receipt = dataset.receipt.sources[index];
        if (index > 0) {
            output << ',';
        }
        output << "{\"source\":" << AuthorityJsonString(receipt.source)
               << ",\"provider\":" << AuthorityJsonString(receipt.provider)
               << ",\"configuredIdentity\":"
               << AuthorityJsonString(receipt.configuredIdentity)
               << ",\"region\":" << AuthorityJsonString(receipt.region)
               << ",\"revision\":" << AuthorityJsonString(receipt.revision)
               << ",\"contentHash\":" << receipt.contentHash
               << ",\"state\":"
               << AuthorityJsonString(AuthorityHealthStateToken(receipt.state))
               << ",\"acceptedPositions\":" << receipt.acceptedPositions
               << ",\"acceptedPolygons\":" << receipt.acceptedPolygons
               << ",\"acceptedPolygonParts\":"
               << receipt.acceptedPolygonParts
               << ",\"rejectedRecords\":" << receipt.rejectedRecords
               << ",\"reason\":" << AuthorityJsonString(receipt.reason)
               << '}';
    }
    output << "]},\"features\":[";

    for (std::size_t polygonIndex = 0;
         polygonIndex < polygons.size();
         ++polygonIndex) {
        const auto& polygon = *polygons[polygonIndex];
        if (polygonIndex > 0) {
            output << ',';
        }
        const auto stateIt = statesByPolygon.find(polygon.id);
        const auto state = stateIt != statesByPolygon.end() &&
                                   stateIt->second != nullptr
            ? stateIt->second->state
            : brain::LiveAuthorityAssignmentState::Offline;
        output << "{\"type\":\"Feature\",\"id\":"
               << AuthorityJsonString(polygon.id)
               << ",\"properties\":{\"canonicalPolygonId\":"
               << AuthorityJsonString(polygon.id)
               << ",\"sourceFeatureId\":"
               << AuthorityJsonString(polygon.sourceFeatureId)
               << ",\"polygonKey\":"
               << AuthorityJsonString(polygon.polygonKey)
               << ",\"name\":" << AuthorityJsonString(polygon.name)
               << ",\"source\":"
               << AuthorityJsonString(
                      xvatsim::core::authority::AuthoritySourceLabel(
                          polygon.source))
               << ",\"kind\":"
               << AuthorityJsonString(
                      polygon.kind ==
                              xvatsim::core::authority::AuthorityKind::Center
                          ? "CENTER"
                          : polygon.kind ==
                                    xvatsim::core::authority::AuthorityKind::Terminal
                              ? "TERMINAL"
                              : "EXTENSION")
               << ",\"state\":"
               << AuthorityJsonString(LiveAuthorityStateToken(state))
               << ",\"assignments\":[";
        const auto assignments = assignmentsByPolygon.find(polygon.id);
        if (assignments != assignmentsByPolygon.end()) {
            for (std::size_t assignmentIndex = 0;
                 assignmentIndex < assignments->second.size();
                 ++assignmentIndex) {
                if (assignmentIndex > 0) {
                    output << ',';
                }
                const auto& assignment = *assignments->second[assignmentIndex];
                output << "{\"state\":"
                       << AuthorityJsonString(
                              LiveAuthorityStateToken(assignment.state))
                       << ",\"callsign\":"
                       << AuthorityJsonString(assignment.callsign)
                       << ",\"frequency\":"
                       << AuthorityJsonString(assignment.frequency)
                       << ",\"positionId\":"
                       << AuthorityJsonString(assignment.positionId)
                       << ",\"proofSource\":"
                       << AuthorityJsonString(assignment.authoritySource)
                       << ",\"proof\":"
                       << AuthorityJsonString(assignment.ownershipProof)
                       << ",\"controllerFeedGeneration\":"
                       << liveMap.controllerFeedGeneration
                       << ",\"dynamicOwnershipGeneration\":"
                       << assignment.dynamicOwnershipGeneration << '}';
            }
        }
        output << "]},\"geometry\":";
        if (polygon.parts.empty()) {
            output << "null";
        } else {
            output << "{\"type\":\"MultiPolygon\",\"coordinates\":[";
            for (std::size_t partIndex = 0;
                 partIndex < polygon.parts.size();
                 ++partIndex) {
                if (partIndex > 0) {
                    output << ',';
                }
                const auto& part = polygon.parts[partIndex];
                output << '[';
                const auto writeRing = [&](const auto& ring) {
                    output << '[';
                    for (std::size_t pointIndex = 0;
                         pointIndex < ring.points.size();
                         ++pointIndex) {
                        if (pointIndex > 0) {
                            output << ',';
                        }
                        output << '[' << ring.points[pointIndex].longitudeDeg
                               << ',' << ring.points[pointIndex].latitudeDeg
                               << ']';
                    }
                    output << ']';
                };
                writeRing(part.exterior);
                for (const auto& hole : part.holes) {
                    output << ',';
                    writeRing(hole);
                }
                output << ']';
            }
            output << "]}";
        }
        output << '}';
    }
    output << "]}";
    return output.str();
}

std::size_t BuildAuthorityControllerRosterSignature(
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot) {
    std::size_t hash = 1469598103934665603ull;
    HashCombine(
        &hash,
        static_cast<std::size_t>(controllerFeedSnapshot.available ? 1 : 0));
    HashCombine(
        &hash,
        static_cast<std::size_t>(controllerFeedSnapshot.stale ? 1 : 0));
    HashCombine(
        &hash,
        static_cast<std::size_t>(controllerFeedSnapshot.generation));

    std::vector<std::string> entries;
    entries.reserve(controllerFeedSnapshot.Controllers().size());
    for (const auto& controller : controllerFeedSnapshot.Controllers()) {
        std::ostringstream entry;
        entry << xvatsim::core::authority::NormalizeControllerCallsign(
                     controller.callsign)
              << '|'
              << NormalizeFrequency(controller.frequency) << '|'
              << controller.facility << '|'
              << (controller.actionable ? 1 : 0) << '|'
              << (controller.atis ? 1 : 0);
        entries.push_back(entry.str());
    }
    std::sort(entries.begin(), entries.end());
    for (const auto& entry : entries) {
        HashCombineString(&hash, entry);
    }
    return hash;
}

std::size_t BuildAuthorityWatchInputSignature(
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const AuthorityRelevanceScopeArtifacts& scopeArtifacts) {
    std::size_t hash = BuildAuthorityControllerRosterSignature(
        controllerFeedSnapshot);
    HashCombine(&hash, scopeArtifacts.signature);
    return hash;
}

std::size_t BuildScopedAuthorityRelevanceSignature(
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const brain::RouteSectorSnapshot& routeSectorSnapshot,
    const std::unordered_set<std::string>& routeAuthorityMatchKeys) {
    std::size_t hash = BuildAuthorityControllerRosterSignature(
        controllerFeedSnapshot);
    HashCombine(
        &hash,
        BuildAuthorityStructuralScopeRouteSignature(routeSectorSnapshot));
    auto sortedRouteKeys = SortedAuthorityStrings(routeAuthorityMatchKeys);
    for (const auto& routeKey : sortedRouteKeys) {
        HashCombineString(&hash, routeKey);
    }
    return hash;
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

struct ExpandedFmsRouteCacheResult {
    core::preflight::PreflightRouteCache cache;
    std::string sourceName;
};

std::filesystem::path BuildXPlaneFmsPlansFolderPath() {
    const auto& xplaneRoot = ActiveRoutePreparationState().xplaneRootPath;
    if (!xplaneRoot.empty()) {
        return std::filesystem::path(xplaneRoot) / "Output" / "FMS plans";
    }

    return std::filesystem::path(core::preflight::kDefaultFmsPlansFolder);
}

std::unordered_set<std::string> BuildFmsRouteEvidenceTokens(
    const core::preflight::FmsPlan& plan) {
    std::unordered_set<std::string> tokens;
    for (const auto& [_, value] : plan.metadata) {
        const auto normalized = NormalizeRouteToken(value);
        if (!normalized.empty()) {
            tokens.insert(normalized);
        }
    }
    for (const auto& waypoint : plan.waypoints) {
        const auto ident = NormalizeRouteToken(waypoint.ident);
        if (!ident.empty()) {
            tokens.insert(ident);
        }
        const auto via = NormalizeRouteToken(waypoint.via);
        if (!via.empty() && !IsRouteControlToken(via)) {
            tokens.insert(via);
        }
    }
    return tokens;
}

bool ExpandedFmsPlanMatchesFiledRoute(
    const core::preflight::FmsPlan& plan,
    const brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    if (NormalizeRouteToken(plan.departureIcao) !=
            NormalizeRouteToken(networkPlanSnapshot.departureIcao) ||
        NormalizeRouteToken(plan.destinationIcao) !=
            NormalizeRouteToken(networkPlanSnapshot.destinationIcao)) {
        return false;
    }
    if (networkPlanSnapshot.routeText.empty()) {
        return false;
    }

    const auto evidenceTokens = BuildFmsRouteEvidenceTokens(plan);
    const auto proceduresByName =
        BuildRouteProcedureCatalog(networkPlanSnapshot);
    const auto grammarCatalog =
        BuildRouteGrammarCatalog(GetAirwayGraph(), &proceduresByName);
    const auto parsedTokens =
        xvatsim::core::route::ParseRouteTokens(
            networkPlanSnapshot.routeText,
            &grammarCatalog);

    std::size_t requiredTokens = 0;
    for (const auto& parsedToken : parsedTokens) {
        if (parsedToken.kind == xvatsim::core::route::RouteTokenKind::Control ||
            parsedToken.kind == xvatsim::core::route::RouteTokenKind::Empty) {
            continue;
        }

        const auto normalized = parsedToken.normalized.empty()
                                    ? parsedToken.rawNormalized
                                    : parsedToken.normalized;
        if (normalized.empty() || IsRouteControlToken(normalized)) {
            continue;
        }

        ++requiredTokens;
        if (evidenceTokens.find(normalized) == evidenceTokens.end()) {
            return false;
        }
    }

    return requiredTokens > 0;
}

std::optional<ExpandedFmsRouteCacheResult> TryBuildExpandedFmsRouteCache(
    const brain::NetworkPlanSnapshot& networkPlanSnapshot,
    const RouteCancellationToken* cancellation = nullptr) {
    const auto departureIcao = NormalizeRouteToken(networkPlanSnapshot.departureIcao);
    const auto destinationIcao = NormalizeRouteToken(networkPlanSnapshot.destinationIcao);
    if (departureIcao.empty() ||
        destinationIcao.empty() ||
        networkPlanSnapshot.routeText.empty()) {
        return std::nullopt;
    }

    const auto fmsPlansFolder = BuildXPlaneFmsPlansFolderPath();
    const auto& source = ActiveRoutePreparationState().source;
    if (source == nullptr) {
        return std::nullopt;
    }

    const auto fileStemPrefix = departureIcao + destinationIcao;
    std::optional<ExpandedFmsRouteCacheResult> bestResult;
    for (const auto& path : source->ListRegularFiles(fmsPlansFolder)) {
        if (RouteCancellationRequested(cancellation)) {
            return std::nullopt;
        }
        if (NormalizeRouteToken(path.extension().string()) != "FMS") {
            continue;
        }
        const auto stem = NormalizeRouteToken(path.stem().string());
        if (stem.rfind(fileStemPrefix, 0) != 0) {
            continue;
        }

        const auto parseResult = source->LoadFmsPlan(path);
        if (!parseResult.ok ||
            !ExpandedFmsPlanMatchesFiledRoute(
                parseResult.plan,
                networkPlanSnapshot)) {
            continue;
        }

        auto cache = core::preflight::BuildPreflightRouteCache(parseResult.plan);
        const auto validation =
            core::preflight::ValidatePreflightRouteCacheForNetworkPlan(
                cache,
                networkPlanSnapshot,
                false);
        if (!validation.accepted) {
            continue;
        }

        ExpandedFmsRouteCacheResult result;
        result.cache = std::move(cache);
        result.sourceName = path.filename().string();
        if (!bestResult.has_value() ||
            result.cache.plan.sourceModifiedUnixSeconds >
                bestResult->cache.plan.sourceModifiedUnixSeconds ||
            (result.cache.plan.sourceModifiedUnixSeconds ==
                 bestResult->cache.plan.sourceModifiedUnixSeconds &&
             result.sourceName > bestResult->sourceName)) {
            bestResult = std::move(result);
        }
    }

    return bestResult;
}

std::vector<brain::RouteWaypointSnapshot> ResolveRouteWaypoints(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::NetworkPlanSnapshot& networkPlanSnapshot,
    RouteResolveDiagnostics* diagnostics,
    RouteWorkerPhaseTimings* timings = nullptr,
    const RouteCancellationToken* cancellation = nullptr) {
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

    const auto procedureStarted = std::chrono::steady_clock::now();
    const auto proceduresByName =
        BuildRouteProcedureCatalog(networkPlanSnapshot);
    if (RouteCancellationRequested(cancellation)) {
        return {};
    }
    if (timings != nullptr) {
        timings->procedureMetadataUs = std::chrono::duration_cast<
            std::chrono::microseconds>(
                std::chrono::steady_clock::now() - procedureStarted)
                                           .count();
    }
    const auto grammarStarted = std::chrono::steady_clock::now();
    const auto grammarCatalog =
        BuildRouteGrammarCatalog(GetAirwayGraph(), &proceduresByName);
    const auto parsedTokens =
        xvatsim::core::route::ParseRouteTokens(
            networkPlanSnapshot.routeText,
            &grammarCatalog);
    if (timings != nullptr) {
        timings->grammarParsingUs = std::chrono::duration_cast<
            std::chrono::microseconds>(
                std::chrono::steady_clock::now() - grammarStarted)
                                      .count();
    }
    for (const auto& parsedToken : parsedTokens) {
        if (diagnostics != nullptr && !parsedToken.rawNormalized.empty()) {
            diagnostics->rawTokens.push_back(parsedToken.rawNormalized);
        }
    }

    for (std::size_t tokenIndex = 0; tokenIndex < parsedTokens.size(); ++tokenIndex) {
        if (RouteCancellationRequested(cancellation)) {
            return {};
        }
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
                    diagnostics->unresolvedAirwayTokens.push_back(parsedToken.normalized);
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
        DeduplicatePreserveOrder(&diagnostics->unresolvedAirwayTokens);
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
                    << diagnostics.unresolvedAirwayTokens.size() << "|"
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
           << " unresolvedAirways=" << diagnostics.unresolvedAirwayTokens.size()
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
           << " unresolvedAirwayList="
           << SummarizeStrings(diagnostics.unresolvedAirwayTokens)
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

struct RouteSourceDataset {
    std::uint64_t identity = 0;
    std::uint64_t centerBoundaryGeneration = 0;
    std::uint64_t authorityCatalogGeneration = 0;
    std::uint64_t retainedBytes = 0;
    std::vector<xvatsim::core::route::SectorFeature> traversalFeatures;
    ControllerAuthorityCatalog controllerAuthorityCatalog;
};

struct RouteDatasetPublicationState {
    std::shared_ptr<const RouteSourceDataset> dataset;
    std::uint64_t identity = 0;
    std::uint64_t contentIdentity = 0;
    std::uint64_t publicationGeneration = 0;
    std::uint64_t centerBoundaryGeneration = 0;
    std::uint64_t authorityCatalogGeneration = 0;
    bool available = false;
    bool stale = true;
};

std::shared_ptr<RouteSourceDataset> BuildRouteSourceDataset(
    const std::vector<unsigned char>& boundaryPayload,
    const std::vector<unsigned char>& authorityCatalogPayload,
    const std::vector<unsigned char>& ownershipPayload);

namespace {

std::uint64_t BuildRoutePublicationIdentity(
    std::uint64_t contentIdentity,
    std::uint64_t publicationGeneration,
    bool available,
    bool stale) {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto add = [&](std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            hash ^= (value >> shift) & 0xffULL;
            hash *= 1099511628211ULL;
        }
    };
    add(contentIdentity);
    add(publicationGeneration);
    add(available ? 1 : 0);
    add(stale ? 1 : 0);
    return hash == 0 ? 1 : hash;
}

std::shared_ptr<const RouteDatasetPublicationState>
BuildRouteDatasetPublicationState(
    std::shared_ptr<const RouteSourceDataset> dataset,
    std::uint64_t publicationGeneration,
    bool available,
    bool stale) {
    auto publication = std::make_shared<RouteDatasetPublicationState>();
    publication->dataset = std::move(dataset);
    publication->contentIdentity = publication->dataset != nullptr
        ? publication->dataset->identity
        : 0;
    publication->publicationGeneration = publicationGeneration;
    publication->centerBoundaryGeneration = publication->dataset != nullptr
        ? publication->dataset->centerBoundaryGeneration
        : 0;
    publication->authorityCatalogGeneration = publication->dataset != nullptr
        ? publication->dataset->authorityCatalogGeneration
        : 0;
    publication->available = available && publication->dataset != nullptr;
    publication->stale = stale || !publication->available;
    publication->identity = BuildRoutePublicationIdentity(
        publication->contentIdentity,
        publicationGeneration,
        publication->available,
        publication->stale);
    return publication;
}

void HashDatasetBytes(std::uint64_t* hash, const std::vector<unsigned char>& bytes) {
    if (hash == nullptr) {
        return;
    }
    for (const auto value : bytes) {
        *hash ^= static_cast<std::uint64_t>(value);
        *hash *= 1099511628211ULL;
    }
    *hash ^= static_cast<std::uint64_t>(bytes.size());
    *hash *= 1099511628211ULL;
}

std::shared_ptr<CanonicalAuthorityMapDataset>
CompileCanonicalAuthoritySourcePayloads(
    const std::vector<unsigned char>& boundaryPayload,
    const std::vector<unsigned char>& terminalBoundaryPayload,
    const std::vector<unsigned char>& authorityCatalogPayload,
    const std::vector<unsigned char>& ownershipPayload) {
    if (boundaryPayload.empty() || authorityCatalogPayload.empty()) {
        return {};
    }

    // Provider parsing is an adapter concern. Reconciliation, identity,
    // referential validation, receipts, and immutable dataset construction
    // have one owner in the dedicated authority-map component.
    (void)GetCachedSectorFeatures(boundaryPayload);
    (void)GetCachedTerminalSectorFeatures(terminalBoundaryPayload);
    return xvatsim::modules::authority_map::
        CompileCanonicalAuthorityMapDataset(
            boundaryPayload,
            terminalBoundaryPayload,
            authorityCatalogPayload,
            ownershipPayload,
            GetCachedCoreControllerAuthorityCatalog(
                authorityCatalogPayload,
                terminalBoundaryPayload,
                ownershipPayload),
            GetCachedCoreAuthorityPolygonCatalog(
                boundaryPayload,
                terminalBoundaryPayload,
                ownershipPayload));
}

}  // namespace

class AuthorityRelevanceEngine {
public:
    brain::AuthorityRelevanceSnapshot Evaluate(
        const CanonicalAuthorityMapDataset& dataset,
        std::uint64_t terminalBoundaryGeneration,
        const brain::AircraftStateSnapshot& aircraftState,
        const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
        const brain::VnasLiveOwnershipSnapshot* dynamicOwnership,
        const brain::RouteSectorSnapshot& routeSectorSnapshot,
        const std::string& scheduleReason,
        const std::atomic<bool>* cancelRequested = nullptr);

    void Reset() {
        hasAuthorityRelevanceCache_ = false;
        lastAuthorityRelevanceBuildTickSeconds_ = 0;
        lastAuthorityRelevanceLatitudeDeg_ = 0.0;
        lastAuthorityRelevanceLongitudeDeg_ = 0.0;
        lastAuthorityRelevanceSignature_ = 0;
        lastAuthorityOperationalScopeSignature_ = 0;
        lastAuthorityWatchInputSignature_ = 0;
        lastAuthorityRelevanceProgressRouteSignature_ = 0;
        lastAuthorityRelevanceProgressWindowNm_ = 0.0;
        cachedAuthorityRelevanceSnapshot_ = {};
        scopeArtifacts_ = {};
        authorityProgressRouteSignature_ = 0;
        authorityProgressWindowNm_ = 0.0;
        lastAuthorityProgressTickSeconds_ = 0;
        liveAuthorityIncrementalCache_ = {};
    }

    void Age(long long ageSeconds) {
        if (ageSeconds > 0 && lastAuthorityRelevanceBuildTickSeconds_ > 0) {
            lastAuthorityRelevanceBuildTickSeconds_ -= ageSeconds;
        }
    }

private:
    bool hasAuthorityRelevanceCache_ = false;
    long long lastAuthorityRelevanceBuildTickSeconds_ = 0;
    double lastAuthorityRelevanceLatitudeDeg_ = 0.0;
    double lastAuthorityRelevanceLongitudeDeg_ = 0.0;
    std::size_t lastAuthorityRelevanceSignature_ = 0;
    std::size_t lastAuthorityOperationalScopeSignature_ = 0;
    std::size_t lastAuthorityWatchInputSignature_ = 0;
    std::size_t lastAuthorityRelevanceProgressRouteSignature_ = 0;
    double lastAuthorityRelevanceProgressWindowNm_ = 0.0;
    brain::AuthorityRelevanceSnapshot cachedAuthorityRelevanceSnapshot_{};
    std::size_t authorityProgressRouteSignature_ = 0;
    double authorityProgressWindowNm_ = 0.0;
    long long lastAuthorityProgressTickSeconds_ = 0;
    AuthorityRelevanceScopeArtifacts scopeArtifacts_;
    CanonicalLiveAuthorityIncrementalCache liveAuthorityIncrementalCache_;
};

RouteSectorResolver::RouteSectorResolver()
    : authorityMapStore_(std::make_unique<
          xvatsim::modules::authority_map::CanonicalAuthorityMapStore>()),
      authorityEngine_(std::make_unique<AuthorityRelevanceEngine>()),
      synchronousRouteOracleEngine_(
          std::make_unique<RoutePreparationEngine>(
              GetXPlaneRootPath(), nullptr, false)) {}

RouteSectorResolver::~RouteSectorResolver() {
    StopSourceRefreshAfterFlightLoop();
}

void RouteSectorResolver::LoadBoundaryPayloadsForTesting(
    const std::string& boundaryGeoJson,
    const std::string& terminalGeoJson,
    const std::string& authorityCatalogDat) const {
    LoadBoundaryPayloadsForTesting(
        boundaryGeoJson,
        terminalGeoJson,
        authorityCatalogDat,
        {});
}

void RouteSectorResolver::LoadBoundaryPayloadsForTesting(
    const std::string& boundaryGeoJson,
    const std::string& terminalGeoJson,
    const std::string& authorityCatalogDat,
    const std::string& ownershipJson) const {
    StopSourceRefreshAfterFlightLoop();

    std::lock_guard<std::mutex> lock(fetchMutex_);
    boundaryPayload_.assign(boundaryGeoJson.begin(), boundaryGeoJson.end());
    terminalBoundaryPayload_.assign(terminalGeoJson.begin(), terminalGeoJson.end());
    vatspyPayload_.assign(authorityCatalogDat.begin(), authorityCatalogDat.end());
    ownershipPayload_.assign(ownershipJson.begin(), ownershipJson.end());
    auto authorityDataset = CompileCanonicalAuthoritySourcePayloads(
        boundaryPayload_,
        terminalBoundaryPayload_,
        vatspyPayload_,
        ownershipPayload_);
    routeDataset_ = BuildRouteSourceDataset(
        boundaryPayload_,
        vatspyPayload_,
        ownershipPayload_);
    pendingAuthorityDataset_.reset();
    pendingBoundaryPayload_.clear();
    pendingTerminalBoundaryPayload_.clear();
    pendingVatspyPayload_.clear();
    pendingOwnershipPayload_.clear();
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
    if (authorityDataset != nullptr) {
        authorityDataset->centerBoundaryGeneration =
            centerBoundaryGeneration_;
        authorityDataset->authorityCatalogGeneration =
            authorityCatalogGeneration_;
        authorityDataset->terminalBoundaryGeneration =
            terminalBoundaryGeneration_;
    }
    if (routeDataset_ != nullptr) {
        routeDataset_->centerBoundaryGeneration = centerBoundaryGeneration_;
        routeDataset_->authorityCatalogGeneration =
            authorityCatalogGeneration_;
    }
    publishedTerminalBoundaryGeneration_.store(
        terminalBoundaryGeneration_, std::memory_order_release);
    authorityMapStore_->Publish(std::move(authorityDataset));
    ++routePublicationGeneration_;
    if (routePublicationGeneration_ == 0) {
        routePublicationGeneration_ = 1;
    }
    std::atomic_store_explicit(
        &publishedRoutePublication_,
        BuildRouteDatasetPublicationState(
            std::shared_ptr<const RouteSourceDataset>(routeDataset_),
            routePublicationGeneration_,
            routeDataset_ != nullptr,
            routeDataset_ == nullptr),
        std::memory_order_release);
    lastFetchSucceeded_ =
        hasBoundaryCache_ && hasAuthorityCatalogCache_ && hasTerminalBoundaryCache_;
    const auto loadedTickSeconds = CurrentTickSeconds();
    lastFetchTickSeconds_.store(
        loadedTickSeconds, std::memory_order_release);
    lastSuccessfulCenterFetchTickSeconds_ =
        (hasBoundaryCache_ && hasAuthorityCatalogCache_)
            ? loadedTickSeconds
            : 0;
    lastSuccessfulTerminalFetchTickSeconds_ =
        hasTerminalBoundaryCache_ ? loadedTickSeconds : 0;
    hasSnapshotCache_ = false;
    cachedSnapshot_ = {};
    hasAuthorityRelevanceCache_ = false;
    cachedAuthorityRelevanceSnapshot_ = {};
    if (authorityEngine_ != nullptr) {
        authorityEngine_->Reset();
    }
    lastAuthorityRelevanceBuildTickSeconds_ = 0;
    lastAuthorityRelevanceLatitudeDeg_ = 0.0;
    lastAuthorityRelevanceLongitudeDeg_ = 0.0;
    lastAuthorityRelevanceSignature_ = 0;
    lastAuthorityOperationalScopeSignature_ = 0;
    lastAuthorityWatchInputSignature_ = 0;
    lastAuthorityRelevanceProgressRouteSignature_ = 0;
    lastAuthorityRelevanceProgressWindowNm_ = 0.0;
    authorityProgressRouteSignature_ = 0;
    authorityProgressWindowNm_ = 0.0;
    lastAuthorityProgressTickSeconds_ = 0;
    airportCoverageCache_.clear();
    hasPendingPayload_ = false;
    pendingPublicationReady_.store(false, std::memory_order_release);
    fetchInProgress_ = false;
}

void RouteSectorResolver::QueueBoundaryPayloadsForTesting(
    const std::string& boundaryGeoJson,
    const std::string& terminalGeoJson,
    const std::string& authorityCatalogDat) const {
    QueueBoundaryPayloadsForTesting(
        boundaryGeoJson,
        terminalGeoJson,
        authorityCatalogDat,
        {});
}

void RouteSectorResolver::QueueBoundaryPayloadsForTesting(
    const std::string& boundaryGeoJson,
    const std::string& terminalGeoJson,
    const std::string& authorityCatalogDat,
    const std::string& ownershipJson) const {
    StopSourceRefreshAfterFlightLoop();

    StageFetchedPayloads(
        std::vector<unsigned char>(boundaryGeoJson.begin(), boundaryGeoJson.end()),
        std::vector<unsigned char>(terminalGeoJson.begin(), terminalGeoJson.end()),
        std::vector<unsigned char>(authorityCatalogDat.begin(), authorityCatalogDat.end()),
        std::vector<unsigned char>(ownershipJson.begin(), ownershipJson.end()));
    fetchInProgress_ = false;
}

void RouteSectorResolver::AgeAuthorityRelevanceCacheForTesting(
    long long ageSeconds) const {
    if (authorityEngine_ != nullptr) {
        authorityEngine_->Age(ageSeconds);
    }
    if (ageSeconds <= 0 || lastAuthorityRelevanceBuildTickSeconds_ <= 0) {
        return;
    }

    lastAuthorityRelevanceBuildTickSeconds_ -= ageSeconds;
}

void RouteSectorResolver::SetPreflightRouteCache(
    const core::preflight::PreflightRouteCache& cache,
    const std::string& validationReason) {
    preflightRouteCache_ = cache;
    preflightRouteCacheReason_ = validationReason;
    hasSnapshotCache_ = false;
    lastSnapshotRouteKey_.clear();
    hasAuthorityRelevanceCache_ = false;
    cachedAuthorityRelevanceSnapshot_ = {};
    if (authorityEngine_ != nullptr) {
        authorityEngine_->Reset();
    }
    lastAuthorityRelevanceSignature_ = 0;
    lastAuthorityOperationalScopeSignature_ = 0;
    lastAuthorityWatchInputSignature_ = 0;
    lastAuthorityRelevanceProgressRouteSignature_ = 0;
    lastAuthorityRelevanceProgressWindowNm_ = 0.0;
    authorityProgressRouteSignature_ = 0;
    authorityProgressWindowNm_ = 0.0;
    lastAuthorityProgressTickSeconds_ = 0;
}

void RouteSectorResolver::ClearPreflightRouteCache() {
    preflightRouteCache_.reset();
    preflightRouteCacheReason_.clear();
    hasSnapshotCache_ = false;
    lastSnapshotRouteKey_.clear();
    hasAuthorityRelevanceCache_ = false;
    cachedAuthorityRelevanceSnapshot_ = {};
    if (authorityEngine_ != nullptr) {
        authorityEngine_->Reset();
    }
    lastAuthorityRelevanceSignature_ = 0;
    lastAuthorityOperationalScopeSignature_ = 0;
    lastAuthorityWatchInputSignature_ = 0;
    lastAuthorityRelevanceProgressRouteSignature_ = 0;
    lastAuthorityRelevanceProgressWindowNm_ = 0.0;
    authorityProgressRouteSignature_ = 0;
    authorityProgressWindowNm_ = 0.0;
    lastAuthorityProgressTickSeconds_ = 0;
}

void RouteSectorResolver::ResetRuntimeState() {
    // Per-flight reset only. Source fetching/publication is independent and
    // must never be joined or mutex-waited from a lifecycle callback.
    hasSnapshotCache_ = false;
    lastSnapshotBuildTickSeconds_ = 0;
    lastSnapshotLatitudeDeg_ = 0.0;
    lastSnapshotLongitudeDeg_ = 0.0;
    lastSnapshotRouteKey_.clear();
    cachedSnapshot_ = {};
    hasAuthorityRelevanceCache_ = false;
    cachedAuthorityRelevanceSnapshot_ = {};
    if (authorityEngine_ != nullptr) {
        authorityEngine_->Reset();
    }
    lastAuthorityRelevanceBuildTickSeconds_ = 0;
    lastAuthorityRelevanceLatitudeDeg_ = 0.0;
    lastAuthorityRelevanceLongitudeDeg_ = 0.0;
    lastAuthorityRelevanceSignature_ = 0;
    lastAuthorityOperationalScopeSignature_ = 0;
    lastAuthorityWatchInputSignature_ = 0;
    lastAuthorityRelevanceProgressRouteSignature_ = 0;
    lastAuthorityRelevanceProgressWindowNm_ = 0.0;
    authorityProgressRouteSignature_ = 0;
    authorityProgressWindowNm_ = 0.0;
    lastAuthorityProgressTickSeconds_ = 0;
    airportCoverageCache_.clear();
}

void RouteSectorResolver::ResetSourceCaches() {
    ResetRuntimeState();

    std::lock_guard<std::mutex> lock(fetchMutex_);
    authorityMapStore_->ResetAfterReadersStopped();
    std::atomic_store_explicit(
        &publishedRoutePublication_,
        std::shared_ptr<const RouteDatasetPublicationState>{},
        std::memory_order_release);
    publishedTerminalBoundaryGeneration_.store(0, std::memory_order_release);
    hasBoundaryCache_ = false;
    hasAuthorityCatalogCache_ = false;
    hasTerminalBoundaryCache_ = false;
    boundaryPayload_.clear();
    terminalBoundaryPayload_.clear();
    vatspyPayload_.clear();
    ownershipPayload_.clear();
    pendingAuthorityDataset_.reset();
    routeDataset_.reset();
    pendingRouteDataset_.reset();
    routePublicationGeneration_ = 0;
    pendingBoundaryPayload_.clear();
    pendingTerminalBoundaryPayload_.clear();
    pendingVatspyPayload_.clear();
    pendingOwnershipPayload_.clear();
    hasPendingPayload_ = false;
    pendingPublicationReady_.store(false, std::memory_order_release);
    centerBoundaryGeneration_ = 0;
    authorityCatalogGeneration_ = 0;
    terminalBoundaryGeneration_ = 0;
}

void RouteSectorResolver::Reset() {
    ResetRuntimeState();
}

const CanonicalAuthorityMapDataset*
RouteSectorResolver::GetAuthoritySourceDataset() const {
    return authorityMapStore_->Current();
}

AuthorityDatasetPublication
RouteSectorResolver::GetAuthoritySourceDatasetPublication() const {
    AuthorityDatasetPublication publication;
    publication.dataset = GetAuthoritySourceDataset();
    if (publication.dataset != nullptr) {
        publication.identity = publication.dataset->identity;
        publication.terminalBoundaryGeneration =
            publication.dataset->terminalBoundaryGeneration;
    }
    return publication;
}

RouteDatasetPublication
RouteSectorResolver::GetRouteSourceDatasetPublication() const {
    RouteDatasetPublication publication;
    const auto state = std::atomic_load_explicit(
        &publishedRoutePublication_, std::memory_order_acquire);
    if (state != nullptr) {
        publication.dataset = state->dataset;
        publication.identity = state->identity;
        publication.contentIdentity = state->contentIdentity;
        publication.publicationGeneration = state->publicationGeneration;
        publication.centerBoundaryGeneration =
            state->centerBoundaryGeneration;
        publication.authorityCatalogGeneration =
            state->authorityCatalogGeneration;
        publication.available = state->available;
        publication.stale = state->stale;
    }
    return publication;
}

void RouteSectorResolver::StartSourceRefreshBeforeFlightLoop() const {
    HarvestPendingFetch();
    if (!sourceRefreshStarted_.load(std::memory_order_acquire)) {
        StartAsyncBoundaryFetch(CurrentTickSeconds());
    }
}

void RouteSectorResolver::StopSourceRefreshAfterFlightLoop() const {
    sourceRefreshStop_.store(true, std::memory_order_release);
    sourceRefreshWait_.notify_all();
    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }
    sourceRefreshStarted_.store(false, std::memory_order_release);
    fetchInProgress_.store(false, std::memory_order_release);
}

bool RouteSectorResolver::TryHarvestSourcePublications() const {
    if (fetchInProgress_.load(std::memory_order_acquire) ||
        !pendingPublicationReady_.load(std::memory_order_acquire)) {
        return false;
    }
    HarvestPendingFetch();
    return GetRouteSourceDatasetPublication().dataset != nullptr;
}

std::uint64_t RouteSectorResolver::GetAuthoritySourceDatasetIdentity() const {
    return GetAuthoritySourceDatasetPublication().identity;
}

std::uint64_t RouteSectorResolver::GetTerminalBoundaryGeneration() const {
    const auto publication = GetAuthoritySourceDatasetPublication();
    if (publication.dataset != nullptr) {
        return publication.terminalBoundaryGeneration;
    }
    return publishedTerminalBoundaryGeneration_.load(
        std::memory_order_acquire);
}

brain::RouteSectorSnapshot RouteSectorResolver::Resolve(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::NetworkPlanSnapshot& networkPlanSnapshot) const {
    if (networkPlanSnapshot.stale) {
        brain::RouteSectorSnapshot snapshot;
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.diagnosticCacheStatus = "input-stale";
        snapshot.diagnosticReason = "network-plan-stale";
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
        snapshot.diagnosticCacheStatus =
            fetchInProgress_ ? "source-pending" : "source-unavailable";
        snapshot.diagnosticReason = "boundary-cache-unavailable";
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
    const auto movementThresholdNm = aircraftState.onGround
                                         ? kGroundSnapshotMovementThresholdNm
                                         : kAirborneSnapshotMovementThresholdNm;
    const auto movedEnough =
        !hasSnapshotCache_ || movedDistanceNm >= movementThresholdNm;

    if (!routeChanged && !movedEnough) {
        auto snapshot = cachedSnapshot_;
        snapshot.diagnosticCacheStatus = centerAuthorityFresh
                                             ? "route-cache-hit"
                                             : "route-cache-stale-hit";
        snapshot.diagnosticReason = "route-key-and-movement-unchanged";
        snapshot.stale = !centerAuthorityFresh;
        if (snapshot.stale) {
            AppendStaleSuffixIfNeeded(&snapshot.statusLine);
        }
        return snapshot;
    }

    auto snapshot = BuildSnapshot(aircraftState, networkPlanSnapshot);
    if (snapshot.diagnosticCacheStatus.empty()) {
        if (snapshot.statusLine.find("via preflight cache") != std::string::npos) {
            snapshot.diagnosticCacheStatus = "preflight-route-cache-build";
        } else if (snapshot.statusLine.find("via expanded FMS plan") !=
                   std::string::npos) {
            snapshot.diagnosticCacheStatus = "expanded-fms-route-build";
        } else {
            snapshot.diagnosticCacheStatus = "route-rebuild";
        }
    }
    if (snapshot.diagnosticReason.empty()) {
        if (routeSourceChanged) {
            snapshot.diagnosticReason = "source-generation-changed";
        } else if (routeChanged) {
            snapshot.diagnosticReason = "route-key-changed";
        } else if (movedEnough) {
            snapshot.diagnosticReason = aircraftState.onGround
                                            ? "ground-movement-threshold"
                                            : "airborne-movement-threshold";
        } else {
            snapshot.diagnosticReason = "route-refresh";
        }
    }
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

brain::AuthorityRelevanceSnapshot AuthorityRelevanceEngine::Evaluate(
    const CanonicalAuthorityMapDataset& dataset,
    std::uint64_t terminalBoundaryGeneration,
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const brain::VnasLiveOwnershipSnapshot* dynamicOwnership,
    const brain::RouteSectorSnapshot& routeSectorSnapshot,
    const std::string& scheduleReason,
    const std::atomic<bool>* cancelRequested) {
    const auto terminalBoundaryGeneration_ = terminalBoundaryGeneration;
    brain::AuthorityRelevanceSnapshot snapshot;
    const auto cancellationRequested = [&]() {
        return cancelRequested != nullptr &&
               cancelRequested->load(std::memory_order_relaxed);
    };
    const auto cancelledSnapshot = [&]() {
        brain::AuthorityRelevanceSnapshot cancelled;
        cancelled.available = false;
        cancelled.stale = true;
        cancelled.controllerFeedGeneration = controllerFeedSnapshot.generation;
        cancelled.centerBoundaryGeneration = routeSectorSnapshot.centerBoundaryGeneration;
        cancelled.authorityCatalogGeneration = routeSectorSnapshot.authorityCatalogGeneration;
        cancelled.terminalCoverageGeneration = terminalBoundaryGeneration_;
        cancelled.diagnosticCacheStatus = "authority-worker-cancelled";
        cancelled.diagnosticReason = "cooperative-cancellation";
        cancelled.statusLine = "AUTHORITY worker cancelled";
        return cancelled;
    };
    if (cancellationRequested()) {
        return cancelledSnapshot();
    }
    InitializeAuthorityRelevanceSourceEvidence(
        &snapshot,
        controllerFeedSnapshot,
        routeSectorSnapshot,
        scheduleReason);

    if (scheduleReason.empty()) {
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.controllerFeedGeneration = controllerFeedSnapshot.generation;
        snapshot.centerBoundaryGeneration =
            routeSectorSnapshot.centerBoundaryGeneration;
        snapshot.authorityCatalogGeneration =
            routeSectorSnapshot.authorityCatalogGeneration;
        snapshot.terminalCoverageGeneration = terminalBoundaryGeneration_;
        snapshot.diagnosticCacheStatus = "not-brain-scheduled";
        snapshot.diagnosticReason = "missing-schedule-reason";
        snapshot.statusLine = "AUTHORITY verifier not scheduled";
        PopulateBasicAuthorityControllerEvidence(&snapshot, controllerFeedSnapshot);
        snapshot.evidence.source.cacheStatus = snapshot.diagnosticCacheStatus;
        snapshot.evidence.source.cacheReason = snapshot.diagnosticReason;
        FinalizeAuthorityRelevanceEvidenceGuardrails(&snapshot);
        return snapshot;
    }

    // The input feed is the complete current radio-board candidate set. Static
    // ownership is resolved before route/endpoint relevance, but no controller
    // outside that admission set can enter this live projection.
    auto liveMapBuild =
        BuildRadioScopedLiveAuthorityMap(
            dataset,
            controllerFeedSnapshot,
            dynamicOwnership,
            &liveAuthorityIncrementalCache_);
    liveMapBuild.map = brain::BuildBrainOwnedLiveAuthorityMapSnapshot(
        std::move(liveMapBuild.map),
        &liveMapBuild.diagnostics);
    liveMapBuild.activePolygons.erase(
        std::remove_if(
            liveMapBuild.activePolygons.begin(),
            liveMapBuild.activePolygons.end(),
            [&](const auto& activePolygon) {
                return std::none_of(
                    liveMapBuild.map.assignments.begin(),
                    liveMapBuild.map.assignments.end(),
                    [&](const auto& assignment) {
                        return brain::IsActiveLiveAuthorityState(
                                   assignment.state) &&
                               assignment.canonicalPolygonId ==
                                   activePolygon.polygonId &&
                               assignment.positionId ==
                                   activePolygon.authorityId &&
                               assignment.matchedPattern ==
                                   activePolygon.matchedPattern &&
                               xvatsim::core::authority::
                                       NormalizeControllerCallsign(
                                           assignment.callsign) ==
                                   xvatsim::core::authority::
                                       NormalizeControllerCallsign(
                                           activePolygon.callsign);
                    });
            }),
        liveMapBuild.activePolygons.end());
    snapshot.liveMap = liveMapBuild.map;
    const auto liveMapUnchanged =
        hasAuthorityRelevanceCache_ &&
        cachedAuthorityRelevanceSnapshot_.liveMap.stableHash != 0 &&
        cachedAuthorityRelevanceSnapshot_.liveMap.stableHash ==
            liveMapBuild.map.stableHash;

    if (!routeSectorSnapshot.available ||
        routeSectorSnapshot.stale ||
        !routeSectorSnapshot.routeResolved) {
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.controllerFeedGeneration = controllerFeedSnapshot.generation;
        snapshot.centerBoundaryGeneration = routeSectorSnapshot.centerBoundaryGeneration;
        snapshot.authorityCatalogGeneration = routeSectorSnapshot.authorityCatalogGeneration;
        snapshot.terminalCoverageGeneration = terminalBoundaryGeneration_;
        snapshot.diagnosticCacheStatus = "input-unavailable";
        snapshot.diagnosticReason = "route-snapshot-unavailable";
        snapshot.statusLine = "AUTHORITY route unavailable";
        snapshot.diagnostics = liveMapBuild.diagnostics;
        PopulateBasicAuthorityControllerEvidence(&snapshot, controllerFeedSnapshot);
        snapshot.evidence.source.cacheStatus = snapshot.diagnosticCacheStatus;
        snapshot.evidence.source.cacheReason = snapshot.diagnosticReason;
        FinalizeAuthorityRelevanceEvidenceGuardrails(&snapshot);
        return snapshot;
    }

    snapshot.available = true;
    snapshot.stale = false;
    snapshot.controllerFeedGeneration = controllerFeedSnapshot.generation;
    snapshot.centerBoundaryGeneration = routeSectorSnapshot.centerBoundaryGeneration;
    snapshot.authorityCatalogGeneration = routeSectorSnapshot.authorityCatalogGeneration;
    snapshot.terminalCoverageGeneration = terminalBoundaryGeneration_;
    const auto nowSeconds = CurrentTickSeconds();
    const auto remainingRouteDistanceNm = RouteDistanceNm(routeSectorSnapshot.waypoints);
    const auto routeProgressSignature =
        BuildAuthorityProgressRouteSignature(routeSectorSnapshot);
    const auto arrivalPrep =
        remainingRouteDistanceNm <= kAuthorityArrivalPrepDistanceNm;
    const auto targetAuthorityWindowNm =
        arrivalPrep
            ? std::max(kAuthorityNearRouteWindowNm, remainingRouteDistanceNm)
            : kAuthorityNearRouteWindowNm;
    if (authorityProgressRouteSignature_ != routeProgressSignature ||
        std::fabs(authorityProgressWindowNm_ - targetAuthorityWindowNm) > 1.0) {
        authorityProgressRouteSignature_ = routeProgressSignature;
        authorityProgressWindowNm_ = targetAuthorityWindowNm;
        lastAuthorityProgressTickSeconds_ = nowSeconds;
    }

    const auto workScope =
        BuildAuthorityRelevanceWorkScope(
            aircraftState,
            routeSectorSnapshot,
            authorityProgressWindowNm_);
    const auto operationalScopeSignature =
        BuildAuthorityOperationalScopeSignature(
            aircraftState,
            controllerFeedSnapshot,
            workScope,
            terminalBoundaryGeneration_);
    const auto progressSignatureMatchesLastProof =
        hasAuthorityRelevanceCache_ &&
        routeProgressSignature == lastAuthorityRelevanceProgressRouteSignature_ &&
        std::fabs(
            authorityProgressWindowNm_ -
            lastAuthorityRelevanceProgressWindowNm_) <= 1.0;
    if (hasAuthorityRelevanceCache_ &&
        liveMapUnchanged &&
        progressSignatureMatchesLastProof &&
        operationalScopeSignature == lastAuthorityOperationalScopeSignature_) {
        const auto watchCadenceSeconds =
            HasRelevantCenterAuthority(cachedAuthorityRelevanceSnapshot_)
                ? kAuthorityControlledWindowWatchCadenceSeconds
                : kAuthorityEmptyWindowWatchCadenceSeconds;
        if (lastAuthorityRelevanceBuildTickSeconds_ > 0 &&
            (nowSeconds - lastAuthorityRelevanceBuildTickSeconds_) <
                watchCadenceSeconds) {
            auto cachedSnapshot = cachedAuthorityRelevanceSnapshot_;
            cachedSnapshot.liveMap = liveMapBuild.map;
            cachedSnapshot.diagnosticCacheStatus = "authority-cache-cadence-hit";
            cachedSnapshot.diagnosticReason =
                HasRelevantCenterAuthority(cachedAuthorityRelevanceSnapshot_)
                    ? "controlled-window-watch-cadence"
                    : "empty-window-watch-cadence";
            cachedSnapshot.evidence.source.cacheHit = true;
            cachedSnapshot.evidence.source.cacheStatus =
                cachedSnapshot.diagnosticCacheStatus;
            cachedSnapshot.evidence.source.cacheReason =
                cachedSnapshot.diagnosticReason;
            FinalizeAuthorityRelevanceEvidenceGuardrails(&cachedSnapshot);
            return cachedSnapshot;
        }
    }
    const auto& scopeArtifacts =
        ResolveAuthorityRelevanceScopeArtifacts(
            &scopeArtifacts_,
            dataset.controllerAuthorityCatalog,
            dataset.authorityPolygonCatalog,
            aircraftState,
            controllerFeedSnapshot,
            workScope,
            terminalBoundaryGeneration_);
    if (cancellationRequested()) {
        return cancelledSnapshot();
    }
    // Activation is global and source-owned. Endpoint-local synthetic records
    // and route-scoped catalogs are query aids only; they may never light a
    // polygon or create authority.
    const auto& controllerAuthorityCatalog = dataset.controllerAuthorityCatalog;
    const auto& authorityPolygonCatalog = dataset.authorityPolygonCatalog;
    const auto& authorityPolygonExactIndexesByKey =
        scopeArtifacts.authorityPolygonExactIndexesByKey;
    const auto& authorityPolygonFamilyIndexesByKey =
        scopeArtifacts.authorityPolygonFamilyIndexesByKey;
    const auto& routeAuthorityPolygonKeys = scopeArtifacts.routeAuthorityPolygonKeys;
    const auto& routePoints = workScope.routePoints;
    const xvatsim::core::authority::GeoPoint aircraftPosition{
        aircraftState.latitudeDeg,
        aircraftState.longitudeDeg,
    };
    const auto& routeScopedAuthorityPolygonCatalog =
        scopeArtifacts.routeScopedAuthorityPolygonCatalog;
    const auto& routeAuthorityMatchKeys = scopeArtifacts.routeAuthorityMatchKeys;
    const auto& routeScopedControllerAuthorityCatalog =
        scopeArtifacts.routeScopedControllerAuthorityCatalog;
    const auto& routeScopedAuthorityPolygons =
        scopeArtifacts.routeScopedAuthorityPolygons;

    const auto currentStableProofEntries =
        BuildCurrentStableAuthorityProofEntries(
            controllerFeedSnapshot,
            routeScopedControllerAuthorityCatalog,
            routeAuthorityMatchKeys,
            nullptr);
    const auto cachedStableProofEntries =
        hasAuthorityRelevanceCache_
            ? BuildCachedStableAuthorityProofEntries(
                  cachedAuthorityRelevanceSnapshot_,
                  nullptr)
            : std::vector<std::string>{};
    const auto routeAuthorityKeysSorted =
        SortedAuthorityStrings(routeAuthorityPolygonKeys);
    const auto routeAuthorityMatchKeysSorted =
        SortedAuthorityStrings(routeAuthorityMatchKeys);
    auto cachedRouteAuthorityKeys =
        hasAuthorityRelevanceCache_
            ? cachedAuthorityRelevanceSnapshot_.evidence.source.routeAuthorityKeys
            : std::vector<std::string>{};
    std::sort(cachedRouteAuthorityKeys.begin(), cachedRouteAuthorityKeys.end());
    cachedRouteAuthorityKeys.erase(
        std::unique(cachedRouteAuthorityKeys.begin(), cachedRouteAuthorityKeys.end()),
        cachedRouteAuthorityKeys.end());
    const auto stableProofCovered =
        hasAuthorityRelevanceCache_ &&
        !currentStableProofEntries.empty() &&
        SortedVectorIncludes(cachedStableProofEntries, currentStableProofEntries);
    const auto routeKeysCovered =
        hasAuthorityRelevanceCache_ &&
        SortedVectorIncludes(cachedRouteAuthorityKeys, routeAuthorityKeysSorted);
    if (liveMapUnchanged && stableProofCovered && routeKeysCovered) {
        const auto routeWindowReuse = !progressSignatureMatchesLastProof;
        const auto cacheStatus =
            routeWindowReuse
                ? std::string("authority-cache-window-reuse")
                : std::string("authority-cache-stable-proof-reuse");
        const auto cacheReason =
            routeWindowReuse
                ? std::string("route-window-proof-subset")
                : std::string("stable-proof-ownership-unchanged");
        const auto refreshedSlice =
            BuildCurrentRelevantAuthoritySliceFromLiveMap(
                liveMapBuild.activePolygons,
                authorityPolygonCatalog,
                liveMapBuild.map,
                aircraftState.valid,
                aircraftPosition,
                routePoints);
        auto reusedSnapshot =
            BuildAuthorityProofReuseSnapshot(
                cachedAuthorityRelevanceSnapshot_,
                liveMapBuild.map,
                controllerFeedSnapshot,
                routeSectorSnapshot,
                workScope,
                routeAuthorityKeysSorted,
                routeAuthorityMatchKeysSorted,
                refreshedSlice,
                cacheStatus,
                cacheReason,
                terminalBoundaryGeneration_);
        if (!routeWindowReuse) {
            cachedAuthorityRelevanceSnapshot_ = reusedSnapshot;
            lastAuthorityRelevanceBuildTickSeconds_ = nowSeconds;
            lastAuthorityOperationalScopeSignature_ = operationalScopeSignature;
            lastAuthorityRelevanceProgressRouteSignature_ =
                routeProgressSignature;
            lastAuthorityRelevanceProgressWindowNm_ =
                authorityProgressWindowNm_;
        }
        return reusedSnapshot;
    }

    const auto watchInputSignature =
        BuildAuthorityWatchInputSignature(
            controllerFeedSnapshot,
            scopeArtifacts);
    if (hasAuthorityRelevanceCache_ &&
        liveMapUnchanged &&
        progressSignatureMatchesLastProof &&
        operationalScopeSignature == lastAuthorityOperationalScopeSignature_ &&
        watchInputSignature == lastAuthorityWatchInputSignature_) {
        lastAuthorityRelevanceBuildTickSeconds_ = nowSeconds;
        auto cachedSnapshot = cachedAuthorityRelevanceSnapshot_;
        cachedSnapshot.liveMap = liveMapBuild.map;
        cachedSnapshot.diagnosticCacheStatus = "authority-cache-watch-hit";
        cachedSnapshot.diagnosticReason = "watch-input-unchanged";
        cachedSnapshot.evidence.source.cacheHit = true;
        cachedSnapshot.evidence.source.cacheStatus =
            cachedSnapshot.diagnosticCacheStatus;
        cachedSnapshot.evidence.source.cacheReason =
            cachedSnapshot.diagnosticReason;
        FinalizeAuthorityRelevanceEvidenceGuardrails(&cachedSnapshot);
        return cachedSnapshot;
    }

    auto relevanceSignature =
        BuildScopedAuthorityRelevanceSignature(
            controllerFeedSnapshot,
            workScope.routeSectorSnapshot,
            routeAuthorityMatchKeys);
    AddAuthorityWorkScopeSignature(
        &relevanceSignature,
        aircraftState,
        workScope,
        terminalBoundaryGeneration_);
    const auto signatureChanged =
        !hasAuthorityRelevanceCache_ ||
        !progressSignatureMatchesLastProof ||
        relevanceSignature != lastAuthorityRelevanceSignature_ ||
        operationalScopeSignature != lastAuthorityOperationalScopeSignature_;
    if (liveMapUnchanged && !signatureChanged) {
        lastAuthorityRelevanceBuildTickSeconds_ = nowSeconds;
        lastAuthorityWatchInputSignature_ = watchInputSignature;
        auto cachedSnapshot = cachedAuthorityRelevanceSnapshot_;
        cachedSnapshot.liveMap = liveMapBuild.map;
        cachedSnapshot.diagnosticCacheStatus = "authority-cache-signature-hit";
        cachedSnapshot.diagnosticReason = "scoped-signature-unchanged";
        cachedSnapshot.evidence.source.cacheHit = true;
        cachedSnapshot.evidence.source.cacheStatus =
            cachedSnapshot.diagnosticCacheStatus;
        cachedSnapshot.evidence.source.cacheReason =
            cachedSnapshot.diagnosticReason;
        FinalizeAuthorityRelevanceEvidenceGuardrails(&cachedSnapshot);
        return cachedSnapshot;
    }

    snapshot.diagnosticCacheStatus = "authority-proof-build";
    snapshot.diagnosticReason =
        !hasAuthorityRelevanceCache_
            ? "no-authority-cache"
            : (!progressSignatureMatchesLastProof ? "route-progress-dirty"
                                                  : "signature-or-source-dirty");
    snapshot.diagnosticWorkStage = workScope.stage;
    snapshot.diagnosticWindowNm = workScope.windowNm;
    snapshot.diagnosticDeferredSectorCount = workScope.deferredSectorCount;
    snapshot.evidence.source.workStage = workScope.stage;
    snapshot.evidence.source.workWindowNm = workScope.windowNm;
    snapshot.evidence.source.workDeferredSectorCount =
        workScope.deferredSectorCount;
    snapshot.evidence.source.cacheHit = false;
    snapshot.evidence.source.cacheStatus = snapshot.diagnosticCacheStatus;
    snapshot.evidence.source.cacheReason = snapshot.diagnosticReason;
    snapshot.evidence.source.routeAuthorityKeys =
        SortedAuthorityStrings(routeAuthorityPolygonKeys);
    snapshot.evidence.source.routeAuthorityMatchKeys =
        SortedAuthorityStrings(routeAuthorityMatchKeys);

    std::unordered_set<std::string> routeEndpointAuthorityKeys;
    AddRouteEndpointAuthorityKeys(
        workScope.routeSectorSnapshot,
        workScope.includeDepartureEndpoint,
        workScope.includeDestinationEndpoint,
        &routeEndpointAuthorityKeys);
    const auto queryScopedPolygonIds =
        BuildAuthorityPolygonIdSet(routeScopedAuthorityPolygonCatalog);
    const auto scopedPolygonsById =
        BuildRouteScopedAuthorityPolygonsById(routeScopedAuthorityPolygons);
    snapshot.evidence.polygonEvidence.reserve(
        authorityPolygonCatalog.polygons.size());
    for (const auto& polygon : authorityPolygonCatalog.polygons) {
        const auto routeKeyMatch =
            AuthorityPolygonMatchesRouteKeys(polygon, routeAuthorityPolygonKeys);
        const auto routeFamilyMatch =
            !routeKeyMatch &&
            AuthorityPolygonFamilyMatchesRouteKeys(
                polygon,
                routeAuthorityPolygonKeys);
        const auto routeEndpointMatch =
            AuthorityPolygonMatchesRouteKeys(polygon, routeEndpointAuthorityKeys);
        if (!routeKeyMatch &&
            !routeFamilyMatch &&
            !routeEndpointMatch &&
            queryScopedPolygonIds.find(polygon.id) == queryScopedPolygonIds.end()) {
            continue;
        }
        auto polygonEvidence = BuildAuthorityPolygonEvidence(
            polygon,
            routeAuthorityPolygonKeys,
            routeEndpointAuthorityKeys,
            queryScopedPolygonIds,
            scopedPolygonsById);
        snapshot.evidence.polygonEvidence.push_back(std::move(polygonEvidence));
    }

    auto activeAuthorityPolygons = liveMapBuild.activePolygons;
    snapshot.diagnostics = liveMapBuild.diagnostics;
    std::unordered_set<std::string> insertedEvidenceActiveKeys;
    const auto candidateControllers = liveMapBuild.candidateControllers;
    const auto activationDataGaps = liveMapBuild.activationDataGaps;
    for (const auto& controller : controllerFeedSnapshot.Controllers()) {
        if (cancellationRequested()) {
            return cancelledSnapshot();
        }
        const auto isAirportLocalCandidate =
            IsAirportLocalControllerCandidate(controller);
        const auto isAirspaceAuthorityCandidate =
            IsAuthorityControllerCandidate(controller);
        auto controllerEvidence = BuildAuthorityControllerEvidence(
            controller,
            isAirportLocalCandidate,
            isAirspaceAuthorityCandidate);
        bool controllerEvidenceRecorded = false;
        auto recordControllerEvidence = [&]() {
            if (controllerEvidenceRecorded) {
                return;
            }
            snapshot.evidence.controllerEvidence.push_back(
                std::move(controllerEvidence));
            controllerEvidenceRecorded = true;
        };
        if (!isAirspaceAuthorityCandidate && !isAirportLocalCandidate) {
            recordControllerEvidence();
            continue;
        }

        const auto normalizedCallsign =
            xvatsim::core::authority::NormalizeControllerCallsign(controller.callsign);

        const auto authorityDecisions =
            xvatsim::core::authority::EvaluateControllerAuthority(
                controllerAuthorityCatalog,
                controller.callsign,
                controller.frequency,
                controller.facility);
        const auto routeScopedDecisions =
            FilterAuthorityDecisionsToRouteKeys(
                authorityDecisions,
                routeAuthorityMatchKeys);
        const auto terminalRejectionDecisions =
            FilterTerminalAuthorityRejectionDecisions(authorityDecisions);
        controllerEvidence.authorityDecisions =
            BuildAuthorityDecisionEvidenceList(
                authorityDecisions,
                routeScopedDecisions);
        if (authorityDecisions.empty()) {
            if (isAirportLocalCandidate && !isAirspaceAuthorityCandidate) {
                controllerEvidence.evidenceReasons.push_back(
                    "airport-local-no-authority-decision");
                recordControllerEvidence();
                continue;
            }
            controllerEvidence.evidenceReasons.push_back("unmapped-controller");
            recordControllerEvidence();
            continue;
        }

        const auto hasAcceptedAuthorityDecision =
            std::any_of(
                authorityDecisions.begin(),
                authorityDecisions.end(),
                [](const auto& decision) { return decision.accepted; });
        if (routeScopedDecisions.empty() &&
            !hasAcceptedAuthorityDecision &&
            terminalRejectionDecisions.empty()) {
            controllerEvidence.evidenceReasons.push_back(
                "no-route-scoped-authority-decision");
            recordControllerEvidence();
            continue;
        }

        const auto activationResult =
            xvatsim::core::authority::ActivateAuthorityPolygons(
                controllerAuthorityCatalog,
                authorityPolygonCatalog,
                controller.callsign,
                controller.frequency,
                controller.facility);
        for (const auto& activePolygon : activationResult.activePolygons) {
            auto activeEvidence = BuildActiveAuthorityPolygonEvidence(
                activePolygon,
                authorityPolygonCatalog,
                routeAuthorityPolygonKeys);
            controllerEvidence.activePolygons.push_back(activeEvidence);
            const auto activeKey = ActiveAuthorityKey(activePolygon);
            if (!insertedEvidenceActiveKeys.insert(activeKey).second) {
                activeEvidence.filteredReason = "duplicate-active-key";
                snapshot.evidence.activePolygonEvidence.push_back(
                    std::move(activeEvidence));
                continue;
            }
            UpsertActiveAuthorityPolygonEvidence(
                &snapshot.evidence.activePolygonEvidence,
                std::move(activeEvidence));
        }
        recordControllerEvidence();
    }

    const auto relevantAuthorityPolygons =
        xvatsim::core::authority::ResolveRelevantAuthorityPolygons(
            activeAuthorityPolygons,
            authorityPolygonCatalog,
            aircraftState.valid,
            aircraftPosition,
            routePoints);

    std::unordered_set<std::string> relevantActiveKeys;
    for (const auto& relevantAuthorityPolygon : relevantAuthorityPolygons) {
        relevantActiveKeys.insert(
            ActiveAuthorityKey(relevantAuthorityPolygon.activePolygon));
        for (auto& assignment : snapshot.liveMap.assignments) {
            if (!brain::IsActiveLiveAuthorityState(assignment.state) ||
                assignment.canonicalPolygonId !=
                    relevantAuthorityPolygon.activePolygon.polygonId ||
                xvatsim::core::authority::NormalizeControllerCallsign(
                    assignment.callsign) !=
                    xvatsim::core::authority::NormalizeControllerCallsign(
                        relevantAuthorityPolygon.activePolygon.callsign)) {
                continue;
            }
            assignment.aircraftInside =
                relevantAuthorityPolygon.aircraftInside;
            assignment.routeIntersects =
                relevantAuthorityPolygon.routeIntersects;
            assignment.routeEntryDistanceNm =
                relevantAuthorityPolygon.routeEntryDistanceNm;
        }
    }
    MarkAuthorityEvidenceRelevantSurvivors(
        &snapshot,
        relevantAuthorityPolygons);
    for (const auto& activePolygon : activeAuthorityPolygons) {
        if (relevantActiveKeys.find(ActiveAuthorityKey(activePolygon)) !=
            relevantActiveKeys.end()) {
            continue;
        }
        AppendAuthorityDiagnostic(
            &snapshot.diagnostics,
            activePolygon.callsign + ":active-not-relevant:" +
                activePolygon.authorityId + ":" +
                activePolygon.polygonId + ":" +
                AuthorityKindLabel(activePolygon.kind));
    }

    SortUniqueStrings(&snapshot.diagnostics);
    std::ostringstream status;
    status << "AUTHORITY candidates=" << candidateControllers
           << " authorities=" << controllerAuthorityCatalog.authorities.size()
           << " polygons=" << authorityPolygonCatalog.polygons.size()
           << " routeKeys=" << routeAuthorityPolygonKeys.size()
           << " active=" << activeAuthorityPolygons.size()
           << " relevant=" << relevantAuthorityPolygons.size()
           << " diagnostics=" << snapshot.diagnostics.size()
           << " activationGaps=" << activationDataGaps;
    if (workScope.deferredSectorCount > 0 ||
        workScope.stage == "MID_ROUTE_QUEUE" ||
        workScope.stage == "FULL_ROUTE_CACHE") {
        status << " stage=" << workScope.stage
               << " window=" << static_cast<int>(std::round(workScope.windowNm)) << "nm"
               << " deferred=" << workScope.deferredSectorCount;
    }
    snapshot.statusLine = status.str();
    snapshot.evidence.source.cacheStatus = snapshot.diagnosticCacheStatus;
    snapshot.evidence.source.cacheReason = snapshot.diagnosticReason;
    FinalizeAuthorityRelevanceEvidenceGuardrails(&snapshot);
    // Brain publishes the relevance view directly from the canonical live map.
    const auto brainAuthorityPreview =
        brain::BuildBrainAuthorityRelevanceDecisionPreview(snapshot);
    snapshot =
        brain::BuildBrainOwnedAuthorityRelevanceSnapshot(
            std::move(snapshot),
            brainAuthorityPreview);
    cachedAuthorityRelevanceSnapshot_ = snapshot;
    hasAuthorityRelevanceCache_ = true;
    lastAuthorityRelevanceBuildTickSeconds_ = CurrentTickSeconds();
    lastAuthorityRelevanceLatitudeDeg_ = aircraftState.latitudeDeg;
    lastAuthorityRelevanceLongitudeDeg_ = aircraftState.longitudeDeg;
    lastAuthorityRelevanceSignature_ = relevanceSignature;
    lastAuthorityOperationalScopeSignature_ = operationalScopeSignature;
    lastAuthorityWatchInputSignature_ = watchInputSignature;
    lastAuthorityRelevanceProgressRouteSignature_ = routeProgressSignature;
    lastAuthorityRelevanceProgressWindowNm_ = authorityProgressWindowNm_;
    return snapshot;
}

brain::RouteSectorSnapshot
RouteSectorResolver::ResolveSynchronousRouteOracleForTesting(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::NetworkPlanSnapshot& networkPlanSnapshot) const {
    if (networkPlanSnapshot.stale) {
        brain::RouteSectorSnapshot snapshot;
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.diagnosticCacheStatus = "input-stale";
        snapshot.diagnosticReason = "network-plan-stale";
        snapshot.statusLine = networkPlanSnapshot.statusLine.empty()
            ? "ROUTE plan stale"
            : "ROUTE plan stale: " + networkPlanSnapshot.statusLine;
        return snapshot;
    }
    if (!hasBoundaryCache_) {
        brain::RouteSectorSnapshot snapshot;
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.diagnosticCacheStatus = "source-unavailable";
        snapshot.diagnosticReason = "boundary-cache-unavailable";
        snapshot.statusLine = "ROUTE sectors unavailable";
        return snapshot;
    }
    RoutePreparationEngineState frozenOracleState;
    frozenOracleState.xplaneRootPath = GetXPlaneRootPath();
    frozenOracleState.source =
        std::make_shared<const LocalRoutePreparationSource>(false);
    ScopedRoutePreparationState frozenOracleScope(&frozenOracleState);
    auto snapshot =
        BuildSnapshotFrozenOracle(aircraftState, networkPlanSnapshot);
    if (snapshot.diagnosticCacheStatus.empty()) {
        if (snapshot.statusLine.find("via preflight cache") !=
            std::string::npos) {
            snapshot.diagnosticCacheStatus = "preflight-route-cache-build";
        } else if (snapshot.statusLine.find("via expanded FMS plan") !=
                   std::string::npos) {
            snapshot.diagnosticCacheStatus = "expanded-fms-route-build";
        } else {
            snapshot.diagnosticCacheStatus = "route-rebuild";
        }
    }
    if (snapshot.diagnosticReason.empty()) {
        snapshot.diagnosticReason = "route-key-changed";
    }
    snapshot.stale = false;
    if (snapshot.statusLine.empty()) {
        snapshot.statusLine = snapshot.routeResolved
            ? "ROUTE sectors active"
            : "ROUTE pending";
    }
    return snapshot;
}

brain::AuthorityRelevanceSnapshot
RouteSectorResolver::ResolveBrainScheduledAuthorityVerification(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const brain::RouteSectorSnapshot& routeSectorSnapshot,
    const std::string& scheduleReason,
    std::shared_ptr<const brain::VnasLiveOwnershipSnapshot>
        dynamicOwnership) const {
    const auto* authorityDataset = GetAuthoritySourceDataset();
    if (authorityDataset == nullptr || authorityEngine_ == nullptr) {
        brain::AuthorityRelevanceSnapshot snapshot;
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.controllerFeedGeneration = controllerFeedSnapshot.generation;
        snapshot.centerBoundaryGeneration = routeSectorSnapshot.centerBoundaryGeneration;
        snapshot.authorityCatalogGeneration = routeSectorSnapshot.authorityCatalogGeneration;
        snapshot.terminalCoverageGeneration = terminalBoundaryGeneration_;
        snapshot.diagnosticCacheStatus = "authority-dataset-unavailable";
        snapshot.diagnosticReason = "immutable-source-dataset-missing";
        snapshot.statusLine = "AUTHORITY source dataset unavailable";
        return snapshot;
    }
    const auto authorityControllerProjection =
        brain::BuildBrainAuthorityControllerEvidence(
            controllerFeedSnapshot.Controllers());
    const auto authorityControllers =
        brain::ExpandBrainAuthorityControllerEvidence(
            authorityControllerProjection);
    auto canonicalControllerFeed = controllerFeedSnapshot;
    canonicalControllerFeed.controllers =
        controllerFeedSnapshot.controllers != nullptr
            ? &authorityControllers
            : nullptr;
    canonicalControllerFeed.ownedControllers.reset();

    auto snapshot = authorityEngine_->Evaluate(
        *authorityDataset,
        terminalBoundaryGeneration_,
        aircraftState,
        canonicalControllerFeed,
        dynamicOwnership.get(),
        routeSectorSnapshot,
        scheduleReason);
    LogAuthorityDiagnosticsIfChanged(snapshot);
    return snapshot;
}

std::string RouteSectorResolver::BuildAuthorityDiagnosticGeoJson(
    const brain::AuthorityRelevanceSnapshot& snapshot) const {
    const auto* authorityDataset = GetAuthoritySourceDataset();
    if (authorityDataset == nullptr ||
        snapshot.liveMap.canonicalMapIdentity == 0 ||
        snapshot.liveMap.canonicalMapIdentity != authorityDataset->identity) {
        return {};
    }
    return BuildCanonicalAuthorityDiagnosticGeoJson(
        *authorityDataset,
        snapshot);
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
        snapshot.diagnosticCacheStatus = "input-waiting";
        snapshot.diagnosticReason = "airport-coordinates-missing";
        snapshot.statusLine = "AIRPORT sectors waiting";
        return snapshot;
    }

    if (!hasBoundaryCache_ && !hasTerminalBoundaryCache_) {
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.diagnosticCacheStatus =
            fetchInProgress_ ? "source-pending" : "source-unavailable";
        snapshot.diagnosticReason = "airport-boundary-cache-unavailable";
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
            snapshot.diagnosticCacheStatus = "airport-coverage-cache-hit";
            snapshot.diagnosticReason = "airport-key-and-source-unchanged";
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
    snapshot.diagnosticCacheStatus = "airport-coverage-build";
    snapshot.diagnosticReason = "cache-miss-or-source-changed";
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

brain::BrainEndpointPolygonQueryOutput
RouteSectorResolver::QueryEndpointPolygons(
    const brain::BrainEndpointPolygonQueryInput& input) {
    const auto started = std::chrono::steady_clock::now();
    brain::BrainEndpointPolygonQueryOutput output;
    output.airportIcao = NormalizeAirportIcao(input.airportIcao);
    output.source = "CANONICAL_AUTHORITY_MAP";

    if (!input.hasAirportCoordinates || output.airportIcao.empty()) {
        output.pending = true;
        output.status = "canonical-authority-endpoint-waiting";
        output.cacheStatus = "endpoint-coordinates-missing";
        output.lookupUs = std::chrono::duration_cast<std::chrono::microseconds>(
                              std::chrono::steady_clock::now() - started)
                              .count();
        return output;
    }

    (void)RefreshBoundariesIfNeeded();
    const auto publication = GetAuthoritySourceDatasetPublication();
    if (publication.dataset == nullptr) {
        output.stale = true;
        output.status = "canonical-authority-map-unavailable";
        output.cacheStatus = "authority-dataset-unavailable";
        output.lookupUs = std::chrono::duration_cast<std::chrono::microseconds>(
                              std::chrono::steady_clock::now() - started)
                              .count();
        return output;
    }

    output.available = true;
    output.resolved = true;
    output.sourceGeneration = publication.terminalBoundaryGeneration;
    output.stale = !IsTerminalBoundaryCacheFresh(CurrentTickSeconds());
    output.cacheStatus = "canonical-authority-endpoint-query";
    const auto endpointTokens = BuildAirportLocalTokens(output.airportIcao);
    const xvatsim::core::authority::GeoPoint airportPoint{
        input.airportLatitudeDeg,
        input.airportLongitudeDeg,
    };

    std::vector<const xvatsim::core::authority::AuthorityPolygon*> anchors;
    for (const auto& polygon :
         publication.dataset->authorityPolygonCatalog.polygons) {
        if (polygon.kind !=
            xvatsim::core::authority::AuthorityKind::Terminal) {
            continue;
        }
        if (xvatsim::core::authority::AuthorityPolygonContainsPoint(
                polygon, airportPoint)) {
            anchors.push_back(&polygon);
        }
    }

    for (const auto& polygon :
         publication.dataset->authorityPolygonCatalog.polygons) {
        if (polygon.kind !=
            xvatsim::core::authority::AuthorityKind::Terminal) {
            continue;
        }
        const auto containsAirport = std::find(
            anchors.begin(), anchors.end(), &polygon) != anchors.end();
        auto endpointContext = containsAirport;
        if (!endpointContext) {
            endpointContext = std::any_of(
                anchors.begin(),
                anchors.end(),
                [&](const auto* anchor) {
                    return anchor != nullptr &&
                           xvatsim::core::authority::AuthorityPolygonsIntersect(
                               polygon, *anchor);
                });
        }
        if (!endpointContext) {
            continue;
        }

        output.polygons.push_back(BuildCanonicalTerminalPolygonFact(
            polygon, containsAirport, endpointContext));
        if (!containsAirport) {
            continue;
        }
        AppendUniqueTerminalToken(
            &output.polygonKeys,
            !polygon.sourceFeatureId.empty()
                ? polygon.sourceFeatureId
                : polygon.polygonKey);

        std::vector<std::string> roleSuffixes;
        for (const auto role : polygon.serviceRoles) {
            AppendUniqueTerminalToken(
                &roleSuffixes,
                role ==
                        xvatsim::core::authority::AuthorityServiceRole::Departure
                    ? "DEP"
                    : "APP");
        }
        if (roleSuffixes.empty()) {
            AppendUniqueTerminalToken(&roleSuffixes, "APP");
        }
        const auto serviceBase = !polygon.sourceFeatureId.empty()
            ? polygon.sourceFeatureId
            : polygon.polygonKey;
        for (const auto& suffix : roleSuffixes) {
            const auto serviceToken =
                TerminalServiceToken(serviceBase, suffix);
            if (TerminalServiceMatchesEndpoint(
                    serviceToken, endpointTokens, serviceBase)) {
                AppendUniqueTerminalToken(&output.ownerTokens, serviceToken);
            }
        }
        for (const auto& lookupKey : polygon.lookupKeys) {
            const auto normalized = NormalizeAuthorityCatalogKey(lookupKey);
            if (normalized ==
                NormalizeAuthorityCatalogKey(polygon.polygonKey)) {
                continue;
            }
            const auto separator = normalized.rfind('_');
            if (separator == std::string::npos ||
                separator >= normalized.size() - 1 ||
                !IsTerminalServiceSuffix(
                    normalized.substr(separator + 1))) {
                continue;
            }
            if (TerminalServiceMatchesEndpoint(
                    normalized, endpointTokens, serviceBase)) {
                AppendUniqueTerminalToken(
                    &output.ownerTokens, normalized);
            }
        }
    }

    std::sort(output.ownerTokens.begin(), output.ownerTokens.end());
    output.ownerTokens.erase(
        std::unique(output.ownerTokens.begin(), output.ownerTokens.end()),
        output.ownerTokens.end());
    std::sort(output.polygonKeys.begin(), output.polygonKeys.end());
    output.polygonKeys.erase(
        std::unique(output.polygonKeys.begin(), output.polygonKeys.end()),
        output.polygonKeys.end());
    output.status = output.ownerTokens.empty()
        ? "canonical-authority-no-containing-terminal"
        : "canonical-authority-owner-resolved";
    output.lookupUs = std::chrono::duration_cast<std::chrono::microseconds>(
                          std::chrono::steady_clock::now() - started)
                          .count();
    return output;
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
            traversalPolygon.holes.reserve(polygon.holes.size());
            for (const auto& sourceHole : polygon.holes) {
                std::vector<xvatsim::core::route::GeoPoint> hole;
                hole.reserve(sourceHole.size());
                for (const auto& point : sourceHole) {
                    hole.push_back({point.latitudeDeg, point.longitudeDeg});
                }
                traversalPolygon.holes.push_back(std::move(hole));
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

void PopulateEndpointAirportCallsignAliases(
    brain::RouteSectorSnapshot* snapshot,
    const ControllerAuthorityCatalog& authorityCatalog) {
    if (snapshot == nullptr) return;
    snapshot->airportCallsignAliases.clear();
    const auto departure = NormalizeAirportIcao(snapshot->departureIcao);
    const auto arrival = NormalizeAirportIcao(snapshot->destinationIcao);
    for (const auto& alias : authorityCatalog.airportCallsignAliases) {
        if (alias.airportIcao != departure && alias.airportIcao != arrival) continue;
        brain::AirportCallsignAliasSnapshot fact;
        fact.airportIcao = alias.airportIcao;
        fact.callsignPrefix = alias.callsignPrefix;
        fact.boundaryId = alias.boundaryId;
        fact.source = "VATSPY_AIRPORT";
        fact.sourceRecord = alias.sourceRecord;
        snapshot->airportCallsignAliases.push_back(std::move(fact));
    }
}

std::shared_ptr<RouteSourceDataset> BuildRouteSourceDataset(
    const std::vector<unsigned char>& boundaryPayload,
    const std::vector<unsigned char>& authorityCatalogPayload,
    const std::vector<unsigned char>& ownershipPayload) {
    if (boundaryPayload.empty() || authorityCatalogPayload.empty()) {
        return {};
    }

    auto dataset = std::make_shared<RouteSourceDataset>();
    std::uint64_t identity = 1469598103934665603ULL;
    HashDatasetBytes(&identity, boundaryPayload);
    HashDatasetBytes(&identity, authorityCatalogPayload);
    HashDatasetBytes(&identity, ownershipPayload);
    dataset->identity = identity == 0 ? 1 : identity;

    const auto& features = GetCachedSectorFeatures(boundaryPayload);
    const auto& authorityCatalog =
        GetCachedControllerAuthorityCatalog(
            authorityCatalogPayload,
            ownershipPayload);
    dataset->traversalFeatures = ToTraversalFeatures(features);
    dataset->controllerAuthorityCatalog = authorityCatalog;
    dataset->retainedBytes = sizeof(RouteSourceDataset);
    for (auto& feature : dataset->traversalFeatures) {
        dataset->retainedBytes += sizeof(feature) + feature.label.capacity();
        for (const auto& token : feature.tokens) {
            dataset->retainedBytes += token.capacity();
        }
        for (const auto& pattern : feature.controllerCallsignPatterns) {
            dataset->retainedBytes += pattern.capacity();
        }
        for (const auto& prefix : feature.controllerPrefixes) {
            dataset->retainedBytes += prefix.capacity();
        }
        for (const auto& polygon : feature.polygons) {
            dataset->retainedBytes +=
                sizeof(polygon) + polygon.ring.capacity() * sizeof(
                    xvatsim::core::route::GeoPoint);
            dataset->retainedBytes += polygon.holes.capacity() * sizeof(
                std::vector<xvatsim::core::route::GeoPoint>);
            for (const auto& hole : polygon.holes) {
                dataset->retainedBytes += hole.capacity() * sizeof(
                    xvatsim::core::route::GeoPoint);
            }
        }
    }
    return dataset;
}

namespace {

bool RouteCancellationRequested(const RouteCancellationToken* cancellation) {
    return cancellation != nullptr &&
           cancellation->IsCancellationRequested();
}

brain::RouteSectorSnapshot CancelledRouteSnapshot() {
    brain::RouteSectorSnapshot snapshot;
    snapshot.available = false;
    snapshot.stale = true;
    snapshot.diagnosticCacheStatus = "route-worker-cancelled";
    snapshot.diagnosticReason = "cooperative-cancellation";
    snapshot.statusLine = "ROUTE preparation cancelled";
    return snapshot;
}

brain::RouteSectorSnapshot PrepareRouteSnapshotCore(
    RoutePreparationEngineState* state,
    const RoutePreparationRequest& request,
    RouteWorkerPhaseTimings* timings,
    const RouteCancellationToken* cancellation) {
    const auto totalStarted = std::chrono::steady_clock::now();
    const auto finish = [&](brain::RouteSectorSnapshot snapshot) {
        if (timings != nullptr) {
            timings->totalUs = std::chrono::duration_cast<
                std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - totalStarted)
                                   .count();
        }
        return snapshot;
    };
    if (RouteCancellationRequested(cancellation)) {
        return finish(CancelledRouteSnapshot());
    }

    brain::RouteSectorSnapshot snapshot;
    const auto sourceStarted = std::chrono::steady_clock::now();
    const auto* dataset = request.routeSourceDataset.get();
    const auto* networkPlan = request.networkPlan.get();
    if (timings != nullptr) {
        timings->sourceAcquisitionUs = std::chrono::duration_cast<
            std::chrono::microseconds>(
                std::chrono::steady_clock::now() - sourceStarted)
                                            .count();
    }
    if (dataset == nullptr || networkPlan == nullptr) {
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.diagnosticCacheStatus = "source-unavailable";
        snapshot.diagnosticReason = "boundary-cache-unavailable";
        snapshot.statusLine = "ROUTE sectors unavailable";
        return finish(std::move(snapshot));
    }
    if (networkPlan->stale) {
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.diagnosticCacheStatus = "input-stale";
        snapshot.diagnosticReason = "network-plan-stale";
        snapshot.statusLine = networkPlan->statusLine.empty()
                                  ? "ROUTE plan stale"
                                  : "ROUTE plan stale: " +
                                        networkPlan->statusLine;
        return finish(std::move(snapshot));
    }

    snapshot.available = true;
    snapshot.stale = false;
    snapshot.departureIcao = networkPlan->departureIcao;
    snapshot.destinationIcao = networkPlan->destinationIcao;
    snapshot.centerBoundaryGeneration = dataset->centerBoundaryGeneration;
    snapshot.authorityCatalogGeneration =
        dataset->authorityCatalogGeneration;
    PopulateEndpointAirportCallsignAliases(
        &snapshot, dataset->controllerAuthorityCatalog);
    if (!request.routeAnchor.valid ||
        !networkPlan->hasDestinationCoordinates) {
        snapshot.statusLine = "ROUTE waiting for destination";
        snapshot.diagnosticCacheStatus = "route-rebuild";
        snapshot.diagnosticReason = "route-key-changed";
        return finish(std::move(snapshot));
    }
    if (dataset->traversalFeatures.empty()) {
        snapshot.statusLine = "ROUTE sectors unavailable";
        snapshot.diagnosticCacheStatus = "route-rebuild";
        snapshot.diagnosticReason = "route-key-changed";
        return finish(std::move(snapshot));
    }

    ScopedRoutePreparationState activeState(state);
    const auto navigationStarted = std::chrono::steady_clock::now();
    (void)GetAirwayGraph();
    if (timings != nullptr) {
        timings->navigationInitializationUs = std::chrono::duration_cast<
            std::chrono::microseconds>(
                std::chrono::steady_clock::now() - navigationStarted)
                                                   .count();
    }
    if (RouteCancellationRequested(cancellation)) {
        return finish(CancelledRouteSnapshot());
    }

    RouteResolveDiagnostics diagnostics;
    bool usedPreflightRouteCache = false;
    bool usedExpandedFmsRouteCache = false;
    const auto preflightStarted = std::chrono::steady_clock::now();
    if (request.preflightCandidate != nullptr) {
        const auto cacheValidation = state != nullptr &&
                state->source != nullptr
            ? state->source->ValidatePreflightCandidate(
                  *request.preflightCandidate, *networkPlan)
            : core::preflight::CacheValidationResult{};
        if (cacheValidation.accepted) {
            snapshot.waypoints =
                core::preflight::BuildRouteWaypointsFromCache(
                    *request.preflightCandidate);
            usedPreflightRouteCache = snapshot.waypoints.size() >= 2;
            diagnostics.resolvedTokens.push_back(
                "PREFLIGHT_CACHE:" +
                request.preflightCandidate->plan.routeIdentityHash);
            diagnostics.procedureMetadataSources.push_back(
                cacheValidation.reason.empty()
                    ? "PREFLIGHT_ROUTE_CACHE"
                    : "PREFLIGHT_ROUTE_CACHE:" +
                          cacheValidation.reason);
        }
    }
    if (timings != nullptr) {
        timings->preflightValidationUs = std::chrono::duration_cast<
            std::chrono::microseconds>(
                std::chrono::steady_clock::now() - preflightStarted)
                                            .count();
    }
    if (RouteCancellationRequested(cancellation)) {
        return finish(CancelledRouteSnapshot());
    }

    if (!usedPreflightRouteCache) {
        const auto fmsStarted = std::chrono::steady_clock::now();
        if (auto expandedFmsRoute =
                TryBuildExpandedFmsRouteCache(*networkPlan, cancellation);
            expandedFmsRoute.has_value()) {
            snapshot.waypoints =
                core::preflight::BuildRouteWaypointsFromCache(
                    expandedFmsRoute->cache);
            usedExpandedFmsRouteCache = snapshot.waypoints.size() >= 2;
            if (usedExpandedFmsRouteCache) {
                diagnostics.resolvedTokens.push_back(
                    "EXPANDED_FMS:" +
                    expandedFmsRoute->cache.plan.routeIdentityHash);
                diagnostics.procedureMetadataSources.push_back(
                    "EXPANDED_FMS:" + expandedFmsRoute->sourceName);
            }
        }
        if (timings != nullptr) {
            timings->fmsDiscoveryUs = std::chrono::duration_cast<
                std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - fmsStarted)
                                          .count();
        }
    }
    if (RouteCancellationRequested(cancellation)) {
        return finish(CancelledRouteSnapshot());
    }

    if (!usedPreflightRouteCache && !usedExpandedFmsRouteCache) {
        const auto waypointStarted = std::chrono::steady_clock::now();
        snapshot.waypoints = ResolveRouteWaypoints(
            request.routeAnchor,
            *networkPlan,
            &diagnostics,
            timings,
            cancellation);
        if (timings != nullptr) {
            const auto fullResolutionUs = std::chrono::duration_cast<
                std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - waypointStarted)
                                                .count();
            timings->waypointResolutionUs = std::max<long long>(
                0,
                fullResolutionUs - timings->procedureMetadataUs -
                    timings->grammarParsingUs);
        }
    }
    if (RouteCancellationRequested(cancellation)) {
        return finish(CancelledRouteSnapshot());
    }

    snapshot.routeResolved = snapshot.waypoints.size() >= 2;
    if (snapshot.routeResolved &&
        !usedPreflightRouteCache &&
        !usedExpandedFmsRouteCache &&
        !diagnostics.unresolvedAirwayTokens.empty()) {
        snapshot.routeResolved = false;
        snapshot.statusLine = AppendUnsupportedRouteStatus(
            "ROUTE incomplete unresolved-airways " +
                std::to_string(diagnostics.unresolvedAirwayTokens.size()),
            diagnostics);
        snapshot.diagnosticCacheStatus = "route-rebuild";
        snapshot.diagnosticReason = "route-key-changed";
        return finish(std::move(snapshot));
    }
    if (!snapshot.routeResolved) {
        snapshot.statusLine =
            AppendUnsupportedRouteStatus("ROUTE unresolved", diagnostics);
        snapshot.diagnosticCacheStatus = "route-rebuild";
        snapshot.diagnosticReason = "route-key-changed";
        return finish(std::move(snapshot));
    }

    const auto traversalPreparationStarted =
        std::chrono::steady_clock::now();
    xvatsim::core::route::TraversalTuning traversalTuning;
    traversalTuning.mode = xvatsim::core::route::TraversalMode::Exact;
    traversalTuning.routeSectorSanityLimit = kRouteSectorSanityLimit;
    if (timings != nullptr) {
        timings->traversalPreparationUs = std::chrono::duration_cast<
            std::chrono::microseconds>(
                std::chrono::steady_clock::now() -
                traversalPreparationStarted)
                                               .count();
    }
    const auto traversalStarted = std::chrono::steady_clock::now();
    auto traversalSnapshot =
        xvatsim::core::route::BuildRouteSectorSnapshotFromWaypoints(
            snapshot.waypoints,
            dataset->traversalFeatures,
            traversalTuning);
    if (timings != nullptr) {
        timings->polygonTraversalUs = std::chrono::duration_cast<
            std::chrono::microseconds>(
                std::chrono::steady_clock::now() - traversalStarted)
                                         .count();
    }
    if (RouteCancellationRequested(cancellation)) {
        return finish(CancelledRouteSnapshot());
    }

    snapshot.currentSectors = std::move(traversalSnapshot.currentSectors);
    snapshot.nextSectors = std::move(traversalSnapshot.nextSectors);
    const auto prefixStarted = std::chrono::steady_clock::now();
    PopulateTraversalControllerPrefixes(
        &snapshot.currentSectors,
        dataset->controllerAuthorityCatalog);
    PopulateTraversalControllerPrefixes(
        &snapshot.nextSectors,
        dataset->controllerAuthorityCatalog);
    if (timings != nullptr) {
        timings->controllerPrefixUs = std::chrono::duration_cast<
            std::chrono::microseconds>(
                std::chrono::steady_clock::now() - prefixStarted)
                                          .count();
    }
    snapshot.routeResolved = traversalSnapshot.routeResolved;
    snapshot.statusLine =
        AppendUnsupportedRouteStatus(traversalSnapshot.statusLine, diagnostics);
    if (usedPreflightRouteCache) {
        snapshot.statusLine += " via preflight cache";
        snapshot.diagnosticCacheStatus = "preflight-route-cache-build";
    } else if (usedExpandedFmsRouteCache) {
        snapshot.statusLine += " via expanded FMS plan";
        snapshot.diagnosticCacheStatus = "expanded-fms-route-build";
    } else {
        snapshot.diagnosticCacheStatus = "route-rebuild";
    }
    snapshot.statusLine = AppendAuthorityGapStatus(
        snapshot.statusLine,
        snapshot.currentSectors,
        snapshot.nextSectors);
    snapshot.diagnosticReason = "route-key-changed";
    if (timings != nullptr) {
        timings->outputFinalizationUs = std::max<long long>(
            0,
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - prefixStarted)
                    .count() - timings->controllerPrefixUs);
    }
    return finish(std::move(snapshot));
}

}  // namespace

struct RoutePreparationEngine::Implementation {
    Implementation(
        std::string xplaneRootPath,
        std::shared_ptr<const RoutePreparationSource> source,
        bool verifyPreflightSourceFile) {
        state.xplaneRootPath = std::move(xplaneRootPath);
        state.source = source != nullptr
            ? std::move(source)
            : std::make_shared<const LocalRoutePreparationSource>(
                  verifyPreflightSourceFile);
        if (!state.xplaneRootPath.empty() &&
            state.xplaneRootPath.back() != '\\' &&
            state.xplaneRootPath.back() != '/') {
            state.xplaneRootPath.push_back('\\');
        }
    }

    RoutePreparationEngineState state;
};

RoutePreparationEngine::RoutePreparationEngine(
    std::string xplaneRootPath,
    std::shared_ptr<const RoutePreparationSource> source,
    bool verifyPreflightSourceFile)
    : implementation_(
          std::make_unique<Implementation>(
              std::move(xplaneRootPath),
              std::move(source),
              verifyPreflightSourceFile)) {}

RoutePreparationEngine::~RoutePreparationEngine() = default;

RoutePreparationResult RoutePreparationEngine::Prepare(
    const RoutePreparationRequest& request,
    const RouteCancellationToken* cancellation) {
    RoutePreparationResult result;
    if (implementation_ == nullptr) {
        result.reason = "route-engine-unavailable";
        return result;
    }
    auto route = PrepareRouteSnapshotCore(
        &implementation_->state,
        request,
        &result.timings,
        cancellation);
    result.reason = route.diagnosticReason;
    result.comprehensiveDigest =
        brain::HashBrainAuthorityRouteSnapshot(route);
    result.route = std::make_shared<const brain::RouteSectorSnapshot>(
        std::move(route));
    return result;
}

ExpandedFmsObservationResult RoutePreparationEngine::ObserveExpandedFms(
    const brain::NetworkPlanSnapshot& networkPlan,
    const RouteCancellationToken* cancellation) {
    ExpandedFmsObservationResult observation;
    if (implementation_ == nullptr) {
        observation.reason = "route-engine-unavailable";
        return observation;
    }
    ScopedRoutePreparationState activeState(&implementation_->state);
    const auto expanded = TryBuildExpandedFmsRouteCache(
        networkPlan, cancellation);
    if (RouteCancellationRequested(cancellation)) {
        observation.cancelled = true;
        observation.reason = "cooperative-cancellation";
        return observation;
    }
    std::size_t hash = 0;
    HashCombineString(&hash, "EXPANDED_FMS_OBSERVATION_V1");
    HashCombine(&hash, static_cast<std::size_t>(
        HashRouteNetworkPlan(networkPlan)));
    if (expanded.has_value()) {
        observation.hasMatchingPlan = true;
        HashCombineString(&hash, expanded->sourceName);
        HashCombine(&hash, static_cast<std::size_t>(
            HashRoutePreflightCandidate(
                &expanded->cache, expanded->sourceName)));
        observation.reason = "expanded-fms-match";
    } else {
        HashCombineString(&hash, "NO_MATCHING_EXPANDED_FMS");
        observation.reason = "expanded-fms-no-match";
    }
    observation.identity = static_cast<std::uint64_t>(hash == 0 ? 1 : hash);
    return observation;
}

void RoutePreparationEngine::Reset() {
    if (implementation_ == nullptr) {
        return;
    }
    const auto root = implementation_->state.xplaneRootPath;
    const auto source = implementation_->state.source;
    implementation_->state = {};
    implementation_->state.xplaneRootPath = root;
    implementation_->state.source = source;
}

std::uint64_t HashRouteNetworkPlan(
    const brain::NetworkPlanSnapshot& networkPlan) {
    std::size_t hash = 0;
    HashCombine(&hash, networkPlan.feedAvailable ? 1U : 0U);
    HashCombine(&hash, networkPlan.stale ? 1U : 0U);
    HashCombine(&hash, networkPlan.matched ? 1U : 0U);
    HashCombine(&hash, static_cast<std::size_t>(networkPlan.cid));
    HashCombineString(&hash, networkPlan.matchedCallsign);
    HashCombineString(&hash, networkPlan.departureIcao);
    HashCombineDouble(&hash, networkPlan.departureLatDeg);
    HashCombineDouble(&hash, networkPlan.departureLonDeg);
    HashCombine(&hash, networkPlan.hasDepartureCoordinates ? 1U : 0U);
    HashCombineString(&hash, networkPlan.destinationIcao);
    HashCombineDouble(&hash, networkPlan.destinationLatDeg);
    HashCombineDouble(&hash, networkPlan.destinationLonDeg);
    HashCombine(&hash, networkPlan.hasDestinationCoordinates ? 1U : 0U);
    HashCombineString(&hash, networkPlan.routeText);
    return static_cast<std::uint64_t>(hash == 0 ? 1 : hash);
}

std::uint64_t HashRoutePreflightCandidate(
    const core::preflight::PreflightRouteCache* candidate,
    std::string_view validationReason) {
    std::size_t hash = 0;
    HashCombine(&hash, candidate == nullptr ? 0U : 1U);
    HashCombineString(&hash, std::string(validationReason));
    if (candidate == nullptr) {
        return static_cast<std::uint64_t>(hash == 0 ? 1 : hash);
    }
    HashCombine(&hash, static_cast<std::size_t>(candidate->schemaVersion));
    HashCombineString(&hash, candidate->builderVersion);
    HashCombineString(&hash, candidate->compatibilityVersion);
    HashCombineString(&hash, candidate->plan.sourcePath);
    HashCombine(&hash, static_cast<std::size_t>(
        candidate->plan.sourceModifiedUnixSeconds));
    HashCombine(&hash, static_cast<std::size_t>(
        candidate->plan.sourceSizeBytes));
    HashCombineString(&hash, candidate->plan.sourceContentHash);
    HashCombineString(&hash, candidate->plan.departureIcao);
    HashCombineString(&hash, candidate->plan.destinationIcao);
    HashCombineString(&hash, candidate->plan.routeIdentityHash);
    HashCombine(&hash, candidate->plan.waypoints.size());
    for (const auto& waypoint : candidate->plan.waypoints) {
        HashCombine(&hash, static_cast<std::size_t>(waypoint.type));
        HashCombineString(&hash, waypoint.ident);
        HashCombineString(&hash, waypoint.via);
        HashCombineDouble(&hash, waypoint.altitudeFt);
        HashCombineDouble(&hash, waypoint.latitudeDeg);
        HashCombineDouble(&hash, waypoint.longitudeDeg);
    }
    return static_cast<std::uint64_t>(hash == 0 ? 1 : hash);
}

std::uint64_t HashRouteAnchor(
    const brain::AircraftStateSnapshot& aircraft,
    const brain::NetworkPlanSnapshot& networkPlan) {
    std::size_t hash = 0;
    HashCombine(&hash, networkPlan.hasDepartureCoordinates ? 1U : 0U);
    if (!networkPlan.hasDepartureCoordinates) {
        HashCombine(&hash, aircraft.valid ? 1U : 0U);
        HashCombineDouble(&hash, aircraft.latitudeDeg);
        HashCombineDouble(&hash, aircraft.longitudeDeg);
    }
    return static_cast<std::uint64_t>(hash == 0 ? 1 : hash);
}

brain::AircraftStateSnapshot SelectStickyRouteAnchor(
    RouteAnchorSelectionState* state,
    const brain::AircraftStateSnapshot& currentAircraft,
    const brain::NetworkPlanSnapshot& networkPlan,
    std::uint64_t networkPlanDigest) {
    if (state == nullptr || networkPlan.hasDepartureCoordinates) {
        return currentAircraft;
    }
    const auto recapture = !state->captured ||
        state->networkPlanDigest != networkPlanDigest ||
        (!state->anchor.valid && currentAircraft.valid);
    if (recapture) {
        state->captured = true;
        state->networkPlanDigest = networkPlanDigest;
        state->anchor = currentAircraft;
    }
    return state->anchor;
}

RouteCoordinatorDecision DecideRouteCoordinatorAction(
    const RouteCoordinatorDecisionInput& input) {
    RouteCoordinatorDecision decision;
    if (!input.sourceUsable) {
        decision.failClosed = true;
        decision.reason = "route-source-unusable";
        return decision;
    }
    if (input.hasEligibleRequest) {
        decision.failClosed = !input.acceptedCurrent;
        decision.reason = "route-worker-busy";
        return decision;
    }
    if (!input.fmsObservationReady) {
        decision.action = RouteCoordinatorAction::ObserveExpandedFms;
        decision.failClosed = true;
        decision.reason = "expanded-fms-observation-required";
        return decision;
    }
    if ((!input.acceptedCurrent || input.brainNeedsWorker) &&
        input.retryDue) {
        decision.action = RouteCoordinatorAction::PrepareRoute;
        decision.failClosed = true;
        decision.reason = input.brainNeedsWorker
            ? "unresolved-route-retry"
            : "route-preparation-required";
        return decision;
    }
    if (input.acceptedCurrent && input.fmsObservationDue &&
        !input.workerBusy) {
        decision.action = RouteCoordinatorAction::ObserveExpandedFms;
        decision.reason = "expanded-fms-quiet-observation-due";
        return decision;
    }
    decision.failClosed = input.brainNeedsWorker || !input.acceptedCurrent;
    decision.reason = input.retryDue
        ? "route-coordinator-stable"
        : "route-retry-backoff";
    return decision;
}

std::uint64_t RoutePreparationPolicyIdentity() {
    static const std::uint64_t identity = []() {
        std::size_t hash = 0;
        HashCombineString(
            &hash,
            "GATE_B_B3_1_ROUTE_PREPARATION_V2|EXACT|SANITY_30|EXPANDED_FMS_OBSERVED");
        return static_cast<std::uint64_t>(hash == 0 ? 1 : hash);
    }();
    return identity;
}

bool SameRouteWorkerSemanticIdentity(
    const RouteWorkerIdentity& left,
    const RouteWorkerIdentity& right) {
    return left.lifecycleEpoch == right.lifecycleEpoch &&
           left.planKey == right.planKey &&
           left.networkPlanDigest == right.networkPlanDigest &&
           left.routeSourceDatasetIdentity ==
               right.routeSourceDatasetIdentity &&
           left.preflightCandidateIdentity ==
               right.preflightCandidateIdentity &&
           left.routePolicyIdentity == right.routePolicyIdentity &&
           left.routeAnchorDigest == right.routeAnchorDigest &&
           left.expandedFmsObservationIdentity ==
               right.expandedFmsObservationIdentity;
}

brain::RouteSectorSnapshot RouteSectorResolver::BuildSnapshot(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::NetworkPlanSnapshot& networkPlanSnapshot) const {
    RoutePreparationRequest request;
    request.routeAnchor = aircraftState;
    request.networkPlan =
        std::make_shared<const brain::NetworkPlanSnapshot>(
            networkPlanSnapshot);
    request.routeSourceDataset =
        GetRouteSourceDatasetPublication().dataset;
    if (preflightRouteCache_.has_value()) {
        request.preflightCandidate = std::make_shared<
            const core::preflight::PreflightRouteCache>(
                *preflightRouteCache_);
        request.preflightValidationReason = preflightRouteCacheReason_;
    }
    if (synchronousRouteOracleEngine_ == nullptr) {
        brain::RouteSectorSnapshot unavailable;
        unavailable.available = false;
        unavailable.stale = true;
        unavailable.statusLine = "ROUTE engine unavailable";
        return unavailable;
    }
    auto prepared = synchronousRouteOracleEngine_->Prepare(request);
    if (prepared.route == nullptr) {
        brain::RouteSectorSnapshot unavailable;
        unavailable.available = false;
        unavailable.stale = true;
        unavailable.statusLine = "ROUTE engine unavailable";
        return unavailable;
    }
    auto oracleSnapshot = *prepared.route;
    oracleSnapshot.diagnosticCacheStatus.clear();
    oracleSnapshot.diagnosticReason.clear();
    return oracleSnapshot;
}

brain::RouteSectorSnapshot RouteSectorResolver::BuildSnapshotFrozenOracle(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::NetworkPlanSnapshot& networkPlanSnapshot) const {
    brain::RouteSectorSnapshot snapshot;
    snapshot.available = true;
    snapshot.departureIcao = networkPlanSnapshot.departureIcao;
    snapshot.destinationIcao = networkPlanSnapshot.destinationIcao;
    snapshot.centerBoundaryGeneration = hasBoundaryCache_ ? centerBoundaryGeneration_ : 0;
    snapshot.authorityCatalogGeneration =
        hasAuthorityCatalogCache_ ? authorityCatalogGeneration_ : 0;

    if (!aircraftState.valid || !networkPlanSnapshot.hasDestinationCoordinates) {
        snapshot.statusLine = "ROUTE waiting for destination";
        return snapshot;
    }

    const auto& features = GetCachedSectorFeatures(boundaryPayload_);
    const auto& authorityCatalog =
        GetCachedControllerAuthorityCatalog(vatspyPayload_, ownershipPayload_);
    PopulateEndpointAirportCallsignAliases(&snapshot, authorityCatalog);
    if (features.empty()) {
        snapshot.statusLine = "ROUTE sectors unavailable";
        return snapshot;
    }

    RouteResolveDiagnostics diagnostics;
    bool usedPreflightRouteCache = false;
    bool usedExpandedFmsRouteCache = false;
    std::string expandedFmsSourceName;
    if (preflightRouteCache_.has_value()) {
        const auto cacheValidation =
            core::preflight::ValidatePreflightRouteCacheForNetworkPlan(
                *preflightRouteCache_,
                networkPlanSnapshot,
                false);
        if (cacheValidation.accepted) {
            snapshot.waypoints =
                core::preflight::BuildRouteWaypointsFromCache(*preflightRouteCache_);
            usedPreflightRouteCache = snapshot.waypoints.size() >= 2;
            diagnostics.resolvedTokens.push_back(
                "PREFLIGHT_CACHE:" +
                preflightRouteCache_->plan.routeIdentityHash);
            if (!preflightRouteCacheReason_.empty()) {
                diagnostics.procedureMetadataSources.push_back(
                    "PREFLIGHT_ROUTE_CACHE:" + preflightRouteCacheReason_);
            } else {
                diagnostics.procedureMetadataSources.push_back(
                    "PREFLIGHT_ROUTE_CACHE");
            }
        }
    }
    if (!usedPreflightRouteCache) {
        if (auto expandedFmsRoute =
                TryBuildExpandedFmsRouteCache(networkPlanSnapshot);
            expandedFmsRoute.has_value()) {
            snapshot.waypoints =
                core::preflight::BuildRouteWaypointsFromCache(
                    expandedFmsRoute->cache);
            usedExpandedFmsRouteCache = snapshot.waypoints.size() >= 2;
            expandedFmsSourceName = expandedFmsRoute->sourceName;
            if (usedExpandedFmsRouteCache) {
                diagnostics.resolvedTokens.push_back(
                    "EXPANDED_FMS:" +
                    expandedFmsRoute->cache.plan.routeIdentityHash);
                diagnostics.procedureMetadataSources.push_back(
                    "EXPANDED_FMS:" + expandedFmsSourceName);
            }
        }
    }
    if (!usedPreflightRouteCache && !usedExpandedFmsRouteCache) {
        snapshot.waypoints =
            ResolveRouteWaypoints(aircraftState, networkPlanSnapshot, &diagnostics);
    }
    snapshot.routeResolved = snapshot.waypoints.size() >= 2;
    const auto directDistanceNm = GreatCircleDistanceNm(
        aircraftState.latitudeDeg,
        aircraftState.longitudeDeg,
        networkPlanSnapshot.destinationLatDeg,
        networkPlanSnapshot.destinationLonDeg);
    if (snapshot.routeResolved &&
        !usedPreflightRouteCache &&
        !usedExpandedFmsRouteCache &&
        !diagnostics.unresolvedAirwayTokens.empty()) {
        const auto routeDistanceNm = RouteDistanceNm(snapshot.waypoints);
        snapshot.routeResolved = false;
        snapshot.statusLine =
            AppendUnsupportedRouteStatus(
                "ROUTE incomplete unresolved-airways " +
                    std::to_string(diagnostics.unresolvedAirwayTokens.size()),
                diagnostics);
        LogRouteDiagnosticsIfChanged(
            networkPlanSnapshot,
            diagnostics,
            snapshot,
            directDistanceNm,
            routeDistanceNm,
            "none",
            false);
        return snapshot;
    }
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
    if (usedPreflightRouteCache) {
        snapshot.statusLine += " via preflight cache";
    } else if (usedExpandedFmsRouteCache) {
        snapshot.statusLine += " via expanded FMS plan";
    }
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
    const auto& authorityCatalog =
        GetCachedControllerAuthorityCatalog(
            vatspyPayload_,
            terminalBoundaryPayload_,
            ownershipPayload_);

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
    bool expected = false;
    if (!sourceRefreshStarted_.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return;
    }
    sourceRefreshStop_.store(false, std::memory_order_release);
    lastFetchTickSeconds_.store(nowSeconds, std::memory_order_release);
    fetchInProgress_.store(true, std::memory_order_release);
    fetchThread_ = std::thread([this]() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        while (!sourceRefreshStop_.load(std::memory_order_acquire)) {
            lastFetchTickSeconds_.store(
                CurrentTickSeconds(), std::memory_order_release);
            fetchInProgress_.store(true, std::memory_order_release);
            const auto mapDataManifest = DownloadMapDataManifest();
            const auto payload = DownloadBoundaryPayload(mapDataManifest);
            const auto vatspyPayload =
                DownloadVatSpyDataPayload(mapDataManifest);
            const auto terminalPayload =
                DownloadTerminalBoundaryPayload(mapDataManifest);
            const auto ownershipPayload =
                DownloadOwnershipPayload(mapDataManifest);
            std::vector<unsigned char> boundaryPayload(
                payload.begin(), payload.end());
            std::vector<unsigned char> authorityCatalogPayload(
                vatspyPayload.begin(), vatspyPayload.end());
            std::vector<unsigned char> terminalBoundaryPayload(
                terminalPayload.begin(), terminalPayload.end());
            std::vector<unsigned char> ownershipAuthorityPayload(
                ownershipPayload.begin(), ownershipPayload.end());

            // Compile and warm each publication on this coordinator, never
            // on X-Plane's callback thread.
            if (!boundaryPayload.empty()) {
                (void)GetCachedSectorFeatures(boundaryPayload);
            }
            if (!authorityCatalogPayload.empty()) {
                (void)GetCachedControllerAuthorityCatalog(
                    authorityCatalogPayload,
                    ownershipAuthorityPayload);
                (void)GetCachedCoreControllerAuthorityCatalog(
                    authorityCatalogPayload,
                    terminalBoundaryPayload,
                    ownershipAuthorityPayload);
            }
            if (!terminalBoundaryPayload.empty()) {
                (void)GetCachedTerminalSectorFeatures(
                    terminalBoundaryPayload);
            }
            if (!boundaryPayload.empty() &&
                !authorityCatalogPayload.empty()) {
                (void)GetCachedCoreAuthorityPolygonCatalog(
                    boundaryPayload,
                    terminalBoundaryPayload,
                    ownershipAuthorityPayload);
            }

            const auto completePackage = !boundaryPayload.empty() &&
                !authorityCatalogPayload.empty() &&
                !terminalBoundaryPayload.empty();
            StageFetchedPayloads(
                std::move(boundaryPayload),
                std::move(terminalBoundaryPayload),
                std::move(authorityCatalogPayload),
                std::move(ownershipAuthorityPayload));
            fetchInProgress_.store(false, std::memory_order_release);

            std::unique_lock<std::mutex> waitLock(
                sourceRefreshWaitMutex_);
            sourceRefreshWait_.wait_for(
                waitLock,
                std::chrono::seconds(
                    completePackage
                        ? kRefreshCadenceSeconds
                        : kFailureBackoffSeconds),
                [this]() {
                    return sourceRefreshStop_.load(
                        std::memory_order_acquire);
                });
        }
        fetchInProgress_.store(false, std::memory_order_release);
    });
}

void RouteSectorResolver::StageFetchedPayloads(
    std::vector<unsigned char> boundaryPayload,
    std::vector<unsigned char> terminalBoundaryPayload,
    std::vector<unsigned char> authorityCatalogPayload,
    std::vector<unsigned char> ownershipPayload) const {
    std::vector<unsigned char> effectiveBoundaryPayload = boundaryPayload;
    std::vector<unsigned char> effectiveTerminalBoundaryPayload =
        terminalBoundaryPayload;
    std::vector<unsigned char> effectiveAuthorityCatalogPayload =
        authorityCatalogPayload;
    std::vector<unsigned char> effectiveOwnershipPayload = ownershipPayload;
    const auto hasCenterPackage =
        !boundaryPayload.empty() && !authorityCatalogPayload.empty();
    bool centerContentChanged = false;
    bool terminalContentChanged = false;
    bool authorityDatasetMissing = false;
    {
        std::lock_guard<std::mutex> lock(fetchMutex_);
        centerContentChanged = hasCenterPackage &&
            (boundaryPayload != boundaryPayload_ ||
             authorityCatalogPayload != vatspyPayload_ ||
             ownershipPayload != ownershipPayload_);
        terminalContentChanged = !terminalBoundaryPayload.empty() &&
            terminalBoundaryPayload != terminalBoundaryPayload_;
        authorityDatasetMissing = GetAuthoritySourceDataset() == nullptr;
        if (effectiveBoundaryPayload.empty()) {
            effectiveBoundaryPayload = boundaryPayload_;
        }
        if (effectiveTerminalBoundaryPayload.empty()) {
            effectiveTerminalBoundaryPayload = terminalBoundaryPayload_;
        }
        if (effectiveAuthorityCatalogPayload.empty()) {
            effectiveAuthorityCatalogPayload = vatspyPayload_;
        }
        if (effectiveOwnershipPayload.empty()) {
            effectiveOwnershipPayload = ownershipPayload_;
        }
    }
    const auto compiledDataset =
        centerContentChanged || terminalContentChanged ||
                authorityDatasetMissing
            ? CompileCanonicalAuthoritySourcePayloads(
                  effectiveBoundaryPayload,
                  effectiveTerminalBoundaryPayload,
                  effectiveAuthorityCatalogPayload,
                  effectiveOwnershipPayload)
            : std::shared_ptr<CanonicalAuthorityMapDataset>{};
    const auto compiledRouteDataset =
        centerContentChanged
            ? BuildRouteSourceDataset(
                  effectiveBoundaryPayload,
                  effectiveAuthorityCatalogPayload,
                  effectiveOwnershipPayload)
            : std::shared_ptr<RouteSourceDataset>{};
    {
        std::lock_guard<std::mutex> lock(fetchMutex_);
        pendingBoundaryPayload_.clear();
        pendingVatspyPayload_.clear();
        pendingTerminalBoundaryPayload_.clear();
        pendingOwnershipPayload_.clear();
        pendingAuthorityDataset_.reset();
        pendingRouteDataset_.reset();

        if (!boundaryPayload.empty() && !authorityCatalogPayload.empty()) {
            pendingBoundaryPayload_ = std::move(boundaryPayload);
            pendingVatspyPayload_ = std::move(authorityCatalogPayload);
            pendingOwnershipPayload_ = std::move(ownershipPayload);
        }
        if (!terminalBoundaryPayload.empty()) {
            pendingTerminalBoundaryPayload_ =
                std::move(terminalBoundaryPayload);
        }
        pendingAuthorityDataset_ = compiledDataset;
        pendingRouteDataset_ = compiledRouteDataset;
        hasPendingPayload_ = true;
    }
    pendingPublicationReady_.store(true, std::memory_order_release);
}

void RouteSectorResolver::HarvestPendingFetch() const {
    if (fetchInProgress_.load(std::memory_order_acquire) ||
        !pendingPublicationReady_.exchange(
            false, std::memory_order_acq_rel)) {
        return;
    }

    std::lock_guard<std::mutex> lock(fetchMutex_);
    if (!hasPendingPayload_) {
        return;
    }

    hasPendingPayload_ = false;
    const auto hasPendingCenterPackage =
        !pendingBoundaryPayload_.empty() && !pendingVatspyPayload_.empty();
    const auto hasPendingTerminalPackage = !pendingTerminalBoundaryPayload_.empty();
    const auto centerContentChanged =
        hasPendingCenterPackage && pendingRouteDataset_ != nullptr;
    const auto terminalContentChanged =
        hasPendingTerminalPackage &&
        pendingTerminalBoundaryPayload_ != terminalBoundaryPayload_;
    const auto previousRoutePublication = std::atomic_load_explicit(
        &publishedRoutePublication_, std::memory_order_acquire);

    if (hasPendingCenterPackage) {
        if (centerContentChanged || !hasBoundaryCache_ ||
            !hasAuthorityCatalogCache_) {
            boundaryPayload_ = std::move(pendingBoundaryPayload_);
            vatspyPayload_ = std::move(pendingVatspyPayload_);
            ownershipPayload_ = std::move(pendingOwnershipPayload_);
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
        }
        lastSuccessfulCenterFetchTickSeconds_ = CurrentTickSeconds();
    }
    if (hasPendingTerminalPackage) {
        // Preserve the legacy coherent-package generation contract: a real
        // center/authority replacement advances the terminal generation even
        // when the terminal bytes happen to be unchanged. A wholly identical
        // refresh remains quiet across every generation and publication.
        if (terminalContentChanged || centerContentChanged ||
            !hasTerminalBoundaryCache_) {
            terminalBoundaryPayload_ =
                std::move(pendingTerminalBoundaryPayload_);
            hasTerminalBoundaryCache_ = true;
            ++terminalBoundaryGeneration_;
            if (terminalBoundaryGeneration_ == 0) {
                terminalBoundaryGeneration_ = 1;
            }
        }
        lastSuccessfulTerminalFetchTickSeconds_ = CurrentTickSeconds();
    }
    if (pendingAuthorityDataset_ != nullptr &&
        (centerContentChanged || terminalContentChanged ||
         GetAuthoritySourceDataset() == nullptr)) {
        pendingAuthorityDataset_->centerBoundaryGeneration =
            centerBoundaryGeneration_;
        pendingAuthorityDataset_->authorityCatalogGeneration =
            authorityCatalogGeneration_;
        pendingAuthorityDataset_->terminalBoundaryGeneration =
            terminalBoundaryGeneration_;
        publishedTerminalBoundaryGeneration_.store(
            terminalBoundaryGeneration_, std::memory_order_release);
        authorityMapStore_->Publish(pendingAuthorityDataset_);
    } else {
        publishedTerminalBoundaryGeneration_.store(
            terminalBoundaryGeneration_, std::memory_order_release);
    }
    if (pendingRouteDataset_ != nullptr) {
        pendingRouteDataset_->centerBoundaryGeneration =
            centerBoundaryGeneration_;
        pendingRouteDataset_->authorityCatalogGeneration =
            authorityCatalogGeneration_;
        routeDataset_ = pendingRouteDataset_;
    }

    const auto routeAvailable = routeDataset_ != nullptr;
    const auto routeStale = !hasPendingCenterPackage || !routeAvailable;
    const auto routePublicationChanged =
        previousRoutePublication == nullptr ||
        centerContentChanged ||
        previousRoutePublication->available != routeAvailable ||
        previousRoutePublication->stale != routeStale;
    if (routePublicationChanged) {
        ++routePublicationGeneration_;
        if (routePublicationGeneration_ == 0) {
            routePublicationGeneration_ = 1;
        }
        std::atomic_store_explicit(
            &publishedRoutePublication_,
            BuildRouteDatasetPublicationState(
                std::shared_ptr<const RouteSourceDataset>(routeDataset_),
                routePublicationGeneration_,
                routeAvailable,
                routeStale),
            std::memory_order_release);
    }
    pendingBoundaryPayload_.clear();
    pendingVatspyPayload_.clear();
    pendingTerminalBoundaryPayload_.clear();
    pendingOwnershipPayload_.clear();
    pendingAuthorityDataset_.reset();
    pendingRouteDataset_.reset();
    lastFetchSucceeded_ = hasPendingCenterPackage && hasPendingTerminalPackage;
    hasSnapshotCache_ = false;
    lastSnapshotRouteKey_.clear();
    hasAuthorityRelevanceCache_ = false;
    cachedAuthorityRelevanceSnapshot_ = {};
    if (authorityEngine_ != nullptr) {
        authorityEngine_->Reset();
    }
    lastAuthorityRelevanceBuildTickSeconds_ = 0;
    lastAuthorityRelevanceLatitudeDeg_ = 0.0;
    lastAuthorityRelevanceLongitudeDeg_ = 0.0;
    lastAuthorityRelevanceSignature_ = 0;
    lastAuthorityOperationalScopeSignature_ = 0;
    lastAuthorityWatchInputSignature_ = 0;
    lastAuthorityRelevanceProgressRouteSignature_ = 0;
    lastAuthorityRelevanceProgressWindowNm_ = 0.0;
    authorityProgressRouteSignature_ = 0;
    authorityProgressWindowNm_ = 0.0;
    lastAuthorityProgressTickSeconds_ = 0;
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
    const auto lastFetchTickSeconds =
        lastFetchTickSeconds_.load(std::memory_order_acquire);

    const auto cadenceSeconds =
        lastFetchSucceeded_ ? kRefreshCadenceSeconds : kFailureBackoffSeconds;
    if ((hasBoundaryCache_ || hasTerminalBoundaryCache_) &&
        (nowSeconds - lastFetchTickSeconds) < cadenceSeconds) {
        return lastFetchSucceeded_;
    }

    if (!hasBoundaryCache_ &&
        !hasTerminalBoundaryCache_ &&
        lastFetchTickSeconds != 0 &&
        (nowSeconds - lastFetchTickSeconds) < cadenceSeconds) {
        return false;
    }

    if (!sourceRefreshStarted_.load(std::memory_order_acquire)) {
        StartAsyncBoundaryFetch(nowSeconds);
    }

    return IsCenterAuthorityCacheFresh(nowSeconds) ||
           IsTerminalBoundaryCacheFresh(nowSeconds);
}

struct AuthorityRelevanceWorker::Implementation {
    static_assert(
        std::atomic<AuthorityWorkerRequest*>::is_always_lock_free,
        "Gate A request mailbox requires lock-free pointer atomics");
    static_assert(
        std::atomic<AuthorityWorkerFact*>::is_always_lock_free,
        "Gate A completion mailbox requires lock-free pointer atomics");

    HANDLE wakeEvent = nullptr;
    std::thread worker;
    std::atomic<AuthorityWorkerRequest*> pending{nullptr};
    std::atomic<AuthorityWorkerFact*> completed{nullptr};
    std::atomic<AuthorityWorkerRequest*> retiredRequests{nullptr};
    std::atomic<AuthorityWorkerFact*> retiredFacts{nullptr};
    // Worker-thread-only retention. Main-thread releases can only decrement a
    // non-final reference after setting AuthoritySnapshotLease::retired.
    std::vector<std::shared_ptr<AuthoritySnapshotLease>> retainedLeases;
    AuthorityRelevanceEngine engine;
    std::atomic<bool> cancelRequested{false};
    std::atomic<bool> running{false};
    std::atomic<bool> started{false};
    std::atomic<bool> stopping{false};
    std::atomic<bool> resetEngineBeforeNext{false};
    std::atomic<std::uint64_t> latestMailboxSequence{0};
    std::atomic<std::uint64_t> activeMailboxSequence{0};
    std::atomic<std::uint64_t> starts{0};
    std::atomic<std::uint64_t> replacements{0};
    std::atomic<std::uint64_t> completions{0};
    std::atomic<std::uint64_t> cancellations{0};
    std::atomic<std::uint64_t> maximumPendingDepth{0};
    std::atomic<std::uint64_t> workerThreadSnapshotRetirements{0};

    Implementation() = default;

    ~Implementation() {
        Stop();
    }

    static long long MonotonicMilliseconds() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    static void PushRetiredRequest(
        std::atomic<AuthorityWorkerRequest*>* stack,
        AuthorityWorkerRequest* request) {
        if (stack == nullptr || request == nullptr) {
            return;
        }
        auto* head = stack->load(std::memory_order_relaxed);
        do {
            request->retirementNext = head;
        } while (!stack->compare_exchange_weak(
            head,
            request,
            std::memory_order_release,
            std::memory_order_relaxed));
    }

    static void PushRetiredFact(
        std::atomic<AuthorityWorkerFact*>* stack,
        AuthorityWorkerFact* fact) {
        if (stack == nullptr || fact == nullptr) {
            return;
        }
        auto* head = stack->load(std::memory_order_relaxed);
        do {
            fact->retirementNext = head;
        } while (!stack->compare_exchange_weak(
            head,
            fact,
            std::memory_order_release,
            std::memory_order_relaxed));
    }

    void Signal() const {
        if (wakeEvent != nullptr) {
            SetEvent(wakeEvent);
        }
    }

    void RetireFactOnWorker(AuthorityWorkerFact* fact) {
        if (fact == nullptr) {
            return;
        }
        if (fact->snapshotLease != nullptr) {
            fact->snapshotLease->retired.store(
                true, std::memory_order_release);
        }
        delete fact;
    }

    void DrainRetiredObjects() {
        auto* request = retiredRequests.exchange(
            nullptr, std::memory_order_acquire);
        while (request != nullptr) {
            auto* next = request->retirementNext;
            delete request;
            request = next;
        }

        auto* fact = retiredFacts.exchange(
            nullptr, std::memory_order_acquire);
        while (fact != nullptr) {
            auto* next = fact->retirementNext;
            RetireFactOnWorker(fact);
            fact = next;
        }

        for (auto lease = retainedLeases.begin();
             lease != retainedLeases.end();) {
            if (*lease == nullptr ||
                (*lease)->retired.load(std::memory_order_acquire)) {
                lease = retainedLeases.erase(lease);
                workerThreadSnapshotRetirements.fetch_add(
                    1, std::memory_order_relaxed);
            } else {
                ++lease;
            }
        }
    }

    bool StartThread() {
        if (started.load(std::memory_order_acquire)) {
            return true;
        }
        if (stopping.load(std::memory_order_acquire)) {
            return false;
        }
        wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (wakeEvent == nullptr) {
            return false;
        }
        try {
            worker = std::thread([this]() { Run(); });
        } catch (...) {
            CloseHandle(wakeEvent);
            wakeEvent = nullptr;
            return false;
        }
        started.store(true, std::memory_order_release);
        return true;
    }

    void Run() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        for (;;) {
            WaitForSingleObject(wakeEvent, INFINITE);
            DrainRetiredObjects();
            if (stopping.load(std::memory_order_acquire)) {
                auto* abandoned = pending.exchange(
                    nullptr, std::memory_order_acq_rel);
                delete abandoned;
                RetireFactOnWorker(completed.exchange(
                    nullptr, std::memory_order_acq_rel));
                DrainRetiredObjects();
                retainedLeases.clear();
                running.store(false, std::memory_order_release);
                activeMailboxSequence.store(0, std::memory_order_release);
                return;
            }

            // Preserve one completed immutable fact until the Brain harvests
            // it. A queued follow-up is intentionally left pending and will
            // be woken by TryHarvest. This prevents a fast delta completion
            // from overwriting the completed baseline before the Brain can
            // reconcile its still-current per-controller facts.
            if (completed.load(std::memory_order_acquire) != nullptr) {
                continue;
            }

            auto* request = pending.exchange(
                nullptr, std::memory_order_acq_rel);
            if (request == nullptr) {
                continue;
            }
            running.store(true, std::memory_order_release);
            activeMailboxSequence.store(
                request->mailboxSequence, std::memory_order_release);
            cancelRequested.store(false, std::memory_order_release);
            if (resetEngineBeforeNext.exchange(
                    false, std::memory_order_acq_rel)) {
                engine.Reset();
            }

            AuthorityWorkerFact fact;
            fact.identity = request->identity;
            fact.dispatchedAircraft = request->aircraft;
            brain::AuthorityRelevanceSnapshot snapshot;
            const auto started = std::chrono::steady_clock::now();
            auto remainingDelayMs = request->cooperativeDelayForTestingMs;
            while (remainingDelayMs > 0 &&
                   !cancelRequested.load(std::memory_order_relaxed)) {
                const auto delaySliceMs =
                    std::min<std::uint32_t>(remainingDelayMs, 10);
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(delaySliceMs));
                remainingDelayMs -= delaySliceMs;
            }
            if (cancelRequested.load(std::memory_order_acquire)) {
                snapshot.available = false;
                snapshot.stale = true;
                snapshot.diagnosticCacheStatus =
                    "authority-worker-cancelled";
                snapshot.diagnosticReason = "cooperative-cancellation";
                fact.reason = "cooperative-cancellation";
                fact.cancelled = true;
            } else if (request->dataset == nullptr ||
                       request->route == nullptr) {
                snapshot.available = false;
                snapshot.stale = true;
                fact.reason = "authority-dataset-unavailable";
            } else {
                std::vector<brain::ControllerSnapshot> authorityControllers;
                brain::ControllerFeedSnapshot controllerFeed;
                controllerFeed.available = request->controllerFeedAvailable;
                controllerFeed.stale = request->controllerFeedStale;
                controllerFeed.controllerComponentComplete =
                    request->controllerFeedComplete;
                controllerFeed.generation = request->controllerFeedGeneration;
                controllerFeed.connectedControllers =
                    request->controllerFeedConnectedControllers;
                if (request->hasControllerEvidence &&
                    request->controllers != nullptr) {
                    authorityControllers =
                        brain::ExpandBrainAuthorityControllerEvidence(
                            *request->controllers);
                    controllerFeed.controllers = &authorityControllers;
                }
                snapshot = engine.Evaluate(
                    *request->dataset,
                    request->terminalBoundaryGeneration,
                    request->aircraft,
                    controllerFeed,
                    request->dynamicOwnership.get(),
                    *request->route,
                    request->scheduleReason,
                    &cancelRequested);
                fact.snapshotDigest =
                    brain::HashBrainAuthorityRelevanceSnapshot(snapshot);
                fact.cancelled = cancelRequested.load(std::memory_order_relaxed) ||
                    snapshot.diagnosticCacheStatus ==
                        "authority-worker-cancelled";
                fact.reason = fact.cancelled
                    ? "cooperative-cancellation"
                    : snapshot.diagnosticReason;
            }
            fact.workerElapsedUs =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - started)
                    .count();
            fact.completedMonotonicMs = MonotonicMilliseconds();
            fact.completed = !fact.cancelled;

            fact.cancelled =
                fact.cancelled ||
                cancelRequested.load(std::memory_order_acquire);
            fact.completed = !fact.cancelled;
            activeMailboxSequence.store(0, std::memory_order_release);
            running.store(false, std::memory_order_release);
            if (fact.cancelled) {
                cancellations.fetch_add(1, std::memory_order_relaxed);
            } else {
                auto lease = std::make_shared<AuthoritySnapshotLease>();
                lease->snapshot = std::make_shared<
                    const brain::AuthorityRelevanceSnapshot>(
                        std::move(snapshot));
                fact.snapshotLease = lease;
                retainedLeases.push_back(std::move(lease));
                auto* published = new AuthorityWorkerFact(std::move(fact));
                auto* obsolete = completed.exchange(
                    published, std::memory_order_acq_rel);
                RetireFactOnWorker(obsolete);
                completions.fetch_add(1, std::memory_order_relaxed);
            }
            delete request;
            DrainRetiredObjects();
        }
    }

    void Stop() {
        if (!started.load(std::memory_order_acquire) ||
            stopping.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        latestMailboxSequence.fetch_add(1, std::memory_order_acq_rel);
        cancelRequested.store(true, std::memory_order_release);
        PushRetiredRequest(
            &retiredRequests,
            pending.exchange(nullptr, std::memory_order_acq_rel));
        auto* abandonedFact = completed.exchange(
            nullptr, std::memory_order_acq_rel);
        if (abandonedFact != nullptr &&
            abandonedFact->snapshotLease != nullptr) {
            abandonedFact->snapshotLease->retired.store(
                true, std::memory_order_release);
        }
        PushRetiredFact(&retiredFacts, abandonedFact);
        Signal();
        if (worker.joinable()) {
            worker.join();
        }
        if (wakeEvent != nullptr) {
            CloseHandle(wakeEvent);
            wakeEvent = nullptr;
        }
    }
};

AuthorityRelevanceWorker::AuthorityRelevanceWorker()
    : implementation_(std::make_unique<Implementation>()) {}

AuthorityRelevanceWorker::~AuthorityRelevanceWorker() = default;

bool AuthorityRelevanceWorker::Start() {
    return implementation_ != nullptr && implementation_->StartThread();
}

bool AuthorityRelevanceWorker::StartLatest(AuthorityWorkerRequest request) {
    if (implementation_ == nullptr || request.dataset == nullptr ||
        request.route == nullptr ||
        !implementation_->started.load(std::memory_order_acquire) ||
        implementation_->stopping.load(std::memory_order_acquire)) {
        return false;
    }
    AuthorityWorkerRequest* packaged = nullptr;
    try {
        packaged = new AuthorityWorkerRequest(std::move(request));
    } catch (...) {
        return false;
    }
    packaged->mailboxSequence =
        implementation_->latestMailboxSequence.fetch_add(
            1, std::memory_order_acq_rel) + 1;
    auto* replaced = implementation_->pending.exchange(
        packaged, std::memory_order_acq_rel);
    if (replaced != nullptr) {
        implementation_->replacements.fetch_add(
            1, std::memory_order_relaxed);
        Implementation::PushRetiredRequest(
            &implementation_->retiredRequests, replaced);
    }
    // Ordinary roster churn never cancels useful in-flight authority work.
    // The running request finishes and the single pending mailbox retains only
    // the newest Brain-issued follow-up. Structural/lifecycle cancellation is
    // owned exclusively by CancelPending/CancelAndJoin.
    implementation_->maximumPendingDepth.store(1, std::memory_order_relaxed);
    implementation_->starts.fetch_add(1, std::memory_order_relaxed);
    implementation_->Signal();
    return true;
}

bool AuthorityRelevanceWorker::TryHarvest(AuthorityWorkerFact* fact) {
    if (implementation_ == nullptr || fact == nullptr) {
        return false;
    }
    auto* completed = implementation_->completed.exchange(
        nullptr, std::memory_order_acq_rel);
    if (completed == nullptr) {
        return false;
    }
    if (fact->snapshotLease != nullptr) {
        fact->snapshotLease->retired.store(true, std::memory_order_release);
        implementation_->Signal();
    }
    *fact = std::move(*completed);
    delete completed;
    implementation_->Signal();
    return true;
}

bool AuthorityRelevanceWorker::IsRunning() const {
    if (implementation_ == nullptr) {
        return false;
    }
    return implementation_->running.load(std::memory_order_acquire) ||
           implementation_->pending.load(std::memory_order_acquire) != nullptr;
}

void AuthorityRelevanceWorker::NotifyRetirement() {
    if (implementation_ != nullptr) {
        implementation_->Signal();
    }
}

void AuthorityRelevanceWorker::CancelPending() {
    if (implementation_ == nullptr) {
        return;
    }
    implementation_->latestMailboxSequence.fetch_add(
        1, std::memory_order_acq_rel);
    implementation_->cancelRequested.store(true, std::memory_order_release);
    implementation_->resetEngineBeforeNext.store(
        true, std::memory_order_release);
    Implementation::PushRetiredRequest(
        &implementation_->retiredRequests,
        implementation_->pending.exchange(
            nullptr, std::memory_order_acq_rel));
    auto* completed = implementation_->completed.exchange(
        nullptr, std::memory_order_acq_rel);
    if (completed != nullptr && completed->snapshotLease != nullptr) {
        completed->snapshotLease->retired.store(
            true, std::memory_order_release);
    }
    Implementation::PushRetiredFact(
        &implementation_->retiredFacts, completed);
    implementation_->Signal();
}

void AuthorityRelevanceWorker::CancelAndJoin() {
    if (implementation_ != nullptr) {
        implementation_->Stop();
    }
}

AuthorityWorkerSnapshot AuthorityRelevanceWorker::Snapshot() const {
    if (implementation_ == nullptr) {
        return {};
    }
    AuthorityWorkerSnapshot snapshot;
    snapshot.running = implementation_->running.load(std::memory_order_acquire);
    snapshot.pending =
        implementation_->pending.load(std::memory_order_acquire) != nullptr;
    snapshot.completed =
        implementation_->completed.load(std::memory_order_acquire) != nullptr;
    snapshot.starts = implementation_->starts.load(std::memory_order_relaxed);
    snapshot.replacements =
        implementation_->replacements.load(std::memory_order_relaxed);
    snapshot.completions =
        implementation_->completions.load(std::memory_order_relaxed);
    snapshot.cancellations =
        implementation_->cancellations.load(std::memory_order_relaxed);
    snapshot.maximumPendingDepth =
        implementation_->maximumPendingDepth.load(std::memory_order_relaxed);
    snapshot.workerThreadSnapshotRetirements =
        implementation_->workerThreadSnapshotRetirements.load(
            std::memory_order_relaxed);
    return snapshot;
}

}  // namespace xvatsim::modules::route_sector
