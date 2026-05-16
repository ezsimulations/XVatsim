#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "XVatsim/brain/BrainOrchestrator.h"
#include "XVatsim/brain/BrainTypes.h"
#include "XVatsim/core/ControllerAuthority.h"
#include "XVatsim/core/MapDataSource.h"
#include "XVatsim/core/RouteGrammar.h"
#include "XVatsim/core/RouteResolution.h"
#include "XVatsim/core/RouteTraversal.h"
#include "XVatsim/core/WorkflowEngine.h"
#include "XVatsim/modules/arrival/ArrivalAirspaceModule.h"
#include "XVatsim/modules/arrival/ArrivalLocalModule.h"
#include "XVatsim/modules/departure/DepartureModule.h"
#include "XVatsim/modules/enroute/EnrouteModule.h"
#include "XVatsim/modules/route_sector/RouteSectorResolver.h"

namespace {

using xvatsim::brain::BoardSource;
using xvatsim::brain::BoardStationSnapshot;
using xvatsim::brain::ModuleBoardSnapshot;
using xvatsim::brain::StationRole;
using xvatsim::brain::WorkflowStage;
using xvatsim::core::workflow::FlightContext;
using xvatsim::core::workflow::HandoffDecision;
using xvatsim::core::workflow::WorkflowState;

struct ScenarioExpectations {
    struct WaypointPoint {
        std::string ident;
        double latitudeDeg = 0.0;
        double longitudeDeg = 0.0;
    };

