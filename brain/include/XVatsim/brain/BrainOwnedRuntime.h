#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "XVatsim/brain/BrainDisplayIntent.h"
#include "XVatsim/brain/BrainTypes.h"
#include "XVatsim/brain/BrainWorkflow.h"
#include "XVatsim/brain/PhaseSnapshotPublisher.h"
#include "XVatsim/brain/RadioReachableSnapshot.h"

namespace xvatsim::brain {

enum class BrainOwnedCandidateDecision {
    Pending,
    Accepted,
    Rejected,
    NeedsVerification,
};

enum class BrainOwnedDisplayOverrideMode {
    Auto,
    ForcedOpen,
    ForcedSleep,
};

enum class BrainOwnedTextEntryMode {
    None,
    ManualCtaf,
    DiversionAirport,
};

struct BrainOwnedCandidateCompletion {
    std::uint64_t radioBoardHash = 0;
    std::uint64_t routePolygonHash = 0;
    WorkflowStage workflowStage = WorkflowStage::None;
    int currentPolygonIndex = 0;
    std::string currentPolygonKey;
    std::string matchedPolygonKey;
    std::string callsign;
    std::string frequency;
    RadioReachableFacilityGroup facilityGroup = RadioReachableFacilityGroup::Other;
    DisplayRelation displayRelation = DisplayRelation::Unknown;
    BrainOwnedCandidateDecision decision = BrainOwnedCandidateDecision::Pending;
    bool displayed = false;
    bool hasRouteEntryDistance = false;
    double routeEntryDistanceNm = 0.0;
    std::string reason;
    std::string stableKey;
};

struct BrainOwnedControllerMessageState {
    bool primed = false;
    int lastSequence = 0;
    bool visible = false;
    bool cachedAvailable = false;
    std::string from;
    std::string body;
};

struct BrainTerminalAuthorityWorkerInput {
    std::string airportIcao;
    bool hasAirportCoordinates = false;
    double airportLatitudeDeg = 0.0;
    double airportLongitudeDeg = 0.0;
    long long nowSeconds = 0;
};

struct BrainTerminalAuthorityWorkerOutput {
    bool available = false;
    bool pending = false;
    bool resolved = false;
    bool stale = false;
    std::string airportIcao;
    std::vector<std::string> ownerTokens;
    std::vector<std::string> polygonKeys;
    std::string source;
    std::string status;
    std::string cacheStatus;
    std::uint64_t sourceGeneration = 0;
    long long lookupUs = 0;
};

class BrainTerminalAuthorityWorker {
public:
    virtual ~BrainTerminalAuthorityWorker() = default;

    virtual BrainTerminalAuthorityWorkerOutput ResolveAirportTerminalOwner(
        const BrainTerminalAuthorityWorkerInput& input) = 0;
};

enum class BrainAirportFrequencyEndpoint {
    Unknown,
    Departure,
    Arrival,
};

struct BrainAirportFrequencyRecord {
    BrainAirportFrequencyEndpoint endpoint = BrainAirportFrequencyEndpoint::Unknown;
    std::string airportIcao;
    StationRole role = StationRole::Other;
    std::string frequency;
    std::string frequencyUse;
    std::string sectorization;
    std::string facility;
    std::string servicedFacility;
    std::string towerOrCommCall;
    std::string primaryApproachRadioCall;
};

struct BrainAirportFrequencyWorkerInput {
    std::string departureIcao;
    std::string arrivalIcao;
    long long nowSeconds = 0;
};

struct BrainAirportFrequencyWorkerOutput {
    bool available = false;
    bool pending = false;
    bool resolved = false;
    bool stale = false;
    std::string departureIcao;
    std::string arrivalIcao;
    std::vector<BrainAirportFrequencyRecord> departureFrequencies;
    std::vector<BrainAirportFrequencyRecord> arrivalFrequencies;
    std::string source;
    std::string status;
    std::string cacheStatus;
    std::uint64_t sourceGeneration = 0;
    long long lookupUs = 0;
};

class BrainAirportFrequencyWorker {
public:
    virtual ~BrainAirportFrequencyWorker() = default;

    virtual BrainAirportFrequencyWorkerOutput ResolveAirportFrequencies(
        const BrainAirportFrequencyWorkerInput& input) = 0;
};

struct BrainOwnedRuntimeState {
    bool hasRoutePolygonSnapshot = false;
    RouteSectorSnapshot routePolygonSnapshot;
    std::uint64_t routePolygonHash = 0;
    long long lastRoutePolygonRefreshSeconds = 0;
    std::string routePlanKey;
    int currentPolygonIndex = 0;
    std::string currentPolygonKey;
    std::string nextPolygonKey;
    std::string arrivalPolygonKey;
    std::string finalRoutePolygonKey;
    double routeProgressDistanceNm = 0.0;
    std::string lastRoutePolygonTransitionReason;
    bool lastRoutePolygonTransitionChanged = false;

    bool hasRadioBoard = false;
    long long lastRadioBoardRefreshSeconds = 0;
    std::uint64_t lastControllerGeneration = 0;
    TransceiverResolutionSnapshot transceiverSnapshot;
    RadioReachableControllerSnapshot radioSnapshot;
    RadioReachableControllerSnapshot gatedRadioSnapshot;
    RadioReachableCandidateDiff radioDiff;
    bool hasDepartureTerminalAuthority = false;
    BrainTerminalAuthorityWorkerOutput departureTerminalAuthority;
    std::string departureTerminalAuthorityRequestKey;
    std::uint64_t departureTerminalAuthorityHash = 0;
    long long lastDepartureTerminalAuthorityLookupSeconds = 0;
    bool hasArrivalTerminalAuthority = false;
    BrainTerminalAuthorityWorkerOutput arrivalTerminalAuthority;
    std::string arrivalTerminalAuthorityRequestKey;
    std::uint64_t arrivalTerminalAuthorityHash = 0;
    long long lastArrivalTerminalAuthorityLookupSeconds = 0;
    bool hasAirportFrequencies = false;
    BrainAirportFrequencyWorkerOutput airportFrequencies;
    std::string airportFrequencyRequestKey;
    std::uint64_t airportFrequencyHash = 0;
    long long lastAirportFrequencyLookupSeconds = 0;

    ModuleBoardSnapshot departureBoardSnapshot;
    ModuleBoardSnapshot arrivalBoardSnapshot;
    ModuleBoardSnapshot enrouteBoardSnapshot;
    ModuleBoardSnapshot relevanceDepartureBoardSnapshot;
    ModuleBoardSnapshot relevanceArrivalBoardSnapshot;
    ModuleBoardSnapshot relevanceEnrouteBoardSnapshot;
    FinalDisplaySnapshot finalDisplaySnapshot;
    PhaseSnapshotPublisherState phaseSnapshotPublisherState;
    std::uint64_t lastDisplayIntentHash = 0;
    AircraftStateSnapshot lastAircraftStateSnapshot;
    PilotIdentitySnapshot lastPilotIdentitySnapshot;
    FlightPlanSnapshot lastFlightPlanSnapshot;
    NetworkPlanSnapshot lastNetworkPlanSnapshot;
    bool hasFlightPlanSnapshot = false;
    FlightPlanSnapshot flightPlanSnapshot;
    long long lastFlightPlanSampleSeconds = 0;
    bool hasActiveCruiseTarget = false;
    bool cruiseTargetManualOverride = false;
    bool cruiseAltitudeReachedThisFlight = false;
    double activeCruiseTargetFt = 0.0;
    double cruiseGateSatisfiedSinceSeconds = -1.0;
    std::string cruiseTargetSourceKey;
    std::string standbyAssistLatchKey;
    bool standbyAssistWriteConsumed = false;
    std::string diversionOverrideSourceKey;
    std::string preflightRouteCacheAppliedPlanKey;
    BrainOwnedDisplayOverrideMode displayOverrideMode =
        BrainOwnedDisplayOverrideMode::Auto;
    BrainOwnedTextEntryMode pendingTextEntryMode =
        BrainOwnedTextEntryMode::None;
    ManualQuerySnapshot manualQuerySnapshot;
    long long manualQueryVisibleUntilSeconds = 0;
    BrainOwnedControllerMessageState controllerMessageState;
    bool departureReleasedThisFlight = false;
    bool arrivalAwakeThisFlight = false;
    double airborneSinceSeconds = -1.0;
    bool sawXPilotConnectedThisFlight = false;
    workflow::FlightContext flightContext;
    workflow::XPilotSessionBoundaryState xPilotSessionBoundaryState;
    bool coldDarkResetApplied = false;
    bool aircraftStateInvalidBoundaryActive = false;
    bool pendingAutomaticFlightRecovery = false;
    bool manualFlightRecoveryRequested = false;

