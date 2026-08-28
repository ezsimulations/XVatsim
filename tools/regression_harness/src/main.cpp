#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "XVatsim/brain/BrainOrchestrator.h"
#include "XVatsim/brain/BrainDisplayIntent.h"
#include "XVatsim/brain/BrainMetarRuntime.h"
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
#include "XVatsim/modules/overlay/OverlayAccessoryCore.h"
#include "XVatsim/modules/metar/VatsimMetarClient.h"
#include "XVatsim/modules/route_sector/RouteSectorResolver.h"
#include "XVatsim/modules/settings_store/SettingsStore.h"
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

struct OperatingModeScenarioInput {
    std::string probe;
    std::string settingsEntry;
    std::string initialMode;
    std::vector<std::string> selectionRequests;
    std::vector<std::string> roundTripModes;
    std::vector<std::string> resetPaths;
    std::string parityStage;
    std::string persistence;
    int processingCycles = 0;
};

struct OperatingModeScenarioExpectations {
    std::optional<std::string> mode;
    std::optional<std::string> loadStatus;
    std::optional<std::string> source;
    std::optional<std::string> reason;
    std::optional<int> generation;
    std::optional<int> changeCount;
    std::optional<int> persistenceRequests;
    std::optional<int> saveAttempts;
    std::optional<int> saveSuccesses;
    std::optional<std::string> requestSource;
    std::optional<std::string> requestReason;
    std::optional<bool> stateUnchanged;
    std::optional<bool> resetPreserved;
    std::optional<bool> noAutomaticVfr;
    std::vector<std::string> roundTripModes;
    std::optional<bool> parity;
    std::optional<std::string> allowedParityDifference;
    std::optional<int> retryCount;
    std::optional<int> processingCycles;
    std::optional<bool> missingPlanProcessed;
    std::vector<std::string> resetTrace;
    std::optional<int> pipelineRuns;
    std::optional<std::string> pipelineStage;
    std::vector<std::string> pipelineControllerCallsigns;
    std::vector<std::string> pipelineDisplayCallsigns;
};

struct Step3ScenarioInput {
    std::vector<std::string> actions;
    std::vector<std::string> expectations;
};

struct Step4ScenarioInput {
    std::string probe;
};

struct ScenarioData {
    std::string name;
    Step3ScenarioInput step3;
    Step4ScenarioInput step4;
    OperatingModeScenarioInput operatingMode;
    OperatingModeScenarioExpectations operatingModeExpectations;
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

std::optional<int> ParseNonnegativeInt(const std::string& value) {
    try {
        std::size_t consumed = 0;
        const auto trimmed = Trim(value);
        const auto parsed = std::stoi(trimmed, &consumed);
        if (consumed != trimmed.size() || parsed < 0) {
            return std::nullopt;
        }
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
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
    if (key == "step3.action") {
        scenario->step3.actions.push_back(value);
        return true;
    }
    if (key == "step4.probe") {
        scenario->step4.probe = value;
        return true;
    }
    if (key == "expect.step3") {
        scenario->step3.expectations.push_back(value);
        return true;
    }
    if (key == "operating_mode.probe") {
        scenario->operatingMode.probe = value;
        return true;
    }
    if (key == "operating_mode.settings_entry") {
        scenario->operatingMode.settingsEntry = value;
        return true;
    }
    if (key == "operating_mode.initial_mode") {
        scenario->operatingMode.initialMode = value;
        return true;
    }
    if (key == "operating_mode.selection_requests") {
        scenario->operatingMode.selectionRequests = Split(value, ',');
        return true;
    }
    if (key == "operating_mode.round_trip_modes") {
        scenario->operatingMode.roundTripModes = Split(value, ',');
        return true;
    }
    if (key == "operating_mode.reset_paths") {
        scenario->operatingMode.resetPaths = Split(value, ',');
        return true;
    }
    if (key == "operating_mode.parity_stage") {
        scenario->operatingMode.parityStage = value;
        return true;
    }
    if (key == "operating_mode.persistence") {
        scenario->operatingMode.persistence = value;
        return true;
    }
    if (key == "operating_mode.processing_cycles") {
        const auto parsed = ParseNonnegativeInt(value);
        if (!parsed.has_value()) {
            return false;
        }
        scenario->operatingMode.processingCycles = *parsed;
        return true;
    }
    if (key == "expect.operating_mode") {
        scenario->operatingModeExpectations.mode = value;
        return true;
    }
    if (key == "expect.operating_mode_load_status") {
        scenario->operatingModeExpectations.loadStatus = value;
        return true;
    }
    if (key == "expect.operating_mode_source") {
        scenario->operatingModeExpectations.source = value;
        return true;
    }
    if (key == "expect.operating_mode_reason") {
        scenario->operatingModeExpectations.reason = value;
        return true;
    }
    if (key == "expect.operating_mode_generation" ||
        key == "expect.operating_mode_change_count" ||
        key == "expect.operating_mode_persistence_requests" ||
        key == "expect.operating_mode_save_attempts" ||
        key == "expect.operating_mode_save_successes" ||
        key == "expect.operating_mode_retry_count" ||
        key == "expect.operating_mode_processing_cycles" ||
        key == "expect.operating_mode_pipeline_runs") {
        const auto parsed = ParseNonnegativeInt(value);
        if (!parsed.has_value()) {
            return false;
        }
        if (key == "expect.operating_mode_generation") {
            scenario->operatingModeExpectations.generation = *parsed;
        } else if (key == "expect.operating_mode_change_count") {
            scenario->operatingModeExpectations.changeCount = *parsed;
        } else if (key == "expect.operating_mode_persistence_requests") {
            scenario->operatingModeExpectations.persistenceRequests = *parsed;
        } else if (key == "expect.operating_mode_save_attempts") {
            scenario->operatingModeExpectations.saveAttempts = *parsed;
        } else if (key == "expect.operating_mode_save_successes") {
            scenario->operatingModeExpectations.saveSuccesses = *parsed;
        } else if (key == "expect.operating_mode_retry_count") {
            scenario->operatingModeExpectations.retryCount = *parsed;
        } else if (key == "expect.operating_mode_processing_cycles") {
            scenario->operatingModeExpectations.processingCycles = *parsed;
        } else {
            scenario->operatingModeExpectations.pipelineRuns = *parsed;
        }
        return true;
    }
    if (key == "expect.operating_mode_request_source") {
        scenario->operatingModeExpectations.requestSource = value;
        return true;
    }
    if (key == "expect.operating_mode_request_reason") {
        scenario->operatingModeExpectations.requestReason = value;
        return true;
    }
    if (key == "expect.operating_mode_state_unchanged" ||
        key == "expect.operating_mode_reset_preserved" ||
        key == "expect.operating_mode_no_automatic_vfr" ||
        key == "expect.operating_mode_missing_plan_processed" ||
        key == "expect.operating_mode_parity") {
        bool parsed = false;
        if (!ParseBool(value, &parsed)) {
            return false;
        }
        if (key == "expect.operating_mode_state_unchanged") {
            scenario->operatingModeExpectations.stateUnchanged = parsed;
        } else if (key == "expect.operating_mode_reset_preserved") {
            scenario->operatingModeExpectations.resetPreserved = parsed;
        } else if (key == "expect.operating_mode_no_automatic_vfr") {
            scenario->operatingModeExpectations.noAutomaticVfr = parsed;
        } else if (key == "expect.operating_mode_missing_plan_processed") {
            scenario->operatingModeExpectations.missingPlanProcessed = parsed;
        } else {
            scenario->operatingModeExpectations.parity = parsed;
        }
        return true;
    }
    if (key == "expect.operating_mode_round_trip") {
        scenario->operatingModeExpectations.roundTripModes = Split(value, ',');
        return true;
    }
    if (key == "expect.operating_mode_allowed_parity_difference") {
        scenario->operatingModeExpectations.allowedParityDifference = value;
        return true;
    }
    if (key == "expect.operating_mode_reset_trace") {
        scenario->operatingModeExpectations.resetTrace = Split(value, ',');
        return true;
    }
    if (key == "expect.operating_mode_pipeline_stage") {
        scenario->operatingModeExpectations.pipelineStage = value;
        return true;
    }
    if (key == "expect.operating_mode_pipeline_controller_callsigns") {
        scenario->operatingModeExpectations.pipelineControllerCallsigns =
            Split(value, ',');
        return true;
    }
    if (key == "expect.operating_mode_pipeline_display_callsigns") {
        scenario->operatingModeExpectations.pipelineDisplayCallsigns =
            Split(value, ',');
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

struct OperatingModeProbeActual {
    std::string mode = "IFR";
    std::string loadStatus = "missing";
    std::string source = "default";
    std::string reason = "missing-setting";
    int generation = 0;
    int changeCount = 0;
    int persistenceRequests = 0;
    int saveAttempts = 0;
    int saveSuccesses = 0;
    std::string requestSource = "none";
    std::string requestReason = "none";
    bool stateUnchanged = false;
    bool resetPreserved = false;
    bool noAutomaticVfr = false;
    std::vector<std::string> roundTripModes;
    bool parity = false;
    std::string allowedParityDifference;
    int retryCount = 0;
    int processingCycles = 0;
    bool missingPlanProcessed = false;
    std::vector<std::string> resetTrace;
    int pipelineRuns = 0;
    std::string pipelineStage;
    std::vector<std::string> pipelineControllerCallsigns;
    std::vector<std::string> pipelineDisplayCallsigns;
};

xvatsim::brain::BrainOwnedOperatingMode ParseHarnessOperatingMode(
    const std::string& value) {
    return ToUpperCopy(value) == "VFR"
               ? xvatsim::brain::BrainOwnedOperatingMode::VFR
               : xvatsim::brain::BrainOwnedOperatingMode::IFR;
}

xvatsim::modules::settings_store::StoredOperatingMode
ToHarnessStoredOperatingMode(xvatsim::brain::BrainOwnedOperatingMode mode) {
    return mode == xvatsim::brain::BrainOwnedOperatingMode::VFR
               ? xvatsim::modules::settings_store::StoredOperatingMode::VFR
               : xvatsim::modules::settings_store::StoredOperatingMode::IFR;
}

xvatsim::brain::BrainOwnedOperatingModeLoadStatus
ToHarnessBrainLoadStatus(
    xvatsim::modules::settings_store::StoredOperatingModeLoadStatus status) {
    using BrainStatus = xvatsim::brain::BrainOwnedOperatingModeLoadStatus;
    using StoredStatus =
        xvatsim::modules::settings_store::StoredOperatingModeLoadStatus;
    switch (status) {
        case StoredStatus::Valid:
            return BrainStatus::Valid;
        case StoredStatus::Invalid:
            return BrainStatus::Invalid;
        case StoredStatus::Unavailable:
            return BrainStatus::Unavailable;
        case StoredStatus::Missing:
        default:
            return BrainStatus::Missing;
    }
}

xvatsim::brain::BrainOwnedOperatingMode ToHarnessBrainOperatingMode(
    xvatsim::modules::settings_store::StoredOperatingMode mode) {
    return mode == xvatsim::modules::settings_store::StoredOperatingMode::VFR
               ? xvatsim::brain::BrainOwnedOperatingMode::VFR
               : xvatsim::brain::BrainOwnedOperatingMode::IFR;
}

void InitializeHarnessOperatingModeFromSettings(
    xvatsim::brain::BrainOwnedRuntimeState* state,
    const xvatsim::modules::settings_store::PluginSettings& settings) {
    xvatsim::brain::BrainOwnedOperatingModeInitializationInput input;
    input.loadStatus =
        ToHarnessBrainLoadStatus(settings.operatingModeLoadStatus);
    input.storedMode = ToHarnessBrainOperatingMode(settings.operatingMode);
    xvatsim::brain::InitializeBrainOwnedOperatingMode(state, input);
}

std::string HarnessOperatingModeName(
    xvatsim::brain::BrainOwnedOperatingMode mode) {
    return mode == xvatsim::brain::BrainOwnedOperatingMode::VFR ? "VFR" : "IFR";
}

std::string HarnessLoadStatusName(
    xvatsim::modules::settings_store::StoredOperatingModeLoadStatus status) {
    using Status =
        xvatsim::modules::settings_store::StoredOperatingModeLoadStatus;
    switch (status) {
        case Status::Valid:
            return "valid";
        case Status::Invalid:
            return "invalid";
        case Status::Unavailable:
            return "unavailable";
        case Status::Missing:
        default:
            return "missing";
    }
}

std::filesystem::path OperatingModeProbePath(const ScenarioData& scenario) {
    std::string token = scenario.name;
    std::transform(
        token.begin(),
        token.end(),
        token.begin(),
        [](unsigned char ch) {
            return std::isalnum(ch) != 0 ? static_cast<char>(std::tolower(ch)) : '_';
        });
    return std::filesystem::temp_directory_path() /
           "xvatsim_v2_step_02_harness" / (token + ".prf");
}

bool WriteOperatingModeProbeSettings(
    const std::filesystem::path& path,
    const std::string& contents) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }
    std::ofstream output(path, std::ios::trunc);
    output << contents;
    return output.good();
}

struct OperatingModePipelinePass {
    WorkflowStage stage = WorkflowStage::None;
    std::vector<std::string> controllerCallsigns;
    std::vector<std::string> displayCallsigns;
    std::string serializedOutput;
};

std::string SerializeOperatingModePipelineOutput(
    const xvatsim::core::workflow::HandoffDecision& workflowDecision,
    const xvatsim::brain::RadioReachableControllerSnapshot& radioSnapshot,
    const xvatsim::brain::BrainControllerRelevanceWorkerOutput& relevance,
    const xvatsim::brain::BrainOwnedPublisherOutput& publisher,
    const xvatsim::brain::OverlayViewModel& view) {
    const auto& display = publisher.finalDisplay;
    std::ostringstream stream;
    stream << static_cast<int>(workflowDecision.stage) << ':'
           << workflowDecision.reason << '|'
           << (radioSnapshot.available ? 1 : 0) << ':'
           << (radioSnapshot.stale ? 1 : 0) << ':'
           << radioSnapshot.stableHash << '|'
           << (relevance.available ? 1 : 0) << ':'
           << (relevance.stale ? 1 : 0) << ':' << relevance.reason << '|'
           << (display.available ? 1 : 0) << '|'
           << static_cast<int>(display.source) << '|'
           << display.airportIcao << '|';
    for (const auto& station : display.stations) {
        stream << static_cast<int>(station.role) << ':'
               << station.callsign << ':' << station.frequency << ':'
               << station.stableCompletionKey << ';';
    }
    stream << "view=" << static_cast<int>(view.mode) << ':'
           << (view.visible ? 1 : 0) << ':' << view.title << ':'
           << view.headerRightText << '|';
    for (const auto& line : view.bodyLines) {
        stream << line.text << ':' << static_cast<int>(line.tone) << ';';
    }
    const auto appendBoard = [&](const xvatsim::brain::ModuleBoardSnapshot& board) {
        stream << "board=" << (board.available ? 1 : 0) << ':'
               << static_cast<int>(board.source) << ':' << board.airportIcao
               << ':';
        for (const auto& station : board.stations) {
            stream << static_cast<int>(station.role) << ':' << station.callsign
                   << ':' << station.frequency << ':'
                   << station.stableCompletionKey << ';';
        }
    };
    appendBoard(publisher.departureBoard);
    appendBoard(publisher.enrouteBoard);
    appendBoard(publisher.arrivalBoard);
    for (const auto& completion : relevance.completions) {
        stream << completion.callsign << ':' << completion.frequency << ':'
               << completion.stableKey << ':'
               << static_cast<int>(completion.decision) << ':'
               << (completion.displayed ? 1 : 0) << ';';
    }
    return stream.str();
}

OperatingModePipelinePass ExecuteOperatingModePipelinePass(
    const ScenarioData& scenario,
    xvatsim::brain::BrainOwnedRuntimeState* state) {
    OperatingModePipelinePass pass;
    auto workflowState = scenario.workflowState;
    const auto workflowDecision =
        xvatsim::core::workflow::ResolveWorkflowStage(
            scenario.aircraftState,
            scenario.radioStateSnapshot,
            scenario.departureTerminalCoverageKnown,
            scenario.insideDepartureTerminalCoverage,
            scenario.departureBoard,
            scenario.enrouteBoard,
            scenario.nowSeconds,
            &workflowState,
            scenario.tuning);
    pass.stage = workflowDecision.stage;

    xvatsim::brain::ControllerFeedSnapshot controllerFeed;
    controllerFeed.generation = scenario.controllerFeedGeneration;
    controllerFeed.stale = scenario.controllerFeedStale;
    controllerFeed.available =
        scenario.controllerFeedAvailable.value_or(!scenario.controllers.empty());
    if (controllerFeed.stale) {
        controllerFeed.available = false;
    }
    if ((controllerFeed.available && !controllerFeed.stale) ||
        scenario.forceControllerFeedEntries) {
        controllerFeed.connectedControllers =
            static_cast<int>(scenario.controllers.size());
        controllerFeed.controllers = &scenario.controllers;
    }

    xvatsim::brain::RadioReachableBuildOptions radioOptions;
    radioOptions.available = scenario.transceiverResolutionSnapshot.available;
    radioOptions.stale = scenario.transceiverResolutionSnapshot.stale;
    radioOptions.generation = controllerFeed.generation;
    radioOptions.source = xvatsim::brain::RadioReachableSource::AFVRadioRange;
    radioOptions.changeReason = "operating-mode-pipeline-proof";
    radioOptions.nowSeconds = scenario.nowSeconds;
    const auto radioSnapshot =
        xvatsim::brain::BuildRadioReachableControllerSnapshotFromTransceivers(
            scenario.transceiverResolutionSnapshot,
            controllerFeed,
            radioOptions);
    const auto gatedRadioSnapshot =
        xvatsim::brain::RunBrainOwnedRadioPhaseGate(
            state,
            radioSnapshot,
            workflowDecision.stage,
            "operating-mode-pipeline-proof");

    xvatsim::brain::BrainControllerRelevanceWorkerInput relevanceInput;
    relevanceInput.workflowStage = workflowDecision.stage;
    relevanceInput.radioBoardHash = gatedRadioSnapshot.stableHash;
    relevanceInput.routePolygonHash = 1;
    relevanceInput.currentPolygonIndex = 1;
    relevanceInput.currentPolygonKey =
        scenario.routeSectorSnapshot.currentSectors.empty()
            ? "CURRENT"
            : scenario.routeSectorSnapshot.currentSectors.front().identifier;
    relevanceInput.nextPolygonKey =
        scenario.routeSectorSnapshot.nextSectors.empty()
            ? ""
            : scenario.routeSectorSnapshot.nextSectors.front().identifier;
    relevanceInput.currentSectors = scenario.routeSectorSnapshot.currentSectors;
    relevanceInput.nextSectors = scenario.routeSectorSnapshot.nextSectors;
    relevanceInput.departureIcao =
        scenario.workflowState.flightContext.departureIcao;
    relevanceInput.arrivalIcao =
        scenario.workflowState.flightContext.destinationIcao;
    relevanceInput.radios = scenario.radioStateSnapshot;
    relevanceInput.candidates = gatedRadioSnapshot.candidates;
    const auto relevance =
        xvatsim::brain::RunBrainControllerRelevanceWorker(relevanceInput);

    state->routePolygonHash = relevanceInput.routePolygonHash;
    state->currentPolygonIndex = relevanceInput.currentPolygonIndex;
    state->currentPolygonKey = relevanceInput.currentPolygonKey;
    state->nextPolygonKey = relevanceInput.nextPolygonKey;
    state->candidateCompletions = relevance.completions;
    xvatsim::brain::BrainOwnedPublisherFactInput publisherFacts;
    publisherFacts.workflowStage = workflowDecision.stage;
    publisherFacts.radios = scenario.radioStateSnapshot;
    publisherFacts.departureBoard = relevance.departureBoard;
    publisherFacts.arrivalBoard = relevance.arrivalBoard;
    publisherFacts.enrouteBoard = relevance.enrouteBoard;
    publisherFacts.completions = relevance.completions;
    publisherFacts.publishReason = "operating-mode-pipeline-proof";
    publisherFacts.productPlanKey = "OPERATING-MODE-PIPELINE-PROOF";
    publisherFacts.productPlanKeySource = "harness-scenario";
    const auto publisherInput =
        xvatsim::brain::BuildBrainOwnedPublisherInputFromFacts(
            *state,
            publisherFacts);
    const auto publisher =
        xvatsim::brain::RunBrainOwnedPublisher(state, publisherInput);
    xvatsim::brain::CommitBrainOwnedPublishedRuntimeFromPublisherOutput(
        state,
        workflowDecision.stage,
        publisherFacts.productPlanKey,
        gatedRadioSnapshot,
        publisher,
        publisher.finalDisplay);
    xvatsim::brain::CommitBrainOwnedWorkflowState(state, workflowState);

    const auto view = xvatsim::brain::BrainOrchestrator::BuildOverlayViewModel(
        workflowDecision.stage,
        scenario.aircraftState,
        scenario.xPilotSessionSnapshot,
        scenario.radioStateSnapshot,
        scenario.networkPlanSnapshot,
        controllerFeed,
        scenario.transceiverResolutionSnapshot,
        publisher.finalDisplay,
        xvatsim::brain::ManualQuerySnapshot{});

    const auto* stageBoard = &publisher.departureBoard;
    if (workflowDecision.stage == WorkflowStage::Enroute) {
        stageBoard = &publisher.enrouteBoard;
    } else if (workflowDecision.stage == WorkflowStage::Arrival) {
        stageBoard = &publisher.arrivalBoard;
    }
    pass.controllerCallsigns = ExtractCallsigns(*stageBoard);
    pass.displayCallsigns = ExtractCallsigns(publisher.finalDisplay);
    pass.serializedOutput = SerializeOperatingModePipelineOutput(
        workflowDecision,
        gatedRadioSnapshot,
        relevance,
        publisher,
        view);
    return pass;
}

void ExecuteHarnessSessionRuntimeCacheReset(
    xvatsim::brain::BrainOwnedRuntimeState* state) {
    xvatsim::brain::ResetBrainOwnedRuntimeState(state);
    xvatsim::brain::ResetBrainOwnedDisplayPublisherState(state);
}

void ExecuteHarnessColdDarkPresentationReset(
    xvatsim::brain::BrainOwnedRuntimeState* state) {
    xvatsim::brain::ClearBrainOwnedPendingTextEntryMode(state);
    xvatsim::brain::ClearBrainOwnedManualQuery(state);
    xvatsim::brain::ResetBrainOwnedCruiseTarget(state);
    xvatsim::brain::ClearBrainOwnedDiversionOverrideSource(state);
    xvatsim::brain::ResetBrainOwnedRuntimeCachePreservingFlightContext(state);
    xvatsim::brain::ResetBrainOwnedWorkflowProgress(state);
    xvatsim::brain::ResetBrainOwnedEnrouteInitialHold(state);
    xvatsim::brain::ClearBrainOwnedFlightContext(state);
    xvatsim::brain::ClearBrainOwnedLastSampledFacts(state);
    xvatsim::brain::ClearBrainOwnedXPilotConnectionTracking(state);
    xvatsim::brain::ClearBrainOwnedFlightRecoveryRequests(state);
    xvatsim::brain::ClearBrainOwnedAircraftStateInvalidBoundary(state);
    xvatsim::brain::ResetBrainOwnedControllerMessageState(state);
    xvatsim::brain::ClearBrainOwnedManualQuery(state);
    xvatsim::brain::ResetBrainOwnedDisplayPublisherState(state);
    xvatsim::brain::ResetBrainOwnedStandbyAssistLatch(state);
}

void ExecuteHarnessPluginRuntimeReset(
    xvatsim::brain::BrainOwnedRuntimeState* state) {
    xvatsim::brain::ClearBrainOwnedPendingTextEntryMode(state);
    xvatsim::brain::ClearBrainOwnedManualQuery(state);
    xvatsim::brain::ClearBrainOwnedFlightRecoveryRequests(state);
    ExecuteHarnessSessionRuntimeCacheReset(state);
    ExecuteHarnessColdDarkPresentationReset(state);
    xvatsim::brain::SetBrainOwnedColdDarkResetApplied(state, false);
}

bool HarnessOperatingModeStateEquals(
    const xvatsim::brain::BrainOwnedOperatingModeState& left,
    const xvatsim::brain::BrainOwnedOperatingModeState& right) {
    return left.mode == right.mode && left.source == right.source &&
           left.reason == right.reason && left.generation == right.generation;
}

OperatingModePipelinePass ExecuteHarnessOrdinaryProcessingCycle(
    const ScenarioData& scenario,
    xvatsim::brain::BrainOwnedRuntimeState* state) {
    xvatsim::brain::BrainOwnedFlightPlanSampleInput sampleInput;
    sampleInput.flightContextActive = state->flightContext.active;
    sampleInput.nowSeconds = static_cast<long long>(scenario.nowSeconds);
    sampleInput.sampleCadenceSeconds = 5;
    const auto sampleDecision =
        xvatsim::brain::DecideBrainOwnedFlightPlanSample(*state, sampleInput);
    if (sampleDecision.shouldSample) {
        xvatsim::brain::BrainOwnedFlightPlanSampleCommitInput commitInput;
        commitInput.nowSeconds = sampleInput.nowSeconds;
        commitInput.snapshot = scenario.flightPlanSnapshot;
        xvatsim::brain::CommitBrainOwnedFlightPlanSample(state, commitInput);
    }

    xvatsim::brain::PilotIdentitySnapshot pilotIdentity;
    pilotIdentity.connected = scenario.xPilotSessionSnapshot.connected;
    pilotIdentity.ready = scenario.xPilotSessionSnapshot.connected;
    pilotIdentity.callsign = scenario.xPilotSessionSnapshot.callsign;
    pilotIdentity.normalizedCallsign = scenario.xPilotSessionSnapshot.callsign;
    xvatsim::brain::CommitBrainOwnedLastSampledFacts(
        state,
        scenario.aircraftState,
        pilotIdentity,
        state->hasFlightPlanSnapshot ? state->flightPlanSnapshot
                                     : scenario.flightPlanSnapshot,
        scenario.networkPlanSnapshot);
    return ExecuteOperatingModePipelinePass(scenario, state);
}

OperatingModeProbeActual ExecuteOperatingModeProbe(
    const ScenarioData& scenario) {
    OperatingModeProbeActual actual;
    const auto path = OperatingModeProbePath(scenario);
    std::error_code cleanupError;
    std::filesystem::remove(path, cleanupError);

    auto loadSettingsEntry = [&](const std::string& entry) {
        xvatsim::modules::settings_store::SettingsStore store;
        store.SetPath(path.string());
        std::filesystem::remove(path, cleanupError);
        if (entry == "<empty>") {
            WriteOperatingModeProbeSettings(path, "operating_mode=\n");
        } else if (entry == "<malformed>") {
            WriteOperatingModeProbeSettings(path, "operating_mode vfr\n");
        } else if (entry == "<unknown>") {
            WriteOperatingModeProbeSettings(path, "operating_mode=visual\n");
        } else if (entry != "<missing>" && !entry.empty()) {
            WriteOperatingModeProbeSettings(
                path,
                "operating_mode=" + entry + "\n");
        }
        return store.Load();
    };

    xvatsim::brain::BrainOwnedRuntimeState state;
    xvatsim::brain::BrainOwnedOperatingModeSelectionResult lastSelection;

    if (scenario.operatingMode.probe == "settings-load") {
        const auto settings = loadSettingsEntry(scenario.operatingMode.settingsEntry);
        InitializeHarnessOperatingModeFromSettings(&state, settings);
        actual.loadStatus = HarnessLoadStatusName(settings.operatingModeLoadStatus);

        if (scenario.operatingMode.settingsEntry == "<unknown>") {
            for (const auto& invalid : {"<empty>", "<malformed>", "<unknown>"}) {
                const auto invalidSettings = loadSettingsEntry(invalid);
                if (invalidSettings.operatingModeLoadStatus !=
                        xvatsim::modules::settings_store::
                            StoredOperatingModeLoadStatus::Invalid ||
                    invalidSettings.operatingMode !=
                        xvatsim::modules::settings_store::StoredOperatingMode::IFR) {
                    actual.loadStatus = "invalid-set-failed";
                }
            }
        }

        for (const auto& mode : scenario.operatingMode.roundTripModes) {
            const auto loaded = loadSettingsEntry(ToUpperCopy(mode) == "VFR" ? "VFR" : " IFR ");
            if (loaded.operatingModeLoadStatus ==
                xvatsim::modules::settings_store::StoredOperatingModeLoadStatus::Valid) {
                actual.roundTripModes.push_back(
                    loaded.operatingMode ==
                            xvatsim::modules::settings_store::StoredOperatingMode::VFR
                        ? "VFR"
                        : "IFR");
            }
        }
    } else if (scenario.operatingMode.probe == "round-trip") {
        xvatsim::modules::settings_store::SettingsStore store;
        store.SetPath(path.string());
        for (const auto& mode : scenario.operatingMode.roundTripModes) {
            xvatsim::modules::settings_store::PluginSettings settings;
            settings.operatingMode = ToHarnessStoredOperatingMode(
                ParseHarnessOperatingMode(mode));
            settings.operatingModeLoadStatus =
                xvatsim::modules::settings_store::
                    StoredOperatingModeLoadStatus::Valid;
            ++actual.saveAttempts;
            if (store.Save(settings)) {
                ++actual.saveSuccesses;
                const auto loaded = store.Load();
                if (loaded.operatingModeLoadStatus ==
                    xvatsim::modules::settings_store::
                        StoredOperatingModeLoadStatus::Valid) {
                    actual.roundTripModes.push_back(
                        loaded.operatingMode ==
                                xvatsim::modules::settings_store::
                                    StoredOperatingMode::VFR
                            ? "VFR"
                            : "IFR");
                }
            }
        }
    } else {
        if (!scenario.operatingMode.settingsEntry.empty()) {
            const auto settings =
                loadSettingsEntry(scenario.operatingMode.settingsEntry);
            InitializeHarnessOperatingModeFromSettings(&state, settings);
            actual.loadStatus =
                HarnessLoadStatusName(settings.operatingModeLoadStatus);
        } else {
            xvatsim::brain::InitializeBrainOwnedOperatingMode(
                &state,
                xvatsim::brain::BrainOwnedOperatingModeInitializationInput{});
            if (ToUpperCopy(scenario.operatingMode.initialMode) == "VFR") {
                lastSelection =
                    xvatsim::brain::RequestBrainOwnedOperatingModeSelection(
                        &state,
                        xvatsim::brain::BrainOwnedOperatingMode::VFR);
            }
        }

        if (scenario.operatingMode.probe == "selection" ||
            scenario.operatingMode.probe == "persistence-failure") {
            const auto beforeSelectionState = state.operatingMode;
            for (const auto& request : scenario.operatingMode.selectionRequests) {
                lastSelection =
                    xvatsim::brain::RequestBrainOwnedOperatingModeSelection(
                        &state,
                        ParseHarnessOperatingMode(request));
                if (lastSelection.changed) {
                    ++actual.changeCount;
                }
                if (lastSelection.persistenceRequested) {
                    ++actual.persistenceRequests;
                }
                if (scenario.operatingMode.probe == "persistence-failure" &&
                    lastSelection.persistenceRequested) {
                    xvatsim::modules::settings_store::SettingsStore unavailableStore;
                    xvatsim::modules::settings_store::PluginSettings settings;
                    settings.operatingMode =
                        ToHarnessStoredOperatingMode(lastSelection.effectiveMode);
                    ++actual.saveAttempts;
                    if (unavailableStore.Save(settings)) {
                        ++actual.saveSuccesses;
                    }
                }
            }
            actual.stateUnchanged =
                beforeSelectionState.mode == state.operatingMode.mode &&
                beforeSelectionState.source == state.operatingMode.source &&
                beforeSelectionState.reason == state.operatingMode.reason &&
                beforeSelectionState.generation ==
                    state.operatingMode.generation;

            if (scenario.operatingMode.probe == "persistence-failure") {
                const auto attemptsBeforeProcessing = actual.saveAttempts;
                for (int cycle = 0;
                     cycle < scenario.operatingMode.processingCycles;
                     ++cycle) {
                    (void)ExecuteHarnessOrdinaryProcessingCycle(
                        scenario,
                        &state);
                    ++actual.processingCycles;
                }
                actual.retryCount = actual.saveAttempts - attemptsBeforeProcessing;
            }
        } else if (scenario.operatingMode.probe == "reset-preservation") {
            actual.resetPreserved = true;
            for (const auto& resetPath : scenario.operatingMode.resetPaths) {
                xvatsim::brain::BrainOwnedRuntimeState resetState;
                xvatsim::brain::InitializeBrainOwnedOperatingMode(
                    &resetState,
                    xvatsim::brain::BrainOwnedOperatingModeInitializationInput{});
                (void)xvatsim::brain::RequestBrainOwnedOperatingModeSelection(
                    &resetState,
                    xvatsim::brain::BrainOwnedOperatingMode::VFR);
                const auto expected = resetState.operatingMode;
                if (resetPath == "runtime") {
                    xvatsim::brain::ResetBrainOwnedRuntimeState(&resetState);
                    actual.resetTrace.push_back("runtime:reset-runtime");
                } else if (resetPath == "cache-preserving") {
                    xvatsim::brain::ResetBrainOwnedRuntimeCachePreservingFlightContext(
                        &resetState);
                    actual.resetTrace.push_back(
                        "cache-preserving:reset-cache-preserving");
                } else if (resetPath == "session") {
                    ExecuteHarnessPluginRuntimeReset(&resetState);
                    actual.resetTrace.push_back("session:plugin-runtime-reset");
                } else if (resetPath == "cold-dark") {
                    xvatsim::brain::workflow::AircraftRuntimeBoundaryInput input;
                    input.aircraftState.valid = true;
                    input.aircraftState.batteryOn = false;
                    input.coldDarkResetApplied = resetState.coldDarkResetApplied;
                    input.aircraftStateInvalidBoundaryActive =
                        resetState.aircraftStateInvalidBoundaryActive;
                    const auto decision =
                        xvatsim::brain::workflow::ResolveAircraftRuntimeBoundary(
                            input);
                    if (decision.shouldResetSessionRuntimeCaches) {
                        ExecuteHarnessSessionRuntimeCacheReset(&resetState);
                    }
                    if (decision.shouldResetPresentationState) {
                        ExecuteHarnessColdDarkPresentationReset(&resetState);
                    }
                    xvatsim::brain::ApplyBrainOwnedAircraftRuntimeBoundaryDecision(
                        &resetState,
                        decision);
                    actual.resetTrace.push_back(
                        decision.shouldResetSessionRuntimeCaches &&
                                decision.shouldResetPresentationState
                            ? "cold-dark:session-caches+presentation"
                            : "cold-dark:boundary-decision-failed");
                } else if (resetPath == "invalid-aircraft") {
                    xvatsim::brain::workflow::AircraftRuntimeBoundaryInput input;
                    input.aircraftState.valid = false;
                    input.coldDarkResetApplied = resetState.coldDarkResetApplied;
                    input.aircraftStateInvalidBoundaryActive =
                        resetState.aircraftStateInvalidBoundaryActive;
                    const auto decision =
                        xvatsim::brain::workflow::ResolveAircraftRuntimeBoundary(
                            input);
                    if (decision.shouldResetForInvalidAircraftState) {
                        ExecuteHarnessPluginRuntimeReset(&resetState);
                    }
                    xvatsim::brain::ApplyBrainOwnedAircraftRuntimeBoundaryDecision(
                        &resetState,
                        decision);
                    actual.resetTrace.push_back(
                        decision.shouldResetForInvalidAircraftState
                            ? "invalid-aircraft:invalid-reset"
                            : "invalid-aircraft:boundary-decision-failed");
                } else if (resetPath == "xpilot-disconnect") {
                    resetState.xPilotSessionBoundaryState.lastXPilotConnected = true;
                    resetState.xPilotSessionBoundaryState.lastConnectedPilotCallsign =
                        "N100PC";
                    xvatsim::brain::workflow::XPilotSessionBoundaryInput input;
                    input.state = resetState.xPilotSessionBoundaryState;
                    input.xPilotSession.connected = false;
                    const auto decision =
                        xvatsim::brain::workflow::ResolveXPilotSessionBoundary(
                            input);
                    if (decision.shouldPreserveFlightStateForDisconnect) {
                        xvatsim::brain::ResetBrainOwnedDisplayPublisherState(
                            &resetState);
                        xvatsim::brain::ResetBrainOwnedStandbyAssistLatch(
                            &resetState);
                    }
                    xvatsim::brain::ApplyBrainOwnedXPilotSessionBoundaryDecision(
                        &resetState,
                        decision);
                    actual.resetTrace.push_back(
                        decision.shouldPreserveFlightStateForDisconnect
                            ? "xpilot-disconnect:preserve-flight-state"
                            : "xpilot-disconnect:boundary-decision-failed");
                } else if (resetPath == "xpilot-reconnect") {
                    resetState.xPilotSessionBoundaryState.lastXPilotConnected = false;
                    resetState.xPilotSessionBoundaryState.disconnectedPilotCallsign =
                        "N100PC";
                    xvatsim::brain::workflow::XPilotSessionBoundaryInput input;
                    input.state = resetState.xPilotSessionBoundaryState;
                    input.xPilotSession.connected = true;
                    input.xPilotSession.callsign = "N100PC";
                    input.pilotIdentity.connected = true;
                    input.pilotIdentity.ready = true;
                    input.pilotIdentity.callsign = "N100PC";
                    input.pilotIdentity.normalizedCallsign = "N100PC";
                    const auto decision =
                        xvatsim::brain::workflow::ResolveXPilotSessionBoundary(
                            input);
                    xvatsim::brain::ApplyBrainOwnedXPilotSessionBoundaryDecision(
                        &resetState,
                        decision);
                    actual.resetTrace.push_back(
                        decision.shouldQueueAutomaticRecovery
                            ? "xpilot-reconnect:queue-recovery"
                            : "xpilot-reconnect:boundary-decision-failed");
                } else if (resetPath == "callsign-change") {
                    resetState.xPilotSessionBoundaryState.lastXPilotConnected = true;
                    resetState.xPilotSessionBoundaryState.lastConnectedPilotCallsign =
                        "N100PC";
                    xvatsim::brain::workflow::XPilotSessionBoundaryInput input;
                    input.state = resetState.xPilotSessionBoundaryState;
                    input.xPilotSession.connected = true;
                    input.xPilotSession.callsign = "N200PC";
                    input.pilotIdentity.connected = true;
                    input.pilotIdentity.ready = true;
                    input.pilotIdentity.callsign = "N200PC";
                    input.pilotIdentity.normalizedCallsign = "N200PC";
                    const auto decision =
                        xvatsim::brain::workflow::ResolveXPilotSessionBoundary(
                            input);
                    if (decision.shouldResetFlightScopedState) {
                        ExecuteHarnessPluginRuntimeReset(&resetState);
                    }
                    xvatsim::brain::ApplyBrainOwnedXPilotSessionBoundaryDecision(
                        &resetState,
                        decision);
                    actual.resetTrace.push_back(
                        decision.shouldResetFlightScopedState
                            ? "callsign-change:flight-scoped-reset"
                            : "callsign-change:boundary-decision-failed");
                } else if (resetPath == "plugin-disable-enable") {
                    ExecuteHarnessPluginRuntimeReset(&resetState);
                    ExecuteHarnessPluginRuntimeReset(&resetState);
                    actual.resetTrace.push_back(
                        "plugin-disable-enable:disable-reset+enable-reset");
                } else {
                    actual.resetTrace.push_back(resetPath + ":unknown");
                }
                actual.resetPreserved =
                    actual.resetPreserved &&
                    HarnessOperatingModeStateEquals(
                        resetState.operatingMode,
                        expected);
                state = resetState;
            }
        } else if (scenario.operatingMode.probe == "missing-flight-plan") {
            const auto before = state.operatingMode;
            (void)ExecuteHarnessOrdinaryProcessingCycle(scenario, &state);
            ++actual.processingCycles;
            actual.missingPlanProcessed =
                state.hasFlightPlanSnapshot &&
                !state.flightPlanSnapshot.available &&
                !state.lastFlightPlanSnapshot.available;
            actual.noAutomaticVfr =
                HarnessOperatingModeStateEquals(state.operatingMode, before) &&
                state.operatingMode.mode ==
                    xvatsim::brain::BrainOwnedOperatingMode::IFR &&
                actual.changeCount == 0 && actual.persistenceRequests == 0;
        } else if (scenario.operatingMode.probe == "parity") {
            xvatsim::brain::BrainOwnedRuntimeState ifrState;
            xvatsim::brain::BrainOwnedRuntimeState vfrState;
            xvatsim::brain::BrainOwnedOperatingModeInitializationInput ifrInput;
            ifrInput.loadStatus =
                xvatsim::brain::BrainOwnedOperatingModeLoadStatus::Valid;
            ifrInput.storedMode =
                xvatsim::brain::BrainOwnedOperatingMode::IFR;
            xvatsim::brain::InitializeBrainOwnedOperatingMode(&ifrState, ifrInput);
            xvatsim::brain::BrainOwnedOperatingModeInitializationInput vfrInput;
            vfrInput.loadStatus =
                xvatsim::brain::BrainOwnedOperatingModeLoadStatus::Valid;
            vfrInput.storedMode =
                xvatsim::brain::BrainOwnedOperatingMode::VFR;
            xvatsim::brain::InitializeBrainOwnedOperatingMode(
                &vfrState,
                vfrInput);
            const auto ifrPass =
                ExecuteOperatingModePipelinePass(scenario, &ifrState);
            const auto vfrPass =
                ExecuteOperatingModePipelinePass(scenario, &vfrState);
            actual.pipelineRuns = 2;
            actual.pipelineStage = WorkflowStageToString(ifrPass.stage);
            actual.pipelineControllerCallsigns = ifrPass.controllerCallsigns;
            actual.pipelineDisplayCallsigns = ifrPass.displayCallsigns;
            actual.parity =
                ifrPass.stage == vfrPass.stage &&
                ifrPass.controllerCallsigns == vfrPass.controllerCallsigns &&
                ifrPass.displayCallsigns == vfrPass.displayCallsigns &&
                ifrPass.serializedOutput == vfrPass.serializedOutput;
            actual.allowedParityDifference = "state-diagnostic-only";
        }
    }

    actual.mode = HarnessOperatingModeName(state.operatingMode.mode);
    actual.source = xvatsim::brain::ToString(state.operatingMode.source);
    actual.reason = state.operatingMode.reason;
    actual.generation = static_cast<int>(state.operatingMode.generation);
    if (!lastSelection.requestReason.empty()) {
        actual.requestSource =
            xvatsim::brain::ToString(lastSelection.requestSource);
        actual.requestReason = lastSelection.requestReason;
    }

    std::filesystem::remove(path, cleanupError);
    return actual;
}

int RunOperatingModeProbe(const ScenarioData& scenario) {
    const auto actual = ExecuteOperatingModeProbe(scenario);
    const auto& expected = scenario.operatingModeExpectations;
    int failures = 0;
    const auto checkString = [&](const char* label,
                                 const std::optional<std::string>& wanted,
                                 const std::string& observed) {
        if (wanted.has_value() && *wanted != observed) {
            failures += PrintMismatch(label, *wanted, observed);
        }
    };
    const auto checkInt = [&](const char* label,
                              const std::optional<int>& wanted,
                              int observed) {
        if (wanted.has_value() && *wanted != observed) {
            failures += PrintMismatch(
                label,
                std::to_string(*wanted),
                std::to_string(observed));
        }
    };
    const auto checkBool = [&](const char* label,
                               const std::optional<bool>& wanted,
                               bool observed) {
        if (wanted.has_value() && *wanted != observed) {
            failures += PrintMismatch(
                label,
                *wanted ? "true" : "false",
                observed ? "true" : "false");
        }
    };

    checkString("operating mode", expected.mode, actual.mode);
    checkString("operating mode load status", expected.loadStatus, actual.loadStatus);
    checkString("operating mode source", expected.source, actual.source);
    checkString("operating mode reason", expected.reason, actual.reason);
    checkInt("operating mode generation", expected.generation, actual.generation);
    checkInt("operating mode change count", expected.changeCount, actual.changeCount);
    checkInt(
        "operating mode persistence requests",
        expected.persistenceRequests,
        actual.persistenceRequests);
    checkInt("operating mode save attempts", expected.saveAttempts, actual.saveAttempts);
    checkInt("operating mode save successes", expected.saveSuccesses, actual.saveSuccesses);
    checkString(
        "operating mode request source",
        expected.requestSource,
        actual.requestSource);
    checkString(
        "operating mode request reason",
        expected.requestReason,
        actual.requestReason);
    checkBool(
        "operating mode state unchanged",
        expected.stateUnchanged,
        actual.stateUnchanged);
    checkBool(
        "operating mode reset preserved",
        expected.resetPreserved,
        actual.resetPreserved);
    checkBool(
        "operating mode no automatic VFR",
        expected.noAutomaticVfr,
        actual.noAutomaticVfr);
    checkBool("operating mode parity", expected.parity, actual.parity);
    checkString(
        "operating mode allowed parity difference",
        expected.allowedParityDifference,
        actual.allowedParityDifference);
    checkInt("operating mode retry count", expected.retryCount, actual.retryCount);
    checkInt(
        "operating mode processing cycles",
        expected.processingCycles,
        actual.processingCycles);
    checkBool(
        "operating mode missing plan processed",
        expected.missingPlanProcessed,
        actual.missingPlanProcessed);
    checkInt(
        "operating mode pipeline runs",
        expected.pipelineRuns,
        actual.pipelineRuns);
    checkString(
        "operating mode pipeline stage",
        expected.pipelineStage,
        actual.pipelineStage);
    if (const auto mismatch = CheckStringList(
            "operating mode reset trace",
            expected.resetTrace,
            actual.resetTrace)) {
        failures += *mismatch;
    }
    if (const auto mismatch = CheckStringList(
            "operating mode pipeline controller callsigns",
            expected.pipelineControllerCallsigns,
            actual.pipelineControllerCallsigns)) {
        failures += *mismatch;
    }
    if (const auto mismatch = CheckStringList(
            "operating mode pipeline display callsigns",
            expected.pipelineDisplayCallsigns,
            actual.pipelineDisplayCallsigns)) {
        failures += *mismatch;
    }
    if (!expected.roundTripModes.empty() &&
        expected.roundTripModes != actual.roundTripModes) {
        failures += PrintMismatch(
            "operating mode round trip",
            JoinCsv(expected.roundTripModes),
            JoinCsv(actual.roundTripModes));
    }

    if (failures != 0) {
        return 1;
    }
    std::cout << "Scenario passed: " << scenario.name << "\n";
    return 0;
}

std::string Step3Bool(bool value) { return value ? "true" : "false"; }

std::string Step3DigestHex(const std::array<std::uint8_t, 32>& digest) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(digest.size() * 2);
    for (const auto value : digest) {
        result.push_back(digits[(value >> 4U) & 0x0fU]);
        result.push_back(digits[value & 0x0fU]);
    }
    return result;
}

std::string Step3Status(xvatsim::brain::BrainOwnedAccessoryOperationStatus status) {
    return status == xvatsim::brain::BrainOwnedAccessoryOperationStatus::Available
        ? "available"
        : "unavailable";
}

std::string Step3Drawer(xvatsim::brain::BrainOwnedAccessoryDrawerId drawer) {
    using Drawer = xvatsim::brain::BrainOwnedAccessoryDrawerId;
    switch (drawer) {
        case Drawer::Metar: return "METAR";
        case Drawer::Atis: return "ATIS";
        case Drawer::Pdc: return "PDC";
        case Drawer::None: default: return "NONE";
    }
}

xvatsim::brain::BrainOwnedAccessoryDrawerId Step3DrawerFromToken(
    const std::string& token) {
    using Drawer = xvatsim::brain::BrainOwnedAccessoryDrawerId;
    if (token == "METAR") return Drawer::Metar;
    if (token == "ATIS") return Drawer::Atis;
    if (token == "PDC") return Drawer::Pdc;
    return Drawer::None;
}

std::string Step3Action(xvatsim::brain::BrainOwnedAccessoryDrawerAction action) {
    using Action = xvatsim::brain::BrainOwnedAccessoryDrawerAction;
    switch (action) {
        case Action::Opened: return "Opened";
        case Action::Closed: return "Closed";
        case Action::Switched: return "Switched";
        case Action::DuplicateRequestIgnored: return "DuplicateRequestIgnored";
        case Action::None: default: return "None";
    }
}

std::string Step3WheelScope(xvatsim::modules::overlay::AccessoryWheelScope scope) {
    using Scope=xvatsim::modules::overlay::AccessoryWheelScope;
    if(scope==Scope::Drawer) return "drawer";
    if(scope==Scope::MainCard) return "main";
    return "unhandled";
}

xvatsim::modules::overlay::AccessoryPerformanceCategory
Step3PerformanceCategoryFromToken(const std::string& token) {
    using Category =
        xvatsim::modules::overlay::AccessoryPerformanceCategory;
    if (token == "rail-raster") return Category::RailRasterization;
    if (token == "drawer-raster") return Category::DrawerRasterization;
    if (token == "rail-upload") return Category::RailTextureUpload;
    if (token == "drawer-upload") return Category::DrawerTextureUpload;
    if (token == "completed-draw") return Category::CompletedAccessoryDraw;
    if (token == "open-action") return Category::OpenAction;
    if (token == "switch-action") return Category::AtomicSwitchAction;
    if (token == "close-action") return Category::CloseAction;
    if (token == "effective-scroll-action") {
        return Category::EffectiveScrollAction;
    }
    if (token == "dispatch-wall") {
        return Category::BrainPresentationDispatchWall;
    }
    if (token == "action-draw-wall") {
        return Category::MatchingActionDrawWall;
    }
    if (token == "combined-action-wall") {
        return Category::CombinedActionWall;
    }
    if (token == "callback-wait") return Category::CallbackWait;
    if (token == "preparation-wait") return Category::PreparationWait;
    if (token == "frame-interval") return Category::ContainingFrameInterval;
    return Category::Count;
}

xvatsim::modules::overlay::AccessoryRasterReason
Step3RasterReasonFromToken(const std::string& token) {
    using Reason = xvatsim::modules::overlay::AccessoryRasterReason;
    if (token == "initial") return Reason::Initial;
    if (token == "selection-open-close-switch" || token == "selection") {
        return Reason::Selection;
    }
    if (token == "scale-or-resize") return Reason::EffectiveScaleOrResize;
    if (token == "typography-or-layout") return Reason::TypographyOrLayout;
    if (token == "content-generation") return Reason::ContentGeneration;
    if (token == "presentation-scroll") return Reason::PresentationScroll;
    return Reason::Count;
}

std::size_t Step3DrawerIndex(xvatsim::brain::BrainOwnedAccessoryDrawerId drawer) {
    using Drawer = xvatsim::brain::BrainOwnedAccessoryDrawerId;
    if (drawer == Drawer::Atis) return 1;
    if (drawer == Drawer::Pdc) return 2;
    return 0;
}

bool Step3IsValidUtf8(const std::string& text) {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index]);
        std::size_t continuationCount = 0;
        if (first <= 0x7F) continuationCount = 0;
        else if (first >= 0xC2 && first <= 0xDF) continuationCount = 1;
        else if (first >= 0xE0 && first <= 0xEF) continuationCount = 2;
        else if (first >= 0xF0 && first <= 0xF4) continuationCount = 3;
        else return false;
        if (index + continuationCount >= text.size()) return false;
        for (std::size_t offset = 1; offset <= continuationCount; ++offset) {
            const auto value = static_cast<unsigned char>(text[index + offset]);
            if ((value & 0xC0) != 0x80) return false;
        }
        index += continuationCount + 1;
    }
    return true;
}

std::string Step3JoinLines(const std::vector<std::string>& lines) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index != 0) stream << '\n';
        stream << lines[index];
    }
    return stream.str();
}

std::string Step3ConcatenateLines(const std::vector<std::string>& lines) {
    std::ostringstream stream;
    for (const auto& line : lines) stream << line;
    return stream.str();
}

std::string Step3NormalizeCrlf(const std::string& text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\r' && index + 1 < text.size() &&
            text[index + 1] == '\n') {
            normalized.push_back('\n');
            ++index;
        } else {
            normalized.push_back(text[index]);
        }
    }
    return normalized;
}

std::string Step3RemoveLineFeeds(std::string text) {
    text.erase(std::remove(text.begin(), text.end(), '\n'), text.end());
    return text;
}

struct Step3ExecutionContext {
    xvatsim::modules::overlay::AccessoryTextMeasurementContext*
        measurementContext = nullptr;
    std::map<int, xvatsim::modules::overlay::AccessoryTypographyMetrics>
        typography;
    std::unordered_map<std::string, std::unique_ptr<xvatsim::brain::BrainOwnedRuntimeState>> states;
    std::unordered_map<std::string, xvatsim::brain::BrainOwnedAccessoryPresentationHandle> snapshots;
    std::unordered_map<std::string, xvatsim::modules::overlay::AccessoryLayoutResult> layouts;
    std::unordered_map<std::string, xvatsim::modules::overlay::AccessoryHitTestResult> hits;
    std::unordered_map<std::string, xvatsim::modules::overlay::AccessoryPresentationState> presenters;
    std::unordered_map<std::string,
        xvatsim::modules::overlay::AccessoryClickFactQueue> clickQueues;
    std::unordered_map<std::string,
        xvatsim::modules::overlay::AccessoryInputDispatchCoordinator>
        inputDispatchers;
    std::unordered_map<std::string,
        std::unique_ptr<xvatsim::modules::overlay::AccessoryPerformanceCollector>>
        dispatchPerformanceCollectors;
    std::unordered_map<std::string, std::uint64_t>
        presenterRenderGenerations;
    std::unordered_map<std::string,
        std::unique_ptr<xvatsim::modules::overlay::AccessoryPerformanceCollector>>
        performanceCollectors;
    std::unordered_map<std::string, std::string> mainCardSignatures;
    std::unordered_map<std::string, std::string> observed;

    Step3ExecutionContext() {
        measurementContext =
            xvatsim::modules::overlay::InitializeAccessoryTextMeasurement();
        for (const float scale : {0.85f, 1.0f, 1.35f}) {
            const int key = static_cast<int>(std::lround(scale * 1000.0f));
            typography.emplace(
                key,
                xvatsim::modules::overlay::PrepareAccessoryTypography(
                    measurementContext,
                    scale));
        }
    }

    ~Step3ExecutionContext() {
        xvatsim::modules::overlay::ShutdownAccessoryTextMeasurement(
            measurementContext);
        measurementContext = nullptr;
    }

    const xvatsim::modules::overlay::AccessoryTypographyMetrics* Typography(
        float scale) {
        const int key = static_cast<int>(std::lround(scale * 1000.0f));
        auto found = typography.find(key);
        if (found == typography.end()) {
            found = typography.emplace(
                key,
                xvatsim::modules::overlay::PrepareAccessoryTypography(
                    measurementContext,
                    scale)).first;
        }
        return &found->second;
    }

    xvatsim::brain::BrainOwnedRuntimeState* State(const std::string& name) {
        auto& state = states[name];
        if (!state) state = std::make_unique<xvatsim::brain::BrainOwnedRuntimeState>();
        return state.get();
    }
};

xvatsim::modules::overlay::AccessoryGdiMeasurementCounters Step3GdiCounterDelta(
    const xvatsim::modules::overlay::AccessoryGdiMeasurementCounters& before,
    const xvatsim::modules::overlay::AccessoryGdiMeasurementCounters& after) {
    xvatsim::modules::overlay::AccessoryGdiMeasurementCounters result;
    result.measurementCalls = after.measurementCalls - before.measurementCalls;
    result.bitmapConstructions =
        after.bitmapConstructions - before.bitmapConstructions;
    result.graphicsConstructions =
        after.graphicsConstructions - before.graphicsConstructions;
    result.fontConstructions = after.fontConstructions - before.fontConstructions;
    return result;
}

void ObserveStep3GdiCounters(
    const xvatsim::modules::overlay::AccessoryGdiMeasurementCounters& counters,
    const std::string& prefix,
    std::unordered_map<std::string, std::string>* observed) {
    (*observed)[prefix + ".gdi_measurements"] =
        std::to_string(counters.measurementCalls);
    (*observed)[prefix + ".gdi_bitmaps"] =
        std::to_string(counters.bitmapConstructions);
    (*observed)[prefix + ".gdi_graphics"] =
        std::to_string(counters.graphicsConstructions);
    (*observed)[prefix + ".gdi_fonts"] =
        std::to_string(counters.fontConstructions);
}

xvatsim::modules::overlay::AccessoryLayoutResult ResolveStep3Layout(
    Step3ExecutionContext* context,
    xvatsim::modules::overlay::AccessoryLayoutInput input) {
    input.typography = context == nullptr ? nullptr : context->Typography(input.scale);
    return xvatsim::modules::overlay::ResolveAccessoryLayout(input);
}

std::vector<std::string> Step3HistoryKeys(
    const xvatsim::brain::BrainOwnedAccessoryHistory& history) {
    std::vector<std::string> values;
    for (const auto& entry : history.entries) values.push_back(entry.stableKey);
    return values;
}

std::vector<std::string> Step3HistoryBodies(
    const xvatsim::brain::BrainOwnedAccessoryHistory& history) {
    std::vector<std::string> values;
    for (const auto& entry : history.entries) values.push_back(entry.body);
    return values;
}

std::vector<std::string> Step3HistoryTitles(
    const xvatsim::brain::BrainOwnedAccessoryHistory& history) {
    std::vector<std::string> values;
    for (const auto& entry : history.entries) values.push_back(entry.title);
    return values;
}

std::vector<std::string> Step3HistorySequences(
    const xvatsim::brain::BrainOwnedAccessoryHistory& history) {
    std::vector<std::string> values;
    for (const auto& entry : history.entries) {
        values.push_back(std::to_string(entry.acceptedSequence));
    }
    return values;
}

void ObserveStep3History(
    const xvatsim::brain::BrainOwnedAccessoryHistory& history,
    const std::string& prefix,
    std::unordered_map<std::string, std::string>* observed) {
    (*observed)[prefix + ".count"] = std::to_string(history.entries.size());
    (*observed)[prefix + ".bytes"] = std::to_string(history.retainedBytes);
    (*observed)[prefix + ".generation"] = std::to_string(history.generation);
    (*observed)[prefix + ".next_sequence"] =
        std::to_string(history.nextAcceptedSequence);
    (*observed)[prefix + ".keys"] = JoinCsv(Step3HistoryKeys(history));
    (*observed)[prefix + ".titles"] = JoinCsv(Step3HistoryTitles(history));
    (*observed)[prefix + ".bodies"] = JoinCsv(Step3HistoryBodies(history));
    (*observed)[prefix + ".sequences"] = JoinCsv(Step3HistorySequences(history));
    (*observed)[prefix + ".newest_key"] = "";
    (*observed)[prefix + ".oldest_key"] = "";
    (*observed)[prefix + ".newest_sequence"] = "0";
    (*observed)[prefix + ".oldest_sequence"] = "0";
    int caseVariantCount=0;
    bool upperBody=false,lowerBody=false;
    for(const auto& entry:history.entries) {
        if(entry.stableKey=="CaseKey") {caseVariantCount++; upperBody=entry.body=="UPPER";}
        if(entry.stableKey=="casekey") {caseVariantCount++; lowerBody=entry.body=="LOWER";}
    }
    (*observed)[prefix + ".case_variant_count"]=std::to_string(caseVariantCount);
    (*observed)[prefix + ".case_sensitive_pair"]=
        Step3Bool(caseVariantCount==2 && upperBody && lowerBody);
    std::size_t recountedBytes=0;
    for(const auto& entry:history.entries) recountedBytes+=entry.retainedBytes;
    (*observed)[prefix + ".within_count_limit"]=
        Step3Bool(history.entries.size()<=32);
    (*observed)[prefix + ".within_byte_limit"]=
        Step3Bool(history.retainedBytes<=65536);
    (*observed)[prefix + ".recounted_bytes"]=std::to_string(recountedBytes);
    if (!history.entries.empty()) {
        const auto& newest = history.entries.front();
        const auto& oldest = history.entries.back();
        (*observed)[prefix + ".newest_key"] = newest.stableKey;
        (*observed)[prefix + ".oldest_key"] = oldest.stableKey;
        (*observed)[prefix + ".newest_sequence"] =
            std::to_string(newest.acceptedSequence);
        (*observed)[prefix + ".oldest_sequence"] =
            std::to_string(oldest.acceptedSequence);
    }
}

void ObserveStep3RenderCounters(
    const xvatsim::modules::overlay::AccessoryRenderCounters& counters,
    const std::string& prefix,
    std::unordered_map<std::string, std::string>* observed) {
    (*observed)[prefix + ".history_visits"] = std::to_string(counters.historyVisits);
    (*observed)[prefix + ".copies"] = std::to_string(counters.entryCopies);
    (*observed)[prefix + ".wraps"] = std::to_string(counters.wrapVisits);
    (*observed)[prefix + ".main"] = std::to_string(counters.mainCardRasterRequests);
    (*observed)[prefix + ".rail"] = std::to_string(counters.railRasterRequests);
    (*observed)[prefix + ".drawer"] = std::to_string(counters.drawerRasterRequests);
    (*observed)[prefix + ".uploads"] = std::to_string(counters.uploadRequests);
    (*observed)[prefix + ".publications"] =
        std::to_string(counters.snapshotPublications);
}

void ObserveStep3PerformanceSnapshot(
    const xvatsim::modules::overlay::AccessoryPerformanceSnapshot& snapshot,
    const std::string& prefix,
    std::unordered_map<std::string, std::string>* observed) {
    (*observed)[prefix+".epoch"]=std::to_string(snapshot.epoch);
    (*observed)[prefix+".revision"]=
        std::to_string(snapshot.measurementRevision);
    (*observed)[prefix+".threshold_failure"]=
        Step3Bool(snapshot.thresholdFailure);
    (*observed)[prefix+".first_violation_us"]=
        std::to_string(snapshot.firstViolationMicroseconds);
    (*observed)[prefix+".first_violation_category"]=
        xvatsim::modules::overlay::AccessoryPerformanceCategoryToken(
            snapshot.firstViolationCategory);
    (*observed)[prefix+".warning_count"]=
        std::to_string(snapshot.warningCount);
    (*observed)[prefix+".publication_count"]=
        std::to_string(snapshot.publicationCount);
    (*observed)[prefix+".actions_completed"]=
        std::to_string(snapshot.actionsCompleted);
    (*observed)[prefix+".synchronous_wall_within_budget_count"]=
        std::to_string(snapshot.synchronousWallWithinBudgetCount);
    (*observed)[prefix+".frame_cadence_limited_count"]=
        std::to_string(snapshot.frameCadenceLimitedCount);
    (*observed)[prefix+".preparation_limited_count"]=
        std::to_string(snapshot.preparationLimitedCount);
    (*observed)[prefix+".synchronous_wall_failure_count"]=
        std::to_string(snapshot.synchronousWallFailureCount);
    (*observed)[prefix+".render_wall_failure_count"]=
        std::to_string(snapshot.renderWallFailureCount);
    (*observed)[prefix+".timing_unavailable_count"]=
        std::to_string(snapshot.timingUnavailableCount);
    (*observed)[prefix+".cadence_contract_failure_count"]=
        std::to_string(snapshot.cadenceContractFailureCount);
    (*observed)[prefix+".missed_eligible_draws"]=
        std::to_string(snapshot.missedEligibleDraws);
    (*observed)[prefix+".unique_draw_samples"]=
        std::to_string(snapshot.uniqueDrawSamples);
    (*observed)[prefix+".draw_sample_references"]=
        std::to_string(snapshot.drawSampleReferences);
    (*observed)[prefix+".coalesced_action_count"]=
        std::to_string(snapshot.coalescedActionCount);
    (*observed)[prefix+".maximum_actions_per_draw"]=
        std::to_string(snapshot.maximumActionsPerDraw);
    (*observed)[prefix+".violation_record_count"]=
        std::to_string(snapshot.firstViolationRecordCount);
    (*observed)[prefix+".serialized_violation_record_count"]=
        std::to_string(snapshot.firstViolationRecordCount);
    (*observed)[prefix+".dropped_violation_record_count"]=
        std::to_string(snapshot.droppedViolationRecordCount);
    for(std::size_t index=0;index<snapshot.categories.size();++index) {
        const auto category=static_cast<
            xvatsim::modules::overlay::AccessoryPerformanceCategory>(index);
        const auto token=
            xvatsim::modules::overlay::AccessoryPerformanceCategoryToken(category);
        const auto& summary=snapshot.categories[index];
        (*observed)[prefix+"."+token+".count"]=
            std::to_string(summary.count);
        (*observed)[prefix+"."+token+".p50_us"]=
            std::to_string(summary.p50Microseconds);
        (*observed)[prefix+"."+token+".p95_us"]=
            std::to_string(summary.p95Microseconds);
        (*observed)[prefix+"."+token+".max_us"]=
            std::to_string(summary.maximumMicroseconds);
    }
    const char* actionTokens[]{"open", "switch", "close"};
    for(std::size_t actionIndex=0;
        actionIndex<snapshot.actionStageWall.size();++actionIndex) {
        for(std::size_t stageIndex=0;
            stageIndex<snapshot.actionStageWall[actionIndex].size();
            ++stageIndex) {
            const auto stage=static_cast<
                xvatsim::modules::overlay::AccessoryDispatchStage>(stageIndex);
            const auto& summary=
                snapshot.actionStageWall[actionIndex][stageIndex];
            const std::string key=prefix+"."+actionTokens[actionIndex]+"-"+
                xvatsim::modules::overlay::AccessoryDispatchStageToken(stage);
            (*observed)[key+".count"]=std::to_string(summary.count);
            (*observed)[key+".p50_us"]=
                std::to_string(summary.p50Microseconds);
            (*observed)[key+".p95_us"]=
                std::to_string(summary.p95Microseconds);
            (*observed)[key+".max_us"]=
                std::to_string(summary.maximumMicroseconds);
        }
    }
    for(std::size_t index=0;index<snapshot.railRasterReasons.size();++index) {
        const auto reason=static_cast<
            xvatsim::modules::overlay::AccessoryRasterReason>(index);
        const auto token=
            xvatsim::modules::overlay::AccessoryRasterReasonToken(reason);
        (*observed)[prefix+".rail_reason."+token]=
            std::to_string(snapshot.railRasterReasons[index]);
        (*observed)[prefix+".drawer_reason."+token]=
            std::to_string(snapshot.drawerRasterReasons[index]);
    }
    const auto& action=snapshot.lastAction;
    (*observed)[prefix+".last.available"]=Step3Bool(action.available);
    (*observed)[prefix+".last.category"]=
        xvatsim::modules::overlay::AccessoryPerformanceCategoryToken(
            action.actionCategory);
    (*observed)[prefix+".last.classification"]=
        xvatsim::modules::overlay::AccessoryActionTimingClassificationToken(
            action.classification);
    (*observed)[prefix+".last.request_sequence"]=
        std::to_string(action.requestSequence);
    (*observed)[prefix+".last.mouse_fact_accepted_us"]=
        std::to_string(action.mouseFactAcceptedMicroseconds);
    (*observed)[prefix+".last.dispatch_started_us"]=
        std::to_string(action.dispatchStartedMicroseconds);
    (*observed)[prefix+".last.dispatch_completed_us"]=
        std::to_string(action.dispatchCompletedMicroseconds);
    (*observed)[prefix+".last.preceding_draw_entered_us"]=
        std::to_string(action.precedingDrawEnteredMicroseconds);
    (*observed)[prefix+".last.matching_draw_entered_us"]=
        std::to_string(action.matchingDrawEnteredMicroseconds);
    (*observed)[prefix+".last.matching_draw_completed_us"]=
        std::to_string(action.matchingDrawCompletedMicroseconds);
    (*observed)[prefix+".last.dispatch_wall_us"]=
        std::to_string(action.dispatchWallMicroseconds);
    (*observed)[prefix+".last.queued_before_dispatch_us"]=
        std::to_string(action.queuedBeforeDispatchMicroseconds);
    (*observed)[prefix+".last.callback_wait_us"]=
        std::to_string(action.callbackWaitMicroseconds);
    (*observed)[prefix+".last.action_draw_wall_us"]=
        std::to_string(action.actionDrawWallMicroseconds);
    (*observed)[prefix+".last.draw_callback_elapsed_us"]=
        std::to_string(action.matchingDrawCallbackElapsedMicroseconds);
    (*observed)[prefix+".last.combined_action_wall_us"]=
        std::to_string(action.combinedActionWallMicroseconds);
    (*observed)[prefix+".last.preparation_wait_us"]=
        std::to_string(action.preparationWaitMicroseconds);
    (*observed)[prefix+".last.draw_sample_id"]=
        std::to_string(action.drawSampleId);
    (*observed)[prefix+".last.shared_draw_sample"]=
        Step3Bool(action.sharedDrawSample);
    (*observed)[prefix+".last.draw_sample_fan_out"]=
        std::to_string(action.drawSampleFanOut);
    (*observed)[prefix+".last.render_wall_failure"]=
        Step3Bool(action.renderWallFailure);
    (*observed)[prefix+".last.render_wall_failure_category"]=
        xvatsim::modules::overlay::AccessoryPerformanceCategoryToken(
            action.renderWallFailureCategory);
    (*observed)[prefix+".last.render_wall_failure_us"]=
        std::to_string(action.renderWallFailureMicroseconds);
    (*observed)[prefix+".last.end_to_end_us"]=
        std::to_string(action.endToEndMicroseconds);
    (*observed)[prefix+".last.frame_interval_us"]=
        std::to_string(action.containingFrameIntervalMicroseconds);
    (*observed)[prefix+".last.expected_draw_ordinal"]=
        std::to_string(action.expectedDrawOrdinal);
    (*observed)[prefix+".last.matching_draw_ordinal"]=
        std::to_string(action.matchingDrawOrdinal);
    (*observed)[prefix+".last.missed_eligible_draws"]=
        std::to_string(action.missedEligibleDraws);
    (*observed)[prefix+".last.accounting_exact"]=
        Step3Bool(action.accountingExact);
    for(std::size_t stageIndex=0;
        stageIndex<action.stages.elapsedMicroseconds.size();++stageIndex) {
        const auto stage=static_cast<
            xvatsim::modules::overlay::AccessoryDispatchStage>(stageIndex);
        (*observed)[prefix+".last."+
            xvatsim::modules::overlay::AccessoryDispatchStageToken(stage)+
            "_us"]=
                std::to_string(action.stages.elapsedMicroseconds[stageIndex]);
    }
    for(std::size_t index=0;
        index<snapshot.firstViolationRecordCount &&
        index<snapshot.firstViolationRecords.size();++index) {
        (*observed)[prefix+".violation."+std::to_string(index)+
            ".classification"]=
                xvatsim::modules::overlay::
                    AccessoryActionTimingClassificationToken(
                        snapshot.firstViolationRecords[index].classification);
        (*observed)[prefix+".violation."+std::to_string(index)+
            ".request_sequence"]=std::to_string(
                snapshot.firstViolationRecords[index].requestSequence);
        (*observed)[prefix+".violation."+std::to_string(index)+
            ".draw_sample_id"]=std::to_string(
                snapshot.firstViolationRecords[index].drawSampleId);
        (*observed)[prefix+".violation."+std::to_string(index)+
            ".fan_out"]=std::to_string(
                snapshot.firstViolationRecords[index].drawSampleFanOut);
    }
}

void ObserveStep3DispatchSnapshot(
    const xvatsim::modules::overlay::AccessoryInputDispatchSnapshot& snapshot,
    const std::string& prefix,
    std::unordered_map<std::string, std::string>* observed) {
    (*observed)[prefix+".in_flight"]=Step3Bool(snapshot.inFlight);
    (*observed)[prefix+".presentation_bound"]=
        Step3Bool(snapshot.presentationBound);
    (*observed)[prefix+".in_flight_sequence"]=
        std::to_string(snapshot.inFlightRequestSequence);
    (*observed)[prefix+".expected_selection_generation"]=
        std::to_string(snapshot.expectedSelectionGeneration);
    (*observed)[prefix+".expected_render_generation"]=
        std::to_string(snapshot.expectedRenderGeneration);
    (*observed)[prefix+".expected_drawer"]=
        Step3Drawer(snapshot.expectedDrawer);
    (*observed)[prefix+".notifications"]=
        std::to_string(snapshot.dispatchNotifications);
    (*observed)[prefix+".begin_attempts"]=
        std::to_string(snapshot.beginAttempts);
    (*observed)[prefix+".begun_count"]=
        std::to_string(snapshot.requestsBegun);
    (*observed)[prefix+".blocked_count"]=
        std::to_string(snapshot.blockedWhileInFlight);
    (*observed)[prefix+".bound_count"]=
        std::to_string(snapshot.presentationsBound);
    (*observed)[prefix+".completed_count"]=
        std::to_string(snapshot.matchingDrawCompletions);
    (*observed)[prefix+".exact_completed"]=
        std::to_string(snapshot.exactMatchCompletions);
    (*observed)[prefix+".superseded_completed"]=
        std::to_string(snapshot.supersededGenerationCompletions);
    (*observed)[prefix+".explicit_cancellations"]=
        std::to_string(snapshot.explicitCancellations);
    (*observed)[prefix+".selection_superseded_cancellations"]=
        std::to_string(snapshot.selectionSupersededCancellations);
    (*observed)[prefix+".mismatched_draws"]=
        std::to_string(snapshot.mismatchedDrawAttempts);
    (*observed)[prefix+".invalidated_in_flight"]=
        std::to_string(snapshot.invalidatedInFlight);
    (*observed)[prefix+".maximum_in_flight_us"]=
        std::to_string(snapshot.maximumInFlightMicroseconds);
}

bool Step3PresenterTitleMarkerVisible(
    const xvatsim::modules::overlay::AccessoryPresentationState& presenter) {
    if (presenter.preparedPlan == nullptr) return false;
    for(const auto& lines:presenter.preparedPlan->layout.renderedTitleLines) {
        if(Step3ConcatenateLines(lines).find("CONTENT LIMITED")!=
            std::string::npos) return true;
    }
    return false;
}

bool Step3PresenterPairingIntact(
    const xvatsim::modules::overlay::AccessoryPresentationState& presenter) {
    if (presenter.preparedPlan == nullptr) return false;
    const auto& plan = presenter.preparedPlan->layout;
    return plan.renderedEntryKeys.size()==plan.renderedEntryTitles.size() &&
        plan.renderedEntryKeys.size()==plan.renderedEntryBodies.size() &&
        plan.renderedEntryKeys.size()==plan.renderedTitleLines.size() &&
        plan.renderedEntryKeys.size()==plan.renderedBodyLines.size();
}

std::shared_ptr<const xvatsim::modules::overlay::AccessoryPreparedDrawerPlan>
Step3PreparePresentationPlan(
    Step3ExecutionContext* context,
    const xvatsim::brain::BrainOwnedAccessoryPresentationHandle& presentation,
    const xvatsim::modules::overlay::AccessoryLayoutResult& layout) {
    if (context == nullptr || presentation.snapshot == nullptr ||
        presentation.snapshot->activeDrawer ==
            xvatsim::brain::BrainOwnedAccessoryDrawerId::None) return nullptr;
    auto result = std::make_shared<
        xvatsim::modules::overlay::AccessoryPreparedDrawerPlan>();
    result->key.drawer = presentation.snapshot->activeDrawer;
    result->key.historyGeneration = presentation.historyGeneration;
    result->key.layoutGeneration = presentation.layoutGeneration;
    result->key.scaleThousandths = static_cast<int>(
        std::lround(layout.scale * 1000.0f));
    result->key.contentWidth = std::max(1,
        layout.drawerBounds.right - layout.drawerBounds.left -
            (2 * layout.drawerContentInset));
    result->key.visibleLineCapacity = layout.drawerVisibleLineCapacity;
    result->layout = xvatsim::modules::overlay::BuildAccessoryHistoryLayout(
        context->measurementContext, *presentation.snapshot, layout);
    return result;
}

bool ExecuteStep3Action(
    const std::string& actionText,
    Step3ExecutionContext* context,
    std::string* error) {
    if (context == nullptr) return false;
    const auto parts = Split(actionText, ':');
    if (parts.empty()) return false;
    const auto fail = [&](const std::string& message) {
        if (error != nullptr) *error = message + ": " + actionText;
        return false;
    };
    const auto parseInt = [&](const std::string& text, int* value) {
        const auto parsed = ParseNonnegativeInt(text);
        if (!parsed.has_value()) return false;
        *value = *parsed;
        return true;
    };

    if (parts[0] == "new" && parts.size() == 2) {
        context->states[parts[1]] =
            std::make_unique<xvatsim::brain::BrainOwnedRuntimeState>();
        return true;
    }
    if (parts[0] == "worker-contract" && parts.size() == 2) {
        using namespace xvatsim::modules::overlay;
        using Drawer = xvatsim::brain::BrainOwnedAccessoryDrawerId;
        xvatsim::brain::BrainOwnedRuntimeState state;
        const std::array<Drawer,3> drawers{Drawer::Metar,Drawer::Atis,Drawer::Pdc};
        for (const auto drawer : drawers) {
            for (int index = 0; index < 14; ++index) {
                xvatsim::brain::BrainOwnedAccessoryHistoryEntryInput entry;
                entry.drawer = drawer;
                entry.stableKey = "worker-" + std::to_string(static_cast<int>(drawer)) +
                    "-" + std::to_string(index);
                entry.title = index == 13 ? "Unicode éª â Î©" :
                    "Prepared title " + std::to_string(index);
                const char fill = index % 3 == 0 ? 'W' : index % 3 == 1 ? 'i' : 'M';
                entry.body.assign(4800, fill);
                if (index == 13) entry.body += " éªâÎ©";
                xvatsim::brain::AcceptBrainOwnedAccessoryHistoryEntry(&state, entry);
            }
        }
        AccessoryLayoutInput layoutInput;
        layoutInput.screenWidth=1920; layoutInput.screenHeight=1080;
        layoutInput.windowLeft=300; layoutInput.windowTop=900;
        layoutInput.scale=1.0f; layoutInput.cardAnimationProgress=1.0f;
        layoutInput.drawerOpen=true; layoutInput.typography=context->Typography(1.0f);
        const auto layout=ResolveAccessoryLayout(layoutInput);
        const auto submitEventually=[](
            AccessoryPreparationWorker* target,
            const AccessoryPreparationRequest& request) {
            const auto deadline=std::chrono::steady_clock::now()+
                std::chrono::seconds(5);
            while(std::chrono::steady_clock::now()<deadline) {
                if(target->Request(request)) return true;
                const auto state=target->State();
                if(state==AccessoryPreparationWorkerState::Failed ||
                   state==AccessoryPreparationWorkerState::Stopped) return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return false;
        };
        AccessoryPreparationWorker worker;
        const std::uint64_t mainThread=1;
        const bool started=worker.Start(mainThread);
        std::array<AccessoryPreparationKey,3> keys{};
        std::array<std::shared_ptr<const xvatsim::brain::BrainOwnedAccessoryPreparationSnapshot>,3> source{};
        for(std::size_t index=0;index<drawers.size();++index) {
            const auto handle=xvatsim::brain::ProjectBrainOwnedAccessoryPreparation(
                &state,drawers[index],nullptr);
            source[index]=handle.snapshot;
            keys[index].drawer=drawers[index];
            keys[index].historyGeneration=handle.historyGeneration;
            keys[index].contentGeneration=handle.contentGeneration;
            keys[index].layoutGeneration=1;
            keys[index].typographyGeneration=context->Typography(1.0f)->generation;
            keys[index].scaleThousandths=1000;
            keys[index].contentWidth=layout.drawerBounds.right-layout.drawerBounds.left-
                2*layout.drawerContentInset;
            keys[index].visibleLineCapacity=layout.drawerVisibleLineCapacity;
            AccessoryPreparationRequest request;
            request.key=keys[index]; request.snapshot=handle.snapshot; request.layout=layout;
            submitEventually(&worker,request);
        }
        std::array<std::shared_ptr<const AccessoryPreparedDrawerPlan>,3> ready{};
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(std::chrono::steady_clock::now()<deadline) {
            bool all=true;
            for(std::size_t index=0;index<ready.size();++index) {
                if(!ready[index]) ready[index]=worker.TryTakeReady(keys[index],nullptr);
                all=all && ready[index]!=nullptr;
            }
            if(all) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        std::mutex publicationGateMutex;
        std::condition_variable publicationGateChanged;
        bool publicationGateEntered=false;
        bool publicationGateReleased=false;
        AccessoryPreparationWorkerHooks contentionHooks;
        contentionHooks.beforeReadyPublication=[&] {
            std::unique_lock<std::mutex> lock(publicationGateMutex);
            publicationGateEntered=true;
            publicationGateChanged.notify_all();
            publicationGateChanged.wait(lock,[&]{return publicationGateReleased;});
        };
        AccessoryPreparationWorker contentionWorker(std::move(contentionHooks));
        const bool contentionStarted=contentionWorker.Start(mainThread);
        AccessoryPreparationRequest contentionRequest;
        contentionRequest.key=keys[0];
        contentionRequest.snapshot=source[0];
        contentionRequest.layout=layout;
        const bool contentionRequested=submitEventually(
            &contentionWorker,contentionRequest);
        {
            std::unique_lock<std::mutex> lock(publicationGateMutex);
            publicationGateChanged.wait_for(
                lock,std::chrono::seconds(5),[&]{return publicationGateEntered;});
        }
        auto wrongContentionKey=keys[0];
        ++wrongContentionKey.layoutGeneration;
        const auto contentionCheckStarted=std::chrono::steady_clock::now();
        const auto contentionWrong=contentionWorker.TryTakeReady(
            wrongContentionKey,nullptr);
        const auto contentionExact=contentionWorker.TryTakeReady(keys[0],nullptr);
        const auto contentionCheckUs=static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now()-contentionCheckStarted).count());

        std::array<AccessoryPreparationRequest,3> contendedRequests{};
        std::array<bool,3> contendedSubmissionResults{};
        std::uint64_t maximumContendedSubmissionUs=0;
        for(std::size_t index=0;index<contendedRequests.size();++index) {
            contendedRequests[index].key=keys[index];
            contendedRequests[index].key.layoutGeneration=101;
            contendedRequests[index].snapshot=source[index];
            contendedRequests[index].layout=layout;
            const auto enqueueStarted=std::chrono::steady_clock::now();
            contendedSubmissionResults[index]=contentionWorker.Request(
                contendedRequests[index]);
            maximumContendedSubmissionUs=std::max<std::uint64_t>(
                maximumContendedSubmissionUs,
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now()-enqueueStarted).count()));
        }
        const bool callerMarkedSubmitted=std::any_of(
            contendedSubmissionResults.begin(),contendedSubmissionResults.end(),
            [](bool submitted){return submitted;});
        {
            std::lock_guard<std::mutex> lock(publicationGateMutex);
            publicationGateReleased=true;
        }
        publicationGateChanged.notify_all();
        std::array<bool,3> retriedSubmissions{};
        for(std::size_t index=0;index<contendedRequests.size();++index) {
            retriedSubmissions[index]=submitEventually(
                &contentionWorker,contendedRequests[index]);
        }
        const auto staleAfterRetry=contentionWorker.TryTakeReady(
            keys[0],nullptr);
        std::array<std::shared_ptr<const AccessoryPreparedDrawerPlan>,3>
            contentionPublished{};
        const auto contentionDeadline=
            std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(std::any_of(contentionPublished.begin(),contentionPublished.end(),
                          [](const auto& value){return value==nullptr;}) &&
              std::chrono::steady_clock::now()<contentionDeadline) {
            for(std::size_t index=0;index<contentionPublished.size();++index) {
                if(!contentionPublished[index]) {
                    contentionPublished[index]=contentionWorker.TryTakeReady(
                        contendedRequests[index].key,nullptr);
                }
            }
            if(std::any_of(contentionPublished.begin(),contentionPublished.end(),
                           [](const auto& value){return value==nullptr;}))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        auto rapidState=state;
        xvatsim::brain::BrainOwnedAccessorySelectionRequest rapidClick;
        std::array<xvatsim::brain::BrainOwnedAccessorySelectionDecision,3>
            rapidDecisions{};
        for(std::size_t index=0;index<drawers.size();++index) {
            rapidClick.drawer=drawers[index];
            rapidClick.requestSequence=index+1;
            rapidDecisions[index]=
                xvatsim::brain::RequestBrainOwnedAccessoryDrawerSelection(
                    &rapidState,rapidClick);
        }
        const auto rapidPresentation=
            xvatsim::brain::ProjectBrainOwnedAccessoryPresentation(
                &rapidState,101,nullptr);
        AccessoryPresentationState rapidPresenter;
        AccessoryPresentationUpdateInput rapidInput;
        rapidInput.presentation=rapidPresentation;
        rapidInput.layout=layout;
        rapidInput.measurementContext=context->measurementContext;
        rapidInput.preparedPlan=contentionPublished[2];
        const auto rapidUpdate=UpdateAccessoryPresentation(
            &rapidPresenter,rapidInput);
        const auto contentionCounters=contentionWorker.SnapshotCounters();
        const auto contentionIdleBefore=contentionWorker.SnapshotCounters();
        const auto contentionGdiBefore=GetAccessoryGdiMeasurementCounters(
            context->measurementContext);
        const auto contentionIdle=RunUnchangedAccessoryPresentationUpdates(
            &rapidPresenter,rapidInput,10000);
        const auto contentionGdiAfter=GetAccessoryGdiMeasurementCounters(
            context->measurementContext);
        const auto contentionGdiDelta=Step3GdiCounterDelta(
            contentionGdiBefore,contentionGdiAfter);
        const auto contentionIdleAfter=contentionWorker.SnapshotCounters();
        bool contentionExactPublished=true;
        for(std::size_t index=0;index<contentionPublished.size();++index) {
            contentionExactPublished=contentionExactPublished &&
                contentionPublished[index]!=nullptr &&
                contentionPublished[index]->key==contendedRequests[index].key;
        }
        contentionWorker.Stop();

        enum class DelayedStartupStage { Priority, Measurement };
        const auto runDelayedStartupProof=[&](
            DelayedStartupStage delayedStage,
            AccessoryPreparationWorkerFailure expectedFailure) {
            struct Result {
                bool startReturned=false;
                bool repeatedStartReturned=false;
                std::uint64_t startUs=0;
                std::uint64_t repeatedStartUs=0;
                bool remainedStarting=false;
                bool requestRetained=false;
                bool becameReady=false;
                bool exactPublished=false;
                bool failedState=false;
                bool requestRejected=false;
                bool noReady=false;
                bool noRetry=false;
                bool oneDiagnostic=false;
                bool bounded=false;
                bool pendingResolved=false;
                bool stopped=false;
            } result;
            std::mutex gateMutex;
            std::condition_variable gateChanged;
            bool gateEntered=false;
            bool gateReleased=false;
            const auto waitAtGate=[&] {
                std::unique_lock<std::mutex> lock(gateMutex);
                gateEntered=true;
                gateChanged.notify_all();
                gateChanged.wait(lock,[&]{return gateReleased;});
            };
            AccessoryPreparationWorkerHooks hooks;
            hooks.assignBelowNormalPriority=[&] {
                if(delayedStage==DelayedStartupStage::Priority) waitAtGate();
                return expectedFailure!=
                    AccessoryPreparationWorkerFailure::PriorityAssignment;
            };
            if(delayedStage==DelayedStartupStage::Measurement) {
                hooks.initializeTextMeasurement=[&] {
                    waitAtGate();
                    if(expectedFailure==AccessoryPreparationWorkerFailure::
                            TextMeasurementInitialization) {
                        return static_cast<AccessoryTextMeasurementContext*>(nullptr);
                    }
                    return InitializeAccessoryTextMeasurement();
                };
            }
            AccessoryPreparationWorker delayedWorker(std::move(hooks));
            const auto startBegan=std::chrono::steady_clock::now();
            result.startReturned=delayedWorker.Start(mainThread);
            result.startUs=static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now()-startBegan).count());
            {
                std::unique_lock<std::mutex> lock(gateMutex);
                gateChanged.wait_for(
                    lock,std::chrono::seconds(5),[&]{return gateEntered;});
            }
            result.requestRetained=delayedWorker.Request(contentionRequest);
            const auto secondStartBegan=std::chrono::steady_clock::now();
            result.repeatedStartReturned=delayedWorker.Start(mainThread);
            result.repeatedStartUs=static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now()-secondStartBegan).count());
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            const auto gatedCounters=delayedWorker.SnapshotCounters();
            result.remainedStarting=
                delayedWorker.State()==AccessoryPreparationWorkerState::Starting &&
                gatedCounters.maximumQueueDepth<=1 &&
                gatedCounters.jobsRequested==1 && gatedCounters.jobsStarted==0;
            {
                std::lock_guard<std::mutex> lock(gateMutex);
                gateReleased=true;
            }
            gateChanged.notify_all();
            const auto terminalDeadline=
                std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while(delayedWorker.State()==AccessoryPreparationWorkerState::Starting &&
                  std::chrono::steady_clock::now()<terminalDeadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if(expectedFailure==AccessoryPreparationWorkerFailure::None) {
                result.becameReady=
                    delayedWorker.State()==AccessoryPreparationWorkerState::Ready;
                std::shared_ptr<const AccessoryPreparedDrawerPlan> published;
                while(!published && std::chrono::steady_clock::now()<terminalDeadline) {
                    published=delayedWorker.TryTakeReady(keys[0],nullptr);
                    if(!published)
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                result.exactPublished=published!=nullptr &&
                    published->key==keys[0];
            } else {
                result.failedState=
                    delayedWorker.State()==AccessoryPreparationWorkerState::Failed &&
                    delayedWorker.Failure()==expectedFailure;
                result.requestRejected=!delayedWorker.Request(contentionRequest);
                result.noReady=
                    delayedWorker.TryTakeReady(keys[0],nullptr)==nullptr;
            }
            const auto beforeIdle=delayedWorker.SnapshotCounters();
            for(int iteration=0;iteration<10000;++iteration) {
                if(delayedWorker.State()==AccessoryPreparationWorkerState::Ready &&
                   expectedFailure!=AccessoryPreparationWorkerFailure::None) {
                    delayedWorker.Request(contentionRequest);
                }
            }
            const bool failedRestartRejected=
                expectedFailure==AccessoryPreparationWorkerFailure::None ||
                !delayedWorker.Start(mainThread);
            const auto afterIdle=delayedWorker.SnapshotCounters();
            result.noRetry=failedRestartRejected &&
                afterIdle.startupAttemptCount==beforeIdle.startupAttemptCount &&
                afterIdle.jobsRequested==beforeIdle.jobsRequested &&
                afterIdle.jobsStarted==beforeIdle.jobsStarted;
            result.oneDiagnostic=expectedFailure==
                    AccessoryPreparationWorkerFailure::None
                ? afterIdle.startupFailureDiagnosticCount==0
                : afterIdle.startupFailureDiagnosticCount==1;
            result.bounded=afterIdle.maximumQueueDepth<=1 &&
                afterIdle.maximumReadyCacheCount==0 &&
                (expectedFailure==AccessoryPreparationWorkerFailure::None ||
                 (afterIdle.jobsCompleted==0 && afterIdle.jobsCancelled>=1));
            const auto availability=ResolveAccessoryPreparationAvailability(
                AccessoryPreparationWorkerState::Failed,true);
            result.pendingResolved=!availability.ready &&
                !availability.retryStartup && availability.cancelPendingAction;
            delayedWorker.Stop();
            result.stopped=delayedWorker.State()==
                    AccessoryPreparationWorkerState::Stopped &&
                delayedWorker.SnapshotCounters().runningWorkerThreads==0;
            return result;
        };
        const auto delayedSuccess=runDelayedStartupProof(
            DelayedStartupStage::Priority,
            AccessoryPreparationWorkerFailure::None);
        const auto priorityFailure=runDelayedStartupProof(
            DelayedStartupStage::Priority,
            AccessoryPreparationWorkerFailure::PriorityAssignment);
        const auto measurementFailure=runDelayedStartupProof(
            DelayedStartupStage::Measurement,
            AccessoryPreparationWorkerFailure::TextMeasurementInitialization);

        AccessoryPreparationKey newestReplacementKey=keys[2];
        for(std::uint64_t generation=2;generation<=200;++generation) {
            AccessoryPreparationRequest replacement;
            replacement.key=keys[2];
            replacement.key.layoutGeneration=generation;
            replacement.snapshot=source[2]; replacement.layout=layout;
            if(submitEventually(&worker,replacement)) {
                newestReplacementKey=replacement.key;
            }
        }
        std::shared_ptr<const AccessoryPreparedDrawerPlan> replacementReady;
        const auto replacementDeadline=
            std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!replacementReady && std::chrono::steady_clock::now()<replacementDeadline) {
            replacementReady=worker.TryTakeReady(newestReplacementKey,nullptr);
            if(!replacementReady) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        bool reconstruction=true,markers=true,fit=true;
        const bool maximumRetainedPrepared =
            state.accessory.histories[0].retainedBytes > 60000 &&
            state.accessory.histories[1].retainedBytes > 60000 &&
            state.accessory.histories[2].retainedBytes > 60000;
        for(std::size_t drawer=0;drawer<ready.size();++drawer) {
            reconstruction=reconstruction && ready[drawer]!=nullptr && source[drawer]!=nullptr;
            if(!ready[drawer]||!source[drawer]) continue;
            reconstruction=reconstruction &&
                ready[drawer]->layout.renderedEntryKeys.size()==source[drawer]->entries.size();
            for(std::size_t entry=0;entry<source[drawer]->entries.size();++entry) {
                reconstruction=reconstruction &&
                    ready[drawer]->layout.renderedEntryKeys[entry]==source[drawer]->entries[entry].stableKey &&
                    ready[drawer]->layout.renderedEntryTitles[entry]==source[drawer]->entries[entry].title &&
                    ready[drawer]->layout.renderedEntryBodies[entry]==source[drawer]->entries[entry].body;
            }
            markers=markers && ready[drawer]->layout.finalMarkerBelongsToSelectedHistory &&
                ready[drawer]->layout.finalMarker.find("END OF ")==0;
            fit=fit && ready[drawer]->layout.allWrappedLinesFit;
        }
        xvatsim::brain::BrainOwnedAccessorySelectionRequest click;
        click.drawer=Drawer::Metar; click.requestSequence=1;
        xvatsim::brain::RequestBrainOwnedAccessoryDrawerSelection(&state,click);
        auto presentation=xvatsim::brain::ProjectBrainOwnedAccessoryPresentation(&state,1,nullptr);
        AccessoryPresentationState presenter;
        AccessoryPresentationUpdateInput updateInput;
        updateInput.presentation=presentation; updateInput.layout=layout;
        updateInput.measurementContext=context->measurementContext;
        updateInput.preparedPlan=ready[0];
        const auto openStarted=std::chrono::steady_clock::now();
        const auto open=UpdateAccessoryPresentation(&presenter,updateInput);
        const auto openUs=static_cast<std::uint64_t>(std::chrono::duration_cast<
            std::chrono::microseconds>(std::chrono::steady_clock::now()-openStarted).count());
        click.drawer=Drawer::Atis; click.requestSequence=2;
        xvatsim::brain::RequestBrainOwnedAccessoryDrawerSelection(&state,click);
        presentation=xvatsim::brain::ProjectBrainOwnedAccessoryPresentation(&state,1,nullptr);
        updateInput.presentation=presentation; updateInput.preparedPlan=ready[1];
        const auto switchStarted=std::chrono::steady_clock::now();
        const auto switched=UpdateAccessoryPresentation(&presenter,updateInput);
        const auto switchUs=static_cast<std::uint64_t>(std::chrono::duration_cast<
            std::chrono::microseconds>(std::chrono::steady_clock::now()-switchStarted).count());
        const auto workerBeforeIdle=worker.SnapshotCounters();
        const auto idle=RunUnchangedAccessoryPresentationUpdates(&presenter,updateInput,10000);
        const auto workerAfterIdle=worker.SnapshotCounters();
        const bool normalStartupReady=
            worker.State()==AccessoryPreparationWorkerState::Ready &&
            worker.Failure()==AccessoryPreparationWorkerFailure::None;
        auto staleInput=updateInput;
        staleInput.presentation.layoutGeneration=2;
        const auto stale=UpdateAccessoryPresentation(&presenter,staleInput);
        const auto historyCountsBeforeDisable=std::array<std::size_t,3>{
            state.accessory.histories[0].entries.size(),
            state.accessory.histories[1].entries.size(),
            state.accessory.histories[2].entries.size()};
        xvatsim::brain::DisableBrainOwnedAccessoryRuntime(&state);
        worker.CancelAll();
        worker.Stop();
        const bool disabledStopped=worker.SnapshotCounters().runningWorkerThreads==0;
        xvatsim::brain::EnableBrainOwnedAccessoryRuntime(&state);
        worker.Start(mainThread);
        const bool historiesPreserved=historyCountsBeforeDisable==
            std::array<std::size_t,3>{
                state.accessory.histories[0].entries.size(),
                state.accessory.histories[1].entries.size(),
                state.accessory.histories[2].entries.size()};
        AccessoryPreparationRequest recreated;
        recreated.key=keys[0]; recreated.snapshot=source[0]; recreated.layout=layout;
        const bool recreatedRequested=submitEventually(&worker,recreated);
        std::shared_ptr<const AccessoryPreparedDrawerPlan> recreatedReady;
        const auto recreateDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!recreatedReady && std::chrono::steady_clock::now()<recreateDeadline) {
            recreatedReady=worker.TryTakeReady(keys[0],nullptr);
            if(!recreatedReady) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto oldKey=keys[0];
        xvatsim::brain::ResetBrainOwnedAccessoryForSessionReset(&state);
        const auto clearedHandle=xvatsim::brain::ProjectBrainOwnedAccessoryPreparation(
            &state,Drawer::Metar,nullptr);
        AccessoryPreparationRequest clearedRequest;
        clearedRequest.key=oldKey;
        clearedRequest.key.historyGeneration=clearedHandle.historyGeneration;
        clearedRequest.key.contentGeneration=clearedHandle.contentGeneration;
        clearedRequest.snapshot=clearedHandle.snapshot; clearedRequest.layout=layout;
        submitEventually(&worker,clearedRequest);
        const bool resetRejectedOld=worker.TryTakeReady(oldKey,nullptr)==nullptr;
        xvatsim::brain::ResetBrainOwnedAccessoryForCallsignChange(
            &state,"OLDPC","NEWPC");
        worker.CancelAll();
        const bool callsignRejectedOld=worker.TryTakeReady(oldKey,nullptr)==nullptr;
        worker.Stop();
        const auto counters=worker.SnapshotCounters();
        const auto& prefix=parts[1];
        context->observed[prefix+".started"]=Step3Bool(started);
        context->observed[prefix+".all_ready"]=Step3Bool(
            std::all_of(ready.begin(),ready.end(),[](const auto& value){return value!=nullptr;}));
        context->observed[prefix+".reconstruction"]=Step3Bool(reconstruction);
        context->observed[prefix+".markers"]=Step3Bool(markers);
        context->observed[prefix+".fit"]=Step3Bool(fit);
        context->observed[prefix+".maximum_retained_prepared"]=
            Step3Bool(maximumRetainedPrepared);
        context->observed[prefix+".open_under_budget"]=Step3Bool(
            open.status==xvatsim::brain::BrainOwnedAccessoryOperationStatus::Available && openUs<=16700);
        context->observed[prefix+".switch_under_budget"]=Step3Bool(
            switched.status==xvatsim::brain::BrainOwnedAccessoryOperationStatus::Available && switchUs<=16700);
        context->observed[prefix+".stale_rejected"]=Step3Bool(stale.preparationPending);
        context->observed[prefix+".disable_stopped"]=Step3Bool(disabledStopped);
        context->observed[prefix+".disable_enable_preserved"]=Step3Bool(historiesPreserved);
        context->observed[prefix+".resources_recreated"]=Step3Bool(
            recreatedRequested && recreatedReady!=nullptr);
        context->observed[prefix+".replacement_proven"]=Step3Bool(
            replacementReady!=nullptr && counters.jobsReplaced>0);
        context->observed[prefix+".reset_rejected_stale"]=Step3Bool(resetRejectedOld);
        context->observed[prefix+".callsign_rejected_stale"]=Step3Bool(callsignRejectedOld);
        context->observed[prefix+".idle_zero"]=Step3Bool(
            idle.delta.historyVisits==0 && idle.delta.entryCopies==0 && idle.delta.wrapVisits==0 &&
            idle.delta.railRasterRequests==0 && idle.delta.drawerRasterRequests==0 &&
            idle.delta.uploadRequests==0 &&
            workerAfterIdle.jobsRequested==workerBeforeIdle.jobsRequested &&
            workerAfterIdle.jobsStarted==workerBeforeIdle.jobsStarted &&
            workerAfterIdle.enqueueAttemptCount==
                workerBeforeIdle.enqueueAttemptCount);
        context->observed[prefix+".jobs_requested_at_least_three"]=
            Step3Bool(counters.jobsRequested>=3);
        context->observed[prefix+".jobs_completed_at_least_three"]=
            Step3Bool(counters.jobsCompleted>=3);
        context->observed[prefix+".max_queue_depth"]=std::to_string(counters.maximumQueueDepth);
        context->observed[prefix+".max_ready_count"]=std::to_string(counters.maximumReadyCacheCount);
        context->observed[prefix+".queue_bounded"]=Step3Bool(counters.maximumQueueDepth<=3);
        context->observed[prefix+".ready_bounded"]=Step3Bool(counters.maximumReadyCacheCount<=3);
        context->observed[prefix+".priority_requested"]=Step3Bool(counters.priorityRequested);
        context->observed[prefix+".priority_succeeded"]=Step3Bool(counters.prioritySucceeded);
        context->observed[prefix+".thread_distinct"]=Step3Bool(
            counters.workerThreadIdentity!=0 && counters.workerThreadIdentity!=mainThread);
        context->observed[prefix+".prohibited_accesses"]=std::to_string(counters.prohibitedAccessCount);
        context->observed[prefix+".threads_after_stop"]=std::to_string(counters.runningWorkerThreads);
        context->observed[prefix+".wake_count"]=std::to_string(counters.workerWakeCount);
        context->observed[prefix+".sleep_count"]=std::to_string(counters.workerSleepCount);
        context->observed[prefix+".worker_slept_and_woke"]=Step3Bool(
            counters.workerWakeCount>=1 && counters.workerSleepCount>=1);
        context->observed[prefix+".max_slice_us"]=
            std::to_string(counters.maximumContiguousExecutionMicroseconds);
        context->observed[prefix+".publication_max_us"]=
            std::to_string(counters.maximumPublicationMicroseconds);
        context->observed[prefix+".open_us"]=std::to_string(openUs);
        context->observed[prefix+".switch_us"]=std::to_string(switchUs);
        context->observed[prefix+".publication_under_budget"]=Step3Bool(
            counters.maximumPublicationMicroseconds<=16700);
        context->observed[prefix+".contention_started"]=Step3Bool(
            contentionStarted && contentionRequested && publicationGateEntered);
        context->observed[prefix+".contention_nonblocking"]=Step3Bool(
            contentionCheckUs<=1000 && contentionWrong==nullptr &&
            contentionExact==nullptr);
        context->observed[prefix+".contention_stale_rejected"]=Step3Bool(
            contentionWrong==nullptr);
        context->observed[prefix+".contention_exact_published"]=Step3Bool(
            contentionExactPublished);
        context->observed[prefix+".contention_at_least_two"]=Step3Bool(
            contentionCounters.readinessContentionCount>=2);
        context->observed[prefix+".enqueue_contention_nonblocking"]=Step3Bool(
            !callerMarkedSubmitted && maximumContendedSubmissionUs<=16700 &&
            contentionCounters.enqueueContentionCount>=3);
        context->observed[prefix+".enqueue_caller_not_marked"]=
            Step3Bool(!callerMarkedSubmitted);
        context->observed[prefix+".enqueue_retry_exact"]=Step3Bool(
            std::all_of(retriedSubmissions.begin(),retriedSubmissions.end(),
                        [](bool value){return value;}) &&
            staleAfterRetry==nullptr &&
            std::all_of(contentionPublished.begin(),contentionPublished.end(),
                        [](const auto& value){return value!=nullptr;}));
        context->observed[prefix+".enqueue_rapid_serialized"]=Step3Bool(
            rapidDecisions[0].action==
                xvatsim::brain::BrainOwnedAccessoryDrawerAction::Opened &&
            rapidDecisions[1].action==
                xvatsim::brain::BrainOwnedAccessoryDrawerAction::Switched &&
            rapidDecisions[2].action==
                xvatsim::brain::BrainOwnedAccessoryDrawerAction::Switched &&
            rapidState.accessory.selectionGeneration==3 &&
            rapidUpdate.status==
                xvatsim::brain::BrainOwnedAccessoryOperationStatus::Available &&
            rapidPresenter.activeSnapshot!=nullptr &&
            rapidPresenter.activeSnapshot->activeDrawer==Drawer::Pdc &&
            rapidUpdate.publishedSnapshotCount==1);
        context->observed[prefix+".enqueue_idle_zero"]=Step3Bool(
            contentionIdle.delta.historyVisits==0 &&
            contentionIdle.delta.entryCopies==0 &&
            contentionIdle.delta.wrapVisits==0 &&
            contentionIdle.delta.railRasterRequests==0 &&
            contentionIdle.delta.drawerRasterRequests==0 &&
            contentionIdle.delta.uploadRequests==0 &&
            contentionGdiDelta.measurementCalls==0 &&
            contentionGdiDelta.bitmapConstructions==0 &&
            contentionGdiDelta.graphicsConstructions==0 &&
            contentionGdiDelta.fontConstructions==0 &&
            contentionIdleAfter.enqueueAttemptCount==
                contentionIdleBefore.enqueueAttemptCount &&
            contentionIdleAfter.enqueueContentionCount==
                contentionIdleBefore.enqueueContentionCount &&
            contentionIdleAfter.jobsRequested==
                contentionIdleBefore.jobsRequested &&
            contentionIdleAfter.jobsStarted==contentionIdleBefore.jobsStarted &&
            contentionIdleAfter.startupFailureDiagnosticCount==
                contentionIdleBefore.startupFailureDiagnosticCount);
        context->observed[prefix+".enqueue_counters_exact"]=Step3Bool(
            contentionCounters.enqueueAttemptCount==
                contentionCounters.enqueueContentionCount+
                    contentionCounters.enqueueSuccessCount &&
            contentionCounters.enqueueContentionCount>=3 &&
            contentionCounters.enqueueSuccessCount==4 &&
            contentionCounters.maximumEnqueueMicroseconds<=16700 &&
            counters.enqueueReplacementCount==counters.jobsReplaced &&
            counters.enqueueReplacementCount>0);
        context->observed[prefix+".enqueue_attempts"]=
            std::to_string(contentionCounters.enqueueAttemptCount);
        context->observed[prefix+".enqueue_contentions"]=
            std::to_string(contentionCounters.enqueueContentionCount);
        context->observed[prefix+".enqueue_successes"]=
            std::to_string(contentionCounters.enqueueSuccessCount);
        context->observed[prefix+".enqueue_replacements"]=
            std::to_string(contentionCounters.enqueueReplacementCount);
        context->observed[prefix+".enqueue_max_us"]=
            std::to_string(contentionCounters.maximumEnqueueMicroseconds);
        context->observed[prefix+".normal_startup_ready"]=
            Step3Bool(normalStartupReady && counters.startupSuccessCount>=2);
        context->observed[prefix+".delayed_success_async"]=Step3Bool(
            delayedSuccess.startReturned && delayedSuccess.repeatedStartReturned &&
            delayedSuccess.startUs<=16700 && delayedSuccess.repeatedStartUs<=16700 &&
            delayedSuccess.remainedStarting);
        context->observed[prefix+".delayed_success_retained_exact"]=Step3Bool(
            delayedSuccess.requestRetained && delayedSuccess.becameReady &&
            delayedSuccess.exactPublished && delayedSuccess.stopped);
        context->observed[prefix+".priority_failure_explicit"]=Step3Bool(
            priorityFailure.startReturned &&
            priorityFailure.repeatedStartReturned &&
            priorityFailure.startUs<=16700 &&
            priorityFailure.repeatedStartUs<=16700 &&
            priorityFailure.remainedStarting && priorityFailure.failedState);
        context->observed[prefix+".priority_failure_safe"]=Step3Bool(
            priorityFailure.requestRetained && priorityFailure.requestRejected &&
            priorityFailure.noReady &&
            priorityFailure.noRetry && priorityFailure.oneDiagnostic &&
            priorityFailure.bounded && priorityFailure.pendingResolved &&
            priorityFailure.stopped);
        context->observed[prefix+".measurement_failure_explicit"]=Step3Bool(
            measurementFailure.startReturned &&
            measurementFailure.repeatedStartReturned &&
            measurementFailure.startUs<=16700 &&
            measurementFailure.repeatedStartUs<=16700 &&
            measurementFailure.remainedStarting &&
            measurementFailure.failedState);
        context->observed[prefix+".measurement_failure_safe"]=Step3Bool(
            measurementFailure.requestRetained &&
            measurementFailure.requestRejected && measurementFailure.noReady &&
            measurementFailure.noRetry && measurementFailure.oneDiagnostic &&
            measurementFailure.bounded && measurementFailure.pendingResolved &&
            measurementFailure.stopped);
        std::cout << "STEP3_WORKER_PROOF jobsRequested=" << counters.jobsRequested
                  << " jobsReplaced=" << counters.jobsReplaced
                  << " jobsStarted=" << counters.jobsStarted
                  << " jobsCompleted=" << counters.jobsCompleted
                  << " jobsCancelled=" << counters.jobsCancelled
                  << " staleRejected=" << counters.staleResultsRejected
                  << " maxQueueDepth=" << counters.maximumQueueDepth
                  << " maxReadyCount=" << counters.maximumReadyCacheCount
                  << " wakeCount=" << counters.workerWakeCount
                  << " sleepCount=" << counters.workerSleepCount
                  << " maxSliceUs=" << counters.maximumContiguousExecutionMicroseconds
                  << " metarPrepTotalUs=" << counters.totalPreparationMicroseconds[0]
                  << " atisPrepTotalUs=" << counters.totalPreparationMicroseconds[1]
                  << " pdcPrepTotalUs=" << counters.totalPreparationMicroseconds[2]
                  << " publicationMaxUs=" << counters.maximumPublicationMicroseconds
                  << " readinessChecks=" << counters.readinessCheckCount
                  << " readinessContentions=" << counters.readinessContentionCount
                  << " forcedContentionUs=" << contentionCheckUs
                  << " forcedContentions="
                  << contentionCounters.readinessContentionCount
                  << " enqueueAttempts="
                  << contentionCounters.enqueueAttemptCount
                  << " enqueueContentions="
                  << contentionCounters.enqueueContentionCount
                  << " enqueueSuccesses="
                  << contentionCounters.enqueueSuccessCount
                  << " enqueueReplacements="
                  << contentionCounters.enqueueReplacementCount
                  << " enqueueMaxUs="
                  << contentionCounters.maximumEnqueueMicroseconds
                  << " replacementCounter="
                  << counters.enqueueReplacementCount
                  << " startupAttempts=" << counters.startupAttemptCount
                  << " startupSuccesses=" << counters.startupSuccessCount
                  << " startupFailures=" << counters.startupFailureCount
                  << " delayedStartUs=" << delayedSuccess.startUs
                  << " delayedRepeatStartUs=" << delayedSuccess.repeatedStartUs
                  << " delayedPriorityFailureStartUs=" << priorityFailure.startUs
                  << " delayedGdiFailureStartUs=" << measurementFailure.startUs
                  << " openPublishUs=" << openUs
                  << " switchPublishUs=" << switchUs
                  << " priorityRequested=" << (counters.priorityRequested?"true":"false")
                  << " prioritySucceeded=" << (counters.prioritySucceeded?"true":"false")
                  << " prohibitedAccesses=" << counters.prohibitedAccessCount
                  << " threadsAfterStop=" << counters.runningWorkerThreads << '\n';
        return true;
    }
    if (parts[0] == "restart" && parts.size() == 2) {
        context->states[parts[1]].reset();
        context->states[parts[1]] =
            std::make_unique<xvatsim::brain::BrainOwnedRuntimeState>();
        context->observed[parts[1] + ".restart_recreated"] = "true";
        return true;
    }
    if (parts[0] == "mode" && parts.size() == 3) {
        const auto mode = parts[2] == "VFR"
            ? xvatsim::brain::BrainOwnedOperatingMode::VFR
            : xvatsim::brain::BrainOwnedOperatingMode::IFR;
        auto* state = context->State(parts[1]);
        const auto result = xvatsim::brain::RequestBrainOwnedOperatingModeSelection(state, mode);
        context->observed[parts[1] + ".mode"] =
            result.effectiveMode == xvatsim::brain::BrainOwnedOperatingMode::VFR ? "VFR" : "IFR";
        return true;
    }
    if (parts[0] == "accept" && parts.size() == 7) {
        xvatsim::brain::BrainOwnedAccessoryHistoryEntryInput input;
        input.drawer = Step3DrawerFromToken(parts[2]);
        input.stableKey = parts[3];
        input.title = parts[4];
        input.body = parts[5];
        const auto result = xvatsim::brain::AcceptBrainOwnedAccessoryHistoryEntry(
            context->State(parts[1]), input);
        const auto prefix = parts[6];
        context->observed[prefix + ".status"] = Step3Status(result.status);
        context->observed[prefix + ".accepted"] = Step3Bool(result.accepted);
        context->observed[prefix + ".duplicate"] = Step3Bool(result.duplicate);
        context->observed[prefix + ".limited"] = Step3Bool(result.contentLimited);
        context->observed[prefix + ".sequence"] = std::to_string(result.acceptedSequence);
        context->observed[prefix + ".generation"] = std::to_string(result.historyGeneration);
        return true;
    }
    if (parts[0] == "accept-oversized" && parts.size() == 5) {
        xvatsim::brain::BrainOwnedAccessoryHistoryEntryInput input;
        input.drawer = Step3DrawerFromToken(parts[2]);
        input.stableKey = parts[3];
        input.title = std::string(129, 'T');
        input.body = std::string(8189, 'Z') + "\xE2\x82\xAC" +
            std::string(1000, 'Q');
        const auto result = xvatsim::brain::AcceptBrainOwnedAccessoryHistoryEntry(
            context->State(parts[1]), input);
        const auto prefix = parts[4];
        context->observed[prefix + ".status"] = Step3Status(result.status);
        context->observed[prefix + ".accepted"] = Step3Bool(result.accepted);
        context->observed[prefix + ".limited"] = Step3Bool(result.contentLimited);
        const auto& history = context->State(parts[1])->accessory.histories[
            Step3DrawerIndex(input.drawer)];
        context->observed[prefix + ".stored_count"] =
            std::to_string(history.entries.size());
        if (!history.entries.empty()) {
            const auto& stored = history.entries.front();
            context->observed[prefix + ".stored_valid_utf8"] =
                Step3Bool(Step3IsValidUtf8(stored.title) && Step3IsValidUtf8(stored.body));
            context->observed[prefix + ".stored_marker_visible"] =
                Step3Bool(stored.title.find("CONTENT LIMITED") != std::string::npos ||
                          stored.body.find("CONTENT LIMITED") != std::string::npos);
            context->observed[prefix + ".stored_bytes"] =
                std::to_string(stored.retainedBytes);
            context->observed[prefix + ".recounted_bytes"] =
                std::to_string(stored.stableKey.size() + stored.title.size() + stored.body.size());
            context->observed[prefix + ".title_bytes"] =
                std::to_string(stored.title.size());
            context->observed[prefix + ".body_bytes"] =
                std::to_string(stored.body.size());
            context->observed[prefix + ".title_within_limit"] =
                Step3Bool(stored.title.size() <= 128);
            context->observed[prefix + ".body_within_limit"] =
                Step3Bool(stored.body.size() <= 8192);
            context->observed[prefix + ".entry_within_limit"] =
                Step3Bool(stored.retainedBytes <= 128 + 128 + 8192);
            context->observed[prefix + ".drawer_within_limit"] =
                Step3Bool(history.retainedBytes <= 65536);
            const std::string marker="CONTENT LIMITED";
            const bool markerAtEnd=
                (stored.title.size()>=marker.size() &&
                 stored.title.compare(stored.title.size()-marker.size(),marker.size(),marker)==0) ||
                (stored.body.size()>=marker.size() &&
                 stored.body.compare(stored.body.size()-marker.size(),marker.size(),marker)==0);
            context->observed[prefix + ".marker_at_end"] = Step3Bool(markerAtEnd);
            context->observed[prefix + ".marker_bytes_accounted"] = Step3Bool(
                markerAtEnd && stored.retainedBytes==stored.stableKey.size()+
                    stored.title.size()+stored.body.size());
        } else {
            context->observed[prefix + ".stored_valid_utf8"] = "false";
            context->observed[prefix + ".stored_marker_visible"] = "false";
            context->observed[prefix + ".stored_bytes"] = "0";
            context->observed[prefix + ".recounted_bytes"] = "0";
            context->observed[prefix + ".title_bytes"] = "0";
            context->observed[prefix + ".body_bytes"] = "0";
            context->observed[prefix + ".title_within_limit"] = "false";
            context->observed[prefix + ".body_within_limit"] = "false";
            context->observed[prefix + ".entry_within_limit"] = "false";
            context->observed[prefix + ".drawer_within_limit"] = "false";
            context->observed[prefix + ".marker_at_end"] = "false";
            context->observed[prefix + ".marker_bytes_accounted"] = "false";
        }
        return true;
    }
    if (parts[0] == "accept-title-only-oversized" && parts.size() == 5) {
        xvatsim::brain::BrainOwnedAccessoryHistoryEntryInput input;
        input.drawer=Step3DrawerFromToken(parts[2]);
        input.stableKey=parts[3];
        input.title=std::string(129,'T');
        input.body="SHORT BODY";
        const auto result=xvatsim::brain::AcceptBrainOwnedAccessoryHistoryEntry(
            context->State(parts[1]),input);
        const auto& history=context->State(parts[1])->accessory.histories[
            Step3DrawerIndex(input.drawer)];
        const auto stored=std::find_if(
            history.entries.begin(),history.entries.end(),
            [&](const auto& entry){return entry.stableKey==input.stableKey;});
        const auto& prefix=parts[4];
        context->observed[prefix+".accepted"]=Step3Bool(result.accepted);
        context->observed[prefix+".limited"]=Step3Bool(result.contentLimited);
        context->observed[prefix+".stored_title_marker"]=Step3Bool(
            stored!=history.entries.end() &&
            stored->title.find("CONTENT LIMITED")!=std::string::npos);
        context->observed[prefix+".body_unchanged"]=Step3Bool(
            stored!=history.entries.end() && stored->body=="SHORT BODY");
        return true;
    }
    if (parts[0] == "accept-contract" && parts.size() == 5) {
        xvatsim::brain::BrainOwnedAccessoryHistoryEntryInput input;
        input.drawer=Step3DrawerFromToken(parts[2]);
        const auto& variant=parts[3];
        if(variant=="key128") {
            input.stableKey=std::string(128,'K'); input.title="TITLE"; input.body="BODY";
        } else if(variant=="key129") {
            input.stableKey=std::string(129,'K'); input.title="TITLE"; input.body="BODY";
        } else if(variant=="empty-key") {
            input.stableKey=""; input.title="TITLE"; input.body="BODY";
        } else if(variant=="bad-key") {
            input.stableKey=std::string("BAD\xC3\x28",5); input.title="TITLE"; input.body="BODY";
        } else if(variant=="bad-title") {
            input.stableKey="BAD_TITLE"; input.title=std::string("T\xC3\x28",3); input.body="BODY";
        } else if(variant=="bad-body") {
            input.stableKey="BAD_BODY"; input.title="TITLE"; input.body=std::string("B\xC3\x28",3);
        } else if(variant=="crlf") {
            input.stableKey="CRLF"; input.title="Title\r\nLine"; input.body="A  B\r\nC\tD";
        } else if(variant=="case-upper") {
            input.stableKey="CaseKey"; input.title="CASE"; input.body="UPPER";
        } else if(variant=="case-lower") {
            input.stableKey="casekey"; input.title="CASE"; input.body="LOWER";
        } else if(variant=="key-crlf") {
            input.stableKey="EQUIV\r\nKEY"; input.title="SAME"; input.body="SAME";
        } else if(variant=="key-lf") {
            input.stableKey="EQUIV\nKEY"; input.title="SAME"; input.body="SAME";
        } else {
            return fail("unknown history contract variant");
        }
        auto* state=context->State(parts[1]);
        auto& history=state->accessory.histories[Step3DrawerIndex(input.drawer)];
        const auto beforeCount=history.entries.size();
        const auto result=xvatsim::brain::AcceptBrainOwnedAccessoryHistoryEntry(state,input);
        const auto& prefix=parts[4];
        context->observed[prefix+".status"]=Step3Status(result.status);
        context->observed[prefix+".accepted"]=Step3Bool(result.accepted);
        context->observed[prefix+".duplicate"]=Step3Bool(result.duplicate);
        context->observed[prefix+".sequence"]=
            std::to_string(result.acceptedSequence);
        context->observed[prefix+".generation"]=
            std::to_string(result.historyGeneration);
        context->observed[prefix+".count_changed"]=
            Step3Bool(history.entries.size()!=beforeCount);
        const auto expectedStoredKey=Step3NormalizeCrlf(input.stableKey);
        const auto stored=std::find_if(
            history.entries.begin(),history.entries.end(),
            [&](const auto& entry){return entry.stableKey==expectedStoredKey;});
        context->observed[prefix+".stored"]=Step3Bool(stored!=history.entries.end());
        context->observed[prefix+".stored_key_bytes"]=
            stored==history.entries.end()?"0":std::to_string(stored->stableKey.size());
        context->observed[prefix+".stored_key_exact"]=
            Step3Bool(stored!=history.entries.end() &&
                      stored->stableKey==expectedStoredKey);
        context->observed[prefix+".source_digest_hex"]=
            stored==history.entries.end()
                ? ""
                : Step3DigestHex(stored->sourceContentDigest);
        context->observed[prefix+".key_crlf_normalized"]=Step3Bool(
            stored!=history.entries.end() &&
            stored->stableKey=="EQUIV\nKEY" &&
            stored->stableKey.find('\r')==std::string::npos);
        context->observed[prefix+".crlf_normalized"]=Step3Bool(
            stored!=history.entries.end() &&
            stored->title=="Title\nLine" && stored->body=="A  B\nC\tD");
        context->observed[prefix+".whitespace_preserved"]=Step3Bool(
            stored!=history.entries.end() &&
            stored->body.find("A  B")!=std::string::npos &&
            stored->body.find('\t')!=std::string::npos);
        return true;
    }
    if (parts[0] == "accept-limited-tail-updates" && parts.size() == 4) {
        auto* state=context->State(parts[1]);
        const auto drawer=Step3DrawerFromToken(parts[2]);
        const auto accept=[&](
            const std::string& key,
            const std::string& title,
            const std::string& body) {
            xvatsim::brain::BrainOwnedAccessoryHistoryEntryInput input;
            input.drawer=drawer;
            input.stableKey=key;
            input.title=title;
            input.body=body;
            return xvatsim::brain::AcceptBrainOwnedAccessoryHistoryEntry(state,input);
        };
        const auto& history=state->accessory.histories[Step3DrawerIndex(drawer)];
        const auto findEntry=[&](const std::string& key) {
            return std::find_if(
                history.entries.begin(),history.entries.end(),
                [&](const auto& entry){return entry.stableKey==key;});
        };
        const std::string titlePrefix(200,'T');
        const auto titleFirst=accept("TAIL_TITLE",titlePrefix+"A","BODY");
        const auto firstTitleEntry=findEntry("TAIL_TITLE");
        const auto firstTitleDigest=firstTitleEntry==history.entries.end()
            ? std::array<std::uint8_t,32>{}
            : firstTitleEntry->sourceContentDigest;
        const auto titleSecond=accept("TAIL_TITLE",titlePrefix+"B","BODY");
        const auto secondTitleEntry=findEntry("TAIL_TITLE");
        const auto secondTitleDigest=secondTitleEntry==history.entries.end()
            ? std::array<std::uint8_t,32>{}
            : secondTitleEntry->sourceContentDigest;
        const std::string bodyPrefix(9000,'B');
        const auto bodyFirst=accept("TAIL_BODY","TITLE",bodyPrefix+"A");
        const auto firstBodyEntry=findEntry("TAIL_BODY");
        const auto firstBodyDigest=firstBodyEntry==history.entries.end()
            ? std::array<std::uint8_t,32>{}
            : firstBodyEntry->sourceContentDigest;
        const auto bodySecond=accept("TAIL_BODY","TITLE",bodyPrefix+"B");
        const auto secondBodyEntry=findEntry("TAIL_BODY");
        const auto secondBodyDigest=secondBodyEntry==history.entries.end()
            ? std::array<std::uint8_t,32>{}
            : secondBodyEntry->sourceContentDigest;
        const auto storedTitle=findEntry("TAIL_TITLE");
        const auto storedBody=findEntry("TAIL_BODY");
        const auto& prefix=parts[3];
        context->observed[prefix+".title_first_accepted"]=
            Step3Bool(titleFirst.accepted);
        context->observed[prefix+".title_update_accepted"]=
            Step3Bool(titleSecond.accepted);
        context->observed[prefix+".title_update_duplicate"]=
            Step3Bool(titleSecond.duplicate);
        context->observed[prefix+".title_sequence_advanced"]=Step3Bool(
            titleSecond.acceptedSequence==titleFirst.acceptedSequence+1);
        context->observed[prefix+".title_generation_advanced"]=Step3Bool(
            titleSecond.historyGeneration==titleFirst.historyGeneration+1);
        context->observed[prefix+".title_retained_limited"]=Step3Bool(
            storedTitle!=history.entries.end() && storedTitle->contentLimited &&
            storedTitle->title.size()==128);
        context->observed[prefix+".title_sha256_changed"]=Step3Bool(
            firstTitleDigest!=secondTitleDigest);
        context->observed[prefix+".title_digest_bytes"]=
            std::to_string(secondTitleDigest.size());
        context->observed[prefix+".body_first_accepted"]=
            Step3Bool(bodyFirst.accepted);
        context->observed[prefix+".body_update_accepted"]=
            Step3Bool(bodySecond.accepted);
        context->observed[prefix+".body_update_duplicate"]=
            Step3Bool(bodySecond.duplicate);
        context->observed[prefix+".body_sequence_advanced"]=Step3Bool(
            bodySecond.acceptedSequence==bodyFirst.acceptedSequence+1);
        context->observed[prefix+".body_generation_advanced"]=Step3Bool(
            bodySecond.historyGeneration==bodyFirst.historyGeneration+1);
        context->observed[prefix+".body_retained_limited"]=Step3Bool(
            storedBody!=history.entries.end() && storedBody->contentLimited &&
            storedBody->body.size()==8192);
        context->observed[prefix+".body_sha256_changed"]=Step3Bool(
            firstBodyDigest!=secondBodyDigest);
        context->observed[prefix+".body_digest_bytes"]=
            std::to_string(secondBodyDigest.size());
        return true;
    }
    if (parts[0] == "series" && parts.size() == 6) {
        int count = 0;
        int bodyLength = 0;
        if (!parseInt(parts[3], &count) || !parseInt(parts[4], &bodyLength)) return fail("invalid series count");
        auto* state = context->State(parts[1]);
        auto drawer = Step3DrawerFromToken(parts[2]);
        int accepted = 0;
        for (int index = 1; index <= count; ++index) {
            xvatsim::brain::BrainOwnedAccessoryHistoryEntryInput input;
            input.drawer = drawer;
            input.stableKey = "K" + std::to_string(index);
            input.title = "T" + std::to_string(index);
            input.body = std::string(static_cast<std::size_t>(bodyLength), 'X');
            if (xvatsim::brain::AcceptBrainOwnedAccessoryHistoryEntry(state, input).accepted) accepted++;
        }
        const auto& history = state->accessory.histories[Step3DrawerIndex(drawer)];
        context->observed[parts[5] + ".accepted"] = std::to_string(accepted);
        ObserveStep3History(history, parts[5], &context->observed);
        context->observed[parts[5] + ".within_count_limit"] =
            Step3Bool(history.entries.size() <= 32);
        context->observed[parts[5] + ".within_byte_limit"] =
            Step3Bool(history.retainedBytes <= 65536);
        std::size_t recountedBytes = 0;
        for (const auto& entry : history.entries) recountedBytes += entry.retainedBytes;
        context->observed[parts[5] + ".recounted_bytes"] =
            std::to_string(recountedBytes);
        return true;
    }
    if (parts[0] == "click" && parts.size() == 5) {
        int sequence = 0;
        if (!parseInt(parts[3], &sequence)) return fail("invalid click sequence");
        xvatsim::brain::BrainOwnedAccessorySelectionRequest request;
        request.drawer = Step3DrawerFromToken(parts[2]);
        request.requestSequence = static_cast<std::uint64_t>(sequence);
        const auto decision = xvatsim::brain::RequestBrainOwnedAccessoryDrawerSelection(
            context->State(parts[1]), request);
        const auto prefix = parts[4];
        context->observed[prefix + ".status"] = Step3Status(decision.status);
        context->observed[prefix + ".action"] = Step3Action(decision.action);
        context->observed[prefix + ".previous"] = Step3Drawer(decision.previousDrawer);
        context->observed[prefix + ".active"] = Step3Drawer(decision.activeDrawer);
        context->observed[prefix + ".generation"] = std::to_string(decision.selectionGeneration);
        return true;
    }
    if (parts[0] == "click-from-hit" && parts.size() == 5) {
        const auto hit = context->hits.find(parts[2]);
        if (hit == context->hits.end()) return fail("hit result missing");
        int sequence = 0;
        if (!parseInt(parts[3], &sequence)) return fail("invalid click sequence");
        xvatsim::brain::BrainOwnedAccessorySelectionRequest request;
        request.drawer = hit->second.drawer;
        request.requestSequence = static_cast<std::uint64_t>(sequence);
        const auto decision = xvatsim::brain::RequestBrainOwnedAccessoryDrawerSelection(
            context->State(parts[1]), request);
        const auto& prefix = parts[4];
        context->observed[prefix + ".hit_handled"] = Step3Bool(hit->second.handled);
        context->observed[prefix + ".requested_drawer"] = Step3Drawer(request.drawer);
        context->observed[prefix + ".status"] = Step3Status(decision.status);
        context->observed[prefix + ".action"] = Step3Action(decision.action);
        context->observed[prefix + ".previous"] = Step3Drawer(decision.previousDrawer);
        context->observed[prefix + ".active"] = Step3Drawer(decision.activeDrawer);
        context->observed[prefix + ".generation"] =
            std::to_string(decision.selectionGeneration);
        return true;
    }
    if (parts[0] == "click-queue-new" && parts.size() == 2) {
        context->clickQueues[parts[1]] =
            xvatsim::modules::overlay::AccessoryClickFactQueue{};
        return true;
    }
    if (parts[0] == "click-queue-produce" && parts.size() == 5) {
        int started = 0;
        if(!parseInt(parts[3],&started)) return fail("invalid click start time");
        auto& queue=context->clickQueues[parts[1]];
        xvatsim::modules::overlay::AccessoryClickFact fact;
        const auto produced=queue.Produce(
            Step3DrawerFromToken(parts[2]),
            static_cast<std::uint64_t>(started),
            &fact);
        const auto& prefix=parts[4];
        context->observed[prefix+".produced"]=Step3Bool(produced);
        context->observed[prefix+".drawer"]=Step3Drawer(fact.drawer);
        context->observed[prefix+".sequence"]=
            std::to_string(fact.requestSequence);
        context->observed[prefix+".started_us"]=
            std::to_string(fact.startedMicroseconds);
        context->observed[prefix+".pending"]=
            std::to_string(queue.PendingCount());
        context->observed[prefix+".next_sequence"]=
            std::to_string(queue.NextSequence());
        return true;
    }
    if (parts[0] == "click-queue-produce-hit" && parts.size() == 5) {
        int started = 0;
        if(!parseInt(parts[3],&started)) return fail("invalid hit click start time");
        const auto hit=context->hits.find(parts[2]);
        if(hit==context->hits.end()) return fail("click queue hit missing");
        auto& queue=context->clickQueues[parts[1]];
        xvatsim::modules::overlay::AccessoryClickFact fact;
        const auto produced=queue.Produce(
            hit->second.drawer,
            static_cast<std::uint64_t>(started),
            &fact);
        const auto& prefix=parts[4];
        context->observed[prefix+".hit_handled"]=
            Step3Bool(hit->second.handled);
        context->observed[prefix+".produced"]=Step3Bool(produced);
        context->observed[prefix+".drawer"]=Step3Drawer(fact.drawer);
        context->observed[prefix+".sequence"]=
            std::to_string(fact.requestSequence);
        context->observed[prefix+".pending"]=
            std::to_string(queue.PendingCount());
        return true;
    }
    if (parts[0] == "click-queue-discard" && parts.size() == 3) {
        auto& queue=context->clickQueues[parts[1]];
        const auto nextBefore=queue.NextSequence();
        const auto discarded=queue.DiscardPending();
        const auto& prefix=parts[2];
        context->observed[prefix+".discarded"]=std::to_string(discarded);
        context->observed[prefix+".pending"]=
            std::to_string(queue.PendingCount());
        context->observed[prefix+".next_sequence_unchanged"]=Step3Bool(
            queue.NextSequence()==nextBefore);
        context->observed[prefix+".discarded_total"]=
            std::to_string(queue.DiscardedCount());
        return true;
    }
    if (parts[0] == "click-queue-consume" && parts.size() == 3) {
        auto& queue=context->clickQueues[parts[1]];
        xvatsim::modules::overlay::AccessoryClickFact fact;
        const auto consumed=queue.Consume(&fact);
        const auto& prefix=parts[2];
        context->observed[prefix+".consumed"]=Step3Bool(consumed);
        context->observed[prefix+".drawer"]=
            consumed ? Step3Drawer(fact.drawer) : "NONE";
        context->observed[prefix+".sequence"]=
            consumed ? std::to_string(fact.requestSequence) : "0";
        context->observed[prefix+".pending"]=
            std::to_string(queue.PendingCount());
        return true;
    }
    if (parts[0] == "click-queue-consume-to-brain" && parts.size() == 4) {
        auto& queue=context->clickQueues[parts[1]];
        xvatsim::modules::overlay::AccessoryClickFact fact;
        const auto consumed=queue.Consume(&fact);
        const auto& prefix=parts[3];
        context->observed[prefix+".consumed"]=Step3Bool(consumed);
        if(!consumed) {
            context->observed[prefix+".action"]="None";
            return true;
        }
        xvatsim::brain::BrainOwnedAccessorySelectionRequest request;
        request.drawer=fact.drawer;
        request.requestSequence=fact.requestSequence;
        const auto decision=
            xvatsim::brain::RequestBrainOwnedAccessoryDrawerSelection(
                context->State(parts[2]),request);
        context->observed[prefix+".drawer"]=Step3Drawer(request.drawer);
        context->observed[prefix+".requested_drawer"]=Step3Drawer(request.drawer);
        context->observed[prefix+".sequence"]=
            std::to_string(request.requestSequence);
        context->observed[prefix+".action"]=Step3Action(decision.action);
        context->observed[prefix+".generation"]=
            std::to_string(decision.selectionGeneration);
        context->observed[prefix+".pending"]=
            std::to_string(queue.PendingCount());
        return true;
    }
    if (parts[0] == "dispatch-new" && parts.size() == 3) {
        int epoch=0;
        if(!parseInt(parts[2],&epoch)||epoch==0) {
            return fail("invalid dispatch epoch");
        }
        context->inputDispatchers[parts[1]] =
            xvatsim::modules::overlay::AccessoryInputDispatchCoordinator{};
        auto collector=std::make_unique<
            xvatsim::modules::overlay::AccessoryPerformanceCollector>();
        collector->ResetForNewProcess(static_cast<std::uint64_t>(epoch));
        context->dispatchPerformanceCollectors[parts[1]]=std::move(collector);
        return true;
    }
    if (parts[0] == "dispatch-produce" && parts.size() == 6) {
        int started=0;
        if(!parseInt(parts[4],&started)) return fail("invalid dispatch click time");
        auto dispatcher=context->inputDispatchers.find(parts[1]);
        if(dispatcher==context->inputDispatchers.end()) {
            return fail("input dispatcher missing");
        }
        auto& queue=context->clickQueues[parts[2]];
        xvatsim::modules::overlay::AccessoryClickFact fact;
        const auto produced=queue.Produce(
            Step3DrawerFromToken(parts[3]),
            static_cast<std::uint64_t>(started),&fact);
        if(produced) dispatcher->second.RecordDispatchNotification();
        const auto& prefix=parts[5];
        context->observed[prefix+".produced"]=Step3Bool(produced);
        context->observed[prefix+".sequence"]=
            std::to_string(fact.requestSequence);
        context->observed[prefix+".pending"]=
            std::to_string(queue.PendingCount());
        ObserveStep3DispatchSnapshot(
            dispatcher->second.Snapshot(),prefix,&context->observed);
        return true;
    }
    if (parts[0] == "dispatch-begin-present" && parts.size() == 10) {
        int layoutGeneration=0,dispatchTime=0;
        if(!parseInt(parts[7],&layoutGeneration)||
           !parseInt(parts[8],&dispatchTime)) {
            return fail("invalid dispatch presentation input");
        }
        auto dispatcher=context->inputDispatchers.find(parts[1]);
        auto collector=context->dispatchPerformanceCollectors.find(parts[1]);
        const auto layout=context->layouts.find(parts[5]);
        if(dispatcher==context->inputDispatchers.end()||
           collector==context->dispatchPerformanceCollectors.end()) {
            return fail("input dispatcher missing");
        }
        if(layout==context->layouts.end()) return fail("dispatch layout missing");
        auto& queue=context->clickQueues[parts[2]];
        auto* state=context->State(parts[3]);
        auto& presenter=context->presenters[parts[4]];
        xvatsim::modules::overlay::AccessoryClickFact fact;
        const auto began=dispatcher->second.TryBegin(&queue,&fact);
        const auto& prefix=parts[9];
        context->observed[prefix+".began"]=Step3Bool(began);
        context->observed[prefix+".pending"]=
            std::to_string(queue.PendingCount());
        if(!began) {
            ObserveStep3DispatchSnapshot(
                dispatcher->second.Snapshot(),prefix,&context->observed);
            return true;
        }
        xvatsim::brain::BrainOwnedAccessorySelectionRequest request;
        request.drawer=fact.drawer;
        request.requestSequence=fact.requestSequence;
        const auto decision=
            xvatsim::brain::RequestBrainOwnedAccessoryDrawerSelection(
                state,request);
        xvatsim::brain::BrainOwnedAccessoryProjectionCounters projectionCounters;
        const auto presentation=
            xvatsim::brain::ProjectBrainOwnedAccessoryPresentation(
                state,static_cast<std::uint64_t>(layoutGeneration),
                &projectionCounters);
        xvatsim::modules::overlay::AccessoryPresentationUpdateInput input;
        input.presentation=presentation;
        input.layout=layout->second;
        input.preparedPlan=Step3PreparePresentationPlan(
            context,presentation,layout->second);
        input.measurementContext=context->measurementContext;
        const auto main=context->mainCardSignatures.find(parts[6]);
        if(main!=context->mainCardSignatures.end()) {
            input.mainCardProductionSignature=main->second;
        }
        const auto update=
            xvatsim::modules::overlay::UpdateAccessoryPresentation(
                &presenter,input);
        auto& renderGeneration=context->presenterRenderGenerations[parts[4]];
        if(update.delta.railRasterRequests>0 ||
           update.delta.drawerRasterRequests>0) {
            ++renderGeneration;
        }
        const auto bound=dispatcher->second.BindPresentation(
            fact.requestSequence,decision.action,
            presenter.selectionGeneration,renderGeneration);
        xvatsim::modules::overlay::AccessoryActionDispatchTimingInput timing;
        timing.dispatchStartedMicroseconds=fact.startedMicroseconds;
        timing.dispatchCompletedMicroseconds=
            static_cast<std::uint64_t>(dispatchTime);
        const auto timingBegan=bound && collector->second->BeginDrawerAction(
            decision.action,fact.requestSequence,fact.startedMicroseconds,
            presenter.selectionGeneration,renderGeneration,
            static_cast<std::uint64_t>(dispatchTime),0,timing);
        context->observed[prefix+".bound"]=Step3Bool(bound);
        context->observed[prefix+".timing_began"]=Step3Bool(timingBegan);
        context->observed[prefix+".sequence"]=
            std::to_string(fact.requestSequence);
        context->observed[prefix+".action"]=Step3Action(decision.action);
        context->observed[prefix+".active"]=Step3Drawer(decision.activeDrawer);
        context->observed[prefix+".selection_generation"]=
            std::to_string(presenter.selectionGeneration);
        context->observed[prefix+".render_generation"]=
            std::to_string(renderGeneration);
        context->observed[prefix+".dispatch_latency_us"]=
            std::to_string(static_cast<std::uint64_t>(dispatchTime)>=
                fact.startedMicroseconds
                ? static_cast<std::uint64_t>(dispatchTime)-fact.startedMicroseconds
                : 0);
        context->observed[prefix+".projection_history_visits"]=
            std::to_string(projectionCounters.historyVisits);
        context->observed[prefix+".dispatch_record"]=
            "began=true,sequence="+std::to_string(fact.requestSequence)+
            ",action="+Step3Action(decision.action)+
            ",dispatchLatencyUs="+
                context->observed[prefix+".dispatch_latency_us"]+
            ",selectionGeneration="+
                std::to_string(presenter.selectionGeneration)+
            ",renderGeneration="+std::to_string(renderGeneration)+
            ",generalLoopInvocations=0";
        ObserveStep3RenderCounters(update.delta,prefix,&context->observed);
        ObserveStep3DispatchSnapshot(
            dispatcher->second.Snapshot(),prefix,&context->observed);
        return true;
    }
    if (parts[0] == "dispatch-draw" &&
        (parts.size() == 8 || parts.size() == 9)) {
        int completed=0;
        if(!parseInt(parts[4],&completed)) return fail("invalid dispatch draw time");
        auto dispatcher=context->inputDispatchers.find(parts[1]);
        auto collector=context->dispatchPerformanceCollectors.find(parts[1]);
        auto presenter=context->presenters.find(parts[3]);
        if(dispatcher==context->inputDispatchers.end()||
           collector==context->dispatchPerformanceCollectors.end()||
           presenter==context->presenters.end()) {
            return fail("dispatch draw state missing");
        }
        std::uint64_t selectionGeneration=presenter->second.selectionGeneration;
        std::uint64_t renderGeneration=
            context->presenterRenderGenerations[parts[3]];
        if(parts[5]!="current") {
            try { selectionGeneration=std::stoull(parts[5]); }
            catch(...) { return fail("invalid draw selection generation"); }
        }
        if(parts[6]!="current") {
            try { renderGeneration=std::stoull(parts[6]); }
            catch(...) { return fail("invalid draw render generation"); }
        }
        auto selectedDrawer = presenter->second.activeSnapshot == nullptr
                ? xvatsim::brain::BrainOwnedAccessoryDrawerId::None
                : presenter->second.activeSnapshot->activeDrawer;
        const auto& prefix = parts.size() == 9 ? parts[8] : parts[7];
        if(parts.size() == 9) {
            selectedDrawer = Step3DrawerFromToken(parts[7]);
        }
        const auto completion=dispatcher->second.CompleteMatchingDraw(
            selectedDrawer,
            selectionGeneration,renderGeneration,
            static_cast<std::uint64_t>(completed));
        const auto matchingPerformanceAction=
            collector->second->HasMatchingPendingAction(
                selectionGeneration,renderGeneration,0);
        xvatsim::modules::overlay::AccessoryActionDrawTimingInput drawTiming;
        drawTiming.actionDrawWallMicroseconds=1;
        const auto performanceCompleted=
            collector->second->CompletePendingActions(
                static_cast<std::uint64_t>(completed),
                selectionGeneration,renderGeneration,0,0,0,drawTiming);
        auto& queue=context->clickQueues[parts[2]];
        if(completion.completed && queue.PendingCount()>0) {
            dispatcher->second.RecordDispatchNotification();
        }
        context->observed[prefix+".completed"]=
            Step3Bool(completion.completed);
        context->observed[prefix+".terminal"]=
            Step3Bool(completion.terminal);
        context->observed[prefix+".cancelled"]=
            Step3Bool(completion.cancelled);
        context->observed[prefix+".disposition"]=
            xvatsim::modules::overlay::AccessoryInputDispatchDispositionToken(
                completion.disposition);
        context->observed[prefix+".sequence"]=
            std::to_string(completion.fact.requestSequence);
        context->observed[prefix+".action"]=Step3Action(completion.action);
        context->observed[prefix+".performance_completed"]=
            std::to_string(performanceCompleted);
        context->observed[prefix+".pending"]=
            std::to_string(queue.PendingCount());
        context->observed[prefix+".dispatch_record"]=
            "completed="+Step3Bool(completion.completed)+
            ",sequence="+std::to_string(completion.fact.requestSequence)+
            ",action="+Step3Action(completion.action)+
            ",selectionGeneration="+std::to_string(selectionGeneration)+
            ",renderGeneration="+std::to_string(renderGeneration)+
            ",performanceCompleted="+
                std::to_string(performanceCompleted);
        ObserveStep3DispatchSnapshot(
            dispatcher->second.Snapshot(),prefix,&context->observed);
        ObserveStep3PerformanceSnapshot(
            collector->second->Snapshot(),prefix,&context->observed);
        return true;
    }
    if (parts[0] == "dispatch-invalidate" && parts.size() == 4) {
        auto dispatcher=context->inputDispatchers.find(parts[1]);
        auto collector=context->dispatchPerformanceCollectors.find(parts[1]);
        if(dispatcher==context->inputDispatchers.end()||
           collector==context->dispatchPerformanceCollectors.end()) {
            return fail("input dispatcher missing");
        }
        const auto invalidated=dispatcher->second.InvalidateInFlight();
        collector->second->DiscardPendingActions();
        auto& queue=context->clickQueues[parts[2]];
        const auto discarded=queue.DiscardPending();
        const auto& prefix=parts[3];
        context->observed[prefix+".invalidated"]=Step3Bool(invalidated);
        context->observed[prefix+".discarded"]=std::to_string(discarded);
        context->observed[prefix+".next_sequence"]=
            std::to_string(queue.NextSequence());
        ObserveStep3DispatchSnapshot(
            dispatcher->second.Snapshot(),prefix,&context->observed);
        return true;
    }
    if (parts[0] == "dispatch-snapshot" && parts.size() == 3) {
        const auto dispatcher=context->inputDispatchers.find(parts[1]);
        if(dispatcher==context->inputDispatchers.end()) {
            return fail("input dispatcher missing");
        }
        ObserveStep3DispatchSnapshot(
            dispatcher->second.Snapshot(),parts[2],&context->observed);
        const auto snapshot=dispatcher->second.Snapshot();
        context->observed[parts[2]+".dispatch_record"]=
            "notifications="+std::to_string(snapshot.dispatchNotifications)+
            ",beginAttempts="+std::to_string(snapshot.beginAttempts)+
            ",begun="+std::to_string(snapshot.requestsBegun)+
            ",bound="+std::to_string(snapshot.presentationsBound)+
            ",completed="+
                std::to_string(snapshot.matchingDrawCompletions)+
            ",mismatched="+
                std::to_string(snapshot.mismatchedDrawAttempts);
        return true;
    }
    if (parts[0] == "project" && parts.size() == 4) {
        int generation = 0;
        if (!parseInt(parts[2], &generation)) return fail("invalid layout generation");
        xvatsim::brain::BrainOwnedAccessoryProjectionCounters counters;
        auto handle = xvatsim::brain::ProjectBrainOwnedAccessoryPresentation(
            context->State(parts[1]), static_cast<std::uint64_t>(generation), &counters);
        context->snapshots[parts[3]] = handle;
        context->observed[parts[3] + ".available"] = Step3Bool(handle.snapshot != nullptr &&
            handle.snapshot->status == xvatsim::brain::BrainOwnedAccessoryOperationStatus::Available);
        context->observed[parts[3] + ".orb_count"] =
            handle.snapshot ? std::to_string(handle.snapshot->orbs.size()) : "0";
        context->observed[parts[3] + ".entry_count"] =
            handle.snapshot ? std::to_string(handle.snapshot->entries.size()) : "0";
        context->observed[parts[3] + ".active"] =
            handle.snapshot ? Step3Drawer(handle.snapshot->activeDrawer) : "NONE";
        context->observed[parts[3] + ".history_visits"] = std::to_string(counters.historyVisits);
        context->observed[parts[3] + ".entries_copied"] = std::to_string(counters.entriesCopied);
        context->observed[parts[3] + ".snapshot_identity"] =
            std::to_string(handle.snapshot ? handle.snapshot->snapshotIdentity : 0);
        context->observed[parts[3] + ".selection_generation"] =
            std::to_string(handle.selectionGeneration);
        context->observed[parts[3] + ".history_generation"] =
            std::to_string(handle.historyGeneration);
        context->observed[parts[3] + ".callsign"] =
            handle.snapshot ? handle.snapshot->callsignIdentity : "";
        std::vector<std::string> labels;
        int neutralCount=0,selectedCount=0,openIndicatorCount=0;
        std::string selectedDrawer;
        if(handle.snapshot!=nullptr) {
            for(const auto& orb:handle.snapshot->orbs) {
                labels.push_back(orb.label);
                if(orb.neutral) neutralCount++;
                if(orb.selected) {
                    selectedCount++;
                    selectedDrawer=Step3Drawer(orb.drawer);
                }
                if(orb.selectedIndicator=="OPEN") openIndicatorCount++;
            }
        }
        context->observed[parts[3]+".labels"]=JoinCsv(labels);
        context->observed[parts[3]+".neutral_count"]=std::to_string(neutralCount);
        context->observed[parts[3]+".selected_count"]=std::to_string(selectedCount);
        context->observed[parts[3]+".selected_drawer"]=selectedDrawer;
        context->observed[parts[3]+".open_indicator_count"]=
            std::to_string(openIndicatorCount);
        context->observed[parts[3]+".empty_state_text"]=
            handle.snapshot ? handle.snapshot->emptyStateText : "";
        bool placeholderCached=false;
        if(handle.snapshot!=nullptr && !handle.snapshot->emptyStateText.empty()) {
            for(const auto& history:context->State(parts[1])->accessory.histories) {
                for(const auto& entry:history.entries) {
                    placeholderCached=placeholderCached ||
                        entry.title==handle.snapshot->emptyStateText ||
                        entry.body==handle.snapshot->emptyStateText;
                }
            }
        }
        context->observed[parts[3]+".placeholder_cached"]=Step3Bool(placeholderCached);
        std::vector<std::string> keys;
        std::vector<std::string> bodies;
        std::vector<std::string> sequences;
        if (handle.snapshot != nullptr) {
            for (const auto& entry : handle.snapshot->entries) {
                keys.push_back(entry.stableKey);
                bodies.push_back(entry.body);
                sequences.push_back(std::to_string(entry.acceptedSequence));
            }
        }
        context->observed[parts[3] + ".keys"] = JoinCsv(keys);
        context->observed[parts[3] + ".bodies"] = JoinCsv(bodies);
        context->observed[parts[3] + ".sequences"] = JoinCsv(sequences);
        return true;
    }
    if (parts[0] == "history" && parts.size() == 4) {
        const auto drawer = Step3DrawerFromToken(parts[2]);
        const auto& history = context->State(parts[1])->accessory.histories[Step3DrawerIndex(drawer)];
        ObserveStep3History(history, parts[3], &context->observed);
        return true;
    }
    if (parts[0] == "seed-all" && parts.size() == 4) {
        auto* state = context->State(parts[1]);
        const xvatsim::brain::BrainOwnedAccessoryDrawerId drawers[]{
            xvatsim::brain::BrainOwnedAccessoryDrawerId::Metar,
            xvatsim::brain::BrainOwnedAccessoryDrawerId::Atis,
            xvatsim::brain::BrainOwnedAccessoryDrawerId::Pdc};
        const char* tokens[]{"M", "A", "P"};
        int accepted = 0;
        for (std::size_t index = 0; index < 3; ++index) {
            xvatsim::brain::BrainOwnedAccessoryHistoryEntryInput input;
            input.drawer = drawers[index];
            input.stableKey = std::string(tokens[index]) + parts[2];
            input.title = std::string(tokens[index]) + "-TITLE";
            input.body = std::string(tokens[index]) + "-BODY-" + parts[2];
            if (xvatsim::brain::AcceptBrainOwnedAccessoryHistoryEntry(state, input).accepted) {
                accepted++;
            }
        }
        context->observed[parts[3] + ".accepted"] = std::to_string(accepted);
        bool keysCorrect=true,bodiesCorrect=true,sequencesCorrect=true;
        bool bytesAccounted=true,generationsAdvanced=true;
        for (std::size_t index = 0; index < 3; ++index) {
            const auto& history=state->accessory.histories[index];
            ObserveStep3History(
                history,
                parts[3] + "." + tokens[index],
                &context->observed);
            keysCorrect=keysCorrect && history.entries.size()==1 &&
                history.entries.front().stableKey==std::string(tokens[index])+parts[2];
            bodiesCorrect=bodiesCorrect && history.entries.size()==1 &&
                history.entries.front().body==std::string(tokens[index])+"-BODY-"+parts[2];
            sequencesCorrect=sequencesCorrect && history.entries.size()==1 &&
                history.entries.front().acceptedSequence==1;
            bytesAccounted=bytesAccounted && history.entries.size()==1 &&
                history.retainedBytes==history.entries.front().retainedBytes &&
                history.retainedBytes==history.entries.front().stableKey.size()+
                    history.entries.front().title.size()+history.entries.front().body.size();
            generationsAdvanced=generationsAdvanced && history.generation==1;
        }
        context->observed[parts[3]+".keys_correct"]=Step3Bool(keysCorrect);
        context->observed[parts[3]+".bodies_correct"]=Step3Bool(bodiesCorrect);
        context->observed[parts[3]+".sequences_correct"]=Step3Bool(sequencesCorrect);
        context->observed[parts[3]+".bytes_accounted"]=Step3Bool(bytesAccounted);
        context->observed[parts[3]+".generations_advanced"]=
            Step3Bool(generationsAdvanced);
        return true;
    }
    if (parts[0] == "accessory-state" && parts.size() == 3) {
        const auto* state = context->State(parts[1]);
        context->observed[parts[2] + ".active"] =
            Step3Drawer(state->accessory.activeDrawer);
        context->observed[parts[2] + ".callsign"] = state->accessory.callsignIdentity;
        context->observed[parts[2] + ".callsign_generation"] =
            std::to_string(state->accessory.callsignIdentityGeneration);
        context->observed[parts[2] + ".clear_generation"] =
            std::to_string(state->accessory.historyClearGeneration);
        const char* tokens[]{"M", "A", "P"};
        for (std::size_t index = 0; index < 3; ++index) {
            ObserveStep3History(
                state->accessory.histories[index],
                parts[2] + "." + tokens[index],
                &context->observed);
        }
        return true;
    }
    if (parts[0] == "compare-history-observations" && parts.size() == 4) {
        const char* drawers[]{"M", "A", "P"};
        const char* fields[]{"keys", "bodies", "sequences", "bytes", "generation"};
        for (const auto* field : fields) {
            bool equal = true;
            for (const auto* drawer : drawers) {
                const auto before = context->observed.find(
                    parts[1] + "." + drawer + "." + field);
                const auto after = context->observed.find(
                    parts[2] + "." + drawer + "." + field);
                if (before == context->observed.end() ||
                    after == context->observed.end()) {
                    return fail("history observation missing");
                }
                equal = equal && before->second == after->second;
            }
            context->observed[parts[3] + "." + field + "_equal"] = Step3Bool(equal);
        }
        return true;
    }
    if (parts[0] == "compare-cleared-history-observations" && parts.size() == 4) {
        const char* drawers[]{"M", "A", "P"};
        bool keysCleared=true,bodiesCleared=true,sequencesCleared=true;
        bool bytesCleared=true,generationsAdvanced=true;
        for (const auto* drawer : drawers) {
            const auto key = [&](const std::string& prefix, const char* field) {
                return context->observed.find(prefix + "." + drawer + "." + field);
            };
            const auto beforeGeneration=key(parts[1],"generation");
            const auto afterGeneration=key(parts[2],"generation");
            const auto afterKeys=key(parts[2],"keys");
            const auto afterBodies=key(parts[2],"bodies");
            const auto afterSequences=key(parts[2],"sequences");
            const auto afterBytes=key(parts[2],"bytes");
            if(beforeGeneration==context->observed.end() ||
               afterGeneration==context->observed.end() ||
               afterKeys==context->observed.end() ||
               afterBodies==context->observed.end() ||
               afterSequences==context->observed.end() ||
               afterBytes==context->observed.end()) {
                return fail("cleared history observation missing");
            }
            keysCleared=keysCleared && afterKeys->second.empty();
            bodiesCleared=bodiesCleared && afterBodies->second.empty();
            sequencesCleared=sequencesCleared && afterSequences->second.empty();
            bytesCleared=bytesCleared && afterBytes->second=="0";
            try {
                generationsAdvanced=generationsAdvanced &&
                    std::stoull(afterGeneration->second)==
                        std::stoull(beforeGeneration->second)+1;
            } catch (...) {
                return fail("invalid history generation observation");
            }
        }
        context->observed[parts[3]+".keys_cleared"]=Step3Bool(keysCleared);
        context->observed[parts[3]+".bodies_cleared"]=Step3Bool(bodiesCleared);
        context->observed[parts[3]+".sequences_cleared"]=Step3Bool(sequencesCleared);
        context->observed[parts[3]+".bytes_cleared"]=Step3Bool(bytesCleared);
        context->observed[parts[3]+".generations_advanced"]=
            Step3Bool(generationsAdvanced);
        return true;
    }
    if (parts[0] == "cache-refresh" && parts.size() == 2) {
        xvatsim::brain::ResetBrainOwnedRuntimeCachePreservingFlightContext(context->State(parts[1]));
        context->observed[parts[1] + ".cache_refreshed"] = "true";
        return true;
    }
    if (parts[0] == "boundary" && (parts.size() == 4 || parts.size() == 6)) {
        auto* state = context->State(parts[1]);
        xvatsim::brain::BrainOwnedAccessoryBoundaryDecision decision;
        if (parts[2] == "close-display") decision = xvatsim::brain::CloseBrainOwnedAccessoryForDisplayClose(state);
        else if (parts[2] == "xpilot-disconnect") decision = xvatsim::brain::CloseBrainOwnedAccessoryForTemporaryXPilotDisconnect(state);
        else if (parts[2] == "invalid-aircraft") decision = xvatsim::brain::CloseBrainOwnedAccessoryForInvalidAircraft(state);
        else if (parts[2] == "plugin-disable") decision = xvatsim::brain::DisableBrainOwnedAccessoryRuntime(state);
        else if (parts[2] == "plugin-enable") decision = xvatsim::brain::EnableBrainOwnedAccessoryRuntime(state);
        else if (parts[2] == "overlay-sleep") decision = xvatsim::brain::CloseBrainOwnedAccessoryForTemporaryOverlaySleep(state);
        else if (parts[2] == "session-reset") decision = xvatsim::brain::ResetBrainOwnedAccessoryForSessionReset(state);
        else if (parts[2] == "new-flight") decision = xvatsim::brain::ResetBrainOwnedAccessoryForConfirmedNewFlight(state);
        else if (parts[2] == "cold-dark") decision = xvatsim::brain::ResetBrainOwnedAccessoryForConfirmedColdDark(state);
        else if (parts[2] == "callsign-change" && parts.size() == 6) {
            decision = xvatsim::brain::ResetBrainOwnedAccessoryForCallsignChange(
                state, parts[3], parts[4]);
        }
        else if (parts[2] == "plugin-stop") decision = xvatsim::brain::StopBrainOwnedAccessoryRuntime(state);
        else return fail("unknown lifecycle entry point");
        const auto& prefix = parts.back();
        context->observed[prefix + ".status"] = Step3Status(decision.status);
        context->observed[prefix + ".closed"] = Step3Bool(decision.drawerClosed);
        context->observed[prefix + ".cleared"] = Step3Bool(decision.historiesCleared);
        context->observed[prefix + ".cleared_before_identity"] =
            Step3Bool(decision.clearedBeforeIdentityProjection);
        context->observed[prefix + ".previous_callsign"] = decision.previousCallsign;
        context->observed[prefix + ".active_callsign"] = decision.activeCallsign;
        context->observed[prefix + ".clear_generation"] =
            std::to_string(decision.historyClearGeneration);
        context->observed[prefix + ".identity_generation"] =
            std::to_string(decision.callsignIdentityGeneration);
        return true;
    }
    if (parts[0] == "layout" && parts.size() == 10) {
        int width=0,height=0,left=0,top=0,open=0;
        if (!parseInt(parts[2],&width)||!parseInt(parts[3],&height)||!parseInt(parts[5],&left)||
            !parseInt(parts[6],&top)||!parseInt(parts[8],&open)) return fail("invalid layout integer");
        xvatsim::modules::overlay::AccessoryLayoutInput input;
        input.screenWidth=width; input.screenHeight=height; input.scale=std::stof(parts[4]);
        input.windowLeft=left; input.windowTop=top; input.drawerOpen=open!=0;
        input.cardAnimationProgress=std::stof(parts[7]);
        auto result=ResolveStep3Layout(context,input);
        context->layouts[parts[9]]=result;
        context->observed[parts[9]+".status"]=Step3Status(result.status);
        context->observed[parts[9]+".visible"]=Step3Bool(result.accessoriesVisible);
        context->observed[parts[9]+".interactive"]=Step3Bool(result.accessoriesInteractive);
        context->observed[parts[9]+".orb_count"]=std::to_string(result.orbs.size());
        context->observed[parts[9]+".closed_width"]=std::to_string(result.closedWidth);
        context->observed[parts[9]+".closed_height"]=std::to_string(result.closedHeight);
        context->observed[parts[9]+".open_width"]=std::to_string(result.openWidth);
        context->observed[parts[9]+".open_height"]=std::to_string(result.openHeight);
        context->observed[parts[9]+".closed_design_width"]=
            std::to_string(result.closedDesignWidth);
        context->observed[parts[9]+".closed_design_height"]=
            std::to_string(result.closedDesignHeight);
        context->observed[parts[9]+".open_design_width"]=
            std::to_string(result.openDesignWidth);
        context->observed[parts[9]+".open_design_height"]=
            std::to_string(result.openDesignHeight);
        context->observed[parts[9]+".orb_design_diameter"]=
            std::to_string(result.orbDiameterDesignPixels);
        context->observed[parts[9]+".dirty"]=Step3Bool(result.positionSettingsDirty);
        context->observed[parts[9]+".inside"]=Step3Bool(result.allPhysicalBoundsInsideScreen);
        context->observed[parts[9]+".temporary_clamp"]=
            Step3Bool(result.temporaryClampApplied);
        context->observed[parts[9]+".closed_size_restored"]=
            Step3Bool(result.closedDimensionsRestored);
        context->observed[parts[9]+".anchor_restored"]=
            Step3Bool(result.closedAnchorRestored);
        context->observed[parts[9]+".drawer_content_inset"]=
            std::to_string(result.drawerContentInset);
        context->observed[parts[9]+".drawer_header_top"]=
            std::to_string(result.drawerHeaderTop);
        context->observed[parts[9]+".drawer_header_height"]=
            std::to_string(result.drawerHeaderHeight);
        context->observed[parts[9]+".drawer_content_top"]=
            std::to_string(result.drawerContentTop);
        context->observed[parts[9]+".drawer_content_bottom"]=
            std::to_string(result.drawerContentBottom);
        context->observed[parts[9]+".drawer_line_height"]=
            std::to_string(result.drawerLineHeight);
        context->observed[parts[9]+".drawer_visible_capacity"]=
            std::to_string(result.drawerVisibleLineCapacity);
        context->observed[parts[9]+".drawer_capacity_pixel_fit"]=
            Step3Bool(result.drawerContentTop +
                result.drawerVisibleLineCapacity * result.drawerLineHeight <=
                result.drawerContentBottom);
        context->observed[parts[9]+".drawer_next_line_clips"]=
            Step3Bool(result.drawerContentTop +
                (result.drawerVisibleLineCapacity + 1) * result.drawerLineHeight >
                result.drawerContentBottom);
        if(!result.orbs.empty()) context->observed[parts[9]+".orb_diameter"]=std::to_string(result.orbs.front().diameterPhysicalPixels);
        return true;
    }
    if (parts[0] == "layout-warm" && parts.size() == 4) {
        int iterations=0;
        if(!parseInt(parts[1],&iterations)) return fail("invalid layout warm count");
        xvatsim::modules::overlay::AccessoryLayoutInput input;
        input.scale=std::stof(parts[2]);
        input.drawerOpen=true;
        input.cardAnimationProgress=1.0f;
        input.typography=context->Typography(input.scale);
        const auto gdiBefore=
            xvatsim::modules::overlay::GetAccessoryGdiMeasurementCounters(
                context->measurementContext);
        int available=0;
        for(int index=0;index<iterations;++index) {
            const auto layout=
                xvatsim::modules::overlay::ResolveAccessoryLayout(input);
            if(layout.status==
                xvatsim::brain::BrainOwnedAccessoryOperationStatus::Available) {
                available++;
            }
        }
        const auto gdiAfter=
            xvatsim::modules::overlay::GetAccessoryGdiMeasurementCounters(
                context->measurementContext);
        const auto& prefix=parts[3];
        context->observed[prefix+".iterations"]=std::to_string(iterations);
        context->observed[prefix+".available"]=std::to_string(available);
        const auto gdiDelta=Step3GdiCounterDelta(gdiBefore,gdiAfter);
        ObserveStep3GdiCounters(
            gdiDelta,
            prefix,
            &context->observed);
        context->observed[prefix+".zero_work_record"]=
            "iterations="+std::to_string(iterations)+
            ",gdi="+std::to_string(gdiDelta.measurementCalls)+
            ",bitmaps="+std::to_string(gdiDelta.bitmapConstructions)+
            ",graphics="+std::to_string(gdiDelta.graphicsConstructions)+
            ",fonts="+std::to_string(gdiDelta.fontConstructions);
        return true;
    }
    if (parts[0] == "layout-matrix" && parts.size() == 2) {
        const int screens[][2]{{1280,720},{1920,1080},{3840,2160}};
        const float scales[]{0.85f,1.0f,1.35f};
        int cases=0,available=0,violations=0,boundsPass=0;
        int capacityCases=0,capacityHeightPass=0,capacityRemainingPass=0;
        int exactDesignPass=0;
        int temporaryClampPass=0,intentionalPass=0,restorePass=0;
        int screenChangeClampPass=0,intentionalAnchorRestorePass=0;
        for(const auto& screen:screens) for(float scale:scales) for(int edge=0;edge<4;++edge){
            xvatsim::modules::overlay::AccessoryLayoutInput input;
            input.screenWidth=screen[0]; input.screenHeight=screen[1]; input.scale=scale;
            input.drawerOpen=true; input.cardAnimationProgress=1.0f;
            input.savedClosedAnchorValid=true;
            input.savedClosedAnchorLeft=screen[0]/2;
            input.savedClosedAnchorTop=screen[1]/2;
            input.windowLeft=edge==0 ? -200 : (edge==1 ? screen[0]+200 : screen[0]/2);
            input.windowTop=edge==2 ? screen[1]+200 : (edge==3 ? -200 : screen[1]/2);
            const auto result=ResolveStep3Layout(context,input);
            cases++;
            if(result.status==xvatsim::brain::BrainOwnedAccessoryOperationStatus::Available) available++; else violations++;
            const auto rectInside=[&](const xvatsim::modules::overlay::AccessoryRect& rect){
                return rect.left>=0 && rect.bottom>=0 && rect.right<=screen[0] &&
                    rect.top<=screen[1] && rect.left<=rect.right && rect.bottom<=rect.top;
            };
            bool physicalBounds = result.allPhysicalBoundsInsideScreen &&
                result.resolvedBounds.left >= 0 && result.resolvedBounds.bottom >= 0 &&
                result.resolvedBounds.right <= screen[0] && result.resolvedBounds.top <= screen[1] &&
                rectInside(result.resolvedBounds) && rectInside(result.drawerBounds);
            for(const auto& orb:result.orbs) physicalBounds=physicalBounds && rectInside(orb.bounds);
            if (physicalBounds) boundsPass++; else violations++;
            const bool exactDesign = result.closedDesignWidth==430 &&
                result.closedDesignHeight==374 && result.openDesignWidth==430 &&
                result.openDesignHeight==506 && result.orbDiameterDesignPixels==52 &&
                result.railDesignBounds.top==320 && result.railDesignBounds.bottom==372 &&
                result.drawerDesignBounds.top==378 && result.drawerDesignBounds.bottom==494 &&
                result.mainCardToRailGapDesignPixels==2 &&
                result.interOrbGapDesignPixels==16 &&
                result.railToDrawerGapDesignPixels==6 &&
                result.openBottomMarginDesignPixels==12;
            if(exactDesign) exactDesignPass++; else violations++;
            if(screen[1]==720 && scale==1.35f) {
                capacityCases++;
                if(result.openHeight==683) capacityHeightPass++; else violations++;
                const int remainingCapacity=screen[1]-result.openHeight;
                if(remainingCapacity==37 && remainingCapacity>=16) {
                    capacityRemainingPass++;
                } else {
                    violations++;
                }
            }
            const bool temporary = result.temporaryClampApplied &&
                !result.positionSettingsDirty && !result.intentionalMoveApplied;
            if (temporary) temporaryClampPass++; else violations++;

            auto intentionalInput = input;
            intentionalInput.anchorOperation =
                xvatsim::modules::overlay::AccessoryAnchorOperation::IntentionalMove;
            const auto intentional=ResolveStep3Layout(context,intentionalInput);
            const bool intentionalOk = intentional.intentionalMoveApplied &&
                intentional.positionSettingsDirty;
            if (intentionalOk) intentionalPass++; else violations++;

            auto closedInput = input;
            closedInput.drawerOpen = false;
            closedInput.anchorOperation =
                xvatsim::modules::overlay::AccessoryAnchorOperation::CloseDrawer;
            closedInput.windowLeft = result.resolvedLeft;
            closedInput.windowTop = result.resolvedTop;
            const auto closed=ResolveStep3Layout(context,closedInput);
            const int expectedClosedLeft=std::clamp(
                closedInput.savedClosedAnchorLeft,
                0,
                std::max(0,closedInput.screenWidth-closed.closedWidth));
            const int expectedClosedTop=std::clamp(
                closedInput.savedClosedAnchorTop,
                std::min(closed.closedHeight,closedInput.screenHeight),
                closedInput.screenHeight);
            const bool restored = closed.closedDimensionsRestored &&
                closed.closedAnchorRestored && !closed.positionSettingsDirty &&
                closed.resolvedLeft==expectedClosedLeft &&
                closed.resolvedTop==expectedClosedTop &&
                closed.resolvedBounds.right-closed.resolvedBounds.left==closed.closedWidth &&
                closed.resolvedBounds.top-closed.resolvedBounds.bottom==closed.closedHeight;
            if (restored) restorePass++; else violations++;

            auto changedScreenInput=closedInput;
            changedScreenInput.savedClosedAnchorLeft=screen[0]+200;
            changedScreenInput.savedClosedAnchorTop=screen[1]+200;
            const auto changedScreen=ResolveStep3Layout(context,changedScreenInput);
            const bool screenChangedClamped=
                changedScreen.closedAnchorRestored &&
                changedScreen.temporaryClampApplied &&
                !changedScreen.positionSettingsDirty &&
                changedScreen.allPhysicalBoundsInsideScreen;
            if(screenChangedClamped) screenChangeClampPass++; else violations++;

            auto intentionalClosedInput=closedInput;
            intentionalClosedInput.savedClosedAnchorLeft=
                intentional.restorableClosedAnchorLeft;
            intentionalClosedInput.savedClosedAnchorTop=
                intentional.restorableClosedAnchorTop;
            intentionalClosedInput.windowLeft=intentional.resolvedLeft;
            intentionalClosedInput.windowTop=intentional.resolvedTop;
            const auto intentionalClosed=
                ResolveStep3Layout(context,intentionalClosedInput);
            const bool intentionalAnchorRestored=
                intentional.restorableClosedAnchorLeft==intentional.resolvedLeft &&
                intentional.restorableClosedAnchorTop==intentional.resolvedTop &&
                intentionalClosed.closedAnchorRestored &&
                intentionalClosed.resolvedLeft==intentional.restorableClosedAnchorLeft &&
                intentionalClosed.resolvedTop==intentional.restorableClosedAnchorTop &&
                !intentionalClosed.positionSettingsDirty;
            if(intentionalAnchorRestored) intentionalAnchorRestorePass++;
            else violations++;
        }
        context->observed[parts[1]+".cases"]=std::to_string(cases);
        context->observed[parts[1]+".available_cases"]=std::to_string(available);
        context->observed[parts[1]+".bounds_pass"]=std::to_string(boundsPass);
        context->observed[parts[1]+".capacity_cases"]=std::to_string(capacityCases);
        context->observed[parts[1]+".capacity_height_pass"]=
            std::to_string(capacityHeightPass);
        context->observed[parts[1]+".capacity_remaining_pass"]=
            std::to_string(capacityRemainingPass);
        context->observed[parts[1]+".exact_design_pass"]=
            std::to_string(exactDesignPass);
        context->observed[parts[1]+".temporary_clamp_pass"]=std::to_string(temporaryClampPass);
        context->observed[parts[1]+".intentional_pass"]=std::to_string(intentionalPass);
        context->observed[parts[1]+".restore_pass"]=std::to_string(restorePass);
        context->observed[parts[1]+".screen_change_clamp_pass"]=
            std::to_string(screenChangeClampPass);
        context->observed[parts[1]+".intentional_anchor_restore_pass"]=
            std::to_string(intentionalAnchorRestorePass);
        context->observed[parts[1]+".violations"]=std::to_string(violations);
        return true;
    }
    if (parts[0] == "anchor-contract" && parts.size() == 2) {
        using xvatsim::modules::overlay::AccessoryAnchorOperation;
        using xvatsim::modules::overlay::AccessoryAnchorState;
        using xvatsim::modules::overlay::AccessoryAnchorUpdateInput;
        using xvatsim::modules::overlay::AccessoryRect;
        const auto& prefix = parts[1];
        int violations = 0;
        const auto updateAnchor = [context](
            AccessoryAnchorState* state,
            AccessoryAnchorOperation operation,
            bool drawerOpen,
            int left,
            int top,
            int screenWidth,
            int screenHeight,
            float scale) {
            AccessoryAnchorUpdateInput input;
            input.operation = operation;
            input.layout.screenWidth = screenWidth;
            input.layout.screenHeight = screenHeight;
            input.layout.windowLeft = left;
            input.layout.windowTop = top;
            input.layout.scale = scale;
            input.layout.cardAnimationProgress = 1.0f;
            input.layout.drawerOpen = drawerOpen;
            input.layout.typography = context->Typography(scale);
            return xvatsim::modules::overlay::UpdateAccessoryAnchorState(
                state, input);
        };
        const auto rectTranslated = [](const AccessoryRect& before,
                                       const AccessoryRect& after,
                                       int deltaX,
                                       int deltaY) {
            return after.left - before.left == deltaX &&
                after.right - before.right == deltaX &&
                after.top - before.top == deltaY &&
                after.bottom - before.bottom == deltaY;
        };
        const auto allAttached = [&](const auto& before, const auto& after) {
            const int deltaX = after.resolvedLeft - before.resolvedLeft;
            const int deltaY = after.resolvedTop - before.resolvedTop;
            bool attached =
                rectTranslated(before.mainCardBounds, after.mainCardBounds,
                    deltaX, deltaY) &&
                rectTranslated(before.railBounds, after.railBounds,
                    deltaX, deltaY) &&
                before.orbs.size() == after.orbs.size();
            if (before.drawerBounds.right > before.drawerBounds.left ||
                after.drawerBounds.right > after.drawerBounds.left) {
                attached = attached && rectTranslated(
                    before.drawerBounds, after.drawerBounds, deltaX, deltaY);
            }
            for (std::size_t index = 0;
                 attached && index < before.orbs.size(); ++index) {
                attached = rectTranslated(
                    before.orbs[index].bounds,
                    after.orbs[index].bounds,
                    deltaX,
                    deltaY);
                if (!attached) break;
                const auto& orb = after.orbs[index];
                const auto hit = xvatsim::modules::overlay::HitTestAccessoryOrb(
                    after,
                    (orb.bounds.left + orb.bounds.right) / 2,
                    (orb.bounds.top + orb.bounds.bottom) / 2);
                attached = hit.handled && hit.drawer == orb.drawer;
            }
            return attached;
        };

        int compactScalePass = 0;
        int sixLinePass = 0;
        int contentSeparationPass = 0;
        std::vector<std::string> compactPhysicalSizes;
        for (const float scale : {0.85f, 1.0f, 1.35f}) {
            AccessoryAnchorState state;
            const auto initialized = updateAnchor(
                &state, AccessoryAnchorOperation::Initialize,
                false, 300, 700, 1280, 720, scale);
            const auto opened = updateAnchor(
                &state, AccessoryAnchorOperation::OpenDrawer,
                true, initialized.layout.resolvedLeft,
                initialized.layout.resolvedTop, 1280, 720, scale);
            const bool compact =
                initialized.layout.closedDesignWidth == 430 &&
                initialized.layout.closedDesignHeight == 374 &&
                opened.layout.openDesignWidth == 430 &&
                opened.layout.openDesignHeight == 506 &&
                opened.layout.railDesignBounds.left == 118 &&
                opened.layout.railDesignBounds.top == 320 &&
                opened.layout.railDesignBounds.right == 306 &&
                opened.layout.railDesignBounds.bottom == 372 &&
                opened.layout.drawerDesignBounds.left == 0 &&
                opened.layout.drawerDesignBounds.top == 378 &&
                opened.layout.drawerDesignBounds.right == 430 &&
                opened.layout.drawerDesignBounds.bottom == 494 &&
                opened.layout.openBottomMarginDesignPixels == 12 &&
                opened.layout.railToDrawerGapDesignPixels == 6 &&
                initialized.layout.resolvedBounds.top -
                    initialized.layout.resolvedBounds.bottom ==
                    initialized.layout.closedHeight &&
                initialized.layout.drawerBounds.left ==
                    initialized.layout.drawerBounds.right;
            if (compact) compactScalePass++; else violations++;
            if (opened.layout.drawerVisibleLineCapacity == 6) {
                sixLinePass++;
            } else {
                violations++;
            }
            bool separated = opened.layout.railDesignBounds.top >= 320;
            for (const auto& orb : opened.layout.orbs) {
                separated = separated && orb.designBounds.top == 320 &&
                    orb.designBounds.bottom == 372 &&
                    orb.designCenter.y == 346;
            }
            if (separated) contentSeparationPass++; else violations++;
            compactPhysicalSizes.push_back(
                std::to_string(initialized.layout.closedHeight) + "/" +
                std::to_string(opened.layout.openHeight));
        }

        AccessoryAnchorState closedDragState;
        auto closedPrevious = updateAnchor(
            &closedDragState, AccessoryAnchorOperation::Initialize,
            false, 300, 900, 1920, 1080, 1.0f);
        int closedDragSamples = 0;
        int closedAttachmentPass = 0;
        for (const auto target : std::array<std::pair<int, int>, 3>{{
                 {360, 860}, {430, 810}, {520, 760}}}) {
            const auto moved = updateAnchor(
                &closedDragState, AccessoryAnchorOperation::IntentionalMove,
                false, target.first, target.second, 1920, 1080, 1.0f);
            closedDragSamples++;
            const bool pass = allAttached(closedPrevious.layout, moved.layout) &&
                moved.positionSettingsDirty &&
                closedDragState.currentAnchorLeft == moved.layout.resolvedLeft &&
                closedDragState.currentAnchorTop == moved.layout.resolvedTop &&
                closedDragState.savedClosedAnchorLeft == moved.layout.resolvedLeft &&
                closedDragState.savedClosedAnchorTop == moved.layout.resolvedTop;
            if (pass) closedAttachmentPass++; else violations++;
            closedPrevious = moved;
        }
        const auto finalClosedLeft = closedPrevious.layout.resolvedLeft;
        const auto finalClosedTop = closedPrevious.layout.resolvedTop;
        const auto ordinaryClosed = updateAnchor(
            &closedDragState, AccessoryAnchorOperation::OrdinaryRefresh,
            false, finalClosedLeft, finalClosedTop, 1920, 1080, 1.0f);
        const bool closedReleaseRetained =
            ordinaryClosed.layout.resolvedLeft == finalClosedLeft &&
            ordinaryClosed.layout.resolvedTop == finalClosedTop &&
            !ordinaryClosed.positionSettingsDirty &&
            !ordinaryClosed.layout.closedAnchorRestored;
        if (!closedReleaseRetained) violations++;
        const auto disableRefresh = updateAnchor(
            &closedDragState, AccessoryAnchorOperation::OrdinaryRefresh,
            false, finalClosedLeft, finalClosedTop, 1920, 1080, 1.0f);
        const auto enableRefresh = updateAnchor(
            &closedDragState, AccessoryAnchorOperation::OrdinaryRefresh,
            false, finalClosedLeft, finalClosedTop, 1920, 1080, 1.0f);
        const bool disableEnableRetained =
            disableRefresh.layout.resolvedLeft == finalClosedLeft &&
            disableRefresh.layout.resolvedTop == finalClosedTop &&
            enableRefresh.layout.resolvedLeft == finalClosedLeft &&
            enableRefresh.layout.resolvedTop == finalClosedTop &&
            !disableRefresh.positionSettingsDirty &&
            !enableRefresh.positionSettingsDirty;
        if (!disableEnableRetained) violations++;

        auto openPrevious = updateAnchor(
            &closedDragState, AccessoryAnchorOperation::OpenDrawer,
            true, finalClosedLeft, finalClosedTop, 1920, 1080, 1.0f);
        int openDragSamples = 0;
        int openAttachmentPass = 0;
        for (const auto target : std::array<std::pair<int, int>, 3>{{
                 {570, 800}, {640, 850}, {710, 900}}}) {
            const auto moved = updateAnchor(
                &closedDragState, AccessoryAnchorOperation::IntentionalMove,
                true, target.first, target.second, 1920, 1080, 1.0f);
            openDragSamples++;
            const bool pass = allAttached(openPrevious.layout, moved.layout) &&
                moved.positionSettingsDirty &&
                closedDragState.intentionalMoveSinceOpen &&
                closedDragState.savedClosedAnchorLeft == moved.layout.resolvedLeft &&
                closedDragState.savedClosedAnchorTop == moved.layout.resolvedTop;
            if (pass) openAttachmentPass++; else violations++;
            openPrevious = moved;
        }
        const auto closeAfterOpenDrag = updateAnchor(
            &closedDragState, AccessoryAnchorOperation::CloseDrawer,
            false, openPrevious.layout.resolvedLeft,
            openPrevious.layout.resolvedTop, 1920, 1080, 1.0f);
        const bool openDragReplacedAnchor =
            closeAfterOpenDrag.explicitClosedAnchorRestored &&
            closeAfterOpenDrag.layout.resolvedLeft ==
                openPrevious.layout.resolvedLeft &&
            closeAfterOpenDrag.layout.resolvedTop ==
                openPrevious.layout.resolvedTop &&
            !closeAfterOpenDrag.positionSettingsDirty;
        if (!openDragReplacedAnchor) violations++;

        AccessoryAnchorState temporaryClampState;
        const auto preOpen = updateAnchor(
            &temporaryClampState, AccessoryAnchorOperation::Initialize,
            false, 400, 520, 1280, 720, 1.35f);
        const auto clampedOpen = updateAnchor(
            &temporaryClampState, AccessoryAnchorOperation::OpenDrawer,
            true, preOpen.layout.resolvedLeft, preOpen.layout.resolvedTop,
            1280, 720, 1.35f);
        const auto clampedOpenRefresh = updateAnchor(
            &temporaryClampState, AccessoryAnchorOperation::OrdinaryRefresh,
            true, clampedOpen.layout.resolvedLeft,
            clampedOpen.layout.resolvedTop, 1280, 720, 1.35f);
        const bool temporaryClampSurvivedRefresh =
            temporaryClampState.temporaryOpenClampApplied &&
            clampedOpenRefresh.layout.resolvedLeft ==
                clampedOpen.layout.resolvedLeft &&
            clampedOpenRefresh.layout.resolvedTop ==
                clampedOpen.layout.resolvedTop;
        const auto restoredClose = updateAnchor(
            &temporaryClampState, AccessoryAnchorOperation::CloseDrawer,
            false, clampedOpenRefresh.layout.resolvedLeft,
            clampedOpenRefresh.layout.resolvedTop, 1280, 720, 1.35f);
        const bool temporaryClampRestored =
            clampedOpen.savedClosedAnchorCaptured &&
            clampedOpen.temporaryOpenClampApplied &&
            temporaryClampSurvivedRefresh &&
            preOpen.layout.resolvedTop == 520 &&
            clampedOpen.layout.resolvedTop == 683 &&
            restoredClose.explicitClosedAnchorRestored &&
            restoredClose.layout.resolvedLeft == preOpen.layout.resolvedLeft &&
            restoredClose.layout.resolvedTop == preOpen.layout.resolvedTop &&
            !restoredClose.positionSettingsDirty;
        if (!temporaryClampRestored) violations++;

        AccessoryAnchorState screenClampState;
        updateAnchor(&screenClampState, AccessoryAnchorOperation::Initialize,
            false, 1400, 900, 1920, 1080, 1.0f);
        const auto technicalClamp = updateAnchor(
            &screenClampState, AccessoryAnchorOperation::ScreenBoundsChanged,
            false, 1400, 900, 1280, 720, 1.0f);
        const auto afterTechnicalRefresh = updateAnchor(
            &screenClampState, AccessoryAnchorOperation::OrdinaryRefresh,
            false, technicalClamp.layout.resolvedLeft,
            technicalClamp.layout.resolvedTop, 1280, 720, 1.0f);
        const bool technicalClampPass =
            technicalClamp.layout.temporaryClampApplied &&
            !technicalClamp.positionSettingsDirty &&
            screenClampState.savedClosedAnchorLeft ==
                technicalClamp.layout.resolvedLeft &&
            screenClampState.savedClosedAnchorTop ==
                technicalClamp.layout.resolvedTop &&
            afterTechnicalRefresh.layout.resolvedLeft ==
                technicalClamp.layout.resolvedLeft &&
            afterTechnicalRefresh.layout.resolvedTop ==
                technicalClamp.layout.resolvedTop &&
            !afterTechnicalRefresh.layout.closedAnchorRestored;
        if (!technicalClampPass) violations++;

        xvatsim::brain::BrainOwnedRuntimeState brainState;
        xvatsim::brain::BrainOwnedAccessoryHistoryEntryInput historyInput;
        historyInput.drawer = xvatsim::brain::BrainOwnedAccessoryDrawerId::Metar;
        historyInput.stableKey = "TRANSLATION";
        historyInput.title = "Translation proof";
        historyInput.body = "Texture-local content remains unchanged.";
        const auto accepted =
            xvatsim::brain::AcceptBrainOwnedAccessoryHistoryEntry(
                &brainState, historyInput);
        xvatsim::brain::BrainOwnedAccessorySelectionRequest selection;
        selection.drawer = xvatsim::brain::BrainOwnedAccessoryDrawerId::Metar;
        selection.requestSequence = 1;
        const auto selected =
            xvatsim::brain::RequestBrainOwnedAccessoryDrawerSelection(
                &brainState, selection);
        AccessoryAnchorState translationState;
        const auto translationClosed = updateAnchor(
            &translationState, AccessoryAnchorOperation::Initialize,
            false, 300, 900, 1920, 1080, 1.0f);
        const auto translationOpen = updateAnchor(
            &translationState, AccessoryAnchorOperation::OpenDrawer,
            true, translationClosed.layout.resolvedLeft,
            translationClosed.layout.resolvedTop, 1920, 1080, 1.0f);
        const auto presentation =
            xvatsim::brain::ProjectBrainOwnedAccessoryPresentation(
                &brainState, 1, nullptr);
        xvatsim::modules::overlay::AccessoryPresentationState presenter;
        xvatsim::modules::overlay::AccessoryPresentationUpdateInput warmInput;
        warmInput.presentation = presentation;
        warmInput.layout = translationOpen.layout;
        warmInput.preparedPlan = Step3PreparePresentationPlan(
            context, presentation, translationOpen.layout);
        warmInput.mainCardProductionSignature = "translation-main";
        warmInput.measurementContext = context->measurementContext;
        const auto warm = xvatsim::modules::overlay::UpdateAccessoryPresentation(
            &presenter, warmInput);
        const auto railSignature = presenter.railRenderSignature;
        const auto drawerSignature = presenter.drawerRenderSignature;
        const auto snapshotIdentity = presenter.activeSnapshotIdentity;
        const auto gdiBefore =
            xvatsim::modules::overlay::GetAccessoryGdiMeasurementCounters(
                context->measurementContext);
        const auto translated = updateAnchor(
            &translationState, AccessoryAnchorOperation::IntentionalMove,
            true, 480, 780, 1920, 1080, 1.0f);
        auto translatedInput = warmInput;
        translatedInput.layout = translated.layout;
        const auto translatedUpdate =
            xvatsim::modules::overlay::UpdateAccessoryPresentation(
                &presenter, translatedInput);
        const auto gdiAfter =
            xvatsim::modules::overlay::GetAccessoryGdiMeasurementCounters(
                context->measurementContext);
        const auto gdiDelta = Step3GdiCounterDelta(gdiBefore, gdiAfter);
        const auto& delta = translatedUpdate.delta;
        const bool translationZeroWork = accepted.accepted &&
            selected.action ==
                xvatsim::brain::BrainOwnedAccessoryDrawerAction::Opened &&
            warm.delta.railRasterRequests == 1 &&
            warm.delta.drawerRasterRequests == 1 &&
            allAttached(translationOpen.layout, translated.layout) &&
            delta.historyVisits == 0 && delta.entryCopies == 0 &&
            delta.wrapVisits == 0 && delta.mainCardRasterRequests == 0 &&
            delta.railRasterRequests == 0 &&
            delta.drawerRasterRequests == 0 && delta.uploadRequests == 0 &&
            gdiDelta.measurementCalls == 0 &&
            gdiDelta.bitmapConstructions == 0 &&
            gdiDelta.graphicsConstructions == 0 &&
            gdiDelta.fontConstructions == 0 &&
            presenter.railRenderSignature == railSignature &&
            presenter.drawerRenderSignature == drawerSignature &&
            presenter.activeSnapshotIdentity == snapshotIdentity &&
            presenter.layoutGeneration == 1;
        if (!translationZeroWork) violations++;

        context->observed[prefix + ".compact_scale_pass"] =
            std::to_string(compactScalePass);
        context->observed[prefix + ".compact_physical_sizes"] =
            JoinCsv(compactPhysicalSizes);
        context->observed[prefix + ".six_line_pass"] =
            std::to_string(sixLinePass);
        context->observed[prefix + ".content_separation_pass"] =
            std::to_string(contentSeparationPass);
        context->observed[prefix + ".closed_drag_samples"] =
            std::to_string(closedDragSamples);
        context->observed[prefix + ".closed_attachment_pass"] =
            std::to_string(closedAttachmentPass);
        context->observed[prefix + ".closed_release_retained"] =
            Step3Bool(closedReleaseRetained);
        context->observed[prefix + ".ordinary_refresh_retained"] =
            Step3Bool(closedReleaseRetained);
        context->observed[prefix + ".disable_enable_retained"] =
            Step3Bool(disableEnableRetained);
        context->observed[prefix + ".open_drag_samples"] =
            std::to_string(openDragSamples);
        context->observed[prefix + ".open_attachment_pass"] =
            std::to_string(openAttachmentPass);
        context->observed[prefix + ".open_drag_replaced_anchor"] =
            Step3Bool(openDragReplacedAnchor);
        context->observed[prefix + ".temporary_clamp_restored"] =
            Step3Bool(temporaryClampRestored);
        context->observed[prefix + ".technical_clamp_pass"] =
            Step3Bool(technicalClampPass);
        context->observed[prefix + ".translation_zero_work"] =
            Step3Bool(translationZeroWork);
        context->observed[prefix + ".translation_history_visits"] =
            std::to_string(delta.historyVisits);
        context->observed[prefix + ".translation_copies"] =
            std::to_string(delta.entryCopies);
        context->observed[prefix + ".translation_wraps"] =
            std::to_string(delta.wrapVisits);
        context->observed[prefix + ".translation_rail"] =
            std::to_string(delta.railRasterRequests);
        context->observed[prefix + ".translation_drawer"] =
            std::to_string(delta.drawerRasterRequests);
        context->observed[prefix + ".translation_uploads"] =
            std::to_string(delta.uploadRequests);
        context->observed[prefix + ".translation_gdi"] =
            std::to_string(gdiDelta.measurementCalls);
        context->observed[prefix + ".violations"] =
            std::to_string(violations);
        return true;
    }
    if (parts[0] == "hit-matrix" && parts.size() == 2) {
        const float scales[]{0.85f,1.0f,1.35f};
        int layouts=0,visibleOrbs=0,centerHits=0,edgeHits=0,outsideMisses=0;
        int gapMisses=0,drawerIdentityMatches=0,labelFits=0,openFits=0;
        int designDiameterPass=0,physicalDiameterPass=0,coordinatePass=0;
        int labelMeasurementPass=0,indicatorMeasurementPass=0;
        std::vector<std::string> physicalDiameters;
        std::vector<std::string> labelMeasurements;
        std::vector<std::string> indicatorMeasurements;
        for(float scale:scales){
            xvatsim::modules::overlay::AccessoryLayoutInput input;
            input.scale=scale; input.cardAnimationProgress=1.0f;
            const auto layout=ResolveStep3Layout(context,input);
            if(layout.status==xvatsim::brain::BrainOwnedAccessoryOperationStatus::Available) layouts++;
            if(layout.orbDiameterDesignPixels==52) designDiameterPass++;
            const int expectedPhysical=static_cast<int>(std::lround(52.0f*scale));
            physicalDiameters.push_back(layout.orbs.empty()
                ? "0"
                : std::to_string(layout.orbs.front().diameterPhysicalPixels));
            bool layoutPhysicalPass=layout.orbs.size()==3;
            bool layoutCoordinatePass=layout.orbs.size()==3 &&
                layout.railDesignBounds.top==320 && layout.railDesignBounds.bottom==372 &&
                layout.drawerDesignBounds.top==378 && layout.drawerDesignBounds.bottom==494 &&
                layout.interOrbGapDesignPixels==16;
            for(const auto& orb:layout.orbs){
                if(orb.visible) visibleOrbs++;
                if(orb.labelFits) labelFits++;
                if(orb.openIndicatorFits) openFits++;
                const bool labelMeasured=
                    orb.labelMeasuredWidth>0 && orb.labelMeasuredHeight>0 &&
                    orb.labelMeasuredWidth<=orb.labelAvailableWidth &&
                    orb.labelMeasuredHeight<=orb.labelAvailableHeight;
                const bool indicatorMeasured=
                    orb.openIndicatorMeasuredWidth>0 &&
                    orb.openIndicatorMeasuredHeight>0 &&
                    orb.openIndicatorMeasuredWidth<=orb.openIndicatorAvailableWidth &&
                    orb.openIndicatorMeasuredHeight<=orb.openIndicatorAvailableHeight;
                if(labelMeasured) labelMeasurementPass++;
                if(indicatorMeasured) indicatorMeasurementPass++;
                std::ostringstream labelRecord;
                labelRecord << scale << ':' << Step3Drawer(orb.drawer) << '='
                    << orb.labelMeasuredWidth << 'x' << orb.labelMeasuredHeight
                    << '/' << orb.labelAvailableWidth << 'x'
                    << orb.labelAvailableHeight;
                labelMeasurements.push_back(labelRecord.str());
                std::ostringstream indicatorRecord;
                indicatorRecord << scale << ':' << Step3Drawer(orb.drawer) << '='
                    << orb.openIndicatorMeasuredWidth << 'x'
                    << orb.openIndicatorMeasuredHeight << '/'
                    << orb.openIndicatorAvailableWidth << 'x'
                    << orb.openIndicatorAvailableHeight;
                indicatorMeasurements.push_back(indicatorRecord.str());
                const int x=(orb.bounds.left+orb.bounds.right)/2;
                const int y=(orb.bounds.top+orb.bounds.bottom)/2;
                const int radius=orb.diameterPhysicalPixels/2;
                const auto center=xvatsim::modules::overlay::HitTestAccessoryOrb(layout,x,y);
                const auto edge=xvatsim::modules::overlay::HitTestAccessoryOrb(layout,x+radius-1,y);
                const auto outside=xvatsim::modules::overlay::HitTestAccessoryOrb(layout,x+radius+1,y);
                if(center.handled) centerHits++;
                if(edge.handled) edgeHits++;
                if(!outside.handled) outsideMisses++;
                if(center.handled && center.drawer==orb.drawer) drawerIdentityMatches++;
                layoutPhysicalPass=layoutPhysicalPass &&
                    orb.diameterPhysicalPixels==expectedPhysical;
            }
            if(layout.orbs.size()==3) {
                const int expectedLeft[]{118,186,254};
                const int expectedRight[]{170,238,306};
                const int expectedCenterX[]{144,212,280};
                for(std::size_t index=0;index<3;++index) {
                    const auto& orb=layout.orbs[index];
                    layoutCoordinatePass=layoutCoordinatePass &&
                        orb.designBounds.left==expectedLeft[index] &&
                        orb.designBounds.right==expectedRight[index] &&
                        orb.designBounds.top==320 && orb.designBounds.bottom==372 &&
                        orb.designCenter.x==expectedCenterX[index] &&
                        orb.designCenter.y==346;
                }
            }
            if(layoutPhysicalPass) physicalDiameterPass++;
            if(layoutCoordinatePass) coordinatePass++;
            for(std::size_t index=1;index<layout.orbs.size();++index){
                const int gapX=(layout.orbs[index-1].bounds.right+layout.orbs[index].bounds.left)/2;
                const int gapY=(layout.orbs[index].bounds.top+layout.orbs[index].bounds.bottom)/2;
                if(!xvatsim::modules::overlay::HitTestAccessoryOrb(layout,gapX,gapY).handled) gapMisses++;
            }
        }
        context->observed[parts[1]+".layouts"]=std::to_string(layouts);
        context->observed[parts[1]+".visible_orbs"]=std::to_string(visibleOrbs);
        context->observed[parts[1]+".center_hits"]=std::to_string(centerHits);
        context->observed[parts[1]+".edge_hits"]=std::to_string(edgeHits);
        context->observed[parts[1]+".outside_misses"]=std::to_string(outsideMisses);
        context->observed[parts[1]+".gap_misses"]=std::to_string(gapMisses);
        context->observed[parts[1]+".identity_matches"]=std::to_string(drawerIdentityMatches);
        context->observed[parts[1]+".design_diameter_pass"]=
            std::to_string(designDiameterPass);
        context->observed[parts[1]+".physical_diameter_pass"]=
            std::to_string(physicalDiameterPass);
        context->observed[parts[1]+".physical_diameters"]=JoinCsv(physicalDiameters);
        context->observed[parts[1]+".coordinate_pass"]=
            std::to_string(coordinatePass);
        context->observed[parts[1]+".label_fits"]=std::to_string(labelFits);
        context->observed[parts[1]+".open_fits"]=std::to_string(openFits);
        context->observed[parts[1]+".label_measurement_pass"]=
            std::to_string(labelMeasurementPass);
        context->observed[parts[1]+".indicator_measurement_pass"]=
            std::to_string(indicatorMeasurementPass);
        context->observed[parts[1]+".label_measurement_record"]=
            JoinCsv(labelMeasurements);
        context->observed[parts[1]+".indicator_measurement_record"]=
            JoinCsv(indicatorMeasurements);
        return true;
    }
    if (parts[0] == "hit" && parts.size() == 5) {
        int x=0,y=0; if(!parseInt(parts[2],&x)||!parseInt(parts[3],&y)) return fail("invalid hit point");
        auto found=context->layouts.find(parts[1]); if(found==context->layouts.end()) return fail("layout missing");
        auto result=xvatsim::modules::overlay::HitTestAccessoryOrb(found->second,x,y);
        context->hits[parts[4]]=result;
        context->observed[parts[4]+".status"]=Step3Status(result.status);
        context->observed[parts[4]+".handled"]=Step3Bool(result.handled);
        context->observed[parts[4]+".drawer"]=Step3Drawer(result.drawer);
        return true;
    }
    if (parts[0] == "hit-orb" && parts.size() == 5) {
        const auto layout=context->layouts.find(parts[1]);
        if(layout==context->layouts.end()) return fail("layout missing");
        const auto drawer=Step3DrawerFromToken(parts[2]);
        const auto orb=std::find_if(
            layout->second.orbs.begin(),layout->second.orbs.end(),
            [&](const auto& candidate){return candidate.drawer==drawer;});
        if(orb==layout->second.orbs.end()) {
            context->hits[parts[4]]={};
            context->observed[parts[4]+".status"]="unavailable";
            context->observed[parts[4]+".handled"]="false";
            context->observed[parts[4]+".drawer"]="NONE";
            return true;
        }
        const int centerX=(orb->bounds.left+orb->bounds.right)/2;
        const int centerY=(orb->bounds.top+orb->bounds.bottom)/2;
        const int radius=orb->diameterPhysicalPixels/2;
        int x=centerX;
        if(parts[3]=="edge") x=centerX+radius-1;
        else if(parts[3]=="outside") x=centerX+radius+1;
        const auto result=xvatsim::modules::overlay::HitTestAccessoryOrb(
            layout->second,x,centerY);
        context->hits[parts[4]]=result;
        context->observed[parts[4]+".status"]=Step3Status(result.status);
        context->observed[parts[4]+".handled"]=Step3Bool(result.handled);
        context->observed[parts[4]+".drawer"]=Step3Drawer(result.drawer);
        return true;
    }
    if (parts[0] == "wrap" && parts.size() == 3) {
        xvatsim::modules::overlay::AccessoryTextLayoutInput input;
        if(parts[1]=="normal") input.text="METAR data is not enabled in Step 3.";
        else if(parts[1]=="words") input.text=
            "Measured wrapping keeps complete words together whenever the next word "
            "would exceed the available drawer width.";
        else if(parts[1]=="whitespace") input.text="A  B\r\nC\tD";
        else if(parts[1]=="token") input.text=std::string(256,'A');
        else if(parts[1]=="long") input.text=std::string(4096,'L');
        else input.text=std::string(8189,'Z') + "\xE2\x82\xAC" + std::string(1000,'Q');
        input.contentWidth=340; input.maxRetainedBytes=8192;
        auto result=xvatsim::modules::overlay::BuildAccessoryTextLayout(
            context->measurementContext,input);
        context->observed[parts[2]+".status"]=Step3Status(result.status);
        context->observed[parts[2]+".fits"]=Step3Bool(result.allLinesFit);
        context->observed[parts[2]+".measurement_available"]=
            Step3Bool(result.measurementAvailable);
        context->observed[parts[2]+".word_wrapped"]=
            Step3Bool(result.usedWordWrapping);
        context->observed[parts[2]+".character_fallback"]=
            Step3Bool(result.usedCharacterFallback);
        context->observed[parts[2]+".maximum_measured_width"]=
            std::to_string(result.maximumMeasuredLineWidth);
        context->observed[parts[2]+".measured_within_width"]=Step3Bool(
            result.maximumMeasuredLineWidth<=input.contentWidth);
        context->observed[parts[2]+".limited"]=Step3Bool(result.contentLimited);
        context->observed[parts[2]+".valid_utf8"]=Step3Bool(result.validUtf8);
        context->observed[parts[2]+".valid_boundary"]=
            Step3Bool(result.endedAtValidUtf8Boundary &&
                      Step3IsValidUtf8(result.reconstructedText));
        context->observed[parts[2]+".marker_visible"]=
            Step3Bool(result.contentLimitedMarkerVisible &&
                      result.reconstructedText.find("CONTENT LIMITED") != std::string::npos);
        context->observed[parts[2]+".retained_bytes"]=
            std::to_string(result.retainedBytes);
        context->observed[parts[2]+".reconstructed_exact"]=
            Step3Bool(result.reconstructedText == input.text);
        const std::string permittedNormalized = parts[1]=="whitespace"
            ? std::string("A  B\nC\tD")
            : input.text;
        context->observed[parts[2]+".normalization_contract"]=
            Step3Bool(result.reconstructedText == permittedNormalized);
        context->observed[parts[2]+".reconstructed_matches_lines"]=
            Step3Bool(
                Step3RemoveLineFeeds(result.reconstructedText)==
                Step3ConcatenateLines(result.lines));
        context->observed[parts[2]+".line_count"]=std::to_string(result.lines.size());
        context->observed[parts[2]+".duration_us"]=
            std::to_string(result.elapsedMicroseconds);
        context->observed[parts[2]+".within_16_7ms"]=
            Step3Bool(result.elapsedMicroseconds<=16700);
        context->observed[parts[2]+".timing_record"]=
            std::to_string(result.elapsedMicroseconds)+"us";
        context->observed[parts[2]+".final_marker"]=result.finalMarker;
        return true;
    }
    if (parts[0] == "wrap-measurement-matrix" && parts.size() == 2) {
        const float scales[]{0.85f,1.0f,1.35f};
        const std::string words=
            "Measured wrapping keeps complete words together whenever the next word "
            "would exceed the available drawer width.";
        const std::string token(256,'A');
        int availableCases=0,wordFitCases=0,wordWrapCases=0;
        int tokenFitCases=0,tokenFallbackCases=0,violations=0;
        std::vector<std::string> records;
        for(const auto scale:scales) {
            const int contentWidth=static_cast<int>(std::lround(340.0f*scale));
            xvatsim::modules::overlay::AccessoryTextLayoutInput wordInput;
            wordInput.text=words;
            wordInput.contentWidth=contentWidth;
            wordInput.maxRetainedBytes=8192;
            wordInput.scale=scale;
            wordInput.fontRole=
                xvatsim::modules::overlay::AccessoryFontRole::DrawerBody;
            const auto wordResult=
                xvatsim::modules::overlay::BuildAccessoryTextLayout(
                    context->measurementContext,wordInput);
            auto tokenInput=wordInput;
            tokenInput.text=token;
            const auto tokenResult=
                xvatsim::modules::overlay::BuildAccessoryTextLayout(
                    context->measurementContext,tokenInput);
            const bool available=
                wordResult.measurementAvailable && tokenResult.measurementAvailable;
            const bool wordFit=wordResult.allLinesFit &&
                wordResult.maximumMeasuredLineWidth<=contentWidth;
            const bool tokenFit=tokenResult.allLinesFit &&
                tokenResult.maximumMeasuredLineWidth<=contentWidth;
            if(available) availableCases++; else violations++;
            if(wordFit) wordFitCases++; else violations++;
            if(wordResult.usedWordWrapping) wordWrapCases++; else violations++;
            if(tokenFit) tokenFitCases++; else violations++;
            if(tokenResult.usedCharacterFallback) tokenFallbackCases++;
            else violations++;
            std::ostringstream record;
            record << scale << ":words="
                   << wordResult.maximumMeasuredLineWidth << '/' << contentWidth
                   << ":token=" << tokenResult.maximumMeasuredLineWidth
                   << '/' << contentWidth;
            records.push_back(record.str());
        }
        const auto& prefix=parts[1];
        context->observed[prefix+".cases"]="3";
        context->observed[prefix+".available_cases"]=
            std::to_string(availableCases);
        context->observed[prefix+".word_fit_cases"]=
            std::to_string(wordFitCases);
        context->observed[prefix+".word_wrap_cases"]=
            std::to_string(wordWrapCases);
        context->observed[prefix+".token_fit_cases"]=
            std::to_string(tokenFitCases);
        context->observed[prefix+".token_fallback_cases"]=
            std::to_string(tokenFallbackCases);
        context->observed[prefix+".violations"]=std::to_string(violations);
        context->observed[prefix+".measurement_record"]=JoinCsv(records);
        return true;
    }
    if (parts[0] == "wrap-performance-matrix" && parts.size() == 2) {
        const auto fillExact=[](
            const std::string& pattern,
            std::size_t bytes,
            const std::string& tail=std::string{}) {
            std::string value;
            value.reserve(bytes);
            const auto bodyBytes=bytes-tail.size();
            while(value.size()+pattern.size()<=bodyBytes) value+=pattern;
            while(value.size()<bodyBytes) value.push_back('X');
            value+=tail;
            return value;
        };
        std::vector<std::string> inputs{
            fillExact("W",8192),
            fillExact("M",8192),
            fillExact("Q",8192),
            fillExact("i",8192),
            fillExact("l",8192),
            fillExact(".",8192),
            fillExact("1",8192),
            fillExact("WiM1",8192),
            fillExact("Ab9Z",8192),
            fillExact(".,;!",8192),
            fillExact("Wili",8192),
            fillExact("Q1iM",8192),
            fillExact("A",8192,"X"),
            fillExact("A",8192,"Y"),
            fillExact("B",8192,"Z"),
            fillExact("B",8192,"Q"),
            fillExact("\xC3\xA9",8192),
            fillExact("\xE2\x82\xAC",8192,"XX"),
            fillExact("\xF0\x9F\x9B\xA9",8192),
            fillExact("W\xC3\xA9\xE2\x82\xAC",8192,"Z")};
        int validInputs=0,fitPass=0,reconstructionPass=0;
        int fallbackPass=0,withinPass=0;
        std::vector<std::uint64_t> durations;
        std::vector<std::string> durationRecords;
        for(std::size_t index=0;index<inputs.size();++index) {
            xvatsim::modules::overlay::AccessoryTextLayoutInput input;
            input.text=inputs[index];
            input.contentWidth=340;
            input.maxRetainedBytes=8192;
            input.scale=1.0f;
            input.fontRole=
                xvatsim::modules::overlay::AccessoryFontRole::DrawerBody;
            const auto result=
                xvatsim::modules::overlay::BuildAccessoryTextLayout(
                    context->measurementContext,input);
            if(Step3IsValidUtf8(input.text) && input.text.size()==8192) {
                validInputs++;
            }
            if(result.allLinesFit && result.maximumMeasuredLineWidth<=340) {
                fitPass++;
            }
            if(result.reconstructedText==input.text &&
                Step3ConcatenateLines(result.lines)==input.text) {
                reconstructionPass++;
            }
            if(result.usedCharacterFallback) fallbackPass++;
            if(result.elapsedMicroseconds<=16700) withinPass++;
            durations.push_back(result.elapsedMicroseconds);
            durationRecords.push_back(
                std::to_string(index+1)+"="+
                std::to_string(result.elapsedMicroseconds)+"us");
        }
        auto sortedDurations=durations;
        std::sort(sortedDurations.begin(),sortedDurations.end());
        const auto percentile=[&](double fraction) {
            const auto rank=static_cast<std::size_t>(
                std::ceil(fraction*static_cast<double>(sortedDurations.size())));
            return sortedDurations[std::max<std::size_t>(1,rank)-1];
        };
        const auto& prefix=parts[1];
        context->observed[prefix+".count"]=std::to_string(inputs.size());
        context->observed[prefix+".distinct_count"]=std::to_string(
            std::unordered_set<std::string>(inputs.begin(),inputs.end()).size());
        context->observed[prefix+".valid_inputs"]=std::to_string(validInputs);
        context->observed[prefix+".fit_pass"]=std::to_string(fitPass);
        context->observed[prefix+".reconstruction_pass"]=
            std::to_string(reconstructionPass);
        context->observed[prefix+".fallback_pass"]=
            std::to_string(fallbackPass);
        context->observed[prefix+".within_pass"]=std::to_string(withinPass);
        context->observed[prefix+".p50_us"]=std::to_string(percentile(0.50));
        context->observed[prefix+".p95_us"]=std::to_string(percentile(0.95));
        context->observed[prefix+".maximum_us"]=
            std::to_string(sortedDurations.back());
        context->observed[prefix+".timing_record"]=
            JoinCsv(durationRecords)+",p50="+
            context->observed[prefix+".p50_us"]+"us,p95="+
            context->observed[prefix+".p95_us"]+"us,max="+
            context->observed[prefix+".maximum_us"]+"us";
        return true;
    }
    if (parts[0] == "history-layout" && parts.size() == 4) {
        const auto found=context->snapshots.find(parts[1]);
        const auto layout=context->layouts.find(parts[2]);
        if(found==context->snapshots.end() || found->second.snapshot==nullptr) {
            context->observed[parts[3]+".status"]="unavailable";
            context->observed[parts[3]+".drawer"]="NONE";
            context->observed[parts[3]+".keys"]="";
            context->observed[parts[3]+".titles"]="";
            context->observed[parts[3]+".bodies"]="";
            context->observed[parts[3]+".sequences"]="";
            context->observed[parts[3]+".maximum_offset"]="0";
            context->observed[parts[3]+".final_marker"]="";
            context->observed[parts[3]+".marker_owned"]="false";
            return true;
        }
        if(layout==context->layouts.end()) return fail("history layout geometry missing");
        const auto result=xvatsim::modules::overlay::BuildAccessoryHistoryLayout(
            context->measurementContext,*found->second.snapshot,layout->second);
        context->observed[parts[3]+".status"]=Step3Status(result.status);
        context->observed[parts[3]+".drawer"]=Step3Drawer(result.drawer);
        context->observed[parts[3]+".keys"]=JoinCsv(result.renderedEntryKeys);
        context->observed[parts[3]+".titles"]=JoinCsv(result.renderedEntryTitles);
        context->observed[parts[3]+".bodies"]=JoinCsv(result.renderedEntryBodies);
        context->observed[parts[3]+".pairing_intact"]=Step3Bool(
            result.renderedEntryKeys.size()==result.renderedEntryTitles.size() &&
            result.renderedEntryKeys.size()==result.renderedEntryBodies.size() &&
            result.renderedEntryKeys.size()==result.renderedTitleLines.size() &&
            result.renderedEntryKeys.size()==result.renderedBodyLines.size());
        bool titleMarkerVisible=false;
        std::size_t measuredTitleLines=0;
        std::size_t measuredBodyLines=0;
        for(const auto& lines:result.renderedTitleLines) {
            measuredTitleLines+=lines.size();
            titleMarkerVisible=titleMarkerVisible ||
                Step3ConcatenateLines(lines).find("CONTENT LIMITED")!=
                    std::string::npos;
        }
        for(const auto& lines:result.renderedBodyLines) {
            measuredBodyLines+=lines.size();
        }
        context->observed[parts[3]+".title_marker_visible"]=
            Step3Bool(titleMarkerVisible);
        context->observed[parts[3]+".title_line_count"]=
            std::to_string(measuredTitleLines);
        context->observed[parts[3]+".body_line_count"]=
            std::to_string(measuredBodyLines);
        std::vector<std::string> sequences;
        for(const auto value:result.renderedAcceptedSequences) {
            sequences.push_back(std::to_string(value));
        }
        context->observed[parts[3]+".sequences"]=JoinCsv(sequences);
        context->observed[parts[3]+".maximum_offset"]=std::to_string(result.maximumOffset);
        context->observed[parts[3]+".visible_capacity"]=
            std::to_string(result.visibleLineCapacity);
        context->observed[parts[3]+".total_lines"]=
            std::to_string(result.totalScrollableLineCount);
        context->observed[parts[3]+".capacity_matches_layout"]=Step3Bool(
            result.visibleLineCapacity==layout->second.drawerVisibleLineCapacity);
        context->observed[parts[3]+".all_wrapped_lines_fit"]=
            Step3Bool(result.allWrappedLinesFit);
        context->observed[parts[3]+".final_marker"]=result.finalMarker;
        context->observed[parts[3]+".marker_owned"]=
            Step3Bool(result.finalMarkerBelongsToSelectedHistory);
        return true;
    }
    if (parts[0] == "wheel-point" && parts.size() == 7) {
        int clicks=0,offset=0,maximum=0;
        try { clicks=std::stoi(parts[3]); } catch (...) { return fail("invalid wheel clicks"); }
        if(!parseInt(parts[4],&offset)||!parseInt(parts[5],&maximum)) {
            return fail("invalid wheel input");
        }
        const auto layout=context->layouts.find(parts[1]);
        if(layout==context->layouts.end()) return fail("wheel layout missing");
        xvatsim::modules::overlay::AccessoryWheelInput input;
        input.layout=layout->second;
        const auto center=[](const auto& rect){
            return std::pair<int,int>{(rect.left+rect.right)/2,(rect.top+rect.bottom)/2};
        };
        std::pair<int,int> point;
        if(parts[2]=="drawer") point=center(layout->second.drawerBounds);
        else if(parts[2]=="main") point=center(layout->second.mainCardBounds);
        else point={layout->second.resolvedBounds.right+1,layout->second.resolvedBounds.top+1};
        input.pointerX=point.first;
        input.pointerY=point.second;
        input.wheelClicks=clicks;
        input.drawerOffset=offset;
        input.drawerMaximumOffset=maximum;
        input.mainCardOffset=offset;
        input.mainCardMaximumOffset=maximum;
        auto result=xvatsim::modules::overlay::ApplyAccessoryWheel(input);
        context->observed[parts[6]+".status"]=Step3Status(result.status);
        context->observed[parts[6]+".scope"]=Step3WheelScope(result.scope);
        context->observed[parts[6]+".handled"]=Step3Bool(result.handled);
        context->observed[parts[6]+".drawer_changed"]=Step3Bool(result.drawerChanged);
        context->observed[parts[6]+".main_changed"]=Step3Bool(result.mainCardChanged);
        context->observed[parts[6]+".raster"]=Step3Bool(result.accessoryRasterRequested);
        context->observed[parts[6]+".drawer_offset"]=std::to_string(result.drawerOffset);
        context->observed[parts[6]+".main_offset"]=std::to_string(result.mainCardOffset);
        context->observed[parts[6]+".pointer_x"]=std::to_string(input.pointerX);
        context->observed[parts[6]+".pointer_y"]=std::to_string(input.pointerY);
        return true;
    }
    if (parts[0] == "perf-new" && parts.size() == 3) {
        int epoch=0;
        if(!parseInt(parts[2],&epoch) || epoch==0) return fail("invalid performance epoch");
        auto collector=std::make_unique<
            xvatsim::modules::overlay::AccessoryPerformanceCollector>();
        collector->ResetForNewProcess(static_cast<std::uint64_t>(epoch));
        context->performanceCollectors[parts[1]]=std::move(collector);
        return true;
    }
    if (parts[0] == "perf-record-series" && parts.size() == 5) {
        const auto collector=context->performanceCollectors.find(parts[1]);
        if(collector==context->performanceCollectors.end()) {
            return fail("performance collector missing");
        }
        const auto category=Step3PerformanceCategoryFromToken(parts[2]);
        if(category==xvatsim::modules::overlay::AccessoryPerformanceCategory::Count) {
            return fail("invalid performance category");
        }
        int recorded=0;
        for(const auto& value:Split(parts[3],',')) {
            try {
                collector->second->Record(category,std::stoull(value));
                ++recorded;
            } catch (...) {
                return fail("invalid performance series");
            }
        }
        context->observed[parts[4]+".recorded"]=std::to_string(recorded);
        ObserveStep3PerformanceSnapshot(
            collector->second->Snapshot(),parts[4],&context->observed);
        return true;
    }
    if (parts[0] == "perf-raster-reason" && parts.size() == 6) {
        const auto collector=context->performanceCollectors.find(parts[1]);
        if(collector==context->performanceCollectors.end()) {
            return fail("performance collector missing");
        }
        const bool drawer=parts[2]=="drawer";
        if(!drawer && parts[2]!="rail") return fail("invalid raster surface");
        const auto reason=Step3RasterReasonFromToken(parts[3]);
        if(reason==xvatsim::modules::overlay::AccessoryRasterReason::Count) {
            return fail("invalid raster reason");
        }
        int count=0;
        if(!parseInt(parts[4],&count) || count<0) {
            return fail("invalid raster reason count");
        }
        for(int index=0;index<count;++index) {
            collector->second->RecordRasterReason(drawer,reason);
        }
        ObserveStep3PerformanceSnapshot(
            collector->second->Snapshot(),parts[5],&context->observed);
        return true;
    }
    if (parts[0] == "perf-begin-dual-action" && parts.size() == 18) {
        const auto collector=context->performanceCollectors.find(parts[1]);
        if(collector==context->performanceCollectors.end()) {
            return fail("performance collector missing");
        }
        using Action=xvatsim::brain::BrainOwnedAccessoryDrawerAction;
        Action action=Action::None;
        if(parts[2]=="Opened") action=Action::Opened;
        else if(parts[2]=="Switched") action=Action::Switched;
        else if(parts[2]=="Closed") action=Action::Closed;
        const bool cpuAvailable=parts[10]=="true";
        if(!cpuAvailable && parts[10]!="false") {
            return fail("invalid thread CPU availability");
        }
        try {
            xvatsim::modules::overlay::AccessoryActionDispatchTimingInput timing;
            timing.dispatchStartedMicroseconds=std::stoull(parts[5]);
            timing.dispatchCompletedMicroseconds=std::stoull(parts[6]);
            for(std::size_t index=0;index<5;++index) {
                timing.stages.elapsedMicroseconds[index]=
                    std::stoull(parts[12+index]);
            }
            const auto began=collector->second->BeginDrawerAction(
                action,std::stoull(parts[3]),std::stoull(parts[4]),
                std::stoull(parts[7]),std::stoull(parts[8]),
                std::stoull(parts[6]),std::stoull(parts[9]),timing);
            context->observed[parts[17]+".began"]=Step3Bool(began);
        } catch (...) {
            return fail("invalid dual-clock action input");
        }
        return true;
    }
    if (parts[0] == "perf-complete-dual" && parts.size() == 14) {
        const auto collector=context->performanceCollectors.find(parts[1]);
        if(collector==context->performanceCollectors.end()) {
            return fail("performance collector missing");
        }
        const bool cpuAvailable=parts[9]=="true";
        if(!cpuAvailable && parts[9]!="false") {
            return fail("invalid draw thread CPU availability");
        }
        try {
            xvatsim::modules::overlay::AccessoryActionDrawTimingInput timing;
            timing.actionDrawWallMicroseconds=std::stoull(parts[8]);
            timing.renderWallFailure=parts[11]!="none";
            if(timing.renderWallFailure) {
                timing.renderWallFailureCategory=
                    Step3PerformanceCategoryFromToken(parts[11]);
                if(timing.renderWallFailureCategory==
                    xvatsim::modules::overlay::AccessoryPerformanceCategory::Count) {
                    return fail("invalid render wall failure category");
                }
            }
            timing.renderWallFailureMicroseconds=std::stoull(parts[12]);
            const auto matching=collector->second->HasMatchingPendingAction(
                std::stoull(parts[3]),std::stoull(parts[4]),
                std::stoull(parts[7]));
            const auto completed=collector->second->CompletePendingActions(
                std::stoull(parts[2]),std::stoull(parts[3]),
                std::stoull(parts[4]),std::stoull(parts[5]),
                std::stoull(parts[6]),std::stoull(parts[7]),timing);
            context->observed[parts[13]+".completed"]=
                std::to_string(completed);
            ObserveStep3PerformanceSnapshot(
                collector->second->Snapshot(),parts[13],&context->observed);
        } catch (...) {
            return fail("invalid dual-clock completion input");
        }
        return true;
    }
    if (parts[0] == "perf-begin-phased-action" && parts.size() == 10) {
        const auto collector=context->performanceCollectors.find(parts[1]);
        if(collector==context->performanceCollectors.end()) {
            return fail("performance collector missing");
        }
        using Action=xvatsim::brain::BrainOwnedAccessoryDrawerAction;
        Action action=Action::None;
        if(parts[2]=="Opened") action=Action::Opened;
        else if(parts[2]=="Switched") action=Action::Switched;
        else if(parts[2]=="Closed") action=Action::Closed;
        try {
            xvatsim::modules::overlay::AccessoryActionDispatchTimingInput timing;
            timing.dispatchStartedMicroseconds=std::stoull(parts[4]);
            timing.dispatchCompletedMicroseconds=std::stoull(parts[5]);
            const auto began=collector->second->BeginDrawerAction(
                action,std::stoull(parts[3]),std::stoull(parts[4]),
                std::stoull(parts[6]),std::stoull(parts[7]),
                std::stoull(parts[5]),std::stoull(parts[8]),timing);
            context->observed[parts[9]+".began"]=Step3Bool(began);
        } catch (...) {
            return fail("invalid phased action input");
        }
        return true;
    }
    if (parts[0] == "perf-complete-phased" && parts.size() == 10) {
        const auto collector=context->performanceCollectors.find(parts[1]);
        if(collector==context->performanceCollectors.end()) {
            return fail("performance collector missing");
        }
        try {
            xvatsim::modules::overlay::AccessoryActionDrawTimingInput timing;
            timing.actionDrawWallMicroseconds=std::stoull(parts[8]);
            const auto completed=collector->second->CompletePendingActions(
                std::stoull(parts[2]),std::stoull(parts[3]),
                std::stoull(parts[4]),std::stoull(parts[5]),
                std::stoull(parts[6]),std::stoull(parts[7]),
                timing);
            context->observed[parts[9]+".completed"]=
                std::to_string(completed);
            ObserveStep3PerformanceSnapshot(
                collector->second->Snapshot(),parts[9],&context->observed);
        } catch (...) {
            return fail("invalid phased completion input");
        }
        return true;
    }
    if (parts[0] == "perf-begin-action" && parts.size() == 6) {
        const auto collector=context->performanceCollectors.find(parts[1]);
        if(collector==context->performanceCollectors.end()) {
            return fail("performance collector missing");
        }
        int sequence=0,started=0;
        if(!parseInt(parts[3],&sequence)||!parseInt(parts[4],&started)) {
            return fail("invalid performance action input");
        }
        using Action=xvatsim::brain::BrainOwnedAccessoryDrawerAction;
        Action action=Action::None;
        if(parts[2]=="Opened") action=Action::Opened;
        else if(parts[2]=="Switched") action=Action::Switched;
        else if(parts[2]=="Closed") action=Action::Closed;
        const auto began=collector->second->BeginDrawerAction(
            action,static_cast<std::uint64_t>(sequence),
            static_cast<std::uint64_t>(started));
        context->observed[parts[5]+".began"]=Step3Bool(began);
        return true;
    }
    if (parts[0] == "perf-begin-scroll" && parts.size() == 4) {
        const auto collector=context->performanceCollectors.find(parts[1]);
        if(collector==context->performanceCollectors.end()) {
            return fail("performance collector missing");
        }
        int started=0;
        if(!parseInt(parts[2],&started)) return fail("invalid scroll start");
        context->observed[parts[3]+".began"]=Step3Bool(
            collector->second->BeginEffectiveScroll(
                static_cast<std::uint64_t>(started)));
        return true;
    }
    if (parts[0] == "perf-complete" && parts.size() == 4) {
        const auto collector=context->performanceCollectors.find(parts[1]);
        if(collector==context->performanceCollectors.end()) {
            return fail("performance collector missing");
        }
        int completed=0;
        if(!parseInt(parts[2],&completed)) return fail("invalid completion time");
        context->observed[parts[3]+".completed"]=std::to_string(
            collector->second->CompletePendingActions(
                static_cast<std::uint64_t>(completed)));
        ObserveStep3PerformanceSnapshot(
            collector->second->Snapshot(),parts[3],&context->observed);
        return true;
    }
    if (parts[0] == "perf-warning" && parts.size() == 3) {
        const auto collector=context->performanceCollectors.find(parts[1]);
        if(collector==context->performanceCollectors.end()) {
            return fail("performance collector missing");
        }
        xvatsim::modules::overlay::AccessoryPerformanceCategory category;
        std::uint64_t elapsed=0;
        const auto emitted=collector->second->ConsumeFirstViolationWarning(
            &category,&elapsed);
        context->observed[parts[2]+".emitted"]=Step3Bool(emitted);
        context->observed[parts[2]+".category"]=emitted
            ? xvatsim::modules::overlay::AccessoryPerformanceCategoryToken(category)
            : "none";
        context->observed[parts[2]+".elapsed_us"]=std::to_string(elapsed);
        ObserveStep3PerformanceSnapshot(
            collector->second->Snapshot(),parts[2],&context->observed);
        return true;
    }
    if (parts[0] == "perf-publish" && parts.size() == 3) {
        const auto collector=context->performanceCollectors.find(parts[1]);
        if(collector==context->performanceCollectors.end()) {
            return fail("performance collector missing");
        }
        xvatsim::modules::overlay::AccessoryPerformanceSnapshot snapshot;
        const auto published=
            collector->second->BeginAggregatePublication(&snapshot);
        context->observed[parts[2]+".published"]=Step3Bool(published);
        ObserveStep3PerformanceSnapshot(
            collector->second->Snapshot(),parts[2],&context->observed);
        return true;
    }
    if (parts[0] == "perf-snapshot" && parts.size() == 3) {
        const auto collector=context->performanceCollectors.find(parts[1]);
        if(collector==context->performanceCollectors.end()) {
            return fail("performance collector missing");
        }
        ObserveStep3PerformanceSnapshot(
            collector->second->Snapshot(),parts[2],&context->observed);
        return true;
    }
    if (parts[0] == "presenter-new" && parts.size() == 2) {
        context->presenters[parts[1]] =
            xvatsim::modules::overlay::AccessoryPresentationState{};
        return true;
    }
    if (parts[0] == "presenter-update" && parts.size() == 6) {
        const auto snapshot=context->snapshots.find(parts[2]);
        const auto layout=context->layouts.find(parts[3]);
        if(snapshot==context->snapshots.end()) return fail("presentation snapshot missing");
        if(layout==context->layouts.end()) return fail("presentation layout missing");
        xvatsim::modules::overlay::AccessoryPresentationUpdateInput input;
        input.presentation=snapshot->second;
        input.layout=layout->second;
        input.preparedPlan=Step3PreparePresentationPlan(
            context,snapshot->second,layout->second);
        input.measurementContext=context->measurementContext;
        const auto main=context->mainCardSignatures.find(parts[4]);
        if(main!=context->mainCardSignatures.end()) input.mainCardProductionSignature=main->second;
        auto& presenter=context->presenters[parts[1]];
        const auto gdiBefore=
            xvatsim::modules::overlay::GetAccessoryGdiMeasurementCounters(
                context->measurementContext);
        const auto result=xvatsim::modules::overlay::UpdateAccessoryPresentation(&presenter,input);
        const auto gdiAfter=
            xvatsim::modules::overlay::GetAccessoryGdiMeasurementCounters(
                context->measurementContext);
        const auto& prefix=parts[5];
        context->observed[prefix+".status"]=Step3Status(result.status);
        context->observed[prefix+".snapshot_changed"]=Step3Bool(result.snapshotChanged);
        context->observed[prefix+".selection_changed"]=Step3Bool(result.selectionChanged);
        context->observed[prefix+".history_changed"]=Step3Bool(result.historyChanged);
        context->observed[prefix+".layout_changed"]=Step3Bool(result.layoutChanged);
        context->observed[prefix+".plan_built"]=Step3Bool(result.cachedPlanBuilt);
        context->observed[prefix+".offset_reset"]=Step3Bool(result.drawerOffsetReset);
        context->observed[prefix+".main_unchanged"]=Step3Bool(result.mainCardUnchanged);
        context->observed[prefix+".published_count"]=
            std::to_string(result.publishedSnapshotCount);
        context->observed[prefix+".published_drawer"]=Step3Drawer(result.publishedDrawer);
        context->observed[prefix+".intermediate_none"]=
            Step3Bool(result.intermediateNonePublished);
        ObserveStep3RenderCounters(result.delta,prefix,&context->observed);
        ObserveStep3GdiCounters(
            Step3GdiCounterDelta(gdiBefore,gdiAfter),
            prefix,
            &context->observed);
        context->observed[prefix+".offset"]=std::to_string(presenter.drawerOffset);
        context->observed[prefix+".maximum_offset"]=
            std::to_string(presenter.drawerMaximumOffset);
        context->observed[prefix+".snapshot_identity"]=
            std::to_string(presenter.activeSnapshotIdentity);
        context->observed[prefix+".selection_generation"]=
            std::to_string(presenter.selectionGeneration);
        context->observed[prefix+".history_generation"]=
            std::to_string(presenter.historyGeneration);
        context->observed[prefix+".layout_generation"]=
            std::to_string(presenter.layoutGeneration);
        context->observed[prefix+".keys"]=presenter.preparedPlan ?
            JoinCsv(presenter.preparedPlan->layout.renderedEntryKeys) : "";
        context->observed[prefix+".titles"]=presenter.preparedPlan ?
            JoinCsv(presenter.preparedPlan->layout.renderedEntryTitles) : "";
        context->observed[prefix+".bodies"]=presenter.preparedPlan ?
            JoinCsv(presenter.preparedPlan->layout.renderedEntryBodies) : "";
        context->observed[prefix+".title_marker_visible"]=
            Step3Bool(Step3PresenterTitleMarkerVisible(presenter));
        context->observed[prefix+".pairing_intact"]=
            Step3Bool(Step3PresenterPairingIntact(presenter));
        return true;
    }
    if (parts[0] == "presenter-warm" && parts.size() == 7) {
        int iterations=0;
        if(!parseInt(parts[5],&iterations)) return fail("invalid warm iteration count");
        const auto snapshot=context->snapshots.find(parts[2]);
        const auto layout=context->layouts.find(parts[3]);
        if(snapshot==context->snapshots.end()) return fail("warm snapshot missing");
        if(layout==context->layouts.end()) return fail("warm layout missing");
        xvatsim::modules::overlay::AccessoryPresentationUpdateInput input;
        input.presentation=snapshot->second;
        input.layout=layout->second;
        input.preparedPlan=Step3PreparePresentationPlan(
            context,snapshot->second,layout->second);
        input.measurementContext=context->measurementContext;
        const auto main=context->mainCardSignatures.find(parts[4]);
        if(main!=context->mainCardSignatures.end()) input.mainCardProductionSignature=main->second;
        auto& presenter=context->presenters[parts[1]];
        const auto gdiBefore=
            xvatsim::modules::overlay::GetAccessoryGdiMeasurementCounters(
                context->measurementContext);
        const auto result=
            xvatsim::modules::overlay::RunUnchangedAccessoryPresentationUpdates(
                &presenter,input,iterations);
        const auto gdiAfter=
            xvatsim::modules::overlay::GetAccessoryGdiMeasurementCounters(
                context->measurementContext);
        const auto& prefix=parts[6];
        context->observed[prefix+".status"]=Step3Status(result.status);
        context->observed[prefix+".iterations"]=std::to_string(result.iterations);
        ObserveStep3RenderCounters(result.delta,prefix,&context->observed);
        const auto gdiDelta=Step3GdiCounterDelta(gdiBefore,gdiAfter);
        ObserveStep3GdiCounters(
            gdiDelta,
            prefix,
            &context->observed);
        context->observed[prefix+".zero_work_record"]=
            "iterations="+std::to_string(result.iterations)+
            ",gdi="+std::to_string(gdiDelta.measurementCalls)+
            ",history="+std::to_string(result.delta.historyVisits)+
            ",copies="+std::to_string(result.delta.entryCopies)+
            ",wraps="+std::to_string(result.delta.wrapVisits)+
            ",main="+std::to_string(result.delta.mainCardRasterRequests)+
            ",rail="+std::to_string(result.delta.railRasterRequests)+
            ",drawer="+std::to_string(result.delta.drawerRasterRequests)+
            ",uploads="+std::to_string(result.delta.uploadRequests);
        return true;
    }
    if (parts[0] == "presenter-scroll" && parts.size() == 6) {
        int clicks=0;
        try { clicks=std::stoi(parts[4]); } catch (...) { return fail("invalid presenter scroll"); }
        const auto layout=context->layouts.find(parts[2]);
        if(layout==context->layouts.end()) return fail("presenter scroll layout missing");
        xvatsim::modules::overlay::AccessoryPresentationScrollInput input;
        input.layout=layout->second;
        if(parts[3]=="drawer") {
            input.pointerX=(input.layout.drawerBounds.left+input.layout.drawerBounds.right)/2;
            input.pointerY=(input.layout.drawerBounds.top+input.layout.drawerBounds.bottom)/2;
        } else if(parts[3]=="main") {
            input.pointerX=(input.layout.mainCardBounds.left+input.layout.mainCardBounds.right)/2;
            input.pointerY=(input.layout.mainCardBounds.top+input.layout.mainCardBounds.bottom)/2;
        } else if(parts[3]=="outside") {
            input.pointerX=input.layout.resolvedBounds.right+1;
            input.pointerY=input.layout.resolvedBounds.bottom+1;
        } else {
            return fail("invalid presenter scroll pointer region");
        }
        input.wheelClicks=clicks;
        auto& presenter=context->presenters[parts[1]];
        const auto result=xvatsim::modules::overlay::ScrollAccessoryPresentation(
            &presenter,input);
        const auto& prefix=parts[5];
        context->observed[prefix+".status"]=Step3Status(result.status);
        context->observed[prefix+".scope"]=Step3WheelScope(result.scope);
        context->observed[prefix+".handled"]=Step3Bool(result.handled);
        context->observed[prefix+".changed"]=Step3Bool(result.changed);
        context->observed[prefix+".reached_final"]=Step3Bool(result.reachedFinalMarker);
        context->observed[prefix+".previous_offset"]=std::to_string(result.previousOffset);
        context->observed[prefix+".offset"]=std::to_string(result.drawerOffset);
        context->observed[prefix+".maximum_offset"]=
            std::to_string(result.drawerMaximumOffset);
        context->observed[prefix+".snapshot_identity"]=
            std::to_string(presenter.activeSnapshotIdentity);
        context->observed[prefix+".selection_generation"]=
            std::to_string(presenter.selectionGeneration);
        context->observed[prefix+".history_generation"]=
            std::to_string(presenter.historyGeneration);
        context->observed[prefix+".layout_generation"]=
            std::to_string(presenter.layoutGeneration);
        ObserveStep3RenderCounters(result.delta,prefix,&context->observed);
        return true;
    }
    if (parts[0] == "presenter-scroll-to-max" && parts.size() == 4) {
        const auto layout=context->layouts.find(parts[2]);
        if(layout==context->layouts.end()) return fail("presenter max scroll layout missing");
        auto& presenter=context->presenters[parts[1]];
        const int measuredMaximum=presenter.drawerMaximumOffset;
        const int requiredClicks=std::max(1,measuredMaximum-presenter.drawerOffset+1);
        xvatsim::modules::overlay::AccessoryPresentationScrollInput input;
        input.layout=layout->second;
        input.pointerX=(input.layout.drawerBounds.left+input.layout.drawerBounds.right)/2;
        input.pointerY=(input.layout.drawerBounds.top+input.layout.drawerBounds.bottom)/2;
        input.wheelClicks=requiredClicks;
        const auto result=xvatsim::modules::overlay::ScrollAccessoryPresentation(
            &presenter,input);
        const auto& prefix=parts[3];
        context->observed[prefix+".status"]=Step3Status(result.status);
        context->observed[prefix+".scope"]=Step3WheelScope(result.scope);
        context->observed[prefix+".measured_maximum"]=std::to_string(measuredMaximum);
        context->observed[prefix+".requested_clicks"]=std::to_string(requiredClicks);
        context->observed[prefix+".offset"]=std::to_string(result.drawerOffset);
        context->observed[prefix+".maximum_offset"]=
            std::to_string(result.drawerMaximumOffset);
        context->observed[prefix+".at_measured_maximum"]=
            Step3Bool(result.drawerOffset==measuredMaximum);
        context->observed[prefix+".reached_final"]=Step3Bool(result.reachedFinalMarker);
        context->observed[prefix+".snapshot_identity"]=
            std::to_string(presenter.activeSnapshotIdentity);
        context->observed[prefix+".selection_generation"]=
            std::to_string(presenter.selectionGeneration);
        context->observed[prefix+".history_generation"]=
            std::to_string(presenter.historyGeneration);
        context->observed[prefix+".layout_generation"]=
            std::to_string(presenter.layoutGeneration);
        ObserveStep3RenderCounters(result.delta,prefix,&context->observed);
        const auto renderPlan=
            xvatsim::modules::overlay::BuildAccessoryDrawerRenderPlan(
                presenter,layout->second);
        context->observed[prefix+".render_capacity"]=
            std::to_string(renderPlan.visibleLineCapacity);
        context->observed[prefix+".render_visible_count"]=
            std::to_string(renderPlan.visibleLines.size());
        context->observed[prefix+".render_total_lines"]=
            std::to_string(renderPlan.totalLineCount);
        context->observed[prefix+".render_pixel_fit"]=
            Step3Bool(renderPlan.everyLineFitsPixelGeometry);
        context->observed[prefix+".render_final_marker_visible"]=
            Step3Bool(renderPlan.finalMarkerVisible);
        return true;
    }
    if (parts[0] == "drawer-render-plan" && parts.size() == 4) {
        const auto presenter=context->presenters.find(parts[1]);
        const auto layout=context->layouts.find(parts[2]);
        if(presenter==context->presenters.end()) return fail("render presenter missing");
        if(layout==context->layouts.end()) return fail("render layout missing");
        const auto result=
            xvatsim::modules::overlay::BuildAccessoryDrawerRenderPlan(
                presenter->second,layout->second);
        const auto& prefix=parts[3];
        context->observed[prefix+".status"]=Step3Status(result.status);
        context->observed[prefix+".capacity"]=
            std::to_string(result.visibleLineCapacity);
        context->observed[prefix+".visible_count"]=
            std::to_string(result.visibleLines.size());
        context->observed[prefix+".first_line"]=
            std::to_string(result.firstVisibleLine);
        context->observed[prefix+".total_lines"]=
            std::to_string(result.totalLineCount);
        context->observed[prefix+".pixel_fit"]=
            Step3Bool(result.everyLineFitsPixelGeometry);
        context->observed[prefix+".final_marker_visible"]=
            Step3Bool(result.finalMarkerVisible);
        context->observed[prefix+".visible_count_within_capacity"]=Step3Bool(
            static_cast<int>(result.visibleLines.size())<=
                result.visibleLineCapacity);
        return true;
    }
    if (parts[0] == "presenter-observe" && parts.size() == 3) {
        const auto& presenter=context->presenters[parts[1]];
        const auto& prefix=parts[2];
        context->observed[prefix+".offset"]=std::to_string(presenter.drawerOffset);
        context->observed[prefix+".maximum_offset"]=
            std::to_string(presenter.drawerMaximumOffset);
        context->observed[prefix+".snapshot_identity"]=
            std::to_string(presenter.activeSnapshotIdentity);
        context->observed[prefix+".selection_generation"]=
            std::to_string(presenter.selectionGeneration);
        context->observed[prefix+".history_generation"]=
            std::to_string(presenter.historyGeneration);
        context->observed[prefix+".layout_generation"]=
            std::to_string(presenter.layoutGeneration);
        context->observed[prefix+".plan_available"]=Step3Bool(presenter.cachedPlanAvailable);
        context->observed[prefix+".keys"]=presenter.preparedPlan ?
            JoinCsv(presenter.preparedPlan->layout.renderedEntryKeys) : "";
        context->observed[prefix+".titles"]=presenter.preparedPlan ?
            JoinCsv(presenter.preparedPlan->layout.renderedEntryTitles) : "";
        context->observed[prefix+".bodies"]=presenter.preparedPlan ?
            JoinCsv(presenter.preparedPlan->layout.renderedEntryBodies) : "";
        context->observed[prefix+".title_marker_visible"]=
            Step3Bool(Step3PresenterTitleMarkerVisible(presenter));
        context->observed[prefix+".pairing_intact"]=
            Step3Bool(Step3PresenterPairingIntact(presenter));
        context->observed[prefix+".final_marker"]=presenter.finalHistoryMarker;
        context->observed[prefix+".main_signature"]=presenter.mainCardProductionSignature;
        ObserveStep3RenderCounters(presenter.counters,prefix,&context->observed);
        return true;
    }
    if ((parts[0] == "pipeline" || parts[0] == "pipeline-alternate") &&
        parts.size() == 2) {
        xvatsim::brain::FinalDisplaySnapshot display;
        display.available = true;
        display.source = xvatsim::brain::BoardSource::Enroute;
        xvatsim::brain::FinalDisplayStationSnapshot station;
        const bool alternate=parts[0]=="pipeline-alternate";
        station.role = alternate
            ? xvatsim::brain::StationRole::Approach
            : xvatsim::brain::StationRole::Center;
        station.callsign = alternate ? "SOCAL_APP" : "LAX_CTR";
        station.frequency = alternate ? "124.300" : "125.800";
        station.online = true;
        display.stations.push_back(station);
        xvatsim::brain::AircraftStateSnapshot aircraft;
        aircraft.valid = true;
        xvatsim::brain::XPilotSessionSnapshot xpilot;
        xpilot.connected = true;
        const auto view=xvatsim::brain::BrainOrchestrator::BuildOverlayViewModel(
            xvatsim::brain::WorkflowStage::Enroute,
            aircraft,
            xpilot,
            {}, {}, {}, {}, display, {}, {});
        context->observed[parts[1]+".visible"]=Step3Bool(view.visible);
        context->observed[parts[1]+".title"]=view.title;
        context->observed[parts[1]+".body_count"]=std::to_string(view.bodyLines.size());
        std::ostringstream signature;
        signature << (view.visible ? "visible" : "hidden") << '|' << view.title;
        for(const auto& line:view.bodyLines) signature << '|' << line.text;
        context->mainCardSignatures[parts[1]]=signature.str();
        context->observed[parts[1]+".signature"]=signature.str();
        return true;
    }
    return fail("unknown Step 3 action");
}

int RunStep3ContractProbe(const ScenarioData& scenario) {
    if (scenario.step3.actions.empty() || scenario.step3.expectations.empty()) {
        std::cerr << "STEP3_SCENARIO_CONFIGURATION_ERROR: " << scenario.name
                  << ": executable actions and expectations are required\n";
        return 2;
    }

    Step3ExecutionContext context;
    for (const auto& action : scenario.step3.actions) {
        std::string error;
        if (!ExecuteStep3Action(action, &context, &error)) {
            std::cerr << "STEP3_SCENARIO_CONFIGURATION_ERROR: " << scenario.name
                      << ": " << error << "\n";
            return 2;
        }
    }

    int failures = 0;
    for (const auto& expectation : scenario.step3.expectations) {
        const auto separator = expectation.find('=');
        if (separator == std::string::npos || separator == 0) {
            std::cerr << "STEP3_SCENARIO_CONFIGURATION_ERROR: " << scenario.name
                      << ": invalid expectation: " << expectation << "\n";
            return 2;
        }
        const auto key = expectation.substr(0, separator);
        const auto expectedToken = expectation.substr(separator + 1);
        auto observed = context.observed.find(key);
        if (observed == context.observed.end()) {
            std::cerr << "STEP3_SCENARIO_CONFIGURATION_ERROR: " << scenario.name
                      << ": expectation has no operation-produced observation: " << key << "\n";
            return 2;
        }
        std::string expected = expectedToken;
        if (!expectedToken.empty() && expectedToken.front() == '@') {
            const auto reference = context.observed.find(expectedToken.substr(1));
            if (reference == context.observed.end()) {
                std::cerr << "STEP3_SCENARIO_CONFIGURATION_ERROR: " << scenario.name
                          << ": missing expectation reference: " << expectedToken << "\n";
                return 2;
            }
            expected = reference->second;
        }
        if (observed->second != expected) {
            std::cerr << "STEP3_ASSERTION_FAILED: " << scenario.name << ": " << key
                      << " expected=" << expected << " observed=" << observed->second << "\n";
            failures++;
        }
    }
    if (failures != 0) return 1;
    std::vector<std::pair<std::string,std::string>> measurementRecords;
    std::vector<std::pair<std::string,std::string>> timingRecords;
    std::vector<std::pair<std::string,std::string>> zeroWorkRecords;
    std::vector<std::pair<std::string,std::string>> dispatchRecords;
    for(const auto& observation:context.observed) {
        if(observation.first.size()>=18 &&
           observation.first.compare(
               observation.first.size()-18,
               18,
               "measurement_record")==0) {
            measurementRecords.push_back(observation);
        }
        if(observation.first.size()>=13 &&
           observation.first.compare(
               observation.first.size()-13,
               13,
               "timing_record")==0) {
            timingRecords.push_back(observation);
        }
        if(observation.first.size()>=16 &&
           observation.first.compare(
               observation.first.size()-16,
               16,
               "zero_work_record")==0) {
            zeroWorkRecords.push_back(observation);
        }
        if(observation.first.size()>=15 &&
           observation.first.compare(
               observation.first.size()-15,
               15,
               "dispatch_record")==0) {
            dispatchRecords.push_back(observation);
        }
    }
    std::sort(measurementRecords.begin(),measurementRecords.end());
    for(const auto& record:measurementRecords) {
        std::cout << "STEP3_GDIPLUS_MEASUREMENT: " << record.first
                  << '=' << record.second << "\n";
    }
    std::sort(timingRecords.begin(),timingRecords.end());
    for(const auto& record:timingRecords) {
        std::cout << "STEP3_GDIPLUS_TIMING: " << record.first
                  << '=' << record.second << "\n";
    }
    std::sort(zeroWorkRecords.begin(),zeroWorkRecords.end());
    for(const auto& record:zeroWorkRecords) {
        std::cout << "STEP3_ZERO_WORK: " << record.first
                  << '=' << record.second << "\n";
    }
    std::sort(dispatchRecords.begin(),dispatchRecords.end());
    for(const auto& record:dispatchRecords) {
        std::cout << "STEP3_EVENT_DISPATCH: " << record.first
                  << '=' << record.second << "\n";
    }
    std::cout << "Scenario passed: " << scenario.name << "\n";
    return 0;
}

class Step4FakeWorker final : public xvatsim::brain::BrainMetarWorker {
public:
    bool Start(const xvatsim::brain::BrainMetarWorkerRequest& request) override {
        if (running || !startAllowed) return false;
        requests.push_back(request);
        running = true;
        return true;
    }

    bool TryHarvest(xvatsim::brain::BrainMetarWorkerFact* fact) override {
        if (fact == nullptr || !ready.has_value()) return false;
        *fact = *ready;
        ready.reset();
        running = false;
        return true;
    }

    bool IsRunning() const override { return running; }

    void CancelAndJoin() override {
        running = false;
        ++cancelCount;
    }

    xvatsim::brain::BrainMetarWorkerShutdownSnapshot ShutdownSnapshot()
        const override {
        xvatsim::brain::BrainMetarWorkerShutdownSnapshot snapshot;
        snapshot.running = running;
        snapshot.handlesClosed = !running;
        snapshot.callbacksClosed = !running;
        return snapshot;
    }

    void Complete(
        xvatsim::brain::BrainMetarWorkerStatus status,
        std::string station = {},
        std::string raw = {}) {
        if (requests.empty()) return;
        CompleteRequest(requests.back(), status, std::move(station), std::move(raw));
    }

    void CompleteRequest(
        const xvatsim::brain::BrainMetarWorkerRequest& request,
        xvatsim::brain::BrainMetarWorkerStatus status,
        std::string station = {},
        std::string raw = {}) {
        xvatsim::brain::BrainMetarWorkerFact fact;
        fact.request = request;
        fact.status = status;
        fact.stationIcao = station.empty() ? request.airportIcao : std::move(station);
        fact.rawMetar = std::move(raw);
        fact.httpStatus = status == xvatsim::brain::BrainMetarWorkerStatus::Success
            ? 200 : 0;
        fact.payloadBytes = fact.rawMetar.size();
        fact.networkElapsedUs = 2'500'000;
        fact.diagnostic = status == xvatsim::brain::BrainMetarWorkerStatus::Success
            ? "fixture-success" : "fixture-failure";
        ready = std::move(fact);
        running = false;
    }

    bool startAllowed = true;
    bool running = false;
    int cancelCount = 0;
    std::vector<xvatsim::brain::BrainMetarWorkerRequest> requests;
    std::optional<xvatsim::brain::BrainMetarWorkerFact> ready;
};

#if defined(_WIN32)
class Step4LoopbackHttpPeer {
public:
    struct Response {
        int status = 200;
        std::string body;
        bool stall = false;
    };

    explicit Step4LoopbackHttpPeer(Response response)
        : response_(std::move(response)) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return;
        winsockStarted_ = true;
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener_ == INVALID_SOCKET) return;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (bind(listener_, reinterpret_cast<const sockaddr*>(&address),
                 sizeof(address)) == SOCKET_ERROR ||
            listen(listener_, 1) == SOCKET_ERROR) {
            return;
        }
        int addressSize = sizeof(address);
        if (getsockname(listener_, reinterpret_cast<sockaddr*>(&address),
                        &addressSize) == SOCKET_ERROR) {
            return;
        }
        port_ = ntohs(address.sin_port);
        thread_ = std::thread([this]() {
            const auto client = accept(listener_, nullptr, nullptr);
            if (client == INVALID_SOCKET) return;
            client_.store(client, std::memory_order_release);
            std::string request;
            char chunk[1024]{};
            while (request.find("\r\n\r\n") == std::string::npos &&
                   request.size() < 8192) {
                const auto received = recv(client, chunk, sizeof(chunk), 0);
                if (received <= 0) break;
                request.append(chunk, static_cast<std::size_t>(received));
            }
            const auto lineEnd = request.find("\r\n");
            const auto firstLine = request.substr(0, lineEnd);
            const auto firstSpace = firstLine.find(' ');
            const auto secondSpace = firstSpace == std::string::npos
                ? std::string::npos : firstLine.find(' ', firstSpace + 1);
            if (firstSpace != std::string::npos &&
                secondSpace != std::string::npos) {
                {
                    std::lock_guard<std::mutex> lock(requestMutex_);
                    requestPath_ = firstLine.substr(
                        firstSpace + 1, secondSpace - firstSpace - 1);
                }
                accepted_.store(true, std::memory_order_release);
            }
            if (response_.stall) {
                while (!stop_.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            } else {
                const auto reason = response_.status == 200 ? "OK" : "ERROR";
                std::ostringstream response;
                response << "HTTP/1.1 " << response_.status << ' ' << reason
                         << "\r\nContent-Type: application/json"
                         << "\r\nContent-Length: " << response_.body.size()
                         << "\r\nConnection: close\r\n\r\n"
                         << response_.body;
                const auto bytes = response.str();
                std::size_t sent = 0;
                while (sent < bytes.size()) {
                    const auto count = send(
                        client, bytes.data() + sent,
                        static_cast<int>(bytes.size() - sent), 0);
                    if (count <= 0) break;
                    sent += static_cast<std::size_t>(count);
                }
                responseCompleted_.store(
                    sent == bytes.size(), std::memory_order_release);
            }
            const auto owned = client_.exchange(
                INVALID_SOCKET, std::memory_order_acq_rel);
            if (owned != INVALID_SOCKET) {
                shutdown(owned, SD_BOTH);
                closesocket(owned);
            }
        });
    }

    ~Step4LoopbackHttpPeer() {
        stop_.store(true, std::memory_order_release);
        const auto client = client_.exchange(
            INVALID_SOCKET, std::memory_order_acq_rel);
        if (client != INVALID_SOCKET) {
            shutdown(client, SD_BOTH);
            closesocket(client);
        }
        if (listener_ != INVALID_SOCKET) {
            closesocket(listener_);
            listener_ = INVALID_SOCKET;
        }
        if (thread_.joinable()) thread_.join();
        if (winsockStarted_) WSACleanup();
    }

    bool Ready() const { return port_ != 0 && thread_.joinable(); }
    bool Accepted() const {
        return accepted_.load(std::memory_order_acquire);
    }
    bool ResponseCompleted() const {
        return responseCompleted_.load(std::memory_order_acquire);
    }
    std::string RequestPath() const {
        std::lock_guard<std::mutex> lock(requestMutex_);
        return requestPath_;
    }
    unsigned short Port() const { return port_; }

private:
    Response response_;
    bool winsockStarted_ = false;
    SOCKET listener_ = INVALID_SOCKET;
    std::atomic<SOCKET> client_{INVALID_SOCKET};
    unsigned short port_ = 0;
    std::atomic<bool> accepted_{false};
    std::atomic<bool> responseCompleted_{false};
    std::atomic<bool> stop_{false};
    mutable std::mutex requestMutex_;
    std::string requestPath_;
    std::thread thread_;
};
#endif

struct Step4Fixture {
    xvatsim::brain::BrainOwnedRuntimeState state;
    Step4FakeWorker worker;
    xvatsim::brain::BrainOwnedAsyncFactCycleInput input;

    Step4Fixture() {
        xvatsim::brain::EnableBrainOwnedAccessoryRuntime(&state);
        input.pluginEnabled = true;
        input.xpilotConnected = true;
        input.workflowStage = WorkflowStage::Departure;
        input.operatingMode = xvatsim::brain::BrainOwnedOperatingMode::IFR;
        input.flightContext.active = true;
        input.flightContext.callsign = "N123XV";
        input.flightContext.departureIcao = "KDFW";
        input.flightContext.destinationIcao = "KSAN";
        input.monotonicMs = 1'000;
        input.utcUnixSeconds = 1'787'860'800;
    }

    xvatsim::brain::BrainOwnedAsyncFactCycleOutput Cycle(long long advanceMs = 0) {
        input.monotonicMs += advanceMs;
        xvatsim::brain::BrainOwnedAsyncWorkerBindings bindings;
        bindings.metar = &worker;
        return xvatsim::brain::RunBrainOwnedAsyncFactCycle(
            &state, input, bindings);
    }

    xvatsim::brain::BrainOwnedAsyncFactCycleOutput AcceptPrimary(
        const std::string& raw = "KDFW 271951Z 18010KT 10SM FEW050") {
        if (worker.requests.empty() || !worker.running) Cycle();
        worker.Complete(
            xvatsim::brain::BrainMetarWorkerStatus::Success,
            worker.requests.back().airportIcao,
            raw);
        return Cycle(1);
    }

    xvatsim::brain::BrainOwnedTextEntryDecision SubmitLookup(
        const std::string& icao,
        long long advanceMs = 1) {
        input.monotonicMs += advanceMs;
        xvatsim::brain::BrainOwnedTextEntryFact fact;
        fact.mode = xvatsim::brain::BrainOwnedTextEntryMode::MetarAirportLookup;
        fact.text = icao;
        fact.monotonicMs = input.monotonicMs;
        return xvatsim::brain::CommitBrainOwnedTextEntryFact(&state, fact);
    }

    xvatsim::brain::BrainOwnedAsyncFactCycleOutput AcceptLookup(
        const std::string& raw = "KABQ 271953Z 18012KT 4SM BKN020") {
        Cycle();
        worker.Complete(
            xvatsim::brain::BrainMetarWorkerStatus::Success,
            worker.requests.back().airportIcao,
            raw);
        return Cycle(1);
    }
};

#if defined(_WIN32)
struct Step4LoopbackRunResult {
    bool dispatched = false;
    bool terminal = false;
    bool disposition = false;
    bool responseCompleted = false;
    std::string requestPath;
    xvatsim::brain::BrainMetarTerminalDiagnostic terminalDiagnostic;
    xvatsim::brain::BrainMetarDispositionDiagnostic dispositionDiagnostic;
    xvatsim::brain::BrainMetarWorkerShutdownSnapshot shutdown;
    std::uint64_t parseCount = 0;
    std::uint64_t historyMutationCount = 0;
    bool primaryValid = false;
    xvatsim::brain::BrainMetarFlightCategory category =
        xvatsim::brain::BrainMetarFlightCategory::Unknown;
    long long elapsedMs = 0;
};

Step4LoopbackRunResult RunStep4LoopbackLifecycle(
    Step4LoopbackHttpPeer::Response response) {
    Step4LoopbackRunResult result;
    Step4LoopbackHttpPeer peer(std::move(response));
    if (!peer.Ready()) return result;
    xvatsim::modules::metar::VatsimMetarClient::ProofEndpoint endpoint;
    endpoint.host = L"127.0.0.1";
    endpoint.port = peer.Port();
    endpoint.secure = false;
    xvatsim::modules::metar::VatsimMetarClient client(endpoint);
    xvatsim::brain::BrainOwnedRuntimeState state;
    xvatsim::brain::EnableBrainOwnedAccessoryRuntime(&state);
    xvatsim::brain::BrainOwnedAsyncFactCycleInput input;
    input.pluginEnabled = true;
    input.xpilotConnected = true;
    input.workflowStage = xvatsim::brain::WorkflowStage::Departure;
    input.operatingMode = xvatsim::brain::BrainOwnedOperatingMode::IFR;
    input.flightContext.active = true;
    input.flightContext.callsign = "N123XV";
    input.flightContext.departureIcao = "KDFW";
    input.flightContext.destinationIcao = "KSAN";
    input.monotonicMs = 1'000;
    input.utcUnixSeconds = 1'787'860'800;
    xvatsim::brain::BrainOwnedAsyncWorkerBindings bindings;
    bindings.metar = &client;
    const auto started = std::chrono::steady_clock::now();
    auto cycle = xvatsim::brain::RunBrainOwnedAsyncFactCycle(
        &state, input, bindings);
    result.dispatched = cycle.dispatchDiagnostic.available;
    const auto deadline = started + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        ++input.monotonicMs;
        cycle = xvatsim::brain::RunBrainOwnedAsyncFactCycle(
            &state, input, bindings);
        if (cycle.terminalDiagnostic.available) {
            result.terminal = true;
            result.disposition = cycle.dispositionDiagnostic.available;
            result.terminalDiagnostic = cycle.terminalDiagnostic;
            result.dispositionDiagnostic = cycle.dispositionDiagnostic;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    result.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    result.requestPath = peer.RequestPath();
    result.responseCompleted = peer.ResponseCompleted();
    result.parseCount = state.metar.parseCount;
    result.historyMutationCount = state.metar.historyMutationCount;
    result.primaryValid = state.metar.primaryObservation.valid;
    result.category = state.metar.primaryObservation.category;
    client.CancelAndJoin();
    result.shutdown = client.ShutdownSnapshot();
    return result;
}
#endif

const xvatsim::brain::BrainOwnedAccessoryOrbPresentation* Step4MetarOrb(
    const xvatsim::brain::BrainOwnedAccessoryPresentationHandle& handle) {
    if (!handle.snapshot) return nullptr;
    for (const auto& orb : handle.snapshot->orbs) {
        if (orb.drawer == xvatsim::brain::BrainOwnedAccessoryDrawerId::Metar) {
            return &orb;
        }
    }
    return nullptr;
}

bool Step4HasTitle(
    xvatsim::brain::BrainOwnedRuntimeState* state,
    const std::string& token) {
    const auto handle = xvatsim::brain::ProjectBrainOwnedAccessoryPresentation(
        state, 1, nullptr);
    if (!handle.snapshot) return false;
    return std::any_of(
        handle.snapshot->entries.begin(), handle.snapshot->entries.end(),
        [&](const auto& entry) {
            return entry.title.find(token) != std::string::npos;
        });
}

void Step4SelectDrawer(
    xvatsim::brain::BrainOwnedRuntimeState* state,
    xvatsim::brain::BrainOwnedAccessoryDrawerId drawer,
    std::uint64_t sequence = 1) {
    xvatsim::brain::BrainOwnedAccessorySelectionRequest request;
    request.drawer = drawer;
    request.requestSequence = sequence;
    (void)xvatsim::brain::RequestBrainOwnedAccessoryDrawerSelection(
        state, request);
}

void Step4Require(
    bool condition,
    const std::string& message,
    std::vector<std::string>* failures) {
    if (!condition && failures != nullptr) failures->push_back(message);
}

bool Step4FileContains(
    const std::filesystem::path& path,
    const std::string& token) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    const std::string content{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    return content.find(token) != std::string::npos;
}

int RunStep4ContractProbe(const ScenarioData& scenario) {
    using namespace xvatsim::brain;
    using xvatsim::modules::metar::BuildVatsimMetarRequestPath;
    using xvatsim::modules::metar::ExtractVatsimMetarJson;
    const auto& probe = scenario.step4.probe;
    std::vector<std::string> failures;
    auto require = [&](bool condition, const std::string& message) {
        Step4Require(condition, message, &failures);
    };
    const std::string primaryVfr = "KDFW 271951Z 18010KT 10SM FEW050";
    const std::string primaryIfr = "KSAN 271952Z 24009KT 2SM BKN008";
    const std::string lookupMvfr = "KABQ 271953Z 18012KT 4SM BKN020";

    const auto requireNeutralMetarOrb = [&](BrainOwnedRuntimeState* state) {
        const auto handle = ProjectBrainOwnedAccessoryPresentation(
            state, 1, nullptr);
        const auto* orb = Step4MetarOrb(handle);
        require(orb != nullptr, "METAR ORB missing");
        if (orb == nullptr) return;
        require(orb->label == "METAR", "neutral ORB must contain METAR");
        require(orb->airportIcao.empty() && orb->categoryText.empty() &&
                    orb->stateText.empty() && orb->selectedIndicator.empty(),
                "neutral ORB contains forbidden additional text");
        require(orb->tone == BrainOwnedAccessoryOrbPresentation::Tone::Gray &&
                    orb->neutral,
                "neutral ORB must use neutral gray tone");
    };
    const auto requireSuccessfulMetarOrb = [&](
        BrainOwnedRuntimeState* state,
        const std::string& airport,
        const std::string& category,
        BrainOwnedAccessoryOrbPresentation::Tone tone) {
        const auto handle = ProjectBrainOwnedAccessoryPresentation(
            state, 1, nullptr);
        const auto* orb = Step4MetarOrb(handle);
        require(orb != nullptr, "METAR ORB missing");
        if (orb == nullptr) return;
        require(orb->label.empty(),
                "successful ORB must remove METAR title");
        require(orb->airportIcao == airport &&
                    orb->categoryText == category,
                "successful ORB must contain exact ICAO/category lines");
        require(orb->stateText.empty() &&
                    orb->selectedIndicator.empty(),
                "successful ORB contains forbidden state/open text");
        require(orb->tone == tone && !orb->neutral,
                "successful ORB tone mismatch");
    };

    if (probe == "correction_sendrequest_completion_callback") {
        require(Step4FileContains(
                    "modules/metar/src/VatsimMetarClient.cpp",
                    "WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE"),
                "send-request-complete callback flag is not registered");
        require(!Step4FileContains(
                    "modules/metar/src/VatsimMetarClient.cpp",
                    "WINHTTP_CALLBACK_FLAG_SEND_REQUEST |"),
                "legacy send-request callback flag remains registered");
        require(Step4FileContains(
                    "modules/metar/src/VatsimMetarClient.cpp",
                    "WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE"),
                "send completion state is not awaited");
    } else if (probe == "correction_real_winhttp_loopback_success") {
#if defined(_WIN32)
        const auto run = RunStep4LoopbackLifecycle({
            200,
            "[{\"id\":\"KDFW\",\"metar\":\"KDFW 271951Z 18010KT 10SM FEW050\"}]",
            false});
        const auto requiredProgress =
            BrainMetarSendCompletionObserved |
            BrainMetarResponseHeadersReceived |
            BrainMetarHttp200Accepted |
            BrainMetarPayloadReadComplete |
            BrainMetarJsonAccepted;
        require(run.dispatched && run.terminal && run.disposition,
                "loopback lifecycle did not dispatch, harvest, and dispose");
        require(run.requestPath == "/KDFW?format=json",
                "loopback request path mismatch");
        require(run.responseCompleted,
                "loopback peer did not complete response");
        require(run.terminalDiagnostic.status == BrainMetarWorkerStatus::Success &&
                    run.terminalDiagnostic.terminalStage ==
                        BrainMetarTransportStage::Completed &&
                    run.terminalDiagnostic.httpStatus == 200 &&
                    run.terminalDiagnostic.source == "VATSIM_METAR" &&
                    run.terminalDiagnostic.stationIcao == "KDFW" &&
                    (run.terminalDiagnostic.transportProgress & requiredProgress) ==
                        requiredProgress,
                "successful WinHTTP terminal lifecycle is incomplete");
        require(run.terminalDiagnostic.request.airportIcao == "KDFW" &&
                    run.parseCount == 1 && run.primaryValid &&
                    run.category == BrainMetarFlightCategory::Vfr &&
                    run.historyMutationCount == 1 &&
                    run.dispositionDiagnostic.accepted &&
                    run.dispositionDiagnostic.parsingAttempted &&
                    run.dispositionDiagnostic.acceptedCategory ==
                        BrainMetarFlightCategory::Vfr,
                "loopback fact did not reach one brain parse and acceptance");
        require(run.elapsedMs <= 2'000 && !run.shutdown.running &&
                    run.shutdown.handlesClosed && run.shutdown.callbacksClosed,
                "loopback success exceeded bound or leaked worker state");
        std::cout << "STEP4_CORRECTION_LOOPBACK_SUCCESS: elapsed_ms="
                  << run.elapsedMs << " path=" << run.requestPath
                  << " parse_count=" << run.parseCount << "\n";
#else
        require(false, "real WinHTTP loopback proof requires Windows");
#endif
    } else if (probe == "correction_real_winhttp_http_failure_ledger") {
#if defined(_WIN32)
        const auto run = RunStep4LoopbackLifecycle({503, "{}", false});
        require(run.terminal && run.disposition &&
                    run.terminalDiagnostic.status ==
                        BrainMetarWorkerStatus::HttpFailure &&
                    run.terminalDiagnostic.terminalStage ==
                        BrainMetarTransportStage::HttpStatus &&
                    run.terminalDiagnostic.httpStatus == 503 &&
                    run.terminalDiagnostic.diagnostic ==
                        "http-status-rejected" &&
                    !run.dispositionDiagnostic.accepted &&
                    !run.dispositionDiagnostic.parsingAttempted &&
                    run.parseCount == 0,
                "HTTP failure ledger is incomplete or attempted parsing");
#else
        require(false, "real WinHTTP loopback proof requires Windows");
#endif
    } else if (probe == "correction_real_winhttp_json_failure_ledger") {
#if defined(_WIN32)
        const auto run = RunStep4LoopbackLifecycle({200, "{malformed", false});
        require(run.terminal && run.disposition &&
                    run.terminalDiagnostic.status ==
                        BrainMetarWorkerStatus::JsonRejected &&
                    run.terminalDiagnostic.terminalStage ==
                        BrainMetarTransportStage::JsonValidation &&
                    run.terminalDiagnostic.diagnostic == "malformed-json" &&
                    !run.dispositionDiagnostic.accepted &&
                    !run.dispositionDiagnostic.parsingAttempted &&
                    run.parseCount == 0,
                "JSON failure ledger is incomplete or attempted parsing");
#else
        require(false, "real WinHTTP loopback proof requires Windows");
#endif
    } else if (probe == "correction_transport_timeout_ledger") {
        const struct {
            BrainMetarTransportStage stage;
            BrainMetarWinHttpOperation operation;
            BrainMetarWorkerStatus status;
            const char* reason;
        } diagnosticCases[]{
            {BrainMetarTransportStage::Startup,
             BrainMetarWinHttpOperation::OpenSession,
             BrainMetarWorkerStatus::TransportFailure, "startup-failure"},
            {BrainMetarTransportStage::SendStart,
             BrainMetarWinHttpOperation::SendRequest,
             BrainMetarWorkerStatus::TransportFailure, "send-start-failure"},
            {BrainMetarTransportStage::ReceiveStart,
             BrainMetarWinHttpOperation::ReceiveResponse,
             BrainMetarWorkerStatus::TransportFailure, "receive-start-failure"},
            {BrainMetarTransportStage::ResponseHeaders,
             BrainMetarWinHttpOperation::ReceiveResponse,
             BrainMetarWorkerStatus::TransportFailure, "header-failure"},
            {BrainMetarTransportStage::DataAvailability,
             BrainMetarWinHttpOperation::QueryDataAvailable,
             BrainMetarWorkerStatus::TransportFailure,
             "data-availability-failure"},
            {BrainMetarTransportStage::Read,
             BrainMetarWinHttpOperation::ReadData,
             BrainMetarWorkerStatus::TransportFailure, "read-failure"},
            {BrainMetarTransportStage::PayloadValidation,
             BrainMetarWinHttpOperation::ReadData,
             BrainMetarWorkerStatus::PayloadRejected,
             "payload-bound-rejection"},
            {BrainMetarTransportStage::StationValidation,
             BrainMetarWinHttpOperation::None,
             BrainMetarWorkerStatus::WrongStation,
             "wrong-station-rejection"},
        };
        for (const auto& item : diagnosticCases) {
            Step4Fixture ledger;
            ledger.Cycle();
            BrainMetarWorkerFact ledgerFact;
            ledgerFact.request = ledger.worker.requests.back();
            ledgerFact.status = item.status;
            ledgerFact.terminalStage = item.stage;
            ledgerFact.winHttpOperation = item.operation;
            ledgerFact.winHttpError = 123;
            ledgerFact.diagnostic = item.reason;
            ledger.worker.ready = ledgerFact;
            ledger.worker.running = false;
            const auto ledgerOutput = ledger.Cycle(1);
            require(ledgerOutput.terminalDiagnostic.available &&
                        ledgerOutput.terminalDiagnostic.terminalStage ==
                            item.stage &&
                        ledgerOutput.terminalDiagnostic.winHttpOperation ==
                            item.operation &&
                        ledgerOutput.terminalDiagnostic.status == item.status &&
                        ledgerOutput.terminalDiagnostic.diagnostic == item.reason &&
                        ledgerOutput.dispositionDiagnostic.available &&
                        !ledgerOutput.dispositionDiagnostic.accepted &&
                        !ledgerOutput.dispositionDiagnostic.parsingAttempted &&
                        ledger.state.metar.parseCount == 0,
                    std::string("terminal diagnostic matrix mismatch: ") +
                        item.reason);
        }
        Step4Fixture f;
        const auto dispatch = f.Cycle();
        BrainMetarWorkerFact fact;
        fact.request = f.worker.requests.back();
        fact.status = BrainMetarWorkerStatus::TransportFailure;
        fact.terminalStage = BrainMetarTransportStage::SendCompletion;
        fact.winHttpOperation = BrainMetarWinHttpOperation::SendRequest;
        fact.winHttpError = ERROR_TIMEOUT;
        fact.diagnostic = "send-completion-timeout";
        fact.completedMonotonicMs = f.input.monotonicMs + 5'000;
        fact.networkElapsedUs = 5'000'000;
        f.worker.ready = fact;
        f.worker.running = false;
        const auto terminal = f.Cycle(5'000);
        require(dispatch.dispatchDiagnostic.available &&
                    terminal.terminalDiagnostic.available &&
                    terminal.terminalDiagnostic.terminalStage ==
                        BrainMetarTransportStage::SendCompletion &&
                    terminal.terminalDiagnostic.diagnostic ==
                        "send-completion-timeout" &&
                    terminal.terminalDiagnostic.winHttpError == ERROR_TIMEOUT,
                "send-completion timeout stage was not preserved");
        require(terminal.dispositionDiagnostic.available &&
                    !terminal.dispositionDiagnostic.accepted &&
                    !terminal.dispositionDiagnostic.parsingAttempted &&
                    f.state.metar.parseCount == 0 &&
                    f.state.metar.historyMutationCount == 0,
                "transport timeout reached parser or mutated history");
    } else if (probe == "correction_parser_rejection_ledger") {
        Step4Fixture f;
        f.Cycle();
        BrainMetarWorkerFact fact;
        fact.request = f.worker.requests.back();
        fact.status = BrainMetarWorkerStatus::Success;
        fact.stationIcao = "KDFW";
        fact.rawMetar = "KDFW RMK TRUNCATED";
        fact.httpStatus = 200;
        fact.terminalStage = BrainMetarTransportStage::Completed;
        fact.diagnostic = "vatsim-metar-accepted";
        f.worker.ready = fact;
        f.worker.running = false;
        const auto output = f.Cycle(1);
        require(output.terminalDiagnostic.available &&
                    output.terminalDiagnostic.status ==
                        BrainMetarWorkerStatus::Success &&
                    output.dispositionDiagnostic.available &&
                    !output.dispositionDiagnostic.accepted &&
                    output.dispositionDiagnostic.parsingAttempted &&
                    output.dispositionDiagnostic.parserReason ==
                        "observation-time-missing" &&
                    f.state.metar.parseCount == 1 &&
                    f.state.metar.historyMutationCount == 0,
                "parser rejection disposition ledger mismatch");
    } else if (probe == "correction_orb_startup_metar_only") {
        BrainOwnedRuntimeState state;
        EnableBrainOwnedAccessoryRuntime(&state);
        state.metar.initialized = true;
        requireNeutralMetarOrb(&state);
    } else if (probe == "correction_orb_pending_metar_only") {
        Step4Fixture f;
        f.Cycle();
        requireNeutralMetarOrb(&f.state);
    } else if (probe == "correction_orb_unavailable_metar_only") {
        Step4Fixture f;
        f.Cycle();
        f.worker.Complete(BrainMetarWorkerStatus::TransportFailure);
        f.Cycle(1);
        requireNeutralMetarOrb(&f.state);
    } else if (probe == "correction_orb_stale_metar_only") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        f.state.metar.visibleState = BrainMetarVisibleState::Stale;
        ++f.state.metar.presentationGeneration;
        requireNeutralMetarOrb(&f.state);
    } else if (probe == "correction_orb_success_exact_two_lines") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        requireSuccessfulMetarOrb(
            &f.state, "KDFW", "VFR",
            BrainOwnedAccessoryOrbPresentation::Tone::Green);
    } else if (probe == "correction_orb_cached_success_exact_two_lines") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        f.state.metar.visibleState = BrainMetarVisibleState::Cached;
        ++f.state.metar.presentationGeneration;
        requireSuccessfulMetarOrb(
            &f.state, "KDFW", "VFR",
            BrainOwnedAccessoryOrbPresentation::Tone::Green);
    } else if (probe == "correction_orb_selected_has_no_open_text") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        Step4SelectDrawer(&f.state, BrainOwnedAccessoryDrawerId::Metar);
        requireSuccessfulMetarOrb(
            &f.state, "KDFW", "VFR",
            BrainOwnedAccessoryOrbPresentation::Tone::Green);
    } else if (probe == "correction_lookup_never_changes_orb") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        requireSuccessfulMetarOrb(
            &f.state, "KDFW", "VFR",
            BrainOwnedAccessoryOrbPresentation::Tone::Green);
        f.SubmitLookup("KABQ");
        requireSuccessfulMetarOrb(
            &f.state, "KDFW", "VFR",
            BrainOwnedAccessoryOrbPresentation::Tone::Green);
        f.AcceptLookup(lookupMvfr);
        requireSuccessfulMetarOrb(
            &f.state, "KDFW", "VFR",
            BrainOwnedAccessoryOrbPresentation::Tone::Green);
    } else if (probe == "ifr_departure_primary_only" ||
        probe == "ifr_departure_no_arrival_prefetch") {
        Step4Fixture f;
        f.Cycle();
        require(f.worker.requests.size() == 1, "one departure request expected");
        require(f.worker.requests[0].airportIcao == "KDFW", "departure must be KDFW");
        require(f.worker.requests[0].purpose == BrainMetarRequestPurpose::PrimaryTarget,
                "first request must be primary target");
        f.AcceptPrimary(primaryVfr);
        f.Cycle(59'000);
        require(f.worker.requests.size() == 1, "arrival must not be prefetched");
    } else if (probe == "departure_enroute_primary_switch" ||
               probe == "ifr_arrival_primary_stable") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        f.input.workflowStage = WorkflowStage::Enroute;
        f.Cycle(1);
        require(f.state.metar.primaryAirportIcao == "KSAN", "enroute target must be arrival");
        require(f.worker.requests.back().airportIcao == "KSAN", "arrival request expected");
        f.worker.Complete(BrainMetarWorkerStatus::Success, "KSAN", primaryIfr);
        f.Cycle(1);
        const auto count = f.worker.requests.size();
        f.input.workflowStage = WorkflowStage::Arrival;
        f.Cycle(1);
        require(f.state.metar.primaryAirportIcao == "KSAN", "arrival target must remain KSAN");
        require(f.worker.requests.size() == count, "arrival transition must not duplicate request");
    } else if (probe == "vfr_departure_primary" ||
               probe == "vfr_brain_committed_departure_source") {
        Step4Fixture f;
        f.input.operatingMode = BrainOwnedOperatingMode::VFR;
        f.input.flightContext = {};
        f.input.flightPlan.available = true;
        f.input.flightPlan.departureIcao = "KAPA";
        f.input.flightPlan.departureSource = AirportSource::CurrentLocation;
        f.input.flightPlan.hasDepartureCoordinates = true;
        f.input.flightPlan.departureLatDeg = 39.57;
        f.input.flightPlan.departureLonDeg = -104.85;
        f.Cycle();
        require(f.state.metar.primaryAirportIcao == "KAPA", "committed VFR departure expected");
        require(f.state.metar.primaryLatchedFromVfr, "VFR primary must latch");
        f.input.flightPlan.departureIcao = "KBJC";
        f.input.flightPlan.departureLatDeg = 39.91;
        f.input.flightPlan.departureLonDeg = -105.12;
        f.Cycle(1);
        require(f.state.metar.primaryAirportIcao == "KAPA", "VFR target must not follow aircraft");
    } else if (probe == "vfr_no_nearby_scan" ||
               probe == "vfr_missing_departure_unknown") {
        Step4Fixture f;
        f.input.operatingMode = BrainOwnedOperatingMode::VFR;
        f.input.flightContext = {};
        f.input.flightPlan = {};
        f.Cycle();
        require(f.state.metar.primaryAirportIcao.empty(), "unproved VFR target must be empty");
        require(f.worker.requests.empty(), "no VFR scanning request permitted");
        require(f.state.metar.visibleState == BrainMetarVisibleState::Unknown ||
                    f.state.metar.visibleState == BrainMetarVisibleState::Unavailable,
                "VFR missing departure must be unknown/unavailable");
    } else if (probe == "lookup_preserves_primary" ||
               probe == "lookup_orb_primary_authority") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        f.SubmitLookup("kabq");
        f.AcceptLookup(lookupMvfr);
        require(f.state.metar.primaryAirportIcao == "KDFW", "lookup must preserve primary");
        const auto presentation = ProjectBrainOwnedAccessoryPresentation(&f.state, 1, nullptr);
        const auto* orb = Step4MetarOrb(presentation);
        require(orb && orb->airportIcao == "KDFW" && orb->categoryText == "VFR",
                "ORB must remain primary authority");
    } else if (probe == "lookup_spotlight_activation" ||
               probe == "lookup_pending_success_to_spotlight") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        f.SubmitLookup("KABQ");
        f.AcceptLookup(lookupMvfr);
        require(f.state.metar.transientPresentation ==
                    BrainMetarTransientPresentation::LookupSpotlight,
                "lookup success must spotlight");
        require(Step4HasTitle(&f.state, "METAR LOOKUP — KABQ"),
                "spotlight title missing");
    } else if (probe == "lookup_spotlight_expiry_no_duplicate") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        f.SubmitLookup("KABQ");
        f.AcceptLookup(lookupMvfr);
        const auto before = f.state.accessory.histories[0].entries.size();
        f.Cycle(8'000);
        require(f.state.metar.transientPresentation == BrainMetarTransientPresentation::None,
                "spotlight must expire");
        require(f.state.accessory.histories[0].entries.size() == before,
                "spotlight expiry must not duplicate history");
    } else if (probe == "primary_update_preempts_spotlight" ||
               probe == "primary_state_change_preempts_spotlight") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        f.SubmitLookup("KABQ");
        f.AcceptLookup(lookupMvfr);
        f.state.metar.nextPrimaryEligibleMonotonicMs = f.input.monotonicMs;
        f.Cycle();
        f.worker.Complete(BrainMetarWorkerStatus::Success, "KDFW",
                          "SPECI KDFW 271954Z 18012KT 1/2SM VV003");
        f.Cycle(1);
        require(f.state.metar.transientPresentation == BrainMetarTransientPresentation::None,
                "new primary observation must preempt spotlight");
        require(f.state.metar.primaryObservation.category == BrainMetarFlightCategory::Lifr,
                "updated primary must publish");

        Step4Fixture target;
        target.AcceptPrimary(primaryVfr);
        target.SubmitLookup("KABQ");
        target.AcceptLookup(lookupMvfr);
        target.input.workflowStage = WorkflowStage::Enroute;
        target.Cycle(1);
        require(target.state.metar.transientPresentation ==
                    BrainMetarTransientPresentation::None,
                "target change must preempt spotlight");

        Step4Fixture stale;
        stale.AcceptPrimary(primaryVfr);
        stale.SubmitLookup("KABQ");
        stale.AcceptLookup(lookupMvfr);
        stale.Cycle(stale.state.metar.freshUntilMonotonicMs -
                    stale.input.monotonicMs);
        require(stale.state.metar.transientPresentation ==
                    BrainMetarTransientPresentation::None,
                "stale transition must preempt spotlight");
    } else if (probe == "lookup_truthful_newest_history") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        f.SubmitLookup("KABQ");
        f.AcceptLookup(lookupMvfr);
        const auto& entries = f.state.accessory.histories[0].entries;
        require(entries.size() == 2, "two truthful history entries expected");
        require(entries[0].title.find("KABQ") != std::string::npos,
                "newest observation must be first");
        require(entries[1].title.find("KDFW") != std::string::npos,
                "old primary must remain second");
    } else if (probe == "lookup_no_periodic_refresh") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        f.SubmitLookup("KABQ");
        f.AcceptLookup(lookupMvfr);
        f.Cycle(60'000);
        require(f.worker.requests.back().airportIcao == "KDFW",
                "only primary may refresh");
        require(std::count_if(f.worker.requests.begin(), f.worker.requests.end(),
                    [](const auto& request) { return request.airportIcao == "KABQ"; }) == 1,
                "lookup airport must be one-shot");
    } else if (probe == "lookup_replacement_invalidates_pending") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        f.SubmitLookup("KABQ");
        f.Cycle();
        const auto old = f.worker.requests.back();
        f.SubmitLookup("KPHX");
        f.worker.CompleteRequest(old, BrainMetarWorkerStatus::Success, "KABQ", lookupMvfr);
        const auto rejected = f.Cycle(1);
        require(rejected.completionRejected, "older lookup completion must reject");
        require(f.worker.requests.back().airportIcao == "KPHX",
                "newest lookup must dispatch next");
    } else if (probe == "target_switch_rejects_stale_completion") {
        Step4Fixture f;
        f.Cycle();
        const auto departureRequest = f.worker.requests.back();
        f.input.workflowStage = WorkflowStage::Enroute;
        f.Cycle(1);
        f.worker.CompleteRequest(departureRequest, BrainMetarWorkerStatus::Success,
                                 "KDFW", primaryVfr);
        const auto rejected = f.Cycle(1);
        require(rejected.completionRejected, "obsolete departure completion must reject");
        require(f.state.metar.primaryAirportIcao == "KSAN", "arrival target must survive");
    } else if (probe == "strict_icao_validation") {
        std::string normalized;
        require(NormalizeStrictMetarIcao(" kjfk ", &normalized) && normalized == "KJFK",
                "valid ICAO normalization failed");
        for (const auto& invalid : {"", "ALL", "all", "K*", "KJFK,KLAX",
                                    "../K", "KJ/F", "KJFKX", " K J "}) {
            require(!NormalizeStrictMetarIcao(invalid, &normalized),
                    std::string("invalid ICAO accepted: ") + invalid);
        }
    } else if (probe == "official_vatsim_url_only") {
        require(BuildVatsimMetarRequestPath("KJFK") == L"/KJFK?format=json",
                "official request path mismatch");
        require(BuildVatsimMetarRequestPath("../K").empty(),
                "unsafe request path accepted");
        require(Step4FileContains("modules/metar/src/VatsimMetarClient.cpp",
                                  "metar.vatsim.net"), "official host missing");
        require(!Step4FileContains("modules/metar/src/VatsimMetarClient.cpp", "noaa"),
                "alternate source present");
    } else if (probe == "vatsim_json_extraction") {
        const auto result = ExtractVatsimMetarJson(
            "KJFK", R"([{"id":"KJFK","metar":" KJFK 271951Z 18010KT 10SM FEW050 "}])");
        require(result.accepted && result.stationIcao == "KJFK",
                "valid JSON must extract matching station");
        require(result.rawMetar == "KJFK 271951Z 18010KT 10SM FEW050",
                "raw METAR trim mismatch");
    } else if (probe == "transport_response_rejections") {
        require(!ExtractVatsimMetarJson("KJFK", "").accepted, "empty payload accepted");
        require(!ExtractVatsimMetarJson("KJFK", "{").accepted, "malformed JSON accepted");
        require(!ExtractVatsimMetarJson(
                    "KJFK", R"([{"id":"KLAX","metar":"KLAX 271951Z 10SM SKC"}])").accepted,
                "wrong station accepted");
        require(!ExtractVatsimMetarJson("KJFK", std::string(65'537, 'x')).accepted,
                "oversized payload accepted");
        require(!ExtractVatsimMetarJson("KJFK", "[]").accepted,
                "empty response accepted");
        Step4Fixture f;
        f.Cycle();
        f.worker.Complete(BrainMetarWorkerStatus::Success, "KLAX",
                          "KLAX 271951Z 18010KT 10SM SKC");
        f.Cycle(1);
        require(!f.state.metar.primaryObservation.valid &&
                    f.state.metar.sourceHealth == BrainMetarSourceHealth::Failed,
                "brain accepted mismatched success fact");
    } else if (probe == "metar_speci_parsing" ||
               probe == "vatsim_raw_optional_prefixes") {
        const auto metar = ParseBrainOwnedMetarReport(
            "KDFW", "METAR KDFW 271951Z 18010KT 10SM FEW050", 1'787'860'800);
        const auto speci = ParseBrainOwnedMetarReport(
            "KDFW", "SPECI KDFW 271952Z 18010KT 2SM BKN008", 1'787'860'800);
        const auto bare = ParseBrainOwnedMetarReport(
            "KDFW", "KDFW 271953Z 18010KT 4SM SCT020", 1'787'860'800);
        const auto corrected = ParseBrainOwnedMetarReport(
            "KDFW", "METAR COR KDFW 271954Z 18010KT 10SM SKC", 1'787'860'800);
        require(metar.valid && !metar.speci, "METAR parsing failed");
        require(speci.valid && speci.speci, "SPECI parsing failed");
        require(bare.valid && corrected.valid, "optional prefix/station parsing failed");
    } else if (probe == "observation_time_month_boundary") {
        const auto septemberReference = static_cast<std::int64_t>(1'788'220'920);
        const auto august = ParseBrainOwnedMetarReport(
            "KJFK", "KJFK 312359Z 18010KT 10SM SKC", septemberReference);
        require(august.valid, "previous-month observation must resolve");
        require(septemberReference - august.observationUnixSeconds < 300,
                "month-boundary observation age incorrect");
        const auto future = ParseBrainOwnedMetarReport(
            "KJFK", "KJFK 020100Z 18010KT 10SM SKC", septemberReference);
        require(!future.valid, "implausibly future observation accepted");
    } else if (probe == "category_threshold_boundaries") {
        struct Case { const char* weather; BrainMetarFlightCategory category; };
        const Case cases[]{
            {"10SM BKN031", BrainMetarFlightCategory::Vfr},
            {"5SM BKN030", BrainMetarFlightCategory::Mvfr},
            {"3SM BKN010", BrainMetarFlightCategory::Mvfr},
            {"2SM BKN009", BrainMetarFlightCategory::Ifr},
            {"1SM BKN005", BrainMetarFlightCategory::Ifr},
            {"M1/4SM VV004", BrainMetarFlightCategory::Lifr}};
        for (const auto& item : cases) {
            const auto parsed = ParseBrainOwnedMetarReport(
                "KDFW", std::string("KDFW 271951Z 18010KT ") + item.weather,
                1'787'860'800);
            require(parsed.valid && parsed.category == item.category,
                    std::string("threshold mismatch: ") + item.weather);
        }
    } else if (probe == "visibility_formats") {
        for (const auto& weather : {"10SM SKC", "1 1/2SM BKN020", ".5SM VV004",
                                    "1600 BKN020", "9999 NSC", "CAVOK"}) {
            const auto parsed = ParseBrainOwnedMetarReport(
                "KDFW", std::string("KDFW 271951Z 18010KT ") + weather,
                1'787'860'800);
            require(parsed.valid && parsed.visibilityKnown,
                    std::string("visibility not parsed: ") + weather);
        }
        const auto rvr = ParseBrainOwnedMetarReport(
            "KDFW", "KDFW 271951Z 18010KT R18/0600FT BKN008", 1'787'860'800);
        require(rvr.valid && !rvr.visibilityKnown,
                "RVR must not become prevailing visibility");
    } else if (probe == "ceiling_rules") {
        const auto few = ParseBrainOwnedMetarReport(
            "KDFW", "KDFW 271951Z 18010KT 10SM FEW005 SCT009", 1'787'860'800);
        const auto broken = ParseBrainOwnedMetarReport(
            "KDFW", "KDFW 271951Z 18010KT 10SM FEW002 BKN008 OVC020", 1'787'860'800);
        const auto vv = ParseBrainOwnedMetarReport(
            "KDFW", "KDFW 271951Z 18010KT 1SM VV004", 1'787'860'800);
        require(few.noCeilingProven && few.category == BrainMetarFlightCategory::Vfr,
                "FEW/SCT must not form ceiling");
        require(broken.ceilingFeet == 800 && broken.category == BrainMetarFlightCategory::Ifr,
                "lowest BKN/OVC ceiling incorrect");
        require(vv.ceilingFeet == 400 && vv.category == BrainMetarFlightCategory::Lifr,
                "vertical visibility ceiling incorrect");
    } else if (probe == "ambiguous_weather_unknown") {
        for (const auto& raw : {"KDFW 271951Z 18010KT 10SM",
                                "KDFW 271951Z 18010KT BKN///",
                                "KDFW 271951Z 18010KT 10SM BKN///",
                                "KDFW 271951Z RMK 1/4SM VV001"}) {
            const auto parsed = ParseBrainOwnedMetarReport("KDFW", raw, 1'787'860'800);
            require(!parsed.valid || parsed.category == BrainMetarFlightCategory::Unknown,
                    std::string("ambiguous report classified: ") + raw);
        }
    } else if (probe == "changed_content_history_once") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        const auto hiddenBefore = ProjectBrainOwnedAccessoryPreparation(
            &f.state, BrainOwnedAccessoryDrawerId::Metar, nullptr);
        f.state.metar.nextPrimaryEligibleMonotonicMs = f.input.monotonicMs;
        f.Cycle();
        f.worker.Complete(BrainMetarWorkerStatus::Success, "KDFW",
                          "SPECI KDFW 271954Z 18010KT 4SM BKN020");
        const auto changedOutput = f.Cycle(1);
        require(changedOutput.acceptedNetworkElapsedUs == 2'500'000 &&
                    changedOutput.simulatorThreadElapsedUs >= 0,
                "network wait and simulator-thread timing were not separated");
        require(f.state.accessory.histories[0].entries.size() == 2,
                "changed observation must enter history once");
        BrainOwnedAccessoryProjectionCounters hiddenCounters;
        const auto hiddenAfter = ProjectBrainOwnedAccessoryPreparation(
            &f.state, BrainOwnedAccessoryDrawerId::Metar, &hiddenCounters);
        require(hiddenBefore.snapshot && hiddenAfter.snapshot &&
                    hiddenBefore.snapshot->snapshotIdentity ==
                        hiddenAfter.snapshot->snapshotIdentity &&
                    hiddenCounters.snapshotBuilds == 0,
                "hidden METAR content must not prepare or wrap");
        Step4SelectDrawer(&f.state, BrainOwnedAccessoryDrawerId::Metar);
        BrainOwnedAccessoryProjectionCounters openedCounters;
        const auto opened = ProjectBrainOwnedAccessoryPreparation(
            &f.state, BrainOwnedAccessoryDrawerId::Metar, &openedCounters);
        require(opened.snapshot && hiddenAfter.snapshot &&
                    opened.snapshot->snapshotIdentity !=
                        hiddenAfter.snapshot->snapshotIdentity &&
                    openedCounters.snapshotBuilds == 1,
                "opening METAR must prepare latest changed content once");
        std::cout << "STEP4_TIMING_SEPARATION: network_us="
                  << changedOutput.acceptedNetworkElapsedUs
                  << " simulator_thread_us="
                  << changedOutput.simulatorThreadElapsedUs << "\n";
    } else if (probe == "unchanged_content_zero_publication" ||
               probe == "identical_content_health_only") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        const auto parseCount = f.state.metar.parseCount;
        const auto historyCount = f.state.metar.historyMutationCount;
        const auto contentGeneration = f.state.metar.contentGeneration;
        const auto presentationGeneration = f.state.metar.presentationGeneration;
        f.state.metar.sourceHealth = BrainMetarSourceHealth::Failed;
        f.state.metar.visibleState = BrainMetarVisibleState::Cached;
        f.state.metar.nextPrimaryEligibleMonotonicMs = f.input.monotonicMs;
        f.Cycle();
        f.worker.Complete(BrainMetarWorkerStatus::Success, "KDFW", primaryVfr);
        const auto output = f.Cycle(1);
        require(f.state.metar.parseCount == parseCount, "identical content reparsed");
        require(f.state.metar.historyMutationCount == historyCount,
                "identical content mutated history");
        require(f.state.metar.contentGeneration == contentGeneration,
                "identical content changed content generation");
        require(f.state.metar.sourceHealth == BrainMetarSourceHealth::Healthy &&
                    output.sourceHealthChanged,
                "identical success must recover source health");
        require(f.state.metar.presentationGeneration >= presentationGeneration,
                "presentation generation regressed");
    } else if (probe == "speci_between_clock_boundaries") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        f.Cycle(60'000);
        f.worker.Complete(BrainMetarWorkerStatus::Success, "KDFW",
                          "SPECI KDFW 271952Z 18010KT 1/2SM VV003");
        const auto output = f.Cycle(1);
        require(output.contentChanged && f.state.metar.primaryObservation.speci,
                "changed SPECI must be accepted at revalidation");
    } else if (probe == "fresh_cache_transient_failure") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        f.state.metar.nextPrimaryEligibleMonotonicMs = f.input.monotonicMs;
        f.Cycle();
        f.worker.Complete(BrainMetarWorkerStatus::TransportFailure);
        f.Cycle(1);
        require(f.state.metar.visibleState == BrainMetarVisibleState::Cached,
                "fresh cache must survive transient failure");
        require(f.state.metar.primaryObservation.category == BrainMetarFlightCategory::Vfr,
                "fresh cached category must remain usable");
    } else if (probe == "stale_primary_gray_unknown" ||
               probe == "freshness_monotonic_deadline") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        f.input.utcUnixSeconds -= 86'400;
        f.Cycle(f.state.metar.freshUntilMonotonicMs - f.input.monotonicMs);
        const auto presentation = ProjectBrainOwnedAccessoryPresentation(&f.state, 1, nullptr);
        const auto* orb = Step4MetarOrb(presentation);
        require(f.state.metar.visibleState == BrainMetarVisibleState::Stale,
                "monotonic freshness deadline must stale");
        require(orb && orb->label == "METAR" &&
                    orb->airportIcao.empty() && orb->categoryText.empty() &&
                    orb->stateText.empty() &&
                    orb->selectedIndicator.empty() &&
                    orb->tone == BrainOwnedAccessoryOrbPresentation::Tone::Gray,
                "stale ORB must be neutral METAR only");
    } else if (probe == "history_isolation") {
        Step4Fixture f;
        BrainOwnedAccessoryHistoryEntryInput atis;
        atis.drawer = BrainOwnedAccessoryDrawerId::Atis;
        atis.stableKey = "ATIS|KSAN|A";
        atis.title = "KSAN ATIS A";
        atis.body = "ATIS BODY";
        AcceptBrainOwnedAccessoryHistoryEntry(&f.state, atis);
        BrainOwnedAccessoryHistoryEntryInput pdc = atis;
        pdc.drawer = BrainOwnedAccessoryDrawerId::Pdc;
        pdc.stableKey = "PDC|KSAN|1";
        AcceptBrainOwnedAccessoryHistoryEntry(&f.state, pdc);
        f.AcceptPrimary(primaryVfr);
        require(f.state.accessory.histories[0].entries.size() == 1 &&
                    f.state.accessory.histories[1].entries.size() == 1 &&
                    f.state.accessory.histories[2].entries.size() == 1,
                "METAR/ATIS/PDC histories must remain isolated");
    } else if (probe == "lifecycle_boundaries") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        const auto before = f.state.accessory.histories[0].entries.size();
        BrainOwnedAsyncWorkerBindings bindings;
        bindings.metar = &f.worker;
        ApplyBrainOwnedAsyncWorkerLifecycleBoundary(&f.state, bindings, false);
        ResetBrainOwnedRuntimeCachePreservingFlightContext(&f.state);
        require(f.state.accessory.histories[0].entries.size() == before &&
                    f.state.metar.primaryObservation.valid,
                "disable must preserve accepted cache/history");
        ApplyBrainOwnedAsyncWorkerLifecycleBoundary(&f.state, bindings, true);
        ResetBrainOwnedAccessoryForSessionReset(&f.state);
        require(!f.state.metar.primaryObservation.valid &&
                    f.state.accessory.histories[0].entries.empty(),
                "hard boundary must clear METAR state/history");

        Step4Fixture cleanup;
        cleanup.Cycle();
        cleanup.worker.Complete(
            BrainMetarWorkerStatus::Success, "KDFW", primaryVfr);
        BrainOwnedAsyncWorkerBindings cleanupBindings;
        cleanupBindings.metar = &cleanup.worker;
        const auto parseBefore = cleanup.state.metar.parseCount;
        const auto historyBefore = cleanup.state.metar.historyMutationCount;
        const auto presentationBefore =
            cleanup.state.metar.presentationGeneration;
        const auto cleanupSnapshot =
            ApplyBrainOwnedAsyncWorkerLifecycleBoundary(
                &cleanup.state, cleanupBindings, false);
        require(cleanupSnapshot.terminalFactDrained &&
                    cleanupSnapshot.terminalDiagnostic.available &&
                    cleanupSnapshot.dispositionDiagnostic.available &&
                    !cleanupSnapshot.dispositionDiagnostic.accepted &&
                    !cleanupSnapshot.dispositionDiagnostic.parsingAttempted &&
                    !cleanupSnapshot.dispositionDiagnostic.historyMutated &&
                    !cleanupSnapshot.dispositionDiagnostic.presentationChanged &&
                    cleanup.state.metar.parseCount == parseBefore &&
                    cleanup.state.metar.historyMutationCount == historyBefore &&
                    cleanup.state.metar.presentationGeneration ==
                        presentationBefore &&
                    !cleanup.state.metar.primaryObservation.valid,
                "lifecycle drain parsed or accepted cancelled-boundary weather");
    } else if (probe == "worker_prompt_cancel_join") {
        long long maximumCancellationMs = 0;
        for (int phase = 0; phase < 5; ++phase) {
            std::atomic<bool> entered{false};
            xvatsim::modules::metar::VatsimMetarClient client(
                [&](const BrainMetarWorkerRequest& request, const auto& cancelled) {
                    entered.store(true, std::memory_order_release);
                    while (!cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    BrainMetarWorkerFact fact;
                    fact.request = request;
                    fact.status = BrainMetarWorkerStatus::Cancelled;
                    fact.diagnostic = "cancelled-fixture-phase-" + std::to_string(phase);
                    return fact;
                });
            BrainMetarWorkerRequest request;
            request.airportIcao = "KDFW";
            request.requestId = static_cast<std::uint64_t>(phase + 1);
            require(client.Start(request), "cancellation fixture failed to start");
            const auto waitStart = std::chrono::steady_clock::now();
            while (!entered.load(std::memory_order_acquire) &&
                   std::chrono::steady_clock::now() - waitStart <
                       std::chrono::milliseconds(100)) {
                std::this_thread::yield();
            }
            const auto started = std::chrono::steady_clock::now();
            client.CancelAndJoin();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            maximumCancellationMs = std::max(maximumCancellationMs, elapsed);
            BrainMetarWorkerFact drained;
            const bool terminalDrained = client.TryHarvest(&drained);
            const auto shutdown = client.ShutdownSnapshot();
            require(elapsed <= 500 && !shutdown.running && shutdown.handlesClosed &&
                        shutdown.callbacksClosed && terminalDrained &&
                        drained.status == BrainMetarWorkerStatus::Cancelled,
                    "worker cancellation exceeded bound or leaked state");
        }
#if defined(_WIN32)
        Step4LoopbackHttpPeer peer({200, {}, true});
        require(peer.Ready(), "stalled loopback peer failed to start");
        xvatsim::modules::metar::VatsimMetarClient::ProofEndpoint endpoint;
        endpoint.host = L"127.0.0.1";
        endpoint.port = peer.Port();
        endpoint.secure = false;
        xvatsim::modules::metar::VatsimMetarClient loopbackClient(endpoint);
        BrainMetarWorkerRequest loopbackRequest;
        loopbackRequest.airportIcao = "KDFW";
        loopbackRequest.requestId = 100;
        require(loopbackClient.Start(loopbackRequest),
                "loopback WinHTTP request failed to start");
        const auto acceptDeadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!peer.Accepted() &&
               std::chrono::steady_clock::now() < acceptDeadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(peer.Accepted(),
                "loopback peer did not accept the WinHTTP request");
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        const auto loopbackStarted = std::chrono::steady_clock::now();
        loopbackClient.CancelAndJoin();
        const auto loopbackCancellationMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - loopbackStarted).count();
        maximumCancellationMs = std::max(
            maximumCancellationMs, loopbackCancellationMs);
        BrainMetarWorkerFact loopbackDrained;
        const bool loopbackTerminalDrained =
            loopbackClient.TryHarvest(&loopbackDrained);
        const auto loopbackShutdown = loopbackClient.ShutdownSnapshot();
        require(loopbackCancellationMs <= 500 &&
                    !loopbackShutdown.running &&
                    loopbackShutdown.handlesClosed &&
                    loopbackShutdown.callbacksClosed &&
                    loopbackTerminalDrained &&
                    loopbackDrained.status == BrainMetarWorkerStatus::Cancelled,
                "real WinHTTP loopback cancellation leaked or exceeded bound");
        std::cout << "STEP4_LOOPBACK_SHUTDOWN: accepted=true max_ms="
                  << loopbackCancellationMs << " limit_ms=500\n";
#endif
        std::cout << "STEP4_WORKER_SHUTDOWN: phases=5 max_ms="
                  << maximumCancellationMs << " limit_ms=500\n";
    } else if (probe == "warm_unchanged_zero_work") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        f.input.monotonicMs += 1;
        const auto before = f.state.metar;
        xvatsim::modules::overlay::AccessoryInputDispatchCoordinator
            idleDispatcher;
        const auto idleDispatchBefore = idleDispatcher.Snapshot();
        const auto presentation = ProjectBrainOwnedAccessoryPresentation(
            &f.state, 1, nullptr);
        xvatsim::modules::overlay::AccessoryLayoutInput layoutInput;
        layoutInput.screenWidth = 1920;
        layoutInput.screenHeight = 1080;
        layoutInput.windowLeft = 100;
        layoutInput.windowTop = 900;
        layoutInput.scale = 1.0f;
        layoutInput.cardAnimationProgress = 1.0f;
        layoutInput.drawerOpen = false;
        const auto layout = xvatsim::modules::overlay::ResolveAccessoryLayout(
            layoutInput);
        xvatsim::modules::overlay::AccessoryPresentationState presenter;
        xvatsim::modules::overlay::AccessoryPresentationUpdateInput update;
        update.presentation = presentation;
        update.layout = layout;
        update.mainCardProductionSignature = "step4-warm-main-card";
        update.measurementContext =
            xvatsim::modules::overlay::InitializeAccessoryTextMeasurement();
        require(update.measurementContext != nullptr,
                "warm proof measurement context unavailable");
        (void)xvatsim::modules::overlay::UpdateAccessoryPresentation(
            &presenter, update);
        BrainOwnedAccessoryProjectionCounters recurringProjection;
        std::uint64_t recurringMetarDiagnostics = 0;
        const auto brainStarted = std::chrono::steady_clock::now();
        for (int index = 0; index < 100'000; ++index) {
            const auto cycle = f.Cycle();
            recurringMetarDiagnostics +=
                cycle.dispatchDiagnostic.available ? 1U : 0U;
            recurringMetarDiagnostics +=
                cycle.terminalDiagnostic.available ? 1U : 0U;
            recurringMetarDiagnostics +=
                cycle.dispositionDiagnostic.available ? 1U : 0U;
            BrainOwnedAccessoryProjectionCounters counters;
            (void)ProjectBrainOwnedAccessoryPresentation(&f.state, 1, &counters);
            recurringProjection.historyVisits += counters.historyVisits;
            recurringProjection.entriesCopied += counters.entriesCopied;
            recurringProjection.snapshotBuilds += counters.snapshotBuilds;
        }
        const auto brainElapsedUs =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - brainStarted).count();
        const auto accessoryStarted = std::chrono::steady_clock::now();
        const auto warm =
            xvatsim::modules::overlay::RunUnchangedAccessoryPresentationUpdates(
                &presenter, update, 100'000);
        const auto accessoryElapsedUs =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - accessoryStarted).count();
        const auto idleDispatchAfter = idleDispatcher.Snapshot();
        require(f.state.metar.parseCount == before.parseCount &&
                    f.state.metar.fingerprintCount == before.fingerprintCount &&
                    f.state.metar.historyMutationCount == before.historyMutationCount &&
                    f.state.metar.contentGeneration == before.contentGeneration &&
                    f.state.metar.presentationGeneration == before.presentationGeneration &&
                    recurringMetarDiagnostics == 0,
                "warm unchanged cycles performed recurring content work");
        require(recurringProjection.historyVisits == 0 &&
                    recurringProjection.entriesCopied == 0 &&
                    recurringProjection.snapshotBuilds == 0,
                "warm unchanged cycles rebuilt brain presentation");
        require(warm.iterations == 100'000 &&
                    warm.delta.historyVisits == 0 &&
                    warm.delta.entryCopies == 0 &&
                    warm.delta.wrapVisits == 0 &&
                    warm.delta.mainCardRasterRequests == 0 &&
                    warm.delta.railRasterRequests == 0 &&
                    warm.delta.drawerRasterRequests == 0 &&
                    warm.delta.uploadRequests == 0 &&
                    warm.delta.snapshotPublications == 0,
                "warm unchanged cycles performed recurring accessory work");
        require(!idleDispatchAfter.inFlight &&
                    idleDispatchAfter.dispatchNotifications ==
                        idleDispatchBefore.dispatchNotifications &&
                    idleDispatchAfter.beginAttempts ==
                        idleDispatchBefore.beginAttempts &&
                    idleDispatchAfter.requestsBegun ==
                        idleDispatchBefore.requestsBegun &&
                    idleDispatchAfter.matchingDrawCompletions ==
                        idleDispatchBefore.matchingDrawCompletions &&
                    idleDispatchAfter.mismatchedDrawAttempts ==
                        idleDispatchBefore.mismatchedDrawAttempts,
                "warm unchanged cycles performed recurring input dispatch");
        std::cout << "STEP4_WARM_IDLE: cycles=100000 brain_us="
                  << brainElapsedUs << " accessory_us=" << accessoryElapsedUs
                  << " parse_delta=0 fingerprint_delta=0 history_delta=0"
                  << " wraps_delta=0 rasters_delta=0 uploads_delta=0"
                  << " input_dispatch_delta=0 terminal_diagnostics_delta=0"
                  << " publications_delta=0\n";
        xvatsim::modules::overlay::ShutdownAccessoryTextMeasurement(
            update.measurementContext);
    } else if (probe == "normal_binary_source_isolation") {
        require(!Step4FileContains("plugin/src/XVatsimPlugin.cpp", "VatsimMetarClient"),
                "plugin directly names concrete METAR client");
        require(!Step4FileContains("plugin/src/XVatsimPlugin.cpp", "metar.vatsim.net"),
                "plugin contains METAR endpoint policy");
        require(!Step4FileContains("modules/metar/src/VatsimMetarClient.cpp", "NOAA") &&
                    !Step4FileContains("modules/metar/src/VatsimMetarClient.cpp", "aviationweather"),
                "alternate weather source found");
        require(!Step4FileContains("plugin/CMakeLists.txt", "STEP4_METAR_FIXTURE"),
                "normal plugin includes Step 4 fixtures");
    } else if (probe == "request_priority_and_backoff") {
        Step4Fixture f;
        f.Cycle();
        f.worker.Complete(BrainMetarWorkerStatus::TransportFailure);
        f.Cycle(1);
        const auto firstEligible = f.state.metar.nextPrimaryEligibleMonotonicMs;
        require(firstEligible - f.input.monotonicMs == 120'000,
                "first backoff must be 120 seconds");
        f.Cycle(119'999);
        require(f.worker.requests.size() == 1, "request dispatched before backoff");
        f.Cycle(1);
        require(f.worker.requests.size() == 2, "request missing at backoff boundary");
        f.worker.Complete(BrainMetarWorkerStatus::TransportFailure);
        f.Cycle(1);
        require(f.state.metar.nextPrimaryEligibleMonotonicMs - f.input.monotonicMs == 240'000,
                "second backoff must be 240 seconds");
    } else if (probe == "orb_category_text_and_tone") {
        const struct { const char* raw; const char* text;
                       BrainOwnedAccessoryOrbPresentation::Tone tone; } cases[]{
            {"KDFW 271951Z 18010KT 10SM SKC", "VFR", BrainOwnedAccessoryOrbPresentation::Tone::Green},
            {"KDFW 271951Z 18010KT 4SM BKN020", "MVFR", BrainOwnedAccessoryOrbPresentation::Tone::Blue},
            {"KDFW 271951Z 18010KT 2SM BKN008", "IFR", BrainOwnedAccessoryOrbPresentation::Tone::Red},
            {"KDFW 271951Z 18010KT M1/4SM VV003", "LIFR", BrainOwnedAccessoryOrbPresentation::Tone::Magenta}};
        for (const auto& item : cases) {
            Step4Fixture f;
            f.AcceptPrimary(item.raw);
            const auto handle = ProjectBrainOwnedAccessoryPresentation(&f.state, 1, nullptr);
            const auto* orb = Step4MetarOrb(handle);
            require(orb && orb->label.empty() &&
                        orb->airportIcao == "KDFW" &&
                        orb->categoryText == item.text &&
                        orb->stateText.empty() &&
                        orb->selectedIndicator.empty() &&
                        orb->tone == item.tone,
                    std::string("ORB category/tone mismatch: ") + item.text);
        }
    } else if (probe == "pinned_primary_not_history") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        Step4SelectDrawer(&f.state, BrainOwnedAccessoryDrawerId::Metar);
        const auto stored = f.state.accessory.histories[0].entries.size();
        const auto handle = ProjectBrainOwnedAccessoryPresentation(&f.state, 1, nullptr);
        require(handle.snapshot && handle.snapshot->entries.size() == stored + 2,
                "pinned primary and recent heading should be presentation-only");
        require(f.state.accessory.histories[0].entries.size() == stored,
                "pinned primary mutated history");
    } else if (probe == "disconnect_inflight_completion_deferred") {
        Step4Fixture f;
        f.Cycle();
        f.input.xpilotConnected = false;
        f.worker.Complete(BrainMetarWorkerStatus::Success, "KDFW", primaryVfr);
        f.Cycle(1);
        require(f.state.metar.deferredDisconnectedFact.has_value() &&
                    !f.state.metar.primaryObservation.valid,
                "disconnect completion must remain deferred and unparsed");
        f.input.xpilotConnected = true;
        f.Cycle(1);
        require(f.state.metar.primaryObservation.valid,
                "valid deferred completion must accept on reconnect");
    } else if (probe == "disconnect_request_suppression") {
        Step4Fixture f;
        f.input.xpilotConnected = false;
        for (int index = 0; index < 100; ++index) f.Cycle(1'000);
        require(f.worker.requests.empty(), "disconnected runtime dispatched request");
        require(f.state.accessory.activeDrawer == BrainOwnedAccessoryDrawerId::None,
                "disconnected drawer must remain closed");
    } else if (probe == "brain_owned_worker_dispatch_only") {
        require(Step4FileContains("plugin/src/XVatsimPlugin.cpp",
                                  "RunBrainOwnedAsyncFactCycle"),
                "generic brain cycle binding missing");
        require(Step4FileContains("plugin/src/XVatsimPlugin.cpp",
                                  "AsyncFactWorkerHost"),
                "generic worker host missing");
        require(!Step4FileContains("plugin/src/XVatsimPlugin.cpp",
                                   "VatsimMetarClient"),
                "plugin owns feature-specific client");
        require(!Step4FileContains("plugin/src/XVatsimPlugin.cpp",
                                   "kBrainMetarRefresh"),
                "plugin owns METAR cadence");
    } else if (probe == "lookup_pending_fetching_presentation") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        const auto decision = f.SubmitLookup("KABQ");
        require(decision.accepted &&
                    f.state.accessory.activeDrawer == BrainOwnedAccessoryDrawerId::Metar,
                "valid lookup must immediately open METAR drawer");
        require(Step4HasTitle(&f.state, "FETCHING METAR — KABQ"),
                "pending lookup presentation missing");
        require(f.state.metar.transientDeadlineMonotonicMs - f.input.monotonicMs == 20'000,
                "pending deadline must be 20 seconds");
    } else if (probe == "lookup_pending_failure_returns_primary") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        f.SubmitLookup("KABQ");
        f.Cycle();
        f.worker.Complete(BrainMetarWorkerStatus::TransportFailure);
        f.Cycle(1);
        require(Step4HasTitle(&f.state, "METAR LOOKUP FAILED — KABQ"),
                "bounded failure presentation missing");
        f.Cycle(4'000);
        require(f.state.metar.transientPresentation == BrainMetarTransientPresentation::None &&
                    Step4HasTitle(&f.state, "METAR — KDFW"),
                "failure must return to pinned primary");
    } else if (probe == "lookup_manual_close_respected" ||
               probe == "lookup_completion_no_delayed_reopen") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        f.SubmitLookup("KABQ");
        f.Cycle();
        Step4SelectDrawer(&f.state, BrainOwnedAccessoryDrawerId::Metar, 2);
        require(f.state.accessory.activeDrawer == BrainOwnedAccessoryDrawerId::None,
                "manual close did not close drawer");
        f.worker.Complete(BrainMetarWorkerStatus::Success, "KABQ", lookupMvfr);
        f.Cycle(1);
        require(f.state.accessory.activeDrawer == BrainOwnedAccessoryDrawerId::None &&
                    f.state.metar.transientPresentation == BrainMetarTransientPresentation::None &&
                    f.state.metar.transientDeadlineMonotonicMs == 0,
                "completion reopened drawer or started hidden timer");
        f.Cycle(30'000);
        require(f.state.accessory.activeDrawer == BrainOwnedAccessoryDrawerId::None,
                "delayed reopen occurred");
        require(f.state.accessory.histories[0].entries.size() == 2,
                "valid unseen lookup must remain in history");
    } else if (probe == "lookup_drawer_switch_respected") {
        for (const auto drawer : {BrainOwnedAccessoryDrawerId::Atis,
                                  BrainOwnedAccessoryDrawerId::Pdc}) {
            Step4Fixture f;
            f.AcceptPrimary(primaryVfr);
            f.SubmitLookup("KABQ");
            f.Cycle();
            Step4SelectDrawer(&f.state, drawer, 2);
            f.worker.Complete(BrainMetarWorkerStatus::Success, "KABQ", lookupMvfr);
            f.Cycle(1);
            require(f.state.accessory.activeDrawer == drawer &&
                        f.state.metar.transientDeadlineMonotonicMs == 0,
                    "lookup completion overrode newer drawer selection");
        }
    } else if (probe == "accessory_second_identical_lookup") {
        Step4Fixture f;
        f.AcceptPrimary(primaryVfr);
        f.SubmitLookup("KABQ");
        f.AcceptLookup(lookupMvfr);
        f.Cycle(8'000);
        require(f.state.metar.transientPresentation ==
                    BrainMetarTransientPresentation::None,
                "first spotlight did not expire");
        const auto parseBefore = f.state.metar.parseCount;
        const auto historyBefore = f.state.metar.historyMutationCount;
        const auto historySizeBefore =
            f.state.accessory.histories[0].entries.size();
        const auto presentationBefore = f.state.metar.presentationGeneration;
        f.SubmitLookup("KABQ");
        f.AcceptLookup(lookupMvfr);
        require(f.state.metar.parseCount == parseBefore,
                "identical second lookup reparsed content");
        require(f.state.metar.historyMutationCount == historyBefore &&
                    f.state.accessory.histories[0].entries.size() ==
                        historySizeBefore,
                "identical second lookup mutated history");
        require(f.state.metar.transientPresentation ==
                    BrainMetarTransientPresentation::LookupSpotlight,
                "identical second lookup did not activate spotlight");
        require(f.state.metar.presentationGeneration > presentationBefore,
                "identical second lookup did not publish visible spotlight");
        const auto spotlightGeneration = f.state.metar.presentationGeneration;
        f.Cycle(7'999);
        require(f.state.metar.transientPresentation ==
                    BrainMetarTransientPresentation::LookupSpotlight,
                "second spotlight expired early");
        f.Cycle(1);
        require(f.state.metar.transientPresentation ==
                    BrainMetarTransientPresentation::None,
                "second spotlight did not expire at eight seconds");
        require(f.state.metar.presentationGeneration ==
                    spotlightGeneration + 1,
                "spotlight return did not publish exactly once");
        require(Step4HasTitle(&f.state, "METAR — KDFW"),
                "second spotlight did not return to pinned primary");
        require(f.state.metar.parseCount == parseBefore &&
                    f.state.metar.historyMutationCount == historyBefore,
                "spotlight expiry performed content work");
    } else if (probe == "accessory_spotlight_manual_ownership") {
        for (const auto drawer : {BrainOwnedAccessoryDrawerId::Atis,
                                  BrainOwnedAccessoryDrawerId::Pdc,
                                  BrainOwnedAccessoryDrawerId::None}) {
            Step4Fixture f;
            f.AcceptPrimary(primaryVfr);
            f.SubmitLookup("KABQ");
            f.AcceptLookup(lookupMvfr);
            Step4SelectDrawer(
                &f.state,
                drawer == BrainOwnedAccessoryDrawerId::None
                    ? BrainOwnedAccessoryDrawerId::Metar : drawer,
                9);
            f.Cycle(8'500);
            require(f.state.accessory.activeDrawer == drawer,
                    "spotlight expiry overrode pilot drawer ownership");
            require(f.state.metar.transientPresentation ==
                        BrainMetarTransientPresentation::None &&
                        f.state.metar.transientDeadlineMonotonicMs == 0,
                    "manual ownership left a delayed spotlight timer");
            requireSuccessfulMetarOrb(
                &f.state, "KDFW", "VFR",
                BrainOwnedAccessoryOrbPresentation::Tone::Green);
        }
    } else if (probe == "accessory_orb_typography") {
        using namespace xvatsim::modules::overlay;
        auto* measurement = InitializeAccessoryTextMeasurement();
        require(measurement != nullptr, "GDI text measurement unavailable");
        if (measurement != nullptr) {
            for (const float scale : {0.85f, 1.0f, 1.35f}) {
                for (const std::string& text :
                     {"KDFW", "VFR", "MVFR", "IFR", "LIFR"}) {
                    AccessoryTextMeasurementInput input;
                    input.text = text;
                    input.role = AccessoryFontRole::MetarOrbDetail;
                    input.scale = scale;
                    const auto measured = MeasureAccessoryText(measurement, input);
                    require(measured.status ==
                                BrainOwnedAccessoryOperationStatus::Available,
                            "METAR ORB detail measurement unavailable");
                    require(measured.fontFamily == "Segoe UI" &&
                                measured.bold &&
                                std::fabs(measured.fontPixelSize -
                                          (10.0f * scale)) < 0.01f,
                            "METAR ORB detail font is not Segoe UI Bold 10px");
                    require(measured.measuredWidth <=
                                static_cast<int>(std::lround(48.0f * scale)) &&
                                measured.measuredHeight <=
                                static_cast<int>(std::lround(15.0f * scale)),
                            "METAR ORB detail text clips authorized bounds");
                }
            }
            ShutdownAccessoryTextMeasurement(measurement);
        }
        Step4Fixture f;
        f.Cycle();
        requireNeutralMetarOrb(&f.state);
        f.AcceptPrimary(primaryVfr);
        requireSuccessfulMetarOrb(
            &f.state, "KDFW", "VFR",
            BrainOwnedAccessoryOrbPresentation::Tone::Green);
    } else if (probe == "accessory_dispatch_stress") {
        using namespace xvatsim::modules::overlay;
        AccessoryClickFactQueue queue;
        AccessoryInputDispatchCoordinator dispatcher;
        std::uint64_t now = 1;
        for (int index = 0; index < 1'000; ++index) {
            const auto drawer = index % 3 == 0
                ? BrainOwnedAccessoryDrawerId::Metar
                : index % 3 == 1 ? BrainOwnedAccessoryDrawerId::Atis
                                 : BrainOwnedAccessoryDrawerId::Pdc;
            AccessoryClickFact produced;
            require(queue.Produce(drawer, now++, &produced),
                    "stress queue dropped a valid click");
            dispatcher.RecordDispatchNotification();
            AccessoryClickFact consumed;
            require(dispatcher.TryBegin(&queue, &consumed),
                    "stress dispatcher did not begin click");
            require(dispatcher.BindPresentation(
                        consumed.requestSequence,
                        BrainOwnedAccessoryDrawerAction::Opened,
                        static_cast<std::uint64_t>(index + 1),
                        static_cast<std::uint64_t>(index + 1)),
                    "stress dispatcher did not bind action");
            const auto completion = dispatcher.CompleteMatchingDraw(
                drawer, static_cast<std::uint64_t>(index + 1),
                static_cast<std::uint64_t>(index + 2), now++);
            require(completion.completed && completion.terminal &&
                        completion.disposition ==
                            AccessoryInputDispatchDisposition::
                                CompatibleRenderSuperseded,
                    "compatible stress draw did not terminate action");
        }
        const auto snapshot = dispatcher.Snapshot();
        require(!snapshot.inFlight && queue.PendingCount() == 0,
                "stress left input queued or in flight");
        require(snapshot.requestsBegun == 1'000 &&
                    snapshot.matchingDrawCompletions == 1'000 &&
                    snapshot.supersededGenerationCompletions == 1'000 &&
                    snapshot.mismatchedDrawAttempts == 0,
                "stress terminal accounting mismatch");
        require(snapshot.maximumInFlightMicroseconds <= 500'000,
                "stress action exceeded 500-millisecond liveness limit");
        std::cout << "STEP4_ACCESSORY_STRESS: actions=1000"
                  << " superseded=1000 queued=0 in_flight=false"
                  << " max_in_flight_us="
                  << snapshot.maximumInFlightMicroseconds
                  << " limit_us=500000\n";
    } else if (probe == "accessory_deferred_binding_boundaries") {
        require(Step4FileContains("modules/overlay/src/OverlayWindow.cpp",
                                  "ClearDeferredAccessoryInputBinding(true);"),
                "lifecycle stop does not cancel deferred binding");
        require(Step4FileContains("modules/overlay/src/OverlayWindow.cpp",
                                  "ClearDeferredAccessoryInputBinding(false);"),
                "deferred binding is not cleared before bind/discard");
        require(Step4FileContains("modules/overlay/src/OverlayWindow.cpp",
                                  "CancelAccessoryInputDispatch(fact.requestSequence);"),
                "failed deferred binding does not release dispatcher");
        require(Step4FileContains("modules/overlay/src/OverlayWindow.cpp",
                                  "NotifyNextAccessoryInputIfPending();"),
                "failed deferred binding does not wake the next queued click");
    } else if (probe == "accessory_bounded_diagnostics") {
        require(Step4FileContains("plugin/src/XVatsimPlugin.cpp",
                                  "supersededGenerationCompletions"),
                "accessory supersession summary missing");
        require(Step4FileContains("plugin/src/XVatsimPlugin.cpp",
                                  "maximumInFlightMicroseconds"),
                "accessory liveness maximum missing");
    } else {
        std::cerr << "STEP4_SCENARIO_CONFIGURATION_ERROR: " << scenario.name
                  << ": unknown probe " << probe << "\n";
        return 2;
    }

    if (!failures.empty()) {
        for (const auto& failure : failures) {
            std::cerr << "STEP4_ASSERTION_FAILED: " << scenario.name
                      << ": " << failure << "\n";
        }
        return 1;
    }
    std::cout << "Scenario passed: " << scenario.name << "\n";
    return 0;
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

    if (!scenario.operatingMode.probe.empty()) {
        return RunOperatingModeProbe(scenario);
    }
    if (!scenario.step3.actions.empty()) {
        return RunStep3ContractProbe(scenario);
    }
    if (!scenario.step4.probe.empty()) {
        return RunStep4ContractProbe(scenario);
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
