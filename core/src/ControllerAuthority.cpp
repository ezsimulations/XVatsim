#include "XVatsim/core/ControllerAuthority.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>

namespace xvatsim::core::authority {

namespace {

constexpr int kVatsimFlightServiceFacility = 1;
constexpr int kVatsimDeliveryFacility = 2;
constexpr int kVatsimGroundFacility = 3;
constexpr int kVatsimTowerFacility = 4;
constexpr int kVatsimApproachFacility = 5;
constexpr int kVatsimCenterFacility = 6;
constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusNm = 3440.065;

struct Vector3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Point2D {
    double x = 0.0;
    double y = 0.0;
};

double ToRadians(double degrees) {
    return degrees * kPi / 180.0;
}

double ClampUnit(double value) {
    return std::clamp(value, -1.0, 1.0);
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

double ShortestLongitudeDeltaDeg(double fromLongitudeDeg, double toLongitudeDeg) {
    auto deltaDeg = toLongitudeDeg - fromLongitudeDeg;
    while (deltaDeg > 180.0) {
        deltaDeg -= 360.0;
    }
    while (deltaDeg < -180.0) {
        deltaDeg += 360.0;
    }
    return deltaDeg;
}

double UnwrapLongitudeRelativeDeg(double referenceLongitudeDeg, double longitudeDeg) {
    return referenceLongitudeDeg +
           ShortestLongitudeDeltaDeg(referenceLongitudeDeg, longitudeDeg);
}

double AngularDistanceRad(
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
    return 2.0 * std::atan2(std::sqrt(clampedA), std::sqrt(1.0 - clampedA));
}

double GreatCircleDistanceNm(
    double latitudeDegA,
    double longitudeDegA,
    double latitudeDegB,
    double longitudeDegB) {
    return kEarthRadiusNm *
           AngularDistanceRad(latitudeDegA, longitudeDegA, latitudeDegB, longitudeDegB);
}

double DotProduct(const Vector3& a, const Vector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
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

GeoPoint ToGeoPoint(const Vector3& vector) {
    const auto normalized = NormalizeVector(vector);
    if (!normalized.has_value()) {
        return {};
    }

    const auto horizontalLength =
        std::sqrt(normalized->x * normalized->x + normalized->y * normalized->y);
    return {
        std::atan2(normalized->z, horizontalLength) * 180.0 / kPi,
        NormalizeLongitudeDeg(std::atan2(normalized->y, normalized->x) * 180.0 / kPi),
    };
}

double AngularDistanceRad(const Vector3& a, const Vector3& b) {
    return std::acos(ClampUnit(DotProduct(a, b)));
}

GeoPoint InterpolatePoint(const GeoPoint& start, const GeoPoint& end, double fraction) {
    const auto startVector = ToUnitVector(start);
    const auto endVector = ToUnitVector(end);
    const auto angularDistanceRad = AngularDistanceRad(startVector, endVector);
    const auto sinAngularDistance = std::sin(angularDistanceRad);
    if (angularDistanceRad <= 1e-10 || std::fabs(sinAngularDistance) <= 1e-12) {
        return fraction < 0.5 ? start : end;
    }

    const auto startScale =
        std::sin((1.0 - fraction) * angularDistanceRad) / sinAngularDistance;
    const auto endScale =
        std::sin(fraction * angularDistanceRad) / sinAngularDistance;
    return ToGeoPoint(AddVector(
        ScaleVector(startVector, startScale),
        ScaleVector(endVector, endScale)));
}

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

std::vector<std::string> SplitPipeFields(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t startIndex = 0;
    while (startIndex <= line.size()) {
        const auto separatorIndex = line.find('|', startIndex);
        if (separatorIndex == std::string::npos) {
            fields.push_back(line.substr(startIndex));
            break;
        }
        fields.push_back(line.substr(startIndex, separatorIndex - startIndex));
        startIndex = separatorIndex + 1;
    }
    return fields;
}

void SortUnique(std::vector<std::string>* values) {
    if (values == nullptr) {
        return;
    }
    std::sort(values->begin(), values->end());
    values->erase(std::unique(values->begin(), values->end()), values->end());
}

std::vector<std::string> BuildCenterActivationPatterns(const std::string& prefix) {
    const auto normalizedPrefix = NormalizeAuthorityToken(prefix);
    if (normalizedPrefix.empty()) {
        return {};
    }

    std::vector<std::string> patterns{
        normalizedPrefix + "_CTR",
        normalizedPrefix + "_*_CTR",
        normalizedPrefix + "_FSS",
        normalizedPrefix + "_*_FSS",
    };
    SortUnique(&patterns);
    return patterns;
}

bool EndsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool PatternFacilityMatches(const std::string& pattern, int vatsimFacility) {
    if (EndsWith(pattern, "_DEL")) {
        return vatsimFacility == kVatsimDeliveryFacility;
    }
    if (EndsWith(pattern, "_GND")) {
        return vatsimFacility == kVatsimGroundFacility;
    }
    if (EndsWith(pattern, "_TWR")) {
        return vatsimFacility == kVatsimTowerFacility;
    }
    if (EndsWith(pattern, "_APP") || EndsWith(pattern, "_DEP")) {
        return vatsimFacility == kVatsimApproachFacility;
    }
    if (EndsWith(pattern, "_CTR")) {
        return vatsimFacility == kVatsimCenterFacility;
    }
    if (EndsWith(pattern, "_FSS")) {
        return vatsimFacility == kVatsimFlightServiceFacility;
    }
    return false;
}

std::string SourcePrefix(AuthoritySource source) {
    switch (source) {
        case AuthoritySource::VatSpyFir:
            return "VATSPY_FIR";
        case AuthoritySource::VatSpyUir:
            return "VATSPY_UIR";
        case AuthoritySource::VatSpyBoundary:
            return "VATSPY_BOUNDARY";
        case AuthoritySource::SimAwareTracon:
            return "SIMAWARE_TRACON";
        case AuthoritySource::VatGlasses:
            return "VATGLASSES";
        case AuthoritySource::VatsimRadarExtension:
            return "VATSIM_RADAR_EXTENSION";
    }
    return "UNKNOWN";
}

AuthorityKind SourceDefaultKind(AuthoritySource source) {
    switch (source) {
        case AuthoritySource::SimAwareTracon:
            return AuthorityKind::Terminal;
        case AuthoritySource::VatsimRadarExtension:
            return AuthorityKind::Extension;
        case AuthoritySource::VatSpyFir:
        case AuthoritySource::VatSpyUir:
        case AuthoritySource::VatSpyBoundary:
        case AuthoritySource::VatGlasses:
            return AuthorityKind::Center;
    }
    return AuthorityKind::Center;
}

std::string PolygonIdFromRecord(const AuthorityPolygonSourceRecord& record) {
    const auto sourcePrefix = SourcePrefix(record.source);
    const auto baseId = NormalizeAuthorityToken(record.id);
    if (baseId.empty()) {
        return {};
    }

    if (record.source == AuthoritySource::SimAwareTracon) {
        // SimAware TRACON records use a missing suffix for approach coverage.
        const auto suffix = NormalizeAuthorityToken(record.suffix).empty()
                                ? std::string("APP")
                                : NormalizeAuthorityToken(record.suffix);
        return sourcePrefix + ":" + baseId + "_" + suffix;
    }

    return sourcePrefix + ":" + baseId;
}

std::string AuthorityIdFromPositionRecord(const AuthorityPositionSourceRecord& record) {
    const auto sourcePrefix = SourcePrefix(record.source);
    const auto baseId = NormalizeAuthorityToken(record.id);
    if (baseId.empty()) {
        return {};
    }

    return sourcePrefix + ":" + baseId;
}

void AddLookupKey(std::vector<std::string>* lookupKeys, const std::string& rawKey) {
    if (lookupKeys == nullptr) {
        return;
    }
    const auto key = NormalizeAuthorityToken(rawKey);
    if (!key.empty()) {
        lookupKeys->push_back(key);
    }
}

void AddSimAwareTraconLookupKeys(
    const AuthorityPolygonSourceRecord& record,
    std::vector<std::string>* lookupKeys) {
    if (lookupKeys == nullptr) {
        return;
    }

    const auto id = NormalizeAuthorityToken(record.id);
    const auto suffix = NormalizeAuthorityToken(record.suffix).empty()
                            ? std::string("APP")
                            : NormalizeAuthorityToken(record.suffix);
    AddLookupKey(lookupKeys, id);
    AddLookupKey(lookupKeys, id + "_" + suffix);
    for (const auto& prefix : record.prefixes) {
        const auto normalizedPrefix = NormalizeAuthorityToken(prefix);
        if (normalizedPrefix.empty()) {
            continue;
        }
        AddLookupKey(lookupKeys, normalizedPrefix);
        AddLookupKey(lookupKeys, normalizedPrefix + "_" + suffix);
    }
}

bool RingIsValid(const AuthorityPolygonRing& ring) {
    return ring.points.size() >= 3;
}

std::vector<AuthorityPolygonRing> ValidRings(
    const std::vector<AuthorityPolygonRing>& rings) {
    std::vector<AuthorityPolygonRing> validRings;
    for (const auto& ring : rings) {
        if (RingIsValid(ring)) {
            validRings.push_back(ring);
        }
    }
    return validRings;
}

bool PolygonMatchesAuthorityKey(
    const AuthorityPolygon& polygon,
    const std::string& rawAuthorityKey) {
    const auto authorityKey = NormalizeAuthorityToken(rawAuthorityKey);
    if (authorityKey.empty()) {
        return false;
    }
    if (polygon.polygonKey == authorityKey) {
        return true;
    }
    return std::find(
               polygon.lookupKeys.begin(),
               polygon.lookupKeys.end(),
               authorityKey) != polygon.lookupKeys.end();
}

bool CrossesAntiMeridian(double longitudeDegA, double longitudeDegB) {
    return std::fabs(
               NormalizeLongitudeDeg(longitudeDegB) -
               NormalizeLongitudeDeg(longitudeDegA)) > 180.0;
}

bool RingCrossesAntiMeridian(const AuthorityPolygonRing& ring) {
    double minLongitudeDeg = std::numeric_limits<double>::max();
    double maxLongitudeDeg = std::numeric_limits<double>::lowest();
    for (std::size_t index = 0; index < ring.points.size(); ++index) {
        const auto longitudeDeg = NormalizeLongitudeDeg(ring.points[index].longitudeDeg);
        minLongitudeDeg = std::min(minLongitudeDeg, longitudeDeg);
        maxLongitudeDeg = std::max(maxLongitudeDeg, longitudeDeg);
        if (index > 0 &&
            CrossesAntiMeridian(
                ring.points[index - 1].longitudeDeg,
                ring.points[index].longitudeDeg)) {
            return true;
        }
    }

    return (maxLongitudeDeg - minLongitudeDeg) > 180.0;
}

bool PointInRing(const GeoPoint& point, const AuthorityPolygonRing& ring) {
    bool inside = false;
    const auto count = ring.points.size();
    if (count < 3) {
        return false;
    }

    const auto crossesAntiMeridian = RingCrossesAntiMeridian(ring);
    const auto pointLongitudeDeg = NormalizeLongitudeDeg(point.longitudeDeg);

    for (std::size_t i = 0, j = count - 1; i < count; j = i++) {
        const auto& pi = ring.points[i];
        const auto& pj = ring.points[j];
        const auto piLongitudeDeg =
            crossesAntiMeridian
                ? UnwrapLongitudeRelativeDeg(pointLongitudeDeg, pi.longitudeDeg)
                : pi.longitudeDeg;
        const auto pjLongitudeDeg =
            crossesAntiMeridian
                ? UnwrapLongitudeRelativeDeg(pointLongitudeDeg, pj.longitudeDeg)
                : pj.longitudeDeg;
        const auto intersects =
            ((pi.latitudeDeg > point.latitudeDeg) != (pj.latitudeDeg > point.latitudeDeg)) &&
            (pointLongitudeDeg <
             (pjLongitudeDeg - piLongitudeDeg) * (point.latitudeDeg - pi.latitudeDeg) /
                     ((pj.latitudeDeg - pi.latitudeDeg) == 0.0
                          ? 1e-12
                          : (pj.latitudeDeg - pi.latitudeDeg)) +
                 piLongitudeDeg);
        if (intersects) {
            inside = !inside;
        }
    }

    return inside;
}

bool PointInPolygon(const GeoPoint& point, const AuthorityPolygon& polygon) {
    for (const auto& ring : polygon.rings) {
        if (PointInRing(point, ring)) {
            return true;
        }
    }
    return false;
}

Point2D ProjectPointForSegment(
    const GeoPoint& point,
    double referenceLatitudeDeg,
    double referenceLongitudeDeg) {
    const auto unwrappedLongitudeDeg =
        UnwrapLongitudeRelativeDeg(referenceLongitudeDeg, point.longitudeDeg);
    const auto cosReferenceLatitude =
        std::max(std::cos(ToRadians(referenceLatitudeDeg)), 1e-6);
    return {
        (unwrappedLongitudeDeg - referenceLongitudeDeg) * cosReferenceLatitude,
        point.latitudeDeg - referenceLatitudeDeg,
    };
}

double Cross2D(const Point2D& left, const Point2D& right) {
    return left.x * right.y - left.y * right.x;
}

Point2D Subtract2D(const Point2D& left, const Point2D& right) {
    return {left.x - right.x, left.y - right.y};
}

std::optional<double> SegmentIntersectionFraction(
    const Point2D& segmentStart,
    const Point2D& segmentEnd,
    const Point2D& edgeStart,
    const Point2D& edgeEnd) {
    constexpr double kIntersectionTolerance = 1e-9;

    const auto segmentDelta = Subtract2D(segmentEnd, segmentStart);
    const auto edgeDelta = Subtract2D(edgeEnd, edgeStart);
    const auto denominator = Cross2D(segmentDelta, edgeDelta);
    if (std::fabs(denominator) <= kIntersectionTolerance) {
        return std::nullopt;
    }

    const auto delta = Subtract2D(edgeStart, segmentStart);
    const auto segmentFraction = Cross2D(delta, edgeDelta) / denominator;
    const auto edgeFraction = Cross2D(delta, segmentDelta) / denominator;
    if (segmentFraction < -kIntersectionTolerance ||
        segmentFraction > 1.0 + kIntersectionTolerance ||
        edgeFraction < -kIntersectionTolerance ||
        edgeFraction > 1.0 + kIntersectionTolerance) {
        return std::nullopt;
    }

    return std::clamp(segmentFraction, 0.0, 1.0);
}

std::vector<double> CollectSegmentBoundaryFractions(
    const GeoPoint& start,
    const GeoPoint& end,
    const AuthorityPolygon& polygon) {
    std::vector<double> fractions;
    const auto referenceLatitudeDeg = (start.latitudeDeg + end.latitudeDeg) / 2.0;
    const auto referenceLongitudeDeg =
        NormalizeLongitudeDeg(
            start.longitudeDeg +
            ShortestLongitudeDeltaDeg(start.longitudeDeg, end.longitudeDeg) / 2.0);
    const auto projectedStart =
        ProjectPointForSegment(start, referenceLatitudeDeg, referenceLongitudeDeg);
    const auto projectedEnd =
        ProjectPointForSegment(end, referenceLatitudeDeg, referenceLongitudeDeg);

    for (const auto& ring : polygon.rings) {
        const auto count = ring.points.size();
        if (count < 2) {
            continue;
        }

        for (std::size_t index = 0; index < count; ++index) {
            const auto& edgeStartPoint = ring.points[index];
            const auto& edgeEndPoint = ring.points[(index + 1) % count];
            const auto projectedEdgeStart =
                ProjectPointForSegment(
                    edgeStartPoint,
                    referenceLatitudeDeg,
                    referenceLongitudeDeg);
            const auto projectedEdgeEnd =
                ProjectPointForSegment(
                    edgeEndPoint,
                    referenceLatitudeDeg,
                    referenceLongitudeDeg);
            const auto fraction = SegmentIntersectionFraction(
                projectedStart,
                projectedEnd,
                projectedEdgeStart,
                projectedEdgeEnd);
            if (fraction.has_value()) {
                fractions.push_back(*fraction);
            }
        }
    }

    std::sort(fractions.begin(), fractions.end());
    fractions.erase(
        std::unique(
            fractions.begin(),
            fractions.end(),
            [](double left, double right) { return std::fabs(left - right) <= 1e-6; }),
        fractions.end());
    return fractions;
}

std::optional<double> FindEntryFractionByBinarySearch(
    const GeoPoint& start,
    const GeoPoint& end,
    const AuthorityPolygon& polygon) {
    const auto startInside = PointInPolygon(start, polygon);
    const auto endInside = PointInPolygon(end, polygon);
    if (startInside || !endInside) {
        return std::nullopt;
    }

    double outsideFraction = 0.0;
    double insideFraction = 1.0;
    for (int iteration = 0; iteration < 48; ++iteration) {
        const auto middleFraction = (outsideFraction + insideFraction) / 2.0;
        const auto middlePoint = InterpolatePoint(start, end, middleFraction);
        if (PointInPolygon(middlePoint, polygon)) {
            insideFraction = middleFraction;
        } else {
            outsideFraction = middleFraction;
        }
    }

    return insideFraction;
}

std::optional<double> FindPolygonEntryFraction(
    const GeoPoint& start,
    const GeoPoint& end,
    const AuthorityPolygon& polygon) {
    if (PointInPolygon(start, polygon)) {
        return 0.0;
    }

    const auto boundaryFractions = CollectSegmentBoundaryFractions(start, end, polygon);
    for (const auto fraction : boundaryFractions) {
        const auto beforeFraction = std::max(0.0, fraction - 1e-6);
        const auto afterFraction = std::min(1.0, fraction + 1e-6);
        const auto beforePoint = InterpolatePoint(start, end, beforeFraction);
        const auto afterPoint = InterpolatePoint(start, end, afterFraction);
        const auto beforeInside = PointInPolygon(beforePoint, polygon);
        const auto afterInside = PointInPolygon(afterPoint, polygon);
        if (!beforeInside && afterInside) {
            return fraction;
        }
    }

    return FindEntryFractionByBinarySearch(start, end, polygon);
}

std::optional<double> FindRouteEntryDistanceNm(
    const std::vector<GeoPoint>& routePoints,
    const AuthorityPolygon& polygon) {
    if (routePoints.empty()) {
        return std::nullopt;
    }
    if (PointInPolygon(routePoints.front(), polygon)) {
        return 0.0;
    }

    double accumulatedDistanceNm = 0.0;
    for (std::size_t index = 1; index < routePoints.size(); ++index) {
        const auto& start = routePoints[index - 1];
        const auto& end = routePoints[index];
        const auto segmentDistanceNm = GreatCircleDistanceNm(
            start.latitudeDeg,
            start.longitudeDeg,
            end.latitudeDeg,
            end.longitudeDeg);
        const auto entryFraction = FindPolygonEntryFraction(start, end, polygon);
        if (entryFraction.has_value()) {
            return accumulatedDistanceNm + segmentDistanceNm * *entryFraction;
        }
        accumulatedDistanceNm += segmentDistanceNm;
    }

    return std::nullopt;
}

const AuthorityPolygon* FindPolygonById(
    const AuthorityPolygonCatalog& catalog,
    const std::string& polygonId) {
    for (const auto& polygon : catalog.polygons) {
        if (polygon.id == polygonId) {
            return &polygon;
        }
    }
    return nullptr;
}

}  // namespace

std::string AuthoritySourceLabel(AuthoritySource source) {
    return SourcePrefix(source);
}

std::string NormalizeAuthorityToken(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const auto character : value) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0 ||
            character == '_' || character == '-') {
            normalized.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(character))));
        }
    }
    return normalized;
}

