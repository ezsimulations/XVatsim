#include "XVatsim/brain/BrainOwnedRuntime.h"

#include "XVatsim/brain/BrainOwnedWorkerTypes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace xvatsim::brain {
namespace {

constexpr char kUnicomFallbackFrequency[] = "122.800";

const char* WorkflowStageToken(WorkflowStage stage) {
    switch (stage) {
        case WorkflowStage::Departure:
            return "DEP";
        case WorkflowStage::Enroute:
            return "ENR";
        case WorkflowStage::Arrival:
            return "ARR";
        case WorkflowStage::None:
        default:
            return "NONE";
    }
}

std::string NormalizeCallsign(std::string value) {
    value.erase(
        std::remove_if(
            value.begin(),
            value.end(),
            [](unsigned char c) { return std::isspace(c) != 0; }),
        value.end());
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
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

bool StationRequiresCompletion(const BoardStationSnapshot& station) {
    return station.role != StationRole::Ctaf &&
           station.role != StationRole::Unicom;
}

bool IsCtafOrUnicom(const BoardStationSnapshot& station) {
    return station.role == StationRole::Ctaf ||
           station.role == StationRole::Unicom;
}

bool IsLiveRouteCenterStation(const BoardStationSnapshot& station) {
    return station.role == StationRole::Center &&
           !station.offline &&
           !station.frequency.empty();
}

bool HasLiveRouteCenters(const ModuleBoardSnapshot& board) {
    return std::any_of(
        board.stations.begin(),
        board.stations.end(),
        IsLiveRouteCenterStation);
}

bool IsBlockedControllerFrequency(const std::string& frequency) {
    const auto normalizedFrequency = NormalizeFrequency(frequency);
    return normalizedFrequency == "121500" || normalizedFrequency == "199998";
}

bool IsStandbyEligibleRole(StationRole role) {
    switch (role) {
        case StationRole::Delivery:
        case StationRole::Ground:
        case StationRole::Tower:
        case StationRole::Departure:
        case StationRole::Approach:
        case StationRole::Center:
            return true;
        case StationRole::Atis:
        case StationRole::Ctaf:
        case StationRole::Unicom:
        case StationRole::Other:
        default:
            return false;
    }
}

int StandbyRoleRank(WorkflowStage workflowStage, StationRole role) {
    if (workflowStage == WorkflowStage::Arrival) {
        switch (role) {
            case StationRole::Center:
                return 0;
            case StationRole::Approach:
            case StationRole::Departure:
                return 1;
            case StationRole::Tower:
                return 2;
            case StationRole::Ground:
                return 3;
            case StationRole::Delivery:
                return 4;
            default:
                return 99;
        }
    }

    switch (role) {
        case StationRole::Delivery:
            return 0;
        case StationRole::Ground:
            return 1;
        case StationRole::Tower:
            return 2;
        case StationRole::Departure:
        case StationRole::Approach:
            return 3;
        case StationRole::Center:
            return 4;
        default:
            return 99;
    }
}

bool FrequencyTuned(
    const std::string& frequency,
    const RadioStateSnapshot& radioStateSnapshot) {
    const auto normalizedTarget = NormalizeFrequency(frequency);
    if (normalizedTarget.empty()) {
        return false;
    }

    return NormalizeFrequency(radioStateSnapshot.com1ActiveFrequency) ==
               normalizedTarget ||
           NormalizeFrequency(radioStateSnapshot.com2ActiveFrequency) ==
               normalizedTarget;
}

std::string StandbyAssistWorkflowKey(
    WorkflowStage workflowStage,
    const std::string& planKey,
    const RadioStateSnapshot& radios,
    const BoardStationSnapshot& targetStation) {
    return planKey + "|" +
           std::to_string(static_cast<int>(workflowStage)) + "|" +
           NormalizeFrequency(radios.com1ActiveFrequency) + "|" +
           NormalizeCallsign(targetStation.callsign) + "|" +
           NormalizeFrequency(targetStation.frequency);
}

std::string StationKey(const BoardStationSnapshot& station) {
    return std::to_string(static_cast<int>(station.role)) + "|" +
           NormalizeCallsign(station.callsign) + "|" +
           NormalizeFrequency(station.frequency) + "|" +
           station.annotation;
}

void RemoveCtafStations(ModuleBoardSnapshot* board) {
    if (board == nullptr) {
        return;
    }

    board->stations.erase(
        std::remove_if(
            board->stations.begin(),
            board->stations.end(),
            IsCtafOrUnicom),
        board->stations.end());
    board->available = board->available || !board->stations.empty();
}

void AppendUniqueStation(
    const BoardStationSnapshot& station,
    ModuleBoardSnapshot* board,
    std::unordered_set<std::string>* keys) {
    if (board == nullptr || keys == nullptr) {
        return;
    }
    if (!keys->insert(StationKey(station)).second) {
        return;
    }

    board->stations.push_back(station);
    board->available = true;
}

bool CompletionApprovesStation(
    const BrainOwnedCandidateCompletion& completion,
    const BoardStationSnapshot& station) {
    return completion.decision == BrainOwnedCandidateDecision::Accepted &&
           NormalizeCallsign(completion.callsign) ==
               NormalizeCallsign(station.callsign) &&
           NormalizeFrequency(completion.frequency) ==
               NormalizeFrequency(station.frequency);
}

bool CompletionDisplayedInFinalBoard(
    const ModuleBoardSnapshot& board,
    const BrainOwnedCandidateCompletion& completion) {
    return std::any_of(
        board.stations.begin(),
        board.stations.end(),
        [&](const auto& displayedStation) {
            return NormalizeCallsign(displayedStation.callsign) ==
                       NormalizeCallsign(completion.callsign) &&
                   NormalizeFrequency(displayedStation.frequency) ==
                       NormalizeFrequency(completion.frequency);
        });
}

bool BuildCtafStationFromLookupFact(
    const BrainOwnedCtafLookupFact& fact,
    const RadioStateSnapshot& radios,
    BoardStationSnapshot* station) {
    if (station == nullptr || fact.airportIcao.empty()) {
        return false;
    }

    *station = {};
    station->callsign = fact.airportIcao;
    if (fact.available) {
        station->role = StationRole::Ctaf;
        station->frequency = fact.frequency;
        station->tuned = FrequencyTuned(fact.frequency, radios);
    } else if (fact.resolved) {
        station->role = StationRole::Unicom;
        station->frequency = kUnicomFallbackFrequency;
        station->tuned = FrequencyTuned(kUnicomFallbackFrequency, radios);
    } else {
        station->role = StationRole::Ctaf;
        station->annotation = "lookup";
    }
    return true;
}

double ResolveCruiseComparisonAltitudeFt(
    const AircraftStateSnapshot& aircraftState,
    double cruiseTargetFt) {
    auto comparisonAltitudeFt = aircraftState.altitudeOperationalFt;
    if (cruiseTargetFt >= 18000.0 && aircraftState.hasAltimeterSetting) {
        comparisonAltitudeFt +=
            (29.92 - aircraftState.altimeterSettingInHg) * 1000.0;
    }
    return comparisonAltitudeFt;
}

double NormalizeCruiseAltitudeFt(double altitudeFt) {
    if (altitudeFt <= 0.0) {
        return 0.0;
    }
    return std::round(altitudeFt / 100.0) * 100.0;
}

std::string FormatCruiseTargetText(double altitudeFt) {
    const auto normalizedAltitudeFt = NormalizeCruiseAltitudeFt(altitudeFt);
    if (normalizedAltitudeFt <= 0.0) {
        return {};
    }

    const auto roundedAltitudeFt = static_cast<int>(normalizedAltitudeFt);
    if (roundedAltitudeFt >= 18000) {
        return "FL" + std::to_string(roundedAltitudeFt / 100);
    }

    return std::to_string(roundedAltitudeFt);
}

bool AircraftWithinCruiseTargetBand(
    const AircraftStateSnapshot& aircraftState,
    double cruiseTargetFt,
    const BrainOwnedCruiseTargetTuning& tuning) {
    return std::fabs(
               ResolveCruiseComparisonAltitudeFt(
                   aircraftState,
                   cruiseTargetFt) -
               cruiseTargetFt) <= tuning.gateToleranceFt;
}

}  // namespace

void ResetBrainOwnedRuntimeState(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    const auto displayOverrideMode = state->displayOverrideMode;
    *state = {};
    state->displayOverrideMode = displayOverrideMode;
}

void ResetBrainOwnedRuntimeCachePreservingFlightContext(
    BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    const auto flightContext = state->flightContext;
    const auto displayOverrideMode = state->displayOverrideMode;
    const auto pendingTextEntryMode = state->pendingTextEntryMode;
    const auto lastAircraftStateSnapshot = state->lastAircraftStateSnapshot;
    const auto lastPilotIdentitySnapshot = state->lastPilotIdentitySnapshot;
    const auto lastFlightPlanSnapshot = state->lastFlightPlanSnapshot;
    const auto lastNetworkPlanSnapshot = state->lastNetworkPlanSnapshot;
    *state = {};
    state->flightContext = flightContext;
    state->displayOverrideMode = displayOverrideMode;
    state->pendingTextEntryMode = pendingTextEntryMode;
    state->lastAircraftStateSnapshot = lastAircraftStateSnapshot;
    state->lastPilotIdentitySnapshot = lastPilotIdentitySnapshot;
    state->lastFlightPlanSnapshot = lastFlightPlanSnapshot;
    state->lastNetworkPlanSnapshot = lastNetworkPlanSnapshot;
}

void ResetBrainOwnedDisplayPublisherState(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->phaseSnapshotPublisherState.Reset();
}

void CommitBrainOwnedLastSampledFacts(
    BrainOwnedRuntimeState* state,
    const AircraftStateSnapshot& aircraftState,
    const PilotIdentitySnapshot& pilotIdentity,
    const FlightPlanSnapshot& flightPlan,
    const NetworkPlanSnapshot& networkPlan) {
    if (state == nullptr) {
        return;
    }
    state->lastAircraftStateSnapshot = aircraftState;
    state->lastPilotIdentitySnapshot = pilotIdentity;
    state->lastFlightPlanSnapshot = flightPlan;
    state->lastNetworkPlanSnapshot = networkPlan;
}

void ClearBrainOwnedLastSampledFacts(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->lastAircraftStateSnapshot = {};
    state->lastPilotIdentitySnapshot = {};
    state->lastFlightPlanSnapshot = {};
    state->lastNetworkPlanSnapshot = {};
}

void CommitBrainOwnedFlightContext(
    BrainOwnedRuntimeState* state,
    const workflow::FlightContext& flightContext) {
    if (state == nullptr) {
        return;
    }
    state->flightContext = flightContext;
}

void ClearBrainOwnedFlightContext(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->flightContext = {};
}

void ClearBrainOwnedXPilotConnectionTracking(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->xPilotSessionBoundaryState.lastXPilotConnected = false;
    state->xPilotSessionBoundaryState.lastConnectedPilotCallsign.clear();
}

void ClearBrainOwnedFlightRecoveryRequests(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->xPilotSessionBoundaryState.disconnectedPilotCallsign.clear();
    state->pendingAutomaticFlightRecovery = false;
    state->manualFlightRecoveryRequested = false;
}

void SetBrainOwnedAutomaticFlightRecoveryPending(
    BrainOwnedRuntimeState* state,
    bool pending) {
    if (state == nullptr) {
        return;
    }
    state->pendingAutomaticFlightRecovery = pending;
}

void SetBrainOwnedManualFlightRecoveryRequested(
    BrainOwnedRuntimeState* state,
    bool requested) {
    if (state == nullptr) {
        return;
    }
    state->manualFlightRecoveryRequested = requested;
}

void SetBrainOwnedColdDarkResetApplied(
    BrainOwnedRuntimeState* state,
    bool applied) {
    if (state == nullptr) {
        return;
    }
    state->coldDarkResetApplied = applied;
}

void ClearBrainOwnedAircraftStateInvalidBoundary(
    BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->aircraftStateInvalidBoundaryActive = false;
}

void ApplyBrainOwnedXPilotSessionBoundaryDecision(
    BrainOwnedRuntimeState* state,
    const workflow::XPilotSessionBoundaryDecision& decision) {
    if (state == nullptr) {
        return;
    }
    state->xPilotSessionBoundaryState = decision.nextState;
    if (decision.shouldClearPendingRecoveryRequests) {
        state->pendingAutomaticFlightRecovery = false;
        state->manualFlightRecoveryRequested = false;
    }
    if (decision.shouldQueueAutomaticRecovery) {
        state->pendingAutomaticFlightRecovery = true;
    }
    if (decision.sawXPilotConnectedThisFlight) {
        state->sawXPilotConnectedThisFlight = true;
    }
}

void ApplyBrainOwnedAircraftRuntimeBoundaryDecision(
    BrainOwnedRuntimeState* state,
    const workflow::AircraftRuntimeBoundaryDecision& decision) {
    if (state == nullptr) {
        return;
    }
    state->coldDarkResetApplied = decision.nextColdDarkResetApplied;
    state->aircraftStateInvalidBoundaryActive =
        decision.nextAircraftStateInvalidBoundaryActive;
}

void ResetBrainOwnedStandbyAssistLatch(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->standbyAssistLatchKey.clear();
    state->standbyAssistWriteConsumed = false;
}

void ClearBrainOwnedDiversionOverrideSource(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->diversionOverrideSourceKey.clear();
}

void SetBrainOwnedDiversionOverrideSourceKey(
    BrainOwnedRuntimeState* state,
    const std::string& sourcePlanKey) {
    if (state == nullptr) {
        return;
    }
    state->diversionOverrideSourceKey = sourcePlanKey;
}

BrainOwnedDiversionOverrideDecision DecideBrainOwnedDiversionOverride(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedDiversionOverrideInput& input) {
    BrainOwnedDiversionOverrideDecision decision;
    if (!input.hasOverride) {
        return decision;
    }

    if (input.sourcePlanKey.empty() ||
        state.diversionOverrideSourceKey.empty() ||
        input.sourcePlanKey != state.diversionOverrideSourceKey) {
        decision.clearOverride = true;
        decision.logLine =
            "[XVatsim] Diversion override cleared because source VATSIM flight plan was stale, unmatched, or changed.\n";
        return decision;
    }

    decision.useOverride = true;
    return decision;
}

void ClearBrainOwnedPreflightRouteCacheApplication(
    BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->preflightRouteCacheAppliedPlanKey.clear();
}

void SetBrainOwnedDisplayOverrideMode(
    BrainOwnedRuntimeState* state,
    BrainOwnedDisplayOverrideMode mode) {
    if (state == nullptr) {
        return;
    }
    state->displayOverrideMode = mode;
}

void ClearBrainOwnedManualQuery(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->manualQuerySnapshot = {};
    state->manualQueryVisibleUntilSeconds = 0;
}

void SetBrainOwnedPendingTextEntryMode(
    BrainOwnedRuntimeState* state,
    BrainOwnedTextEntryMode mode) {
    if (state == nullptr) {
        return;
    }
    state->pendingTextEntryMode = mode;
}

void ClearBrainOwnedPendingTextEntryMode(BrainOwnedRuntimeState* state) {
    SetBrainOwnedPendingTextEntryMode(state, BrainOwnedTextEntryMode::None);
}

BrainOwnedTextEntryMode ConsumeBrainOwnedPendingTextEntryMode(
    BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return BrainOwnedTextEntryMode::None;
    }
    const auto mode = state->pendingTextEntryMode;
    state->pendingTextEntryMode = BrainOwnedTextEntryMode::None;
    return mode;
}

