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
    SpecialSectorData,
    AirportLocal,
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

struct AuthorityPolygonPart {
    std::string componentId;
    AuthorityPolygonRing exterior;
    std::vector<AuthorityPolygonRing> holes;
};

enum class AuthorityServiceRole {
    Approach,
    Departure,
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
    std::vector<std::string> controllerFrequencies;
    std::string sourceRecord;
    std::string proofSource;
    std::string proofDetail;
};

struct AuthorityPolygon {
    std::string id;
    std::string sourceFeatureId;
    AuthoritySource source = AuthoritySource::VatSpyBoundary;
    AuthorityKind kind = AuthorityKind::Center;
    std::string name;
    std::string polygonKey;
    std::vector<std::string> lookupKeys;
    std::vector<AuthorityServiceRole> serviceRoles;
    std::vector<std::string> controllerPrefixes;
    std::vector<AuthorityPolygonPart> parts;
    std::string sourceRecord;
};

struct AuthorityDataGap {
    std::string authorityId;
    std::string polygonKey;
    std::string reason;
    std::string sourceRecord;
    AuthoritySource source = AuthoritySource::VatSpyBoundary;
};

struct AirportCallsignAlias {
    std::string airportIcao;
    std::string callsignPrefix;
    std::string boundaryId;
    std::string sourceRecord;
};

struct ControllerAuthorityCatalog {
    std::vector<ControllerAuthority> authorities;
    std::vector<AuthorityDataGap> dataGaps;
    std::vector<AirportCallsignAlias> airportCallsignAliases;
};

struct AuthorityPolygonCatalog {
    std::vector<AuthorityPolygon> polygons;
    std::vector<AuthorityDataGap> dataGaps;
};

enum class AuthoritySourceHealthState {
    Unavailable,
    Partial,
    Complete,
    Stale,
    Invalid,
};

struct AuthoritySourceHealthReceipt {
    std::string source;
    std::string provider;
    std::string configuredIdentity;
    std::string region = "GLOBAL";
    std::string revision;
    std::uint64_t contentHash = 0;
    AuthoritySourceHealthState state =
        AuthoritySourceHealthState::Unavailable;
    std::uint64_t payloadBytes = 0;
    std::uint64_t acceptedPositions = 0;
    std::uint64_t acceptedPolygons = 0;
    std::uint64_t acceptedPolygonParts = 0;
    std::uint64_t acceptedRecords = 0;
    std::uint64_t rejectedRecords = 0;
    bool required = false;
    std::string reason;
};

struct AuthorityCatalogReceipt {
    std::uint64_t controllerPositions = 0;
    std::uint64_t polygonDefinitions = 0;
    std::uint64_t polygonParts = 0;
    std::uint64_t polygonHoles = 0;
    std::uint64_t dataGaps = 0;
    std::uint64_t retainedBytes = 0;
    bool uniquePolygonIds = false;
    bool uniquePositionIds = false;
    std::uint64_t unresolvedPositionPolygonReferences = 0;
    std::vector<AuthoritySourceHealthReceipt> sources;
};

// Immutable source generation shared by activation and every spatial query.
// Route and endpoint scopes are views over this catalog, never replacement
// catalogs and never ownership filters.
struct CanonicalAuthorityMapDataset {
    std::uint64_t identity = 0;
    std::uint64_t centerBoundaryGeneration = 0;
    std::uint64_t authorityCatalogGeneration = 0;
    std::uint64_t terminalBoundaryGeneration = 0;
    ControllerAuthorityCatalog controllerAuthorityCatalog;
    AuthorityPolygonCatalog authorityPolygonCatalog;
    AuthorityCatalogReceipt receipt;
};

struct AuthorityPolygonSourceRecord {
    AuthoritySource source = AuthoritySource::VatSpyBoundary;
    // Stable source feature identity. It is not a controller-position join key.
    std::string id;
    // Primary controller-position join key. Additional joins belong in
    // lookupTokens; neither field controls canonical geometry identity.
    std::string polygonKey;
    std::string name;
    std::string suffix;
    std::vector<AuthorityServiceRole> serviceRoles;
    std::vector<std::string> prefixes;
    std::vector<std::string> lookupTokens;
    std::vector<AuthorityPolygonPart> parts;
    std::string sourceRecord;
};

struct AuthorityPositionSourceRecord {
    AuthoritySource source = AuthoritySource::VatGlasses;
    AuthorityKind kind = AuthorityKind::Center;
    std::string id;
    std::string name;
    std::string polygonKey;
    std::string frequency;
    std::vector<std::string> controllerCallsignPatterns;
    std::string sourceRecord;
    std::string proofSource;
    std::string proofDetail;
};

