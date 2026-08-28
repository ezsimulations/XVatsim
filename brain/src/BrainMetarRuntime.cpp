#include "XVatsim/brain/BrainMetarRuntime.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace xvatsim::brain {

namespace {

constexpr double kKilometersToStatuteMiles = 0.621371192237334;
constexpr std::int64_t kFutureToleranceSeconds = 5 * 60;

std::string TrimOuter(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string Upper(std::string value) {
    for (auto& character : value) {
        character = static_cast<char>(std::toupper(
            static_cast<unsigned char>(character)));
    }
    return value;
}

std::vector<std::string> TokenizeBeforeRemarks(const std::string& raw) {
    std::istringstream stream(raw);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token) {
        token = Upper(token);
        if (token == "RMK") break;
        tokens.push_back(std::move(token));
    }
    return tokens;
}

bool AllDigits(const std::string& value) {
    return !value.empty() && std::all_of(
        value.begin(), value.end(), [](char character) {
            return std::isdigit(static_cast<unsigned char>(character)) != 0;
        });
}

bool ParseFraction(const std::string& token, double* value) {
    if (value == nullptr) return false;
    const auto slash = token.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 >= token.size()) {
        return false;
    }
    const auto numerator = token.substr(0, slash);
    const auto denominator = token.substr(slash + 1);
    if (!AllDigits(numerator) || !AllDigits(denominator)) return false;
    const auto denominatorValue = std::stoi(denominator);
    if (denominatorValue <= 0) return false;
    *value = static_cast<double>(std::stoi(numerator)) /
        static_cast<double>(denominatorValue);
    return std::isfinite(*value);
}