std::string NormalizeControllerCallsign(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const auto character : value) {
        if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            continue;
        }
        normalized.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(character))));
    }
    return normalized;
}

bool CallsignMatchesPattern(
    const std::string& rawPattern,
    const std::string& rawCallsign) {
    const auto pattern = NormalizeControllerCallsign(rawPattern);
    const auto callsign = NormalizeControllerCallsign(rawCallsign);
    if (pattern.empty() || callsign.empty()) {
        return false;
    }

    const auto wildcardIndex = pattern.find('*');
    if (wildcardIndex == std::string::npos) {
        return pattern == callsign;
    }
    if (pattern.find('*', wildcardIndex + 1) != std::string::npos) {
        return false;
    }

    const auto prefix = pattern.substr(0, wildcardIndex);
    const auto suffix = pattern.substr(wildcardIndex + 1);
    if (callsign.size() < prefix.size() + suffix.size()) {
        return false;
    }

    return callsign.compare(0, prefix.size(), prefix) == 0 &&
           callsign.compare(callsign.size() - suffix.size(), suffix.size(), suffix) == 0;
}

ControllerAuthorityCatalog CompileVatSpyAuthorityCatalog(
    const std::string& vatspyDat) {
    enum class Section {
        None,
        Firs,
        Uirs,
    };

    ControllerAuthorityCatalog catalog;
    Section currentSection = Section::None;

    std::istringstream stream(vatspyDat);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const auto trimmedLine = Trim(line);
        if (trimmedLine.empty() || trimmedLine.front() == ';') {
            continue;
        }

        if (trimmedLine == "[FIRs]") {
            currentSection = Section::Firs;
            continue;
        }
        if (trimmedLine == "[UIRs]") {
            currentSection = Section::Uirs;
            continue;
        }
        if (trimmedLine.front() == '[') {
            currentSection = Section::None;
            continue;
        }
        if (currentSection != Section::Firs && currentSection != Section::Uirs) {
            continue;
        }

        const auto fields = SplitPipeFields(trimmedLine);
        if (fields.size() < 4) {
            continue;
        }

        const auto sectorIdentifier = NormalizeAuthorityToken(fields[0]);
        const auto callsignPrefix = NormalizeAuthorityToken(fields[2]);
        auto boundaryIdentifier = NormalizeAuthorityToken(fields[3]);
        if (boundaryIdentifier.empty()) {
            boundaryIdentifier = sectorIdentifier;
        }
        if (sectorIdentifier.empty() && boundaryIdentifier.empty()) {
            continue;
        }

        const auto source =
            currentSection == Section::Uirs ? AuthoritySource::VatSpyUir : AuthoritySource::VatSpyFir;
        ControllerAuthority authority;
        authority.source = source;
        authority.kind = AuthorityKind::Center;
        authority.name = Trim(fields[1]);
        authority.polygonKey = boundaryIdentifier;
        authority.id = SourcePrefix(source) + ":" +
                       (!sectorIdentifier.empty() ? sectorIdentifier : boundaryIdentifier);
        authority.sourceRecord = trimmedLine;
        AddLookupKey(&authority.lookupKeys, sectorIdentifier);
        AddLookupKey(&authority.lookupKeys, boundaryIdentifier);
        SortUnique(&authority.lookupKeys);

        if (!callsignPrefix.empty()) {
            authority.controllerPrefixes.push_back(callsignPrefix);
            SortUnique(&authority.controllerPrefixes);
            authority.controllerCallsignPatterns =
                BuildCenterActivationPatterns(callsignPrefix);
        } else {
            catalog.dataGaps.push_back({
                authority.id,
                authority.polygonKey,
                "missing-callsign-prefix",
                authority.sourceRecord,
            });
        }

        catalog.authorities.push_back(std::move(authority));
    }

    std::sort(
        catalog.authorities.begin(),
        catalog.authorities.end(),
        [](const auto& left, const auto& right) {
            return left.id < right.id;
        });
    std::sort(
        catalog.dataGaps.begin(),
        catalog.dataGaps.end(),
        [](const auto& left, const auto& right) {
            if (left.authorityId != right.authorityId) {
                return left.authorityId < right.authorityId;
            }
            return left.reason < right.reason;
        });
    return catalog;
}

