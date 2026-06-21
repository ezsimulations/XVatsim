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

constexpr wchar_t kUserAgent[] = L"XVatsim/1.0.4";
constexpr wchar_t kHost[] = L"data.vatsim.net";
constexpr wchar_t kPath[] = L"/v3/transceivers-data.json";
constexpr long long kRefreshCadenceSeconds = 15;
constexpr long long kFailureBackoffSeconds = 60;
constexpr long long kFeedFreshSeconds = 45;
constexpr long long kFeedHoldoverSeconds = 180;
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
constexpr const char* kResolutionPathAuthorityStations = "authority-stations";
constexpr const char* kResolutionPathAirportCoverage = "airport-coverage";

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

struct DisplayFrequencyResolution {
    std::string frequency;
    std::string source = "none";
    std::string unavailableReason;
    bool controllerFrequencyGuard = false;
    bool transceiverFrequencyGuard = false;
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

DisplayFrequencyResolution ResolveDisplayFrequencyEvidence(
    const std::string& controllerFrequency,
    const std::string& transceiverFrequency) {
    DisplayFrequencyResolution resolution;
    const auto controllerHasFrequency = !controllerFrequency.empty();
    const auto transceiverHasFrequency = !transceiverFrequency.empty();
    resolution.controllerFrequencyGuard =
        controllerHasFrequency && IsGuardFrequency(controllerFrequency);
    resolution.transceiverFrequencyGuard =
        transceiverHasFrequency && IsGuardFrequency(transceiverFrequency);

    if (controllerHasFrequency && !resolution.controllerFrequencyGuard) {
        resolution.frequency = controllerFrequency;
        resolution.source = "controller";
        return resolution;
    }

    if (transceiverHasFrequency && !resolution.transceiverFrequencyGuard) {
        resolution.frequency = transceiverFrequency;
        resolution.source = "transceiver";
        return resolution;
    }

    if (!controllerHasFrequency && !transceiverHasFrequency) {
        resolution.unavailableReason = "both-empty";
    } else if (resolution.controllerFrequencyGuard &&
               resolution.transceiverFrequencyGuard) {
        resolution.unavailableReason = "both-guard";
    } else if (resolution.controllerFrequencyGuard &&
               !transceiverHasFrequency) {
        resolution.unavailableReason = "controller-guard-transceiver-empty";
    } else if (!controllerHasFrequency &&
               resolution.transceiverFrequencyGuard) {
        resolution.unavailableReason = "controller-empty-transceiver-guard";
    } else {
        resolution.unavailableReason = "no-display-frequency";
    }
    return resolution;
}

std::string ResolveDisplayFrequency(
    const std::string& controllerFrequency,
    const std::string& transceiverFrequency) {
    return ResolveDisplayFrequencyEvidence(
               controllerFrequency,
               transceiverFrequency)
        .frequency;
}

std::vector<CachedTransceiver> ParseTransceivers(
    const std::string& payload,
    brain::TransceiverParserHygieneCounters* counters = nullptr) {
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
        if (counters != nullptr) {
            ++counters->emptyPayload;
        }
        return parsedTransceivers;
    }