bool ParseNumber(const std::string& token, double* value) {
    if (value == nullptr || token.empty()) return false;
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stod(token, &consumed);
        if (consumed != token.size() || !std::isfinite(parsed)) return false;
        *value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseSmVisibility(
    const std::vector<std::string>& tokens,
    std::size_t index,
    double* visibility,
    bool* consumedPreviousWhole) {
    if (visibility == nullptr || consumedPreviousWhole == nullptr ||
        index >= tokens.size()) return false;
    *consumedPreviousWhole = false;
    auto token = tokens[index];
    if (token.size() <= 2 || token.substr(token.size() - 2) != "SM") {
        return false;
    }
    token.resize(token.size() - 2);
    if (token.empty()) return false;
    if (token.front() == 'P') {
        token.erase(token.begin());
        double value = 0.0;
        if (!ParseNumber(token, &value)) return false;
        *visibility = value + 0.001;
        return true;
    }
    if (token.front() == 'M') {
        token.erase(token.begin());
        double value = 0.0;
        if (!ParseFraction(token, &value) && !ParseNumber(token, &value)) {
            return false;
        }
        *visibility = std::max(0.0, value - 0.001);
        return true;
    }
    double value = 0.0;
    if (ParseFraction(token, &value)) {
        if (index > 0 && AllDigits(tokens[index - 1])) {
            value += std::stod(tokens[index - 1]);
            *consumedPreviousWhole = true;
        }
        *visibility = value;
        return true;
    }
    if (!ParseNumber(token, &value)) return false;
    *visibility = value;
    return true;
}

int VisibilityCategory(double visibilitySm) {
    if (visibilitySm < 1.0) return 3;
    if (visibilitySm < 3.0) return 2;
    if (visibilitySm <= 5.0) return 1;
    return 0;
}

int CeilingCategory(int ceilingFeet, bool noCeiling) {
    if (noCeiling || ceilingFeet > 3000) return 0;
    if (ceilingFeet >= 1000) return 1;
    if (ceilingFeet >= 500) return 2;
    return 3;
}

BrainMetarFlightCategory CategoryFromRank(int rank) {
    switch (rank) {
        case 0: return BrainMetarFlightCategory::Vfr;
        case 1: return BrainMetarFlightCategory::Mvfr;
        case 2: return BrainMetarFlightCategory::Ifr;
        case 3: return BrainMetarFlightCategory::Lifr;
        default: return BrainMetarFlightCategory::Unknown;
    }
}

bool ParseObservationToken(
    const std::string& token,
    int* day,
    int* hour,
    int* minute) {
    if (token.size() != 7 || token.back() != 'Z' ||
        !AllDigits(token.substr(0, 6))) return false;
    const auto parsedDay = std::stoi(token.substr(0, 2));
    const auto parsedHour = std::stoi(token.substr(2, 2));
    const auto parsedMinute = std::stoi(token.substr(4, 2));
    if (parsedDay < 1 || parsedDay > 31 || parsedHour > 23 ||
        parsedMinute > 59) return false;
    if (day != nullptr) *day = parsedDay;
    if (hour != nullptr) *hour = parsedHour;
    if (minute != nullptr) *minute = parsedMinute;
    return true;
}

bool ResolveObservationUnix(
    int day,
    int hour,
    int minute,
    std::int64_t reference,
    std::int64_t* resolved) {
    if (resolved == nullptr) return false;
    std::time_t referenceTime = static_cast<std::time_t>(reference);
    std::tm referenceTm{};
    if (gmtime_s(&referenceTm, &referenceTime) != 0) return false;
    std::int64_t best = 0;
    std::int64_t bestDistance = std::numeric_limits<std::int64_t>::max();
    bool found = false;
    for (int monthOffset = -1; monthOffset <= 1; ++monthOffset) {
        std::tm candidate{};
        candidate.tm_year = referenceTm.tm_year;
        candidate.tm_mon = referenceTm.tm_mon + monthOffset;
        candidate.tm_mday = day;
        candidate.tm_hour = hour;
        candidate.tm_min = minute;
        candidate.tm_sec = 0;
        const auto candidateTime = _mkgmtime64(&candidate);
        if (candidateTime < 0 || candidate.tm_mday != day ||
            candidate.tm_hour != hour || candidate.tm_min != minute) continue;
        const auto candidateSeconds = static_cast<std::int64_t>(candidateTime);
        const auto distance = std::llabs(reference - candidateSeconds);
        if (!found || distance < bestDistance) {
            best = candidateSeconds;
            bestDistance = distance;
            found = true;
        }
    }
    if (!found || best - reference > kFutureToleranceSeconds) return false;
    *resolved = best;
    return true;
}

void InvalidateAccessoryProjection(BrainOwnedRuntimeState* state) {
    if (state == nullptr) return;
    state->accessory.cachedPresentationSnapshot.reset();
    if (state->accessory.activeDrawer == BrainOwnedAccessoryDrawerId::Metar) {
        state->accessory.cachedPreparationSnapshots[0].reset();
    }
}

void TouchPresentation(BrainOwnedRuntimeState* state) {
    if (state == nullptr) return;
    ++state->metar.presentationGeneration;
    InvalidateAccessoryProjection(state);
}

bool PresentationOwned(const BrainOwnedRuntimeState& state) {
    return state.metar.lookupPresentationOwnershipValid &&
        state.accessory.activeDrawer == BrainOwnedAccessoryDrawerId::Metar &&
        state.accessory.selectionGeneration ==
            state.metar.lookupPresentationSelectionGeneration;
}

void DropLostPresentationOwnership(BrainOwnedRuntimeState* state) {
    if (state == nullptr || !state->metar.lookupPresentationOwnershipValid) return;
    if (PresentationOwned(*state)) return;
    state->metar.lookupPresentationOwnershipValid = false;
    if (state->metar.transientPresentation !=
        BrainMetarTransientPresentation::None) {
        state->metar.transientPresentation =
            BrainMetarTransientPresentation::None;
        state->metar.transientDeadlineMonotonicMs = 0;
        TouchPresentation(state);
    }
}

void SelectMetarDrawerForLookup(BrainOwnedRuntimeState* state) {
    if (state == nullptr) return;
    if (state->accessory.activeDrawer != BrainOwnedAccessoryDrawerId::Metar) {
        state->accessory.activeDrawer = BrainOwnedAccessoryDrawerId::Metar;
        ++state->accessory.selectionGeneration;
        ++state->accessory.scrollResetGeneration;
    }
    state->metar.lookupPresentationSelectionGeneration =
        state->accessory.selectionGeneration;
    state->metar.lookupPresentationOwnershipValid = true;
}

std::string ResolveBrainOwnedMetarPrimaryTarget(
    BrainOwnedMetarRuntimeState* metar,
    const BrainOwnedAsyncFactCycleInput& input,
    bool* fromVfr) {
    if (fromVfr != nullptr) *fromVfr = false;
    if (metar == nullptr) return {};
    if (input.operatingMode == BrainOwnedOperatingMode::VFR) {
        if (metar->targetContextInitialized && metar->primaryLatchedFromVfr &&
            !metar->primaryAirportIcao.empty()) {
            if (fromVfr != nullptr) *fromVfr = true;
            return metar->primaryAirportIcao;
        }
        std::string candidate;
        const bool permittedSource =
            input.flightPlan.departureSource == AirportSource::OnboardFms ||
            input.flightPlan.departureSource == AirportSource::CurrentLocation;
        if (input.flightPlan.available && permittedSource &&
            input.flightPlan.hasDepartureCoordinates &&
            std::isfinite(input.flightPlan.departureLatDeg) &&
            std::isfinite(input.flightPlan.departureLonDeg) &&
            input.flightPlan.departureLatDeg >= -90.0 &&
            input.flightPlan.departureLatDeg <= 90.0 &&
            input.flightPlan.departureLonDeg >= -180.0 &&
            input.flightPlan.departureLonDeg <= 180.0 &&
            NormalizeStrictMetarIcao(input.flightPlan.departureIcao, &candidate)) {
            if (fromVfr != nullptr) *fromVfr = true;
            return candidate;
        }
        return {};
    }
    if (!input.flightContext.active) return {};
    std::string candidate;
    const auto raw = input.workflowStage == WorkflowStage::Enroute ||
            input.workflowStage == WorkflowStage::Arrival
        ? input.flightContext.destinationIcao
        : input.flightContext.departureIcao;
    return NormalizeStrictMetarIcao(raw, &candidate) ? candidate : std::string{};
}

std::string BuildTargetContextKey(
    const BrainOwnedAsyncFactCycleInput& input) {
    std::ostringstream key;
    key << static_cast<int>(input.operatingMode) << '|'
        << static_cast<int>(input.workflowStage) << '|'
        << (input.flightContext.active ? '1' : '0') << '|'
        << input.flightContext.departureIcao << '|'
        << input.flightContext.destinationIcao;
    if (input.operatingMode == BrainOwnedOperatingMode::VFR) {
        key << '|' << (input.flightPlan.available ? '1' : '0')
            << '|' << static_cast<int>(input.flightPlan.departureSource)
            << '|' << (input.flightPlan.hasDepartureCoordinates ? '1' : '0')
            << '|' << input.flightPlan.departureIcao;
    }
    return key.str();
}

void EndSpotlightForPrimaryChange(BrainOwnedRuntimeState* state) {
    if (state == nullptr ||
        state->metar.transientPresentation !=
            BrainMetarTransientPresentation::LookupSpotlight) return;
    state->metar.transientPresentation = BrainMetarTransientPresentation::None;
    state->metar.transientDeadlineMonotonicMs = 0;
    state->metar.lookupPresentationOwnershipValid = false;
}

void SetPrimaryTarget(
    BrainOwnedRuntimeState* state,
    const std::string& target,
    bool fromVfr,
    long long nowMs) {
    if (state == nullptr) return;
    if (state->metar.primaryAirportIcao == target) {
        if (fromVfr && !target.empty()) {
            state->metar.primaryLatchedFromVfr = true;
        }
        return;
    }
    EndSpotlightForPrimaryChange(state);
    state->metar.primaryAirportIcao = target;
    state->metar.primaryLatchedFromVfr = fromVfr && !target.empty();
    ++state->metar.primaryGeneration;
    state->metar.primaryObservation = {};
    state->metar.primaryContentFingerprint = 0;
    state->metar.freshUntilMonotonicMs = 0;
    state->metar.nextPrimaryEligibleMonotonicMs = nowMs;
    state->metar.consecutiveFailures = 0;
    state->metar.sourceHealth = target.empty()
        ? BrainMetarSourceHealth::Unknown
        : BrainMetarSourceHealth::Pending;
    state->metar.visibleState = target.empty()
        ? BrainMetarVisibleState::Unavailable
        : BrainMetarVisibleState::Pending;
    ++state->metar.sourceHealthGeneration;
    ++state->metar.freshnessGeneration;
    TouchPresentation(state);
}

long long FailureBackoffMs(int failures) {
    if (failures <= 1) return kBrainMetarFirstFailureBackoffMs;
    if (failures == 2) return kBrainMetarSecondFailureBackoffMs;
    return kBrainMetarMaximumFailureBackoffMs;
}

std::string HistoryTitle(
    const BrainMetarParsedObservation& observation,
    BrainMetarRequestPurpose purpose) {
    std::ostringstream stream;
    stream << observation.stationIcao << " · "
           << (purpose == BrainMetarRequestPurpose::PilotLookup
                   ? "PILOT REQUEST"
                   : "AUTOMATIC TARGET")
           << " · " << std::setfill('0') << std::setw(2)
           << observation.observationHour << std::setw(2)
           << observation.observationMinute << 'Z';
    return stream.str();
}

bool PublishHistory(
    BrainOwnedRuntimeState* state,
    const BrainMetarParsedObservation& observation,
    BrainMetarRequestPurpose purpose) {
    BrainOwnedAccessoryHistoryEntryInput entry;
    entry.drawer = BrainOwnedAccessoryDrawerId::Metar;
    entry.stableKey = "METAR|" + observation.stationIcao + "|" +
        std::to_string(observation.observationUnixSeconds);
    entry.title = HistoryTitle(observation, purpose);
    entry.body = observation.rawMetar;
    entry.chronological = true;
    entry.chronologyKey = observation.observationUnixSeconds;
    const auto decision = AcceptBrainOwnedAccessoryHistoryEntry(state, entry);
    if (decision.accepted) {
        ++state->metar.historyMutationCount;
        return true;
    }
    return false;
}

void RecordPrimaryFailure(
    BrainOwnedRuntimeState* state,
    long long nowMs,
    bool* presentationChanged) {
    if (state == nullptr) return;
    const auto oldVisible = state->metar.visibleState;
    const auto oldHealth = state->metar.sourceHealth;
    state->metar.sourceHealth = BrainMetarSourceHealth::Failed;
    state->metar.lastFailureMonotonicMs = nowMs;
    ++state->metar.consecutiveFailures;
    state->metar.nextPrimaryEligibleMonotonicMs =
        nowMs + FailureBackoffMs(state->metar.consecutiveFailures);
    if (!state->metar.primaryObservation.valid) {
        state->metar.visibleState = BrainMetarVisibleState::Unavailable;
    } else if (nowMs >= state->metar.freshUntilMonotonicMs) {
        state->metar.visibleState = BrainMetarVisibleState::Stale;
    } else {
        state->metar.visibleState = BrainMetarVisibleState::Cached;
    }
    ++state->metar.sourceHealthGeneration;
    if (oldVisible != state->metar.visibleState) {
        ++state->metar.freshnessGeneration;
        EndSpotlightForPrimaryChange(state);
        TouchPresentation(state);
        if (presentationChanged != nullptr) *presentationChanged = true;
    } else if (oldHealth != state->metar.sourceHealth &&
               state->metar.visibleState == BrainMetarVisibleState::Cached) {
        TouchPresentation(state);
        if (presentationChanged != nullptr) *presentationChanged = true;
    }
}

bool RequestMatchesCurrent(
    const BrainOwnedMetarRuntimeState& metar,
    const BrainMetarWorkerFact& fact) {
    if (fact.request.purpose == BrainMetarRequestPurpose::PilotLookup) {
        return !metar.lookupInvalidated &&
            fact.request.lookupGeneration == metar.lookupGeneration &&
            fact.request.airportIcao == metar.pendingLookupIcao;
    }
    return fact.request.primaryGeneration == metar.primaryGeneration &&
        fact.request.airportIcao == metar.primaryAirportIcao;
}

void HandleLookupFailure(
    BrainOwnedRuntimeState* state,
    long long nowMs,
    bool* presentationChanged) {
    if (state == nullptr) return;
    state->metar.lookupAwaitingDispatch = false;
    if (PresentationOwned(*state)) {
        state->metar.transientPresentation =
            BrainMetarTransientPresentation::LookupFailure;
        state->metar.transientDeadlineMonotonicMs =
            nowMs + kBrainMetarLookupFailureMs;
        TouchPresentation(state);
        if (presentationChanged != nullptr) *presentationChanged = true;
    } else {
        state->metar.lookupPresentationOwnershipValid = false;
        state->metar.transientPresentation =
            BrainMetarTransientPresentation::None;
        state->metar.transientDeadlineMonotonicMs = 0;
    }
}

void RejectBrainOwnedMetarWorkerFact(
    BrainOwnedRuntimeState* state,
    BrainOwnedAsyncFactCycleOutput* output,
    const char* reason) {
    if (state == nullptr || output == nullptr) return;
    ++state->metar.completionRejectedCount;
    output->completionRejected = true;
    output->reason = reason == nullptr
        ? "metar-completion-rejected" : reason;
}

BrainMetarTerminalDiagnostic BuildTerminalDiagnostic(
    const BrainMetarWorkerFact& fact) {
    BrainMetarTerminalDiagnostic diagnostic;
    diagnostic.available = true;
    diagnostic.request = fact.request;
    diagnostic.stationIcao = fact.stationIcao;
    diagnostic.status = fact.status;
    diagnostic.terminalStage = fact.terminalStage;
    diagnostic.winHttpOperation = fact.winHttpOperation;
    diagnostic.winHttpResult = fact.winHttpResult;
    diagnostic.winHttpError = fact.winHttpError;
    diagnostic.transportProgress = fact.transportProgress;
    diagnostic.httpStatus = fact.httpStatus;
    diagnostic.payloadBytes = fact.payloadBytes;
    diagnostic.networkElapsedUs = fact.networkElapsedUs;
    diagnostic.completedMonotonicMs = fact.completedMonotonicMs;
    diagnostic.diagnostic = fact.diagnostic;
    diagnostic.source = fact.source;
    return diagnostic;
}

void BeginDispositionDiagnostic(
    const BrainMetarWorkerFact& fact,
    BrainOwnedAsyncFactCycleOutput* output) {
    if (output == nullptr) return;
    output->dispositionDiagnostic.available = true;
    output->dispositionDiagnostic.requestId = fact.request.requestId;
    output->dispositionDiagnostic.airportIcao = fact.request.airportIcao;
    output->dispositionDiagnostic.parserReason = "not-attempted";
}

void FinalizeDispositionDiagnostic(
    const BrainOwnedRuntimeState& state,
    std::uint64_t historyMutationBefore,
    std::uint64_t presentationGenerationBefore,
    BrainOwnedAsyncFactCycleOutput* output) {
    if (output == nullptr || !output->dispositionDiagnostic.available) return;
    output->dispositionDiagnostic.historyMutated =
        state.metar.historyMutationCount != historyMutationBefore;
    output->dispositionDiagnostic.presentationChanged =
        state.metar.presentationGeneration != presentationGenerationBefore;
    output->dispositionDiagnostic.reason = output->reason;
}

void CommitBrainOwnedMetarWorkerFact(
    BrainOwnedRuntimeState* state,
    const BrainMetarWorkerFact& fact,
    const BrainOwnedAsyncFactCycleInput& input,
    BrainOwnedAsyncFactCycleOutput* output) {
    if (state == nullptr || output == nullptr) return;
    BeginDispositionDiagnostic(fact, output);
    if (!RequestMatchesCurrent(state->metar, fact)) {
        RejectBrainOwnedMetarWorkerFact(
            state, output, "metar-completion-generation-rejected");
        return;
    }
    state->metar.lastAcceptedNetworkElapsedUs = fact.networkElapsedUs;
    output->acceptedNetworkElapsedUs = fact.networkElapsedUs;
    std::string returnedStation;
    const bool validSuccessSource =
        fact.status != BrainMetarWorkerStatus::Success ||
        (fact.source == "VATSIM_METAR" &&
         NormalizeStrictMetarIcao(fact.stationIcao, &returnedStation) &&
         returnedStation == fact.request.airportIcao);
    if (!validSuccessSource) {
        if (fact.request.purpose == BrainMetarRequestPurpose::PilotLookup) {
            HandleLookupFailure(state, input.monotonicMs,
                                &output->presentationChanged);
        } else {
            RecordPrimaryFailure(state, input.monotonicMs,
                                 &output->presentationChanged);
            output->sourceHealthChanged = true;
        }
        ++state->metar.completionAcceptedCount;
        output->completionAccepted = true;
        output->reason = "metar-worker-source-rejected";
        return;
    }
    if (fact.status != BrainMetarWorkerStatus::Success) {
        if (fact.request.purpose == BrainMetarRequestPurpose::PilotLookup) {
            HandleLookupFailure(state, input.monotonicMs,
                                &output->presentationChanged);
        } else {
            RecordPrimaryFailure(state, input.monotonicMs,
                                 &output->presentationChanged);
            output->sourceHealthChanged = true;
        }
        ++state->metar.completionAcceptedCount;
        output->completionAccepted = true;
        output->reason = "metar-worker-failure-accepted";
        return;
    }

    if (fact.request.purpose != BrainMetarRequestPurpose::PilotLookup) {
        ++state->metar.fingerprintCount;
        const auto fingerprint = FingerprintBrainMetarContent(fact.rawMetar);
        const bool sameContent = state->metar.primaryObservation.valid &&
            state->metar.primaryContentFingerprint == fingerprint &&
            state->metar.primaryObservation.rawMetar == fact.rawMetar;
        const auto oldHealth = state->metar.sourceHealth;
        const auto oldVisible = state->metar.visibleState;
        state->metar.sourceHealth = BrainMetarSourceHealth::Healthy;
        state->metar.lastSuccessMonotonicMs = input.monotonicMs;
        const auto ageWasVisible = state->metar.visibleFetchAgeMinutes != 0;
        state->metar.visibleFetchAgeMinutes = 0;
        state->metar.nextVisibleAgeBucketMonotonicMs =
            input.monotonicMs + 60'000;
        state->metar.consecutiveFailures = 0;
        state->metar.nextPrimaryEligibleMonotonicMs =
            input.monotonicMs + kBrainMetarRefreshMs;
        if (oldHealth != state->metar.sourceHealth) {
            ++state->metar.sourceHealthGeneration;
            output->sourceHealthChanged = true;
        }
        if (sameContent) {
            if (state->metar.primaryObservation.valid &&
                input.monotonicMs >= state->metar.freshUntilMonotonicMs) {
                state->metar.visibleState = BrainMetarVisibleState::Stale;
            } else {
                state->metar.visibleState = BrainMetarVisibleState::Fresh;
            }
            if (oldVisible != state->metar.visibleState ||
                (oldHealth == BrainMetarSourceHealth::Failed &&
                 oldVisible == BrainMetarVisibleState::Cached) ||
                (ageWasVisible &&
                 state->accessory.activeDrawer ==
                     BrainOwnedAccessoryDrawerId::Metar)) {
                if (oldVisible != state->metar.visibleState) {
                    EndSpotlightForPrimaryChange(state);
                }
                TouchPresentation(state);
                output->presentationChanged = true;
            }
            ++state->metar.completionAcceptedCount;
            output->completionAccepted = true;
            output->reason = "metar-identical-health-updated";
            output->dispositionDiagnostic.accepted = true;
            return;
        }

        ++state->metar.parseCount;
        auto parsed = ParseBrainOwnedMetarReport(
            fact.stationIcao, fact.rawMetar, input.utcUnixSeconds);
        output->dispositionDiagnostic.parsingAttempted = true;
        output->dispositionDiagnostic.parserReason = parsed.diagnostic;
        if (!parsed.valid) {
            RecordPrimaryFailure(state, input.monotonicMs,
                                 &output->presentationChanged);
            output->reason = "metar-primary-parse-rejected";
            output->completionAccepted = true;
            ++state->metar.completionAcceptedCount;
            return;
        }
        state->metar.primaryObservation = parsed;
        state->metar.primaryContentFingerprint = fingerprint;
        const auto observationAgeSeconds = std::max<std::int64_t>(
            0, input.utcUnixSeconds - parsed.observationUnixSeconds);
        const auto remainingMs = std::max<long long>(
            0, kBrainMetarFreshnessMs - observationAgeSeconds * 1000);
        state->metar.freshUntilMonotonicMs = input.monotonicMs + remainingMs;
        state->metar.visibleState = remainingMs > 0
            ? BrainMetarVisibleState::Fresh
            : BrainMetarVisibleState::Stale;
        ++state->metar.contentGeneration;
        ++state->metar.freshnessGeneration;
        output->contentChanged = true;
        output->freshnessChanged = true;
        PublishHistory(state, parsed, fact.request.purpose);
        EndSpotlightForPrimaryChange(state);
        TouchPresentation(state);
        output->presentationChanged = true;
        output->completionAccepted = true;
        output->dispositionDiagnostic.accepted = true;
        output->dispositionDiagnostic.acceptedCategory = parsed.category;
        ++state->metar.completionAcceptedCount;
        output->reason = "metar-primary-content-accepted";
        return;
    }

    ++state->metar.fingerprintCount;
    const auto fingerprint = FingerprintBrainMetarContent(fact.rawMetar);
    const auto sameLookupContent =
        state->metar.lookupObservation.valid &&
        state->metar.lookupObservation.stationIcao == fact.stationIcao &&
        state->metar.lookupContentFingerprint == fingerprint &&
        state->metar.lookupObservation.rawMetar == fact.rawMetar;
    const auto samePrimaryContent =
        state->metar.primaryObservation.valid &&
        state->metar.primaryObservation.stationIcao == fact.stationIcao &&
        state->metar.primaryContentFingerprint == fingerprint &&
        state->metar.primaryObservation.rawMetar == fact.rawMetar;
    if (sameLookupContent || samePrimaryContent) {
        if (samePrimaryContent) {
            state->metar.lookupObservation = state->metar.primaryObservation;
            state->metar.lookupContentFingerprint = fingerprint;
        }
        state->metar.lookupAwaitingDispatch = false;
        state->metar.lookupTimedOut = false;
        if (PresentationOwned(*state)) {
            state->metar.transientPresentation =
                BrainMetarTransientPresentation::LookupSpotlight;
            state->metar.transientDeadlineMonotonicMs =
                input.monotonicMs + kBrainMetarLookupSpotlightMs;
            TouchPresentation(state);
            output->presentationChanged = true;
        } else {
            state->metar.transientPresentation =
                BrainMetarTransientPresentation::None;
            state->metar.transientDeadlineMonotonicMs = 0;
            state->metar.lookupPresentationOwnershipValid = false;
        }
        output->completionAccepted = true;
        output->dispositionDiagnostic.accepted = true;
        output->dispositionDiagnostic.acceptedCategory =
            state->metar.lookupObservation.category;
        ++state->metar.completionAcceptedCount;
        output->reason = "metar-lookup-identical-content-accepted";
        return;
    }

    ++state->metar.parseCount;
    auto parsed = ParseBrainOwnedMetarReport(
        fact.stationIcao, fact.rawMetar, input.utcUnixSeconds);
    output->dispositionDiagnostic.parsingAttempted = true;
    output->dispositionDiagnostic.parserReason = parsed.diagnostic;
    if (!parsed.valid) {
        HandleLookupFailure(state, input.monotonicMs,
                            &output->presentationChanged);
        output->completionAccepted = true;
        ++state->metar.completionAcceptedCount;
        output->reason = "metar-lookup-parse-rejected";
        return;
    }
    state->metar.lookupObservation = parsed;
    state->metar.lookupContentFingerprint = fingerprint;
    state->metar.lookupAwaitingDispatch = false;
    state->metar.lookupTimedOut = false;
    output->contentChanged = PublishHistory(
        state, parsed, BrainMetarRequestPurpose::PilotLookup);
    ++state->metar.contentGeneration;
    output->contentChanged = true;
    if (PresentationOwned(*state)) {
        state->metar.transientPresentation =
            BrainMetarTransientPresentation::LookupSpotlight;
        state->metar.transientDeadlineMonotonicMs =
            input.monotonicMs + kBrainMetarLookupSpotlightMs;
        TouchPresentation(state);
        output->presentationChanged = true;
    } else {
        state->metar.transientPresentation =
            BrainMetarTransientPresentation::None;
        state->metar.transientDeadlineMonotonicMs = 0;
        state->metar.lookupPresentationOwnershipValid = false;
    }
    output->completionAccepted = true;
    output->dispositionDiagnostic.accepted = true;
    output->dispositionDiagnostic.acceptedCategory = parsed.category;
    ++state->metar.completionAcceptedCount;
    output->reason = "metar-lookup-content-accepted";
}

BrainMetarWorkerRequest MakeRequest(
    BrainOwnedMetarRuntimeState* metar,
    const std::string& airport,
    BrainMetarRequestPurpose purpose,
    long long nowMs) {
    BrainMetarWorkerRequest request;
    request.airportIcao = airport;
    request.requestId = metar->nextRequestId++;
    request.purpose = purpose;
    request.primaryGeneration = metar->primaryGeneration;
    request.lookupGeneration = metar->lookupGeneration;
    request.dispatchedMonotonicMs = nowMs;
    return request;
}

std::optional<BrainMetarWorkerRequest> PlanBrainOwnedMetarWork(
    BrainOwnedMetarRuntimeState* metar,
    const BrainOwnedAsyncFactCycleInput& input) {
    if (metar == nullptr) return std::nullopt;
    if (!metar->primaryAirportIcao.empty() &&
        !metar->primaryObservation.valid &&
        input.monotonicMs >= metar->nextPrimaryEligibleMonotonicMs) {
        return MakeRequest(
            metar, metar->primaryAirportIcao,
            BrainMetarRequestPurpose::PrimaryTarget, input.monotonicMs);
    }
    if (metar->lookupAwaitingDispatch && !metar->lookupInvalidated &&
        !metar->pendingLookupIcao.empty()) {
        return MakeRequest(
            metar, metar->pendingLookupIcao,
            BrainMetarRequestPurpose::PilotLookup, input.monotonicMs);
    }
    if (!metar->primaryAirportIcao.empty() &&
        input.monotonicMs >= metar->nextPrimaryEligibleMonotonicMs) {
        return MakeRequest(
            metar, metar->primaryAirportIcao,
            BrainMetarRequestPurpose::PrimaryRefresh, input.monotonicMs);
    }
    return std::nullopt;
}

}  // namespace

