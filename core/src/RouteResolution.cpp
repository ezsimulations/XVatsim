#include "XVatsim/core/RouteResolution.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <queue>
#include <sstream>
#include <unordered_set>

#include "XVatsim/core/RouteGrammar.h"

namespace xvatsim::core::route {

namespace {

constexpr double kEarthRadiusNm = 3440.065;
constexpr double kPi = 3.14159265358979323846;

struct GeoPoint {
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
};

struct Vector3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
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
    const AirwayGraph& graph,
    ResolvedRoutePoint* outPoint);

bool ResolveRoutePointTokenWithAirwayEntryContext(
    const std::string& token,
    const std::optional<GeoPoint>& referencePoint,
    const std::string& airwayToken,
    const std::string& airwayEndToken,
    const AirwayGraph& graph,
    ResolvedRoutePoint* outPoint);

double ToRadians(double degrees) {
    return degrees * kPi / 180.0;
}

double GreatCircleDistanceNm(
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
    return kEarthRadiusNm * 2.0 * std::atan2(std::sqrt(clampedA), std::sqrt(1.0 - clampedA));
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

double ClampUnit(double value) {
    return std::clamp(value, -1.0, 1.0);
}

double AngularDistanceRad(const Vector3& a, const Vector3& b) {
    return std::acos(ClampUnit(DotProduct(a, b)));
}

double AngularDistanceRad(
    double latitudeDegA,
    double longitudeDegA,
    double latitudeDegB,
    double longitudeDegB) {
    return GreatCircleDistanceNm(latitudeDegA, longitudeDegA, latitudeDegB, longitudeDegB) /
           kEarthRadiusNm;
}

std::string SplitAndNormalizeAirwayName(const std::string& value) {
    return NormalizeRouteToken(value);
}

std::vector<std::string> SplitAirwayNames(const std::string& airwayNamesToken) {
    std::vector<std::string> airwayNames;
    std::string current;
    for (const auto character : airwayNamesToken) {
        if (character == '-') {
            const auto normalized = SplitAndNormalizeAirwayName(current);
            if (!normalized.empty()) {
                airwayNames.push_back(normalized);
            }
            current.clear();
            continue;
        }
        current.push_back(character);
    }

    const auto normalized = SplitAndNormalizeAirwayName(current);
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

bool IsSupportedNavDataType(int navDataType) {
    return navDataType == 2 || navDataType == 3;
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

void LoadFixNodesFromPayload(const std::string& payload, AirwayGraph* graph) {
    if (graph == nullptr || payload.empty()) {
        return;
    }

    std::istringstream stream(payload);
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

void LoadNavNodesFromPayload(const std::string& payload, AirwayGraph* graph) {
    if (graph == nullptr || payload.empty()) {
        return;
    }

    std::istringstream stream(payload);
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

bool TokenCanActAsPoint(const ParsedRouteToken& token) {
    return token.kind == RouteTokenKind::Point ||
           token.kind == RouteTokenKind::Coordinate ||
           (token.kind == RouteTokenKind::Ambiguous && token.matchesPointCatalog);
}

bool TokenCanActAsAirway(const ParsedRouteToken& token) {
    return token.kind == RouteTokenKind::Airway ||
           (token.kind == RouteTokenKind::Ambiguous && token.matchesAirwayCatalog);
}

std::optional<std::size_t> FindNextAnchorTokenIndex(
    const std::vector<ParsedRouteToken>& parsedTokens,
    std::size_t startIndex,
    RouteResolveDiagnostics* diagnostics) {
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
    const std::vector<ParsedRouteToken>& parsedTokens,
    std::size_t startIndex,
    RouteResolveDiagnostics* diagnostics) {
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
    const std::vector<ParsedRouteToken>& parsedTokens,
    std::size_t startIndex) {
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

std::string ResolveProcedureMetadataSourceTag(
    const ProcedureCatalogEntry& procedureEntry) {
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

const std::string* ResolveSidBoundaryFix(const ProcedureCatalogEntry& procedureEntry) {
    if (procedureEntry.sidOrderedFixes.empty()) {
        return nullptr;
    }
    return &procedureEntry.sidOrderedFixes.back();
}

const std::string* ResolveStarBoundaryFix(const ProcedureCatalogEntry& procedureEntry) {
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
    const ProcedureCatalogEntry& procedureEntry) {
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
    const ProcedureCatalogEntry& procedureEntry) {
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
    const ParsedRouteToken& procedureToken,
    const std::vector<ParsedRouteToken>& parsedTokens,
    std::size_t tokenIndex,
    const RouteGrammarCatalog& grammarCatalog,
    const std::optional<GeoPoint>& referencePoint,
    const AirwayGraph& graph,
    std::vector<ResolvedRoutePoint>* outPoints) {
    if (outPoints == nullptr) {
        return false;
    }

    const auto* procedureEntry =
        LookupProcedureCatalogEntry(grammarCatalog, procedureToken.normalized);
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
                graph,
                &resolvedPoint);
        if (!resolvedWithAirwayEntry &&
            !ResolveRoutePointTokenWithRouteContext(
                orderedFix,
                currentReferencePoint,
                nextOrderedFix,
                std::nullopt,
                graph,
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
    const ParsedRouteToken& procedureToken,
    const RouteGrammarCatalog& grammarCatalog,
    const std::optional<GeoPoint>& referencePoint,
    const AirwayGraph& graph,
    std::vector<ResolvedRoutePoint>* outPoints) {
    if (outPoints == nullptr) {
        return false;
    }

    const auto* procedureEntry =
        LookupProcedureCatalogEntry(grammarCatalog, procedureToken.normalized);
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
                graph,
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

bool TryResolveSyntheticSidWaypoint(
    const ParsedRouteToken& procedureToken,
    const std::vector<ParsedRouteToken>& parsedTokens,
    std::size_t tokenIndex,
    const RouteGrammarCatalog& grammarCatalog,
    const std::optional<GeoPoint>& referencePoint,
    const AirwayGraph& graph,
    ResolvedRoutePoint* outPoint,
    RouteResolveDiagnostics* diagnostics) {
    if (outPoint == nullptr) {
        return false;
    }

    const auto* procedureEntry =
        LookupProcedureCatalogEntry(grammarCatalog, procedureToken.normalized);
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
            graph,
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
    const ParsedRouteToken& procedureToken,
    const RouteGrammarCatalog& grammarCatalog,
    const std::optional<GeoPoint>& referencePoint,
    const AirwayGraph& graph,
    ResolvedRoutePoint* outPoint,
    RouteResolveDiagnostics* diagnostics) {
    (void)diagnostics;
    if (outPoint == nullptr) {
        return false;
    }

    const auto* procedureEntry =
        LookupProcedureCatalogEntry(grammarCatalog, procedureToken.normalized);
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

void RecordProcedureMetadata(
    const ParsedRouteToken& procedureToken,
    std::size_t tokenIndex,
    const std::vector<ParsedRouteToken>& parsedTokens,
    const RouteGrammarCatalog* grammarCatalog,
    RouteResolveDiagnostics* diagnostics) {
    if (grammarCatalog == nullptr || diagnostics == nullptr) {
        return;
    }

    diagnostics->recognizedProcedureTokens.push_back(procedureToken.normalized);
    const auto* procedureEntry =
        LookupProcedureCatalogEntry(*grammarCatalog, procedureToken.normalized);
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

bool ResolveRoutePointCandidates(
    const std::string& token,
    const AirwayGraph& graph,
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
    const AirwayGraph& graph,
    ResolvedRoutePoint* outPoint) {
    if (outPoint == nullptr || token.empty()) {
        return false;
    }

    std::vector<ResolvedRoutePoint> candidates;
    if (!ResolveRoutePointCandidates(token, graph, &candidates)) {
        return false;
    }

    if (candidates.size() == 1 && !nextPointToken.has_value() &&
        !destinationPoint.has_value()) {
        *outPoint = candidates.front();
        return true;
    }

    std::vector<ResolvedRoutePoint> nextCandidates;
    if (nextPointToken.has_value() && !nextPointToken->empty()) {
        ResolveRoutePointCandidates(*nextPointToken, graph, &nextCandidates);
    }

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
    const AirwayGraph& graph,
    double* outDistanceNm) {
    if (outDistanceNm == nullptr ||
        endNodeIndices.empty() ||
        startNodeIndex >= graph.nodes.size()) {
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
    const AirwayGraph& graph,
    ResolvedRoutePoint* outPoint) {
    if (outPoint == nullptr ||
        token.empty() ||
        airwayToken.empty() ||
        airwayEndToken.empty()) {
        return false;
    }

    std::vector<ResolvedRoutePoint> candidates;
    if (!ResolveRoutePointCandidates(token, graph, &candidates)) {
        return false;
    }

    std::vector<ResolvedRoutePoint> endCandidates;
    if (!ResolveRoutePointCandidates(airwayEndToken, graph, &endCandidates)) {
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
                graph,
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
    const AirwayGraph& graph,
    std::vector<ResolvedRoutePoint>* outExpandedSegment,
    ResolvedRoutePoint* outResolvedEndWaypoint) {
    if (outExpandedSegment == nullptr) {
        return false;
    }

    outExpandedSegment->clear();
    if (startWaypoint.ident.empty() || endWaypoint.ident.empty()) {
        return false;
    }

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

}  // namespace

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
    AirwayGraph* graph) {
    if (graph == nullptr) {
        return false;
    }

    const auto normalizedAirway = NormalizeRouteToken(airwayName);
    if (normalizedAirway.empty()) {
        return false;
    }

    const auto startNodeIndex = FindExactGraphNodeIndex(
        *graph,
        NormalizeRouteToken(startIdent),
        NormalizeRouteToken(startRegion),
        startNavDataType);
    const auto endNodeIndex = FindExactGraphNodeIndex(
        *graph,
        NormalizeRouteToken(endIdent),
        NormalizeRouteToken(endRegion),
        endNavDataType);
    if (!startNodeIndex.has_value() || !endNodeIndex.has_value()) {
        return false;
    }

    const auto edgeDistanceNm = GreatCircleDistanceNm(
        graph->nodes[*startNodeIndex].latitudeDeg,
        graph->nodes[*startNodeIndex].longitudeDeg,
        graph->nodes[*endNodeIndex].latitudeDeg,
        graph->nodes[*endNodeIndex].longitudeDeg);

    auto& adjacency = graph->adjacencyByAirway[normalizedAirway];
    if (addForward) {
        adjacency[*startNodeIndex].push_back({*endNodeIndex, edgeDistanceNm});
    }
    if (addBackward) {
        adjacency[*endNodeIndex].push_back({*startNodeIndex, edgeDistanceNm});
    }
    return true;
}

AirwayGraph BuildAirwayGraphFromPayloads(
    const std::string& fixPayload,
    const std::string& navPayload,
    const std::string& airwayPayload) {
    AirwayGraph graph;
    LoadFixNodesFromPayload(fixPayload, &graph);
    LoadNavNodesFromPayload(navPayload, &graph);

    std::istringstream stream(airwayPayload);
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

        const auto airwayNames = SplitAirwayNames(fields.back());
        if (airwayNames.empty()) {
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

        const auto direction = fields[6];
        const auto addForward = direction != "B";
        const auto addBackward = direction == "N" || direction == "B";

        for (const auto& airwayName : airwayNames) {
            AddAirwayConnection(
                fields[0],
                fields[1],
                startNavDataType,
                fields[3],
                fields[4],
                endNavDataType,
                airwayName,
                addForward,
                addBackward,
                &graph);
        }
    }

    return graph;
}

RouteGrammarCatalog BuildRouteGrammarCatalog(const AirwayGraph& graph) {
    return BuildRouteGrammarCatalog(graph, nullptr);
}

RouteGrammarCatalog BuildRouteGrammarCatalog(
    const AirwayGraph& graph,
    const std::unordered_map<std::string, ProcedureCatalogEntry>* proceduresByName) {
    RouteGrammarCatalog catalog;
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

std::vector<brain::RouteWaypointSnapshot> ResolveRouteWaypoints(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::NetworkPlanSnapshot& networkPlanSnapshot,
    const AirwayGraph& graph,
    const RouteGrammarCatalog* grammarCatalog,
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

    std::optional<RouteGrammarCatalog> fallbackGrammarCatalog;
    if (grammarCatalog == nullptr) {
        fallbackGrammarCatalog = BuildRouteGrammarCatalog(graph);
        grammarCatalog = &(*fallbackGrammarCatalog);
    }
    const auto parsedTokens =
        ParseRouteTokens(networkPlanSnapshot.routeText, grammarCatalog);
    for (const auto& parsedToken : parsedTokens) {
        if (diagnostics != nullptr && !parsedToken.rawNormalized.empty()) {
            diagnostics->rawTokens.push_back(parsedToken.rawNormalized);
        }
    }

    for (std::size_t tokenIndex = 0; tokenIndex < parsedTokens.size(); ++tokenIndex) {
        const auto& parsedToken = parsedTokens[tokenIndex];
        if (parsedToken.kind == RouteTokenKind::Control ||
            parsedToken.kind == RouteTokenKind::Empty ||
            parsedToken.normalized.empty()) {
            continue;
        }

        if (parsedToken.kind == RouteTokenKind::Procedure) {
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
                    *grammarCatalog,
                    referencePoint,
                    graph,
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
                    *grammarCatalog,
                    referencePoint,
                    graph,
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
        if (parsedToken.kind == RouteTokenKind::Unknown) {
            if (diagnostics != nullptr) {
                diagnostics->unsupportedTokens.push_back(parsedToken.normalized);
            }
            continue;
        }

        if (TokenCanActAsAirway(parsedToken)) {
            bool attemptedAirwayExpansion = false;
            bool airwayExpanded = false;

            if (!resolvedFiledRoute.empty()) {
                const auto nextAnchorIndex =
                    FindNextAnchorTokenIndex(parsedTokens, tokenIndex + 1, diagnostics);
                if (nextAnchorIndex.has_value() &&
                    !parsedTokens[*nextAnchorIndex].normalized.empty()) {
                    attemptedAirwayExpansion = true;

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
                            graph,
                            &expandedSegment,
                            &resolvedAirwayEndWaypoint)) {
                        airwayExpanded = true;
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
                            RouteTokenKind::Procedure) {
                        attemptedAirwayExpansion = true;
                        const auto& nextProcedureToken =
                            parsedTokens[*nextMeaningfulIndex];
                        std::vector<ResolvedRoutePoint> syntheticStarSequence;
                        if (TryResolveSyntheticStarOrderedSequence(
                                nextProcedureToken,
                                *grammarCatalog,
                                referencePoint,
                                graph,
                                &syntheticStarSequence)) {
                            std::vector<ResolvedRoutePoint> expandedSegment;
                            ResolvedRoutePoint resolvedAirwayEndWaypoint;
                            if (ExpandAirwaySegment(
                                    resolvedFiledRoute.back(),
                                    parsedToken.normalized,
                                    syntheticStarSequence.front(),
                                    graph,
                                    &expandedSegment,
                                    &resolvedAirwayEndWaypoint)) {
                                airwayExpanded = true;
                                syntheticStarSequence.front() = resolvedAirwayEndWaypoint;
                                bool rebuiltOrderedSequence = true;
                                const auto* procedureEntry =
                                    LookupProcedureCatalogEntry(
                                        *grammarCatalog,
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
                                                graph,
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
                                if (!rebuiltOrderedSequence) {
                                    airwayExpanded = false;
                                }
                                if (airwayExpanded && diagnostics != nullptr) {
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
                                if (airwayExpanded) {
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
                                *grammarCatalog,
                                referencePoint,
                                graph,
                                &syntheticStarWaypoint,
                                diagnostics)) {
                            std::vector<ResolvedRoutePoint> expandedSegment;
                            ResolvedRoutePoint resolvedAirwayEndWaypoint;
                            if (ExpandAirwaySegment(
                                    resolvedFiledRoute.back(),
                                    parsedToken.normalized,
                                    syntheticStarWaypoint,
                                    graph,
                                    &expandedSegment,
                                    &resolvedAirwayEndWaypoint)) {
                                airwayExpanded = true;
                                if (diagnostics != nullptr) {
                                    diagnostics->expandedTokens.push_back(
                                        parsedToken.normalized);
                                    const auto* procedureEntry =
                                        LookupProcedureCatalogEntry(
                                            *grammarCatalog,
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

            if (parsedToken.kind == RouteTokenKind::Airway) {
                if (diagnostics != nullptr) {
                    diagnostics->unresolvedTokens.push_back(parsedToken.normalized);
                    diagnostics->unresolvedAirwayTokens.push_back(parsedToken.normalized);
                }
                continue;
            }

            if (airwayExpanded || attemptedAirwayExpansion) {
                // Ambiguous tokens fall through to point resolution only if
                // route context could not prove the airway interpretation.
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
                    graph,
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
                graph,
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

}  // namespace xvatsim::core::route