    try {
        const auto root = JsonArray::Parse(to_hstring(payload));
        for (uint32_t clientIndex = 0; clientIndex < root.Size(); ++clientIndex) {
            const auto client = root.GetObjectAt(clientIndex);
            const auto callsign =
                NormalizeCallsign(to_string(client.GetNamedString(L"callsign", L"")));
            if (callsign.empty()) {
                if (counters != nullptr) {
                    ++counters->invalidClientCallsign;
                }
                continue;
            }
            const auto transceivers = client.GetNamedArray(L"transceivers", JsonArray{});

            for (uint32_t transceiverIndex = 0; transceiverIndex < transceivers.Size(); ++transceiverIndex) {
                if (parsedTransceivers.size() >= kMaxTransceivers) {
                    if (counters != nullptr) {
                        counters->maxTransceiverTruncation = true;
                    }
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
                const auto invalidCallsign = parsed.callsign.empty();
                const auto invalidFrequency = parsed.frequency.empty();
                const auto invalidPosition =
                    !IsValidPosition(parsed.latitudeDeg, parsed.longitudeDeg);
                const auto invalidHeight =
                    !std::isfinite(parsed.heightAglFt) ||
                    parsed.heightAglFt < 0.0;
                if (invalidCallsign ||
                    invalidFrequency ||
                    invalidPosition ||
                    invalidHeight) {
                    if (counters != nullptr) {
                        if (invalidCallsign) {
                            ++counters->invalidClientCallsign;
                        }
                        if (invalidFrequency) {
                            ++counters->invalidTransceiverFrequency;
                        }
                        if (invalidPosition) {
                            ++counters->invalidPosition;
                        }
                        if (invalidHeight) {
                            ++counters->invalidHeight;
                        }
                    }
                    continue;
                }
                parsedTransceivers.push_back(std::move(parsed));
            }
        }
    } catch (...) {
        if (counters != nullptr) {
            ++counters->parseException;
        }
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

brain::TransceiverStationEvidenceSnapshot BuildStationEvidence(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::ControllerSnapshot& controller,
    const CachedTransceiver& transceiver,
    double maxCandidateDistanceNm) {
    brain::TransceiverStationEvidenceSnapshot evidence;
    evidence.sourceFrequency = transceiver.frequency;
    evidence.latitudeDeg = transceiver.latitudeDeg;
    evidence.longitudeDeg = transceiver.longitudeDeg;
    evidence.heightAglFt = transceiver.heightAglFt;
    evidence.hasAircraftDistance = true;
    evidence.aircraftDistanceNm = GreatCircleDistanceNm(
        aircraftState.latitudeDeg,
        aircraftState.longitudeDeg,
        transceiver.latitudeDeg,
        transceiver.longitudeDeg);
    evidence.maxCandidateDistanceNm = maxCandidateDistanceNm;
    evidence.withinMaxCandidateDistance =
        evidence.aircraftDistanceNm <= maxCandidateDistanceNm;

    const auto controllerVisualRangeNm =
        controller.visualRangeNm > 0
            ? static_cast<double>(controller.visualRangeNm)
            : 0.0;
    evidence.receivableRangeNm = std::max(
        {kMinReceivableRangeNm,
         controllerVisualRangeNm,
         RadioHorizonNm(
             aircraftState.altitudeAglFt,
             transceiver.heightAglFt)});
    evidence.hasReceivableRange = true;
    evidence.withinReceivableRange =
        evidence.aircraftDistanceNm <= evidence.receivableRangeNm;
    evidence.score =
        evidence.receivableRangeNm - evidence.aircraftDistanceNm;
    evidence.transceiverFrequencyGuard =
        !transceiver.frequency.empty() && IsGuardFrequency(transceiver.frequency);
    return evidence;
}

void PopulateControllerFeedSourceEvidence(
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    brain::TransceiverSourceEvidenceSnapshot* evidence) {
    if (evidence == nullptr) {
        return;
    }

    if (!controllerFeedSnapshot.available || controllerFeedSnapshot.stale) {
        evidence->sourceControllerCountKnown = false;
        evidence->sourceControllerCount = 0;
        return;
    }

    evidence->sourceControllerCountKnown = true;
    evidence->sourceControllerCount =
        static_cast<int>(controllerFeedSnapshot.Controllers().size());
}

bool NormalResolveStationUsableForCompatibilityProjection(
    const brain::TransceiverStationEvidenceSnapshot& station) {
    return station.withinMaxCandidateDistance &&
           station.withinReceivableRange;
}

bool NormalResolveStationBetterForCompatibilityProjection(
    const brain::TransceiverStationEvidenceSnapshot& candidate,
    const brain::TransceiverStationEvidenceSnapshot& currentBest) {
    if (candidate.score != currentBest.score) {
        return candidate.score > currentBest.score;
    }
    return candidate.aircraftDistanceNm < currentBest.aircraftDistanceNm;
}

const brain::TransceiverStationEvidenceSnapshot*
FindNormalResolveCompatibilityStation(
    const brain::TransceiverControllerEvidenceSnapshot& evidence) {
    const brain::TransceiverStationEvidenceSnapshot* best = nullptr;
    for (const auto& station : evidence.stations) {
        if (!NormalResolveStationUsableForCompatibilityProjection(station)) {
            continue;
        }
        if (best == nullptr ||
            NormalResolveStationBetterForCompatibilityProjection(
                station,
                *best)) {
            best = &station;
        }
    }
    return best;
}

bool HasNormalResolveOverMaxStation(
    const brain::TransceiverControllerEvidenceSnapshot& evidence) {
    return std::any_of(
        evidence.stations.begin(),
        evidence.stations.end(),
        [](const auto& station) {
            return !station.withinMaxCandidateDistance;
        });
}

void PopulateNormalResolveCompatibilityCandidates(
    brain::TransceiverResolutionSnapshot* snapshot) {
    if (snapshot == nullptr) {
        return;
    }

    snapshot->candidatesCompatibilityOnly = true;
    snapshot->candidates.clear();
    snapshot->distanceRejectedControllers = 0;

    // Compatibility projection only. BrainRadioRangeWorker owns the live
    // accept/reject decision from controllerEvidence when evidence exists.
    for (const auto& evidence : snapshot->controllerEvidence) {
        if (!evidence.actionable || !evidence.hasTransceiverEntry) {
            continue;
        }

        const auto* bestStation =
            FindNormalResolveCompatibilityStation(evidence);
        if (bestStation == nullptr) {
            if (HasNormalResolveOverMaxStation(evidence)) {
                ++snapshot->distanceRejectedControllers;
            }
            continue;
        }

        if (evidence.resolvedDisplayFrequency.empty()) {
            continue;
        }

        brain::ReceivableControllerSnapshot candidate;
        candidate.callsign = evidence.callsign;
        candidate.frequency = evidence.resolvedDisplayFrequency;
        candidate.distanceNm = bestStation->aircraftDistanceNm;
        candidate.score = bestStation->score;
        candidate.latitudeDeg = bestStation->latitudeDeg;
        candidate.longitudeDeg = bestStation->longitudeDeg;
        snapshot->candidates.push_back(std::move(candidate));
    }
}

brain::TransceiverStationEvidenceSnapshot BuildAuthorityStationEvidence(
    const CachedTransceiver& transceiver) {
    brain::TransceiverStationEvidenceSnapshot evidence;
    evidence.sourceFrequency = transceiver.frequency;
    evidence.latitudeDeg = transceiver.latitudeDeg;
    evidence.longitudeDeg = transceiver.longitudeDeg;
    evidence.heightAglFt = transceiver.heightAglFt;
    evidence.score = 0.0;
    evidence.transceiverFrequencyGuard =
        !transceiver.frequency.empty() && IsGuardFrequency(transceiver.frequency);
    return evidence;
}

std::string PathReasonFromDisplayUnavailableReason(
    const std::string& unavailableReason) {
    if (unavailableReason.find("guard") != std::string::npos) {
        return "guard-frequency";
    }
    if (unavailableReason.find("empty") != std::string::npos) {
        return "empty-frequency";
    }
    return unavailableReason.empty()
               ? std::string("no-display-frequency")
               : unavailableReason;
}

void PopulateAuthorityStationsEvidenceAndCompatibilityCandidates(
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const std::unordered_map<std::string, std::vector<CachedTransceiver>>& indexedTransceivers,
    bool emitCompatibilityCandidates,
    const std::string& pathUnavailableReason,
    brain::TransceiverResolutionSnapshot* snapshot) {
    if (snapshot == nullptr ||
        !controllerFeedSnapshot.available ||
        controllerFeedSnapshot.stale) {
        return;
    }

    snapshot->resolutionPath = kResolutionPathAuthorityStations;
    snapshot->candidatesCompatibilityOnly = true;
    snapshot->controllerEvidence.clear();
    if (emitCompatibilityCandidates) {
        snapshot->candidates.clear();
    }

    for (const auto& controller : controllerFeedSnapshot.Controllers()) {
        brain::TransceiverControllerEvidenceSnapshot evidence;
        evidence.callsign = controller.callsign;
        evidence.controllerFrequency = controller.frequency;
        evidence.facility = controller.facility;
        evidence.actionable = controller.actionable;
        evidence.atis = controller.atis;
        evidence.visualRangeNm = controller.visualRangeNm;
        evidence.controllerFrequencyGuard =
            !controller.frequency.empty() && IsGuardFrequency(controller.frequency);

        const auto transceiverEntry = indexedTransceivers.find(controller.callsign);
        evidence.hasTransceiverEntry =
            transceiverEntry != indexedTransceivers.end();
        evidence.matchingTransceiverCount =
            evidence.hasTransceiverEntry
                ? static_cast<int>(transceiverEntry->second.size())
                : 0;

        std::string firstUnavailableDisplayReason;
        if (evidence.hasTransceiverEntry) {
            evidence.stations.reserve(transceiverEntry->second.size());
            for (const auto& transceiver : transceiverEntry->second) {
                auto stationEvidence = BuildAuthorityStationEvidence(transceiver);
                evidence.transceiverFrequencyGuard =
                    evidence.transceiverFrequencyGuard ||
                    stationEvidence.transceiverFrequencyGuard;

                const auto displayResolution = ResolveDisplayFrequencyEvidence(
                    controller.frequency,
                    transceiver.frequency);
                evidence.controllerFrequencyGuard =
                    evidence.controllerFrequencyGuard ||
                    displayResolution.controllerFrequencyGuard;
                evidence.transceiverFrequencyGuard =
                    evidence.transceiverFrequencyGuard ||
                    displayResolution.transceiverFrequencyGuard;
                if (!displayResolution.frequency.empty()) {
                    if (evidence.resolvedDisplayFrequency.empty()) {
                        evidence.resolvedDisplayFrequency =
                            displayResolution.frequency;
                        evidence.displayFrequencySource =
                            displayResolution.source;
                        evidence.displayFrequencyUnavailableReason.clear();
                    }
                    if (emitCompatibilityCandidates &&
                        pathUnavailableReason.empty() &&
                        controller.actionable) {
                        brain::ReceivableControllerSnapshot candidate;
                        candidate.callsign = controller.callsign;
                        candidate.frequency = displayResolution.frequency;
                        candidate.distanceNm = 0.0;
                        candidate.score = 0.0;
                        candidate.latitudeDeg = transceiver.latitudeDeg;
                        candidate.longitudeDeg = transceiver.longitudeDeg;
                        snapshot->candidates.push_back(std::move(candidate));
                    }
                } else if (firstUnavailableDisplayReason.empty()) {
                    firstUnavailableDisplayReason =
                        displayResolution.unavailableReason;
                }

                evidence.stations.push_back(std::move(stationEvidence));
            }
        }

        if (evidence.resolvedDisplayFrequency.empty()) {
            evidence.displayFrequencySource = "none";
            evidence.displayFrequencyUnavailableReason =
                evidence.hasTransceiverEntry
                    ? firstUnavailableDisplayReason
                    : std::string("missing-transceiver");
            if (evidence.displayFrequencyUnavailableReason.empty()) {
                evidence.displayFrequencyUnavailableReason =
                    "no-display-frequency";
            }
        }

        if (!pathUnavailableReason.empty()) {
            evidence.pathUnavailableReason = pathUnavailableReason;
        } else if (!evidence.actionable) {
            evidence.pathUnavailableReason = "non-actionable";
        } else if (!evidence.hasTransceiverEntry) {
            evidence.pathUnavailableReason = "missing-transceiver";
        } else if (evidence.resolvedDisplayFrequency.empty()) {
            evidence.pathUnavailableReason =
                PathReasonFromDisplayUnavailableReason(
                    evidence.displayFrequencyUnavailableReason);
        }

        snapshot->controllerEvidence.push_back(std::move(evidence));
    }

    const auto sourceControllerCount =
        static_cast<int>(controllerFeedSnapshot.Controllers().size());
    const auto evidenceControllerCount =
        static_cast<int>(snapshot->controllerEvidence.size());
    snapshot->droppedBeforeBrainControllers =
        std::max(0, sourceControllerCount - evidenceControllerCount);
}

brain::TransceiverStationEvidenceSnapshot BuildAirportCoverageStationEvidence(
    const brain::ControllerSnapshot& controller,
    const CachedTransceiver& transceiver,
    double airportLatitudeDeg,
    double airportLongitudeDeg) {
    brain::TransceiverStationEvidenceSnapshot evidence;
    evidence.sourceFrequency = transceiver.frequency;
    evidence.latitudeDeg = transceiver.latitudeDeg;
    evidence.longitudeDeg = transceiver.longitudeDeg;
    evidence.heightAglFt = transceiver.heightAglFt;
    evidence.hasAircraftDistance = true;
    evidence.aircraftDistanceNm = GreatCircleDistanceNm(
        airportLatitudeDeg,
        airportLongitudeDeg,
        transceiver.latitudeDeg,
        transceiver.longitudeDeg);

    const auto controllerVisualRangeNm =
        controller.visualRangeNm > 0
            ? static_cast<double>(controller.visualRangeNm)
            : 0.0;
    evidence.receivableRangeNm = std::max(
        kMinReceivableRangeNm,
        std::max(
            controllerVisualRangeNm,
            RadioHorizonNm(
                kAirportCoverageAircraftAglFt,
                transceiver.heightAglFt)));
    evidence.hasReceivableRange = true;
    evidence.withinReceivableRange =
        evidence.aircraftDistanceNm <= evidence.receivableRangeNm;
    // Airport coverage has no separate max-candidate envelope. Keep this
    // field non-filtering so old radio-range diagnostics do not misread it.
    evidence.maxCandidateDistanceNm = evidence.receivableRangeNm;
    evidence.withinMaxCandidateDistance = true;
    evidence.score =
        evidence.receivableRangeNm - evidence.aircraftDistanceNm;
    evidence.transceiverFrequencyGuard =
        !transceiver.frequency.empty() && IsGuardFrequency(transceiver.frequency);
    return evidence;
}

void PopulateAirportCoverageEvidenceAndCompatibilityCandidates(
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const std::unordered_map<std::string, std::vector<CachedTransceiver>>& indexedTransceivers,
    bool emitCompatibilityCandidates,
    const std::string& pathUnavailableReason,
    double airportLatitudeDeg,
    double airportLongitudeDeg,
    brain::TransceiverResolutionSnapshot* snapshot) {
    if (snapshot == nullptr ||
        !controllerFeedSnapshot.available ||
        controllerFeedSnapshot.stale) {
        return;
    }

    snapshot->resolutionPath = kResolutionPathAirportCoverage;
    snapshot->candidatesCompatibilityOnly = true;
    snapshot->controllerEvidence.clear();
    if (emitCompatibilityCandidates) {
        snapshot->candidates.clear();
    }

    for (const auto& controller : controllerFeedSnapshot.Controllers()) {
        brain::TransceiverControllerEvidenceSnapshot evidence;
        evidence.callsign = controller.callsign;
        evidence.controllerFrequency = controller.frequency;
        evidence.facility = controller.facility;
        evidence.actionable = controller.actionable;
        evidence.atis = controller.atis;
        evidence.visualRangeNm = controller.visualRangeNm;
        evidence.controllerFrequencyGuard =
            !controller.frequency.empty() && IsGuardFrequency(controller.frequency);

        const auto transceiverEntry = indexedTransceivers.find(controller.callsign);
        evidence.hasTransceiverEntry =
            transceiverEntry != indexedTransceivers.end();
        evidence.matchingTransceiverCount =
            evidence.hasTransceiverEntry
                ? static_cast<int>(transceiverEntry->second.size())
                : 0;

        double bestScore = -1.0;
        double bestDistanceNm = 0.0;
        double bestLatitudeDeg = 0.0;
        double bestLongitudeDeg = 0.0;
        std::string bestTransceiverFrequency;
        std::size_t bestEvidenceIndex = std::numeric_limits<std::size_t>::max();

        if (evidence.hasTransceiverEntry) {
            evidence.stations.reserve(transceiverEntry->second.size());
            for (const auto& transceiver : transceiverEntry->second) {
                auto stationEvidence = BuildAirportCoverageStationEvidence(
                    controller,
                    transceiver,
                    airportLatitudeDeg,
                    airportLongitudeDeg);
                evidence.transceiverFrequencyGuard =
                    evidence.transceiverFrequencyGuard ||
                    stationEvidence.transceiverFrequencyGuard;

                if (stationEvidence.withinReceivableRange &&
                    stationEvidence.score > bestScore) {
                    bestScore = stationEvidence.score;
                    bestDistanceNm = stationEvidence.aircraftDistanceNm;
                    bestTransceiverFrequency = transceiver.frequency;
                    bestLatitudeDeg = transceiver.latitudeDeg;
                    bestLongitudeDeg = transceiver.longitudeDeg;
                    bestEvidenceIndex = evidence.stations.size();
                }

                evidence.stations.push_back(std::move(stationEvidence));
            }
        }

        if (bestEvidenceIndex < evidence.stations.size()) {
            evidence.stations[bestEvidenceIndex].bestByModuleScore = true;
            const auto displayResolution = ResolveDisplayFrequencyEvidence(
                controller.frequency,
                bestTransceiverFrequency);
            evidence.resolvedDisplayFrequency = displayResolution.frequency;
            evidence.displayFrequencySource = displayResolution.source;
            evidence.displayFrequencyUnavailableReason =
                displayResolution.unavailableReason;
            evidence.controllerFrequencyGuard =
                evidence.controllerFrequencyGuard ||
                displayResolution.controllerFrequencyGuard;
            if (!bestTransceiverFrequency.empty()) {
                evidence.transceiverFrequencyGuard =
                    evidence.transceiverFrequencyGuard ||
                    displayResolution.transceiverFrequencyGuard;
            }

            if (emitCompatibilityCandidates &&
                pathUnavailableReason.empty() &&
                controller.actionable &&
                !displayResolution.frequency.empty()) {
                brain::ReceivableControllerSnapshot candidate;
                candidate.callsign = controller.callsign;
                candidate.frequency = displayResolution.frequency;
                candidate.distanceNm = bestDistanceNm;
                candidate.score = bestScore;
                candidate.latitudeDeg = bestLatitudeDeg;
                candidate.longitudeDeg = bestLongitudeDeg;
                snapshot->candidates.push_back(std::move(candidate));
            }
        } else if (!evidence.hasTransceiverEntry) {
            evidence.displayFrequencySource = "none";
            evidence.displayFrequencyUnavailableReason =
                "missing-transceiver";
        } else {
            evidence.displayFrequencySource = "none";
            evidence.displayFrequencyUnavailableReason =
                "no-airport-covering-station";
        }

        if (!pathUnavailableReason.empty()) {
            evidence.pathUnavailableReason = pathUnavailableReason;
        } else if (!evidence.actionable) {
            evidence.pathUnavailableReason = "non-actionable";
        } else if (!evidence.hasTransceiverEntry) {
            evidence.pathUnavailableReason = "missing-transceiver";
        } else if (bestEvidenceIndex >= evidence.stations.size()) {
            evidence.pathUnavailableReason = "no-airport-covering-station";
        } else if (evidence.resolvedDisplayFrequency.empty()) {
            evidence.pathUnavailableReason =
                PathReasonFromDisplayUnavailableReason(
                    evidence.displayFrequencyUnavailableReason);
        }

        snapshot->controllerEvidence.push_back(std::move(evidence));
    }

    const auto sourceControllerCount =
        static_cast<int>(controllerFeedSnapshot.Controllers().size());
    const auto evidenceControllerCount =
        static_cast<int>(snapshot->controllerEvidence.size());
    snapshot->droppedBeforeBrainControllers =
        std::max(0, sourceControllerCount - evidenceControllerCount);
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
    snapshot.candidatesCompatibilityOnly = true;
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
        brain::TransceiverControllerEvidenceSnapshot controllerEvidence;
        controllerEvidence.callsign = controller.callsign;
        controllerEvidence.controllerFrequency = controller.frequency;
        controllerEvidence.facility = controller.facility;
        controllerEvidence.actionable = controller.actionable;
        controllerEvidence.atis = controller.atis;
        controllerEvidence.visualRangeNm = controller.visualRangeNm;
        controllerEvidence.controllerFrequencyGuard =
            !controller.frequency.empty() && IsGuardFrequency(controller.frequency);

        const auto transceiverEntry = indexedTransceivers.find(controller.callsign);
        controllerEvidence.hasTransceiverEntry =
            transceiverEntry != indexedTransceivers.end();
        controllerEvidence.matchingTransceiverCount =
            controllerEvidence.hasTransceiverEntry
                ? static_cast<int>(transceiverEntry->second.size())
                : 0;

        double bestScore = -1.0;
        std::string bestTransceiverFrequency;
        bool rejectedByDistanceEnvelope = false;
        std::size_t bestEvidenceIndex = std::numeric_limits<std::size_t>::max();

        if (controllerEvidence.hasTransceiverEntry) {
            controllerEvidence.stations.reserve(transceiverEntry->second.size());
            for (const auto& transceiver : transceiverEntry->second) {
                auto stationEvidence = BuildStationEvidence(
                    aircraftState,
                    controller,
                    transceiver,
                    snapshot.maxCandidateDistanceNm);
                controllerEvidence.transceiverFrequencyGuard =
                    controllerEvidence.transceiverFrequencyGuard ||
                    stationEvidence.transceiverFrequencyGuard;
                if (!stationEvidence.withinMaxCandidateDistance) {
                    rejectedByDistanceEnvelope = true;
                    controllerEvidence.stations.push_back(
                        std::move(stationEvidence));
                    continue;
                }

                if (!stationEvidence.withinReceivableRange) {
                    controllerEvidence.stations.push_back(
                        std::move(stationEvidence));
                    continue;
                }

                const auto score = stationEvidence.score;
                if (score > bestScore) {
                    bestScore = score;
                    bestTransceiverFrequency = transceiver.frequency;
                    bestEvidenceIndex = controllerEvidence.stations.size();
                }
                controllerEvidence.stations.push_back(std::move(stationEvidence));
            }
        }

        if (bestEvidenceIndex < controllerEvidence.stations.size()) {
            controllerEvidence.stations[bestEvidenceIndex].bestByModuleScore = true;
        }

        if (bestScore >= 0.0) {
            const auto displayResolution = ResolveDisplayFrequencyEvidence(
                controller.frequency,
                bestTransceiverFrequency);
            controllerEvidence.resolvedDisplayFrequency =
                displayResolution.frequency;
            controllerEvidence.displayFrequencySource = displayResolution.source;
            controllerEvidence.displayFrequencyUnavailableReason =
                displayResolution.unavailableReason;
            controllerEvidence.controllerFrequencyGuard =
                displayResolution.controllerFrequencyGuard;
            if (!bestTransceiverFrequency.empty()) {
                controllerEvidence.transceiverFrequencyGuard =
                    controllerEvidence.transceiverFrequencyGuard ||
                    displayResolution.transceiverFrequencyGuard;
            }
        } else if (!controllerEvidence.hasTransceiverEntry) {
            controllerEvidence.displayFrequencySource = "none";
            controllerEvidence.displayFrequencyUnavailableReason =
                "missing-transceiver";
        } else {
            controllerEvidence.displayFrequencySource = "none";
            controllerEvidence.displayFrequencyUnavailableReason =
                rejectedByDistanceEnvelope
                    ? "over-max-or-no-receivable-transceiver"
                    : "no-receivable-transceiver";
        }

        snapshot.controllerEvidence.push_back(controllerEvidence);
    }

    const auto sourceControllerCount =
        static_cast<int>(controllerFeedSnapshot.Controllers().size());
    const auto evidenceControllerCount =
        static_cast<int>(snapshot.controllerEvidence.size());
    snapshot.droppedBeforeBrainControllers =
        std::max(0, sourceControllerCount - evidenceControllerCount);
    PopulateNormalResolveCompatibilityCandidates(&snapshot);

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
    snapshot.resolutionPath = kResolutionPathAuthorityStations;
    snapshot.candidatesCompatibilityOnly = true;
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

    PopulateAuthorityStationsEvidenceAndCompatibilityCandidates(
        controllerFeedSnapshot,
        indexedTransceivers,
        true,
        {},
        &snapshot);

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
    snapshot.resolutionPath = kResolutionPathAirportCoverage;
    snapshot.candidatesCompatibilityOnly = true;
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

    PopulateAirportCoverageEvidenceAndCompatibilityCandidates(
        controllerFeedSnapshot,
        indexedTransceivers,
        true,
        {},
        airportLatitudeDeg,
        airportLongitudeDeg,
        &snapshot);

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
    cachedParserCounters_ = {};
    pendingParserCounters_ = {};
    hasFeedCache_ = false;
    hasPendingFeed_ = false;
    lastFetchSucceeded_ = false;
    lastRefreshAttemptedFetch_ = false;
    lastRefreshFailureReason_.clear();
    lastFetchTickSeconds_ = 0;
    lastSuccessfulFetchTickSeconds_ = 0;
    hasResolveCache_ = false;
    cachedSnapshot_ = {};
    lastResolveUsedHoldover_ = false;
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

brain::TransceiverSourceEvidenceSnapshot
TransceiverResolver::BuildSourceEvidence(
    long long nowSeconds,
    bool refreshSucceeded,
    bool holdoverUsed,
    bool holdoverExpired) const {
    brain::TransceiverSourceEvidenceSnapshot evidence;
    evidence.feedCacheExists = hasFeedCache_;
    evidence.cachedTransceiverCount =
        static_cast<int>(cachedTransceivers_.size());
    evidence.cacheFresh = IsFeedCacheFresh(nowSeconds);
    evidence.cacheStale = !evidence.cacheFresh;
    evidence.holdoverUsed = holdoverUsed;
    evidence.holdoverExpired = holdoverExpired;
    evidence.hasFeedAgeSeconds = lastSuccessfulFetchTickSeconds_ != 0;
    evidence.feedAgeSeconds =
        evidence.hasFeedAgeSeconds ? FeedCacheAgeSeconds(nowSeconds) : 0;
    evidence.fetchAttempted = lastRefreshAttemptedFetch_;
    evidence.fetchInProgress = fetchInProgress_.load();
    evidence.fetchFailed =
        !refreshSucceeded && !evidence.fetchInProgress &&
        !lastRefreshFailureReason_.empty();
    evidence.failureReason = lastRefreshFailureReason_;
    evidence.parser = cachedParserCounters_;
    return evidence;
}

brain::TransceiverResolutionSnapshot TransceiverResolver::Resolve(
    const brain::AircraftStateSnapshot& aircraftState,
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot) {
    const auto refreshSucceeded = RefreshFeedIfNeeded();
    const auto nowSeconds = CurrentTickSeconds();
    const auto useHoldover =
        !refreshSucceeded && IsFeedCacheUsableAsHoldover(nowSeconds);

    if (cachedTransceivers_.empty()) {
        brain::TransceiverResolutionSnapshot snapshot;
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.candidatesCompatibilityOnly = true;
        snapshot.statusLine = "RX feed unavailable";
        snapshot.sourceEvidence = BuildSourceEvidence(
            nowSeconds,
            refreshSucceeded,
            false,
            false);
        PopulateControllerFeedSourceEvidence(
            controllerFeedSnapshot,
            &snapshot.sourceEvidence);
        return snapshot;
    }

    if (!refreshSucceeded && !useHoldover) {
        brain::TransceiverResolutionSnapshot snapshot;
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.candidatesCompatibilityOnly = true;
        snapshot.statusLine = "RX feed holdover expired";
        snapshot.sourceEvidence = BuildSourceEvidence(
            nowSeconds,
            refreshSucceeded,
            false,
            true);
        PopulateControllerFeedSourceEvidence(
            controllerFeedSnapshot,
            &snapshot.sourceEvidence);
        return snapshot;
    }

    const auto controllerFeedHash = BuildControllerFeedHash(controllerFeedSnapshot);
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
    const auto holdoverStateChanged =
        !hasResolveCache_ || lastResolveUsedHoldover_ != useHoldover;

    if (!feedChanged && !cadenceExpired && !movedEnough && !holdoverStateChanged) {
        auto snapshot = cachedSnapshot_;
        snapshot.stale = false;
        snapshot.sourceEvidence = BuildSourceEvidence(
            nowSeconds,
            refreshSucceeded,
            useHoldover,
            false);
        PopulateControllerFeedSourceEvidence(
            controllerFeedSnapshot,
            &snapshot.sourceEvidence);
        return snapshot;
    }

    auto snapshot = ResolveReceivableControllers(
        aircraftState,
        controllerFeedSnapshot,
        indexedTransceivers_,
        false);
    if (useHoldover) {
        snapshot.statusLine +=
            " holdover=1 feedAgeSeconds=" +
            std::to_string(FeedCacheAgeSeconds(nowSeconds));
    }
    snapshot.sourceEvidence = BuildSourceEvidence(
        nowSeconds,
        refreshSucceeded,
        useHoldover,
        false);
    PopulateControllerFeedSourceEvidence(
        controllerFeedSnapshot,
        &snapshot.sourceEvidence);
    cachedSnapshot_ = snapshot;
    lastResolveUsedHoldover_ = useHoldover;
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
        snapshot.resolutionPath = kResolutionPathAuthorityStations;
        snapshot.candidatesCompatibilityOnly = true;
        snapshot.statusLine = "AUTHORITY stations feed unavailable";
        PopulateAuthorityStationsEvidenceAndCompatibilityCandidates(
            controllerFeedSnapshot,
            indexedTransceivers_,
            false,
            "transceiver-feed-unavailable",
            &snapshot);
        snapshot.sourceEvidence = BuildSourceEvidence(
            CurrentTickSeconds(),
            refreshSucceeded,
            false,
            false);
        PopulateControllerFeedSourceEvidence(
            controllerFeedSnapshot,
            &snapshot.sourceEvidence);
        return snapshot;
    }

    if (!refreshSucceeded) {
        brain::TransceiverResolutionSnapshot snapshot;
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.resolutionPath = kResolutionPathAuthorityStations;
        snapshot.candidatesCompatibilityOnly = true;
        snapshot.statusLine = "AUTHORITY stations feed stale";
        PopulateAuthorityStationsEvidenceAndCompatibilityCandidates(
            controllerFeedSnapshot,
            indexedTransceivers_,
            false,
            "transceiver-feed-stale",
            &snapshot);
        snapshot.sourceEvidence = BuildSourceEvidence(
            CurrentTickSeconds(),
            refreshSucceeded,
            false,
            true);
        PopulateControllerFeedSourceEvidence(
            controllerFeedSnapshot,
            &snapshot.sourceEvidence);
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
        snapshot.sourceEvidence = BuildSourceEvidence(
            nowSeconds,
            refreshSucceeded,
            false,
            false);
        PopulateControllerFeedSourceEvidence(
            controllerFeedSnapshot,
            &snapshot.sourceEvidence);
        return snapshot;
    }

    auto snapshot = xvatsim::modules::transceiver_resolver::ResolveAuthorityStations(
        controllerFeedSnapshot,
        indexedTransceivers_,
        false);
    snapshot.sourceEvidence = BuildSourceEvidence(
        nowSeconds,
        refreshSucceeded,
        false,
        false);
    PopulateControllerFeedSourceEvidence(
        controllerFeedSnapshot,
        &snapshot.sourceEvidence);
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
        snapshot.resolutionPath = kResolutionPathAirportCoverage;
        snapshot.candidatesCompatibilityOnly = true;
        snapshot.statusLine = "AIRSPACE waiting for airport";
        PopulateAirportCoverageEvidenceAndCompatibilityCandidates(
            controllerFeedSnapshot,
            indexedTransceivers_,
            false,
            "airport-coordinates-unavailable",
            airportLatitudeDeg,
            airportLongitudeDeg,
            &snapshot);
        snapshot.sourceEvidence = BuildSourceEvidence(
            CurrentTickSeconds(),
            refreshSucceeded,
            false,
            false);
        PopulateControllerFeedSourceEvidence(
            controllerFeedSnapshot,
            &snapshot.sourceEvidence);
        return snapshot;
    }

    if (cachedTransceivers_.empty()) {
        brain::TransceiverResolutionSnapshot snapshot;
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.resolutionPath = kResolutionPathAirportCoverage;
        snapshot.candidatesCompatibilityOnly = true;
        snapshot.statusLine = "AIRSPACE feed unavailable";
        PopulateAirportCoverageEvidenceAndCompatibilityCandidates(
            controllerFeedSnapshot,
            indexedTransceivers_,
            false,
            "transceiver-feed-unavailable",
            airportLatitudeDeg,
            airportLongitudeDeg,
            &snapshot);
        snapshot.sourceEvidence = BuildSourceEvidence(
            CurrentTickSeconds(),
            refreshSucceeded,
            false,
            false);
        PopulateControllerFeedSourceEvidence(
            controllerFeedSnapshot,
            &snapshot.sourceEvidence);
        return snapshot;
    }

    if (!refreshSucceeded) {
        brain::TransceiverResolutionSnapshot snapshot;
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.resolutionPath = kResolutionPathAirportCoverage;
        snapshot.candidatesCompatibilityOnly = true;
        snapshot.statusLine = "AIRSPACE feed stale";
        PopulateAirportCoverageEvidenceAndCompatibilityCandidates(
            controllerFeedSnapshot,
            indexedTransceivers_,
            false,
            "transceiver-feed-stale",
            airportLatitudeDeg,
            airportLongitudeDeg,
            &snapshot);
        snapshot.sourceEvidence = BuildSourceEvidence(
            CurrentTickSeconds(),
            refreshSucceeded,
            false,
            true);
        PopulateControllerFeedSourceEvidence(
            controllerFeedSnapshot,
            &snapshot.sourceEvidence);
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
        snapshot.sourceEvidence = BuildSourceEvidence(
            nowSeconds,
            refreshSucceeded,
            false,
            false);
        PopulateControllerFeedSourceEvidence(
            controllerFeedSnapshot,
            &snapshot.sourceEvidence);
        return snapshot;
    }

    auto snapshot = ResolveAirportCoveredControllers(
        controllerFeedSnapshot,
        indexedTransceivers_,
        false,
        airportLatitudeDeg,
        airportLongitudeDeg);
    snapshot.sourceEvidence = BuildSourceEvidence(
        nowSeconds,
        refreshSucceeded,
        false,
        false);
    PopulateControllerFeedSourceEvidence(
        controllerFeedSnapshot,
        &snapshot.sourceEvidence);
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
    lastRefreshAttemptedFetch_ = false;
    lastRefreshFailureReason_.clear();
    HarvestPendingFetch();

    const auto fetchHung =
        fetchInProgress_.load() &&
        lastFetchTickSeconds_ != 0 &&
        (nowSeconds - lastFetchTickSeconds_) >= kHungFetchStaleSeconds;
    if (fetchHung) {
        hasResolveCache_ = false;
        hasAirportCoverageCache_ = false;
        lastRefreshFailureReason_ = "fetch-hung";
        return false;
    }

    const auto cadenceSeconds =
        lastFetchSucceeded_ ? kRefreshCadenceSeconds : kFailureBackoffSeconds;
    if (hasFeedCache_ && (nowSeconds - lastFetchTickSeconds_) < cadenceSeconds) {
        const auto fresh = IsFeedCacheFresh(nowSeconds);
        if (!fresh) {
            lastRefreshFailureReason_ = "cache-stale";
        }
        return fresh;
    }

    if (!hasFeedCache_ &&
        lastFetchTickSeconds_ != 0 &&
        (nowSeconds - lastFetchTickSeconds_) < cadenceSeconds) {
        lastRefreshFailureReason_ = "fetch-backoff";
        return false;
    }

    lastRefreshAttemptedFetch_ = true;
    if (!fetchInProgress_.load() && !StartAsyncFetch(nowSeconds)) {
        lastFetchSucceeded_ = false;
        hasResolveCache_ = false;
        hasAirportCoverageCache_ = false;
        lastRefreshFailureReason_ = "fetch-start-failed";
        return false;
    }

    const auto fresh = IsFeedCacheFresh(nowSeconds);
    if (!fresh && lastRefreshFailureReason_.empty()) {
        lastRefreshFailureReason_ =
            fetchInProgress_.load() ? "fetch-in-progress" : "cache-stale";
    }
    return fresh;
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
        pendingParserCounters_ = {};
        hasPendingFeed_ = false;
    }

