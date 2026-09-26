#pragma once

#include <cstdint>
#include <memory>
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
    // Gate B: the prepared route is immutable and shared from the route worker
    // through Brain consumers. Routine flight-loop reuse must not deep-copy the
    // waypoint or sector ledgers.
    std::shared_ptr<const RouteSectorSnapshot> route;
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
    // When publication replaces a derived immutable route, ownership is
    // returned to the plugin for bounded worker-thread reclamation.
    std::shared_ptr<const RouteSectorSnapshot> retiredRoute;
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
bool EqualBrainRouteSectorSnapshotsExact(
    const RouteSectorSnapshot& left,
    const RouteSectorSnapshot& right);

// Full worker identity for authority evaluation. Unlike routePolygonHash this
// includes waypoint geometry and therefore protects asynchronous completion
// publication after late FMS enrichment or route-shape changes.
std::uint64_t HashBrainAuthorityRouteSnapshot(
    const RouteSectorSnapshot& snapshot);

std::uint64_t HashBrainControllerEvidence(
    const std::vector<ControllerSnapshot>& controllers,
    bool available,
    bool stale,
    std::uint64_t generation,
    int connectedControllers);

std::uint64_t HashBrainControllerEvidenceContent(
    const std::vector<ControllerSnapshot>& controllers);

std::uint64_t HashBrainControllerEvidenceFromContentDigest(
    std::uint64_t controllerContentDigest,
    bool available,
    bool stale,
    std::uint64_t generation,
    int connectedControllers);

// Authority-only semantic controller identity. Feed generation, aggregate
// counts, visual range, and input order do not affect authority decisions.
std::vector<AuthorityControllerSnapshot> BuildBrainAuthorityControllerEvidence(
    const std::vector<ControllerSnapshot>& controllers);

std::vector<ControllerSnapshot> ExpandBrainAuthorityControllerEvidence(
    const std::vector<AuthorityControllerSnapshot>& controllers);

std::uint64_t HashBrainAuthorityControllerEvidenceContent(
    const std::vector<ControllerSnapshot>& controllers);

std::uint64_t HashBrainAuthorityControllerEvidenceContent(
    const std::vector<AuthorityControllerSnapshot>& controllers);

std::uint64_t HashBrainAuthorityControllerEvidence(
    const std::vector<ControllerSnapshot>& controllers,
    bool available,
    bool stale);

std::uint64_t HashBrainAuthorityControllerEvidenceFromContentDigest(
    std::uint64_t controllerContentDigest,
    bool available,
    bool stale);

std::uint64_t HashBrainTransceiverEvidence(
    const TransceiverResolutionSnapshot& snapshot);

AuthorityTransceiverEvidenceSnapshot BuildBrainAuthorityTransceiverEvidence(
    const TransceiverResolutionSnapshot& snapshot);

std::uint64_t HashBrainAuthorityTransceiverEvidence(
    const AuthorityTransceiverEvidenceSnapshot& snapshot);

TransceiverResolutionSnapshot ExpandBrainAuthorityTransceiverEvidence(
    const AuthorityTransceiverEvidenceSnapshot& snapshot);

std::uint64_t HashBrainAuthorityRelevanceSnapshot(
    const AuthorityRelevanceSnapshot& snapshot);

struct BrainAuthorityCompletionIdentity {
    std::uint64_t requestId = 0;
    std::uint64_t lifecycleEpoch = 0;
    std::string planKey;
    std::uint64_t routeDigest = 0;
    std::uint64_t controllerDigest = 0;
    std::uint64_t transceiverDigest = 0;
    std::uint64_t datasetIdentity = 0;
};

struct BrainAuthorityCompletionValidationInput {
    BrainAuthorityCompletionIdentity expected;
    BrainAuthorityCompletionIdentity completed;
    AircraftStateSnapshot currentAircraft;
    AircraftStateSnapshot dispatchedAircraft;
    long long nowMonotonicMs = 0;
    long long completedMonotonicMs = 0;
    long long maximumAgeMs = 5000;
    double maximumDisplacementNm = 25.0;
};

struct BrainAuthorityCompletionDecision {
    bool accepted = false;
    bool staleRejected = false;
    std::string reason;
    long long ageMs = 0;
    double displacementNm = 0.0;
};

