#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "XVatsim/brain/BrainOwnedRuntime.h"
#include "XVatsim/brain/BrainTypes.h"
#include "XVatsim/brain/RadioReachableSnapshot.h"

namespace xvatsim::brain {

struct BrainRadioRangeWorkerInput {
    AircraftStateSnapshot aircraft;
    RadioStateSnapshot radios;
    ControllerFeedSnapshot controllerFeed;
    std::string planKey;
};

struct BrainRadioRangePreviewDecision {
    std::string callsign;
    std::string frequency;
    std::string decision;
    std::string reason;
    // Compares against the legacy compatibility projection only. This is not
    // used as radio board authority when liveCandidatesBrainOwned is true.
    bool matchesOldSurvivor = false;
    bool hasStation = false;
    double distanceNm = 0.0;
    double score = 0.0;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
};

struct BrainRadioRangePreviewSummary {
    int evidenceControllerCount = 0;
    // Legacy compatibility candidates retained for regression comparison.
    int oldSurvivorCount = 0;
    int previewSurvivorCount = 0;
    int previewRejectedCount = 0;
    int oldSurvivorMismatchCount = 0;
    bool liveCandidatesBrainOwned = false;
    bool resolverCandidatesCompatibilityOnly = false;
    int droppedBeforeBrainControllers = 0;
};

struct BrainRadioRangeDecisionPreview {
    std::vector<BrainRadioRangePreviewDecision> decisions;
    BrainRadioRangePreviewSummary summary;
};

struct BrainAuthorityStationsPreviewDecision {
    std::string callsign;
    std::string frequency;
    std::string decision;
    std::string reason;
    bool matchesOldSurvivor = false;
    bool hasStation = false;
    int stationIndex = -1;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
};

struct BrainAuthorityStationsPreviewSummary {
    std::string path;
    int evidenceControllerCount = 0;
    int oldSurvivorCount = 0;
    int previewSurvivorCount = 0;
    int previewRejectedCount = 0;
    int oldSurvivorMismatchCount = 0;
    bool liveCandidatesBrainOwned = false;
    bool resolverCandidatesCompatibilityOnly = false;
    int droppedBeforeBrainControllers = 0;
};

struct BrainAuthorityStationsDecisionPreview {
    std::vector<BrainAuthorityStationsPreviewDecision> decisions;
    BrainAuthorityStationsPreviewSummary summary;
};

struct BrainAirportCoveragePreviewDecision {
    std::string callsign;
    std::string frequency;
    std::string decision;
    std::string reason;
    bool matchesOldSurvivor = false;
    bool hasStation = false;
    int stationIndex = -1;
    double distanceNm = 0.0;
    double score = 0.0;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
};

struct BrainAirportCoveragePreviewSummary {
    std::string path;
    int evidenceControllerCount = 0;
    int oldSurvivorCount = 0;
    int previewSurvivorCount = 0;
    int previewRejectedCount = 0;
    int oldSurvivorMismatchCount = 0;
    bool liveCandidatesBrainOwned = false;
    bool resolverCandidatesCompatibilityOnly = false;
    int droppedBeforeBrainControllers = 0;
};

struct BrainAirportCoverageDecisionPreview {
    std::vector<BrainAirportCoveragePreviewDecision> decisions;
    BrainAirportCoveragePreviewSummary summary;
};

struct BrainAuthorityRelevancePreviewDecision {
    std::string evidenceKind;
    std::string callsign;
    std::string authorityId;
    std::string polygonId;
    std::string polygonKey;
    std::string matchedPattern;
    std::string proofSource;
    std::string decision;
    std::string reason;
    bool matchesOldSurvivor = false;
};

struct BrainAuthorityRelevancePreviewSummary {
    std::string authority = "preview-only";
    int sourceControllerCount = 0;
    int evidenceControllerCount = 0;
    int compatibilityRelevantAuthorityCount = 0;
    int previewSurvivorCount = 0;
    int previewRejectedCount = 0;
    int oldSurvivorMismatchCount = 0;
    int droppedBeforeBrainControllers = 0;
    bool relevantAuthoritiesCompatibilityOnly = false;
    bool liveRelevantAuthoritiesBrainOwned = false;
};

struct BrainAuthorityRelevanceDecisionPreview {
    std::vector<BrainAuthorityRelevancePreviewDecision> decisions;
    BrainAuthorityRelevancePreviewSummary summary;
};

BrainAuthorityRelevanceDecisionPreview
BuildBrainAuthorityRelevanceDecisionPreview(
    const AuthorityRelevanceSnapshot& authorityRelevance);

// Brain-owned live projection for route_sector authority relevance evidence.
// route_sector compatibility survivors remain available for comparison when
// evidence exists, but migrated live consumers must use relevantAuthorities
// after this projection.
AuthorityRelevanceSnapshot BuildBrainOwnedAuthorityRelevanceSnapshot(
    AuthorityRelevanceSnapshot authorityRelevance,
    const BrainAuthorityRelevanceDecisionPreview& preview);

struct BrainRadioRangeWorkerOutput {
    bool available = false;
    bool stale = true;
    std::string reason;
    TransceiverResolutionSnapshot transceivers;
    RadioReachableControllerSnapshot radioBoard;
    RadioReachableCandidateDiff diff;
    BrainRadioRangeDecisionPreview decisionPreview;
};

// Brain-owned live projection for normal Resolve radio-range evidence.
// When transceiver evidence exists, the radio board is built from brain
// decisions, not the resolver compatibility candidates vector.
BrainRadioRangeWorkerOutput BuildBrainRadioRangeWorkerOutput(
    const BrainRadioRangeWorkerInput& input,
    const TransceiverResolutionSnapshot& transceivers,
    double nowSeconds);

BrainAuthorityStationsDecisionPreview
BuildBrainAuthorityStationsDecisionPreview(
    const TransceiverResolutionSnapshot& transceivers);

// Brain-owned live projection for ResolveAuthorityStations evidence.
// The resolver's candidates vector remains compatibility-only when evidence
// exists and must not be used as decision authority by migrated callers.
TransceiverResolutionSnapshot BuildBrainOwnedAuthorityStationsCandidateSnapshot(
    TransceiverResolutionSnapshot transceivers,
    const BrainAuthorityStationsDecisionPreview& preview);

BrainAirportCoverageDecisionPreview
BuildBrainAirportCoverageDecisionPreview(
    const TransceiverResolutionSnapshot& transceivers);

// Brain-owned live projection for ResolveAirportCoverage evidence.
// The resolver's candidates vector remains compatibility-only when evidence
// exists and must not be used as decision authority by migrated callers.
TransceiverResolutionSnapshot BuildBrainOwnedAirportCoverageCandidateSnapshot(
    TransceiverResolutionSnapshot transceivers,
    const BrainAirportCoverageDecisionPreview& preview);

struct BrainRoutePolygonWorkerInput {
    AircraftStateSnapshot aircraft;
    NetworkPlanSnapshot networkPlan;
    std::string planKey;
};

struct BrainRoutePolygonWorkerOutput {
    bool available = false;
    bool stale = true;
    std::string reason;
    RouteSectorSnapshot route;
    std::uint64_t routePolygonHash = 0;
    int currentPolygonIndex = 0;
    std::string currentPolygonKey;
    std::string nextPolygonKey;
    std::string arrivalPolygonKey;
    std::string finalRoutePolygonKey;
};

struct BrainOwnedRoutePolygonRefreshInput {
    AircraftStateSnapshot aircraft;
    std::string routeRuntimeKey;
    long long nowSeconds = 0;
    long long pendingRetrySeconds = 0;
};

struct BrainOwnedRoutePolygonRuntimeOutput {
    BrainRoutePolygonWorkerOutput route;
    bool needsWorker = false;
    bool routeChanged = false;
    bool transitionChanged = false;
    bool transitionEvaluated = false;
    bool cacheHit = false;
    bool reset = false;
    std::string reason;
    std::string cacheStatus;
    std::string diagnosticResult;
    std::string transitionReason;
    std::string transitionCacheStatus;
    std::string transitionDiagnosticResult;
};

std::uint64_t HashBrainRouteSectorSnapshot(
    const RouteSectorSnapshot& snapshot);

BrainRoutePolygonWorkerOutput BuildBrainRoutePolygonWorkerOutput(
    const RouteSectorSnapshot& route);

BrainOwnedRoutePolygonRuntimeOutput BeginBrainOwnedRoutePolygonRefresh(
    BrainOwnedRuntimeState* state,
    const BrainOwnedRoutePolygonRefreshInput& input);

BrainOwnedRoutePolygonRuntimeOutput CommitBrainOwnedRoutePolygonRefresh(
    BrainOwnedRuntimeState* state,
    const BrainOwnedRoutePolygonRefreshInput& input,
    const BrainRoutePolygonWorkerOutput& workerOutput);

struct BrainControllerRelevanceWorkerInput {
    WorkflowStage workflowStage = WorkflowStage::None;
    std::uint64_t radioBoardHash = 0;
    std::uint64_t routePolygonHash = 0;
    int currentPolygonIndex = 0;
    std::string currentPolygonKey;
    std::string nextPolygonKey;
    std::string arrivalPolygonKey;
    double routeProgressDistanceNm = 0.0;
    std::string departureIcao;
    std::string arrivalIcao;
    std::uint64_t departureTerminalAuthorityHash = 0;
    BrainTerminalAuthorityWorkerOutput departureTerminalAuthority;
    std::uint64_t arrivalTerminalAuthorityHash = 0;
    BrainTerminalAuthorityWorkerOutput arrivalTerminalAuthority;
    std::uint64_t airportFrequencyHash = 0;
    BrainAirportFrequencyWorkerOutput airportFrequencies;
    std::uint64_t authorityRelevanceHash = 0;
    AuthorityRelevanceSnapshot authorityRelevance;
    std::uint64_t radioTuningHash = 0;
    RadioStateSnapshot radios;
    std::vector<RouteSectorMatchSnapshot> currentSectors;
    std::vector<RouteSectorMatchSnapshot> nextSectors;
    std::vector<RadioReachableControllerCandidate> candidates;
};

struct BrainOwnedControllerRelevanceInputRequest {
    WorkflowStage workflowStage = WorkflowStage::None;
    RadioReachableControllerSnapshot radioSnapshot;
    std::string departureIcao;
    std::string arrivalIcao;
    std::uint64_t authorityRelevanceHash = 0;
    AuthorityRelevanceSnapshot authorityRelevance;
    RadioStateSnapshot radios;
};

struct BrainControllerRelevanceWorkerOutput {
    bool available = false;
    bool stale = true;
    bool needsFallbackVerification = false;
    std::string reason;
    std::vector<BrainOwnedCandidateCompletion> completions;
    ModuleBoardSnapshot departureBoard;
    ModuleBoardSnapshot arrivalBoard;
    ModuleBoardSnapshot enrouteBoard;
};

struct BrainOwnedControllerRelevanceRuntimeOutput {
    BrainControllerRelevanceWorkerOutput relevance;
    bool cacheHit = false;
    std::string cacheStatus;
};

BrainControllerRelevanceWorkerOutput RunBrainControllerRelevanceWorker(
    const BrainControllerRelevanceWorkerInput& input);

BrainControllerRelevanceWorkerInput BuildBrainOwnedControllerRelevanceInput(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedControllerRelevanceInputRequest& request);

BrainOwnedControllerRelevanceRuntimeOutput RunBrainOwnedControllerRelevance(
    BrainOwnedRuntimeState* state,
    const BrainControllerRelevanceWorkerInput& input);

struct BrainUiWorkerInput {
    WorkflowStage workflowStage = WorkflowStage::None;
    FinalDisplaySnapshot finalDisplay;
    std::string reason;
};

struct BrainUiWorkerOutput {
    bool rendered = false;
    std::string reason;
};

}  // namespace xvatsim::brain