bool NormalizeStrictMetarIcao(
    const std::string& input,
    std::string* normalized) {
    if (normalized != nullptr) normalized->clear();
    const auto trimmed = TrimOuter(input);
    if (trimmed.size() != 4) return false;
    std::string candidate;
    candidate.reserve(4);
    for (char character : trimmed) {
        const auto value = static_cast<unsigned char>(character);
        if (std::isalnum(value) == 0 || value > 0x7f) return false;
        candidate.push_back(static_cast<char>(std::toupper(value)));
    }
    if (candidate == "ALL") return false;
    if (normalized != nullptr) *normalized = candidate;
    return true;
}

std::uint64_t FingerprintBrainMetarContent(const std::string& rawMetar) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto character : rawMetar) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 1099511628211ULL;
    }
    return hash;
}

BrainMetarParsedObservation ParseBrainOwnedMetarReport(
    const std::string& verifiedStationIcao,
    const std::string& rawMetar,
    std::int64_t referenceUtcUnixSeconds) {
    BrainMetarParsedObservation result;
    if (!NormalizeStrictMetarIcao(verifiedStationIcao, &result.stationIcao)) {
        result.diagnostic = "invalid-station";
        return result;
    }
    const auto trimmed = TrimOuter(rawMetar);
    if (trimmed.empty() || trimmed.size() > 4096) {
        result.diagnostic = "invalid-raw-size";
        return result;
    }
    result.rawMetar = trimmed;
    const auto tokens = TokenizeBeforeRemarks(trimmed);
    if (tokens.empty()) {
        result.diagnostic = "empty-report";
        return result;
    }
    std::size_t index = 0;
    if (tokens[index] == "METAR" || tokens[index] == "SPECI") {
        result.speci = tokens[index] == "SPECI";
        ++index;
    }
    if (index < tokens.size() &&
        (tokens[index] == "COR" || tokens[index] == "AMD")) {
        ++index;
    }
    if (index < tokens.size()) {
        std::string embedded;
        if (NormalizeStrictMetarIcao(tokens[index], &embedded)) {
            if (embedded != result.stationIcao) {
                result.diagnostic = "embedded-station-mismatch";
                return result;
            }
            ++index;
        }
    }
    if (index >= tokens.size() || !ParseObservationToken(
            tokens[index], &result.observationDay,
            &result.observationHour, &result.observationMinute)) {
        result.diagnostic = "observation-time-missing";
        return result;
    }
    if (!ResolveObservationUnix(
            result.observationDay, result.observationHour,
            result.observationMinute, referenceUtcUnixSeconds,
            &result.observationUnixSeconds)) {
        result.diagnostic = "observation-time-implausible";
        return result;
    }
    ++index;

    bool cavok = false;
    bool skyEvidence = false;
    bool ambiguousSky = false;
    int lowestCeiling = std::numeric_limits<int>::max();
    for (std::size_t tokenIndex = index; tokenIndex < tokens.size(); ++tokenIndex) {
        const auto& token = tokens[tokenIndex];
        if (token == "CAVOK") {
            cavok = true;
            result.visibilityKnown = true;
            result.visibilitySm = 10.0 * kKilometersToStatuteMiles;
            result.ceilingKnown = true;
            result.noCeilingProven = true;
            skyEvidence = true;
            continue;
        }
        bool consumedPrevious = false;
        double sm = 0.0;
        if (ParseSmVisibility(tokens, tokenIndex, &sm, &consumedPrevious)) {
            result.visibilityKnown = true;
            result.visibilitySm = sm;
            continue;
        }
        if (token.size() == 4 && AllDigits(token)) {
            const auto meters = std::stoi(token);
            if (meters >= 0) {
                result.visibilityKnown = true;
                result.visibilitySm = (meters == 9999 ? 10000.0 : meters) /
                    1000.0 * kKilometersToStatuteMiles;
            }
            continue;
        }
        if (token.size() >= 2 && token[0] == 'R' &&
            token.find('/') != std::string::npos) {
            continue;
        }
        if (token == "NSC" || token == "NCD" || token == "CLR" ||
            token == "SKC") {
            skyEvidence = true;
            result.ceilingKnown = true;
            result.noCeilingProven = true;
            continue;
        }
        if (token.size() >= 2 && token.substr(0, 2) == "VV") {
            skyEvidence = true;
            if (token.size() < 5 || !AllDigits(token.substr(2, 3))) {
                ambiguousSky = true;
            } else {
                lowestCeiling = std::min(
                    lowestCeiling, std::stoi(token.substr(2, 3)) * 100);
            }
            continue;
        }
        if (token.size() >= 3) {
            const auto cover = token.substr(0, 3);
            if (cover == "FEW" || cover == "SCT" || cover == "BKN" ||
                cover == "OVC") {
                skyEvidence = true;
                if (token.size() < 6 || !AllDigits(token.substr(3, 3))) {
                    if (cover == "BKN" || cover == "OVC") {
                        ambiguousSky = true;
                    }
                    continue;
                }
                if (cover == "BKN" || cover == "OVC") {
                    lowestCeiling = std::min(
                        lowestCeiling, std::stoi(token.substr(3, 3)) * 100);
                }
            }
        }
    }
    if (!cavok && lowestCeiling != std::numeric_limits<int>::max()) {
        result.ceilingKnown = true;
        result.noCeilingProven = false;
        result.ceilingFeet = lowestCeiling;
    } else if (!cavok && skyEvidence && !ambiguousSky) {
        result.ceilingKnown = true;
        result.noCeilingProven = true;
    }
    const int visibilityRank = result.visibilityKnown
        ? VisibilityCategory(result.visibilitySm) : -1;
    const int ceilingRank = result.ceilingKnown
        ? CeilingCategory(result.ceilingFeet, result.noCeilingProven) : -1;
    if (visibilityRank == 3 || ceilingRank == 3) {
        result.category = BrainMetarFlightCategory::Lifr;
    } else if (visibilityRank >= 0 && ceilingRank >= 0) {
        result.category = CategoryFromRank(std::max(visibilityRank, ceilingRank));
    } else {
        result.category = BrainMetarFlightCategory::Unknown;
    }
    result.valid = true;
    result.diagnostic = result.category == BrainMetarFlightCategory::Unknown
        ? "valid-report-category-unknown" : "valid-report-classified";
    return result;
}

