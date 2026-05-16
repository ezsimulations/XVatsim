#include "XVatsim/core/RouteTraversal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace xvatsim::core::route {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusNm = 3440.065;

double ToRadians(double degrees) {
    return degrees * kPi / 180.0;
}

double ClampUnit(double value) {
    return std::clamp(value, -1.0, 1.0);
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

struct Vector3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Point2D {
    double x = 0.0;
    double y = 0.0;
};

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

bool CrossesAntiMeridian(double longitudeDegA, double longitudeDegB) {
    return std::fabs(
               NormalizeLongitudeDeg(longitudeDegB) -
               NormalizeLongitudeDeg(longitudeDegA)) > 180.0;
}

bool RingCrossesAntiMeridian(const SectorPolygon& polygon) {
    double minLongitudeDeg = std::numeric_limits<double>::max();
    double maxLongitudeDeg = std::numeric_limits<double>::lowest();
    for (std::size_t index = 0; index < polygon.ring.size(); ++index) {
        const auto longitudeDeg = NormalizeLongitudeDeg(polygon.ring[index].longitudeDeg);
        minLongitudeDeg = std::min(minLongitudeDeg, longitudeDeg);
        maxLongitudeDeg = std::max(maxLongitudeDeg, longitudeDeg);
        if (index > 0 &&
            CrossesAntiMeridian(
                polygon.ring[index - 1].longitudeDeg,
                polygon.ring[index].longitudeDeg)) {
            return true;
        }
    }

    return (maxLongitudeDeg - minLongitudeDeg) > 180.0;
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

bool PointInRing(const GeoPoint& point, const SectorPolygon& polygon) {
    bool inside = false;
    const auto count = polygon.ring.size();
    if (count < 3) {
        return false;
    }

    const auto crossesAntiMeridian = RingCrossesAntiMeridian(polygon);
    const auto pointLongitudeDeg = NormalizeLongitudeDeg(point.longitudeDeg);

    for (std::size_t i = 0, j = count - 1; i < count; j = i++) {
        const auto& pi = polygon.ring[i];
        const auto& pj = polygon.ring[j];
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
                     ((pj.latitudeDeg - pi.latitudeDeg) == 0.0 ? 1e-12 : (pj.latitudeDeg - pi.latitudeDeg)) +
                 piLongitudeDeg);
        if (intersects) {
            inside = !inside;
        }
    }

    return inside;
}

bool PointInFeature(const GeoPoint& point, const SectorFeature& feature) {
    for (const auto& polygon : feature.polygons) {
        if (PointInRing(point, polygon)) {
            return true;
        }
    }
    return false;
}

std::vector<GeoPoint> SampleRoutePoints(
    const std::vector<brain::RouteWaypointSnapshot>& waypoints,
    std::vector<double>* outDistancesNm,
    double routeSampleStepNm) {
    std::vector<GeoPoint> points;
    if (outDistancesNm != nullptr) {
        outDistancesNm->clear();
    }
    if (waypoints.empty()) {
        return points;
    }

    points.push_back({waypoints.front().latitudeDeg, waypoints.front().longitudeDeg});
    if (outDistancesNm != nullptr) {
        outDistancesNm->push_back(0.0);
    }

    double accumulatedDistanceNm = 0.0;
    for (std::size_t index = 1; index < waypoints.size(); ++index) {
        const GeoPoint start{waypoints[index - 1].latitudeDeg, waypoints[index - 1].longitudeDeg};
        const GeoPoint end{waypoints[index].latitudeDeg, waypoints[index].longitudeDeg};
        const auto segmentDistanceNm = GreatCircleDistanceNm(
            start.latitudeDeg,
            start.longitudeDeg,
            end.latitudeDeg,
            end.longitudeDeg);
        const auto sampleCount = std::max(1, static_cast<int>(std::ceil(segmentDistanceNm / routeSampleStepNm)));
        for (int sampleIndex = 1; sampleIndex <= sampleCount; ++sampleIndex) {
            const auto fraction = static_cast<double>(sampleIndex) / static_cast<double>(sampleCount);
            const auto samplePoint = InterpolatePoint(start, end, fraction);
            points.push_back(samplePoint);
            if (outDistancesNm != nullptr) {
                outDistancesNm->push_back(accumulatedDistanceNm + segmentDistanceNm * fraction);
            }
        }
        accumulatedDistanceNm += segmentDistanceNm;
    }

    return points;
}

std::vector<std::size_t> ResolveContainingFeatures(
    const GeoPoint& point,
    const std::vector<SectorFeature>& features) {
    std::vector<std::size_t> matches;
    for (std::size_t index = 0; index < features.size(); ++index) {
        if (PointInFeature(point, features[index])) {
            matches.push_back(index);
        }
    }
    return matches;
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
    const SectorFeature& feature) {
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

    for (const auto& polygon : feature.polygons) {
        const auto count = polygon.ring.size();
        if (count < 2) {
            continue;
        }

        for (std::size_t index = 0; index < count; ++index) {
            const auto& edgeStartPoint = polygon.ring[index];
            const auto& edgeEndPoint = polygon.ring[(index + 1) % count];
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
    const SectorFeature& feature) {
    const auto startInside = PointInFeature(start, feature);
    const auto endInside = PointInFeature(end, feature);
    if (startInside || !endInside) {
        return std::nullopt;
    }

    double outsideFraction = 0.0;
    double insideFraction = 1.0;
    for (int iteration = 0; iteration < 48; ++iteration) {
        const auto middleFraction = (outsideFraction + insideFraction) / 2.0;
        const auto middlePoint = InterpolatePoint(start, end, middleFraction);
        if (PointInFeature(middlePoint, feature)) {
            insideFraction = middleFraction;
        } else {
            outsideFraction = middleFraction;
        }
    }

    return insideFraction;
}

std::optional<double> FindFeatureEntryFraction(
    const GeoPoint& start,
    const GeoPoint& end,
    const SectorFeature& feature) {
    if (PointInFeature(start, feature)) {
        return 0.0;
    }

    const auto boundaryFractions = CollectSegmentBoundaryFractions(start, end, feature);
    for (const auto fraction : boundaryFractions) {
        const auto beforeFraction = std::max(0.0, fraction - 1e-6);
        const auto afterFraction = std::min(1.0, fraction + 1e-6);
        const auto beforePoint = InterpolatePoint(start, end, beforeFraction);
        const auto afterPoint = InterpolatePoint(start, end, afterFraction);
        const auto beforeInside = PointInFeature(beforePoint, feature);
        const auto afterInside = PointInFeature(afterPoint, feature);
        if (!beforeInside && afterInside) {
            return fraction;
        }
    }

    return FindEntryFractionByBinarySearch(start, end, feature);
}

std::string NormalizeAuthorityIdentifier(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const auto character : value) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0 ||
            character == '_' || character == '-') {
            normalized.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(character))));
        }
    }

    const auto suffixSeparator = normalized.find('-');
    if (suffixSeparator != std::string::npos && suffixSeparator > 0) {
        normalized.erase(suffixSeparator);
    }
    return normalized;
}

