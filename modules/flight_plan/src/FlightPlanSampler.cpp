#include "XVatsim/modules/flight_plan/FlightPlanSampler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cctype>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include "XPLMNavigation.h"

namespace xvatsim::modules::flight_plan {

namespace {

constexpr long long kSampleCadenceSeconds = 1;
constexpr double kSampleMovementThresholdNm = 0.05;
constexpr double kEarthRadiusNm = 3440.065;
constexpr double kCurrentAirportMaxDistanceNm = 10.0;
constexpr double kCurrentAirportMaxAglFt = 2500.0;
constexpr std::size_t kMaxAirportIdChars = 8;

long long CurrentTickSeconds() {
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

double ToRadians(double degrees) {
    return degrees * 3.14159265358979323846 / 180.0;
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
    const auto c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return kEarthRadiusNm * c;
}

std::string NormalizeAirportId(const char* airportId) {
    std::string normalized;
    if (airportId == nullptr) {
        return normalized;
    }

    for (const auto* character = airportId; *character != '\0'; ++character) {
        const auto value = static_cast<unsigned char>(*character);
        if (std::isalnum(value) == 0) {
            continue;
        }
        normalized.push_back(static_cast<char>(std::toupper(value)));
    }
    if (normalized.size() > kMaxAirportIdChars) {
        return {};
    }
    return normalized;
}

bool IsValidPosition(double latitudeDeg, double longitudeDeg) {
    return std::isfinite(latitudeDeg) &&
           std::isfinite(longitudeDeg) &&
           latitudeDeg >= -90.0 &&
           latitudeDeg <= 90.0 &&
           longitudeDeg >= -180.0 &&
           longitudeDeg <= 180.0;
}

bool ReadFmsAirportEndpoint(
    int index,
    detail::FmsAirportEndpoint* endpoint,
    void*) {
    if (endpoint == nullptr) {
        return false;
    }

    XPLMNavType navType = 0;
    char navId[256] = {};
    float latitude = 0.0f;
    float longitude = 0.0f;

    XPLMGetFMSEntryInfo(
        index,
        &navType,
        navId,
        nullptr,
        nullptr,
        &latitude,
        &longitude);

    const auto normalizedAirportId = NormalizeAirportId(navId);
    if (navType != xplm_Nav_Airport ||
        normalizedAirportId.empty() ||
        !IsValidPosition(latitude, longitude)) {
        return false;
    }

    endpoint->index = index;
    endpoint->airportIcao = normalizedAirportId;
    endpoint->latitudeDeg = latitude;
    endpoint->longitudeDeg = longitude;
    return true;
}

void ApplyNearestAirportFallback(
    const brain::AircraftStateSnapshot& aircraftState,
    brain::FlightPlanSnapshot* snapshot) {
    if (snapshot == nullptr || !aircraftState.valid) {
        return;
    }

    if (!aircraftState.onGround && aircraftState.altitudeAglFt > kCurrentAirportMaxAglFt) {
        return;
    }

    float latitude = static_cast<float>(aircraftState.latitudeDeg);
    float longitude = static_cast<float>(aircraftState.longitudeDeg);
    // Milestone 8: acceptable XPLMFindNavAid use. This is only a bounded
    // current-airport fallback near the aircraft for departure confirmation;
    // it must not be used for route waypoint or controller-authority logic.
    const auto nearestAirport = XPLMFindNavAid(
        nullptr,
        nullptr,
        &latitude,
        &longitude,
        nullptr,
        xplm_Nav_Airport);
    if (nearestAirport == XPLM_NAV_NOT_FOUND) {
        return;
    }

    XPLMNavType navType = 0;
    float airportLatitude = 0.0f;
    float airportLongitude = 0.0f;
    char airportId[32] = {};

    XPLMGetNavAidInfo(
        nearestAirport,
        &navType,
        &airportLatitude,
        &airportLongitude,
        nullptr,
        nullptr,
        nullptr,
        airportId,
        nullptr,
        nullptr);

    const auto normalizedAirportId = NormalizeAirportId(airportId);
    if (navType != xplm_Nav_Airport ||
        normalizedAirportId.empty() ||
        !IsValidPosition(airportLatitude, airportLongitude)) {
        return;
    }

    const auto distanceToAirportNm = GreatCircleDistanceNm(
        aircraftState.latitudeDeg,
        aircraftState.longitudeDeg,
        airportLatitude,
        airportLongitude);
    if (distanceToAirportNm > kCurrentAirportMaxDistanceNm) {
        return;
    }

    snapshot->currentAirportIcao = normalizedAirportId;
    snapshot->currentAirportLatDeg = airportLatitude;
    snapshot->currentAirportLonDeg = airportLongitude;
    snapshot->hasCurrentAirportCoordinates = true;
    snapshot->currentAirportSource = brain::AirportSource::CurrentLocation;

    if (snapshot->departureIcao.empty() && aircraftState.onGround) {
        snapshot->departureIcao = normalizedAirportId;
        snapshot->departureLatDeg = airportLatitude;
        snapshot->departureLonDeg = airportLongitude;
        snapshot->hasDepartureCoordinates = true;
        snapshot->departureSource = brain::AirportSource::CurrentLocation;
    }
}

void ApplyGpsDestinationFallback(brain::FlightPlanSnapshot* snapshot) {
    if (snapshot == nullptr || !snapshot->destinationIcao.empty()) {
        return;
    }

    if (XPLMGetGPSDestinationType() != xplm_Nav_Airport) {
        return;
    }

    const auto destinationRef = XPLMGetGPSDestination();
    if (destinationRef == XPLM_NAV_NOT_FOUND) {
        return;
    }

    XPLMNavType navType = 0;
    float latitude = 0.0f;
    float longitude = 0.0f;
    char airportId[32] = {};

    XPLMGetNavAidInfo(
        destinationRef,
        &navType,
        &latitude,
        &longitude,
        nullptr,
        nullptr,
        nullptr,
        airportId,
        nullptr,
        nullptr);

    const auto normalizedAirportId = NormalizeAirportId(airportId);
    if (navType != xplm_Nav_Airport ||
        normalizedAirportId.empty() ||
        !IsValidPosition(latitude, longitude)) {
        return;
    }

    snapshot->destinationIcao = normalizedAirportId;
    snapshot->destinationLatDeg = latitude;
    snapshot->destinationLonDeg = longitude;
    snapshot->hasDestinationCoordinates = true;
    snapshot->destinationSource = brain::AirportSource::OnboardGps;
}

}  // namespace

namespace detail {

FmsAirportEndpoints FindFmsAirportEndpointsBidirectionally(
    int entryCount,
    FmsAirportEndpointReader reader,
    void* context) {
    FmsAirportEndpoints endpoints;
    if (reader == nullptr) {
        return endpoints;
    }

    const auto boundedEntryCount =
        std::clamp(entryCount, 0, kMaximumFmsEntries);
    int frontIndex = 0;
    int backIndex = boundedEntryCount - 1;

    while (frontIndex <= backIndex &&
           (!endpoints.departure || !endpoints.destination)) {
        if (!endpoints.departure) {
            FmsAirportEndpoint candidate;
            ++endpoints.entriesRead;
            if (reader(frontIndex, &candidate, context)) {
                candidate.index = frontIndex;
                endpoints.departure = std::move(candidate);
            }
            ++frontIndex;
        }

        if (!endpoints.destination && backIndex >= frontIndex) {
            FmsAirportEndpoint candidate;
            ++endpoints.entriesRead;
            if (reader(backIndex, &candidate, context)) {
                candidate.index = backIndex;
                endpoints.destination = std::move(candidate);
            }
            --backIndex;
        }
    }

    // A single valid airport has historically served as both endpoints. If
    // the two searches cross before finding their own side, all remaining
    // entries have been examined and the found endpoint is necessarily unique.
    if (!endpoints.departure && endpoints.destination) {
        endpoints.departure = endpoints.destination;
    } else if (endpoints.departure && !endpoints.destination) {
        endpoints.destination = endpoints.departure;
    }

    return endpoints;
}

}  // namespace detail

void FlightPlanSampler::Reset() {
    hasSampleCache_ = false;
    cachedSnapshot_ = {};
    lastSampleTickSeconds_ = 0;
    lastSampleLatitudeDeg_ = 0.0;
    lastSampleLongitudeDeg_ = 0.0;
}

brain::FlightPlanSnapshot FlightPlanSampler::Sample(
    const brain::AircraftStateSnapshot& aircraftState) const {
    const auto nowSeconds = CurrentTickSeconds();
    if (hasSampleCache_) {
        const auto cadenceExpired =
            (nowSeconds - lastSampleTickSeconds_) >= kSampleCadenceSeconds;
        const auto movedDistanceNm =
            aircraftState.valid
                ? GreatCircleDistanceNm(
                      lastSampleLatitudeDeg_,
                      lastSampleLongitudeDeg_,
                      aircraftState.latitudeDeg,
                      aircraftState.longitudeDeg)
                : 0.0;
        if (!cadenceExpired &&
            (!aircraftState.valid || movedDistanceNm < kSampleMovementThresholdNm)) {
            return cachedSnapshot_;
        }
    }

    brain::FlightPlanSnapshot snapshot;

    const auto endpoints = detail::FindFmsAirportEndpointsBidirectionally(
        XPLMCountFMSEntries(),
        &ReadFmsAirportEndpoint,
        nullptr);
    if (endpoints.departure) {
        snapshot.departureIcao = endpoints.departure->airportIcao;
        snapshot.departureLatDeg = endpoints.departure->latitudeDeg;
        snapshot.departureLonDeg = endpoints.departure->longitudeDeg;
        snapshot.hasDepartureCoordinates = true;
        snapshot.departureSource = brain::AirportSource::OnboardFms;
    }
    if (endpoints.destination) {
        snapshot.destinationIcao = endpoints.destination->airportIcao;
        snapshot.destinationLatDeg = endpoints.destination->latitudeDeg;
        snapshot.destinationLonDeg = endpoints.destination->longitudeDeg;
        snapshot.hasDestinationCoordinates = true;
        snapshot.destinationSource = brain::AirportSource::OnboardFms;
    }

    ApplyNearestAirportFallback(aircraftState, &snapshot);
    ApplyGpsDestinationFallback(&snapshot);

    snapshot.available =
        !snapshot.departureIcao.empty() || !snapshot.destinationIcao.empty();
    cachedSnapshot_ = snapshot;
    hasSampleCache_ = true;
    lastSampleTickSeconds_ = nowSeconds;
    if (aircraftState.valid) {
        lastSampleLatitudeDeg_ = aircraftState.latitudeDeg;
        lastSampleLongitudeDeg_ = aircraftState.longitudeDeg;
    }
    return snapshot;
}

}  // namespace xvatsim::modules::flight_plan