const char* BrainMetarCategoryToken(BrainMetarFlightCategory category) {
    switch (category) {
        case BrainMetarFlightCategory::Vfr: return "VFR";
        case BrainMetarFlightCategory::Mvfr: return "MVFR";
        case BrainMetarFlightCategory::Ifr: return "IFR";
        case BrainMetarFlightCategory::Lifr: return "LIFR";
        case BrainMetarFlightCategory::Unknown:
        default: return "UNKNOWN";
    }
}

const char* BrainMetarVisibleStateToken(BrainMetarVisibleState state) {
    switch (state) {
        case BrainMetarVisibleState::Pending: return "PENDING";
        case BrainMetarVisibleState::Fresh: return "";
        case BrainMetarVisibleState::Cached: return "CACHED";
        case BrainMetarVisibleState::Stale: return "STALE";
        case BrainMetarVisibleState::Unavailable: return "UNAVAILABLE";
        case BrainMetarVisibleState::Unknown:
        default: return "UNKNOWN";
    }
}

BrainOwnedTextEntryDecision CommitBrainOwnedTextEntryFact(
    BrainOwnedRuntimeState* state,
    const BrainOwnedTextEntryFact& fact) {
    BrainOwnedTextEntryDecision decision;
    if (state == nullptr || fact.mode != BrainOwnedTextEntryMode::MetarAirportLookup) {
        decision.reason = "text-entry-mode-not-metar";
        return decision;
    }
    if (state->metar.dispatchSuspendedForDisconnect) {
        decision.reason = "metar-lookup-disconnected";
        return decision;
    }
    if (!NormalizeStrictMetarIcao(fact.text, &decision.normalizedText)) {
        decision.reason = "metar-lookup-invalid-icao";
        return decision;
    }
    state->metar.initialized = true;
    ++state->metar.lookupGeneration;
    state->metar.pendingLookupIcao = decision.normalizedText;
    state->metar.lookupAwaitingDispatch = true;
    state->metar.lookupInvalidated = false;
    if (state->metar.lookupObservation.stationIcao != decision.normalizedText) {
        state->metar.lookupObservation = {};
        state->metar.lookupContentFingerprint = 0;
    }
    state->metar.lookupTimedOut = false;
    SelectMetarDrawerForLookup(state);
    state->metar.transientPresentation =
        BrainMetarTransientPresentation::LookupPending;
    state->metar.transientDeadlineMonotonicMs =
        fact.monotonicMs + kBrainMetarLookupPendingMs;
    TouchPresentation(state);
    decision.accepted = true;
    decision.presentationChanged = true;
    decision.reason = "metar-lookup-accepted";
    return decision;
}