bool HasBrainOwnedPendingTextEntry(const BrainOwnedRuntimeState& state) {
    return state.pendingTextEntryMode != BrainOwnedTextEntryMode::None;
}

void ShowBrainOwnedManualQueryLine(
    BrainOwnedRuntimeState* state,
    const std::string& line,
    long long visibleUntilSeconds) {
    if (state == nullptr) {
        return;
    }
    state->manualQuerySnapshot = {};
    if (line.empty()) {
        state->manualQueryVisibleUntilSeconds = 0;
        return;
    }
    state->manualQuerySnapshot.visible = true;
    state->manualQuerySnapshot.line = line;
    state->manualQueryVisibleUntilSeconds = visibleUntilSeconds;
}

void CommitBrainOwnedManualQuerySnapshot(
    BrainOwnedRuntimeState* state,
    ManualQuerySnapshot snapshot,
    long long visibleUntilSeconds) {
    if (state == nullptr) {
        return;
    }
    state->manualQuerySnapshot = std::move(snapshot);
    state->manualQueryVisibleUntilSeconds =
        state->manualQuerySnapshot.visible ? visibleUntilSeconds : 0;
}

void ExpireBrainOwnedManualQuery(
    BrainOwnedRuntimeState* state,
    long long nowSeconds) {
    if (state == nullptr || !state->manualQuerySnapshot.visible) {
        return;
    }
    if (nowSeconds < state->manualQueryVisibleUntilSeconds) {
        return;
    }
    ClearBrainOwnedManualQuery(state);
}