    WorkflowStage lastWorkflowStage = WorkflowStage::None;
    std::string lastPlanKey;
    std::uint64_t lastRadioBoardHash = 0;
    std::uint64_t lastRoutePolygonHash = 0;
    std::uint64_t lastDepartureTerminalAuthorityHash = 0;
    std::uint64_t lastArrivalTerminalAuthorityHash = 0;
    std::uint64_t lastAirportFrequencyHash = 0;
    std::uint64_t lastAuthorityRelevanceHash = 0;
    std::uint64_t lastRadioTuningHash = 0;
    std::string lastWakeReason;
    std::string lastIdleReason;

    bool candidatesComplete = false;
    bool heavyFallbackRequested = false;
    bool heavyFallbackRunning = false;
    bool enrouteInitialHoldStarted = false;
    double enrouteInitialHoldUntilSeconds = -1.0;
    std::vector<BrainOwnedCandidateCompletion> candidateCompletions;
};

struct BrainOwnedBoardFilterOutput {
    ModuleBoardSnapshot board;
    int rejectedUnapprovedStations = 0;
};

struct BrainOwnedRadioBoardReuseInput {
    long long nowSeconds = 0;
    long long refreshIntervalSeconds = 0;
    std::uint64_t controllerGeneration = 0;
};

struct BrainOwnedRadioBoardReuseOutput {
    bool canReuse = false;
    RadioReachableControllerSnapshot radioSnapshot;
    std::string reason;
    std::string cacheStatus;
};

struct BrainOwnedRadioBoardCommitInput {
    long long nowSeconds = 0;
    std::uint64_t controllerGeneration = 0;
    TransceiverResolutionSnapshot transceiverSnapshot;
    RadioReachableControllerSnapshot radioSnapshot;
};

struct BrainOwnedRadioBoardCommitOutput {
    RadioReachableControllerSnapshot radioSnapshot;
    RadioReachableCandidateDiff diff;
    bool boardChanged = false;
    std::string reason;
    std::string cacheStatus;
};

struct BrainOwnedTerminalAuthorityRefreshInput {
    bool flightContextActive = false;
    std::string airportIcao;
    bool hasAirportCoordinates = false;
    double airportLatitudeDeg = 0.0;
    double airportLongitudeDeg = 0.0;
    long long nowSeconds = 0;
};

struct BrainOwnedTerminalAuthorityRefreshPlan {
    bool shouldRunWorker = false;
    BrainTerminalAuthorityWorkerInput workerInput;
    BrainTerminalAuthorityWorkerOutput cachedFact;
    std::string requestKey;
    std::string reason;
    std::string cacheStatus;
};

struct BrainOwnedAirportFrequencyRefreshInput {
    bool flightContextActive = false;
    std::string departureIcao;
    std::string arrivalIcao;
    long long nowSeconds = 0;
};

struct BrainOwnedAirportFrequencyRefreshPlan {
    bool shouldRunWorker = false;
    BrainAirportFrequencyWorkerInput workerInput;
    BrainAirportFrequencyWorkerOutput cachedFact;
    std::string requestKey;
    std::string reason;
    std::string cacheStatus;
};

struct BrainOwnedPublishedRuntimeInput {
    WorkflowStage workflowStage = WorkflowStage::None;
    std::string planKey;
    RadioReachableControllerSnapshot gatedRadioSnapshot;
    ModuleBoardSnapshot departureBoard;
    ModuleBoardSnapshot arrivalBoard;
    ModuleBoardSnapshot enrouteBoard;
    FinalDisplaySnapshot finalDisplay;
};

struct BrainOwnedOverlayWakeInput {
    AircraftStateSnapshot aircraftState;
    XPilotSessionSnapshot xPilotSession;
    WorkflowStage workflowStage = WorkflowStage::None;
    FinalDisplaySnapshot finalDisplay;
    BrainOwnedDisplayOverrideMode displayOverrideMode =
        BrainOwnedDisplayOverrideMode::Auto;
    bool manualQueryVisible = false;
    bool textEntryActive = false;
    bool controllerMessageVisible = false;
    bool sawXPilotConnectedThisFlight = false;
    bool enrouteInitialHoldActive = false;
};

struct BrainOwnedOverlayWakeDecision {
    bool shouldWake = false;
    bool hideUntilXpilotConnect = false;
    bool xPilotDisconnectedAlert = false;
    std::string reason;
};

struct BrainOwnedEnrouteInitialHoldInput {
    WorkflowStage workflowStage = WorkflowStage::None;
    double nowSeconds = 0.0;
    double holdSeconds = 0.0;
};

struct BrainOwnedEnrouteInitialHoldOutput {
    bool active = false;
    bool started = false;
    double holdUntilSeconds = -1.0;
};

struct BrainOwnedFlightPlanSampleInput {
    bool flightContextActive = false;
    long long nowSeconds = 0;
    long long sampleCadenceSeconds = 0;
};

struct BrainOwnedFlightPlanSampleDecision {
    bool shouldSample = true;
    FlightPlanSnapshot cachedSnapshot;
    std::string reason;
};

struct BrainOwnedFlightPlanSampleCommitInput {
    long long nowSeconds = 0;
    FlightPlanSnapshot snapshot;
};

struct BrainOwnedWorkflowSelectionInput {
    AircraftStateSnapshot aircraft;
    RadioStateSnapshot radios;
    RadioReachableControllerSnapshot radioSnapshot;
    std::string departureIcao;
    std::string arrivalIcao;
    BrainTerminalAuthorityWorkerOutput departureTerminalAuthority;
    double nowSeconds = 0.0;
    workflow::WorkflowTuning tuning;
};

struct BrainOwnedWorkflowSelectionOutput {
    workflow::HandoffDecision decision;
};

struct BrainOwnedCruiseTargetTuning {
    double gateToleranceFt = 1000.0;
    double stableVerticalSpeedFpm = 800.0;
    double gateDwellSeconds = 10.0;
};

struct BrainOwnedCruiseTargetPlanInput {
    bool flightContextActive = false;
    std::string planKey;
    NetworkPlanSnapshot networkPlan;
};

struct BrainOwnedCruiseTargetPlanOutput {
    bool changed = false;
    std::string logLine;
};

enum class BrainOwnedCruiseTargetCommand {
    CurrentAltitude,
    FiledAltitude,
};

struct BrainOwnedCruiseTargetCommandInput {
    BrainOwnedCruiseTargetCommand command =
        BrainOwnedCruiseTargetCommand::FiledAltitude;
    bool flightContextActive = false;
    std::string planKey;
    AircraftStateSnapshot aircraftState;
    NetworkPlanSnapshot networkPlan;
    double nowSeconds = 0.0;
    BrainOwnedCruiseTargetTuning tuning;
};

struct BrainOwnedCruiseTargetCommandOutput {
    bool accepted = false;
    bool changed = false;
    std::string statusLine;
};

struct BrainOwnedCruiseTargetProgressInput {
    AircraftStateSnapshot aircraftState;
    double nowSeconds = 0.0;
    BrainOwnedCruiseTargetTuning tuning;
};

struct BrainOwnedStandbyAssistAdvisoryCandidate {
    std::string sourceDecisionId;
    std::string sourceEvidenceId;
    std::string endpoint;
    std::string airportIcao;
    std::string advisoryDecision;
    std::string projectedRole;
    std::string projectedFrequency;
    bool acceptedByAdvisory = false;
    bool fallbackUsed = false;
    std::string sourceConfidence;
    std::string confidenceLevel;
    double positiveScore = 0.0;
    double negativeScore = 0.0;
    bool hardBlock = false;
    std::string hardBlockReason;
    std::string advisoryReason;
};

struct BrainOwnedStandbyRecommendationDecision {
    std::string standbyDecisionId;
    std::string subjectKey;
    std::string sourceDomain;
    std::string sourceDecisionId;
    std::string sourceEvidenceId;
    std::string endpoint;
    std::string airportIcao;
    std::string callsign;
    std::string role;
    std::string frequency;
    std::string workflowStage;
    std::string planKey;
    int boardIndex = -1;
    std::string displayRelation;
    bool candidateVisibleInFinalBoard = false;
    bool acceptedByAdvisory = false;
    std::string advisoryDecision;
    std::string sourceConfidence;
    std::string confidenceLevel;
    bool fallbackUsed = false;
    double positiveScore = 0.0;
    double negativeScore = 0.0;
    bool hardBlock = false;
    std::string hardBlockReason;
    bool alreadyCom1Active = false;
    bool alreadyCom2Active = false;
    bool alreadyCom1Standby = false;
    std::string targetCom;
    bool eligible = false;
    bool previewEligible = false;
    std::string previewRecommendation;
    std::string previewSkipReason;
    bool liveWriteEligible = false;
    bool productGateEnabled = false;
    bool directCtafLivePromotionAllowed = false;
    std::string livePromotionReason;
    std::string livePromotionBlockedReason;
    bool promotedFromDryRun = false;
    std::string actualSelectedTargetSource;
    std::string actualSelectedTargetFrequency;
    bool actualWriteEligible = false;
    bool noControllerTargetAvailable = false;
    bool controllerTargetPreserved = false;
    std::string featureGateRequired;
    bool featureGateSatisfied = false;
    std::string featureGateBlockedReason;
    bool dryRunLiveEligible = false;
    std::string dryRunLiveRecommendation;
    std::string dryRunSkipReason;
    std::string dryRunSafetyGate;
    bool dryRunWouldSelectTarget = false;
    bool dryRunWouldDisplaceControllerTarget = false;
    bool dryRunBlockedByExistingControllerTarget = false;
    bool dryRunBlockedByStandbyDisabled = false;
    bool dryRunBlockedByAlreadyCom1Standby = false;
    bool dryRunBlockedByFrequencyState = false;
    std::string dryRunTargetCom;
    std::string dryRunTargetFrequency;
    std::string dryRunPromotionClass;
    std::string advisoryProductGate;
    std::string advisoryWritePolicy;
    std::string advisoryFrequencyResolutionState;
    std::string advisoryCandidateType;
    std::string skipReason;
    std::string finalRecommendation;
};

struct BrainOwnedStandbyRecommendationSummary {
    int standbyEvidenceCount = 0;
    int standbyCandidateCount = 0;
    int advisoryCandidateCount = 0;
    int selectedTargetCount = 0;
    int writeDecisionCount = 0;
    int writeAttemptCount = 0;
    int writeSuccessCount = 0;
    int writeFailureCount = 0;
    int skippedEmptyFrequencyCount = 0;
    int skippedPendingLookupCount = 0;
    int skippedLookupFailedCount = 0;
    int skippedGuardFrequencyCount = 0;
    int skippedRoleNotEligibleCount = 0;
    int skippedAlreadyActiveCount = 0;
    int writerResultCount = 0;
    int writerSuccessCount = 0;
    int writerFailureCount = 0;
    int writerBlockedBeforeWriteCount = 0;
    int writerUnknownResultCount = 0;
    int writerDatarefMissingCount = 0;
    int writerDatarefNotWritableCount = 0;
    int writerInvalidFrequencyCount = 0;
    int writerNoTargetCount = 0;
    int writerNoWriteRequestedCount = 0;
    int writerControllerSourceCount = 0;
    int writerDirectCtafSourceCount = 0;
    bool standbyRecommendationsBrainOwned = true;
};

struct BrainOwnedStandbyAssistPlanInput {
    WorkflowStage workflowStage = WorkflowStage::None;
    std::string planKey;
    RadioStateSnapshot radios;
    bool standbyAssistEnabled = true;
    bool directCtafStandbyAssistEnabled = false;
    std::string directCtafGateSource = "unknown";
    FinalDisplaySnapshot board;
    std::vector<BrainOwnedStandbyAssistAdvisoryCandidate>
        ctafUnicomAdvisoryCandidates;
};

struct BrainOwnedStandbyAssistSettingsDiagnostics {
    bool standbyAssistEnabled = false;
    bool directCtafStandbyAssistEnabled = false;
    std::string directCtafGateSource = "unknown";
    bool directCtafGateEffective = false;
};

struct BrainOwnedStandbyAssistPlanOutput {
    bool hasTarget = false;
    WorkflowStage workflowStage = WorkflowStage::None;
    FinalDisplaySnapshot board;
    std::size_t targetStationIndex = 0;
    std::string targetFrequency;
    std::string latchKey;
    bool targetAlreadyInCom1Standby = false;
    std::string targetStandbyDecisionId;
    std::string targetAdvisorySourceDecisionId;
    std::string actualSelectedTargetSource = "none";
    std::string actualSelectedTargetFrequency;
    BrainOwnedStandbyAssistSettingsDiagnostics settingsDiagnostics;
    std::vector<BrainOwnedStandbyRecommendationDecision>
        standbyDecisions;
    BrainOwnedStandbyRecommendationSummary standbySummary;
};

struct BrainOwnedStandbyAssistWriterResult {
    bool writerResultKnown = false;
    std::string writerResultCode;
    std::string writerFailureReason;
    std::string writerFailureDomain;
    std::string writerInputFrequency;
    std::string writerNormalizedFrequency;
    std::string writerTargetCom;
    std::string writerDatarefName;
    bool writerDatarefAvailable = false;
    bool writerDatarefWritable = false;
    bool writerValidationPassed = false;
    bool writerWriteAttempted = false;
    bool writerWriteSucceeded = false;
    bool writerWriteBlockedBeforeSimWrite = false;
    bool writerWriteFailedAtSimLayer = false;
    std::string writerResultSource = "none";
    std::string writerResultDecisionId;
    std::string writerResultLinkedStandbyDecisionId;
};

struct BrainOwnedStandbyAssistSideEffectDecision {
    bool shouldWriteCom1Standby = false;
    bool standbyLoaded = false;
    std::string targetFrequency;
    std::string sideEffectDecisionId;
    std::string standbyDecisionId;
    bool standbyAssistEnabled = false;
    std::string latchKey;
    bool latchConsumed = false;
    bool writeAllowed = false;
    bool writeAttempted = false;
    bool writeSucceededKnown = false;
    bool writeSucceeded = false;
    std::string writerTarget;
    std::string failureReason;
    bool displayStandbyMarkerApplied = false;
    std::string actualSelectedTargetSource;
    std::string actualSelectedTargetFrequency;
    bool actualWriteEligible = false;
    bool actualWriteAttempted = false;
    bool actualWriteSucceededKnown = false;
    bool actualWriteSucceeded = false;
    BrainOwnedStandbyAssistWriterResult writerResult;
    BrainOwnedStandbyRecommendationSummary standbySummary;
};

struct BrainOwnedDiversionOverrideInput {
    bool hasOverride = false;
    std::string sourcePlanKey;
};

struct BrainOwnedDiversionOverrideDecision {
    bool useOverride = false;
    bool clearOverride = false;
    std::string logLine;
};

struct BrainOwnedPreflightRouteCacheInput {
    std::string planKey;
    bool hasCandidate = false;
};

struct BrainOwnedPreflightRouteCacheDecision {
    bool shouldClearRouteResolverCache = false;
    bool shouldValidateCandidate = false;
    std::string logLine;
};

struct BrainOwnedPreflightRouteCacheValidationInput {
    bool accepted = false;
    std::string reason;
};

struct BrainOwnedPreflightRouteCacheValidationDecision {
    bool shouldApplyRouteResolverCache = false;
    std::string logLine;
};

struct BrainOwnedCtafLookupFact {
    std::string airportIcao;
    bool resolved = false;
    bool available = false;
    std::string frequency;
    bool lookupAttempted = false;
    std::string lookupSkippedReason;
    bool cacheHit = false;
    bool fetchInProgress = false;
    bool requestSucceeded = false;
    std::string statusCodeClass;
    long long lastAttemptAgeSeconds = -1;
    int failureCount = 0;
    std::string pendingReason;
};

// CTAF/UNICOM lookup facts are source evidence only. The lookup layer reports
// what it knows; brain-owned advisory decisions below decide live row projection.
struct BrainOwnedCtafUnicomSourceEvidence {
    std::string evidenceId;
    std::string endpoint;
    std::string airportIcao;
    bool lookupAttempted = false;
    std::string lookupSkippedReason;
    bool cacheHit = false;
    bool fetchInProgress = false;
    bool requestSucceeded = false;
    std::string statusCodeClass;
    bool resolved = false;
    bool available = false;
    std::string frequency;
    long long lastAttemptAgeSeconds = -1;
    int failureCount = 0;
    bool fallbackEligible = false;
    std::string fallbackFrequency;
    std::string sourceConfidence;
    std::string sourceReason;
    std::string pendingReason;
};

// Compatibility/parity record for the legacy lookup-to-row projection. When
// source evidence exists, this vector is diagnostic only and is not live row
// authority.
struct BrainOwnedCtafUnicomProjectionEvidence {
    std::string projectionEvidenceId;
    std::string sourceEvidenceId;
    std::string endpoint;
    std::string airportIcao;
    std::string projectedRole;
    std::string projectedFrequency;
    bool fallbackUsed = false;
    bool unresolvedProjectedEmptyFrequency = false;
    int legacyRowRemovedCount = 0;
    int duplicateSuppressedCount = 0;
    bool diagnosticCompatibilityProjectionOnly = true;
    // Deprecated compatibility mirror retained for public/header consumers.
    // New diagnostics should use diagnosticCompatibilityProjectionOnly.
    bool completionBypassCompatibilityOnly = true;
    bool completionBypassRetired = true;
    bool completionBypassLiveAuthority = false;
    bool completionBypassDiagnosticOnly = true;
    bool legacyDiagnosticLiveRowEmitted = false;
    // Deprecated compatibility mirror retained for public/header consumers.
    // New diagnostics should use legacyDiagnosticLiveRowEmitted.
    bool liveRowEmitted = false;
};

// Summary for source and compatibility projection evidence. advisoryDecisionCount
// intentionally remains zero until CTAF/UNICOM gets live advisory completion
// records in a later migration step.
struct BrainOwnedCtafUnicomEvidenceSummary {
    int sourceEvidenceCount = 0;
    int projectionEvidenceCount = 0;
    int liveRowEmittedCount = 0;
    int diagnosticCompatibilityProjectionOnly = 0;
    // Deprecated compatibility mirror retained for public/header consumers.
    // New diagnostics should use diagnosticCompatibilityProjectionOnly.
    int completionBypassCompatibilityOnly = 0;
    int historicalCompatibilityRowCount = 0;
    int legacyDiagnosticLiveRowEmittedCount = 0;
    bool compatibilityRowsDiagnosticOnly = true;
    bool legacyBypassFieldsQuarantined = true;
    int advisoryDecisionCount = 0;
};

// Brain-owned advisory decision preview. These decisions are the live row source
// when evidence exists; matchesCurrentProjection proves parity with the
// compatibility projection path.
struct BrainOwnedCtafUnicomAdvisoryPreviewDecision {
    std::string advisoryDecisionId;
    std::string sourceEvidenceId;
    std::string endpoint;
    std::string airportIcao;
    std::string decision;
    std::string projectedRole;
    std::string projectedFrequency;
    bool fallbackUsed = false;
    std::string sourceConfidence;
    std::string confidenceLevel;
    double positiveScore = 0.0;
    double negativeScore = 0.0;
    bool hardBlock = false;
    std::string reason;
    bool wouldEmitLiveRow = false;
    bool matchesCurrentProjection = false;
};

// Diagnostic preview summary for CTAF/UNICOM advisory decisions.
struct BrainOwnedCtafUnicomAdvisoryPreviewSummary {
    int sourceEvidenceCount = 0;
    int projectionEvidenceCount = 0;
    int advisoryPreviewDecisionCount = 0;
    int previewWouldEmitLiveRowCount = 0;
    int previewMatchesCurrentProjectionCount = 0;
    int previewMismatchCount = 0;
    int diagnosticCompatibilityProjectionOnly = 0;
    // Deprecated compatibility mirror retained for public/header consumers.
    // New diagnostics should use diagnosticCompatibilityProjectionOnly.
    int completionBypassCompatibilityOnly = 0;
};

// Authority guardrail for CTAF/UNICOM advisory rows. liveRowsBrainOwned must be
// true whenever source evidence exists. The old completion bypass is retained
// only as diagnostic compatibility evidence.
struct BrainOwnedCtafUnicomAdvisoryAuthoritySummary {
    std::string advisoryAuthority;
    int sourceEvidenceCount = 0;
    int advisoryPreviewDecisionCount = 0;
    int liveAdvisoryRowCount = 0;
    int compatibilityProjectionCount = 0;
    int oldVsBrainMismatchCount = 0;
    int diagnosticCompatibilityProjectionOnly = 0;
    // Deprecated compatibility mirror retained for public/header consumers.
    // New diagnostics should use diagnosticCompatibilityProjectionOnly.
    int completionBypassCompatibilityOnly = 0;
    bool completionBypassRetired = true;
    int liveBypassAuthorityCount = 0;
    int diagnosticBypassRowCount = 0;
    int brainAdvisoryLiveRowCount = 0;
    int duplicateLiveRowCount = 0;
    bool bypassRetirementSafe = false;
    bool noLiveBypassAuthority = true;
    bool compatibilityRowsDiagnosticOnly = true;
    bool liveRowsBrainAdvisoryOwned = true;
    bool legacyBypassFieldsQuarantined = true;
    bool liveRowsBrainOwned = false;
};

struct BrainOwnedCtafUnicomBypassAuditDecision {
    std::string ctafUnicomBypassAuditDecisionId;
    std::string advisoryDecisionId;
    std::string sourceEvidenceId;
    std::string projectionEvidenceId;
    std::string endpoint;
    std::string airportIcao;
    std::string callsign;
    std::string role;
    std::string frequency;
    bool diagnosticCompatibilityWouldDisplay = false;
    bool bypassRequired = false;
    std::string diagnosticCompatibilityReason;
    std::string bypassReason;
    std::string advisoryAuthority;
    bool advisoryWouldEmitLiveRow = false;
    bool advisoryMatchesBypassRow = false;
    bool roleMatches = false;
    bool frequencyMatches = false;
    bool endpointMatches = false;
    bool airportMatches = false;
    bool visibilityMatches = false;
    bool bypassRowHasBrainEquivalent = false;
    bool brainRowHasBypassEquivalent = false;
    bool wouldRetireSafely = false;
    std::string retirementBlockedReason;
    bool diagnosticCompatibilityOnly = false;
    bool compatibilityOnly = false;
    std::string mismatchReason;
    bool missingAdvisoryDecision = false;
    bool missingSourceEvidence = false;
    bool pendingLookup = false;
    bool lookupFailed = false;
    bool emptyFrequency = false;
    bool unicomFallback = false;
    bool standbyConsumesAdvisoryDecision = false;
    bool standbyConsumesBypassRow = false;
    std::string retirementPolicy;
    std::string retirementPolicyReason;
    std::string retirementBlockerClass;
    bool retirementBlockerResolved = false;
    bool retirementStillBlocked = false;
    bool retirementSafeAfterPolicy = false;
    bool compatibilityDuplicateSuppressed = false;
    std::string duplicateSuppressionReason;
    bool nonDisplayableByPolicy = false;
    bool deferredByPolicy = false;
    bool failedLookupByPolicy = false;
    bool emptyFrequencyByPolicy = false;
    bool missingEvidenceByPolicy = false;
    bool wouldLoseFrequencyIfBypassRemoved = false;
    bool wouldLoseVisibilityIfBypassRemoved = false;
    bool safeToRemoveBypassAfterCleanup = false;
    bool completionBypassRetired = true;
    bool completionBypassLiveAuthority = false;
    bool completionBypassDiagnosticOnly = true;
    int retiredBypassCompatibilityRowCount = 0;
    bool bypassRetirementFallbackWarning = false;
    bool missingEvidenceWarningOnly = false;
    bool missingEvidenceFallbackPreserved = false;
    bool advisoryProjectionAuthority = false;
    std::string diagnosticLiveRowAuthority;
    std::string liveRowAuthority;
    std::string standbyAuthority;
    bool bypassRetirementRegressionSafe = false;
};

struct BrainOwnedCtafUnicomBypassAuditSummary {
    int bypassAuditDecisionCount = 0;
    int bypassRowCount = 0;
    int brainOwnedAdvisoryRowCount = 0;
    int matchingBrainEquivalentCount = 0;
    int missingBrainEquivalentCount = 0;
    int mismatchCount = 0;
    int safeToRetireCount = 0;
    int blockedRetirementCount = 0;
    int pendingLookupCount = 0;
    int lookupFailedCount = 0;
    int emptyFrequencyCount = 0;
    int unicomFallbackCount = 0;
    int standbyAdvisoryConsumerCount = 0;
    int standbyBypassConsumerCount = 0;
    int retirementPolicyDecisionCount = 0;
    int resolvedBlockerCount = 0;
    int stillBlockedCount = 0;
    int policyNonDisplayableCount = 0;
    int policyDeferredCount = 0;
    int policyFailedLookupCount = 0;
    int policyEmptyFrequencyCount = 0;
    int duplicateSuppressedCount = 0;
    int missingEvidencePolicyCount = 0;
    int wouldLoseFrequencyCount = 0;
    int wouldLoseVisibilityCount = 0;
    int bypassRemovalSafeCandidateCount = 0;
    int bypassRemovalStillUnsafeCount = 0;
    bool completionBypassRetired = true;
    int liveBypassAuthorityCount = 0;
    int diagnosticBypassRowCount = 0;
    int brainAdvisoryLiveRowCount = 0;
    int missingEvidenceWarningCount = 0;
    int compatibilityFallbackWarningCount = 0;
    int missingEvidenceFallbackWarningCount = 0;
    int duplicateLiveRowCount = 0;
    int pendingNonDisplayableCount = 0;
    int failedLookupNonDisplayableCount = 0;
    int emptyFrequencyNonDisplayableCount = 0;
    int retiredBypassCompatibilityRowCount = 0;
    bool bypassRetirementSafe = false;
    bool noLiveBypassAuthority = true;
    bool compatibilityRowsDiagnosticOnly = true;
    bool liveRowsBrainAdvisoryOwned = true;
    bool standbyRowsAdvisoryOwned = true;
    bool legacyBypassFieldsQuarantined = true;
    bool diagnosticCompatibilityProjectionOnly = false;
    bool completionBypassCompatibilityOnly = false;
    bool ctafUnicomBypassRetirementReady = false;
};

struct BrainOwnedCtafUnicomMissingEvidenceAuditDecision {
    std::string missingEvidenceAuditDecisionId;
    std::string missingEvidenceEndpoint;
    std::string missingEvidenceAirportIcao;
    std::string missingEvidenceRole;
    std::string missingEvidenceFrequency;
    std::string missingEvidenceCause;
    bool missingSourceEvidence = false;
    bool missingAdvisoryDecision = false;
    bool incompleteAdvisoryDecision = false;
    bool oldCompatibilityWouldDisplay = false;
    bool wouldLoseFrequency = false;
    bool wouldLoseVisibility = false;
    bool warningOnly = false;
    std::string warningReason;
    std::string recoveryHint;
    std::string warningLabel;
    bool liveAuthorityRestored = false;
    bool liveCompatibilityFallbackUsed = false;
    bool standbyConsumesWarning = false;
    bool standbyWriteBlockedByMissingEvidence = false;
    bool authorityInvariantPreserved = true;
    bool failSoftVisible = false;
    bool operatorActionRequired = false;
};

struct BrainOwnedCtafUnicomMissingEvidenceAuditSummary {
    int missingEvidenceAuditCount = 0;
    int missingSourceEvidenceCount = 0;
    int missingAdvisoryDecisionCount = 0;
    int incompleteAdvisoryDecisionCount = 0;
    int oldCompatibilityWouldDisplayCount = 0;
    int wouldLoseFrequencyCount = 0;
    int wouldLoseVisibilityCount = 0;
    int warningOnlyCount = 0;
    int liveAuthorityRestoredCount = 0;
    int liveCompatibilityFallbackUsedCount = 0;
    int standbyConsumesWarningCount = 0;
    int authorityInvariantPreservedCount = 0;
    int operatorActionRequiredCount = 0;
};

struct BrainOwnedCtafUnicomLegacyBypassAliasAuditDecision {
    std::string legacyAliasAuditId;
    std::string aliasName;
    std::string aliasLocation;
    std::string aliasCategory;
    std::string currentMeaning;
    std::string misleadingRisk;
    std::string recommendedAction;
    std::string migrationTarget;
    bool consumerKnown = false;
    std::string consumerRisk;
    bool canRenameNow = false;
    bool canRemoveNow = false;
    std::string removalBlockedReason;
    bool authorityInvariantProtected = true;
    bool liveAuthorityImplication = false;
    bool replacementFieldPresent = false;
    std::string replacementFieldName;
    bool legacyFieldStillPresent = false;
    bool replacementMatchesLegacy = false;
    bool harnessMigratedToReplacement = false;
    bool oldAliasDeprecated = false;
    bool safeToRemoveLegacyLater = false;
    bool replacementMigrationComplete = false;
    std::string replacementMismatchReason;
};

struct BrainOwnedCtafUnicomLegacyBypassAliasAuditSummary {
    int aliasAuditCount = 0;
    int renameNowCandidateCount = 0;
    int renameLaterCount = 0;
    int removeLaterCount = 0;
    int harnessOnlyAliasCount = 0;
    int reportOnlyAliasCount = 0;
    int publicConsumerRiskCount = 0;
    int unknownConsumerRiskCount = 0;
    int liveAuthorityMisleadingAliasCount = 0;
    int authorityInvariantProtectedCount = 0;
    int replacementFieldCount = 0;
    int legacyFieldStillPresentCount = 0;
    int replacementMatchesLegacyCount = 0;
    int replacementMismatchCount = 0;
    int harnessMigratedToReplacementCount = 0;
    int deprecatedAliasCount = 0;
    int safeToRemoveLegacyLaterCount = 0;
    int reportOnlyAliasRemovedCount = 0;
    int reportOnlyAliasRemovalSafeCount = 0;
    int reportOnlyAliasStillFoundCount = 0;
    bool replacementMigrationComplete = false;
};

struct BrainOwnedCtafUnicomPublicUnknownAliasConsumerAuditDecision {
    std::string consumerAliasName;
    std::string replacementName;
    std::string definitionLocation;
    std::string emissionLocation;
    int harnessUsageCount = 0;
    int reportUsageCount = 0;
    int runtimeUsageCount = 0;
    int docsUsageCount = 0;
    int pluginUsageCount = 0;
    bool externalConsumerRisk = false;
    bool unknownConsumerRisk = false;
    bool replacementEmittedSameScope = false;
    bool internalConsumersMigrated = false;
    bool aliasCompatibilityOnly = false;
    bool removalReadyLater = false;
    std::string removalBlockedReason;
    std::string nextMigrationAction;
};

struct BrainOwnedCtafUnicomPublicUnknownAliasConsumerAuditSummary {
    int publicUnknownAliasCount = 0;
    int replacementSameScopeCount = 0;
    int internalMigratedCount = 0;
    int compatibilityOnlyAliasCount = 0;
    int removalReadyLaterCount = 0;
    int removalBlockedCount = 0;
    int externalRiskCount = 0;
    int unknownRiskCount = 0;
    int runtimeUsageCount = 0;
    int harnessLegacyUsageCount = 0;
    int reportLegacyUsageCount = 0;
};

struct BrainOwnedCtafUnicomExternalAliasDeprecationDecision {
    std::string aliasName;
    std::string replacementName;
    std::string aliasRiskClass;
    std::string deprecationStatus;
    bool activeGeneratedAliasPresent = false;
    bool aliasRemovedFromActiveOutput = false;
    bool aliasDeprecated = false;
    bool canonicalReplacementPreferred = false;
    bool replacementUsedByHarness = false;
    bool replacementCarriesEquivalentMeaning = false;
    bool authorityInvariantProtected = true;
    bool liveAuthorityImplication = false;
    bool publicHeaderRiskAliasRetained = false;
    bool runtimeBehaviorChanged = false;
    std::string removalBlockedReason;
    std::string nextMigrationAction;
};

struct BrainOwnedCtafUnicomExternalAliasDeprecationSummary {
    int aliasDeprecationDecisionCount = 0;
    int externalRiskAliasCount = 0;
    int externalAliasDeprecatedCount = 0;
    int externalAliasRemovedCount = 0;
    int activeGeneratedAliasRetainedCount = 0;
    int canonicalReplacementPreferredCount = 0;
    int replacementEquivalentCount = 0;
    int publicHeaderRiskAliasRetainedCount = 0;
    bool liveRowEmittedRetained = false;
    bool completionBypassCompatibilityOnlyRetained = false;
    bool runtimeBehaviorChanged = false;
    bool noLiveBypassAuthority = true;
};

struct BrainOwnedCtafUnicomPublicHeaderAliasRiskClosureDecision {
    std::string publicHeaderAliasName;
    std::string deprecatedAliasName;
    std::string replacementName;
    std::string headerDefinitionLocation;
    std::string runtimeWriteLocation;
    std::string harnessOutputLocation;
    int harnessExpectationUsageCount = 0;
    int pluginUsageCount = 0;
    int moduleUsageCount = 0;
    int docsUsageCount = 0;
    int reportUsageCount = 0;
    bool replacementSameScope = false;
    bool replacementMatchesLegacy = false;
    bool compatibilityOnly = false;
    bool deprecatedPublicHeaderAliasRetained = false;
    bool deprecatedAliasStillEmitted = false;
    bool replacementPreferred = false;
    bool replacementMatchesDeprecatedAlias = false;
    bool canDeprecateNow = false;
    bool canRemoveLater = false;
    std::string removalBlockedReason;
    bool publicHeaderConsumerRisk = false;
    bool externalConsumerRisk = false;
    std::string recommendedAction;
    std::string nextMigrationStep;
};

struct BrainOwnedCtafUnicomPublicHeaderAliasRiskClosureSummary {
    int publicHeaderAliasCount = 0;
    int replacementSameScopeCount = 0;
    int replacementMatchesLegacyCount = 0;
    int compatibilityOnlyCount = 0;
    int deprecatedPublicHeaderAliasCount = 0;
    int deprecatedPublicHeaderAliasRetainedCount = 0;
    int deprecatedAliasReplacementMatchCount = 0;
    int deprecatedAliasReplacementMismatchCount = 0;
    int deprecatedAliasRemovalBlockedCount = 0;
    int canDeprecateNowCount = 0;
    int canRemoveLaterCount = 0;
    int removalBlockedCount = 0;
    int pluginUsageCount = 0;
    int moduleUsageCount = 0;
    int harnessLegacyUsageCount = 0;
    int publicHeaderRiskCount = 0;
    bool deprecatedAliasDocumentationPresent = false;
    bool publicHeaderCompatibilityWindowOpen = false;
    bool ctafUnicomAliasCleanupClosedExceptCompatibilityWindow = false;
};

struct BrainOwnedPublisherInput {
    WorkflowStage workflowStage = WorkflowStage::None;
    double routeProgressDistanceNm = 0.0;
    std::string currentPolygonKey;
    std::string nextPolygonKey;
    std::string arrivalPolygonKey;
    RadioStateSnapshot radios;
    ModuleBoardSnapshot departureBoard;
    ModuleBoardSnapshot arrivalBoard;
    ModuleBoardSnapshot enrouteBoard;
    std::vector<BrainOwnedCandidateCompletion> completions;
    bool hasDepartureCtafStation = false;
    BoardStationSnapshot departureCtafStation;
    bool hasArrivalCtafStation = false;
    BoardStationSnapshot arrivalCtafStation;
    std::vector<BrainOwnedCtafUnicomSourceEvidence> ctafUnicomSourceEvidence;
    bool omitDepartureCtafUnicomAdvisoryDecisionForDiagnostics = false;
    bool omitArrivalCtafUnicomAdvisoryDecisionForDiagnostics = false;
    bool incompleteDepartureCtafUnicomAdvisoryDecisionForDiagnostics = false;
    bool incompleteArrivalCtafUnicomAdvisoryDecisionForDiagnostics = false;
    bool verificationPending = false;
    std::string publishReason;
    std::string productPlanKey;
    std::string productPlanKeySource;
    std::string productPlanKeyMissingReason;
    bool sourceOwnedFallbackStableKeyShadowEnabled = false;
    std::string sourceOwnedFallbackStableKeyShadowGateSource = "default";
    bool sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled = false;
    std::string
        sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource =
            "default";
    bool sourceOwnedFallbackStableKeyLiveConsumptionEnabled = false;
    std::string sourceOwnedFallbackStableKeyLiveConsumptionGateSource =
        "default";
};

struct BrainOwnedPublisherFactInput {
    WorkflowStage workflowStage = WorkflowStage::None;
    RadioStateSnapshot radios;
    ModuleBoardSnapshot departureBoard;
    ModuleBoardSnapshot arrivalBoard;
    ModuleBoardSnapshot enrouteBoard;
    std::vector<BrainOwnedCandidateCompletion> completions;
    BrainOwnedCtafLookupFact departureCtaf;
    BrainOwnedCtafLookupFact arrivalCtaf;
    bool verificationPending = false;
    std::string publishReason;
    std::string productPlanKey;
    std::string productPlanKeySource;
    std::string productPlanKeyMissingReason;
    bool sourceOwnedFallbackStableKeyShadowEnabled = false;
    std::string sourceOwnedFallbackStableKeyShadowGateSource = "default";
    bool sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled = false;
    std::string
        sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource =
            "default";
    bool sourceOwnedFallbackStableKeyLiveConsumptionEnabled = false;
    std::string sourceOwnedFallbackStableKeyLiveConsumptionGateSource =
        "default";
};

struct BrainOwnedPublisherOutput {
    ModuleBoardSnapshot departureBoard;
    ModuleBoardSnapshot arrivalBoard;
    ModuleBoardSnapshot enrouteBoard;
    FinalDisplaySnapshot finalDisplay;
    BrainDisplayIntentOutput displayIntent;
    PhaseSnapshotPublishResult phasePublish;
    std::string phasePublisherStateSummary;
    std::vector<BrainOwnedCtafUnicomSourceEvidence> ctafUnicomSourceEvidence;
    std::vector<BrainOwnedCtafUnicomProjectionEvidence> ctafUnicomProjectionEvidence;
    BrainOwnedCtafUnicomEvidenceSummary ctafUnicomEvidenceSummary;
    std::vector<BrainOwnedCtafUnicomAdvisoryPreviewDecision>
        ctafUnicomAdvisoryPreviewDecisions;
    std::vector<BrainOwnedStandbyAssistAdvisoryCandidate>
        ctafUnicomStandbyAdvisoryCandidates;
    BrainOwnedCtafUnicomAdvisoryPreviewSummary
        ctafUnicomAdvisoryPreviewSummary;
    BrainOwnedCtafUnicomAdvisoryAuthoritySummary
        ctafUnicomAdvisoryAuthoritySummary;
    std::vector<BrainOwnedCtafUnicomBypassAuditDecision>
        ctafUnicomBypassAuditDecisions;
    BrainOwnedCtafUnicomBypassAuditSummary
        ctafUnicomBypassAuditSummary;
    std::vector<BrainOwnedCtafUnicomMissingEvidenceAuditDecision>
        ctafUnicomMissingEvidenceAuditDecisions;
    BrainOwnedCtafUnicomMissingEvidenceAuditSummary
        ctafUnicomMissingEvidenceAuditSummary;
    std::vector<BrainOwnedCtafUnicomLegacyBypassAliasAuditDecision>
        ctafUnicomLegacyBypassAliasAuditDecisions;
    BrainOwnedCtafUnicomLegacyBypassAliasAuditSummary
        ctafUnicomLegacyBypassAliasAuditSummary;
    std::vector<BrainOwnedCtafUnicomPublicUnknownAliasConsumerAuditDecision>
        ctafUnicomPublicUnknownAliasConsumerAuditDecisions;
    BrainOwnedCtafUnicomPublicUnknownAliasConsumerAuditSummary
        ctafUnicomPublicUnknownAliasConsumerAuditSummary;
    std::vector<BrainOwnedCtafUnicomExternalAliasDeprecationDecision>
        ctafUnicomExternalAliasDeprecationDecisions;
    BrainOwnedCtafUnicomExternalAliasDeprecationSummary
        ctafUnicomExternalAliasDeprecationSummary;
    std::vector<BrainOwnedCtafUnicomPublicHeaderAliasRiskClosureDecision>
        ctafUnicomPublicHeaderAliasRiskClosureDecisions;
    BrainOwnedCtafUnicomPublicHeaderAliasRiskClosureSummary
        ctafUnicomPublicHeaderAliasRiskClosureSummary;
    int rejectedUnapprovedStations = 0;
};

void ResetBrainOwnedRuntimeState(BrainOwnedRuntimeState* state);
void ResetBrainOwnedRuntimeCachePreservingFlightContext(
    BrainOwnedRuntimeState* state);
void ResetBrainOwnedDisplayPublisherState(BrainOwnedRuntimeState* state);

void CommitBrainOwnedLastSampledFacts(
    BrainOwnedRuntimeState* state,
    const AircraftStateSnapshot& aircraftState,
    const PilotIdentitySnapshot& pilotIdentity,
    const FlightPlanSnapshot& flightPlan,
    const NetworkPlanSnapshot& networkPlan);

void ClearBrainOwnedLastSampledFacts(BrainOwnedRuntimeState* state);

std::string BuildBrainOwnedPlanIdentityKey(
    std::string callsign,
    std::string departureIcao,
    std::string destinationIcao);

std::string BuildBrainOwnedNetworkPlanIdentityKey(
    const NetworkPlanSnapshot& networkPlanSnapshot);

void CommitBrainOwnedFlightContext(
    BrainOwnedRuntimeState* state,
    const workflow::FlightContext& flightContext);

void ClearBrainOwnedFlightContext(BrainOwnedRuntimeState* state);

void ClearBrainOwnedXPilotConnectionTracking(BrainOwnedRuntimeState* state);

void ClearBrainOwnedFlightRecoveryRequests(BrainOwnedRuntimeState* state);

void SetBrainOwnedAutomaticFlightRecoveryPending(
    BrainOwnedRuntimeState* state,
    bool pending);

void SetBrainOwnedManualFlightRecoveryRequested(
    BrainOwnedRuntimeState* state,
    bool requested);

void SetBrainOwnedColdDarkResetApplied(
    BrainOwnedRuntimeState* state,
    bool applied);

void ClearBrainOwnedAircraftStateInvalidBoundary(
    BrainOwnedRuntimeState* state);

void ApplyBrainOwnedXPilotSessionBoundaryDecision(
    BrainOwnedRuntimeState* state,
    const workflow::XPilotSessionBoundaryDecision& decision);

void ApplyBrainOwnedAircraftRuntimeBoundaryDecision(
    BrainOwnedRuntimeState* state,
    const workflow::AircraftRuntimeBoundaryDecision& decision);

void ResetBrainOwnedStandbyAssistLatch(BrainOwnedRuntimeState* state);

void ClearBrainOwnedDiversionOverrideSource(BrainOwnedRuntimeState* state);

void SetBrainOwnedDiversionOverrideSourceKey(
    BrainOwnedRuntimeState* state,
    const std::string& sourcePlanKey);

BrainOwnedDiversionOverrideDecision DecideBrainOwnedDiversionOverride(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedDiversionOverrideInput& input);

void ClearBrainOwnedPreflightRouteCacheApplication(
    BrainOwnedRuntimeState* state);

void SetBrainOwnedDisplayOverrideMode(
    BrainOwnedRuntimeState* state,
    BrainOwnedDisplayOverrideMode mode);

void ClearBrainOwnedManualQuery(BrainOwnedRuntimeState* state);

void SetBrainOwnedPendingTextEntryMode(
    BrainOwnedRuntimeState* state,
    BrainOwnedTextEntryMode mode);

void ClearBrainOwnedPendingTextEntryMode(BrainOwnedRuntimeState* state);

BrainOwnedTextEntryMode ConsumeBrainOwnedPendingTextEntryMode(
    BrainOwnedRuntimeState* state);

bool HasBrainOwnedPendingTextEntry(const BrainOwnedRuntimeState& state);

void ShowBrainOwnedManualQueryLine(
    BrainOwnedRuntimeState* state,
    const std::string& line,
    long long visibleUntilSeconds);

void CommitBrainOwnedManualQuerySnapshot(
    BrainOwnedRuntimeState* state,
    ManualQuerySnapshot snapshot,
    long long visibleUntilSeconds);

void ExpireBrainOwnedManualQuery(
    BrainOwnedRuntimeState* state,
    long long nowSeconds);

void ResetBrainOwnedControllerMessageState(BrainOwnedRuntimeState* state);

void ClearBrainOwnedControllerMessage(BrainOwnedRuntimeState* state);

void RecallBrainOwnedControllerMessage(BrainOwnedRuntimeState* state);

void UpdateBrainOwnedControllerMessageState(
    BrainOwnedRuntimeState* state,
    const XPilotPrivateMessageSnapshot& messageSnapshot,
    bool controllerMessageUiEnabled);

BrainOwnedPreflightRouteCacheDecision BeginBrainOwnedPreflightRouteCacheApplication(
    BrainOwnedRuntimeState* state,
    const BrainOwnedPreflightRouteCacheInput& input);

BrainOwnedPreflightRouteCacheValidationDecision
DecideBrainOwnedPreflightRouteCacheValidation(
    const BrainOwnedPreflightRouteCacheValidationInput& input);

std::string ToString(BrainOwnedCandidateDecision decision);

std::string BuildBrainOwnedCandidateCompletionKey(
    std::uint64_t radioBoardHash,
    std::uint64_t routePolygonHash,
    WorkflowStage workflowStage,
    const std::string& currentPolygonKey,
    const RadioReachableControllerCandidate& candidate);

void RecordBrainOwnedCandidateCompletion(
    BrainOwnedRuntimeState* state,
    BrainOwnedCandidateCompletion completion);

BrainOwnedBoardFilterOutput FilterBrainOwnedBoardByAcceptedCompletions(
    const ModuleBoardSnapshot& board,
    const std::vector<BrainOwnedCandidateCompletion>& completions);

BrainOwnedRadioBoardReuseOutput TryReuseBrainOwnedRadioBoard(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedRadioBoardReuseInput& input);

BrainOwnedRadioBoardCommitOutput CommitBrainOwnedRadioBoardRefresh(
    BrainOwnedRuntimeState* state,
    const BrainOwnedRadioBoardCommitInput& input);

BrainTerminalAuthorityWorkerOutput RefreshBrainOwnedDepartureTerminalAuthority(
    BrainOwnedRuntimeState* state,
    const workflow::FlightContext& flightContext,
    long long nowSeconds,
    BrainTerminalAuthorityWorker* worker);

BrainTerminalAuthorityWorkerOutput RefreshBrainOwnedArrivalTerminalAuthority(
    BrainOwnedRuntimeState* state,
    const workflow::FlightContext& flightContext,
    long long nowSeconds,
    BrainTerminalAuthorityWorker* worker);

BrainAirportFrequencyWorkerOutput RefreshBrainOwnedAirportFrequencies(
    BrainOwnedRuntimeState* state,
    const workflow::FlightContext& flightContext,
    long long nowSeconds,
    BrainAirportFrequencyWorker* worker);

BrainOwnedTerminalAuthorityRefreshPlan BeginBrainOwnedDepartureTerminalAuthorityRefresh(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedTerminalAuthorityRefreshInput& input);

BrainOwnedTerminalAuthorityRefreshPlan BeginBrainOwnedArrivalTerminalAuthorityRefresh(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedTerminalAuthorityRefreshInput& input);

void CommitBrainOwnedDepartureTerminalAuthorityRefresh(
    BrainOwnedRuntimeState* state,
    const BrainOwnedTerminalAuthorityRefreshPlan& plan,
    const BrainTerminalAuthorityWorkerOutput& workerOutput);

void CommitBrainOwnedArrivalTerminalAuthorityRefresh(
    BrainOwnedRuntimeState* state,
    const BrainOwnedTerminalAuthorityRefreshPlan& plan,
    const BrainTerminalAuthorityWorkerOutput& workerOutput);

BrainOwnedAirportFrequencyRefreshPlan BeginBrainOwnedAirportFrequencyRefresh(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedAirportFrequencyRefreshInput& input);

void CommitBrainOwnedAirportFrequencyRefresh(
    BrainOwnedRuntimeState* state,
    const BrainOwnedAirportFrequencyRefreshPlan& plan,
    const BrainAirportFrequencyWorkerOutput& workerOutput);

RadioReachableControllerSnapshot RunBrainOwnedRadioPhaseGate(
    BrainOwnedRuntimeState* state,
    const RadioReachableControllerSnapshot& radioSnapshot,
    WorkflowStage workflowStage,
    const std::string& reason);

void CommitBrainOwnedPublishedRuntime(
    BrainOwnedRuntimeState* state,
    const BrainOwnedPublishedRuntimeInput& input);

BrainOwnedOverlayWakeDecision DecideBrainOwnedOverlayWake(
    const BrainOwnedOverlayWakeInput& input);

void ResetBrainOwnedEnrouteInitialHold(BrainOwnedRuntimeState* state);

BrainOwnedEnrouteInitialHoldOutput UpdateBrainOwnedEnrouteInitialHold(
    BrainOwnedRuntimeState* state,
    const BrainOwnedEnrouteInitialHoldInput& input);

BrainOwnedFlightPlanSampleDecision DecideBrainOwnedFlightPlanSample(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedFlightPlanSampleInput& input);

void CommitBrainOwnedFlightPlanSample(
    BrainOwnedRuntimeState* state,
    const BrainOwnedFlightPlanSampleCommitInput& input);

void ResetBrainOwnedCruiseTarget(BrainOwnedRuntimeState* state);

BrainOwnedCruiseTargetPlanOutput SyncBrainOwnedCruiseTargetFromNetworkPlan(
    BrainOwnedRuntimeState* state,
    const BrainOwnedCruiseTargetPlanInput& input);

BrainOwnedCruiseTargetCommandOutput ApplyBrainOwnedCruiseTargetCommand(
    BrainOwnedRuntimeState* state,
    const BrainOwnedCruiseTargetCommandInput& input);

void UpdateBrainOwnedCruiseTargetProgress(
    BrainOwnedRuntimeState* state,
    const BrainOwnedCruiseTargetProgressInput& input);

std::string BuildBrainOwnedCruiseTargetHeaderText(
    const BrainOwnedRuntimeState& state);

void ResetBrainOwnedWorkflowProgress(BrainOwnedRuntimeState* state);

void ResetBrainOwnedWorkflowArrivalWake(BrainOwnedRuntimeState* state);

workflow::WorkflowState BuildBrainOwnedWorkflowState(
    const BrainOwnedRuntimeState& state);

void CommitBrainOwnedWorkflowState(
    BrainOwnedRuntimeState* state,
    const workflow::WorkflowState& workflowState);

BrainOwnedWorkflowSelectionOutput ResolveBrainOwnedWorkflowSelection(
    BrainOwnedRuntimeState* state,
    const BrainOwnedWorkflowSelectionInput& input);

void ApplyBrainOwnedWorkflowRecoveryStage(
    BrainOwnedRuntimeState* state,
    WorkflowStage stage,
    double nowSeconds);

void SetBrainOwnedXPilotConnectedSeen(
    BrainOwnedRuntimeState* state,
    bool seen);

void MarkBrainOwnedXPilotConnectedIfConnected(
    BrainOwnedRuntimeState* state,
    const XPilotSessionSnapshot& xPilotSession);

BrainOwnedStandbyAssistPlanOutput BuildBrainOwnedStandbyAssistPlan(
    const BrainOwnedStandbyAssistPlanInput& input);

BrainOwnedStandbyAssistSideEffectDecision
DecideBrainOwnedStandbyAssistSideEffect(
    BrainOwnedRuntimeState* state,
    const BrainOwnedStandbyAssistPlanOutput& plan,
    bool standbyAssistEnabled);

BrainOwnedStandbyAssistWriterResult
BuildBrainOwnedStandbyAssistWriterResult(
    const BrainOwnedStandbyAssistSideEffectDecision& decision,
    bool writeSucceeded);

BrainOwnedStandbyAssistWriterResult
BuildBrainOwnedStandbyAssistWriterResultFromCode(
    const BrainOwnedStandbyAssistSideEffectDecision& decision,
    const std::string& writerResultCode);

BrainOwnedStandbyAssistSideEffectDecision
CompleteBrainOwnedStandbyAssistSideEffectDecision(
    const BrainOwnedStandbyAssistPlanOutput& plan,
    BrainOwnedStandbyAssistSideEffectDecision decision,
    bool standbyLoaded);

BrainOwnedStandbyAssistSideEffectDecision
CompleteBrainOwnedStandbyAssistSideEffectDecision(
    const BrainOwnedStandbyAssistPlanOutput& plan,
    BrainOwnedStandbyAssistSideEffectDecision decision,
    const BrainOwnedStandbyAssistWriterResult& writerResult);

FinalDisplaySnapshot ApplyBrainOwnedStandbyAssistResult(
    const BrainOwnedStandbyAssistPlanOutput& plan,
    bool standbyLoaded);

BrainOwnedPublisherInput BuildBrainOwnedPublisherInputFromFacts(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedPublisherFactInput& facts);

void MarkBrainOwnedDisplayedCompletionsFromFinalDisplay(
    BrainOwnedRuntimeState* state,
    const FinalDisplaySnapshot& finalDisplay);

BrainOwnedPublisherOutput RunBrainOwnedPublisher(
    BrainOwnedRuntimeState* state,
    const BrainOwnedPublisherInput& input);

void CommitBrainOwnedPublishedRuntimeFromPublisherOutput(
    BrainOwnedRuntimeState* state,
    WorkflowStage workflowStage,
    const std::string& planKey,
    const RadioReachableControllerSnapshot& gatedRadioSnapshot,
    const BrainOwnedPublisherOutput& publisherOutput,
    const FinalDisplaySnapshot& finalDisplay);

bool BrainOwnedCandidatesCompleteForCurrentBoard(
    const BrainOwnedRuntimeState& state,
    const RadioReachableControllerSnapshot& radioSnapshot,
    WorkflowStage workflowStage,
    const std::string& currentPolygonKey);

std::string BrainOwnedRuntimeStateSummary(const BrainOwnedRuntimeState& state);

}  // namespace xvatsim::brain