    std::optional<WorkflowStage> stage;
    std::optional<std::string> reason;
    std::optional<bool> departureLocationConfirmed;
    std::optional<BoardSource> displaySource;
    std::vector<std::string> displayCallsigns;
    std::vector<std::string> overlayBodyLines;
    std::optional<bool> departureCollectedAvailable;
    std::vector<std::string> departureCollectedCallsigns;
    std::optional<bool> arrivalAirspaceAvailable;
    std::vector<std::string> arrivalAirspaceCallsigns;
    std::optional<bool> arrivalLocalAvailable;
    std::vector<std::string> arrivalLocalCallsigns;
    std::optional<bool> airportCoverageAvailable;
    std::optional<bool> airportTerminalInside;
    std::vector<std::string> airportCoverageMatchTokens;
    std::vector<std::string> airportCoverageControllerPrefixes;
    std::vector<std::string> airportCoverageGenerations;
    std::optional<bool> enrouteAvailable;
    std::vector<std::string> enrouteCallsigns;
    std::optional<bool> resolverRouteAvailable;
    std::optional<bool> resolverRouteResolved;
    std::optional<std::string> resolverRouteStatus;
    std::vector<std::string> resolverRouteAuthorityGaps;
    std::vector<std::string> resolverRouteCurrentSectors;
    std::vector<std::string> resolverRouteNextSectors;
    std::vector<std::string> resolverRouteCurrentControllerPatterns;
    std::vector<std::string> resolverRouteNextControllerPatterns;
    std::vector<std::string> resolverRouteCurrentControllerPrefixes;
    std::vector<std::string> resolverRouteNextControllerPrefixes;
    std::vector<std::string> resolverRouteGenerations;
    std::vector<std::string> authorityCatalogIds;
    std::vector<std::string> authorityDataGaps;
    std::vector<std::string> authorityActiveMatches;
    std::vector<std::string> authorityUnmappedCallsigns;
    std::vector<std::string> authorityPolygonIds;
    std::vector<std::string> authorityPolygonLookupKeys;
    std::vector<std::string> authorityPolygonRingCounts;
    std::vector<std::string> authorityPolygonDataGaps;
    std::vector<std::string> authorityActivePolygonMatches;
    std::vector<std::string> authorityActivePolygonDataGaps;
    std::vector<std::string> authorityRelevantPolygonMatches;
    std::optional<bool> sourceManifestValid;
    std::vector<std::string> sourceManifestValues;
    std::optional<bool> routeResolved;
    std::vector<std::string> routeCurrentSectors;
    std::vector<std::string> routeNextSectors;
    std::vector<std::string> routeCurrentControllerPrefixes;
    std::vector<std::string> routeNextControllerPrefixes;
    std::vector<std::string> routeTokenKinds;
    std::vector<std::string> resolvedWaypointIdents;
    std::vector<WaypointPoint> resolvedWaypointPoints;
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

struct TerminalCoverageFeatureSpec {
    std::string id;
    std::string name;
    std::string suffix;
    std::vector<std::string> prefixes;
    std::vector<xvatsim::core::route::SectorPolygon> polygons;
};

struct CenterCoverageFeatureSpec {
    std::string label;
    std::string name;
    std::string callsign;
    std::vector<std::string> tokens;
    std::vector<xvatsim::core::route::SectorPolygon> polygons;
};

struct ScenarioData {
    std::string name;
    double nowSeconds = 0.0;
    bool departureTerminalCoverageKnown = false;
    bool insideDepartureTerminalCoverage = false;
    xvatsim::core::workflow::WorkflowTuning tuning;
    WorkflowState workflowState;
    xvatsim::brain::AircraftStateSnapshot aircraftState;
    xvatsim::brain::FlightPlanSnapshot flightPlanSnapshot;
    xvatsim::brain::NetworkPlanSnapshot networkPlanSnapshot;
    xvatsim::brain::RadioStateSnapshot radioStateSnapshot;
    xvatsim::brain::XPilotSessionSnapshot xPilotSessionSnapshot;
    xvatsim::brain::TransceiverResolutionSnapshot transceiverResolutionSnapshot;
    std::optional<WorkflowStage> overlayWorkflowStage;
    xvatsim::brain::RouteSectorSnapshot routeSectorSnapshot;
    xvatsim::brain::AirportSectorSnapshot departureAirportSectorSnapshot;
    xvatsim::brain::AirportSectorSnapshot arrivalAirportSectorSnapshot;
    std::string airportCoverageBuildIcao;
    double airportCoverageBuildLatitudeDeg = 0.0;
    double airportCoverageBuildLongitudeDeg = 0.0;
    bool hasAirportCoverageBuildCoordinates = false;
    bool airportCoverageBuildsPreRefreshSnapshot = false;
    bool hasAirportTerminalProbeCoordinates = false;
    bool airportTerminalProbeUsesPreRefreshSnapshot = false;
    double airportTerminalProbeLatitudeDeg = 0.0;
    double airportTerminalProbeLongitudeDeg = 0.0;
    std::vector<CenterCoverageFeatureSpec> airportCoverageCenterFeatures;
    std::vector<TerminalCoverageFeatureSpec> airportCoverageTerminalFeatures;
    std::vector<std::string> airportCoverageAuthorityCatalogLines;
    std::vector<CenterCoverageFeatureSpec> pendingAirportCoverageCenterFeatures;
    std::vector<TerminalCoverageFeatureSpec> pendingAirportCoverageTerminalFeatures;
    std::vector<std::string> pendingAirportCoverageAuthorityCatalogLines;
    bool hasPendingAirportCoveragePayloads = false;
    bool resolveRouteWithResolver = false;
    bool resolverRouteBuildsPreRefreshSnapshot = false;
    std::vector<CenterCoverageFeatureSpec> resolverRouteCenterFeatures;
    std::vector<std::string> resolverRouteAuthorityCatalogLines;
    std::string resolverRouteOwnershipJson;
    std::vector<CenterCoverageFeatureSpec> pendingResolverRouteCenterFeatures;
    std::vector<std::string> pendingResolverRouteAuthorityCatalogLines;
    std::string pendingResolverRouteOwnershipJson;
    bool hasPendingResolverRoutePayloads = false;
    std::vector<std::string> authorityCatalogFirLines;
    std::vector<std::string> authorityCatalogUirLines;
    std::vector<xvatsim::core::authority::AuthorityPolygonSourceRecord>
        authorityPolygonRecords;
    std::vector<xvatsim::core::authority::AuthorityPositionSourceRecord>
        authorityPositionRecords;
    bool authorityEnrouteHandoff = false;
    std::vector<xvatsim::brain::ControllerSnapshot> controllers;
    std::optional<bool> controllerFeedAvailable;
    bool controllerFeedStale = false;
    bool forceControllerFeedEntries = false;
    std::string sourceManifestJson;
    xvatsim::core::route::AirwayGraph routeGraph;
    std::unordered_map<std::string, xvatsim::core::route::ProcedureCatalogEntry> proceduresByName;
    std::vector<xvatsim::brain::RouteWaypointSnapshot> routeWaypoints;
    std::vector<xvatsim::core::route::SectorFeature> traversalFeatures;
    xvatsim::core::route::TraversalTuning traversalTuning;
    ModuleBoardSnapshot departureBoard;
    ModuleBoardSnapshot arrivalBoard;
    ModuleBoardSnapshot enrouteBoard;
    ScenarioExpectations expectations;
};

bool AddCenterCoverageFeature(
    std::vector<CenterCoverageFeatureSpec>* features,
    const std::string& value);
bool AddAuthorityPolygonSourceRecord(
    std::vector<xvatsim::core::authority::AuthorityPolygonSourceRecord>* records,
    xvatsim::core::authority::AuthoritySource source,
    const std::string& value);
bool AddAuthorityPositionSourceRecord(
    std::vector<xvatsim::core::authority::AuthorityPositionSourceRecord>* records,
    xvatsim::core::authority::AuthoritySource source,
    const std::string& value);

std::string Trim(std::string value) {
    const auto notSpace = [](unsigned char ch) { return std::isspace(ch) == 0; };
    value.erase(
        value.begin(),
        std::find_if(value.begin(), value.end(), notSpace));
    value.erase(
        std::find_if(value.rbegin(), value.rend(), notSpace).base(),
        value.end());
    return value;
}

std::string ToUpperCopy(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return value;
}

std::vector<std::string> Split(const std::string& input, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream stream(input);
    std::string part;
    while (std::getline(stream, part, delimiter)) {
        parts.push_back(Trim(part));
    }
    return parts;
}

bool ParseBool(const std::string& value, bool* outValue) {
    if (outValue == nullptr) {
        return false;
    }

    const auto normalized = ToUpperCopy(Trim(value));
    if (normalized == "1" || normalized == "TRUE" || normalized == "YES" || normalized == "ON") {
        *outValue = true;
        return true;
    }
    if (normalized == "0" || normalized == "FALSE" || normalized == "NO" || normalized == "OFF") {
        *outValue = false;
        return true;
    }
    return false;
}

std::optional<double> ParseDouble(const std::string& value) {
    try {
        return std::stod(Trim(value));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<WorkflowStage> ParseWorkflowStage(const std::string& value) {
    const auto normalized = ToUpperCopy(Trim(value));
    if (normalized == "NONE") {
        return WorkflowStage::None;
    }
    if (normalized == "DEPARTURE") {
        return WorkflowStage::Departure;
    }
    if (normalized == "ENROUTE") {
        return WorkflowStage::Enroute;
    }
    if (normalized == "ARRIVAL") {
        return WorkflowStage::Arrival;
    }
    return std::nullopt;
}

std::optional<BoardSource> ParseBoardSource(const std::string& value) {
    const auto normalized = ToUpperCopy(Trim(value));
    if (normalized == "NONE") {
        return BoardSource::None;
    }
    if (normalized == "DEPARTURE") {
        return BoardSource::Departure;
    }
    if (normalized == "ARRIVAL") {
        return BoardSource::Arrival;
    }
    if (normalized == "ENROUTE") {
        return BoardSource::Enroute;
    }
    return std::nullopt;
}

std::optional<StationRole> ParseStationRole(const std::string& value) {
    const auto normalized = ToUpperCopy(Trim(value));
    if (normalized == "DELIVERY") {
        return StationRole::Delivery;
    }
    if (normalized == "GROUND") {
        return StationRole::Ground;
    }
    if (normalized == "TOWER") {
        return StationRole::Tower;
    }
    if (normalized == "DEPARTURE") {
        return StationRole::Departure;
    }
    if (normalized == "APPROACH") {
        return StationRole::Approach;
    }
    if (normalized == "CENTER") {
        return StationRole::Center;
    }
    if (normalized == "ATIS") {
        return StationRole::Atis;
    }
    if (normalized == "CTAF") {
        return StationRole::Ctaf;
    }
    if (normalized == "UNICOM") {
        return StationRole::Unicom;
    }
    if (normalized == "OTHER") {
        return StationRole::Other;
    }
    return std::nullopt;
}

std::optional<xvatsim::core::authority::AuthorityKind> ParseAuthorityKind(
    const std::string& value) {
    const auto normalized = ToUpperCopy(Trim(value));
    if (normalized == "CENTER" || normalized == "CTR") {
        return xvatsim::core::authority::AuthorityKind::Center;
    }
    if (normalized == "TERMINAL" || normalized == "TRACON" || normalized == "APPROACH") {
        return xvatsim::core::authority::AuthorityKind::Terminal;
    }
    if (normalized == "EXTENSION") {
        return xvatsim::core::authority::AuthorityKind::Extension;
    }
    return std::nullopt;
}

std::string WorkflowStageToString(WorkflowStage stage) {
    switch (stage) {
    case WorkflowStage::None:
        return "None";
    case WorkflowStage::Departure:
        return "Departure";
    case WorkflowStage::Enroute:
        return "Enroute";
    case WorkflowStage::Arrival:
        return "Arrival";
    }
    return "Unknown";
}

std::string BoardSourceToString(BoardSource source) {
    switch (source) {
    case BoardSource::None:
        return "None";
    case BoardSource::Departure:
        return "Departure";
    case BoardSource::Arrival:
        return "Arrival";
    case BoardSource::Enroute:
        return "Enroute";
    }
    return "Unknown";
}

std::vector<std::string> ExtractCallsigns(const ModuleBoardSnapshot& board) {
    std::vector<std::string> callsigns;
    callsigns.reserve(board.stations.size());
    for (const auto& station : board.stations) {
        callsigns.push_back(station.callsign);
    }
    return callsigns;
}

std::vector<std::string> ExtractOverlayBodyLines(
    const xvatsim::brain::OverlayViewModel& overlayModel) {
    std::vector<std::string> lines;
    lines.reserve(overlayModel.bodyLines.size());
    for (const auto& line : overlayModel.bodyLines) {
        lines.push_back(line.text);
    }
    return lines;
}

std::vector<std::string> ExtractSectorIdentifiers(
    const std::vector<xvatsim::brain::RouteSectorMatchSnapshot>& sectors) {
    std::vector<std::string> identifiers;
    identifiers.reserve(sectors.size());
    for (const auto& sector : sectors) {
        identifiers.push_back(sector.identifier);
    }
    return identifiers;
}

std::vector<std::string> ExtractSectorControllerPrefixes(
    const std::vector<xvatsim::brain::RouteSectorMatchSnapshot>& sectors) {
    std::vector<std::string> values;
    values.reserve(sectors.size());
    for (const auto& sector : sectors) {
        auto prefixes = sector.controllerPrefixes;
        std::sort(prefixes.begin(), prefixes.end());
        std::ostringstream stream;
        stream << sector.identifier << ":";
        for (std::size_t index = 0; index < prefixes.size(); ++index) {
            if (index > 0) {
                stream << ">";
            }
            stream << prefixes[index];
        }
        values.push_back(stream.str());
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractSectorControllerPatterns(
    const std::vector<xvatsim::brain::RouteSectorMatchSnapshot>& sectors) {
    std::vector<std::string> values;
    values.reserve(sectors.size());
    for (const auto& sector : sectors) {
        auto patterns = sector.controllerCallsignPatterns;
        std::sort(patterns.begin(), patterns.end());
        std::ostringstream stream;
        stream << sector.identifier << ":";
        for (std::size_t index = 0; index < patterns.size(); ++index) {
            if (index > 0) {
                stream << ">";
            }
            stream << patterns[index];
        }
        values.push_back(stream.str());
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractCoverageMatchTokens(
    const xvatsim::brain::AirportSectorSnapshot& snapshot) {
    std::vector<std::string> values;
    for (const auto& sector : snapshot.coveringSectors) {
        auto tokens = sector.matchTokens;
        std::sort(tokens.begin(), tokens.end());
        std::ostringstream stream;
        stream << sector.identifier << ":";
        for (std::size_t index = 0; index < tokens.size(); ++index) {
            if (index > 0) {
                stream << ">";
            }
            stream << tokens[index];
        }
        values.push_back(stream.str());
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractCoverageControllerPrefixes(
    const xvatsim::brain::AirportSectorSnapshot& snapshot) {
    std::vector<std::string> values;
    for (const auto& sector : snapshot.coveringSectors) {
        auto prefixes = sector.controllerPrefixes;
        std::sort(prefixes.begin(), prefixes.end());
        std::ostringstream stream;
        stream << sector.identifier << ":";
        for (std::size_t index = 0; index < prefixes.size(); ++index) {
            if (index > 0) {
                stream << ">";
            }
            stream << prefixes[index];
        }
        values.push_back(stream.str());
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractCoverageGenerations(
    const xvatsim::brain::AirportSectorSnapshot& snapshot) {
    return {
        "center:" + std::to_string(snapshot.centerBoundaryGeneration),
        "authority:" + std::to_string(snapshot.authorityCatalogGeneration),
        "terminal:" + std::to_string(snapshot.terminalCoverageGeneration),
    };
}

std::vector<std::string> ExtractRouteGenerations(
    const xvatsim::brain::RouteSectorSnapshot& snapshot) {
    return {
        "center:" + std::to_string(snapshot.centerBoundaryGeneration),
        "authority:" + std::to_string(snapshot.authorityCatalogGeneration),
    };
}

std::vector<std::string> ExtractAuthorityCatalogIds(
    const xvatsim::core::authority::ControllerAuthorityCatalog& catalog) {
    std::vector<std::string> values;
    values.reserve(catalog.authorities.size());
    for (const auto& authority : catalog.authorities) {
        values.push_back(authority.id);
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityDataGaps(
    const xvatsim::core::authority::ControllerAuthorityCatalog& catalog) {
    std::vector<std::string> values;
    values.reserve(catalog.dataGaps.size());
    for (const auto& gap : catalog.dataGaps) {
        values.push_back(
            gap.authorityId + ":" + gap.polygonKey + ":" + gap.reason);
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityActiveMatches(
    const std::vector<xvatsim::core::authority::ActiveControllerAuthority>& matches) {
    std::vector<std::string> values;
    values.reserve(matches.size());
    for (const auto& match : matches) {
        values.push_back(
            match.callsign + ":" +
            match.authorityId + ":" +
            match.polygonKey + ":" +
            match.matchedPattern);
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityPolygonIds(
    const xvatsim::core::authority::AuthorityPolygonCatalog& catalog) {
    std::vector<std::string> values;
    values.reserve(catalog.polygons.size());
    for (const auto& polygon : catalog.polygons) {
        values.push_back(polygon.id);
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityPolygonLookupKeys(
    const xvatsim::core::authority::AuthorityPolygonCatalog& catalog) {
    std::vector<std::string> values;
    values.reserve(catalog.polygons.size());
    for (const auto& polygon : catalog.polygons) {
        auto lookupKeys = polygon.lookupKeys;
        std::sort(lookupKeys.begin(), lookupKeys.end());
        std::ostringstream stream;
        stream << polygon.id << ":";
        for (std::size_t index = 0; index < lookupKeys.size(); ++index) {
            if (index > 0) {
                stream << ">";
            }
            stream << lookupKeys[index];
        }
        values.push_back(stream.str());
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityPolygonRingCounts(
    const xvatsim::core::authority::AuthorityPolygonCatalog& catalog) {
    std::vector<std::string> values;
    values.reserve(catalog.polygons.size());
    for (const auto& polygon : catalog.polygons) {
        values.push_back(polygon.id + ":" + std::to_string(polygon.rings.size()));
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityPolygonDataGaps(
    const xvatsim::core::authority::AuthorityPolygonCatalog& catalog) {
    std::vector<std::string> values;
    values.reserve(catalog.dataGaps.size());
    for (const auto& gap : catalog.dataGaps) {
        values.push_back(
            gap.authorityId + ":" + gap.polygonKey + ":" + gap.reason);
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityActivePolygonMatches(
    const std::vector<xvatsim::core::authority::ActiveAuthorityPolygon>& activePolygons) {
    std::vector<std::string> values;
    values.reserve(activePolygons.size());
    for (const auto& activePolygon : activePolygons) {
        values.push_back(
            activePolygon.callsign + ":" +
            activePolygon.authorityId + ":" +
            activePolygon.polygonId + ":" +
            activePolygon.polygonKey + ":" +
            activePolygon.matchedPattern);
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityActivePolygonDataGaps(
    const std::vector<xvatsim::core::authority::AuthorityDataGap>& dataGaps) {
    std::vector<std::string> values;
    values.reserve(dataGaps.size());
    for (const auto& gap : dataGaps) {
        values.push_back(
            gap.authorityId + ":" + gap.polygonKey + ":" + gap.reason);
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityRelevantPolygonMatches(
    const std::vector<xvatsim::core::authority::RelevantAuthorityPolygon>& relevantPolygons) {
    std::vector<std::string> values;
    values.reserve(relevantPolygons.size());
    for (const auto& relevantPolygon : relevantPolygons) {
        const auto roundedEntryNm =
            static_cast<int>(std::round(std::max(0.0, relevantPolygon.routeEntryDistanceNm)));
        values.push_back(
            relevantPolygon.activePolygon.callsign + ":" +
            relevantPolygon.activePolygon.authorityId + ":" +
            relevantPolygon.activePolygon.polygonId + ":aircraft=" +
            (relevantPolygon.aircraftInside ? "1" : "0") + ":route=" +
            (relevantPolygon.routeIntersects ? "1" : "0") + ":entry=" +
            std::to_string(roundedEntryNm));
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractSourceManifestValues(
    const xvatsim::core::source_data::MapDataManifest& manifest) {
    return {
        "commit:" + manifest.currentCommitHash,
        "dat:" + manifest.firBoundariesDatUrl,
        "geojson:" + manifest.firBoundariesGeoJsonUrl,
        "simaware:" + manifest.simawareTraconGeoJsonUrl,
        "vatspy:" + manifest.vatspyDatUrl,
        "vatglasses:" + manifest.vatglassesOwnershipUrl,
    };
}

xvatsim::brain::AuthorityRelevanceKind ToBrainAuthorityKind(
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

std::vector<std::string> ExtractAuthorityGaps(
    const xvatsim::brain::RouteSectorSnapshot& snapshot) {
    std::vector<std::string> gaps;
    auto appendGaps = [&](const auto& sectors, const std::string& label) {
        for (const auto& sector : sectors) {
            if (!sector.controllerCallsignPatterns.empty() ||
                !sector.controllerPrefixes.empty()) {
                continue;
            }
            gaps.push_back(label + ":" + sector.identifier);
        }
    };

    appendGaps(snapshot.currentSectors, "current");
    appendGaps(snapshot.nextSectors, "next");
    return gaps;
}

std::vector<std::string> ExtractTokenKinds(
    const std::vector<xvatsim::core::route::ParsedRouteToken>& tokens) {
    std::vector<std::string> values;
    values.reserve(tokens.size());
    for (const auto& token : tokens) {
        values.push_back(
            token.normalized + ":" +
            xvatsim::core::route::RouteTokenKindToString(token.kind));
    }
    return values;
}

std::vector<std::string> ExtractWaypointIdents(
    const std::vector<xvatsim::brain::RouteWaypointSnapshot>& waypoints) {
    std::vector<std::string> identifiers;
    identifiers.reserve(waypoints.size());
    for (const auto& waypoint : waypoints) {
        identifiers.push_back(waypoint.ident);
    }
    return identifiers;
}

std::string FormatWaypointPoint(const xvatsim::brain::RouteWaypointSnapshot& waypoint) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(4);
    stream << waypoint.ident << "@" << waypoint.latitudeDeg << "," << waypoint.longitudeDeg;
    return stream.str();
}

std::vector<std::string> ExtractWaypointPoints(
    const std::vector<xvatsim::brain::RouteWaypointSnapshot>& waypoints) {
    std::vector<std::string> points;
    points.reserve(waypoints.size());
    for (const auto& waypoint : waypoints) {
        points.push_back(FormatWaypointPoint(waypoint));
    }
    return points;
}

std::vector<std::string> ExtractRouteDiagnostics(
    const std::vector<std::string>& tokens) {
    return tokens;
}

bool AssignScenarioProperty(ScenarioData* scenario, const std::string& key, const std::string& value) {
    if (scenario == nullptr) {
        return false;
    }

    if (key == "name") {
        scenario->name = value;
        return true;
    }
    if (key == "now_seconds") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->nowSeconds = *parsed;
        return true;
    }
    if (key == "departure.inside_terminal_coverage") {
        return ParseBool(value, &scenario->insideDepartureTerminalCoverage);
    }
    if (key == "departure.terminal_coverage_known") {
        return ParseBool(value, &scenario->departureTerminalCoverageKnown);
    }
    if (key == "departure.coverage.available") {
        return ParseBool(value, &scenario->departureAirportSectorSnapshot.available);
    }
    if (key == "departure.coverage.stale") {
        return ParseBool(value, &scenario->departureAirportSectorSnapshot.stale);
    }
    if (key == "departure.coverage.has_center") {
        return ParseBool(value, &scenario->departureAirportSectorSnapshot.hasCenterCoverageData);
    }
    if (key == "departure.coverage.has_terminal") {
        return ParseBool(
            value,
            &scenario->departureAirportSectorSnapshot.hasTerminalCoverageData);
    }
    if (key == "arrival.coverage.available") {
        return ParseBool(value, &scenario->arrivalAirportSectorSnapshot.available);
    }
    if (key == "arrival.coverage.stale") {
        return ParseBool(value, &scenario->arrivalAirportSectorSnapshot.stale);
    }
    if (key == "arrival.coverage.has_center") {
        return ParseBool(value, &scenario->arrivalAirportSectorSnapshot.hasCenterCoverageData);
    }
    if (key == "arrival.coverage.has_terminal") {
        return ParseBool(value, &scenario->arrivalAirportSectorSnapshot.hasTerminalCoverageData);
    }
    if (key == "airport.coverage_builder_icao") {
        scenario->airportCoverageBuildIcao = value;
        return true;
    }
    if (key == "airport.coverage_builder_lat") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->airportCoverageBuildLatitudeDeg = *parsed;
        scenario->hasAirportCoverageBuildCoordinates = true;
        return true;
    }
    if (key == "airport.coverage_builder_lon") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->airportCoverageBuildLongitudeDeg = *parsed;
        scenario->hasAirportCoverageBuildCoordinates = true;
        return true;
    }
    if (key == "airport.coverage_builds_pre_refresh_snapshot") {
        return ParseBool(value, &scenario->airportCoverageBuildsPreRefreshSnapshot);
    }
    if (key == "airport.terminal_probe_lat") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->airportTerminalProbeLatitudeDeg = *parsed;
        scenario->hasAirportTerminalProbeCoordinates = true;
        return true;
    }
    if (key == "airport.terminal_probe_lon") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->airportTerminalProbeLongitudeDeg = *parsed;
        scenario->hasAirportTerminalProbeCoordinates = true;
        return true;
    }
    if (key == "airport.terminal_probe_uses_pre_refresh_snapshot") {
        return ParseBool(value, &scenario->airportTerminalProbeUsesPreRefreshSnapshot);
    }
    if (key == "airport.authority_catalog_fir") {
        scenario->airportCoverageAuthorityCatalogLines.push_back(value);
        return true;
    }
    if (key == "airport.pending_authority_catalog_fir") {
        scenario->pendingAirportCoverageAuthorityCatalogLines.push_back(value);
        scenario->hasPendingAirportCoveragePayloads = true;
        return true;
    }
    if (key == "resolver.route_resolve") {
        return ParseBool(value, &scenario->resolveRouteWithResolver);
    }
    if (key == "resolver.route_builds_pre_refresh_snapshot") {
        return ParseBool(value, &scenario->resolverRouteBuildsPreRefreshSnapshot);
    }
    if (key == "resolver.center_feature") {
        return AddCenterCoverageFeature(&scenario->resolverRouteCenterFeatures, value);
    }
    if (key == "resolver.authority_catalog_fir") {
        scenario->resolverRouteAuthorityCatalogLines.push_back(value);
        return true;
    }
    if (key == "resolver.ownership_json") {
        scenario->resolverRouteOwnershipJson = value;
        return true;
    }
    if (key == "resolver.pending_center_feature") {
        if (!AddCenterCoverageFeature(&scenario->pendingResolverRouteCenterFeatures, value)) {
            return false;
        }
        scenario->hasPendingResolverRoutePayloads = true;
        return true;
    }
    if (key == "resolver.pending_authority_catalog_fir") {
        scenario->pendingResolverRouteAuthorityCatalogLines.push_back(value);
        scenario->hasPendingResolverRoutePayloads = true;
        return true;
    }
    if (key == "resolver.pending_ownership_json") {
        scenario->pendingResolverRouteOwnershipJson = value;
        scenario->hasPendingResolverRoutePayloads = true;
        return true;
    }
    if (key == "authority_catalog.fir") {
        scenario->authorityCatalogFirLines.push_back(value);
        return true;
    }
    if (key == "authority_catalog.uir") {
        scenario->authorityCatalogUirLines.push_back(value);
        return true;
    }
    if (key == "authority.enroute_handoff") {
        return ParseBool(value, &scenario->authorityEnrouteHandoff);
    }
    if (key == "authority_position.vatglasses") {
        return AddAuthorityPositionSourceRecord(
            &scenario->authorityPositionRecords,
            xvatsim::core::authority::AuthoritySource::VatGlasses,
            value);
    }
    if (key == "authority_position.extension") {
        return AddAuthorityPositionSourceRecord(
            &scenario->authorityPositionRecords,
            xvatsim::core::authority::AuthoritySource::VatsimRadarExtension,
            value);
    }
    if (key == "authority_position_json.vatglasses") {
        auto records = xvatsim::core::authority::ParseAuthorityPositionSourceRecordsJson(
            xvatsim::core::authority::AuthoritySource::VatGlasses,
            value);
        scenario->authorityPositionRecords.insert(
            scenario->authorityPositionRecords.end(),
            std::make_move_iterator(records.begin()),
            std::make_move_iterator(records.end()));
        return true;
    }
    if (key == "authority_position_json.extension") {
        auto records = xvatsim::core::authority::ParseAuthorityPositionSourceRecordsJson(
            xvatsim::core::authority::AuthoritySource::VatsimRadarExtension,
            value);
        scenario->authorityPositionRecords.insert(
            scenario->authorityPositionRecords.end(),
            std::make_move_iterator(records.begin()),
            std::make_move_iterator(records.end()));
        return true;
    }
    if (key == "authority_polygon.vatspy") {
        return AddAuthorityPolygonSourceRecord(
            &scenario->authorityPolygonRecords,
            xvatsim::core::authority::AuthoritySource::VatSpyBoundary,
            value);
    }
    if (key == "authority_polygon.tracon") {
        return AddAuthorityPolygonSourceRecord(
            &scenario->authorityPolygonRecords,
            xvatsim::core::authority::AuthoritySource::SimAwareTracon,
            value);
    }
    if (key == "authority_polygon.vatglasses") {
        return AddAuthorityPolygonSourceRecord(
            &scenario->authorityPolygonRecords,
            xvatsim::core::authority::AuthoritySource::VatGlasses,
            value);
    }
    if (key == "authority_polygon.extension") {
        return AddAuthorityPolygonSourceRecord(
            &scenario->authorityPolygonRecords,
            xvatsim::core::authority::AuthoritySource::VatsimRadarExtension,
            value);
    }
    if (key == "source_manifest.json") {
        scenario->sourceManifestJson = value;
        return true;
    }
    if (key == "tuning.arrival_wake_distance_nm") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->tuning.arrivalWakeDistanceNm = *parsed;
        return true;
    }
    if (key == "tuning.departure_confirm_distance_nm") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->tuning.departureConfirmDistanceNm = *parsed;
        return true;
    }
    if (key == "tuning.destination_ground_distance_nm") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->tuning.destinationGroundDistanceNm = *parsed;
        return true;
    }
    if (key == "tuning.departure_release_hold_seconds") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->tuning.departureReleaseHoldSeconds = *parsed;
        return true;
    }
    if (key == "traversal.route_sample_step_nm") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->traversalTuning.routeSampleStepNm = *parsed;
        return true;
    }
    if (key == "traversal.mode") {
        const auto normalized = ToUpperCopy(Trim(value));
        if (normalized == "EXACT") {
            scenario->traversalTuning.mode = xvatsim::core::route::TraversalMode::Exact;
            return true;
        }
        if (normalized == "SAMPLED") {
            scenario->traversalTuning.mode = xvatsim::core::route::TraversalMode::Sampled;
            return true;
        }
        if (normalized != "EXACT" && normalized != "SAMPLED") {
            return false;
        }
    }
    if (key == "traversal.route_sector_sanity_limit") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->traversalTuning.routeSectorSanityLimit = static_cast<std::size_t>(*parsed);
        return true;
    }
    if (key == "state.flight_active") {
        return ParseBool(value, &scenario->workflowState.flightContext.active);
    }
    if (key == "state.departure_released") {
        return ParseBool(value, &scenario->workflowState.departureReleasedThisFlight);
    }
    if (key == "state.arrival_awake") {
        return ParseBool(value, &scenario->workflowState.arrivalAwakeThisFlight);
    }
    if (key == "state.airborne_since_seconds") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->workflowState.airborneSinceSeconds = *parsed;
        return true;
    }
    if (key == "flight.callsign") {
        scenario->workflowState.flightContext.callsign = value;
        return true;
    }
    if (key == "flight.departure_icao") {
        scenario->workflowState.flightContext.departureIcao = value;
        return true;
    }
    if (key == "flight.departure_lat") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->workflowState.flightContext.departureLatDeg = *parsed;
        return true;
    }
    if (key == "flight.departure_lon") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->workflowState.flightContext.departureLonDeg = *parsed;
        return true;
    }
    if (key == "flight.has_departure_coordinates") {
        return ParseBool(value, &scenario->workflowState.flightContext.hasDepartureCoordinates);
    }
    if (key == "flight.destination_icao") {
        scenario->workflowState.flightContext.destinationIcao = value;
        return true;
    }
    if (key == "flight.destination_lat") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->workflowState.flightContext.destinationLatDeg = *parsed;
        return true;
    }
    if (key == "flight.destination_lon") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->workflowState.flightContext.destinationLonDeg = *parsed;
        return true;
    }
    if (key == "flight.has_destination_coordinates") {
        return ParseBool(value, &scenario->workflowState.flightContext.hasDestinationCoordinates);
    }
    if (key == "flight.route_text") {
        scenario->workflowState.flightContext.routeText = value;
        return true;
    }
    if (key == "aircraft.valid") {
        return ParseBool(value, &scenario->aircraftState.valid);
    }
    if (key == "aircraft.on_ground") {
        return ParseBool(value, &scenario->aircraftState.onGround);
    }
    if (key == "aircraft.battery_on") {
        return ParseBool(value, &scenario->aircraftState.batteryOn);
    }
    if (key == "aircraft.latitude_deg") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->aircraftState.latitudeDeg = *parsed;
        return true;
    }
    if (key == "aircraft.longitude_deg") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->aircraftState.longitudeDeg = *parsed;
        return true;
    }
    if (key == "radio.com1_active") {
        scenario->radioStateSnapshot.com1ActiveFrequency = value;
        return true;
    }
    if (key == "radio.com2_active") {
        scenario->radioStateSnapshot.com2ActiveFrequency = value;
        return true;
    }
    if (key == "xpilot.connected") {
        return ParseBool(value, &scenario->xPilotSessionSnapshot.connected);
    }
    if (key == "plan.matched") {
        return ParseBool(value, &scenario->networkPlanSnapshot.matched);
    }
    if (key == "plan.stale") {
        return ParseBool(value, &scenario->networkPlanSnapshot.stale);
    }
    if (key == "plan.departure_icao") {
        scenario->networkPlanSnapshot.departureIcao = value;
        return true;
    }
    if (key == "plan.departure_lat") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->networkPlanSnapshot.departureLatDeg = *parsed;
        return true;
    }
    if (key == "plan.departure_lon") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->networkPlanSnapshot.departureLonDeg = *parsed;
        return true;
    }
    if (key == "plan.has_departure_coordinates") {
        return ParseBool(value, &scenario->networkPlanSnapshot.hasDepartureCoordinates);
    }
    if (key == "plan.destination_icao") {
        scenario->networkPlanSnapshot.destinationIcao = value;
        return true;
    }
    if (key == "plan.destination_lat") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->networkPlanSnapshot.destinationLatDeg = *parsed;
        return true;
    }
    if (key == "plan.destination_lon") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->networkPlanSnapshot.destinationLonDeg = *parsed;
        return true;
    }
    if (key == "plan.has_destination_coordinates") {
        return ParseBool(value, &scenario->networkPlanSnapshot.hasDestinationCoordinates);
    }
    if (key == "plan.route_text") {
        scenario->networkPlanSnapshot.routeText = value;
        return true;
    }
    if (key == "route.stale") {
        return ParseBool(value, &scenario->routeSectorSnapshot.stale);
    }
    if (key == "fms.current_airport_icao") {
        scenario->flightPlanSnapshot.currentAirportIcao = value;
        return true;
    }
    if (key == "expect.stage") {
        scenario->expectations.stage = ParseWorkflowStage(value);
        return scenario->expectations.stage.has_value();
    }
    if (key == "expect.reason") {
        scenario->expectations.reason = value;
        return true;
    }
    if (key == "expect.departure_location_confirmed") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.departureLocationConfirmed = parsed;
        return true;
    }
    if (key == "expect.display_source") {
        scenario->expectations.displaySource = ParseBoardSource(value);
        return scenario->expectations.displaySource.has_value();
    }
    if (key == "expect.display_callsigns") {
        scenario->expectations.displayCallsigns = Split(value, ',');
        return true;
    }
    if (key == "expect.overlay_body_lines") {
        scenario->expectations.overlayBodyLines = Split(value, '|');
        return true;
    }
    if (key == "expect.departure_collected_available") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.departureCollectedAvailable = parsed;
        return true;
    }
    if (key == "expect.departure_collected_callsigns") {
        scenario->expectations.departureCollectedCallsigns = Split(value, ',');
        return true;
    }
    if (key == "expect.arrival_airspace_available") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.arrivalAirspaceAvailable = parsed;
        return true;
    }
    if (key == "expect.arrival_airspace_callsigns") {
        scenario->expectations.arrivalAirspaceCallsigns = Split(value, ',');
        return true;
    }
    if (key == "expect.arrival_local_available") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.arrivalLocalAvailable = parsed;
        return true;
    }
    if (key == "expect.arrival_local_callsigns") {
        scenario->expectations.arrivalLocalCallsigns = Split(value, ',');
        return true;
    }
    if (key == "expect.airport_coverage_available") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.airportCoverageAvailable = parsed;
        return true;
    }
    if (key == "expect.airport_terminal_inside") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.airportTerminalInside = parsed;
        return true;
    }
    if (key == "expect.airport_coverage_match_tokens") {
        scenario->expectations.airportCoverageMatchTokens = Split(value, ',');
        return true;
    }
    if (key == "expect.airport_coverage_controller_prefixes") {
        scenario->expectations.airportCoverageControllerPrefixes = Split(value, ',');
        return true;
    }
    if (key == "expect.airport_coverage_generations") {
        scenario->expectations.airportCoverageGenerations = Split(value, ',');
        return true;
    }
    if (key == "expect.enroute_available") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.enrouteAvailable = parsed;
        return true;
    }
    if (key == "expect.enroute_callsigns") {
        scenario->expectations.enrouteCallsigns = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_route_available") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.resolverRouteAvailable = parsed;
        return true;
    }
    if (key == "expect.resolver_route_resolved") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.resolverRouteResolved = parsed;
        return true;
    }
    if (key == "expect.resolver_route_status") {
        scenario->expectations.resolverRouteStatus = value;
        return true;
    }
    if (key == "expect.resolver_route_authority_gaps") {
        scenario->expectations.resolverRouteAuthorityGaps = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_route_current_sectors") {
        scenario->expectations.resolverRouteCurrentSectors = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_route_next_sectors") {
        scenario->expectations.resolverRouteNextSectors = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_route_current_controller_patterns") {
        scenario->expectations.resolverRouteCurrentControllerPatterns = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_route_next_controller_patterns") {
        scenario->expectations.resolverRouteNextControllerPatterns = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_route_current_controller_prefixes") {
        scenario->expectations.resolverRouteCurrentControllerPrefixes = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_route_next_controller_prefixes") {
        scenario->expectations.resolverRouteNextControllerPrefixes = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_route_generations") {
        scenario->expectations.resolverRouteGenerations = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_catalog_ids") {
        scenario->expectations.authorityCatalogIds = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_data_gaps") {
        scenario->expectations.authorityDataGaps = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_active_matches") {
        scenario->expectations.authorityActiveMatches = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_unmapped_callsigns") {
        scenario->expectations.authorityUnmappedCallsigns = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_polygon_ids") {
        scenario->expectations.authorityPolygonIds = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_polygon_lookup_keys") {
        scenario->expectations.authorityPolygonLookupKeys = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_polygon_ring_counts") {
        scenario->expectations.authorityPolygonRingCounts = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_polygon_data_gaps") {
        scenario->expectations.authorityPolygonDataGaps = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_active_polygon_matches") {
        scenario->expectations.authorityActivePolygonMatches = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_active_polygon_data_gaps") {
        scenario->expectations.authorityActivePolygonDataGaps = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_relevant_polygon_matches") {
        scenario->expectations.authorityRelevantPolygonMatches = Split(value, ',');
        return true;
    }
    if (key == "expect.source_manifest_valid") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.sourceManifestValid = parsed;
        return true;
    }
    if (key == "expect.source_manifest_values") {
        scenario->expectations.sourceManifestValues = Split(value, ',');
        return true;
    }
    if (key == "expect.route_resolved") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.routeResolved = parsed;
        return true;
    }
    if (key == "expect.route_current_sectors") {
        scenario->expectations.routeCurrentSectors = Split(value, ',');
        return true;
    }
    if (key == "expect.route_next_sectors") {
        scenario->expectations.routeNextSectors = Split(value, ',');
        return true;
    }
    if (key == "expect.route_current_controller_prefixes") {
        scenario->expectations.routeCurrentControllerPrefixes = Split(value, ',');
        return true;
    }
    if (key == "expect.route_next_controller_prefixes") {
        scenario->expectations.routeNextControllerPrefixes = Split(value, ',');
        return true;
    }
    if (key == "expect.route_token_kinds") {
        scenario->expectations.routeTokenKinds = Split(value, ',');
        return true;
    }
    if (key == "expect.resolved_waypoints") {
        scenario->expectations.resolvedWaypointIdents = Split(value, ',');
        return true;
    }
    if (key == "expect.resolved_waypoint_points") {
        scenario->expectations.resolvedWaypointPoints.clear();
        for (const auto& part : Split(value, '|')) {
            const auto atIndex = part.find('@');
            if (atIndex == std::string::npos) {
                return false;
            }
            const auto ident = Trim(part.substr(0, atIndex));
            const auto coordinateParts = Split(part.substr(atIndex + 1), ',');
            if (ident.empty() || coordinateParts.size() != 2) {
                return false;
            }

            const auto latitudeDeg = ParseDouble(coordinateParts[0]);
            const auto longitudeDeg = ParseDouble(coordinateParts[1]);
            if (!latitudeDeg.has_value() || !longitudeDeg.has_value()) {
                return false;
            }

            scenario->expectations.resolvedWaypointPoints.push_back({
                ident,
                *latitudeDeg,
                *longitudeDeg,
            });
        }
        return true;
    }
    if (key == "expect.resolved_tokens") {
        scenario->expectations.resolvedTokens = Split(value, ',');
        return true;
    }
    if (key == "expect.expanded_tokens") {
        scenario->expectations.expandedTokens = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_tokens") {
        scenario->expectations.recognizedProcedureTokens = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_sources") {
        scenario->expectations.procedureMetadataSources = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_records") {
        scenario->expectations.procedureRecordKinds = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_runways") {
        scenario->expectations.procedureRunwayRecords = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_authorities") {
        scenario->expectations.procedureCatalogAuthorities = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_catalog_fixes") {
        scenario->expectations.procedureCatalogFixes = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_boundary_fixes") {
        scenario->expectations.procedureBoundaryFixes = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_ordered_fixes") {
        scenario->expectations.procedureOrderedFixes = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_synthetic_waypoints") {
        scenario->expectations.procedureSyntheticWaypoints = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_synthetic_sources") {
        scenario->expectations.procedureSyntheticSources = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_application_states") {
        scenario->expectations.procedureApplicationStates = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_application_blocks") {
        scenario->expectations.procedureApplicationBlocks = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_applied_fix_sequences") {
        scenario->expectations.procedureAppliedFixSequences = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_catalog_transitions") {
        scenario->expectations.procedureCatalogTransitions = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_support") {
        scenario->expectations.procedureSupportDirections = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_links") {
        scenario->expectations.procedureTransitionLinks = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_misses") {
        scenario->expectations.procedureTransitionMisses = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_anchor_links") {
        scenario->expectations.procedureAnchorLinks = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_context_only") {
        scenario->expectations.procedureContextOnlyTokens = Split(value, ',');
        return true;
    }
    if (key == "expect.ignored_tokens") {
        scenario->expectations.ignoredTokens = Split(value, ',');
        return true;
    }
    if (key == "expect.unsupported_tokens") {
        scenario->expectations.unsupportedTokens = Split(value, ',');
        return true;
    }
    if (key == "expect.unresolved_tokens") {
        scenario->expectations.unresolvedTokens = Split(value, ',');
        return true;
    }

