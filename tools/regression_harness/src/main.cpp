#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "XVatsim/brain/BrainOrchestrator.h"
#include "XVatsim/brain/BrainDisplayIntent.h"
#include "XVatsim/brain/BrainOwnedRuntime.h"
#include "XVatsim/brain/BrainOwnedWorkerTypes.h"
#include "XVatsim/brain/BrainTypes.h"
#include "XVatsim/brain/PhaseSnapshotPublisher.h"
#include "XVatsim/brain/RadioReachableSnapshot.h"
#include "XVatsim/brain/BrainWorkModel.h"
#include "XVatsim/brain/BrainWorkScheduler.h"
#include "XVatsim/core/ControllerAuthority.h"
#include "XVatsim/core/MapDataSource.h"
#include "XVatsim/core/PreflightRouteCache.h"
#include "XVatsim/core/RouteGrammar.h"
#include "XVatsim/core/RouteResolution.h"
#include "XVatsim/core/RouteTraversal.h"
#include "XVatsim/core/WorkflowEngine.h"
#include "XVatsim/modules/arrival/ArrivalAirspaceModule.h"
#include "XVatsim/modules/arrival/ArrivalLocalModule.h"
#include "XVatsim/modules/airport_frequency_catalog/AirportFrequencyCatalogResolver.h"
#include "XVatsim/modules/departure/DepartureModule.h"
#include "XVatsim/modules/enroute/EnrouteModule.h"
#include "XVatsim/modules/route_sector/RouteSectorResolver.h"
#include "XVatsim/modules/terminal_authority/TerminalAuthorityResolver.h"
#include "XVatsim/modules/transceiver_resolver/TransceiverResolver.h"
#include "XVatsim/modules/update_checker/UpdateChecker.h"

namespace {

using xvatsim::brain::BoardSource;
using xvatsim::brain::BoardStationSnapshot;
using xvatsim::brain::DisplayRelation;
using xvatsim::brain::ModuleBoardSnapshot;
using xvatsim::brain::StationRole;
using xvatsim::brain::WorkflowStage;
using xvatsim::core::workflow::FlightContext;
using xvatsim::core::workflow::HandoffDecision;
using xvatsim::core::workflow::WorkflowState;

struct ScenarioExpectations {
    struct WaypointPoint {
        std::string ident;
        double latitudeDeg = 0.0;
        double longitudeDeg = 0.0;
    };

    std::optional<WorkflowStage> stage;
    std::optional<std::string> reason;
    std::optional<bool> departureLocationConfirmed;
    std::optional<bool> recoveryAccepted;
    std::optional<WorkflowStage> recoveryStage;
    std::optional<std::string> recoveryReason;
    std::optional<bool> recoveryUsedPreservedContext;
    std::optional<bool> recoveryUsedFreshNetworkPlan;
    std::optional<bool> recoveryFlightContextActive;
    std::optional<BoardSource> displaySource;
    std::vector<std::string> displayCallsigns;
    std::vector<std::string> overlayBodyLines;
    std::vector<std::string> overlayBodyTones;
    std::optional<std::string> overlayVersionText;
    std::optional<std::string> overlayVersionAlternateText;
    std::optional<std::string> overlayVersionTone;
    std::optional<bool> overlayVersionRotates;
    std::optional<bool> overlayNoticeVisible;
    std::optional<std::string> overlayNoticeSeverity;
    std::optional<std::string> overlayNoticeTitle;
    std::vector<std::string> overlayNoticeBodyLines;
    std::optional<bool> departureCollectedAvailable;
    std::vector<std::string> departureCollectedCallsigns;
    std::optional<bool> arrivalAirspaceAvailable;
    std::vector<std::string> arrivalAirspaceCallsigns;
    std::optional<bool> arrivalLocalAvailable;
    std::vector<std::string> arrivalLocalCallsigns;
    std::optional<bool> airportCoverageAvailable;
    std::optional<bool> airportTerminalInside;
    std::vector<std::string> airportCoverageMatchTokens;
    std::vector<std::string> airportCoverageControllerPrefixes;
    std::vector<std::string> airportCoverageControllerPatterns;
    std::vector<std::string> airportCoverageGenerations;
    std::optional<bool> enrouteAvailable;
    std::vector<std::string> enrouteCallsigns;
    std::optional<bool> resolverRouteAvailable;
    std::optional<bool> resolverRouteResolved;
    std::optional<std::string> resolverRouteStatus;
    std::vector<std::string> resolverRouteAuthorityGaps;
    std::vector<std::string> resolverRouteCurrentSectors;
    std::vector<std::string> resolverRouteNextSectors;
    std::vector<std::string> resolverRouteCurrentControllerPatterns;
    std::vector<std::string> resolverRouteNextControllerPatterns;
    std::vector<std::string> resolverRouteCurrentControllerPrefixes;
    std::vector<std::string> resolverRouteNextControllerPrefixes;
    std::vector<std::string> resolverRouteGenerations;
    std::vector<std::string> routeAuthorityPlanSequence;
    std::vector<std::string> routeAuthorityPlanFlags;
    std::vector<std::string> routeAuthorityPlanSources;
    std::optional<bool> resolverAuthorityRelevanceAvailable;
    std::optional<std::string> resolverAuthorityStatus;
    std::optional<std::string> resolverAuthorityCacheStatus;
    std::optional<std::string> resolverAuthorityCacheReason;
    std::optional<std::string> resolverAuthorityRepeatCacheStatus;
    std::optional<std::string> resolverAuthorityRepeatCacheReason;
    std::vector<std::string> resolverAuthorityDiagnostics;
    std::vector<std::string> resolverAuthorityRelevantMatches;
    std::vector<std::string> resolverAuthorityRepeatRelevantMatches;
    std::vector<std::string> resolverAuthorityProofSources;
    std::vector<std::string> resolverAuthorityProofDetails;
    std::vector<std::string> resolverAuthorityProofDetailContains;
    std::optional<std::string> resolverAuthorityEvidenceVisibility;
    std::vector<std::string> resolverAuthorityControllerEvidence;
    std::vector<std::string> resolverAuthorityDecisionEvidence;
    std::vector<std::string> resolverAuthorityPolygonEvidenceContains;
    std::vector<std::string> resolverAuthorityActivePolygonEvidenceContains;
    std::vector<std::string> resolverAuthorityTransceiverProofEvidenceContains;
    std::vector<std::string> resolverAuthorityDuplicatedAtisProofEvidenceContains;
    std::optional<std::string> resolverAuthorityPreviewSummary;
    std::vector<std::string> resolverAuthorityPreviewDecisionsContains;
    std::optional<bool> resolverEnrouteAvailable;
    std::vector<std::string> resolverEnrouteCallsigns;
    std::vector<std::string> sourceRegistryValues;
    std::optional<int> sourceRegistryCount;
    std::vector<std::string> sourceRegistrySourceCounts;
    std::vector<std::string> authorityCatalogIds;
    std::vector<std::string> authorityDataGaps;
    std::vector<std::string> authorityActiveMatches;
    std::vector<std::string> authorityUnmappedCallsigns;
    std::vector<std::string> authorityPolygonIds;
    std::vector<std::string> authorityPolygonLookupKeys;
    std::vector<std::string> authorityPolygonRingCounts;
    std::vector<std::string> authorityPolygonDataGaps;
    std::vector<std::string> authorityActivePolygonMatches;
    std::vector<std::string> authorityActivePolygonDataGaps;
    std::vector<std::string> authorityRelevantPolygonMatches;
    std::optional<bool> sourceManifestValid;
    std::vector<std::string> sourceManifestValues;
    std::optional<std::string> sourcePackagePayload;
    std::optional<std::string> updateStatus;
    std::optional<std::string> updateLatestVersion;
    std::optional<std::string> updateDownloadPageUrl;
    std::optional<std::string> updateErrorClass;
    std::optional<bool> updateCritical;
    std::optional<bool> routeResolved;
    std::vector<std::string> routeCurrentSectors;
    std::vector<std::string> routeNextSectors;
    std::vector<std::string> routeCurrentControllerPrefixes;
    std::vector<std::string> routeNextControllerPrefixes;
    std::vector<std::string> routeTokenKinds;
    std::vector<std::string> resolvedWaypointIdents;
    std::vector<WaypointPoint> resolvedWaypointPoints;
    std::vector<std::string> resolvedTokens;
    std::vector<std::string> expandedTokens;
    std::vector<std::string> recognizedProcedureTokens;
    std::vector<std::string> procedureMetadataSources;
    std::vector<std::string> procedureRecordKinds;
    std::vector<std::string> procedureRunwayRecords;
    std::vector<std::string> procedureCatalogAuthorities;
    std::vector<std::string> procedureCatalogFixes;
    std::vector<std::string> procedureBoundaryFixes;
    std::vector<std::string> procedureOrderedFixes;
    std::vector<std::string> procedureSyntheticWaypoints;
    std::vector<std::string> procedureSyntheticSources;
    std::vector<std::string> procedureApplicationStates;
    std::vector<std::string> procedureApplicationBlocks;
    std::vector<std::string> procedureAppliedFixSequences;
    std::vector<std::string> procedureCatalogTransitions;
    std::vector<std::string> procedureSupportDirections;
    std::vector<std::string> procedureTransitionLinks;
    std::vector<std::string> procedureTransitionMisses;
    std::vector<std::string> procedureAnchorLinks;
    std::vector<std::string> procedureContextOnlyTokens;
    std::vector<std::string> ignoredTokens;
    std::vector<std::string> unsupportedTokens;
    std::vector<std::string> unresolvedTokens;
    std::vector<std::string> unresolvedAirwayTokens;
    std::optional<bool> preflightParseOk;
    std::optional<bool> preflightValidationAccepted;
    std::optional<std::string> preflightValidationReason;
    std::optional<std::string> preflightDepartureIcao;
    std::optional<std::string> preflightDestinationIcao;
    std::vector<std::string> preflightWaypointIdents;
    std::vector<std::string> brainWorkOrder;
    std::vector<std::string> brainWorkHeavyFlags;
    std::vector<std::string> brainSchedulerRunnable;
    std::vector<std::string> brainSchedulerDeferred;
    std::optional<std::string> brainSchedulerHeavyCounts;
    std::vector<std::string> brainRoutePlanRebuildSequence;
    std::optional<std::string> brainRoutePlanRebuildLifecycle;
    std::vector<std::string> brainRoutePlanPendingSequence;
    std::optional<std::string> brainRoutePlanPendingLifecycle;
    std::vector<std::string> brainDepartureWorkOrder;
    std::vector<std::string> brainDepartureSchedulerRunnable;
    std::vector<std::string> brainDepartureSchedulerDeferred;
    std::optional<std::string> brainDepartureSchedulerHeavyCounts;
    std::optional<std::string> brainDepartureSnapshotLifecycle;
    std::optional<std::string> brainDeparturePendingLifecycle;
    std::vector<std::string> radioReachableCandidates;
    std::optional<std::string> radioReachableCounts;
    std::optional<std::string> radioReachableHashCheck;
    std::vector<std::string> radioReachableSourceCandidates;
    std::optional<std::string> radioReachableSourceCounts;
    std::optional<bool> transceiverResolverHoldoverAvailable;
    std::optional<bool> transceiverResolverHoldoverStale;
    std::optional<std::string> transceiverResolverHoldoverStatusContains;
    std::vector<std::string> transceiverResolverHoldoverCandidates;
    std::vector<std::string> transceiverResolverHoldoverRadioCandidates;
    std::optional<std::string> transceiverResolverHoldoverRadioCounts;
    std::optional<std::string> transceiverResolverHoldoverRadioStatusContains;
    std::optional<std::string> transceiverResolverHoldoverSourceEvidence;
    std::optional<std::string> transceiverResolverHoldoverEvidenceVisibility;
    std::vector<std::string> transceiverResolverHoldoverControllerEvidence;
    std::vector<std::string> transceiverResolverHoldoverStationEvidence;
    std::optional<std::string> transceiverResolverHoldoverBrainPreviewSummary;
    std::vector<std::string> transceiverResolverHoldoverBrainPreviewDecisions;
    std::optional<bool> transceiverResolverAuthorityAvailable;
    std::optional<bool> transceiverResolverAuthorityStale;
    std::optional<std::string> transceiverResolverAuthorityStatusContains;
    std::vector<std::string> transceiverResolverAuthorityCandidates;
    std::optional<std::string> transceiverResolverAuthoritySourceEvidence;
    std::optional<std::string> transceiverResolverAuthorityEvidenceVisibility;
    std::vector<std::string> transceiverResolverAuthorityControllerEvidence;
    std::vector<std::string> transceiverResolverAuthorityStationEvidence;
    std::optional<std::string> transceiverResolverAuthorityPreviewSummary;
    std::vector<std::string> transceiverResolverAuthorityPreviewDecisions;
    std::optional<bool> transceiverResolverAirportCoverageAvailable;
    std::optional<bool> transceiverResolverAirportCoverageStale;
    std::optional<std::string>
        transceiverResolverAirportCoverageStatusContains;
    std::vector<std::string> transceiverResolverAirportCoverageCandidates;
    std::optional<std::string>
        transceiverResolverAirportCoverageSourceEvidence;
    std::optional<std::string>
        transceiverResolverAirportCoverageEvidenceVisibility;
    std::vector<std::string>
        transceiverResolverAirportCoverageControllerEvidence;
    std::vector<std::string>
        transceiverResolverAirportCoverageStationEvidence;
    std::optional<std::string>
        transceiverResolverAirportCoveragePreviewSummary;
    std::vector<std::string>
        transceiverResolverAirportCoveragePreviewDecisions;
    std::vector<std::string> radioReachableGateDepartureCandidates;
    std::vector<std::string> radioReachableGateEnrouteCandidates;
    std::vector<std::string> radioReachableGateArrivalCandidates;
    std::vector<std::string> radioReachableGateNoneCandidates;
    std::vector<std::string> radioReachableVerifierEnrouteControllers;
    std::vector<std::string> radioReachableVerifierUnchangedControllers;
    std::optional<std::string> radioReachableVerifierUnchangedStatus;
    std::vector<std::string> terminalAuthorityOwners;
    std::vector<std::string> terminalAuthorityPolygons;
    std::vector<std::string> airportFrequencyDepartureRecords;
    std::vector<std::string> airportFrequencyArrivalRecords;
    std::vector<std::string> brainControllerRelevanceDepartureCallsigns;
    std::vector<std::string> brainControllerRelevanceArrivalCallsigns;
    std::vector<std::string> brainControllerRelevanceEnrouteCallsigns;
    std::vector<std::string> brainControllerRelevanceCompletions;
    std::vector<std::string> phasePublisherReuseLifecycle;
    std::vector<std::string> phasePublisherIsolationLifecycle;
    std::vector<std::string> phasePublisherWorkflowClearLifecycle;
    std::optional<std::string> phasePublisherReuseLedgerSummary;
    std::vector<std::string> phasePublisherReuseLedgerDecisionsContains;
    std::optional<std::string> phasePublisherPlanContextSummary;
    std::optional<std::string> phasePublisherStableKeySummary;
    std::optional<std::string> phasePublisherStableKeyConsumerDryRunSummary;
    std::optional<std::string> phasePublisherStableKeyShadowSummary;
    std::optional<std::string>
        phasePublisherStableKeyLiveConsumptionReadinessSummary;
    std::optional<std::string>
        phasePublisherStableKeyLiveConsumptionSummary;
    std::vector<std::string> brainOrdinaryMovementWorkOrder;
    std::vector<std::string> brainOrdinaryMovementHeavyFlags;
    std::vector<std::string> brainDisplayIntentRows;
    std::optional<std::string> brainDisplayIntentDecisionSummary;
    std::optional<std::string> brainDisplayIntentFailSoftSummary;
    std::vector<std::string> brainDisplayIntentDecisionsContains;
    std::optional<std::string> brainDisplayOverlayCapSummary;
    std::vector<std::string> brainDisplayOverlayCapDecisionsContains;
    std::optional<std::string> brainDisplaySourceLinkSummary;
    std::optional<std::string> brainDisplayStableKeyAuditSummary;
    std::vector<std::string> brainDisplayStableKeyAuditDecisionsContains;
    std::optional<std::string> brainDisplaySourceOwnedStableKeySummary;
    std::optional<std::string> brainDisplayStableKeyConsumerDryRunSummary;
    std::vector<std::string>
        brainDisplayStableKeyConsumerDryRunDecisionsContains;
    std::optional<std::string> brainDisplayStableKeyShadowSummary;
    std::vector<std::string> brainDisplayStableKeyShadowDecisionsContains;
    std::optional<std::string>
        brainDisplayStableKeyLiveConsumptionReadinessSummary;
    std::vector<std::string>
        brainDisplayStableKeyLiveConsumptionReadinessDecisionsContains;
    std::optional<std::string>
        brainDisplayStableKeyLiveConsumptionSummary;
    std::vector<std::string>
        brainDisplayStableKeyLiveConsumptionDecisionsContains;
    std::optional<std::string> brainDisplayUpstreamStableKeySourceAuditSummary;
    std::vector<std::string>
        brainDisplayUpstreamStableKeySourceAuditDecisionsContains;
    std::optional<std::string> ctafUnicomEvidenceSummary;
    std::vector<std::string> ctafUnicomSourceEvidence;
    std::vector<std::string> ctafUnicomProjectionEvidence;
    std::optional<std::string> ctafUnicomAdvisoryPreviewSummary;
    std::vector<std::string> ctafUnicomAdvisoryPreviewDecisions;
    std::optional<std::string> ctafUnicomAdvisoryAuthoritySummary;
    std::optional<std::string> ctafUnicomBypassAuditSummary;
    std::vector<std::string> ctafUnicomBypassAuditDecisionsContains;
    std::optional<std::string> ctafUnicomMissingEvidenceAuditSummary;
    std::vector<std::string>
        ctafUnicomMissingEvidenceAuditDecisionsContains;
    std::optional<std::string> ctafUnicomLegacyBypassAliasAuditSummary;
    std::vector<std::string>
        ctafUnicomLegacyBypassAliasAuditDecisionsContains;
    std::optional<std::string>
        ctafUnicomPublicUnknownAliasConsumerAuditSummary;
    std::vector<std::string>
        ctafUnicomPublicUnknownAliasConsumerAuditDecisionsContains;
    std::optional<std::string> ctafUnicomExternalAliasDeprecationSummary;
    std::vector<std::string>
        ctafUnicomExternalAliasDeprecationDecisionsContains;
    std::optional<std::string>
        ctafUnicomPublicHeaderAliasRiskClosureSummary;
    std::vector<std::string>
        ctafUnicomPublicHeaderAliasRiskClosureDecisionsContains;
    std::vector<std::string> ctafUnicomPublisherRows;
    std::optional<std::string>
        ctafUnicomPublisherStableKeyShadowSummary;
    std::optional<std::string>
        ctafUnicomPublisherStableKeyLiveConsumptionReadinessSummary;
    std::optional<std::string>
        ctafUnicomPublisherStableKeyLiveConsumptionSummary;
    std::vector<std::string>
        ctafUnicomPublisherStableKeyLiveConsumptionDecisionsContains;
    std::optional<std::string>
        ctafUnicomPublisherPhaseStableKeyShadowSummary;
    std::optional<std::string>
        ctafUnicomPublisherPhaseStableKeyLiveConsumptionReadinessSummary;
    std::optional<std::string>
        ctafUnicomPublisherPhaseStableKeyLiveConsumptionSummary;
    std::vector<std::string>
        ctafUnicomPublisherPhaseReuseLedgerDecisionsContains;
    std::optional<std::string> standbyAssistSummary;
    std::vector<std::string> standbyAssistDecisionsContains;
    std::optional<std::string> standbyAssistSettingsDiagnostics;
    std::optional<std::string> standbyAssistSideEffectSummary;
    std::optional<std::string> standbyAssistSideEffectActualSummary;
    std::optional<std::string> standbyAssistWriterResultSummary;
    std::vector<std::string> standbyAssistWriterResultContains;
    std::optional<std::string> standbyAssistWriterCounterSummary;
};

struct TerminalCoverageFeatureSpec {
    std::string id;
    std::string name;
    std::string suffix;
    std::vector<std::string> prefixes;
    std::vector<xvatsim::core::route::SectorPolygon> polygons;
};

struct CenterCoverageFeatureSpec {
    std::string label;
    std::string name;
    std::string callsign;
    std::vector<std::string> tokens;
    std::vector<xvatsim::core::route::SectorPolygon> polygons;
};

struct ScenarioData {
    std::string name;
    double nowSeconds = 0.0;
    bool departureTerminalCoverageKnown = false;
    bool insideDepartureTerminalCoverage = false;
    xvatsim::core::workflow::WorkflowTuning tuning;
    WorkflowState workflowState;
    xvatsim::brain::AircraftStateSnapshot aircraftState;
    xvatsim::brain::FlightPlanSnapshot flightPlanSnapshot;
    xvatsim::brain::NetworkPlanSnapshot networkPlanSnapshot;
    xvatsim::brain::RadioStateSnapshot radioStateSnapshot;
    xvatsim::brain::XPilotSessionSnapshot xPilotSessionSnapshot;
    xvatsim::brain::TransceiverResolutionSnapshot transceiverResolutionSnapshot;
    bool transceiverResolverHoldoverProbe = false;
    long long transceiverResolverHoldoverCacheAgeSeconds = 60;
    bool transceiverResolverHoldoverLastFetchSucceeded = false;
    bool transceiverResolverAuthorityProbe = false;
    long long transceiverResolverAuthorityCacheAgeSeconds = 0;
    bool transceiverResolverAuthorityLastFetchSucceeded = true;
    bool transceiverResolverAirportCoverageProbe = false;
    long long transceiverResolverAirportCoverageCacheAgeSeconds = 0;
    bool transceiverResolverAirportCoverageLastFetchSucceeded = true;
    bool transceiverResolverAirportCoverageHasCoordinates = true;
    double transceiverResolverAirportCoverageLatitudeDeg = 0.0;
    double transceiverResolverAirportCoverageLongitudeDeg = 0.0;
    std::optional<WorkflowStage> overlayWorkflowStage;
    std::optional<WorkflowStage> displayIntentWorkflowStage;
    std::string phasePublisherReuseProbe;
    bool ctafUnicomPublisherProbe = false;
    std::optional<WorkflowStage> ctafUnicomPublisherStage;
    std::string ctafUnicomPublisherProductPlanKey;
    bool ctafUnicomPublisherAcceptBoardRows = false;
    xvatsim::brain::BrainOwnedCtafLookupFact ctafUnicomDepartureFact;
    xvatsim::brain::BrainOwnedCtafLookupFact ctafUnicomArrivalFact;
    bool ctafUnicomOmitDepartureSourceEvidence = false;
    bool ctafUnicomOmitArrivalSourceEvidence = false;
    bool ctafUnicomOmitDepartureAdvisoryDecision = false;
    bool ctafUnicomOmitArrivalAdvisoryDecision = false;
    bool ctafUnicomIncompleteDepartureAdvisoryDecision = false;
    bool ctafUnicomIncompleteArrivalAdvisoryDecision = false;
    double displayIntentRouteProgressNm = 0.0;
    std::string displayIntentCurrentPolygonKey;
    std::string displayIntentNextPolygonKey;
    std::string displayIntentArrivalPolygonKey;
    bool sourceOwnedFallbackStableKeyShadowEnabled = false;
    std::string sourceOwnedFallbackStableKeyShadowGateSource = "default";
    bool sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled = false;
    std::string
        sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource =
            "default";
    bool sourceOwnedFallbackStableKeyLiveConsumptionEnabled = false;
    std::string sourceOwnedFallbackStableKeyLiveConsumptionGateSource =
        "default";
    bool settingsSourceOwnedFallbackStableKeyLiveConsumptionLoaded = false;
    bool settingsSourceOwnedFallbackStableKeyLiveConsumptionEnabled = false;
    bool settingsSourceOwnedFallbackStableKeyLiveConsumptionSourceLoaded =
        false;
    std::string
        settingsSourceOwnedFallbackStableKeyLiveConsumptionGateSource =
            "default";
    std::vector<xvatsim::brain::BrainDisplayRelationFact> displayIntentRelationFacts;
    bool applyStandbyAssist = false;
    std::optional<WorkflowStage> standbyAssistWorkflowStage;
    std::string standbyAssistPlanKey;
    std::optional<bool> standbyAssistLoaded;
    bool standbyAssistSideEffect = false;
    bool standbyAssistEnabled = true;
    bool standbyAssistDirectCtafEnabled = false;
    std::string standbyAssistDirectCtafGateSource = "default";
    bool standbyAssistUseDisplayBoardWithCtafAdvisories = false;
    std::optional<bool> standbyAssistWriteSucceeded;
    std::optional<std::string> standbyAssistWriterResultCode;
    xvatsim::brain::RouteSectorSnapshot routeSectorSnapshot;
    xvatsim::brain::AirportSectorSnapshot departureAirportSectorSnapshot;
    xvatsim::brain::AirportSectorSnapshot arrivalAirportSectorSnapshot;
    std::string airportCoverageBuildIcao;
    double airportCoverageBuildLatitudeDeg = 0.0;
    double airportCoverageBuildLongitudeDeg = 0.0;
    bool hasAirportCoverageBuildCoordinates = false;
    bool airportCoverageBuildsPreRefreshSnapshot = false;
    bool hasAirportTerminalProbeCoordinates = false;
    bool airportTerminalProbeUsesPreRefreshSnapshot = false;
    double airportTerminalProbeLatitudeDeg = 0.0;
    double airportTerminalProbeLongitudeDeg = 0.0;
    std::vector<CenterCoverageFeatureSpec> airportCoverageCenterFeatures;
    std::vector<TerminalCoverageFeatureSpec> airportCoverageTerminalFeatures;
    std::vector<std::string> airportCoverageAuthorityCatalogLines;
    std::vector<CenterCoverageFeatureSpec> pendingAirportCoverageCenterFeatures;
    std::vector<TerminalCoverageFeatureSpec> pendingAirportCoverageTerminalFeatures;
    std::vector<std::string> pendingAirportCoverageAuthorityCatalogLines;
    bool hasPendingAirportCoveragePayloads = false;
    std::string terminalAuthorityAirportIcao;
    double terminalAuthorityLatitudeDeg = 0.0;
    double terminalAuthorityLongitudeDeg = 0.0;
    bool hasTerminalAuthorityCoordinates = false;
    std::vector<TerminalCoverageFeatureSpec> terminalAuthorityFeatures;
    std::vector<std::string> airportFrequencyFrqRows;
    WorkflowStage controllerRelevanceWorkflowStage = WorkflowStage::Departure;
    bool resolveRouteWithResolver = false;
    bool resolverRouteBuildsPreRefreshSnapshot = false;
    std::vector<CenterCoverageFeatureSpec> resolverRouteCenterFeatures;
    std::vector<TerminalCoverageFeatureSpec> resolverRouteTerminalFeatures;
    std::vector<std::string> resolverRouteAuthorityCatalogLines;
    std::string resolverRouteOwnershipJson;
    std::vector<CenterCoverageFeatureSpec> pendingResolverRouteCenterFeatures;
    std::vector<TerminalCoverageFeatureSpec> pendingResolverRouteTerminalFeatures;
    std::vector<std::string> pendingResolverRouteAuthorityCatalogLines;
    std::string pendingResolverRouteOwnershipJson;
    bool hasPendingResolverRoutePayloads = false;
    std::vector<std::string> authorityCatalogFirLines;
    std::vector<std::string> authorityCatalogUirLines;
    std::vector<xvatsim::core::authority::AuthorityPolygonSourceRecord>
        authorityPolygonRecords;
    std::vector<xvatsim::core::authority::AuthorityPositionSourceRecord>
        authorityPositionRecords;
    bool authorityEnrouteHandoff = false;
    bool authorityEnrouteSnapshotAvailable = true;
    bool authorityEnrouteSnapshotStale = false;
    bool recoveryRequested = false;
    xvatsim::core::workflow::RecoveryRequestMode recoveryMode =
        xvatsim::core::workflow::RecoveryRequestMode::AutomaticReconnect;
    std::vector<xvatsim::brain::ControllerSnapshot> controllers;
    std::optional<bool> controllerFeedAvailable;
    bool controllerFeedStale = false;
    bool forceControllerFeedEntries = false;
    std::uint64_t controllerFeedGeneration = 0;
    std::uint64_t resolverAuthorityRepeatControllerFeedGeneration = 0;
    long long resolverAuthorityRepeatCacheAgeSeconds = 0;
    std::vector<xvatsim::brain::ControllerSnapshot> resolverAuthorityRepeatControllers;
    bool resolverAuthorityRepeatReplaceControllers = false;
    bool hasResolverAuthorityRepeatAircraftState = false;
    xvatsim::brain::AircraftStateSnapshot resolverAuthorityRepeatAircraftState;
    std::string sourceManifestJson;
    std::string sourcePackagePositionsJson;
    std::string sourcePackageAirspaceJson;
    std::string sourcePackageOwnershipJson;
    std::vector<std::string> sourcePackageSpecialSectorJsons;
    std::vector<std::string> sourcePackageTerminalAuthorityJsons;
    std::vector<std::string> sourceRegistryJsons;
    std::string updateManifestPayload;
    std::string updateInstalledVersion = "1.2.3";
    std::string updateManifestUrl =
        "https://ezsimulations.github.io/XVatsim/xvatsim_update.json";
    std::unordered_map<std::string, std::string> sourceRegistryPayloadsByUrl;
    xvatsim::core::route::AirwayGraph routeGraph;
    std::string routeGraphFixPayload;
    std::string routeGraphNavPayload;
    std::string routeGraphAirwayPayload;
    std::unordered_map<std::string, xvatsim::core::route::ProcedureCatalogEntry> proceduresByName;
    std::vector<xvatsim::brain::RouteWaypointSnapshot> routeWaypoints;
    std::vector<xvatsim::core::route::SectorFeature> traversalFeatures;
    xvatsim::core::route::TraversalTuning traversalTuning;
    std::string preflightFmsText;
    std::string preflightCurrentFmsText;
    bool preflightValidateAgainstPlan = false;
    bool preflightVerifySourceFile = false;
    bool resolverUsesPreflightCache = false;
    ModuleBoardSnapshot departureBoard;
    ModuleBoardSnapshot arrivalBoard;
    ModuleBoardSnapshot enrouteBoard;
    ScenarioExpectations expectations;
};

bool AddCenterCoverageFeature(
    std::vector<CenterCoverageFeatureSpec>* features,
    const std::string& value);
bool AddTerminalCoverageFeature(
    std::vector<TerminalCoverageFeatureSpec>* features,
    const std::string& value);
bool AddAuthorityPolygonSourceRecord(
    std::vector<xvatsim::core::authority::AuthorityPolygonSourceRecord>* records,
    xvatsim::core::authority::AuthoritySource source,
    const std::string& value);
bool AddAuthorityPositionSourceRecord(
    std::vector<xvatsim::core::authority::AuthorityPositionSourceRecord>* records,
    xvatsim::core::authority::AuthoritySource source,
    const std::string& value);

std::string Trim(std::string value) {
    const auto notSpace = [](unsigned char ch) { return std::isspace(ch) == 0; };
    value.erase(
        value.begin(),
        std::find_if(value.begin(), value.end(), notSpace));
    value.erase(
        std::find_if(value.rbegin(), value.rend(), notSpace).base(),
        value.end());
    return value;
}

std::string ToUpperCopy(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return value;
}

std::vector<std::string> Split(const std::string& input, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream stream(input);
    std::string part;
    while (std::getline(stream, part, delimiter)) {
        parts.push_back(Trim(part));
    }
    return parts;
}

bool ParseBool(const std::string& value, bool* outValue) {
    if (outValue == nullptr) {
        return false;
    }

    const auto normalized = ToUpperCopy(Trim(value));
    if (normalized == "1" || normalized == "TRUE" || normalized == "YES" || normalized == "ON") {
        *outValue = true;
        return true;
    }
    if (normalized == "0" || normalized == "FALSE" || normalized == "NO" || normalized == "OFF") {
        *outValue = false;
        return true;
    }
    return false;
}

std::string NormalizeSourceOwnedLiveConsumptionSettingsSourceForHarness(
    const std::string& value) {
    const auto normalized = ToUpperCopy(Trim(value));
    if (normalized == "SETTINGS-STORE" ||
        normalized == "SETTINGS_STORE" ||
        normalized == "SETTINGSSTORE") {
        return "settings-store";
    }
    return "unknown";
}

xvatsim::brain::BrainOwnedCandidateCompletion
BuildHarnessAcceptedCompletion(
    const BoardStationSnapshot& station,
    DisplayRelation relation) {
    xvatsim::brain::BrainOwnedCandidateCompletion completion;
    completion.callsign = station.callsign;
    completion.frequency = station.frequency;
    completion.currentPolygonKey = station.polygonKey;
    completion.matchedPolygonKey = station.polygonKey;
    completion.displayRelation = relation;
    completion.decision = xvatsim::brain::BrainOwnedCandidateDecision::Accepted;
    completion.reason = "harness-publisher-board-row-accepted";
    completion.stableKey =
        station.stableCompletionKey.empty()
            ? station.callsign + "|" + station.frequency
            : station.stableCompletionKey;
    return completion;
}

void AppendHarnessAcceptedCompletionsFromBoard(
    const ModuleBoardSnapshot& board,
    DisplayRelation relation,
    std::vector<xvatsim::brain::BrainOwnedCandidateCompletion>* completions) {
    if (completions == nullptr) {
        return;
    }
    for (const auto& station : board.stations) {
        completions->push_back(
            BuildHarnessAcceptedCompletion(station, relation));
    }
}

std::optional<double> ParseDouble(const std::string& value) {
    try {
        return std::stod(Trim(value));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<WorkflowStage> ParseWorkflowStage(const std::string& value) {
    const auto normalized = ToUpperCopy(Trim(value));
    if (normalized == "NONE") {
        return WorkflowStage::None;
    }
    if (normalized == "DEPARTURE") {
        return WorkflowStage::Departure;
    }
    if (normalized == "ENROUTE") {
        return WorkflowStage::Enroute;
    }
    if (normalized == "ARRIVAL") {
        return WorkflowStage::Arrival;
    }
    return std::nullopt;
}

std::optional<xvatsim::brain::DisplayRelation> ParseDisplayRelation(
    const std::string& value) {
    const auto normalized = ToUpperCopy(Trim(value));
    if (normalized == "UNKNOWN") {
        return xvatsim::brain::DisplayRelation::Unknown;
    }
    if (normalized == "CURRENT" || normalized == "CURRENT_POLYGON" ||
        normalized == "CURRENTPOLYGON") {
        return xvatsim::brain::DisplayRelation::CurrentPolygon;
    }
    if (normalized == "NEXT" || normalized == "NEXT_POLYGON" ||
        normalized == "NEXTPOLYGON") {
        return xvatsim::brain::DisplayRelation::NextPolygon;
    }
    if (normalized == "ARRIVAL" || normalized == "ARRIVAL_PREP" ||
        normalized == "ARRIVALPREP") {
        return xvatsim::brain::DisplayRelation::ArrivalPrep;
    }
    if (normalized == "FILTERED") {
        return xvatsim::brain::DisplayRelation::Filtered;
    }
    if (normalized == "HIDDEN") {
        return xvatsim::brain::DisplayRelation::Hidden;
    }
    return std::nullopt;
}

std::optional<xvatsim::core::workflow::RecoveryRequestMode> ParseRecoveryRequestMode(
    const std::string& value) {
    const auto normalized = ToUpperCopy(Trim(value));
    if (normalized == "AUTOMATIC" || normalized == "AUTO" ||
        normalized == "RECONNECT") {
        return xvatsim::core::workflow::RecoveryRequestMode::AutomaticReconnect;
    }
    if (normalized == "MANUAL") {
        return xvatsim::core::workflow::RecoveryRequestMode::Manual;
    }
    return std::nullopt;
}

std::optional<BoardSource> ParseBoardSource(const std::string& value) {
    const auto normalized = ToUpperCopy(Trim(value));
    if (normalized == "NONE") {
        return BoardSource::None;
    }
    if (normalized == "DEPARTURE") {
        return BoardSource::Departure;
    }
    if (normalized == "ARRIVAL") {
        return BoardSource::Arrival;
    }
    if (normalized == "ENROUTE") {
        return BoardSource::Enroute;
    }
    return std::nullopt;
}

std::optional<StationRole> ParseStationRole(const std::string& value) {
    const auto normalized = ToUpperCopy(Trim(value));
    if (normalized == "DELIVERY") {
        return StationRole::Delivery;
    }
    if (normalized == "GROUND") {
        return StationRole::Ground;
    }
    if (normalized == "TOWER") {
        return StationRole::Tower;
    }
    if (normalized == "DEPARTURE") {
        return StationRole::Departure;
    }
    if (normalized == "APPROACH") {
        return StationRole::Approach;
    }
    if (normalized == "CENTER") {
        return StationRole::Center;
    }
    if (normalized == "ATIS") {
        return StationRole::Atis;
    }
    if (normalized == "CTAF") {
        return StationRole::Ctaf;
    }
    if (normalized == "UNICOM") {
        return StationRole::Unicom;
    }
    if (normalized == "OTHER") {
        return StationRole::Other;
    }
    return std::nullopt;
}

std::optional<xvatsim::core::authority::AuthorityKind> ParseAuthorityKind(
    const std::string& value) {
    const auto normalized = ToUpperCopy(Trim(value));
    if (normalized == "CENTER" || normalized == "CTR") {
        return xvatsim::core::authority::AuthorityKind::Center;
    }
    if (normalized == "TERMINAL" || normalized == "TRACON" || normalized == "APPROACH") {
        return xvatsim::core::authority::AuthorityKind::Terminal;
    }
    if (normalized == "EXTENSION") {
        return xvatsim::core::authority::AuthorityKind::Extension;
    }
    return std::nullopt;
}

std::string WorkflowStageToString(WorkflowStage stage) {
    switch (stage) {
    case WorkflowStage::None:
        return "None";
    case WorkflowStage::Departure:
        return "Departure";
    case WorkflowStage::Enroute:
        return "Enroute";
    case WorkflowStage::Arrival:
        return "Arrival";
    }
    return "Unknown";
}

std::string BoardSourceToString(BoardSource source) {
    switch (source) {
    case BoardSource::None:
        return "None";
    case BoardSource::Departure:
        return "Departure";
    case BoardSource::Arrival:
        return "Arrival";
    case BoardSource::Enroute:
        return "Enroute";
    }
    return "Unknown";
}

std::vector<std::string> ExtractCallsigns(const ModuleBoardSnapshot& board) {
    std::vector<std::string> callsigns;
    callsigns.reserve(board.stations.size());
    for (const auto& station : board.stations) {
        callsigns.push_back(station.callsign);
    }
    return callsigns;
}

std::vector<std::string> ExtractTerminalAuthorityOwners(
    const xvatsim::brain::BrainTerminalAuthorityWorkerOutput& output) {
    return output.ownerTokens;
}

std::vector<std::string> ExtractTerminalAuthorityPolygons(
    const xvatsim::brain::BrainTerminalAuthorityWorkerOutput& output) {
    return output.polygonKeys;
}

std::string StationRoleToToken(StationRole role) {
    switch (role) {
    case StationRole::Delivery:
        return "DEL";
    case StationRole::Ground:
        return "GND";
    case StationRole::Tower:
        return "TWR";
    case StationRole::Departure:
    case StationRole::Approach:
        return "APP_DEP";
    case StationRole::Center:
        return "CTR";
    case StationRole::Atis:
        return "ATIS";
    case StationRole::Ctaf:
        return "CTAF";
    case StationRole::Unicom:
        return "UNICOM";
    case StationRole::Other:
    default:
        return "OTHER";
    }
}

std::vector<std::string> ExtractAirportFrequencyRecords(
    const std::vector<xvatsim::brain::BrainAirportFrequencyRecord>& records) {
    std::vector<std::string> values;
    values.reserve(records.size());
    for (const auto& record : records) {
        std::ostringstream stream;
        stream << record.airportIcao << ":"
               << StationRoleToToken(record.role) << ":"
               << record.frequency << ":"
               << record.frequencyUse;
        values.push_back(stream.str());
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractControllerRelevanceCompletions(
    const xvatsim::brain::BrainControllerRelevanceWorkerOutput& output) {
    std::vector<std::string> values;
    values.reserve(output.completions.size());
    for (const auto& completion : output.completions) {
        std::ostringstream stream;
        stream << completion.callsign << ":"
               << xvatsim::brain::ToString(completion.decision) << ":"
               << completion.reason;
        values.push_back(stream.str());
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractCallsigns(
    const xvatsim::brain::FinalDisplaySnapshot& board) {
    std::vector<std::string> callsigns;
    callsigns.reserve(board.stations.size());
    for (const auto& station : board.stations) {
        callsigns.push_back(station.callsign);
    }
    return callsigns;
}

std::vector<std::string> ExtractDisplayIntentRows(
    const xvatsim::brain::FinalDisplaySnapshot& board) {
    std::vector<std::string> rows;
    rows.reserve(board.stations.size());
    for (const auto& station : board.stations) {
        std::ostringstream stream;
        stream << station.callsign << ":"
               << xvatsim::brain::ToString(station.displayRelation);
        if (!station.annotation.empty()) {
            stream << ":" << station.annotation;
        } else if (station.sectorActive) {
            stream << ":ACTIVE";
        }
        rows.push_back(stream.str());
    }
    return rows;
}

std::string BrainDisplayIntentDecisionSummaryText(
    const xvatsim::brain::BrainDisplayIntentOutput& output) {
    const auto& summary = output.displayDecisionSummary;
    std::ostringstream stream;
    stream << "accepted=" << summary.acceptedCompletionCount
           << ",decisions=" << summary.displayDecisionCount
           << ",displayedFinal=" << summary.displayedFinalCount
           << ",hiddenAfterAccept=" << summary.hiddenAfterAcceptCount
           << ",filteredAfterAccept=" << summary.filteredAfterAcceptCount
           << ",duplicates=" << summary.duplicateSuppressedCount
           << ",stageSuppressed=" << summary.stageSuppressedCount
           << ",missing=" << summary.missingDecisionCount;
    return stream.str();
}

std::string BrainDisplayIntentFailSoftSummaryText(
    const xvatsim::brain::BrainDisplayIntentOutput& output) {
    const auto& summary = output.failSoftPreviewSummary;
    std::ostringstream stream;
    stream << "preview=" << summary.failSoftPreviewCount
           << ",keepDisplay=" << summary.recommendKeepDisplayCount
           << ",keepHide=" << summary.recommendKeepHideCount
           << ",displayWithWarning="
           << summary.recommendDisplayWithWarningCount
           << ",stageDefer=" << summary.recommendStageDeferCount
           << ",lowerPriorityDisplay="
           << summary.recommendLowerPriorityDisplayCount
           << ",hardBlockHide=" << summary.recommendHardBlockHideCount
           << ",needsMoreEvidence="
           << summary.recommendNeedsMoreEvidenceCount
           << ",currentHideButFailSoftWouldShowOrWarn="
           << summary.currentHideButFailSoftWouldShowOrWarnCount;
    return stream.str();
}

std::string FormatDisplayDecisionScore(double score) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << score;
    return stream.str();
}

std::string OverlayCapToken(const std::string& value) {
    return value.empty() ? std::string("<none>") : value;
}

std::vector<std::string> ExtractDisplayIntentDecisionRows(
    const xvatsim::brain::BrainDisplayIntentOutput& output) {
    std::vector<std::string> rows;
    rows.reserve(output.displayDecisions.size());
    for (const auto& decision : output.displayDecisions) {
        std::ostringstream stream;
        stream << "id=" << decision.decisionId
               << ":" << decision.callsign << "@" << decision.frequency
               << ":decision=" << decision.decision
               << ":reason=" << decision.reason
               << ":role=" << StationRoleToToken(decision.role)
               << ":source=" << BoardSourceToString(decision.sourceBoard)
               << ":stage=" << WorkflowStageToString(decision.workflowStage)
               << ":accepted=" << (decision.acceptedByRelevance ? 1 : 0)
               << ":relationFact="
               << (decision.relationFactPresent ? 1 : 0)
               << "/" << xvatsim::brain::ToString(decision.relationFactValue)
               << ":fallback=" << (decision.fallbackRelationUsed ? 1 : 0)
               << "/" << xvatsim::brain::ToString(decision.fallbackRelationValue)
               << ":final=" << xvatsim::brain::ToString(decision.finalRelation)
               << ":displayable=" << (decision.displayable ? 1 : 0)
               << ":displayed="
               << (decision.displayedInFinalSnapshot ? 1 : 0)
               << ":duplicate=" << (decision.duplicateSuppressed ? 1 : 0)
               << ":stageSuppressed="
               << (decision.stageSuppressed ? 1 : 0)
               << ":confidence=" << decision.confidenceLevel
               << ":score="
               << FormatDisplayDecisionScore(decision.positiveScore) << "/"
               << FormatDisplayDecisionScore(decision.negativeScore)
               << ":hardBlock=" << (decision.hardBlock ? 1 : 0)
               << ":scoreSummary=" << decision.scoreSummary
               << ":failSoft=" << decision.failSoftRecommendation
               << ":failSoftReason=" << decision.failSoftReason
               << ":failSoftWouldShowOrWarn="
               << (decision.currentHideButFailSoftWouldShowOrWarn ? 1 : 0);
        if (!decision.duplicateKey.empty()) {
            stream << ":duplicateKey=" << decision.duplicateKey;
        }
        if (!decision.duplicateKeptDecisionId.empty()) {
            stream << ":kept=" << decision.duplicateKeptDecisionId;
        }
        if (!decision.duplicateDroppedDecisionId.empty()) {
            stream << ":dropped=" << decision.duplicateDroppedDecisionId;
        }
        stream << ":sourceEvidence=" << OverlayCapToken(decision.sourceEvidenceId)
               << ":sourceType=" << OverlayCapToken(decision.sourceEvidenceType)
               << ":sourceDomain="
               << OverlayCapToken(decision.sourceEvidenceDomain)
               << ":sourceLinked="
               << (decision.sourceEvidenceLinked ? 1 : 0)
               << ":sourceStatus="
               << OverlayCapToken(decision.sourceEvidenceLinkStatus)
               << ":missingReason="
               << OverlayCapToken(decision.sourceEvidenceMissingReason)
               << ":sourceDecision="
               << OverlayCapToken(decision.sourceDecisionId)
               << ":sourceDecisionLinked="
               << (decision.sourceDecisionLinked ? 1 : 0)
               << ":displayDecisionLinked="
               << (decision.displayDecisionLinked ? 1 : 0)
               << ":capDecisionLinked="
               << (decision.capDecisionLinked ? 1 : 0)
               << ":linkConfidence="
               << OverlayCapToken(decision.linkageConfidence)
               << ":linkFallback="
               << (decision.linkageFallbackUsed ? 1 : 0);
        rows.push_back(stream.str());
    }
    return rows;
}

std::string BrainDisplayOverlayCapSummaryText(
    const xvatsim::brain::BrainDisplayIntentOutput& output) {
    const auto& summary = output.overlayCapSummary;
    std::ostringstream stream;
    stream << "decisions=" << summary.overlayCapDecisionCount
           << ",capLimit=" << summary.capLimit
           << ",candidates=" << summary.candidateBeforeCapCount
           << ",visibleAfterCap=" << summary.visibleAfterCapCount
           << ",cappedHidden=" << summary.cappedHiddenCount
           << ",moreAtc=" << summary.moreAtcCount
           << ",contributes=" << summary.contributesToMoreAtcCount
           << ",nonCappedHidden=" << summary.nonCappedHiddenCount
           << ",duplicates=" << summary.duplicateHiddenCount
           << ",stageDeferred=" << summary.stageDeferredHiddenCount
           << ",brainOwned=" << (summary.capLedgerBrainOwned ? 1 : 0)
           << ",behaviorChanged="
           << (summary.overlayCapBehaviorChanged ? 1 : 0);
    return stream.str();
}

std::string BrainDisplaySourceLinkSummaryText(
    const xvatsim::brain::BrainDisplayIntentOutput& output) {
    const auto& summary = output.sourceLinkSummary;
    std::ostringstream stream;
    stream << "decisions=" << summary.displaySourceLinkDecisionCount
           << ",displayLinked=" << summary.displaySourceLinkedCount
           << ",displayMissing=" << summary.displaySourceMissingCount
           << ",capLinked=" << summary.capSourceLinkedCount
           << ",capMissing=" << summary.capSourceMissingCount
           << ",synthetic=" << summary.syntheticRowCount
           << ",legacy=" << summary.legacyRowCount
           << ",unknown=" << summary.unknownSourceLinkCount
           << ",brainOwned=" << (summary.sourceLinkageBrainOwned ? 1 : 0)
           << ",displayBehaviorChanged="
           << (summary.displayBehaviorChanged ? 1 : 0);
    return stream.str();
}

std::string BrainDisplayStableKeyAuditSummaryText(
    const xvatsim::brain::BrainDisplayIntentOutput& output) {
    const auto& summary = output.stableKeyAuditSummary;
    std::ostringstream stream;
    stream << "decisions=" << summary.stableKeyAuditDecisionCount
           << ",present=" << summary.stableKeyPresentCount
           << ",missing=" << summary.stableKeyMissingCount
           << ",fallback=" << summary.fallbackDerivedKeyCount
           << ",synthetic=" << summary.syntheticKeyCount
           << ",legacy=" << summary.legacyKeyCount
           << ",duplicated=" << summary.duplicatedKeyCount
           << ",changedAcrossReuse=" << summary.changedAcrossReuseCount
           << ",unsafeSameKey=" << summary.unsafeSameKeyCount
           << ",linkedDisplay=" << summary.keyLedgerLinkedDisplayCount
           << ",linkedCap=" << summary.keyLedgerLinkedCapCount
           << ",linkedPhase=" << summary.keyLedgerLinkedPhaseReuseCount
           << ",brainOwned=" << (summary.stableKeyAuditBrainOwned ? 1 : 0)
           << ",displayBehaviorChanged="
           << (summary.displayBehaviorChanged ? 1 : 0);
    return stream.str();
}

std::string BrainDisplaySourceOwnedStableKeySummaryText(
    const xvatsim::brain::BrainDisplayIntentOutput& output) {
    const auto& summary = output.sourceOwnedStableKeySummary;
    std::ostringstream stream;
    stream << "decisions=" << summary.sourceOwnedStableKeyDecisionCount
           << ",present=" << summary.sourceOwnedStableKeyPresentCount
           << ",generatedFallback="
           << summary.generatedFallbackKeyPresentCount
           << ",matches=" << summary.sourceOwnedMatchesFallbackCount
           << ",mismatch=" << summary.sourceOwnedMismatchCount
           << ",planAvailable=" << summary.planContextAvailableCount
           << ",planMissing=" << summary.planContextMissingCount
           << ",migrationReady=" << summary.migrationReadyCount
           << ",behaviorConsumerEnabled="
           << summary.behaviorConsumerEnabledCount
           << ",behaviorChanged=" << (summary.behaviorChanged ? 1 : 0);
    return stream.str();
}

template <typename Summary>
std::string StableKeyConsumerDryRunSummaryText(const Summary& summary) {
    std::ostringstream stream;
    stream << "decisions=" << summary.dryRunStableKeyConsumerDecisionCount
           << ",sourceOwnedPresent=" << summary.sourceOwnedKeyPresentCount
           << ",migrationReady=" << summary.migrationReadyCount
           << ",dedupeGroupWouldChange="
           << summary.dedupeGroupWouldChangeCount
           << ",duplicateSuppressionWouldChange="
           << summary.duplicateSuppressionWouldChangeCount
           << ",completionIdentityWouldChange="
           << summary.completionIdentityWouldChangeCount
           << ",phaseReuseWouldChange="
           << summary.phaseReuseWouldChangeCount
           << ",rowOrderingWouldChange="
           << summary.rowOrderingWouldChangeCount
           << ",overlayCapWouldChange="
           << summary.overlayCapWouldChangeCount
           << ",moreAtcWouldChange=" << summary.moreAtcWouldChangeCount
           << ",drift=" << summary.driftDetectedCount
           << ",safeForOptIn=" << summary.safeForOptInCount
           << ",behaviorConsumerEnabled="
           << summary.behaviorConsumerEnabledCount
           << ",displayBehaviorChanged="
           << (summary.displayBehaviorChanged ? 1 : 0);
    return stream.str();
}

std::string BrainDisplayStableKeyConsumerDryRunSummaryText(
    const xvatsim::brain::BrainDisplayIntentOutput& output) {
    return StableKeyConsumerDryRunSummaryText(
        output.stableKeyConsumerDryRunSummary);
}

template <typename Summary>
std::string StableKeyShadowSummaryText(const Summary& summary) {
    std::ostringstream stream;
    stream << "decisions=" << summary.shadowDecisionCount
           << ",gateEnabled=" << summary.shadowGateEnabledCount
           << ",attempted=" << summary.shadowRecomputeAttemptedCount
           << ",skipped=" << summary.shadowRecomputeSkippedCount
           << ",hashMismatch=" << summary.shadowHashMismatchCount
           << ",rowOrderingMismatch="
           << summary.shadowRowOrderingMismatchCount
           << ",dedupeMismatch=" << summary.shadowDedupeMismatchCount
           << ",duplicateSuppressionMismatch="
           << summary.shadowDuplicateSuppressionMismatchCount
           << ",completionIdentityMismatch="
           << summary.shadowCompletionIdentityMismatchCount
           << ",phaseReuseMismatch="
           << summary.shadowPhaseReuseMismatchCount
           << ",overlayCapMismatch="
           << summary.shadowOverlayCapMismatchCount
           << ",moreAtcMismatch=" << summary.shadowMoreAtcMismatchCount
           << ",missingPlanBlocked="
           << summary.shadowMissingPlanBlockedCount
           << ",drift=" << summary.shadowDriftDetectedCount
           << ",safeForFutureLiveOptIn="
           << summary.shadowSafeForFutureLiveOptInCount
           << ",behaviorConsumerEnabled="
           << summary.shadowBehaviorConsumerEnabledCount
           << ",behaviorChanged=" << (summary.behaviorChanged ? 1 : 0);
    return stream.str();
}

std::string BrainDisplayStableKeyShadowSummaryText(
    const xvatsim::brain::BrainDisplayIntentOutput& output) {
    return StableKeyShadowSummaryText(
        output.sourceOwnedFallbackStableKeyShadowSummary);
}

template <typename Summary>
std::string StableKeyLiveConsumptionReadinessSummaryText(
    const Summary& summary) {
    std::ostringstream stream;
    stream << "decisions=" << summary.readinessDecisionCount
           << ",proposalGateArmed=" << summary.proposalGateArmedCount
           << ",shadowParityClean=" << summary.shadowParityCleanCount
           << ",planContextAvailable=" << summary.planContextAvailableCount
           << ",missingPlanBlocked=" << summary.missingPlanBlockedCount
           << ",driftBlocked=" << summary.driftBlockedCount
           << ",shadowNotAttemptedBlocked="
           << summary.shadowNotAttemptedBlockedCount
           << ",readinessBlocked=" << summary.readinessBlockedCount
           << ",readyForFutureLiveConsumption="
           << summary.readyForFutureLiveConsumptionCount
           << ",liveBehaviorConsumerEnabled="
           << summary.liveConsumptionBehaviorEnabledCount
           << ",shadowBehaviorConsumerEnabled="
           << summary.shadowBehaviorConsumerEnabledCount
           << ",behaviorChanged=" << (summary.behaviorChanged ? 1 : 0);
    return stream.str();
}

std::string BrainDisplayStableKeyLiveConsumptionReadinessSummaryText(
    const xvatsim::brain::BrainDisplayIntentOutput& output) {
    return StableKeyLiveConsumptionReadinessSummaryText(
        output
            .sourceOwnedFallbackStableKeyLiveConsumptionReadinessSummary);
}

template <typename Summary>
std::string StableKeyLiveConsumptionSummaryText(const Summary& summary) {
    std::ostringstream stream;
    stream << "decisions=" << summary.liveConsumptionDecisionCount
           << ",gateArmed=" << summary.liveConsumptionGateArmedCount
           << ",allowed=" << summary.liveConsumptionAllowedCount
           << ",blocked=" << summary.liveConsumptionBlockedCount
           << ",sourceOwnedConsumed=" << summary.sourceOwnedConsumedCount
           << ",generatedFallbackConsumed="
           << summary.generatedFallbackConsumedCount
           << ",missingPlanBlocked=" << summary.missingPlanBlockedCount
           << ",shadowGateOffBlocked="
           << summary.shadowGateOffBlockedCount
           << ",shadowParityNotAttemptedBlocked="
           << summary.shadowParityNotAttemptedBlockedCount
           << ",shadowDriftBlocked=" << summary.shadowDriftBlockedCount
           << ",hashMismatchBlocked="
           << summary.hashMismatchBlockedCount
           << ",rowOrderingMismatchBlocked="
           << summary.rowOrderingMismatchBlockedCount
           << ",dedupeMismatchBlocked="
           << summary.dedupeMismatchBlockedCount
           << ",duplicateSuppressionMismatchBlocked="
           << summary.duplicateSuppressionMismatchBlockedCount
           << ",completionIdentityMismatchBlocked="
           << summary.completionIdentityMismatchBlockedCount
           << ",phaseReuseMismatchBlocked="
           << summary.phaseReuseMismatchBlockedCount
           << ",overlayCapMismatchBlocked="
           << summary.overlayCapMismatchBlockedCount
           << ",moreAtcMismatchBlocked="
           << summary.moreAtcMismatchBlockedCount
           << ",missingSourceOwnedKeyBlocked="
           << summary.missingSourceOwnedKeyBlockedCount
           << ",migrationNotReadyBlocked="
           << summary.migrationNotReadyBlockedCount
           << ",defaultModeProtected="
           << summary.defaultModeProtectedCount
           << ",behaviorChanged=" << (summary.behaviorChanged ? 1 : 0);
    return stream.str();
}

std::string BrainDisplayStableKeyLiveConsumptionSummaryText(
    const xvatsim::brain::BrainDisplayIntentOutput& output) {
    return StableKeyLiveConsumptionSummaryText(
        output.sourceOwnedFallbackStableKeyLiveConsumptionSummary);
}

std::vector<std::string> ExtractDisplayStableKeyConsumerDryRunRows(
    const xvatsim::brain::BrainDisplayIntentOutput& output) {
    std::vector<std::string> rows;
    rows.reserve(output.stableKeyConsumerDryRunDecisions.size());
    for (const auto& decision : output.stableKeyConsumerDryRunDecisions) {
        std::ostringstream stream;
        stream << "id="
               << OverlayCapToken(
                      decision.dryRunStableKeyConsumerDecisionId)
               << ":subject=" << OverlayCapToken(decision.subjectKey)
               << ":" << OverlayCapToken(decision.callsign)
               << "@" << OverlayCapToken(decision.frequency)
               << ":role=" << StationRoleToToken(decision.role)
               << ":endpoint=" << OverlayCapToken(decision.endpoint)
               << ":airport=" << OverlayCapToken(decision.airportIcao)
               << ":currentKey="
               << OverlayCapToken(decision.currentBehaviorKey)
               << ":sourceOwnedKey="
               << OverlayCapToken(
                      decision.sourceOwnedStableCompletionKey)
               << ":generatedFallbackKey="
               << OverlayCapToken(
                      decision.generatedFallbackStableCompletionKey)
               << ":currentKeySource="
               << OverlayCapToken(decision.currentBehaviorKeySource)
               << ":sourceOwnedPresent="
               << (decision.sourceOwnedKeyPresent ? 1 : 0)
               << ":migrationReady="
               << (decision.sourceOwnedKeyMigrationReady ? 1 : 0)
               << ":behaviorConsumer="
               << (decision.behaviorConsumerEnabled ? 1 : 0)
               << ":dedupeCurrent="
               << OverlayCapToken(decision.dryRunDedupeGroupCurrent)
               << ":dedupeSourceOwned="
               << OverlayCapToken(decision.dryRunDedupeGroupSourceOwned)
               << ":dedupeWouldChange="
               << (decision.dryRunDedupeGroupWouldChange ? 1 : 0)
               << ":duplicateSuppressionWouldChange="
               << (decision.dryRunDuplicateSuppressionWouldChange ? 1 : 0)
               << ":completionIdentityWouldChange="
               << (decision.dryRunCompletionIdentityWouldChange ? 1 : 0)
               << ":phaseCurrent="
               << (decision.dryRunPhaseReuseMatchCurrent ? 1 : 0)
               << ":phaseSourceOwned="
               << (decision.dryRunPhaseReuseMatchSourceOwned ? 1 : 0)
               << ":phaseWouldChange="
               << (decision.dryRunPhaseReuseWouldChange ? 1 : 0)
               << ":rowOrderingWouldChange="
               << (decision.dryRunRowOrderingWouldChange ? 1 : 0)
               << ":overlayCapWouldChange="
               << (decision.dryRunOverlayCapWouldChange ? 1 : 0)
               << ":moreAtcWouldChange="
               << (decision.dryRunMoreAtcWouldChange ? 1 : 0)
               << ":drift="
               << (decision.dryRunDriftDetected ? 1 : 0)
               << ":driftReason="
               << OverlayCapToken(decision.dryRunDriftReason)
               << ":safeForOptIn="
               << (decision.dryRunSafeForOptIn ? 1 : 0)
               << ":blockedReason="
               << OverlayCapToken(decision.dryRunBlockedReason);
        rows.push_back(stream.str());
    }
    return rows;
}

std::vector<std::string> ExtractDisplayStableKeyShadowRows(
    const xvatsim::brain::BrainDisplayIntentOutput& output) {
    std::vector<std::string> rows;
    rows.reserve(output.sourceOwnedFallbackStableKeyShadowDecisions.size());
    for (const auto& decision :
         output.sourceOwnedFallbackStableKeyShadowDecisions) {
        std::ostringstream stream;
        stream << "id=" << OverlayCapToken(decision.shadowDecisionId)
               << ":dryRun="
               << OverlayCapToken(
                      decision.dryRunStableKeyConsumerDecisionId)
               << ":subject=" << OverlayCapToken(decision.subjectKey)
               << ":" << OverlayCapToken(decision.callsign)
               << "@" << OverlayCapToken(decision.frequency)
               << ":role=" << StationRoleToToken(decision.role)
               << ":endpoint=" << OverlayCapToken(decision.endpoint)
               << ":airport=" << OverlayCapToken(decision.airportIcao)
               << ":gateEnabled="
               << (decision.sourceOwnedFallbackShadowGateEnabled ? 1 : 0)
               << ":gateSource="
               << OverlayCapToken(decision.sourceOwnedFallbackShadowGateSource)
               << ":attempted="
               << (decision.shadowRecomputeAttempted ? 1 : 0)
               << ":skipped="
               << OverlayCapToken(decision.shadowRecomputeSkippedReason)
               << ":behaviorConsumer="
               << (decision.shadowBehaviorConsumerEnabled ? 1 : 0)
               << ":hashCurrent="
               << OverlayCapToken(decision.shadowFinalBoardHashCurrent)
               << ":hashSourceOwned="
               << OverlayCapToken(decision.shadowFinalBoardHashSourceOwned)
               << ":hashMatches="
               << (decision.shadowFinalBoardHashMatches ? 1 : 0)
               << ":rowOrderingMatches="
               << (decision.shadowRowOrderingMatches ? 1 : 0)
               << ":dedupeMatches="
               << (decision.shadowDedupeGroupsMatch ? 1 : 0)
               << ":duplicateSuppressionMatches="
               << (decision.shadowDuplicateSuppressionMatches ? 1 : 0)
               << ":completionIdentityMatches="
               << (decision.shadowCompletionIdentityMatches ? 1 : 0)
               << ":phaseReuseMatches="
               << (decision.shadowPhaseReuseMatches ? 1 : 0)
               << ":overlayCapMatches="
               << (decision.shadowOverlayCapMatches ? 1 : 0)
               << ":moreAtcMatches="
               << (decision.shadowMoreAtcMatches ? 1 : 0)
               << ":missingPlanBlocked="
               << (decision.shadowMissingPlanContextBlocked ? 1 : 0)
               << ":drift="
               << (decision.shadowDriftDetected ? 1 : 0)
               << ":driftReason="
               << OverlayCapToken(decision.shadowDriftReason)
               << ":safeForFutureLiveOptIn="
               << (decision.shadowSafeForFutureLiveOptIn ? 1 : 0);
        rows.push_back(stream.str());
    }
    return rows;
}

std::vector<std::string>
ExtractDisplayStableKeyLiveConsumptionReadinessRows(
    const xvatsim::brain::BrainDisplayIntentOutput& output) {
    std::vector<std::string> rows;
    rows.reserve(
        output
            .sourceOwnedFallbackStableKeyLiveConsumptionReadinessDecisions
            .size());
    for (const auto& decision :
         output.sourceOwnedFallbackStableKeyLiveConsumptionReadinessDecisions) {
        std::ostringstream stream;
        stream << "id=" << OverlayCapToken(decision.readinessDecisionId)
               << ":shadow=" << OverlayCapToken(decision.shadowDecisionId)
               << ":dryRun="
               << OverlayCapToken(
                      decision.dryRunStableKeyConsumerDecisionId)
               << ":subject=" << OverlayCapToken(decision.subjectKey)
               << ":" << OverlayCapToken(decision.callsign)
               << "@" << OverlayCapToken(decision.frequency)
               << ":role=" << StationRoleToToken(decision.role)
               << ":endpoint=" << OverlayCapToken(decision.endpoint)
               << ":airport=" << OverlayCapToken(decision.airportIcao)
               << ":proposalGateArmed="
               << (decision.proposalGateArmed ? 1 : 0)
               << ":proposalGateSource="
               << OverlayCapToken(decision.proposalGateSource)
               << ":shadowGateEnabled="
               << (decision.shadowGateEnabled ? 1 : 0)
               << ":shadowAttempted="
               << (decision.shadowRecomputeAttempted ? 1 : 0)
               << ":shadowParityClean="
               << (decision.shadowParityClean ? 1 : 0)
               << ":planContextAvailable="
               << (decision.planContextAvailable ? 1 : 0)
               << ":shadowDrift="
               << (decision.shadowDriftDetected ? 1 : 0)
               << ":blockedReason="
               << OverlayCapToken(decision.blockedReason)
               << ":readyForFutureLiveConsumption="
               << (decision.readyForFutureLiveConsumption ? 1 : 0)
               << ":liveBehaviorConsumer="
               << (decision.liveConsumptionBehaviorEnabled ? 1 : 0)
               << ":shadowBehaviorConsumer="
               << (decision.shadowBehaviorConsumerEnabled ? 1 : 0);
        rows.push_back(stream.str());
    }
    return rows;
}

std::vector<std::string> ExtractDisplayStableKeyLiveConsumptionRows(
    const xvatsim::brain::BrainDisplayIntentOutput& output) {
    std::vector<std::string> rows;
    rows.reserve(
        output.sourceOwnedFallbackStableKeyLiveConsumptionDecisions.size());
    for (const auto& decision :
         output.sourceOwnedFallbackStableKeyLiveConsumptionDecisions) {
        std::ostringstream stream;
        stream << "id="
               << OverlayCapToken(decision.liveConsumptionDecisionId)
               << ":subject=" << OverlayCapToken(decision.subjectKey)
               << ":" << OverlayCapToken(decision.callsign)
               << "@" << OverlayCapToken(decision.frequency)
               << ":role=" << StationRoleToToken(decision.role)
               << ":endpoint=" << OverlayCapToken(decision.endpoint)
               << ":airport=" << OverlayCapToken(decision.airportIcao)
               << ":generatedFallbackKey="
               << OverlayCapToken(
                      decision.generatedFallbackStableCompletionKey)
               << ":sourceOwnedKey="
               << OverlayCapToken(
                      decision.sourceOwnedStableCompletionKey)
               << ":sourceOwnedPresent="
               << (decision.sourceOwnedKeyPresent ? 1 : 0)
               << ":migrationReady="
               << (decision.sourceOwnedKeyMigrationReady ? 1 : 0)
               << ":planContextAvailable="
               << (decision.planContextAvailable ? 1 : 0)
               << ":shadowGateEnabled="
               << (decision.shadowGateEnabled ? 1 : 0)
               << ":shadowAttempted="
               << (decision.shadowRecomputeAttempted ? 1 : 0)
               << ":shadowParityClean="
               << (decision.shadowParityClean ? 1 : 0)
               << ":shadowDrift="
               << (decision.shadowDriftDetected ? 1 : 0)
               << ":shadowHashMatches="
               << (decision.shadowFinalBoardHashMatches ? 1 : 0)
               << ":shadowRowOrderingMatches="
               << (decision.shadowRowOrderingMatches ? 1 : 0)
               << ":shadowDedupeMatches="
               << (decision.shadowDedupeGroupsMatch ? 1 : 0)
               << ":shadowDuplicateSuppressionMatches="
               << (decision.shadowDuplicateSuppressionMatches ? 1 : 0)
               << ":shadowCompletionIdentityMatches="
               << (decision.shadowCompletionIdentityMatches ? 1 : 0)
               << ":shadowPhaseReuseMatches="
               << (decision.shadowPhaseReuseMatches ? 1 : 0)
               << ":shadowOverlayCapMatches="
               << (decision.shadowOverlayCapMatches ? 1 : 0)
               << ":shadowMoreAtcMatches="
               << (decision.shadowMoreAtcMatches ? 1 : 0)
               << ":proposalGateArmed="
               << (decision.proposalGateArmed ? 1 : 0)
               << ":liveGateArmed="
               << (decision.liveConsumptionGateArmed ? 1 : 0)
               << ":liveGateSource="
               << OverlayCapToken(decision.liveConsumptionGateSource)
               << ":allowed="
               << (decision.liveConsumptionAllowed ? 1 : 0)
               << ":blockedReason="
               << OverlayCapToken(decision.liveConsumptionBlockedReason)
               << ":consumedKeyType="
               << OverlayCapToken(decision.consumedKeyType)
               << ":behaviorChanged="
               << (decision.behaviorChanged ? 1 : 0)
               << ":defaultModeProtected="
               << (decision.defaultModeProtected ? 1 : 0);
        rows.push_back(stream.str());
    }
    return rows;
}

std::vector<std::string> ExtractDisplayStableKeyAuditRows(
    const xvatsim::brain::BrainDisplayIntentOutput& output) {
    std::vector<std::string> rows;
    rows.reserve(output.stableKeyAuditDecisions.size());
    for (const auto& decision : output.stableKeyAuditDecisions) {
        std::ostringstream stream;
        stream << "id=" << OverlayCapToken(decision.stableKeyAuditDecisionId)
               << ":displayDecision="
               << OverlayCapToken(decision.displayDecisionId)
               << ":capDecision="
               << OverlayCapToken(decision.overlayCapDecisionId)
               << ":phaseReuse="
               << OverlayCapToken(decision.phaseReuseDecisionId)
               << ":sourceEvidence="
               << OverlayCapToken(decision.sourceEvidenceId)
               << ":sourceDecision="
               << OverlayCapToken(decision.sourceDecisionId)
               << ":subject=" << OverlayCapToken(decision.subjectKey)
               << ":stableKey="
               << OverlayCapToken(decision.stableCompletionKey)
               << ":present="
               << (decision.stableCompletionKeyPresent ? 1 : 0)
               << ":source="
               << OverlayCapToken(decision.stableCompletionKeySource)
               << ":status="
               << OverlayCapToken(decision.stableCompletionKeyStatus)
               << ":reason="
               << OverlayCapToken(decision.keyDerivationReason)
               << ":parts=" << (decision.keyIncludesCallsign ? 1 : 0)
               << "/" << (decision.keyIncludesRole ? 1 : 0)
               << "/" << (decision.keyIncludesFrequency ? 1 : 0)
               << "/" << (decision.keyIncludesEndpoint ? 1 : 0)
               << "/" << (decision.keyIncludesAirport ? 1 : 0)
               << ":matchesDisplay="
               << (decision.keyMatchesDisplayDecision ? 1 : 0)
               << ":matchesCap="
               << (decision.keyMatchesCapDecision ? 1 : 0)
               << ":matchesPhase="
               << (decision.keyMatchesPhaseReuseDecision ? 1 : 0)
               << ":duplicate="
               << (decision.duplicateKeyDetected ? 1 : 0)
               << ":duplicateGroup="
               << OverlayCapToken(decision.duplicateKeyGroup)
               << ":continuityKnown="
               << (decision.keyContinuityKnown ? 1 : 0)
               << ":changedAcrossReuse="
               << (decision.keyChangedAcrossReuse ? 1 : 0)
               << ":unsafeSameKey="
               << (decision.unsafeSameKeyAcrossChangedFacts ? 1 : 0)
               << ":warning=" << (decision.keyAuditWarning ? 1 : 0)
               << ":warningReason="
               << OverlayCapToken(decision.keyAuditWarningReason)
               << ":sourceOwnedKey="
               << OverlayCapToken(decision.sourceOwnedStableCompletionKey)
               << ":sourceOwnedPresent="
               << (decision.sourceOwnedStableCompletionKeyPresent ? 1 : 0)
               << ":sourceOwnedSource="
               << OverlayCapToken(
                      decision.sourceOwnedStableCompletionKeySource)
               << ":sourceOwnedShape="
               << OverlayCapToken(
                      decision.sourceOwnedStableCompletionKeyShape)
               << ":generatedFallbackKey="
               << OverlayCapToken(
                      decision.generatedFallbackStableCompletionKey)
               << ":sourceOwnedMatchesFallback="
               << (decision.sourceOwnedMatchesGeneratedFallback ? 1 : 0)
               << ":sourceOwnedMismatchReason="
               << OverlayCapToken(decision.sourceOwnedKeyMismatchReason)
               << ":sourceOwnedPlanContext="
               << OverlayCapToken(decision.sourceOwnedKeyPlanContext)
               << ":sourceOwnedPlanAvailable="
               << (decision.sourceOwnedKeyPlanContextAvailable ? 1 : 0)
               << ":sourceOwnedPlanSource="
               << OverlayCapToken(decision.sourceOwnedKeyPlanContextSource)
               << ":sourceOwnedMigrationReady="
               << (decision.sourceOwnedKeyMigrationReady ? 1 : 0)
               << ":sourceOwnedBehaviorConsumer="
               << (decision.sourceOwnedKeyBehaviorConsumerEnabled ? 1 : 0);
        rows.push_back(stream.str());
    }
    return rows;
}

std::string BrainDisplayUpstreamStableKeySourceAuditSummaryText(
    const xvatsim::brain::BrainDisplayIntentOutput& output) {
    const auto& summary = output.upstreamStableKeySourceAuditSummary;
    std::ostringstream stream;
    stream << "decisions=" << summary.upstreamStableKeyAuditCount
           << ",sourceOwned=" << summary.sourceOwnedKeyCount
           << ",evidenceId=" << summary.evidenceIdKeyCount
           << ",decisionId=" << summary.decisionIdKeyCount
           << ",fallback=" << summary.fallbackKeySourceCount
           << ",synthetic=" << summary.syntheticKeySourceCount
           << ",legacy=" << summary.legacyKeySourceCount
           << ",missing=" << summary.missingKeySourceCount
           << ",unknown=" << summary.unknownKeySourceCount
           << ",high=" << summary.highPriorityMigrationCount
           << ",medium=" << summary.mediumPriorityMigrationCount
           << ",low=" << summary.lowPriorityMigrationCount
           << ",dedupeRisk=" << summary.dedupeRiskCount
           << ",reuseRisk=" << summary.reuseContinuityRiskCount
           << ",behaviorChangeRequired="
           << summary.migrationRequiresBehaviorChangeCount
           << ",brainOwned="
           << (summary.stableKeySourceAuditBrainOwned ? 1 : 0);
    return stream.str();
}

std::vector<std::string> ExtractDisplayUpstreamStableKeySourceAuditRows(
    const xvatsim::brain::BrainDisplayIntentOutput& output) {
    std::vector<std::string> rows;
    rows.reserve(output.upstreamStableKeySourceAuditDecisions.size());
    for (const auto& decision :
         output.upstreamStableKeySourceAuditDecisions) {
        std::ostringstream stream;
        stream << "id="
               << OverlayCapToken(decision.upstreamStableKeyAuditId)
               << ":sourceClass="
               << OverlayCapToken(decision.sourceClass)
               << ":producer=" << OverlayCapToken(decision.producerName)
               << ":displayRows="
               << (decision.producesDisplayRows ? 1 : 0)
               << ":completionRows="
               << (decision.producesCompletionRows ? 1 : 0)
               << ":evidenceRows="
               << (decision.producesEvidenceRows ? 1 : 0)
               << ":stableKeyProvided="
               << (decision.stableKeyProvided ? 1 : 0)
               << ":field="
               << OverlayCapToken(decision.stableKeyFieldName)
               << ":source="
               << OverlayCapToken(decision.stableKeySource)
               << ":fallbackDownstream="
               << (decision.fallbackKeyUsedDownstream ? 1 : 0)
               << ":missingRisk="
               << (decision.missingKeyRisk ? 1 : 0)
               << ":duplicateRisk="
               << (decision.duplicateKeyRisk ? 1 : 0)
               << ":reuseRisk="
               << (decision.reuseContinuityRisk ? 1 : 0)
               << ":dedupeRisk="
               << (decision.dedupeRisk ? 1 : 0)
               << ":owner="
               << OverlayCapToken(decision.recommendedStableKeyOwner)
               << ":shape="
               << OverlayCapToken(decision.recommendedStableKeyShape)
               << ":priority="
               << OverlayCapToken(decision.migrationPriority)
               << ":blocked="
               << OverlayCapToken(decision.migrationBlockedReason)
               << ":behaviorChangeRequired="
               << (decision.behaviorChangeRequiredForMigration ? 1 : 0);
        rows.push_back(stream.str());
    }
    return rows;
}

std::vector<std::string> ExtractDisplayOverlayCapDecisionRows(
    const xvatsim::brain::BrainDisplayIntentOutput& output) {
    std::vector<std::string> rows;
    rows.reserve(output.overlayCapDecisions.size());
    for (const auto& decision : output.overlayCapDecisions) {
        std::ostringstream stream;
        stream << "id=" << decision.overlayCapDecisionId
               << ":" << decision.callsign << "@" << decision.frequency
               << ":sourceDecision="
               << OverlayCapToken(decision.sourceDecisionId)
               << ":sourceEvidence="
               << OverlayCapToken(decision.sourceEvidenceId)
               << ":displayDecision="
               << OverlayCapToken(decision.displayDecisionId)
               << ":subject=" << OverlayCapToken(decision.subjectKey)
               << ":role=" << StationRoleToToken(decision.role)
               << ":endpoint=" << OverlayCapToken(decision.endpoint)
               << ":airport=" << OverlayCapToken(decision.airportIcao)
               << ":relation="
               << xvatsim::brain::ToString(decision.displayRelation)
               << ":stage=" << WorkflowStageToString(decision.workflowStage)
               << ":before=" << decision.boardIndexBeforeCap
               << ":after=" << decision.boardIndexAfterCap
               << ":capLimit=" << decision.capLimit
               << ":visibleBefore=" << (decision.visibleBeforeCap ? 1 : 0)
               << ":visibleAfter=" << (decision.visibleAfterCap ? 1 : 0)
               << ":capped=" << (decision.cappedByOverlayLimit ? 1 : 0)
               << ":reason=" << OverlayCapToken(decision.capReason)
               << ":contributes="
               << (decision.contributesToMoreAtcCount ? 1 : 0)
               << ":moreBefore=" << decision.moreAtcCountBeforeRow
               << ":moreAfter=" << decision.moreAtcCountAfterRow
               << ":retained=" << decision.retainedVisibleRowCount
               << ":cappedHidden=" << decision.cappedHiddenRowCount
               << ":outcome="
               << OverlayCapToken(decision.finalDisplayOutcome)
               << ":confidence="
               << OverlayCapToken(decision.confidenceLevel)
               << ":fallback=" << (decision.fallbackUsed ? 1 : 0)
               << ":hardBlock=" << (decision.hardBlock ? 1 : 0)
               << ":hardBlockReason="
               << OverlayCapToken(decision.hardBlockReason)
               << ":sourceType="
               << OverlayCapToken(decision.sourceEvidenceType)
               << ":sourceDomain="
               << OverlayCapToken(decision.sourceEvidenceDomain)
               << ":sourceLinked="
               << (decision.sourceEvidenceLinked ? 1 : 0)
               << ":sourceStatus="
               << OverlayCapToken(decision.sourceEvidenceLinkStatus)
               << ":missingReason="
               << OverlayCapToken(decision.sourceEvidenceMissingReason)
               << ":sourceDecisionLinked="
               << (decision.sourceDecisionLinked ? 1 : 0)
               << ":displayDecisionLinked="
               << (decision.displayDecisionLinked ? 1 : 0)
               << ":capDecisionLinked="
               << (decision.capDecisionLinked ? 1 : 0)
               << ":linkConfidence="
               << OverlayCapToken(decision.linkageConfidence)
               << ":linkFallback="
               << (decision.linkageFallbackUsed ? 1 : 0);
        rows.push_back(stream.str());
    }
    return rows;
}

std::vector<std::string> ExtractOverlayBodyLines(
    const xvatsim::brain::OverlayViewModel& overlayModel) {
    std::vector<std::string> lines;
    lines.reserve(overlayModel.bodyLines.size());
    for (const auto& line : overlayModel.bodyLines) {
        lines.push_back(line.text);
    }
    return lines;
}

std::string OverlayToneToken(xvatsim::brain::OverlayTone tone) {
    switch (tone) {
        case xvatsim::brain::OverlayTone::Active:
            return "Active";
        case xvatsim::brain::OverlayTone::Next:
            return "Next";
        case xvatsim::brain::OverlayTone::Normal:
        default:
            return "Normal";
    }
}

std::vector<std::string> ExtractOverlayBodyTones(
    const xvatsim::brain::OverlayViewModel& overlayModel) {
    std::vector<std::string> tones;
    tones.reserve(overlayModel.bodyLines.size());
    for (const auto& line : overlayModel.bodyLines) {
        tones.push_back(OverlayToneToken(line.tone));
    }
    return tones;
}

std::string OverlayVersionToneToken(
    xvatsim::brain::OverlayVersionTone tone) {
    switch (tone) {
        case xvatsim::brain::OverlayVersionTone::Current:
            return "Current";
        case xvatsim::brain::OverlayVersionTone::UpdateAvailable:
            return "UpdateAvailable";
        case xvatsim::brain::OverlayVersionTone::Error:
            return "Error";
        case xvatsim::brain::OverlayVersionTone::Unknown:
        default:
            return "Unknown";
    }
}

std::string OverlayNoticeSeverityToken(
    xvatsim::brain::OverlayNoticeSeverity severity) {
    switch (severity) {
        case xvatsim::brain::OverlayNoticeSeverity::Success:
            return "Success";
        case xvatsim::brain::OverlayNoticeSeverity::Warning:
            return "Warning";
        case xvatsim::brain::OverlayNoticeSeverity::Error:
            return "Error";
        case xvatsim::brain::OverlayNoticeSeverity::Info:
        default:
            return "Info";
    }
}

std::vector<std::string> ExtractOverlayNoticeBodyLines(
    const xvatsim::brain::OverlayViewModel& overlayModel) {
    return overlayModel.systemNotice.bodyLines;
}

xvatsim::brain::OverlayUpdateStatus OverlayUpdateStatusFromChecker(
    xvatsim::modules::update_checker::UpdateStatus status) {
    using xvatsim::modules::update_checker::UpdateStatus;
    switch (status) {
        case UpdateStatus::Available:
            return xvatsim::brain::OverlayUpdateStatus::Available;
        case UpdateStatus::Current:
            return xvatsim::brain::OverlayUpdateStatus::Current;
        case UpdateStatus::CheckFailed:
            return xvatsim::brain::OverlayUpdateStatus::Failed;
        case UpdateStatus::InProgress:
            return xvatsim::brain::OverlayUpdateStatus::Checking;
        case UpdateStatus::Unknown:
        default:
            return xvatsim::brain::OverlayUpdateStatus::Unknown;
    }
}

std::vector<std::string> ExtractSectorIdentifiers(
    const std::vector<xvatsim::brain::RouteSectorMatchSnapshot>& sectors) {
    std::vector<std::string> identifiers;
    identifiers.reserve(sectors.size());
    for (const auto& sector : sectors) {
        identifiers.push_back(sector.identifier);
    }
    return identifiers;
}

std::vector<std::string> ExtractSectorControllerPrefixes(
    const std::vector<xvatsim::brain::RouteSectorMatchSnapshot>& sectors) {
    std::vector<std::string> values;
    values.reserve(sectors.size());
    for (const auto& sector : sectors) {
        auto prefixes = sector.controllerPrefixes;
        std::sort(prefixes.begin(), prefixes.end());
        std::ostringstream stream;
        stream << sector.identifier << ":";
        for (std::size_t index = 0; index < prefixes.size(); ++index) {
            if (index > 0) {
                stream << ">";
            }
            stream << prefixes[index];
        }
        values.push_back(stream.str());
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractSectorControllerPatterns(
    const std::vector<xvatsim::brain::RouteSectorMatchSnapshot>& sectors) {
    std::vector<std::string> values;
    values.reserve(sectors.size());
    for (const auto& sector : sectors) {
        auto patterns = sector.controllerCallsignPatterns;
        std::sort(patterns.begin(), patterns.end());
        std::ostringstream stream;
        stream << sector.identifier << ":";
        for (std::size_t index = 0; index < patterns.size(); ++index) {
            if (index > 0) {
                stream << ">";
            }
            stream << patterns[index];
        }
        values.push_back(stream.str());
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractCoverageMatchTokens(
    const xvatsim::brain::AirportSectorSnapshot& snapshot) {
    std::vector<std::string> values;
    for (const auto& sector : snapshot.coveringSectors) {
        auto tokens = sector.matchTokens;
        std::sort(tokens.begin(), tokens.end());
        std::ostringstream stream;
        stream << sector.identifier << ":";
        for (std::size_t index = 0; index < tokens.size(); ++index) {
            if (index > 0) {
                stream << ">";
            }
            stream << tokens[index];
        }
        values.push_back(stream.str());
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractCoverageControllerPrefixes(
    const xvatsim::brain::AirportSectorSnapshot& snapshot) {
    std::vector<std::string> values;
    for (const auto& sector : snapshot.coveringSectors) {
        auto prefixes = sector.controllerPrefixes;
        std::sort(prefixes.begin(), prefixes.end());
        std::ostringstream stream;
        stream << sector.identifier << ":";
        for (std::size_t index = 0; index < prefixes.size(); ++index) {
            if (index > 0) {
                stream << ">";
            }
            stream << prefixes[index];
        }
        values.push_back(stream.str());
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractCoverageGenerations(
    const xvatsim::brain::AirportSectorSnapshot& snapshot) {
    return {
        "center:" + std::to_string(snapshot.centerBoundaryGeneration),
        "authority:" + std::to_string(snapshot.authorityCatalogGeneration),
        "terminal:" + std::to_string(snapshot.terminalCoverageGeneration),
    };
}

std::vector<std::string> ExtractRouteGenerations(
    const xvatsim::brain::RouteSectorSnapshot& snapshot) {
    return {
        "center:" + std::to_string(snapshot.centerBoundaryGeneration),
        "authority:" + std::to_string(snapshot.authorityCatalogGeneration),
    };
}

std::vector<std::string> ExtractAuthorityCatalogIds(
    const xvatsim::core::authority::ControllerAuthorityCatalog& catalog) {
    std::vector<std::string> values;
    values.reserve(catalog.authorities.size());
    for (const auto& authority : catalog.authorities) {
        values.push_back(authority.id);
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityDataGaps(
    const xvatsim::core::authority::ControllerAuthorityCatalog& catalog) {
    std::vector<std::string> values;
    values.reserve(catalog.dataGaps.size());
    for (const auto& gap : catalog.dataGaps) {
        values.push_back(
            gap.authorityId + ":" + gap.polygonKey + ":" + gap.reason);
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityActiveMatches(
    const std::vector<xvatsim::core::authority::ActiveControllerAuthority>& matches) {
    std::vector<std::string> values;
    values.reserve(matches.size());
    for (const auto& match : matches) {
        values.push_back(
            match.callsign + ":" +
            match.authorityId + ":" +
            match.polygonKey + ":" +
            match.matchedPattern);
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityPolygonIds(
    const xvatsim::core::authority::AuthorityPolygonCatalog& catalog) {
    std::vector<std::string> values;
    values.reserve(catalog.polygons.size());
    for (const auto& polygon : catalog.polygons) {
        values.push_back(polygon.id);
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityPolygonLookupKeys(
    const xvatsim::core::authority::AuthorityPolygonCatalog& catalog) {
    std::vector<std::string> values;
    values.reserve(catalog.polygons.size());
    for (const auto& polygon : catalog.polygons) {
        auto lookupKeys = polygon.lookupKeys;
        std::sort(lookupKeys.begin(), lookupKeys.end());
        std::ostringstream stream;
        stream << polygon.id << ":";
        for (std::size_t index = 0; index < lookupKeys.size(); ++index) {
            if (index > 0) {
                stream << ">";
            }
            stream << lookupKeys[index];
        }
        values.push_back(stream.str());
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityPolygonRingCounts(
    const xvatsim::core::authority::AuthorityPolygonCatalog& catalog) {
    std::vector<std::string> values;
    values.reserve(catalog.polygons.size());
    for (const auto& polygon : catalog.polygons) {
        values.push_back(polygon.id + ":" + std::to_string(polygon.rings.size()));
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityPolygonDataGaps(
    const xvatsim::core::authority::AuthorityPolygonCatalog& catalog) {
    std::vector<std::string> values;
    values.reserve(catalog.dataGaps.size());
    for (const auto& gap : catalog.dataGaps) {
        values.push_back(
            gap.authorityId + ":" + gap.polygonKey + ":" + gap.reason);
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityActivePolygonMatches(
    const std::vector<xvatsim::core::authority::ActiveAuthorityPolygon>& activePolygons) {
    std::vector<std::string> values;
    values.reserve(activePolygons.size());
    for (const auto& activePolygon : activePolygons) {
        values.push_back(
            activePolygon.callsign + ":" +
            activePolygon.authorityId + ":" +
            activePolygon.polygonId + ":" +
            activePolygon.polygonKey + ":" +
            activePolygon.matchedPattern);
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityActivePolygonDataGaps(
    const std::vector<xvatsim::core::authority::AuthorityDataGap>& dataGaps) {
    std::vector<std::string> values;
    values.reserve(dataGaps.size());
    for (const auto& gap : dataGaps) {
        values.push_back(
            gap.authorityId + ":" + gap.polygonKey + ":" + gap.reason);
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityRelevantPolygonMatches(
    const std::vector<xvatsim::core::authority::RelevantAuthorityPolygon>& relevantPolygons) {
    std::vector<std::string> values;
    values.reserve(relevantPolygons.size());
    for (const auto& relevantPolygon : relevantPolygons) {
        const auto roundedEntryNm =
            static_cast<int>(std::round(std::max(0.0, relevantPolygon.routeEntryDistanceNm)));
        values.push_back(
            relevantPolygon.activePolygon.callsign + ":" +
            relevantPolygon.activePolygon.authorityId + ":" +
            relevantPolygon.activePolygon.polygonId + ":aircraft=" +
            (relevantPolygon.aircraftInside ? "1" : "0") + ":route=" +
            (relevantPolygon.routeIntersects ? "1" : "0") + ":entry=" +
            std::to_string(roundedEntryNm));
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityRelevanceMatches(
    const xvatsim::brain::AuthorityRelevanceSnapshot& snapshot) {
    std::vector<std::string> values;
    values.reserve(snapshot.relevantAuthorities.size());
    for (const auto& authority : snapshot.relevantAuthorities) {
        const auto roundedEntryNm =
            static_cast<int>(std::round(std::max(0.0, authority.routeEntryDistanceNm)));
        values.push_back(
            authority.callsign + ":" +
            authority.authorityId + ":" +
            authority.polygonId + ":aircraft=" +
            (authority.aircraftInside ? "1" : "0") + ":route=" +
            (authority.routeIntersects ? "1" : "0") + ":entry=" +
            std::to_string(roundedEntryNm));
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityRelevanceDiagnostics(
    const xvatsim::brain::AuthorityRelevanceSnapshot& snapshot) {
    auto diagnostics = snapshot.diagnostics;
    std::sort(diagnostics.begin(), diagnostics.end());
    return diagnostics;
}

std::vector<std::string> ExtractAuthorityRelevanceProofSources(
    const xvatsim::brain::AuthorityRelevanceSnapshot& snapshot) {
    std::vector<std::string> values;
    values.reserve(snapshot.relevantAuthorities.size());
    for (const auto& authority : snapshot.relevantAuthorities) {
        values.push_back(authority.callsign + ":" + authority.proofSource);
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityRelevanceProofDetails(
    const xvatsim::brain::AuthorityRelevanceSnapshot& snapshot) {
    std::vector<std::string> values;
    values.reserve(snapshot.relevantAuthorities.size());
    for (const auto& authority : snapshot.relevantAuthorities) {
        values.push_back(authority.callsign + ":" + authority.proofDetail);
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::string JoinPipeOrNone(std::vector<std::string> values) {
    if (values.empty()) {
        return "<none>";
    }
    std::sort(values.begin(), values.end());
    std::ostringstream stream;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            stream << "|";
        }
        stream << values[i];
    }
    return stream.str();
}

std::string RoundedEvidenceText(double value) {
    if (!std::isfinite(value)) {
        return "max";
    }
    return std::to_string(static_cast<int>(std::round(value)));
}

std::string AuthorityRelevanceEvidenceVisibilitySummary(
    const xvatsim::brain::AuthorityRelevanceSnapshot& snapshot) {
    const auto& source = snapshot.evidence.source;
    std::ostringstream stream;
    stream << "scheduled=" << (source.scheduled ? 1 : 0)
           << ",source=" << source.sourceControllerCount
           << ",evidence=" << snapshot.evidence.controllerEvidence.size()
           << ",dropped=" << snapshot.droppedBeforeBrainControllers
           << ",compatOnly="
           << (snapshot.relevantAuthoritiesCompatibilityOnly ? 1 : 0)
           << ",compatRelevant="
           << snapshot.compatibilityRelevantAuthorityCount
           << ",feed=" << (source.controllerFeedAvailable ? 1 : 0)
           << "/" << (source.controllerFeedStale ? 1 : 0)
           << ",route=" << (source.routeSnapshotAvailable ? 1 : 0)
           << "/" << (source.routeSnapshotStale ? 1 : 0)
           << "/" << (source.routeResolved ? 1 : 0)
           << ",stage=" << source.workStage
           << ",window="
           << static_cast<int>(std::round(std::max(0.0, source.workWindowNm)))
           << ",deferred=" << source.workDeferredSectorCount
           << ",routeKeys=" << source.routeAuthorityKeys.size()
           << ",matchKeys=" << source.routeAuthorityMatchKeys.size()
           << ",tx="
           << (source.authorityTransceiverSnapshotPresent ? 1 : 0)
           << "/" << (source.authorityTransceiverAvailable ? 1 : 0)
           << "/" << (source.authorityTransceiverStale ? 1 : 0)
           << "/" << source.authorityTransceiverCandidateCount;
    return stream.str();
}

std::vector<std::string> ExtractAuthorityControllerEvidence(
    const xvatsim::brain::AuthorityRelevanceSnapshot& snapshot) {
    std::vector<std::string> values;
    values.reserve(snapshot.evidence.controllerEvidence.size());
    for (const auto& controller : snapshot.evidence.controllerEvidence) {
        std::ostringstream stream;
        stream << controller.callsign
               << ":freq=" << controller.frequency
               << ":fac=" << controller.facility
               << ":act=" << (controller.actionable ? 1 : 0)
               << ":atis=" << (controller.atis ? 1 : 0)
               << ":guard=" << (controller.guardFrequency ? 1 : 0)
               << ":empty=" << (controller.emptyCallsign ? 1 : 0)
               << ":local=" << (controller.airportLocalCandidate ? 1 : 0)
               << ":airspace="
               << (controller.airspaceAuthorityCandidate ? 1 : 0)
               << ":considered="
               << (controller.sourceControllerConsidered ? 1 : 0)
               << ":reasons="
               << JoinPipeOrNone(controller.evidenceReasons)
               << ":decisions=" << controller.authorityDecisions.size()
               << ":active=" << controller.activePolygons.size();
        values.push_back(stream.str());
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityDecisionEvidence(
    const xvatsim::brain::AuthorityRelevanceSnapshot& snapshot) {
    std::vector<std::string> values;
    for (const auto& controller : snapshot.evidence.controllerEvidence) {
        for (const auto& decision : controller.authorityDecisions) {
            std::ostringstream stream;
            stream << controller.callsign
                   << ":" << decision.authorityId
                   << ":" << decision.authoritySource
                   << ":" << decision.authorityKind
                   << ":" << decision.polygonKey
                   << ":accepted=" << (decision.accepted ? 1 : 0)
                   << ":routeScope="
                   << (decision.oldRouteScopeMatched ? 1 : 0)
                   << ":oldRelevant="
                   << (decision.oldRelevantAuthoritySurvivor ? 1 : 0)
                   << ":pattern=" << decision.matchedPattern
                   << ":reject="
                   << JoinPipeOrNone(decision.rejectionReasons);
            values.push_back(stream.str());
        }
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityPolygonEvidence(
    const xvatsim::brain::AuthorityRelevanceSnapshot& snapshot) {
    std::vector<std::string> values;
    values.reserve(snapshot.evidence.polygonEvidence.size());
    for (const auto& polygon : snapshot.evidence.polygonEvidence) {
        std::ostringstream stream;
        stream << polygon.polygonId
               << ":key=" << polygon.polygonKey
               << ":src=" << polygon.authoritySource
               << ":kind=" << polygon.authorityKind
               << ":routeKey=" << (polygon.routeKeyMatch ? 1 : 0)
               << ":family=" << (polygon.routeFamilyMatch ? 1 : 0)
               << ":endpoint=" << (polygon.routeEndpointMatch ? 1 : 0)
               << ":scoped=" << (polygon.inOldScopedCatalog ? 1 : 0)
               << ":geom=" << (polygon.routeGeometryRelevant ? 1 : 0)
               << ":inside=" << (polygon.aircraftInside ? 1 : 0)
               << ":route=" << (polygon.routeIntersects ? 1 : 0)
               << ":entry=" << RoundedEvidenceText(polygon.routeEntryDistanceNm)
               << ":oldRelevant="
               << (polygon.oldCompatibilityRelevantSurvivor ? 1 : 0)
               << ":scopedReason="
               << (polygon.oldScopedOutReason.empty()
                       ? std::string("<none>")
                       : polygon.oldScopedOutReason);
        values.push_back(stream.str());
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityActivePolygonEvidence(
    const xvatsim::brain::AuthorityRelevanceSnapshot& snapshot) {
    std::vector<std::string> values;
    values.reserve(snapshot.evidence.activePolygonEvidence.size());
    for (const auto& polygon : snapshot.evidence.activePolygonEvidence) {
        std::ostringstream stream;
        stream << polygon.callsign
               << ":" << polygon.authorityId
               << ":" << polygon.polygonId
               << ":key=" << polygon.polygonKey
               << ":src=" << polygon.authoritySource
               << ":kind=" << polygon.authorityKind
               << ":proof=" << polygon.activeProofSource
               << ":active=" << (polygon.activePolygon ? 1 : 0)
               << ":routeKey=" << (polygon.routeKeyMatch ? 1 : 0)
               << ":routeCompat=" << (polygon.routeKeyCompatible ? 1 : 0)
               << ":geoCompat=" << (polygon.geometryCompatible ? 1 : 0)
               << ":oldRelevant="
               << (polygon.oldCompatibilityRelevantSurvivor ? 1 : 0)
               << ":reason="
               << (polygon.compatibilityFilteredReason.empty()
                       ? std::string("<none>")
                       : polygon.compatibilityFilteredReason);
        if (!polygon.activeProofDetail.empty()) {
            stream << ":detail=" << polygon.activeProofDetail;
        }
        values.push_back(stream.str());
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityTransceiverProofEvidence(
    const xvatsim::brain::AuthorityRelevanceSnapshot& snapshot) {
    std::vector<std::string> values;
    values.reserve(snapshot.evidence.transceiverRouteProofEvidence.size());
    for (const auto& proof : snapshot.evidence.transceiverRouteProofEvidence) {
        std::ostringstream stream;
        stream << proof.callsign
               << ":stationCount=" << proof.stationCandidateCount
               << ":station=" << proof.stationCallsign << "@"
               << proof.stationFrequency
               << ":polygon=" << proof.polygonKey
               << ":src=" << proof.authoritySource
               << ":kind=" << proof.authorityKind
               << ":dist="
               << RoundedEvidenceText(proof.stationPolygonDistanceNm)
               << ":tol=" << RoundedEvidenceText(proof.toleranceNm)
               << ":within=" << (proof.withinTolerance ? 1 : 0)
               << ":owner=" << (proof.sourceOwnershipMatch ? 1 : 0)
               << ":unownedBorder="
               << (proof.unownedBorderMismatch ? 1 : 0)
               << ":blocked="
               << (proof.blockedByDirectActiveProof ? 1 : 0)
               << ":noStation=" << (proof.noStationCandidates ? 1 : 0)
               << ":score=" << RoundedEvidenceText(proof.stationScore)
               << ":bestScore=" << (proof.bestByModuleScore ? 1 : 0)
               << ":oldProof=" << (proof.oldProofSurvivor ? 1 : 0)
               << ":reason="
               << (proof.proofRejectionReason.empty()
                       ? std::string("<none>")
                       : proof.proofRejectionReason);
        values.push_back(stream.str());
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractAuthorityDuplicatedAtisProofEvidence(
    const xvatsim::brain::AuthorityRelevanceSnapshot& snapshot) {
    std::vector<std::string> values;
    values.reserve(snapshot.evidence.duplicatedAtisProofEvidence.size());
    for (const auto& proof : snapshot.evidence.duplicatedAtisProofEvidence) {
        std::ostringstream stream;
        stream << proof.callsign
               << ":token=" << proof.coveredToken
               << ":text=" << (proof.textAtisPresent ? 1 : 0)
               << ":tokens=" << JoinPipeOrNone(proof.extractedCoveredTokens)
               << ":aliases=" << JoinPipeOrNone(proof.matchedRouteAuthorityAliases)
               << ":auth=" << proof.authorityId
               << ":src=" << proof.authoritySource
               << ":kind=" << proof.authorityKind
               << ":polygon=" << proof.polygonKey
               << ":allowed=" << (proof.sourceKindAllowed ? 1 : 0)
               << ":routePolygon="
               << (proof.routeRelevantPolygonFound ? 1 : 0)
               << ":facility=" << (proof.facilityEligible ? 1 : 0)
               << ":missingOwner="
               << (proof.missingSourceOwnership ? 1 : 0)
               << ":oldProof=" << (proof.oldProofSurvivor ? 1 : 0)
               << ":reason="
               << (proof.proofRejectionReason.empty()
                       ? std::string("<none>")
                       : proof.proofRejectionReason);
        values.push_back(stream.str());
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> ExtractSourceManifestValues(
    const xvatsim::core::source_data::MapDataManifest& manifest) {
    std::vector<std::string> values{
        "commit:" + manifest.currentCommitHash,
        "dat:" + manifest.firBoundariesDatUrl,
        "geojson:" + manifest.firBoundariesGeoJsonUrl,
        "simaware:" + manifest.simawareTraconGeoJsonUrl,
        "vatspy:" + manifest.vatspyDatUrl,
        "vatglasses:" + manifest.vatglassesOwnershipUrl,
    };
    if (!manifest.vatglassesDynamicBaseUrl.empty()) {
        values.push_back("vatglasses_base:" + manifest.vatglassesDynamicBaseUrl);
    }
    if (!manifest.vatglassesPositionsUrl.empty()) {
        values.push_back("vatglasses_positions:" + manifest.vatglassesPositionsUrl);
    }
    if (!manifest.vatglassesAirspaceUrl.empty()) {
        values.push_back("vatglasses_airspace:" + manifest.vatglassesAirspaceUrl);
    }
    if (!manifest.vatglassesDynamicOwnershipUrl.empty()) {
        values.push_back("vatglasses_dynamic_ownership:" +
                         manifest.vatglassesDynamicOwnershipUrl);
    }
    if (!manifest.vatglassesDynamicOwnershipFile.empty()) {
        values.push_back("vatglasses_dynamic_ownership_file:" +
                         manifest.vatglassesDynamicOwnershipFile);
    }
    if (!manifest.specialSectorDataUrl.empty()) {
        values.push_back("special_sector_data:" + manifest.specialSectorDataUrl);
    }
    for (const auto& url : manifest.specialSectorDataUrls) {
        values.push_back("special_sector_data_url:" + url);
    }
    if (!manifest.terminalAuthorityDataUrl.empty()) {
        values.push_back("terminal_authority_data:" + manifest.terminalAuthorityDataUrl);
    }
    for (const auto& url : manifest.terminalAuthorityDataUrls) {
        values.push_back("terminal_authority_data_url:" + url);
    }
    if (!manifest.authoritySourceRegistryUrl.empty()) {
        values.push_back("authority_source_registry:" + manifest.authoritySourceRegistryUrl);
    }
    for (const auto& url : manifest.authoritySourceRegistryUrls) {
        values.push_back("authority_source_registry_url:" + url);
    }
    return values;
}

std::vector<std::string> ExtractSourceRegistryValues(
    const std::vector<std::string>& registryJsons) {
    std::vector<std::string> values;
    for (const auto& registryJson : registryJsons) {
        for (const auto& entry :
             xvatsim::core::source_data::ParseAuthoritySourceRegistryJson(
                 registryJson)) {
            if (entry.source == "VATGLASSES_DYNAMIC_DIRECTORY") {
                values.push_back(
                    entry.source + ":" +
                    entry.positionsUrl + "|" +
                    entry.airspaceUrl + "|" +
                    entry.ownershipUrl);
            } else {
                values.push_back(entry.source + ":" + entry.url);
            }
        }
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<xvatsim::core::source_data::AuthoritySourceRegistryEntry>
ExtractSourceRegistryEntries(const std::vector<std::string>& registryJsons) {
    std::vector<xvatsim::core::source_data::AuthoritySourceRegistryEntry> entries;
    for (const auto& registryJson : registryJsons) {
        auto parsedEntries =
            xvatsim::core::source_data::ParseAuthoritySourceRegistryJson(
                registryJson);
        entries.insert(
            entries.end(),
            std::make_move_iterator(parsedEntries.begin()),
            std::make_move_iterator(parsedEntries.end()));
    }
    return entries;
}

std::vector<std::string> ExtractSourceRegistrySourceCounts(
    const std::vector<std::string>& registryJsons) {
    std::unordered_map<std::string, int> countsBySource;
    for (const auto& entry : ExtractSourceRegistryEntries(registryJsons)) {
        ++countsBySource[entry.source];
    }
    std::vector<std::string> values;
    values.reserve(countsBySource.size());
    for (const auto& [source, count] : countsBySource) {
        values.push_back(source + ":" + std::to_string(count));
    }
    std::sort(values.begin(), values.end());
    return values;
}

xvatsim::brain::AuthorityRelevanceKind ToBrainAuthorityKind(
    xvatsim::core::authority::AuthorityKind kind) {
    switch (kind) {
        case xvatsim::core::authority::AuthorityKind::Terminal:
            return xvatsim::brain::AuthorityRelevanceKind::Terminal;
        case xvatsim::core::authority::AuthorityKind::Extension:
            return xvatsim::brain::AuthorityRelevanceKind::Extension;
        case xvatsim::core::authority::AuthorityKind::Center:
            return xvatsim::brain::AuthorityRelevanceKind::Center;
    }

    return xvatsim::brain::AuthorityRelevanceKind::Center;
}

std::vector<std::string> ExtractAuthorityGaps(
    const xvatsim::brain::RouteSectorSnapshot& snapshot) {
    std::vector<std::string> gaps;
    auto appendGaps = [&](const auto& sectors, const std::string& label) {
        for (const auto& sector : sectors) {
            if (!sector.controllerCallsignPatterns.empty() ||
                !sector.controllerPrefixes.empty()) {
                continue;
            }
            gaps.push_back(label + ":" + sector.identifier);
        }
    };

    appendGaps(snapshot.currentSectors, "current");
    appendGaps(snapshot.nextSectors, "next");
    return gaps;
}

std::vector<std::string> ExtractTokenKinds(
    const std::vector<xvatsim::core::route::ParsedRouteToken>& tokens) {
    std::vector<std::string> values;
    values.reserve(tokens.size());
    for (const auto& token : tokens) {
        values.push_back(
            token.normalized + ":" +
            xvatsim::core::route::RouteTokenKindToString(token.kind));
    }
    return values;
}

std::vector<std::string> ExtractWaypointIdents(
    const std::vector<xvatsim::brain::RouteWaypointSnapshot>& waypoints) {
    std::vector<std::string> identifiers;
    identifiers.reserve(waypoints.size());
    for (const auto& waypoint : waypoints) {
        identifiers.push_back(waypoint.ident);
    }
    return identifiers;
}

std::string FormatWaypointPoint(const xvatsim::brain::RouteWaypointSnapshot& waypoint) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(4);
    stream << waypoint.ident << "@" << waypoint.latitudeDeg << "," << waypoint.longitudeDeg;
    return stream.str();
}

std::vector<std::string> ExtractWaypointPoints(
    const std::vector<xvatsim::brain::RouteWaypointSnapshot>& waypoints) {
    std::vector<std::string> points;
    points.reserve(waypoints.size());
    for (const auto& waypoint : waypoints) {
        points.push_back(FormatWaypointPoint(waypoint));
    }
    return points;
}

std::vector<std::string> ExtractRouteDiagnostics(
    const std::vector<std::string>& tokens) {
    return tokens;
}

xvatsim::brain::RouteSectorMatchSnapshot MakeBrainRouteSector(
    std::string identifier,
    double entryDistanceNm,
    std::vector<std::string> controllerPatterns = {}) {
    xvatsim::brain::RouteSectorMatchSnapshot sector;
    sector.identifier = std::move(identifier);
    sector.entryDistanceNm = entryDistanceNm;
    sector.matchTokens.push_back(sector.identifier);
    sector.controllerCallsignPatterns = std::move(controllerPatterns);
    sector.centerCoverage = true;
    return sector;
}

xvatsim::brain::NetworkPlanSnapshot MakeBrainRoutePlanNetworkSnapshot(
    std::string callsign,
    std::string departureIcao,
    std::string destinationIcao,
    std::string routeText) {
    xvatsim::brain::NetworkPlanSnapshot snapshot;
    snapshot.feedAvailable = true;
    snapshot.stale = false;
    snapshot.matched = true;
    snapshot.matchedCallsign = std::move(callsign);
    snapshot.departureIcao = std::move(departureIcao);
    snapshot.destinationIcao = std::move(destinationIcao);
    snapshot.routeText = std::move(routeText);
    return snapshot;
}

xvatsim::brain::RouteSectorSnapshot MakeBrainRouteSectorSnapshot(
    std::string departureIcao,
    std::string destinationIcao,
    std::vector<xvatsim::brain::RouteSectorMatchSnapshot> currentSectors,
    std::vector<xvatsim::brain::RouteSectorMatchSnapshot> nextSectors) {
    xvatsim::brain::RouteSectorSnapshot snapshot;
    snapshot.available = true;
    snapshot.stale = false;
    snapshot.routeResolved = true;
    snapshot.departureIcao = std::move(departureIcao);
    snapshot.destinationIcao = std::move(destinationIcao);
    snapshot.centerBoundaryGeneration = 1;
    snapshot.authorityCatalogGeneration = 1;
    snapshot.currentSectors = std::move(currentSectors);
    snapshot.nextSectors = std::move(nextSectors);
    snapshot.statusLine = "ROUTE harness";
    return snapshot;
}

xvatsim::brain::RouteAuthorityPlan BuildBrainRoutePlanRebuildProbe() {
    xvatsim::brain::RouteAuthorityPlan activePlan;
    std::string activeCacheKey;
    std::uint64_t activeGeneration = 0;

    auto firstNetworkPlan = MakeBrainRoutePlanNetworkSnapshot(
        "DAL100",
        "KAAA",
        "KCCC",
        "AAA BBB CCC");
    auto firstRoute = MakeBrainRouteSectorSnapshot(
        "KAAA",
        "KCCC",
        {MakeBrainRouteSector("AAA", 0.0, {"AAA_CTR"})},
        {
            MakeBrainRouteSector("BBB", 100.0, {"BBB_CTR"}),
            MakeBrainRouteSector("CCC", 200.0, {"CCC_CTR"}),
        });
    (void)xvatsim::brain::UpdateRouteAuthorityPlanCache(
        firstNetworkPlan,
        firstRoute,
        &activePlan,
        &activeCacheKey,
        &activeGeneration);

    auto rerouteNetworkPlan = MakeBrainRoutePlanNetworkSnapshot(
        "DAL100",
        "KAAA",
        "KDDD",
        "AAA DDD");
    auto reroute = MakeBrainRouteSectorSnapshot(
        "KAAA",
        "KDDD",
        {MakeBrainRouteSector("AAA", 0.0, {"AAA_CTR"})},
        {MakeBrainRouteSector("DDD", 160.0, {"DDD_CTR"})});
    return xvatsim::brain::UpdateRouteAuthorityPlanCache(
        rerouteNetworkPlan,
        reroute,
        &activePlan,
        &activeCacheKey,
        &activeGeneration);
}

xvatsim::brain::RouteAuthorityPlan BuildBrainRoutePlanPendingProbe() {
    xvatsim::brain::RouteAuthorityPlan activePlan;
    std::string activeCacheKey;
    std::uint64_t activeGeneration = 0;

    auto firstNetworkPlan = MakeBrainRoutePlanNetworkSnapshot(
        "DAL100",
        "KAAA",
        "KCCC",
        "AAA BBB CCC");
    auto firstRoute = MakeBrainRouteSectorSnapshot(
        "KAAA",
        "KCCC",
        {MakeBrainRouteSector("AAA", 0.0, {"AAA_CTR"})},
        {
            MakeBrainRouteSector("BBB", 100.0, {"BBB_CTR"}),
            MakeBrainRouteSector("CCC", 200.0, {"CCC_CTR"}),
        });
    (void)xvatsim::brain::UpdateRouteAuthorityPlanCache(
        firstNetworkPlan,
        firstRoute,
        &activePlan,
        &activeCacheKey,
        &activeGeneration);

    auto changedNetworkPlan = MakeBrainRoutePlanNetworkSnapshot(
        "DAL100",
        "KAAA",
        "KDDD",
        "BROKEN");
    xvatsim::brain::RouteSectorSnapshot unresolvedRoute;
    unresolvedRoute.available = true;
    unresolvedRoute.stale = false;
    unresolvedRoute.routeResolved = false;
    unresolvedRoute.departureIcao = "KAAA";
    unresolvedRoute.destinationIcao = "KDDD";
    unresolvedRoute.diagnosticReason = "route-sector-unresolved";
    unresolvedRoute.statusLine = "ROUTE unresolved";
    return xvatsim::brain::UpdateRouteAuthorityPlanCache(
        changedNetworkPlan,
        unresolvedRoute,
        &activePlan,
        &activeCacheKey,
        &activeGeneration);
}

std::vector<std::string> BrainWorkTypeNamesForItems(
    const std::vector<xvatsim::brain::BrainWorkItem>& items) {
    std::vector<std::string> names;
    names.reserve(items.size());
    for (const auto& item : items) {
        names.push_back(xvatsim::brain::ToString(item.type));
    }
    return names;
}

xvatsim::brain::AirportSectorSnapshot MakeBrainDepartureAirportSector(
    bool stale = false) {
    xvatsim::brain::AirportSectorSnapshot snapshot;
    snapshot.available = true;
    snapshot.stale = stale;
    snapshot.hasCenterCoverageData = true;
    snapshot.hasTerminalCoverageData = true;
    snapshot.centerBoundaryGeneration = 1;
    snapshot.authorityCatalogGeneration = 1;
    snapshot.terminalCoverageGeneration = 1;
    snapshot.airportIcao = "KAAA";
    snapshot.statusLine = stale ? "AIRPORT sectors stale" : "AIRPORT sectors active";
    auto terminalSector = MakeBrainRouteSector("AAA_APP", 0.0, {"AAA_APP"});
    terminalSector.terminalCoverage = true;
    snapshot.coveringSectors.push_back(std::move(terminalSector));
    return snapshot;
}

xvatsim::brain::ModuleBoardSnapshot MakeBrainDepartureBoard() {
    xvatsim::brain::ModuleBoardSnapshot board;
    board.available = true;
    board.source = xvatsim::brain::BoardSource::Departure;
    board.airportIcao = "KAAA";
    xvatsim::brain::BoardStationSnapshot station;
    station.role = xvatsim::brain::StationRole::Departure;
    station.callsign = "AAA_DEP";
    station.frequency = "123.450";
    board.stations.push_back(std::move(station));
    return board;
}

std::vector<std::string> BuildBrainDepartureWorkOrderProbe() {
    auto networkPlan = MakeBrainRoutePlanNetworkSnapshot(
        "DAL100",
        "KAAA",
        "KCCC",
        "AAA BBB CCC");
    auto routePlan = BuildBrainRoutePlanRebuildProbe();
    routePlan.departureIcao = "KAAA";
    routePlan.routeHash = "route-hash";
    routePlan.cacheKey = "route-cache";
    routePlan.routeMapGeneration = 4;
    if (!routePlan.polygons.empty()) {
        routePlan.polygons.front().current = true;
        routePlan.polygons.front().sequence = 1;
    }
    auto airportSector = MakeBrainDepartureAirportSector();
    return BrainWorkTypeNamesForItems(
        xvatsim::brain::BuildDepartureAuthorityWorkQueue(
            networkPlan,
            routePlan,
            airportSector));
}

xvatsim::brain::BrainWorkCyclePlan BuildBrainDepartureSchedulerProbePlan() {
    auto networkPlan = MakeBrainRoutePlanNetworkSnapshot(
        "DAL100",
        "KAAA",
        "KCCC",
        "AAA BBB CCC");
    auto routePlan = BuildBrainRoutePlanRebuildProbe();
    routePlan.departureIcao = "KAAA";
    routePlan.routeHash = "route-hash";
    routePlan.cacheKey = "route-cache";
    routePlan.routeMapGeneration = 4;
    if (!routePlan.polygons.empty()) {
        routePlan.polygons.front().current = true;
        routePlan.polygons.front().sequence = 1;
    }
    auto airportSector = MakeBrainDepartureAirportSector();
    xvatsim::brain::BrainWorkScheduler scheduler;
    return scheduler.PlanCycle(
        xvatsim::brain::BuildDepartureAuthorityWorkQueue(
            networkPlan,
            routePlan,
            airportSector));
}

std::vector<std::string> BuildBrainDepartureSchedulerRunnableProbe() {
    return BrainWorkTypeNamesForItems(
        BuildBrainDepartureSchedulerProbePlan().runnableItems);
}

std::vector<std::string> BuildBrainDepartureSchedulerDeferredProbe() {
    return BrainWorkTypeNamesForItems(
        BuildBrainDepartureSchedulerProbePlan().deferredItems);
}

std::string BuildBrainDepartureSchedulerHeavyCountsProbe() {
    const auto plan = BuildBrainDepartureSchedulerProbePlan();
    std::ostringstream stream;
    stream << "requested=" << plan.requestedHeavyCount
           << ",runnable=" << plan.runnableHeavyCount
           << ",deferred=" << plan.deferredHeavyCount;
    return stream.str();
}

xvatsim::brain::DepartureAuthoritySnapshot BuildBrainDepartureSnapshotProbe() {
    xvatsim::brain::DepartureAuthoritySnapshot activeSnapshot;
    std::string activeCacheKey;
    std::uint64_t activeGeneration = 0;
    auto networkPlan = MakeBrainRoutePlanNetworkSnapshot(
        "DAL100",
        "KAAA",
        "KCCC",
        "AAA BBB CCC");
    auto routePlan = BuildBrainRoutePlanRebuildProbe();
    routePlan.departureIcao = "KAAA";
    routePlan.routeMapGeneration = 4;
    return xvatsim::brain::UpdateDepartureAuthoritySnapshotCache(
        networkPlan,
        routePlan,
        MakeBrainDepartureAirportSector(),
        MakeBrainDepartureBoard(),
        &activeSnapshot,
        &activeCacheKey,
        &activeGeneration);
}

xvatsim::brain::DepartureAuthoritySnapshot BuildBrainDeparturePendingProbe() {
    xvatsim::brain::DepartureAuthoritySnapshot activeSnapshot;
    std::string activeCacheKey;
    std::uint64_t activeGeneration = 0;
    auto networkPlan = MakeBrainRoutePlanNetworkSnapshot(
        "DAL100",
        "KAAA",
        "KCCC",
        "AAA BBB CCC");
    auto routePlan = BuildBrainRoutePlanRebuildProbe();
    routePlan.departureIcao = "KAAA";
    routePlan.routeMapGeneration = 4;
    (void)xvatsim::brain::UpdateDepartureAuthoritySnapshotCache(
        networkPlan,
        routePlan,
        MakeBrainDepartureAirportSector(),
        MakeBrainDepartureBoard(),
        &activeSnapshot,
        &activeCacheKey,
        &activeGeneration);
    return xvatsim::brain::UpdateDepartureAuthoritySnapshotCache(
        networkPlan,
        routePlan,
        MakeBrainDepartureAirportSector(true),
        MakeBrainDepartureBoard(),
        &activeSnapshot,
        &activeCacheKey,
        &activeGeneration);
}

std::vector<xvatsim::brain::BrainWorkItem> BuildBrainWorkModelProbeQueue() {
    using namespace xvatsim::brain;

    std::vector<BrainWorkItem> items;

    BrainWorkItem futureDiagnostic;
    futureDiagnostic.type = BrainWorkType::Diagnostics;
    futureDiagnostic.priority = BrainWorkPriority::Diagnostics;
    futureDiagnostic.reason = BrainWorkReason::FutureRoutePrep;
    futureDiagnostic.budget = BrainWorkBudget::Light;
    futureDiagnostic.target.stage = WorkflowStage::Enroute;
    futureDiagnostic.cacheKey = "diag:future";
    futureDiagnostic.enqueueSequence = 99;
    items.push_back(std::move(futureDiagnostic));

    BrainWorkItem nextCenterProof;
    nextCenterProof.type = BrainWorkType::ResolveNextCenterWindow;
    nextCenterProof.priority = BrainWorkPriority::NextCenterLookahead;
    nextCenterProof.reason = BrainWorkReason::NextPolygonLookahead;
    nextCenterProof.budget = BrainWorkBudget::Heavy;
    nextCenterProof.target.stage = WorkflowStage::Enroute;
    nextCenterProof.target.polygonKey = "OAK";
    nextCenterProof.target.polygonSequence = 2;
    nextCenterProof.target.distanceToTargetNm = 190.0;
    nextCenterProof.cacheKey = "center:oak";
    nextCenterProof.enqueueSequence = 4;
    items.push_back(std::move(nextCenterProof));

    BrainWorkItem departureLocal;
    departureLocal.type = BrainWorkType::ResolveDepartureAirportLocal;
    departureLocal.priority = BrainWorkPriority::DepartureAuthority;
    departureLocal.reason = BrainWorkReason::NewFlightPlan;
    departureLocal.budget = BrainWorkBudget::Medium;
    departureLocal.target.stage = WorkflowStage::Departure;
    departureLocal.target.airportIcao = "KLAX";
    departureLocal.cacheKey = "local:klax";
    departureLocal.enqueueSequence = 1;
    items.push_back(std::move(departureLocal));

    BrainWorkItem currentCenter;
    currentCenter.type = BrainWorkType::ResolveCurrentCenter;
    currentCenter.priority = BrainWorkPriority::CurrentEnrouteAuthority;
    currentCenter.reason = BrainWorkReason::CurrentPolygonChanged;
    currentCenter.budget = BrainWorkBudget::Heavy;
    currentCenter.target.stage = WorkflowStage::Enroute;
    currentCenter.target.polygonKey = "LAX";
    currentCenter.target.polygonSequence = 1;
    currentCenter.cacheKey = "center:lax";
    currentCenter.enqueueSequence = 3;
    items.push_back(std::move(currentCenter));

    BrainWorkItem routeMap;
    routeMap.type = BrainWorkType::BuildRouteScopedMap;
    routeMap.priority = BrainWorkPriority::SafetyCurrentPosition;
    routeMap.reason = BrainWorkReason::NewFlightPlan;
    routeMap.budget = BrainWorkBudget::Heavy;
    routeMap.target.stage = WorkflowStage::Departure;
    routeMap.target.flightIdentityKey = "DAL100|KLAX|KPDX";
    routeMap.cacheKey = "route:dal100-klax-kpdx";
    routeMap.enqueueSequence = 0;
    items.push_back(std::move(routeMap));

    BrainWorkItem arrivalTerminal;
    arrivalTerminal.type = BrainWorkType::ResolveArrivalTerminal;
    arrivalTerminal.priority = BrainWorkPriority::ArrivalAuthority;
    arrivalTerminal.reason = BrainWorkReason::ArrivalWakeDistance;
    arrivalTerminal.budget = BrainWorkBudget::Medium;
    arrivalTerminal.target.stage = WorkflowStage::Arrival;
    arrivalTerminal.target.airportIcao = "KPDX";
    arrivalTerminal.cacheKey = "terminal:kpdx";
    arrivalTerminal.enqueueSequence = 6;
    items.push_back(std::move(arrivalTerminal));

    BrainWorkItem uiPublish;
    uiPublish.type = BrainWorkType::PublishUiSnapshot;
    uiPublish.priority = BrainWorkPriority::Diagnostics;
    uiPublish.reason = BrainWorkReason::UiRefresh;
    uiPublish.budget = BrainWorkBudget::Light;
    uiPublish.target.stage = WorkflowStage::Departure;
    uiPublish.cacheKey = "ui:last-proven";
    uiPublish.enqueueSequence = 5;
    items.push_back(std::move(uiPublish));

    return items;
}

std::vector<std::string> BuildBrainWorkModelOrderProbe() {
    auto items = BuildBrainWorkModelProbeQueue();
    xvatsim::brain::SortBrainWorkQueue(&items);

    std::vector<std::string> ordered;
    ordered.reserve(items.size());
    for (const auto& item : items) {
        ordered.push_back(xvatsim::brain::BrainWorkStableId(item));
    }
    return ordered;
}

std::vector<std::string> BuildBrainWorkModelHeavyProbe() {
    auto items = BuildBrainWorkModelProbeQueue();
    xvatsim::brain::SortBrainWorkQueue(&items);

    std::vector<std::string> flags;
    flags.reserve(items.size());
    for (const auto& item : items) {
        flags.push_back(
            std::string(xvatsim::brain::ToString(item.type)) +
            ":heavy=" +
            (xvatsim::brain::IsHeavyBrainWork(item) ? "1" : "0"));
    }
    return flags;
}

std::vector<std::string> StableIdsForBrainWorkItems(
    const std::vector<xvatsim::brain::BrainWorkItem>& items) {
    std::vector<std::string> stableIds;
    stableIds.reserve(items.size());
    for (const auto& item : items) {
        stableIds.push_back(xvatsim::brain::BrainWorkStableId(item));
    }
    return stableIds;
}

xvatsim::brain::BrainWorkCyclePlan BuildBrainSchedulerProbePlan() {
    xvatsim::brain::BrainWorkScheduler scheduler;
    return scheduler.PlanCycle(BuildBrainWorkModelProbeQueue());
}

std::vector<std::string> BuildBrainSchedulerRunnableProbe() {
    return StableIdsForBrainWorkItems(BuildBrainSchedulerProbePlan().runnableItems);
}

std::vector<std::string> BuildBrainSchedulerDeferredProbe() {
    return StableIdsForBrainWorkItems(BuildBrainSchedulerProbePlan().deferredItems);
}

std::string BuildBrainSchedulerHeavyCountsProbe() {
    const auto plan = BuildBrainSchedulerProbePlan();
    std::ostringstream stream;
    stream << "requested=" << plan.requestedHeavyCount
           << ",runnable=" << plan.runnableHeavyCount
           << ",deferred=" << plan.deferredHeavyCount
           << ",multi=" << (plan.RequestedMultipleHeavyJobs() ? 1 : 0);
    return stream.str();
}

xvatsim::brain::ControllerSnapshot MakeRadioReachableProbeController(
    const std::string& callsign,
    const std::string& frequency,
    int facility,
    bool atis = false) {
    xvatsim::brain::ControllerSnapshot controller;
    controller.callsign = callsign;
    controller.frequency = frequency;
    controller.facility = facility;
    controller.visualRangeNm = 200;
    controller.actionable = true;
    controller.atis = atis;
    return controller;
}

std::vector<xvatsim::brain::ControllerSnapshot> BuildRadioReachableProbeControllers() {
    return {
        MakeRadioReachableProbeController("PANC_DEL", "118.600", 2),
        MakeRadioReachableProbeController("PANC_GND", "121.900", 3),
        MakeRadioReachableProbeController("PANC_TWR", "118.300", 4),
        MakeRadioReachableProbeController("ANC_APP", "125.700", 5),
        MakeRadioReachableProbeController("LAX_25_CTR", "126.525", 6),
        MakeRadioReachableProbeController("PANC_ATIS", "135.800", 0, true),
        MakeRadioReachableProbeController("DOG_POOP", "199.999", 0),
    };
}

xvatsim::brain::RadioReachableControllerSnapshot BuildRadioReachableProbeSnapshot() {
    xvatsim::brain::RadioReachableBuildOptions options;
    options.available = true;
    options.stale = false;
    options.generation = 42;
    options.source = xvatsim::brain::RadioReachableSource::TestHarness;
    options.changeReason = "probe";
    options.nowSeconds = 123.0;
    return xvatsim::brain::BuildRadioReachableControllerSnapshot(
        BuildRadioReachableProbeControllers(),
        options);
}

std::string BuildRadioReachableHashCheckProbe() {
    const auto original = BuildRadioReachableProbeSnapshot();
    const auto repeated = BuildRadioReachableProbeSnapshot();

    auto changedControllers = BuildRadioReachableProbeControllers();
    changedControllers[4].frequency = "134.700";
    xvatsim::brain::RadioReachableBuildOptions options;
    options.available = true;
    options.stale = false;
    options.generation = 43;
    options.source = xvatsim::brain::RadioReachableSource::TestHarness;
    options.changeReason = "changed-frequency";
    options.nowSeconds = 124.0;
    const auto changed =
        xvatsim::brain::BuildRadioReachableControllerSnapshot(changedControllers, options);

    std::ostringstream stream;
    stream << "same=" << (original.stableHash == repeated.stableHash ? 1 : 0)
           << ",changed=" << (original.stableHash != changed.stableHash ? 1 : 0);
    return stream.str();
}

xvatsim::brain::RadioReachableControllerSnapshot BuildRadioReachablePhaseGateProbe(
    xvatsim::brain::WorkflowStage stage) {
    xvatsim::brain::RadioReachablePhaseGateOptions options;
    options.stage = stage;
    options.includeAtis = false;
    options.reason = "probe-phase-gate";
    return xvatsim::brain::ApplyRadioReachablePhaseGate(
        BuildRadioReachableProbeSnapshot(),
        options);
}

xvatsim::brain::RadioReachableVerificationFeed BuildRadioReachableVerifierEnrouteProbe() {
    const xvatsim::brain::RadioReachableControllerSnapshot previous;
    const auto current = BuildRadioReachablePhaseGateProbe(WorkflowStage::Enroute);
    const auto diff = xvatsim::brain::DiffRadioReachableSnapshots(previous, current);
    return xvatsim::brain::BuildRadioReachableVerificationFeed(current, &diff);
}

xvatsim::brain::RadioReachableVerificationFeed BuildRadioReachableVerifierUnchangedProbe() {
    const auto current = BuildRadioReachablePhaseGateProbe(WorkflowStage::Enroute);
    const auto diff = xvatsim::brain::DiffRadioReachableSnapshots(current, current);
    return xvatsim::brain::BuildRadioReachableVerificationFeed(current, &diff);
}

xvatsim::brain::FinalDisplaySnapshot MakePhasePublisherBoard(
    BoardSource source,
    StationRole role,
    const std::string& callsign,
    const std::string& frequency,
    const std::string& displayDecisionId = {},
    const std::string& capDecisionId = {},
    const std::string& sourceEvidenceId = {},
    const std::string& sourceOwnedStableCompletionKey = {},
    const std::string& generatedFallbackStableCompletionKey = {},
    bool sourceOwnedKeyMigrationReady = false,
    bool sourceOwnedKeyPlanContextAvailable = false) {
    xvatsim::brain::FinalDisplaySnapshot board;
    board.available = true;
    board.source = source;

    xvatsim::brain::FinalDisplayStationSnapshot station;
    station.role = role;
    station.callsign = callsign;
    station.frequency = frequency;
    station.online = true;
    station.displayDecisionId = displayDecisionId;
    station.overlayCapDecisionId = capDecisionId;
    station.sourceEvidenceId = sourceEvidenceId;
    station.sourceOwnedStableCompletionKey = sourceOwnedStableCompletionKey;
    station.generatedFallbackStableCompletionKey =
        generatedFallbackStableCompletionKey;
    station.sourceOwnedStableCompletionKeyPresent =
        !sourceOwnedStableCompletionKey.empty();
    station.sourceOwnedKeyMigrationReady = sourceOwnedKeyMigrationReady;
    station.sourceOwnedKeyPlanContextAvailable =
        sourceOwnedKeyPlanContextAvailable;
    station.sourceOwnedKeyBehaviorConsumerEnabled = false;
    if (!sourceEvidenceId.empty()) {
        station.sourceEvidenceType = "phase-reuse-fixture";
        station.sourceEvidenceDomain = "phase-publisher";
        station.sourceEvidenceLinkStatus = "linked";
        station.sourceDecisionId = "phase-source:" + callsign;
    }
    board.stations.push_back(std::move(station));
    return board;
}

std::string PhaseReuseSummaryText(
    const xvatsim::brain::PhaseSnapshotPublishResult& result) {
    const auto& summary = result.phaseReuseSummary;
    std::ostringstream stream;
    stream << "decisions=" << summary.phaseReuseDecisionCount
           << ",fresh=" << summary.freshCurrentRowCount
           << ",reused=" << summary.reusedLastProvenRowCount
           << ",displaced=" << summary.displacedByFreshEvidenceCount
           << ",blocked=" << summary.blockedReuseCount
           << ",stale=" << summary.staleReuseBlockedCount
           << ",planMismatch=" << summary.planMismatchBlockedCount
           << ",stageMismatch=" << summary.stageMismatchBlockedCount
           << ",frequencyMismatch="
           << summary.frequencyMismatchBlockedCount
           << ",roleMismatch=" << summary.roleMismatchBlockedCount
           << ",noCandidate=" << summary.noReuseCandidateCount
           << ",brainOwned="
           << (summary.phaseReuseLedgerBrainOwned ? 1 : 0)
           << ",displayBehaviorChanged="
           << (summary.displayBehaviorChanged ? 1 : 0);
    return stream.str();
}

std::string PhasePlanContextSummaryText(
    const xvatsim::brain::PhaseSnapshotPublishResult& result) {
    const auto& summary = result.phasePlanContextSummary;
    std::ostringstream stream;
    stream << "decisions=" << summary.phasePlanContextDecisionCount
           << ",available=" << summary.productPlanKeyAvailableCount
           << ",missing=" << summary.productPlanKeyMissingCount
           << ",liveProduct=" << summary.liveProductPlanContextCount
           << ",harness=" << summary.harnessPlanProbeCount
           << ",missingContext=" << summary.missingPlanContextCount
           << ",continuityKnown=" << summary.planContinuityKnownCount
           << ",continuityUnknown=" << summary.planContinuityUnknownCount
           << ",liveMismatch=" << summary.livePlanMismatchCount
           << ",harnessMismatch=" << summary.harnessPlanMismatchCount
           << ",brainOwned="
           << (summary.phasePlanContextBrainOwned ? 1 : 0)
           << ",publishBehaviorChanged="
           << (summary.publishBehaviorChanged ? 1 : 0);
    return stream.str();
}

std::string PhaseStableKeySummaryText(
    const xvatsim::brain::PhaseSnapshotPublishResult& result) {
    const auto& summary = result.phaseStableKeyAuditSummary;
    std::ostringstream stream;
    stream << "decisions=" << summary.stableKeyAuditDecisionCount
           << ",present=" << summary.stableKeyPresentCount
           << ",missing=" << summary.stableKeyMissingCount
           << ",fallback=" << summary.fallbackDerivedKeyCount
           << ",synthetic=" << summary.syntheticKeyCount
           << ",legacy=" << summary.legacyKeyCount
           << ",duplicated=" << summary.duplicatedKeyCount
           << ",changedAcrossReuse=" << summary.changedAcrossReuseCount
           << ",unsafeSameKey=" << summary.unsafeSameKeyCount
           << ",linkedDisplay=" << summary.keyLedgerLinkedDisplayCount
           << ",linkedCap=" << summary.keyLedgerLinkedCapCount
           << ",linkedPhase=" << summary.keyLedgerLinkedPhaseReuseCount
           << ",brainOwned=" << (summary.stableKeyAuditBrainOwned ? 1 : 0)
           << ",displayBehaviorChanged="
           << (summary.displayBehaviorChanged ? 1 : 0);
    return stream.str();
}

std::string PhaseStableKeyConsumerDryRunSummaryText(
    const xvatsim::brain::PhaseSnapshotPublishResult& result) {
    return StableKeyConsumerDryRunSummaryText(
        result.phaseStableKeyConsumerDryRunSummary);
}

std::string PhaseStableKeyShadowSummaryText(
    const xvatsim::brain::PhaseSnapshotPublishResult& result) {
    return StableKeyShadowSummaryText(
        result.phaseSourceOwnedFallbackStableKeyShadowSummary);
}

std::string PhaseStableKeyLiveConsumptionReadinessSummaryText(
    const xvatsim::brain::PhaseSnapshotPublishResult& result) {
    return StableKeyLiveConsumptionReadinessSummaryText(
        result
            .phaseSourceOwnedFallbackStableKeyLiveConsumptionReadinessSummary);
}

std::string PhaseStableKeyLiveConsumptionSummaryText(
    const xvatsim::brain::PhaseSnapshotPublishResult& result) {
    return StableKeyLiveConsumptionSummaryText(
        result.phaseSourceOwnedFallbackStableKeyLiveConsumptionSummary);
}

std::vector<std::string> PhaseReuseDecisionRows(
    const xvatsim::brain::PhaseSnapshotPublishResult& result) {
    std::vector<std::string> rows;
    rows.reserve(result.phaseReuseDecisions.size());
    for (const auto& decision : result.phaseReuseDecisions) {
        std::ostringstream stream;
        stream << "id=" << OverlayCapToken(decision.phaseReuseDecisionId)
               << ":" << OverlayCapToken(decision.callsign)
               << "@" << OverlayCapToken(decision.frequency)
               << ":decision=" << OverlayCapToken(decision.reuseDecision)
               << ":displayDecision="
               << OverlayCapToken(decision.displayDecisionId)
               << ":capDecision=" << OverlayCapToken(decision.capDecisionId)
               << ":sourceDecision="
               << OverlayCapToken(decision.sourceDecisionId)
               << ":sourceEvidence="
               << OverlayCapToken(decision.sourceEvidenceId)
               << ":subject=" << OverlayCapToken(decision.subjectKey)
               << ":role=" << StationRoleToToken(decision.role)
               << ":endpoint=" << OverlayCapToken(decision.endpoint)
               << ":airport=" << OverlayCapToken(decision.airportIcao)
               << ":previousStage="
               << WorkflowStageToString(decision.previousWorkflowStage)
               << ":currentStage="
               << WorkflowStageToString(decision.currentWorkflowStage)
               << ":previousPlan="
               << OverlayCapToken(decision.previousPlanKey)
               << ":currentPlan="
               << OverlayCapToken(decision.currentPlanKey)
               << ":previousSnapshot="
               << OverlayCapToken(decision.previousSnapshotKey)
               << ":currentSnapshot="
               << OverlayCapToken(decision.currentSnapshotKey)
               << ":previousIndex=" << decision.previousBoardIndex
               << ":currentIndex=" << decision.currentBoardIndex
               << ":candidate=" << (decision.reuseCandidate ? 1 : 0)
               << ":reused="
               << (decision.reusedFromPreviousSnapshot ? 1 : 0)
               << ":freshAvailable="
               << (decision.freshCurrentEvidenceAvailable ? 1 : 0)
               << ":freshAccepted="
               << (decision.freshCurrentEvidenceAccepted ? 1 : 0)
               << ":freshIncomplete="
               << (decision.freshCurrentEvidenceIncomplete ? 1 : 0)
               << ":reusedBecauseIncomplete="
               << (decision.reusedBecauseCurrentIncomplete ? 1 : 0)
               << ":displaced="
               << (decision.displacedByFreshEvidence ? 1 : 0)
               << ":staleBlocked="
               << (decision.staleReuseBlocked ? 1 : 0)
               << ":allowed=" << (decision.reuseAllowed ? 1 : 0)
               << ":blockedReason="
               << OverlayCapToken(decision.reuseBlockedReason)
               << ":sourceLinked="
               << (decision.sourceEvidenceLinked ? 1 : 0)
               << ":sourceStatus="
               << OverlayCapToken(decision.sourceEvidenceLinkStatus)
               << ":confidence="
               << OverlayCapToken(decision.confidenceLevel)
               << ":fallback=" << (decision.fallbackUsed ? 1 : 0)
               << ":productPlan="
               << OverlayCapToken(decision.productPlanKey)
               << ":productPlanAvailable="
               << (decision.productPlanKeyAvailable ? 1 : 0)
               << ":productPlanSource="
               << OverlayCapToken(decision.productPlanKeySource)
               << ":productPlanMissingReason="
               << OverlayCapToken(decision.productPlanKeyMissingReason)
               << ":previousProductPlan="
               << OverlayCapToken(decision.previousProductPlanKey)
               << ":currentProductPlan="
               << OverlayCapToken(decision.currentProductPlanKey)
               << ":planContinuityKnown="
               << (decision.planContinuityKnown ? 1 : 0)
               << ":planContinuity="
               << OverlayCapToken(decision.planContinuityStatus)
               << ":planMismatchSource="
               << OverlayCapToken(decision.planMismatchDiagnosticSource)
               << ":planContextLinked="
               << (decision.phaseReusePlanContextLinked ? 1 : 0)
               << ":stableKey="
               << OverlayCapToken(decision.stableCompletionKey)
               << ":stableKeyPresent="
               << (decision.stableCompletionKeyPresent ? 1 : 0)
               << ":stableKeySource="
               << OverlayCapToken(decision.stableCompletionKeySource)
               << ":stableKeyStatus="
               << OverlayCapToken(decision.stableCompletionKeyStatus)
               << ":keyReason="
               << OverlayCapToken(decision.keyDerivationReason)
               << ":keyParts="
               << (decision.keyIncludesCallsign ? 1 : 0) << "/"
               << (decision.keyIncludesRole ? 1 : 0) << "/"
               << (decision.keyIncludesFrequency ? 1 : 0) << "/"
               << (decision.keyIncludesEndpoint ? 1 : 0) << "/"
               << (decision.keyIncludesAirport ? 1 : 0)
               << ":keyMatchesDisplay="
               << (decision.keyMatchesDisplayDecision ? 1 : 0)
               << ":keyMatchesCap="
               << (decision.keyMatchesCapDecision ? 1 : 0)
               << ":keyMatchesPhase="
               << (decision.keyMatchesPhaseReuseDecision ? 1 : 0)
               << ":duplicateKey="
               << (decision.duplicateKeyDetected ? 1 : 0)
               << ":duplicateGroup="
               << OverlayCapToken(decision.duplicateKeyGroup)
               << ":keyContinuityKnown="
               << (decision.keyContinuityKnown ? 1 : 0)
               << ":keyChangedAcrossReuse="
               << (decision.keyChangedAcrossReuse ? 1 : 0)
               << ":unsafeSameKey="
                << (decision.unsafeSameKeyAcrossChangedFacts ? 1 : 0)
                << ":keyWarning="
                << (decision.keyAuditWarning ? 1 : 0)
                << ":keyWarningReason="
                << OverlayCapToken(decision.keyAuditWarningReason)
                << ":dryRunCurrentKey="
                << OverlayCapToken(decision.currentBehaviorKey)
                << ":dryRunSourceOwnedKey="
                << OverlayCapToken(
                       decision.sourceOwnedStableCompletionKey)
                << ":dryRunGeneratedFallbackKey="
                << OverlayCapToken(
                       decision.generatedFallbackStableCompletionKey)
                << ":dryRunCurrentKeySource="
                << OverlayCapToken(decision.currentBehaviorKeySource)
                << ":dryRunSourceOwnedPresent="
                << (decision.sourceOwnedKeyPresent ? 1 : 0)
                << ":dryRunMigrationReady="
                << (decision.sourceOwnedKeyMigrationReady ? 1 : 0)
                << ":dryRunBehaviorConsumer="
                << (decision.behaviorConsumerEnabled ? 1 : 0)
                << ":dryRunDedupeCurrent="
                << OverlayCapToken(decision.dryRunDedupeGroupCurrent)
                << ":dryRunDedupeSourceOwned="
                << OverlayCapToken(decision.dryRunDedupeGroupSourceOwned)
                << ":dryRunDedupeWouldChange="
                << (decision.dryRunDedupeGroupWouldChange ? 1 : 0)
                << ":dryRunDuplicateSuppressionWouldChange="
                << (decision.dryRunDuplicateSuppressionWouldChange ? 1 : 0)
                << ":dryRunCompletionIdentityWouldChange="
                << (decision.dryRunCompletionIdentityWouldChange ? 1 : 0)
                << ":dryRunPhaseCurrent="
                << (decision.dryRunPhaseReuseMatchCurrent ? 1 : 0)
                << ":dryRunPhaseSourceOwned="
                << (decision.dryRunPhaseReuseMatchSourceOwned ? 1 : 0)
                << ":dryRunPhaseWouldChange="
                << (decision.dryRunPhaseReuseWouldChange ? 1 : 0)
                << ":dryRunRowOrderingWouldChange="
                << (decision.dryRunRowOrderingWouldChange ? 1 : 0)
                << ":dryRunOverlayCapWouldChange="
                << (decision.dryRunOverlayCapWouldChange ? 1 : 0)
                << ":dryRunMoreAtcWouldChange="
                << (decision.dryRunMoreAtcWouldChange ? 1 : 0)
                << ":dryRunDrift="
                << (decision.dryRunDriftDetected ? 1 : 0)
                << ":dryRunDriftReason="
                << OverlayCapToken(decision.dryRunDriftReason)
                << ":dryRunSafeForOptIn="
                << (decision.dryRunSafeForOptIn ? 1 : 0)
                << ":dryRunBlockedReason="
                << OverlayCapToken(decision.dryRunBlockedReason)
                << ":shadowGateEnabled="
                << (decision.sourceOwnedFallbackShadowGateEnabled ? 1 : 0)
                << ":shadowGateSource="
                << OverlayCapToken(
                       decision.sourceOwnedFallbackShadowGateSource)
                << ":shadowAttempted="
                << (decision.shadowRecomputeAttempted ? 1 : 0)
                << ":shadowSkipped="
                << OverlayCapToken(decision.shadowRecomputeSkippedReason)
                << ":shadowBehaviorConsumer="
                << (decision.shadowBehaviorConsumerEnabled ? 1 : 0)
                << ":shadowHashCurrent="
                << OverlayCapToken(decision.shadowFinalBoardHashCurrent)
                << ":shadowHashSourceOwned="
                << OverlayCapToken(decision.shadowFinalBoardHashSourceOwned)
                << ":shadowHashMatches="
                << (decision.shadowFinalBoardHashMatches ? 1 : 0)
                << ":shadowRowOrderingMatches="
                << (decision.shadowRowOrderingMatches ? 1 : 0)
                << ":shadowDedupeMatches="
                << (decision.shadowDedupeGroupsMatch ? 1 : 0)
                << ":shadowDuplicateSuppressionMatches="
                << (decision.shadowDuplicateSuppressionMatches ? 1 : 0)
                << ":shadowCompletionIdentityMatches="
                << (decision.shadowCompletionIdentityMatches ? 1 : 0)
                << ":shadowPhaseReuseMatches="
                << (decision.shadowPhaseReuseMatches ? 1 : 0)
                << ":shadowOverlayCapMatches="
                << (decision.shadowOverlayCapMatches ? 1 : 0)
                << ":shadowMoreAtcMatches="
                << (decision.shadowMoreAtcMatches ? 1 : 0)
                << ":shadowMissingPlanBlocked="
                << (decision.shadowMissingPlanContextBlocked ? 1 : 0)
                << ":shadowDrift="
                << (decision.shadowDriftDetected ? 1 : 0)
                << ":shadowDriftReason="
                << OverlayCapToken(decision.shadowDriftReason)
                << ":shadowSafeForFutureLiveOptIn="
                << (decision.shadowSafeForFutureLiveOptIn ? 1 : 0)
                << ":liveProposalGateArmed="
                << (decision.liveConsumptionProposalGateArmed ? 1 : 0)
                << ":liveProposalGateSource="
                << OverlayCapToken(decision.liveConsumptionProposalGateSource)
                << ":liveShadowParityClean="
                << (decision.liveConsumptionShadowParityClean ? 1 : 0)
                << ":livePlanContextAvailable="
                << (decision.liveConsumptionPlanContextAvailable ? 1 : 0)
                << ":liveReadyForFutureOptIn="
                << (decision.liveConsumptionReadyForFutureOptIn ? 1 : 0)
                << ":liveBlockedReason="
                << OverlayCapToken(decision.liveConsumptionBlockedReason)
                << ":liveBehaviorConsumer="
                << (decision.liveConsumptionBehaviorEnabled ? 1 : 0)
                << ":liveDecision="
                << OverlayCapToken(decision.gatedLiveConsumptionDecisionId)
                << ":liveGateArmed="
                << (decision.liveConsumptionGateArmed ? 1 : 0)
                << ":liveGateSource="
                << OverlayCapToken(decision.liveConsumptionGateSource)
                << ":liveAllowed="
                << (decision.liveConsumptionAllowed ? 1 : 0)
                << ":liveGateBlockedReason="
                << OverlayCapToken(
                       decision.gatedLiveConsumptionBlockedReason)
                << ":liveConsumedKeyType="
                << OverlayCapToken(decision.liveConsumptionConsumedKeyType)
                << ":liveDecisionBehaviorChanged="
                << (decision.liveConsumptionDecisionBehaviorChanged ? 1 : 0)
                << ":liveDefaultModeProtected="
                << (decision.liveConsumptionDefaultModeProtected ? 1 : 0);
        rows.push_back(stream.str());
    }
    return rows;
}

std::string PhasePublisherResultSummary(
    const xvatsim::brain::PhaseSnapshotPublishResult& result) {
    std::ostringstream stream;
    stream << "stored=" << (result.storedNewProven ? 1 : 0)
           << ":reused=" << (result.usedLastProven ? 1 : 0)
           << ":source=" << BoardSourceToString(result.snapshot.source)
           << ":stations=";
    if (result.snapshot.stations.empty()) {
        stream << "none";
        return stream.str();
    }
    for (std::size_t index = 0; index < result.snapshot.stations.size(); ++index) {
        if (index > 0) {
            stream << "|";
        }
        stream << result.snapshot.stations[index].callsign;
    }
    return stream.str();
}

std::vector<std::string> BuildPhasePublisherReuseProbe() {
    xvatsim::brain::PhaseSnapshotPublisherState state;
    std::vector<std::string> lifecycle;

    xvatsim::brain::PhaseSnapshotPublishRequest provenRequest;
    provenRequest.stage = WorkflowStage::Enroute;
    provenRequest.candidate = MakePhasePublisherBoard(
        BoardSource::Enroute,
        StationRole::Center,
        "NY_CTR",
        "125.325");
    provenRequest.reason = "harness-proven";
    lifecycle.push_back(
        PhasePublisherResultSummary(
            xvatsim::brain::PublishPhaseSnapshot(&state, provenRequest)));

    xvatsim::brain::PhaseSnapshotPublishRequest pendingRequest;
    pendingRequest.stage = WorkflowStage::Enroute;
    pendingRequest.verificationPending = true;
    pendingRequest.reason = "harness-pending";
    lifecycle.push_back(
        PhasePublisherResultSummary(
            xvatsim::brain::PublishPhaseSnapshot(&state, pendingRequest)));

    return lifecycle;
}

std::vector<std::string> BuildPhasePublisherIsolationProbe() {
    xvatsim::brain::PhaseSnapshotPublisherState state;
    std::vector<std::string> lifecycle;

    xvatsim::brain::PhaseSnapshotPublishRequest enrouteRequest;
    enrouteRequest.stage = WorkflowStage::Enroute;
    enrouteRequest.candidate = MakePhasePublisherBoard(
        BoardSource::Enroute,
        StationRole::Center,
        "NY_CTR",
        "125.325");
    lifecycle.push_back(
        PhasePublisherResultSummary(
            xvatsim::brain::PublishPhaseSnapshot(&state, enrouteRequest)));

    xvatsim::brain::PhaseSnapshotPublishRequest arrivalPendingRequest;
    arrivalPendingRequest.stage = WorkflowStage::Arrival;
    arrivalPendingRequest.verificationPending = true;
    arrivalPendingRequest.reason = "arrival-pending-before-proof";
    lifecycle.push_back(
        PhasePublisherResultSummary(
            xvatsim::brain::PublishPhaseSnapshot(&state, arrivalPendingRequest)));

    xvatsim::brain::PhaseSnapshotPublishRequest arrivalProvenRequest;
    arrivalProvenRequest.stage = WorkflowStage::Arrival;
    arrivalProvenRequest.candidate = MakePhasePublisherBoard(
        BoardSource::Arrival,
        StationRole::Approach,
        "BOS_APP",
        "124.100");
    lifecycle.push_back(
        PhasePublisherResultSummary(
            xvatsim::brain::PublishPhaseSnapshot(&state, arrivalProvenRequest)));

    lifecycle.push_back(
        PhasePublisherResultSummary(
            xvatsim::brain::PublishPhaseSnapshot(&state, arrivalPendingRequest)));

    return lifecycle;
}

std::vector<std::string> BuildPhasePublisherWorkflowClearProbe() {
    xvatsim::brain::PhaseSnapshotPublisherState state;
    std::vector<std::string> lifecycle;

    xvatsim::brain::PhaseSnapshotPublishRequest provenEnrouteRequest;
    provenEnrouteRequest.stage = WorkflowStage::Enroute;
    provenEnrouteRequest.candidate = MakePhasePublisherBoard(
        BoardSource::Enroute,
        StationRole::Center,
        "NY_CTR",
        "125.325");
    provenEnrouteRequest.reason = "touchdown-last-proven-center";
    lifecycle.push_back(
        PhasePublisherResultSummary(
            xvatsim::brain::PublishPhaseSnapshot(&state, provenEnrouteRequest)));

    xvatsim::brain::PhaseSnapshotPublishRequest touchdownPendingRequest;
    touchdownPendingRequest.stage = WorkflowStage::Enroute;
    touchdownPendingRequest.verificationPending = true;
    touchdownPendingRequest.reason = "touchdown-authority-refresh-pending";
    lifecycle.push_back(
        PhasePublisherResultSummary(
            xvatsim::brain::PublishPhaseSnapshot(&state, touchdownPendingRequest)));

    state.Reset();
    lifecycle.push_back(
        PhasePublisherResultSummary(
            xvatsim::brain::PublishPhaseSnapshot(&state, touchdownPendingRequest)));

    return lifecycle;
}

xvatsim::brain::PhaseSnapshotPublishResult BuildPhasePublisherReuseLedgerProbe(
    const std::string& probeName,
    bool sourceOwnedFallbackShadowEnabled = false,
    const std::string& sourceOwnedFallbackShadowGateSource = "default",
    bool sourceOwnedFallbackLiveConsumptionProposalEnabled = false,
    const std::string&
        sourceOwnedFallbackLiveConsumptionProposalGateSource = "default",
    bool sourceOwnedFallbackLiveConsumptionEnabled = false,
    const std::string& sourceOwnedFallbackLiveConsumptionGateSource =
        "default") {
    xvatsim::brain::PhaseSnapshotPublisherState state;

    auto publishFresh =
        [&](const std::string& callsign,
            const std::string& frequency,
            const std::string& planKey,
            const std::string& snapshotKey,
            StationRole role = StationRole::Center,
            const std::string& displayDecisionId = {},
            const std::string& capDecisionId = {},
            const std::string& sourceEvidenceId = {},
            const std::string& planSource = "harness",
            const std::string& missingReason = {},
            const std::string& sourceOwnedStableCompletionKey = {},
            const std::string& generatedFallbackStableCompletionKey = {},
            bool sourceOwnedKeyMigrationReady = false,
            bool sourceOwnedKeyPlanContextAvailable = false) {
        xvatsim::brain::PhaseSnapshotPublishRequest request;
        request.stage = WorkflowStage::Enroute;
        request.candidate = MakePhasePublisherBoard(
            BoardSource::Enroute,
            role,
            callsign,
            frequency,
            displayDecisionId,
            capDecisionId,
            sourceEvidenceId,
            sourceOwnedStableCompletionKey,
            generatedFallbackStableCompletionKey,
            sourceOwnedKeyMigrationReady,
            sourceOwnedKeyPlanContextAvailable);
        request.currentPlanKey = planKey;
        request.productPlanKey = planKey;
        request.productPlanKeyAvailable = !planKey.empty();
        request.productPlanKeySource =
            planKey.empty() ? "unavailable" : planSource;
        request.productPlanKeyMissingReason =
            planKey.empty()
                ? (missingReason.empty()
                       ? std::string("harness-plan-key-not-provided")
                       : missingReason)
                : std::string{};
        request.sourceOwnedFallbackStableKeyShadowEnabled =
            sourceOwnedFallbackShadowEnabled;
        request.sourceOwnedFallbackStableKeyShadowGateSource =
            sourceOwnedFallbackShadowGateSource;
        request
            .sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled =
            sourceOwnedFallbackLiveConsumptionProposalEnabled;
        request
            .sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource =
            sourceOwnedFallbackLiveConsumptionProposalGateSource;
        request.sourceOwnedFallbackStableKeyLiveConsumptionEnabled =
            sourceOwnedFallbackLiveConsumptionEnabled;
        request.sourceOwnedFallbackStableKeyLiveConsumptionGateSource =
            sourceOwnedFallbackLiveConsumptionGateSource;
        request.currentSnapshotKey = snapshotKey;
        return xvatsim::brain::PublishPhaseSnapshot(&state, request);
    };

    auto publishPending =
        [&](WorkflowStage stage,
            const std::string& planKey,
            const std::string& snapshotKey,
            const std::string& planSource = "harness",
            const std::string& missingReason = {}) {
        xvatsim::brain::PhaseSnapshotPublishRequest request;
        request.stage = stage;
        request.verificationPending = true;
        request.currentPlanKey = planKey;
        request.productPlanKey = planKey;
        request.productPlanKeyAvailable = !planKey.empty();
        request.productPlanKeySource =
            planKey.empty() ? "unavailable" : planSource;
        request.productPlanKeyMissingReason =
            planKey.empty()
                ? (missingReason.empty()
                       ? std::string("harness-plan-key-not-provided")
                       : missingReason)
                : std::string{};
        request.sourceOwnedFallbackStableKeyShadowEnabled =
            sourceOwnedFallbackShadowEnabled;
        request.sourceOwnedFallbackStableKeyShadowGateSource =
            sourceOwnedFallbackShadowGateSource;
        request
            .sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled =
            sourceOwnedFallbackLiveConsumptionProposalEnabled;
        request
            .sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource =
            sourceOwnedFallbackLiveConsumptionProposalGateSource;
        request.sourceOwnedFallbackStableKeyLiveConsumptionEnabled =
            sourceOwnedFallbackLiveConsumptionEnabled;
        request.sourceOwnedFallbackStableKeyLiveConsumptionGateSource =
            sourceOwnedFallbackLiveConsumptionGateSource;
        request.currentSnapshotKey = snapshotKey;
        request.reason = "phase-reuse-ledger-pending";
        return xvatsim::brain::PublishPhaseSnapshot(&state, request);
    };

    if (probeName == "fresh-current-row") {
        return publishFresh(
            "FRESH_CTR",
            "124.100",
            "plan-a",
            "snap-fresh",
            StationRole::Center,
            "display:fresh",
            "overlay-cap|0",
            "source:fresh");
    }

    if (probeName == "live-product-plan-present") {
        return publishFresh(
            "LIVE_CTR",
            "125.300",
            "live-plan-a",
            "snap-live-product",
            StationRole::Center,
            "display:live",
            "overlay-cap|0",
            "source:live",
            "live-product");
    }

    if (probeName == "missing-product-plan-key") {
        return publishFresh(
            "MISSING_CTR",
            "125.400",
            "",
            "snap-missing-current",
            StationRole::Center,
            "display:missing",
            "overlay-cap|0",
            "source:missing",
            "unavailable",
            "product-plan-key-missing-in-probe");
    }

    if (probeName == "live-same-plan") {
        (void)publishFresh(
            "SAME_CTR",
            "125.500",
            "live-plan-a",
            "snap-same-proven",
            StationRole::Center,
            "display:same",
            "overlay-cap|0",
            "source:same",
            "live-product");
        return publishPending(
            WorkflowStage::Enroute,
            "live-plan-a",
            "snap-same-pending",
            "live-product");
    }

    if (probeName == "live-changed-plan") {
        (void)publishFresh(
            "CHANGE_CTR",
            "125.600",
            "live-plan-a",
            "snap-change-old",
            StationRole::Center,
            "display:change-old",
            "overlay-cap|0",
            "source:change-old",
            "live-product");
        return publishFresh(
            "CHANGE_CTR",
            "125.600",
            "live-plan-b",
            "snap-change-new",
            StationRole::Center,
            "display:change-new",
            "overlay-cap|0",
            "source:change-new",
            "live-product");
    }

    if (probeName == "missing-previous-plan") {
        (void)publishFresh(
            "PREV_MISSING_CTR",
            "125.700",
            "",
            "snap-prev-missing-old",
            StationRole::Center,
            "display:prev-missing-old",
            "overlay-cap|0",
            "source:prev-missing-old",
            "unavailable",
            "previous-product-plan-key-missing");
        return publishFresh(
            "PREV_MISSING_CTR",
            "125.700",
            "live-plan-a",
            "snap-prev-missing-new",
            StationRole::Center,
            "display:prev-missing-new",
            "overlay-cap|0",
            "source:prev-missing-new",
            "live-product");
    }

    if (probeName == "missing-current-plan") {
        (void)publishFresh(
            "CURR_MISSING_CTR",
            "125.800",
            "live-plan-a",
            "snap-current-present-old",
            StationRole::Center,
            "display:current-present-old",
            "overlay-cap|0",
            "source:current-present-old",
            "live-product");
        return publishFresh(
            "CURR_MISSING_CTR",
            "125.800",
            "",
            "snap-current-missing-new",
            StationRole::Center,
            "display:current-missing-new",
            "overlay-cap|0",
            "source:current-missing-new",
            "unavailable",
            "current-product-plan-key-missing");
    }

    if (probeName == "reused-last-proven-row") {
        (void)publishFresh(
            "REUSE_CTR",
            "124.200",
            "plan-a",
            "snap-proven",
            StationRole::Center,
            "display:reuse",
            "overlay-cap|0",
            "source:reuse");
        return publishPending(
            WorkflowStage::Enroute,
            "plan-a",
            "snap-pending");
    }

    if (probeName == "source-owned-reused-last-proven-row") {
        (void)publishFresh(
            "SRC_CTR",
            "124.200",
            "plan-a",
            "snap-source-owned-proven",
            StationRole::Center,
            "display:source-owned-reuse",
            "overlay-cap|0",
            "",
            "harness",
            "",
            "source-owned:fallback-polygon-geometry|SRC_CTR|6|124200|current=KZAB;next=none;arrival=none;stage=Enroute",
            "row|enroute||6|SRC_CTR|124.200",
            true,
            true);
        return publishPending(
            WorkflowStage::Enroute,
            "plan-a",
            "snap-source-owned-pending");
    }

    if (probeName == "source-owned-fresh-displaces-previous") {
        (void)publishFresh(
            "OLD_SRC",
            "124.300",
            "plan-a",
            "snap-source-owned-old",
            StationRole::Center,
            "",
            "",
            "",
            "harness",
            "",
            "source-owned:fallback-polygon-geometry|OLD_SRC|6|124300|current=KZAB;next=none;arrival=none;stage=Enroute",
            "row|enroute||6|OLD_SRC|124.300",
            true,
            true);
        return publishFresh(
            "NEW_SRC",
            "124.400",
            "plan-a",
            "snap-source-owned-new",
            StationRole::Center,
            "",
            "",
            "",
            "harness",
            "",
            "source-owned:fallback-polygon-geometry|NEW_SRC|6|124400|current=KZAB;next=none;arrival=none;stage=Enroute",
            "row|enroute||6|NEW_SRC|124.400",
            true,
            true);
    }

    if (probeName == "source-owned-plan-context-drift") {
        (void)publishFresh(
            "DRIFT_SRC",
            "124.550",
            "plan-a",
            "snap-source-owned-drift-old",
            StationRole::Center,
            "",
            "",
            "",
            "harness",
            "",
            "source-owned:fallback-polygon-geometry|DRIFT_SRC|6|124550|current=KZAB;next=none;arrival=none;stage=Enroute",
            "row|enroute||6|DRIFT_SRC|124.550",
            true,
            true);
        return publishFresh(
            "DRIFT_SRC",
            "124.550",
            "plan-a",
            "snap-source-owned-drift-new",
            StationRole::Center,
            "",
            "",
            "",
            "harness",
            "",
            "source-owned:fallback-polygon-geometry|DRIFT_SRC|6|124550|current=KZLA;next=none;arrival=none;stage=Enroute",
            "row|enroute||6|DRIFT_SRC|124.550",
            true,
            true);
    }

    if (probeName == "source-owned-frequency-mismatch") {
        (void)publishFresh(
            "FREQ_SRC",
            "124.700",
            "plan-a",
            "snap-source-owned-frequency-old",
            StationRole::Center,
            "",
            "",
            "",
            "harness",
            "",
            "source-owned:fallback-polygon-geometry|FREQ_SRC|6|124700|current=KZAB;next=none;arrival=none;stage=Enroute",
            "row|enroute||6|FREQ_SRC|124.700",
            true,
            true);
        return publishFresh(
            "FREQ_SRC",
            "124.800",
            "plan-a",
            "snap-source-owned-frequency-new",
            StationRole::Center,
            "",
            "",
            "",
            "harness",
            "",
            "source-owned:fallback-polygon-geometry|FREQ_SRC|6|124800|current=KZAB;next=none;arrival=none;stage=Enroute",
            "row|enroute||6|FREQ_SRC|124.800",
            true,
            true);
    }

    if (probeName == "source-owned-role-mismatch") {
        (void)publishFresh(
            "ROLE_SRC",
            "124.900",
            "plan-a",
            "snap-source-owned-role-old",
            StationRole::Center,
            "",
            "",
            "",
            "harness",
            "",
            "source-owned:fallback-polygon-geometry|ROLE_SRC|6|124900|current=KZAB;next=none;arrival=none;stage=Enroute",
            "row|enroute||6|ROLE_SRC|124.900",
            true,
            true);
        return publishFresh(
            "ROLE_SRC",
            "124.900",
            "plan-a",
            "snap-source-owned-role-new",
            StationRole::Approach,
            "",
            "",
            "",
            "harness",
            "",
            "source-owned:fallback-polygon-geometry|ROLE_SRC|5|124900|current=KZAB;next=none;arrival=none;stage=Enroute",
            "row|enroute||5|ROLE_SRC|124.900",
            true,
            true);
    }

    if (probeName == "displaced-by-fresh-current-row") {
        (void)publishFresh(
            "OLD_CTR",
            "124.300",
            "plan-a",
            "snap-old");
        return publishFresh(
            "NEW_CTR",
            "124.400",
            "plan-a",
            "snap-new");
    }

    if (probeName == "blocked-plan-mismatch") {
        (void)publishFresh(
            "PLAN_CTR",
            "124.500",
            "plan-a",
            "snap-plan-a");
        return publishFresh(
            "PLAN_CTR",
            "124.500",
            "plan-b",
            "snap-plan-b");
    }

    if (probeName == "blocked-stage-mismatch") {
        (void)publishFresh(
            "STAGE_CTR",
            "124.600",
            "plan-a",
            "snap-stage-enroute");
        return publishPending(
            WorkflowStage::Arrival,
            "plan-a",
            "snap-stage-arrival-pending");
    }

    if (probeName == "blocked-frequency-mismatch") {
        (void)publishFresh(
            "FREQ_CTR",
            "124.700",
            "plan-a",
            "snap-frequency-old");
        return publishFresh(
            "FREQ_CTR",
            "124.800",
            "plan-a",
            "snap-frequency-new");
    }

    if (probeName == "blocked-role-mismatch") {
        (void)publishFresh(
            "ROLE_CTR",
            "124.900",
            "plan-a",
            "snap-role-old",
            StationRole::Center);
        return publishFresh(
            "ROLE_CTR",
            "124.900",
            "plan-a",
            "snap-role-new",
            StationRole::Approach);
    }

    if (probeName == "blocked-stale-reuse") {
        (void)publishFresh(
            "STALE_CTR",
            "125.000",
            "plan-a",
            "snap-stale-old");
        state.enrouteMetadata.stale = true;
        return publishFresh(
            "STALE_CTR",
            "125.000",
            "plan-a",
            "snap-stale-new");
    }

    if (probeName == "reused-near-cap-linked") {
        (void)publishFresh(
            "CAP_CTR",
            "125.100",
            "plan-a",
            "snap-cap-proven",
            StationRole::Center,
            "display:cap39",
            "overlay-cap|39",
            "source:cap39");
        return publishPending(
            WorkflowStage::Enroute,
            "plan-a",
            "snap-cap-pending");
    }

    if (probeName == "no-reuse-candidate") {
        return publishPending(
            WorkflowStage::Enroute,
            "plan-a",
            "snap-no-candidate");
    }

    return publishFresh(
        "DEFAULT_CTR",
        "125.200",
        "plan-default",
        "snap-default");
}

std::vector<xvatsim::brain::BrainWorkItem> BuildOrdinaryMovementWorkQueueProbe() {
    using namespace xvatsim::brain;

    std::vector<BrainWorkItem> items;

    BrainWorkItem radioDiff;
    radioDiff.type = BrainWorkType::RunAuthorityFastPath;
    radioDiff.priority = BrainWorkPriority::CurrentEnrouteAuthority;
    radioDiff.reason = BrainWorkReason::AircraftMovementThreshold;
    radioDiff.budget = BrainWorkBudget::Light;
    radioDiff.target.stage = WorkflowStage::Enroute;
    radioDiff.target.polygonKey = "NY";
    radioDiff.target.polygonSequence = 2;
    radioDiff.cacheKey = "movement:ny:fast-path";
    radioDiff.enqueueSequence = 0;
    items.push_back(std::move(radioDiff));

    BrainWorkItem publishUi;
    publishUi.type = BrainWorkType::PublishUiSnapshot;
    publishUi.priority = BrainWorkPriority::Diagnostics;
    publishUi.reason = BrainWorkReason::UiRefresh;
    publishUi.budget = BrainWorkBudget::Light;
    publishUi.target.stage = WorkflowStage::Enroute;
    publishUi.cacheKey = "ui:last-proven";
    publishUi.enqueueSequence = 1;
    items.push_back(std::move(publishUi));

    SortBrainWorkQueue(&items);
    return items;
}

std::vector<std::string> BuildBrainOrdinaryMovementWorkOrderProbe() {
    return StableIdsForBrainWorkItems(BuildOrdinaryMovementWorkQueueProbe());
}

std::vector<std::string> BuildBrainOrdinaryMovementHeavyProbe() {
    std::vector<std::string> flags;
    const auto items = BuildOrdinaryMovementWorkQueueProbe();
    flags.reserve(items.size());
    for (const auto& item : items) {
        flags.push_back(
            std::string(xvatsim::brain::ToString(item.type)) +
            ":heavy=" +
            (xvatsim::brain::IsHeavyBrainWork(item) ? "1" : "0"));
    }
    return flags;
}

bool AssignScenarioProperty(ScenarioData* scenario, const std::string& key, const std::string& value) {
    if (scenario == nullptr) {
        return false;
    }

    if (key == "name") {
        scenario->name = value;
        return true;
    }
    if (key == "now_seconds") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->nowSeconds = *parsed;
        return true;
    }
    if (key == "phase_publisher_reuse_probe") {
        scenario->phasePublisherReuseProbe = value;
        return true;
    }
    if (key == "departure.inside_terminal_coverage") {
        return ParseBool(value, &scenario->insideDepartureTerminalCoverage);
    }
    if (key == "departure.terminal_coverage_known") {
        return ParseBool(value, &scenario->departureTerminalCoverageKnown);
    }
    if (key == "departure.coverage.available") {
        return ParseBool(value, &scenario->departureAirportSectorSnapshot.available);
    }
    if (key == "departure.coverage.stale") {
        return ParseBool(value, &scenario->departureAirportSectorSnapshot.stale);
    }
    if (key == "departure.coverage.has_center") {
        return ParseBool(value, &scenario->departureAirportSectorSnapshot.hasCenterCoverageData);
    }
    if (key == "departure.coverage.has_terminal") {
        return ParseBool(
            value,
            &scenario->departureAirportSectorSnapshot.hasTerminalCoverageData);
    }
    if (key == "arrival.coverage.available") {
        return ParseBool(value, &scenario->arrivalAirportSectorSnapshot.available);
    }
    if (key == "arrival.coverage.stale") {
        return ParseBool(value, &scenario->arrivalAirportSectorSnapshot.stale);
    }
    if (key == "arrival.coverage.has_center") {
        return ParseBool(value, &scenario->arrivalAirportSectorSnapshot.hasCenterCoverageData);
    }
    if (key == "arrival.coverage.has_terminal") {
        return ParseBool(value, &scenario->arrivalAirportSectorSnapshot.hasTerminalCoverageData);
    }
    if (key == "airport.coverage_builder_icao") {
        scenario->airportCoverageBuildIcao = value;
        return true;
    }
    if (key == "airport.coverage_builder_lat") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->airportCoverageBuildLatitudeDeg = *parsed;
        scenario->hasAirportCoverageBuildCoordinates = true;
        return true;
    }
    if (key == "airport.coverage_builder_lon") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->airportCoverageBuildLongitudeDeg = *parsed;
        scenario->hasAirportCoverageBuildCoordinates = true;
        return true;
    }
    if (key == "airport.coverage_builds_pre_refresh_snapshot") {
        return ParseBool(value, &scenario->airportCoverageBuildsPreRefreshSnapshot);
    }
    if (key == "airport.terminal_probe_lat") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->airportTerminalProbeLatitudeDeg = *parsed;
        scenario->hasAirportTerminalProbeCoordinates = true;
        return true;
    }
    if (key == "airport.terminal_probe_lon") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->airportTerminalProbeLongitudeDeg = *parsed;
        scenario->hasAirportTerminalProbeCoordinates = true;
        return true;
    }
    if (key == "airport.terminal_probe_uses_pre_refresh_snapshot") {
        return ParseBool(value, &scenario->airportTerminalProbeUsesPreRefreshSnapshot);
    }
    if (key == "airport.authority_catalog_fir") {
        scenario->airportCoverageAuthorityCatalogLines.push_back(value);
        return true;
    }
    if (key == "airport.pending_authority_catalog_fir") {
        scenario->pendingAirportCoverageAuthorityCatalogLines.push_back(value);
        scenario->hasPendingAirportCoveragePayloads = true;
        return true;
    }
    if (key == "resolver.route_resolve") {
        return ParseBool(value, &scenario->resolveRouteWithResolver);
    }
    if (key == "resolver.route_builds_pre_refresh_snapshot") {
        return ParseBool(value, &scenario->resolverRouteBuildsPreRefreshSnapshot);
    }
    if (key == "resolver.use_preflight_cache") {
        return ParseBool(value, &scenario->resolverUsesPreflightCache);
    }
    if (key == "resolver.center_feature") {
        return AddCenterCoverageFeature(&scenario->resolverRouteCenterFeatures, value);
    }
    if (key == "resolver.terminal_feature") {
        return AddTerminalCoverageFeature(&scenario->resolverRouteTerminalFeatures, value);
    }
    if (key == "resolver.authority_catalog_fir") {
        scenario->resolverRouteAuthorityCatalogLines.push_back(value);
        return true;
    }
    if (key == "resolver.ownership_json") {
        scenario->resolverRouteOwnershipJson = value;
        return true;
    }
    if (key == "resolver.pending_center_feature") {
        if (!AddCenterCoverageFeature(&scenario->pendingResolverRouteCenterFeatures, value)) {
            return false;
        }
        scenario->hasPendingResolverRoutePayloads = true;
        return true;
    }
    if (key == "resolver.pending_terminal_feature") {
        if (!AddTerminalCoverageFeature(
                &scenario->pendingResolverRouteTerminalFeatures,
                value)) {
            return false;
        }
        scenario->hasPendingResolverRoutePayloads = true;
        return true;
    }
    if (key == "resolver.pending_authority_catalog_fir") {
        scenario->pendingResolverRouteAuthorityCatalogLines.push_back(value);
        scenario->hasPendingResolverRoutePayloads = true;
        return true;
    }
    if (key == "resolver.pending_ownership_json") {
        scenario->pendingResolverRouteOwnershipJson = value;
        scenario->hasPendingResolverRoutePayloads = true;
        return true;
    }
    if (key == "authority_catalog.fir") {
        scenario->authorityCatalogFirLines.push_back(value);
        return true;
    }
    if (key == "authority_catalog.uir") {
        scenario->authorityCatalogUirLines.push_back(value);
        return true;
    }
    if (key == "authority.enroute_handoff") {
        return ParseBool(value, &scenario->authorityEnrouteHandoff);
    }
    if (key == "authority.board_handoff") {
        return ParseBool(value, &scenario->authorityEnrouteHandoff);
    }
    if (key == "authority.enroute_snapshot_available") {
        return ParseBool(value, &scenario->authorityEnrouteSnapshotAvailable);
    }
    if (key == "authority.enroute_snapshot_stale") {
        return ParseBool(value, &scenario->authorityEnrouteSnapshotStale);
    }
    if (key == "authority_position.vatglasses") {
        return AddAuthorityPositionSourceRecord(
            &scenario->authorityPositionRecords,
            xvatsim::core::authority::AuthoritySource::VatGlasses,
            value);
    }
    if (key == "authority_position.extension") {
        return AddAuthorityPositionSourceRecord(
            &scenario->authorityPositionRecords,
            xvatsim::core::authority::AuthoritySource::VatsimRadarExtension,
            value);
    }
    if (key == "authority_position.tracon") {
        return AddAuthorityPositionSourceRecord(
            &scenario->authorityPositionRecords,
            xvatsim::core::authority::AuthoritySource::SimAwareTracon,
            value);
    }
    if (key == "authority_position.special_sector") {
        return AddAuthorityPositionSourceRecord(
            &scenario->authorityPositionRecords,
            xvatsim::core::authority::AuthoritySource::SpecialSectorData,
            value);
    }
    if (key == "authority_position.local") {
        return AddAuthorityPositionSourceRecord(
            &scenario->authorityPositionRecords,
            xvatsim::core::authority::AuthoritySource::AirportLocal,
            value);
    }
    if (key == "authority_position_json.vatglasses") {
        auto records = xvatsim::core::authority::ParseAuthorityPositionSourceRecordsJson(
            xvatsim::core::authority::AuthoritySource::VatGlasses,
            value);
        scenario->authorityPositionRecords.insert(
            scenario->authorityPositionRecords.end(),
            std::make_move_iterator(records.begin()),
            std::make_move_iterator(records.end()));
        return true;
    }
    if (key == "authority_position_json.extension") {
        auto records = xvatsim::core::authority::ParseAuthorityPositionSourceRecordsJson(
            xvatsim::core::authority::AuthoritySource::VatsimRadarExtension,
            value);
        scenario->authorityPositionRecords.insert(
            scenario->authorityPositionRecords.end(),
            std::make_move_iterator(records.begin()),
            std::make_move_iterator(records.end()));
        return true;
    }
    if (key == "authority_position_json.tracon") {
        auto records = xvatsim::core::authority::ParseAuthorityPositionSourceRecordsJson(
            xvatsim::core::authority::AuthoritySource::SimAwareTracon,
            value);
        scenario->authorityPositionRecords.insert(
            scenario->authorityPositionRecords.end(),
            std::make_move_iterator(records.begin()),
            std::make_move_iterator(records.end()));
        return true;
    }
    if (key == "authority_position_json.special_sector") {
        auto records = xvatsim::core::authority::ParseAuthorityPositionSourceRecordsJson(
            xvatsim::core::authority::AuthoritySource::SpecialSectorData,
            value);
        scenario->authorityPositionRecords.insert(
            scenario->authorityPositionRecords.end(),
            std::make_move_iterator(records.begin()),
            std::make_move_iterator(records.end()));
        return true;
    }
    if (key == "authority_polygon.vatspy") {
        return AddAuthorityPolygonSourceRecord(
            &scenario->authorityPolygonRecords,
            xvatsim::core::authority::AuthoritySource::VatSpyBoundary,
            value);
    }
    if (key == "authority_polygon.tracon") {
        return AddAuthorityPolygonSourceRecord(
            &scenario->authorityPolygonRecords,
            xvatsim::core::authority::AuthoritySource::SimAwareTracon,
            value);
    }
    if (key == "authority_polygon.vatglasses") {
        return AddAuthorityPolygonSourceRecord(
            &scenario->authorityPolygonRecords,
            xvatsim::core::authority::AuthoritySource::VatGlasses,
            value);
    }
    if (key == "authority_polygon.extension") {
        return AddAuthorityPolygonSourceRecord(
            &scenario->authorityPolygonRecords,
            xvatsim::core::authority::AuthoritySource::VatsimRadarExtension,
            value);
    }
    if (key == "authority_polygon.special_sector") {
        return AddAuthorityPolygonSourceRecord(
            &scenario->authorityPolygonRecords,
            xvatsim::core::authority::AuthoritySource::SpecialSectorData,
            value);
    }
    if (key == "authority_polygon.local") {
        return AddAuthorityPolygonSourceRecord(
            &scenario->authorityPolygonRecords,
            xvatsim::core::authority::AuthoritySource::AirportLocal,
            value);
    }
    if (key == "source_manifest.json") {
        scenario->sourceManifestJson = value;
        return true;
    }
    if (key == "update.installed_version") {
        scenario->updateInstalledVersion = value;
        return true;
    }
    if (key == "update.manifest_url") {
        scenario->updateManifestUrl = value;
        return true;
    }
    if (key == "update.manifest_payload") {
        scenario->updateManifestPayload = value;
        return true;
    }
    if (key == "source_registry.json" ||
        key == "source_package.registry_json") {
        scenario->sourceRegistryJsons.push_back(value);
        return true;
    }
    if (key == "source_registry.file" ||
        key == "source_package.registry_file") {
        std::ifstream stream(value);
        if (!stream) {
            return false;
        }
        scenario->sourceRegistryJsons.emplace_back(
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>());
        return true;
    }
    if (key == "source_package.registry_payload") {
        const auto separator = value.find("=>");
        if (separator == std::string::npos) {
            return false;
        }
        const auto url = Trim(value.substr(0, separator));
        const auto payload = Trim(value.substr(separator + 2));
        if (url.empty() || payload.empty()) {
            return false;
        }
        scenario->sourceRegistryPayloadsByUrl[url] = payload;
        return true;
    }
    if (key == "source_package.positions_json") {
        scenario->sourcePackagePositionsJson = value;
        return true;
    }
    if (key == "source_package.airspace_json") {
        scenario->sourcePackageAirspaceJson = value;
        return true;
    }
    if (key == "source_package.ownership_json") {
        scenario->sourcePackageOwnershipJson = value;
        return true;
    }
    if (key == "source_package.special_sector_json") {
        scenario->sourcePackageSpecialSectorJsons.push_back(value);
        return true;
    }
    if (key == "source_package.terminal_authority_json") {
        scenario->sourcePackageTerminalAuthorityJsons.push_back(value);
        return true;
    }
    if (key == "tuning.arrival_wake_distance_nm") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->tuning.arrivalWakeDistanceNm = *parsed;
        return true;
    }
    if (key == "tuning.departure_confirm_distance_nm") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->tuning.departureConfirmDistanceNm = *parsed;
        return true;
    }
    if (key == "tuning.destination_ground_distance_nm") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->tuning.destinationGroundDistanceNm = *parsed;
        return true;
    }
    if (key == "tuning.departure_release_hold_seconds") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->tuning.departureReleaseHoldSeconds = *parsed;
        return true;
    }
    if (key == "traversal.route_sample_step_nm") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->traversalTuning.routeSampleStepNm = *parsed;
        return true;
    }
    if (key == "traversal.mode") {
        const auto normalized = ToUpperCopy(Trim(value));
        if (normalized == "EXACT") {
            scenario->traversalTuning.mode = xvatsim::core::route::TraversalMode::Exact;
            return true;
        }
        if (normalized == "SAMPLED") {
            scenario->traversalTuning.mode = xvatsim::core::route::TraversalMode::Sampled;
            return true;
        }
        if (normalized != "EXACT" && normalized != "SAMPLED") {
            return false;
        }
    }
    if (key == "traversal.route_sector_sanity_limit") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->traversalTuning.routeSectorSanityLimit = static_cast<std::size_t>(*parsed);
        return true;
    }
    if (key == "state.flight_active") {
        return ParseBool(value, &scenario->workflowState.flightContext.active);
    }
    if (key == "state.departure_released") {
        return ParseBool(value, &scenario->workflowState.departureReleasedThisFlight);
    }
    if (key == "state.arrival_awake") {
        return ParseBool(value, &scenario->workflowState.arrivalAwakeThisFlight);
    }
    if (key == "state.airborne_since_seconds") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->workflowState.airborneSinceSeconds = *parsed;
        return true;
    }
    if (key == "flight.callsign") {
        scenario->workflowState.flightContext.callsign = value;
        return true;
    }
    if (key == "flight.departure_icao") {
        scenario->workflowState.flightContext.departureIcao = value;
        return true;
    }
    if (key == "flight.departure_lat") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->workflowState.flightContext.departureLatDeg = *parsed;
        return true;
    }
    if (key == "flight.departure_lon") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->workflowState.flightContext.departureLonDeg = *parsed;
        return true;
    }
    if (key == "flight.has_departure_coordinates") {
        return ParseBool(value, &scenario->workflowState.flightContext.hasDepartureCoordinates);
    }
    if (key == "flight.destination_icao") {
        scenario->workflowState.flightContext.destinationIcao = value;
        return true;
    }
    if (key == "flight.destination_lat") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->workflowState.flightContext.destinationLatDeg = *parsed;
        return true;
    }
    if (key == "flight.destination_lon") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->workflowState.flightContext.destinationLonDeg = *parsed;
        return true;
    }
    if (key == "flight.has_destination_coordinates") {
        return ParseBool(value, &scenario->workflowState.flightContext.hasDestinationCoordinates);
    }
    if (key == "flight.route_text") {
        scenario->workflowState.flightContext.routeText = value;
        return true;
    }
    if (key == "aircraft.valid") {
        return ParseBool(value, &scenario->aircraftState.valid);
    }
    if (key == "aircraft.on_ground") {
        return ParseBool(value, &scenario->aircraftState.onGround);
    }
    if (key == "aircraft.battery_on") {
        return ParseBool(value, &scenario->aircraftState.batteryOn);
    }
    if (key == "aircraft.latitude_deg") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->aircraftState.latitudeDeg = *parsed;
        return true;
    }
    if (key == "aircraft.longitude_deg") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->aircraftState.longitudeDeg = *parsed;
        return true;
    }
    if (key == "transceiver_resolver_holdover.probe") {
        return ParseBool(value, &scenario->transceiverResolverHoldoverProbe);
    }
    if (key == "transceiver_resolver_holdover.cache_age_seconds") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->transceiverResolverHoldoverCacheAgeSeconds =
            static_cast<long long>(*parsed);
        return true;
    }
    if (key == "transceiver_resolver_holdover.last_fetch_succeeded") {
        return ParseBool(
            value,
            &scenario->transceiverResolverHoldoverLastFetchSucceeded);
    }
    if (key == "transceiver_resolver_authority.probe") {
        return ParseBool(value, &scenario->transceiverResolverAuthorityProbe);
    }
    if (key == "transceiver_resolver_authority.cache_age_seconds") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->transceiverResolverAuthorityCacheAgeSeconds =
            static_cast<long long>(*parsed);
        return true;
    }
    if (key == "transceiver_resolver_authority.last_fetch_succeeded") {
        return ParseBool(
            value,
            &scenario->transceiverResolverAuthorityLastFetchSucceeded);
    }
    if (key == "transceiver_resolver_airport_coverage.probe") {
        return ParseBool(
            value,
            &scenario->transceiverResolverAirportCoverageProbe);
    }
    if (key == "transceiver_resolver_airport_coverage.cache_age_seconds") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->transceiverResolverAirportCoverageCacheAgeSeconds =
            static_cast<long long>(*parsed);
        return true;
    }
    if (key == "transceiver_resolver_airport_coverage.last_fetch_succeeded") {
        return ParseBool(
            value,
            &scenario
                 ->transceiverResolverAirportCoverageLastFetchSucceeded);
    }
    if (key == "transceiver_resolver_airport_coverage.has_coordinates") {
        return ParseBool(
            value,
            &scenario->transceiverResolverAirportCoverageHasCoordinates);
    }
    if (key == "transceiver_resolver_airport_coverage.lat") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->transceiverResolverAirportCoverageLatitudeDeg = *parsed;
        return true;
    }
    if (key == "transceiver_resolver_airport_coverage.lon") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->transceiverResolverAirportCoverageLongitudeDeg = *parsed;
        return true;
    }
    if (key == "radio.valid") {
        return ParseBool(value, &scenario->radioStateSnapshot.valid);
    }
    if (key == "radio.com1_active") {
        scenario->radioStateSnapshot.com1ActiveFrequency = value;
        return true;
    }
    if (key == "radio.com2_active") {
        scenario->radioStateSnapshot.com2ActiveFrequency = value;
        return true;
    }
    if (key == "radio.com1_standby") {
        scenario->radioStateSnapshot.com1StandbyFrequency = value;
        return true;
    }
    if (key == "xpilot.connected") {
        return ParseBool(value, &scenario->xPilotSessionSnapshot.connected);
    }
    if (key == "recovery.requested") {
        return ParseBool(value, &scenario->recoveryRequested);
    }
    if (key == "recovery.mode") {
        const auto parsed = ParseRecoveryRequestMode(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->recoveryMode = *parsed;
        return true;
    }
    if (key == "plan.matched") {
        return ParseBool(value, &scenario->networkPlanSnapshot.matched);
    }
    if (key == "plan.callsign" || key == "plan.matched_callsign") {
        scenario->networkPlanSnapshot.matchedCallsign = value;
        return true;
    }
    if (key == "plan.stale") {
        return ParseBool(value, &scenario->networkPlanSnapshot.stale);
    }
    if (key == "plan.departure_icao") {
        scenario->networkPlanSnapshot.departureIcao = value;
        return true;
    }
    if (key == "plan.departure_lat") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->networkPlanSnapshot.departureLatDeg = *parsed;
        return true;
    }
    if (key == "plan.departure_lon") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->networkPlanSnapshot.departureLonDeg = *parsed;
        return true;
    }
    if (key == "plan.has_departure_coordinates") {
        return ParseBool(value, &scenario->networkPlanSnapshot.hasDepartureCoordinates);
    }
    if (key == "plan.destination_icao") {
        scenario->networkPlanSnapshot.destinationIcao = value;
        return true;
    }
    if (key == "plan.destination_lat") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->networkPlanSnapshot.destinationLatDeg = *parsed;
        return true;
    }
    if (key == "plan.destination_lon") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->networkPlanSnapshot.destinationLonDeg = *parsed;
        return true;
    }
    if (key == "plan.has_destination_coordinates") {
        return ParseBool(value, &scenario->networkPlanSnapshot.hasDestinationCoordinates);
    }
    if (key == "plan.route_text") {
        scenario->networkPlanSnapshot.routeText = value;
        return true;
    }
    if (key == "preflight.fms_line") {
        scenario->preflightFmsText += value;
        scenario->preflightFmsText += "\n";
        return true;
    }
    if (key == "preflight.current_fms_line") {
        scenario->preflightCurrentFmsText += value;
        scenario->preflightCurrentFmsText += "\n";
        return true;
    }
    if (key == "preflight.validate_against_plan") {
        return ParseBool(value, &scenario->preflightValidateAgainstPlan);
    }
    if (key == "preflight.verify_source_file") {
        return ParseBool(value, &scenario->preflightVerifySourceFile);
    }
    if (key == "route.stale") {
        return ParseBool(value, &scenario->routeSectorSnapshot.stale);
    }
    if (key == "fms.current_airport_icao") {
        scenario->flightPlanSnapshot.currentAirportIcao = value;
        return true;
    }
    if (key == "expect.stage") {
        scenario->expectations.stage = ParseWorkflowStage(value);
        return scenario->expectations.stage.has_value();
    }
    if (key == "expect.reason") {
        scenario->expectations.reason = value;
        return true;
    }
    if (key == "expect.departure_location_confirmed") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.departureLocationConfirmed = parsed;
        return true;
    }
    if (key == "expect.recovery_accepted") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.recoveryAccepted = parsed;
        return true;
    }
    if (key == "expect.recovery_stage") {
        scenario->expectations.recoveryStage = ParseWorkflowStage(value);
        return scenario->expectations.recoveryStage.has_value();
    }
    if (key == "expect.recovery_reason") {
        scenario->expectations.recoveryReason = value;
        return true;
    }
    if (key == "expect.recovery_used_preserved_context") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.recoveryUsedPreservedContext = parsed;
        return true;
    }
    if (key == "expect.recovery_used_fresh_network_plan") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.recoveryUsedFreshNetworkPlan = parsed;
        return true;
    }
    if (key == "expect.recovery_flight_context_active") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.recoveryFlightContextActive = parsed;
        return true;
    }
    if (key == "expect.display_source") {
        scenario->expectations.displaySource = ParseBoardSource(value);
        return scenario->expectations.displaySource.has_value();
    }
    if (key == "expect.display_callsigns") {
        scenario->expectations.displayCallsigns = Split(value, ',');
        return true;
    }
    if (key == "expect.overlay_body_lines") {
        scenario->expectations.overlayBodyLines = Split(value, '|');
        return true;
    }
    if (key == "expect.overlay_body_tones") {
        scenario->expectations.overlayBodyTones = Split(value, ',');
        return true;
    }
    if (key == "expect.overlay_version_text") {
        scenario->expectations.overlayVersionText = value;
        return true;
    }
    if (key == "expect.overlay_version_alternate_text") {
        scenario->expectations.overlayVersionAlternateText = value;
        return true;
    }
    if (key == "expect.overlay_version_tone") {
        scenario->expectations.overlayVersionTone = value;
        return true;
    }
    if (key == "expect.overlay_version_rotates") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.overlayVersionRotates = parsed;
        return true;
    }
    if (key == "expect.overlay_notice_visible") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.overlayNoticeVisible = parsed;
        return true;
    }
    if (key == "expect.overlay_notice_severity") {
        scenario->expectations.overlayNoticeSeverity = value;
        return true;
    }
    if (key == "expect.overlay_notice_title") {
        scenario->expectations.overlayNoticeTitle = value;
        return true;
    }
    if (key == "expect.overlay_notice_body_lines") {
        scenario->expectations.overlayNoticeBodyLines = Split(value, '|');
        return true;
    }
    if (key == "expect.departure_collected_available") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.departureCollectedAvailable = parsed;
        return true;
    }
    if (key == "expect.departure_collected_callsigns") {
        scenario->expectations.departureCollectedCallsigns = Split(value, ',');
        return true;
    }
    if (key == "expect.arrival_airspace_available") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.arrivalAirspaceAvailable = parsed;
        return true;
    }
    if (key == "expect.arrival_airspace_callsigns") {
        scenario->expectations.arrivalAirspaceCallsigns = Split(value, ',');
        return true;
    }
    if (key == "expect.arrival_local_available") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.arrivalLocalAvailable = parsed;
        return true;
    }
    if (key == "expect.arrival_local_callsigns") {
        scenario->expectations.arrivalLocalCallsigns = Split(value, ',');
        return true;
    }
    if (key == "expect.airport_coverage_available") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.airportCoverageAvailable = parsed;
        return true;
    }
    if (key == "expect.airport_terminal_inside") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.airportTerminalInside = parsed;
        return true;
    }
    if (key == "expect.airport_coverage_match_tokens") {
        scenario->expectations.airportCoverageMatchTokens = Split(value, ',');
        return true;
    }
    if (key == "expect.airport_coverage_controller_prefixes") {
        scenario->expectations.airportCoverageControllerPrefixes = Split(value, ',');
        return true;
    }
    if (key == "expect.airport_coverage_controller_patterns") {
        scenario->expectations.airportCoverageControllerPatterns = Split(value, ',');
        return true;
    }
    if (key == "expect.airport_coverage_generations") {
        scenario->expectations.airportCoverageGenerations = Split(value, ',');
        return true;
    }
    if (key == "expect.enroute_available") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.enrouteAvailable = parsed;
        return true;
    }
    if (key == "expect.enroute_callsigns") {
        scenario->expectations.enrouteCallsigns = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_route_available") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.resolverRouteAvailable = parsed;
        return true;
    }
    if (key == "expect.resolver_route_resolved") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.resolverRouteResolved = parsed;
        return true;
    }
    if (key == "expect.resolver_route_status") {
        scenario->expectations.resolverRouteStatus = value;
        return true;
    }
    if (key == "expect.resolver_route_authority_gaps") {
        scenario->expectations.resolverRouteAuthorityGaps = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_route_current_sectors") {
        scenario->expectations.resolverRouteCurrentSectors = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_route_next_sectors") {
        scenario->expectations.resolverRouteNextSectors = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_route_current_controller_patterns") {
        scenario->expectations.resolverRouteCurrentControllerPatterns = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_route_next_controller_patterns") {
        scenario->expectations.resolverRouteNextControllerPatterns = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_route_current_controller_prefixes") {
        scenario->expectations.resolverRouteCurrentControllerPrefixes = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_route_next_controller_prefixes") {
        scenario->expectations.resolverRouteNextControllerPrefixes = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_route_generations") {
        scenario->expectations.resolverRouteGenerations = Split(value, ',');
        return true;
    }
    if (key == "expect.route_authority_plan_sequence") {
        scenario->expectations.routeAuthorityPlanSequence = Split(value, ',');
        return true;
    }
    if (key == "expect.route_authority_plan_flags") {
        scenario->expectations.routeAuthorityPlanFlags = Split(value, ',');
        return true;
    }
    if (key == "expect.route_authority_plan_sources") {
        scenario->expectations.routeAuthorityPlanSources = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_authority_relevance_available") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.resolverAuthorityRelevanceAvailable = parsed;
        return true;
    }
    if (key == "expect.resolver_authority_status") {
        scenario->expectations.resolverAuthorityStatus = value;
        return true;
    }
    if (key == "expect.resolver_authority_cache_status") {
        scenario->expectations.resolverAuthorityCacheStatus = value;
        return true;
    }
    if (key == "expect.resolver_authority_cache_reason") {
        scenario->expectations.resolverAuthorityCacheReason = value;
        return true;
    }
    if (key == "expect.resolver_authority_repeat_cache_status") {
        scenario->expectations.resolverAuthorityRepeatCacheStatus = value;
        return true;
    }
    if (key == "expect.resolver_authority_repeat_cache_reason") {
        scenario->expectations.resolverAuthorityRepeatCacheReason = value;
        return true;
    }
    if (key == "expect.resolver_authority_diagnostics") {
        scenario->expectations.resolverAuthorityDiagnostics = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_authority_relevant_matches") {
        scenario->expectations.resolverAuthorityRelevantMatches = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_authority_repeat_relevant_matches") {
        scenario->expectations.resolverAuthorityRepeatRelevantMatches =
            Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_authority_proof_sources") {
        scenario->expectations.resolverAuthorityProofSources = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_authority_proof_details") {
        scenario->expectations.resolverAuthorityProofDetails = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_authority_proof_detail_contains") {
        scenario->expectations.resolverAuthorityProofDetailContains = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_authority_evidence_visibility") {
        scenario->expectations.resolverAuthorityEvidenceVisibility = value;
        return true;
    }
    if (key == "expect.resolver_authority_controller_evidence") {
        scenario->expectations.resolverAuthorityControllerEvidence = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_authority_decision_evidence") {
        scenario->expectations.resolverAuthorityDecisionEvidence = Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_authority_polygon_evidence_contains") {
        scenario->expectations.resolverAuthorityPolygonEvidenceContains =
            Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_authority_active_polygon_evidence_contains") {
        scenario->expectations.resolverAuthorityActivePolygonEvidenceContains =
            Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_authority_transceiver_proof_evidence_contains") {
        scenario->expectations.resolverAuthorityTransceiverProofEvidenceContains =
            Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_authority_duplicated_atis_proof_evidence_contains") {
        scenario->expectations.resolverAuthorityDuplicatedAtisProofEvidenceContains =
            Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_authority_preview_summary") {
        scenario->expectations.resolverAuthorityPreviewSummary = value;
        return true;
    }
    if (key == "expect.resolver_authority_preview_decisions_contains") {
        scenario->expectations.resolverAuthorityPreviewDecisionsContains =
            Split(value, ',');
        return true;
    }
    if (key == "expect.resolver_enroute_available") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.resolverEnrouteAvailable = parsed;
        return true;
    }
    if (key == "expect.resolver_enroute_callsigns") {
        scenario->expectations.resolverEnrouteCallsigns = Split(value, ',');
        return true;
    }
    if (key == "expect.source_registry_values") {
        scenario->expectations.sourceRegistryValues = Split(value, ',');
        return true;
    }
    if (key == "expect.source_registry_count") {
        const auto parsed = ParseDouble(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->expectations.sourceRegistryCount = static_cast<int>(*parsed);
        return true;
    }
    if (key == "expect.source_registry_source_counts") {
        scenario->expectations.sourceRegistrySourceCounts = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_catalog_ids") {
        scenario->expectations.authorityCatalogIds = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_data_gaps") {
        scenario->expectations.authorityDataGaps = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_active_matches") {
        scenario->expectations.authorityActiveMatches = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_unmapped_callsigns") {
        scenario->expectations.authorityUnmappedCallsigns = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_polygon_ids") {
        scenario->expectations.authorityPolygonIds = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_polygon_lookup_keys") {
        scenario->expectations.authorityPolygonLookupKeys = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_polygon_ring_counts") {
        scenario->expectations.authorityPolygonRingCounts = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_polygon_data_gaps") {
        scenario->expectations.authorityPolygonDataGaps = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_active_polygon_matches") {
        scenario->expectations.authorityActivePolygonMatches = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_active_polygon_data_gaps") {
        scenario->expectations.authorityActivePolygonDataGaps = Split(value, ',');
        return true;
    }
    if (key == "expect.authority_relevant_polygon_matches") {
        scenario->expectations.authorityRelevantPolygonMatches = Split(value, ',');
        return true;
    }
    if (key == "expect.source_manifest_valid") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.sourceManifestValid = parsed;
        return true;
    }
    if (key == "expect.source_manifest_values") {
        scenario->expectations.sourceManifestValues = Split(value, ',');
        return true;
    }
    if (key == "expect.source_package_payload") {
        scenario->expectations.sourcePackagePayload = value;
        return true;
    }
    if (key == "expect.update_status") {
        scenario->expectations.updateStatus = value;
        return true;
    }
    if (key == "expect.update_latest_version") {
        scenario->expectations.updateLatestVersion = value;
        return true;
    }
    if (key == "expect.update_download_page_url") {
        scenario->expectations.updateDownloadPageUrl = value;
        return true;
    }
    if (key == "expect.update_error_class") {
        scenario->expectations.updateErrorClass = value;
        return true;
    }
    if (key == "expect.update_critical") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.updateCritical = parsed;
        return true;
    }
    if (key == "expect.route_resolved") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.routeResolved = parsed;
        return true;
    }
    if (key == "expect.route_current_sectors") {
        scenario->expectations.routeCurrentSectors = Split(value, ',');
        return true;
    }
    if (key == "expect.route_next_sectors") {
        scenario->expectations.routeNextSectors = Split(value, ',');
        return true;
    }
    if (key == "expect.route_current_controller_prefixes") {
        scenario->expectations.routeCurrentControllerPrefixes = Split(value, ',');
        return true;
    }
    if (key == "expect.route_next_controller_prefixes") {
        scenario->expectations.routeNextControllerPrefixes = Split(value, ',');
        return true;
    }
    if (key == "expect.route_token_kinds") {
        scenario->expectations.routeTokenKinds = Split(value, ',');
        return true;
    }
    if (key == "expect.resolved_waypoints") {
        scenario->expectations.resolvedWaypointIdents = Split(value, ',');
        return true;
    }
    if (key == "expect.resolved_waypoint_points") {
        scenario->expectations.resolvedWaypointPoints.clear();
        for (const auto& part : Split(value, '|')) {
            const auto atIndex = part.find('@');
            if (atIndex == std::string::npos) {
                return false;
            }
            const auto ident = Trim(part.substr(0, atIndex));
            const auto coordinateParts = Split(part.substr(atIndex + 1), ',');
            if (ident.empty() || coordinateParts.size() != 2) {
                return false;
            }

            const auto latitudeDeg = ParseDouble(coordinateParts[0]);
            const auto longitudeDeg = ParseDouble(coordinateParts[1]);
            if (!latitudeDeg.has_value() || !longitudeDeg.has_value()) {
                return false;
            }

            scenario->expectations.resolvedWaypointPoints.push_back({
                ident,
                *latitudeDeg,
                *longitudeDeg,
            });
        }
        return true;
    }
    if (key == "expect.resolved_tokens") {
        scenario->expectations.resolvedTokens = Split(value, ',');
        return true;
    }
    if (key == "expect.expanded_tokens") {
        scenario->expectations.expandedTokens = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_tokens") {
        scenario->expectations.recognizedProcedureTokens = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_sources") {
        scenario->expectations.procedureMetadataSources = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_records") {
        scenario->expectations.procedureRecordKinds = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_runways") {
        scenario->expectations.procedureRunwayRecords = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_authorities") {
        scenario->expectations.procedureCatalogAuthorities = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_catalog_fixes") {
        scenario->expectations.procedureCatalogFixes = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_boundary_fixes") {
        scenario->expectations.procedureBoundaryFixes = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_ordered_fixes") {
        scenario->expectations.procedureOrderedFixes = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_synthetic_waypoints") {
        scenario->expectations.procedureSyntheticWaypoints = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_synthetic_sources") {
        scenario->expectations.procedureSyntheticSources = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_application_states") {
        scenario->expectations.procedureApplicationStates = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_application_blocks") {
        scenario->expectations.procedureApplicationBlocks = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_applied_fix_sequences") {
        scenario->expectations.procedureAppliedFixSequences = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_catalog_transitions") {
        scenario->expectations.procedureCatalogTransitions = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_support") {
        scenario->expectations.procedureSupportDirections = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_links") {
        scenario->expectations.procedureTransitionLinks = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_misses") {
        scenario->expectations.procedureTransitionMisses = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_anchor_links") {
        scenario->expectations.procedureAnchorLinks = Split(value, ',');
        return true;
    }
    if (key == "expect.procedure_context_only") {
        scenario->expectations.procedureContextOnlyTokens = Split(value, ',');
        return true;
    }
    if (key == "expect.ignored_tokens") {
        scenario->expectations.ignoredTokens = Split(value, ',');
        return true;
    }
    if (key == "expect.unsupported_tokens") {
        scenario->expectations.unsupportedTokens = Split(value, ',');
        return true;
    }
    if (key == "expect.unresolved_tokens") {
        scenario->expectations.unresolvedTokens = Split(value, ',');
        return true;
    }
    if (key == "expect.unresolved_airway_tokens") {
        scenario->expectations.unresolvedAirwayTokens = Split(value, ',');
        return true;
    }
    if (key == "expect.preflight_parse_ok") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.preflightParseOk = parsed;
        return true;
    }
    if (key == "expect.preflight_validation_accepted") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.preflightValidationAccepted = parsed;
        return true;
    }
    if (key == "expect.preflight_validation_reason") {
        scenario->expectations.preflightValidationReason = value;
        return true;
    }
    if (key == "expect.preflight_departure_icao") {
        scenario->expectations.preflightDepartureIcao = value;
        return true;
    }
    if (key == "expect.preflight_destination_icao") {
        scenario->expectations.preflightDestinationIcao = value;
        return true;
    }
    if (key == "expect.preflight_waypoints") {
        scenario->expectations.preflightWaypointIdents = Split(value, ',');
        return true;
    }
    if (key == "expect.brain_work_order") {
        scenario->expectations.brainWorkOrder = Split(value, ',');
        return true;
    }
    if (key == "expect.brain_work_heavy") {
        scenario->expectations.brainWorkHeavyFlags = Split(value, ',');
        return true;
    }
    if (key == "expect.brain_scheduler_runnable") {
        scenario->expectations.brainSchedulerRunnable = Split(value, ',');
        return true;
    }
    if (key == "expect.brain_scheduler_deferred") {
        scenario->expectations.brainSchedulerDeferred = Split(value, ',');
        return true;
    }
    if (key == "expect.brain_scheduler_heavy_counts") {
        scenario->expectations.brainSchedulerHeavyCounts = value;
        return true;
    }
    if (key == "expect.brain_route_plan_rebuild_sequence") {
        scenario->expectations.brainRoutePlanRebuildSequence = Split(value, ',');
        return true;
    }
    if (key == "expect.brain_route_plan_rebuild_lifecycle") {
        scenario->expectations.brainRoutePlanRebuildLifecycle = value;
        return true;
    }
    if (key == "expect.brain_route_plan_pending_sequence") {
        scenario->expectations.brainRoutePlanPendingSequence = Split(value, ',');
        return true;
    }
    if (key == "expect.brain_route_plan_pending_lifecycle") {
        scenario->expectations.brainRoutePlanPendingLifecycle = value;
        return true;
    }
    if (key == "expect.brain_departure_work_order") {
        scenario->expectations.brainDepartureWorkOrder = Split(value, ',');
        return true;
    }
    if (key == "expect.brain_departure_scheduler_runnable") {
        scenario->expectations.brainDepartureSchedulerRunnable = Split(value, ',');
        return true;
    }
    if (key == "expect.brain_departure_scheduler_deferred") {
        scenario->expectations.brainDepartureSchedulerDeferred = Split(value, ',');
        return true;
    }
    if (key == "expect.brain_departure_scheduler_heavy_counts") {
        scenario->expectations.brainDepartureSchedulerHeavyCounts = value;
        return true;
    }
    if (key == "expect.brain_departure_snapshot_lifecycle") {
        scenario->expectations.brainDepartureSnapshotLifecycle = value;
        return true;
    }
    if (key == "expect.brain_departure_pending_lifecycle") {
        scenario->expectations.brainDeparturePendingLifecycle = value;
        return true;
    }
    if (key == "expect.radio_reachable_candidates") {
        scenario->expectations.radioReachableCandidates = Split(value, ',');
        return true;
    }
    if (key == "expect.radio_reachable_counts") {
        scenario->expectations.radioReachableCounts = value;
        return true;
    }
    if (key == "expect.radio_reachable_hash_check") {
        scenario->expectations.radioReachableHashCheck = value;
        return true;
    }
    if (key == "expect.radio_reachable_source_candidates") {
        scenario->expectations.radioReachableSourceCandidates = Split(value, ',');
        return true;
    }
    if (key == "expect.radio_reachable_source_counts") {
        scenario->expectations.radioReachableSourceCounts = value;
        return true;
    }
    if (key == "expect.transceiver_resolver_holdover_available") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.transceiverResolverHoldoverAvailable = parsed;
        return true;
    }
    if (key == "expect.transceiver_resolver_holdover_stale") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.transceiverResolverHoldoverStale = parsed;
        return true;
    }
    if (key == "expect.transceiver_resolver_holdover_status_contains") {
        scenario->expectations.transceiverResolverHoldoverStatusContains = value;
        return true;
    }
    if (key == "expect.transceiver_resolver_holdover_candidates") {
        scenario->expectations.transceiverResolverHoldoverCandidates =
            Split(value, ',');
        return true;
    }
    if (key == "expect.transceiver_resolver_holdover_radio_candidates") {
        scenario->expectations.transceiverResolverHoldoverRadioCandidates =
            Split(value, ',');
        return true;
    }
    if (key == "expect.transceiver_resolver_holdover_radio_counts") {
        scenario->expectations.transceiverResolverHoldoverRadioCounts = value;
        return true;
    }
    if (key == "expect.transceiver_resolver_holdover_radio_status_contains") {
        scenario->expectations
            .transceiverResolverHoldoverRadioStatusContains = value;
        return true;
    }
    if (key == "expect.transceiver_resolver_holdover_source_evidence") {
        scenario->expectations.transceiverResolverHoldoverSourceEvidence = value;
        return true;
    }
    if (key == "expect.transceiver_resolver_holdover_evidence_visibility") {
        scenario->expectations.transceiverResolverHoldoverEvidenceVisibility =
            value;
        return true;
    }
    if (key == "expect.transceiver_resolver_holdover_controller_evidence") {
        scenario->expectations.transceiverResolverHoldoverControllerEvidence =
            Split(value, ',');
        return true;
    }
    if (key == "expect.transceiver_resolver_holdover_station_evidence") {
        scenario->expectations.transceiverResolverHoldoverStationEvidence =
            Split(value, ',');
        return true;
    }
    if (key == "expect.transceiver_resolver_holdover_brain_preview_summary") {
        scenario->expectations
            .transceiverResolverHoldoverBrainPreviewSummary = value;
        return true;
    }
    if (key == "expect.transceiver_resolver_holdover_brain_preview_decisions") {
        scenario->expectations
            .transceiverResolverHoldoverBrainPreviewDecisions =
            Split(value, ',');
        return true;
    }
    if (key == "expect.transceiver_resolver_authority_available") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.transceiverResolverAuthorityAvailable = parsed;
        return true;
    }
    if (key == "expect.transceiver_resolver_authority_stale") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.transceiverResolverAuthorityStale = parsed;
        return true;
    }
    if (key == "expect.transceiver_resolver_authority_status_contains") {
        scenario->expectations.transceiverResolverAuthorityStatusContains =
            value;
        return true;
    }
    if (key == "expect.transceiver_resolver_authority_candidates") {
        scenario->expectations.transceiverResolverAuthorityCandidates =
            Split(value, ',');
        return true;
    }
    if (key == "expect.transceiver_resolver_authority_source_evidence") {
        scenario->expectations.transceiverResolverAuthoritySourceEvidence =
            value;
        return true;
    }
    if (key == "expect.transceiver_resolver_authority_evidence_visibility") {
        scenario->expectations.transceiverResolverAuthorityEvidenceVisibility =
            value;
        return true;
    }
    if (key == "expect.transceiver_resolver_authority_controller_evidence") {
        scenario->expectations.transceiverResolverAuthorityControllerEvidence =
            Split(value, ',');
        return true;
    }
    if (key == "expect.transceiver_resolver_authority_station_evidence") {
        scenario->expectations.transceiverResolverAuthorityStationEvidence =
            Split(value, ',');
        return true;
    }
    if (key == "expect.transceiver_resolver_authority_preview_summary") {
        scenario->expectations.transceiverResolverAuthorityPreviewSummary =
            value;
        return true;
    }
    if (key == "expect.transceiver_resolver_authority_preview_decisions") {
        scenario->expectations.transceiverResolverAuthorityPreviewDecisions =
            Split(value, ',');
        return true;
    }
    if (key == "expect.transceiver_resolver_airport_coverage_available") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.transceiverResolverAirportCoverageAvailable =
            parsed;
        return true;
    }
    if (key == "expect.transceiver_resolver_airport_coverage_stale") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->expectations.transceiverResolverAirportCoverageStale =
            parsed;
        return true;
    }
    if (key ==
        "expect.transceiver_resolver_airport_coverage_status_contains") {
        scenario->expectations
            .transceiverResolverAirportCoverageStatusContains = value;
        return true;
    }
    if (key == "expect.transceiver_resolver_airport_coverage_candidates") {
        scenario->expectations
            .transceiverResolverAirportCoverageCandidates = Split(value, ',');
        return true;
    }
    if (key ==
        "expect.transceiver_resolver_airport_coverage_source_evidence") {
        scenario->expectations
            .transceiverResolverAirportCoverageSourceEvidence = value;
        return true;
    }
    if (key ==
        "expect.transceiver_resolver_airport_coverage_evidence_visibility") {
        scenario->expectations
            .transceiverResolverAirportCoverageEvidenceVisibility = value;
        return true;
    }
    if (key ==
        "expect.transceiver_resolver_airport_coverage_controller_evidence") {
        scenario->expectations
            .transceiverResolverAirportCoverageControllerEvidence =
            Split(value, ',');
        return true;
    }
    if (key ==
        "expect.transceiver_resolver_airport_coverage_station_evidence") {
        scenario->expectations
            .transceiverResolverAirportCoverageStationEvidence =
            Split(value, ',');
        return true;
    }
    if (key ==
        "expect.transceiver_resolver_airport_coverage_preview_summary") {
        scenario->expectations
            .transceiverResolverAirportCoveragePreviewSummary = value;
        return true;
    }
    if (key ==
        "expect.transceiver_resolver_airport_coverage_preview_decisions") {
        scenario->expectations
            .transceiverResolverAirportCoveragePreviewDecisions =
            Split(value, ',');
        return true;
    }
    if (key == "expect.radio_reachable_gate_departure_candidates") {
        scenario->expectations.radioReachableGateDepartureCandidates = Split(value, ',');
        return true;
    }
    if (key == "expect.radio_reachable_gate_enroute_candidates") {
        scenario->expectations.radioReachableGateEnrouteCandidates = Split(value, ',');
        return true;
    }
    if (key == "expect.radio_reachable_gate_arrival_candidates") {
        scenario->expectations.radioReachableGateArrivalCandidates = Split(value, ',');
        return true;
    }
    if (key == "expect.radio_reachable_gate_none_candidates") {
        scenario->expectations.radioReachableGateNoneCandidates = Split(value, ',');
        return true;
    }
    if (key == "expect.radio_reachable_verifier_enroute_controllers") {
        scenario->expectations.radioReachableVerifierEnrouteControllers = Split(value, ',');
        return true;
    }
    if (key == "expect.radio_reachable_verifier_unchanged_controllers") {
        scenario->expectations.radioReachableVerifierUnchangedControllers = Split(value, ',');
        return true;
    }
    if (key == "expect.radio_reachable_verifier_unchanged_status") {
        scenario->expectations.radioReachableVerifierUnchangedStatus = value;
        return true;
    }
    if (key == "expect.terminal_authority_owners") {
        scenario->expectations.terminalAuthorityOwners = Split(value, ',');
        return true;
    }
    if (key == "expect.terminal_authority_polygons") {
        scenario->expectations.terminalAuthorityPolygons = Split(value, ',');
        return true;
    }
    if (key == "expect.airport_frequency_departure_records") {
        scenario->expectations.airportFrequencyDepartureRecords = Split(value, ',');
        return true;
    }
    if (key == "expect.airport_frequency_arrival_records") {
        scenario->expectations.airportFrequencyArrivalRecords = Split(value, ',');
        return true;
    }
    if (key == "expect.brain_controller_relevance_departure_callsigns") {
        scenario->expectations.brainControllerRelevanceDepartureCallsigns =
            Split(value, ',');
        return true;
    }
    if (key == "expect.brain_controller_relevance_arrival_callsigns") {
        scenario->expectations.brainControllerRelevanceArrivalCallsigns =
            Split(value, ',');
        return true;
    }
    if (key == "expect.brain_controller_relevance_enroute_callsigns") {
        scenario->expectations.brainControllerRelevanceEnrouteCallsigns =
            Split(value, ',');
        return true;
    }
    if (key == "expect.brain_controller_relevance_completions") {
        scenario->expectations.brainControllerRelevanceCompletions =
            Split(value, ',');
        return true;
    }
    if (key == "expect.phase_publisher_reuse_lifecycle") {
        scenario->expectations.phasePublisherReuseLifecycle = Split(value, ',');
        return true;
    }
    if (key == "expect.phase_publisher_isolation_lifecycle") {
        scenario->expectations.phasePublisherIsolationLifecycle = Split(value, ',');
        return true;
    }
    if (key == "expect.phase_publisher_workflow_clear_lifecycle") {
        scenario->expectations.phasePublisherWorkflowClearLifecycle = Split(value, ',');
        return true;
    }
    if (key == "expect.phase_publisher_reuse_ledger_summary") {
        scenario->expectations.phasePublisherReuseLedgerSummary = value;
        return true;
    }
    if (key == "expect.phase_publisher_plan_context_summary") {
        scenario->expectations.phasePublisherPlanContextSummary = value;
        return true;
    }
    if (key == "expect.phase_publisher_stable_key_summary") {
        scenario->expectations.phasePublisherStableKeySummary = value;
        return true;
    }
    if (key == "expect.phase_publisher_stable_key_consumer_dry_run_summary") {
        scenario->expectations.phasePublisherStableKeyConsumerDryRunSummary =
            value;
        return true;
    }
    if (key == "expect.phase_publisher_stable_key_shadow_summary") {
        scenario->expectations.phasePublisherStableKeyShadowSummary = value;
        return true;
    }
    if (key ==
        "expect.phase_publisher_stable_key_live_consumption_readiness_summary") {
        scenario->expectations
            .phasePublisherStableKeyLiveConsumptionReadinessSummary = value;
        return true;
    }
    if (key ==
        "expect.phase_publisher_stable_key_live_consumption_summary") {
        scenario->expectations.phasePublisherStableKeyLiveConsumptionSummary =
            value;
        return true;
    }
    if (key == "expect.phase_publisher_reuse_ledger_decisions") {
        scenario->expectations.phasePublisherReuseLedgerDecisionsContains =
            Split(value, ',');
        return true;
    }
    if (key == "expect.brain_ordinary_movement_work_order") {
        scenario->expectations.brainOrdinaryMovementWorkOrder = Split(value, ',');
        return true;
    }
    if (key == "expect.brain_ordinary_movement_heavy") {
        scenario->expectations.brainOrdinaryMovementHeavyFlags = Split(value, ',');
        return true;
    }
    if (key == "expect.brain_display_intent_rows") {
        scenario->expectations.brainDisplayIntentRows = Split(value, ',');
        return true;
    }
    if (key == "expect.brain_display_intent_decision_summary") {
        scenario->expectations.brainDisplayIntentDecisionSummary = value;
        return true;
    }
    if (key == "expect.brain_display_intent_fail_soft_summary") {
        scenario->expectations.brainDisplayIntentFailSoftSummary = value;
        return true;
    }
    if (key == "expect.brain_display_intent_decisions_contains") {
        scenario->expectations.brainDisplayIntentDecisionsContains =
            Split(value, ',');
        return true;
    }
    if (key == "expect.brain_display_overlay_cap_summary") {
        scenario->expectations.brainDisplayOverlayCapSummary = value;
        return true;
    }
    if (key == "expect.brain_display_overlay_cap_decisions_contains") {
        scenario->expectations.brainDisplayOverlayCapDecisionsContains =
            Split(value, ',');
        return true;
    }
    if (key == "expect.brain_display_source_link_summary") {
        scenario->expectations.brainDisplaySourceLinkSummary = value;
        return true;
    }
    if (key == "expect.brain_display_stable_key_audit_summary") {
        scenario->expectations.brainDisplayStableKeyAuditSummary = value;
        return true;
    }
    if (key == "expect.brain_display_stable_key_audit_decisions_contains") {
        scenario->expectations.brainDisplayStableKeyAuditDecisionsContains =
            Split(value, ',');
        return true;
    }
    if (key == "expect.brain_display_source_owned_stable_key_summary") {
        scenario->expectations.brainDisplaySourceOwnedStableKeySummary =
            value;
        return true;
    }
    if (key == "expect.brain_display_stable_key_consumer_dry_run_summary") {
        scenario->expectations.brainDisplayStableKeyConsumerDryRunSummary =
            value;
        return true;
    }
    if (key ==
        "expect.brain_display_stable_key_consumer_dry_run_decisions_contains") {
        scenario->expectations
            .brainDisplayStableKeyConsumerDryRunDecisionsContains =
            Split(value, ',');
        return true;
    }
    if (key == "expect.brain_display_stable_key_shadow_summary") {
        scenario->expectations.brainDisplayStableKeyShadowSummary = value;
        return true;
    }
    if (key == "expect.brain_display_stable_key_shadow_decisions_contains") {
        scenario->expectations.brainDisplayStableKeyShadowDecisionsContains =
            Split(value, ',');
        return true;
    }
    if (key ==
        "expect.brain_display_stable_key_live_consumption_readiness_summary") {
        scenario->expectations
            .brainDisplayStableKeyLiveConsumptionReadinessSummary = value;
        return true;
    }
    if (key ==
        "expect.brain_display_stable_key_live_consumption_readiness_decisions_contains") {
        scenario->expectations
            .brainDisplayStableKeyLiveConsumptionReadinessDecisionsContains =
            Split(value, ',');
        return true;
    }
    if (key ==
        "expect.brain_display_stable_key_live_consumption_summary") {
        scenario->expectations.brainDisplayStableKeyLiveConsumptionSummary =
            value;
        return true;
    }
    if (key ==
        "expect.brain_display_stable_key_live_consumption_decisions_contains") {
        scenario->expectations
            .brainDisplayStableKeyLiveConsumptionDecisionsContains =
            Split(value, ',');
        return true;
    }
    if (key ==
        "expect.brain_display_upstream_stable_key_source_audit_summary") {
        scenario->expectations
            .brainDisplayUpstreamStableKeySourceAuditSummary = value;
        return true;
    }
    if (key ==
        "expect.brain_display_upstream_stable_key_source_audit_decisions_contains") {
        scenario->expectations
            .brainDisplayUpstreamStableKeySourceAuditDecisionsContains =
            Split(value, ',');
        return true;
    }
    if (key == "expect.ctaf_unicom_evidence_summary") {
        scenario->expectations.ctafUnicomEvidenceSummary = value;
        return true;
    }
    if (key == "expect.ctaf_unicom_source_evidence") {
        scenario->expectations.ctafUnicomSourceEvidence = Split(value, ',');
        return true;
    }
    if (key == "expect.ctaf_unicom_projection_evidence") {
        scenario->expectations.ctafUnicomProjectionEvidence =
            Split(value, ',');
        return true;
    }
    if (key == "expect.ctaf_unicom_advisory_preview_summary") {
        scenario->expectations.ctafUnicomAdvisoryPreviewSummary = value;
        return true;
    }
    if (key == "expect.ctaf_unicom_advisory_preview_decisions") {
        scenario->expectations.ctafUnicomAdvisoryPreviewDecisions =
            Split(value, ',');
        return true;
    }
    if (key == "expect.ctaf_unicom_advisory_authority_summary") {
        scenario->expectations.ctafUnicomAdvisoryAuthoritySummary = value;
        return true;
    }
    if (key == "expect.ctaf_unicom_bypass_audit_summary") {
        scenario->expectations.ctafUnicomBypassAuditSummary = value;
        return true;
    }
    if (key == "expect.ctaf_unicom_bypass_audit_decisions_contains") {
        scenario->expectations.ctafUnicomBypassAuditDecisionsContains =
            Split(value, ',');
        return true;
    }
    if (key == "expect.ctaf_unicom_missing_evidence_audit_summary") {
        scenario->expectations.ctafUnicomMissingEvidenceAuditSummary = value;
        return true;
    }
    if (key ==
        "expect.ctaf_unicom_missing_evidence_audit_decisions_contains") {
        scenario->expectations
            .ctafUnicomMissingEvidenceAuditDecisionsContains =
            Split(value, ',');
        return true;
    }
    if (key == "expect.ctaf_unicom_legacy_bypass_alias_audit_summary") {
        scenario->expectations.ctafUnicomLegacyBypassAliasAuditSummary =
            value;
        return true;
    }
    if (key ==
        "expect.ctaf_unicom_legacy_bypass_alias_audit_decisions_contains") {
        scenario->expectations
            .ctafUnicomLegacyBypassAliasAuditDecisionsContains =
            Split(value, ',');
        return true;
    }
    if (key ==
        "expect.ctaf_unicom_public_unknown_alias_consumer_audit_summary") {
        scenario->expectations
            .ctafUnicomPublicUnknownAliasConsumerAuditSummary = value;
        return true;
    }
    if (key ==
        "expect.ctaf_unicom_public_unknown_alias_consumer_audit_decisions_contains") {
        scenario->expectations
            .ctafUnicomPublicUnknownAliasConsumerAuditDecisionsContains =
            Split(value, ',');
        return true;
    }
    if (key == "expect.ctaf_unicom_external_alias_deprecation_summary") {
        scenario->expectations.ctafUnicomExternalAliasDeprecationSummary =
            value;
        return true;
    }
    if (key ==
        "expect.ctaf_unicom_external_alias_deprecation_decisions_contains") {
        scenario->expectations
            .ctafUnicomExternalAliasDeprecationDecisionsContains =
            Split(value, ',');
        return true;
    }
    if (key ==
        "expect.ctaf_unicom_public_header_alias_risk_closure_summary") {
        scenario->expectations
            .ctafUnicomPublicHeaderAliasRiskClosureSummary = value;
        return true;
    }
    if (key ==
        "expect.ctaf_unicom_public_header_alias_risk_closure_decisions_contains") {
        scenario->expectations
            .ctafUnicomPublicHeaderAliasRiskClosureDecisionsContains =
            Split(value, ',');
        return true;
    }
    if (key == "expect.ctaf_unicom_publisher_rows") {
        scenario->expectations.ctafUnicomPublisherRows = Split(value, ',');
        return true;
    }
    if (key ==
        "expect.ctaf_unicom_publisher_stable_key_shadow_summary") {
        scenario->expectations.ctafUnicomPublisherStableKeyShadowSummary =
            value;
        return true;
    }
    if (key ==
        "expect.ctaf_unicom_publisher_stable_key_live_consumption_readiness_summary") {
        scenario->expectations
            .ctafUnicomPublisherStableKeyLiveConsumptionReadinessSummary =
            value;
        return true;
    }
    if (key ==
        "expect.ctaf_unicom_publisher_stable_key_live_consumption_summary") {
        scenario->expectations
            .ctafUnicomPublisherStableKeyLiveConsumptionSummary = value;
        return true;
    }
    if (key ==
        "expect.ctaf_unicom_publisher_stable_key_live_consumption_decisions_contains") {
        scenario->expectations
            .ctafUnicomPublisherStableKeyLiveConsumptionDecisionsContains =
            Split(value, ',');
        return true;
    }
    if (key ==
        "expect.ctaf_unicom_publisher_phase_stable_key_shadow_summary") {
        scenario->expectations
            .ctafUnicomPublisherPhaseStableKeyShadowSummary = value;
        return true;
    }
    if (key ==
        "expect.ctaf_unicom_publisher_phase_stable_key_live_consumption_readiness_summary") {
        scenario->expectations
            .ctafUnicomPublisherPhaseStableKeyLiveConsumptionReadinessSummary =
            value;
        return true;
    }
    if (key ==
        "expect.ctaf_unicom_publisher_phase_stable_key_live_consumption_summary") {
        scenario->expectations
            .ctafUnicomPublisherPhaseStableKeyLiveConsumptionSummary = value;
        return true;
    }
    if (key ==
        "expect.ctaf_unicom_publisher_phase_reuse_ledger_decisions_contains") {
        scenario->expectations
            .ctafUnicomPublisherPhaseReuseLedgerDecisionsContains =
            Split(value, ',');
        return true;
    }
    if (key == "expect.standby_assist_summary") {
        scenario->expectations.standbyAssistSummary = value;
        return true;
    }
    if (key == "expect.standby_assist_decisions_contains") {
        scenario->expectations.standbyAssistDecisionsContains =
            Split(value, ',');
        return true;
    }
    if (key == "expect.standby_assist_settings_diagnostics") {
        scenario->expectations.standbyAssistSettingsDiagnostics = value;
        return true;
    }
    if (key == "expect.standby_assist_side_effect_summary") {
        scenario->expectations.standbyAssistSideEffectSummary = value;
        return true;
    }
    if (key == "expect.standby_assist_side_effect_actual_summary") {
        scenario->expectations.standbyAssistSideEffectActualSummary = value;
        return true;
    }
    if (key == "expect.standby_assist_writer_result_summary") {
        scenario->expectations.standbyAssistWriterResultSummary = value;
        return true;
    }
    if (key == "expect.standby_assist_writer_result_contains") {
        scenario->expectations.standbyAssistWriterResultContains =
            Split(value, ',');
        return true;
    }
    if (key == "expect.standby_assist_writer_counter_summary") {
        scenario->expectations.standbyAssistWriterCounterSummary = value;
        return true;
    }
    if (key == "standby_assist.apply") {
        return ParseBool(value, &scenario->applyStandbyAssist);
    }
    if (key == "standby_assist.stage") {
        scenario->standbyAssistWorkflowStage = ParseWorkflowStage(value);
        return scenario->standbyAssistWorkflowStage.has_value();
    }
    if (key == "standby_assist.plan_key") {
        scenario->standbyAssistPlanKey = value;
        return true;
    }
    if (key == "standby_assist.loaded") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->standbyAssistLoaded = parsed;
        return true;
    }
    if (key == "standby_assist.side_effect") {
        return ParseBool(value, &scenario->standbyAssistSideEffect);
    }
    if (key == "standby_assist.enabled") {
        return ParseBool(value, &scenario->standbyAssistEnabled);
    }
    if (key == "standby_assist.direct_ctaf_enabled" ||
        key == "standby_assist.direct_ctaf_standby_assist_enabled") {
        const auto parsed =
            ParseBool(value, &scenario->standbyAssistDirectCtafEnabled);
        if (parsed) {
            scenario->standbyAssistDirectCtafGateSource = "harness";
        }
        return parsed;
    }
    if (key == "standby_assist.direct_ctaf_gate_source") {
        scenario->standbyAssistDirectCtafGateSource = value;
        return true;
    }
    if (key == "stable_key.source_owned_fallback_shadow" ||
        key == "source_owned_fallback_stable_key_shadow_enabled") {
        if (!ParseBool(value,
                       &scenario
                            ->sourceOwnedFallbackStableKeyShadowEnabled)) {
            return false;
        }
        if (scenario->sourceOwnedFallbackStableKeyShadowEnabled &&
            scenario->sourceOwnedFallbackStableKeyShadowGateSource ==
                "default") {
            scenario->sourceOwnedFallbackStableKeyShadowGateSource =
                "harness";
        }
        return true;
    }
    if (key == "stable_key.source_owned_fallback_shadow_source" ||
        key == "source_owned_fallback_stable_key_shadow_gate_source") {
        scenario->sourceOwnedFallbackStableKeyShadowGateSource = value;
        return true;
    }
    if (key == "stable_key.source_owned_fallback_live_consumption_proposal" ||
        key ==
            "source_owned_fallback_stable_key_live_consumption_proposal_enabled") {
        if (!ParseBool(
                value,
                &scenario
                     ->sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled)) {
            return false;
        }
        if (scenario
                ->sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled &&
            scenario
                    ->sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource ==
                "default") {
            scenario
                ->sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource =
                "harness";
        }
        return true;
    }
    if (key ==
            "stable_key.source_owned_fallback_live_consumption_proposal_source" ||
        key ==
            "source_owned_fallback_stable_key_live_consumption_proposal_gate_source") {
        scenario
            ->sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource =
            value;
        return true;
    }
    if (key == "stable_key.source_owned_fallback_live_consumption" ||
        key ==
            "source_owned_fallback_stable_key_live_consumption_enabled") {
        if (!ParseBool(
                value,
                &scenario
                     ->sourceOwnedFallbackStableKeyLiveConsumptionEnabled)) {
            return false;
        }
        if (scenario->sourceOwnedFallbackStableKeyLiveConsumptionEnabled &&
            scenario->sourceOwnedFallbackStableKeyLiveConsumptionGateSource ==
                "default") {
            scenario->sourceOwnedFallbackStableKeyLiveConsumptionGateSource =
                "harness";
        }
        return true;
    }
    if (key ==
            "stable_key.source_owned_fallback_live_consumption_source" ||
        key ==
            "source_owned_fallback_stable_key_live_consumption_gate_source") {
        scenario->sourceOwnedFallbackStableKeyLiveConsumptionGateSource =
            value;
        return true;
    }
    if (key ==
            "settings.source_owned_fallback_stable_key_live_consumption" ||
        key ==
            "settings.source_owned_fallback_stable_key_live_consumption_enabled") {
        if (!ParseBool(
                value,
                &scenario
                     ->settingsSourceOwnedFallbackStableKeyLiveConsumptionEnabled)) {
            return false;
        }
        scenario
            ->settingsSourceOwnedFallbackStableKeyLiveConsumptionLoaded =
            true;
        if (!scenario
                 ->settingsSourceOwnedFallbackStableKeyLiveConsumptionSourceLoaded) {
            scenario
                ->settingsSourceOwnedFallbackStableKeyLiveConsumptionGateSource =
                "settings-store";
        }
        scenario->ctafUnicomPublisherProbe = true;
        return true;
    }
    if (key ==
            "settings.source_owned_fallback_stable_key_live_consumption_source" ||
        key ==
            "settings.source_owned_fallback_stable_key_live_consumption_gate_source") {
        scenario
            ->settingsSourceOwnedFallbackStableKeyLiveConsumptionSourceLoaded =
            true;
        scenario
            ->settingsSourceOwnedFallbackStableKeyLiveConsumptionGateSource =
            NormalizeSourceOwnedLiveConsumptionSettingsSourceForHarness(value);
        scenario->ctafUnicomPublisherProbe = true;
        return true;
    }
    if (key == "standby_assist.use_display_board_with_ctaf_advisories") {
        return ParseBool(
            value,
            &scenario->standbyAssistUseDisplayBoardWithCtafAdvisories);
    }
    if (key == "standby_assist.write_succeeded") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        scenario->standbyAssistWriteSucceeded = parsed;
        return true;
    }
    if (key == "standby_assist.writer_result_code") {
        scenario->standbyAssistWriterResultCode = value;
        return true;
    }

    return false;
}

bool AddStation(ModuleBoardSnapshot* board, const std::string& value) {
    if (board == nullptr) {
        return false;
    }

    BoardStationSnapshot station;
    bool hasRole = false;
    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "role") {
            const auto parsedRole = ParseStationRole(fieldValue);
            if (!parsedRole.has_value()) {
                return false;
            }
            station.role = *parsedRole;
            hasRole = true;
        } else if (field == "callsign") {
            station.callsign = fieldValue;
        } else if (field == "frequency") {
            station.frequency = fieldValue;
        } else if (field == "sourceEvidenceId") {
            station.sourceEvidenceId = fieldValue;
        } else if (field == "sourceEvidenceType") {
            station.sourceEvidenceType = fieldValue;
        } else if (field == "sourceEvidenceDomain") {
            station.sourceEvidenceDomain = fieldValue;
        } else if (field == "sourceDecisionId") {
            station.sourceDecisionId = fieldValue;
        } else if (field == "sourceEvidenceLinkStatus") {
            station.sourceEvidenceLinkStatus = fieldValue;
        } else if (field == "sourceEvidenceMissingReason") {
            station.sourceEvidenceMissingReason = fieldValue;
        } else if (field == "stableCompletionKey") {
            station.stableCompletionKey = fieldValue;
        } else if (field == "annotation") {
            // Legacy scenarios may still specify raw-board display text.
            // Raw module boards no longer own annotations.
        } else if (field == "polygonKey") {
            station.polygonKey = fieldValue;
        } else if (field == "displayRelation") {
            const auto parsedRelation = ParseDisplayRelation(fieldValue);
            if (!parsedRelation.has_value()) {
                return false;
            }
            // Legacy scenarios may still specify raw-board display relation.
            // Display Intent now infers relation from fact fields.
        } else if (field == "tuned") {
            if (!ParseBool(fieldValue, &station.tuned)) {
                return false;
            }
        } else if (field == "next") {
            bool ignoredDisplayFlag = false;
            if (!ParseBool(fieldValue, &ignoredDisplayFlag)) {
                return false;
            }
        } else if (field == "standby") {
            bool ignoredDisplayFlag = false;
            if (!ParseBool(fieldValue, &ignoredDisplayFlag)) {
                return false;
            }
        } else if (field == "sectorActive") {
            if (!ParseBool(fieldValue, &station.sectorActive)) {
                return false;
            }
        } else if (field == "online") {
            if (!ParseBool(fieldValue, &station.online)) {
                return false;
            }
        } else if (field == "offline") {
            if (!ParseBool(fieldValue, &station.offline)) {
                return false;
            }
        } else if (field == "routeEntryDistanceNm") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            station.hasRouteEntryDistance = true;
            station.routeEntryDistanceNm = *parsed;
        }
    }

    if (!hasRole || station.callsign.empty()) {
        return false;
    }

    board->stations.push_back(station);
    board->available = true;
    return true;
}

bool AddDisplayRelationFact(
    std::vector<xvatsim::brain::BrainDisplayRelationFact>* facts,
    const std::string& value) {
    if (facts == nullptr) {
        return false;
    }

    xvatsim::brain::BrainDisplayRelationFact fact;
    bool hasRelation = false;
    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "callsign") {
            fact.callsign = fieldValue;
        } else if (field == "frequency") {
            fact.frequency = fieldValue;
        } else if (field == "relation" || field == "displayRelation") {
            const auto parsedRelation = ParseDisplayRelation(fieldValue);
            if (!parsedRelation.has_value()) {
                return false;
            }
            fact.displayRelation = *parsedRelation;
            hasRelation = true;
        } else if (field == "routeEntryDistanceNm") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            fact.hasRouteEntryDistance = true;
            fact.routeEntryDistanceNm = *parsed;
        }
    }

    if (fact.callsign.empty() || fact.frequency.empty() || !hasRelation) {
        return false;
    }

    facts->push_back(std::move(fact));
    return true;
}

bool ParseCtafLookupFact(
    const std::string& value,
    xvatsim::brain::BrainOwnedCtafLookupFact* fact) {
    if (fact == nullptr) {
        return false;
    }

    *fact = {};
    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "airport" || field == "airportIcao") {
            fact->airportIcao = fieldValue;
        } else if (field == "attempted" || field == "lookupAttempted") {
            if (!ParseBool(fieldValue, &fact->lookupAttempted)) {
                return false;
            }
        } else if (field == "skipped" ||
                   field == "lookupSkippedReason") {
            fact->lookupSkippedReason = fieldValue;
        } else if (field == "cacheHit") {
            if (!ParseBool(fieldValue, &fact->cacheHit)) {
                return false;
            }
        } else if (field == "fetchInProgress") {
            if (!ParseBool(fieldValue, &fact->fetchInProgress)) {
                return false;
            }
        } else if (field == "requestSucceeded") {
            if (!ParseBool(fieldValue, &fact->requestSucceeded)) {
                return false;
            }
        } else if (field == "status" ||
                   field == "statusCodeClass") {
            fact->statusCodeClass = fieldValue;
        } else if (field == "resolved") {
            if (!ParseBool(fieldValue, &fact->resolved)) {
                return false;
            }
        } else if (field == "available") {
            if (!ParseBool(fieldValue, &fact->available)) {
                return false;
            }
        } else if (field == "frequency") {
            fact->frequency = fieldValue;
        } else if (field == "age" ||
                   field == "lastAttemptAgeSeconds") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            fact->lastAttemptAgeSeconds =
                static_cast<long long>(*parsed);
        } else if (field == "failures" || field == "failureCount") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            fact->failureCount = static_cast<int>(*parsed);
        } else if (field == "pending" || field == "pendingReason") {
            fact->pendingReason = fieldValue;
        }
    }

    return true;
}

bool AddRouteSector(
    std::vector<xvatsim::brain::RouteSectorMatchSnapshot>* sectors,
    const std::string& value) {
    if (sectors == nullptr) {
        return false;
    }

    xvatsim::brain::RouteSectorMatchSnapshot sector;
    bool hasIdentifier = false;
    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "identifier") {
            sector.identifier = fieldValue;
            hasIdentifier = true;
        } else if (field == "entryDistanceNm") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            sector.entryDistanceNm = *parsed;
        } else if (field == "matchTokens") {
            sector.matchTokens = Split(fieldValue, ',');
        } else if (field == "controllerPatterns" ||
                   field == "controllerCallsignPatterns") {
            sector.controllerCallsignPatterns = Split(fieldValue, ',');
        } else if (field == "controllerPrefixes") {
            sector.controllerPrefixes = Split(fieldValue, ',');
        } else if (field == "centerCoverage") {
            if (!ParseBool(fieldValue, &sector.centerCoverage)) {
                return false;
            }
        } else if (field == "terminalCoverage") {
            if (!ParseBool(fieldValue, &sector.terminalCoverage)) {
                return false;
            }
        } else if (field == "coverage") {
            const auto normalizedCoverage = ToUpperCopy(Trim(fieldValue));
            if (normalizedCoverage == "CENTER") {
                sector.centerCoverage = true;
            } else if (normalizedCoverage == "TERMINAL") {
                sector.terminalCoverage = true;
            } else {
                return false;
            }
        }
    }

    if (!hasIdentifier) {
        return false;
    }

    sectors->push_back(std::move(sector));
    return true;
}

bool AddController(
    std::vector<xvatsim::brain::ControllerSnapshot>* controllers,
    const std::string& value) {
    if (controllers == nullptr) {
        return false;
    }

    xvatsim::brain::ControllerSnapshot controller;
    bool hasCallsign = false;
    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "callsign") {
            controller.callsign = fieldValue;
            hasCallsign = true;
        } else if (field == "frequency") {
            controller.frequency = fieldValue;
        } else if (field == "facility") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            controller.facility = static_cast<int>(*parsed);
        } else if (field == "actionable") {
            if (!ParseBool(fieldValue, &controller.actionable)) {
                return false;
            }
        } else if (field == "atis") {
            if (!ParseBool(fieldValue, &controller.atis)) {
                return false;
            }
        } else if (field == "text_atis" ||
                   field == "textAtis" ||
                   field == "atis_text") {
            controller.textAtis = fieldValue;
        }
    }

    if (!hasCallsign) {
        return false;
    }

    controllers->push_back(std::move(controller));
    return true;
}

bool AddTransceiverCandidate(
    xvatsim::brain::TransceiverResolutionSnapshot* snapshot,
    const std::string& value) {
    if (snapshot == nullptr) {
        return false;
    }

    xvatsim::brain::ReceivableControllerSnapshot candidate;
    bool hasCallsign = false;
    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "callsign") {
            candidate.callsign = fieldValue;
            hasCallsign = true;
        } else if (field == "frequency") {
            candidate.frequency = fieldValue;
        } else if (field == "distanceNm") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            candidate.distanceNm = *parsed;
        } else if (field == "score") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            candidate.score = *parsed;
        } else if (field == "lat") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            candidate.latitudeDeg = *parsed;
        } else if (field == "lon") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            candidate.longitudeDeg = *parsed;
        }
    }

    if (!hasCallsign) {
        return false;
    }

    snapshot->candidates.push_back(std::move(candidate));
    snapshot->receivableControllers = static_cast<int>(snapshot->candidates.size());
    return true;
}

std::vector<xvatsim::modules::transceiver_resolver::CachedTransceiver>
BuildCachedTransceiversForResolverProbe(
    const xvatsim::brain::TransceiverResolutionSnapshot& snapshot) {
    std::vector<xvatsim::modules::transceiver_resolver::CachedTransceiver>
        transceivers;
    transceivers.reserve(snapshot.candidates.size());
    for (const auto& candidate : snapshot.candidates) {
        xvatsim::modules::transceiver_resolver::CachedTransceiver transceiver;
        transceiver.callsign = candidate.callsign;
        transceiver.frequency = candidate.frequency;
        transceiver.latitudeDeg = candidate.latitudeDeg;
        transceiver.longitudeDeg = candidate.longitudeDeg;
        transceiver.heightAglFt = 0.0;
        transceivers.push_back(std::move(transceiver));
    }
    return transceivers;
}

std::vector<std::string> TransceiverResolverCandidateSummaries(
    const xvatsim::brain::TransceiverResolutionSnapshot& snapshot) {
    std::vector<std::string> summaries;
    summaries.reserve(snapshot.candidates.size());
    for (const auto& candidate : snapshot.candidates) {
        summaries.push_back(candidate.callsign + "@" + candidate.frequency);
    }
    return summaries;
}

std::string BoolDigit(bool value) {
    return value ? "1" : "0";
}

std::string EmptyToken(const std::string& value) {
    return value.empty() ? std::string("<empty>") : value;
}

std::string NoneToken(const std::string& value) {
    return value.empty() ? std::string("<none>") : value;
}

std::string CtafUnicomEvidenceSummaryText(
    const xvatsim::brain::BrainOwnedPublisherOutput& output) {
    const auto& summary = output.ctafUnicomEvidenceSummary;
    std::ostringstream stream;
    stream << "source=" << summary.sourceEvidenceCount
           << ",projection=" << summary.projectionEvidenceCount
           << ",legacyDiagnosticLiveRows="
           << summary.legacyDiagnosticLiveRowEmittedCount
           << ",diagnosticCompatibilityProjectionOnly="
           << summary.diagnosticCompatibilityProjectionOnly
           << ",legacyDiagnosticBypassFlag="
           << summary.completionBypassCompatibilityOnly
           << ",historicalCompatibilityRows="
           << summary.historicalCompatibilityRowCount
           << ",compatibilityRowsDiagnosticOnly="
           << BoolDigit(summary.compatibilityRowsDiagnosticOnly)
           << ",legacyBypassFieldsQuarantined="
           << BoolDigit(summary.legacyBypassFieldsQuarantined)
           << ",advisory=" << summary.advisoryDecisionCount;
    return stream.str();
}

std::vector<std::string> CtafUnicomSourceEvidenceSummaries(
    const xvatsim::brain::BrainOwnedPublisherOutput& output) {
    std::vector<std::string> rows;
    rows.reserve(output.ctafUnicomSourceEvidence.size());
    for (const auto& evidence : output.ctafUnicomSourceEvidence) {
        std::ostringstream stream;
        stream << evidence.endpoint
               << ":id=" << evidence.evidenceId
               << ":airport=" << NoneToken(evidence.airportIcao)
               << ":attempted=" << BoolDigit(evidence.lookupAttempted)
               << ":skipped=" << NoneToken(evidence.lookupSkippedReason)
               << ":cacheHit=" << BoolDigit(evidence.cacheHit)
               << ":fetch=" << BoolDigit(evidence.fetchInProgress)
               << ":request=" << BoolDigit(evidence.requestSucceeded)
               << ":status=" << NoneToken(evidence.statusCodeClass)
               << ":resolved=" << BoolDigit(evidence.resolved)
               << ":available=" << BoolDigit(evidence.available)
               << ":freq=" << EmptyToken(evidence.frequency)
               << ":age=" << evidence.lastAttemptAgeSeconds
               << ":failures=" << evidence.failureCount
               << ":fallbackEligible=" << BoolDigit(evidence.fallbackEligible)
               << ":fallbackFreq=" << EmptyToken(evidence.fallbackFrequency)
               << ":confidence=" << NoneToken(evidence.sourceConfidence)
               << ":reason=" << NoneToken(evidence.sourceReason)
               << ":pending=" << NoneToken(evidence.pendingReason);
        rows.push_back(stream.str());
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

std::vector<std::string> CtafUnicomProjectionEvidenceSummaries(
    const xvatsim::brain::BrainOwnedPublisherOutput& output) {
    std::vector<std::string> rows;
    rows.reserve(output.ctafUnicomProjectionEvidence.size());
    for (const auto& evidence : output.ctafUnicomProjectionEvidence) {
        std::ostringstream stream;
        stream << evidence.endpoint
               << ":source=" << NoneToken(evidence.sourceEvidenceId)
               << ":role=" << evidence.projectedRole
               << ":freq=" << EmptyToken(evidence.projectedFrequency)
               << ":fallback=" << BoolDigit(evidence.fallbackUsed)
               << ":empty="
               << BoolDigit(evidence.unresolvedProjectedEmptyFrequency)
               << ":legacyRemoved=" << evidence.legacyRowRemovedCount
               << ":duplicates=" << evidence.duplicateSuppressedCount
               << ":diagnosticCompatibilityProjectionOnly="
               << BoolDigit(evidence.diagnosticCompatibilityProjectionOnly)
               << ":legacyDiagnosticBypass="
               << BoolDigit(evidence.completionBypassCompatibilityOnly)
               << ":bypassRetired="
               << BoolDigit(evidence.completionBypassRetired)
               << ":bypassLiveAuthority="
               << BoolDigit(evidence.completionBypassLiveAuthority)
               << ":bypassDiagnosticOnly="
               << BoolDigit(evidence.completionBypassDiagnosticOnly)
               << ":legacyDiagnosticLiveRowEmitted="
               << BoolDigit(evidence.legacyDiagnosticLiveRowEmitted);
        rows.push_back(stream.str());
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

std::string FormatCtafAdvisoryScore(double score) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << score;
    return stream.str();
}

std::string CtafUnicomAdvisoryPreviewSummaryText(
    const xvatsim::brain::BrainOwnedPublisherOutput& output) {
    const auto& summary = output.ctafUnicomAdvisoryPreviewSummary;
    std::ostringstream stream;
    stream << "source=" << summary.sourceEvidenceCount
           << ",projection=" << summary.projectionEvidenceCount
           << ",preview=" << summary.advisoryPreviewDecisionCount
           << ",wouldEmit=" << summary.previewWouldEmitLiveRowCount
           << ",matches=" << summary.previewMatchesCurrentProjectionCount
           << ",mismatch=" << summary.previewMismatchCount
           << ",diagnosticCompatibilityProjectionOnly="
           << summary.diagnosticCompatibilityProjectionOnly
           << ",legacyDiagnosticBypassFlag="
           << summary.completionBypassCompatibilityOnly;
    return stream.str();
}

std::vector<std::string> CtafUnicomAdvisoryPreviewDecisionSummaries(
    const xvatsim::brain::BrainOwnedPublisherOutput& output) {
    std::vector<std::string> rows;
    rows.reserve(output.ctafUnicomAdvisoryPreviewDecisions.size());
    for (const auto& decision :
         output.ctafUnicomAdvisoryPreviewDecisions) {
        std::ostringstream stream;
        stream << decision.endpoint
               << ":id=" << decision.advisoryDecisionId
               << ":source=" << NoneToken(decision.sourceEvidenceId)
               << ":airport=" << NoneToken(decision.airportIcao)
               << ":decision=" << decision.decision
               << ":role=" << decision.projectedRole
               << ":freq=" << EmptyToken(decision.projectedFrequency)
               << ":fallback=" << BoolDigit(decision.fallbackUsed)
               << ":sourceConfidence="
               << NoneToken(decision.sourceConfidence)
               << ":confidence=" << NoneToken(decision.confidenceLevel)
               << ":score="
               << FormatCtafAdvisoryScore(decision.positiveScore)
               << "/"
               << FormatCtafAdvisoryScore(decision.negativeScore)
               << ":hardBlock=" << BoolDigit(decision.hardBlock)
               << ":reason=" << NoneToken(decision.reason)
               << ":wouldEmit=" << BoolDigit(decision.wouldEmitLiveRow)
               << ":matches="
               << BoolDigit(decision.matchesCurrentProjection);
        rows.push_back(stream.str());
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

std::string CtafUnicomAdvisoryAuthoritySummaryText(
    const xvatsim::brain::BrainOwnedPublisherOutput& output) {
    const auto& summary = output.ctafUnicomAdvisoryAuthoritySummary;
    std::ostringstream stream;
    stream << "authority=" << NoneToken(summary.advisoryAuthority)
           << ",source=" << summary.sourceEvidenceCount
           << ",preview=" << summary.advisoryPreviewDecisionCount
           << ",live=" << summary.liveAdvisoryRowCount
           << ",historicalCompatibilityRows="
           << summary.compatibilityProjectionCount
           << ",mismatch=" << summary.oldVsBrainMismatchCount
           << ",diagnosticCompatibilityProjectionOnly="
           << summary.diagnosticCompatibilityProjectionOnly
           << ",legacyDiagnosticBypassFlag="
           << summary.completionBypassCompatibilityOnly
           << ",brainOwned=" << BoolDigit(summary.liveRowsBrainOwned)
           << ",bypassRetired="
           << BoolDigit(summary.completionBypassRetired)
           << ",liveBypassAuthority=" << summary.liveBypassAuthorityCount
           << ",diagnosticBypassRows=" << summary.diagnosticBypassRowCount
           << ",brainAdvisoryLiveRows="
           << summary.brainAdvisoryLiveRowCount
           << ",duplicateLive=" << summary.duplicateLiveRowCount
           << ",retirementSafe="
           << BoolDigit(summary.bypassRetirementSafe)
           << ",noLiveBypassAuthority="
           << BoolDigit(summary.noLiveBypassAuthority)
           << ",compatibilityRowsDiagnosticOnly="
           << BoolDigit(summary.compatibilityRowsDiagnosticOnly)
           << ",liveRowsBrainAdvisoryOwned="
           << BoolDigit(summary.liveRowsBrainAdvisoryOwned)
           << ",legacyBypassFieldsQuarantined="
           << BoolDigit(summary.legacyBypassFieldsQuarantined);
    return stream.str();
}

std::string CtafUnicomBypassAuditSummaryText(
    const xvatsim::brain::BrainOwnedPublisherOutput& output) {
    const auto& summary = output.ctafUnicomBypassAuditSummary;
    std::ostringstream stream;
    stream << "audit=" << summary.bypassAuditDecisionCount
           << ",legacyDiagnosticBypassRows=" << summary.bypassRowCount
           << ",brainRows=" << summary.brainOwnedAdvisoryRowCount
           << ",matching=" << summary.matchingBrainEquivalentCount
           << ",missing=" << summary.missingBrainEquivalentCount
           << ",mismatch=" << summary.mismatchCount
           << ",safe=" << summary.safeToRetireCount
           << ",blocked=" << summary.blockedRetirementCount
           << ",pending=" << summary.pendingLookupCount
           << ",failed=" << summary.lookupFailedCount
           << ",empty=" << summary.emptyFrequencyCount
           << ",unicom=" << summary.unicomFallbackCount
           << ",standbyAdvisory="
           << summary.standbyAdvisoryConsumerCount
           << ",standbyBypass=" << summary.standbyBypassConsumerCount
           << ",policy=" << summary.retirementPolicyDecisionCount
           << ",resolvedBlockers=" << summary.resolvedBlockerCount
           << ",stillBlocked=" << summary.stillBlockedCount
           << ",policyNonDisplayable="
           << summary.policyNonDisplayableCount
           << ",policyDeferred=" << summary.policyDeferredCount
           << ",policyFailed=" << summary.policyFailedLookupCount
           << ",policyEmpty=" << summary.policyEmptyFrequencyCount
           << ",duplicateSuppressed="
           << summary.duplicateSuppressedCount
           << ",missingEvidencePolicy="
           << summary.missingEvidencePolicyCount
           << ",wouldLoseFrequency="
           << summary.wouldLoseFrequencyCount
           << ",wouldLoseVisibility="
           << summary.wouldLoseVisibilityCount
           << ",bypassSafe="
           << summary.bypassRemovalSafeCandidateCount
           << ",bypassUnsafe="
           << summary.bypassRemovalStillUnsafeCount
           << ",bypassRetired="
           << BoolDigit(summary.completionBypassRetired)
           << ",liveBypassAuthority="
           << summary.liveBypassAuthorityCount
           << ",diagnosticBypassRows="
           << summary.diagnosticBypassRowCount
           << ",brainAdvisoryLive="
           << summary.brainAdvisoryLiveRowCount
           << ",missingEvidenceWarningCount="
           << summary.missingEvidenceWarningCount
           << ",fallbackWarnings="
           << summary.compatibilityFallbackWarningCount
           << ",missingEvidenceWarnings="
           << summary.missingEvidenceFallbackWarningCount
           << ",duplicateLiveRows="
           << summary.duplicateLiveRowCount
           << ",pendingNonDisplayable="
           << summary.pendingNonDisplayableCount
           << ",failedNonDisplayable="
           << summary.failedLookupNonDisplayableCount
           << ",emptyNonDisplayable="
           << summary.emptyFrequencyNonDisplayableCount
           << ",retiredCompatibilityRows="
           << summary.retiredBypassCompatibilityRowCount
           << ",retirementSafe="
           << BoolDigit(summary.bypassRetirementSafe)
           << ",noLiveBypassAuthority="
           << BoolDigit(summary.noLiveBypassAuthority)
           << ",compatibilityRowsDiagnosticOnly="
           << BoolDigit(summary.compatibilityRowsDiagnosticOnly)
           << ",liveRowsBrainAdvisoryOwned="
           << BoolDigit(summary.liveRowsBrainAdvisoryOwned)
           << ",standbyRowsAdvisoryOwned="
           << BoolDigit(summary.standbyRowsAdvisoryOwned)
           << ",legacyBypassFieldsQuarantined="
           << BoolDigit(summary.legacyBypassFieldsQuarantined)
           << ",diagnosticCompatibilityProjectionOnly="
           << BoolDigit(summary.diagnosticCompatibilityProjectionOnly)
           << ",legacyCompatibilityOnly="
           << BoolDigit(summary.completionBypassCompatibilityOnly)
           << ",ready="
           << BoolDigit(summary.ctafUnicomBypassRetirementReady);
    return stream.str();
}

std::vector<std::string> CtafUnicomBypassAuditDecisionSummaries(
    const xvatsim::brain::BrainOwnedPublisherOutput& output) {
    std::vector<std::string> rows;
    rows.reserve(output.ctafUnicomBypassAuditDecisions.size());
    for (const auto& decision : output.ctafUnicomBypassAuditDecisions) {
        std::ostringstream stream;
        stream << decision.endpoint
               << ":id=" << decision.ctafUnicomBypassAuditDecisionId
               << ":advisory=" << NoneToken(decision.advisoryDecisionId)
               << ":source=" << NoneToken(decision.sourceEvidenceId)
               << ":projection=" << NoneToken(decision.projectionEvidenceId)
               << ":endpoint=" << NoneToken(decision.endpoint)
               << ":airport=" << NoneToken(decision.airportIcao)
               << ":callsign=" << NoneToken(decision.callsign)
               << ":role=" << NoneToken(decision.role)
               << ":freq=" << EmptyToken(decision.frequency)
               << ":diagnosticCompatibilityWouldDisplay="
               << BoolDigit(decision.diagnosticCompatibilityWouldDisplay)
               << ":bypassRequired=" << BoolDigit(decision.bypassRequired)
               << ":diagnosticCompatibilityReason="
               << NoneToken(decision.diagnosticCompatibilityReason)
               << ":bypassReason=" << NoneToken(decision.bypassReason)
               << ":authority=" << NoneToken(decision.advisoryAuthority)
               << ":advisoryWouldEmit="
               << BoolDigit(decision.advisoryWouldEmitLiveRow)
               << ":matches=" << BoolDigit(decision.advisoryMatchesBypassRow)
               << ":roleMatches=" << BoolDigit(decision.roleMatches)
               << ":frequencyMatches="
               << BoolDigit(decision.frequencyMatches)
               << ":endpointMatches="
               << BoolDigit(decision.endpointMatches)
               << ":airportMatches=" << BoolDigit(decision.airportMatches)
               << ":visibilityMatches="
               << BoolDigit(decision.visibilityMatches)
               << ":bypassHasBrain="
               << BoolDigit(decision.bypassRowHasBrainEquivalent)
               << ":brainHasBypass="
               << BoolDigit(decision.brainRowHasBypassEquivalent)
               << ":safe=" << BoolDigit(decision.wouldRetireSafely)
               << ":blocked="
               << NoneToken(decision.retirementBlockedReason)
               << ":diagnosticCompatibilityOnly="
               << BoolDigit(decision.diagnosticCompatibilityOnly)
               << ":compatibilityOnly="
               << BoolDigit(decision.compatibilityOnly)
               << ":mismatch=" << NoneToken(decision.mismatchReason)
               << ":missingAdvisory="
               << BoolDigit(decision.missingAdvisoryDecision)
               << ":missingSource="
               << BoolDigit(decision.missingSourceEvidence)
               << ":pending=" << BoolDigit(decision.pendingLookup)
               << ":failed=" << BoolDigit(decision.lookupFailed)
               << ":empty=" << BoolDigit(decision.emptyFrequency)
               << ":unicom=" << BoolDigit(decision.unicomFallback)
               << ":standbyAdvisory="
               << BoolDigit(decision.standbyConsumesAdvisoryDecision)
               << ":standbyBypass="
               << BoolDigit(decision.standbyConsumesBypassRow)
               << ":policy=" << NoneToken(decision.retirementPolicy)
               << ":policyReason="
               << NoneToken(decision.retirementPolicyReason)
               << ":blockerClass="
               << NoneToken(decision.retirementBlockerClass)
               << ":blockerResolved="
               << BoolDigit(decision.retirementBlockerResolved)
               << ":stillBlocked="
               << BoolDigit(decision.retirementStillBlocked)
               << ":safeAfterPolicy="
               << BoolDigit(decision.retirementSafeAfterPolicy)
               << ":duplicateSuppressed="
               << BoolDigit(decision.compatibilityDuplicateSuppressed)
               << ":duplicateReason="
               << NoneToken(decision.duplicateSuppressionReason)
               << ":nonDisplayablePolicy="
               << BoolDigit(decision.nonDisplayableByPolicy)
               << ":deferredPolicy="
               << BoolDigit(decision.deferredByPolicy)
               << ":failedPolicy="
               << BoolDigit(decision.failedLookupByPolicy)
               << ":emptyPolicy="
               << BoolDigit(decision.emptyFrequencyByPolicy)
               << ":missingEvidencePolicy="
               << BoolDigit(decision.missingEvidenceByPolicy)
               << ":wouldLoseFrequency="
               << BoolDigit(decision.wouldLoseFrequencyIfBypassRemoved)
               << ":wouldLoseVisibility="
               << BoolDigit(decision.wouldLoseVisibilityIfBypassRemoved)
               << ":safeToRemoveBypass="
               << BoolDigit(decision.safeToRemoveBypassAfterCleanup)
               << ":bypassRetired="
               << BoolDigit(decision.completionBypassRetired)
               << ":bypassLiveAuthority="
               << BoolDigit(decision.completionBypassLiveAuthority)
               << ":bypassDiagnosticOnly="
               << BoolDigit(decision.completionBypassDiagnosticOnly)
               << ":retiredCompatibilityRows="
               << decision.retiredBypassCompatibilityRowCount
               << ":fallbackWarning="
               << BoolDigit(decision.bypassRetirementFallbackWarning)
               << ":missingEvidenceWarningOnly="
               << BoolDigit(decision.missingEvidenceWarningOnly)
               << ":missingEvidenceFallback="
               << BoolDigit(decision.missingEvidenceFallbackPreserved)
               << ":advisoryProjectionAuthority="
               << BoolDigit(decision.advisoryProjectionAuthority)
               << ":diagnosticLiveRowAuthority="
               << NoneToken(decision.diagnosticLiveRowAuthority)
               << ":liveRowAuthority="
               << NoneToken(decision.liveRowAuthority)
               << ":standbyAuthority="
               << NoneToken(decision.standbyAuthority)
               << ":bypassRegressionSafe="
               << BoolDigit(decision.bypassRetirementRegressionSafe);
        rows.push_back(stream.str());
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

std::string CtafUnicomMissingEvidenceAuditSummaryText(
    const xvatsim::brain::BrainOwnedPublisherOutput& output) {
    const auto& summary = output.ctafUnicomMissingEvidenceAuditSummary;
    std::ostringstream stream;
    stream << "audit=" << summary.missingEvidenceAuditCount
           << ",missingSource=" << summary.missingSourceEvidenceCount
           << ",missingAdvisory="
           << summary.missingAdvisoryDecisionCount
           << ",incompleteAdvisory="
           << summary.incompleteAdvisoryDecisionCount
           << ",oldCompatibilityWouldDisplay="
           << summary.oldCompatibilityWouldDisplayCount
           << ",wouldLoseFrequency=" << summary.wouldLoseFrequencyCount
           << ",wouldLoseVisibility=" << summary.wouldLoseVisibilityCount
           << ",warningOnly=" << summary.warningOnlyCount
           << ",liveAuthorityRestored="
           << summary.liveAuthorityRestoredCount
           << ",liveCompatibilityFallbackUsed="
           << summary.liveCompatibilityFallbackUsedCount
           << ",standbyConsumesWarning="
           << summary.standbyConsumesWarningCount
           << ",authorityInvariantPreserved="
           << summary.authorityInvariantPreservedCount
           << ",operatorActionRequired="
           << summary.operatorActionRequiredCount;
    return stream.str();
}

std::vector<std::string> CtafUnicomMissingEvidenceAuditDecisionSummaries(
    const xvatsim::brain::BrainOwnedPublisherOutput& output) {
    std::vector<std::string> rows;
    rows.reserve(output.ctafUnicomMissingEvidenceAuditDecisions.size());
    for (const auto& decision :
         output.ctafUnicomMissingEvidenceAuditDecisions) {
        std::ostringstream stream;
        stream << decision.missingEvidenceEndpoint
               << ":id=" << decision.missingEvidenceAuditDecisionId
               << ":endpoint="
               << NoneToken(decision.missingEvidenceEndpoint)
               << ":airport="
               << NoneToken(decision.missingEvidenceAirportIcao)
               << ":role=" << NoneToken(decision.missingEvidenceRole)
               << ":freq=" << EmptyToken(decision.missingEvidenceFrequency)
               << ":cause=" << NoneToken(decision.missingEvidenceCause)
               << ":missingSource="
               << BoolDigit(decision.missingSourceEvidence)
               << ":missingAdvisory="
               << BoolDigit(decision.missingAdvisoryDecision)
               << ":incompleteAdvisory="
               << BoolDigit(decision.incompleteAdvisoryDecision)
               << ":oldCompatibilityWouldDisplay="
               << BoolDigit(decision.oldCompatibilityWouldDisplay)
               << ":wouldLoseFrequency="
               << BoolDigit(decision.wouldLoseFrequency)
               << ":wouldLoseVisibility="
               << BoolDigit(decision.wouldLoseVisibility)
               << ":warningOnly=" << BoolDigit(decision.warningOnly)
               << ":warningLabel="
               << NoneToken(decision.warningLabel)
               << ":warningReason="
               << NoneToken(decision.warningReason)
               << ":recoveryHint="
               << NoneToken(decision.recoveryHint)
               << ":liveAuthorityRestored="
               << BoolDigit(decision.liveAuthorityRestored)
               << ":liveCompatibilityFallbackUsed="
               << BoolDigit(decision.liveCompatibilityFallbackUsed)
               << ":standbyConsumesWarning="
               << BoolDigit(decision.standbyConsumesWarning)
               << ":standbyWriteBlockedByMissingEvidence="
               << BoolDigit(decision.standbyWriteBlockedByMissingEvidence)
               << ":authorityInvariantPreserved="
               << BoolDigit(decision.authorityInvariantPreserved)
               << ":failSoftVisible="
               << BoolDigit(decision.failSoftVisible)
               << ":operatorActionRequired="
               << BoolDigit(decision.operatorActionRequired);
        rows.push_back(stream.str());
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

std::string CtafUnicomLegacyBypassAliasAuditSummaryText(
    const xvatsim::brain::BrainOwnedPublisherOutput& output) {
    const auto& summary =
        output.ctafUnicomLegacyBypassAliasAuditSummary;
    std::ostringstream stream;
    stream << "aliasAudit=" << summary.aliasAuditCount
           << ",renameNow=" << summary.renameNowCandidateCount
           << ",renameLater=" << summary.renameLaterCount
           << ",removeLater=" << summary.removeLaterCount
           << ",harnessOnly=" << summary.harnessOnlyAliasCount
           << ",reportOnly=" << summary.reportOnlyAliasCount
           << ",publicRisk=" << summary.publicConsumerRiskCount
           << ",unknownRisk=" << summary.unknownConsumerRiskCount
           << ",liveAuthorityMisleading="
           << summary.liveAuthorityMisleadingAliasCount
           << ",authorityInvariantProtected="
           << summary.authorityInvariantProtectedCount
           << ",replacementFields="
           << summary.replacementFieldCount
           << ",legacyStillPresent="
           << summary.legacyFieldStillPresentCount
           << ",replacementMatches="
           << summary.replacementMatchesLegacyCount
           << ",replacementMismatch="
           << summary.replacementMismatchCount
           << ",harnessMigrated="
           << summary.harnessMigratedToReplacementCount
           << ",deprecatedAliases="
           << summary.deprecatedAliasCount
           << ",safeToRemoveLater="
           << summary.safeToRemoveLegacyLaterCount
           << ",reportOnlyRemoved="
           << summary.reportOnlyAliasRemovedCount
           << ",reportOnlyRemovalSafe="
           << summary.reportOnlyAliasRemovalSafeCount
           << ",reportOnlyStillFound="
           << summary.reportOnlyAliasStillFoundCount
           << ",replacementMigrationComplete="
           << BoolDigit(summary.replacementMigrationComplete);
    return stream.str();
}

std::vector<std::string> CtafUnicomLegacyBypassAliasAuditDecisionSummaries(
    const xvatsim::brain::BrainOwnedPublisherOutput& output) {
    std::vector<std::string> rows;
    rows.reserve(output.ctafUnicomLegacyBypassAliasAuditDecisions.size());
    for (const auto& decision :
         output.ctafUnicomLegacyBypassAliasAuditDecisions) {
        std::ostringstream stream;
        stream << decision.aliasName
               << ":id=" << decision.legacyAliasAuditId
               << ":location=" << NoneToken(decision.aliasLocation)
               << ":category=" << NoneToken(decision.aliasCategory)
               << ":meaning=" << NoneToken(decision.currentMeaning)
               << ":risk=" << NoneToken(decision.misleadingRisk)
               << ":action=" << NoneToken(decision.recommendedAction)
               << ":target=" << NoneToken(decision.migrationTarget)
               << ":consumerKnown=" << BoolDigit(decision.consumerKnown)
               << ":consumerRisk=" << NoneToken(decision.consumerRisk)
               << ":canRenameNow=" << BoolDigit(decision.canRenameNow)
               << ":canRemoveNow=" << BoolDigit(decision.canRemoveNow)
               << ":blocked="
               << NoneToken(decision.removalBlockedReason)
               << ":authorityInvariantProtected="
               << BoolDigit(decision.authorityInvariantProtected)
               << ":liveAuthorityImplication="
               << BoolDigit(decision.liveAuthorityImplication)
               << ":replacementFieldPresent="
               << BoolDigit(decision.replacementFieldPresent)
               << ":replacementFieldName="
               << NoneToken(decision.replacementFieldName)
               << ":legacyFieldStillPresent="
               << BoolDigit(decision.legacyFieldStillPresent)
               << ":replacementMatchesLegacy="
               << BoolDigit(decision.replacementMatchesLegacy)
               << ":harnessMigratedToReplacement="
               << BoolDigit(decision.harnessMigratedToReplacement)
               << ":oldAliasDeprecated="
               << BoolDigit(decision.oldAliasDeprecated)
               << ":safeToRemoveLegacyLater="
               << BoolDigit(decision.safeToRemoveLegacyLater)
               << ":replacementMigrationComplete="
               << BoolDigit(decision.replacementMigrationComplete)
               << ":replacementMismatchReason="
               << NoneToken(decision.replacementMismatchReason);
        rows.push_back(stream.str());
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

std::string CtafUnicomPublicUnknownAliasConsumerAuditSummaryText(
    const xvatsim::brain::BrainOwnedPublisherOutput& output) {
    const auto& summary =
        output.ctafUnicomPublicUnknownAliasConsumerAuditSummary;
    std::ostringstream stream;
    stream << "aliases=" << summary.publicUnknownAliasCount
           << ",sameScope=" << summary.replacementSameScopeCount
           << ",internalMigrated=" << summary.internalMigratedCount
           << ",compatibilityOnly=" << summary.compatibilityOnlyAliasCount
           << ",readyLater=" << summary.removalReadyLaterCount
           << ",blocked=" << summary.removalBlockedCount
           << ",externalRisk=" << summary.externalRiskCount
           << ",unknownRisk=" << summary.unknownRiskCount
           << ",runtimeUsage=" << summary.runtimeUsageCount
           << ",harnessLegacyUsage=" << summary.harnessLegacyUsageCount
           << ",reportLegacyUsage=" << summary.reportLegacyUsageCount;
    return stream.str();
}

std::vector<std::string>
CtafUnicomPublicUnknownAliasConsumerAuditDecisionSummaries(
    const xvatsim::brain::BrainOwnedPublisherOutput& output) {
    std::vector<std::string> rows;
    rows.reserve(
        output.ctafUnicomPublicUnknownAliasConsumerAuditDecisions.size());
    for (const auto& decision :
         output.ctafUnicomPublicUnknownAliasConsumerAuditDecisions) {
        std::ostringstream stream;
        stream << decision.consumerAliasName
               << ":replacement=" << NoneToken(decision.replacementName)
               << ":definition=" << NoneToken(decision.definitionLocation)
               << ":emission=" << NoneToken(decision.emissionLocation)
               << ":harnessUsage=" << decision.harnessUsageCount
               << ":reportUsage=" << decision.reportUsageCount
               << ":runtimeUsage=" << decision.runtimeUsageCount
               << ":docsUsage=" << decision.docsUsageCount
               << ":pluginUsage=" << decision.pluginUsageCount
               << ":externalRisk="
               << BoolDigit(decision.externalConsumerRisk)
               << ":unknownRisk=" << BoolDigit(decision.unknownConsumerRisk)
               << ":replacementSameScope="
               << BoolDigit(decision.replacementEmittedSameScope)
               << ":internalMigrated="
               << BoolDigit(decision.internalConsumersMigrated)
               << ":compatibilityOnly="
               << BoolDigit(decision.aliasCompatibilityOnly)
               << ":readyLater=" << BoolDigit(decision.removalReadyLater)
               << ":blocked="
               << NoneToken(decision.removalBlockedReason)
               << ":next=" << NoneToken(decision.nextMigrationAction);
        rows.push_back(stream.str());
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

std::string CtafUnicomExternalAliasDeprecationSummaryText(
    const xvatsim::brain::BrainOwnedPublisherOutput& output) {
    const auto& summary = output.ctafUnicomExternalAliasDeprecationSummary;
    std::ostringstream stream;
    stream << "decisions=" << summary.aliasDeprecationDecisionCount
           << ",externalRisk=" << summary.externalRiskAliasCount
           << ",externalDeprecated="
           << summary.externalAliasDeprecatedCount
           << ",externalRemoved=" << summary.externalAliasRemovedCount
           << ",activeRetained="
           << summary.activeGeneratedAliasRetainedCount
           << ",replacementPreferred="
           << summary.canonicalReplacementPreferredCount
           << ",replacementEquivalent="
           << summary.replacementEquivalentCount
           << ",publicHeaderRetained="
           << summary.publicHeaderRiskAliasRetainedCount
           << ",liveRowEmittedRetained="
           << BoolDigit(summary.liveRowEmittedRetained)
           << ",completionBypassCompatibilityOnlyRetained="
           << BoolDigit(summary.completionBypassCompatibilityOnlyRetained)
           << ",runtimeChanged="
           << BoolDigit(summary.runtimeBehaviorChanged)
           << ",noLiveBypassAuthority="
           << BoolDigit(summary.noLiveBypassAuthority);
    return stream.str();
}

std::vector<std::string> CtafUnicomExternalAliasDeprecationDecisionSummaries(
    const xvatsim::brain::BrainOwnedPublisherOutput& output) {
    std::vector<std::string> rows;
    rows.reserve(output.ctafUnicomExternalAliasDeprecationDecisions.size());
    for (const auto& decision :
         output.ctafUnicomExternalAliasDeprecationDecisions) {
        std::ostringstream stream;
        stream << decision.aliasName
               << ":replacement=" << NoneToken(decision.replacementName)
               << ":riskClass=" << NoneToken(decision.aliasRiskClass)
               << ":status=" << NoneToken(decision.deprecationStatus)
               << ":activePresent="
               << BoolDigit(decision.activeGeneratedAliasPresent)
               << ":removed="
               << BoolDigit(decision.aliasRemovedFromActiveOutput)
               << ":deprecated=" << BoolDigit(decision.aliasDeprecated)
               << ":replacementPreferred="
               << BoolDigit(decision.canonicalReplacementPreferred)
               << ":harnessUsesReplacement="
               << BoolDigit(decision.replacementUsedByHarness)
               << ":replacementEquivalent="
               << BoolDigit(decision.replacementCarriesEquivalentMeaning)
               << ":authorityInvariantProtected="
               << BoolDigit(decision.authorityInvariantProtected)
               << ":liveAuthorityImplication="
               << BoolDigit(decision.liveAuthorityImplication)
               << ":publicHeaderRetained="
               << BoolDigit(decision.publicHeaderRiskAliasRetained)
               << ":runtimeChanged="
               << BoolDigit(decision.runtimeBehaviorChanged)
               << ":blocked="
               << NoneToken(decision.removalBlockedReason)
               << ":next=" << NoneToken(decision.nextMigrationAction);
        rows.push_back(stream.str());
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

std::string CtafUnicomPublicHeaderAliasRiskClosureSummaryText(
    const xvatsim::brain::BrainOwnedPublisherOutput& output) {
    const auto& summary =
        output.ctafUnicomPublicHeaderAliasRiskClosureSummary;
    std::ostringstream stream;
    stream << "aliases=" << summary.publicHeaderAliasCount
           << ",sameScope=" << summary.replacementSameScopeCount
           << ",replacementMatches="
           << summary.replacementMatchesLegacyCount
           << ",compatibilityOnly=" << summary.compatibilityOnlyCount
           << ",deprecatedPublicHeaderAliasCount="
           << summary.deprecatedPublicHeaderAliasCount
           << ",deprecatedPublicHeaderAliasRetained="
           << summary.deprecatedPublicHeaderAliasRetainedCount
           << ",deprecatedAliasReplacementMatch="
           << summary.deprecatedAliasReplacementMatchCount
           << ",deprecatedAliasReplacementMismatch="
           << summary.deprecatedAliasReplacementMismatchCount
           << ",deprecatedAliasRemovalBlocked="
           << summary.deprecatedAliasRemovalBlockedCount
           << ",canDeprecateNow=" << summary.canDeprecateNowCount
           << ",canRemoveLater=" << summary.canRemoveLaterCount
           << ",removalBlocked=" << summary.removalBlockedCount
           << ",pluginUsage=" << summary.pluginUsageCount
           << ",moduleUsage=" << summary.moduleUsageCount
           << ",harnessLegacyUsage="
           << summary.harnessLegacyUsageCount
           << ",publicHeaderRisk=" << summary.publicHeaderRiskCount
           << ",deprecatedAliasDocumentationPresent="
           << BoolDigit(summary.deprecatedAliasDocumentationPresent)
           << ",publicHeaderCompatibilityWindowOpen="
           << BoolDigit(summary.publicHeaderCompatibilityWindowOpen)
           << ",ctafUnicomAliasCleanupClosedExceptCompatibilityWindow="
           << BoolDigit(
                  summary
                      .ctafUnicomAliasCleanupClosedExceptCompatibilityWindow);
    return stream.str();
}

std::vector<std::string>
CtafUnicomPublicHeaderAliasRiskClosureDecisionSummaries(
    const xvatsim::brain::BrainOwnedPublisherOutput& output) {
    std::vector<std::string> rows;
    rows.reserve(
        output.ctafUnicomPublicHeaderAliasRiskClosureDecisions.size());
    for (const auto& decision :
         output.ctafUnicomPublicHeaderAliasRiskClosureDecisions) {
        std::ostringstream stream;
        stream << decision.publicHeaderAliasName
               << ":deprecatedAliasName="
               << NoneToken(decision.deprecatedAliasName)
               << ":replacement=" << NoneToken(decision.replacementName)
               << ":header=" << NoneToken(decision.headerDefinitionLocation)
               << ":runtime=" << NoneToken(decision.runtimeWriteLocation)
               << ":harnessOutput="
               << NoneToken(decision.harnessOutputLocation)
               << ":harnessExpectations="
               << decision.harnessExpectationUsageCount
               << ":pluginUsage=" << decision.pluginUsageCount
               << ":moduleUsage=" << decision.moduleUsageCount
               << ":docsUsage=" << decision.docsUsageCount
               << ":reportUsage=" << decision.reportUsageCount
               << ":sameScope=" << BoolDigit(decision.replacementSameScope)
               << ":replacementMatches="
               << BoolDigit(decision.replacementMatchesLegacy)
               << ":compatibilityOnly="
               << BoolDigit(decision.compatibilityOnly)
               << ":deprecatedPublicHeaderAliasRetained="
               << BoolDigit(decision.deprecatedPublicHeaderAliasRetained)
               << ":deprecatedAliasStillEmitted="
               << BoolDigit(decision.deprecatedAliasStillEmitted)
               << ":replacementPreferred="
               << BoolDigit(decision.replacementPreferred)
               << ":replacementMatchesDeprecatedAlias="
               << BoolDigit(decision.replacementMatchesDeprecatedAlias)
               << ":canDeprecateNow="
               << BoolDigit(decision.canDeprecateNow)
               << ":canRemoveLater=" << BoolDigit(decision.canRemoveLater)
               << ":blocked="
               << NoneToken(decision.removalBlockedReason)
               << ":removalBlockedReason="
               << NoneToken(decision.removalBlockedReason)
               << ":publicHeaderRisk="
               << BoolDigit(decision.publicHeaderConsumerRisk)
               << ":externalRisk=" << BoolDigit(decision.externalConsumerRisk)
               << ":action=" << NoneToken(decision.recommendedAction)
               << ":next=" << NoneToken(decision.nextMigrationStep);
        rows.push_back(stream.str());
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

std::string StandbyAssistSummaryText(
    const xvatsim::brain::BrainOwnedStandbyRecommendationSummary& summary) {
    std::ostringstream stream;
    stream << "evidence=" << summary.standbyEvidenceCount
           << ",candidates=" << summary.standbyCandidateCount
           << ",advisory=" << summary.advisoryCandidateCount
           << ",selected=" << summary.selectedTargetCount
           << ",writeDecisions=" << summary.writeDecisionCount
           << ",writeAttempts=" << summary.writeAttemptCount
           << ",writeSuccess=" << summary.writeSuccessCount
           << ",writeFailure=" << summary.writeFailureCount
           << ",empty=" << summary.skippedEmptyFrequencyCount
           << ",pending=" << summary.skippedPendingLookupCount
           << ",failed=" << summary.skippedLookupFailedCount
           << ",guard=" << summary.skippedGuardFrequencyCount
           << ",roleSkip=" << summary.skippedRoleNotEligibleCount
           << ",activeSkip=" << summary.skippedAlreadyActiveCount
           << ",brainOwned="
           << BoolDigit(summary.standbyRecommendationsBrainOwned);
    return stream.str();
}

std::string StandbyAssistSettingsDiagnosticsText(
    const xvatsim::brain::BrainOwnedStandbyAssistSettingsDiagnostics&
        diagnostics) {
    std::ostringstream stream;
    stream << "standbyAssistEnabled="
           << BoolDigit(diagnostics.standbyAssistEnabled)
           << ",directCtafStandbyAssistEnabled="
           << BoolDigit(diagnostics.directCtafStandbyAssistEnabled)
           << ",directCtafGateSource="
           << NoneToken(diagnostics.directCtafGateSource)
           << ",directCtafGateEffective="
           << BoolDigit(diagnostics.directCtafGateEffective);
    return stream.str();
}

std::vector<std::string> StandbyAssistDecisionSummaries(
    const xvatsim::brain::BrainOwnedStandbyAssistPlanOutput& plan) {
    std::vector<std::string> rows;
    rows.reserve(plan.standbyDecisions.size());
    for (const auto& decision : plan.standbyDecisions) {
        std::ostringstream stream;
        stream << decision.sourceDomain
               << ":id=" << NoneToken(decision.standbyDecisionId)
               << ":subject=" << NoneToken(decision.subjectKey)
               << ":sourceDecision=" << NoneToken(decision.sourceDecisionId)
               << ":sourceEvidence=" << NoneToken(decision.sourceEvidenceId)
               << ":endpoint=" << NoneToken(decision.endpoint)
               << ":airport=" << NoneToken(decision.airportIcao)
               << ":callsign=" << NoneToken(decision.callsign)
               << ":role=" << NoneToken(decision.role)
               << ":freq=" << EmptyToken(decision.frequency)
               << ":stage=" << NoneToken(decision.workflowStage)
               << ":plan=" << NoneToken(decision.planKey)
               << ":boardIndex=" << decision.boardIndex
               << ":relation=" << NoneToken(decision.displayRelation)
               << ":visible=" << BoolDigit(decision.candidateVisibleInFinalBoard)
               << ":acceptedByAdvisory="
               << BoolDigit(decision.acceptedByAdvisory)
               << ":advisory=" << NoneToken(decision.advisoryDecision)
               << ":sourceConfidence="
               << NoneToken(decision.sourceConfidence)
               << ":confidence=" << NoneToken(decision.confidenceLevel)
               << ":fallback=" << BoolDigit(decision.fallbackUsed)
               << ":score="
               << FormatCtafAdvisoryScore(decision.positiveScore)
               << "/"
               << FormatCtafAdvisoryScore(decision.negativeScore)
               << ":hardBlock=" << BoolDigit(decision.hardBlock)
               << ":hardBlockReason="
               << NoneToken(decision.hardBlockReason)
               << ":com1Active=" << BoolDigit(decision.alreadyCom1Active)
               << ":com2Active=" << BoolDigit(decision.alreadyCom2Active)
               << ":com1Standby=" << BoolDigit(decision.alreadyCom1Standby)
               << ":targetCom=" << NoneToken(decision.targetCom)
               << ":eligible=" << BoolDigit(decision.eligible)
               << ":liveEligible=" << BoolDigit(decision.eligible)
               << ":previewEligible=" << BoolDigit(decision.previewEligible)
               << ":previewRecommendation="
               << NoneToken(decision.previewRecommendation)
               << ":previewSkip="
               << NoneToken(decision.previewSkipReason)
               << ":liveWriteEligible="
               << BoolDigit(decision.liveWriteEligible)
               << ":productGateEnabled="
               << BoolDigit(decision.productGateEnabled)
               << ":directCtafLivePromotionAllowed="
               << BoolDigit(decision.directCtafLivePromotionAllowed)
               << ":livePromotionReason="
               << NoneToken(decision.livePromotionReason)
               << ":livePromotionBlockedReason="
               << NoneToken(decision.livePromotionBlockedReason)
               << ":promotedFromDryRun="
               << BoolDigit(decision.promotedFromDryRun)
               << ":actualSelectedTargetSource="
               << NoneToken(decision.actualSelectedTargetSource)
               << ":actualSelectedTargetFrequency="
               << EmptyToken(decision.actualSelectedTargetFrequency)
               << ":actualWriteEligible="
               << BoolDigit(decision.actualWriteEligible)
               << ":noControllerTargetAvailable="
               << BoolDigit(decision.noControllerTargetAvailable)
               << ":controllerTargetPreserved="
               << BoolDigit(decision.controllerTargetPreserved)
               << ":featureGateRequired="
               << NoneToken(decision.featureGateRequired)
               << ":featureGateSatisfied="
               << BoolDigit(decision.featureGateSatisfied)
               << ":featureGateBlockedReason="
               << NoneToken(decision.featureGateBlockedReason)
               << ":dryRunLiveEligible="
               << BoolDigit(decision.dryRunLiveEligible)
               << ":dryRunLiveRecommendation="
               << NoneToken(decision.dryRunLiveRecommendation)
               << ":dryRunSkip="
               << NoneToken(decision.dryRunSkipReason)
               << ":dryRunSafetyGate="
               << NoneToken(decision.dryRunSafetyGate)
               << ":dryRunWouldSelectTarget="
               << BoolDigit(decision.dryRunWouldSelectTarget)
               << ":dryRunWouldDisplaceControllerTarget="
               << BoolDigit(decision.dryRunWouldDisplaceControllerTarget)
               << ":dryRunBlockedByExistingControllerTarget="
               << BoolDigit(decision.dryRunBlockedByExistingControllerTarget)
               << ":dryRunBlockedByStandbyDisabled="
               << BoolDigit(decision.dryRunBlockedByStandbyDisabled)
               << ":dryRunBlockedByAlreadyCom1Standby="
               << BoolDigit(decision.dryRunBlockedByAlreadyCom1Standby)
               << ":dryRunBlockedByFrequencyState="
               << BoolDigit(decision.dryRunBlockedByFrequencyState)
               << ":dryRunTargetCom="
               << NoneToken(decision.dryRunTargetCom)
               << ":dryRunTargetFrequency="
               << EmptyToken(decision.dryRunTargetFrequency)
               << ":dryRunPromotionClass="
               << NoneToken(decision.dryRunPromotionClass)
               << ":advisoryProductGate="
               << NoneToken(decision.advisoryProductGate)
               << ":advisoryWritePolicy="
               << NoneToken(decision.advisoryWritePolicy)
               << ":advisoryFrequencyResolutionState="
               << NoneToken(decision.advisoryFrequencyResolutionState)
               << ":advisoryCandidateType="
               << NoneToken(decision.advisoryCandidateType)
               << ":skip=" << NoneToken(decision.skipReason)
               << ":final=" << NoneToken(decision.finalRecommendation);
        rows.push_back(stream.str());
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

std::string StandbyAssistSideEffectSummaryText(
    const xvatsim::brain::BrainOwnedStandbyAssistSideEffectDecision& decision) {
    std::ostringstream stream;
    stream << "sideEffect=" << NoneToken(decision.sideEffectDecisionId)
           << ",standbyDecision=" << NoneToken(decision.standbyDecisionId)
           << ",enabled=" << BoolDigit(decision.standbyAssistEnabled)
           << ",latchKey=" << NoneToken(decision.latchKey)
           << ",latchConsumed=" << BoolDigit(decision.latchConsumed)
           << ",writeAllowed=" << BoolDigit(decision.writeAllowed)
           << ",writeAttempted=" << BoolDigit(decision.writeAttempted)
           << ",writeSucceededKnown="
           << BoolDigit(decision.writeSucceededKnown)
           << ",writeSucceeded=" << BoolDigit(decision.writeSucceeded)
           << ",writerTarget=" << NoneToken(decision.writerTarget)
           << ",targetFrequency=" << EmptyToken(decision.targetFrequency)
           << ",failure=" << NoneToken(decision.failureReason)
           << ",marker=" << BoolDigit(decision.displayStandbyMarkerApplied);
    return stream.str();
}

std::string StandbyAssistSideEffectActualSummaryText(
    const xvatsim::brain::BrainOwnedStandbyAssistSideEffectDecision& decision) {
    std::ostringstream stream;
    stream << "actualSelectedTargetSource="
           << NoneToken(decision.actualSelectedTargetSource)
           << ",actualSelectedTargetFrequency="
           << EmptyToken(decision.actualSelectedTargetFrequency)
           << ",actualWriteEligible="
           << BoolDigit(decision.actualWriteEligible)
           << ",actualWriteAttempted="
           << BoolDigit(decision.actualWriteAttempted)
           << ",actualWriteSucceededKnown="
           << BoolDigit(decision.actualWriteSucceededKnown)
           << ",actualWriteSucceeded="
           << BoolDigit(decision.actualWriteSucceeded);
    return stream.str();
}

std::string StandbyAssistWriterResultSummaryText(
    const xvatsim::brain::BrainOwnedStandbyAssistSideEffectDecision& decision) {
    const auto& result = decision.writerResult;
    std::ostringstream stream;
    stream << "known=" << BoolDigit(result.writerResultKnown)
           << ",code=" << NoneToken(result.writerResultCode)
           << ",failureReason=" << NoneToken(result.writerFailureReason)
           << ",failureDomain=" << NoneToken(result.writerFailureDomain)
           << ",inputFrequency=" << EmptyToken(result.writerInputFrequency)
           << ",normalizedFrequency="
           << EmptyToken(result.writerNormalizedFrequency)
           << ",targetCom=" << NoneToken(result.writerTargetCom)
           << ",dataref=" << NoneToken(result.writerDatarefName)
           << ",datarefAvailable="
           << BoolDigit(result.writerDatarefAvailable)
           << ",datarefWritable="
           << BoolDigit(result.writerDatarefWritable)
           << ",validationPassed="
           << BoolDigit(result.writerValidationPassed)
           << ",writeAttempted="
           << BoolDigit(result.writerWriteAttempted)
           << ",writeSucceeded="
           << BoolDigit(result.writerWriteSucceeded)
           << ",blockedBeforeSimWrite="
           << BoolDigit(result.writerWriteBlockedBeforeSimWrite)
           << ",failedAtSimLayer="
           << BoolDigit(result.writerWriteFailedAtSimLayer)
           << ",source=" << NoneToken(result.writerResultSource)
           << ",decisionId="
           << NoneToken(result.writerResultDecisionId)
           << ",linkedStandbyDecisionId="
           << NoneToken(result.writerResultLinkedStandbyDecisionId);
    return stream.str();
}

std::string StandbyAssistWriterCounterSummaryText(
    const xvatsim::brain::BrainOwnedStandbyRecommendationSummary& summary) {
    std::ostringstream stream;
    stream << "writerResults=" << summary.writerResultCount
           << ",writerSuccess=" << summary.writerSuccessCount
           << ",writerFailure=" << summary.writerFailureCount
           << ",writerBlocked="
           << summary.writerBlockedBeforeWriteCount
           << ",writerUnknown=" << summary.writerUnknownResultCount
           << ",writerDatarefMissing="
           << summary.writerDatarefMissingCount
           << ",writerDatarefNotWritable="
           << summary.writerDatarefNotWritableCount
           << ",writerInvalidFrequency="
           << summary.writerInvalidFrequencyCount
           << ",writerNoTarget=" << summary.writerNoTargetCount
           << ",writerNoWriteRequested="
           << summary.writerNoWriteRequestedCount
           << ",writerControllerSource="
           << summary.writerControllerSourceCount
           << ",writerDirectCtafSource="
           << summary.writerDirectCtafSourceCount;
    return stream.str();
}

std::string RoundedIntText(double value) {
    return std::to_string(static_cast<int>(std::round(value)));
}

std::string TransceiverResolverSourceEvidenceSummary(
    const xvatsim::brain::TransceiverResolutionSnapshot& snapshot) {
    const auto& evidence = snapshot.sourceEvidence;
    std::ostringstream stream;
    stream << "cache=" << BoolDigit(evidence.feedCacheExists)
           << ",count=" << evidence.cachedTransceiverCount
           << ",sourceKnown="
           << BoolDigit(evidence.sourceControllerCountKnown)
           << ",sourceControllers=" << evidence.sourceControllerCount
           << ",fresh=" << BoolDigit(evidence.cacheFresh)
           << ",stale=" << BoolDigit(evidence.cacheStale)
           << ",holdover=" << BoolDigit(evidence.holdoverUsed)
           << ",expired=" << BoolDigit(evidence.holdoverExpired)
           << ",age="
           << (evidence.hasFeedAgeSeconds
                   ? std::to_string(evidence.feedAgeSeconds)
                   : std::string("<none>"))
           << ",fetchAttempted=" << BoolDigit(evidence.fetchAttempted)
           << ",fetchInProgress=" << BoolDigit(evidence.fetchInProgress)
           << ",fetchFailed=" << BoolDigit(evidence.fetchFailed)
           << ",reason=" << NoneToken(evidence.failureReason)
           << ",parser=badCallsign:"
           << evidence.parser.invalidClientCallsign
           << "|badFreq:" << evidence.parser.invalidTransceiverFrequency
           << "|badPos:" << evidence.parser.invalidPosition
           << "|badHeight:" << evidence.parser.invalidHeight
           << "|parseException:" << evidence.parser.parseException
           << "|emptyPayload:" << evidence.parser.emptyPayload
           << "|truncated:"
           << BoolDigit(evidence.parser.maxTransceiverTruncation);
    return stream.str();
}

bool TransceiverControllerEvidenceMatchesCandidate(
    const xvatsim::brain::TransceiverControllerEvidenceSnapshot& evidence,
    const xvatsim::brain::ReceivableControllerSnapshot& candidate) {
    return evidence.callsign == candidate.callsign &&
           evidence.resolvedDisplayFrequency == candidate.frequency;
}

bool TransceiverControllerEvidenceMatchesAnyCandidate(
    const xvatsim::brain::TransceiverControllerEvidenceSnapshot& evidence,
    const xvatsim::brain::TransceiverResolutionSnapshot& snapshot) {
    return std::any_of(
        snapshot.candidates.begin(),
        snapshot.candidates.end(),
        [&](const auto& candidate) {
            return TransceiverControllerEvidenceMatchesCandidate(
                evidence,
                candidate);
        });
}

bool TransceiverControllerEvidenceHasReasonFact(
    const xvatsim::brain::TransceiverControllerEvidenceSnapshot& evidence) {
    if (!evidence.actionable ||
        !evidence.hasTransceiverEntry ||
        !evidence.displayFrequencyUnavailableReason.empty()) {
        return true;
    }

    return std::any_of(
        evidence.stations.begin(),
        evidence.stations.end(),
        [](const auto& station) {
            return !station.withinMaxCandidateDistance ||
                   !station.withinReceivableRange;
        });
}

std::string JoinClassFlags(const std::vector<std::string>& classes) {
    if (classes.empty()) {
        return "<none>";
    }

    std::ostringstream stream;
    for (const auto& className : classes) {
        if (stream.tellp() > 0) {
            stream << "|";
        }
        stream << className;
    }
    return stream.str();
}

std::string TransceiverResolverEvidenceVisibilitySummary(
    const xvatsim::brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const xvatsim::brain::TransceiverResolutionSnapshot& snapshot) {
    int survivorsWithEvidence = 0;
    for (const auto& candidate : snapshot.candidates) {
        const auto hasEvidence = std::any_of(
            snapshot.controllerEvidence.begin(),
            snapshot.controllerEvidence.end(),
            [&](const auto& evidence) {
                return TransceiverControllerEvidenceMatchesCandidate(
                    evidence,
                    candidate);
            });
        if (hasEvidence) {
            ++survivorsWithEvidence;
        }
    }

    int nonsurvivorReasonFacts = 0;
    int nonBestSurvivorStations = 0;
    bool hasNonActionable = false;
    bool hasMissingTransceiver = false;
    bool hasOverMaxDistance = false;
    bool hasBeyondReceivableRange = false;
    bool hasGuardFrequency = false;
    bool hasEmptyFrequency = false;

    for (const auto& evidence : snapshot.controllerEvidence) {
        const auto matchesSurvivor =
            TransceiverControllerEvidenceMatchesAnyCandidate(
                evidence,
                snapshot);
        if (!matchesSurvivor &&
            TransceiverControllerEvidenceHasReasonFact(evidence)) {
            ++nonsurvivorReasonFacts;
        }

        hasNonActionable = hasNonActionable || !evidence.actionable;
        hasMissingTransceiver =
            hasMissingTransceiver || !evidence.hasTransceiverEntry;
        hasGuardFrequency =
            hasGuardFrequency ||
            evidence.controllerFrequencyGuard ||
            evidence.transceiverFrequencyGuard;
        hasEmptyFrequency =
            hasEmptyFrequency ||
            evidence.displayFrequencyUnavailableReason == "both-empty";

        bool hasBestSurvivorStation = false;
        for (const auto& station : evidence.stations) {
            hasOverMaxDistance =
                hasOverMaxDistance || !station.withinMaxCandidateDistance;
            hasBeyondReceivableRange =
                hasBeyondReceivableRange ||
                (station.withinMaxCandidateDistance &&
                 !station.withinReceivableRange);
            hasBestSurvivorStation =
                hasBestSurvivorStation ||
                (matchesSurvivor && station.bestByModuleScore);
        }
        if (hasBestSurvivorStation) {
            for (const auto& station : evidence.stations) {
                if (!station.bestByModuleScore) {
                    ++nonBestSurvivorStations;
                }
            }
        }
    }

    std::vector<std::string> classes;
    if (hasNonActionable) {
        classes.push_back("nonactionable");
    }
    if (hasMissingTransceiver) {
        classes.push_back("missing-transceiver");
    }
    if (hasOverMaxDistance) {
        classes.push_back("over-max-distance");
    }
    if (hasBeyondReceivableRange) {
        classes.push_back("beyond-receivable-range");
    }
    if (hasGuardFrequency) {
        classes.push_back("guard-frequency");
    }
    if (hasEmptyFrequency) {
        classes.push_back("empty-frequency");
    }
    if (nonBestSurvivorStations > 0) {
        classes.push_back("alternate-nonbest");
    }

    std::ostringstream stream;
    stream << "sourceKnown="
           << BoolDigit(snapshot.sourceEvidence.sourceControllerCountKnown)
           << ",sourceControllers="
           << snapshot.sourceEvidence.sourceControllerCount
           << ",sourceEntries="
           << controllerFeedSnapshot.Controllers().size()
           << ",evidenceControllers="
           << snapshot.controllerEvidence.size()
           << ",survivors=" << snapshot.candidates.size()
           << ",compatOnly="
           << BoolDigit(snapshot.candidatesCompatibilityOnly)
           << ",droppedBeforeBrain="
           << snapshot.droppedBeforeBrainControllers
           << ",evidenceGteSurvivors="
           << BoolDigit(
                  snapshot.controllerEvidence.size() >=
                  snapshot.candidates.size())
           << ",survivorsWithEvidence=" << survivorsWithEvidence
           << ",nonsurvivorReasonFacts=" << nonsurvivorReasonFacts
           << ",nonBestSurvivorStations=" << nonBestSurvivorStations
           << ",classes=" << JoinClassFlags(classes);
    return stream.str();
}

std::string BrainRadioRangePreviewSummaryText(
    const xvatsim::brain::BrainRadioRangeDecisionPreview& preview) {
    std::ostringstream stream;
    stream << "authority="
           << (preview.summary.liveCandidatesBrainOwned
                   ? "brain-evidence"
                   : "old-candidates-fallback")
           << ",evidence=" << preview.summary.evidenceControllerCount
           << ",oldSurvivors=" << preview.summary.oldSurvivorCount
           << ",previewSurvivors="
           << preview.summary.previewSurvivorCount
           << ",previewRejected="
           << preview.summary.previewRejectedCount
           << ",oldMismatch="
           << preview.summary.oldSurvivorMismatchCount
           << ",compatOnly="
           << BoolDigit(
                  preview.summary.resolverCandidatesCompatibilityOnly)
           << ",droppedBeforeBrain="
           << preview.summary.droppedBeforeBrainControllers;
    return stream.str();
}

std::vector<std::string> BrainRadioRangePreviewDecisionSummaries(
    const xvatsim::brain::BrainRadioRangeDecisionPreview& preview) {
    std::vector<std::string> summaries;
    summaries.reserve(preview.decisions.size());
    for (const auto& decision : preview.decisions) {
        std::ostringstream stream;
        stream << decision.callsign
               << "@" << EmptyToken(decision.frequency)
               << ":" << decision.decision
               << ":" << decision.reason
               << ":old=" << BoolDigit(decision.matchesOldSurvivor);
        summaries.push_back(stream.str());
    }
    return summaries;
}

std::vector<std::string> TransceiverResolverControllerEvidenceSummaries(
    const xvatsim::brain::TransceiverResolutionSnapshot& snapshot) {
    std::vector<std::string> summaries;
    summaries.reserve(snapshot.controllerEvidence.size());
    for (const auto& evidence : snapshot.controllerEvidence) {
        std::ostringstream stream;
        stream << evidence.callsign
               << "@" << EmptyToken(evidence.controllerFrequency)
               << ":facility=" << evidence.facility
               << ":actionable=" << BoolDigit(evidence.actionable)
               << ":atis=" << BoolDigit(evidence.atis)
               << ":visual=" << evidence.visualRangeNm
               << ":tx=" << BoolDigit(evidence.hasTransceiverEntry)
               << "/" << evidence.matchingTransceiverCount
               << ":display=" << EmptyToken(evidence.resolvedDisplayFrequency)
               << ":source=" << NoneToken(evidence.displayFrequencySource)
               << ":reason="
               << NoneToken(evidence.displayFrequencyUnavailableReason)
               << ":ctrlGuard="
               << BoolDigit(evidence.controllerFrequencyGuard)
               << ":txGuard="
               << BoolDigit(evidence.transceiverFrequencyGuard);
        summaries.push_back(stream.str());
    }
    return summaries;
}

std::vector<std::string> TransceiverResolverStationEvidenceSummaries(
    const xvatsim::brain::TransceiverResolutionSnapshot& snapshot) {
    std::vector<std::string> summaries;
    for (const auto& controllerEvidence : snapshot.controllerEvidence) {
        for (std::size_t index = 0;
             index < controllerEvidence.stations.size();
             ++index) {
            const auto& station = controllerEvidence.stations[index];
            std::ostringstream stream;
            stream << controllerEvidence.callsign
                   << "[" << index << "]"
                   << "@" << EmptyToken(station.sourceFrequency)
                   << ":dist=" << RoundedIntText(station.aircraftDistanceNm)
                   << ":max=" << RoundedIntText(station.maxCandidateDistanceNm)
                   << ":withinMax="
                   << BoolDigit(station.withinMaxCandidateDistance)
                   << ":range=" << RoundedIntText(station.receivableRangeNm)
                   << ":withinRange="
                   << BoolDigit(station.withinReceivableRange)
                   << ":score=" << RoundedIntText(station.score)
                   << ":best=" << BoolDigit(station.bestByModuleScore);
            summaries.push_back(stream.str());
        }
    }
    return summaries;
}

std::string TransceiverResolverAuthorityEvidenceVisibilitySummary(
    const xvatsim::brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const xvatsim::brain::TransceiverResolutionSnapshot& snapshot) {
    int survivorsWithEvidence = 0;
    for (const auto& candidate : snapshot.candidates) {
        const auto hasEvidence = std::any_of(
            snapshot.controllerEvidence.begin(),
            snapshot.controllerEvidence.end(),
            [&](const auto& evidence) {
                return TransceiverControllerEvidenceMatchesCandidate(
                    evidence,
                    candidate);
            });
        if (hasEvidence) {
            ++survivorsWithEvidence;
        }
    }

    int nonsurvivorReasonFacts = 0;
    bool hasNonActionable = false;
    bool hasMissingTransceiver = false;
    bool hasGuardFrequency = false;
    bool hasEmptyFrequency = false;

    for (const auto& evidence : snapshot.controllerEvidence) {
        const auto matchesSurvivor =
            TransceiverControllerEvidenceMatchesAnyCandidate(
                evidence,
                snapshot);
        if (!matchesSurvivor &&
            (!evidence.pathUnavailableReason.empty() ||
             !evidence.actionable ||
             !evidence.hasTransceiverEntry ||
             !evidence.displayFrequencyUnavailableReason.empty())) {
            ++nonsurvivorReasonFacts;
        }

        hasNonActionable = hasNonActionable || !evidence.actionable;
        hasMissingTransceiver =
            hasMissingTransceiver || !evidence.hasTransceiverEntry;
        hasGuardFrequency =
            hasGuardFrequency ||
            evidence.pathUnavailableReason == "guard-frequency" ||
            evidence.displayFrequencyUnavailableReason.find("guard") !=
                std::string::npos;
        hasEmptyFrequency =
            hasEmptyFrequency ||
            evidence.pathUnavailableReason == "empty-frequency" ||
            evidence.displayFrequencyUnavailableReason.find("empty") !=
                std::string::npos;
    }

    std::vector<std::string> classes;
    if (hasNonActionable) {
        classes.push_back("nonactionable");
    }
    if (hasMissingTransceiver) {
        classes.push_back("missing-transceiver");
    }
    if (hasGuardFrequency) {
        classes.push_back("guard-frequency");
    }
    if (hasEmptyFrequency) {
        classes.push_back("empty-frequency");
    }

    std::ostringstream stream;
    stream << "path=" << NoneToken(snapshot.resolutionPath)
           << ",sourceKnown="
           << BoolDigit(snapshot.sourceEvidence.sourceControllerCountKnown)
           << ",sourceControllers="
           << snapshot.sourceEvidence.sourceControllerCount
           << ",sourceEntries="
           << controllerFeedSnapshot.Controllers().size()
           << ",evidenceControllers="
           << snapshot.controllerEvidence.size()
           << ",survivors=" << snapshot.candidates.size()
           << ",compatOnly="
           << BoolDigit(snapshot.candidatesCompatibilityOnly)
           << ",droppedBeforeBrain="
           << snapshot.droppedBeforeBrainControllers
           << ",evidenceGteSurvivors="
           << BoolDigit(
                  snapshot.controllerEvidence.size() >=
                  snapshot.candidates.size())
           << ",survivorsWithEvidence=" << survivorsWithEvidence
           << ",nonsurvivorReasonFacts=" << nonsurvivorReasonFacts
           << ",classes=" << JoinClassFlags(classes);
    return stream.str();
}

std::vector<std::string> TransceiverResolverAuthorityControllerEvidenceSummaries(
    const xvatsim::brain::TransceiverResolutionSnapshot& snapshot) {
    std::vector<std::string> summaries;
    summaries.reserve(snapshot.controllerEvidence.size());
    for (const auto& evidence : snapshot.controllerEvidence) {
        std::ostringstream stream;
        stream << evidence.callsign
               << "@" << EmptyToken(evidence.controllerFrequency)
               << ":facility=" << evidence.facility
               << ":actionable=" << BoolDigit(evidence.actionable)
               << ":atis=" << BoolDigit(evidence.atis)
               << ":visual=" << evidence.visualRangeNm
               << ":tx=" << BoolDigit(evidence.hasTransceiverEntry)
               << "/" << evidence.matchingTransceiverCount
               << ":display=" << EmptyToken(evidence.resolvedDisplayFrequency)
               << ":source=" << NoneToken(evidence.displayFrequencySource)
               << ":reason="
               << NoneToken(evidence.displayFrequencyUnavailableReason)
               << ":ctrlGuard="
               << BoolDigit(evidence.controllerFrequencyGuard)
               << ":txGuard="
               << BoolDigit(evidence.transceiverFrequencyGuard)
               << ":pathReason="
               << NoneToken(evidence.pathUnavailableReason);
        summaries.push_back(stream.str());
    }
    return summaries;
}

std::vector<std::string> TransceiverResolverAuthorityStationEvidenceSummaries(
    const xvatsim::brain::TransceiverResolutionSnapshot& snapshot) {
    std::vector<std::string> summaries;
    for (const auto& controllerEvidence : snapshot.controllerEvidence) {
        for (std::size_t index = 0;
             index < controllerEvidence.stations.size();
             ++index) {
            const auto& station = controllerEvidence.stations[index];
            std::ostringstream stream;
            stream << controllerEvidence.callsign
                   << "[" << index << "]"
                   << "@" << EmptyToken(station.sourceFrequency)
                   << ":lat=" << RoundedIntText(station.latitudeDeg)
                   << ":lon=" << RoundedIntText(station.longitudeDeg)
                   << ":height=" << RoundedIntText(station.heightAglFt)
                   << ":score=" << RoundedIntText(station.score)
                   << ":best=" << BoolDigit(station.bestByModuleScore)
                   << ":txGuard="
                   << BoolDigit(station.transceiverFrequencyGuard);
            summaries.push_back(stream.str());
        }
    }
    return summaries;
}

bool AirportCoverageEvidenceHasAnyCoveringStation(
    const xvatsim::brain::TransceiverControllerEvidenceSnapshot& evidence) {
    return std::any_of(
        evidence.stations.begin(),
        evidence.stations.end(),
        [](const auto& station) {
            return station.withinReceivableRange;
        });
}

std::string TransceiverResolverAirportCoverageEvidenceVisibilitySummary(
    const xvatsim::brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const xvatsim::brain::TransceiverResolutionSnapshot& snapshot) {
    int survivorsWithEvidence = 0;
    for (const auto& candidate : snapshot.candidates) {
        const auto hasEvidence = std::any_of(
            snapshot.controllerEvidence.begin(),
            snapshot.controllerEvidence.end(),
            [&](const auto& evidence) {
                return TransceiverControllerEvidenceMatchesCandidate(
                    evidence,
                    candidate);
            });
        if (hasEvidence) {
            ++survivorsWithEvidence;
        }
    }

    int nonsurvivorReasonFacts = 0;
    bool hasNonActionable = false;
    bool hasMissingTransceiver = false;
    bool hasOutOfCoverage = false;
    bool hasAllStationsFailed = false;
    bool hasGuardFrequency = false;
    bool hasEmptyFrequency = false;
    bool hasAlternateNonBest = false;

    for (const auto& evidence : snapshot.controllerEvidence) {
        const auto matchesSurvivor =
            TransceiverControllerEvidenceMatchesAnyCandidate(
                evidence,
                snapshot);
        const auto anyCoveringStation =
            AirportCoverageEvidenceHasAnyCoveringStation(evidence);

        if (!matchesSurvivor &&
            (!evidence.pathUnavailableReason.empty() ||
             !evidence.actionable ||
             !evidence.hasTransceiverEntry ||
             !evidence.displayFrequencyUnavailableReason.empty() ||
             !anyCoveringStation)) {
            ++nonsurvivorReasonFacts;
        }

        hasNonActionable = hasNonActionable || !evidence.actionable;
        hasMissingTransceiver =
            hasMissingTransceiver || !evidence.hasTransceiverEntry;
        hasGuardFrequency =
            hasGuardFrequency ||
            evidence.pathUnavailableReason == "guard-frequency" ||
            evidence.displayFrequencyUnavailableReason.find("guard") !=
                std::string::npos;
        hasEmptyFrequency =
            hasEmptyFrequency ||
            evidence.pathUnavailableReason == "empty-frequency" ||
            evidence.displayFrequencyUnavailableReason.find("empty") !=
                std::string::npos;

        bool hasBestSurvivorStation = false;
        for (const auto& station : evidence.stations) {
            hasOutOfCoverage =
                hasOutOfCoverage || !station.withinReceivableRange;
            hasBestSurvivorStation =
                hasBestSurvivorStation ||
                (matchesSurvivor && station.bestByModuleScore);
        }
        hasAllStationsFailed =
            hasAllStationsFailed ||
            (evidence.hasTransceiverEntry &&
             !evidence.stations.empty() &&
             !anyCoveringStation);

        if (hasBestSurvivorStation) {
            for (const auto& station : evidence.stations) {
                if (!station.bestByModuleScore &&
                    station.withinReceivableRange) {
                    hasAlternateNonBest = true;
                }
            }
        }
    }

    std::vector<std::string> classes;
    if (hasNonActionable) {
        classes.push_back("nonactionable");
    }
    if (hasMissingTransceiver) {
        classes.push_back("missing-transceiver");
    }
    if (hasOutOfCoverage) {
        classes.push_back("out-of-coverage");
    }
    if (hasAllStationsFailed) {
        classes.push_back("all-stations-failed");
    }
    if (hasGuardFrequency) {
        classes.push_back("guard-frequency");
    }
    if (hasEmptyFrequency) {
        classes.push_back("empty-frequency");
    }
    if (hasAlternateNonBest) {
        classes.push_back("alternate-nonbest");
    }

    std::ostringstream stream;
    stream << "path=" << NoneToken(snapshot.resolutionPath)
           << ",sourceKnown="
           << BoolDigit(snapshot.sourceEvidence.sourceControllerCountKnown)
           << ",sourceControllers="
           << snapshot.sourceEvidence.sourceControllerCount
           << ",sourceEntries="
           << controllerFeedSnapshot.Controllers().size()
           << ",evidenceControllers="
           << snapshot.controllerEvidence.size()
           << ",survivors=" << snapshot.candidates.size()
           << ",compatOnly="
           << BoolDigit(snapshot.candidatesCompatibilityOnly)
           << ",droppedBeforeBrain="
           << snapshot.droppedBeforeBrainControllers
           << ",evidenceGteSurvivors="
           << BoolDigit(
                  snapshot.controllerEvidence.size() >=
                  snapshot.candidates.size())
           << ",survivorsWithEvidence=" << survivorsWithEvidence
           << ",nonsurvivorReasonFacts=" << nonsurvivorReasonFacts
           << ",classes=" << JoinClassFlags(classes);
    return stream.str();
}

std::vector<std::string>
TransceiverResolverAirportCoverageControllerEvidenceSummaries(
    const xvatsim::brain::TransceiverResolutionSnapshot& snapshot) {
    return TransceiverResolverAuthorityControllerEvidenceSummaries(snapshot);
}

std::vector<std::string>
TransceiverResolverAirportCoverageStationEvidenceSummaries(
    const xvatsim::brain::TransceiverResolutionSnapshot& snapshot) {
    std::vector<std::string> summaries;
    for (const auto& controllerEvidence : snapshot.controllerEvidence) {
        for (std::size_t index = 0;
             index < controllerEvidence.stations.size();
             ++index) {
            const auto& station = controllerEvidence.stations[index];
            std::ostringstream stream;
            stream << controllerEvidence.callsign
                   << "[" << index << "]"
                   << "@" << EmptyToken(station.sourceFrequency)
                   << ":lat=" << RoundedIntText(station.latitudeDeg)
                   << ":lon=" << RoundedIntText(station.longitudeDeg)
                   << ":height=" << RoundedIntText(station.heightAglFt)
                   << ":airportDist="
                   << RoundedIntText(station.aircraftDistanceNm)
                   << ":coverage="
                   << RoundedIntText(station.receivableRangeNm)
                   << ":withinCoverage="
                   << BoolDigit(station.withinReceivableRange)
                   << ":score=" << RoundedIntText(station.score)
                   << ":best=" << BoolDigit(station.bestByModuleScore)
                   << ":txGuard="
                   << BoolDigit(station.transceiverFrequencyGuard);
            summaries.push_back(stream.str());
        }
    }
    return summaries;
}

std::string BrainAirportCoveragePreviewSummaryText(
    const xvatsim::brain::BrainAirportCoverageDecisionPreview& preview) {
    std::ostringstream stream;
    stream << "authority="
           << (preview.summary.liveCandidatesBrainOwned
                   ? "brain-evidence"
                   : "old-candidates-fallback")
           << ",path=" << NoneToken(preview.summary.path)
           << ",evidence=" << preview.summary.evidenceControllerCount
           << ",oldSurvivors=" << preview.summary.oldSurvivorCount
           << ",previewSurvivors="
           << preview.summary.previewSurvivorCount
           << ",previewRejected="
           << preview.summary.previewRejectedCount
           << ",oldMismatch="
           << preview.summary.oldSurvivorMismatchCount
           << ",compatOnly="
           << BoolDigit(
                  preview.summary.resolverCandidatesCompatibilityOnly)
           << ",droppedBeforeBrain="
           << preview.summary.droppedBeforeBrainControllers;
    return stream.str();
}

std::vector<std::string> BrainAirportCoveragePreviewDecisionSummaries(
    const xvatsim::brain::BrainAirportCoverageDecisionPreview& preview) {
    std::vector<std::string> summaries;
    summaries.reserve(preview.decisions.size());
    for (const auto& decision : preview.decisions) {
        std::ostringstream stream;
        stream << decision.callsign;
        if (decision.hasStation) {
            stream << "[" << decision.stationIndex << "]";
        }
        stream << "@" << EmptyToken(decision.frequency)
               << ":" << decision.decision
               << ":" << decision.reason
               << ":old=" << BoolDigit(decision.matchesOldSurvivor);
        summaries.push_back(stream.str());
    }
    return summaries;
}

std::string BrainAuthorityStationsPreviewSummaryText(
    const xvatsim::brain::BrainAuthorityStationsDecisionPreview& preview) {
    std::ostringstream stream;
    stream << "authority="
           << (preview.summary.liveCandidatesBrainOwned
                   ? "brain-evidence"
                   : "old-candidates-fallback")
           << ",path=" << NoneToken(preview.summary.path)
           << ",evidence=" << preview.summary.evidenceControllerCount
           << ",oldSurvivors=" << preview.summary.oldSurvivorCount
           << ",previewSurvivors="
           << preview.summary.previewSurvivorCount
           << ",previewRejected="
           << preview.summary.previewRejectedCount
           << ",oldMismatch="
           << preview.summary.oldSurvivorMismatchCount
           << ",compatOnly="
           << BoolDigit(
                  preview.summary.resolverCandidatesCompatibilityOnly)
           << ",droppedBeforeBrain="
           << preview.summary.droppedBeforeBrainControllers;
    return stream.str();
}

std::vector<std::string> BrainAuthorityStationsPreviewDecisionSummaries(
    const xvatsim::brain::BrainAuthorityStationsDecisionPreview& preview) {
    std::vector<std::string> summaries;
    summaries.reserve(preview.decisions.size());
    for (const auto& decision : preview.decisions) {
        std::ostringstream stream;
        stream << decision.callsign;
        if (decision.hasStation) {
            stream << "[" << decision.stationIndex << "]";
        }
        stream << "@" << EmptyToken(decision.frequency)
               << ":" << decision.decision
               << ":" << decision.reason
               << ":old=" << BoolDigit(decision.matchesOldSurvivor);
        summaries.push_back(stream.str());
    }
    return summaries;
}

std::string BrainAuthorityRelevancePreviewSummaryText(
    const xvatsim::brain::BrainAuthorityRelevanceDecisionPreview& preview) {
    std::ostringstream stream;
    stream << "authority=" << preview.summary.authority
           << ",source=" << preview.summary.sourceControllerCount
           << ",evidence=" << preview.summary.evidenceControllerCount
           << ",compatRelevant="
           << preview.summary.compatibilityRelevantAuthorityCount
           << ",previewSurvivors=" << preview.summary.previewSurvivorCount
           << ",previewRejected=" << preview.summary.previewRejectedCount
           << ",oldMismatch="
           << preview.summary.oldSurvivorMismatchCount
           << ",droppedBeforeBrain="
           << preview.summary.droppedBeforeBrainControllers
           << ",compatOnly="
           << BoolDigit(preview.summary.relevantAuthoritiesCompatibilityOnly)
           << ",liveOwned="
           << BoolDigit(preview.summary.liveRelevantAuthoritiesBrainOwned);
    return stream.str();
}

std::vector<std::string> BrainAuthorityRelevancePreviewDecisionSummaries(
    const xvatsim::brain::BrainAuthorityRelevanceDecisionPreview& preview) {
    std::vector<std::string> summaries;
    summaries.reserve(preview.decisions.size());
    for (const auto& decision : preview.decisions) {
        std::ostringstream stream;
        stream << decision.evidenceKind
               << ":" << EmptyToken(decision.callsign)
               << ":" << EmptyToken(decision.authorityId)
               << ":" << EmptyToken(decision.polygonId)
               << ":" << EmptyToken(decision.polygonKey)
               << ":" << EmptyToken(decision.matchedPattern)
               << ":" << EmptyToken(decision.proofSource)
               << ":" << decision.decision
               << ":" << decision.reason
               << ":old=" << BoolDigit(decision.matchesOldSurvivor);
        summaries.push_back(stream.str());
    }
    std::sort(summaries.begin(), summaries.end());
    return summaries;
}

bool AddRouteWaypoint(
    std::vector<xvatsim::brain::RouteWaypointSnapshot>* waypoints,
    const std::string& value) {
    if (waypoints == nullptr) {
        return false;
    }

    xvatsim::brain::RouteWaypointSnapshot waypoint;
    bool hasIdent = false;
    bool hasLat = false;
    bool hasLon = false;
    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "ident") {
            waypoint.ident = fieldValue;
            hasIdent = true;
        } else if (field == "lat") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            waypoint.latitudeDeg = *parsed;
            hasLat = true;
        } else if (field == "lon") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            waypoint.longitudeDeg = *parsed;
            hasLon = true;
        }
    }

    if (!hasIdent || !hasLat || !hasLon) {
        return false;
    }

    waypoints->push_back(std::move(waypoint));
    return true;
}

bool AddGraphNodeEntry(
    xvatsim::core::route::AirwayGraph* graph,
    const std::string& value) {
    if (graph == nullptr) {
        return false;
    }

    std::string ident;
    std::string region;
    int navDataType = 0;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    bool hasIdent = false;
    bool hasRegion = false;
    bool hasType = false;
    bool hasLatitude = false;
    bool hasLongitude = false;

    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "ident") {
            ident = fieldValue;
            hasIdent = true;
        } else if (field == "region") {
            region = fieldValue;
            hasRegion = true;
        } else if (field == "type") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            navDataType = static_cast<int>(*parsed);
            hasType = true;
        } else if (field == "lat") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            latitudeDeg = *parsed;
            hasLatitude = true;
        } else if (field == "lon") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            longitudeDeg = *parsed;
            hasLongitude = true;
        }
    }

    if (!hasIdent || !hasRegion || !hasType || !hasLatitude || !hasLongitude) {
        return false;
    }

    xvatsim::core::route::AddGraphNode(
        ident,
        region,
        navDataType,
        latitudeDeg,
        longitudeDeg,
        graph);
    return true;
}

bool AddGraphEdgeEntry(
    xvatsim::core::route::AirwayGraph* graph,
    const std::string& value) {
    if (graph == nullptr) {
        return false;
    }

    std::string startIdent;
    std::string startRegion;
    int startNavDataType = 0;
    std::string endIdent;
    std::string endRegion;
    int endNavDataType = 0;
    std::string airwayName;
    std::string direction = "N";
    bool hasStartIdent = false;
    bool hasStartRegion = false;
    bool hasStartType = false;
    bool hasEndIdent = false;
    bool hasEndRegion = false;
    bool hasEndType = false;
    bool hasAirway = false;

    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "startIdent") {
            startIdent = fieldValue;
            hasStartIdent = true;
        } else if (field == "startRegion") {
            startRegion = fieldValue;
            hasStartRegion = true;
        } else if (field == "startType") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            startNavDataType = static_cast<int>(*parsed);
            hasStartType = true;
        } else if (field == "endIdent") {
            endIdent = fieldValue;
            hasEndIdent = true;
        } else if (field == "endRegion") {
            endRegion = fieldValue;
            hasEndRegion = true;
        } else if (field == "endType") {
            const auto parsed = ParseDouble(fieldValue);
            if (!parsed.has_value()) {
                return false;
            }
            endNavDataType = static_cast<int>(*parsed);
            hasEndType = true;
        } else if (field == "airway") {
            airwayName = fieldValue;
            hasAirway = true;
        } else if (field == "direction") {
            direction = ToUpperCopy(fieldValue);
        }
    }

    if (!hasStartIdent || !hasStartRegion || !hasStartType ||
        !hasEndIdent || !hasEndRegion || !hasEndType || !hasAirway) {
        return false;
    }

    const auto addForward = direction != "B";
    const auto addBackward = direction == "N" || direction == "B";
    return xvatsim::core::route::AddAirwayConnection(
        startIdent,
        startRegion,
        startNavDataType,
        endIdent,
        endRegion,
        endNavDataType,
        airwayName,
        addForward,
        addBackward,
        graph);
}

bool AddTraversalFeature(
    std::vector<xvatsim::core::route::SectorFeature>* features,
    const std::string& value) {
    if (features == nullptr) {
        return false;
    }

    xvatsim::core::route::SectorFeature feature;
    bool hasLabel = false;
    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "label") {
            feature.label = fieldValue;
            hasLabel = true;
        } else if (field == "tokens") {
            feature.tokens = Split(fieldValue, ',');
        } else if (field == "controllerPatterns" ||
                   field == "controllerCallsignPatterns") {
            feature.controllerCallsignPatterns = Split(fieldValue, ',');
        } else if (field == "controllerPrefixes") {
            feature.controllerPrefixes = Split(fieldValue, ',');
        } else if (field == "polygon") {
            xvatsim::core::route::SectorPolygon polygon;
            for (const auto& pointToken : Split(fieldValue, '|')) {
                const auto parts = Split(pointToken, ',');
                if (parts.size() != 2) {
                    return false;
                }
                const auto lat = ParseDouble(parts[0]);
                const auto lon = ParseDouble(parts[1]);
                if (!lat.has_value() || !lon.has_value()) {
                    return false;
                }
                polygon.ring.push_back({*lat, *lon});
            }
            if (polygon.ring.size() < 3) {
                return false;
            }
            feature.polygons.push_back(std::move(polygon));
        }
    }

    if (!hasLabel || feature.polygons.empty()) {
        return false;
    }
    if (feature.tokens.empty()) {
        feature.tokens.push_back(feature.label);
    }

    features->push_back(std::move(feature));
    return true;
}

bool AddTerminalCoverageFeature(
    std::vector<TerminalCoverageFeatureSpec>* features,
    const std::string& value) {
    if (features == nullptr) {
        return false;
    }

    TerminalCoverageFeatureSpec feature;
    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "id") {
            feature.id = fieldValue;
        } else if (field == "name") {
            feature.name = fieldValue;
        } else if (field == "suffix") {
            feature.suffix = fieldValue;
        } else if (field == "prefixes") {
            feature.prefixes = Split(fieldValue, ',');
        } else if (field == "polygon") {
            xvatsim::core::route::SectorPolygon polygon;
            for (const auto& pointToken : Split(fieldValue, '|')) {
                const auto parts = Split(pointToken, ',');
                if (parts.size() != 2) {
                    return false;
                }
                const auto lat = ParseDouble(parts[0]);
                const auto lon = ParseDouble(parts[1]);
                if (!lat.has_value() || !lon.has_value()) {
                    return false;
                }
                polygon.ring.push_back({*lat, *lon});
            }
            if (polygon.ring.size() < 3) {
                return false;
            }
            feature.polygons.push_back(std::move(polygon));
        }
    }

    if (feature.id.empty() || feature.polygons.empty()) {
        return false;
    }

    features->push_back(std::move(feature));
    return true;
}

bool AddCenterCoverageFeature(
    std::vector<CenterCoverageFeatureSpec>* features,
    const std::string& value) {
    if (features == nullptr) {
        return false;
    }

    CenterCoverageFeatureSpec feature;
    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "label") {
            feature.label = fieldValue;
        } else if (field == "name") {
            feature.name = fieldValue;
        } else if (field == "callsign") {
            feature.callsign = fieldValue;
        } else if (field == "tokens") {
            feature.tokens = Split(fieldValue, ',');
        } else if (field == "polygon") {
            xvatsim::core::route::SectorPolygon polygon;
            for (const auto& pointToken : Split(fieldValue, '|')) {
                const auto parts = Split(pointToken, ',');
                if (parts.size() != 2) {
                    return false;
                }
                const auto lat = ParseDouble(parts[0]);
                const auto lon = ParseDouble(parts[1]);
                if (!lat.has_value() || !lon.has_value()) {
                    return false;
                }
                polygon.ring.push_back({*lat, *lon});
            }
            if (polygon.ring.size() < 3) {
                return false;
            }
            feature.polygons.push_back(std::move(polygon));
        }
    }

    if ((feature.label.empty() && feature.name.empty()) || feature.polygons.empty()) {
        return false;
    }

    features->push_back(std::move(feature));
    return true;
}

bool AddAuthorityPolygonSourceRecord(
    std::vector<xvatsim::core::authority::AuthorityPolygonSourceRecord>* records,
    xvatsim::core::authority::AuthoritySource source,
    const std::string& value) {
    if (records == nullptr) {
        return false;
    }

    xvatsim::core::authority::AuthorityPolygonSourceRecord record;
    record.source = source;
    record.sourceRecord = value;

    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "id" || field == "key" || field == "label") {
            record.id = fieldValue;
        } else if (field == "name") {
            record.name = fieldValue;
        } else if (field == "suffix") {
            record.suffix = fieldValue;
        } else if (field == "prefixes" || field == "prefix") {
            record.prefixes = Split(fieldValue, ',');
        } else if (field == "tokens" || field == "lookupTokens") {
            record.lookupTokens = Split(fieldValue, ',');
        } else if (field == "polygon") {
            xvatsim::core::authority::AuthorityPolygonRing ring;
            for (const auto& pointToken : Split(fieldValue, '|')) {
                const auto parts = Split(pointToken, ',');
                if (parts.size() != 2) {
                    return false;
                }
                const auto lat = ParseDouble(parts[0]);
                const auto lon = ParseDouble(parts[1]);
                if (!lat.has_value() || !lon.has_value()) {
                    return false;
                }
                ring.points.push_back({*lat, *lon});
            }
            record.rings.push_back(std::move(ring));
        }
    }

    records->push_back(std::move(record));
    return true;
}

bool AddAuthorityPositionSourceRecord(
    std::vector<xvatsim::core::authority::AuthorityPositionSourceRecord>* records,
    xvatsim::core::authority::AuthoritySource source,
    const std::string& value) {
    if (records == nullptr) {
        return false;
    }

    xvatsim::core::authority::AuthorityPositionSourceRecord record;
    record.source = source;
    record.sourceRecord = value;

    for (const auto& part : Split(value, ';')) {
        const auto equalsIndex = part.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const auto field = Trim(part.substr(0, equalsIndex));
        const auto fieldValue = Trim(part.substr(equalsIndex + 1));
        if (field == "id" || field == "position" || field == "positionId") {
            record.id = fieldValue;
        } else if (field == "name") {
            record.name = fieldValue;
        } else if (field == "frequency") {
            record.frequency = fieldValue;
        } else if (field == "polygon" || field == "polygonKey" || field == "sector") {
            record.polygonKey = fieldValue;
        } else if (field == "patterns" || field == "callsigns" || field == "callsign") {
            record.controllerCallsignPatterns = Split(fieldValue, ',');
        } else if (field == "kind") {
            const auto parsedKind = ParseAuthorityKind(fieldValue);
            if (!parsedKind.has_value()) {
                return false;
            }
            record.kind = *parsedKind;
        }
    }

    records->push_back(std::move(record));
    return true;
}

std::string JsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const auto character : value) {
        if (character == '\\' || character == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(character);
    }
    return escaped;
}

std::string BuildBoundaryPayload(
    const std::vector<CenterCoverageFeatureSpec>& features) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(6);
    stream << "{\"type\":\"FeatureCollection\",\"features\":[";
    bool wroteFeature = false;
    for (const auto& feature : features) {
        if (feature.polygons.empty()) {
            continue;
        }
        if (wroteFeature) {
            stream << ",";
        }
        wroteFeature = true;
        stream << "{\"type\":\"Feature\",\"properties\":{";
        bool wroteProperty = false;
        if (!feature.label.empty()) {
            stream << "\"identifier\":\"" << JsonEscape(feature.label) << "\"";
            wroteProperty = true;
        }
        if (!feature.name.empty()) {
            if (wroteProperty) {
                stream << ",";
            }
            stream << "\"name\":\"" << JsonEscape(feature.name) << "\"";
            wroteProperty = true;
        }
        if (!feature.callsign.empty()) {
            if (wroteProperty) {
                stream << ",";
            }
            stream << "\"callsign\":\"" << JsonEscape(feature.callsign) << "\"";
            wroteProperty = true;
        }
        if (!feature.tokens.empty()) {
            if (wroteProperty) {
                stream << ",";
            }
            stream << "\"tokens\":\"";
            for (std::size_t tokenIndex = 0; tokenIndex < feature.tokens.size(); ++tokenIndex) {
                if (tokenIndex > 0) {
                    stream << " ";
                }
                stream << JsonEscape(feature.tokens[tokenIndex]);
            }
            stream << "\"";
        }
        stream << "},\"geometry\":{\"type\":\"Polygon\",\"coordinates\":[[";

        const auto& ring = feature.polygons.front().ring;
        for (std::size_t pointIndex = 0; pointIndex < ring.size(); ++pointIndex) {
            if (pointIndex > 0) {
                stream << ",";
            }
            stream << "[" << ring[pointIndex].longitudeDeg << ","
                   << ring[pointIndex].latitudeDeg << "]";
        }
        if (!ring.empty() &&
            (ring.front().latitudeDeg != ring.back().latitudeDeg ||
             ring.front().longitudeDeg != ring.back().longitudeDeg)) {
            stream << ",[" << ring.front().longitudeDeg << ","
                   << ring.front().latitudeDeg << "]";
        }
        stream << "]]}}";
    }
    stream << "]}";
    return wroteFeature ? stream.str() : std::string{};
}

std::string BuildTerminalBoundaryPayload(
    const std::vector<TerminalCoverageFeatureSpec>& features) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(6);
    stream << "{\"type\":\"FeatureCollection\",\"features\":[";
    for (std::size_t featureIndex = 0; featureIndex < features.size(); ++featureIndex) {
        const auto& feature = features[featureIndex];
        if (featureIndex > 0) {
            stream << ",";
        }
        stream << "{\"type\":\"Feature\",\"properties\":{";
        stream << "\"id\":\"" << JsonEscape(feature.id) << "\"";
        if (!feature.name.empty()) {
            stream << ",\"name\":\"" << JsonEscape(feature.name) << "\"";
        }
        if (!feature.suffix.empty()) {
            stream << ",\"suffix\":\"" << JsonEscape(feature.suffix) << "\"";
        }
        stream << ",\"prefix\":[";
        for (std::size_t prefixIndex = 0; prefixIndex < feature.prefixes.size(); ++prefixIndex) {
            if (prefixIndex > 0) {
                stream << ",";
            }
            stream << "\"" << JsonEscape(feature.prefixes[prefixIndex]) << "\"";
        }
        stream << "]},\"geometry\":{\"type\":\"Polygon\",\"coordinates\":[[";

        const auto& ring = feature.polygons.front().ring;
        for (std::size_t pointIndex = 0; pointIndex < ring.size(); ++pointIndex) {
            if (pointIndex > 0) {
                stream << ",";
            }
            stream << "[" << ring[pointIndex].longitudeDeg << ","
                   << ring[pointIndex].latitudeDeg << "]";
        }
        if (!ring.empty() &&
            (ring.front().latitudeDeg != ring.back().latitudeDeg ||
             ring.front().longitudeDeg != ring.back().longitudeDeg)) {
            stream << ",[" << ring.front().longitudeDeg << ","
                   << ring.front().latitudeDeg << "]";
        }
        stream << "]]}}";
    }
    stream << "]}";
    return stream.str();
}

std::string CsvEscape(const std::string& value) {
    std::string escaped = "\"";
    for (const auto character : value) {
        if (character == '"') {
            escaped += "\"\"";
        } else {
            escaped.push_back(character);
        }
    }
    escaped += "\"";
    return escaped;
}

std::string BuildAirportFrequencyFrqCsvPayload(
    const std::vector<std::string>& rows) {
    if (rows.empty()) {
        return {};
    }

    std::ostringstream stream;
    stream << "\"FACILITY\",\"FACILITY_TYPE\",\"SERVICED_FACILITY\","
              "\"TOWER_OR_COMM_CALL\",\"PRIMARY_APPROACH_RADIO_CALL\","
              "\"FREQ\",\"SECTORIZATION\",\"FREQ_USE\",\"REMARK\"\n";
    for (const auto& row : rows) {
        const auto fields = Split(row, ';');
        std::unordered_map<std::string, std::string> values;
        for (const auto& field : fields) {
            const auto separator = field.find('=');
            if (separator == std::string::npos) {
                continue;
            }
            values[ToUpperCopy(Trim(field.substr(0, separator)))] =
                Trim(field.substr(separator + 1));
        }

        const std::vector<std::string> columnNames = {
            "FACILITY",
            "FACILITY_TYPE",
            "SERVICED_FACILITY",
            "TOWER_OR_COMM_CALL",
            "PRIMARY_APPROACH_RADIO_CALL",
            "FREQ",
            "SECTORIZATION",
            "FREQ_USE",
            "REMARK"};
        for (std::size_t index = 0; index < columnNames.size(); ++index) {
            if (index > 0) {
                stream << ",";
            }
            const auto found = values.find(columnNames[index]);
            stream << CsvEscape(found != values.end() ? found->second : "");
        }
        stream << "\n";
    }
    return stream.str();
}

std::string BuildAuthorityCatalogPayload(const std::vector<std::string>& firLines) {
    if (firLines.empty()) {
        return {};
    }

    std::ostringstream stream;
    stream << "[FIRs]\n";
    for (const auto& line : firLines) {
        stream << line << "\n";
    }
    return stream.str();
}

std::string BuildAuthorityCompilerPayload(
    const std::vector<std::string>& firLines,
    const std::vector<std::string>& uirLines) {
    if (firLines.empty() && uirLines.empty()) {
        return {};
    }

    std::ostringstream stream;
    if (!firLines.empty()) {
        stream << "[FIRs]\n";
        for (const auto& line : firLines) {
            stream << line << "\n";
        }
    }
    if (!uirLines.empty()) {
        stream << "[UIRs]\n";
        for (const auto& line : uirLines) {
            stream << line << "\n";
        }
    }
    return stream.str();
}

bool LoadScenario(const std::filesystem::path& path, ScenarioData* scenario, std::string* outError) {
    if (scenario == nullptr) {
        return false;
    }

    std::ifstream stream(path);
    if (!stream.is_open()) {
        if (outError != nullptr) {
            *outError = "Unable to open scenario file";
        }
        return false;
    }

    std::string line;
    int lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;
        const auto commentIndex = line.find('#');
        if (commentIndex != std::string::npos) {
            line.erase(commentIndex);
        }

        line = Trim(line);
        if (line.empty()) {
            continue;
        }

        const auto equalsIndex = line.find('=');
        if (equalsIndex == std::string::npos) {
            if (outError != nullptr) {
                *outError = "Line " + std::to_string(lineNumber) + " is missing '='";
            }
            return false;
        }

        const auto key = Trim(line.substr(0, equalsIndex));
        const auto value = Trim(line.substr(equalsIndex + 1));
        if (key == "departure.station") {
            scenario->departureBoard.source = BoardSource::Departure;
            if (!AddStation(&scenario->departureBoard, value)) {
                if (outError != nullptr) {
                    *outError = "Invalid departure.station at line " + std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "arrival.station") {
            scenario->arrivalBoard.source = BoardSource::Arrival;
            if (!AddStation(&scenario->arrivalBoard, value)) {
                if (outError != nullptr) {
                    *outError = "Invalid arrival.station at line " + std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "enroute.station") {
            scenario->enrouteBoard.source = BoardSource::Enroute;
            if (!AddStation(&scenario->enrouteBoard, value)) {
                if (outError != nullptr) {
                    *outError = "Invalid enroute.station at line " + std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "departure.coverage.sector") {
            if (!AddRouteSector(
                    &scenario->departureAirportSectorSnapshot.coveringSectors,
                    value)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid departure.coverage.sector at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->departureAirportSectorSnapshot.available = true;
            scenario->departureAirportSectorSnapshot.stale = false;
            continue;
        }
        if (key == "arrival.coverage.sector") {
            if (!AddRouteSector(
                    &scenario->arrivalAirportSectorSnapshot.coveringSectors,
                    value)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid arrival.coverage.sector at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->arrivalAirportSectorSnapshot.available = true;
            scenario->arrivalAirportSectorSnapshot.stale = false;
            continue;
        }
        if (key == "airport.terminal_feature") {
            if (!AddTerminalCoverageFeature(
                    &scenario->airportCoverageTerminalFeatures,
                    value)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid airport.terminal_feature at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "airport.pending_terminal_feature") {
            if (!AddTerminalCoverageFeature(
                    &scenario->pendingAirportCoverageTerminalFeatures,
                    value)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid airport.pending_terminal_feature at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->hasPendingAirportCoveragePayloads = true;
            continue;
        }
        if (key == "airport.center_feature") {
            if (!AddCenterCoverageFeature(&scenario->airportCoverageCenterFeatures, value)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid airport.center_feature at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "airport.pending_center_feature") {
            if (!AddCenterCoverageFeature(
                    &scenario->pendingAirportCoverageCenterFeatures,
                    value)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid airport.pending_center_feature at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->hasPendingAirportCoveragePayloads = true;
            continue;
        }
        if (key == "route.current_sector") {
            if (!AddRouteSector(&scenario->routeSectorSnapshot.currentSectors, value)) {
                if (outError != nullptr) {
                    *outError = "Invalid route.current_sector at line " + std::to_string(lineNumber);
                }
                return false;
            }
            scenario->routeSectorSnapshot.available = true;
            scenario->routeSectorSnapshot.stale = false;
            scenario->routeSectorSnapshot.routeResolved = true;
            continue;
        }
        if (key == "route.next_sector") {
            if (!AddRouteSector(&scenario->routeSectorSnapshot.nextSectors, value)) {
                if (outError != nullptr) {
                    *outError = "Invalid route.next_sector at line " + std::to_string(lineNumber);
                }
                return false;
            }
            scenario->routeSectorSnapshot.available = true;
            scenario->routeSectorSnapshot.stale = false;
            scenario->routeSectorSnapshot.routeResolved = true;
            continue;
        }
        if (key == "terminal_authority.airport_icao") {
            scenario->terminalAuthorityAirportIcao = value;
            continue;
        }
        if (key == "terminal_authority.lat") {
            const auto parsed = ParseDouble(value);
            if (!parsed.has_value()) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid terminal_authority.lat at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->terminalAuthorityLatitudeDeg = *parsed;
            scenario->hasTerminalAuthorityCoordinates = true;
            continue;
        }
        if (key == "terminal_authority.lon") {
            const auto parsed = ParseDouble(value);
            if (!parsed.has_value()) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid terminal_authority.lon at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->terminalAuthorityLongitudeDeg = *parsed;
            scenario->hasTerminalAuthorityCoordinates = true;
            continue;
        }
        if (key == "terminal_authority.feature") {
            if (!AddTerminalCoverageFeature(
                    &scenario->terminalAuthorityFeatures,
                    value)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid terminal_authority.feature at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "airport_frequency.frq_row") {
            scenario->airportFrequencyFrqRows.push_back(value);
            continue;
        }
        if (key == "brain_controller_relevance.stage") {
            const auto parsed = ParseWorkflowStage(value);
            if (!parsed.has_value()) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid brain_controller_relevance.stage at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->controllerRelevanceWorkflowStage = *parsed;
            continue;
        }
        if (key == "controller.entry") {
            if (!AddController(&scenario->controllers, value)) {
                if (outError != nullptr) {
                    *outError = "Invalid controller.entry at line " + std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "controller.feed_generation") {
            const auto parsed = ParseDouble(value);
            if (!parsed.has_value() || *parsed < 0.0) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid controller.feed_generation at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->controllerFeedGeneration =
                static_cast<std::uint64_t>(*parsed);
            continue;
        }
        if (key == "resolver.authority_repeat_controller.entry") {
            if (!AddController(
                    &scenario->resolverAuthorityRepeatControllers,
                    value)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid resolver.authority_repeat_controller.entry at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "resolver.authority_repeat_controller.feed_generation") {
            const auto parsed = ParseDouble(value);
            if (!parsed.has_value() || *parsed < 0.0) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid resolver.authority_repeat_controller.feed_generation at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->resolverAuthorityRepeatControllerFeedGeneration =
                static_cast<std::uint64_t>(*parsed);
            continue;
        }
        if (key == "resolver.authority_repeat_controller.replace") {
            if (!ParseBool(
                    value,
                    &scenario->resolverAuthorityRepeatReplaceControllers)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid resolver.authority_repeat_controller.replace at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "resolver.authority_repeat_cache_age_seconds") {
            const auto parsed = ParseDouble(value);
            if (!parsed.has_value() || *parsed < 0.0) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid resolver.authority_repeat_cache_age_seconds at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->resolverAuthorityRepeatCacheAgeSeconds =
                static_cast<long long>(*parsed);
            continue;
        }
        if (key == "resolver.authority_repeat_aircraft.valid") {
            scenario->hasResolverAuthorityRepeatAircraftState = true;
            if (!ParseBool(
                    value,
                    &scenario->resolverAuthorityRepeatAircraftState.valid)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid resolver.authority_repeat_aircraft.valid at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "resolver.authority_repeat_aircraft.on_ground") {
            scenario->hasResolverAuthorityRepeatAircraftState = true;
            if (!ParseBool(
                    value,
                    &scenario->resolverAuthorityRepeatAircraftState.onGround)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid resolver.authority_repeat_aircraft.on_ground at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "resolver.authority_repeat_aircraft.latitude_deg") {
            const auto parsed = ParseDouble(value);
            if (!parsed.has_value()) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid resolver.authority_repeat_aircraft.latitude_deg at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->hasResolverAuthorityRepeatAircraftState = true;
            scenario->resolverAuthorityRepeatAircraftState.latitudeDeg = *parsed;
            continue;
        }
        if (key == "resolver.authority_repeat_aircraft.longitude_deg") {
            const auto parsed = ParseDouble(value);
            if (!parsed.has_value()) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid resolver.authority_repeat_aircraft.longitude_deg at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->hasResolverAuthorityRepeatAircraftState = true;
            scenario->resolverAuthorityRepeatAircraftState.longitudeDeg = *parsed;
            continue;
        }
        if (key == "controller.feed_available") {
            bool parsed = false;
            if (!ParseBool(value, &parsed)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid controller.feed_available at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->controllerFeedAvailable = parsed;
            continue;
        }
        if (key == "controller.feed_stale") {
            if (!ParseBool(value, &scenario->controllerFeedStale)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid controller.feed_stale at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "controller.feed_force_entries") {
            if (!ParseBool(value, &scenario->forceControllerFeedEntries)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid controller.feed_force_entries at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "transceiver.available") {
            if (!ParseBool(value, &scenario->transceiverResolutionSnapshot.available)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid transceiver.available at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "transceiver.stale") {
            if (!ParseBool(value, &scenario->transceiverResolutionSnapshot.stale)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid transceiver.stale at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "transceiver.status") {
            scenario->transceiverResolutionSnapshot.statusLine = value;
            continue;
        }
        if (key == "transceiver.candidate") {
            if (!AddTransceiverCandidate(&scenario->transceiverResolutionSnapshot, value)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid transceiver.candidate at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "overlay.stage") {
            scenario->overlayWorkflowStage = ParseWorkflowStage(value);
            if (!scenario->overlayWorkflowStage.has_value()) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid overlay.stage at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "display_intent.stage") {
            scenario->displayIntentWorkflowStage = ParseWorkflowStage(value);
            if (!scenario->displayIntentWorkflowStage.has_value()) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid display_intent.stage at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "display_intent.progress_nm") {
            const auto parsed = ParseDouble(value);
            if (!parsed.has_value()) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid display_intent.progress_nm at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->displayIntentRouteProgressNm = *parsed;
            continue;
        }
        if (key == "display_intent.current_polygon") {
            scenario->displayIntentCurrentPolygonKey = value;
            continue;
        }
        if (key == "display_intent.next_polygon") {
            scenario->displayIntentNextPolygonKey = value;
            continue;
        }
        if (key == "display_intent.arrival_polygon") {
            scenario->displayIntentArrivalPolygonKey = value;
            continue;
        }
        if (key == "display_intent.relation") {
            if (!AddDisplayRelationFact(
                    &scenario->displayIntentRelationFacts,
                    value)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid display_intent.relation at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "ctaf_unicom.probe") {
            if (!ParseBool(value, &scenario->ctafUnicomPublisherProbe)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid ctaf_unicom.probe at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "ctaf_unicom.stage") {
            scenario->ctafUnicomPublisherStage = ParseWorkflowStage(value);
            if (!scenario->ctafUnicomPublisherStage.has_value()) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid ctaf_unicom.stage at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "ctaf_unicom.product_plan_key") {
            scenario->ctafUnicomPublisherProductPlanKey = value;
            scenario->ctafUnicomPublisherProbe = true;
            continue;
        }
        if (key == "ctaf_unicom.accept_board_rows") {
            if (!ParseBool(
                    value,
                    &scenario->ctafUnicomPublisherAcceptBoardRows)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid ctaf_unicom.accept_board_rows at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->ctafUnicomPublisherProbe = true;
            continue;
        }
        if (key == "ctaf_unicom.departure") {
            scenario->ctafUnicomPublisherProbe = true;
            if (!ParseCtafLookupFact(
                    value,
                    &scenario->ctafUnicomDepartureFact)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid ctaf_unicom.departure at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "ctaf_unicom.arrival") {
            scenario->ctafUnicomPublisherProbe = true;
            if (!ParseCtafLookupFact(
                    value,
                    &scenario->ctafUnicomArrivalFact)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid ctaf_unicom.arrival at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "ctaf_unicom.omit_departure_source_evidence") {
            if (!ParseBool(
                    value,
                    &scenario->ctafUnicomOmitDepartureSourceEvidence)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid ctaf_unicom.omit_departure_source_evidence at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->ctafUnicomPublisherProbe = true;
            continue;
        }
        if (key == "ctaf_unicom.omit_arrival_source_evidence") {
            if (!ParseBool(
                    value,
                    &scenario->ctafUnicomOmitArrivalSourceEvidence)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid ctaf_unicom.omit_arrival_source_evidence at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->ctafUnicomPublisherProbe = true;
            continue;
        }
        if (key == "ctaf_unicom.omit_departure_advisory_decision") {
            if (!ParseBool(
                    value,
                    &scenario->ctafUnicomOmitDepartureAdvisoryDecision)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid ctaf_unicom.omit_departure_advisory_decision at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->ctafUnicomPublisherProbe = true;
            continue;
        }
        if (key == "ctaf_unicom.omit_arrival_advisory_decision") {
            if (!ParseBool(
                    value,
                    &scenario->ctafUnicomOmitArrivalAdvisoryDecision)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid ctaf_unicom.omit_arrival_advisory_decision at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->ctafUnicomPublisherProbe = true;
            continue;
        }
        if (key ==
            "ctaf_unicom.incomplete_departure_advisory_decision") {
            if (!ParseBool(
                    value,
                    &scenario
                         ->ctafUnicomIncompleteDepartureAdvisoryDecision)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid ctaf_unicom.incomplete_departure_advisory_decision at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->ctafUnicomPublisherProbe = true;
            continue;
        }
        if (key == "ctaf_unicom.incomplete_arrival_advisory_decision") {
            if (!ParseBool(
                    value,
                    &scenario
                         ->ctafUnicomIncompleteArrivalAdvisoryDecision)) {
                if (outError != nullptr) {
                    *outError =
                        "Invalid ctaf_unicom.incomplete_arrival_advisory_decision at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            scenario->ctafUnicomPublisherProbe = true;
            continue;
        }
        if (key == "route.waypoint") {
            if (!AddRouteWaypoint(&scenario->routeWaypoints, value)) {
                if (outError != nullptr) {
                    *outError = "Invalid route.waypoint at line " + std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "graph.node") {
            if (!AddGraphNodeEntry(&scenario->routeGraph, value)) {
                if (outError != nullptr) {
                    *outError = "Invalid graph.node at line " + std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "graph.edge") {
            if (!AddGraphEdgeEntry(&scenario->routeGraph, value)) {
                if (outError != nullptr) {
                    *outError = "Invalid graph.edge at line " + std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "graph.fix_payload") {
            scenario->routeGraphFixPayload += value;
            scenario->routeGraphFixPayload.push_back('\n');
            continue;
        }
        if (key == "graph.nav_payload") {
            scenario->routeGraphNavPayload += value;
            scenario->routeGraphNavPayload.push_back('\n');
            continue;
        }
        if (key == "graph.airway_payload") {
            scenario->routeGraphAirwayPayload += value;
            scenario->routeGraphAirwayPayload.push_back('\n');
            continue;
        }
        if (key == "procedure.entry") {
            auto addProcedureEntry = [&](const std::string& rawName,
                                         bool hasSid,
                                         bool hasStar,
                                         const std::string& source,
                                         const std::string& transition,
                                         const std::string& authority,
                                         const std::vector<std::string>& fixes) {
                const auto normalizedName =
                    xvatsim::core::route::ExtractRouteTokenBase(rawName);
                if (normalizedName.empty()) {
                    return false;
                }

                auto& entry = scenario->proceduresByName[normalizedName];
                if (!hasSid && !hasStar) {
                    entry.hasSid = true;
                    entry.hasStar = true;
                } else {
                    entry.hasSid = entry.hasSid || hasSid;
                    entry.hasStar = entry.hasStar || hasStar;
                }

                const auto normalizedSource = ToUpperCopy(Trim(source));
                if (normalizedSource.empty() || normalizedSource == "BOTH") {
                    entry.sourcedFromDepartureAirport = true;
                    entry.sourcedFromArrivalAirport = true;
                } else if (normalizedSource == "DEPARTURE" || normalizedSource == "DEP") {
                    entry.sourcedFromDepartureAirport = true;
                } else if (normalizedSource == "ARRIVAL" || normalizedSource == "ARR") {
                    entry.sourcedFromArrivalAirport = true;
                } else {
                    return false;
                }

                const auto normalizedAuthority = ToUpperCopy(Trim(authority));
                const auto effectiveAuthority =
                    normalizedAuthority.empty() ? std::string("PROC") : normalizedAuthority;
                if (hasSid) {
                    entry.sidAuthoritySources.insert(effectiveAuthority);
                }
                if (hasStar) {
                    entry.starAuthoritySources.insert(effectiveAuthority);
                }
                if (!hasSid && !hasStar) {
                    entry.sidAuthoritySources.insert(effectiveAuthority);
                    entry.starAuthoritySources.insert(effectiveAuthority);
                }

                for (const auto& rawFix : fixes) {
                    const auto normalizedFix =
                        xvatsim::core::route::ExtractRouteTokenBase(rawFix);
                    if (normalizedFix.empty() ||
                        xvatsim::core::route::IsRouteControlToken(normalizedFix) ||
                        xvatsim::core::route::IsRunwayProcedureSegmentToken(rawFix)) {
                        continue;
                    }

                    if (hasSid) {
                        if (entry.sidFixes.insert(normalizedFix).second) {
                            entry.sidOrderedFixes.push_back(normalizedFix);
                        }
                    }
                    if (hasStar) {
                        if (entry.starFixes.insert(normalizedFix).second) {
                            entry.starOrderedFixes.push_back(normalizedFix);
                        }
                    }
                    if (!hasSid && !hasStar) {
                        if (entry.sidFixes.insert(normalizedFix).second) {
                            entry.sidOrderedFixes.push_back(normalizedFix);
                        }
                        if (entry.starFixes.insert(normalizedFix).second) {
                            entry.starOrderedFixes.push_back(normalizedFix);
                        }
                    }
                }

                const auto normalizedTransition =
                    xvatsim::core::route::ExtractRouteTokenBase(transition);
                if (!normalizedTransition.empty()) {
                    const auto isRunwayRecord =
                        xvatsim::core::route::IsRunwayProcedureSegmentToken(transition);
                    if (entry.hasSid && hasSid) {
                        if (isRunwayRecord) {
                            entry.hasSidRunwayRecords = true;
                            entry.sidRunwayTransitions.insert(normalizedTransition);
                        } else {
                            entry.sidTransitions.insert(normalizedTransition);
                        }
                    }
                    if (entry.hasStar && hasStar) {
                        if (isRunwayRecord) {
                            entry.hasStarRunwayRecords = true;
                            entry.starRunwayTransitions.insert(normalizedTransition);
                        } else {
                            entry.starTransitions.insert(normalizedTransition);
                        }
                    }
                    if (!hasSid && !hasStar) {
                        if (isRunwayRecord) {
                            entry.hasSidRunwayRecords = true;
                            entry.hasStarRunwayRecords = true;
                            entry.sidRunwayTransitions.insert(normalizedTransition);
                            entry.starRunwayTransitions.insert(normalizedTransition);
                        } else {
                            entry.sidTransitions.insert(normalizedTransition);
                            entry.starTransitions.insert(normalizedTransition);
                        }
                    }
                }
                return true;
            };

            if (value.find('=') == std::string::npos) {
                if (!addProcedureEntry(value, false, false, {}, {}, {}, {})) {
                    if (outError != nullptr) {
                        *outError = "Invalid procedure.entry at line " + std::to_string(lineNumber);
                    }
                    return false;
                }
                continue;
            }

            std::string name;
            std::string source;
            std::string transition;
            std::string authority;
            std::vector<std::string> fixes;
            bool hasSid = false;
            bool hasStar = false;
            for (const auto& part : Split(value, ';')) {
                const auto separator = part.find('=');
                if (separator == std::string::npos) {
                    continue;
                }
                const auto partKey = ToUpperCopy(Trim(part.substr(0, separator)));
                const auto partValue = Trim(part.substr(separator + 1));
                if (partKey == "NAME") {
                    name = partValue;
                } else if (partKey == "TYPE") {
                    const auto normalizedType = ToUpperCopy(partValue);
                    if (normalizedType == "SID") {
                        hasSid = true;
                    } else if (normalizedType == "STAR") {
                        hasStar = true;
                    } else if (normalizedType == "BOTH") {
                        hasSid = true;
                        hasStar = true;
                    }
                } else if (partKey == "SOURCE") {
                    source = partValue;
                } else if (partKey == "TRANSITION") {
                    transition = partValue;
                } else if (partKey == "AUTHORITY") {
                    authority = partValue;
                } else if (partKey == "FIX") {
                    fixes.push_back(partValue);
                } else if (partKey == "FIXES") {
                    const auto fixParts = Split(partValue, '|');
                    fixes.insert(fixes.end(), fixParts.begin(), fixParts.end());
                }
            }

            if (!addProcedureEntry(name, hasSid, hasStar, source, transition, authority, fixes)) {
                if (outError != nullptr) {
                    *outError = "Invalid procedure.entry at line " + std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }
        if (key == "feature.entry") {
            if (!AddTraversalFeature(&scenario->traversalFeatures, value)) {
                if (outError != nullptr) {
                    *outError = "Invalid feature.entry at line " + std::to_string(lineNumber);
                }
                return false;
            }
            continue;
        }

        if (!AssignScenarioProperty(scenario, key, value)) {
            if (outError != nullptr) {
                *outError = "Unknown or invalid property at line " + std::to_string(lineNumber) +
                            ": " + key;
            }
            return false;
        }
    }

    if (scenario->name.empty()) {
        scenario->name = path.stem().string();
    }

    if (!scenario->routeGraphFixPayload.empty() ||
        !scenario->routeGraphNavPayload.empty() ||
        !scenario->routeGraphAirwayPayload.empty()) {
        scenario->routeGraph = xvatsim::core::route::BuildAirwayGraphFromPayloads(
            scenario->routeGraphFixPayload,
            scenario->routeGraphNavPayload,
            scenario->routeGraphAirwayPayload);
    }

    if (scenario->departureAirportSectorSnapshot.airportIcao.empty()) {
        scenario->departureAirportSectorSnapshot.airportIcao =
            scenario->workflowState.flightContext.departureIcao;
    }
    if (scenario->arrivalAirportSectorSnapshot.airportIcao.empty()) {
        scenario->arrivalAirportSectorSnapshot.airportIcao =
            scenario->workflowState.flightContext.destinationIcao;
    }
    if (scenario->routeSectorSnapshot.departureIcao.empty()) {
        scenario->routeSectorSnapshot.departureIcao =
            scenario->workflowState.flightContext.departureIcao;
    }
    if (scenario->routeSectorSnapshot.destinationIcao.empty()) {
        scenario->routeSectorSnapshot.destinationIcao =
            scenario->workflowState.flightContext.destinationIcao;
    }

    return true;
}

int PrintMismatch(const std::string& label, const std::string& expected, const std::string& actual) {
    std::cerr << "Mismatch: " << label << " expected [" << expected << "] actual [" << actual << "]\n";
    return 1;
}

bool IsExplicitNoneList(const std::vector<std::string>& values) {
    return values.size() == 1 && ToUpperCopy(values.front()) == "<NONE>";
}

std::string JoinCsv(const std::vector<std::string>& values) {
    std::ostringstream joined;
    for (const auto& value : values) {
        if (joined.tellp() > 0) {
            joined << ",";
        }
        joined << value;
    }
    return joined.str();
}

std::optional<int> CheckStringList(
    const char* label,
    const std::vector<std::string>& expectedValues,
    const std::vector<std::string>& actualValues) {
    if (expectedValues.empty()) {
        return std::nullopt;
    }

    if (IsExplicitNoneList(expectedValues)) {
        if (actualValues.empty()) {
            return std::nullopt;
        }
    } else if (expectedValues == actualValues) {
        return std::nullopt;
    }

    return PrintMismatch(label, JoinCsv(expectedValues), JoinCsv(actualValues));
}

std::optional<int> CheckStringListContains(
    const char* label,
    const std::vector<std::string>& expectedSubstrings,
    const std::vector<std::string>& actualValues) {
    if (expectedSubstrings.empty()) {
        return std::nullopt;
    }

    const auto actual = JoinCsv(actualValues);
    std::vector<std::string> missing;
    for (const auto& expectedSubstring : expectedSubstrings) {
        if (actual.find(expectedSubstring) == std::string::npos) {
            missing.push_back(expectedSubstring);
        }
    }
    if (missing.empty()) {
        return std::nullopt;
    }

    return PrintMismatch(label, JoinCsv(missing), actual);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: XVatsimRegressionHarness <scenario-file>\n";
        return 2;
    }

    ScenarioData scenario;
    std::string error;
    if (!LoadScenario(argv[1], &scenario, &error)) {
        std::cerr << "Failed to load scenario: " << error << "\n";
        return 2;
    }

    auto workflowState = scenario.workflowState;
    const auto handoffDecision = xvatsim::core::workflow::ResolveWorkflowStage(
        scenario.aircraftState,
        scenario.radioStateSnapshot,
        scenario.departureTerminalCoverageKnown,
        scenario.insideDepartureTerminalCoverage,
        scenario.departureBoard,
        scenario.enrouteBoard,
        scenario.nowSeconds,
        &workflowState,
        scenario.tuning);
    const auto departureLocationConfirmed =
        xvatsim::core::workflow::CanConfirmDepartureLocation(
            scenario.aircraftState,
            scenario.flightPlanSnapshot,
            scenario.networkPlanSnapshot,
            scenario.tuning);
    xvatsim::core::workflow::RecoveryDecision recoveryDecision;
    if (scenario.recoveryRequested) {
        recoveryDecision =
            xvatsim::core::workflow::ResolveCurrentFlightRecovery(
                scenario.aircraftState,
                scenario.flightPlanSnapshot,
                scenario.networkPlanSnapshot,
                scenario.workflowState.flightContext,
                scenario.recoveryMode,
                scenario.tuning);
    }

    xvatsim::brain::BrainDisplayIntentInput displayIntentInput;
    displayIntentInput.workflowStage =
        scenario.displayIntentWorkflowStage.value_or(handoffDecision.stage);
    displayIntentInput.routeProgressDistanceNm =
        scenario.displayIntentRouteProgressNm;
    displayIntentInput.currentPolygonKey =
        scenario.displayIntentCurrentPolygonKey;
    displayIntentInput.nextPolygonKey =
        scenario.displayIntentNextPolygonKey;
    displayIntentInput.arrivalPolygonKey =
        scenario.displayIntentArrivalPolygonKey;
    displayIntentInput.sourceOwnedFallbackStableKeyShadowEnabled =
        scenario.sourceOwnedFallbackStableKeyShadowEnabled;
    displayIntentInput.sourceOwnedFallbackStableKeyShadowGateSource =
        scenario.sourceOwnedFallbackStableKeyShadowGateSource;
    displayIntentInput
        .sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled =
        scenario
            .sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled;
    displayIntentInput
        .sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource =
        scenario
            .sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource;
    displayIntentInput.sourceOwnedFallbackStableKeyLiveConsumptionEnabled =
        scenario.sourceOwnedFallbackStableKeyLiveConsumptionEnabled;
    displayIntentInput.sourceOwnedFallbackStableKeyLiveConsumptionGateSource =
        scenario.sourceOwnedFallbackStableKeyLiveConsumptionGateSource;
    displayIntentInput.radios = scenario.radioStateSnapshot;
    displayIntentInput.departureBoard = scenario.departureBoard;
    displayIntentInput.arrivalBoard = scenario.arrivalBoard;
    displayIntentInput.enrouteBoard = scenario.enrouteBoard;
    displayIntentInput.relationFacts = scenario.displayIntentRelationFacts;
    const auto displayIntentOutput =
        xvatsim::brain::RunBrainDisplayIntentWorker(displayIntentInput);
    auto displayBoard = displayIntentOutput.finalDisplay;
    xvatsim::brain::BrainOwnedStandbyAssistPlanOutput standbyPlan;
    xvatsim::brain::BrainOwnedStandbyAssistSideEffectDecision
        standbySideEffectDecision;

    xvatsim::brain::ControllerFeedSnapshot controllerFeedSnapshot;
    controllerFeedSnapshot.generation = scenario.controllerFeedGeneration;
    controllerFeedSnapshot.stale = scenario.controllerFeedStale;
    controllerFeedSnapshot.available =
        scenario.controllerFeedAvailable.value_or(!scenario.controllers.empty());
    if (controllerFeedSnapshot.stale) {
        controllerFeedSnapshot.available = false;
    }
    if ((controllerFeedSnapshot.available && !controllerFeedSnapshot.stale) ||
        scenario.forceControllerFeedEntries) {
        controllerFeedSnapshot.connectedControllers =
            static_cast<int>(scenario.controllers.size());
        controllerFeedSnapshot.controllers = &scenario.controllers;
    }

    const auto vatSpyAuthorityCatalog =
        xvatsim::core::authority::CompileVatSpyAuthorityCatalog(
            BuildAuthorityCompilerPayload(
                scenario.authorityCatalogFirLines,
                scenario.authorityCatalogUirLines));
    const auto positionAuthorityCatalog =
        xvatsim::core::authority::CompileAuthorityPositionCatalog(
            scenario.authorityPositionRecords);
    const auto authorityCatalog =
        xvatsim::core::authority::MergeControllerAuthorityCatalogs(
            vatSpyAuthorityCatalog,
            positionAuthorityCatalog);
    const auto authorityPolygonCatalog =
        xvatsim::core::authority::CompileAuthorityPolygons(
            scenario.authorityPolygonRecords);
    std::vector<xvatsim::core::authority::ActiveControllerAuthority>
        activeAuthorityMatches;
    std::vector<xvatsim::core::authority::ActiveAuthorityPolygon>
        activeAuthorityPolygons;
    std::vector<xvatsim::core::authority::AuthorityDataGap>
        activeAuthorityPolygonDataGaps;
    std::vector<std::string> authorityUnmappedCallsigns;
    if (!scenario.authorityCatalogFirLines.empty() ||
        !scenario.authorityCatalogUirLines.empty() ||
        !scenario.authorityPositionRecords.empty()) {
        for (const auto& controller : scenario.controllers) {
            const auto matches = xvatsim::core::authority::ResolveControllerAuthority(
                authorityCatalog,
                controller.callsign,
                controller.frequency,
                controller.facility);
            if (matches.empty()) {
                authorityUnmappedCallsigns.push_back(
                    xvatsim::core::authority::NormalizeControllerCallsign(
                        controller.callsign));
                continue;
            }
            activeAuthorityMatches.insert(
                activeAuthorityMatches.end(),
                matches.begin(),
                matches.end());
            const auto activationResult =
                xvatsim::core::authority::ActivateAuthorityPolygons(
                    authorityCatalog,
                    authorityPolygonCatalog,
                    controller.callsign,
                    controller.frequency,
                    controller.facility);
            activeAuthorityPolygons.insert(
                activeAuthorityPolygons.end(),
                activationResult.activePolygons.begin(),
                activationResult.activePolygons.end());
            activeAuthorityPolygonDataGaps.insert(
                activeAuthorityPolygonDataGaps.end(),
                activationResult.dataGaps.begin(),
                activationResult.dataGaps.end());
        }
        std::sort(
            authorityUnmappedCallsigns.begin(),
            authorityUnmappedCallsigns.end());
    }
    std::vector<xvatsim::core::authority::GeoPoint> authorityRoutePoints;
    authorityRoutePoints.reserve(scenario.routeWaypoints.size());
    for (const auto& waypoint : scenario.routeWaypoints) {
        authorityRoutePoints.push_back({
            waypoint.latitudeDeg,
            waypoint.longitudeDeg,
        });
    }
    const xvatsim::core::authority::GeoPoint authorityAircraftPosition{
        scenario.aircraftState.latitudeDeg,
        scenario.aircraftState.longitudeDeg,
    };
    const auto relevantAuthorityPolygons =
        xvatsim::core::authority::ResolveRelevantAuthorityPolygons(
            activeAuthorityPolygons,
            authorityPolygonCatalog,
            scenario.aircraftState.valid,
            authorityAircraftPosition,
            authorityRoutePoints);
    xvatsim::brain::AuthorityRelevanceSnapshot authorityRelevanceSnapshot;
    if (scenario.authorityEnrouteHandoff) {
        authorityRelevanceSnapshot.available = scenario.authorityEnrouteSnapshotAvailable;
        authorityRelevanceSnapshot.stale = scenario.authorityEnrouteSnapshotStale;

        std::unordered_map<std::string, std::string> controllerFrequenciesByCallsign;
        for (const auto& controller : scenario.controllers) {
            controllerFrequenciesByCallsign[ToUpperCopy(Trim(controller.callsign))] =
                controller.frequency;
        }

        for (const auto& relevantAuthorityPolygon : relevantAuthorityPolygons) {
            xvatsim::brain::RelevantAuthoritySnapshot relevantAuthority;
            relevantAuthority.callsign =
                relevantAuthorityPolygon.activePolygon.callsign;
            relevantAuthority.authorityId =
                relevantAuthorityPolygon.activePolygon.authorityId;
            relevantAuthority.polygonId =
                relevantAuthorityPolygon.activePolygon.polygonId;
            relevantAuthority.polygonKey =
                relevantAuthorityPolygon.activePolygon.polygonKey;
            relevantAuthority.matchedPattern =
                relevantAuthorityPolygon.activePolygon.matchedPattern;
            relevantAuthority.proofSource =
                relevantAuthorityPolygon.activePolygon.proofSource;
            relevantAuthority.proofDetail =
                relevantAuthorityPolygon.activePolygon.proofDetail;
            relevantAuthority.kind =
                ToBrainAuthorityKind(relevantAuthorityPolygon.activePolygon.kind);
            relevantAuthority.aircraftInside =
                relevantAuthorityPolygon.aircraftInside;
            relevantAuthority.routeIntersects =
                relevantAuthorityPolygon.routeIntersects;
            relevantAuthority.routeEntryDistanceNm =
                relevantAuthorityPolygon.routeEntryDistanceNm;

            const auto frequencyIt = controllerFrequenciesByCallsign.find(
                ToUpperCopy(Trim(relevantAuthority.callsign)));
            if (frequencyIt != controllerFrequenciesByCallsign.end()) {
                relevantAuthority.frequency = frequencyIt->second;
            }

            authorityRelevanceSnapshot.relevantAuthorities.push_back(
                std::move(relevantAuthority));
        }
    }

    xvatsim::modules::departure::DepartureModule departureModule;
    const auto collectedDepartureBoard = departureModule.Collect(
        scenario.xPilotSessionSnapshot,
        controllerFeedSnapshot,
        scenario.radioStateSnapshot,
        scenario.workflowState.flightContext.departureIcao,
        scenario.departureAirportSectorSnapshot,
        scenario.authorityEnrouteHandoff ? &authorityRelevanceSnapshot : nullptr,
        nullptr);
    xvatsim::modules::arrival::ArrivalAirspaceModule arrivalAirspaceModule;
    const auto collectedArrivalAirspaceBoard = arrivalAirspaceModule.Collect(
        scenario.xPilotSessionSnapshot,
        controllerFeedSnapshot,
        scenario.radioStateSnapshot,
        scenario.workflowState.flightContext.destinationIcao,
        scenario.arrivalAirportSectorSnapshot,
        scenario.authorityEnrouteHandoff ? &authorityRelevanceSnapshot : nullptr);
    xvatsim::modules::arrival::ArrivalLocalModule arrivalLocalModule;
    const auto collectedArrivalLocalBoard = arrivalLocalModule.Collect(
        scenario.xPilotSessionSnapshot,
        controllerFeedSnapshot,
        scenario.radioStateSnapshot,
        scenario.workflowState.flightContext.destinationIcao,
        scenario.authorityEnrouteHandoff ? &authorityRelevanceSnapshot : nullptr);
    xvatsim::brain::AirportSectorSnapshot builtAirportCoverageSnapshot;
    xvatsim::brain::AirportSectorSnapshot preRefreshAirportCoverageSnapshot;
    bool hasPreRefreshAirportCoverageSnapshot = false;
    std::optional<bool> airportTerminalInside;
    xvatsim::brain::RouteSectorSnapshot resolverRouteSectorSnapshot;
    xvatsim::brain::RouteAuthorityPlan resolverRouteAuthorityPlan;
    xvatsim::brain::AuthorityRelevanceSnapshot resolverAuthorityRelevanceSnapshot;
    xvatsim::brain::AuthorityRelevanceSnapshot resolverAuthorityRepeatSnapshot;
    xvatsim::brain::ModuleBoardSnapshot resolverEnrouteBoard;
    if (!scenario.airportCoverageBuildIcao.empty()) {
        xvatsim::modules::route_sector::RouteSectorResolver routeSectorResolver;
        routeSectorResolver.LoadBoundaryPayloadsForTesting(
            BuildBoundaryPayload(scenario.airportCoverageCenterFeatures),
            BuildTerminalBoundaryPayload(scenario.airportCoverageTerminalFeatures),
            BuildAuthorityCatalogPayload(scenario.airportCoverageAuthorityCatalogLines));
        if (scenario.airportCoverageBuildsPreRefreshSnapshot ||
            scenario.airportTerminalProbeUsesPreRefreshSnapshot) {
            preRefreshAirportCoverageSnapshot = routeSectorResolver.ResolveAirportCoverage(
                scenario.airportCoverageBuildIcao,
                scenario.hasAirportCoverageBuildCoordinates,
                scenario.airportCoverageBuildLatitudeDeg,
                scenario.airportCoverageBuildLongitudeDeg);
            hasPreRefreshAirportCoverageSnapshot = true;
        }
        if (scenario.hasPendingAirportCoveragePayloads) {
            routeSectorResolver.QueueBoundaryPayloadsForTesting(
                BuildBoundaryPayload(scenario.pendingAirportCoverageCenterFeatures),
                BuildTerminalBoundaryPayload(scenario.pendingAirportCoverageTerminalFeatures),
                BuildAuthorityCatalogPayload(
                    scenario.pendingAirportCoverageAuthorityCatalogLines));
        }
        builtAirportCoverageSnapshot = routeSectorResolver.ResolveAirportCoverage(
            scenario.airportCoverageBuildIcao,
            scenario.hasAirportCoverageBuildCoordinates,
            scenario.airportCoverageBuildLatitudeDeg,
            scenario.airportCoverageBuildLongitudeDeg);
        if (scenario.hasAirportTerminalProbeCoordinates) {
            const auto& probeSnapshot =
                (scenario.airportTerminalProbeUsesPreRefreshSnapshot &&
                 hasPreRefreshAirportCoverageSnapshot)
                    ? preRefreshAirportCoverageSnapshot
                    : builtAirportCoverageSnapshot;
            airportTerminalInside = routeSectorResolver.IsInsideAirportTerminalCoverage(
                probeSnapshot,
                scenario.airportTerminalProbeLatitudeDeg,
                scenario.airportTerminalProbeLongitudeDeg);
        }
    }
    xvatsim::modules::enroute::EnrouteModule enrouteModule;
    const auto collectedEnrouteBoard = enrouteModule.Collect(
        scenario.xPilotSessionSnapshot,
        controllerFeedSnapshot,
        scenario.radioStateSnapshot,
        scenario.routeSectorSnapshot,
        scenario.authorityEnrouteHandoff ? &authorityRelevanceSnapshot : nullptr);
    xvatsim::modules::update_checker::UpdateCheckResult updateCheckResult;
    if (!scenario.updateManifestPayload.empty()) {
        xvatsim::modules::update_checker::UpdateCheckRequest updateRequest;
        updateRequest.installedVersion = scenario.updateInstalledVersion;
        updateRequest.manifestUrl = scenario.updateManifestUrl;
        updateRequest.source =
            xvatsim::modules::update_checker::UpdateCheckSource::Automatic;
        updateCheckResult =
            xvatsim::modules::update_checker::EvaluateUpdateManifestPayload(
                updateRequest,
                scenario.updateManifestPayload);
    }
    xvatsim::brain::OverlayUpdateSnapshot overlayUpdateSnapshot;
    overlayUpdateSnapshot.installedVersion = scenario.updateInstalledVersion;
    overlayUpdateSnapshot.status =
        OverlayUpdateStatusFromChecker(updateCheckResult.status);
    overlayUpdateSnapshot.latestVersion = updateCheckResult.latestVersion;
    overlayUpdateSnapshot.downloadPageUrl = updateCheckResult.downloadPageUrl;
    overlayUpdateSnapshot.errorClass = updateCheckResult.errorClass;
    overlayUpdateSnapshot.critical = updateCheckResult.critical;
    overlayUpdateSnapshot.automaticNoticeRequested =
        updateCheckResult.status ==
        xvatsim::modules::update_checker::UpdateStatus::Available;
    const auto overlayWorkflowStage =
        scenario.overlayWorkflowStage.value_or(handoffDecision.stage);
    auto routePlanSnapshot = scenario.networkPlanSnapshot;
    if (routePlanSnapshot.routeText.empty()) {
        routePlanSnapshot.routeText = scenario.workflowState.flightContext.routeText;
    }
    const auto effectiveRouteText = routePlanSnapshot.routeText;
    xvatsim::core::preflight::FmsParseResult preflightParseResult;
    xvatsim::core::preflight::PreflightRouteCache preflightRouteCache;
    xvatsim::core::preflight::CacheValidationResult preflightValidationResult;
    std::filesystem::path preflightSourcePath;
    if (!scenario.preflightFmsText.empty()) {
        long long preflightModifiedUnixSeconds = 0;
        std::uintmax_t preflightSourceSizeBytes = scenario.preflightFmsText.size();
        if (scenario.preflightVerifySourceFile) {
            std::string fileName = "xvatsim_harness_preflight";
            for (const auto ch : scenario.name) {
                if (std::isalnum(static_cast<unsigned char>(ch)) != 0) {
                    fileName.push_back(static_cast<char>(
                        std::tolower(static_cast<unsigned char>(ch))));
                } else if (!fileName.empty() && fileName.back() != '_') {
                    fileName.push_back('_');
                }
            }
            fileName += ".fms";
            preflightSourcePath =
                std::filesystem::temp_directory_path() / fileName;
            {
                std::ofstream sourceFile(preflightSourcePath, std::ios::binary);
                sourceFile << scenario.preflightFmsText;
            }
            std::error_code ec;
            preflightSourceSizeBytes =
                std::filesystem::file_size(preflightSourcePath, ec);
            if (ec) {
                preflightSourceSizeBytes = scenario.preflightFmsText.size();
            }
            preflightModifiedUnixSeconds =
                xvatsim::core::preflight::GetFileModifiedUnixSeconds(
                    preflightSourcePath);
        }
        preflightParseResult = xvatsim::core::preflight::ParseFmsPlanText(
            scenario.preflightFmsText,
            preflightSourcePath,
            preflightModifiedUnixSeconds,
            preflightSourceSizeBytes);
        if (preflightParseResult.ok) {
            preflightRouteCache =
                xvatsim::core::preflight::BuildPreflightRouteCache(
                    preflightParseResult.plan);
            if (!scenario.preflightCurrentFmsText.empty() &&
                !preflightSourcePath.empty()) {
                std::ofstream currentSourceFile(
                    preflightSourcePath,
                    std::ios::binary | std::ios::trunc);
                currentSourceFile << scenario.preflightCurrentFmsText;
            }
            if (scenario.preflightValidateAgainstPlan ||
                scenario.resolverUsesPreflightCache) {
                preflightValidationResult =
                    xvatsim::core::preflight::ValidatePreflightRouteCacheForNetworkPlan(
                        preflightRouteCache,
                        routePlanSnapshot,
                        scenario.preflightVerifySourceFile);
            }
        }
    }
    if (scenario.resolveRouteWithResolver) {
        xvatsim::modules::route_sector::RouteSectorResolver routeSectorResolver;
        routeSectorResolver.LoadBoundaryPayloadsForTesting(
            BuildBoundaryPayload(scenario.resolverRouteCenterFeatures),
            BuildTerminalBoundaryPayload(scenario.resolverRouteTerminalFeatures),
            BuildAuthorityCatalogPayload(scenario.resolverRouteAuthorityCatalogLines),
            scenario.resolverRouteOwnershipJson);
        if (scenario.resolverUsesPreflightCache && preflightParseResult.ok) {
            routeSectorResolver.SetPreflightRouteCache(
                preflightRouteCache,
                "harness preflight cache");
        }
        if (scenario.resolverRouteBuildsPreRefreshSnapshot) {
            (void)routeSectorResolver.Resolve(scenario.aircraftState, routePlanSnapshot);
        }
        if (scenario.hasPendingResolverRoutePayloads) {
            routeSectorResolver.QueueBoundaryPayloadsForTesting(
                BuildBoundaryPayload(scenario.pendingResolverRouteCenterFeatures),
                BuildTerminalBoundaryPayload(scenario.pendingResolverRouteTerminalFeatures),
                BuildAuthorityCatalogPayload(
                    scenario.pendingResolverRouteAuthorityCatalogLines),
                scenario.pendingResolverRouteOwnershipJson);
        }
        resolverRouteSectorSnapshot =
            routeSectorResolver.Resolve(scenario.aircraftState, routePlanSnapshot);
        resolverRouteAuthorityPlan =
            xvatsim::brain::BuildRouteAuthorityPlanFromRouteSectorSnapshot(
                routePlanSnapshot,
                resolverRouteSectorSnapshot,
                1);
        resolverAuthorityRelevanceSnapshot =
            routeSectorResolver.ResolveBrainScheduledAuthorityVerification(
                scenario.aircraftState,
                controllerFeedSnapshot,
                resolverRouteSectorSnapshot,
                "regression-harness-authority-verifier",
                (scenario.transceiverResolutionSnapshot.available ||
                 !scenario.transceiverResolutionSnapshot.candidates.empty())
                    ? &scenario.transceiverResolutionSnapshot
                    : nullptr);
        if (scenario.resolverAuthorityRepeatControllerFeedGeneration > 0 ||
            scenario.resolverAuthorityRepeatCacheAgeSeconds > 0 ||
            !scenario.resolverAuthorityRepeatControllers.empty() ||
            scenario.hasResolverAuthorityRepeatAircraftState) {
            if (scenario.resolverAuthorityRepeatCacheAgeSeconds > 0) {
                routeSectorResolver.AgeAuthorityRelevanceCacheForTesting(
                    scenario.resolverAuthorityRepeatCacheAgeSeconds);
            }
            auto repeatControllers =
                scenario.resolverAuthorityRepeatReplaceControllers
                    ? scenario.resolverAuthorityRepeatControllers
                    : scenario.controllers;
            if (!scenario.resolverAuthorityRepeatReplaceControllers) {
                repeatControllers.insert(
                    repeatControllers.end(),
                    scenario.resolverAuthorityRepeatControllers.begin(),
                    scenario.resolverAuthorityRepeatControllers.end());
            }
            auto repeatControllerFeedSnapshot = controllerFeedSnapshot;
            repeatControllerFeedSnapshot.generation =
                scenario.resolverAuthorityRepeatControllerFeedGeneration > 0
                    ? scenario.resolverAuthorityRepeatControllerFeedGeneration
                    : controllerFeedSnapshot.generation;
            if ((repeatControllerFeedSnapshot.available &&
                 !repeatControllerFeedSnapshot.stale) ||
                scenario.forceControllerFeedEntries) {
                repeatControllerFeedSnapshot.connectedControllers =
                    static_cast<int>(repeatControllers.size());
                repeatControllerFeedSnapshot.controllers = &repeatControllers;
            }
            auto repeatAircraftState = scenario.aircraftState;
            if (scenario.hasResolverAuthorityRepeatAircraftState) {
                repeatAircraftState =
                    scenario.resolverAuthorityRepeatAircraftState;
            }
            auto repeatRouteSectorSnapshot = resolverRouteSectorSnapshot;
            if (scenario.hasResolverAuthorityRepeatAircraftState) {
                repeatRouteSectorSnapshot =
                    routeSectorResolver.Resolve(
                        repeatAircraftState,
                        routePlanSnapshot);
            }
            resolverAuthorityRepeatSnapshot =
                routeSectorResolver.ResolveBrainScheduledAuthorityVerification(
                    repeatAircraftState,
                    repeatControllerFeedSnapshot,
                    repeatRouteSectorSnapshot,
                    "regression-harness-authority-verifier-repeat",
                    (scenario.transceiverResolutionSnapshot.available ||
                     !scenario.transceiverResolutionSnapshot.candidates.empty())
                        ? &scenario.transceiverResolutionSnapshot
                        : nullptr);
        }
        resolverEnrouteBoard = enrouteModule.Collect(
            scenario.xPilotSessionSnapshot,
            controllerFeedSnapshot,
            scenario.radioStateSnapshot,
            resolverRouteSectorSnapshot,
            &resolverAuthorityRelevanceSnapshot);
    }
    const auto grammarCatalog =
        xvatsim::core::route::BuildRouteGrammarCatalog(
            scenario.routeGraph,
            &scenario.proceduresByName);
    const auto parsedRouteTokens =
        xvatsim::core::route::ParseRouteTokens(effectiveRouteText, &grammarCatalog);
    xvatsim::core::route::RouteResolveDiagnostics routeResolveDiagnostics;
    const auto resolvedRouteWaypoints =
        xvatsim::core::route::ResolveRouteWaypoints(
            scenario.aircraftState,
            routePlanSnapshot,
            scenario.routeGraph,
            &grammarCatalog,
            &routeResolveDiagnostics);
    const auto traversedRouteSnapshot =
        xvatsim::core::route::BuildRouteSectorSnapshotFromWaypoints(
            scenario.routeWaypoints,
            scenario.traversalFeatures,
            scenario.traversalTuning);
    const auto sourceManifest =
        xvatsim::core::source_data::ParseMapDataManifestJson(
            scenario.sourceManifestJson);
    const auto vatglassesSourcePackagePayload =
        xvatsim::core::source_data::BuildVatGlassesDynamicSourcePayload(
            scenario.sourcePackagePositionsJson,
            scenario.sourcePackageAirspaceJson,
            scenario.sourcePackageOwnershipJson);
    auto supplementalSourcePackagePayloads = scenario.sourcePackageSpecialSectorJsons;
    supplementalSourcePackagePayloads.insert(
        supplementalSourcePackagePayloads.end(),
        scenario.sourcePackageTerminalAuthorityJsons.begin(),
        scenario.sourcePackageTerminalAuthorityJsons.end());
    for (const auto& registryJson : scenario.sourceRegistryJsons) {
        for (const auto& entry :
             xvatsim::core::source_data::ParseAuthoritySourceRegistryJson(
                 registryJson)) {
            const auto payloadIt = scenario.sourceRegistryPayloadsByUrl.find(
                entry.source == "VATGLASSES_DYNAMIC_DIRECTORY"
                    ? entry.positionsUrl + "|" + entry.airspaceUrl + "|" + entry.ownershipUrl
                    : entry.url);
            if (payloadIt != scenario.sourceRegistryPayloadsByUrl.end()) {
                supplementalSourcePackagePayloads.push_back(payloadIt->second);
            }
        }
    }
    const auto sourcePackagePayload =
        xvatsim::core::source_data::BuildAuthoritySourcePackagePayload(
            vatglassesSourcePackagePayload,
            supplementalSourcePackagePayloads);

    xvatsim::brain::BrainTerminalAuthorityWorkerOutput terminalAuthorityOutput;
    if (!scenario.terminalAuthorityAirportIcao.empty()) {
        xvatsim::modules::terminal_authority::TerminalAuthorityResolver
            terminalAuthorityResolver;
        terminalAuthorityResolver.LoadPayloadForTesting(
            BuildTerminalBoundaryPayload(scenario.terminalAuthorityFeatures));
        xvatsim::brain::BrainTerminalAuthorityWorkerInput terminalInput;
        terminalInput.airportIcao = scenario.terminalAuthorityAirportIcao;
        terminalInput.hasAirportCoordinates =
            scenario.hasTerminalAuthorityCoordinates;
        terminalInput.airportLatitudeDeg =
            scenario.terminalAuthorityLatitudeDeg;
        terminalInput.airportLongitudeDeg =
            scenario.terminalAuthorityLongitudeDeg;
        terminalInput.nowSeconds =
            static_cast<long long>(scenario.nowSeconds);
        terminalAuthorityOutput =
            terminalAuthorityResolver.ResolveAirportTerminalOwner(
                terminalInput);
    }

    xvatsim::brain::BrainOwnedRuntimeState airportFrequencyRuntimeState;
    xvatsim::brain::BrainAirportFrequencyWorkerOutput airportFrequencyOutput;
    if (!scenario.airportFrequencyFrqRows.empty()) {
        xvatsim::modules::airport_frequency_catalog::AirportFrequencyCatalogResolver
            airportFrequencyResolver;
        airportFrequencyResolver.LoadFrqCsvPayloadForTesting(
            BuildAirportFrequencyFrqCsvPayload(scenario.airportFrequencyFrqRows));
        airportFrequencyOutput =
            xvatsim::brain::RefreshBrainOwnedAirportFrequencies(
                &airportFrequencyRuntimeState,
                scenario.workflowState.flightContext,
                static_cast<long long>(scenario.nowSeconds),
                &airportFrequencyResolver);
    }

    xvatsim::brain::RadioReachableBuildOptions relevanceRadioOptions;
    relevanceRadioOptions.generation = controllerFeedSnapshot.generation;
    relevanceRadioOptions.source =
        xvatsim::brain::RadioReachableSource::AFVRadioRange;
    relevanceRadioOptions.changeReason = "harness-controller-relevance";
    relevanceRadioOptions.nowSeconds = scenario.nowSeconds;
    const auto controllerRelevanceRadioSnapshot =
        xvatsim::brain::BuildRadioReachableControllerSnapshotFromTransceivers(
            scenario.transceiverResolutionSnapshot,
            controllerFeedSnapshot,
            relevanceRadioOptions);
    xvatsim::brain::BrainControllerRelevanceWorkerInput
        controllerRelevanceInput;
    controllerRelevanceInput.workflowStage =
        scenario.controllerRelevanceWorkflowStage;
    controllerRelevanceInput.radioBoardHash =
        controllerRelevanceRadioSnapshot.stableHash;
    controllerRelevanceInput.routePolygonHash = 1;
    controllerRelevanceInput.currentPolygonIndex = 1;
    controllerRelevanceInput.currentPolygonKey =
        scenario.routeSectorSnapshot.currentSectors.empty()
            ? "CURRENT"
            : scenario.routeSectorSnapshot.currentSectors.front().identifier;
    controllerRelevanceInput.nextPolygonKey =
        scenario.routeSectorSnapshot.nextSectors.empty()
            ? ""
            : scenario.routeSectorSnapshot.nextSectors.front().identifier;
    controllerRelevanceInput.currentSectors =
        scenario.routeSectorSnapshot.currentSectors;
    controllerRelevanceInput.nextSectors =
        scenario.routeSectorSnapshot.nextSectors;
    controllerRelevanceInput.departureIcao =
        scenario.workflowState.flightContext.departureIcao;
    controllerRelevanceInput.arrivalIcao =
        scenario.workflowState.flightContext.destinationIcao;
    if (scenario.controllerRelevanceWorkflowStage == WorkflowStage::Arrival) {
        controllerRelevanceInput.arrivalTerminalAuthorityHash = 1;
        controllerRelevanceInput.arrivalTerminalAuthority =
            terminalAuthorityOutput;
    } else {
        controllerRelevanceInput.departureTerminalAuthorityHash = 1;
        controllerRelevanceInput.departureTerminalAuthority =
            terminalAuthorityOutput;
    }
    controllerRelevanceInput.airportFrequencyHash =
        airportFrequencyRuntimeState.airportFrequencyHash;
    controllerRelevanceInput.airportFrequencies =
        airportFrequencyOutput;
    if (authorityRelevanceSnapshot.available) {
        controllerRelevanceInput.authorityRelevanceHash = 1;
        controllerRelevanceInput.authorityRelevance = authorityRelevanceSnapshot;
    }
    controllerRelevanceInput.radios = scenario.radioStateSnapshot;
    controllerRelevanceInput.candidates =
        controllerRelevanceRadioSnapshot.candidates;
    const auto controllerRelevanceOutput =
        xvatsim::brain::RunBrainControllerRelevanceWorker(
            controllerRelevanceInput);

    const auto shouldRunCtafUnicomPublisherProbe =
        scenario.ctafUnicomPublisherProbe ||
        scenario.expectations.ctafUnicomEvidenceSummary.has_value() ||
        !scenario.expectations.ctafUnicomSourceEvidence.empty() ||
        !scenario.expectations.ctafUnicomProjectionEvidence.empty() ||
        scenario.expectations.ctafUnicomAdvisoryPreviewSummary.has_value() ||
        !scenario.expectations.ctafUnicomAdvisoryPreviewDecisions.empty() ||
        scenario.expectations.ctafUnicomAdvisoryAuthoritySummary.has_value() ||
        scenario.expectations.ctafUnicomBypassAuditSummary.has_value() ||
        !scenario.expectations.ctafUnicomBypassAuditDecisionsContains.empty() ||
        scenario.expectations.ctafUnicomMissingEvidenceAuditSummary.has_value() ||
        !scenario.expectations
             .ctafUnicomMissingEvidenceAuditDecisionsContains.empty() ||
        scenario.expectations.ctafUnicomLegacyBypassAliasAuditSummary
            .has_value() ||
        !scenario.expectations
             .ctafUnicomLegacyBypassAliasAuditDecisionsContains.empty() ||
        scenario.expectations
            .ctafUnicomPublicUnknownAliasConsumerAuditSummary.has_value() ||
        !scenario.expectations
             .ctafUnicomPublicUnknownAliasConsumerAuditDecisionsContains
             .empty() ||
        scenario.expectations.ctafUnicomExternalAliasDeprecationSummary
            .has_value() ||
        !scenario.expectations
             .ctafUnicomExternalAliasDeprecationDecisionsContains.empty() ||
        scenario.expectations
            .ctafUnicomPublicHeaderAliasRiskClosureSummary.has_value() ||
        !scenario.expectations
             .ctafUnicomPublicHeaderAliasRiskClosureDecisionsContains
             .empty() ||
        !scenario.expectations.ctafUnicomPublisherRows.empty();
    xvatsim::brain::BrainOwnedPublisherOutput ctafUnicomPublisherOutput;
    if (shouldRunCtafUnicomPublisherProbe) {
        xvatsim::brain::BrainOwnedRuntimeState publisherState;
        publisherState.routeProgressDistanceNm =
            scenario.displayIntentRouteProgressNm;
        publisherState.currentPolygonKey =
            scenario.displayIntentCurrentPolygonKey;
        publisherState.nextPolygonKey = scenario.displayIntentNextPolygonKey;
        publisherState.arrivalPolygonKey =
            scenario.displayIntentArrivalPolygonKey;

        xvatsim::brain::BrainOwnedPublisherFactInput publisherFacts;
        publisherFacts.workflowStage =
            scenario.ctafUnicomPublisherStage.value_or(
                scenario.displayIntentWorkflowStage.value_or(
                    handoffDecision.stage));
        publisherFacts.radios = scenario.radioStateSnapshot;
        publisherFacts.departureBoard = scenario.departureBoard;
        publisherFacts.arrivalBoard = scenario.arrivalBoard;
        publisherFacts.enrouteBoard = scenario.enrouteBoard;
        publisherFacts.completions = controllerRelevanceOutput.completions;
        if (scenario.ctafUnicomPublisherAcceptBoardRows) {
            AppendHarnessAcceptedCompletionsFromBoard(
                scenario.departureBoard,
                DisplayRelation::Unknown,
                &publisherFacts.completions);
            AppendHarnessAcceptedCompletionsFromBoard(
                scenario.arrivalBoard,
                DisplayRelation::Unknown,
                &publisherFacts.completions);
            AppendHarnessAcceptedCompletionsFromBoard(
                scenario.enrouteBoard,
                DisplayRelation::Unknown,
                &publisherFacts.completions);
        }
        publisherFacts.departureCtaf = scenario.ctafUnicomDepartureFact;
        publisherFacts.arrivalCtaf = scenario.ctafUnicomArrivalFact;
        publisherFacts.publishReason = "harness-ctaf-unicom";
        publisherFacts.productPlanKey =
            scenario.ctafUnicomPublisherProductPlanKey;
        publisherFacts.productPlanKeySource =
            scenario.ctafUnicomPublisherProductPlanKey.empty()
                ? std::string("unavailable")
                : std::string("harness");
        publisherFacts.productPlanKeyMissingReason =
            scenario.ctafUnicomPublisherProductPlanKey.empty()
                ? std::string("harness-publisher-plan-key-empty")
                : std::string{};
        publisherFacts.sourceOwnedFallbackStableKeyShadowEnabled =
            scenario.sourceOwnedFallbackStableKeyShadowEnabled;
        publisherFacts.sourceOwnedFallbackStableKeyShadowGateSource =
            scenario.sourceOwnedFallbackStableKeyShadowGateSource;
        publisherFacts
            .sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled =
            scenario
                .sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled;
        publisherFacts
            .sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource =
            scenario
                .sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource;
        if (scenario
                .settingsSourceOwnedFallbackStableKeyLiveConsumptionLoaded) {
            publisherFacts
                .sourceOwnedFallbackStableKeyLiveConsumptionEnabled =
                scenario
                    .settingsSourceOwnedFallbackStableKeyLiveConsumptionEnabled;
            publisherFacts
                .sourceOwnedFallbackStableKeyLiveConsumptionGateSource =
                scenario
                    .settingsSourceOwnedFallbackStableKeyLiveConsumptionGateSource;
        } else {
            publisherFacts
                .sourceOwnedFallbackStableKeyLiveConsumptionEnabled =
                scenario.sourceOwnedFallbackStableKeyLiveConsumptionEnabled;
            publisherFacts
                .sourceOwnedFallbackStableKeyLiveConsumptionGateSource =
                scenario.sourceOwnedFallbackStableKeyLiveConsumptionGateSource;
        }

        auto publisherInput =
            xvatsim::brain::BuildBrainOwnedPublisherInputFromFacts(
                publisherState,
                publisherFacts);
        publisherInput.omitDepartureCtafUnicomAdvisoryDecisionForDiagnostics =
            scenario.ctafUnicomOmitDepartureAdvisoryDecision;
        publisherInput.omitArrivalCtafUnicomAdvisoryDecisionForDiagnostics =
            scenario.ctafUnicomOmitArrivalAdvisoryDecision;
        publisherInput
            .incompleteDepartureCtafUnicomAdvisoryDecisionForDiagnostics =
            scenario.ctafUnicomIncompleteDepartureAdvisoryDecision;
        publisherInput
            .incompleteArrivalCtafUnicomAdvisoryDecisionForDiagnostics =
            scenario.ctafUnicomIncompleteArrivalAdvisoryDecision;
        if (scenario.ctafUnicomOmitDepartureSourceEvidence ||
            scenario.ctafUnicomOmitArrivalSourceEvidence) {
            publisherInput.ctafUnicomSourceEvidence.erase(
                std::remove_if(
                    publisherInput.ctafUnicomSourceEvidence.begin(),
                    publisherInput.ctafUnicomSourceEvidence.end(),
                    [&](const auto& evidence) {
                        return (scenario.ctafUnicomOmitDepartureSourceEvidence &&
                                evidence.endpoint == "departure") ||
                               (scenario.ctafUnicomOmitArrivalSourceEvidence &&
                                evidence.endpoint == "arrival");
                    }),
                publisherInput.ctafUnicomSourceEvidence.end());
        }
        ctafUnicomPublisherOutput =
            xvatsim::brain::RunBrainOwnedPublisher(
                &publisherState,
                publisherInput);
    }

    if (scenario.applyStandbyAssist) {
        xvatsim::brain::BrainOwnedStandbyAssistPlanInput standbyInput;
        standbyInput.workflowStage =
            scenario.standbyAssistWorkflowStage.value_or(
                displayIntentInput.workflowStage);
        standbyInput.planKey =
            scenario.standbyAssistPlanKey.empty()
                ? std::string("HARNESS")
                : scenario.standbyAssistPlanKey;
        standbyInput.radios = scenario.radioStateSnapshot;
        standbyInput.standbyAssistEnabled = scenario.standbyAssistEnabled;
        standbyInput.directCtafStandbyAssistEnabled =
            scenario.standbyAssistDirectCtafEnabled;
        standbyInput.directCtafGateSource =
            scenario.standbyAssistDirectCtafGateSource;
        if (shouldRunCtafUnicomPublisherProbe) {
            standbyInput.board =
                scenario.standbyAssistUseDisplayBoardWithCtafAdvisories
                    ? displayBoard
                    : ctafUnicomPublisherOutput.finalDisplay;
            standbyInput.ctafUnicomAdvisoryCandidates =
                ctafUnicomPublisherOutput.ctafUnicomStandbyAdvisoryCandidates;
        } else {
            standbyInput.board = displayBoard;
        }
        standbyPlan =
            xvatsim::brain::BuildBrainOwnedStandbyAssistPlan(standbyInput);

        auto standbyLoaded =
            scenario.standbyAssistLoaded.value_or(
                standbyPlan.targetAlreadyInCom1Standby);
        if (scenario.standbyAssistSideEffect) {
            xvatsim::brain::BrainOwnedRuntimeState standbyState;
            standbySideEffectDecision =
                xvatsim::brain::DecideBrainOwnedStandbyAssistSideEffect(
                    &standbyState,
                    standbyPlan,
                    scenario.standbyAssistEnabled);
            auto writerResult = standbySideEffectDecision.writerResult;
            standbyLoaded = standbySideEffectDecision.standbyLoaded;
            if (standbySideEffectDecision.writeAttempted) {
                standbyLoaded =
                    scenario.standbyAssistWriteSucceeded.value_or(false);
                writerResult =
                    xvatsim::brain::BuildBrainOwnedStandbyAssistWriterResult(
                        standbySideEffectDecision,
                        standbyLoaded);
            }
            if (scenario.standbyAssistWriterResultCode.has_value()) {
                writerResult =
                    xvatsim::brain::BuildBrainOwnedStandbyAssistWriterResultFromCode(
                        standbySideEffectDecision,
                        *scenario.standbyAssistWriterResultCode);
            }
            standbySideEffectDecision =
                xvatsim::brain::CompleteBrainOwnedStandbyAssistSideEffectDecision(
                    standbyPlan,
                    standbySideEffectDecision,
                    writerResult);
            standbyLoaded = standbySideEffectDecision.standbyLoaded;
        }
        displayBoard =
            xvatsim::brain::ApplyBrainOwnedStandbyAssistResult(
                standbyPlan,
                standbyLoaded);
    }

    const auto overlayModel =
        xvatsim::brain::BrainOrchestrator::BuildOverlayViewModel(
            overlayWorkflowStage,
            scenario.aircraftState,
            scenario.xPilotSessionSnapshot,
            scenario.radioStateSnapshot,
            scenario.networkPlanSnapshot,
            controllerFeedSnapshot,
            scenario.transceiverResolutionSnapshot,
            displayBoard,
            xvatsim::brain::ManualQuerySnapshot{},
            overlayUpdateSnapshot);

    std::cout << "Scenario: " << scenario.name << "\n";
    std::cout << "PreflightParseOk: "
              << (preflightParseResult.ok ? "true" : "false") << "\n";
    std::cout << "PreflightDeparture: "
              << preflightParseResult.plan.departureIcao << "\n";
    std::cout << "PreflightDestination: "
              << preflightParseResult.plan.destinationIcao << "\n";
    std::cout << "PreflightWaypoints:";
    for (const auto& waypoint : preflightParseResult.plan.waypoints) {
        std::cout << " " << waypoint.ident;
    }
    std::cout << "\n";
    std::cout << "PreflightValidationAccepted: "
              << (preflightValidationResult.accepted ? "true" : "false") << "\n";
    std::cout << "PreflightValidationReason: "
              << preflightValidationResult.reason << "\n";
    std::cout << "Stage: " << WorkflowStageToString(handoffDecision.stage) << "\n";
    std::cout << "Reason: " << handoffDecision.reason << "\n";
    std::cout << "DepartureLocationConfirmed: "
              << (departureLocationConfirmed ? "true" : "false") << "\n";
    std::cout << "RecoveryAccepted: "
              << (recoveryDecision.accepted ? "true" : "false") << "\n";
    std::cout << "RecoveryStage: "
              << WorkflowStageToString(recoveryDecision.stage) << "\n";
    std::cout << "RecoveryReason: " << recoveryDecision.reason << "\n";
    std::cout << "RecoveryUsedPreservedContext: "
              << (recoveryDecision.usedPreservedContext ? "true" : "false") << "\n";
    std::cout << "RecoveryUsedFreshNetworkPlan: "
              << (recoveryDecision.usedFreshNetworkPlan ? "true" : "false") << "\n";
    std::cout << "RecoveryFlightContextActive: "
              << (recoveryDecision.flightContext.active ? "true" : "false") << "\n";
    std::cout << "DisplaySource: " << BoardSourceToString(displayBoard.source) << "\n";
    std::cout << "DisplayCallsigns:";
    for (const auto& callsign : ExtractCallsigns(displayBoard)) {
        std::cout << " " << callsign;
    }
    std::cout << "\n";
    std::cout << "BrainDisplayIntentRows:";
    for (const auto& row : ExtractDisplayIntentRows(displayIntentOutput.finalDisplay)) {
        std::cout << " " << row;
    }
    std::cout << "\n";
    std::cout << "BrainDisplayIntentDecisionSummary: "
              << BrainDisplayIntentDecisionSummaryText(displayIntentOutput)
              << "\n";
    std::cout << "BrainDisplayIntentFailSoftSummary: "
              << BrainDisplayIntentFailSoftSummaryText(displayIntentOutput)
              << "\n";
    std::cout << "BrainDisplayIntentDecisions:";
    for (const auto& decision : ExtractDisplayIntentDecisionRows(displayIntentOutput)) {
        std::cout << " " << decision;
    }
    std::cout << "\n";
    std::cout << "BrainDisplayOverlayCapSummary: "
              << BrainDisplayOverlayCapSummaryText(displayIntentOutput)
              << "\n";
    std::cout << "BrainDisplaySourceLinkSummary: "
              << BrainDisplaySourceLinkSummaryText(displayIntentOutput)
              << "\n";
    std::cout << "BrainDisplayStableKeyAuditSummary: "
              << BrainDisplayStableKeyAuditSummaryText(displayIntentOutput)
              << "\n";
    std::cout << "BrainDisplaySourceOwnedStableKeySummary: "
              << BrainDisplaySourceOwnedStableKeySummaryText(
                     displayIntentOutput)
              << "\n";
    std::cout << "BrainDisplayStableKeyConsumerDryRunSummary: "
              << BrainDisplayStableKeyConsumerDryRunSummaryText(
                     displayIntentOutput)
              << "\n";
    std::cout << "BrainDisplayStableKeyConsumerDryRunDecisions:";
    for (const auto& decision :
         ExtractDisplayStableKeyConsumerDryRunRows(displayIntentOutput)) {
        std::cout << " " << decision;
    }
    std::cout << "\n";
    std::cout << "BrainDisplayStableKeyShadowSummary: "
              << BrainDisplayStableKeyShadowSummaryText(displayIntentOutput)
              << "\n";
    std::cout << "BrainDisplayStableKeyShadowDecisions:";
    for (const auto& decision :
         ExtractDisplayStableKeyShadowRows(displayIntentOutput)) {
        std::cout << " " << decision;
    }
    std::cout << "\n";
    std::cout
        << "BrainDisplayStableKeyLiveConsumptionReadinessSummary: "
        << BrainDisplayStableKeyLiveConsumptionReadinessSummaryText(
               displayIntentOutput)
        << "\n";
    std::cout
        << "BrainDisplayStableKeyLiveConsumptionReadinessDecisions:";
    for (const auto& decision :
         ExtractDisplayStableKeyLiveConsumptionReadinessRows(
             displayIntentOutput)) {
        std::cout << " " << decision;
    }
    std::cout << "\n";
    std::cout << "BrainDisplayStableKeyLiveConsumptionSummary: "
              << BrainDisplayStableKeyLiveConsumptionSummaryText(
                     displayIntentOutput)
              << "\n";
    std::cout << "BrainDisplayStableKeyLiveConsumptionDecisions:";
    for (const auto& decision :
         ExtractDisplayStableKeyLiveConsumptionRows(displayIntentOutput)) {
        std::cout << " " << decision;
    }
    std::cout << "\n";
    std::cout << "BrainDisplayStableKeyAuditDecisions:";
    for (const auto& decision :
         ExtractDisplayStableKeyAuditRows(displayIntentOutput)) {
        std::cout << " " << decision;
    }
    std::cout << "\n";
    std::cout << "BrainDisplayUpstreamStableKeySourceAuditSummary: "
              << BrainDisplayUpstreamStableKeySourceAuditSummaryText(
                     displayIntentOutput)
              << "\n";
    std::cout << "BrainDisplayUpstreamStableKeySourceAuditDecisions:";
    for (const auto& decision :
         ExtractDisplayUpstreamStableKeySourceAuditRows(displayIntentOutput)) {
        std::cout << " " << decision;
    }
    std::cout << "\n";
    std::cout << "BrainDisplayOverlayCapDecisions:";
    for (const auto& decision :
         ExtractDisplayOverlayCapDecisionRows(displayIntentOutput)) {
        std::cout << " " << decision;
    }
    std::cout << "\n";
    if (!scenario.phasePublisherReuseProbe.empty()) {
        const auto phaseLiveConsumptionEnabled =
            scenario
                    .settingsSourceOwnedFallbackStableKeyLiveConsumptionLoaded
                ? scenario
                      .settingsSourceOwnedFallbackStableKeyLiveConsumptionEnabled
                : scenario.sourceOwnedFallbackStableKeyLiveConsumptionEnabled;
        const auto phaseLiveConsumptionGateSource =
            scenario
                    .settingsSourceOwnedFallbackStableKeyLiveConsumptionLoaded
                ? scenario
                      .settingsSourceOwnedFallbackStableKeyLiveConsumptionGateSource
                : scenario.sourceOwnedFallbackStableKeyLiveConsumptionGateSource;
        const auto phaseReuseProbeResult =
            BuildPhasePublisherReuseLedgerProbe(
                scenario.phasePublisherReuseProbe,
                scenario.sourceOwnedFallbackStableKeyShadowEnabled,
                scenario.sourceOwnedFallbackStableKeyShadowGateSource,
                scenario
                    .sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled,
                scenario
                    .sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource,
                phaseLiveConsumptionEnabled,
                phaseLiveConsumptionGateSource);
        std::cout << "PhasePublisherReuseLedgerSummary: "
                  << PhaseReuseSummaryText(phaseReuseProbeResult)
                  << "\n";
        std::cout << "PhasePublisherPlanContextSummary: "
                  << PhasePlanContextSummaryText(phaseReuseProbeResult)
                  << "\n";
        std::cout << "PhasePublisherStableKeySummary: "
                  << PhaseStableKeySummaryText(phaseReuseProbeResult)
                  << "\n";
        std::cout << "PhasePublisherStableKeyConsumerDryRunSummary: "
                  << PhaseStableKeyConsumerDryRunSummaryText(
                         phaseReuseProbeResult)
                  << "\n";
        std::cout << "PhasePublisherStableKeyShadowSummary: "
                  << PhaseStableKeyShadowSummaryText(
                         phaseReuseProbeResult)
                  << "\n";
        std::cout
            << "PhasePublisherStableKeyLiveConsumptionReadinessSummary: "
            << PhaseStableKeyLiveConsumptionReadinessSummaryText(
                   phaseReuseProbeResult)
            << "\n";
        std::cout << "PhasePublisherStableKeyLiveConsumptionSummary: "
                  << PhaseStableKeyLiveConsumptionSummaryText(
                         phaseReuseProbeResult)
                  << "\n";
        std::cout << "PhasePublisherReuseLedgerDecisions:";
        for (const auto& row :
             PhaseReuseDecisionRows(phaseReuseProbeResult)) {
            std::cout << " " << row;
        }
        std::cout << "\n";
    }
    if (scenario.applyStandbyAssist) {
        std::cout << "StandbyAssistSummary: "
                  << StandbyAssistSummaryText(standbyPlan.standbySummary)
                  << "\n";
        std::cout << "StandbyAssistSettingsDiagnostics: "
                  << StandbyAssistSettingsDiagnosticsText(
                         standbyPlan.settingsDiagnostics)
                  << "\n";
        std::cout << "StandbyAssistDecisions:";
        for (const auto& row : StandbyAssistDecisionSummaries(standbyPlan)) {
            std::cout << " " << row;
        }
        std::cout << "\n";
        if (scenario.standbyAssistSideEffect) {
            std::cout << "StandbyAssistSideEffectSummary: "
                      << StandbyAssistSideEffectSummaryText(
                             standbySideEffectDecision)
                      << "\n";
            std::cout << "StandbyAssistSideEffectActualSummary: "
                      << StandbyAssistSideEffectActualSummaryText(
                             standbySideEffectDecision)
                      << "\n";
            std::cout << "StandbyAssistWriterResultSummary: "
                      << StandbyAssistWriterResultSummaryText(
                             standbySideEffectDecision)
                      << "\n";
            std::cout << "StandbyAssistWriterCounterSummary: "
                      << StandbyAssistWriterCounterSummaryText(
                             standbySideEffectDecision.standbySummary)
                      << "\n";
        }
    }
    if (shouldRunCtafUnicomPublisherProbe) {
        std::cout << "CtafUnicomEvidenceSummary: "
                  << CtafUnicomEvidenceSummaryText(
                         ctafUnicomPublisherOutput)
                  << "\n";
        std::cout << "CtafUnicomSourceEvidence:";
        for (const auto& row :
             CtafUnicomSourceEvidenceSummaries(
                 ctafUnicomPublisherOutput)) {
            std::cout << " " << row;
        }
        std::cout << "\n";
        std::cout << "CtafUnicomProjectionEvidence:";
        for (const auto& row :
             CtafUnicomProjectionEvidenceSummaries(
                 ctafUnicomPublisherOutput)) {
            std::cout << " " << row;
        }
        std::cout << "\n";
        std::cout << "CtafUnicomAdvisoryPreviewSummary: "
                  << CtafUnicomAdvisoryPreviewSummaryText(
                         ctafUnicomPublisherOutput)
                  << "\n";
        std::cout << "CtafUnicomAdvisoryPreviewDecisions:";
        for (const auto& row :
             CtafUnicomAdvisoryPreviewDecisionSummaries(
                 ctafUnicomPublisherOutput)) {
            std::cout << " " << row;
        }
        std::cout << "\n";
        std::cout << "CtafUnicomAdvisoryAuthoritySummary: "
                  << CtafUnicomAdvisoryAuthoritySummaryText(
                         ctafUnicomPublisherOutput)
                  << "\n";
        std::cout << "CtafUnicomBypassAuditSummary: "
                  << CtafUnicomBypassAuditSummaryText(
                         ctafUnicomPublisherOutput)
                  << "\n";
        std::cout << "CtafUnicomBypassAuditDecisions:";
        for (const auto& row :
             CtafUnicomBypassAuditDecisionSummaries(
                 ctafUnicomPublisherOutput)) {
            std::cout << " " << row;
        }
        std::cout << "\n";
        std::cout << "CtafUnicomMissingEvidenceAuditSummary: "
                  << CtafUnicomMissingEvidenceAuditSummaryText(
                         ctafUnicomPublisherOutput)
                  << "\n";
        std::cout << "CtafUnicomMissingEvidenceAuditDecisions:";
        for (const auto& row :
             CtafUnicomMissingEvidenceAuditDecisionSummaries(
                 ctafUnicomPublisherOutput)) {
            std::cout << " " << row;
        }
        std::cout << "\n";
        std::cout << "CtafUnicomLegacyBypassAliasAuditSummary: "
                  << CtafUnicomLegacyBypassAliasAuditSummaryText(
                         ctafUnicomPublisherOutput)
                  << "\n";
        std::cout << "CtafUnicomLegacyBypassAliasAuditDecisions:";
        for (const auto& row :
             CtafUnicomLegacyBypassAliasAuditDecisionSummaries(
                 ctafUnicomPublisherOutput)) {
            std::cout << " " << row;
        }
        std::cout << "\n";
        std::cout
            << "CtafUnicomPublicUnknownAliasConsumerAuditSummary: "
            << CtafUnicomPublicUnknownAliasConsumerAuditSummaryText(
                   ctafUnicomPublisherOutput)
            << "\n";
        std::cout
            << "CtafUnicomPublicUnknownAliasConsumerAuditDecisions:";
        for (const auto& row :
             CtafUnicomPublicUnknownAliasConsumerAuditDecisionSummaries(
                 ctafUnicomPublisherOutput)) {
            std::cout << " " << row;
        }
        std::cout << "\n";
        std::cout << "CtafUnicomExternalAliasDeprecationSummary: "
                  << CtafUnicomExternalAliasDeprecationSummaryText(
                         ctafUnicomPublisherOutput)
                  << "\n";
        std::cout << "CtafUnicomExternalAliasDeprecationDecisions:";
        for (const auto& row :
             CtafUnicomExternalAliasDeprecationDecisionSummaries(
                 ctafUnicomPublisherOutput)) {
            std::cout << " " << row;
        }
        std::cout << "\n";
        std::cout
            << "CtafUnicomPublicHeaderAliasRiskClosureSummary: "
            << CtafUnicomPublicHeaderAliasRiskClosureSummaryText(
                   ctafUnicomPublisherOutput)
            << "\n";
        std::cout
            << "CtafUnicomPublicHeaderAliasRiskClosureDecisions:";
        for (const auto& row :
             CtafUnicomPublicHeaderAliasRiskClosureDecisionSummaries(
                 ctafUnicomPublisherOutput)) {
            std::cout << " " << row;
        }
        std::cout << "\n";
        std::cout << "CtafUnicomPublisherRows:";
        for (const auto& row : ExtractDisplayIntentRows(
                 ctafUnicomPublisherOutput.finalDisplay)) {
            std::cout << " " << row;
        }
        std::cout << "\n";
        std::cout << "CtafUnicomPublisherStableKeyShadowSummary: "
                  << BrainDisplayStableKeyShadowSummaryText(
                         ctafUnicomPublisherOutput.displayIntent)
                  << "\n";
        std::cout
            << "CtafUnicomPublisherStableKeyLiveConsumptionReadinessSummary: "
            << BrainDisplayStableKeyLiveConsumptionReadinessSummaryText(
                   ctafUnicomPublisherOutput.displayIntent)
            << "\n";
        std::cout << "CtafUnicomPublisherStableKeyLiveConsumptionSummary: "
                  << BrainDisplayStableKeyLiveConsumptionSummaryText(
                         ctafUnicomPublisherOutput.displayIntent)
                  << "\n";
        std::cout
            << "CtafUnicomPublisherStableKeyLiveConsumptionDecisions:";
        for (const auto& row :
             ExtractDisplayStableKeyLiveConsumptionRows(
                 ctafUnicomPublisherOutput.displayIntent)) {
            std::cout << " " << row;
        }
        std::cout << "\n";
        std::cout << "CtafUnicomPublisherPhaseStableKeyShadowSummary: "
                  << PhaseStableKeyShadowSummaryText(
                         ctafUnicomPublisherOutput.phasePublish)
                  << "\n";
        std::cout
            << "CtafUnicomPublisherPhaseStableKeyLiveConsumptionReadinessSummary: "
            << PhaseStableKeyLiveConsumptionReadinessSummaryText(
                   ctafUnicomPublisherOutput.phasePublish)
            << "\n";
        std::cout
            << "CtafUnicomPublisherPhaseStableKeyLiveConsumptionSummary: "
            << PhaseStableKeyLiveConsumptionSummaryText(
                   ctafUnicomPublisherOutput.phasePublish)
            << "\n";
        std::cout << "CtafUnicomPublisherPhaseReuseLedgerDecisions:";
        for (const auto& row :
             PhaseReuseDecisionRows(ctafUnicomPublisherOutput.phasePublish)) {
            std::cout << " " << row;
        }
        std::cout << "\n";
    }
    std::cout << "TerminalAuthorityOwners:";
    for (const auto& owner : ExtractTerminalAuthorityOwners(terminalAuthorityOutput)) {
        std::cout << " " << owner;
    }
    std::cout << "\n";
    std::cout << "TerminalAuthorityPolygons:";
    for (const auto& polygon : ExtractTerminalAuthorityPolygons(terminalAuthorityOutput)) {
        std::cout << " " << polygon;
    }
    std::cout << "\n";
    std::cout << "AirportFrequencyDepartureRecords:";
    for (const auto& record : ExtractAirportFrequencyRecords(
             airportFrequencyOutput.departureFrequencies)) {
        std::cout << " " << record;
    }
    std::cout << "\n";
    std::cout << "AirportFrequencyArrivalRecords:";
    for (const auto& record : ExtractAirportFrequencyRecords(
             airportFrequencyOutput.arrivalFrequencies)) {
        std::cout << " " << record;
    }
    std::cout << "\n";
    std::cout << "BrainControllerRelevanceDepartureCallsigns:";
    for (const auto& callsign : ExtractCallsigns(controllerRelevanceOutput.departureBoard)) {
        std::cout << " " << callsign;
    }
    std::cout << "\n";
    std::cout << "BrainControllerRelevanceArrivalCallsigns:";
    for (const auto& callsign : ExtractCallsigns(controllerRelevanceOutput.arrivalBoard)) {
        std::cout << " " << callsign;
    }
    std::cout << "\n";
    std::cout << "BrainControllerRelevanceCompletions:";
    for (const auto& completion : ExtractControllerRelevanceCompletions(
             controllerRelevanceOutput)) {
        std::cout << " " << completion;
    }
    std::cout << "\n";
    std::cout << "OverlayBodyLines:";
    for (const auto& line : ExtractOverlayBodyLines(overlayModel)) {
        std::cout << " " << line;
    }
    std::cout << "\n";
    std::cout << "OverlayBodyTones:";
    for (const auto& tone : ExtractOverlayBodyTones(overlayModel)) {
        std::cout << " " << tone;
    }
    std::cout << "\n";
    std::cout << "OverlayVersionText: "
              << overlayModel.version.text << "\n";
    std::cout << "OverlayVersionAlternateText: "
              << overlayModel.version.alternateText << "\n";
    std::cout << "OverlayVersionTone: "
              << OverlayVersionToneToken(overlayModel.version.tone) << "\n";
    std::cout << "OverlayVersionRotates: "
              << (overlayModel.version.rotateAlternate ? "true" : "false")
              << "\n";
    std::cout << "OverlayNoticeVisible: "
              << (overlayModel.systemNotice.visible ? "true" : "false")
              << "\n";
    std::cout << "OverlayNoticeSeverity: "
              << OverlayNoticeSeverityToken(overlayModel.systemNotice.severity)
              << "\n";
    std::cout << "OverlayNoticeTitle: "
              << overlayModel.systemNotice.title << "\n";
    std::cout << "OverlayNoticeBodyLines:";
    for (const auto& line : ExtractOverlayNoticeBodyLines(overlayModel)) {
        std::cout << " " << line;
    }
    std::cout << "\n";
    std::cout << "DepartureCollectedAvailable: "
              << (collectedDepartureBoard.available ? "true" : "false") << "\n";
    std::cout << "DepartureCollectedCallsigns:";
    for (const auto& callsign : ExtractCallsigns(collectedDepartureBoard)) {
        std::cout << " " << callsign;
    }
    std::cout << "\n";
    std::cout << "ArrivalAirspaceAvailable: "
              << (collectedArrivalAirspaceBoard.available ? "true" : "false") << "\n";
    std::cout << "ArrivalAirspaceCallsigns:";
    for (const auto& callsign : ExtractCallsigns(collectedArrivalAirspaceBoard)) {
        std::cout << " " << callsign;
    }
    std::cout << "\n";
    std::cout << "ArrivalLocalAvailable: "
              << (collectedArrivalLocalBoard.available ? "true" : "false") << "\n";
    std::cout << "ArrivalLocalCallsigns:";
    for (const auto& callsign : ExtractCallsigns(collectedArrivalLocalBoard)) {
        std::cout << " " << callsign;
    }
    std::cout << "\n";
    std::cout << "AirportCoverageAvailable: "
              << (builtAirportCoverageSnapshot.available ? "true" : "false") << "\n";
    std::cout << "AirportCoverageMatchTokens:";
    for (const auto& value : ExtractCoverageMatchTokens(builtAirportCoverageSnapshot)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AirportCoverageControllerPrefixes:";
    for (const auto& value : ExtractCoverageControllerPrefixes(builtAirportCoverageSnapshot)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AirportCoverageGenerations:";
    for (const auto& value : ExtractCoverageGenerations(builtAirportCoverageSnapshot)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AirportTerminalInside: ";
    if (airportTerminalInside.has_value()) {
        std::cout << (*airportTerminalInside ? "true" : "false");
    } else {
        std::cout << "unset";
    }
    std::cout << "\n";
    std::cout << "EnrouteAvailable: " << (collectedEnrouteBoard.available ? "true" : "false") << "\n";
    std::cout << "EnrouteCallsigns:";
    for (const auto& callsign : ExtractCallsigns(collectedEnrouteBoard)) {
        std::cout << " " << callsign;
    }
    std::cout << "\n";
    std::cout << "AuthorityCatalogIds:";
    for (const auto& value : ExtractAuthorityCatalogIds(authorityCatalog)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AuthorityDataGaps:";
    for (const auto& value : ExtractAuthorityDataGaps(authorityCatalog)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AuthorityActiveMatches:";
    for (const auto& value : ExtractAuthorityActiveMatches(activeAuthorityMatches)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AuthorityUnmappedCallsigns:";
    for (const auto& value : authorityUnmappedCallsigns) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AuthorityPolygonIds:";
    for (const auto& value : ExtractAuthorityPolygonIds(authorityPolygonCatalog)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AuthorityPolygonLookupKeys:";
    for (const auto& value : ExtractAuthorityPolygonLookupKeys(authorityPolygonCatalog)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AuthorityPolygonRingCounts:";
    for (const auto& value : ExtractAuthorityPolygonRingCounts(authorityPolygonCatalog)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AuthorityPolygonDataGaps:";
    for (const auto& value : ExtractAuthorityPolygonDataGaps(authorityPolygonCatalog)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AuthorityActivePolygonMatches:";
    for (const auto& value : ExtractAuthorityActivePolygonMatches(activeAuthorityPolygons)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AuthorityActivePolygonDataGaps:";
    for (const auto& value :
         ExtractAuthorityActivePolygonDataGaps(activeAuthorityPolygonDataGaps)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "AuthorityRelevantPolygonMatches:";
    for (const auto& value :
         ExtractAuthorityRelevantPolygonMatches(relevantAuthorityPolygons)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "SourceManifestValid: "
              << (sourceManifest.valid ? "true" : "false") << "\n";
    std::cout << "SourceManifestValues:";
    for (const auto& value : ExtractSourceManifestValues(sourceManifest)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "UpdateStatus: "
              << xvatsim::modules::update_checker::ToString(
                     updateCheckResult.status)
              << "\n";
    std::cout << "UpdateLatestVersion: "
              << updateCheckResult.latestVersion << "\n";
    std::cout << "UpdateCritical: "
              << (updateCheckResult.critical ? "true" : "false") << "\n";
    std::cout << "UpdateDownloadPageUrl: "
              << updateCheckResult.downloadPageUrl << "\n";
    std::cout << "UpdateErrorClass: "
              << updateCheckResult.errorClass << "\n";
    std::cout << "SourceRegistryValues:";
    for (const auto& value : ExtractSourceRegistryValues(scenario.sourceRegistryJsons)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "SourceRegistryCount: "
              << ExtractSourceRegistryEntries(scenario.sourceRegistryJsons).size()
              << "\n";
    std::cout << "SourceRegistrySourceCounts:";
    for (const auto& value :
         ExtractSourceRegistrySourceCounts(scenario.sourceRegistryJsons)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "ResolverRouteAvailable: "
              << (resolverRouteSectorSnapshot.available ? "true" : "false") << "\n";
    std::cout << "ResolverRouteResolved: "
              << (resolverRouteSectorSnapshot.routeResolved ? "true" : "false") << "\n";
    std::cout << "ResolverRouteStatus: " << resolverRouteSectorSnapshot.statusLine << "\n";
    std::cout << "ResolverRouteAuthorityGaps:";
    for (const auto& value : ExtractAuthorityGaps(resolverRouteSectorSnapshot)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "ResolverRouteCurrentSectors:";
    for (const auto& identifier :
         ExtractSectorIdentifiers(resolverRouteSectorSnapshot.currentSectors)) {
        std::cout << " " << identifier;
    }
    std::cout << "\n";
    std::cout << "ResolverRouteNextSectors:";
    for (const auto& identifier :
         ExtractSectorIdentifiers(resolverRouteSectorSnapshot.nextSectors)) {
        std::cout << " " << identifier;
    }
    std::cout << "\n";
    std::cout << "ResolverRouteCurrentControllerPatterns:";
    for (const auto& value :
         ExtractSectorControllerPatterns(resolverRouteSectorSnapshot.currentSectors)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "ResolverRouteNextControllerPatterns:";
    for (const auto& value :
         ExtractSectorControllerPatterns(resolverRouteSectorSnapshot.nextSectors)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "ResolverRouteCurrentControllerPrefixes:";
    for (const auto& value :
         ExtractSectorControllerPrefixes(resolverRouteSectorSnapshot.currentSectors)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "ResolverRouteNextControllerPrefixes:";
    for (const auto& value :
         ExtractSectorControllerPrefixes(resolverRouteSectorSnapshot.nextSectors)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "ResolverRouteGenerations:";
    for (const auto& value : ExtractRouteGenerations(resolverRouteSectorSnapshot)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "RouteAuthorityPlanSequence:";
    for (const auto& value :
         xvatsim::brain::RouteAuthorityPlanPolygonSequence(
             resolverRouteAuthorityPlan)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "RouteAuthorityPlanFlags:";
    for (const auto& value :
         xvatsim::brain::RouteAuthorityPlanFlagSummary(
             resolverRouteAuthorityPlan)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "RouteAuthorityPlanSources:";
    for (const auto& value :
         xvatsim::brain::RouteAuthorityPlanSourceSummary(
             resolverRouteAuthorityPlan)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "ResolverAuthorityRelevanceAvailable: "
              << (resolverAuthorityRelevanceSnapshot.available ? "true" : "false") << "\n";
    std::cout << "ResolverAuthorityStatus: "
              << resolverAuthorityRelevanceSnapshot.statusLine << "\n";
    std::cout << "ResolverAuthorityCacheStatus: "
              << resolverAuthorityRelevanceSnapshot.diagnosticCacheStatus << "\n";
    std::cout << "ResolverAuthorityCacheReason: "
              << resolverAuthorityRelevanceSnapshot.diagnosticReason << "\n";
    std::cout << "ResolverAuthorityRepeatCacheStatus: "
              << resolverAuthorityRepeatSnapshot.diagnosticCacheStatus << "\n";
    std::cout << "ResolverAuthorityRepeatCacheReason: "
              << resolverAuthorityRepeatSnapshot.diagnosticReason << "\n";
    std::cout << "ResolverAuthorityRepeatRelevantMatches:";
    for (const auto& value : ExtractAuthorityRelevanceMatches(
             resolverAuthorityRepeatSnapshot)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "ResolverAuthorityDiagnostics:";
    for (const auto& value : ExtractAuthorityRelevanceDiagnostics(
             resolverAuthorityRelevanceSnapshot)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "ResolverAuthorityRelevantMatches:";
    for (const auto& value : ExtractAuthorityRelevanceMatches(
             resolverAuthorityRelevanceSnapshot)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "ResolverAuthorityProofSources:";
    for (const auto& value : ExtractAuthorityRelevanceProofSources(
             resolverAuthorityRelevanceSnapshot)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "ResolverAuthorityProofDetails:";
    for (const auto& value : ExtractAuthorityRelevanceProofDetails(
             resolverAuthorityRelevanceSnapshot)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "ResolverEnrouteAvailable: "
              << (resolverEnrouteBoard.available ? "true" : "false") << "\n";
    std::cout << "ResolverEnrouteCallsigns:";
    for (const auto& callsign : ExtractCallsigns(resolverEnrouteBoard)) {
        std::cout << " " << callsign;
    }
    std::cout << "\n";
    std::cout << "RouteResolved: " << (traversedRouteSnapshot.routeResolved ? "true" : "false") << "\n";
    std::cout << "RouteCurrentSectors:";
    for (const auto& identifier : ExtractSectorIdentifiers(traversedRouteSnapshot.currentSectors)) {
        std::cout << " " << identifier;
    }
    std::cout << "\n";
    std::cout << "RouteNextSectors:";
    for (const auto& identifier : ExtractSectorIdentifiers(traversedRouteSnapshot.nextSectors)) {
        std::cout << " " << identifier;
    }
    std::cout << "\n";
    std::cout << "RouteCurrentControllerPrefixes:";
    for (const auto& value : ExtractSectorControllerPrefixes(traversedRouteSnapshot.currentSectors)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "RouteNextControllerPrefixes:";
    for (const auto& value : ExtractSectorControllerPrefixes(traversedRouteSnapshot.nextSectors)) {
        std::cout << " " << value;
    }
    std::cout << "\n";
    std::cout << "RouteTokenKinds:";
    for (const auto& token : ExtractTokenKinds(parsedRouteTokens)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ResolvedWaypoints:";
    for (const auto& ident : ExtractWaypointIdents(resolvedRouteWaypoints)) {
        std::cout << " " << ident;
    }
    std::cout << "\n";
    std::cout << "ResolvedTokens:";
    for (const auto& token : ExtractRouteDiagnostics(routeResolveDiagnostics.resolvedTokens)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ExpandedTokens:";
    for (const auto& token : ExtractRouteDiagnostics(routeResolveDiagnostics.expandedTokens)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureTokens:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.recognizedProcedureTokens)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureSources:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureMetadataSources)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureRecords:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureRecordKinds)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureRunways:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureRunwayRecords)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureAuthorities:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureCatalogAuthorities)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureCatalogFixes:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureCatalogFixes)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureBoundaryFixes:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureBoundaryFixes)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureOrderedFixes:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureOrderedFixes)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureSyntheticWaypoints:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureSyntheticWaypoints)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureSyntheticSources:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureSyntheticSources)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureApplicationStates:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureApplicationStates)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureApplicationBlocks:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureApplicationBlocks)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureAppliedFixSequences:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureAppliedFixSequences)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureCatalogTransitions:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureCatalogTransitions)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureSupport:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureSupportDirections)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureLinks:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureTransitionLinks)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureMisses:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureTransitionMisses)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureAnchorLinks:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureAnchorLinks)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "ProcedureContextOnly:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.procedureContextOnlyTokens)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "IgnoredTokens:";
    for (const auto& token : ExtractRouteDiagnostics(routeResolveDiagnostics.ignoredTokens)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "UnsupportedTokens:";
    for (const auto& token : ExtractRouteDiagnostics(routeResolveDiagnostics.unsupportedTokens)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "UnresolvedTokens:";
    for (const auto& token : ExtractRouteDiagnostics(routeResolveDiagnostics.unresolvedTokens)) {
        std::cout << " " << token;
    }
    std::cout << "\n";
    std::cout << "UnresolvedAirwayTokens:";
    for (const auto& token :
         ExtractRouteDiagnostics(routeResolveDiagnostics.unresolvedAirwayTokens)) {
        std::cout << " " << token;
    }
    std::cout << "\n";

    if (scenario.expectations.stage.has_value() &&
        handoffDecision.stage != *scenario.expectations.stage) {
        return PrintMismatch(
            "stage",
            WorkflowStageToString(*scenario.expectations.stage),
            WorkflowStageToString(handoffDecision.stage));
    }

    if (scenario.expectations.reason.has_value() &&
        handoffDecision.reason != *scenario.expectations.reason) {
        return PrintMismatch(
            "reason",
            *scenario.expectations.reason,
            handoffDecision.reason);
    }

    if (scenario.expectations.departureLocationConfirmed.has_value() &&
        departureLocationConfirmed != *scenario.expectations.departureLocationConfirmed) {
        return PrintMismatch(
            "departureLocationConfirmed",
            *scenario.expectations.departureLocationConfirmed ? "true" : "false",
            departureLocationConfirmed ? "true" : "false");
    }

    if (scenario.expectations.recoveryAccepted.has_value() &&
        recoveryDecision.accepted != *scenario.expectations.recoveryAccepted) {
        return PrintMismatch(
            "recoveryAccepted",
            *scenario.expectations.recoveryAccepted ? "true" : "false",
            recoveryDecision.accepted ? "true" : "false");
    }

    if (scenario.expectations.recoveryStage.has_value() &&
        recoveryDecision.stage != *scenario.expectations.recoveryStage) {
        return PrintMismatch(
            "recoveryStage",
            WorkflowStageToString(*scenario.expectations.recoveryStage),
            WorkflowStageToString(recoveryDecision.stage));
    }

    if (scenario.expectations.recoveryReason.has_value() &&
        recoveryDecision.reason != *scenario.expectations.recoveryReason) {
        return PrintMismatch(
            "recoveryReason",
            *scenario.expectations.recoveryReason,
            recoveryDecision.reason);
    }

    if (scenario.expectations.recoveryUsedPreservedContext.has_value() &&
        recoveryDecision.usedPreservedContext !=
            *scenario.expectations.recoveryUsedPreservedContext) {
        return PrintMismatch(
            "recoveryUsedPreservedContext",
            *scenario.expectations.recoveryUsedPreservedContext ? "true" : "false",
            recoveryDecision.usedPreservedContext ? "true" : "false");
    }

    if (scenario.expectations.recoveryUsedFreshNetworkPlan.has_value() &&
        recoveryDecision.usedFreshNetworkPlan !=
            *scenario.expectations.recoveryUsedFreshNetworkPlan) {
        return PrintMismatch(
            "recoveryUsedFreshNetworkPlan",
            *scenario.expectations.recoveryUsedFreshNetworkPlan ? "true" : "false",
            recoveryDecision.usedFreshNetworkPlan ? "true" : "false");
    }

    if (scenario.expectations.recoveryFlightContextActive.has_value() &&
        recoveryDecision.flightContext.active !=
            *scenario.expectations.recoveryFlightContextActive) {
        return PrintMismatch(
            "recoveryFlightContextActive",
            *scenario.expectations.recoveryFlightContextActive ? "true" : "false",
            recoveryDecision.flightContext.active ? "true" : "false");
    }

    if (scenario.expectations.displaySource.has_value() &&
        displayBoard.source != *scenario.expectations.displaySource) {
        return PrintMismatch(
            "displaySource",
            BoardSourceToString(*scenario.expectations.displaySource),
            BoardSourceToString(displayBoard.source));
    }

    if (const auto mismatch = CheckStringList(
            "displayCallsigns",
            scenario.expectations.displayCallsigns,
            ExtractCallsigns(displayBoard));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "brainDisplayIntentRows",
            scenario.expectations.brainDisplayIntentRows,
            ExtractDisplayIntentRows(displayIntentOutput.finalDisplay));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.brainDisplayIntentDecisionSummary.has_value()) {
        const auto summary =
            BrainDisplayIntentDecisionSummaryText(displayIntentOutput);
        if (summary !=
            *scenario.expectations.brainDisplayIntentDecisionSummary) {
            return PrintMismatch(
                "brainDisplayIntentDecisionSummary",
                *scenario.expectations.brainDisplayIntentDecisionSummary,
                summary);
        }
    }

    if (scenario.expectations.brainDisplayIntentFailSoftSummary.has_value()) {
        const auto summary =
            BrainDisplayIntentFailSoftSummaryText(displayIntentOutput);
        if (summary !=
            *scenario.expectations.brainDisplayIntentFailSoftSummary) {
            return PrintMismatch(
                "brainDisplayIntentFailSoftSummary",
                *scenario.expectations.brainDisplayIntentFailSoftSummary,
                summary);
        }
    }

    if (const auto mismatch = CheckStringListContains(
            "brainDisplayIntentDecisions",
            scenario.expectations.brainDisplayIntentDecisionsContains,
            ExtractDisplayIntentDecisionRows(displayIntentOutput));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.brainDisplayOverlayCapSummary.has_value()) {
        const auto summary =
            BrainDisplayOverlayCapSummaryText(displayIntentOutput);
        if (summary != *scenario.expectations.brainDisplayOverlayCapSummary) {
            return PrintMismatch(
                "brainDisplayOverlayCapSummary",
                *scenario.expectations.brainDisplayOverlayCapSummary,
                summary);
        }
    }

    if (const auto mismatch = CheckStringListContains(
            "brainDisplayOverlayCapDecisions",
            scenario.expectations.brainDisplayOverlayCapDecisionsContains,
            ExtractDisplayOverlayCapDecisionRows(displayIntentOutput));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.brainDisplaySourceLinkSummary.has_value()) {
        const auto summary =
            BrainDisplaySourceLinkSummaryText(displayIntentOutput);
        if (summary != *scenario.expectations.brainDisplaySourceLinkSummary) {
            return PrintMismatch(
                "brainDisplaySourceLinkSummary",
                *scenario.expectations.brainDisplaySourceLinkSummary,
                summary);
        }
    }

    if (scenario.expectations.brainDisplayStableKeyAuditSummary.has_value()) {
        const auto summary =
            BrainDisplayStableKeyAuditSummaryText(displayIntentOutput);
        if (summary !=
            *scenario.expectations.brainDisplayStableKeyAuditSummary) {
            return PrintMismatch(
                "brainDisplayStableKeyAuditSummary",
                *scenario.expectations.brainDisplayStableKeyAuditSummary,
                summary);
        }
    }

    if (const auto mismatch = CheckStringListContains(
            "brainDisplayStableKeyAuditDecisions",
            scenario.expectations.brainDisplayStableKeyAuditDecisionsContains,
            ExtractDisplayStableKeyAuditRows(displayIntentOutput));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.brainDisplaySourceOwnedStableKeySummary
            .has_value()) {
        const auto summary =
            BrainDisplaySourceOwnedStableKeySummaryText(displayIntentOutput);
        if (summary !=
            *scenario.expectations.brainDisplaySourceOwnedStableKeySummary) {
            return PrintMismatch(
                "brainDisplaySourceOwnedStableKeySummary",
                *scenario.expectations
                     .brainDisplaySourceOwnedStableKeySummary,
                summary);
        }
    }

    if (scenario.expectations.brainDisplayStableKeyConsumerDryRunSummary
            .has_value()) {
        const auto summary =
            BrainDisplayStableKeyConsumerDryRunSummaryText(
                displayIntentOutput);
        if (summary !=
            *scenario.expectations
                 .brainDisplayStableKeyConsumerDryRunSummary) {
            return PrintMismatch(
                "brainDisplayStableKeyConsumerDryRunSummary",
                *scenario.expectations
                     .brainDisplayStableKeyConsumerDryRunSummary,
                summary);
        }
    }

    if (const auto mismatch = CheckStringListContains(
            "brainDisplayStableKeyConsumerDryRunDecisions",
            scenario.expectations
                .brainDisplayStableKeyConsumerDryRunDecisionsContains,
            ExtractDisplayStableKeyConsumerDryRunRows(displayIntentOutput));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.brainDisplayStableKeyShadowSummary
            .has_value()) {
        const auto summary =
            BrainDisplayStableKeyShadowSummaryText(displayIntentOutput);
        if (summary !=
            *scenario.expectations.brainDisplayStableKeyShadowSummary) {
            return PrintMismatch(
                "brainDisplayStableKeyShadowSummary",
                *scenario.expectations.brainDisplayStableKeyShadowSummary,
                summary);
        }
    }

    if (const auto mismatch = CheckStringListContains(
            "brainDisplayStableKeyShadowDecisions",
            scenario.expectations.brainDisplayStableKeyShadowDecisionsContains,
            ExtractDisplayStableKeyShadowRows(displayIntentOutput));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations
            .brainDisplayStableKeyLiveConsumptionReadinessSummary
            .has_value()) {
        const auto summary =
            BrainDisplayStableKeyLiveConsumptionReadinessSummaryText(
                displayIntentOutput);
        if (summary !=
            *scenario.expectations
                 .brainDisplayStableKeyLiveConsumptionReadinessSummary) {
            return PrintMismatch(
                "brainDisplayStableKeyLiveConsumptionReadinessSummary",
                *scenario.expectations
                     .brainDisplayStableKeyLiveConsumptionReadinessSummary,
                summary);
        }
    }

    if (const auto mismatch = CheckStringListContains(
            "brainDisplayStableKeyLiveConsumptionReadinessDecisions",
            scenario.expectations
                .brainDisplayStableKeyLiveConsumptionReadinessDecisionsContains,
            ExtractDisplayStableKeyLiveConsumptionReadinessRows(
                displayIntentOutput));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.brainDisplayStableKeyLiveConsumptionSummary
            .has_value()) {
        const auto summary =
            BrainDisplayStableKeyLiveConsumptionSummaryText(
                displayIntentOutput);
        if (summary !=
            *scenario.expectations
                 .brainDisplayStableKeyLiveConsumptionSummary) {
            return PrintMismatch(
                "brainDisplayStableKeyLiveConsumptionSummary",
                *scenario.expectations
                     .brainDisplayStableKeyLiveConsumptionSummary,
                summary);
        }
    }

    if (const auto mismatch = CheckStringListContains(
            "brainDisplayStableKeyLiveConsumptionDecisions",
            scenario.expectations
                .brainDisplayStableKeyLiveConsumptionDecisionsContains,
            ExtractDisplayStableKeyLiveConsumptionRows(displayIntentOutput));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations
            .brainDisplayUpstreamStableKeySourceAuditSummary.has_value()) {
        const auto summary =
            BrainDisplayUpstreamStableKeySourceAuditSummaryText(
                displayIntentOutput);
        if (summary !=
            *scenario.expectations
                 .brainDisplayUpstreamStableKeySourceAuditSummary) {
            return PrintMismatch(
                "brainDisplayUpstreamStableKeySourceAuditSummary",
                *scenario.expectations
                     .brainDisplayUpstreamStableKeySourceAuditSummary,
                summary);
        }
    }

    if (const auto mismatch = CheckStringListContains(
            "brainDisplayUpstreamStableKeySourceAuditDecisions",
            scenario.expectations
                .brainDisplayUpstreamStableKeySourceAuditDecisionsContains,
            ExtractDisplayUpstreamStableKeySourceAuditRows(
                displayIntentOutput));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.ctafUnicomEvidenceSummary.has_value()) {
        const auto summary =
            CtafUnicomEvidenceSummaryText(ctafUnicomPublisherOutput);
        if (summary != *scenario.expectations.ctafUnicomEvidenceSummary) {
            return PrintMismatch(
                "ctafUnicomEvidenceSummary",
                *scenario.expectations.ctafUnicomEvidenceSummary,
                summary);
        }
    }

    if (const auto mismatch = CheckStringList(
            "ctafUnicomSourceEvidence",
            scenario.expectations.ctafUnicomSourceEvidence,
            CtafUnicomSourceEvidenceSummaries(ctafUnicomPublisherOutput));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "ctafUnicomProjectionEvidence",
            scenario.expectations.ctafUnicomProjectionEvidence,
            CtafUnicomProjectionEvidenceSummaries(
                ctafUnicomPublisherOutput));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.ctafUnicomAdvisoryPreviewSummary
            .has_value()) {
        const auto summary =
            CtafUnicomAdvisoryPreviewSummaryText(
                ctafUnicomPublisherOutput);
        if (summary !=
            *scenario.expectations.ctafUnicomAdvisoryPreviewSummary) {
            return PrintMismatch(
                "ctafUnicomAdvisoryPreviewSummary",
                *scenario.expectations
                     .ctafUnicomAdvisoryPreviewSummary,
                summary);
        }
    }

    if (const auto mismatch = CheckStringList(
            "ctafUnicomAdvisoryPreviewDecisions",
            scenario.expectations.ctafUnicomAdvisoryPreviewDecisions,
            CtafUnicomAdvisoryPreviewDecisionSummaries(
                ctafUnicomPublisherOutput));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.ctafUnicomAdvisoryAuthoritySummary
            .has_value()) {
        const auto summary =
            CtafUnicomAdvisoryAuthoritySummaryText(
                ctafUnicomPublisherOutput);
        if (summary !=
            *scenario.expectations.ctafUnicomAdvisoryAuthoritySummary) {
            return PrintMismatch(
                "ctafUnicomAdvisoryAuthoritySummary",
                *scenario.expectations
                     .ctafUnicomAdvisoryAuthoritySummary,
                summary);
        }
    }

    if (scenario.expectations.ctafUnicomBypassAuditSummary.has_value()) {
        const auto summary =
            CtafUnicomBypassAuditSummaryText(ctafUnicomPublisherOutput);
        if (summary != *scenario.expectations.ctafUnicomBypassAuditSummary) {
            return PrintMismatch(
                "ctafUnicomBypassAuditSummary",
                *scenario.expectations.ctafUnicomBypassAuditSummary,
                summary);
        }
    }

    if (const auto mismatch = CheckStringListContains(
            "ctafUnicomBypassAuditDecisions",
            scenario.expectations.ctafUnicomBypassAuditDecisionsContains,
            CtafUnicomBypassAuditDecisionSummaries(
                ctafUnicomPublisherOutput));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.ctafUnicomMissingEvidenceAuditSummary
            .has_value()) {
        const auto summary =
            CtafUnicomMissingEvidenceAuditSummaryText(
                ctafUnicomPublisherOutput);
        if (summary !=
            *scenario.expectations
                 .ctafUnicomMissingEvidenceAuditSummary) {
            return PrintMismatch(
                "ctafUnicomMissingEvidenceAuditSummary",
                *scenario.expectations
                     .ctafUnicomMissingEvidenceAuditSummary,
                summary);
        }
    }

    if (const auto mismatch = CheckStringListContains(
            "ctafUnicomMissingEvidenceAuditDecisions",
            scenario.expectations
                .ctafUnicomMissingEvidenceAuditDecisionsContains,
            CtafUnicomMissingEvidenceAuditDecisionSummaries(
                ctafUnicomPublisherOutput));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.ctafUnicomLegacyBypassAliasAuditSummary
            .has_value()) {
        const auto summary =
            CtafUnicomLegacyBypassAliasAuditSummaryText(
                ctafUnicomPublisherOutput);
        if (summary !=
            *scenario.expectations
                 .ctafUnicomLegacyBypassAliasAuditSummary) {
            return PrintMismatch(
                "ctafUnicomLegacyBypassAliasAuditSummary",
                *scenario.expectations
                     .ctafUnicomLegacyBypassAliasAuditSummary,
                summary);
        }
    }

    if (const auto mismatch = CheckStringListContains(
            "ctafUnicomLegacyBypassAliasAuditDecisions",
            scenario.expectations
                .ctafUnicomLegacyBypassAliasAuditDecisionsContains,
            CtafUnicomLegacyBypassAliasAuditDecisionSummaries(
                ctafUnicomPublisherOutput));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations
            .ctafUnicomPublicUnknownAliasConsumerAuditSummary.has_value()) {
        const auto summary =
            CtafUnicomPublicUnknownAliasConsumerAuditSummaryText(
                ctafUnicomPublisherOutput);
        if (summary !=
            *scenario.expectations
                 .ctafUnicomPublicUnknownAliasConsumerAuditSummary) {
            return PrintMismatch(
                "ctafUnicomPublicUnknownAliasConsumerAuditSummary",
                *scenario.expectations
                     .ctafUnicomPublicUnknownAliasConsumerAuditSummary,
                summary);
        }
    }

    if (const auto mismatch = CheckStringListContains(
            "ctafUnicomPublicUnknownAliasConsumerAuditDecisions",
            scenario.expectations
                .ctafUnicomPublicUnknownAliasConsumerAuditDecisionsContains,
            CtafUnicomPublicUnknownAliasConsumerAuditDecisionSummaries(
                ctafUnicomPublisherOutput));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.ctafUnicomExternalAliasDeprecationSummary
            .has_value()) {
        const auto summary =
            CtafUnicomExternalAliasDeprecationSummaryText(
                ctafUnicomPublisherOutput);
        if (summary !=
            *scenario.expectations
                 .ctafUnicomExternalAliasDeprecationSummary) {
            return PrintMismatch(
                "ctafUnicomExternalAliasDeprecationSummary",
                *scenario.expectations
                     .ctafUnicomExternalAliasDeprecationSummary,
                summary);
        }
    }

    if (const auto mismatch = CheckStringListContains(
            "ctafUnicomExternalAliasDeprecationDecisions",
            scenario.expectations
                .ctafUnicomExternalAliasDeprecationDecisionsContains,
            CtafUnicomExternalAliasDeprecationDecisionSummaries(
                ctafUnicomPublisherOutput));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations
            .ctafUnicomPublicHeaderAliasRiskClosureSummary.has_value()) {
        const auto summary =
            CtafUnicomPublicHeaderAliasRiskClosureSummaryText(
                ctafUnicomPublisherOutput);
        if (summary !=
            *scenario.expectations
                 .ctafUnicomPublicHeaderAliasRiskClosureSummary) {
            return PrintMismatch(
                "ctafUnicomPublicHeaderAliasRiskClosureSummary",
                *scenario.expectations
                     .ctafUnicomPublicHeaderAliasRiskClosureSummary,
                summary);
        }
    }

    if (const auto mismatch = CheckStringListContains(
            "ctafUnicomPublicHeaderAliasRiskClosureDecisions",
            scenario.expectations
                .ctafUnicomPublicHeaderAliasRiskClosureDecisionsContains,
            CtafUnicomPublicHeaderAliasRiskClosureDecisionSummaries(
                ctafUnicomPublisherOutput));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "ctafUnicomPublisherRows",
            scenario.expectations.ctafUnicomPublisherRows,
            ExtractDisplayIntentRows(
                ctafUnicomPublisherOutput.finalDisplay));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations
            .ctafUnicomPublisherStableKeyShadowSummary.has_value()) {
        const auto summary =
            BrainDisplayStableKeyShadowSummaryText(
                ctafUnicomPublisherOutput.displayIntent);
        if (summary !=
            *scenario.expectations
                 .ctafUnicomPublisherStableKeyShadowSummary) {
            return PrintMismatch(
                "ctafUnicomPublisherStableKeyShadowSummary",
                *scenario.expectations
                     .ctafUnicomPublisherStableKeyShadowSummary,
                summary);
        }
    }

    if (scenario.expectations
            .ctafUnicomPublisherStableKeyLiveConsumptionReadinessSummary
            .has_value()) {
        const auto summary =
            BrainDisplayStableKeyLiveConsumptionReadinessSummaryText(
                ctafUnicomPublisherOutput.displayIntent);
        if (summary !=
            *scenario.expectations
                 .ctafUnicomPublisherStableKeyLiveConsumptionReadinessSummary) {
            return PrintMismatch(
                "ctafUnicomPublisherStableKeyLiveConsumptionReadinessSummary",
                *scenario.expectations
                     .ctafUnicomPublisherStableKeyLiveConsumptionReadinessSummary,
                summary);
        }
    }

    if (scenario.expectations
            .ctafUnicomPublisherStableKeyLiveConsumptionSummary
            .has_value()) {
        const auto summary =
            BrainDisplayStableKeyLiveConsumptionSummaryText(
                ctafUnicomPublisherOutput.displayIntent);
        if (summary !=
            *scenario.expectations
                 .ctafUnicomPublisherStableKeyLiveConsumptionSummary) {
            return PrintMismatch(
                "ctafUnicomPublisherStableKeyLiveConsumptionSummary",
                *scenario.expectations
                     .ctafUnicomPublisherStableKeyLiveConsumptionSummary,
                summary);
        }
    }

    if (const auto mismatch = CheckStringListContains(
            "ctafUnicomPublisherStableKeyLiveConsumptionDecisions",
            scenario.expectations
                .ctafUnicomPublisherStableKeyLiveConsumptionDecisionsContains,
            ExtractDisplayStableKeyLiveConsumptionRows(
                ctafUnicomPublisherOutput.displayIntent));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations
            .ctafUnicomPublisherPhaseStableKeyShadowSummary.has_value()) {
        const auto summary =
            PhaseStableKeyShadowSummaryText(
                ctafUnicomPublisherOutput.phasePublish);
        if (summary !=
            *scenario.expectations
                 .ctafUnicomPublisherPhaseStableKeyShadowSummary) {
            return PrintMismatch(
                "ctafUnicomPublisherPhaseStableKeyShadowSummary",
                *scenario.expectations
                     .ctafUnicomPublisherPhaseStableKeyShadowSummary,
                summary);
        }
    }

    if (scenario.expectations
            .ctafUnicomPublisherPhaseStableKeyLiveConsumptionReadinessSummary
            .has_value()) {
        const auto summary =
            PhaseStableKeyLiveConsumptionReadinessSummaryText(
                ctafUnicomPublisherOutput.phasePublish);
        if (summary !=
            *scenario.expectations
                 .ctafUnicomPublisherPhaseStableKeyLiveConsumptionReadinessSummary) {
            return PrintMismatch(
                "ctafUnicomPublisherPhaseStableKeyLiveConsumptionReadinessSummary",
                *scenario.expectations
                     .ctafUnicomPublisherPhaseStableKeyLiveConsumptionReadinessSummary,
                summary);
        }
    }

    if (scenario.expectations
            .ctafUnicomPublisherPhaseStableKeyLiveConsumptionSummary
            .has_value()) {
        const auto summary =
            PhaseStableKeyLiveConsumptionSummaryText(
                ctafUnicomPublisherOutput.phasePublish);
        if (summary !=
            *scenario.expectations
                 .ctafUnicomPublisherPhaseStableKeyLiveConsumptionSummary) {
            return PrintMismatch(
                "ctafUnicomPublisherPhaseStableKeyLiveConsumptionSummary",
                *scenario.expectations
                     .ctafUnicomPublisherPhaseStableKeyLiveConsumptionSummary,
                summary);
        }
    }

    if (const auto mismatch = CheckStringListContains(
            "ctafUnicomPublisherPhaseReuseLedgerDecisions",
            scenario.expectations
                .ctafUnicomPublisherPhaseReuseLedgerDecisionsContains,
            PhaseReuseDecisionRows(
                ctafUnicomPublisherOutput.phasePublish));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.standbyAssistSummary.has_value()) {
        const auto summary =
            StandbyAssistSummaryText(standbyPlan.standbySummary);
        if (summary != *scenario.expectations.standbyAssistSummary) {
            return PrintMismatch(
                "standbyAssistSummary",
                *scenario.expectations.standbyAssistSummary,
                summary);
        }
    }

    if (const auto mismatch = CheckStringListContains(
            "standbyAssistDecisions",
            scenario.expectations.standbyAssistDecisionsContains,
            StandbyAssistDecisionSummaries(standbyPlan));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.standbyAssistSettingsDiagnostics.has_value()) {
        const auto summary =
            StandbyAssistSettingsDiagnosticsText(
                standbyPlan.settingsDiagnostics);
        if (summary !=
            *scenario.expectations.standbyAssistSettingsDiagnostics) {
            return PrintMismatch(
                "standbyAssistSettingsDiagnostics",
                *scenario.expectations.standbyAssistSettingsDiagnostics,
                summary);
        }
    }

    if (scenario.expectations.standbyAssistSideEffectSummary.has_value()) {
        const auto summary =
            StandbyAssistSideEffectSummaryText(standbySideEffectDecision);
        if (summary != *scenario.expectations.standbyAssistSideEffectSummary) {
            return PrintMismatch(
                "standbyAssistSideEffectSummary",
                *scenario.expectations.standbyAssistSideEffectSummary,
                summary);
        }
    }
    if (scenario.expectations.standbyAssistSideEffectActualSummary.has_value()) {
        const auto summary =
            StandbyAssistSideEffectActualSummaryText(standbySideEffectDecision);
        if (summary !=
            *scenario.expectations.standbyAssistSideEffectActualSummary) {
            return PrintMismatch(
                "standbyAssistSideEffectActualSummary",
                *scenario.expectations
                     .standbyAssistSideEffectActualSummary,
                summary);
        }
    }
    if (scenario.expectations.standbyAssistWriterResultSummary.has_value()) {
        const auto summary =
            StandbyAssistWriterResultSummaryText(standbySideEffectDecision);
        if (summary !=
            *scenario.expectations.standbyAssistWriterResultSummary) {
            return PrintMismatch(
                "standbyAssistWriterResultSummary",
                *scenario.expectations.standbyAssistWriterResultSummary,
                summary);
        }
    }
    if (!scenario.expectations.standbyAssistWriterResultContains.empty()) {
        const auto summary =
            StandbyAssistWriterResultSummaryText(standbySideEffectDecision);
        for (const auto& expected :
             scenario.expectations.standbyAssistWriterResultContains) {
            if (summary.find(expected) == std::string::npos) {
                return PrintMismatch(
                    "standbyAssistWriterResultContains",
                    expected,
                    summary);
            }
        }
    }
    if (scenario.expectations.standbyAssistWriterCounterSummary.has_value()) {
        const auto summary =
            StandbyAssistWriterCounterSummaryText(
                standbySideEffectDecision.standbySummary);
        if (summary !=
            *scenario.expectations.standbyAssistWriterCounterSummary) {
            return PrintMismatch(
                "standbyAssistWriterCounterSummary",
                *scenario.expectations.standbyAssistWriterCounterSummary,
                summary);
        }
    }

    if (const auto mismatch = CheckStringList(
            "overlayBodyLines",
            scenario.expectations.overlayBodyLines,
            ExtractOverlayBodyLines(overlayModel));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "overlayBodyTones",
            scenario.expectations.overlayBodyTones,
            ExtractOverlayBodyTones(overlayModel));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.overlayVersionText.has_value() &&
        overlayModel.version.text != *scenario.expectations.overlayVersionText) {
        return PrintMismatch(
            "overlayVersionText",
            *scenario.expectations.overlayVersionText,
            overlayModel.version.text);
    }

    if (scenario.expectations.overlayVersionAlternateText.has_value() &&
        overlayModel.version.alternateText !=
            *scenario.expectations.overlayVersionAlternateText) {
        return PrintMismatch(
            "overlayVersionAlternateText",
            *scenario.expectations.overlayVersionAlternateText,
            overlayModel.version.alternateText);
    }

    if (scenario.expectations.overlayVersionTone.has_value() &&
        OverlayVersionToneToken(overlayModel.version.tone) !=
            *scenario.expectations.overlayVersionTone) {
        return PrintMismatch(
            "overlayVersionTone",
            *scenario.expectations.overlayVersionTone,
            OverlayVersionToneToken(overlayModel.version.tone));
    }

    if (scenario.expectations.overlayVersionRotates.has_value() &&
        overlayModel.version.rotateAlternate !=
            *scenario.expectations.overlayVersionRotates) {
        return PrintMismatch(
            "overlayVersionRotates",
            *scenario.expectations.overlayVersionRotates ? "true" : "false",
            overlayModel.version.rotateAlternate ? "true" : "false");
    }

    if (scenario.expectations.overlayNoticeVisible.has_value() &&
        overlayModel.systemNotice.visible !=
            *scenario.expectations.overlayNoticeVisible) {
        return PrintMismatch(
            "overlayNoticeVisible",
            *scenario.expectations.overlayNoticeVisible ? "true" : "false",
            overlayModel.systemNotice.visible ? "true" : "false");
    }

    if (scenario.expectations.overlayNoticeSeverity.has_value() &&
        OverlayNoticeSeverityToken(overlayModel.systemNotice.severity) !=
            *scenario.expectations.overlayNoticeSeverity) {
        return PrintMismatch(
            "overlayNoticeSeverity",
            *scenario.expectations.overlayNoticeSeverity,
            OverlayNoticeSeverityToken(overlayModel.systemNotice.severity));
    }

    if (scenario.expectations.overlayNoticeTitle.has_value() &&
        overlayModel.systemNotice.title !=
            *scenario.expectations.overlayNoticeTitle) {
        return PrintMismatch(
            "overlayNoticeTitle",
            *scenario.expectations.overlayNoticeTitle,
            overlayModel.systemNotice.title);
    }

    if (const auto mismatch = CheckStringList(
            "overlayNoticeBodyLines",
            scenario.expectations.overlayNoticeBodyLines,
            ExtractOverlayNoticeBodyLines(overlayModel));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.departureCollectedAvailable.has_value() &&
        collectedDepartureBoard.available !=
            *scenario.expectations.departureCollectedAvailable) {
        return PrintMismatch(
            "departureCollectedAvailable",
            *scenario.expectations.departureCollectedAvailable ? "true" : "false",
            collectedDepartureBoard.available ? "true" : "false");
    }

    if (const auto mismatch = CheckStringList(
            "departureCollectedCallsigns",
            scenario.expectations.departureCollectedCallsigns,
            ExtractCallsigns(collectedDepartureBoard));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.arrivalAirspaceAvailable.has_value() &&
        collectedArrivalAirspaceBoard.available !=
            *scenario.expectations.arrivalAirspaceAvailable) {
        return PrintMismatch(
            "arrivalAirspaceAvailable",
            *scenario.expectations.arrivalAirspaceAvailable ? "true" : "false",
            collectedArrivalAirspaceBoard.available ? "true" : "false");
    }

    if (const auto mismatch = CheckStringList(
            "arrivalAirspaceCallsigns",
            scenario.expectations.arrivalAirspaceCallsigns,
            ExtractCallsigns(collectedArrivalAirspaceBoard));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.arrivalLocalAvailable.has_value() &&
        collectedArrivalLocalBoard.available !=
            *scenario.expectations.arrivalLocalAvailable) {
        return PrintMismatch(
            "arrivalLocalAvailable",
            *scenario.expectations.arrivalLocalAvailable ? "true" : "false",
            collectedArrivalLocalBoard.available ? "true" : "false");
    }

    if (const auto mismatch = CheckStringList(
            "arrivalLocalCallsigns",
            scenario.expectations.arrivalLocalCallsigns,
            ExtractCallsigns(collectedArrivalLocalBoard));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.airportCoverageAvailable.has_value() &&
        builtAirportCoverageSnapshot.available !=
            *scenario.expectations.airportCoverageAvailable) {
        return PrintMismatch(
            "airportCoverageAvailable",
            *scenario.expectations.airportCoverageAvailable ? "true" : "false",
            builtAirportCoverageSnapshot.available ? "true" : "false");
    }

    if (const auto mismatch = CheckStringList(
            "airportCoverageMatchTokens",
            scenario.expectations.airportCoverageMatchTokens,
            ExtractCoverageMatchTokens(builtAirportCoverageSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "airportCoverageControllerPrefixes",
            scenario.expectations.airportCoverageControllerPrefixes,
            ExtractCoverageControllerPrefixes(builtAirportCoverageSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "airportCoverageControllerPatterns",
            scenario.expectations.airportCoverageControllerPatterns,
            ExtractSectorControllerPatterns(builtAirportCoverageSnapshot.coveringSectors));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "airportCoverageGenerations",
            scenario.expectations.airportCoverageGenerations,
            ExtractCoverageGenerations(builtAirportCoverageSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.airportTerminalInside.has_value()) {
        if (!airportTerminalInside.has_value()) {
            return PrintMismatch(
                "airportTerminalInside",
                *scenario.expectations.airportTerminalInside ? "true" : "false",
                "unset");
        }
        if (*airportTerminalInside != *scenario.expectations.airportTerminalInside) {
            return PrintMismatch(
                "airportTerminalInside",
                *scenario.expectations.airportTerminalInside ? "true" : "false",
                *airportTerminalInside ? "true" : "false");
        }
    }

    if (scenario.expectations.enrouteAvailable.has_value() &&
        collectedEnrouteBoard.available != *scenario.expectations.enrouteAvailable) {
        return PrintMismatch(
            "enrouteAvailable",
            *scenario.expectations.enrouteAvailable ? "true" : "false",
            collectedEnrouteBoard.available ? "true" : "false");
    }

    if (const auto mismatch = CheckStringList(
            "enrouteCallsigns",
            scenario.expectations.enrouteCallsigns,
            ExtractCallsigns(collectedEnrouteBoard));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityCatalogIds",
            scenario.expectations.authorityCatalogIds,
            ExtractAuthorityCatalogIds(authorityCatalog));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityDataGaps",
            scenario.expectations.authorityDataGaps,
            ExtractAuthorityDataGaps(authorityCatalog));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityActiveMatches",
            scenario.expectations.authorityActiveMatches,
            ExtractAuthorityActiveMatches(activeAuthorityMatches));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityUnmappedCallsigns",
            scenario.expectations.authorityUnmappedCallsigns,
            authorityUnmappedCallsigns);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityPolygonIds",
            scenario.expectations.authorityPolygonIds,
            ExtractAuthorityPolygonIds(authorityPolygonCatalog));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityPolygonLookupKeys",
            scenario.expectations.authorityPolygonLookupKeys,
            ExtractAuthorityPolygonLookupKeys(authorityPolygonCatalog));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityPolygonRingCounts",
            scenario.expectations.authorityPolygonRingCounts,
            ExtractAuthorityPolygonRingCounts(authorityPolygonCatalog));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityPolygonDataGaps",
            scenario.expectations.authorityPolygonDataGaps,
            ExtractAuthorityPolygonDataGaps(authorityPolygonCatalog));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityActivePolygonMatches",
            scenario.expectations.authorityActivePolygonMatches,
            ExtractAuthorityActivePolygonMatches(activeAuthorityPolygons));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityActivePolygonDataGaps",
            scenario.expectations.authorityActivePolygonDataGaps,
            ExtractAuthorityActivePolygonDataGaps(activeAuthorityPolygonDataGaps));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "authorityRelevantPolygonMatches",
            scenario.expectations.authorityRelevantPolygonMatches,
            ExtractAuthorityRelevantPolygonMatches(relevantAuthorityPolygons));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.sourceManifestValid.has_value() &&
        sourceManifest.valid != *scenario.expectations.sourceManifestValid) {
        return PrintMismatch(
            "sourceManifestValid",
            *scenario.expectations.sourceManifestValid ? "true" : "false",
            sourceManifest.valid ? "true" : "false");
    }

    if (const auto mismatch = CheckStringList(
            "sourceManifestValues",
            scenario.expectations.sourceManifestValues,
            ExtractSourceManifestValues(sourceManifest));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.sourcePackagePayload.has_value() &&
        sourcePackagePayload != *scenario.expectations.sourcePackagePayload) {
        return PrintMismatch(
            "sourcePackagePayload",
            *scenario.expectations.sourcePackagePayload,
            sourcePackagePayload);
    }

    if (scenario.expectations.updateStatus.has_value() &&
        xvatsim::modules::update_checker::ToString(updateCheckResult.status) !=
            *scenario.expectations.updateStatus) {
        return PrintMismatch(
            "updateStatus",
            *scenario.expectations.updateStatus,
            xvatsim::modules::update_checker::ToString(
                updateCheckResult.status));
    }
    if (scenario.expectations.updateLatestVersion.has_value() &&
        updateCheckResult.latestVersion !=
            *scenario.expectations.updateLatestVersion) {
        return PrintMismatch(
            "updateLatestVersion",
            *scenario.expectations.updateLatestVersion,
            updateCheckResult.latestVersion);
    }
    if (scenario.expectations.updateDownloadPageUrl.has_value() &&
        updateCheckResult.downloadPageUrl !=
            *scenario.expectations.updateDownloadPageUrl) {
        return PrintMismatch(
            "updateDownloadPageUrl",
            *scenario.expectations.updateDownloadPageUrl,
            updateCheckResult.downloadPageUrl);
    }
    if (scenario.expectations.updateErrorClass.has_value() &&
        updateCheckResult.errorClass !=
            *scenario.expectations.updateErrorClass) {
        return PrintMismatch(
            "updateErrorClass",
            *scenario.expectations.updateErrorClass,
            updateCheckResult.errorClass);
    }
    if (scenario.expectations.updateCritical.has_value() &&
        updateCheckResult.critical != *scenario.expectations.updateCritical) {
        return PrintMismatch(
            "updateCritical",
            *scenario.expectations.updateCritical ? "true" : "false",
            updateCheckResult.critical ? "true" : "false");
    }

    if (const auto mismatch = CheckStringList(
            "sourceRegistryValues",
            scenario.expectations.sourceRegistryValues,
            ExtractSourceRegistryValues(scenario.sourceRegistryJsons));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.sourceRegistryCount.has_value()) {
        const auto actualCount = static_cast<int>(
            ExtractSourceRegistryEntries(scenario.sourceRegistryJsons).size());
        if (actualCount != *scenario.expectations.sourceRegistryCount) {
            return PrintMismatch(
                "sourceRegistryCount",
                std::to_string(*scenario.expectations.sourceRegistryCount),
                std::to_string(actualCount));
        }
    }

    if (const auto mismatch = CheckStringList(
            "sourceRegistrySourceCounts",
            scenario.expectations.sourceRegistrySourceCounts,
            ExtractSourceRegistrySourceCounts(scenario.sourceRegistryJsons));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.resolverRouteAvailable.has_value() &&
        resolverRouteSectorSnapshot.available !=
            *scenario.expectations.resolverRouteAvailable) {
        return PrintMismatch(
            "resolverRouteAvailable",
            *scenario.expectations.resolverRouteAvailable ? "true" : "false",
            resolverRouteSectorSnapshot.available ? "true" : "false");
    }

    if (scenario.expectations.resolverRouteResolved.has_value() &&
        resolverRouteSectorSnapshot.routeResolved !=
            *scenario.expectations.resolverRouteResolved) {
        return PrintMismatch(
            "resolverRouteResolved",
            *scenario.expectations.resolverRouteResolved ? "true" : "false",
            resolverRouteSectorSnapshot.routeResolved ? "true" : "false");
    }

    if (scenario.expectations.resolverRouteStatus.has_value() &&
        resolverRouteSectorSnapshot.statusLine != *scenario.expectations.resolverRouteStatus) {
        return PrintMismatch(
            "resolverRouteStatus",
            *scenario.expectations.resolverRouteStatus,
            resolverRouteSectorSnapshot.statusLine);
    }

    if (const auto mismatch = CheckStringList(
            "resolverRouteAuthorityGaps",
            scenario.expectations.resolverRouteAuthorityGaps,
            ExtractAuthorityGaps(resolverRouteSectorSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "resolverRouteCurrentSectors",
            scenario.expectations.resolverRouteCurrentSectors,
            ExtractSectorIdentifiers(resolverRouteSectorSnapshot.currentSectors));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "resolverRouteNextSectors",
            scenario.expectations.resolverRouteNextSectors,
            ExtractSectorIdentifiers(resolverRouteSectorSnapshot.nextSectors));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "resolverRouteCurrentControllerPatterns",
            scenario.expectations.resolverRouteCurrentControllerPatterns,
            ExtractSectorControllerPatterns(resolverRouteSectorSnapshot.currentSectors));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "resolverRouteNextControllerPatterns",
            scenario.expectations.resolverRouteNextControllerPatterns,
            ExtractSectorControllerPatterns(resolverRouteSectorSnapshot.nextSectors));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "resolverRouteCurrentControllerPrefixes",
            scenario.expectations.resolverRouteCurrentControllerPrefixes,
            ExtractSectorControllerPrefixes(resolverRouteSectorSnapshot.currentSectors));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "resolverRouteNextControllerPrefixes",
            scenario.expectations.resolverRouteNextControllerPrefixes,
            ExtractSectorControllerPrefixes(resolverRouteSectorSnapshot.nextSectors));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "resolverRouteGenerations",
            scenario.expectations.resolverRouteGenerations,
            ExtractRouteGenerations(resolverRouteSectorSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "routeAuthorityPlanSequence",
            scenario.expectations.routeAuthorityPlanSequence,
            xvatsim::brain::RouteAuthorityPlanPolygonSequence(
                resolverRouteAuthorityPlan));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "routeAuthorityPlanFlags",
            scenario.expectations.routeAuthorityPlanFlags,
            xvatsim::brain::RouteAuthorityPlanFlagSummary(
                resolverRouteAuthorityPlan));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "routeAuthorityPlanSources",
            scenario.expectations.routeAuthorityPlanSources,
            xvatsim::brain::RouteAuthorityPlanSourceSummary(
                resolverRouteAuthorityPlan));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.resolverAuthorityRelevanceAvailable.has_value() &&
        resolverAuthorityRelevanceSnapshot.available !=
            *scenario.expectations.resolverAuthorityRelevanceAvailable) {
        return PrintMismatch(
            "resolverAuthorityRelevanceAvailable",
            *scenario.expectations.resolverAuthorityRelevanceAvailable ? "true" : "false",
            resolverAuthorityRelevanceSnapshot.available ? "true" : "false");
    }

    if (scenario.expectations.resolverAuthorityStatus.has_value() &&
        resolverAuthorityRelevanceSnapshot.statusLine !=
            *scenario.expectations.resolverAuthorityStatus) {
        return PrintMismatch(
            "resolverAuthorityStatus",
            *scenario.expectations.resolverAuthorityStatus,
            resolverAuthorityRelevanceSnapshot.statusLine);
    }

    if (scenario.expectations.resolverAuthorityCacheStatus.has_value() &&
        resolverAuthorityRelevanceSnapshot.diagnosticCacheStatus !=
            *scenario.expectations.resolverAuthorityCacheStatus) {
        return PrintMismatch(
            "resolverAuthorityCacheStatus",
            *scenario.expectations.resolverAuthorityCacheStatus,
            resolverAuthorityRelevanceSnapshot.diagnosticCacheStatus);
    }

    if (scenario.expectations.resolverAuthorityCacheReason.has_value() &&
        resolverAuthorityRelevanceSnapshot.diagnosticReason !=
            *scenario.expectations.resolverAuthorityCacheReason) {
        return PrintMismatch(
            "resolverAuthorityCacheReason",
            *scenario.expectations.resolverAuthorityCacheReason,
            resolverAuthorityRelevanceSnapshot.diagnosticReason);
    }

    if (scenario.expectations.resolverAuthorityRepeatCacheStatus.has_value() &&
        resolverAuthorityRepeatSnapshot.diagnosticCacheStatus !=
            *scenario.expectations.resolverAuthorityRepeatCacheStatus) {
        return PrintMismatch(
            "resolverAuthorityRepeatCacheStatus",
            *scenario.expectations.resolverAuthorityRepeatCacheStatus,
            resolverAuthorityRepeatSnapshot.diagnosticCacheStatus);
    }

    if (scenario.expectations.resolverAuthorityRepeatCacheReason.has_value() &&
        resolverAuthorityRepeatSnapshot.diagnosticReason !=
            *scenario.expectations.resolverAuthorityRepeatCacheReason) {
        return PrintMismatch(
            "resolverAuthorityRepeatCacheReason",
            *scenario.expectations.resolverAuthorityRepeatCacheReason,
            resolverAuthorityRepeatSnapshot.diagnosticReason);
    }

    if (const auto mismatch = CheckStringList(
            "resolverAuthorityDiagnostics",
            scenario.expectations.resolverAuthorityDiagnostics,
            ExtractAuthorityRelevanceDiagnostics(resolverAuthorityRelevanceSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "resolverAuthorityRelevantMatches",
            scenario.expectations.resolverAuthorityRelevantMatches,
            ExtractAuthorityRelevanceMatches(resolverAuthorityRelevanceSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "resolverAuthorityRepeatRelevantMatches",
            scenario.expectations.resolverAuthorityRepeatRelevantMatches,
            ExtractAuthorityRelevanceMatches(resolverAuthorityRepeatSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "resolverAuthorityProofSources",
            scenario.expectations.resolverAuthorityProofSources,
            ExtractAuthorityRelevanceProofSources(resolverAuthorityRelevanceSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "resolverAuthorityProofDetails",
            scenario.expectations.resolverAuthorityProofDetails,
            ExtractAuthorityRelevanceProofDetails(resolverAuthorityRelevanceSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringListContains(
            "resolverAuthorityProofDetailContains",
            scenario.expectations.resolverAuthorityProofDetailContains,
            ExtractAuthorityRelevanceProofDetails(resolverAuthorityRelevanceSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.resolverAuthorityEvidenceVisibility.has_value()) {
        const auto actual = AuthorityRelevanceEvidenceVisibilitySummary(
            resolverAuthorityRelevanceSnapshot);
        if (actual !=
            *scenario.expectations.resolverAuthorityEvidenceVisibility) {
            return PrintMismatch(
                "resolverAuthorityEvidenceVisibility",
                *scenario.expectations.resolverAuthorityEvidenceVisibility,
                actual);
        }
    }

    if (const auto mismatch = CheckStringList(
            "resolverAuthorityControllerEvidence",
            scenario.expectations.resolverAuthorityControllerEvidence,
            ExtractAuthorityControllerEvidence(resolverAuthorityRelevanceSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "resolverAuthorityDecisionEvidence",
            scenario.expectations.resolverAuthorityDecisionEvidence,
            ExtractAuthorityDecisionEvidence(resolverAuthorityRelevanceSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringListContains(
            "resolverAuthorityPolygonEvidenceContains",
            scenario.expectations.resolverAuthorityPolygonEvidenceContains,
            ExtractAuthorityPolygonEvidence(resolverAuthorityRelevanceSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringListContains(
            "resolverAuthorityActivePolygonEvidenceContains",
            scenario.expectations.resolverAuthorityActivePolygonEvidenceContains,
            ExtractAuthorityActivePolygonEvidence(
                resolverAuthorityRelevanceSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringListContains(
            "resolverAuthorityTransceiverProofEvidenceContains",
            scenario.expectations.resolverAuthorityTransceiverProofEvidenceContains,
            ExtractAuthorityTransceiverProofEvidence(
                resolverAuthorityRelevanceSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringListContains(
            "resolverAuthorityDuplicatedAtisProofEvidenceContains",
            scenario.expectations.resolverAuthorityDuplicatedAtisProofEvidenceContains,
            ExtractAuthorityDuplicatedAtisProofEvidence(
                resolverAuthorityRelevanceSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    const auto resolverAuthorityRelevancePreview =
        xvatsim::brain::BuildBrainAuthorityRelevanceDecisionPreview(
            resolverAuthorityRelevanceSnapshot);
    if (scenario.expectations.resolverAuthorityPreviewSummary.has_value()) {
        const auto actual = BrainAuthorityRelevancePreviewSummaryText(
            resolverAuthorityRelevancePreview);
        if (actual != *scenario.expectations.resolverAuthorityPreviewSummary) {
            return PrintMismatch(
                "resolverAuthorityPreviewSummary",
                *scenario.expectations.resolverAuthorityPreviewSummary,
                actual);
        }
    }

    if (const auto mismatch = CheckStringListContains(
            "resolverAuthorityPreviewDecisionsContains",
            scenario.expectations.resolverAuthorityPreviewDecisionsContains,
            BrainAuthorityRelevancePreviewDecisionSummaries(
                resolverAuthorityRelevancePreview));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.resolverEnrouteAvailable.has_value() &&
        resolverEnrouteBoard.available != *scenario.expectations.resolverEnrouteAvailable) {
        return PrintMismatch(
            "resolverEnrouteAvailable",
            *scenario.expectations.resolverEnrouteAvailable ? "true" : "false",
            resolverEnrouteBoard.available ? "true" : "false");
    }

    if (const auto mismatch = CheckStringList(
            "resolverEnrouteCallsigns",
            scenario.expectations.resolverEnrouteCallsigns,
            ExtractCallsigns(resolverEnrouteBoard));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.routeResolved.has_value() &&
        traversedRouteSnapshot.routeResolved != *scenario.expectations.routeResolved) {
        return PrintMismatch(
            "routeResolved",
            *scenario.expectations.routeResolved ? "true" : "false",
            traversedRouteSnapshot.routeResolved ? "true" : "false");
    }

    if (const auto mismatch = CheckStringList(
            "routeCurrentSectors",
            scenario.expectations.routeCurrentSectors,
            ExtractSectorIdentifiers(traversedRouteSnapshot.currentSectors));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "routeNextSectors",
            scenario.expectations.routeNextSectors,
            ExtractSectorIdentifiers(traversedRouteSnapshot.nextSectors));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "routeCurrentControllerPrefixes",
            scenario.expectations.routeCurrentControllerPrefixes,
            ExtractSectorControllerPrefixes(traversedRouteSnapshot.currentSectors));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "routeNextControllerPrefixes",
            scenario.expectations.routeNextControllerPrefixes,
            ExtractSectorControllerPrefixes(traversedRouteSnapshot.nextSectors));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (!scenario.expectations.routeTokenKinds.empty()) {
        const auto actualKinds = ExtractTokenKinds(parsedRouteTokens);
        if (actualKinds != scenario.expectations.routeTokenKinds) {
            std::ostringstream expected;
            std::ostringstream actual;
            for (const auto& value : scenario.expectations.routeTokenKinds) {
                if (expected.tellp() > 0) {
                    expected << ",";
                }
                expected << value;
            }
            for (const auto& value : actualKinds) {
                if (actual.tellp() > 0) {
                    actual << ",";
                }
                actual << value;
            }
            return PrintMismatch("routeTokenKinds", expected.str(), actual.str());
        }
    }

    if (!scenario.expectations.resolvedWaypointIdents.empty()) {
        const auto actualWaypoints = ExtractWaypointIdents(resolvedRouteWaypoints);
        if (actualWaypoints != scenario.expectations.resolvedWaypointIdents) {
            std::ostringstream expected;
            std::ostringstream actual;
            for (const auto& ident : scenario.expectations.resolvedWaypointIdents) {
                if (expected.tellp() > 0) {
                    expected << ",";
                }
                expected << ident;
            }
            for (const auto& ident : actualWaypoints) {
                if (actual.tellp() > 0) {
                    actual << ",";
                }
                actual << ident;
            }
            return PrintMismatch("resolvedWaypoints", expected.str(), actual.str());
        }
    }

    if (!scenario.expectations.resolvedWaypointPoints.empty()) {
        bool matches = resolvedRouteWaypoints.size() == scenario.expectations.resolvedWaypointPoints.size();
        if (matches) {
            constexpr double kToleranceDeg = 1e-4;
            for (std::size_t index = 0; index < resolvedRouteWaypoints.size(); ++index) {
                const auto& actual = resolvedRouteWaypoints[index];
                const auto& expected = scenario.expectations.resolvedWaypointPoints[index];
                if (actual.ident != expected.ident ||
                    std::abs(actual.latitudeDeg - expected.latitudeDeg) > kToleranceDeg ||
                    std::abs(actual.longitudeDeg - expected.longitudeDeg) > kToleranceDeg) {
                    matches = false;
                    break;
                }
            }
        }

        if (!matches) {
            std::ostringstream expected;
            std::ostringstream actual;
            for (const auto& waypoint : scenario.expectations.resolvedWaypointPoints) {
                if (expected.tellp() > 0) {
                    expected << "|";
                }
                expected.setf(std::ios::fixed);
                expected.precision(4);
                expected << waypoint.ident << "@" << waypoint.latitudeDeg << "," << waypoint.longitudeDeg;
            }
            for (const auto& waypoint : resolvedRouteWaypoints) {
                if (actual.tellp() > 0) {
                    actual << "|";
                }
                actual << FormatWaypointPoint(waypoint);
            }
            return PrintMismatch("resolvedWaypointPoints", expected.str(), actual.str());
        }
    }

    const auto checkDiagnosticList =
        [&](const char* label,
            const std::vector<std::string>& expectedValues,
            const std::vector<std::string>& actualValues) -> std::optional<int> {
            if (expectedValues.empty()) {
                return std::nullopt;
            }
            if (expectedValues.size() == 1 &&
                ToUpperCopy(expectedValues.front()) == "<NONE>") {
                if (actualValues.empty()) {
                    return std::nullopt;
                }
            } else if (expectedValues == actualValues) {
                return std::nullopt;
            }

            std::ostringstream expected;
            std::ostringstream actual;
            for (const auto& value : expectedValues) {
                if (expected.tellp() > 0) {
                    expected << ",";
                }
                expected << value;
            }
            for (const auto& value : actualValues) {
                if (actual.tellp() > 0) {
                    actual << ",";
                }
                actual << value;
            }
            return PrintMismatch(label, expected.str(), actual.str());
        };

    if (const auto mismatch = checkDiagnosticList(
            "resolvedTokens",
            scenario.expectations.resolvedTokens,
            routeResolveDiagnostics.resolvedTokens);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "expandedTokens",
            scenario.expectations.expandedTokens,
            routeResolveDiagnostics.expandedTokens);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureTokens",
            scenario.expectations.recognizedProcedureTokens,
            routeResolveDiagnostics.recognizedProcedureTokens);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureSources",
            scenario.expectations.procedureMetadataSources,
            routeResolveDiagnostics.procedureMetadataSources);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureRecords",
            scenario.expectations.procedureRecordKinds,
            routeResolveDiagnostics.procedureRecordKinds);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureRunways",
            scenario.expectations.procedureRunwayRecords,
            routeResolveDiagnostics.procedureRunwayRecords);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureAuthorities",
            scenario.expectations.procedureCatalogAuthorities,
            routeResolveDiagnostics.procedureCatalogAuthorities);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureCatalogFixes",
            scenario.expectations.procedureCatalogFixes,
            routeResolveDiagnostics.procedureCatalogFixes);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureBoundaryFixes",
            scenario.expectations.procedureBoundaryFixes,
            routeResolveDiagnostics.procedureBoundaryFixes);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureOrderedFixes",
            scenario.expectations.procedureOrderedFixes,
            routeResolveDiagnostics.procedureOrderedFixes);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureSyntheticWaypoints",
            scenario.expectations.procedureSyntheticWaypoints,
            routeResolveDiagnostics.procedureSyntheticWaypoints);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureSyntheticSources",
            scenario.expectations.procedureSyntheticSources,
            routeResolveDiagnostics.procedureSyntheticSources);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureApplicationStates",
            scenario.expectations.procedureApplicationStates,
            routeResolveDiagnostics.procedureApplicationStates);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureApplicationBlocks",
            scenario.expectations.procedureApplicationBlocks,
            routeResolveDiagnostics.procedureApplicationBlocks);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureAppliedFixSequences",
            scenario.expectations.procedureAppliedFixSequences,
            routeResolveDiagnostics.procedureAppliedFixSequences);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureCatalogTransitions",
            scenario.expectations.procedureCatalogTransitions,
            routeResolveDiagnostics.procedureCatalogTransitions);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureSupport",
            scenario.expectations.procedureSupportDirections,
            routeResolveDiagnostics.procedureSupportDirections);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureLinks",
            scenario.expectations.procedureTransitionLinks,
            routeResolveDiagnostics.procedureTransitionLinks);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureMisses",
            scenario.expectations.procedureTransitionMisses,
            routeResolveDiagnostics.procedureTransitionMisses);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureAnchorLinks",
            scenario.expectations.procedureAnchorLinks,
            routeResolveDiagnostics.procedureAnchorLinks);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "procedureContextOnly",
            scenario.expectations.procedureContextOnlyTokens,
            routeResolveDiagnostics.procedureContextOnlyTokens);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "ignoredTokens",
            scenario.expectations.ignoredTokens,
            routeResolveDiagnostics.ignoredTokens);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "unsupportedTokens",
            scenario.expectations.unsupportedTokens,
            routeResolveDiagnostics.unsupportedTokens);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "unresolvedTokens",
            scenario.expectations.unresolvedTokens,
            routeResolveDiagnostics.unresolvedTokens);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = checkDiagnosticList(
            "unresolvedAirwayTokens",
            scenario.expectations.unresolvedAirwayTokens,
            routeResolveDiagnostics.unresolvedAirwayTokens);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.preflightParseOk.has_value() &&
        preflightParseResult.ok != *scenario.expectations.preflightParseOk) {
        return PrintMismatch(
            "preflightParseOk",
            *scenario.expectations.preflightParseOk ? "true" : "false",
            preflightParseResult.ok ? "true" : "false");
    }

    if (scenario.expectations.preflightValidationAccepted.has_value() &&
        preflightValidationResult.accepted !=
            *scenario.expectations.preflightValidationAccepted) {
        return PrintMismatch(
            "preflightValidationAccepted",
            *scenario.expectations.preflightValidationAccepted ? "true" : "false",
            preflightValidationResult.accepted ? "true" : "false");
    }

    if (scenario.expectations.preflightValidationReason.has_value() &&
        preflightValidationResult.reason !=
            *scenario.expectations.preflightValidationReason) {
        return PrintMismatch(
            "preflightValidationReason",
            *scenario.expectations.preflightValidationReason,
            preflightValidationResult.reason);
    }

    if (scenario.expectations.preflightDepartureIcao.has_value() &&
        preflightParseResult.plan.departureIcao !=
            *scenario.expectations.preflightDepartureIcao) {
        return PrintMismatch(
            "preflightDepartureIcao",
            *scenario.expectations.preflightDepartureIcao,
            preflightParseResult.plan.departureIcao);
    }

    if (scenario.expectations.preflightDestinationIcao.has_value() &&
        preflightParseResult.plan.destinationIcao !=
            *scenario.expectations.preflightDestinationIcao) {
        return PrintMismatch(
            "preflightDestinationIcao",
            *scenario.expectations.preflightDestinationIcao,
            preflightParseResult.plan.destinationIcao);
    }

    std::vector<std::string> preflightWaypointIdents;
    preflightWaypointIdents.reserve(preflightParseResult.plan.waypoints.size());
    for (const auto& waypoint : preflightParseResult.plan.waypoints) {
        preflightWaypointIdents.push_back(waypoint.ident);
    }
    if (const auto mismatch = CheckStringList(
            "preflightWaypoints",
            scenario.expectations.preflightWaypointIdents,
            preflightWaypointIdents);
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "brainWorkOrder",
            scenario.expectations.brainWorkOrder,
            BuildBrainWorkModelOrderProbe());
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "brainWorkHeavy",
            scenario.expectations.brainWorkHeavyFlags,
            BuildBrainWorkModelHeavyProbe());
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "brainSchedulerRunnable",
            scenario.expectations.brainSchedulerRunnable,
            BuildBrainSchedulerRunnableProbe());
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "brainSchedulerDeferred",
            scenario.expectations.brainSchedulerDeferred,
            BuildBrainSchedulerDeferredProbe());
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.brainSchedulerHeavyCounts.has_value() &&
        BuildBrainSchedulerHeavyCountsProbe() !=
            *scenario.expectations.brainSchedulerHeavyCounts) {
        return PrintMismatch(
            "brainSchedulerHeavyCounts",
            *scenario.expectations.brainSchedulerHeavyCounts,
            BuildBrainSchedulerHeavyCountsProbe());
    }

    const auto routePlanRebuildProbe = BuildBrainRoutePlanRebuildProbe();
    if (const auto mismatch = CheckStringList(
            "brainRoutePlanRebuildSequence",
            scenario.expectations.brainRoutePlanRebuildSequence,
            xvatsim::brain::RouteAuthorityPlanPolygonSequence(
                routePlanRebuildProbe));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.brainRoutePlanRebuildLifecycle.has_value()) {
        const auto actual =
            xvatsim::brain::RouteAuthorityPlanLifecycleSummary(
                routePlanRebuildProbe);
        if (actual != *scenario.expectations.brainRoutePlanRebuildLifecycle) {
            return PrintMismatch(
                "brainRoutePlanRebuildLifecycle",
                *scenario.expectations.brainRoutePlanRebuildLifecycle,
                actual);
        }
    }

    const auto routePlanPendingProbe = BuildBrainRoutePlanPendingProbe();
    if (const auto mismatch = CheckStringList(
            "brainRoutePlanPendingSequence",
            scenario.expectations.brainRoutePlanPendingSequence,
            xvatsim::brain::RouteAuthorityPlanPolygonSequence(
                routePlanPendingProbe));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.brainRoutePlanPendingLifecycle.has_value()) {
        const auto actual =
            xvatsim::brain::RouteAuthorityPlanLifecycleSummary(
                routePlanPendingProbe);
        if (actual != *scenario.expectations.brainRoutePlanPendingLifecycle) {
            return PrintMismatch(
                "brainRoutePlanPendingLifecycle",
                *scenario.expectations.brainRoutePlanPendingLifecycle,
                actual);
        }
    }

    if (const auto mismatch = CheckStringList(
            "brainDepartureWorkOrder",
            scenario.expectations.brainDepartureWorkOrder,
            BuildBrainDepartureWorkOrderProbe());
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "brainDepartureSchedulerRunnable",
            scenario.expectations.brainDepartureSchedulerRunnable,
            BuildBrainDepartureSchedulerRunnableProbe());
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "brainDepartureSchedulerDeferred",
            scenario.expectations.brainDepartureSchedulerDeferred,
            BuildBrainDepartureSchedulerDeferredProbe());
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.brainDepartureSchedulerHeavyCounts.has_value() &&
        BuildBrainDepartureSchedulerHeavyCountsProbe() !=
            *scenario.expectations.brainDepartureSchedulerHeavyCounts) {
        return PrintMismatch(
            "brainDepartureSchedulerHeavyCounts",
            *scenario.expectations.brainDepartureSchedulerHeavyCounts,
            BuildBrainDepartureSchedulerHeavyCountsProbe());
    }

    if (scenario.expectations.brainDepartureSnapshotLifecycle.has_value()) {
        const auto actual =
            xvatsim::brain::DepartureAuthoritySnapshotLifecycleSummary(
                BuildBrainDepartureSnapshotProbe());
        if (actual != *scenario.expectations.brainDepartureSnapshotLifecycle) {
            return PrintMismatch(
                "brainDepartureSnapshotLifecycle",
                *scenario.expectations.brainDepartureSnapshotLifecycle,
                actual);
        }
    }

    if (scenario.expectations.brainDeparturePendingLifecycle.has_value()) {
        const auto actual =
            xvatsim::brain::DepartureAuthoritySnapshotLifecycleSummary(
                BuildBrainDeparturePendingProbe());
        if (actual != *scenario.expectations.brainDeparturePendingLifecycle) {
            return PrintMismatch(
                "brainDeparturePendingLifecycle",
                *scenario.expectations.brainDeparturePendingLifecycle,
                actual);
        }
    }

    const auto radioReachableProbe = BuildRadioReachableProbeSnapshot();
    if (const auto mismatch = CheckStringList(
            "radioReachableCandidates",
            scenario.expectations.radioReachableCandidates,
            xvatsim::brain::RadioReachableCandidateSummaries(radioReachableProbe));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.radioReachableCounts.has_value()) {
        const auto actual =
            xvatsim::brain::RadioReachableGroupCountSummary(radioReachableProbe);
        if (actual != *scenario.expectations.radioReachableCounts) {
            return PrintMismatch(
                "radioReachableCounts",
                *scenario.expectations.radioReachableCounts,
                actual);
        }
    }

    if (scenario.expectations.radioReachableHashCheck.has_value()) {
        const auto actual = BuildRadioReachableHashCheckProbe();
        if (actual != *scenario.expectations.radioReachableHashCheck) {
            return PrintMismatch(
                "radioReachableHashCheck",
                *scenario.expectations.radioReachableHashCheck,
                actual);
        }
    }

    xvatsim::brain::RadioReachableBuildOptions radioReachableSourceOptions;
    radioReachableSourceOptions.generation = controllerFeedSnapshot.generation;
    radioReachableSourceOptions.source =
        xvatsim::brain::RadioReachableSource::AFVRadioRange;
    radioReachableSourceOptions.changeReason = "harness-transceiver-source";
    radioReachableSourceOptions.nowSeconds = scenario.nowSeconds;
    const auto radioReachableSourceSnapshot =
        xvatsim::brain::BuildRadioReachableControllerSnapshotFromTransceivers(
            scenario.transceiverResolutionSnapshot,
            controllerFeedSnapshot,
            radioReachableSourceOptions);
    if (const auto mismatch = CheckStringList(
            "radioReachableSourceCandidates",
            scenario.expectations.radioReachableSourceCandidates,
            xvatsim::brain::RadioReachableCandidateSummaries(
                radioReachableSourceSnapshot));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.radioReachableSourceCounts.has_value()) {
        const auto actual =
            xvatsim::brain::RadioReachableGroupCountSummary(
                radioReachableSourceSnapshot);
        if (actual != *scenario.expectations.radioReachableSourceCounts) {
            return PrintMismatch(
                "radioReachableSourceCounts",
                *scenario.expectations.radioReachableSourceCounts,
                actual);
        }
    }

    const auto shouldRunTransceiverResolverHoldoverProbe =
        scenario.transceiverResolverHoldoverProbe ||
        scenario.expectations.transceiverResolverHoldoverAvailable.has_value() ||
        scenario.expectations.transceiverResolverHoldoverStale.has_value() ||
        scenario.expectations.transceiverResolverHoldoverStatusContains.has_value() ||
        !scenario.expectations.transceiverResolverHoldoverCandidates.empty() ||
        !scenario.expectations.transceiverResolverHoldoverRadioCandidates.empty() ||
        scenario.expectations.transceiverResolverHoldoverRadioCounts.has_value() ||
        scenario.expectations.transceiverResolverHoldoverRadioStatusContains.has_value() ||
        scenario.expectations.transceiverResolverHoldoverSourceEvidence.has_value() ||
        scenario.expectations.transceiverResolverHoldoverEvidenceVisibility.has_value() ||
        !scenario.expectations.transceiverResolverHoldoverControllerEvidence.empty() ||
        !scenario.expectations.transceiverResolverHoldoverStationEvidence.empty() ||
        scenario.expectations
            .transceiverResolverHoldoverBrainPreviewSummary.has_value() ||
        !scenario.expectations
             .transceiverResolverHoldoverBrainPreviewDecisions.empty();
    if (shouldRunTransceiverResolverHoldoverProbe) {
        xvatsim::modules::transceiver_resolver::TransceiverResolver resolver;
        resolver.SeedFeedCacheForTesting(
            BuildCachedTransceiversForResolverProbe(
                scenario.transceiverResolutionSnapshot),
            scenario.transceiverResolverHoldoverCacheAgeSeconds,
            scenario.transceiverResolverHoldoverLastFetchSucceeded);

        const auto transceiverResolverHoldoverSnapshot =
            resolver.Resolve(scenario.aircraftState, controllerFeedSnapshot);
        xvatsim::brain::BrainRadioRangeWorkerInput previewInput;
        previewInput.aircraft = scenario.aircraftState;
        previewInput.radios = scenario.radioStateSnapshot;
        previewInput.controllerFeed = controllerFeedSnapshot;
        previewInput.planKey = scenario.name;
        const auto transceiverResolverBrainPreviewOutput =
            xvatsim::brain::BuildBrainRadioRangeWorkerOutput(
                previewInput,
                transceiverResolverHoldoverSnapshot,
                scenario.nowSeconds);
        if (scenario.expectations.transceiverResolverHoldoverAvailable.has_value() &&
            transceiverResolverHoldoverSnapshot.available !=
                *scenario.expectations.transceiverResolverHoldoverAvailable) {
            return PrintMismatch(
                "transceiverResolverHoldoverAvailable",
                *scenario.expectations.transceiverResolverHoldoverAvailable
                    ? "true"
                    : "false",
                transceiverResolverHoldoverSnapshot.available ? "true" : "false");
        }
        if (scenario.expectations.transceiverResolverHoldoverStale.has_value() &&
            transceiverResolverHoldoverSnapshot.stale !=
                *scenario.expectations.transceiverResolverHoldoverStale) {
            return PrintMismatch(
                "transceiverResolverHoldoverStale",
                *scenario.expectations.transceiverResolverHoldoverStale
                    ? "true"
                    : "false",
                transceiverResolverHoldoverSnapshot.stale ? "true" : "false");
        }
        if (scenario.expectations.transceiverResolverHoldoverStatusContains
                .has_value() &&
            transceiverResolverHoldoverSnapshot.statusLine.find(
                *scenario.expectations.transceiverResolverHoldoverStatusContains) ==
                std::string::npos) {
            return PrintMismatch(
                "transceiverResolverHoldoverStatusContains",
                *scenario.expectations.transceiverResolverHoldoverStatusContains,
                transceiverResolverHoldoverSnapshot.statusLine);
        }
        if (const auto mismatch = CheckStringList(
                "transceiverResolverHoldoverCandidates",
                scenario.expectations.transceiverResolverHoldoverCandidates,
                TransceiverResolverCandidateSummaries(
                    transceiverResolverHoldoverSnapshot));
            mismatch.has_value()) {
            return *mismatch;
        }
        if (scenario.expectations.transceiverResolverHoldoverSourceEvidence
                .has_value()) {
            const auto actual = TransceiverResolverSourceEvidenceSummary(
                transceiverResolverHoldoverSnapshot);
            if (actual !=
                *scenario.expectations
                     .transceiverResolverHoldoverSourceEvidence) {
                return PrintMismatch(
                    "transceiverResolverHoldoverSourceEvidence",
                    *scenario.expectations
                         .transceiverResolverHoldoverSourceEvidence,
                    actual);
            }
        }
        if (scenario.expectations.transceiverResolverHoldoverEvidenceVisibility
                .has_value()) {
            const auto actual = TransceiverResolverEvidenceVisibilitySummary(
                controllerFeedSnapshot,
                transceiverResolverHoldoverSnapshot);
            if (actual !=
                *scenario.expectations
                     .transceiverResolverHoldoverEvidenceVisibility) {
                return PrintMismatch(
                    "transceiverResolverHoldoverEvidenceVisibility",
                    *scenario.expectations
                         .transceiverResolverHoldoverEvidenceVisibility,
                    actual);
            }
        }
        if (const auto mismatch = CheckStringList(
                "transceiverResolverHoldoverControllerEvidence",
                scenario.expectations
                    .transceiverResolverHoldoverControllerEvidence,
                TransceiverResolverControllerEvidenceSummaries(
                    transceiverResolverHoldoverSnapshot));
            mismatch.has_value()) {
            return *mismatch;
        }
        if (const auto mismatch = CheckStringList(
                "transceiverResolverHoldoverStationEvidence",
                scenario.expectations
                    .transceiverResolverHoldoverStationEvidence,
                TransceiverResolverStationEvidenceSummaries(
                    transceiverResolverHoldoverSnapshot));
            mismatch.has_value()) {
            return *mismatch;
        }
        if (scenario.expectations
                .transceiverResolverHoldoverBrainPreviewSummary.has_value()) {
            const auto actual = BrainRadioRangePreviewSummaryText(
                transceiverResolverBrainPreviewOutput.decisionPreview);
            if (actual !=
                *scenario.expectations
                     .transceiverResolverHoldoverBrainPreviewSummary) {
                return PrintMismatch(
                    "transceiverResolverHoldoverBrainPreviewSummary",
                    *scenario.expectations
                         .transceiverResolverHoldoverBrainPreviewSummary,
                    actual);
            }
        }
        if (const auto mismatch = CheckStringList(
                "transceiverResolverHoldoverBrainPreviewDecisions",
                scenario.expectations
                    .transceiverResolverHoldoverBrainPreviewDecisions,
                BrainRadioRangePreviewDecisionSummaries(
                    transceiverResolverBrainPreviewOutput.decisionPreview));
            mismatch.has_value()) {
            return *mismatch;
        }

        if (const auto mismatch = CheckStringList(
                "transceiverResolverHoldoverRadioCandidates",
                scenario.expectations.transceiverResolverHoldoverRadioCandidates,
                xvatsim::brain::RadioReachableCandidateSummaries(
                    transceiverResolverBrainPreviewOutput.radioBoard));
            mismatch.has_value()) {
            return *mismatch;
        }
        if (scenario.expectations.transceiverResolverHoldoverRadioCounts
                .has_value()) {
            const auto actual =
                xvatsim::brain::RadioReachableGroupCountSummary(
                    transceiverResolverBrainPreviewOutput.radioBoard);
            if (actual !=
                *scenario.expectations.transceiverResolverHoldoverRadioCounts) {
                return PrintMismatch(
                    "transceiverResolverHoldoverRadioCounts",
                    *scenario.expectations.transceiverResolverHoldoverRadioCounts,
                    actual);
            }
        }
        if (scenario.expectations
                .transceiverResolverHoldoverRadioStatusContains.has_value() &&
            transceiverResolverBrainPreviewOutput.radioBoard.statusLine.find(
                *scenario.expectations
                     .transceiverResolverHoldoverRadioStatusContains) ==
                std::string::npos) {
            return PrintMismatch(
                "transceiverResolverHoldoverRadioStatusContains",
                *scenario.expectations
                     .transceiverResolverHoldoverRadioStatusContains,
                transceiverResolverBrainPreviewOutput.radioBoard.statusLine);
        }
    }

    const auto shouldRunTransceiverResolverAuthorityProbe =
        scenario.transceiverResolverAuthorityProbe ||
        scenario.expectations.transceiverResolverAuthorityAvailable.has_value() ||
        scenario.expectations.transceiverResolverAuthorityStale.has_value() ||
        scenario.expectations.transceiverResolverAuthorityStatusContains.has_value() ||
        !scenario.expectations.transceiverResolverAuthorityCandidates.empty() ||
        scenario.expectations.transceiverResolverAuthoritySourceEvidence
            .has_value() ||
        scenario.expectations.transceiverResolverAuthorityEvidenceVisibility
            .has_value() ||
        !scenario.expectations.transceiverResolverAuthorityControllerEvidence
             .empty() ||
        !scenario.expectations.transceiverResolverAuthorityStationEvidence.empty() ||
        scenario.expectations.transceiverResolverAuthorityPreviewSummary
            .has_value() ||
        !scenario.expectations.transceiverResolverAuthorityPreviewDecisions
             .empty();
    if (shouldRunTransceiverResolverAuthorityProbe) {
        xvatsim::modules::transceiver_resolver::TransceiverResolver resolver;
        resolver.SeedFeedCacheForTesting(
            BuildCachedTransceiversForResolverProbe(
                scenario.transceiverResolutionSnapshot),
            scenario.transceiverResolverAuthorityCacheAgeSeconds,
            scenario.transceiverResolverAuthorityLastFetchSucceeded);

        const auto resolverAuthoritySnapshot =
            resolver.ResolveAuthorityStations(controllerFeedSnapshot);
        const auto authorityPreview =
            xvatsim::brain::BuildBrainAuthorityStationsDecisionPreview(
                resolverAuthoritySnapshot);
        const auto authoritySnapshot =
            xvatsim::brain::BuildBrainOwnedAuthorityStationsCandidateSnapshot(
                resolverAuthoritySnapshot,
                authorityPreview);
        if (scenario.expectations.transceiverResolverAuthorityAvailable
                .has_value() &&
            authoritySnapshot.available !=
                *scenario.expectations.transceiverResolverAuthorityAvailable) {
            return PrintMismatch(
                "transceiverResolverAuthorityAvailable",
                *scenario.expectations.transceiverResolverAuthorityAvailable
                    ? "true"
                    : "false",
                authoritySnapshot.available ? "true" : "false");
        }
        if (scenario.expectations.transceiverResolverAuthorityStale
                .has_value() &&
            authoritySnapshot.stale !=
                *scenario.expectations.transceiverResolverAuthorityStale) {
            return PrintMismatch(
                "transceiverResolverAuthorityStale",
                *scenario.expectations.transceiverResolverAuthorityStale
                    ? "true"
                    : "false",
                authoritySnapshot.stale ? "true" : "false");
        }
        if (scenario.expectations
                .transceiverResolverAuthorityStatusContains.has_value() &&
            authoritySnapshot.statusLine.find(
                *scenario.expectations
                     .transceiverResolverAuthorityStatusContains) ==
                std::string::npos) {
            return PrintMismatch(
                "transceiverResolverAuthorityStatusContains",
                *scenario.expectations
                     .transceiverResolverAuthorityStatusContains,
                authoritySnapshot.statusLine);
        }
        if (const auto mismatch = CheckStringList(
                "transceiverResolverAuthorityCandidates",
                scenario.expectations.transceiverResolverAuthorityCandidates,
                TransceiverResolverCandidateSummaries(authoritySnapshot));
            mismatch.has_value()) {
            return *mismatch;
        }
        if (scenario.expectations.transceiverResolverAuthoritySourceEvidence
                .has_value()) {
            const auto actual =
                TransceiverResolverSourceEvidenceSummary(
                    resolverAuthoritySnapshot);
            if (actual !=
                *scenario.expectations
                     .transceiverResolverAuthoritySourceEvidence) {
                return PrintMismatch(
                    "transceiverResolverAuthoritySourceEvidence",
                    *scenario.expectations
                         .transceiverResolverAuthoritySourceEvidence,
                    actual);
            }
        }
        if (scenario.expectations
                .transceiverResolverAuthorityEvidenceVisibility.has_value()) {
            const auto actual =
                TransceiverResolverAuthorityEvidenceVisibilitySummary(
                    controllerFeedSnapshot,
                    resolverAuthoritySnapshot);
            if (actual !=
                *scenario.expectations
                     .transceiverResolverAuthorityEvidenceVisibility) {
                return PrintMismatch(
                    "transceiverResolverAuthorityEvidenceVisibility",
                    *scenario.expectations
                         .transceiverResolverAuthorityEvidenceVisibility,
                    actual);
            }
        }
        if (const auto mismatch = CheckStringList(
                "transceiverResolverAuthorityControllerEvidence",
                scenario.expectations
                    .transceiverResolverAuthorityControllerEvidence,
                TransceiverResolverAuthorityControllerEvidenceSummaries(
                    resolverAuthoritySnapshot));
            mismatch.has_value()) {
            return *mismatch;
        }
        if (const auto mismatch = CheckStringList(
                "transceiverResolverAuthorityStationEvidence",
                scenario.expectations
                    .transceiverResolverAuthorityStationEvidence,
                TransceiverResolverAuthorityStationEvidenceSummaries(
                    resolverAuthoritySnapshot));
            mismatch.has_value()) {
            return *mismatch;
        }
        if (scenario.expectations.transceiverResolverAuthorityPreviewSummary
                .has_value()) {
            const auto actual =
                BrainAuthorityStationsPreviewSummaryText(authorityPreview);
            if (actual !=
                *scenario.expectations
                     .transceiverResolverAuthorityPreviewSummary) {
                return PrintMismatch(
                    "transceiverResolverAuthorityPreviewSummary",
                    *scenario.expectations
                         .transceiverResolverAuthorityPreviewSummary,
                    actual);
            }
        }
        if (const auto mismatch = CheckStringList(
                "transceiverResolverAuthorityPreviewDecisions",
                scenario.expectations
                    .transceiverResolverAuthorityPreviewDecisions,
                BrainAuthorityStationsPreviewDecisionSummaries(
                    authorityPreview));
            mismatch.has_value()) {
            return *mismatch;
        }
    }

    const auto shouldRunTransceiverResolverAirportCoverageProbe =
        scenario.transceiverResolverAirportCoverageProbe ||
        scenario.expectations.transceiverResolverAirportCoverageAvailable
            .has_value() ||
        scenario.expectations.transceiverResolverAirportCoverageStale
            .has_value() ||
        scenario.expectations
            .transceiverResolverAirportCoverageStatusContains.has_value() ||
        !scenario.expectations.transceiverResolverAirportCoverageCandidates
             .empty() ||
        scenario.expectations.transceiverResolverAirportCoverageSourceEvidence
            .has_value() ||
        scenario.expectations
            .transceiverResolverAirportCoverageEvidenceVisibility.has_value() ||
        !scenario.expectations
             .transceiverResolverAirportCoverageControllerEvidence.empty() ||
        !scenario.expectations
             .transceiverResolverAirportCoverageStationEvidence.empty() ||
        scenario.expectations
            .transceiverResolverAirportCoveragePreviewSummary.has_value() ||
        !scenario.expectations
             .transceiverResolverAirportCoveragePreviewDecisions.empty();
    if (shouldRunTransceiverResolverAirportCoverageProbe) {
        xvatsim::modules::transceiver_resolver::TransceiverResolver resolver;
        resolver.SeedFeedCacheForTesting(
            BuildCachedTransceiversForResolverProbe(
                scenario.transceiverResolutionSnapshot),
            scenario.transceiverResolverAirportCoverageCacheAgeSeconds,
            scenario.transceiverResolverAirportCoverageLastFetchSucceeded);

        const auto resolverAirportCoverageSnapshot =
            resolver.ResolveAirportCoverage(
                controllerFeedSnapshot,
                scenario.transceiverResolverAirportCoverageHasCoordinates,
                scenario.transceiverResolverAirportCoverageLatitudeDeg,
                scenario.transceiverResolverAirportCoverageLongitudeDeg);
        const auto airportCoveragePreview =
            xvatsim::brain::BuildBrainAirportCoverageDecisionPreview(
                resolverAirportCoverageSnapshot);
        const auto airportCoverageSnapshot =
            xvatsim::brain::BuildBrainOwnedAirportCoverageCandidateSnapshot(
                resolverAirportCoverageSnapshot,
                airportCoveragePreview);
        if (scenario.expectations
                .transceiverResolverAirportCoverageAvailable.has_value() &&
            airportCoverageSnapshot.available !=
                *scenario.expectations
                     .transceiverResolverAirportCoverageAvailable) {
            return PrintMismatch(
                "transceiverResolverAirportCoverageAvailable",
                *scenario.expectations
                         .transceiverResolverAirportCoverageAvailable
                    ? "true"
                    : "false",
                airportCoverageSnapshot.available ? "true" : "false");
        }
        if (scenario.expectations
                .transceiverResolverAirportCoverageStale.has_value() &&
            airportCoverageSnapshot.stale !=
                *scenario.expectations
                     .transceiverResolverAirportCoverageStale) {
            return PrintMismatch(
                "transceiverResolverAirportCoverageStale",
                *scenario.expectations
                         .transceiverResolverAirportCoverageStale
                    ? "true"
                    : "false",
                airportCoverageSnapshot.stale ? "true" : "false");
        }
        if (scenario.expectations
                .transceiverResolverAirportCoverageStatusContains
                .has_value() &&
            airportCoverageSnapshot.statusLine.find(
                *scenario.expectations
                     .transceiverResolverAirportCoverageStatusContains) ==
                std::string::npos) {
            return PrintMismatch(
                "transceiverResolverAirportCoverageStatusContains",
                *scenario.expectations
                     .transceiverResolverAirportCoverageStatusContains,
                airportCoverageSnapshot.statusLine);
        }
        if (const auto mismatch = CheckStringList(
                "transceiverResolverAirportCoverageCandidates",
                scenario.expectations
                    .transceiverResolverAirportCoverageCandidates,
                TransceiverResolverCandidateSummaries(
                    airportCoverageSnapshot));
            mismatch.has_value()) {
            return *mismatch;
        }
        if (scenario.expectations
                .transceiverResolverAirportCoverageSourceEvidence
                .has_value()) {
            const auto actual = TransceiverResolverSourceEvidenceSummary(
                resolverAirportCoverageSnapshot);
            if (actual !=
                *scenario.expectations
                     .transceiverResolverAirportCoverageSourceEvidence) {
                return PrintMismatch(
                    "transceiverResolverAirportCoverageSourceEvidence",
                    *scenario.expectations
                         .transceiverResolverAirportCoverageSourceEvidence,
                    actual);
            }
        }
        if (scenario.expectations
                .transceiverResolverAirportCoverageEvidenceVisibility
                .has_value()) {
            const auto actual =
                TransceiverResolverAirportCoverageEvidenceVisibilitySummary(
                    controllerFeedSnapshot,
                    resolverAirportCoverageSnapshot);
            if (actual !=
                *scenario.expectations
                     .transceiverResolverAirportCoverageEvidenceVisibility) {
                return PrintMismatch(
                    "transceiverResolverAirportCoverageEvidenceVisibility",
                    *scenario.expectations
                         .transceiverResolverAirportCoverageEvidenceVisibility,
                    actual);
            }
        }
        if (const auto mismatch = CheckStringList(
                "transceiverResolverAirportCoverageControllerEvidence",
                scenario.expectations
                    .transceiverResolverAirportCoverageControllerEvidence,
                TransceiverResolverAirportCoverageControllerEvidenceSummaries(
                    resolverAirportCoverageSnapshot));
            mismatch.has_value()) {
            return *mismatch;
        }
        if (const auto mismatch = CheckStringList(
                "transceiverResolverAirportCoverageStationEvidence",
                scenario.expectations
                    .transceiverResolverAirportCoverageStationEvidence,
                TransceiverResolverAirportCoverageStationEvidenceSummaries(
                    resolverAirportCoverageSnapshot));
            mismatch.has_value()) {
            return *mismatch;
        }
        if (scenario.expectations
                .transceiverResolverAirportCoveragePreviewSummary
                .has_value()) {
            const auto actual = BrainAirportCoveragePreviewSummaryText(
                airportCoveragePreview);
            if (actual !=
                *scenario.expectations
                     .transceiverResolverAirportCoveragePreviewSummary) {
                return PrintMismatch(
                    "transceiverResolverAirportCoveragePreviewSummary",
                    *scenario.expectations
                         .transceiverResolverAirportCoveragePreviewSummary,
                    actual);
            }
        }
        if (const auto mismatch = CheckStringList(
                "transceiverResolverAirportCoveragePreviewDecisions",
                scenario.expectations
                    .transceiverResolverAirportCoveragePreviewDecisions,
                BrainAirportCoveragePreviewDecisionSummaries(
                    airportCoveragePreview));
            mismatch.has_value()) {
            return *mismatch;
        }
    }

    if (const auto mismatch = CheckStringList(
            "radioReachableGateDepartureCandidates",
            scenario.expectations.radioReachableGateDepartureCandidates,
            xvatsim::brain::RadioReachableCandidateSummaries(
                BuildRadioReachablePhaseGateProbe(WorkflowStage::Departure)));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "radioReachableGateEnrouteCandidates",
            scenario.expectations.radioReachableGateEnrouteCandidates,
            xvatsim::brain::RadioReachableCandidateSummaries(
                BuildRadioReachablePhaseGateProbe(WorkflowStage::Enroute)));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "radioReachableGateArrivalCandidates",
            scenario.expectations.radioReachableGateArrivalCandidates,
            xvatsim::brain::RadioReachableCandidateSummaries(
                BuildRadioReachablePhaseGateProbe(WorkflowStage::Arrival)));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "radioReachableGateNoneCandidates",
            scenario.expectations.radioReachableGateNoneCandidates,
            xvatsim::brain::RadioReachableCandidateSummaries(
                BuildRadioReachablePhaseGateProbe(WorkflowStage::None)));
        mismatch.has_value()) {
        return *mismatch;
    }

    const auto radioReachableVerifierEnroute =
        BuildRadioReachableVerifierEnrouteProbe();
    if (const auto mismatch = CheckStringList(
            "radioReachableVerifierEnrouteControllers",
            scenario.expectations.radioReachableVerifierEnrouteControllers,
            xvatsim::brain::RadioReachableVerificationFeedSummaries(
                radioReachableVerifierEnroute));
        mismatch.has_value()) {
        return *mismatch;
    }

    const auto radioReachableVerifierUnchanged =
        BuildRadioReachableVerifierUnchangedProbe();
    if (const auto mismatch = CheckStringList(
            "radioReachableVerifierUnchangedControllers",
            scenario.expectations.radioReachableVerifierUnchangedControllers,
            xvatsim::brain::RadioReachableVerificationFeedSummaries(
                radioReachableVerifierUnchanged));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.radioReachableVerifierUnchangedStatus.has_value() &&
        radioReachableVerifierUnchanged.statusLine !=
            *scenario.expectations.radioReachableVerifierUnchangedStatus) {
        return PrintMismatch(
            "radioReachableVerifierUnchangedStatus",
            *scenario.expectations.radioReachableVerifierUnchangedStatus,
            radioReachableVerifierUnchanged.statusLine);
    }

    if (const auto mismatch = CheckStringList(
            "terminalAuthorityOwners",
            scenario.expectations.terminalAuthorityOwners,
            ExtractTerminalAuthorityOwners(terminalAuthorityOutput));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "terminalAuthorityPolygons",
            scenario.expectations.terminalAuthorityPolygons,
            ExtractTerminalAuthorityPolygons(terminalAuthorityOutput));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "airportFrequencyDepartureRecords",
            scenario.expectations.airportFrequencyDepartureRecords,
            ExtractAirportFrequencyRecords(
                airportFrequencyOutput.departureFrequencies));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "airportFrequencyArrivalRecords",
            scenario.expectations.airportFrequencyArrivalRecords,
            ExtractAirportFrequencyRecords(
                airportFrequencyOutput.arrivalFrequencies));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "brainControllerRelevanceDepartureCallsigns",
            scenario.expectations.brainControllerRelevanceDepartureCallsigns,
            ExtractCallsigns(controllerRelevanceOutput.departureBoard));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "brainControllerRelevanceArrivalCallsigns",
            scenario.expectations.brainControllerRelevanceArrivalCallsigns,
            ExtractCallsigns(controllerRelevanceOutput.arrivalBoard));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "brainControllerRelevanceEnrouteCallsigns",
            scenario.expectations.brainControllerRelevanceEnrouteCallsigns,
            ExtractCallsigns(controllerRelevanceOutput.enrouteBoard));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "brainControllerRelevanceCompletions",
            scenario.expectations.brainControllerRelevanceCompletions,
            ExtractControllerRelevanceCompletions(controllerRelevanceOutput));
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "phasePublisherReuseLifecycle",
            scenario.expectations.phasePublisherReuseLifecycle,
            BuildPhasePublisherReuseProbe());
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "phasePublisherIsolationLifecycle",
            scenario.expectations.phasePublisherIsolationLifecycle,
            BuildPhasePublisherIsolationProbe());
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "phasePublisherWorkflowClearLifecycle",
            scenario.expectations.phasePublisherWorkflowClearLifecycle,
            BuildPhasePublisherWorkflowClearProbe());
        mismatch.has_value()) {
        return *mismatch;
    }

    if (scenario.expectations.phasePublisherReuseLedgerSummary.has_value() ||
        scenario.expectations.phasePublisherPlanContextSummary.has_value() ||
        scenario.expectations.phasePublisherStableKeySummary.has_value() ||
        scenario.expectations.phasePublisherStableKeyConsumerDryRunSummary
            .has_value() ||
        scenario.expectations.phasePublisherStableKeyShadowSummary
            .has_value() ||
        scenario.expectations
            .phasePublisherStableKeyLiveConsumptionReadinessSummary
            .has_value() ||
        scenario.expectations.phasePublisherStableKeyLiveConsumptionSummary
            .has_value() ||
        !scenario.expectations.phasePublisherReuseLedgerDecisionsContains.empty()) {
        const auto phaseLiveConsumptionEnabled =
            scenario
                    .settingsSourceOwnedFallbackStableKeyLiveConsumptionLoaded
                ? scenario
                      .settingsSourceOwnedFallbackStableKeyLiveConsumptionEnabled
                : scenario.sourceOwnedFallbackStableKeyLiveConsumptionEnabled;
        const auto phaseLiveConsumptionGateSource =
            scenario
                    .settingsSourceOwnedFallbackStableKeyLiveConsumptionLoaded
                ? scenario
                      .settingsSourceOwnedFallbackStableKeyLiveConsumptionGateSource
                : scenario.sourceOwnedFallbackStableKeyLiveConsumptionGateSource;
        const auto phaseReuseProbeResult =
            BuildPhasePublisherReuseLedgerProbe(
                scenario.phasePublisherReuseProbe,
                scenario.sourceOwnedFallbackStableKeyShadowEnabled,
                scenario.sourceOwnedFallbackStableKeyShadowGateSource,
                scenario
                    .sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled,
                scenario
                    .sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource,
                phaseLiveConsumptionEnabled,
                phaseLiveConsumptionGateSource);
        if (scenario.expectations.phasePublisherReuseLedgerSummary.has_value()) {
            const auto summary = PhaseReuseSummaryText(phaseReuseProbeResult);
            if (summary !=
                *scenario.expectations.phasePublisherReuseLedgerSummary) {
                return PrintMismatch(
                    "phasePublisherReuseLedgerSummary",
                    *scenario.expectations.phasePublisherReuseLedgerSummary,
                    summary);
            }
        }
        if (scenario.expectations.phasePublisherPlanContextSummary.has_value()) {
            const auto summary =
                PhasePlanContextSummaryText(phaseReuseProbeResult);
            if (summary !=
                *scenario.expectations.phasePublisherPlanContextSummary) {
                return PrintMismatch(
                    "phasePublisherPlanContextSummary",
                    *scenario.expectations.phasePublisherPlanContextSummary,
                    summary);
            }
        }
        if (scenario.expectations.phasePublisherStableKeySummary.has_value()) {
            const auto summary =
                PhaseStableKeySummaryText(phaseReuseProbeResult);
            if (summary !=
                *scenario.expectations.phasePublisherStableKeySummary) {
                return PrintMismatch(
                    "phasePublisherStableKeySummary",
                    *scenario.expectations.phasePublisherStableKeySummary,
                summary);
            }
        }
        if (scenario.expectations
                .phasePublisherStableKeyConsumerDryRunSummary.has_value()) {
            const auto summary =
                PhaseStableKeyConsumerDryRunSummaryText(
                    phaseReuseProbeResult);
            if (summary !=
                *scenario.expectations
                     .phasePublisherStableKeyConsumerDryRunSummary) {
                return PrintMismatch(
                    "phasePublisherStableKeyConsumerDryRunSummary",
                    *scenario.expectations
                         .phasePublisherStableKeyConsumerDryRunSummary,
                    summary);
            }
        }
        if (scenario.expectations.phasePublisherStableKeyShadowSummary
                .has_value()) {
            const auto summary =
                PhaseStableKeyShadowSummaryText(phaseReuseProbeResult);
            if (summary !=
                *scenario.expectations.phasePublisherStableKeyShadowSummary) {
                return PrintMismatch(
                    "phasePublisherStableKeyShadowSummary",
                    *scenario.expectations.phasePublisherStableKeyShadowSummary,
                    summary);
            }
        }
        if (scenario.expectations
                .phasePublisherStableKeyLiveConsumptionReadinessSummary
                .has_value()) {
            const auto summary =
                PhaseStableKeyLiveConsumptionReadinessSummaryText(
                    phaseReuseProbeResult);
            if (summary !=
                *scenario.expectations
                     .phasePublisherStableKeyLiveConsumptionReadinessSummary) {
                return PrintMismatch(
                    "phasePublisherStableKeyLiveConsumptionReadinessSummary",
                    *scenario.expectations
                         .phasePublisherStableKeyLiveConsumptionReadinessSummary,
                    summary);
            }
        }
        if (scenario.expectations
                .phasePublisherStableKeyLiveConsumptionSummary.has_value()) {
            const auto summary =
                PhaseStableKeyLiveConsumptionSummaryText(
                    phaseReuseProbeResult);
            if (summary !=
                *scenario.expectations
                     .phasePublisherStableKeyLiveConsumptionSummary) {
                return PrintMismatch(
                    "phasePublisherStableKeyLiveConsumptionSummary",
                    *scenario.expectations
                         .phasePublisherStableKeyLiveConsumptionSummary,
                    summary);
            }
        }
        if (const auto mismatch = CheckStringListContains(
                "phasePublisherReuseLedgerDecisions",
                scenario.expectations.phasePublisherReuseLedgerDecisionsContains,
                PhaseReuseDecisionRows(phaseReuseProbeResult));
            mismatch.has_value()) {
            return *mismatch;
        }
    }

    if (const auto mismatch = CheckStringList(
            "brainOrdinaryMovementWorkOrder",
            scenario.expectations.brainOrdinaryMovementWorkOrder,
            BuildBrainOrdinaryMovementWorkOrderProbe());
        mismatch.has_value()) {
        return *mismatch;
    }

    if (const auto mismatch = CheckStringList(
            "brainOrdinaryMovementHeavy",
            scenario.expectations.brainOrdinaryMovementHeavyFlags,
            BuildBrainOrdinaryMovementHeavyProbe());
        mismatch.has_value()) {
        return *mismatch;
    }

    return 0;
}
