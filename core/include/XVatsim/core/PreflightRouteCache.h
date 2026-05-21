#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::core::preflight {

constexpr int kPreflightRouteCacheSchemaVersion = 1;
constexpr const char* kPreflightRouteCacheFileName =
    "xvatsim_preflight_route_cache.json";
constexpr const char* kPreflightBuilderVersion = "1.0.0";
constexpr const char* kPreflightCompatibilityVersion = "1.0.0";
constexpr const char* kDefaultFmsPlansFolder =
    "C:\\X-Plane 12\\Output\\FMS plans";

struct FmsWaypoint {
    int type = 0;
    std::string ident;
    std::string via;
    double altitudeFt = 0.0;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
};

struct FmsPlan {
    std::string sourcePath;
    long long sourceModifiedUnixSeconds = 0;
    std::uintmax_t sourceSizeBytes = 0;
    std::string sourceContentHash;
    std::string cycle;
    std::string departureIcao;
    std::string destinationIcao;
    std::unordered_map<std::string, std::string> metadata;
    std::vector<FmsWaypoint> waypoints;
    std::string routeIdentityHash;
};

struct FmsParseResult {
    bool ok = false;
    FmsPlan plan;
    std::string message;
};

struct PreflightRouteCache {
    int schemaVersion = kPreflightRouteCacheSchemaVersion;
    std::string builderVersion = kPreflightBuilderVersion;
    std::string compatibilityVersion = kPreflightCompatibilityVersion;
    long long createdUnixSeconds = 0;
    FmsPlan plan;
    std::string authoritySourceRegistryHash;
    std::string boundaryDataHash;
    std::string airacIdentity;
    std::vector<std::string> routeAuthorityPolygonIds;
};

struct CacheValidationResult {
    bool accepted = false;
    std::string reason;
};

std::string Trim(std::string value);
std::string ToUpperCopy(std::string value);
std::string ComputeContentHashHex(const std::string& content);
long long GetFileModifiedUnixSeconds(const std::filesystem::path& path);

FmsParseResult ParseFmsPlanText(
    const std::string& content,
    const std::filesystem::path& sourcePath = {},
    long long modifiedUnixSeconds = 0,
    std::uintmax_t sourceSizeBytes = 0);

FmsParseResult LoadFmsPlanFile(const std::filesystem::path& sourcePath);

PreflightRouteCache BuildPreflightRouteCache(const FmsPlan& plan);

std::string SerializePreflightRouteCacheJson(const PreflightRouteCache& cache);

bool WritePreflightRouteCacheFile(
    const PreflightRouteCache& cache,
    const std::filesystem::path& cachePath,
    std::string* outError);

bool LoadPreflightRouteCacheFile(
    const std::filesystem::path& cachePath,
    PreflightRouteCache* outCache,
    std::string* outError);

CacheValidationResult ValidatePreflightRouteCacheForNetworkPlan(
    const PreflightRouteCache& cache,
    const brain::NetworkPlanSnapshot& networkPlanSnapshot,
    bool verifySourceFile);

std::vector<brain::RouteWaypointSnapshot> BuildRouteWaypointsFromCache(
    const PreflightRouteCache& cache);

std::filesystem::path BuildCachePathBesideExecutable(
    const std::filesystem::path& executablePath);

}  // namespace xvatsim::core::preflight