std::vector<brain::RouteSectorMatchSnapshot> CollapseSectorAuthorities(
    const std::vector<brain::RouteSectorMatchSnapshot>& sectors) {
    struct Accumulator {
        brain::RouteSectorMatchSnapshot sector;
        std::unordered_set<std::string> tokens;
        std::unordered_set<std::string> controllerPrefixes;
        bool centerCoverage = false;
        bool terminalCoverage = false;
    };

    std::vector<Accumulator> accumulators;
    std::unordered_map<std::string, std::size_t> authorityIndices;

    for (const auto& sector : sectors) {
        auto authorityIdentifier = NormalizeAuthorityIdentifier(sector.identifier);
        if (authorityIdentifier.empty()) {
            authorityIdentifier = sector.identifier;
        }
        if (authorityIdentifier.empty()) {
            continue;
        }

        auto [indexIt, inserted] =
            authorityIndices.emplace(authorityIdentifier, accumulators.size());
        if (inserted) {
            Accumulator accumulator;
            accumulator.sector.identifier = authorityIdentifier;
            accumulator.sector.entryDistanceNm = std::max(0.0, sector.entryDistanceNm);
            accumulator.tokens.insert(authorityIdentifier);
            accumulators.push_back(std::move(accumulator));
        }

        auto& accumulator = accumulators[indexIt->second];
        accumulator.centerCoverage =
            accumulator.centerCoverage || sector.centerCoverage;
        accumulator.terminalCoverage =
            accumulator.terminalCoverage || sector.terminalCoverage;
        accumulator.sector.entryDistanceNm = std::min(
            accumulator.sector.entryDistanceNm,
            std::max(0.0, sector.entryDistanceNm));
        accumulator.tokens.insert(authorityIdentifier);

        for (const auto& token : sector.matchTokens) {
            const auto normalizedToken = NormalizeAuthorityIdentifier(token);
            if (!normalizedToken.empty()) {
                accumulator.tokens.insert(normalizedToken);
            }
        }

        for (const auto& controllerPrefix : sector.controllerPrefixes) {
            const auto normalizedPrefix = NormalizeAuthorityIdentifier(controllerPrefix);
            if (!normalizedPrefix.empty()) {
                accumulator.controllerPrefixes.insert(normalizedPrefix);
            }
        }
    }

    std::vector<brain::RouteSectorMatchSnapshot> collapsedSectors;
    collapsedSectors.reserve(accumulators.size());
    for (auto& accumulator : accumulators) {
        accumulator.sector.matchTokens.assign(
            accumulator.tokens.begin(),
            accumulator.tokens.end());
        std::sort(
            accumulator.sector.matchTokens.begin(),
            accumulator.sector.matchTokens.end());
        accumulator.sector.controllerPrefixes.assign(
            accumulator.controllerPrefixes.begin(),
            accumulator.controllerPrefixes.end());
        std::sort(
            accumulator.sector.controllerPrefixes.begin(),
            accumulator.sector.controllerPrefixes.end());
        accumulator.sector.centerCoverage = accumulator.centerCoverage;
        accumulator.sector.terminalCoverage = accumulator.terminalCoverage;
        collapsedSectors.push_back(std::move(accumulator.sector));
    }

    std::stable_sort(
        collapsedSectors.begin(),
        collapsedSectors.end(),
        [](const auto& left, const auto& right) {
            if (left.entryDistanceNm != right.entryDistanceNm) {
                return left.entryDistanceNm < right.entryDistanceNm;
            }
            return left.identifier < right.identifier;
        });
    return collapsedSectors;
}