    return false;
}

bool AddStation(ModuleBoardSnapshot* board, const std::string& value) {
    if (board == nullptr) {
        return false;
    }

    BoardStationSnapshot station;
    bool hasRole = false;
    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "role") {
            const auto parsedRole = ParseStationRole(fieldValue);
            if (!parsedRole.has_value()) {
                return false;
            }
            station.role = *parsedRole;
            hasRole = true;
        } else if (field == "callsign") {
            station.callsign = fieldValue;
        } else if (field == "frequency") {
            station.frequency = fieldValue;
        } else if (field == "annotation") {
            station.annotation = fieldValue;
        } else if (field == "tuned") {
            if (!ParseBool(fieldValue, &station.tuned)) {
                return false;
            }
        } else if (field == "next") {
            if (!ParseBool(fieldValue, &station.next)) {
                return false;
            }
        } else if (field == "standby") {
            if (!ParseBool(fieldValue, &station.standby)) {
                return false;
            }
        } else if (field == "sectorActive") {
            if (!ParseBool(fieldValue, &station.sectorActive)) {
                return false;
            }
        } else if (field == "online") {
            if (!ParseBool(fieldValue, &station.online)) {
                return false;
            }
        } else if (field == "offline") {
            if (!ParseBool(fieldValue, &station.offline)) {
                return false;
            }
        } else if (field == "routeEntryDistanceNm") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            station.hasRouteEntryDistance = true;
            station.routeEntryDistanceNm = *parsed;
        }
    }

    if (!hasRole || station.callsign.empty()) {
        return false;
    }

    board->stations.push_back(station);
    board->available = true;
    board->displayStations = true;
    return true;
}

