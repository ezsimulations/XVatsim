#include "XVatsim/brain/BrainOwnedRuntime.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace xvatsim::brain {
namespace {

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

}  // namespace

void ResetBrainOwnedRuntimeState(BrainOwnedRuntimeState* state) {
    if (state == nullptr) {
        return;
    }
    *state = {};
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