void ResetBrainOwnedControllerMessageState(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->controllerMessageState = {};
}

void ClearBrainOwnedControllerMessage(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    state->controllerMessageState.visible = false;
}

void RecallBrainOwnedControllerMessage(BrainOwnedRuntimeState* state) {
    if (state == nullptr ||
        !state->controllerMessageState.cachedAvailable) {
        return;
    }
    state->controllerMessageState.visible = true;
}

void UpdateBrainOwnedControllerMessageState(
    BrainOwnedRuntimeState* state,
    const XPilotPrivateMessageSnapshot& messageSnapshot,
    bool controllerMessageUiEnabled) {
    if (state == nullptr) {
        return;
    }
    if (!controllerMessageUiEnabled || !messageSnapshot.loaded) {
        ResetBrainOwnedControllerMessageState(state);
        return;
    }

    auto& pendingMessage = state->controllerMessageState;
    if (!pendingMessage.primed) {
        pendingMessage.primed = true;
        pendingMessage.lastSequence = messageSnapshot.sequence;
        return;
    }

    if (messageSnapshot.sequence < pendingMessage.lastSequence) {
        pendingMessage.lastSequence = messageSnapshot.sequence;
        pendingMessage.visible = false;
        pendingMessage.cachedAvailable = false;
        pendingMessage.from.clear();
        pendingMessage.body.clear();
        return;
    }

    if (messageSnapshot.sequence == pendingMessage.lastSequence) {
        return;
    }

    pendingMessage.lastSequence = messageSnapshot.sequence;
    if (!messageSnapshot.available) {
        return;
    }

    pendingMessage.cachedAvailable = true;
    pendingMessage.visible = true;
    pendingMessage.from = messageSnapshot.from;
    pendingMessage.body = messageSnapshot.body;
}

BrainOwnedPreflightRouteCacheDecision BeginBrainOwnedPreflightRouteCacheApplication(
    BrainOwnedRuntimeState* state,
    const BrainOwnedPreflightRouteCacheInput& input) {
    BrainOwnedPreflightRouteCacheDecision decision;
    if (state == nullptr || input.planKey.empty()) {
        return decision;
    }
    if (input.planKey == state->preflightRouteCacheAppliedPlanKey) {
        return decision;
    }

    state->preflightRouteCacheAppliedPlanKey = input.planKey;
    decision.shouldClearRouteResolverCache = true;
    if (!input.hasCandidate) {
        decision.logLine =
            "[XVatsim] Preflight route cache unavailable; using normal route preparation.\n";
        return decision;
    }

    decision.shouldValidateCandidate = true;
    return decision;
}

BrainOwnedPreflightRouteCacheValidationDecision
DecideBrainOwnedPreflightRouteCacheValidation(
    const BrainOwnedPreflightRouteCacheValidationInput& input) {
    BrainOwnedPreflightRouteCacheValidationDecision decision;
    if (!input.accepted) {
        decision.logLine =
            "[XVatsim] Preflight route cache rejected: " + input.reason +
            ". Falling back to normal route preparation.\n";
        return decision;
    }

    decision.shouldApplyRouteResolverCache = true;
    return decision;
}

std::string ToString(BrainOwnedCandidateDecision decision) {
    switch (decision) {
        case BrainOwnedCandidateDecision::Pending:
            return "pending";
        case BrainOwnedCandidateDecision::Accepted:
            return "accepted";
        case BrainOwnedCandidateDecision::Rejected:
            return "rejected";
        case BrainOwnedCandidateDecision::NeedsVerification:
            return "needs_verification";
    }
    return "pending";
}

