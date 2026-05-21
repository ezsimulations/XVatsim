#pragma once

#include <string>
#include <vector>

namespace xvatsim::core::source_data {

struct AuthoritySourceRegistryEntry {
    std::string source;
    std::string url;
    std::string positionsUrl;
    std::string airspaceUrl;
    std::string ownershipUrl;
    std::vector<std::string> regions;
};

struct MapDataManifest {
    bool valid = false;
    std::string currentCommitHash;
    std::string firBoundariesDatUrl;
    std::string firBoundariesGeoJsonUrl;
    std::string vatspyDatUrl;
    std::string simawareTraconGeoJsonUrl;
    std::string vatglassesOwnershipUrl;
    std::string vatglassesDynamicBaseUrl;
    std::string vatglassesPositionsUrl;
    std::string vatglassesAirspaceUrl;
    std::string vatglassesDynamicOwnershipUrl;
    std::string vatglassesDynamicOwnershipFile;
    std::string specialSectorDataUrl;
    std::vector<std::string> specialSectorDataUrls;
    std::string terminalAuthorityDataUrl;
    std::vector<std::string> terminalAuthorityDataUrls;
    std::string authoritySourceRegistryUrl;
    std::vector<std::string> authoritySourceRegistryUrls;
};

MapDataManifest ParseMapDataManifestJson(const std::string& payload);
MapDataManifest BuildFallbackMapDataManifest();
std::string BuildVatGlassesDynamicSourcePayload(
    const std::string& positionsJson,
    const std::string& airspaceJson,
    const std::string& ownershipJson);
std::string BuildAuthoritySourcePackagePayload(
    const std::string& primarySourceJson,
    const std::string& supplementalSourceJson);
std::string BuildAuthoritySourcePackagePayload(
    const std::string& primarySourceJson,
    const std::vector<std::string>& supplementalSourceJsons);
std::vector<AuthoritySourceRegistryEntry> ParseAuthoritySourceRegistryJson(
    const std::string& payload);

}  // namespace xvatsim::core::source_data
