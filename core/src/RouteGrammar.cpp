#include "XVatsim/core/RouteGrammar.h"

#include <cctype>
#include <cmath>
#include <sstream>

namespace xvatsim::core::route {

namespace {

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

bool CatalogContains(
    const std::unordered_set<std::string>& values,
    const std::string& token) {
    return values.find(token) != values.end();
}

}  // namespace

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

bool IsRunwayProcedureSegmentToken(const std::string& rawToken) {
    auto normalized = ExtractRouteTokenBase(rawToken);
    if (normalized.rfind("RW", 0) == 0) {
        normalized = normalized.substr(2);
    }

    if (normalized.size() < 2 || normalized.size() > 3) {
        return false;
    }
    if (std::isdigit(static_cast<unsigned char>(normalized[0])) == 0 ||
        std::isdigit(static_cast<unsigned char>(normalized[1])) == 0) {
        return false;
    }
    if (normalized.size() == 3) {
        const auto suffix = normalized[2];
        if (suffix != 'L' && suffix != 'R' && suffix != 'C') {
            return false;
        }
    }
    return true;
}

std::vector<ParsedRouteToken> ParseRouteTokens(
    const std::string& routeText,
    const RouteGrammarCatalog* catalog) {
    std::vector<ParsedRouteToken> tokens;
    std::istringstream stream(routeText);
    std::string rawToken;
    while (stream >> rawToken) {
        ParsedRouteToken token;
        token.raw = rawToken;
        token.rawNormalized = NormalizeRouteToken(rawToken);
        token.normalized = ExtractRouteTokenBase(rawToken);

        if (IsRouteControlToken(token.normalized)) {
            token.kind = RouteTokenKind::Control;
        } else if (ResolveCoordinateToken(token.normalized, &token.latitudeDeg, &token.longitudeDeg)) {
            token.kind = RouteTokenKind::Coordinate;
            token.hasCoordinates = true;
        } else if (!token.normalized.empty()) {
            if (catalog != nullptr) {
                token.matchesPointCatalog =
                    CatalogContains(catalog->pointIdents, token.normalized);
                token.matchesAirwayCatalog =
                    CatalogContains(catalog->airwayNames, token.normalized);
                if (const auto* procedureEntry =
                        LookupProcedureCatalogEntry(*catalog, token.normalized);
                    procedureEntry != nullptr) {
                    token.matchesSidProcedureCatalog = procedureEntry->hasSid;
                    token.matchesStarProcedureCatalog = procedureEntry->hasStar;
                    token.matchesProcedureCatalog =
                        token.matchesSidProcedureCatalog ||
                        token.matchesStarProcedureCatalog;
                } else {
                    token.matchesProcedureCatalog =
                        CatalogContains(catalog->procedureNames, token.normalized);
                }
            }

            if (token.matchesProcedureCatalog) {
                token.kind = RouteTokenKind::Procedure;
            } else if (token.matchesPointCatalog && token.matchesAirwayCatalog) {
                token.kind = RouteTokenKind::Ambiguous;
            } else if (token.matchesAirwayCatalog) {
                token.kind = RouteTokenKind::Airway;
            } else if (token.matchesPointCatalog) {
                token.kind = RouteTokenKind::Point;
            } else {
                token.kind = RouteTokenKind::Unknown;
            }
        }

        tokens.push_back(std::move(token));
    }
    return tokens;
}

std::string RouteTokenKindToString(RouteTokenKind kind) {
    switch (kind) {
    case RouteTokenKind::Empty:
        return "Empty";
    case RouteTokenKind::Control:
        return "Control";
    case RouteTokenKind::Coordinate:
        return "Coordinate";
    case RouteTokenKind::Ambiguous:
        return "Ambiguous";
    case RouteTokenKind::Airway:
        return "Airway";
    case RouteTokenKind::Unknown:
        return "Unknown";
    case RouteTokenKind::Procedure:
        return "Procedure";
    case RouteTokenKind::Point:
        return "Point";
    }
    return "Unknown";
}

const ProcedureCatalogEntry* LookupProcedureCatalogEntry(
    const RouteGrammarCatalog& catalog,
    const std::string& token) {
    const auto it = catalog.proceduresByName.find(token);
    if (it == catalog.proceduresByName.end()) {
        return nullptr;
    }
    return &it->second;
}

}  // namespace xvatsim::core::route