struct AuthorityEvidence {
    std::string callsign;
    std::string frequency;
    int vatsimFacility = 0;
    std::string authorityId;
    AuthoritySource authoritySource = AuthoritySource::VatSpyFir;
    AuthorityKind authorityKind = AuthorityKind::Center;
    std::string polygonKey;
    std::string matchedPattern;
    bool callsignMatched = false;
    bool facilityMatched = false;
    bool frequencyRequired = false;
    bool frequencyMatched = false;
    bool frequencyOwned = false;
    std::string proofSource;
    std::string proofDetail;
    std::vector<std::string> proofItems;
    std::vector<std::string> rejectionReasons;
};

struct ActiveControllerAuthority {
    std::string callsign;
    std::string authorityId;
    std::string polygonKey;
    std::string matchedPattern;
    AuthorityKind kind = AuthorityKind::Center;
    std::string proofSource;
    std::string proofDetail;
};

struct AuthorityDecision {
    bool accepted = false;
    AuthorityEvidence evidence;
    ActiveControllerAuthority activeAuthority;
};

struct ActiveAuthorityPolygon {
    std::string callsign;
    std::string authorityId;
    std::string polygonId;
    std::string polygonKey;
    std::string matchedPattern;
    AuthoritySource polygonSource = AuthoritySource::VatSpyBoundary;
    AuthorityKind kind = AuthorityKind::Center;
    std::string proofSource;
    std::string proofDetail;
};

struct AuthorityActivationResult {
    std::vector<ActiveAuthorityPolygon> activePolygons;
    std::vector<AuthorityDataGap> dataGaps;
    std::vector<AuthorityDecision> decisions;
};

struct RelevantAuthorityPolygon {
    ActiveAuthorityPolygon activePolygon;
    bool aircraftInside = false;
    bool routeIntersects = false;
    double routeEntryDistanceNm = 0.0;
};

std::string AuthoritySourceLabel(AuthoritySource source);
std::string NormalizeAuthorityToken(std::string value);
std::string NormalizeControllerCallsign(std::string value);
bool CallsignMatchesPattern(
    const std::string& rawPattern,
    const std::string& rawCallsign);

bool AuthorityPolygonContainsPoint(
    const AuthorityPolygon& polygon,
    const GeoPoint& point);

bool AuthorityPolygonsIntersect(
    const AuthorityPolygon& left,
    const AuthorityPolygon& right);

ControllerAuthorityCatalog CompileVatSpyAuthorityCatalog(
    const std::string& vatspyDat);

ControllerAuthorityCatalog CompileAuthorityPositionCatalog(
    const std::vector<AuthorityPositionSourceRecord>& sourceRecords);

std::vector<AuthorityPositionSourceRecord> ParseAuthorityPositionSourceRecordsJson(
    AuthoritySource source,
    const std::string& payload);

std::vector<AuthorityPolygonSourceRecord> ParseAuthorityPolygonSourceRecordsJson(
    AuthoritySource source,
    const std::string& payload);

ControllerAuthorityCatalog MergeControllerAuthorityCatalogs(
    const ControllerAuthorityCatalog& left,
    const ControllerAuthorityCatalog& right);

AuthorityPolygonCatalog CompileAuthorityPolygons(
    const std::vector<AuthorityPolygonSourceRecord>& sourceRecords);

std::vector<AuthorityDecision> EvaluateControllerAuthority(
    const ControllerAuthorityCatalog& catalog,
    const std::string& callsign,
    const std::string& frequency,
    int vatsimFacility);

std::vector<ActiveControllerAuthority> ResolveControllerAuthority(
    const ControllerAuthorityCatalog& catalog,
    const std::string& callsign,
    int vatsimFacility);

std::vector<ActiveControllerAuthority> ResolveControllerAuthority(
    const ControllerAuthorityCatalog& catalog,
    const std::string& callsign,
    const std::string& frequency,
    int vatsimFacility);

AuthorityActivationResult ActivateAuthorityPolygons(
    const ControllerAuthorityCatalog& controllerCatalog,
    const AuthorityPolygonCatalog& polygonCatalog,
    const std::string& callsign,
    int vatsimFacility);

AuthorityActivationResult ActivateAuthorityPolygons(
    const ControllerAuthorityCatalog& controllerCatalog,
    const AuthorityPolygonCatalog& polygonCatalog,
    const std::string& callsign,
    const std::string& frequency,
    int vatsimFacility);

std::vector<RelevantAuthorityPolygon> ResolveRelevantAuthorityPolygons(
    const std::vector<ActiveAuthorityPolygon>& activePolygons,
    const AuthorityPolygonCatalog& polygonCatalog,
    bool hasAircraftPosition,
    const GeoPoint& aircraftPosition,
    const std::vector<GeoPoint>& routePoints);

}  // namespace xvatsim::core::authority