ControllerAuthorityCatalog CompileAuthorityPositionCatalog(
    const std::vector<AuthorityPositionSourceRecord>& sourceRecords) {
    ControllerAuthorityCatalog catalog;

    for (const auto& record : sourceRecords) {
        const auto authorityId = AuthorityIdFromPositionRecord(record);
        const auto polygonKey = NormalizeAuthorityToken(record.polygonKey);
        if (authorityId.empty()) {
            catalog.dataGaps.push_back({
                SourcePrefix(record.source) + ":<missing-id>",
                polygonKey,
                "missing-position-id",
                record.sourceRecord,
            });
            continue;
        }

        ControllerAuthority authority;
        authority.id = authorityId;
        authority.source = record.source;
        authority.kind = record.kind;
        authority.name = Trim(record.name);
        authority.polygonKey = polygonKey;
        authority.sourceRecord = record.sourceRecord;
        AddLookupKey(&authority.lookupKeys, record.id);
        AddLookupKey(&authority.lookupKeys, record.polygonKey);

        for (const auto& pattern : record.controllerCallsignPatterns) {
            const auto normalizedPattern = NormalizeControllerCallsign(pattern);
            if (!normalizedPattern.empty()) {
                authority.controllerCallsignPatterns.push_back(normalizedPattern);
            }
        }
        SortUnique(&authority.lookupKeys);
        SortUnique(&authority.controllerCallsignPatterns);

        if (authority.polygonKey.empty()) {
            catalog.dataGaps.push_back({
                authority.id,
                authority.polygonKey,
                "missing-polygon-key",
                authority.sourceRecord,
            });
        }
        if (authority.controllerCallsignPatterns.empty()) {
            catalog.dataGaps.push_back({
                authority.id,
                authority.polygonKey,
                "missing-callsign-pattern",
                authority.sourceRecord,
            });
        }

        catalog.authorities.push_back(std::move(authority));
    }

    std::sort(
        catalog.authorities.begin(),
        catalog.authorities.end(),
        [](const auto& left, const auto& right) {
            return left.id < right.id;
        });
    std::sort(
        catalog.dataGaps.begin(),
        catalog.dataGaps.end(),
        [](const auto& left, const auto& right) {
            if (left.authorityId != right.authorityId) {
                return left.authorityId < right.authorityId;
            }
            if (left.polygonKey != right.polygonKey) {
                return left.polygonKey < right.polygonKey;
            }
            return left.reason < right.reason;
        });
    return catalog;
}

