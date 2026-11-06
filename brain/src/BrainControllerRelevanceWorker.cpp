#include "XVatsim/brain/BrainOwnedWorkerTypes.h"
#include "XVatsim/brain/BrainControllerDecision.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace xvatsim::brain {
namespace {

constexpr const char* kCenterTunedOffRouteNotRouteOwnedPolicy =
    "center-tuned-off-route-not-route-owned";
constexpr double kEarthRadiusNm = 3440.065;

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

void HashCombine(std::uint64_t* seed, std::uint64_t value) {
    if (seed == nullptr) {
        return;
    }
    *seed ^= value + 0x9e3779b97f4a7c15ULL + (*seed << 6) + (*seed >> 2);
}

void HashCombine(std::uint64_t* seed, const std::string& value) {
    HashCombine(seed, static_cast<std::uint64_t>(value.size()));
    for (const auto ch : value) {
        HashCombine(
            seed,
            static_cast<std::uint64_t>(static_cast<unsigned char>(ch)));
    }
}

std::uint64_t HashRadioTuningIdentity(const RadioStateSnapshot& radios) {
    std::uint64_t hash = 1469598103934665603ULL;
    HashCombine(&hash, static_cast<std::uint64_t>(radios.valid ? 1u : 0u));
    HashCombine(&hash, NormalizeFrequency(radios.com1ActiveFrequency));
    HashCombine(&hash, NormalizeFrequency(radios.com2ActiveFrequency));
    HashCombine(&hash, NormalizeFrequency(radios.com1StandbyFrequency));
    return hash;
}

std::uint64_t HashTerminalRelevancePolicy(
    double transmitterRadiusNm,
    bool hasDepartureCoordinates,
    double departureLatitudeDeg,
    double departureLongitudeDeg,
    bool hasArrivalCoordinates,
    double arrivalLatitudeDeg,
    double arrivalLongitudeDeg) {
    std::uint64_t hash = 1469598103934665603ULL;
    HashCombine(
        &hash,
        static_cast<std::uint64_t>(std::llround(transmitterRadiusNm * 1000.0)));
    HashCombine(
        &hash,
        static_cast<std::uint64_t>(hasDepartureCoordinates ? 1u : 0u));
    if (hasDepartureCoordinates) {
        HashCombine(
            &hash,
            static_cast<std::uint64_t>(
                std::llround(departureLatitudeDeg * 100000.0)));
        HashCombine(
            &hash,
            static_cast<std::uint64_t>(
                std::llround(departureLongitudeDeg * 100000.0)));
    }
    HashCombine(
        &hash,
        static_cast<std::uint64_t>(hasArrivalCoordinates ? 1u : 0u));
    if (hasArrivalCoordinates) {
        HashCombine(
            &hash,
            static_cast<std::uint64_t>(
                std::llround(arrivalLatitudeDeg * 100000.0)));
        HashCombine(
            &hash,
            static_cast<std::uint64_t>(
                std::llround(arrivalLongitudeDeg * 100000.0)));
    }
    return hash;
}

std::string NormalizeIcaoInput(std::string value) {
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

constexpr const char* kAuthorityRelevancePreviewSurvivor =
    "brain-preview-authority-relevance-survivor";
constexpr const char* kAuthorityRelevancePreviewRejectedNonActionable =
    "brain-preview-authority-relevance-rejected-non-actionable";
constexpr const char* kAuthorityRelevancePreviewRejectedAtis =
    "brain-preview-authority-relevance-rejected-atis";
constexpr const char* kAuthorityRelevancePreviewRejectedGuardFrequency =
    "brain-preview-authority-relevance-rejected-guard-frequency";
constexpr const char* kAuthorityRelevancePreviewRejectedNotAuthorityCandidate =
    "brain-preview-authority-relevance-rejected-not-authority-candidate";
constexpr const char* kAuthorityRelevancePreviewRejectedUnmappedController =
    "brain-preview-authority-relevance-rejected-unmapped-controller";
constexpr const char* kAuthorityRelevancePreviewRejectedRouteScope =
    "brain-preview-authority-relevance-rejected-route-scope";
constexpr const char* kAuthorityRelevancePreviewRejectedActiveNotRelevant =
    "brain-preview-authority-relevance-rejected-active-not-relevant";
constexpr const char* kAuthorityRelevancePreviewRejectedNoUsableProof =
    "brain-preview-authority-relevance-rejected-no-usable-proof";

bool HasEvidenceReason(
    const std::vector<std::string>& reasons,
    const std::string& reason) {
    return std::find(reasons.begin(), reasons.end(), reason) != reasons.end();
}

bool RelevantAuthorityMatchesActiveEvidence(
    const RelevantAuthoritySnapshot& relevant,
    const AuthorityPolygonEvidenceSnapshot& activeEvidence) {
    return relevant.callsign == activeEvidence.callsign &&
           relevant.authorityId == activeEvidence.authorityId &&
           relevant.polygonId == activeEvidence.polygonId &&
           relevant.polygonKey == activeEvidence.polygonKey &&
           relevant.matchedPattern == activeEvidence.matchedPattern;
}

bool PreviewDecisionMatchesRelevantAuthority(
    const BrainAuthorityRelevancePreviewDecision& decision,
    const RelevantAuthoritySnapshot& relevant) {
    return decision.callsign == relevant.callsign &&
           decision.authorityId == relevant.authorityId &&
           decision.polygonId == relevant.polygonId &&
           decision.polygonKey == relevant.polygonKey &&
           decision.matchedPattern == relevant.matchedPattern;
}

const std::vector<RelevantAuthoritySnapshot>& CurrentRelevantAuthorities(
    const AuthorityRelevanceSnapshot& snapshot) {
    return snapshot.relevantAuthorities;
}

bool CallsignHasRelevantAuthority(
    const AuthorityRelevanceSnapshot& snapshot,
    const std::string& callsign) {
    const auto normalizedCallsign = NormalizeCallsign(callsign);
    const auto& relevantAuthorities = CurrentRelevantAuthorities(snapshot);
    return std::any_of(
        relevantAuthorities.begin(),
        relevantAuthorities.end(),
        [&](const auto& relevant) {
            return NormalizeCallsign(relevant.callsign) == normalizedCallsign;
        });
}

BrainAuthorityRelevancePreviewDecision BuildControllerAuthorityRelevanceRejection(
    const AuthorityControllerEvidenceSnapshot& controller,
    const std::string& decision,
    const std::string& reason) {
    BrainAuthorityRelevancePreviewDecision previewDecision;
    previewDecision.evidenceKind = "controller";
    previewDecision.callsign = controller.callsign;
    previewDecision.decision = decision;
    previewDecision.reason = reason;
    return previewDecision;
}

BrainAuthorityRelevancePreviewDecision BuildPolygonAuthorityRelevanceDecision(
    const std::string& evidenceKind,
    const AuthorityPolygonEvidenceSnapshot& polygon,
    const std::string& decision,
    const std::string& reason) {
    BrainAuthorityRelevancePreviewDecision previewDecision;
    previewDecision.evidenceKind = evidenceKind;
    previewDecision.callsign = polygon.callsign;
    previewDecision.authorityId = polygon.authorityId;
    previewDecision.polygonId = polygon.polygonId;
    previewDecision.polygonKey = polygon.polygonKey;
    previewDecision.matchedPattern = polygon.matchedPattern;
    previewDecision.proofSource = polygon.activeProofSource;
    previewDecision.decision = decision;
    previewDecision.reason = reason;
    return previewDecision;
}

std::string ControllerAuthorityRelevanceRejectDecision(
    const AuthorityControllerEvidenceSnapshot& controller,
    std::string* outReason) {
    if (!controller.actionable) {
        if (outReason != nullptr) {
            *outReason = "controller-not-actionable";
        }
        return kAuthorityRelevancePreviewRejectedNonActionable;
    }
    if (controller.atis) {
        if (outReason != nullptr) {
            *outReason = "controller-atis";
        }
        return kAuthorityRelevancePreviewRejectedAtis;
    }
    if (controller.guardFrequency) {
        if (outReason != nullptr) {
            *outReason = "guard-frequency";
        }
        return kAuthorityRelevancePreviewRejectedGuardFrequency;
    }
    if (HasEvidenceReason(controller.evidenceReasons, "not-authority-candidate")) {
        if (outReason != nullptr) {
            *outReason = "not-authority-candidate";
        }
        return kAuthorityRelevancePreviewRejectedNotAuthorityCandidate;
    }
    if (HasEvidenceReason(controller.evidenceReasons, "unmapped-controller")) {
        if (outReason != nullptr) {
            *outReason = "unmapped-controller";
        }
        return kAuthorityRelevancePreviewRejectedUnmappedController;
    }
    if (HasEvidenceReason(
            controller.evidenceReasons,
            "no-route-scoped-authority-decision") ||
        HasEvidenceReason(
            controller.evidenceReasons,
            "airport-local-no-authority-decision")) {
        if (outReason != nullptr) {
            *outReason = "no-route-scoped-authority-decision";
        }
        return kAuthorityRelevancePreviewRejectedRouteScope;
    }
    if (!controller.authorityDecisions.empty()) {
        if (outReason != nullptr) {
            *outReason = "authority-decision-not-relevant";
        }
        return kAuthorityRelevancePreviewRejectedRouteScope;
    }
    if (outReason != nullptr) {
        *outReason = "no-usable-proof";
    }
    return kAuthorityRelevancePreviewRejectedNoUsableProof;
}

std::string ActivePolygonAuthorityRelevanceRejectDecision(
    const AuthorityPolygonEvidenceSnapshot& activePolygon,
    std::string* outReason) {
    const auto reason =
        activePolygon.filteredReason.empty()
            ? std::string("active-not-relevant")
            : activePolygon.filteredReason;
    if (outReason != nullptr) {
        *outReason = reason;
    }
    if (reason == "route-key-filtered" ||
        (!activePolygon.routeKeyCompatible &&
         activePolygon.filteredReason != "active-not-relevant")) {
        return kAuthorityRelevancePreviewRejectedRouteScope;
    }
    if (reason == "duplicate-active-key") {
        return kAuthorityRelevancePreviewRejectedNoUsableProof;
    }
    return kAuthorityRelevancePreviewRejectedActiveNotRelevant;
}

BrainAuthorityRelevanceDecisionPreview
BuildBrainAuthorityRelevanceDecisionPreviewInternal(
    const AuthorityRelevanceSnapshot& authorityRelevance) {
    BrainAuthorityRelevanceDecisionPreview preview;
    const auto& relevantAuthorities =
        CurrentRelevantAuthorities(authorityRelevance);
    preview.summary.authority =
        authorityRelevance.liveRelevantAuthoritiesBrainOwned
            ? "brain-evidence"
            : "preview-only";
    preview.summary.sourceControllerCount =
        authorityRelevance.evidence.source.sourceControllerCount;
    preview.summary.evidenceControllerCount =
        static_cast<int>(authorityRelevance.evidence.controllerEvidence.size());
    preview.summary.relevantAuthorityCount =
        static_cast<int>(relevantAuthorities.size());
    preview.summary.droppedBeforeBrainControllers =
        authorityRelevance.droppedBeforeBrainControllers;
    preview.summary.liveRelevantAuthoritiesBrainOwned =
        authorityRelevance.liveRelevantAuthoritiesBrainOwned;

    for (const auto& controller :
         authorityRelevance.evidence.controllerEvidence) {
        if (CallsignHasRelevantAuthority(authorityRelevance, controller.callsign)) {
            continue;
        }
        std::string reason;
        const auto decision =
            ControllerAuthorityRelevanceRejectDecision(controller, &reason);
        preview.decisions.push_back(
            BuildControllerAuthorityRelevanceRejection(
                controller,
                decision,
                reason));
    }

    for (const auto& polygon : authorityRelevance.evidence.polygonEvidence) {
        if (polygon.queryScopeOutReason.empty() ||
            polygon.relevant) {
            continue;
        }
        preview.decisions.push_back(
            BuildPolygonAuthorityRelevanceDecision(
                "route-scope-polygon",
                polygon,
                kAuthorityRelevancePreviewRejectedRouteScope,
                polygon.queryScopeOutReason));
    }

    for (const auto& activePolygon :
         authorityRelevance.evidence.activePolygonEvidence) {
        if (activePolygon.relevant) {
            auto decision = BuildPolygonAuthorityRelevanceDecision(
                "active-polygon",
                activePolygon,
                kAuthorityRelevancePreviewSurvivor,
                "canonical-live-authority-relevant");
            decision.matchesOldSurvivor =
                std::any_of(
                    relevantAuthorities.begin(),
                    relevantAuthorities.end(),
                    [&](const auto& relevant) {
                        return RelevantAuthorityMatchesActiveEvidence(
                            relevant,
                            activePolygon);
                    });
            preview.decisions.push_back(std::move(decision));
            continue;
        }

        std::string reason;
        const auto decision =
            ActivePolygonAuthorityRelevanceRejectDecision(activePolygon, &reason);
        preview.decisions.push_back(
            BuildPolygonAuthorityRelevanceDecision(
                "active-polygon",
                activePolygon,
                decision,
                reason));
    }

    for (const auto& decision : preview.decisions) {
        if (decision.decision == kAuthorityRelevancePreviewSurvivor) {
            ++preview.summary.previewSurvivorCount;
        } else {
            ++preview.summary.previewRejectedCount;
        }
    }

    for (const auto& relevant : relevantAuthorities) {
        const auto matched = std::any_of(
            preview.decisions.begin(),
            preview.decisions.end(),
            [&](const auto& decision) {
                return decision.decision == kAuthorityRelevancePreviewSurvivor &&
                       PreviewDecisionMatchesRelevantAuthority(decision, relevant);
            });
        if (!matched) {
            ++preview.summary.oldSurvivorMismatchCount;
        }
    }

    for (const auto& decision : preview.decisions) {
        if (decision.decision == kAuthorityRelevancePreviewSurvivor &&
            !decision.matchesOldSurvivor) {
            ++preview.summary.oldSurvivorMismatchCount;
        }
    }

    return preview;
}

}  // namespace

BrainAuthorityRelevanceDecisionPreview
BuildBrainAuthorityRelevanceDecisionPreview(
    const AuthorityRelevanceSnapshot& authorityRelevance) {
    return BuildBrainAuthorityRelevanceDecisionPreviewInternal(authorityRelevance);
}

LiveAuthorityMapSnapshot BuildBrainOwnedLiveAuthorityMapSnapshot(
    LiveAuthorityMapSnapshot liveMap,
    std::vector<std::string>* decisionDiagnostics) {
    if (!liveMap.available || liveMap.brainDecisionsApplied) {
        return liveMap;
    }

    liveMap.conflictedPolygonIds.clear();
    std::unordered_map<
        std::string,
        std::vector<LiveAuthorityAssignmentSnapshot*>>
        assignmentsByPolygon;
    for (auto& assignment : liveMap.assignments) {
        assignment.state = assignment.candidateState;
        if (IsActiveLiveAuthorityState(assignment.candidateState) &&
            !assignment.canonicalPolygonId.empty()) {
            assignmentsByPolygon[assignment.canonicalPolygonId].push_back(
                &assignment);
        }
    }
    for (const auto& [polygonId, assignments] : assignmentsByPolygon) {
        std::unordered_set<std::string> callsigns;
        for (const auto* assignment : assignments) {
            if (assignment != nullptr) {
                callsigns.insert(NormalizeCallsign(assignment->callsign));
            }
        }
        if (callsigns.size() <= 1) {
            continue;
        }
        const auto allExplicitTerminalPositions =
            !assignments.empty() &&
            std::all_of(
                assignments.begin(),
                assignments.end(),
                [](const auto* assignment) {
                    return assignment != nullptr &&
                           assignment->candidateState ==
                               LiveAuthorityAssignmentState::ActiveExact &&
                           assignment->kind ==
                               AuthorityRelevanceKind::Terminal &&
                           assignment->explicitTerminalPosition;
                });
        const auto hasDirect = std::any_of(
            assignments.begin(), assignments.end(), [](const auto* assignment) {
                return assignment != nullptr &&
                       assignment->candidateState ==
                           LiveAuthorityAssignmentState::ActiveExact;
            });
        const auto hasParent = std::any_of(
            assignments.begin(), assignments.end(), [](const auto* assignment) {
                return assignment != nullptr &&
                       assignment->candidateState ==
                           LiveAuthorityAssignmentState::ActiveParent;
            });
        const auto directParentHierarchy =
            hasDirect && hasParent &&
            std::all_of(
                assignments.begin(),
                assignments.end(),
                [](const auto* assignment) {
                    return assignment != nullptr &&
                           (assignment->candidateState ==
                                LiveAuthorityAssignmentState::ActiveExact ||
                            assignment->candidateState ==
                                LiveAuthorityAssignmentState::ActiveParent);
                });
        if (allExplicitTerminalPositions || directParentHierarchy) {
            continue;
        }
        liveMap.conflictedPolygonIds.push_back(polygonId);
        if (decisionDiagnostics != nullptr) {
            decisionDiagnostics->push_back(
                "conflict:" + polygonId + ":controllers=" +
                std::to_string(callsigns.size()));
        }
        for (auto* assignment : assignments) {
            if (assignment != nullptr) {
                assignment->state = LiveAuthorityAssignmentState::Conflict;
                assignment->aircraftInside = false;
                assignment->routeIntersects = false;
                assignment->routeEntryDistanceNm = 0.0;
            }
        }
    }
    std::sort(
        liveMap.conflictedPolygonIds.begin(),
        liveMap.conflictedPolygonIds.end());
    liveMap.conflictedPolygonIds.erase(
        std::unique(
            liveMap.conflictedPolygonIds.begin(),
            liveMap.conflictedPolygonIds.end()),
        liveMap.conflictedPolygonIds.end());
    if (decisionDiagnostics != nullptr) {
        std::sort(decisionDiagnostics->begin(), decisionDiagnostics->end());
        decisionDiagnostics->erase(
            std::unique(
                decisionDiagnostics->begin(), decisionDiagnostics->end()),
            decisionDiagnostics->end());
    }

    const auto statePriority = [](LiveAuthorityAssignmentState state) {
        switch (state) {
            case LiveAuthorityAssignmentState::Conflict:
                return 5;
            case LiveAuthorityAssignmentState::ActiveExact:
                return 4;
            case LiveAuthorityAssignmentState::ActiveDynamic:
                return 3;
            case LiveAuthorityAssignmentState::ActiveParent:
                return 2;
            case LiveAuthorityAssignmentState::Stale:
                return 1;
            case LiveAuthorityAssignmentState::Unmapped:
            case LiveAuthorityAssignmentState::Offline:
                return 0;
        }
        return 0;
    };
    std::unordered_map<std::string, LiveAuthorityAssignmentState>
        stateByPolygon;
    std::unordered_map<std::string, std::vector<std::string>> ownersByPolygon;
    for (const auto& assignment : liveMap.assignments) {
        if (assignment.canonicalPolygonId.empty()) {
            continue;
        }
        ownersByPolygon[assignment.canonicalPolygonId].push_back(
            assignment.callsign);
        const auto found = stateByPolygon.find(assignment.canonicalPolygonId);
        if (found == stateByPolygon.end() ||
            statePriority(assignment.state) > statePriority(found->second)) {
            stateByPolygon[assignment.canonicalPolygonId] = assignment.state;
        }
    }
    for (auto& polygon : liveMap.polygonStates) {
        const auto state = stateByPolygon.find(polygon.canonicalPolygonId);
        polygon.state = state != stateByPolygon.end()
            ? state->second
            : liveMap.stale
                ? LiveAuthorityAssignmentState::Stale
                : LiveAuthorityAssignmentState::Offline;
        polygon.controllerCallsigns.clear();
        const auto owners = ownersByPolygon.find(polygon.canonicalPolygonId);
        if (owners != ownersByPolygon.end()) {
            polygon.controllerCallsigns = owners->second;
            std::sort(
                polygon.controllerCallsigns.begin(),
                polygon.controllerCallsigns.end());
            polygon.controllerCallsigns.erase(
                std::unique(
                    polygon.controllerCallsigns.begin(),
                    polygon.controllerCallsigns.end()),
                polygon.controllerCallsigns.end());
        }
    }
    liveMap.brainDecisionsApplied = true;
    std::ostringstream status;
    status << "LIVE_AUTHORITY_MAP definitions="
           << liveMap.polygonDefinitionCount
           << " assignments=" << liveMap.assignments.size()
           << " conflicts=" << liveMap.conflictedPolygonIds.size()
           << " unmapped=" << liveMap.unmappedControllerCallsigns.size();
    liveMap.statusLine = status.str();
    return liveMap;
}

AuthorityRelevanceSnapshot BuildBrainOwnedAuthorityRelevanceSnapshot(
    AuthorityRelevanceSnapshot authorityRelevance,
    const BrainAuthorityRelevanceDecisionPreview& preview) {
    (void)preview;
    authorityRelevance.liveMap = BuildBrainOwnedLiveAuthorityMapSnapshot(
        std::move(authorityRelevance.liveMap),
        &authorityRelevance.diagnostics);
    if (!authorityRelevance.liveMap.available) {
        return authorityRelevance;
    }

    auto& liveMap = authorityRelevance.liveMap;

    authorityRelevance.relevantAuthorities.clear();
    authorityRelevance.relevantAuthorities.reserve(
        authorityRelevance.liveMap.assignments.size());

    for (const auto& assignment : authorityRelevance.liveMap.assignments) {
        if (!IsActiveLiveAuthorityState(assignment.state) ||
            (!assignment.aircraftInside && !assignment.routeIntersects)) {
            continue;
        }
        RelevantAuthoritySnapshot relevant;
        relevant.callsign = assignment.callsign;
        relevant.frequency = assignment.frequency;
        relevant.authorityId = assignment.positionId;
        relevant.polygonId = assignment.canonicalPolygonId;
        relevant.polygonKey = assignment.polygonKey;
        relevant.matchedPattern = assignment.matchedPattern;
        relevant.proofSource = assignment.authoritySource;
        relevant.proofDetail = assignment.ownershipProof;
        relevant.proofDetail += ";routeRelevance=";
        if (assignment.aircraftInside && assignment.routeIntersects) {
            relevant.proofDetail += "aircraft-inside+route-intersects";
        } else if (assignment.aircraftInside) {
            relevant.proofDetail += "aircraft-inside";
        } else {
            relevant.proofDetail += "route-intersects";
        }
        {
            std::ostringstream routeEntry;
            routeEntry << std::fixed << std::setprecision(1)
                       << std::max(0.0, assignment.routeEntryDistanceNm)
                       << "nm";
            relevant.proofDetail += ";routeEntry=" + routeEntry.str();
        }
        relevant.kind = assignment.kind;
        relevant.aircraftInside = assignment.aircraftInside;
        relevant.routeIntersects = assignment.routeIntersects;
        relevant.routeEntryDistanceNm = assignment.routeEntryDistanceNm;
        authorityRelevance.relevantAuthorities.push_back(std::move(relevant));
    }

    std::sort(
        authorityRelevance.relevantAuthorities.begin(),
        authorityRelevance.relevantAuthorities.end(),
        [](const auto& left, const auto& right) {
            if (left.routeEntryDistanceNm != right.routeEntryDistanceNm) {
                return left.routeEntryDistanceNm < right.routeEntryDistanceNm;
            }
            if (left.callsign != right.callsign) {
                return left.callsign < right.callsign;
            }
            if (left.polygonId != right.polygonId) {
                return left.polygonId < right.polygonId;
            }
            return left.authorityId < right.authorityId;
        });

    authorityRelevance.liveRelevantAuthoritiesBrainOwned = true;
    return authorityRelevance;
}

BrainControllerRelevanceWorkerOutput RunBrainControllerRelevanceWorker(
    const BrainControllerRelevanceWorkerInput& input) {
    return RunBrainControllerEvidencePipeline(input);


}

BrainControllerRelevanceWorkerInput BuildBrainOwnedControllerRelevanceInput(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedControllerRelevanceInputRequest& request) {
    BrainControllerRelevanceWorkerInput input;
    input.workflowStage = request.workflowStage;
    input.radioBoardHash = request.radioSnapshot.stableHash;
    input.routePolygonHash = state.routePolygonHash;
    input.currentPolygonIndex = state.currentPolygonIndex;
    input.currentPolygonKey = state.currentPolygonKey;
    input.nextPolygonKey = state.nextPolygonKey;
    input.arrivalPolygonKey = state.arrivalPolygonKey;
    input.routeProgressDistanceNm = state.routeProgressDistanceNm;
    input.departureIcao = request.departureIcao;
    input.arrivalIcao = request.arrivalIcao;
    input.hasDepartureCoordinates = request.hasDepartureCoordinates;
    input.departureLatitudeDeg = request.departureLatitudeDeg;
    input.departureLongitudeDeg = request.departureLongitudeDeg;
    input.hasArrivalCoordinates = request.hasArrivalCoordinates;
    input.arrivalLatitudeDeg = request.arrivalLatitudeDeg;
    input.arrivalLongitudeDeg = request.arrivalLongitudeDeg;
    input.terminalTransmitterRadiusNm = request.terminalTransmitterRadiusNm;
    input.terminalRelevancePolicyHash = HashTerminalRelevancePolicy(
        input.terminalTransmitterRadiusNm,
        input.hasDepartureCoordinates,
        input.departureLatitudeDeg,
        input.departureLongitudeDeg,
        input.hasArrivalCoordinates,
        input.arrivalLatitudeDeg,
        input.arrivalLongitudeDeg);
    input.departureEndpointPolygonHash =
        state.departureEndpointPolygonHash;
    input.departureEndpointPolygons =
        state.departureEndpointPolygons;
    input.arrivalEndpointPolygonHash =
        state.arrivalEndpointPolygonHash;
    input.arrivalEndpointPolygons =
        state.arrivalEndpointPolygons;
    input.airportFrequencyHash =
        state.airportFrequencyHash;
    input.airportFrequencies =
        state.airportFrequencies;
    input.authorityRelevanceHash = request.authorityRelevanceHash;
    input.authorityRelevance = request.authorityRelevance;
    input.radioTuningHash = HashRadioTuningIdentity(request.radios);
    input.xpilot4Controllers = state.xpilot4Bridge.controllerSnapshot;
    input.xpilot4ControllerHash = input.xpilot4Controllers != nullptr
        ? input.xpilot4Controllers->stableHash
        : 0;
    input.radios = request.radios;
    input.route = state.routePolygonSnapshot;
    input.candidates = request.radioSnapshot.candidates;
    return input;
}

BrainOwnedControllerRelevanceRuntimeOutput RunBrainOwnedControllerRelevance(
    BrainOwnedRuntimeState* state,
    const BrainControllerRelevanceWorkerInput& input) {
    BrainOwnedControllerRelevanceRuntimeOutput output;
    const auto canReuse =
        state != nullptr &&
        state->candidatesComplete &&
        state->hasRadioBoard &&
        state->lastRadioBoardHash == input.radioBoardHash &&
        state->routePolygonHash == input.routePolygonHash &&
        state->lastDepartureEndpointPolygonHash ==
            input.departureEndpointPolygonHash &&
        state->lastArrivalEndpointPolygonHash ==
            input.arrivalEndpointPolygonHash &&
        state->lastAirportFrequencyHash ==
            input.airportFrequencyHash &&
        state->lastAuthorityRelevanceHash ==
            input.authorityRelevanceHash &&
        state->lastTerminalRelevancePolicyHash ==
            input.terminalRelevancePolicyHash &&
        state->lastRadioTuningHash == input.radioTuningHash &&
        state->lastXPilot4ControllerHash == input.xpilot4ControllerHash &&
        state->lastWorkflowStage == input.workflowStage &&
        state->currentPolygonKey == input.currentPolygonKey;

    if (canReuse) {
        output.cacheHit = true;
        output.cacheStatus = "brain-controller-relevance-cache-hit";
        output.relevance.available = true;
        output.relevance.stale = false;
        output.relevance.reason = "board-unchanged-no-relevance-work";
        output.relevance.departureBoard =
            state->relevanceDepartureBoardSnapshot;
        output.relevance.arrivalBoard =
            state->relevanceArrivalBoardSnapshot;
        output.relevance.enrouteBoard =
            state->relevanceEnrouteBoardSnapshot;
        output.relevance.completions = state->candidateCompletions;
        state->lastIdleReason = "board-unchanged-no-relevance-work";
        return output;
    }

    output.cacheHit = false;
    output.cacheStatus = "brain-controller-relevance-ran";
    output.relevance = RunBrainControllerRelevanceWorker(input);

    if (state == nullptr) {
        return output;
    }

    state->relevanceDepartureBoardSnapshot =
        output.relevance.departureBoard;
    state->relevanceArrivalBoardSnapshot =
        output.relevance.arrivalBoard;
    state->relevanceEnrouteBoardSnapshot =
        output.relevance.enrouteBoard;
    state->lastDepartureEndpointPolygonHash =
        input.departureEndpointPolygonHash;
    state->lastArrivalEndpointPolygonHash =
        input.arrivalEndpointPolygonHash;
    state->lastAirportFrequencyHash =
        input.airportFrequencyHash;
    state->lastAuthorityRelevanceHash =
        input.authorityRelevanceHash;
    state->lastTerminalRelevancePolicyHash =
        input.terminalRelevancePolicyHash;
    state->lastRadioTuningHash = input.radioTuningHash;
    state->lastXPilot4ControllerHash = input.xpilot4ControllerHash;

    state->candidateCompletions.clear();
    for (const auto& completion : output.relevance.completions) {
        RecordBrainOwnedCandidateCompletion(state, completion);
    }
    state->candidatesComplete = true;
    state->lastIdleReason.clear();
    return output;
}

}  // namespace xvatsim::brain