std::string BuildBrainOwnedCandidateCompletionKey(
    std::uint64_t radioBoardHash,
    std::uint64_t routePolygonHash,
    WorkflowStage workflowStage,
    const std::string& currentPolygonKey,
    const RadioReachableControllerCandidate& candidate) {
    std::ostringstream stream;
    stream << radioBoardHash << '|'
           << routePolygonHash << '|'
           << WorkflowStageToken(workflowStage) << '|'
           << currentPolygonKey << '|'
           << candidate.stableKey;
    return stream.str();
}

void RecordBrainOwnedCandidateCompletion(
    BrainOwnedRuntimeState* state,
    BrainOwnedCandidateCompletion completion) {
    if (state == nullptr || completion.stableKey.empty()) {
        return;
    }

    const auto existing = std::find_if(
        state->candidateCompletions.begin(),
        state->candidateCompletions.end(),
        [&completion](const BrainOwnedCandidateCompletion& record) {
            return record.stableKey == completion.stableKey;
        });
    if (existing != state->candidateCompletions.end()) {
        *existing = std::move(completion);
        return;
    }

    state->candidateCompletions.push_back(std::move(completion));
}

BrainOwnedBoardFilterOutput FilterBrainOwnedBoardByAcceptedCompletions(
    const ModuleBoardSnapshot& board,
    const std::vector<BrainOwnedCandidateCompletion>& completions) {
    BrainOwnedBoardFilterOutput output;
    output.board = board;
    output.board.stations.clear();

    for (const auto& station : board.stations) {
        if (!StationRequiresCompletion(station)) {
            output.board.stations.push_back(station);
            continue;
        }

        const auto approved = std::any_of(
            completions.begin(),
            completions.end(),
            [&](const auto& completion) {
                return CompletionApprovesStation(completion, station);
            });
        if (approved) {
            output.board.stations.push_back(station);
        } else {
            ++output.rejectedUnapprovedStations;
        }
    }

    output.board.available =
        output.board.available || !output.board.stations.empty();
    return output;
}

BrainOwnedRadioBoardReuseOutput TryReuseBrainOwnedRadioBoard(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedRadioBoardReuseInput& input) {
    BrainOwnedRadioBoardReuseOutput output;
    output.canReuse =
        state.hasRadioBoard &&
        state.lastControllerGeneration == input.controllerGeneration &&
        (input.nowSeconds - state.lastRadioBoardRefreshSeconds) <
            input.refreshIntervalSeconds;
    if (!output.canReuse) {
        return output;
    }

    output.radioSnapshot = state.radioSnapshot;
    output.reason = "board-unchanged-no-authority-work";
    output.cacheStatus = "clean-runtime-cache-hit";
    return output;
}

BrainOwnedRadioBoardCommitOutput CommitBrainOwnedRadioBoardRefresh(
    BrainOwnedRuntimeState* state,
    const BrainOwnedRadioBoardCommitInput& input) {
    BrainOwnedRadioBoardCommitOutput output;
    output.radioSnapshot = input.radioSnapshot;
    const auto previousRadioSnapshot =
        state != nullptr ? state->radioSnapshot
                         : RadioReachableControllerSnapshot{};
    output.diff =
        DiffRadioReachableSnapshots(
            previousRadioSnapshot,
            input.radioSnapshot);
    output.boardChanged =
        state == nullptr ||
        !state->hasRadioBoard ||
        previousRadioSnapshot.stableHash != input.radioSnapshot.stableHash;
    output.reason =
        output.boardChanged ? "radio-board-changed"
                            : "board-unchanged-no-authority-work";
    output.cacheStatus =
        output.boardChanged ? "clean-runtime-board-refresh"
                            : "radio-board-runtime-idle";

    if (state == nullptr) {
        return output;
    }

    state->hasRadioBoard = true;
    state->lastRadioBoardRefreshSeconds = input.nowSeconds;
    state->lastControllerGeneration = input.controllerGeneration;
    state->transceiverSnapshot = input.transceiverSnapshot;
    state->radioSnapshot = input.radioSnapshot;
    state->radioDiff = output.diff;
    state->lastWakeReason =
        output.boardChanged ? "radio-board-changed" : "radio-board-refresh";
    if (output.boardChanged) {
        state->candidateCompletions.clear();
        state->candidatesComplete = false;
    }
    return output;
}

RadioReachableControllerSnapshot RunBrainOwnedRadioPhaseGate(
    BrainOwnedRuntimeState* state,
    const RadioReachableControllerSnapshot& radioSnapshot,
    WorkflowStage workflowStage,
    const std::string& reason) {
    RadioReachablePhaseGateOptions gateOptions;
    gateOptions.stage = workflowStage;
    gateOptions.reason = reason;
    const auto gatedRadioSnapshot =
        ApplyRadioReachablePhaseGate(radioSnapshot, gateOptions);
    if (state != nullptr) {
        state->gatedRadioSnapshot = gatedRadioSnapshot;
    }
    return gatedRadioSnapshot;
}

void CommitBrainOwnedPublishedRuntime(
    BrainOwnedRuntimeState* state,
    const BrainOwnedPublishedRuntimeInput& input) {
    if (state == nullptr) {
        return;
    }
    state->departureBoardSnapshot = input.departureBoard;
    state->arrivalBoardSnapshot = input.arrivalBoard;
    state->enrouteBoardSnapshot = input.enrouteBoard;
    state->activeBoardSnapshot = input.finalDisplay;
    state->finalDisplaySnapshot = input.finalDisplay;
    state->lastWorkflowStage = input.workflowStage;
    state->lastPlanKey = input.planKey;
    state->lastRadioBoardHash = input.gatedRadioSnapshot.stableHash;
}

