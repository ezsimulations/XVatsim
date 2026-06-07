#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "XVatsim/brain/BrainTypes.h"
#include "XVatsim/core/RouteGrammar.h"

namespace xvatsim::core::route {

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

void AddGraphNode(
    const std::string& ident,
    const std::string& region,
    int navDataType,
    double latitudeDeg,
    double longitudeDeg,
    AirwayGraph* graph);

bool AddAirwayConnection(
    const std::string& startIdent,
    const std::string& startRegion,
    int startNavDataType,
    const std::string& endIdent,
    const std::string& endRegion,
    int endNavDataType,
    const std::string& airwayName,
    bool addForward,
    bool addBackward,
    AirwayGraph* graph);

AirwayGraph BuildAirwayGraphFromPayloads(
    const std::string& fixPayload,
    const std::string& navPayload,
    const std::string& airwayPayload);

RouteGrammarCatalog BuildRouteGrammarCatalog(const AirwayGraph& graph);
RouteGrammarCatalog BuildRouteGrammarCatalog(
    const AirwayGraph& graph,
    const std::unordered_map<std::string, ProcedureCatalogEntry>* proceduresByName);

std::vector<brain::RouteWaypointSnapshot> ResolveRouteWaypoints(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::NetworkPlanSnapshot& networkPlanSnapshot,
    const AirwayGraph& graph,
    const RouteGrammarCatalog* grammarCatalog,
    RouteResolveDiagnostics* diagnostics);

}  // namespace xvatsim::core::route
