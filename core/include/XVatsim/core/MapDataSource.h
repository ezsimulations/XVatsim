#pragma once

#include <string>

namespace xvatsim::core::source_data {

struct MapDataManifest {
    bool valid = false;
    std::string currentCommitHash;
    std::string firBoundariesDatUrl;
    std::string firBoundariesGeoJsonUrl;
    std::string vatspyDatUrl;
    std::string simawareTraconGeoJsonUrl;
    std::string vatglassesOwnershipUrl;
};

MapDataManifest ParseMapDataManifestJson(const std::string& payload);
MapDataManifest BuildFallbackMapDataManifest();

}  // namespace xvatsim::core::source_data