BrainOwnedOverlayWakeDecision DecideBrainOwnedOverlayWake(
    const BrainOwnedOverlayWakeInput& input) {
    BrainOwnedOverlayWakeDecision decision;
    decision.xPilotDisconnectedAlert =
        input.sawXPilotConnectedThisFlight &&
        !input.xPilotSession.connected;

    bool autoWake = false;
    if (input.manualQueryVisible || decision.xPilotDisconnectedAlert) {
        autoWake = true;
    } else if (!input.xPilotSession.connected) {
        autoWake = false;
    } else if (input.workflowStage == WorkflowStage::Arrival) {
        autoWake = true;
    } else if (input.workflowStage == WorkflowStage::Enroute) {
        autoWake =
            HasLiveRouteCenters(input.finalDisplay) ||
            input.enrouteInitialHoldActive;
    } else if (input.workflowStage == WorkflowStage::Departure) {
        autoWake = true;
    } else {
        autoWake = true;
    }

    const auto controllerMessageWake =
        input.controllerMessageVisible &&
        input.displayOverrideMode != BrainOwnedDisplayOverrideMode::ForcedSleep;
    const auto criticalWake =
        input.manualQueryVisible ||
        input.textEntryActive ||
        decision.xPilotDisconnectedAlert;

    decision.shouldWake = autoWake;
    if (input.displayOverrideMode == BrainOwnedDisplayOverrideMode::ForcedOpen) {
        decision.shouldWake = true;
    } else if (input.displayOverrideMode ==
               BrainOwnedDisplayOverrideMode::ForcedSleep) {
        decision.shouldWake = false;
    }
    if (criticalWake || controllerMessageWake) {
        decision.shouldWake = true;
    }

    decision.hideUntilXpilotConnect =
        input.displayOverrideMode == BrainOwnedDisplayOverrideMode::Auto &&
        !input.manualQueryVisible &&
        !input.textEntryActive &&
        !input.xPilotSession.connected &&
        !input.sawXPilotConnectedThisFlight;

    decision.reason = "enroute-empty";
    if (input.displayOverrideMode == BrainOwnedDisplayOverrideMode::ForcedOpen) {
        decision.reason = "manual-open";
    } else if (
        input.displayOverrideMode == BrainOwnedDisplayOverrideMode::ForcedSleep) {
        decision.reason = "manual-sleep";
    } else if (input.manualQueryVisible) {
        decision.reason = "manual-query";
    } else if (input.textEntryActive) {
        decision.reason = "text-entry";
    } else if (input.controllerMessageVisible) {
        decision.reason = "controller-message";
    } else if (decision.hideUntilXpilotConnect) {
        decision.reason = "xpilot-waiting";
    } else if (decision.xPilotDisconnectedAlert) {
        decision.reason = "xpilot-disconnected";
    } else if (!input.aircraftState.batteryOn) {
        decision.reason = "battery-off";
    } else if (input.workflowStage == WorkflowStage::Departure) {
        decision.reason = "departure-board";
    } else if (input.workflowStage == WorkflowStage::Arrival) {
        decision.reason = "arrival-board";
    } else if (
        input.workflowStage == WorkflowStage::Enroute &&
        !input.finalDisplay.stations.empty()) {
        decision.reason = "enroute-board";
    } else if (input.workflowStage == WorkflowStage::None) {
        decision.reason = "startup";
    }

    return decision;
}

void ResetBrainOwnedEnrouteInitialHold(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }

    state->enrouteInitialHoldStarted = false;
    state->enrouteInitialHoldUntilSeconds = -1.0;
}

BrainOwnedEnrouteInitialHoldOutput UpdateBrainOwnedEnrouteInitialHold(
    BrainOwnedRuntimeState* state,
    const BrainOwnedEnrouteInitialHoldInput& input) {
    BrainOwnedEnrouteInitialHoldOutput output;
    if (state == nullptr) {
        return output;
    }

    if (input.workflowStage == WorkflowStage::Enroute &&
        !state->enrouteInitialHoldStarted) {
        state->enrouteInitialHoldStarted = true;
        state->enrouteInitialHoldUntilSeconds =
            input.nowSeconds + input.holdSeconds;
        output.started = true;
    }

    output.holdUntilSeconds = state->enrouteInitialHoldUntilSeconds;
    output.active =
        state->enrouteInitialHoldUntilSeconds >= input.nowSeconds;
    return output;
}

BrainOwnedFlightPlanSampleDecision DecideBrainOwnedFlightPlanSample(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedFlightPlanSampleInput& input) {
    BrainOwnedFlightPlanSampleDecision decision;
    decision.reason = "sample-required";

    if (input.flightContextActive &&
        state.hasFlightPlanSnapshot &&
        (input.nowSeconds - state.lastFlightPlanSampleSeconds) <
            input.sampleCadenceSeconds) {
        decision.shouldSample = false;
        decision.cachedSnapshot = state.flightPlanSnapshot;
        decision.reason = "active-flight-context-cadence-hit";
    }

    return decision;
}

void CommitBrainOwnedFlightPlanSample(
    BrainOwnedRuntimeState* state,
    const BrainOwnedFlightPlanSampleCommitInput& input) {
    if (state == nullptr) {
        return;
    }

    state->hasFlightPlanSnapshot = true;
    state->flightPlanSnapshot = input.snapshot;
    state->lastFlightPlanSampleSeconds = input.nowSeconds;
}

void ResetBrainOwnedCruiseTarget(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }

    state->cruiseAltitudeReachedThisFlight = false;
    state->cruiseTargetManualOverride = false;
    state->hasActiveCruiseTarget = false;
    state->activeCruiseTargetFt = 0.0;
    state->cruiseGateSatisfiedSinceSeconds = -1.0;
    state->cruiseTargetSourceKey.clear();
}

BrainOwnedCruiseTargetPlanOutput SyncBrainOwnedCruiseTargetFromNetworkPlan(
    BrainOwnedRuntimeState* state,
    const BrainOwnedCruiseTargetPlanInput& input) {
    BrainOwnedCruiseTargetPlanOutput output;
    if (state == nullptr) {
        return output;
    }

    if (state->hasActiveCruiseTarget ||
        !state->cruiseTargetSourceKey.empty()) {
        if (input.planKey.empty() ||
            (!state->cruiseTargetSourceKey.empty() &&
             state->cruiseTargetSourceKey != input.planKey)) {
            ResetBrainOwnedCruiseTarget(state);
            output.changed = true;
            output.logLine =
                "[XVatsim] Cruise target cleared because source VATSIM flight plan was stale, unmatched, or changed.\n";
            return output;
        }
    }

    if (state->cruiseTargetManualOverride ||
        !input.flightContextActive ||
        input.planKey.empty() ||
        !input.networkPlan.hasFiledCruiseAltitude) {
        return output;
    }

    const auto normalizedAltitudeFt =
        NormalizeCruiseAltitudeFt(input.networkPlan.filedCruiseAltitudeFt);
    if (normalizedAltitudeFt <= 0.0) {
        return output;
    }

    output.changed =
        !state->hasActiveCruiseTarget ||
        state->activeCruiseTargetFt != normalizedAltitudeFt ||
        state->cruiseTargetSourceKey != input.planKey;
    state->activeCruiseTargetFt = normalizedAltitudeFt;
    state->hasActiveCruiseTarget = true;
    state->cruiseTargetSourceKey = input.planKey;
    return output;
}

