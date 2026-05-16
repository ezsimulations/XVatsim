#pragma once

#include <unordered_map>
#include <string>
#include <unordered_set>
#include <vector>

namespace xvatsim::core::route {

enum class RouteTokenKind {
    Empty,
    Control,
    Coordinate,
    Ambiguous,
    Airway,
    Unknown,
    Procedure,
    Point,
};

struct ProcedureCatalogEntry {
    bool hasSid = false;
    bool hasStar = false;
    bool sourcedFromDepartureAirport = false;
    bool sourcedFromArrivalAirport = false;
    bool hasSidRunwayRecords = false;
    bool hasStarRunwayRecords = false;
    std::unordered_set<std::string> sidAuthoritySources;
    std::unordered_set<std::string> starAuthoritySources;
    std::unordered_set<std::string> sidRunwayTransitions;
    std::unordered_set<std::string> starRunwayTransitions;
    std::unordered_set<std::string> sidFixes;
    std::unordered_set<std::string> starFixes;
    std::vector<std::string> sidOrderedFixes;
    std::vector<std::string> starOrderedFixes;
    std::unordered_set<std::string> sidTransitions;
    std::unordered_set<std::string> starTransitions;
};

struct RouteGrammarCatalog {
    std::unordered_set<std::string> pointIdents;
    std::unordered_set<std::string> airwayNames;
    std::unordered_set<std::string> procedureNames;
    std::unordered_map<std::string, ProcedureCatalogEntry> proceduresByName;
};

struct ParsedRouteToken {
    std::string raw;
    std::string rawNormalized;
    std::string normalized;
    RouteTokenKind kind = RouteTokenKind::Empty;
    bool matchesPointCatalog = false;
    bool matchesAirwayCatalog = false;
    bool matchesProcedureCatalog = false;
    bool matchesSidProcedureCatalog = false;
    bool matchesStarProcedureCatalog = false;
    bool hasCoordinates = false;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
};

std::string NormalizeRouteToken(const std::string& token);
std::string ExtractRouteTokenBase(const std::string& token);
bool IsRouteControlToken(const std::string& token);
bool ResolveCoordinateToken(
    const std::string& token,
    double* outLatitudeDeg,
    double* outLongitudeDeg);
bool IsRunwayProcedureSegmentToken(const std::string& rawToken);
std::vector<ParsedRouteToken> ParseRouteTokens(
    const std::string& routeText,
    const RouteGrammarCatalog* catalog = nullptr);
std::string RouteTokenKindToString(RouteTokenKind kind);
const ProcedureCatalogEntry* LookupProcedureCatalogEntry(
    const RouteGrammarCatalog& catalog,
    const std::string& token);

}  // namespace xvatsim::core::route