    fetchInProgress_.store(true);
    try {
        fetchThread_ = std::thread([this]() {
            std::vector<CachedTransceiver> parsedTransceivers;
            brain::TransceiverParserHygieneCounters parserCounters;
            try {
                const auto payload = DownloadJsonDocument();
                parsedTransceivers = ParseTransceivers(payload, &parserCounters);
            } catch (...) {
                parsedTransceivers.clear();
                ++parserCounters.parseException;
            }

            try {
                std::lock_guard<std::mutex> lock(fetchMutex_);
                pendingTransceivers_ = std::move(parsedTransceivers);
                pendingParserCounters_ = parserCounters;
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

bool TransceiverResolver::IsFeedCacheUsableAsHoldover(long long nowSeconds) const {
    return hasFeedCache_ &&
           !cachedTransceivers_.empty() &&
           lastSuccessfulFetchTickSeconds_ != 0 &&
           (nowSeconds - lastSuccessfulFetchTickSeconds_) <= kFeedHoldoverSeconds;
}

long long TransceiverResolver::FeedCacheAgeSeconds(long long nowSeconds) const {
    if (lastSuccessfulFetchTickSeconds_ == 0 ||
        nowSeconds < lastSuccessfulFetchTickSeconds_) {
        return 0;
    }
    return nowSeconds - lastSuccessfulFetchTickSeconds_;
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
        cachedParserCounters_ = pendingParserCounters_;
        pendingParserCounters_ = {};
        lastFetchSucceeded_ = false;
        lastRefreshFailureReason_ = "empty-parsed-feed";
        hasResolveCache_ = false;
        hasAirportCoverageCache_ = false;
        return;
    }

    cachedTransceivers_ = pendingTransceivers_;
    indexedTransceivers_ = IndexTransceiversByCallsign(cachedTransceivers_);
    pendingTransceivers_.clear();
    cachedParserCounters_ = pendingParserCounters_;
    pendingParserCounters_ = {};
    hasFeedCache_ = true;
    lastFetchSucceeded_ = true;
    lastSuccessfulFetchTickSeconds_ = CurrentTickSeconds();
    hasResolveCache_ = false;
    hasAirportCoverageCache_ = false;
}

void TransceiverResolver::SeedFeedCacheForTesting(
    std::vector<CachedTransceiver> transceivers,
    long long successfulFetchAgeSeconds,
    bool lastFetchSucceeded) {
    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }

    const auto nowSeconds = CurrentTickSeconds();
    const auto cacheAgeSeconds = std::max<long long>(0, successfulFetchAgeSeconds);
    std::lock_guard<std::mutex> lock(fetchMutex_);
    cachedTransceivers_ = std::move(transceivers);
    indexedTransceivers_ = IndexTransceiversByCallsign(cachedTransceivers_);
    pendingTransceivers_.clear();
    cachedParserCounters_ = {};
    pendingParserCounters_ = {};
    hasFeedCache_ = !cachedTransceivers_.empty();
    hasPendingFeed_ = false;
    lastFetchSucceeded_ = lastFetchSucceeded && hasFeedCache_;
    lastRefreshAttemptedFetch_ = false;
    lastRefreshFailureReason_.clear();
    lastFetchTickSeconds_ = nowSeconds;
    lastSuccessfulFetchTickSeconds_ =
        hasFeedCache_ ? std::max<long long>(1, nowSeconds - cacheAgeSeconds) : 0;
    hasResolveCache_ = false;
    cachedSnapshot_ = {};
    lastResolveUsedHoldover_ = false;
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
    fetchInProgress_.store(false);
}

}  // namespace xvatsim::modules::transceiver_resolver
