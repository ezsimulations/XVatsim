#include "XVatsim/modules/transceiver_resolver/TransceiverResolver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cctype>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <windows.h>
#include <winhttp.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

namespace xvatsim::modules::transceiver_resolver {

namespace {

constexpr wchar_t kUserAgent[] = L"XVatsim/1.0.0";
constexpr wchar_t kHost[] = L"data.vatsim.net";
constexpr wchar_t kPath[] = L"/v3/transceivers-data.json";
constexpr long long kRefreshCadenceSeconds = 15;
constexpr long long kFailureBackoffSeconds = 60;
constexpr long long kFeedFreshSeconds = 45;
constexpr long long kHungFetchStaleSeconds = 30;
constexpr DWORD kWinHttpResolveTimeoutMs = 5000;
constexpr DWORD kWinHttpConnectTimeoutMs = 5000;
constexpr DWORD kWinHttpSendTimeoutMs = 5000;
constexpr DWORD kWinHttpReceiveTimeoutMs = 10000;
constexpr std::size_t kMaxPayloadBytes = 16 * 1024 * 1024;
constexpr std::size_t kMaxCallsignChars = 32;
constexpr std::size_t kMaxTransceivers = 100000;
constexpr double kMetersToFeet = 3.280839895;
constexpr double kEarthRadiusNm = 3440.065;
constexpr double kMinReceivableRangeNm = 5.0;
constexpr long long kResolveCadenceSeconds = 1;
constexpr long long kAirportCoverageResolveCadenceSeconds = 5;
constexpr long long kAuthorityStationResolveCadenceSeconds = 15;
constexpr double kResolveMovementThresholdNm = 0.05;
constexpr double kResolveAltitudeThresholdFt = 500.0;
constexpr double kAirportCoverageAircraftAglFt = 5000.0;

struct WinHttpHandle {
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) : handle(handle) {}
    ~WinHttpHandle() {
        if (handle != nullptr) {
            WinHttpCloseHandle(handle);
        }
    }

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    WinHttpHandle(WinHttpHandle&& other) noexcept : handle(other.handle) {
        other.handle = nullptr;
    }