BrainOwnedAsyncFactCycleOutput RunBrainOwnedAsyncFactCycle(
    BrainOwnedRuntimeState* state,
    const BrainOwnedAsyncFactCycleInput& input,
    const BrainOwnedAsyncWorkerBindings& workers) {
    const auto cycleStarted = std::chrono::steady_clock::now();
    BrainOwnedAsyncFactCycleOutput output;
    const auto finish = [&]() {
        output.simulatorThreadElapsedUs =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - cycleStarted).count();
        if (state != nullptr) {
            state->metar.lastSimulatorThreadCycleUs =
                output.simulatorThreadElapsedUs;
            state->metar.maximumSimulatorThreadCycleUs = std::max(
                state->metar.maximumSimulatorThreadCycleUs,
                output.simulatorThreadElapsedUs);
            if (output.acceptedNetworkElapsedUs == 0) {
                output.acceptedNetworkElapsedUs =
                    state->metar.lastAcceptedNetworkElapsedUs;
            }
        }
        return output;
    };
    if (state == nullptr) {
        output.reason = "metar-state-unavailable";
        return finish();
    }
    state->metar.initialized = true;
    DropLostPresentationOwnership(state);

    BrainMetarWorkerFact harvested;
    const bool hasHarvested = workers.metar != nullptr &&
        workers.metar->TryHarvest(&harvested);
    if (hasHarvested) {
        state->metar.activeRequest.reset();
        output.terminalDiagnostic = BuildTerminalDiagnostic(harvested);
    }

    if (!input.pluginEnabled || !input.xpilotConnected) {
        state->metar.dispatchSuspendedForDisconnect = true;
        if (hasHarvested) state->metar.deferredDisconnectedFact = harvested;
        output.reason = "metar-disconnected-idle";
        return finish();
    }

    const bool reconnecting = state->metar.dispatchSuspendedForDisconnect;
    state->metar.dispatchSuspendedForDisconnect = false;
    const auto targetContextKey = BuildTargetContextKey(input);
    bool fromVfr = false;
    if (!state->metar.targetContextInitialized || reconnecting ||
        state->metar.targetContextKey != targetContextKey) {
        const auto target = ResolveBrainOwnedMetarPrimaryTarget(
            &state->metar, input, &fromVfr);
        SetPrimaryTarget(state, target, fromVfr, input.monotonicMs);
        state->metar.targetContextInitialized = true;
        state->metar.targetContextKey = targetContextKey;
        state->metar.lastOperatingMode = input.operatingMode;
        state->metar.lastWorkflowStage = input.workflowStage;
    }

    if (hasHarvested) {
        const auto historyMutationBefore = state->metar.historyMutationCount;
        const auto presentationGenerationBefore =
            state->metar.presentationGeneration;
        CommitBrainOwnedMetarWorkerFact(state, harvested, input, &output);
        FinalizeDispositionDiagnostic(
            *state, historyMutationBefore, presentationGenerationBefore,
            &output);
    }
    if (reconnecting && state->metar.deferredDisconnectedFact.has_value()) {
        const auto deferred = *state->metar.deferredDisconnectedFact;
        state->metar.deferredDisconnectedFact.reset();
        const auto historyMutationBefore = state->metar.historyMutationCount;
        const auto presentationGenerationBefore =
            state->metar.presentationGeneration;
        CommitBrainOwnedMetarWorkerFact(state, deferred, input, &output);
        FinalizeDispositionDiagnostic(
            *state, historyMutationBefore, presentationGenerationBefore,
            &output);
    }

    DropLostPresentationOwnership(state);
    if (state->metar.transientPresentation ==
            BrainMetarTransientPresentation::LookupPending &&
        input.monotonicMs >= state->metar.transientDeadlineMonotonicMs) {
        state->metar.lookupInvalidated = true;
        state->metar.lookupAwaitingDispatch = false;
        state->metar.lookupTimedOut = true;
        HandleLookupFailure(state, input.monotonicMs,
                            &output.presentationChanged);
        output.reason = "metar-lookup-presentation-timeout";
    } else if ((state->metar.transientPresentation ==
                    BrainMetarTransientPresentation::LookupSpotlight ||
                state->metar.transientPresentation ==
                    BrainMetarTransientPresentation::LookupFailure) &&
               input.monotonicMs >= state->metar.transientDeadlineMonotonicMs) {
        state->metar.transientPresentation =
            BrainMetarTransientPresentation::None;
        state->metar.transientDeadlineMonotonicMs = 0;
        state->metar.lookupPresentationOwnershipValid = false;
        TouchPresentation(state);
        output.presentationChanged = true;
        output.reason = "metar-transient-presentation-expired";
    }

    if (state->metar.primaryObservation.valid &&
        state->metar.visibleState != BrainMetarVisibleState::Stale &&
        input.monotonicMs >= state->metar.freshUntilMonotonicMs) {
        state->metar.visibleState = BrainMetarVisibleState::Stale;
        ++state->metar.freshnessGeneration;
        EndSpotlightForPrimaryChange(state);
        TouchPresentation(state);
        output.freshnessChanged = true;
        output.presentationChanged = true;
        output.reason = "metar-primary-became-stale";
    }

    if (state->metar.primaryObservation.valid &&
        input.monotonicMs >= state->metar.nextVisibleAgeBucketMonotonicMs) {
        const auto ageMinutes = static_cast<int>(std::min<long long>(
            999, std::max<long long>(
                0, (input.monotonicMs -
                    state->metar.lastSuccessMonotonicMs) / 60'000)));
        state->metar.nextVisibleAgeBucketMonotonicMs =
            input.monotonicMs + 60'000;
        if (ageMinutes != state->metar.visibleFetchAgeMinutes) {
            state->metar.visibleFetchAgeMinutes = ageMinutes;
            ++state->metar.freshnessGeneration;
            output.freshnessChanged = true;
            if (state->accessory.activeDrawer ==
                BrainOwnedAccessoryDrawerId::Metar) {
                TouchPresentation(state);
                output.presentationChanged = true;
                output.reason = "metar-visible-fetch-age-changed";
            }
        }
    }

    const bool workerBusy = state->metar.activeRequest.has_value() ||
        (workers.metar != nullptr && workers.metar->IsRunning());
    if (workerBusy || workers.metar == nullptr) {
        if (output.reason.empty()) output.reason = "metar-worker-busy-or-unbound";
        return finish();
    }

    const auto request = PlanBrainOwnedMetarWork(&state->metar, input);
    if (!request.has_value()) {
        if (output.reason.empty()) output.reason = "metar-no-work-eligible";
        return finish();
    }
    if (!workers.metar->Start(*request)) {
        if (request->purpose == BrainMetarRequestPurpose::PilotLookup) {
            HandleLookupFailure(state, input.monotonicMs,
                                &output.presentationChanged);
        } else {
            RecordPrimaryFailure(state, input.monotonicMs,
                                 &output.presentationChanged);
            output.sourceHealthChanged = true;
        }
        output.reason = "metar-worker-start-failed";
        return finish();
    }
    state->metar.activeRequest = request;
    ++state->metar.requestDispatchCount;
    output.requestDispatched = true;
    output.dispatchDiagnostic.available = true;
    output.dispatchDiagnostic.request = *request;
    output.reason = "metar-worker-dispatched";
    if (request->purpose != BrainMetarRequestPurpose::PilotLookup) {
        const auto prior = state->metar.sourceHealth;
        state->metar.sourceHealth = BrainMetarSourceHealth::Pending;
        if (prior != state->metar.sourceHealth) {
            ++state->metar.sourceHealthGeneration;
            output.sourceHealthChanged = true;
        }
    }
    return finish();
}

BrainMetarWorkerShutdownSnapshot ApplyBrainOwnedAsyncWorkerLifecycleBoundary(
    BrainOwnedRuntimeState* state,
    const BrainOwnedAsyncWorkerBindings& workers,
    bool clearAcceptedState) {
    BrainMetarWorkerShutdownSnapshot snapshot;
    if (workers.metar != nullptr) {
        workers.metar->CancelAndJoin();
        BrainMetarWorkerFact drained;
        const bool drainedFact = workers.metar->TryHarvest(&drained);
        snapshot = workers.metar->ShutdownSnapshot();
        if (drainedFact) {
            snapshot.terminalFactDrained = true;
            snapshot.terminalDiagnostic = BuildTerminalDiagnostic(drained);
            snapshot.dispositionDiagnostic.available = true;
            snapshot.dispositionDiagnostic.requestId = drained.request.requestId;
            snapshot.dispositionDiagnostic.airportIcao =
                drained.request.airportIcao;
            snapshot.dispositionDiagnostic.accepted = false;
            snapshot.dispositionDiagnostic.parsingAttempted = false;
            snapshot.dispositionDiagnostic.parserReason =
                "not-attempted-lifecycle-boundary";
            snapshot.dispositionDiagnostic.historyMutated = false;
            snapshot.dispositionDiagnostic.presentationChanged = false;
            snapshot.dispositionDiagnostic.reason =
                "metar-lifecycle-terminal-drained-rejected";
        }
    }
    if (state != nullptr) {
        state->metar.activeRequest.reset();
        state->metar.deferredDisconnectedFact.reset();
        state->metar.lookupAwaitingDispatch = false;
        state->metar.lookupInvalidated = true;
        state->metar.transientPresentation = BrainMetarTransientPresentation::None;
        state->metar.transientDeadlineMonotonicMs = 0;
        state->metar.lookupPresentationOwnershipValid = false;
        state->metar.targetContextInitialized = false;
        if (clearAcceptedState) ResetBrainOwnedMetarForHardBoundary(state);
    }
    return snapshot;
}

void ResetBrainOwnedMetarForHardBoundary(BrainOwnedRuntimeState* state) {
    if (state == nullptr) return;
    state->metar = {};
    InvalidateAccessoryProjection(state);
}

void SuspendBrainOwnedMetarForXPilotDisconnect(BrainOwnedRuntimeState* state) {
    if (state != nullptr) state->metar.dispatchSuspendedForDisconnect = true;
}

void ResumeBrainOwnedMetarAfterXPilotReconnect(BrainOwnedRuntimeState* state) {
    if (state != nullptr) state->metar.dispatchSuspendedForDisconnect = false;
}

}  // namespace xvatsim::brain