BrainAuthorityCompletionDecision DecideBrainAuthorityCompletion(
    const BrainAuthorityCompletionValidationInput& input);

struct BrainRouteCompletionIdentity {
    std::uint64_t requestId = 0;
    std::uint64_t lifecycleEpoch = 0;
    std::string planKey;
    std::uint64_t networkPlanDigest = 0;
    std::uint64_t routeSourceDatasetIdentity = 0;
    std::uint64_t preflightCandidateIdentity = 0;
    std::uint64_t routePolicyIdentity = 0;
    std::uint64_t routeAnchorDigest = 0;
    std::uint64_t expandedFmsObservationIdentity = 0;
};

struct BrainRouteCompletionValidationInput {
    BrainRouteCompletionIdentity eligible;
    BrainRouteCompletionIdentity desired;
    BrainRouteCompletionIdentity completed;
};

struct BrainRouteCompletionDecision {
    bool accepted = false;
    bool staleRejected = false;
    std::string reason;
};

BrainRouteCompletionDecision DecideBrainRouteCompletion(
    const BrainRouteCompletionValidationInput& input);

BrainRoutePolygonWorkerOutput BuildBrainRoutePolygonWorkerOutput(
    const RouteSectorSnapshot& route);
BrainRoutePolygonWorkerOutput BuildBrainRoutePolygonWorkerOutput(
    std::shared_ptr<const RouteSectorSnapshot> route);

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
    bool hasDepartureCoordinates = false;
    double departureLatitudeDeg = 0.0;
    double departureLongitudeDeg = 0.0;
    bool hasArrivalCoordinates = false;
    double arrivalLatitudeDeg = 0.0;
    double arrivalLongitudeDeg = 0.0;
    bool terminalRelevanceV2Enabled = false;
    bool vnasSectorPrecedenceEnabled = false;
    double terminalTransmitterRadiusNm = 5.0;
    std::uint64_t terminalRelevancePolicyHash = 0;
    std::uint64_t vnasTerminalEvidenceHash = 0;
    std::shared_ptr<const VnasTerminalEvidenceSnapshot> vnasTerminalEvidence;
    std::uint64_t departureTerminalAuthorityHash = 0;
    BrainTerminalAuthorityWorkerOutput departureTerminalAuthority;
    std::uint64_t arrivalTerminalAuthorityHash = 0;
    BrainTerminalAuthorityWorkerOutput arrivalTerminalAuthority;
    std::uint64_t airportFrequencyHash = 0;
    BrainAirportFrequencyWorkerOutput airportFrequencies;
    std::uint64_t authorityRelevanceHash = 0;
    std::shared_ptr<const AuthorityRelevanceSnapshot> authorityRelevance;
    std::uint64_t radioTuningHash = 0;
    std::uint64_t xpilot4ControllerHash = 0;
    std::shared_ptr<const BrainXPilot4ControllerSnapshot> xpilot4Controllers;
    RadioStateSnapshot radios;
    std::shared_ptr<const RouteSectorSnapshot> route;
    // Compatibility-only fixture inputs. Production populates route above so
    // the immutable sector vectors are not copied on the flight loop.
    std::vector<RouteSectorMatchSnapshot> currentSectors;
    std::vector<RouteSectorMatchSnapshot> nextSectors;
    std::vector<RadioReachableControllerCandidate> candidates;
};

struct BrainOwnedControllerRelevanceInputRequest {
    WorkflowStage workflowStage = WorkflowStage::None;
    RadioReachableControllerSnapshot radioSnapshot;
    std::string departureIcao;
    std::string arrivalIcao;
    bool hasDepartureCoordinates = false;
    double departureLatitudeDeg = 0.0;
    double departureLongitudeDeg = 0.0;
    bool hasArrivalCoordinates = false;
    double arrivalLatitudeDeg = 0.0;
    double arrivalLongitudeDeg = 0.0;
    bool terminalRelevanceV2Enabled = false;
    bool vnasSectorPrecedenceEnabled = false;
    double terminalTransmitterRadiusNm = 5.0;
    std::uint64_t vnasTerminalEvidenceHash = 0;
    std::shared_ptr<const VnasTerminalEvidenceSnapshot> vnasTerminalEvidence;
    std::uint64_t authorityRelevanceHash = 0;
    std::shared_ptr<const AuthorityRelevanceSnapshot> authorityRelevance;
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