BrainOwnedCruiseTargetCommandOutput ApplyBrainOwnedCruiseTargetCommand(
    BrainOwnedRuntimeState* state,
    const BrainOwnedCruiseTargetCommandInput& input) {
    BrainOwnedCruiseTargetCommandOutput output;
    if (state == nullptr) {
        return output;
    }

    if (!input.flightContextActive) {
        output.statusLine = "CRUISE unavailable without active flight";
        return output;
    }

    if (input.command == BrainOwnedCruiseTargetCommand::CurrentAltitude &&
        !input.aircraftState.valid) {
        output.statusLine = "CRUISE unavailable without aircraft state";
        return output;
    }

    if (input.planKey.empty()) {
        ResetBrainOwnedCruiseTarget(state);
        output.changed = true;
        output.statusLine = "CRUISE unavailable until VATSIM plan matched";
        return output;
    }

    double normalizedAltitudeFt = 0.0;
    if (input.command == BrainOwnedCruiseTargetCommand::CurrentAltitude) {
        normalizedAltitudeFt =
            NormalizeCruiseAltitudeFt(
                ResolveCruiseComparisonAltitudeFt(
                    input.aircraftState,
                    input.aircraftState.altitudeOperationalFt));
    } else {
        if (!input.networkPlan.hasFiledCruiseAltitude) {
            ResetBrainOwnedCruiseTarget(state);
            output.changed = true;
            output.statusLine = "CRUISE filed altitude unavailable";
            return output;
        }
        normalizedAltitudeFt =
            NormalizeCruiseAltitudeFt(input.networkPlan.filedCruiseAltitudeFt);
    }

    if (normalizedAltitudeFt <= 0.0) {
        ResetBrainOwnedCruiseTarget(state);
        output.changed = true;
        output.statusLine =
            input.command == BrainOwnedCruiseTargetCommand::CurrentAltitude
                ? "CRUISE invalid altitude"
                : "CRUISE invalid filed altitude";
        return output;
    }

    state->activeCruiseTargetFt = normalizedAltitudeFt;
    state->hasActiveCruiseTarget = true;
    state->cruiseTargetManualOverride =
        input.command == BrainOwnedCruiseTargetCommand::CurrentAltitude;
    state->cruiseTargetSourceKey = input.planKey;
    if (input.aircraftState.valid) {
        state->cruiseAltitudeReachedThisFlight =
            AircraftWithinCruiseTargetBand(
                input.aircraftState,
                state->activeCruiseTargetFt,
                input.tuning);
        state->cruiseGateSatisfiedSinceSeconds =
            state->cruiseAltitudeReachedThisFlight
                ? input.nowSeconds
                : -1.0;
    } else {
        state->cruiseAltitudeReachedThisFlight = false;
        state->cruiseGateSatisfiedSinceSeconds = -1.0;
    }

    output.accepted = true;
    output.changed = true;
    output.statusLine =
        "CRUISE target " +
        FormatCruiseTargetText(state->activeCruiseTargetFt) +
        (state->cruiseTargetManualOverride ? " current" : " filed");
    return output;
}

void UpdateBrainOwnedCruiseTargetProgress(
    BrainOwnedRuntimeState* state,
    const BrainOwnedCruiseTargetProgressInput& input) {
    if (state == nullptr ||
        !state->hasActiveCruiseTarget ||
        state->cruiseAltitudeReachedThisFlight) {
        return;
    }

    const auto withinCruiseBand =
        AircraftWithinCruiseTargetBand(
            input.aircraftState,
            state->activeCruiseTargetFt,
            input.tuning);
    const auto verticallyStable =
        std::fabs(input.aircraftState.verticalSpeedFpm) <=
        input.tuning.stableVerticalSpeedFpm;

    if (withinCruiseBand && verticallyStable) {
        if (state->cruiseGateSatisfiedSinceSeconds < 0.0) {
            state->cruiseGateSatisfiedSinceSeconds = input.nowSeconds;
        } else if (
            (input.nowSeconds - state->cruiseGateSatisfiedSinceSeconds) >=
            input.tuning.gateDwellSeconds) {
            state->cruiseAltitudeReachedThisFlight = true;
        }
    } else {
        state->cruiseGateSatisfiedSinceSeconds = -1.0;
    }
}

std::string BuildBrainOwnedCruiseTargetHeaderText(
    const BrainOwnedRuntimeState& state) {
    if (!state.hasActiveCruiseTarget) {
        return {};
    }

    return FormatCruiseTargetText(state.activeCruiseTargetFt);
}

void ResetBrainOwnedWorkflowProgress(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }

    state->departureReleasedThisFlight = false;
    state->arrivalAwakeThisFlight = false;
    state->airborneSinceSeconds = -1.0;
}

void ResetBrainOwnedWorkflowArrivalWake(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }

    state->arrivalAwakeThisFlight = false;
}

workflow::WorkflowState BuildBrainOwnedWorkflowState(
    const BrainOwnedRuntimeState& state) {
    workflow::WorkflowState workflowState;
    workflowState.flightContext = state.flightContext;
    workflowState.departureReleasedThisFlight =
        state.departureReleasedThisFlight;
    workflowState.arrivalAwakeThisFlight = state.arrivalAwakeThisFlight;
    workflowState.airborneSinceSeconds = state.airborneSinceSeconds;
    return workflowState;
}

void CommitBrainOwnedWorkflowState(
    BrainOwnedRuntimeState* state,
    const workflow::WorkflowState& workflowState) {
    if (state == nullptr) {
        return;
    }

    state->departureReleasedThisFlight =
        workflowState.departureReleasedThisFlight;
    state->arrivalAwakeThisFlight = workflowState.arrivalAwakeThisFlight;
    state->airborneSinceSeconds = workflowState.airborneSinceSeconds;
    state->flightContext = workflowState.flightContext;
}

BrainOwnedWorkflowSelectionOutput ResolveBrainOwnedWorkflowSelection(
    BrainOwnedRuntimeState* state,
    const BrainOwnedWorkflowSelectionInput& input) {
    BrainOwnedWorkflowSelectionOutput output;
    if (state == nullptr) {
        return output;
    }

    BrainOwnedControllerRelevanceInputRequest provisionalRequest;
    provisionalRequest.workflowStage = WorkflowStage::None;
    provisionalRequest.radioSnapshot = input.radioSnapshot;
    provisionalRequest.departureIcao = input.departureIcao;
    provisionalRequest.arrivalIcao = input.arrivalIcao;
    provisionalRequest.radios = input.radios;

    const auto provisionalInput =
        BuildBrainOwnedControllerRelevanceInput(
            *state,
            provisionalRequest);
    const auto provisionalRelevance =
        RunBrainControllerRelevanceWorker(provisionalInput);

    output.departureBoard = provisionalRelevance.departureBoard;
    output.arrivalBoard = provisionalRelevance.arrivalBoard;
    output.enrouteBoard = provisionalRelevance.enrouteBoard;

    auto workflowState = BuildBrainOwnedWorkflowState(*state);
    output.decision = workflow::ResolveWorkflowStage(
        input.aircraft,
        input.radios,
        false,
        false,
        output.departureBoard,
        output.enrouteBoard,
        input.nowSeconds,
        &workflowState,
        input.tuning);

    CommitBrainOwnedWorkflowState(state, workflowState);
    return output;
}

void ApplyBrainOwnedWorkflowRecoveryStage(
    BrainOwnedRuntimeState* state,
    WorkflowStage stage,
    double nowSeconds) {
    if (state == nullptr) {
        return;
    }

    switch (stage) {
        case WorkflowStage::Departure:
            state->departureReleasedThisFlight = false;
            state->arrivalAwakeThisFlight = false;
            state->airborneSinceSeconds = -1.0;
            break;
        case WorkflowStage::Enroute:
            state->departureReleasedThisFlight = true;
            state->arrivalAwakeThisFlight = false;
            if (state->airborneSinceSeconds < 0.0) {
                state->airborneSinceSeconds = nowSeconds;
            }
            break;
        case WorkflowStage::Arrival:
            state->departureReleasedThisFlight = true;
            state->arrivalAwakeThisFlight = true;
            if (state->airborneSinceSeconds < 0.0) {
                state->airborneSinceSeconds = nowSeconds;
            }
            break;
        case WorkflowStage::None:
        default:
            break;
    }
}