ControllerAuthorityCatalog MergeControllerAuthorityCatalogs(
    const ControllerAuthorityCatalog& left,
    const ControllerAuthorityCatalog& right) {
    ControllerAuthorityCatalog merged;
    merged.authorities = left.authorities;
    merged.authorities.insert(
        merged.authorities.end(),
        right.authorities.begin(),
        right.authorities.end());
    merged.dataGaps = left.dataGaps;
    merged.dataGaps.insert(
        merged.dataGaps.end(),
        right.dataGaps.begin(),
        right.dataGaps.end());

    std::sort(
        merged.authorities.begin(),
        merged.authorities.end(),
        [](const auto& leftAuthority, const auto& rightAuthority) {
            return leftAuthority.id < rightAuthority.id;
        });
    std::sort(
        merged.dataGaps.begin(),
        merged.dataGaps.end(),
        [](const auto& leftGap, const auto& rightGap) {
            if (leftGap.authorityId != rightGap.authorityId) {
                return leftGap.authorityId < rightGap.authorityId;
            }
            if (leftGap.polygonKey != rightGap.polygonKey) {
                return leftGap.polygonKey < rightGap.polygonKey;
            }
            return leftGap.reason < rightGap.reason;
        });
    return merged;
}

AuthorityPolygonCatalog CompileAuthorityPolygons(
    const std::vector<AuthorityPolygonSourceRecord>& sourceRecords) {
    AuthorityPolygonCatalog catalog;

    for (const auto& record : sourceRecords) {
        const auto polygonId = PolygonIdFromRecord(record);
        const auto polygonKey = NormalizeAuthorityToken(record.id);
        if (polygonId.empty() || polygonKey.empty()) {
            catalog.dataGaps.push_back({
                polygonId.empty() ? SourcePrefix(record.source) + ":<missing-id>" : polygonId,
                polygonKey,
                "missing-polygon-key",
                record.sourceRecord,
            });
            continue;
        }

        AuthorityPolygon polygon;
        polygon.id = polygonId;
        polygon.source = record.source;
        polygon.kind = SourceDefaultKind(record.source);
        polygon.name = Trim(record.name);
        polygon.polygonKey = polygonKey;
        polygon.sourceRecord = record.sourceRecord;

        if (record.source == AuthoritySource::SimAwareTracon) {
            AddSimAwareTraconLookupKeys(record, &polygon.lookupKeys);
        } else {
            AddLookupKey(&polygon.lookupKeys, record.id);
            for (const auto& token : record.lookupTokens) {
                AddLookupKey(&polygon.lookupKeys, token);
            }
        }
        SortUnique(&polygon.lookupKeys);

        polygon.rings = ValidRings(record.rings);
        if (polygon.rings.empty()) {
            catalog.dataGaps.push_back({
                polygon.id,
                polygon.polygonKey,
                "missing-valid-polygon-ring",
                record.sourceRecord,
            });
            continue;
        }

        catalog.polygons.push_back(std::move(polygon));
    }

    std::sort(
        catalog.polygons.begin(),
        catalog.polygons.end(),
        [](const auto& left, const auto& right) {
            return left.id < right.id;
        });
    std::sort(
        catalog.dataGaps.begin(),
        catalog.dataGaps.end(),
        [](const auto& left, const auto& right) {
            if (left.authorityId != right.authorityId) {
                return left.authorityId < right.authorityId;
            }
            return left.reason < right.reason;
        });
    return catalog;
}