bool AddRouteSector(
    std::vector<xvatsim::brain::RouteSectorMatchSnapshot>* sectors,
    const std::string& value) {
    if (sectors == nullptr) {
        return false;
    }

    xvatsim::brain::RouteSectorMatchSnapshot sector;
    bool hasIdentifier = false;
    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "identifier") {
            sector.identifier = fieldValue;
            hasIdentifier = true;
        } else if (field == "entryDistanceNm") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            sector.entryDistanceNm = *parsed;
        } else if (field == "matchTokens") {
            sector.matchTokens = Split(fieldValue, ',');
        } else if (field == "controllerPatterns" ||
                   field == "controllerCallsignPatterns") {
            sector.controllerCallsignPatterns = Split(fieldValue, ',');
        } else if (field == "controllerPrefixes") {
            sector.controllerPrefixes = Split(fieldValue, ',');
        } else if (field == "centerCoverage") {
            if (!ParseBool(fieldValue, &sector.centerCoverage)) {
                return false;
            }
        } else if (field == "terminalCoverage") {
            if (!ParseBool(fieldValue, &sector.terminalCoverage)) {
                return false;
            }
        } else if (field == "coverage") {
            const auto normalizedCoverage = ToUpperCopy(Trim(fieldValue));
            if (normalizedCoverage == "CENTER") {
                sector.centerCoverage = true;
            } else if (normalizedCoverage == "TERMINAL") {
                sector.terminalCoverage = true;
            } else {
                return false;
            }
        }
    }

    if (!hasIdentifier) {
        return false;
    }

    sectors->push_back(std::move(sector));
    return true;
}

bool AddController(
    std::vector<xvatsim::brain::ControllerSnapshot>* controllers,
    const std::string& value) {
    if (controllers == nullptr) {
        return false;
    }

    xvatsim::brain::ControllerSnapshot controller;
    bool hasCallsign = false;
    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "callsign") {
            controller.callsign = fieldValue;
            hasCallsign = true;
        } else if (field == "frequency") {
            controller.frequency = fieldValue;
        } else if (field == "facility") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            controller.facility = static_cast<int>(*parsed);
        } else if (field == "actionable") {
            if (!ParseBool(fieldValue, &controller.actionable)) {
                return false;
            }
        } else if (field == "atis") {
            if (!ParseBool(fieldValue, &controller.atis)) {
                return false;
            }
        }
    }

    if (!hasCallsign) {
        return false;
    }

    controllers->push_back(std::move(controller));
    return true;
}

bool AddTransceiverCandidate(
    xvatsim::brain::TransceiverResolutionSnapshot* snapshot,
    const std::string& value) {
    if (snapshot == nullptr) {
        return false;
    }

    xvatsim::brain::ReceivableControllerSnapshot candidate;
    bool hasCallsign = false;
    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "callsign") {
            candidate.callsign = fieldValue;
            hasCallsign = true;
        } else if (field == "frequency") {
            candidate.frequency = fieldValue;
        } else if (field == "distanceNm") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            candidate.distanceNm = *parsed;
        } else if (field == "score") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            candidate.score = *parsed;
        }
    }

    if (!hasCallsign) {
        return false;
    }

    snapshot->candidates.push_back(std::move(candidate));
    snapshot->receivableControllers = static_cast<int>(snapshot->candidates.size());
    return true;
}

bool AddRouteWaypoint(
    std::vector<xvatsim::brain::RouteWaypointSnapshot>* waypoints,
    const std::string& value) {
    if (waypoints == nullptr) {
        return false;
    }

    xvatsim::brain::RouteWaypointSnapshot waypoint;
    bool hasIdent = false;
    bool hasLat = false;
    bool hasLon = false;
    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "ident") {
            waypoint.ident = fieldValue;
            hasIdent = true;
        } else if (field == "lat") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            waypoint.latitudeDeg = *parsed;
            hasLat = true;
        } else if (field == "lon") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            waypoint.longitudeDeg = *parsed;
            hasLon = true;
        }
    }

    if (!hasIdent || !hasLat || !hasLon) {
        return false;
    }

    waypoints->push_back(std::move(waypoint));
    return true;
}

bool AddGraphNodeEntry(
    xvatsim::core::route::AirwayGraph* graph,
    const std::string& value) {
    if (graph == nullptr) {
        return false;
    }

    std::string ident;
    std::string region;
    int navDataType = 0;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    bool hasIdent = false;
    bool hasRegion = false;
    bool hasType = false;
    bool hasLatitude = false;
    bool hasLongitude = false;

    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "ident") {
            ident = fieldValue;
            hasIdent = true;
        } else if (field == "region") {
            region = fieldValue;
            hasRegion = true;
        } else if (field == "type") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            navDataType = static_cast<int>(*parsed);
            hasType = true;
        } else if (field == "lat") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            latitudeDeg = *parsed;
            hasLatitude = true;
        } else if (field == "lon") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            longitudeDeg = *parsed;
            hasLongitude = true;
        }
    }

    if (!hasIdent || !hasRegion || !hasType || !hasLatitude || !hasLongitude) {
        return false;
    }

    xvatsim::core::route::AddGraphNode(
        ident,
        region,
        navDataType,
        latitudeDeg,
        longitudeDeg,
        graph);
    return true;
}

bool AddGraphEdgeEntry(
    xvatsim::core::route::AirwayGraph* graph,
    const std::string& value) {
    if (graph == nullptr) {
        return false;
    }

    std::string startIdent;
    std::string startRegion;
    int startNavDataType = 0;
    std::string endIdent;
    std::string endRegion;
    int endNavDataType = 0;
    std::string airwayName;
    std::string direction = "N";
    bool hasStartIdent = false;
    bool hasStartRegion = false;
    bool hasStartType = false;
    bool hasEndIdent = false;
    bool hasEndRegion = false;
    bool hasEndType = false;
    bool hasAirway = false;

    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "startIdent") {
            startIdent = fieldValue;
            hasStartIdent = true;
        } else if (field == "startRegion") {
            startRegion = fieldValue;
            hasStartRegion = true;
        } else if (field == "startType") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            startNavDataType = static_cast<int>(*parsed);
            hasStartType = true;
        } else if (field == "endIdent") {
            endIdent = fieldValue;
            hasEndIdent = true;
        } else if (field == "endRegion") {
            endRegion = fieldValue;
            hasEndRegion = true;
        } else if (field == "endType") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            endNavDataType = static_cast<int>(*parsed);
            hasEndType = true;
        } else if (field == "airway") {
            airwayName = fieldValue;
            hasAirway = true;
        } else if (field == "direction") {
            direction = ToUpperCopy(fieldValue);
        }
    }

    if (!hasStartIdent || !hasStartRegion || !hasStartType ||
        !hasEndIdent || !hasEndRegion || !hasEndType || !hasAirway) {
        return false;
    }

    const auto addForward = direction != "B";
    const auto addBackward = direction == "N" || direction == "B";
    return xvatsim::core::route::AddAirwayConnection(
        startIdent,
        startRegion,
        startNavDataType,
        endIdent,
        endRegion,
        endNavDataType,
        airwayName,
        addForward,
        addBackward,
        graph);
}

bool AddTraversalFeature(
    std::vector<xvatsim::core::route::SectorFeature>* features,
    const std::string& value) {
    if (features == nullptr) {
        return false;
    }

    xvatsim::core::route::SectorFeature feature;
    bool hasLabel = false;
    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "label") {
            feature.label = fieldValue;
            hasLabel = true;
        } else if (field == "tokens") {
            feature.tokens = Split(fieldValue, ',');
        } else if (field == "controllerPatterns" ||
                   field == "controllerCallsignPatterns") {
            feature.controllerCallsignPatterns = Split(fieldValue, ',');
        } else if (field == "controllerPrefixes") {
            feature.controllerPrefixes = Split(fieldValue, ',');
        } else if (field == "polygon") {
            xvatsim::core::route::SectorPolygon polygon;
            for (const auto& pointToken : Split(fieldValue, '|')) {
                const auto parts = Split(pointToken, ',');
                if (parts.size() != 2) {
                    return false;
                }
                const auto lat = ParseDouble(parts[0]);
                const auto lon = ParseDouble(parts[1]);
                if (!lat.has_value() || !lon.has_value()) {
                    return false;
                }
                polygon.ring.push_back({*lat, *lon});
            }
            if (polygon.ring.size() < 3) {
                return false;
            }
            feature.polygons.push_back(std::move(polygon));
        }
    }

    if (!hasLabel || feature.polygons.empty()) {
        return false;
    }
    if (feature.tokens.empty()) {
        feature.tokens.push_back(feature.label);
    }

    features->push_back(std::move(feature));
    return true;
}

bool AddTerminalCoverageFeature(
    std::vector<TerminalCoverageFeatureSpec>* features,
    const std::string& value) {
    if (features == nullptr) {
        return false;
    }

    TerminalCoverageFeatureSpec feature;
    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "id") {
            feature.id = fieldValue;
        } else if (field == "name") {
            feature.name = fieldValue;
        } else if (field == "suffix") {
            feature.suffix = fieldValue;
        } else if (field == "prefixes") {
            feature.prefixes = Split(fieldValue, ',');
        } else if (field == "polygon") {
            xvatsim::core::route::SectorPolygon polygon;
            for (const auto& pointToken : Split(fieldValue, '|')) {
                const auto parts = Split(pointToken, ',');
                if (parts.size() != 2) {
                    return false;
                }
                const auto lat = ParseDouble(parts[0]);
                const auto lon = ParseDouble(parts[1]);
                if (!lat.has_value() || !lon.has_value()) {
                    return false;
                }
                polygon.ring.push_back({*lat, *lon});
            }
            if (polygon.ring.size() < 3) {
                return false;
            }
            feature.polygons.push_back(std::move(polygon));
        }
    }

    if (feature.id.empty() || feature.polygons.empty()) {
        return false;
    }

    features->push_back(std::move(feature));
    return true;
}

bool AddCenterCoverageFeature(
    std::vector<CenterCoverageFeatureSpec>* features,
    const std::string& value) {
    if (features == nullptr) {
        return false;
    }

    CenterCoverageFeatureSpec feature;
    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "label") {
            feature.label = fieldValue;
        } else if (field == "name") {
            feature.name = fieldValue;
        } else if (field == "callsign") {
            feature.callsign = fieldValue;
        } else if (field == "tokens") {
            feature.tokens = Split(fieldValue, ',');
        } else if (field == "polygon") {
            xvatsim::core::route::SectorPolygon polygon;
            for (const auto& pointToken : Split(fieldValue, '|')) {
                const auto parts = Split(pointToken, ',');
                if (parts.size() != 2) {
                    return false;
                }
                const auto lat = ParseDouble(parts[0]);
                const auto lon = ParseDouble(parts[1]);
                if (!lat.has_value() || !lon.has_value()) {
                    return false;
                }
                polygon.ring.push_back({*lat, *lon});
            }
            if (polygon.ring.size() < 3) {
                return false;
            }
            feature.polygons.push_back(std::move(polygon));
        }
    }

    if ((feature.label.empty() && feature.name.empty()) || feature.polygons.empty()) {
        return false;
    }

    features->push_back(std::move(feature));
    return true;
}

bool AddAuthorityPolygonSourceRecord(
    std::vector<xvatsim::core::authority::AuthorityPolygonSourceRecord>* records,
    xvatsim::core::authority::AuthoritySource source,
    const std::string& value) {
    if (records == nullptr) {
        return false;
    }

    xvatsim::core::authority::AuthorityPolygonSourceRecord record;
    record.source = source;
    record.sourceRecord = value;

    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "id" || field == "key" || field == "label") {
            record.id = fieldValue;
        } else if (field == "name") {
            record.name = fieldValue;
        } else if (field == "suffix") {
            record.suffix = fieldValue;
        } else if (field == "prefixes" || field == "prefix") {
            record.prefixes = Split(fieldValue, ',');
        } else if (field == "tokens" || field == "lookupTokens") {
            record.lookupTokens = Split(fieldValue, ',');
        } else if (field == "polygon") {
            xvatsim::core::authority::AuthorityPolygonRing ring;
            for (const auto& pointToken : Split(fieldValue, '|')) {
                const auto parts = Split(pointToken, ',');
                if (parts.size() != 2) {
                    return false;
                }
                const auto lat = ParseDouble(parts[0]);
                const auto lon = ParseDouble(parts[1]);
                if (!lat.has_value() || !lon.has_value()) {
                    return false;
                }
                ring.points.push_back({*lat, *lon});
            }
            record.rings.push_back(std::move(ring));
        }
    }

    records->push_back(std::move(record));
    return true;
}

bool AddAuthorityPositionSourceRecord(
    std::vector<xvatsim::core::authority::AuthorityPositionSourceRecord>* records,
    xvatsim::core::authority::AuthoritySource source,
    const std::string& value) {
    if (records == nullptr) {
        return false;
    }

    xvatsim::core::authority::AuthorityPositionSourceRecord record;
    record.source = source;
    record.sourceRecord = value;

    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "id" || field == "position" || field == "positionId") {
            record.id = fieldValue;
        } else if (field == "name") {
            record.name = fieldValue;
        } else if (field == "polygon" || field == "polygonKey" || field == "sector") {
            record.polygonKey = fieldValue;
        } else if (field == "patterns" || field == "callsigns" || field == "callsign") {
            record.controllerCallsignPatterns = Split(fieldValue, ',');
        } else if (field == "kind") {
            const auto parsedKind = ParseAuthorityKind(fieldValue);
            if (!parsedKind.has_value()) {
                return false;
            }
            record.kind = *parsedKind;
        }
    }

    records->push_back(std::move(record));
    return true;
}