void RemoveCurrentAuthoritiesFromNext(
    const std::vector<brain::RouteSectorMatchSnapshot>& currentSectors,
    std::vector<brain::RouteSectorMatchSnapshot>* nextSectors) {
    if (nextSectors == nullptr) {
        return;
    }

    std::unordered_set<std::string> currentAuthorities;
    for (const auto& sector : currentSectors) {
        const auto authorityIdentifier = NormalizeAuthorityIdentifier(sector.identifier);
        if (!authorityIdentifier.empty()) {
            currentAuthorities.insert(authorityIdentifier);
        }
    }

    nextSectors->erase(
        std::remove_if(
            nextSectors->begin(),
            nextSectors->end(),
            [&](const auto& sector) {
                const auto authorityIdentifier = NormalizeAuthorityIdentifier(sector.identifier);
                return !authorityIdentifier.empty() &&
                       currentAuthorities.find(authorityIdentifier) != currentAuthorities.end();
            }),
        nextSectors->end());
}

}  // namespace

brain::RouteSectorSnapshot BuildRouteSectorSnapshotFromWaypoints(
    const std::vector<brain::RouteWaypointSnapshot>& waypoints,
    const std::vector<SectorFeature>& features,
    const TraversalTuning& tuning) {
    brain::RouteSectorSnapshot snapshot;
    snapshot.available = true;
    snapshot.stale = false;
    snapshot.routeResolved = waypoints.size() >= 2;
    snapshot.waypoints = waypoints;

    if (!snapshot.routeResolved) {
        snapshot.statusLine = "ROUTE unresolved";
        return snapshot;
    }

    const GeoPoint routeStart{
        snapshot.waypoints.front().latitudeDeg,
        snapshot.waypoints.front().longitudeDeg,
    };

    std::vector<double> sampleDistancesNm;
    std::vector<GeoPoint> samplePoints;
    if (tuning.mode == TraversalMode::Sampled) {
        samplePoints = SampleRoutePoints(
            snapshot.waypoints,
            &sampleDistancesNm,
            tuning.routeSampleStepNm);
        if (samplePoints.empty()) {
            snapshot.routeResolved = false;
            snapshot.statusLine = "ROUTE unresolved";
            return snapshot;
        }
    }

    const auto currentFeatures = ResolveContainingFeatures(routeStart, features);
    std::unordered_set<std::string> currentIdentifiers;
    for (const auto featureIndex : currentFeatures) {
        const auto& feature = features[featureIndex];
        brain::RouteSectorMatchSnapshot sector;
        sector.identifier = feature.label;
        sector.entryDistanceNm = 0.0;
        sector.matchTokens = feature.tokens;
        sector.controllerPrefixes = feature.controllerPrefixes;
        sector.centerCoverage = true;
        snapshot.currentSectors.push_back(std::move(sector));
        currentIdentifiers.insert(feature.label);
    }

    std::unordered_set<std::string> nextIdentifiers;
    double accumulatedDistanceNm = 0.0;
    for (std::size_t waypointIndex = 1; waypointIndex < snapshot.waypoints.size(); ++waypointIndex) {
        const GeoPoint segmentStart{
            snapshot.waypoints[waypointIndex - 1].latitudeDeg,
            snapshot.waypoints[waypointIndex - 1].longitudeDeg,
        };
        const GeoPoint segmentEnd{
            snapshot.waypoints[waypointIndex].latitudeDeg,
            snapshot.waypoints[waypointIndex].longitudeDeg,
        };
        const auto segmentDistanceNm = GreatCircleDistanceNm(
            segmentStart.latitudeDeg,
            segmentStart.longitudeDeg,
            segmentEnd.latitudeDeg,
            segmentEnd.longitudeDeg);

        if (tuning.mode == TraversalMode::Sampled) {
            const auto sampleCount =
                std::max(1, static_cast<int>(std::ceil(segmentDistanceNm / tuning.routeSampleStepNm)));
            for (int sampleIndex = 1; sampleIndex <= sampleCount; ++sampleIndex) {
                const auto fraction = static_cast<double>(sampleIndex) / static_cast<double>(sampleCount);
                const auto samplePoint = InterpolatePoint(segmentStart, segmentEnd, fraction);
                const auto matches = ResolveContainingFeatures(samplePoint, features);
                for (const auto featureIndex : matches) {
                    const auto& feature = features[featureIndex];
                    if (currentIdentifiers.find(feature.label) != currentIdentifiers.end() ||
                        nextIdentifiers.find(feature.label) != nextIdentifiers.end()) {
                        continue;
                    }

                    brain::RouteSectorMatchSnapshot sector;
                    sector.identifier = feature.label;
                    sector.entryDistanceNm = accumulatedDistanceNm + segmentDistanceNm * fraction;
                    sector.matchTokens = feature.tokens;
                    sector.controllerPrefixes = feature.controllerPrefixes;
                    sector.centerCoverage = true;
                    snapshot.nextSectors.push_back(std::move(sector));
                    nextIdentifiers.insert(feature.label);
                }
            }
        } else {
            for (const auto& feature : features) {
                if (currentIdentifiers.find(feature.label) != currentIdentifiers.end() ||
                    nextIdentifiers.find(feature.label) != nextIdentifiers.end()) {
                    continue;
                }

                const auto entryFraction =
                    FindFeatureEntryFraction(segmentStart, segmentEnd, feature);
                if (!entryFraction.has_value()) {
                    continue;
                }

                brain::RouteSectorMatchSnapshot sector;
                sector.identifier = feature.label;
                sector.entryDistanceNm =
                    accumulatedDistanceNm + segmentDistanceNm * *entryFraction;
                sector.matchTokens = feature.tokens;
                sector.controllerPrefixes = feature.controllerPrefixes;
                sector.centerCoverage = true;
                snapshot.nextSectors.push_back(std::move(sector));
                nextIdentifiers.insert(feature.label);
            }
        }

        accumulatedDistanceNm += segmentDistanceNm;
    }

    snapshot.currentSectors = CollapseSectorAuthorities(snapshot.currentSectors);
    snapshot.nextSectors = CollapseSectorAuthorities(snapshot.nextSectors);
    RemoveCurrentAuthoritiesFromNext(snapshot.currentSectors, &snapshot.nextSectors);

    const auto totalSectorCount =
        snapshot.currentSectors.size() + snapshot.nextSectors.size();
    if (totalSectorCount > tuning.routeSectorSanityLimit) {
        snapshot.currentSectors.clear();
        snapshot.nextSectors.clear();
        snapshot.routeResolved = false;
        snapshot.statusLine = "ROUTE rejected by sector guard";
        return snapshot;
    }

    snapshot.statusLine =
        "ROUTE " + std::to_string(snapshot.waypoints.size()) + " pts " +
        std::to_string(snapshot.currentSectors.size()) + "/" +
        std::to_string(snapshot.nextSectors.size()) + " sectors " +
        (tuning.mode == TraversalMode::Exact ? "exact" : "sampled");
    return snapshot;
}

}  // namespace xvatsim::core::route