    WinHttpHandle& operator=(WinHttpHandle&& other) noexcept {
        if (this != &other) {
            if (handle != nullptr) {
                WinHttpCloseHandle(handle);
            }
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }

    HINTERNET handle = nullptr;
};

long long CurrentTickSeconds() {
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

bool IsValidPosition(double latitudeDeg, double longitudeDeg) {
    return std::isfinite(latitudeDeg) &&
           std::isfinite(longitudeDeg) &&
           latitudeDeg >= -90.0 &&
           latitudeDeg <= 90.0 &&
           longitudeDeg >= -180.0 &&
           longitudeDeg <= 180.0;
}

std::string NormalizeCallsign(const std::string& callsign) {
    std::string normalized;
    normalized.reserve(std::min(callsign.size(), kMaxCallsignChars));

    for (const auto character : callsign) {
        const auto value = static_cast<unsigned char>(character);
        if (std::isalnum(value) != 0 || value == '_' || value == '-') {
            normalized.push_back(static_cast<char>(std::toupper(value)));
        }
        if (normalized.size() > kMaxCallsignChars) {
            return {};
        }
    }

    return normalized;
}

std::string DownloadJsonDocument() {
    WinHttpHandle session(WinHttpOpen(
        kUserAgent,
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (session.handle == nullptr) {
        return {};
    }

    WinHttpHandle connection(WinHttpConnect(session.handle, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (connection.handle == nullptr) {
        return {};
    }

    WinHttpHandle request(WinHttpOpenRequest(
        connection.handle,
        L"GET",
        kPath,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE));
    if (request.handle == nullptr) {
        return {};
    }

    (void)WinHttpSetTimeouts(
        request.handle,
        kWinHttpResolveTimeoutMs,
        kWinHttpConnectTimeoutMs,
        kWinHttpSendTimeoutMs,
        kWinHttpReceiveTimeoutMs);

    if (!WinHttpSendRequest(
            request.handle,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0)) {
        return {};
    }

    if (!WinHttpReceiveResponse(request.handle, nullptr)) {
        return {};
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(
            request.handle,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &statusCodeSize,
            WINHTTP_NO_HEADER_INDEX)) {
        return {};
    }

    if (statusCode != 200) {
        return {};
    }

    std::string payload;
    for (;;) {
        DWORD availableBytes = 0;
        if (!WinHttpQueryDataAvailable(request.handle, &availableBytes)) {
            return {};
        }

        if (availableBytes == 0) {
            break;
        }

        if ((payload.size() + static_cast<std::size_t>(availableBytes)) >
            kMaxPayloadBytes) {
            return {};
        }

        std::vector<char> buffer(availableBytes);
        DWORD downloadedBytes = 0;
        if (!WinHttpReadData(
                request.handle,
                buffer.data(),
                availableBytes,
                &downloadedBytes)) {
            return {};
        }

        if ((payload.size() + static_cast<std::size_t>(downloadedBytes)) >
            kMaxPayloadBytes) {
            return {};
        }

        payload.append(buffer.data(), downloadedBytes);
    }

    return payload;
}

std::string FormatFrequency(long long frequencyHz) {
    if (frequencyHz <= 0) {
        return {};
    }

    const auto mhz = frequencyHz / 1000000;
    const auto khz = (frequencyHz % 1000000) / 1000;
    std::string formatted = std::to_string(mhz) + ".";
    if (khz < 100) {
        formatted += "0";
    }
    if (khz < 10) {
        formatted += "0";
    }
    formatted += std::to_string(khz);
    return formatted;
}

std::string NormalizeFrequency(std::string frequency) {
    frequency.erase(
        std::remove_if(
            frequency.begin(),
            frequency.end(),
            [](unsigned char c) { return std::isspace(c) != 0; }),
        frequency.end());

    std::string digits;
    bool sawDecimal = false;
    int decimals = 0;
    for (const auto character : frequency) {
        if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
            digits.push_back(character);
            if (sawDecimal && decimals < 3) {
                ++decimals;
            }
            continue;
        }

        if (character == '.' && !sawDecimal) {
            sawDecimal = true;
        }
    }

    if (digits.empty()) {
        return {};
    }

    if (sawDecimal) {
        while (decimals < 3) {
            digits.push_back('0');
            ++decimals;
        }
    } else if (digits.size() == 5) {
        digits.push_back('0');
    }

    return digits;
}

bool IsGuardFrequency(const std::string& frequency) {
    const auto normalizedFrequency = NormalizeFrequency(frequency);
    return normalizedFrequency == "121500" || normalizedFrequency == "199998";
}

std::string ResolveDisplayFrequency(
    const std::string& controllerFrequency,
    const std::string& transceiverFrequency) {
    if (!controllerFrequency.empty() && !IsGuardFrequency(controllerFrequency)) {
        return controllerFrequency;
    }
    if (!transceiverFrequency.empty() && !IsGuardFrequency(transceiverFrequency)) {
        return transceiverFrequency;
    }
    return {};
}

std::vector<CachedTransceiver> ParseTransceivers(const std::string& payload) {
    using namespace winrt;
    using namespace winrt::Windows::Data::Json;

    static const bool kWinRtInitialized = []() {
        try {
            init_apartment();
        } catch (...) {
        }
        return true;
    }();
    (void)kWinRtInitialized;

    std::vector<CachedTransceiver> parsedTransceivers;
    if (payload.empty()) {
        return parsedTransceivers;
    }

    try {
        const auto root = JsonArray::Parse(to_hstring(payload));
        for (uint32_t clientIndex = 0; clientIndex < root.Size(); ++clientIndex) {
            const auto client = root.GetObjectAt(clientIndex);
            const auto callsign =
                NormalizeCallsign(to_string(client.GetNamedString(L"callsign", L"")));
            if (callsign.empty()) {
                continue;
            }
            const auto transceivers = client.GetNamedArray(L"transceivers", JsonArray{});

            for (uint32_t transceiverIndex = 0; transceiverIndex < transceivers.Size(); ++transceiverIndex) {
                if (parsedTransceivers.size() >= kMaxTransceivers) {
                    return parsedTransceivers;
                }

                const auto transceiver = transceivers.GetObjectAt(transceiverIndex);
                CachedTransceiver parsed;
                parsed.callsign = callsign;
                const auto frequencyHz =
                    static_cast<long long>(transceiver.GetNamedNumber(L"frequency", 0));
                parsed.frequency = FormatFrequency(frequencyHz);
                parsed.latitudeDeg = transceiver.GetNamedNumber(L"latDeg", 0.0);
                parsed.longitudeDeg = transceiver.GetNamedNumber(L"lonDeg", 0.0);
                const auto heightMslM =
                    transceiver.GetNamedNumber(
                        L"heightMslM",
                        transceiver.GetNamedNumber(L"heightAglM", 0.0));
                parsed.heightAglFt = heightMslM * kMetersToFeet;
                if (parsed.callsign.empty() ||
                    parsed.frequency.empty() ||
                    !IsValidPosition(parsed.latitudeDeg, parsed.longitudeDeg) ||
                    !std::isfinite(parsed.heightAglFt) ||
                    parsed.heightAglFt < 0.0) {
                    continue;
                }
                parsedTransceivers.push_back(std::move(parsed));
            }
        }
    } catch (...) {
        return {};
    }

    return parsedTransceivers;
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

double RadioHorizonNm(double aircraftAglFt, double transceiverAglFt) {
    const auto safeAircraftAglFt = std::max(0.0, aircraftAglFt);
    const auto safeTransceiverAglFt = std::max(0.0, transceiverAglFt);
    return 1.23 * (std::sqrt(safeAircraftAglFt) + std::sqrt(safeTransceiverAglFt));
}

std::unordered_map<std::string, std::vector<CachedTransceiver>> IndexTransceiversByCallsign(
    const std::vector<CachedTransceiver>& transceivers) {
    std::unordered_map<std::string, std::vector<CachedTransceiver>> indexed;
    for (const auto& transceiver : transceivers) {
        indexed[transceiver.callsign].push_back(transceiver);
    }
    return indexed;
}

brain::TransceiverResolutionSnapshot ResolveReceivableControllers(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const std::unordered_map<std::string, std::vector<CachedTransceiver>>& indexedTransceivers,
    bool isStaleFeed) {
    brain::TransceiverResolutionSnapshot snapshot;
    snapshot.available = !indexedTransceivers.empty();
    snapshot.stale = isStaleFeed;
    snapshot.maxCandidateDistanceNm =
        brain::kBrainOwnedMaxRadioBoardCandidateDistanceNm;
    snapshot.statusLine = isStaleFeed ? "RX feed stale" : "RX feed active";

    if (isStaleFeed) {
        snapshot.available = false;
        return snapshot;
    }

    if (!aircraftState.valid) {
        snapshot.available = false;
        snapshot.statusLine = "RX waiting for aircraft";
        return snapshot;
    }

    if (controllerFeedSnapshot.stale) {
        snapshot.available = false;
        snapshot.statusLine = "RX ATC feed stale";
        return snapshot;
    }

    if (!controllerFeedSnapshot.available) {
        snapshot.available = false;
        snapshot.statusLine = "RX waiting for ATC feed";
        return snapshot;
    }

    for (const auto& controller : controllerFeedSnapshot.Controllers()) {
        if (!controller.actionable) {
            continue;
        }

        const auto transceiverEntry = indexedTransceivers.find(controller.callsign);
        if (transceiverEntry == indexedTransceivers.end()) {
            continue;
        }

        double bestDistanceNm = std::numeric_limits<double>::max();
        double bestScore = -1.0;
        std::string bestTransceiverFrequency;
        double bestLatitudeDeg = 0.0;
        double bestLongitudeDeg = 0.0;
        bool rejectedByDistanceEnvelope = false;

        for (const auto& transceiver : transceiverEntry->second) {
            const auto distanceNm = GreatCircleDistanceNm(
                aircraftState.latitudeDeg,
                aircraftState.longitudeDeg,
                transceiver.latitudeDeg,
                transceiver.longitudeDeg);
            if (distanceNm > snapshot.maxCandidateDistanceNm) {
                rejectedByDistanceEnvelope = true;
                continue;
            }

            const auto controllerVisualRangeNm =
                controller.visualRangeNm > 0
                    ? static_cast<double>(controller.visualRangeNm)
                    : 0.0;
            const auto receivableRangeNm = std::max(
                {kMinReceivableRangeNm,
                 controllerVisualRangeNm,
                 RadioHorizonNm(
                     aircraftState.altitudeAglFt,
                     transceiver.heightAglFt)});

            if (distanceNm > receivableRangeNm) {
                continue;
            }

            const auto score = receivableRangeNm - distanceNm;
            if (score > bestScore) {
                bestScore = score;
                bestDistanceNm = distanceNm;
                bestTransceiverFrequency = transceiver.frequency;
                bestLatitudeDeg = transceiver.latitudeDeg;
                bestLongitudeDeg = transceiver.longitudeDeg;
            }
        }

        if (bestScore >= 0.0) {
            const auto displayFrequency = ResolveDisplayFrequency(
                controller.frequency,
                bestTransceiverFrequency);
            if (displayFrequency.empty()) {
                continue;
            }
            brain::ReceivableControllerSnapshot candidate;
            candidate.callsign = controller.callsign;
            candidate.frequency = displayFrequency;
            candidate.distanceNm = bestDistanceNm;
            candidate.score = bestScore;
            candidate.latitudeDeg = bestLatitudeDeg;
            candidate.longitudeDeg = bestLongitudeDeg;
            snapshot.candidates.push_back(std::move(candidate));
            continue;
        }

        if (rejectedByDistanceEnvelope) {
            ++snapshot.distanceRejectedControllers;
        }
    }

    std::sort(
        snapshot.candidates.begin(),
        snapshot.candidates.end(),
        [](const auto& left, const auto& right) {
            if (left.score == right.score) {
                return left.distanceNm < right.distanceNm;
            }
            return left.score > right.score;
        });
    snapshot.receivableControllers = static_cast<int>(snapshot.candidates.size());
    snapshot.statusLine =
        "RX " + std::to_string(snapshot.receivableControllers) + " receivable";
    if (snapshot.distanceRejectedControllers > 0) {
        snapshot.statusLine +=
            " distanceRejected=" +
            std::to_string(snapshot.distanceRejectedControllers) +
            " maxCandidateDistanceNm=" +
            std::to_string(static_cast<int>(
                std::round(snapshot.maxCandidateDistanceNm))) +
            " distanceReason=radio-candidate-over-max-distance";
    }
    return snapshot;
}

brain::TransceiverResolutionSnapshot ResolveAuthorityStations(
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const std::unordered_map<std::string, std::vector<CachedTransceiver>>& indexedTransceivers,
    bool isStaleFeed) {
    brain::TransceiverResolutionSnapshot snapshot;
    snapshot.available = !indexedTransceivers.empty();
    snapshot.stale = isStaleFeed;
    snapshot.statusLine =
        isStaleFeed ? "AUTHORITY stations feed stale" : "AUTHORITY stations active";

    if (isStaleFeed) {
        snapshot.available = false;
        return snapshot;
    }

    if (controllerFeedSnapshot.stale) {
        snapshot.available = false;
        snapshot.statusLine = "AUTHORITY stations ATC feed stale";
        return snapshot;
    }

    if (!controllerFeedSnapshot.available) {
        snapshot.available = false;
        snapshot.statusLine = "AUTHORITY stations waiting for ATC feed";
        return snapshot;
    }

    for (const auto& controller : controllerFeedSnapshot.Controllers()) {
        if (!controller.actionable) {
            continue;
        }

        const auto transceiverEntry = indexedTransceivers.find(controller.callsign);
        if (transceiverEntry == indexedTransceivers.end()) {
            continue;
        }

        for (const auto& transceiver : transceiverEntry->second) {
            const auto displayFrequency = ResolveDisplayFrequency(
                controller.frequency,
                transceiver.frequency);
            if (displayFrequency.empty()) {
                continue;
            }

            brain::ReceivableControllerSnapshot candidate;
            candidate.callsign = controller.callsign;
            candidate.frequency = displayFrequency;
            candidate.distanceNm = 0.0;
            candidate.score = 0.0;
            candidate.latitudeDeg = transceiver.latitudeDeg;
            candidate.longitudeDeg = transceiver.longitudeDeg;
            snapshot.candidates.push_back(std::move(candidate));
        }
    }

    std::sort(
        snapshot.candidates.begin(),
        snapshot.candidates.end(),
        [](const auto& left, const auto& right) {
            if (left.callsign != right.callsign) {
                return left.callsign < right.callsign;
            }
            if (left.frequency != right.frequency) {
                return left.frequency < right.frequency;
            }
            if (left.latitudeDeg != right.latitudeDeg) {
                return left.latitudeDeg < right.latitudeDeg;
            }
            return left.longitudeDeg < right.longitudeDeg;
        });
    snapshot.receivableControllers = static_cast<int>(snapshot.candidates.size());
    snapshot.statusLine =
        "AUTHORITY stations " + std::to_string(snapshot.receivableControllers) + " located";
    return snapshot;
}

brain::TransceiverResolutionSnapshot ResolveAirportCoveredControllers(
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const std::unordered_map<std::string, std::vector<CachedTransceiver>>& indexedTransceivers,
    bool isStaleFeed,
    double airportLatitudeDeg,
    double airportLongitudeDeg) {
    brain::TransceiverResolutionSnapshot snapshot;
    snapshot.available = !indexedTransceivers.empty();
    snapshot.stale = isStaleFeed;
    snapshot.statusLine = isStaleFeed ? "AIRSPACE feed stale" : "AIRSPACE feed active";

    if (isStaleFeed) {
        snapshot.available = false;
        return snapshot;
    }

    if (controllerFeedSnapshot.stale) {
        snapshot.available = false;
        snapshot.statusLine = "AIRSPACE ATC feed stale";
        return snapshot;
    }

    if (!controllerFeedSnapshot.available) {
        snapshot.available = false;
        snapshot.statusLine = "AIRSPACE waiting for ATC feed";
        return snapshot;
    }

    for (const auto& controller : controllerFeedSnapshot.Controllers()) {
        if (!controller.actionable) {
            continue;
        }

        const auto transceiverEntry = indexedTransceivers.find(controller.callsign);
        if (transceiverEntry == indexedTransceivers.end()) {
            continue;
        }

        double bestDistanceNm = std::numeric_limits<double>::max();
        double bestScore = -1.0;
        std::string bestTransceiverFrequency;
        double bestLatitudeDeg = 0.0;
        double bestLongitudeDeg = 0.0;

        for (const auto& transceiver : transceiverEntry->second) {
            const auto distanceNm = GreatCircleDistanceNm(
                airportLatitudeDeg,
                airportLongitudeDeg,
                transceiver.latitudeDeg,
                transceiver.longitudeDeg);
            const auto controllerVisualRangeNm =
                controller.visualRangeNm > 0 ? static_cast<double>(controller.visualRangeNm) : 0.0;
            const auto radioRangeNm = RadioHorizonNm(
                kAirportCoverageAircraftAglFt,
                transceiver.heightAglFt);
            const auto coverageRangeNm = std::max(
                kMinReceivableRangeNm,
                std::max(controllerVisualRangeNm, radioRangeNm));

            if (distanceNm > coverageRangeNm) {
                continue;
            }

            const auto score = coverageRangeNm - distanceNm;
            if (score > bestScore) {
                bestScore = score;
                bestDistanceNm = distanceNm;
                bestTransceiverFrequency = transceiver.frequency;
                bestLatitudeDeg = transceiver.latitudeDeg;
                bestLongitudeDeg = transceiver.longitudeDeg;
            }
        }

        if (bestScore < 0.0) {
            continue;
        }

        const auto displayFrequency = ResolveDisplayFrequency(
            controller.frequency,
            bestTransceiverFrequency);
        if (displayFrequency.empty()) {
            continue;
        }

        brain::ReceivableControllerSnapshot candidate;
        candidate.callsign = controller.callsign;
        candidate.frequency = displayFrequency;
        candidate.distanceNm = bestDistanceNm;
        candidate.score = bestScore;
        candidate.latitudeDeg = bestLatitudeDeg;
        candidate.longitudeDeg = bestLongitudeDeg;
        snapshot.candidates.push_back(std::move(candidate));
    }

    std::sort(
        snapshot.candidates.begin(),
        snapshot.candidates.end(),
        [](const auto& left, const auto& right) {
            if (left.score == right.score) {
                return left.distanceNm < right.distanceNm;
            }
            return left.score > right.score;
        });
    snapshot.receivableControllers = static_cast<int>(snapshot.candidates.size());
    snapshot.statusLine =
        "AIRSPACE " + std::to_string(snapshot.receivableControllers) + " covering";
    return snapshot;
}

std::size_t BuildControllerFeedHash(
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot) {
    std::size_t hash = 1469598103934665603ull;
    hash ^= static_cast<std::size_t>(controllerFeedSnapshot.available ? 1 : 0);
    hash *= 1099511628211ull;
    hash ^= static_cast<std::size_t>(controllerFeedSnapshot.stale ? 1 : 0);
    hash *= 1099511628211ull;
    hash ^= static_cast<std::size_t>(controllerFeedSnapshot.generation);
    hash *= 1099511628211ull;
    hash ^= static_cast<std::size_t>(controllerFeedSnapshot.connectedControllers);
    hash *= 1099511628211ull;
    for (const auto& controller : controllerFeedSnapshot.Controllers()) {
        for (const auto character : controller.callsign) {
            hash ^= static_cast<std::size_t>(static_cast<unsigned char>(character));
            hash *= 1099511628211ull;
        }
        hash ^= 0xff;
        hash *= 1099511628211ull;
        for (const auto character : NormalizeFrequency(controller.frequency)) {
            hash ^= static_cast<std::size_t>(static_cast<unsigned char>(character));
            hash *= 1099511628211ull;
        }
        hash ^= static_cast<std::size_t>(controller.facility);
        hash *= 1099511628211ull;
        hash ^= static_cast<std::size_t>(controller.visualRangeNm);
        hash *= 1099511628211ull;
        hash ^= static_cast<std::size_t>(controller.actionable ? 1 : 0);
        hash *= 1099511628211ull;
        hash ^= static_cast<std::size_t>(controller.atis ? 1 : 0);
        hash *= 1099511628211ull;
    }
    return hash;
}

}  // namespace

TransceiverResolver::~TransceiverResolver() {
    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }
}

void TransceiverResolver::Reset() {
    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }

    std::lock_guard<std::mutex> lock(fetchMutex_);
    cachedTransceivers_.clear();
    indexedTransceivers_.clear();
    pendingTransceivers_.clear();
    hasFeedCache_ = false;
    hasPendingFeed_ = false;
    lastFetchSucceeded_ = false;
    lastFetchTickSeconds_ = 0;
    lastSuccessfulFetchTickSeconds_ = 0;
    hasResolveCache_ = false;
    cachedSnapshot_ = {};
    lastResolveTickSeconds_ = 0;
    lastResolveLatitudeDeg_ = 0.0;
    lastResolveLongitudeDeg_ = 0.0;
    lastResolveAltitudeAglFt_ = 0.0;
    lastControllerFeedHash_ = 0;
    hasAuthorityStationCache_ = false;
    cachedAuthorityStationSnapshot_ = {};
    lastAuthorityStationResolveTickSeconds_ = 0;
    lastAuthorityStationControllerFeedHash_ = 0;
    hasAirportCoverageCache_ = false;
    cachedAirportCoverageSnapshot_ = {};
    lastAirportCoverageResolveTickSeconds_ = 0;
    lastAirportCoverageLatitudeDeg_ = 0.0;
    lastAirportCoverageLongitudeDeg_ = 0.0;
    lastAirportCoverageControllerFeedHash_ = 0;
    fetchInProgress_ = false;
}

brain::TransceiverResolutionSnapshot TransceiverResolver::Resolve(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot) {
    const auto refreshSucceeded = RefreshFeedIfNeeded();

    if (cachedTransceivers_.empty()) {
        brain::TransceiverResolutionSnapshot snapshot;
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.statusLine = "RX feed unavailable";
        return snapshot;
    }

    if (!refreshSucceeded) {
        brain::TransceiverResolutionSnapshot snapshot;
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.statusLine = "RX feed stale";
        return snapshot;
    }

    const auto controllerFeedHash = BuildControllerFeedHash(controllerFeedSnapshot);
    const auto nowSeconds = CurrentTickSeconds();
    const auto movedDistanceNm =
        hasResolveCache_
            ? GreatCircleDistanceNm(
                  lastResolveLatitudeDeg_,
                  lastResolveLongitudeDeg_,
                  aircraftState.latitudeDeg,
                  aircraftState.longitudeDeg)
            : std::numeric_limits<double>::max();
    const auto altitudeDeltaFt =
        hasResolveCache_
            ? std::fabs(lastResolveAltitudeAglFt_ - aircraftState.altitudeAglFt)
            : std::numeric_limits<double>::max();
    const auto feedChanged =
        !hasResolveCache_ || controllerFeedHash != lastControllerFeedHash_;
    const auto cadenceExpired =
        !hasResolveCache_ ||
        (nowSeconds - lastResolveTickSeconds_) >= kResolveCadenceSeconds;
    const auto movedEnough =
        !hasResolveCache_ ||
        movedDistanceNm >= kResolveMovementThresholdNm ||
        altitudeDeltaFt >= kResolveAltitudeThresholdFt;

    if (!feedChanged && !cadenceExpired && !movedEnough) {
        auto snapshot = cachedSnapshot_;
        snapshot.stale = false;
        return snapshot;
    }

    auto snapshot = ResolveReceivableControllers(
        aircraftState,
        controllerFeedSnapshot,
        indexedTransceivers_,
        false);
    cachedSnapshot_ = snapshot;
    hasResolveCache_ = true;
    lastResolveTickSeconds_ = nowSeconds;
    lastResolveLatitudeDeg_ = aircraftState.latitudeDeg;
    lastResolveLongitudeDeg_ = aircraftState.longitudeDeg;
    lastResolveAltitudeAglFt_ = aircraftState.altitudeAglFt;
    lastControllerFeedHash_ = controllerFeedHash;
    return snapshot;
}

brain::TransceiverResolutionSnapshot TransceiverResolver::ResolveAuthorityStations(
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot) {
    const auto refreshSucceeded = RefreshFeedIfNeeded();

    if (cachedTransceivers_.empty()) {
        brain::TransceiverResolutionSnapshot snapshot;
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.statusLine = "AUTHORITY stations feed unavailable";
        return snapshot;
    }

    if (!refreshSucceeded) {
        brain::TransceiverResolutionSnapshot snapshot;
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.statusLine = "AUTHORITY stations feed stale";
        return snapshot;
    }

    const auto controllerFeedHash = BuildControllerFeedHash(controllerFeedSnapshot);
    const auto nowSeconds = CurrentTickSeconds();
    const auto feedChanged =
        !hasAuthorityStationCache_ ||
        controllerFeedHash != lastAuthorityStationControllerFeedHash_;
    const auto cadenceExpired =
        !hasAuthorityStationCache_ ||
        (nowSeconds - lastAuthorityStationResolveTickSeconds_) >=
            kAuthorityStationResolveCadenceSeconds;

    if (!feedChanged && !cadenceExpired) {
        auto snapshot = cachedAuthorityStationSnapshot_;
        snapshot.stale = false;
        return snapshot;
    }

    auto snapshot = xvatsim::modules::transceiver_resolver::ResolveAuthorityStations(
        controllerFeedSnapshot,
        indexedTransceivers_,
        false);
    cachedAuthorityStationSnapshot_ = snapshot;
    hasAuthorityStationCache_ = true;
    lastAuthorityStationResolveTickSeconds_ = nowSeconds;
    lastAuthorityStationControllerFeedHash_ = controllerFeedHash;
    return snapshot;
}

brain::TransceiverResolutionSnapshot TransceiverResolver::ResolveAirportCoverage(
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    bool hasAirportCoordinates,
    double airportLatitudeDeg,
    double airportLongitudeDeg) {
    const auto refreshSucceeded = RefreshFeedIfNeeded();

    if (!hasAirportCoordinates) {
        brain::TransceiverResolutionSnapshot snapshot;
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.statusLine = "AIRSPACE waiting for airport";
        return snapshot;
    }

    if (cachedTransceivers_.empty()) {
        brain::TransceiverResolutionSnapshot snapshot;
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.statusLine = "AIRSPACE feed unavailable";
        return snapshot;
    }

    if (!refreshSucceeded) {
        brain::TransceiverResolutionSnapshot snapshot;
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.statusLine = "AIRSPACE feed stale";
        return snapshot;
    }

    const auto controllerFeedHash = BuildControllerFeedHash(controllerFeedSnapshot);
    const auto nowSeconds = CurrentTickSeconds();
    const auto movedDistanceNm =
        hasAirportCoverageCache_
            ? GreatCircleDistanceNm(
                  lastAirportCoverageLatitudeDeg_,
                  lastAirportCoverageLongitudeDeg_,
                  airportLatitudeDeg,
                  airportLongitudeDeg)
            : std::numeric_limits<double>::max();
    const auto feedChanged =
        !hasAirportCoverageCache_ ||
        controllerFeedHash != lastAirportCoverageControllerFeedHash_;
    const auto cadenceExpired =
        !hasAirportCoverageCache_ ||
        (nowSeconds - lastAirportCoverageResolveTickSeconds_) >=
            kAirportCoverageResolveCadenceSeconds;
    const auto movedEnough =
        !hasAirportCoverageCache_ || movedDistanceNm >= kResolveMovementThresholdNm;

    if (!feedChanged && !cadenceExpired && !movedEnough) {
        auto snapshot = cachedAirportCoverageSnapshot_;
        snapshot.stale = false;
        return snapshot;
    }

    auto snapshot = ResolveAirportCoveredControllers(
        controllerFeedSnapshot,
        indexedTransceivers_,
        false,
        airportLatitudeDeg,
        airportLongitudeDeg);
    cachedAirportCoverageSnapshot_ = snapshot;
    hasAirportCoverageCache_ = true;
    lastAirportCoverageResolveTickSeconds_ = nowSeconds;
    lastAirportCoverageLatitudeDeg_ = airportLatitudeDeg;
    lastAirportCoverageLongitudeDeg_ = airportLongitudeDeg;
    lastAirportCoverageControllerFeedHash_ = controllerFeedHash;
    return snapshot;
}

bool TransceiverResolver::RefreshFeedIfNeeded() {
    const auto nowSeconds = CurrentTickSeconds();
    HarvestPendingFetch();

    const auto fetchHung =
        fetchInProgress_.load() &&
        lastFetchTickSeconds_ != 0 &&
        (nowSeconds - lastFetchTickSeconds_) >= kHungFetchStaleSeconds;
    if (fetchHung) {
        hasResolveCache_ = false;
        hasAirportCoverageCache_ = false;
        return false;
    }

    const auto cadenceSeconds =
        lastFetchSucceeded_ ? kRefreshCadenceSeconds : kFailureBackoffSeconds;
    if (hasFeedCache_ && (nowSeconds - lastFetchTickSeconds_) < cadenceSeconds) {
        return IsFeedCacheFresh(nowSeconds);
    }

    if (!hasFeedCache_ &&
        lastFetchTickSeconds_ != 0 &&
        (nowSeconds - lastFetchTickSeconds_) < cadenceSeconds) {
        return false;
    }

    if (!fetchInProgress_.load() && !StartAsyncFetch(nowSeconds)) {
        lastFetchSucceeded_ = false;
        hasResolveCache_ = false;
        hasAirportCoverageCache_ = false;
        return false;
    }

    return IsFeedCacheFresh(nowSeconds);
}

bool TransceiverResolver::StartAsyncFetch(long long nowSeconds) {
    if (fetchInProgress_.load()) {
        return false;
    }

    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(fetchMutex_);
        pendingTransceivers_.clear();
        hasPendingFeed_ = false;
    }

    fetchInProgress_.store(true);
    try {
        fetchThread_ = std::thread([this]() {
            std::vector<CachedTransceiver> parsedTransceivers;
            try {
                const auto payload = DownloadJsonDocument();
                parsedTransceivers = ParseTransceivers(payload);
            } catch (...) {
                parsedTransceivers.clear();
            }

            try {
                std::lock_guard<std::mutex> lock(fetchMutex_);
                pendingTransceivers_ = std::move(parsedTransceivers);
                hasPendingFeed_ = true;
            } catch (...) {
            }
            fetchInProgress_.store(false);
        });
    } catch (...) {
        fetchInProgress_.store(false);
        return false;
    }

    lastFetchTickSeconds_ = nowSeconds;
    return true;
}

bool TransceiverResolver::IsFeedCacheFresh(long long nowSeconds) const {
    return hasFeedCache_ &&
           lastFetchSucceeded_ &&
           lastSuccessfulFetchTickSeconds_ != 0 &&
           (nowSeconds - lastSuccessfulFetchTickSeconds_) <= kFeedFreshSeconds;
}

void TransceiverResolver::HarvestPendingFetch() {
    if (fetchInProgress_.load()) {
        return;
    }

    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }

    std::lock_guard<std::mutex> lock(fetchMutex_);
    if (!hasPendingFeed_) {
        return;
    }

    hasPendingFeed_ = false;
    if (pendingTransceivers_.empty()) {
        pendingTransceivers_.clear();
        lastFetchSucceeded_ = false;
        hasResolveCache_ = false;
        hasAirportCoverageCache_ = false;
        return;
    }

    cachedTransceivers_ = pendingTransceivers_;
    indexedTransceivers_ = IndexTransceiversByCallsign(cachedTransceivers_);
    pendingTransceivers_.clear();
    hasFeedCache_ = true;
    lastFetchSucceeded_ = true;
    lastSuccessfulFetchTickSeconds_ = CurrentTickSeconds();
    hasResolveCache_ = false;
    hasAirportCoverageCache_ = false;
}

}  // namespace xvatsim::modules::transceiver_resolver