void SetBrainOwnedXPilotConnectedSeen(
    BrainOwnedRuntimeState* state,
    bool seen) {
    if (state == nullptr) {
        return;
    }

    state->sawXPilotConnectedThisFlight = seen;
}

void MarkBrainOwnedXPilotConnectedIfConnected(
    BrainOwnedRuntimeState* state,
    const XPilotSessionSnapshot& xPilotSession) {
    if (state == nullptr || !xPilotSession.connected) {
        return;
    }

    state->sawXPilotConnectedThisFlight = true;
}

BrainOwnedStandbyAssistPlanOutput BuildBrainOwnedStandbyAssistPlan(
    const BrainOwnedStandbyAssistPlanInput& input) {
    BrainOwnedStandbyAssistPlanOutput output;
    output.workflowStage = input.workflowStage;
    output.board = input.board;
    for (auto& station : output.board.stations) {
        station.standby = false;
    }

    if (input.workflowStage != WorkflowStage::Departure &&
        input.workflowStage != WorkflowStage::Arrival &&
        input.workflowStage != WorkflowStage::Enroute) {
        return output;
    }
    if (input.planKey.empty() || !input.radios.valid) {
        return output;
    }

    std::vector<std::size_t> orderedEligibleIndices;
    orderedEligibleIndices.reserve(output.board.stations.size());
    for (std::size_t index = 0; index < output.board.stations.size(); ++index) {
        const auto& station = output.board.stations[index];
        if (station.offline ||
            !IsStandbyEligibleRole(station.role) ||
            station.frequency.empty() ||
            IsBlockedControllerFrequency(station.frequency)) {
            continue;
        }
        orderedEligibleIndices.push_back(index);
    }

    if (input.workflowStage != WorkflowStage::Enroute) {
        std::stable_sort(
            orderedEligibleIndices.begin(),
            orderedEligibleIndices.end(),
            [&](std::size_t leftIndex, std::size_t rightIndex) {
                const auto& left = output.board.stations[leftIndex];
                const auto& right = output.board.stations[rightIndex];
                const auto leftRank =
                    StandbyRoleRank(input.workflowStage, left.role);
                const auto rightRank =
                    StandbyRoleRank(input.workflowStage, right.role);
                if (leftRank != rightRank) {
                    return leftRank < rightRank;
                }
                if (left.frequency != right.frequency) {
                    return left.frequency < right.frequency;
                }
                return left.callsign < right.callsign;
            });
    }

    if (orderedEligibleIndices.empty()) {
        return output;
    }

    std::size_t targetPosition = 0;
    for (std::size_t position = 0; position < orderedEligibleIndices.size();
         ++position) {
        if (output.board.stations[orderedEligibleIndices[position]].tuned) {
            targetPosition = position + 1;
        }
    }

    while (targetPosition < orderedEligibleIndices.size() &&
           output.board.stations[orderedEligibleIndices[targetPosition]].tuned) {
        ++targetPosition;
    }
    if (targetPosition >= orderedEligibleIndices.size()) {
        return output;
    }

    const auto targetIndex = orderedEligibleIndices[targetPosition];
    const auto& targetStation = output.board.stations[targetIndex];
    if (targetStation.frequency.empty()) {
        return output;
    }

    output.hasTarget = true;
    output.targetStationIndex = targetIndex;
    output.targetFrequency = targetStation.frequency;
    output.latchKey =
        StandbyAssistWorkflowKey(
            input.workflowStage,
            input.planKey,
            input.radios,
            targetStation);
    const auto normalizedTarget = NormalizeFrequency(targetStation.frequency);
    output.targetAlreadyInCom1Standby =
        !normalizedTarget.empty() &&
        NormalizeFrequency(input.radios.com1StandbyFrequency) ==
            normalizedTarget;
    return output;
}

BrainOwnedStandbyAssistSideEffectDecision
DecideBrainOwnedStandbyAssistSideEffect(
    BrainOwnedRuntimeState* state,
    const BrainOwnedStandbyAssistPlanOutput& plan,
    bool standbyAssistEnabled) {
    BrainOwnedStandbyAssistSideEffectDecision decision;
    if (state == nullptr || !plan.hasTarget) {
        ResetBrainOwnedStandbyAssistLatch(state);
        return decision;
    }

    if (plan.latchKey != state->standbyAssistLatchKey) {
        state->standbyAssistLatchKey = plan.latchKey;
        state->standbyAssistWriteConsumed = false;
    }

    if (!standbyAssistEnabled) {
        return decision;
    }

    decision.targetFrequency = plan.targetFrequency;
    decision.standbyLoaded = plan.targetAlreadyInCom1Standby;
    decision.shouldWriteCom1Standby =
        !decision.standbyLoaded && !state->standbyAssistWriteConsumed;
    state->standbyAssistWriteConsumed = true;
    return decision;
}

ModuleBoardSnapshot ApplyBrainOwnedStandbyAssistResult(
    const BrainOwnedStandbyAssistPlanOutput& plan,
    bool standbyLoaded) {
    auto board = plan.board;
    if (!plan.hasTarget || plan.targetStationIndex >= board.stations.size()) {
        return board;
    }
    if (plan.workflowStage == WorkflowStage::Enroute) {
        return board;
    }

    auto& targetStation = board.stations[plan.targetStationIndex];
    if (standbyLoaded) {
        targetStation.standby = true;
    } else {
        targetStation.next = true;
    }
    return board;
}

BrainOwnedPublisherInput BuildBrainOwnedPublisherInputFromFacts(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedPublisherFactInput& facts) {
    BrainOwnedPublisherInput input;
    input.workflowStage = facts.workflowStage;
    input.routeProgressDistanceNm = state.routeProgressDistanceNm;
    input.currentPolygonKey = state.currentPolygonKey;
    input.nextPolygonKey = state.nextPolygonKey;
    input.arrivalPolygonKey = state.arrivalPolygonKey;
    input.departureBoard = facts.departureBoard;
    input.arrivalBoard = facts.arrivalBoard;
    input.enrouteBoard = facts.enrouteBoard;
    input.completions = facts.completions;
    input.hasDepartureCtafStation =
        BuildCtafStationFromLookupFact(
            facts.departureCtaf,
            facts.radios,
            &input.departureCtafStation);
    input.hasArrivalCtafStation =
        BuildCtafStationFromLookupFact(
            facts.arrivalCtaf,
            facts.radios,
            &input.arrivalCtafStation);
    input.verificationPending = facts.verificationPending;
    input.publishReason = facts.publishReason;
    return input;
}

