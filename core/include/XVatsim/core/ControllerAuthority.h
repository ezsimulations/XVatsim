#pragma once

#include <string>
#include <vector>

namespace xvatsim::core::authority {

enum class AuthoritySource {
    VatSpyFir,
    VatSpyUir,
    VatSpyBoundary,
    SimAwareTracon,
    VatGlasses,
    VatsimRadarExtension,
};

enum class AuthorityKind {
    Center,
    Terminal,
    Extension,
};

struct GeoPoint {
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
};

struct AuthorityPolygonRing {
    std::vector<GeoPoint> points;
};

struct ControllerAuthority {
    std::string id;
    AuthoritySource source = AuthoritySource::VatSpyFir;
    AuthorityKind kind = AuthorityKind::Center;
    std::string name;
    std::string polygonKey;
    std::vector<std::string> lookupKeys;
    std::vector<std::string> controllerPrefixes;
    std::vector<std::string> controllerCallsignPatterns;
    std::string sourceRecord;
};

struct AuthorityPolygon {
    std::string id;
    AuthoritySource source = AuthoritySource::VatSpyBoundary;
    AuthorityKind kind = AuthorityKind::Center;
    std::string name;
    std::string polygonKey;
    std::vector<std::string> lookupKeys;
    std::vector<AuthorityPolygonRing> rings;
    std::string sourceRecord;
};

struct AuthorityDataGap {
    std::string authorityId;
    std::string polygonKey;
    std::string reason;
    std::string sourceRecord;
};

struct ControllerAuthorityCatalog {
    std::vector<ControllerAuthority> authorities;
    std::vector<AuthorityDataGap> dataGaps;
};

struct AuthorityPolygonCatalog {
    std::vector<AuthorityPolygon> polygons;
    std::vector<AuthorityDataGap> dataGaps;
};

struct AuthorityPolygonSourceRecord {
    AuthoritySource source = AuthoritySource::VatSpyBoundary;
    std::string id;
    std::string name;
    std::string suffix;
    std::vector<std::string> prefixes;
    std::vector<std::string> lookupTokens;
    std::vector<AuthorityPolygonRing> rings;
    std::string sourceRecord;
};

struct ActiveControllerAuthority {
    std::string callsign;
    std::string authorityId;
    std::string polygonKey;
    std::string matchedPattern;
};

struct ActiveAuthorityPolygon {
    std::string callsign;
    std::string authorityId;
    std::string polygonId;
    std::string polygonKey;
    std::string matchedPattern;
    AuthoritySource polygonSource = AuthoritySource::VatSpyBoundary;
    AuthorityKind kind = AuthorityKind::Center;
};

struct AuthorityActivationResult {
    std::vector<ActiveAuthorityPolygon> activePolygons;
    std::vector<AuthorityDataGap> dataGaps;
};

std::string AuthoritySourceLabel(AuthoritySource source);
std::string NormalizeAuthorityToken(std::string value);
std::string NormalizeControllerCallsign(std::string value);
bool CallsignMatchesPattern(
    const std::string& rawPattern,
    const std::string& rawCallsign);

ControllerAuthorityCatalog CompileVatSpyAuthorityCatalog(
    const std::string& vatspyDat);

AuthorityPolygonCatalog CompileAuthorityPolygons(
    const std::vector<AuthorityPolygonSourceRecord>& sourceRecords);

std::vector<ActiveControllerAuthority> ResolveControllerAuthority(
    const ControllerAuthorityCatalog& catalog,
    const std::string& callsign,
    int vatsimFacility);

AuthorityActivationResult ActivateAuthorityPolygons(
    const ControllerAuthorityCatalog& controllerCatalog,
    const AuthorityPolygonCatalog& polygonCatalog,
    const std::string& callsign,
    int vatsimFacility);

}  // namespace xvatsim::core::authority