std::string JsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const auto character : value) {
        if (character == '\\' || character == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(character);
    }
    return escaped;
}

std::string BuildBoundaryPayload(
    const std::vector<CenterCoverageFeatureSpec>& features) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(6);
    stream << "{\"type\":\"FeatureCollection\",\"features\":[";
    bool wroteFeature = false;
    for (const auto& feature : features) {
        if (feature.polygons.empty()) {
            continue;
        }
        if (wroteFeature) {
            stream << ",";
        }
        wroteFeature = true;
        stream << "{\"type\":\"Feature\",\"properties\":{";
        bool wroteProperty = false;
        if (!feature.label.empty()) {
            stream << "\"identifier\":\"" << JsonEscape(feature.label) << "\"";
            wroteProperty = true;
        }
        if (!feature.name.empty()) {
            if (wroteProperty) {
                stream << ",";
            }
            stream << "\"name\":\"" << JsonEscape(feature.name) << "\"";
            wroteProperty = true;
        }
        if (!feature.callsign.empty()) {
            if (wroteProperty) {
                stream << ",";
            }
            stream << "\"callsign\":\"" << JsonEscape(feature.callsign) << "\"";
            wroteProperty = true;
        }
        if (!feature.tokens.empty()) {
            if (wroteProperty) {
                stream << ",";
            }
            stream << "\"tokens\":\"";
            for (std::size_t tokenIndex = 0; tokenIndex < feature.tokens.size(); ++tokenIndex) {
                if (tokenIndex > 0) {
                    stream << " ";
                }
                stream << JsonEscape(feature.tokens[tokenIndex]);
            }
            stream << "\"";
        }
        stream << "},\"geometry\":{\"type\":\"Polygon\",\"coordinates\":[[";

        const auto& ring = feature.polygons.front().ring;
        for (std::size_t pointIndex = 0; pointIndex < ring.size(); ++pointIndex) {
            if (pointIndex > 0) {
                stream << ",";
            }
            stream << "[" << ring[pointIndex].longitudeDeg << ","
                   << ring[pointIndex].latitudeDeg << "]";
        }
        if (!ring.empty() &&
            (ring.front().latitudeDeg != ring.back().latitudeDeg ||
             ring.front().longitudeDeg != ring.back().longitudeDeg)) {
            stream << ",[" << ring.front().longitudeDeg << ","
                   << ring.front().latitudeDeg << "]";
        }
        stream << "]]}}";
    }
    stream << "]}";
    return wroteFeature ? stream.str() : std::string{};
}

std::string BuildTerminalBoundaryPayload(
    const std::vector<TerminalCoverageFeatureSpec>& features) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(6);
    stream << "{\"type\":\"FeatureCollection\",\"features\":[";
    for (std::size_t featureIndex = 0; featureIndex < features.size(); ++featureIndex) {
        const auto& feature = features[featureIndex];
        if (featureIndex > 0) {
            stream << ",";
        }
        stream << "{\"type\":\"Feature\",\"properties\":{";
        stream << "\"id\":\"" << JsonEscape(feature.id) << "\"";
        if (!feature.name.empty()) {
            stream << ",\"name\":\"" << JsonEscape(feature.name) << "\"";
        }
        if (!feature.suffix.empty()) {
            stream << ",\"suffix\":\"" << JsonEscape(feature.suffix) << "\"";
        }
        stream << ",\"prefix\":[";
        for (std::size_t prefixIndex = 0; prefixIndex < feature.prefixes.size(); ++prefixIndex) {
            if (prefixIndex > 0) {
                stream << ",";
            }
            stream << "\"" << JsonEscape(feature.prefixes[prefixIndex]) << "\"";
        }
        stream << "]},\"geometry\":{\"type\":\"Polygon\",\"coordinates\":[[";

        const auto& ring = feature.polygons.front().ring;
        for (std::size_t pointIndex = 0; pointIndex < ring.size(); ++pointIndex) {
            if (pointIndex > 0) {
                stream << ",";
            }
            stream << "[" << ring[pointIndex].longitudeDeg << ","
                   << ring[pointIndex].latitudeDeg << "]";
        }
        if (!ring.empty() &&
            (ring.front().latitudeDeg != ring.back().latitudeDeg ||
             ring.front().longitudeDeg != ring.back().longitudeDeg)) {
            stream << ",[" << ring.front().longitudeDeg << ","
                   << ring.front().latitudeDeg << "]";
        }
        stream << "]]}}";
    }
    stream << "]}";
    return stream.str();
}

std::string BuildAuthorityCatalogPayload(const std::vector<std::string>& firLines) {
    if (firLines.empty()) {
        return {};
    }

    std::ostringstream stream;
    stream << "[FIRs]\n";
    for (const auto& line : firLines) {
        stream << line << "\n";
    }
    return stream.str();
}

std::string BuildAuthorityCompilerPayload(
    const std::vector<std::string>& firLines,
    const std::vector<std::string>& uirLines) {
    if (firLines.empty() && uirLines.empty()) {
        return {};
    }

    std::ostringstream stream;
    if (!firLines.empty()) {
        stream << "[FIRs]\n";
        for (const auto& line : firLines) {
            stream << line << "\n";
        }
    }
    if (!uirLines.empty()) {
        stream << "[UIRs]\n";
        for (const auto& line : uirLines) {
            stream << line << "\n";
        }
    }
    return stream.str();
}