void MarkBrainOwnedDisplayedCompletionsFromFinalDisplay(
    BrainOwnedRuntimeState* state,
    const ModuleBoardSnapshot& finalDisplay) {
    if (state == nullptr) {
        return;
    }

    for (auto& completion : state->candidateCompletions) {
        if (completion.decision != BrainOwnedCandidateDecision::Accepted) {
            completion.displayed = false;
            continue;
        }

        completion.displayed =
            CompletionDisplayedInFinalBoard(finalDisplay, completion);
    }
}

BrainOwnedPublisherOutput RunBrainOwnedPublisher(
    BrainOwnedRuntimeState* state,
    const BrainOwnedPublisherInput& input) {
    BrainOwnedPublisherOutput output;

    const auto departureFilter =
        FilterBrainOwnedBoardByAcceptedCompletions(
            input.departureBoard,
            input.completions);
    output.departureBoard = departureFilter.board;
    output.rejectedUnapprovedStations +=
        departureFilter.rejectedUnapprovedStations;

    const auto arrivalFilter =
        FilterBrainOwnedBoardByAcceptedCompletions(
            input.arrivalBoard,
            input.completions);
    output.arrivalBoard = arrivalFilter.board;
    output.rejectedUnapprovedStations +=
        arrivalFilter.rejectedUnapprovedStations;

    const auto enrouteFilter =
        FilterBrainOwnedBoardByAcceptedCompletions(
            input.enrouteBoard,
            input.completions);
    output.enrouteBoard = enrouteFilter.board;
    output.rejectedUnapprovedStations +=
        enrouteFilter.rejectedUnapprovedStations;

    RemoveCtafStations(&output.departureBoard);
    RemoveCtafStations(&output.arrivalBoard);

    std::unordered_set<std::string> departureCtafKeys;
    std::unordered_set<std::string> arrivalCtafKeys;
    if (input.hasDepartureCtafStation) {
        AppendUniqueStation(
            input.departureCtafStation,
            &output.departureBoard,
            &departureCtafKeys);
    }
    if (input.hasArrivalCtafStation) {
        AppendUniqueStation(
            input.arrivalCtafStation,
            &output.arrivalBoard,
            &arrivalCtafKeys);
    }

    BrainDisplayIntentInput displayIntentInput;
    displayIntentInput.workflowStage = input.workflowStage;
    displayIntentInput.routeProgressDistanceNm =
        input.routeProgressDistanceNm;
    displayIntentInput.currentPolygonKey = input.currentPolygonKey;
    displayIntentInput.nextPolygonKey = input.nextPolygonKey;
    displayIntentInput.arrivalPolygonKey = input.arrivalPolygonKey;
    displayIntentInput.departureBoard = output.departureBoard;
    displayIntentInput.arrivalBoard = output.arrivalBoard;
    displayIntentInput.enrouteBoard = output.enrouteBoard;

    output.displayIntent = RunBrainDisplayIntentWorker(displayIntentInput);
    output.departureBoard = output.displayIntent.departureBoard;
    output.arrivalBoard = output.displayIntent.arrivalBoard;
    output.enrouteBoard = output.displayIntent.enrouteBoard;
    output.finalDisplay = output.displayIntent.finalDisplay;

    if (state != nullptr) {
        PhaseSnapshotPublishRequest publishRequest;
        publishRequest.stage = input.workflowStage;
        publishRequest.candidate = output.finalDisplay;
        publishRequest.verificationPending = input.verificationPending;
        publishRequest.reason = input.publishReason;
        output.phasePublish =
            PublishPhaseSnapshot(
                &state->phaseSnapshotPublisherState,
                publishRequest);
        output.finalDisplay = output.phasePublish.snapshot;
        output.phasePublisherStateSummary =
            PhaseSnapshotPublisherStateSummary(
                state->phaseSnapshotPublisherState);
        state->lastDisplayIntentHash = output.displayIntent.stableHash;
        MarkBrainOwnedDisplayedCompletionsFromFinalDisplay(
            state,
            output.finalDisplay);
    }

    return output;
}

void CommitBrainOwnedPublishedRuntimeFromPublisherOutput(
    BrainOwnedRuntimeState* state,
    WorkflowStage workflowStage,
    const std::string& planKey,
    const RadioReachableControllerSnapshot& gatedRadioSnapshot,
    const BrainOwnedPublisherOutput& publisherOutput,
    const ModuleBoardSnapshot& finalDisplay) {
    BrainOwnedPublishedRuntimeInput input;
    input.workflowStage = workflowStage;
    input.planKey = planKey;
    input.gatedRadioSnapshot = gatedRadioSnapshot;
    input.departureBoard = publisherOutput.departureBoard;
    input.arrivalBoard = publisherOutput.arrivalBoard;
    input.enrouteBoard = publisherOutput.enrouteBoard;
    input.finalDisplay = finalDisplay;
    CommitBrainOwnedPublishedRuntime(state, input);
}

bool BrainOwnedCandidatesCompleteForCurrentBoard(
    const BrainOwnedRuntimeState& state,
    const RadioReachableControllerSnapshot& radioSnapshot,
    WorkflowStage workflowStage,
    const std::string& currentPolygonKey) {
    if (!radioSnapshot.available || radioSnapshot.stale) {
        return false;
    }

    std::unordered_set<std::string> completedKeys;
    completedKeys.reserve(state.candidateCompletions.size());
    for (const auto& completion : state.candidateCompletions) {
        if (completion.radioBoardHash != radioSnapshot.stableHash ||
            completion.routePolygonHash != state.routePolygonHash ||
            completion.workflowStage != workflowStage ||
            completion.currentPolygonKey != currentPolygonKey ||
            completion.decision == BrainOwnedCandidateDecision::Pending) {
            continue;
        }
        completedKeys.insert(completion.stableKey);
    }

    for (const auto& candidate : radioSnapshot.candidates) {
        const auto key = BuildBrainOwnedCandidateCompletionKey(
            radioSnapshot.stableHash,
            state.routePolygonHash,
            workflowStage,
            currentPolygonKey,
            candidate);
        if (completedKeys.find(key) == completedKeys.end()) {
            return false;
        }
    }

    return true;
}

std::string BrainOwnedRuntimeStateSummary(const BrainOwnedRuntimeState& state) {
    std::ostringstream stream;
    stream << "brainOwned"
           << " route=" << (state.hasRoutePolygonSnapshot ? 1 : 0)
           << " routeHash=" << state.routePolygonHash
           << " radio=" << (state.hasRadioBoard ? 1 : 0)
           << " radioHash=" << state.radioSnapshot.stableHash
           << " stage=" << WorkflowStageToken(state.lastWorkflowStage)
           << " polygon=" << state.currentPolygonKey
           << " completions=" << state.candidateCompletions.size()
           << " candidatesComplete=" << (state.candidatesComplete ? 1 : 0)
           << " wake=" << state.lastWakeReason
           << " idle=" << state.lastIdleReason
           << " heavyRequested=" << (state.heavyFallbackRequested ? 1 : 0)
           << " heavyRunning=" << (state.heavyFallbackRunning ? 1 : 0);
    return stream.str();
}

}  // namespace xvatsim::brain