std::vector<ActiveControllerAuthority> ResolveControllerAuthority(
    const ControllerAuthorityCatalog& catalog,
    const std::string& callsign,
    int vatsimFacility) {
    std::vector<ActiveControllerAuthority> matches;
    std::unordered_set<std::string> insertedKeys;

    for (const auto& authority : catalog.authorities) {
        for (const auto& pattern : authority.controllerCallsignPatterns) {
            if (!PatternFacilityMatches(pattern, vatsimFacility) ||
                !CallsignMatchesPattern(pattern, callsign)) {
                continue;
            }

            const auto key = NormalizeControllerCallsign(callsign) + "|" +
                             authority.id + "|" + pattern;
            if (!insertedKeys.insert(key).second) {
                continue;
            }

            matches.push_back({
                NormalizeControllerCallsign(callsign),
                authority.id,
                authority.polygonKey,
                pattern,
            });
        }
    }

    std::sort(
        matches.begin(),
        matches.end(),
        [](const auto& left, const auto& right) {
            if (left.callsign != right.callsign) {
                return left.callsign < right.callsign;
            }
            if (left.authorityId != right.authorityId) {
                return left.authorityId < right.authorityId;
            }
            return left.matchedPattern < right.matchedPattern;
        });
    return matches;
}

AuthorityActivationResult ActivateAuthorityPolygons(
    const ControllerAuthorityCatalog& controllerCatalog,
    const AuthorityPolygonCatalog& polygonCatalog,
    const std::string& callsign,
    int vatsimFacility) {
    AuthorityActivationResult result;
    std::unordered_set<std::string> insertedActiveKeys;
    std::unordered_set<std::string> insertedGapKeys;

    const auto authorityMatches =
        ResolveControllerAuthority(controllerCatalog, callsign, vatsimFacility);
    for (const auto& authorityMatch : authorityMatches) {
        bool matchedPolygon = false;
        for (const auto& polygon : polygonCatalog.polygons) {
            if (!PolygonMatchesAuthorityKey(polygon, authorityMatch.polygonKey)) {
                continue;
            }

            matchedPolygon = true;
            const auto activeKey = authorityMatch.callsign + "|" +
                                   authorityMatch.authorityId + "|" +
                                   polygon.id + "|" +
                                   authorityMatch.matchedPattern;
            if (!insertedActiveKeys.insert(activeKey).second) {
                continue;
            }

            result.activePolygons.push_back({
                authorityMatch.callsign,
                authorityMatch.authorityId,
                polygon.id,
                polygon.polygonKey,
                authorityMatch.matchedPattern,
                polygon.source,
                polygon.kind,
            });
        }

        if (!matchedPolygon) {
            const auto gapKey = authorityMatch.authorityId + "|" +
                                authorityMatch.polygonKey + "|missing-authority-polygon";
            if (insertedGapKeys.insert(gapKey).second) {
                result.dataGaps.push_back({
                    authorityMatch.authorityId,
                    authorityMatch.polygonKey,
                    "missing-authority-polygon",
                    authorityMatch.callsign,
                });
            }
        }
    }

    std::sort(
        result.activePolygons.begin(),
        result.activePolygons.end(),
        [](const auto& left, const auto& right) {
            if (left.callsign != right.callsign) {
                return left.callsign < right.callsign;
            }
            if (left.authorityId != right.authorityId) {
                return left.authorityId < right.authorityId;
            }
            if (left.polygonId != right.polygonId) {
                return left.polygonId < right.polygonId;
            }
            return left.matchedPattern < right.matchedPattern;
        });
    std::sort(
        result.dataGaps.begin(),
        result.dataGaps.end(),
        [](const auto& left, const auto& right) {
            if (left.authorityId != right.authorityId) {
                return left.authorityId < right.authorityId;
            }
            if (left.polygonKey != right.polygonKey) {
                return left.polygonKey < right.polygonKey;
            }
            return left.reason < right.reason;
        });
    return result;
}