bool LoadScenario(const std::filesystem::path& path, ScenarioData* scenario, std::string* outError) {
    if (scenario == nullptr) {
        return false;
    }

    std::ifstream stream(path);
    if (!stream.is_open()) {
        if (outError != nullptr) {
            *outError = "Unable to open scenario file";
        }
        return false;
    }

    std::string line;
    int lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;
        const auto commentIndex = line.find('#');
        if (commentIndex != std::string::npos) {
            line.erase(commentIndex);
        }

        line = Trim(line);
        if (line.empty()) {
            continue;
        }

        const auto equalsIndex = line.find('=');
        if (equalsIndex == std::string::npos) {
            if (outError != nullptr) {
                *outError = "Line " + std::to_string(lineNumber) + " is missing '='";
            }
            return false;
        }

        const auto key = Trim(line.substr(0, equalsIndex));
        const auto value = Trim(line.substr(equalsIndex + 1));
        if (key == "departure.station") {
            scenario->departureBoard.source = BoardSource::Departure;
            if (!AddStation(&scenario->departureBoard, value)) {
                if (outError != nullptr) {
                    *outError = "Invalid departure.station at line " + std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "arrival.station") {
            scenario->arrivalBoard.source = BoardSource::Arrival;
            if (!AddStation(&scenario->arrivalBoard, value)) {
                if (outError != nullptr) {
                    *outError = "Invalid arrival.station at line " + std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "enroute.station") {
            scenario->enrouteBoard.source = BoardSource::Enroute;
            if (!AddStation(&scenario->enrouteBoard, value)) {
                if (outError != nullptr) {
                    *outError = "Invalid enroute.station at line " + std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "departure.coverage.sector") {
            if (!AddRouteSector(
                    &scenario->departureAirportSectorSnapshot.coveringSectors,
                    value)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid departure.coverage.sector at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->departureAirportSectorSnapshot.available = true;
            scenario->departureAirportSectorSnapshot.stale = false;
            continue;
        }
        if (key == "arrival.coverage.sector") {
            if (!AddRouteSector(
                    &scenario->arrivalAirportSectorSnapshot.coveringSectors,
                    value)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid arrival.coverage.sector at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->arrivalAirportSectorSnapshot.available = true;
            scenario->arrivalAirportSectorSnapshot.stale = false;
            continue;
        }
        if (key == "airport.terminal_feature") {
            if (!AddTerminalCoverageFeature(
                    &scenario->airportCoverageTerminalFeatures,
                    value)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid airport.terminal_feature at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "airport.pending_terminal_feature") {
            if (!AddTerminalCoverageFeature(
                    &scenario->pendingAirportCoverageTerminalFeatures,
                    value)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid airport.pending_terminal_feature at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->hasPendingAirportCoveragePayloads = true;
            continue;
        }
        if (key == "airport.center_feature") {
            if (!AddCenterCoverageFeature(&scenario->airportCoverageCenterFeatures, value)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid airport.center_feature at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "airport.pending_center_feature") {
            if (!AddCenterCoverageFeature(
                    &scenario->pendingAirportCoverageCenterFeatures,
                    value)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid airport.pending_center_feature at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->hasPendingAirportCoveragePayloads = true;
            continue;
        }
        if (key == "route.current_sector") {
            if (!AddRouteSector(&scenario->routeSectorSnapshot.currentSectors, value)) {
                if (outError != nullptr) {
                    *outError = "Invalid route.current_sector at line " + std::to_string(lineNumber);
                }
                return false;
            }
            scenario->routeSectorSnapshot.available = true;
            scenario->routeSectorSnapshot.stale = false;
            scenario->routeSectorSnapshot.routeResolved = true;
            continue;
        }
        if (key == "route.next_sector") {
            if (!AddRouteSector(&scenario->routeSectorSnapshot.nextSectors, value)) {
                if (outError != nullptr) {
                    *outError = "Invalid route.next_sector at line " + std::to_string(lineNumber);
                }
                return false;
            }
            scenario->routeSectorSnapshot.available = true;
            scenario->routeSectorSnapshot.stale = false;
            scenario->routeSectorSnapshot.routeResolved = true;
            continue;
        }
        if (key == "controller.entry") {
            if (!AddController(&scenario->controllers, value)) {
                if (outError != nullptr) {
                    *outError = "Invalid controller.entry at line " + std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "controller.feed_available") {
            bool parsed = false;
            if (!ParseBool(value, &parsed)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid controller.feed_available at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->controllerFeedAvailable = parsed;
            continue;
        }
        if (key == "controller.feed_stale") {
            if (!ParseBool(value, &scenario->controllerFeedStale)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid controller.feed_stale at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "controller.feed_force_entries") {
            if (!ParseBool(value, &scenario->forceControllerFeedEntries)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid controller.feed_force_entries at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "transceiver.available") {
            if (!ParseBool(value, &scenario->transceiverResolutionSnapshot.available)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid transceiver.available at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "transceiver.stale") {
            if (!ParseBool(value, &scenario->transceiverResolutionSnapshot.stale)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid transceiver.stale at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "transceiver.status") {
            scenario->transceiverResolutionSnapshot.statusLine = value;
            continue;
        }
        if (key == "transceiver.candidate") {
            if (!AddTransceiverCandidate(&scenario->transceiverResolutionSnapshot, value)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid transceiver.candidate at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "overlay.stage") {
            scenario->overlayWorkflowStage = ParseWorkflowStage(value);
            if (!scenario->overlayWorkflowStage.has_value()) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid overlay.stage at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "route.waypoint") {
            if (!AddRouteWaypoint(&scenario->routeWaypoints, value)) {
                if (outError != nullptr) {
                    *outError = "Invalid route.waypoint at line " + std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "graph.node") {
            if (!AddGraphNodeEntry(&scenario->routeGraph, value)) {
                if (outError != nullptr) {
                    *outError = "Invalid graph.node at line " + std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "graph.edge") {
            if (!AddGraphEdgeEntry(&scenario->routeGraph, value)) {
                if (outError != nullptr) {
                    *outError = "Invalid graph.edge at line " + std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "procedure.entry") {
            auto addProcedureEntry = [&](const std::string& rawName,
                                         bool hasSid,
                                         bool hasStar,
                                         const std::string& source,
                                         const std::string& transition,
                                         const std::string& authority,
                                         const std::vector<std::string>& fixes) {
                const auto normalizedName =
                    xvatsim::core::route::ExtractRouteTokenBase(rawName);
                if (normalizedName.empty()) {
                    return false;
                }

                auto& entry = scenario->proceduresByName[normalizedName];
                if (!hasSid && !hasStar) {
                    entry.hasSid = true;
                    entry.hasStar = true;
                } else {
                    entry.hasSid = entry.hasSid || hasSid;
                    entry.hasStar = entry.hasStar || hasStar;
                }

                const auto normalizedSource = ToUpperCopy(Trim(source));
                if (normalizedSource.empty() || normalizedSource == "BOTH") {
                    entry.sourcedFromDepartureAirport = true;
                    entry.sourcedFromArrivalAirport = true;
                } else if (normalizedSource == "DEPARTURE" || normalizedSource == "DEP") {
                    entry.sourcedFromDepartureAirport = true;
                } else if (normalizedSource == "ARRIVAL" || normalizedSource == "ARR") {
                    entry.sourcedFromArrivalAirport = true;
                } else {
                    return false;
                }

                const auto normalizedAuthority = ToUpperCopy(Trim(authority));
                const auto effectiveAuthority =
                    normalizedAuthority.empty() ? std::string("PROC") : normalizedAuthority;
                if (hasSid) {
                    entry.sidAuthoritySources.insert(effectiveAuthority);
                }
                if (hasStar) {
                    entry.starAuthoritySources.insert(effectiveAuthority);
                }
                if (!hasSid && !hasStar) {
                    entry.sidAuthoritySources.insert(effectiveAuthority);
                    entry.starAuthoritySources.insert(effectiveAuthority);
                }

                for (const auto& rawFix : fixes) {
                    const auto normalizedFix =
                        xvatsim::core::route::ExtractRouteTokenBase(rawFix);
                    if (normalizedFix.empty() ||
                        xvatsim::core::route::IsRouteControlToken(normalizedFix) ||
                        xvatsim::core::route::IsRunwayProcedureSegmentToken(rawFix)) {
                        continue;
                    }

                    if (hasSid) {
                        if (entry.sidFixes.insert(normalizedFix).second) {
                            entry.sidOrderedFixes.push_back(normalizedFix);
                        }
                    }
                    if (hasStar) {
                        if (entry.starFixes.insert(normalizedFix).second) {
                            entry.starOrderedFixes.push_back(normalizedFix);
                        }
                    }
                    if (!hasSid && !hasStar) {
                        if (entry.sidFixes.insert(normalizedFix).second) {
                            entry.sidOrderedFixes.push_back(normalizedFix);
                        }
                        if (entry.starFixes.insert(normalizedFix).second) {
                            entry.starOrderedFixes.push_back(normalizedFix);
                        }
                    }
                }

                const auto normalizedTransition =
                    xvatsim::core::route::ExtractRouteTokenBase(transition);
                if (!normalizedTransition.empty()) {
                    const auto isRunwayRecord =
                        xvatsim::core::route::IsRunwayProcedureSegmentToken(transition);
                    if (entry.hasSid && hasSid) {
                        if (isRunwayRecord) {
                            entry.hasSidRunwayRecords = true;
                            entry.sidRunwayTransitions.insert(normalizedTransition);
                        } else {
                            entry.sidTransitions.insert(normalizedTransition);
                        }
                    }
                    if (entry.hasStar && hasStar) {
                        if (isRunwayRecord) {
                            entry.hasStarRunwayRecords = true;
                            entry.starRunwayTransitions.insert(normalizedTransition);
                        } else {
                            entry.starTransitions.insert(normalizedTransition);
                        }
                    }
                    if (!hasSid && !hasStar) {
                        if (isRunwayRecord) {
                            entry.hasSidRunwayRecords = true;
                            entry.hasStarRunwayRecords = true;
                            entry.sidRunwayTransitions.insert(normalizedTransition);
                            entry.starRunwayTransitions.insert(normalizedTransition);
                        } else {
                            entry.sidTransitions.insert(normalizedTransition);
                            entry.starTransitions.insert(normalizedTransition);
                        }
                    }
                }
                return true;
            };

            if (value.find('=') == std::string::npos) {
                if (!addProcedureEntry(value, false, false, {}, {}, {}, {})) {
                    if (outError != nullptr) {
                        *outError = "Invalid procedure.entry at line " + std::to_string(lineNumber);
                    }
                    return false;
                }
                continue;
            }

            std::string name;
            std::string source;
            std::string transition;
            std::string authority;
            std::vector<std::string> fixes;
            bool hasSid = false;
            bool hasStar = false;
            for (const auto& part : Split(value, ';')) {
                const auto separator = part.find('=');
                if (separator == std::string::npos) {
                    continue;
                }
                const auto partKey = ToUpperCopy(Trim(part.substr(0, separator)));
                const auto partValue = Trim(part.substr(separator + 1));
                if (partKey == "NAME") {
                    name = partValue;
                } else if (partKey == "TYPE") {
                    const auto normalizedType = ToUpperCopy(partValue);
                    if (normalizedType == "SID") {
                        hasSid = true;
                    } else if (normalizedType == "STAR") {
                        hasStar = true;
                    } else if (normalizedType == "BOTH") {
                        hasSid = true;
                        hasStar = true;
                    }
                } else if (partKey == "SOURCE") {
                    source = partValue;
                } else if (partKey == "TRANSITION") {
                    transition = partValue;
                } else if (partKey == "AUTHORITY") {
                    authority = partValue;
                } else if (partKey == "FIX") {
                    fixes.push_back(partValue);
                } else if (partKey == "FIXES") {
                    const auto fixParts = Split(partValue, '|');
                    fixes.insert(fixes.end(), fixParts.begin(), fixParts.end());
                }
            }

            if (!addProcedureEntry(name, hasSid, hasStar, source, transition, authority, fixes)) {
                if (outError != nullptr) {
                    *outError = "Invalid procedure.entry at line " + std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "feature.entry") {
            if (!AddTraversalFeature(&scenario->traversalFeatures, value)) {
                if (outError != nullptr) {
                    *outError = "Invalid feature.entry at line " + std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }

        if (!AssignScenarioProperty(scenario, key, value)) {
            if (outError != nullptr) {
                *outError = "Unknown or invalid property at line " + std::to_string(lineNumber) +
                            ": " + key;
            }
            return false;
        }
    }

    if (scenario->name.empty()) {
        scenario->name = path.stem().string();
    }

    if (scenario->departureAirportSectorSnapshot.airportIcao.empty()) {
        scenario->departureAirportSectorSnapshot.airportIcao =
            scenario->workflowState.flightContext.departureIcao;
    }
    if (scenario->arrivalAirportSectorSnapshot.airportIcao.empty()) {
        scenario->arrivalAirportSectorSnapshot.airportIcao =
            scenario->workflowState.flightContext.destinationIcao;
    }

    return true;
}

int PrintMismatch(const std::string& label, const std::string& expected, const std::string& actual) {
    std::cerr << "Mismatch: " << label << " expected [" << expected << "] actual [" << actual << "]\n";
    return 1;
}

bool IsExplicitNoneList(const std::vector<std::string>& values) {
    return values.size() == 1 && ToUpperCopy(values.front()) == "<NONE>";
}

std::string JoinCsv(const std::vector<std::string>& values) {
    std::ostringstream joined;
    for (const auto& value : values) {
        if (joined.tellp() > 0) {
            joined << ",";
        }
        joined << value;
    }
    return joined.str();
}

std::optional<int> CheckStringList(
    const char* label,
    const std::vector<std::string>& expectedValues,
    const std::vector<std::string>& actualValues) {
    if (expectedValues.empty()) {
        return std::nullopt;
    }

    if (IsExplicitNoneList(expectedValues)) {
        if (actualValues.empty()) {
            return std::nullopt;
        }
    } else if (expectedValues == actualValues) {
        return std::nullopt;
    }

    return PrintMismatch(label, JoinCsv(expectedValues), JoinCsv(actualValues));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: XVatsimRegressionHarness <scenario-file>\n";
        return 2;
    }

    ScenarioData scenario;
    std::string error;
    if (!LoadScenario(argv[1], &scenario, &error)) {
        std::cerr << "Failed to load scenario: " << error << "\n";
        return 2;
    }

    auto workflowState = scenario.workflowState;
    const auto handoffDecision = xvatsim::core::workflow::ResolveWorkflowStage(
        scenario.aircraftState,
        scenario.radioStateSnapshot,
        scenario.departureTerminalCoverageKnown,
        scenario.insideDepartureTerminalCoverage,
        scenario.departureBoard,
        scenario.enrouteBoard,
        scenario.nowSeconds,
        &workflowState,
        scenario.tuning);
    const auto departureLocationConfirmed =
        xvatsim::core::workflow::CanConfirmDepartureLocation(
            scenario.aircraftState,
            scenario.flightPlanSnapshot,
            scenario.networkPlanSnapshot,
            scenario.tuning);

    const auto displayBoard = xvatsim::core::workflow::BuildDisplayBoard(
        handoffDecision.stage,
        scenario.departureBoard,
        scenario.arrivalBoard,
        scenario.enrouteBoard);

    xvatsim::brain::ControllerFeedSnapshot controllerFeedSnapshot;
    controllerFeedSnapshot.stale = scenario.controllerFeedStale;
    controllerFeedSnapshot.available =
        scenario.controllerFeedAvailable.value_or(!scenario.controllers.empty());
    if (controllerFeedSnapshot.stale) {
        controllerFeedSnapshot.available = false;
    }
    if ((controllerFeedSnapshot.available && !controllerFeedSnapshot.stale) ||
        scenario.forceControllerFeedEntries) {
        controllerFeedSnapshot.connectedControllers =
            static_cast<int>(scenario.controllers.size());
        controllerFeedSnapshot.controllers = &scenario.controllers;
    }

    const auto vatSpyAuthorityCatalog =
        xvatsim::core::authority::CompileVatSpyAuthorityCatalog(
            BuildAuthorityCompilerPayload(
                scenario.authorityCatalogFirLines,
                scenario.authorityCatalogUirLines));
    const auto positionAuthorityCatalog =
        xvatsim::core::authority::CompileAuthorityPositionCatalog(
            scenario.authorityPositionRecords);
    const auto authorityCatalog =
        xvatsim::core::authority::MergeControllerAuthorityCatalogs(
            vatSpyAuthorityCatalog,
            positionAuthorityCatalog);
    const auto authorityPolygonCatalog =
        xvatsim::core::authority::CompileAuthorityPolygons(
            scenario.authorityPolygonRecords);
    std::vector<xvatsim::core::authority::ActiveControllerAuthority>
        activeAuthorityMatches;
    std::vector<xvatsim::core::authority::ActiveAuthorityPolygon>
        activeAuthorityPolygons;
    std::vector<xvatsim::core::authority::AuthorityDataGap>
        activeAuthorityPolygonDataGaps;
    std::vector<std::string> authorityUnmappedCallsigns;
    if (!scenario.authorityCatalogFirLines.empty() ||
        !scenario.authorityCatalogUirLines.empty() ||
        !scenario.authorityPositionRecords.empty()) {
        for (const auto& controller : scenario.controllers) {
            const auto matches = xvatsim::core::authority::ResolveControllerAuthority(
                authorityCatalog,
                controller.callsign,
                controller.facility);
            if (matches.empty()) {
                authorityUnmappedCallsigns.push_back(
                    xvatsim::core::authority::NormalizeControllerCallsign(
                        controller.callsign));
                continue;
            }
            activeAuthorityMatches.insert(
                activeAuthorityMatches.end(),
                matches.begin(),
                matches.end());
            const auto activationResult =
                xvatsim::core::authority::ActivateAuthorityPolygons(
                    authorityCatalog,
                    authorityPolygonCatalog,
                    controller.callsign,
                    controller.facility);
            activeAuthorityPolygons.insert(
                activeAuthorityPolygons.end(),
                activationResult.activePolygons.begin(),
                activationResult.activePolygons.end());
            activeAuthorityPolygonDataGaps.insert(
                activeAuthorityPolygonDataGaps.end(),
                activationResult.dataGaps.begin(),
                activationResult.dataGaps.end());
        }
        std::sort(
            authorityUnmappedCallsigns.begin(),
            authorityUnmappedCallsigns.end());
    }
    std::vector<xvatsim::core::authority::GeoPoint> authorityRoutePoints;
    authorityRoutePoints.reserve(scenario.routeWaypoints.size());
    for (const auto& waypoint : scenario.routeWaypoints) {
        authorityRoutePoints.push_back({
            waypoint.latitudeDeg,
            waypoint.longitudeDeg,
        });
    }
    const xvatsim::core::authority::GeoPoint authorityAircraftPosition{
        scenario.aircraftState.latitudeDeg,
        scenario.aircraftState.longitudeDeg,
    };
    const auto relevantAuthorityPolygons =
        xvatsim::core::authority::ResolveRelevantAuthorityPolygons(
            activeAuthorityPolygons,
            authorityPolygonCatalog,
            scenario.aircraftState.valid,
            authorityAircraftPosition,
            authorityRoutePoints);
    xvatsim::brain::AuthorityRelevanceSnapshot authorityRelevanceSnapshot;
    if (scenario.authorityEnrouteHandoff) {
        authorityRelevanceSnapshot.available = true;
        authorityRelevanceSnapshot.stale = false;

        std::unordered_map<std::string, std::string> controllerFrequenciesByCallsign;
        for (const auto& controller : scenario.controllers) {
            controllerFrequenciesByCallsign[ToUpperCopy(Trim(controller.callsign))] =
                controller.frequency;
        }

        for (const auto& relevantAuthorityPolygon : relevantAuthorityPolygons) {
            xvatsim::brain::RelevantAuthoritySnapshot relevantAuthority;
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
            relevantAuthority.kind =
                ToBrainAuthorityKind(relevantAuthorityPolygon.activePolygon.kind);
            relevantAuthority.aircraftInside =
                relevantAuthorityPolygon.aircraftInside;
            relevantAuthority.routeIntersects =
                relevantAuthorityPolygon.routeIntersects;
            relevantAuthority.routeEntryDistanceNm =
                relevantAuthorityPolygon.routeEntryDistanceNm;

            const auto frequencyIt = controllerFrequenciesByCallsign.find(
                ToUpperCopy(Trim(relevantAuthority.callsign)));
            if (frequencyIt != controllerFrequenciesByCallsign.end()) {
                relevantAuthority.frequency = frequencyIt->second;
            }

            authorityRelevanceSnapshot.relevantAuthorities.push_back(
                std::move(relevantAuthority));
        }
    }

    xvatsim::modules::departure::DepartureModule departureModule;
    const auto collectedDepartureBoard = departureModule.Collect(
        scenario.xPilotSessionSnapshot,
        controllerFeedSnapshot,
        scenario.radioStateSnapshot,
        scenario.workflowState.flightContext.departureIcao,
        scenario.departureAirportSectorSnapshot,
        nullptr);
    xvatsim::modules::arrival::ArrivalAirspaceModule arrivalAirspaceModule;
    const auto collectedArrivalAirspaceBoard = arrivalAirspaceModule.Collect(
        scenario.xPilotSessionSnapshot,
        controllerFeedSnapshot,
        scenario.radioStateSnapshot,
        scenario.workflowState.flightContext.destinationIcao,
        scenario.arrivalAirportSectorSnapshot);
    xvatsim::modules::arrival::ArrivalLocalModule arrivalLocalModule;
    const auto collectedArrivalLocalBoard = arrivalLocalModule.Collect(
        scenario.xPilotSessionSnapshot,
        controllerFeedSnapshot,
        scenario.radioStateSnapshot,
        scenario.workflowState.flightContext.destinationIcao);
    xvatsim::brain::AirportSectorSnapshot builtAirportCoverageSnapshot;
    xvatsim::brain::AirportSectorSnapshot preRefreshAirportCoverageSnapshot;
    bool hasPreRefreshAirportCoverageSnapshot = false;
    std::optional<bool> airportTerminalInside;
    xvatsim::brain::RouteSectorSnapshot resolverRouteSectorSnapshot;
    if (!scenario.airportCoverageBuildIcao.empty()) {
        xvatsim::modules::route_sector::RouteSectorResolver routeSectorResolver;
        routeSectorResolver.LoadBoundaryPayloadsForTesting(
            BuildBoundaryPayload(scenario.airportCoverageCenterFeatures),
            BuildTerminalBoundaryPayload(scenario.airportCoverageTerminalFeatures),
            BuildAuthorityCatalogPayload(scenario.airportCoverageAuthorityCatalogLines));
        if (scenario.airportCoverageBuildsPreRefreshSnapshot ||
            scenario.airportTerminalProbeUsesPreRefreshSnapshot) {
            preRefreshAirportCoverageSnapshot = routeSectorResolver.ResolveAirportCoverage(
                scenario.airportCoverageBuildIcao,
                scenario.hasAirportCoverageBuildCoordinates,
                scenario.airportCoverageBuildLatitudeDeg,
                scenario.airportCoverageBuildLongitudeDeg);
            hasPreRefreshAirportCoverageSnapshot = true;
        }
        if (scenario.hasPendingAirportCoveragePayloads) {
            routeSectorResolver.QueueBoundaryPayloadsForTesting(
                BuildBoundaryPayload(scenario.pendingAirportCoverageCenterFeatures),
                BuildTerminalBoundaryPayload(scenario.pendingAirportCoverageTerminalFeatures),
                BuildAuthorityCatalogPayload(
                    scenario.pendingAirportCoverageAuthorityCatalogLines));
        }
        builtAirportCoverageSnapshot = routeSectorResolver.ResolveAirportCoverage(
            scenario.airportCoverageBuildIcao,
            scenario.hasAirportCoverageBuildCoordinates,
            scenario.airportCoverageBuildLatitudeDeg,
            scenario.airportCoverageBuildLongitudeDeg);
        if (scenario.hasAirportTerminalProbeCoordinates) {
            const auto& probeSnapshot =
                (scenario.airportTerminalProbeUsesPreRefreshSnapshot &&
                 hasPreRefreshAirportCoverageSnapshot)
                    ? preRefreshAirportCoverageSnapshot
                    : builtAirportCoverageSnapshot;
            airportTerminalInside = routeSectorResolver.IsInsideAirportTerminalCoverage(
                probeSnapshot,
                scenario.airportTerminalProbeLatitudeDeg,
                scenario.airportTerminalProbeLongitudeDeg);
        }
    }
    xvatsim::modules::enroute::EnrouteModule enrouteModule;
    const auto collectedEnrouteBoard = enrouteModule.Collect(
        scenario.xPilotSessionSnapshot,
        controllerFeedSnapshot,
        scenario.radioStateSnapshot,
        scenario.routeSectorSnapshot,
        scenario.authorityEnrouteHandoff ? &authorityRelevanceSnapshot : nullptr);
    xvatsim::brain::BrainOrchestrator brainOrchestrator;
    const auto overlayWorkflowStage =
        scenario.overlayWorkflowStage.value_or(handoffDecision.stage);
    const auto overlayModel = brainOrchestrator.BuildOverlayViewModel(
        overlayWorkflowStage,
        scenario.aircraftState,
        scenario.xPilotSessionSnapshot,
        scenario.radioStateSnapshot,
        scenario.networkPlanSnapshot,
        controllerFeedSnapshot,
        scenario.transceiverResolutionSnapshot,
        displayBoard,
        xvatsim::brain::ManualQuerySnapshot{});
    auto routePlanSnapshot = scenario.networkPlanSnapshot;
    if (routePlanSnapshot.routeText.empty()) {
        routePlanSnapshot.routeText = scenario.workflowState.flightContext.routeText;
    }
    const auto effectiveRouteText = routePlanSnapshot.routeText;
    if (scenario.resolveRouteWithResolver) {
        xvatsim::modules::route_sector::RouteSectorResolver routeSectorResolver;
        routeSectorResolver.LoadBoundaryPayloadsForTesting(
            BuildBoundaryPayload(scenario.resolverRouteCenterFeatures),
            {},
            BuildAuthorityCatalogPayload(scenario.resolverRouteAuthorityCatalogLines),
            scenario.resolverRouteOwnershipJson);
        if (scenario.resolverRouteBuildsPreRefreshSnapshot) {
            (void)routeSectorResolver.Resolve(scenario.aircraftState, routePlanSnapshot);
        }
        if (scenario.hasPendingResolverRoutePayloads) {
            routeSectorResolver.QueueBoundaryPayloadsForTesting(
                BuildBoundaryPayload(scenario.pendingResolverRouteCenterFeatures),
                {},
                BuildAuthorityCatalogPayload(
                    scenario.pendingResolverRouteAuthorityCatalogLines),
                scenario.pendingResolverRouteOwnershipJson);
        }
        resolverRouteSectorSnapshot =
            routeSectorResolver.Resolve(scenario.aircraftState, routePlanSnapshot);
    }
    const auto grammarCatalog =
        xvatsim::core::route::BuildRouteGrammarCatalog(
            scenario.routeGraph,
            &scenario.proceduresByName);
    const auto parsedRouteTokens =
        xvatsim::core::route::ParseRouteTokens(effectiveRouteText, &grammarCatalog);
    xvatsim::core::route::RouteResolveDiagnostics routeResolveDiagnostics;
    const auto resolvedRouteWaypoints =
        xvatsim::core::route::ResolveRouteWaypoints(
            scenario.aircraftState,
            routePlanSnapshot,
            scenario.routeGraph,
            &grammarCatalog,
            &routeResolveDiagnostics);
    const auto traversedRouteSnapshot =
        xvatsim::core::route::BuildRouteSectorSnapshotFromWaypoints(
            scenario.routeWaypoints,
            scenario.traversalFeatures,
            scenario.traversalTuning);
    const auto sourceManifest =
        xvatsim::core::source_data::ParseMapDataManifestJson(
            scenario.sourceManifestJson);

    std::cout << "Scenario: " << scenario.name << "\n";
    std::cout << "Stage: " << WorkflowStageToString(handoffDecision.stage) << "\n";
    std::cout << "Reason: " << handoffDecision.reason << "\n";
    std::cout << "DepartureLocationConfirmed: "
              << (departureLocationConfirmed ? "true" : "false") << "\n";
    std::cout << "DisplaySource: " << BoardSourceToString(displayBoard.source) << "\n";
    std::cout << "DisplayCallsigns:";
    for (const auto& callsign : ExtractCallsigns(displayBoard)) {
        std::cout << " " << callsign;
    }
    std::cout << "\n";
    std::cout << "OverlayBodyLines:";
    for (const auto& line : ExtractOverlayBodyLines(overlayModel)) {
        std::cout << " " << line;
    }
    std::cout << "\n";
    std::cout << "DepartureCollectedAvailable: "
              << (collectedDepartureBoard.available ? "true" : "false") << "\n";
    std::cout << "DepartureCollectedCallsigns:";
    for (const auto& callsign : ExtractCallsigns(collectedDepartureBoard)) {
        std::cout << " " << callsign;
    }
    std::cout << "\n";
    std::cout << "ArrivalAirspaceAvailable: "
              << (collectedArrivalAirspaceBoard.available ? "true" : "false") << "\n";
    std::cout << "ArrivalAirspaceCallsigns:";
    for (const auto& callsign : ExtractCallsigns(collectedArrivalAirspaceBoard)) {
        std::cout << " " << callsign;
    }
    std::cout << "\n";
    std::cout << "ArrivalLocalAvailable: "
              << (collectedArrivalLocalBoard.available ? "true" : "false") << "\n";
    std::cout << "ArrivalLocalCallsigns:";
    for (const auto& callsign : ExtractCallsigns(collectedArrivalLocalBoard)) {
        std::cout << " " << callsign;
    }
    std::cout << "\n";
    std::cout << "AirportCoverageAvailable: "
              << (builtAirportCoverageSnapshot.available ? "true" : "false") << "\n";
    std::cout << "AirportCoverageMatchTokens:";
    for (const auto& value : ExtractCoverageMatchTokens(builtAirportCoverageSnapshot)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AirportCoverageControllerPrefixes:";
    for (const auto& value : ExtractCoverageControllerPrefixes(builtAirportCoverageSnapshot)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AirportCoverageGenerations:";
    for (const auto& value : ExtractCoverageGenerations(builtAirportCoverageSnapshot)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AirportTerminalInside: ";
    if (airportTerminalInside.has_value()) {
        std::cout << (*airportTerminalInside ? "true" : "false");
    } else {
        std::cout << "unset";
    }
    std::cout << "\n";
    std::cout << "EnrouteAvailable: " << (collectedEnrouteBoard.available ? "true" : "false") << "\n";
    std::cout << "EnrouteCallsigns:";
    for (const auto& callsign : ExtractCallsigns(collectedEnrouteBoard)) {
        std::cout << " " << callsign;
    }
    std::cout << "\n";
    std::cout << "AuthorityCatalogIds:";
    for (const auto& value : ExtractAuthorityCatalogIds(authorityCatalog)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AuthorityDataGaps:";
    for (const auto& value : ExtractAuthorityDataGaps(authorityCatalog)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AuthorityActiveMatches:";
    for (const auto& value : ExtractAuthorityActiveMatches(activeAuthorityMatches)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AuthorityUnmappedCallsigns:";
    for (const auto& value : authorityUnmappedCallsigns) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AuthorityPolygonIds:";
    for (const auto& value : ExtractAuthorityPolygonIds(authorityPolygonCatalog)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AuthorityPolygonLookupKeys:";
    for (const auto& value : ExtractAuthorityPolygonLookupKeys(authorityPolygonCatalog)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AuthorityPolygonRingCounts:";
    for (const auto& value : ExtractAuthorityPolygonRingCounts(authorityPolygonCatalog)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AuthorityPolygonDataGaps:";
    for (const auto& value : ExtractAuthorityPolygonDataGaps(authorityPolygonCatalog)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AuthorityActivePolygonMatches:";
    for (const auto& value : ExtractAuthorityActivePolygonMatches(activeAuthorityPolygons)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AuthorityActivePolygonDataGaps:";
    for (const auto& value :
         ExtractAuthorityActivePolygonDataGaps(activeAuthorityPolygonDataGaps)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AuthorityRelevantPolygonMatches:";
    for (const auto& value :
         ExtractAuthorityRelevantPolygonMatches(relevantAuthorityPolygons)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "SourceManifestValid: "
              << (sourceManifest.valid ? "true" : "false") << "\n";
    std::cout << "SourceManifestValues:";
    for (const auto& value : ExtractSourceManifestValues(sourceManifest)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "ResolverRouteAvailable: "
              << (resolverRouteSectorSnapshot.available ? "true" : "false") << "\n";
    std::cout << "ResolverRouteResolved: "
              << (resolverRouteSectorSnapshot.routeResolved ? "true" : "false") << "\n";
    std::cout << "ResolverRouteStatus: " << resolverRouteSectorSnapshot.statusLine << "\n";
    std::cout << "ResolverRouteAuthorityGaps:";
    for (const auto& value : ExtractAuthorityGaps(resolverRouteSectorSnapshot)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "ResolverRouteCurrentSectors:";
    for (const auto& identifier :
         ExtractSectorIdentifiers(resolverRouteSectorSnapshot.currentSectors)) {
        std::cout << " " << identifier;
    }
    std::cout << "\n";
    std::cout << "ResolverRouteNextSectors:";
    for (const auto& identifier :
         ExtractSectorIdentifiers(resolverRouteSectorSnapshot.nextSectors)) {
        std::cout << " " << identifier;
    }
    std::cout << "\n";
    std::cout << "ResolverRouteCurrentControllerPatterns:";
    for (const auto& value :
         ExtractSectorControllerPatterns(resolverRouteSectorSnapshot.currentSectors)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "ResolverRouteNextControllerPatterns:";
    for (const auto& value :
         ExtractSectorControllerPatterns(resolverRouteSectorSnapshot.nextSectors)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "ResolverRouteCurrentControllerPrefixes:";
    for (const auto& value :
         ExtractSectorControllerPrefixes(resolverRouteSectorSnapshot.currentSectors)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "ResolverRouteNextControllerPrefixes:";
    for (const auto& value :
         ExtractSectorControllerPrefixes(resolverRouteSectorSnapshot.nextSectors)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "ResolverRouteGenerations:";
    for (const auto& value : ExtractRouteGenerations(resolverRouteSectorSnapshot)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "RouteResolved: " << (traversedRouteSnapshot.routeResolved ? "true" : "false") << "\n";
    std::cout << "RouteCurrentSectors:";
    for (const auto& identifier : ExtractSectorIdentifiers(traversedRouteSnapshot.currentSectors)) {
        std::cout << " " << identifier;
    }
    std::cout << "\n";
    std::cout << "RouteNextSectors:";
    for (const auto& identifier : ExtractSectorIdentifiers(traversedRouteSnapshot.nextSectors)) {
        std::cout << " " << identifier;
    }
    std::cout << "\n";
    std::cout << "RouteCurrentControllerPrefixes:";
    for (const auto& value : ExtractSectorControllerPrefixes(traversedRouteSnapshot.currentSectors)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "RouteNextControllerPrefixes:";
    for (const auto& value : ExtractSectorControllerPrefixes(traversedRouteSnapshot.nextSectors)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "RouteTokenKinds:";
    for (const auto& token : ExtractTokenKinds(parsedRouteTokens)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ResolvedWaypoints:";
    for (const auto& ident : ExtractWaypointIdents(resolvedRouteWaypoints)) {
        std::cout << " " << ident;
    }
    std::cout << "\n";
    std::cout << "ResolvedTokens:";
    for (const auto& token : ExtractRouteDiagnostics(routeResolveDiagnostics.resolvedTokens)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ExpandedTokens:";
    for (const auto& token : ExtractRouteDiagnostics(routeResolveDiagnostics.expandedTokens)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureTokens:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.recognizedProcedureTokens)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureSources:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureMetadataSources)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureRecords:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureRecordKinds)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureRunways:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureRunwayRecords)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureAuthorities:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureCatalogAuthorities)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureCatalogFixes:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureCatalogFixes)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureBoundaryFixes:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureBoundaryFixes)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureOrderedFixes:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureOrderedFixes)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureSyntheticWaypoints:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureSyntheticWaypoints)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureSyntheticSources:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureSyntheticSources)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureApplicationStates:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureApplicationStates)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureApplicationBlocks:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureApplicationBlocks)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureAppliedFixSequences:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureAppliedFixSequences)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureCatalogTransitions:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureCatalogTransitions)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureSupport:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureSupportDirections)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureLinks:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureTransitionLinks)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureMisses:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureTransitionMisses)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureAnchorLinks:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureAnchorLinks)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureContextOnly:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureContextOnlyTokens)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "IgnoredTokens:";
    for (const auto& token : ExtractRouteDiagnostics(routeResolveDiagnostics.ignoredTokens)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "UnsupportedTokens:";
    for (const auto& token : ExtractRouteDiagnostics(routeResolveDiagnostics.unsupportedTokens)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "UnresolvedTokens:";
    for (const auto& token : ExtractRouteDiagnostics(routeResolveDiagnostics.unresolvedTokens)) {
        std::cout << " " << token;
    }
    std::cout << "\n";

    if (scenario.expectations.stage.has_value() &&
        handoffDecision.stage != *scenario.expectations.stage) {
        return PrintMismatch(
            "stage",
            WorkflowStageToString(*scenario.expectations.stage),
            WorkflowStageToString(handoffDecision.stage));
    }

    if (scenario.expectations.reason.has_value() &&
        handoffDecision.reason != *scenario.expectations.reason) {
        return PrintMismatch(
            "reason",
            *scenario.expectations.reason,
            handoffDecision.reason);
    }

    if (scenario.expectations.departureLocationConfirmed.has_value() &&
        departureLocationConfirmed != *scenario.expectations.departureLocationConfirmed) {
        return PrintMismatch(
            "departureLocationConfirmed",
            *scenario.expectations.departureLocationConfirmed ? "true" : "false",
            departureLocationConfirmed ? "true" : "false");
    }

    if (scenario.expectations.displaySource.has_value() &&
        displayBoard.source != *scenario.expectations.displaySource) {
        return PrintMismatch(
            "displaySource",
            BoardSourceToString(*scenario.expectations.displaySource),
            BoardSourceToString(displayBoard.source));
    }

    if (const auto mismatch = CheckStringList(
            "displayCallsigns",
            scenario.expectations.displayCallsigns,
            ExtractCallsigns(displayBoard));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "overlayBodyLines",
            scenario.expectations.overlayBodyLines,
            ExtractOverlayBodyLines(overlayModel));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.departureCollectedAvailable.has_value() &&
        collectedDepartureBoard.available !=
            *scenario.expectations.departureCollectedAvailable) {
        return PrintMismatch(
            "departureCollectedAvailable",
            *scenario.expectations.departureCollectedAvailable ? "true" : "false",
            collectedDepartureBoard.available ? "true" : "false");
    }

    if (const auto mismatch = CheckStringList(
            "departureCollectedCallsigns",
            scenario.expectations.departureCollectedCallsigns,
            ExtractCallsigns(collectedDepartureBoard));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.arrivalAirspaceAvailable.has_value() &&
        collectedArrivalAirspaceBoard.available !=
            *scenario.expectations.arrivalAirspaceAvailable) {
        return PrintMismatch(
            "arrivalAirspaceAvailable",
            *scenario.expectations.arrivalAirspaceAvailable ? "true" : "false",
            collectedArrivalAirspaceBoard.available ? "true" : "false");
    }

    if (const auto mismatch = CheckStringList(
            "arrivalAirspaceCallsigns",
            scenario.expectations.arrivalAirspaceCallsigns,
            ExtractCallsigns(collectedArrivalAirspaceBoard));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.arrivalLocalAvailable.has_value() &&
        collectedArrivalLocalBoard.available !=
            *scenario.expectations.arrivalLocalAvailable) {
        return PrintMismatch(
            "arrivalLocalAvailable",
            *scenario.expectations.arrivalLocalAvailable ? "true" : "false",
            collectedArrivalLocalBoard.available ? "true" : "false");
    }

    if (const auto mismatch = CheckStringList(
            "arrivalLocalCallsigns",
            scenario.expectations.arrivalLocalCallsigns,
            ExtractCallsigns(collectedArrivalLocalBoard));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.airportCoverageAvailable.has_value() &&
        builtAirportCoverageSnapshot.available !=
            *scenario.expectations.airportCoverageAvailable) {
        return PrintMismatch(
            "airportCoverageAvailable",
            *scenario.expectations.airportCoverageAvailable ? "true" : "false",
            builtAirportCoverageSnapshot.available ? "true" : "false");
    }

    if (const auto mismatch = CheckStringList(
            "airportCoverageMatchTokens",
            scenario.expectations.airportCoverageMatchTokens,
            ExtractCoverageMatchTokens(builtAirportCoverageSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "airportCoverageControllerPrefixes",
            scenario.expectations.airportCoverageControllerPrefixes,
            ExtractCoverageControllerPrefixes(builtAirportCoverageSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "airportCoverageGenerations",
            scenario.expectations.airportCoverageGenerations,
            ExtractCoverageGenerations(builtAirportCoverageSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.airportTerminalInside.has_value()) {
        if (!airportTerminalInside.has_value()) {
            return PrintMismatch(
                "airportTerminalInside",
                *scenario.expectations.airportTerminalInside ? "true" : "false",
                "unset");
        }
        if (*airportTerminalInside != *scenario.expectations.airportTerminalInside) {
            return PrintMismatch(
                "airportTerminalInside",
                *scenario.expectations.airportTerminalInside ? "true" : "false",
                *airportTerminalInside ? "true" : "false");
        }
    }

    if (scenario.expectations.enrouteAvailable.has_value() &&
        collectedEnrouteBoard.available != *scenario.expectations.enrouteAvailable) {
        return PrintMismatch(
            "enrouteAvailable",
            *scenario.expectations.enrouteAvailable ? "true" : "false",
            collectedEnrouteBoard.available ? "true" : "false");
    }

    if (const auto mismatch = CheckStringList(
            "enrouteCallsigns",
            scenario.expectations.enrouteCallsigns,
            ExtractCallsigns(collectedEnrouteBoard));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityCatalogIds",
            scenario.expectations.authorityCatalogIds,
            ExtractAuthorityCatalogIds(authorityCatalog));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityDataGaps",
            scenario.expectations.authorityDataGaps,
            ExtractAuthorityDataGaps(authorityCatalog));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityActiveMatches",
            scenario.expectations.authorityActiveMatches,
            ExtractAuthorityActiveMatches(activeAuthorityMatches));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityUnmappedCallsigns",
            scenario.expectations.authorityUnmappedCallsigns,
            authorityUnmappedCallsigns);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityPolygonIds",
            scenario.expectations.authorityPolygonIds,
            ExtractAuthorityPolygonIds(authorityPolygonCatalog));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityPolygonLookupKeys",
            scenario.expectations.authorityPolygonLookupKeys,
            ExtractAuthorityPolygonLookupKeys(authorityPolygonCatalog));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityPolygonRingCounts",
            scenario.expectations.authorityPolygonRingCounts,
            ExtractAuthorityPolygonRingCounts(authorityPolygonCatalog));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityPolygonDataGaps",
            scenario.expectations.authorityPolygonDataGaps,
            ExtractAuthorityPolygonDataGaps(authorityPolygonCatalog));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityActivePolygonMatches",
            scenario.expectations.authorityActivePolygonMatches,
            ExtractAuthorityActivePolygonMatches(activeAuthorityPolygons));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityActivePolygonDataGaps",
            scenario.expectations.authorityActivePolygonDataGaps,
            ExtractAuthorityActivePolygonDataGaps(activeAuthorityPolygonDataGaps));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityRelevantPolygonMatches",
            scenario.expectations.authorityRelevantPolygonMatches,
            ExtractAuthorityRelevantPolygonMatches(relevantAuthorityPolygons));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.sourceManifestValid.has_value() &&
        sourceManifest.valid != *scenario.expectations.sourceManifestValid) {
        return PrintMismatch(
            "sourceManifestValid",
            *scenario.expectations.sourceManifestValid ? "true" : "false",
            sourceManifest.valid ? "true" : "false");
    }

    if (const auto mismatch = CheckStringList(
            "sourceManifestValues",
            scenario.expectations.sourceManifestValues,
            ExtractSourceManifestValues(sourceManifest));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.resolverRouteAvailable.has_value() &&
        resolverRouteSectorSnapshot.available !=
            *scenario.expectations.resolverRouteAvailable) {
        return PrintMismatch(
            "resolverRouteAvailable",
            *scenario.expectations.resolverRouteAvailable ? "true" : "false",
            resolverRouteSectorSnapshot.available ? "true" : "false");
    }

    if (scenario.expectations.resolverRouteResolved.has_value() &&
        resolverRouteSectorSnapshot.routeResolved !=
            *scenario.expectations.resolverRouteResolved) {
        return PrintMismatch(
            "resolverRouteResolved",
            *scenario.expectations.resolverRouteResolved ? "true" : "false",
            resolverRouteSectorSnapshot.routeResolved ? "true" : "false");
    }

    if (scenario.expectations.resolverRouteStatus.has_value() &&
        resolverRouteSectorSnapshot.statusLine != *scenario.expectations.resolverRouteStatus) {
        return PrintMismatch(
            "resolverRouteStatus",
            *scenario.expectations.resolverRouteStatus,
            resolverRouteSectorSnapshot.statusLine);
    }

    if (const auto mismatch = CheckStringList(
            "resolverRouteAuthorityGaps",
            scenario.expectations.resolverRouteAuthorityGaps,
            ExtractAuthorityGaps(resolverRouteSectorSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "resolverRouteCurrentSectors",
            scenario.expectations.resolverRouteCurrentSectors,
            ExtractSectorIdentifiers(resolverRouteSectorSnapshot.currentSectors));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "resolverRouteNextSectors",
            scenario.expectations.resolverRouteNextSectors,
            ExtractSectorIdentifiers(resolverRouteSectorSnapshot.nextSectors));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "resolverRouteCurrentControllerPatterns",
            scenario.expectations.resolverRouteCurrentControllerPatterns,
            ExtractSectorControllerPatterns(resolverRouteSectorSnapshot.currentSectors));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "resolverRouteNextControllerPatterns",
            scenario.expectations.resolverRouteNextControllerPatterns,
            ExtractSectorControllerPatterns(resolverRouteSectorSnapshot.nextSectors));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "resolverRouteCurrentControllerPrefixes",
            scenario.expectations.resolverRouteCurrentControllerPrefixes,
            ExtractSectorControllerPrefixes(resolverRouteSectorSnapshot.currentSectors));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "resolverRouteNextControllerPrefixes",
            scenario.expectations.resolverRouteNextControllerPrefixes,
            ExtractSectorControllerPrefixes(resolverRouteSectorSnapshot.nextSectors));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "resolverRouteGenerations",
            scenario.expectations.resolverRouteGenerations,
            ExtractRouteGenerations(resolverRouteSectorSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.routeResolved.has_value() &&
        traversedRouteSnapshot.routeResolved != *scenario.expectations.routeResolved) {
        return PrintMismatch(
            "routeResolved",
            *scenario.expectations.routeResolved ? "true" : "false",
            traversedRouteSnapshot.routeResolved ? "true" : "false");
    }

    if (const auto mismatch = CheckStringList(
            "routeCurrentSectors",
            scenario.expectations.routeCurrentSectors,
            ExtractSectorIdentifiers(traversedRouteSnapshot.currentSectors));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "routeNextSectors",
            scenario.expectations.routeNextSectors,
            ExtractSectorIdentifiers(traversedRouteSnapshot.nextSectors));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "routeCurrentControllerPrefixes",
            scenario.expectations.routeCurrentControllerPrefixes,
            ExtractSectorControllerPrefixes(traversedRouteSnapshot.currentSectors));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "routeNextControllerPrefixes",
            scenario.expectations.routeNextControllerPrefixes,
            ExtractSectorControllerPrefixes(traversedRouteSnapshot.nextSectors));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (!scenario.expectations.routeTokenKinds.empty()) {
        const auto actualKinds = ExtractTokenKinds(parsedRouteTokens);
        if (actualKinds != scenario.expectations.routeTokenKinds) {
            std::ostringstream expected;
            std::ostringstream actual;
            for (const auto& value : scenario.expectations.routeTokenKinds) {
                if (expected.tellp() > 0) {
                    expected << ",";
                }
                expected << value;
            }
            for (const auto& value : actualKinds) {
                if (actual.tellp() > 0) {
                    actual << ",";
                }
                actual << value;
            }
            return PrintMismatch("routeTokenKinds", expected.str(), actual.str());
        }
    }

    if (!scenario.expectations.resolvedWaypointIdents.empty()) {
        const auto actualWaypoints = ExtractWaypointIdents(resolvedRouteWaypoints);
        if (actualWaypoints != scenario.expectations.resolvedWaypointIdents) {
            std::ostringstream expected;
            std::ostringstream actual;
            for (const auto& ident : scenario.expectations.resolvedWaypointIdents) {
                if (expected.tellp() > 0) {
                    expected << ",";
                }
                expected << ident;
            }
            for (const auto& ident : actualWaypoints) {
                if (actual.tellp() > 0) {
                    actual << ",";
                }
                actual << ident;
            }
            return PrintMismatch("resolvedWaypoints", expected.str(), actual.str());
        }
    }

    if (!scenario.expectations.resolvedWaypointPoints.empty()) {
        bool matches = resolvedRouteWaypoints.size() == scenario.expectations.resolvedWaypointPoints.size();
        if (matches) {
            constexpr double kToleranceDeg = 1e-4;
            for (std::size_t index = 0; index < resolvedRouteWaypoints.size(); ++index) {
                const auto& actual = resolvedRouteWaypoints[index];
                const auto& expected = scenario.expectations.resolvedWaypointPoints[index];
                if (actual.ident != expected.ident ||
                    std::abs(actual.latitudeDeg - expected.latitudeDeg) > kToleranceDeg ||
                    std::abs(actual.longitudeDeg - expected.longitudeDeg) > kToleranceDeg) {
                    matches = false;
                    break;
                }
            }
        }

        if (!matches) {
            std::ostringstream expected;
            std::ostringstream actual;
            for (const auto& waypoint : scenario.expectations.resolvedWaypointPoints) {
                if (expected.tellp() > 0) {
                    expected << "|";
                }
                expected.setf(std::ios::fixed);
                expected.precision(4);
                expected << waypoint.ident << "@" << waypoint.latitudeDeg << "," << waypoint.longitudeDeg;
            }
            for (const auto& waypoint : resolvedRouteWaypoints) {
                if (actual.tellp() > 0) {
                    actual << "|";
                }
                actual << FormatWaypointPoint(waypoint);
            }
            return PrintMismatch("resolvedWaypointPoints", expected.str(), actual.str());
        }
    }

    const auto checkDiagnosticList =
        [&](const char* label,
            const std::vector<std::string>& expectedValues,
            const std::vector<std::string>& actualValues) -> std::optional<int> {
            if (expectedValues.empty()) {
                return std::nullopt;
            }
            if (expectedValues.size() == 1 &&
                ToUpperCopy(expectedValues.front()) == "<NONE>") {
                if (actualValues.empty()) {
                    return std::nullopt;
                }
            } else if (expectedValues == actualValues) {
                return std::nullopt;
            }

            std::ostringstream expected;
            std::ostringstream actual;
            for (const auto& value : expectedValues) {
                if (expected.tellp() > 0) {
                    expected << ",";
                }
                expected << value;
            }
            for (const auto& value : actualValues) {
                if (actual.tellp() > 0) {
                    actual << ",";
                }
                actual << value;
            }
            return PrintMismatch(label, expected.str(), actual.str());
        };

    if (const auto mismatch = checkDiagnosticList(
            "resolvedTokens",
            scenario.expectations.resolvedTokens,
            routeResolveDiagnostics.resolvedTokens);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "expandedTokens",
            scenario.expectations.expandedTokens,
            routeResolveDiagnostics.expandedTokens);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureTokens",
            scenario.expectations.recognizedProcedureTokens,
            routeResolveDiagnostics.recognizedProcedureTokens);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureSources",
            scenario.expectations.procedureMetadataSources,
            routeResolveDiagnostics.procedureMetadataSources);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureRecords",
            scenario.expectations.procedureRecordKinds,
            routeResolveDiagnostics.procedureRecordKinds);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureRunways",
            scenario.expectations.procedureRunwayRecords,
            routeResolveDiagnostics.procedureRunwayRecords);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureAuthorities",
            scenario.expectations.procedureCatalogAuthorities,
            routeResolveDiagnostics.procedureCatalogAuthorities);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureCatalogFixes",
            scenario.expectations.procedureCatalogFixes,
            routeResolveDiagnostics.procedureCatalogFixes);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureBoundaryFixes",
            scenario.expectations.procedureBoundaryFixes,
            routeResolveDiagnostics.procedureBoundaryFixes);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureOrderedFixes",
            scenario.expectations.procedureOrderedFixes,
            routeResolveDiagnostics.procedureOrderedFixes);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureSyntheticWaypoints",
            scenario.expectations.procedureSyntheticWaypoints,
            routeResolveDiagnostics.procedureSyntheticWaypoints);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureSyntheticSources",
            scenario.expectations.procedureSyntheticSources,
            routeResolveDiagnostics.procedureSyntheticSources);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureApplicationStates",
            scenario.expectations.procedureApplicationStates,
            routeResolveDiagnostics.procedureApplicationStates);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureApplicationBlocks",
            scenario.expectations.procedureApplicationBlocks,
            routeResolveDiagnostics.procedureApplicationBlocks);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureAppliedFixSequences",
            scenario.expectations.procedureAppliedFixSequences,
            routeResolveDiagnostics.procedureAppliedFixSequences);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureCatalogTransitions",
            scenario.expectations.procedureCatalogTransitions,
            routeResolveDiagnostics.procedureCatalogTransitions);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureSupport",
            scenario.expectations.procedureSupportDirections,
            routeResolveDiagnostics.procedureSupportDirections);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureLinks",
            scenario.expectations.procedureTransitionLinks,
            routeResolveDiagnostics.procedureTransitionLinks);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureMisses",
            scenario.expectations.procedureTransitionMisses,
            routeResolveDiagnostics.procedureTransitionMisses);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureAnchorLinks",
            scenario.expectations.procedureAnchorLinks,
            routeResolveDiagnostics.procedureAnchorLinks);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureContextOnly",
            scenario.expectations.procedureContextOnlyTokens,
            routeResolveDiagnostics.procedureContextOnlyTokens);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "ignoredTokens",
            scenario.expectations.ignoredTokens,
            routeResolveDiagnostics.ignoredTokens);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "unsupportedTokens",
            scenario.expectations.unsupportedTokens,
            routeResolveDiagnostics.unsupportedTokens);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "unresolvedTokens",
            scenario.expectations.unresolvedTokens,
            routeResolveDiagnostics.unresolvedTokens);
        mismatch.has_value()) {
        return *mismatch;
    }

    return 0;
}