std::vector<RelevantAuthorityPolygon> ResolveRelevantAuthorityPolygons(
    const std::vector<ActiveAuthorityPolygon>& activePolygons,
    const AuthorityPolygonCatalog& polygonCatalog,
    bool hasAircraftPosition,
    const GeoPoint& aircraftPosition,
    const std::vector<GeoPoint>& routePoints) {
    std::vector<RelevantAuthorityPolygon> relevantPolygons;

    for (const auto& activePolygon : activePolygons) {
        const auto* polygon = FindPolygonById(polygonCatalog, activePolygon.polygonId);
        if (polygon == nullptr) {
            continue;
        }

        RelevantAuthorityPolygon relevant;
        relevant.activePolygon = activePolygon;
        relevant.aircraftInside =
            hasAircraftPosition && PointInPolygon(aircraftPosition, *polygon);

        const auto routeEntryDistanceNm =
            FindRouteEntryDistanceNm(routePoints, *polygon);
        if (routeEntryDistanceNm.has_value()) {
            relevant.routeIntersects = true;
            relevant.routeEntryDistanceNm = *routeEntryDistanceNm;
        }

        if (relevant.aircraftInside || relevant.routeIntersects) {
            relevantPolygons.push_back(std::move(relevant));
        }
    }

    std::sort(
        relevantPolygons.begin(),
        relevantPolygons.end(),
        [](const auto& left, const auto& right) {
            if (left.routeIntersects != right.routeIntersects) {
                return left.routeIntersects && !right.routeIntersects;
            }
            if (left.routeIntersects && right.routeIntersects &&
                left.routeEntryDistanceNm != right.routeEntryDistanceNm) {
                return left.routeEntryDistanceNm < right.routeEntryDistanceNm;
            }
            if (left.aircraftInside != right.aircraftInside) {
                return left.aircraftInside && !right.aircraftInside;
            }
            if (left.activePolygon.callsign != right.activePolygon.callsign) {
                return left.activePolygon.callsign < right.activePolygon.callsign;
            }
            return left.activePolygon.polygonId < right.activePolygon.polygonId;
        });
    return relevantPolygons;
}

}  // namespace xvatsim::core::authority
