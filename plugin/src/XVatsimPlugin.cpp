#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "XVatsim/brain/BrainOrchestrator.h"
#include "XVatsim/brain/BrainDisplayIntent.h"
#include "XVatsim/brain/BrainOwnedRuntime.h"
#include "XVatsim/brain/BrainOwnedWorkerTypes.h"
#include "XVatsim/brain/BrainWorkflow.h"
#include "XVatsim/brain/BrainWorkModel.h"
#include "XVatsim/brain/RadioReachableSnapshot.h"
#include "XVatsim/core/PreflightRouteCache.h"
#include "XVatsim/modules/aircraft_state/AircraftStateSampler.h"
#include "XVatsim/modules/ctaf_lookup/CtafLookupService.h"
#include "XVatsim/modules/controller_feed/ControllerFeedClient.h"
#include "XVatsim/modules/diversion_context/DiversionContextModule.h"
#include "XVatsim/modules/flight_plan/FlightPlanSampler.h"
#include "XVatsim/modules/network_plan_link/NetworkPlanLink.h"
#include "XVatsim/modules/overlay/OverlayWindow.h"
#include "XVatsim/modules/pilot_identity/PilotIdentityResolver.h"
#include "XVatsim/modules/radio_state/RadioStateSampler.h"
#include "XVatsim/modules/route_sector/RouteSectorResolver.h"
#include "XVatsim/modules/runtime_workers/AsyncDiagnosticsWriter.h"
#include "XVatsim/modules/runtime_workers/AsyncFactWorkerHost.h"
#include "XVatsim/modules/settings_store/SettingsStore.h"
#include "XVatsim/modules/terminal_authority/TerminalAuthorityResolver.h"
#include "XVatsim/modules/transceiver_resolver/TransceiverResolver.h"
#include "XVatsim/modules/update_checker/UpdateChecker.h"
#include "XVatsim/modules/vatsim_data_feed/VatsimDataFeedClient.h"
#include "XVatsim/modules/vnas_data/VnasDataClient.h"
#include "XVatsim/modules/xpilot_bridge/XPilotBridge.h"
#if defined(XVATSIM_STEP3_LIVE_PROOF_FIXTURES)
#include "Step3LiveProofFixtures.h"
#endif
#include "XPLMMenus.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

namespace xvatsim::modules::overlay {
std::string DescribeLastOverlayUpdateTiming();
}

namespace {
constexpr char kPluginName[] = "XVatsim";
constexpr char kInstalledPluginVersion[] = "2.0.1";
constexpr char kPluginSig[] = "org.xvatsim.plugin";
constexpr char kPluginDesc[] = "XVatsim VATSIM workflow display for X-Plane 12.";
constexpr char kUpdateManifestUrl[] =
    "https://ezsimulations.github.io/XVatsim/xvatsim_update.json";
constexpr char kManualCtafCommandName[] = "xvatsim/manual_ctaf_lookup";
constexpr char kManualCtafCommandDesc[] = "Open the XVatsim manual CTAF lookup prompt.";
constexpr char kDisplayOpenCommandName[] = "xvatsim/display_open";
constexpr char kDisplayOpenCommandDesc[] = "Force the XVatsim display open.";
constexpr char kDisplayCloseCommandName[] = "xvatsim/display_close";
constexpr char kDisplayCloseCommandDesc[] = "Force the XVatsim display closed.";
constexpr char kDisplayAutoCommandName[] = "xvatsim/display_auto";
constexpr char kDisplayAutoCommandDesc[] = "Return the XVatsim display to automatic behavior.";
constexpr char kCruiseTargetCurrentCommandName[] = "xvatsim/cruise_target_current";
constexpr char kCruiseTargetCurrentCommandDesc[] =
    "Set the XVatsim cruise target to the current aircraft altitude.";
constexpr char kCruiseTargetFiledCommandName[] = "xvatsim/cruise_target_filed";
constexpr char kCruiseTargetFiledCommandDesc[] =
    "Reset the XVatsim cruise target to the filed VATSIM altitude.";
constexpr char kResetSessionCommandName[] = "xvatsim/reset_session";
constexpr char kResetSessionCommandDesc[] =
    "Reset XVatsim state for the next flight.";
constexpr char kRecoverCurrentFlightCommandName[] = "xvatsim/recover_current_flight";
constexpr char kRecoverCurrentFlightCommandDesc[] =
    "Recover XVatsim workflow state for the current flight.";
constexpr float kUpdateIntervalSeconds = 0.25f;
constexpr float kInitialFlightLoopDelaySeconds = 10.0f;
constexpr float kNextFlightLoopInterval = -1.0f;
constexpr std::size_t kAccessoryInputFactsPerCycle = 8;
constexpr std::uint64_t kAccessorySynchronousBudgetMicroseconds = 16'700;
constexpr std::uint64_t kAccessoryTerminalBudgetMicroseconds = 500'000;
constexpr long long kManualQueryVisibleSeconds = 20;
constexpr double kCruiseGateToleranceFt = 1000.0;
constexpr double kCruiseGateStableVsFpm = 800.0;
constexpr float kCruiseGateDwellSeconds = 10.0f;
constexpr intptr_t kManualCtafMenuItemRef = 1;
constexpr intptr_t kDisplayOpenMenuItemRef = 2;
constexpr intptr_t kDisplayCloseMenuItemRef = 3;
constexpr intptr_t kDisplayAutoMenuItemRef = 4;
constexpr intptr_t kOpacityUpMenuItemRef = 5;
constexpr intptr_t kOpacityDownMenuItemRef = 6;
constexpr intptr_t kScaleUpMenuItemRef = 7;
constexpr intptr_t kScaleDownMenuItemRef = 8;
constexpr intptr_t kAnimationFasterMenuItemRef = 9;
constexpr intptr_t kAnimationSlowerMenuItemRef = 10;
constexpr intptr_t kResetAppearanceMenuItemRef = 11;
constexpr intptr_t kCruiseTargetCurrentMenuItemRef = 12;
constexpr intptr_t kCruiseTargetFiledMenuItemRef = 13;
constexpr intptr_t kResetSessionMenuItemRef = 14;
constexpr intptr_t kStandbyAssistOnMenuItemRef = 15;
constexpr intptr_t kStandbyAssistOffMenuItemRef = 16;
constexpr intptr_t kSetDiversionAirportMenuItemRef = 17;
constexpr intptr_t kRevertToFlightPlanMenuItemRef = 18;
constexpr intptr_t kRecoverCurrentFlightMenuItemRef = 19;
constexpr intptr_t kCheckForUpdatesMenuItemRef = 20;
constexpr intptr_t kIfrModeMenuItemRef = 21;
constexpr intptr_t kVfrModeMenuItemRef = 22;
constexpr intptr_t kSelectMetarAirportMenuItemRef = 23;
constexpr intptr_t kSelectAtisAirportMenuItemRef = 24;
constexpr double kArrivalWakeDistanceNm = 200.0;
constexpr double kTerminalTransmitterRadiusNm = 5.0;
constexpr float kDepartureReleaseHoldSeconds = 180.0f;
constexpr float kEnrouteInitialDisplaySeconds = 180.0f;
constexpr float kOpacityStep = 0.10f;
constexpr float kScaleStep = 0.05f;
constexpr float kAnimationSpeedStep = 0.10f;
constexpr std::size_t kMaxLogFieldChars = 80;
constexpr long long kDiagnosticsSlowRefreshThresholdMs = 33;
constexpr long long kDiagnosticsSlowRefreshLogIntervalSeconds = 10;
constexpr long long kDiagnosticsSummaryIntervalSeconds = 30;
constexpr int kDiagnosticsRetainedDateLogCount = 3;
constexpr long long kRadioBoardSnapshotCadenceSeconds = 1;
constexpr long long kRadioBoardPendingRouteRetrySeconds = 2;
constexpr long long kExpandedFmsObservationIntervalMs = 15000;
constexpr long long kActiveFlightPlanSampleCadenceSeconds = 15;
constexpr long long kEngineer3RadioBoardRefreshSeconds = 5;
constexpr long long kSettledOperationalRefreshIntervalMs = 1000;
constexpr std::size_t kDiagnosticsMaxTraceItems = 24;
constexpr bool kDiagnosticsVerboseUnchangedCandidateDiff = false;

using HandoffDecision = xvatsim::brain::workflow::HandoffDecision;
using SessionBoundaryResult =
    xvatsim::brain::workflow::XPilotSessionBoundaryAction;

struct DiagnosticJobRecord {
    std::string name;
    std::string reason;
    std::string stage;
    std::string cacheStatus;
    std::string result;
    std::string sourceGenerations;
    std::string routeKey;
    long long durationMs = 0;
};

struct RefreshDiagnosticsFrame {
    bool valid = false;
    bool collectJobs = false;
    bool settledFastPath = false;
    bool flightContextActive = false;
    bool xpilotConnected = false;
    bool onGround = false;
    bool batteryOn = false;
    std::string callsign;
    std::string route;
    std::string stage;
    std::string stageReason;
    std::string wakeReason;
    bool shouldWake = false;
    bool routeResolved = false;
    std::string routeStatus;
    std::string authorityStatus;
    std::string vnasStatus;
    int controllerCount = 0;
    int authorityCount = 0;
    int enrouteStationCount = 0;
    std::string authorityProofSummary;
    std::shared_ptr<const xvatsim::brain::AuthorityRelevanceSnapshot>
        authoritySnapshotForDiagnostics;
    bool hasAuthorityProofHash = false;
    std::size_t authorityProofHash = 0;
    long long xpilotPollMs = 0;
    long long vatsimFeedMs = 0;
    long long vnasFeedMs = 0;
    long long controllerFeedMs = 0;
    long long flightPlanMs = 0;
    long long networkPlanMs = 0;
    long long radioMs = 0;
    long long ctafMs = 0;
    long long departureBoardMs = 0;
    long long arrivalBoardMs = 0;
    long long routeResolveMs = 0;
    long long routeAuthorityPlanMs = 0;
    long long authorityStationsMs = 0;
    long long authorityRelevanceMs = 0;
    long long enrouteBoardMs = 0;
    long long workflowMs = 0;
    long long radioRangeResolveMs = 0;
    long long overlayBuildMs = 0;
    long long overlayUpdateMs = 0;
    long long aircraftStateUs = 0;
    long long xpilotPollUs = 0;
    long long vatsimFeedUs = 0;
    long long vnasFeedUs = 0;
    long long controllerFeedUs = 0;
    long long flightPlanUs = 0;
    long long networkPlanUs = 0;
    long long radioUs = 0;
    long long pdcPrivateSourceUs = 0;
    long long manualQueryUs = 0;
    long long flightContextUs = 0;
    long long ctafUs = 0;
    long long routeResolveUs = 0;
    long long routeAuthorityPlanUs = 0;
    long long authorityStationsUs = 0;
    long long authorityRelevanceUs = 0;
    long long departureBoardUs = 0;
    long long arrivalBoardUs = 0;
    long long enrouteBoardUs = 0;
    long long workflowUs = 0;
    long long standbyAssistUs = 0;
    long long wakeDecisionUs = 0;
    long long radioRangeResolveUs = 0;
    long long overlayBuildUs = 0;
    long long overlayUpdateUs = 0;
    long long displayLoggingUs = 0;
    long long refreshGateUs = 0;
    std::string refreshGateReason;
    std::vector<DiagnosticJobRecord> jobs;
};

struct PluginDiagnosticsState {
    long long lastFlightLoopPerfWarningSeconds = 0;
    long long lastSlowRefreshSeconds = 0;
    long long lastSummarySeconds = 0;
    bool hasLastAuthorityHash = false;
    std::size_t lastAuthorityHash = 0;
    bool hasLastRadioBoardTraceHash = false;
    std::size_t lastRadioBoardTraceHash = 0;
    bool hasLastCompletionTraceHash = false;
    std::size_t lastCompletionTraceHash = 0;
    RefreshDiagnosticsFrame frame;
};

struct AccessoryInputRuntimeAccounting {
    std::uint64_t wakeRequests = 0;
    std::uint64_t wakeNotificationSequence = 0;
    std::uint64_t brainCycles = 0;
    std::uint64_t factsConsumed = 0;
    std::uint64_t brainDecisions = 0;
    std::uint64_t commandsIssued = 0;
    std::uint64_t terminalFacts = 0;
    std::uint64_t clickTerminalFacts = 0;
    std::uint64_t lifecycleDiscards = 0;
    std::uint64_t callbackOrderFailures = 0;
    std::uint64_t synchronousBudgetFailures = 0;
    std::uint64_t livenessFailures = 0;
    std::uint64_t activeCadenceReturns = 0;
    std::uint64_t normalCadenceReturns = 0;
    std::uint64_t maximumSynchronousMicroseconds = 0;
    std::uint64_t maximumClickToTerminalMicroseconds = 0;
    std::uint64_t lastObservedNotificationSequence = 0;
    xvatsim::modules::overlay::AccessoryPublicationDiagnosticAccounting
        publication;
};

struct TerminalEvidenceRadioRuntimeCache {
    bool valid = false;
    std::uint64_t baseRadioHash = 0;
    const xvatsim::brain::TransceiverResolutionSnapshot*
        transceiverIdentity = nullptr;
    xvatsim::brain::RadioReachableControllerSnapshot augmented;
};

xvatsim::modules::aircraft_state::AircraftStateSampler gAircraftStateSampler;
xvatsim::modules::ctaf_lookup::CtafLookupService gCtafLookupService;
xvatsim::modules::controller_feed::ControllerFeedClient gControllerFeedClient;
xvatsim::modules::diversion_context::DiversionContextModule gDiversionContextModule;
xvatsim::modules::flight_plan::FlightPlanSampler gFlightPlanSampler;
xvatsim::modules::network_plan_link::NetworkPlanLink gNetworkPlanLink;
xvatsim::modules::overlay::OverlayWindow gOverlayWindow;
xvatsim::modules::pilot_identity::PilotIdentityResolver gPilotIdentityResolver;
xvatsim::modules::radio_state::RadioStateSampler gRadioStateSampler;
xvatsim::modules::route_sector::RouteSectorResolver gRouteSectorResolver;
xvatsim::modules::route_sector::AuthorityRelevanceWorker gAuthorityRelevanceWorker;
xvatsim::modules::route_sector::RoutePreparationWorker gRoutePreparationWorker;
xvatsim::modules::runtime_workers::AsyncDiagnosticsWriter gDiagnosticsWriter;
xvatsim::modules::runtime_workers::AsyncFactWorkerHost gAsyncFactWorkerHost;
xvatsim::modules::settings_store::SettingsStore gSettingsStore;
xvatsim::modules::terminal_authority::TerminalAuthorityResolver
    gTerminalAuthorityResolver;
xvatsim::modules::transceiver_resolver::TransceiverResolver gTransceiverResolver;
xvatsim::modules::update_checker::UpdateChecker gUpdateChecker;
xvatsim::modules::vatsim_data_feed::VatsimDataFeedClient gVatsimDataFeedClient;
xvatsim::modules::vnas_data::VnasDataClient gVnasDataClient;
xvatsim::modules::xpilot_bridge::XPilotBridge gXPilotBridge;
xvatsim::modules::xpilot_bridge::XPilotPrivateObservationQueue
    gXPilotPrivateObservationQueue;
xvatsim::modules::xpilot_bridge::XPilotPrivateQualificationEventLatch
    gXPilotPrivateQualificationEventLatch;
xvatsim::modules::settings_store::PluginSettings gPluginSettings;
XPLMCommandRef gManualCtafCommand = nullptr;
XPLMCommandRef gDisplayOpenCommand = nullptr;
XPLMCommandRef gDisplayCloseCommand = nullptr;
XPLMCommandRef gDisplayAutoCommand = nullptr;
XPLMCommandRef gCruiseTargetCurrentCommand = nullptr;
XPLMCommandRef gCruiseTargetFiledCommand = nullptr;
XPLMCommandRef gResetSessionCommand = nullptr;
XPLMCommandRef gRecoverCurrentFlightCommand = nullptr;
XPLMMenuID gPluginMenu = nullptr;
int gPluginMenuItemIndex = -1;
int gIfrModeMenuItemIndex = -1;
int gVfrModeMenuItemIndex = -1;
bool gFlightLoopRegistered = false;
bool gPluginRuntimeEnabled = false;
xvatsim::brain::BrainOwnedRuntimeState gBrainOwnedRuntimeState;
xvatsim::brain::BrainOwnedOperationalActivationState
    gOperationalActivationState;
#if defined(XVATSIM_STEP3_LIVE_PROOF_FIXTURES)
xvatsim::plugin::step3_live_proof::Step3LiveProofFixtureSession
    gStep3LiveProofFixtureSession;
#endif
PluginDiagnosticsState gDiagnosticsState;
AccessoryInputRuntimeAccounting gAccessoryInputAccounting;
TerminalEvidenceRadioRuntimeCache gTerminalEvidenceRadioCache;
std::uint64_t gFlightLoopTimingSequence = 0;

struct AuthorityAsyncRuntimeState {
    std::uint64_t lifecycleEpoch = 1;
    std::uint64_t nextRequestId = 1;
    bool hasExpectedIdentity = false;
    xvatsim::brain::BrainAuthorityCompletionIdentity expectedIdentity;
    long long lastDispatchMonotonicMs = 0;
    std::shared_ptr<const xvatsim::brain::AuthorityRelevanceSnapshot>
        acceptedSnapshot;
    std::shared_ptr<xvatsim::modules::route_sector::AuthoritySnapshotLease>
        acceptedSnapshotLease;
    std::uint64_t acceptedSnapshotDigest = 0;
    xvatsim::brain::BrainAuthorityCompletionIdentity acceptedIdentity;
    std::uint64_t dispatchCount = 0;
    std::uint64_t acceptedCount = 0;
    std::uint64_t staleRejectedCount = 0;
    std::uint64_t invalidationCount = 0;
    bool hasControllerDigest = false;
    std::uint64_t observedControllerGeneration = 0;
    std::uint64_t observedTransceiverGeneration = 0;
    std::uint64_t controllerGenerationOnlyRefreshCount = 0;
    std::uint64_t transceiverUnchangedRefreshCount = 0;
    std::uint64_t controllerDigest = 0;
};

AuthorityAsyncRuntimeState gAuthorityAsyncRuntime;

struct RouteAsyncRuntimeState {
    std::uint64_t lifecycleEpoch = 1;
    std::uint64_t nextRequestId = 1;
    bool hasDesiredIdentity = false;
    xvatsim::modules::route_sector::RouteWorkerIdentity desiredIdentity;
    bool hasEligibleRequest = false;
    xvatsim::modules::route_sector::RoutePreparationRequest::Kind eligibleKind =
        xvatsim::modules::route_sector::RoutePreparationRequest::Kind::PrepareRoute;
    xvatsim::modules::route_sector::RouteWorkerIdentity eligibleRequest;
    xvatsim::modules::route_sector::RouteWorkerIdentity acceptedIdentity;
    std::shared_ptr<const xvatsim::brain::RouteSectorSnapshot> acceptedRoute;
    std::shared_ptr<xvatsim::modules::route_sector::RouteSnapshotLease>
        acceptedLease;
    std::uint64_t acceptedDigest = 0;
    std::uint64_t dispatchCount = 0;
    std::uint64_t acceptedCount = 0;
    std::uint64_t staleRejectedCount = 0;
    std::uint64_t failureCount = 0;
    std::uint64_t invalidationCount = 0;
    std::uint64_t pendingReplacementCount = 0;
    std::uint64_t observedSourceIdentity = 0;
    xvatsim::modules::route_sector::RouteAnchorSelectionState anchorSelection;
    std::uint64_t fmsObservationNetworkPlanDigest = 0;
    std::uint64_t fmsObservationIdentity = 0;
    bool fmsObservationReady = false;
    long long nextFmsObservationMonotonicMs = 0;
    long long nextRouteRetryMonotonicMs = 0;
    std::uint64_t fmsObservationDispatchCount = 0;
    std::uint64_t fmsObservationAcceptedCount = 0;
    std::uint64_t fmsObservationChangedCount = 0;
    std::uint64_t unresolvedRetryDispatchCount = 0;
    std::uint64_t maximumSubmitUs = 0;
    std::uint64_t maximumMailboxExchangeUs = 0;
    std::uint64_t maximumHarvestUs = 0;
    std::uint64_t maximumTransitionUs = 0;
    std::array<
        std::shared_ptr<const xvatsim::brain::RouteSectorSnapshot>,
        64> deferredRouteRetirements;
    std::shared_ptr<const xvatsim::brain::RouteSectorSnapshot>
        invariantFailureRetirement;
    std::uint64_t retirementBacklogPeak = 0;
};

RouteAsyncRuntimeState gRouteAsyncRuntime;
xvatsim::brain::BrainOwnedAccessoryProjectionCounters
    gAccessoryProjectionCounters;
std::uint64_t gLastAccessorySemanticPresentationGeneration = 0;
std::shared_ptr<const xvatsim::core::preflight::PreflightRouteCache>
    gPreflightRouteCacheCandidate;
std::shared_ptr<const xvatsim::core::preflight::PreflightRouteCache>
    gSelectedPreflightRouteCache;
std::string gSelectedPreflightPlanKey;
std::string gSelectedPreflightValidationReason;
std::string gPreflightRouteCachePath;
std::optional<xvatsim::modules::update_checker::UpdateCheckResult>
    gUpdateSessionResult;
bool gManualUpdateNoticeOpen = false;
std::string gDismissedUpdateNoticeVersion;

void RefreshOverlayFromBrain();
void RefreshOverlayFromBrainEngineer3();
void ResetPresentationStateForColdDark();
void ClearFlightRecoveryState();
void ResetDiagnosticsTraceState();
void AppendDiagnosticsLogLine(
    std::string line,
    xvatsim::modules::runtime_workers::DiagnosticsImportance importance =
        xvatsim::modules::runtime_workers::DiagnosticsImportance::Routine);
void AppendDeferredDiagnosticsLogLine(
    xvatsim::modules::runtime_workers::AsyncDiagnosticsWriter::DeferredFormatter
        formatter,
    xvatsim::modules::runtime_workers::DiagnosticsImportance importance =
        xvatsim::modules::runtime_workers::DiagnosticsImportance::Routine);
std::string SanitizeLogText(std::string value, std::size_t maxChars);
void LogMetarAsyncDiagnostics(
    const xvatsim::brain::BrainOwnedAsyncFactCycleOutput& output);
void LogMetarLifecycleDiagnostics(
    const xvatsim::brain::BrainMetarWorkerShutdownSnapshot& snapshot);
xvatsim::brain::BrainMetarWorkerShutdownSnapshot
ApplyBoundAsyncWorkerLifecycleBoundary(bool clearAcceptedState);
xvatsim::brain::BrainOwnedAccessoryPresentationHandle
SynchronizeAccessoryPresentation(
    xvatsim::modules::overlay::AccessoryDispatchStageWallTimings*
        acceptedActionStages = nullptr);
void RequestAccessoryFlightLoopWake(
    std::uint64_t notificationSequence,
    void* refcon);
bool RequestImmediateGatedFlightLoopCallback();
bool ServicePendingAccessoryInput();
void DrainAccessoryPublicationFacts();
void DiscardPendingAccessoryClickFacts();
void LogRadioBoardCandidateDiffTrace(
    xvatsim::brain::WorkflowStage workflowStage,
    const std::string& planKey,
    const xvatsim::brain::RadioReachableControllerSnapshot& snapshot,
    const xvatsim::brain::RadioReachableCandidateDiff& diff);
void LogCandidateCompletionTrace(
    xvatsim::brain::WorkflowStage workflowStage,
    const std::string& planKey);
long long CurrentTickMilliseconds();
void InvalidateAuthorityAsyncRuntime(const char* reason);
void InvalidateRouteAsyncRuntime(const char* reason);

std::string SummarizeRouteAuthorityPlan(
    const xvatsim::brain::RouteAuthorityPlan& plan);
void ApplyPreflightRouteCacheForPlanIfNeeded(
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot);
float FlightLoopCallback(
    float elapsedSinceLastCall,
    float elapsedTimeSinceLastFlightLoop,
    int counter,
    void* refcon);

long long CurrentTickSeconds() {
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

void LogAtisCycleDiagnostic(
    const xvatsim::brain::BrainOwnedAtisCycleDecision& decision) {
    if (!decision.evaluated && !decision.semanticChanged &&
        !decision.lookupCompleted && !decision.lookupExpired &&
        !decision.ownershipLost) {
        return;
    }
    std::ostringstream stream;
    stream << "event=atis-brain-decision"
           << " feedGeneration=" << decision.feedGeneration
           << " evaluated=" << (decision.evaluated ? 1 : 0)
           << " semanticChanged=" << (decision.semanticChanged ? 1 : 0)
           << " historyMutated=" << (decision.historyMutated ? 1 : 0)
           << " unreadCreated=" << (decision.unreadCreated ? 1 : 0)
           << " lookupCompleted=" << (decision.lookupCompleted ? 1 : 0)
           << " lookupExpired=" << (decision.lookupExpired ? 1 : 0)
           << " ownershipLost=" << (decision.ownershipLost ? 1 : 0)
           << " target=" << SanitizeLogText(decision.targetAirportIcao, 4)
           << " availability="
           << xvatsim::brain::ToString(decision.availability)
           << " selectedRevision="
           << SanitizeLogText(decision.selectedRevisionIdentity, 180)
           << " examinedRecords=" << decision.examinedRecords
           << " candidates=" << decision.candidates
           << " evaluationUs=" << decision.evaluationMicroseconds
           << " reason=" << SanitizeLogText(decision.reason, 96);
    AppendDiagnosticsLogLine(stream.str());
}

xvatsim::brain::BrainOwnedAtisCycleDecision RunAtisCycleFromSharedFeed(
    const xvatsim::modules::vatsim_data_feed::VatsimDataFeedSnapshot& feed,
    bool xpilotConnected,
    xvatsim::brain::WorkflowStage workflowStage) {
    xvatsim::brain::BrainOwnedAtisCycleInput input;
    input.pluginEnabled = gPluginRuntimeEnabled;
    input.xpilotConnected = xpilotConnected;
    input.workflowStage = workflowStage;
    input.operatingMode = gBrainOwnedRuntimeState.operatingMode.mode;
    input.flightContext = gBrainOwnedRuntimeState.flightContext;
    input.feedHasCache = feed.hasCache;
    input.feedStale = feed.stale;
    input.feedFetchInProgress = feed.fetchInProgress;
    input.feedGeneration = feed.generation;
    input.atisRootPresent = feed.atisRootPresent;
    input.atisRootArray = feed.atisRootArray;
    input.atisComponentComplete = feed.atisComponentComplete;
    input.atisMechanicalIssueMask = feed.atisMechanicalIssueMask;
    input.atisRecords = &feed.atisRecords;
    input.monotonicMs = CurrentTickMilliseconds();
    const auto decision = xvatsim::brain::RunBrainOwnedAtisCycle(
        &gBrainOwnedRuntimeState, input);
    LogAtisCycleDiagnostic(decision);
    return decision;
}

long long CurrentTickMilliseconds() {
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

long long CurrentUnixSeconds() {
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

xvatsim::brain::BrainOwnedAsyncFactCycleOutput RunBoundAsyncFactCycle(
    bool xpilotConnected,
    xvatsim::brain::WorkflowStage workflowStage) {
    xvatsim::brain::BrainOwnedAsyncFactCycleInput input;
    input.pluginEnabled = gPluginRuntimeEnabled;
    input.xpilotConnected = xpilotConnected;
    input.workflowStage = workflowStage;
    input.operatingMode = gBrainOwnedRuntimeState.operatingMode.mode;
    input.flightContext = gBrainOwnedRuntimeState.flightContext;
    input.flightPlan = gBrainOwnedRuntimeState.flightPlanSnapshot;
    input.monotonicMs = CurrentTickMilliseconds();
    input.utcUnixSeconds = CurrentUnixSeconds();
    return xvatsim::brain::RunBrainOwnedAsyncFactCycle(
        &gBrainOwnedRuntimeState,
        input,
        gAsyncFactWorkerHost.Bindings());
}

long long ElapsedMicrosecondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - start)
        .count();
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

void HashCombine(std::size_t* seed, std::size_t value) {
    if (seed == nullptr) {
        return;
    }

    *seed ^= value + 0x9e3779b97f4a7c15ull + (*seed << 6) + (*seed >> 2);
}

void HashCombineBool(std::size_t* seed, bool value) {
    HashCombine(seed, static_cast<std::size_t>(value ? 1 : 0));
}

void HashCombineString(std::size_t* seed, const std::string& value) {
    HashCombine(seed, std::hash<std::string>{}(value));
}

void HashCombineDouble(std::size_t* seed, double value) {
    HashCombine(seed, std::hash<double>{}(value));
}

std::size_t HashControllerFeedIdentity(const xvatsim::brain::ControllerFeedSnapshot& snapshot) {
    std::size_t hash = 0;
    HashCombineBool(&hash, snapshot.available);
    HashCombineBool(&hash, snapshot.stale);
    HashCombine(&hash, snapshot.generation);
    HashCombine(&hash, static_cast<std::size_t>(snapshot.connectedControllers));
    for (const auto& controller : snapshot.Controllers()) {
        HashCombineString(&hash, controller.callsign);
        HashCombineString(&hash, NormalizeFrequency(controller.frequency));
        HashCombine(&hash, static_cast<std::size_t>(controller.facility));
        HashCombine(&hash, static_cast<std::size_t>(controller.visualRangeNm));
        HashCombineBool(&hash, controller.actionable);
        HashCombineBool(&hash, controller.atis);
        HashCombineString(&hash, controller.textAtis);
    }
    return hash;
}

std::size_t HashPilotSessionBoardInputs(const xvatsim::brain::XPilotSessionSnapshot& snapshot) {
    std::size_t hash = 0;
    HashCombineBool(&hash, snapshot.loaded);
    HashCombineBool(&hash, snapshot.connected);
    HashCombineString(&hash, NormalizeCallsign(snapshot.callsign));
    return hash;
}

std::uint64_t HashOperationalRadioIdentity(
    const xvatsim::brain::RadioStateSnapshot& snapshot) {
    std::size_t hash = 0;
    HashCombineBool(&hash, snapshot.valid);
    HashCombineString(&hash, snapshot.com1ActiveFrequency);
    HashCombineString(&hash, snapshot.com2ActiveFrequency);
    HashCombineString(&hash, snapshot.com1StandbyFrequency);
    HashCombineBool(&hash, snapshot.standbyAssistEnabled);
    HashCombineBool(&hash, snapshot.com1Powered);
    HashCombineBool(&hash, snapshot.com2Powered);
    HashCombineBool(&hash, snapshot.com1TxAvailable);
    HashCombineBool(&hash, snapshot.com1RxAvailable);
    HashCombineBool(&hash, snapshot.com2TxAvailable);
    HashCombineBool(&hash, snapshot.com2RxAvailable);
    HashCombineBool(&hash, snapshot.com1TxActive);
    HashCombineBool(&hash, snapshot.com1RxActive);
    HashCombineBool(&hash, snapshot.com2TxActive);
    HashCombineBool(&hash, snapshot.com2RxActive);
    HashCombineBool(&hash, snapshot.modeCActive);
    return static_cast<std::uint64_t>(hash);
}

std::uint64_t HashOperationalPresentationIdentity(
    const xvatsim::brain::OverlayUpdateSnapshot& updateSnapshot) {
    std::size_t hash = 0;
    HashCombine(
        &hash,
        static_cast<std::size_t>(gBrainOwnedRuntimeState.operatingMode.mode));
    HashCombine(
        &hash,
        static_cast<std::size_t>(gBrainOwnedRuntimeState.operatingMode.generation));
    HashCombine(
        &hash,
        static_cast<std::size_t>(gBrainOwnedRuntimeState.displayOverrideMode));
    HashCombine(
        &hash,
        static_cast<std::size_t>(gBrainOwnedRuntimeState.pendingTextEntryMode));
    HashCombineBool(
        &hash, gBrainOwnedRuntimeState.manualQuerySnapshot.visible);
    HashCombineString(&hash, gBrainOwnedRuntimeState.manualQuerySnapshot.line);
    HashCombine(
        &hash,
        static_cast<std::size_t>(
            gBrainOwnedRuntimeState.accessory.semanticPresentationGeneration));
    HashCombine(
        &hash,
        static_cast<std::size_t>(
            gBrainOwnedRuntimeState.atis.lookupPresentationSelectionGeneration));
    HashCombine(
        &hash,
        static_cast<std::size_t>(
            gBrainOwnedRuntimeState.metar.presentationGeneration));
    HashCombineBool(&hash, gBrainOwnedRuntimeState.pendingAutomaticFlightRecovery);
    HashCombineBool(&hash, gBrainOwnedRuntimeState.manualFlightRecoveryRequested);
    HashCombineBool(&hash, gBrainOwnedRuntimeState.hasActiveCruiseTarget);
    HashCombineBool(&hash, gBrainOwnedRuntimeState.cruiseTargetManualOverride);
    HashCombineBool(&hash, gBrainOwnedRuntimeState.cruiseAltitudeReachedThisFlight);
    HashCombineDouble(&hash, gBrainOwnedRuntimeState.activeCruiseTargetFt);
    HashCombineString(&hash, gBrainOwnedRuntimeState.diversionOverrideSourceKey);
    HashCombineBool(&hash, gPluginSettings.terminalRelevanceV2Enabled);
    HashCombineBool(&hash, gPluginSettings.vnasSectorPrecedenceEnabled);
    HashCombineBool(&hash, gPluginSettings.standbyAssistEnabled);
    HashCombineBool(&hash, gPluginSettings.directCtafStandbyAssistEnabled);
    HashCombineString(&hash, updateSnapshot.installedVersion);
    HashCombineString(&hash, updateSnapshot.latestVersion);
    HashCombineString(&hash, updateSnapshot.downloadPageUrl);
    HashCombineString(&hash, updateSnapshot.errorClass);
    HashCombine(&hash, static_cast<std::size_t>(updateSnapshot.status));
    HashCombineBool(&hash, updateSnapshot.critical);
    HashCombineBool(&hash, updateSnapshot.manualNoticeRequested);
    HashCombineBool(&hash, updateSnapshot.automaticNoticeRequested);
    return static_cast<std::uint64_t>(hash);
}

void ResetBrainDisplayPublisherCache() {
    xvatsim::brain::ResetBrainOwnedDisplayPublisherState(
        &gBrainOwnedRuntimeState);
}

void ResetBrainOwnedRuntimeCache(bool preserveAccessory = false) {
    if (preserveAccessory) {
        xvatsim::brain::ResetBrainOwnedRuntimeCachePreservingFlightContext(
            &gBrainOwnedRuntimeState);
        return;
    }
    xvatsim::brain::ResetBrainOwnedRuntimeState(&gBrainOwnedRuntimeState);
}

void DiscardPendingTextEntryState() {
    gOverlayWindow.CancelTextEntry();
    std::string discardedSubmission;
    (void)gOverlayWindow.ConsumeSubmittedText(&discardedSubmission);
    xvatsim::brain::ClearBrainOwnedPendingTextEntryMode(
        &gBrainOwnedRuntimeState);
}

void ResetSessionRuntimeCaches(
    bool resetVatsimFeed,
    bool preserveAccessory = false) {
    InvalidateRouteAsyncRuntime(
        resetVatsimFeed ? "session-runtime-reset" : "runtime-cache-reset");
    InvalidateAuthorityAsyncRuntime(
        resetVatsimFeed ? "session-runtime-reset" : "runtime-cache-reset");
    gAircraftStateSampler.Reset();
    gCtafLookupService.Reset();
    gXPilotBridge.Reset();
    gXPilotPrivateQualificationEventLatch.Reset();
    if (resetVatsimFeed) {
        gVatsimDataFeedClient.Reset();
        gVnasDataClient.Reset();
    }
    gNetworkPlanLink.Reset();
    gFlightPlanSampler.Reset();
    gRadioStateSampler.Reset();
    gRouteSectorResolver.ResetRuntimeState();
    gRouteSectorResolver.ClearPreflightRouteCache();
    gTerminalAuthorityResolver.Reset();
    ResetBrainOwnedRuntimeCache(preserveAccessory);
    ResetDiagnosticsTraceState();
    gTransceiverResolver.Reset();
    gTerminalEvidenceRadioCache = {};
    ResetBrainDisplayPublisherCache();
}

void ResetPluginRuntimeState(
    bool resetVatsimFeed,
    bool resetColdDarkLatch,
    bool preserveAccessory = false) {
    gXPilotPrivateObservationQueue.Clear();
    DiscardPendingTextEntryState();
    xvatsim::brain::ClearBrainOwnedManualQuery(&gBrainOwnedRuntimeState);
    ClearFlightRecoveryState();
    ResetSessionRuntimeCaches(resetVatsimFeed, preserveAccessory);
    ResetPresentationStateForColdDark();
    gLastAccessorySemanticPresentationGeneration = 0;
    if (resetColdDarkLatch) {
        xvatsim::brain::SetBrainOwnedColdDarkResetApplied(
            &gBrainOwnedRuntimeState,
            false);
    }
}

void ShowTransientStatusLine(const std::string& line) {
    xvatsim::brain::ShowBrainOwnedManualQueryLine(
        &gBrainOwnedRuntimeState,
        line,
        CurrentTickSeconds() + kManualQueryVisibleSeconds);
}

void ResetStandbyAssistLatch() {
    xvatsim::brain::ResetBrainOwnedStandbyAssistLatch(
        &gBrainOwnedRuntimeState);
}

std::string SanitizeLogText(std::string value, std::size_t maxChars);
void RecordDiagnosticJob(
    std::string name,
    std::string reason,
    long long durationMs,
    std::string cacheStatus,
    std::string result,
    std::string sourceGenerations,
    std::string routeKey);
bool DiagnosticJobsEnabled();

std::string FormatOverlayUpdateResult(bool visible) {
    return std::string("visible=") + (visible ? "1" : "0") + "," +
           xvatsim::modules::overlay::DescribeLastOverlayUpdateTiming();
}

std::string SummarizeBrainStandbyAssist(
    const xvatsim::brain::BrainOwnedStandbyAssistPlanOutput& plan,
    const xvatsim::brain::BrainOwnedStandbyAssistSideEffectDecision& decision) {
    const auto& summary = decision.standbySummary;
    std::ostringstream stream;
    stream << "evidence=" << summary.standbyEvidenceCount
           << ",candidates=" << summary.standbyCandidateCount
           << ",advisory=" << summary.advisoryCandidateCount
           << ",selected=" << summary.selectedTargetCount
           << ",writeDecisions=" << summary.writeDecisionCount
           << ",writeAttempts=" << summary.writeAttemptCount
           << ",writeSuccess=" << summary.writeSuccessCount
           << ",writeFailure=" << summary.writeFailureCount
           << ",writerResults=" << summary.writerResultCount
           << ",writerSuccess=" << summary.writerSuccessCount
           << ",writerFailure=" << summary.writerFailureCount
           << ",writerBlocked=" << summary.writerBlockedBeforeWriteCount
           << ",writerUnknown=" << summary.writerUnknownResultCount
           << ",writerNoTarget=" << summary.writerNoTargetCount
           << ",writerNoWrite=" << summary.writerNoWriteRequestedCount
           << ",writerControllerSource="
           << summary.writerControllerSourceCount
           << ",writerDirectCtafSource="
           << summary.writerDirectCtafSourceCount
           << ",empty=" << summary.skippedEmptyFrequencyCount
           << ",pending=" << summary.skippedPendingLookupCount
           << ",failed=" << summary.skippedLookupFailedCount
           << ",guard=" << summary.skippedGuardFrequencyCount
           << ",roleSkip=" << summary.skippedRoleNotEligibleCount
           << ",activeSkip=" << summary.skippedAlreadyActiveCount
           << ",brainOwned="
           << (summary.standbyRecommendationsBrainOwned ? 1 : 0)
           << ",standbyAssistEnabled="
           << (plan.settingsDiagnostics.standbyAssistEnabled ? 1 : 0)
           << ",directCtafStandbyAssistEnabled="
           << (plan.settingsDiagnostics.directCtafStandbyAssistEnabled ? 1 : 0)
           << ",directCtafGateSource="
           << SanitizeLogText(plan.settingsDiagnostics.directCtafGateSource, 32)
           << ",directCtafGateEffective="
           << (plan.settingsDiagnostics.directCtafGateEffective ? 1 : 0)
           << ",hasTarget=" << (plan.hasTarget ? 1 : 0)
           << ",standbyDecision="
           << SanitizeLogText(decision.standbyDecisionId, 96)
           << ",writeAllowed=" << (decision.writeAllowed ? 1 : 0)
           << ",writeAttempted=" << (decision.writeAttempted ? 1 : 0)
           << ",writeSucceededKnown="
           << (decision.writeSucceededKnown ? 1 : 0)
           << ",writeSucceeded=" << (decision.writeSucceeded ? 1 : 0)
           << ",writerResultKnown="
           << (decision.writerResult.writerResultKnown ? 1 : 0)
           << ",writerResultCode="
           << SanitizeLogText(
                  decision.writerResult.writerResultCode.empty()
                      ? std::string("none")
                      : decision.writerResult.writerResultCode,
                  64)
           << ",writerFailureReason="
           << SanitizeLogText(
                  decision.writerResult.writerFailureReason.empty()
                      ? std::string("none")
                      : decision.writerResult.writerFailureReason,
                  64)
           << ",writerFailureDomain="
           << SanitizeLogText(
                  decision.writerResult.writerFailureDomain.empty()
                      ? std::string("none")
                      : decision.writerResult.writerFailureDomain,
                  64)
           << ",writerSource="
           << SanitizeLogText(
                  decision.writerResult.writerResultSource.empty()
                      ? std::string("none")
                      : decision.writerResult.writerResultSource,
                  64)
           << ",failure="
           << SanitizeLogText(decision.failureReason.empty()
                                  ? std::string("none")
                                  : decision.failureReason,
                              64)
           << ",marker="
           << (decision.displayStandbyMarkerApplied ? 1 : 0);
    return stream.str();
}

void ApplyStandbyRecommendation(
    xvatsim::brain::WorkflowStage workflowStage,
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot,
    const xvatsim::brain::RadioStateSnapshot& radioStateSnapshot,
    const std::vector<xvatsim::brain::BrainOwnedStandbyAssistAdvisoryCandidate>&
        ctafUnicomAdvisoryCandidates,
    xvatsim::brain::FinalDisplaySnapshot* boardSnapshot) {
    if (boardSnapshot == nullptr) {
        return;
    }

    xvatsim::brain::BrainOwnedStandbyAssistPlanInput standbyInput;
    standbyInput.workflowStage = workflowStage;
    standbyInput.planKey = xvatsim::brain::BuildBrainOwnedNetworkPlanIdentityKey(networkPlanSnapshot);
    standbyInput.radios = radioStateSnapshot;
    standbyInput.standbyAssistEnabled = gPluginSettings.standbyAssistEnabled;
    standbyInput.directCtafStandbyAssistEnabled =
        gPluginSettings.directCtafStandbyAssistEnabled;
    standbyInput.directCtafGateSource =
        gPluginSettings.directCtafStandbyAssistGateSource;
    standbyInput.board = *boardSnapshot;
    standbyInput.ctafUnicomAdvisoryCandidates =
        ctafUnicomAdvisoryCandidates;
    const auto standbyPlan =
        xvatsim::brain::BuildBrainOwnedStandbyAssistPlan(standbyInput);
    *boardSnapshot = standbyPlan.board;

    auto sideEffectDecision =
        xvatsim::brain::DecideBrainOwnedStandbyAssistSideEffect(
            &gBrainOwnedRuntimeState,
            standbyPlan,
            gPluginSettings.standbyAssistEnabled);

    auto standbyLoaded = sideEffectDecision.standbyLoaded;
    auto writerResult = sideEffectDecision.writerResult;
    if (sideEffectDecision.shouldWriteCom1Standby) {
        standbyLoaded =
            gRadioStateSampler.SetCom1StandbyFrequency(
                sideEffectDecision.targetFrequency);
        writerResult =
            xvatsim::brain::BuildBrainOwnedStandbyAssistWriterResult(
                sideEffectDecision,
                standbyLoaded);
    }
    sideEffectDecision =
        xvatsim::brain::CompleteBrainOwnedStandbyAssistSideEffectDecision(
            standbyPlan,
            sideEffectDecision,
            writerResult);

    *boardSnapshot =
        xvatsim::brain::ApplyBrainOwnedStandbyAssistResult(
            standbyPlan,
            standbyLoaded);

    if (DiagnosticJobsEnabled()) {
        RecordDiagnosticJob(
            "BrainStandbyAssist",
            "decision-ledger",
            0,
            "brain-owned",
            SummarizeBrainStandbyAssist(standbyPlan, sideEffectDecision),
            {},
            standbyInput.planKey);
    }
}

void ResetCruiseTargetState() {
    xvatsim::brain::ResetBrainOwnedCruiseTarget(
        &gBrainOwnedRuntimeState);
}

void SyncCruiseTargetFromNetworkPlan(
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    xvatsim::brain::BrainOwnedCruiseTargetPlanInput input;
    input.flightContextActive = gBrainOwnedRuntimeState.flightContext.active;
    input.planKey = xvatsim::brain::BuildBrainOwnedNetworkPlanIdentityKey(networkPlanSnapshot);
    input.networkPlan = networkPlanSnapshot;

    const auto output =
        xvatsim::brain::SyncBrainOwnedCruiseTargetFromNetworkPlan(
            &gBrainOwnedRuntimeState,
            input);
    if (!output.logLine.empty()) {
        XPLMDebugString(output.logLine.c_str());
    }
}

void ClearDiversionOverrideState() {
    gDiversionContextModule.Reset();
    xvatsim::brain::ClearBrainOwnedDiversionOverrideSource(
        &gBrainOwnedRuntimeState);
}

void ResetFlightScopedManualPlanState() {
    ResetCruiseTargetState();
    ClearDiversionOverrideState();
}

void ResetEnrouteInitialDisplayHold() {
    xvatsim::brain::ResetBrainOwnedEnrouteInitialHold(
        &gBrainOwnedRuntimeState);
}

void ResetFlightProgressStateForNewContext() {
    xvatsim::brain::ResetBrainOwnedRuntimeCachePreservingFlightContext(
        &gBrainOwnedRuntimeState);
    xvatsim::brain::ResetBrainOwnedWorkflowProgress(
        &gBrainOwnedRuntimeState);
    ResetEnrouteInitialDisplayHold();
}

void ClearXPilotConnectionTracking() {
    xvatsim::brain::ClearBrainOwnedXPilotConnectionTracking(
        &gBrainOwnedRuntimeState);
}

void ClearFlightRecoveryState() {
    xvatsim::brain::ClearBrainOwnedFlightRecoveryRequests(
        &gBrainOwnedRuntimeState);
}

void InvalidateFlightContextPresentationCaches();

void RetargetFlightContextToPlan(
    const xvatsim::brain::PilotIdentitySnapshot& pilotIdentitySnapshot,
    const xvatsim::brain::FlightPlanSnapshot& flightPlanSnapshot,
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    xvatsim::brain::workflow::FlightContextRetargetInput input;
    input.currentContext = gBrainOwnedRuntimeState.flightContext;
    input.pilotIdentity = pilotIdentitySnapshot;
    input.flightPlan = flightPlanSnapshot;
    input.networkPlan = networkPlanSnapshot;

    const auto output =
        xvatsim::brain::workflow::RetargetFlightContextToNetworkPlan(input);
    if (!output.retargeted) {
        return;
    }

    xvatsim::brain::CommitBrainOwnedFlightContext(
        &gBrainOwnedRuntimeState,
        output.flightContext);
    if (output.shouldResetArrivalWake) {
        xvatsim::brain::ResetBrainOwnedWorkflowArrivalWake(
            &gBrainOwnedRuntimeState);
    }
    if (output.shouldResetEnrouteInitialDisplayHold) {
        ResetEnrouteInitialDisplayHold();
    }
    if (output.shouldInvalidatePresentation) {
        InvalidateFlightContextPresentationCaches();
    }
}

xvatsim::brain::NetworkPlanSnapshot BuildEffectiveNetworkPlanSnapshot(
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    xvatsim::brain::BrainOwnedDiversionOverrideInput input;
    input.hasOverride = gDiversionContextModule.HasOverride();
    if (!input.hasOverride) {
        return networkPlanSnapshot;
    }

    input.sourcePlanKey = xvatsim::brain::BuildBrainOwnedNetworkPlanIdentityKey(networkPlanSnapshot);
    const auto decision =
        xvatsim::brain::DecideBrainOwnedDiversionOverride(
            gBrainOwnedRuntimeState,
            input);

    if (decision.clearOverride) {
        ClearDiversionOverrideState();
        if (!decision.logLine.empty()) {
            XPLMDebugString(decision.logLine.c_str());
        }
        return networkPlanSnapshot;
    }

    if (!decision.useOverride) {
        return networkPlanSnapshot;
    }

    return gDiversionContextModule.BuildEffectivePlan(networkPlanSnapshot);
}

void InvalidateFlightContextPresentationCaches() {
    ResetBrainDisplayPublisherCache();
    ResetStandbyAssistLatch();
}

void LogDiversionAction(const std::string& line) {
    if (line.empty()) {
        return;
    }

    std::string message = "[XVatsim] " + line + "\n";
    XPLMDebugString(message.c_str());
}

void UpdateFlightContextIfNeeded(
    const xvatsim::brain::AircraftStateSnapshot& aircraftState,
    const xvatsim::brain::PilotIdentitySnapshot& pilotIdentitySnapshot,
    const xvatsim::brain::FlightPlanSnapshot& flightPlanSnapshot,
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    xvatsim::brain::workflow::FlightContextUpdateInput input;
    input.currentContext = gBrainOwnedRuntimeState.flightContext;
    input.aircraftState = aircraftState;
    input.pilotIdentity = pilotIdentitySnapshot;
    input.flightPlan = flightPlanSnapshot;
    input.networkPlan = networkPlanSnapshot;
    input.tuning.departureConfirmDistanceNm = 10.0;

    const auto output =
        xvatsim::brain::workflow::UpdateFlightContextFromNetworkPlan(input);
    if (!output.changed) {
        return;
    }

    xvatsim::brain::CommitBrainOwnedFlightContext(
        &gBrainOwnedRuntimeState,
        output.flightContext);
    if (output.shouldResetFlightScopedState) {
        (void)ApplyBoundAsyncWorkerLifecycleBoundary(true);
        DiscardPendingAccessoryClickFacts();
        xvatsim::brain::ResetBrainOwnedAccessoryForConfirmedNewFlight(
            &gBrainOwnedRuntimeState);
        ResetFlightScopedManualPlanState();
        ResetFlightProgressStateForNewContext();
        ResetBrainDisplayPublisherCache();
        ResetStandbyAssistLatch();
        return;
    }
    if (output.shouldInvalidatePresentation) {
        InvalidateFlightContextPresentationCaches();
    }
}

const char* RecoveryStageToken(xvatsim::brain::WorkflowStage stage) {
    using xvatsim::brain::WorkflowStage;
    switch (stage) {
        case WorkflowStage::Departure:
            return "DEPARTURE";
        case WorkflowStage::Enroute:
            return "ENROUTE";
        case WorkflowStage::Arrival:
            return "ARRIVAL";
        case WorkflowStage::None:
        default:
            return "NONE";
    }
}

void ClearCurrentFlightForRecoveryBoundary(const char* reason) {
    xvatsim::brain::ClearBrainOwnedFlightContext(&gBrainOwnedRuntimeState);
    ResetFlightScopedManualPlanState();
    ResetFlightProgressStateForNewContext();
    ResetBrainDisplayPublisherCache();
    ResetStandbyAssistLatch();

    std::string line = "[XVatsim] Current flight context cleared";
    if (reason != nullptr && std::strlen(reason) > 0) {
        line += ": ";
        line += reason;
    }
    line += ".\n";
    XPLMDebugString(line.c_str());
}

void ApplyCurrentFlightRecoveryDecision(
    const xvatsim::brain::workflow::RecoveryDecision& decision) {
    if (!decision.accepted) {
        return;
    }

    xvatsim::brain::CommitBrainOwnedFlightContext(
        &gBrainOwnedRuntimeState,
        decision.flightContext);
    xvatsim::brain::ApplyBrainOwnedWorkflowRecoveryStage(
        &gBrainOwnedRuntimeState,
        decision.stage,
        XPLMGetElapsedTime());
    if (decision.stage == xvatsim::brain::WorkflowStage::Enroute ||
        decision.stage == xvatsim::brain::WorkflowStage::Arrival) {
        ResetEnrouteInitialDisplayHold();
    }

    InvalidateFlightContextPresentationCaches();
}

bool AttemptCurrentFlightRecovery(
    bool manual,
    const xvatsim::brain::AircraftStateSnapshot& aircraftState,
    const xvatsim::brain::FlightPlanSnapshot& flightPlanSnapshot,
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    if (!manual && (!networkPlanSnapshot.matched || networkPlanSnapshot.stale)) {
        return false;
    }

    xvatsim::brain::workflow::WorkflowTuning tuning;
    tuning.arrivalWakeDistanceNm = kArrivalWakeDistanceNm;
    tuning.departureConfirmDistanceNm = 10.0;
    const auto decision = xvatsim::brain::workflow::ResolveCurrentFlightRecovery(
        aircraftState,
        flightPlanSnapshot,
        networkPlanSnapshot,
        gBrainOwnedRuntimeState.flightContext,
        manual
            ? xvatsim::brain::workflow::RecoveryRequestMode::Manual
            : xvatsim::brain::workflow::RecoveryRequestMode::AutomaticReconnect,
        tuning);

    std::ostringstream logLine;
    logLine << "[XVatsim] "
            << (manual ? "Manual" : "Automatic")
            << " current-flight recovery "
            << (decision.accepted ? "accepted" : "rejected")
            << " reason=" << decision.reason
            << " stage=" << RecoveryStageToken(decision.stage)
            << " preserved=" << (decision.usedPreservedContext ? 1 : 0)
            << " plan=" << (decision.usedFreshNetworkPlan ? 1 : 0);
    if (!networkPlanSnapshot.departureIcao.empty() ||
        !networkPlanSnapshot.destinationIcao.empty()) {
        logLine << " route="
                << (networkPlanSnapshot.departureIcao.empty()
                        ? "----"
                        : networkPlanSnapshot.departureIcao)
                << "->"
                << (networkPlanSnapshot.destinationIcao.empty()
                        ? "----"
                        : networkPlanSnapshot.destinationIcao);
    }
    logLine << "\n";
    XPLMDebugString(logLine.str().c_str());

    if (decision.accepted) {
        ApplyCurrentFlightRecoveryDecision(decision);
        xvatsim::brain::SetBrainOwnedAutomaticFlightRecoveryPending(
            &gBrainOwnedRuntimeState,
            false);
        xvatsim::brain::SetBrainOwnedManualFlightRecoveryRequested(
            &gBrainOwnedRuntimeState,
            false);
        if (manual) {
            std::string status = "RECOVER ";
            status += RecoveryStageToken(decision.stage);
            status += " ";
            status += decision.reason;
            ShowTransientStatusLine(status);
        }
        return true;
    }

    if (decision.reason == "route-changed") {
        ClearCurrentFlightForRecoveryBoundary("reconnect plan differs from preserved flight");
        xvatsim::brain::SetBrainOwnedAutomaticFlightRecoveryPending(
            &gBrainOwnedRuntimeState,
            false);
    } else if (!manual && decision.reason != "plan-unavailable") {
        xvatsim::brain::SetBrainOwnedAutomaticFlightRecoveryPending(
            &gBrainOwnedRuntimeState,
            false);
    }

    if (manual) {
        xvatsim::brain::SetBrainOwnedManualFlightRecoveryRequested(
            &gBrainOwnedRuntimeState,
            false);
        std::string status = "RECOVER rejected: ";
        status += decision.reason;
        ShowTransientStatusLine(status);
    }
    return false;
}

void AttemptPendingCurrentFlightRecovery(
    const xvatsim::brain::AircraftStateSnapshot& aircraftState,
    const xvatsim::brain::FlightPlanSnapshot& flightPlanSnapshot,
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    if (gBrainOwnedRuntimeState.pendingAutomaticFlightRecovery) {
        (void)AttemptCurrentFlightRecovery(
            false,
            aircraftState,
            flightPlanSnapshot,
            networkPlanSnapshot);
    }
    if (gBrainOwnedRuntimeState.manualFlightRecoveryRequested) {
        (void)AttemptCurrentFlightRecovery(
            true,
            aircraftState,
            flightPlanSnapshot,
            networkPlanSnapshot);
    }
}

bool UpdateEnrouteInitialDisplayHold(xvatsim::brain::WorkflowStage workflowStage) {
    xvatsim::brain::BrainOwnedEnrouteInitialHoldInput input;
    input.workflowStage = workflowStage;
    input.nowSeconds = XPLMGetElapsedTime();
    input.holdSeconds = kEnrouteInitialDisplaySeconds;

    const auto output =
        xvatsim::brain::UpdateBrainOwnedEnrouteInitialHold(
            &gBrainOwnedRuntimeState,
            input);
    return output.active;
}

const char* WorkflowStageToken(xvatsim::brain::WorkflowStage workflowStage) {
    using xvatsim::brain::WorkflowStage;
    switch (workflowStage) {
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

std::string SanitizeLogText(std::string value, std::size_t maxChars = kMaxLogFieldChars) {
    const auto nullPosition = value.find('\0');
    if (nullPosition != std::string::npos) {
        value.resize(nullPosition);
    }

    std::string sanitized;
    sanitized.reserve(std::min(value.size(), maxChars));
    bool pendingSpace = false;
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isspace(byte) != 0) {
            pendingSpace = !sanitized.empty();
            continue;
        }
        if (std::iscntrl(byte) != 0) {
            continue;
        }
        if (pendingSpace) {
            sanitized.push_back(' ');
            pendingSpace = false;
        }
        sanitized.push_back(character);
        if (sanitized.size() > maxChars) {
            sanitized.resize(maxChars > 3 ? maxChars - 3 : maxChars);
            if (maxChars > 3) {
                sanitized += "...";
            }
            break;
        }
    }
    return sanitized;
}

xvatsim::brain::BrainPdcEvaluationContext BuildPdcEvaluationContext() {
    xvatsim::brain::BrainPdcEvaluationContext context;
    const auto& flight = gBrainOwnedRuntimeState.flightContext;
    context.flightContextActive = flight.active;
    context.aircraftCallsign = flight.callsign;
    context.departureIcao = flight.departureIcao;
    context.destinationIcao = flight.destinationIcao;
    return context;
}

void ServicePdcPrivateSource() {
    if (!gBrainOwnedRuntimeState.pdc.initialized ||
        gBrainOwnedRuntimeState.pdc.pluginAdminSuspended) return;
    const auto context = BuildPdcEvaluationContext();
    const auto acquisition = xvatsim::brain::EvaluateBrainOwnedPdcAcquisition(
        &gBrainOwnedRuntimeState, context);
    if (acquisition.clearQueuedFacts || !acquisition.samplePrivatePayload) {
        gXPilotPrivateObservationQueue.Clear();
    }
    if (!acquisition.samplePrivatePayload) return;

    xvatsim::modules::xpilot_bridge::XPilotPrivateObservationRequest request;
    if (acquisition.useSequenceOnlyFastPath) {
        request.fastPathToken.applicable = true;
        request.fastPathToken.pluginInstanceIdentity =
            acquisition.pluginInstanceIdentity;
        request.fastPathToken.capabilityGeneration =
            acquisition.capabilityGeneration;
        request.fastPathToken.brainOwnedSequence = acquisition.brainOwnedSequence;
    }
    const auto sampled = gXPilotBridge.SamplePrivateMessage(request);
    const bool qualificationEvent =
        gXPilotPrivateQualificationEventLatch.Observe(sampled);
    const auto nowUs = static_cast<std::uint64_t>(
        std::max(0.0f, XPLMGetElapsedTime()) * 1'000'000.0f);
    const auto observation =
        xvatsim::modules::xpilot_bridge::ToBrainPdcMechanicalObservation(
            sampled, nowUs);
    if (!gXPilotPrivateObservationQueue.Produce(observation)) {
        xvatsim::brain::RecordBrainOwnedPdcTransportCapacityLoss(
            &gBrainOwnedRuntimeState, 1);
    }
    xvatsim::brain::BrainPdcMechanicalObservation fact;
    while (gXPilotPrivateObservationQueue.Consume(&fact)) {
        const auto decision = xvatsim::brain::EvaluateBrainOwnedPdcObservation(
            &gBrainOwnedRuntimeState, fact, context, nowUs);
        if (decision.productChanged || decision.gapDetected ||
            !decision.mechanicallyAccepted) {
            std::ostringstream line;
            line << "event=pdc-private-observation"
                 << " reason=" << SanitizeLogText(decision.reason, 64)
                 << " mechanical=" << (decision.mechanicallyAccepted ? 1 : 0)
                 << " captured=" << (decision.captureCompleted ? 1 : 0)
                 << " uncertainty=" << (decision.gapDetected ? 1 : 0)
                 << " samplerUs=" << fact.samplerElapsedMicroseconds
                 << " issueMask=0x" << std::hex << fact.mechanicalIssueMask
                 << std::dec
                 << " "
                 << xvatsim::brain::BrainOwnedPdcDiagnosticSummary(
                        gBrainOwnedRuntimeState.pdc);
            AppendDiagnosticsLogLine(line.str());
        }
        if (decision.captureCompleted) {
            gXPilotPrivateObservationQueue.Clear();
            break;
        }
    }
    if (qualificationEvent) {
        AppendDiagnosticsLogLine(
            xvatsim::modules::xpilot_bridge::
                FormatXPilotPrivateQualificationEvent(
                    sampled,
                    gBrainOwnedRuntimeState.pdc.lastObservedSequence,
                    gBrainOwnedRuntimeState.pdc.hasConnectedDisposition
                        ? gBrainOwnedRuntimeState.pdc.connectedDispositionedSequence
                        : 0));
    }
    if (!gBrainOwnedRuntimeState.pdc.captureComplete) {
        (void)gXPilotPrivateObservationQueue.ServiceRetained();
    }
}

const char* MetarRequestPurposeToken(
    xvatsim::brain::BrainMetarRequestPurpose purpose) {
    using Purpose = xvatsim::brain::BrainMetarRequestPurpose;
    switch (purpose) {
        case Purpose::PrimaryTarget: return "primary-target";
        case Purpose::PilotLookup: return "pilot-lookup";
        case Purpose::PrimaryRefresh: return "primary-refresh";
        default: return "unknown";
    }
}

const char* MetarWorkerStatusToken(
    xvatsim::brain::BrainMetarWorkerStatus status) {
    using Status = xvatsim::brain::BrainMetarWorkerStatus;
    switch (status) {
        case Status::None: return "none";
        case Status::Pending: return "pending";
        case Status::Success: return "success";
        case Status::Cancelled: return "cancelled";
        case Status::InvalidRequest: return "invalid-request";
        case Status::TransportFailure: return "transport-failure";
        case Status::HttpFailure: return "http-failure";
        case Status::PayloadRejected: return "payload-rejected";
        case Status::JsonRejected: return "json-rejected";
        case Status::WrongStation: return "wrong-station";
        default: return "unknown";
    }
}

const char* MetarTransportStageToken(
    xvatsim::brain::BrainMetarTransportStage stage) {
    using Stage = xvatsim::brain::BrainMetarTransportStage;
    switch (stage) {
        case Stage::None: return "none";
        case Stage::Startup: return "startup";
        case Stage::SendStart: return "send-start";
        case Stage::SendCompletion: return "send-completion";
        case Stage::ReceiveStart: return "receive-start";
        case Stage::ResponseHeaders: return "response-headers";
        case Stage::HttpStatus: return "http-status";
        case Stage::DataAvailability: return "data-availability";
        case Stage::Read: return "read";
        case Stage::PayloadValidation: return "payload-validation";
        case Stage::JsonValidation: return "json-validation";
        case Stage::StationValidation: return "station-validation";
        case Stage::Completed: return "completed";
        default: return "unknown";
    }
}

const char* MetarWinHttpOperationToken(
    xvatsim::brain::BrainMetarWinHttpOperation operation) {
    using Operation = xvatsim::brain::BrainMetarWinHttpOperation;
    switch (operation) {
        case Operation::None: return "none";
        case Operation::CreateEventHandle: return "create-event";
        case Operation::OpenSession: return "open-session";
        case Operation::ConfigureTimeouts: return "configure-timeouts";
        case Operation::Connect: return "connect";
        case Operation::OpenRequest: return "open-request";
        case Operation::ConfigureRedirects: return "configure-redirects";
        case Operation::RegisterCallback: return "register-callback";
        case Operation::SendRequest: return "send-request";
        case Operation::ReceiveResponse: return "receive-response";
        case Operation::QueryHeaders: return "query-headers";
        case Operation::QueryDataAvailable: return "query-data-available";
        case Operation::ReadData: return "read-data";
        case Operation::CloseRequest: return "close-request";
        default: return "unknown";
    }
}

const char* MetarCategoryDiagnosticToken(
    xvatsim::brain::BrainMetarFlightCategory category) {
    using Category = xvatsim::brain::BrainMetarFlightCategory;
    switch (category) {
        case Category::Vfr: return "VFR";
        case Category::Mvfr: return "MVFR";
        case Category::Ifr: return "IFR";
        case Category::Lifr: return "LIFR";
        case Category::Unknown:
        default: return "UNKNOWN";
    }
}

void LogMetarTerminalDiagnostic(
    const xvatsim::brain::BrainMetarTerminalDiagnostic& diagnostic) {
    if (!diagnostic.available) return;
    std::ostringstream stream;
    stream << "event=metar-terminal"
           << " requestId=" << diagnostic.request.requestId
           << " purpose=" << MetarRequestPurposeToken(
                  diagnostic.request.purpose)
           << " icao=" << SanitizeLogText(
                  diagnostic.request.airportIcao, 4)
           << " returnedIcao=" << SanitizeLogText(
                  diagnostic.stationIcao, 4)
           << " primaryGeneration="
           << diagnostic.request.primaryGeneration
           << " lookupGeneration="
           << diagnostic.request.lookupGeneration
           << " stage=" << MetarTransportStageToken(
                  diagnostic.terminalStage)
           << " status=" << MetarWorkerStatusToken(diagnostic.status)
           << " reason=" << SanitizeLogText(diagnostic.diagnostic, 80)
           << " operation=" << MetarWinHttpOperationToken(
                  diagnostic.winHttpOperation)
           << " result=" << diagnostic.winHttpResult
           << " winhttpError=" << diagnostic.winHttpError
           << " httpStatus=" << diagnostic.httpStatus
           << " payloadBytes=" << diagnostic.payloadBytes
           << " networkUs=" << diagnostic.networkElapsedUs
           << " completedMonotonicMs="
           << diagnostic.completedMonotonicMs
           << " progress=" << diagnostic.transportProgress
           << " source=" << SanitizeLogText(diagnostic.source, 24);
    AppendDiagnosticsLogLine(stream.str());
}

void LogMetarDispositionDiagnostic(
    const xvatsim::brain::BrainMetarDispositionDiagnostic& diagnostic) {
    if (!diagnostic.available) return;
    std::ostringstream stream;
    stream << "event=metar-brain-disposition"
           << " requestId=" << diagnostic.requestId
           << " icao=" << SanitizeLogText(diagnostic.airportIcao, 4)
           << " disposition=" << (diagnostic.accepted ? "accepted" : "rejected")
           << " parseAttempted=" << diagnostic.parsingAttempted
           << " parserReason=" << SanitizeLogText(
                  diagnostic.parserReason, 80)
           << " parserElapsedUs="
           << diagnostic.parserElapsedMicroseconds
           << " parserPath="
           << (diagnostic.parserRanOnSimulatorFlightLoopHarvestPath
                   ? "simulator-flight-loop-harvest" : "not-run")
           << " category=" << MetarCategoryDiagnosticToken(
                  diagnostic.acceptedCategory)
           << " historyMutated=" << diagnostic.historyMutated
           << " presentationChanged=" << diagnostic.presentationChanged
           << " reason=" << SanitizeLogText(diagnostic.reason, 80);
    AppendDiagnosticsLogLine(stream.str());
}

void LogMetarAsyncDiagnostics(
    const xvatsim::brain::BrainOwnedAsyncFactCycleOutput& output) {
    if (output.dispatchDiagnostic.available) {
        const auto& request = output.dispatchDiagnostic.request;
        std::ostringstream stream;
        stream << "event=metar-dispatch"
               << " requestId=" << request.requestId
               << " purpose=" << MetarRequestPurposeToken(request.purpose)
               << " icao=" << SanitizeLogText(request.airportIcao, 4)
               << " primaryGeneration=" << request.primaryGeneration
               << " lookupGeneration=" << request.lookupGeneration
               << " dispatchedMonotonicMs="
               << request.dispatchedMonotonicMs;
        AppendDiagnosticsLogLine(stream.str());
    }
    LogMetarTerminalDiagnostic(output.terminalDiagnostic);
    LogMetarDispositionDiagnostic(output.dispositionDiagnostic);
}

void LogMetarLifecycleDiagnostics(
    const xvatsim::brain::BrainMetarWorkerShutdownSnapshot& snapshot) {
    LogMetarTerminalDiagnostic(snapshot.terminalDiagnostic);
    LogMetarDispositionDiagnostic(snapshot.dispositionDiagnostic);
}

xvatsim::brain::BrainMetarWorkerShutdownSnapshot
ApplyBoundAsyncWorkerLifecycleBoundary(bool clearAcceptedState) {
    const auto snapshot =
        xvatsim::brain::ApplyBrainOwnedAsyncWorkerLifecycleBoundary(
            &gBrainOwnedRuntimeState,
            gAsyncFactWorkerHost.Bindings(),
            clearAcceptedState);
    LogMetarLifecycleDiagnostics(snapshot);
    return snapshot;
}

std::string SummarizeAuthorityProofs(
    const xvatsim::brain::AuthorityRelevanceSnapshot& snapshot) {
    if (!snapshot.available || snapshot.stale) {
        return "unavailable";
    }
    if (snapshot.relevantAuthorities.empty()) {
        return "none";
    }

    std::ostringstream stream;
    const auto countToLog =
        std::min<std::size_t>(snapshot.relevantAuthorities.size(), 10);
    for (std::size_t index = 0; index < countToLog; ++index) {
        const auto& authority = snapshot.relevantAuthorities[index];
        if (index > 0) {
            stream << ";";
        }
        stream << SanitizeLogText(authority.callsign, 32)
               << "@"
               << SanitizeLogText(authority.frequency, 16)
               << ":polygon="
               << SanitizeLogText(authority.polygonKey, 32)
               << ":proof="
               << SanitizeLogText(authority.proofSource, 40)
               << ":detail="
               << SanitizeLogText(authority.proofDetail, 96);
    }
    if (snapshot.relevantAuthorities.size() > countToLog) {
        stream << ";+" << (snapshot.relevantAuthorities.size() - countToLog);
    }
    return stream.str();
}

std::string FormatSourceGenerations(
    std::uint64_t controllerFeedGeneration,
    std::uint64_t centerBoundaryGeneration,
    std::uint64_t authorityCatalogGeneration,
    std::uint64_t terminalCoverageGeneration = 0) {
    std::ostringstream stream;
    stream << "ctrl=" << controllerFeedGeneration
           << ",center=" << centerBoundaryGeneration
           << ",catalog=" << authorityCatalogGeneration;
    if (terminalCoverageGeneration > 0) {
        stream << ",terminal=" << terminalCoverageGeneration;
    }
    return stream.str();
}

std::string FormatSourceGenerations(
    const xvatsim::brain::RouteSectorSnapshot& snapshot,
    const xvatsim::brain::ControllerFeedSnapshot& controllerFeedSnapshot) {
    return FormatSourceGenerations(
        controllerFeedSnapshot.generation,
        snapshot.centerBoundaryGeneration,
        snapshot.authorityCatalogGeneration);
}

std::string FormatSourceGenerations(
    const xvatsim::brain::RouteAuthorityPlan& plan,
    const xvatsim::brain::ControllerFeedSnapshot& controllerFeedSnapshot) {
    return FormatSourceGenerations(
        controllerFeedSnapshot.generation,
        plan.centerBoundaryGeneration,
        plan.authorityCatalogGeneration);
}

std::string FormatSourceGenerations(
    const xvatsim::brain::AuthorityRelevanceSnapshot& snapshot) {
    return FormatSourceGenerations(
        snapshot.controllerFeedGeneration,
        snapshot.centerBoundaryGeneration,
        snapshot.authorityCatalogGeneration,
        snapshot.terminalCoverageGeneration);
}

std::string FormatSourceGenerations(
    const xvatsim::brain::AirportSectorSnapshot& snapshot,
    const xvatsim::brain::ControllerFeedSnapshot& controllerFeedSnapshot) {
    return FormatSourceGenerations(
        controllerFeedSnapshot.generation,
        snapshot.centerBoundaryGeneration,
        snapshot.authorityCatalogGeneration,
        snapshot.terminalCoverageGeneration);
}

std::string FormatSourceGenerations(
    const xvatsim::brain::DepartureAuthoritySnapshot& snapshot,
    const xvatsim::brain::ControllerFeedSnapshot& controllerFeedSnapshot) {
    return FormatSourceGenerations(
        controllerFeedSnapshot.generation,
        snapshot.sourceGenerations.centerBoundary,
        snapshot.sourceGenerations.authorityCatalog,
        snapshot.sourceGenerations.terminalCoverage);
}

void RecordDiagnosticJob(
    std::string name,
    std::string reason,
    long long durationMs,
    std::string cacheStatus,
    std::string result,
    std::string sourceGenerations = {},
    std::string routeKey = {}) {
    if (!DiagnosticJobsEnabled()) {
        return;
    }

    DiagnosticJobRecord job;
    job.name = std::move(name);
    job.reason = std::move(reason);
    job.stage = gDiagnosticsState.frame.stage;
    job.cacheStatus = std::move(cacheStatus);
    job.result = std::move(result);
    job.sourceGenerations = std::move(sourceGenerations);
    job.routeKey = std::move(routeKey);
    job.durationMs = durationMs;
    gDiagnosticsState.frame.jobs.push_back(std::move(job));
}

bool DiagnosticJobsEnabled() {
    return gDiagnosticsState.frame.valid &&
           gDiagnosticsState.frame.collectJobs;
}

xvatsim::brain::BrainOwnedCtafLookupFact BuildBrainOwnedCtafLookupFact(
    const std::string& airportIcao,
    const xvatsim::modules::ctaf_lookup::CtafLookupEntry& ctafLookup) {
    xvatsim::brain::BrainOwnedCtafLookupFact fact;
    fact.airportIcao = airportIcao;
    fact.resolved = ctafLookup.resolved;
    fact.available = ctafLookup.available;
    fact.frequency = ctafLookup.frequency;
    fact.lookupAttempted = !airportIcao.empty();
    fact.cacheHit = ctafLookup.resolved;
    if (ctafLookup.lastAttemptTickSeconds > 0) {
        fact.lastAttemptAgeSeconds =
            std::max<long long>(
                0,
                CurrentTickSeconds() - ctafLookup.lastAttemptTickSeconds);
    }
    fact.fetchInProgress =
        !ctafLookup.resolved && ctafLookup.failureCount == 0 &&
        ctafLookup.lastAttemptTickSeconds > 0;
    fact.requestSucceeded = ctafLookup.resolved;
    fact.statusCodeClass =
        ctafLookup.available
            ? "2xx"
            : (ctafLookup.resolved
                   ? "resolved-no-ctaf"
                   : (ctafLookup.failureCount > 0 ? "failure" : ""));
    fact.failureCount = ctafLookup.failureCount;
    if (!ctafLookup.resolved) {
        fact.pendingReason =
            fact.fetchInProgress
                ? "fetch-in-progress"
                : (ctafLookup.failureCount > 0 ? "lookup-failed"
                                               : "unresolved-pending");
    }
    return fact;
}

std::string SummarizeBrainControllerRelevance(
    const xvatsim::brain::BrainControllerRelevanceWorkerOutput& output) {
    int accepted = 0;
    int rejected = 0;
    int displayed = 0;
    for (const auto& completion : output.completions) {
        if (completion.decision ==
            xvatsim::brain::BrainOwnedCandidateDecision::Accepted) {
            ++accepted;
        } else if (completion.decision ==
                   xvatsim::brain::BrainOwnedCandidateDecision::Rejected) {
            ++rejected;
        }
        if (completion.displayed) {
            ++displayed;
        }
    }

    std::ostringstream stream;
    stream << "candidates=" << output.completions.size()
           << ",accepted=" << accepted
           << ",rejected=" << rejected
           << ",displayed=" << displayed
           << ",depStations=" << output.departureBoard.stations.size()
           << ",enrStations=" << output.enrouteBoard.stations.size()
           << ",arrStations=" << output.arrivalBoard.stations.size();
    return stream.str();
}

xvatsim::brain::BrainControllerRelevanceWorkerOutput
RefreshBrainControllerRelevance(
    const xvatsim::brain::BrainControllerRelevanceWorkerInput& input,
    const std::string& planKey,
    bool recordDiagnostics) {
    const auto started = std::chrono::steady_clock::now();
    const auto runtimeOutput =
        xvatsim::brain::RunBrainOwnedControllerRelevance(
            &gBrainOwnedRuntimeState,
            input);
    const auto elapsedMs =
        runtimeOutput.cacheHit ? 0 : ElapsedMicrosecondsSince(started) / 1000;

    if (recordDiagnostics && DiagnosticJobsEnabled()) {
        RecordDiagnosticJob(
            "BrainControllerRelevanceWorker",
            runtimeOutput.relevance.reason,
            elapsedMs,
            runtimeOutput.cacheStatus,
            SummarizeBrainControllerRelevance(runtimeOutput.relevance),
            {},
            planKey);
    }
    return runtimeOutput.relevance;
}

std::string SummarizeBrainPublisherOutput(
    const xvatsim::brain::BrainOwnedPublisherOutput& output,
    const xvatsim::brain::BrainControllerRelevanceWorkerOutput& relevanceOutput) {
    std::ostringstream stream;
    stream << "finalStations=" << output.finalDisplay.stations.size()
           << ",depStations=" << output.departureBoard.stations.size()
           << ",enrStations=" << output.enrouteBoard.stations.size()
           << ",arrStations=" << output.arrivalBoard.stations.size()
           << ",intentDisplayed=" << output.displayIntent.displayed
           << ",intentHidden=" << output.displayIntent.hidden
           << ",completions=" << relevanceOutput.completions.size()
           << ",rejectedUnapproved=" << output.rejectedUnapprovedStations;
    return stream.str();
}

std::string SummarizeBrainDisplayIntent(
    const xvatsim::brain::BrainDisplayIntentOutput& intentOutput) {
    std::ostringstream stream;
    stream << "displayed=" << intentOutput.displayed
           << ",hidden=" << intentOutput.hidden
           << ",filtered=" << intentOutput.filtered
           << ",finalStations=" << intentOutput.finalDisplay.stations.size()
           << ",hash=" << intentOutput.stableHash;
    const auto countToLog =
        std::min<std::size_t>(intentOutput.diagnostics.size(), 4);
    for (std::size_t index = 0; index < countToLog; ++index) {
        stream << ",d" << index << "="
               << SanitizeLogText(intentOutput.diagnostics[index], 96);
    }
    if (intentOutput.diagnostics.size() > countToLog) {
        stream << ",+" << (intentOutput.diagnostics.size() - countToLog);
    }
    return stream.str();
}

xvatsim::brain::BrainOwnedPublisherOutput RunBrainPublisher(
    xvatsim::brain::WorkflowStage workflowStage,
    const xvatsim::brain::BrainControllerRelevanceWorkerOutput& relevanceOutput,
    const xvatsim::modules::ctaf_lookup::CtafLookupEntry& departureCtafLookup,
    const xvatsim::modules::ctaf_lookup::CtafLookupEntry& arrivalCtafLookup,
    const xvatsim::brain::RadioStateSnapshot& radioStateSnapshot,
    const std::string& planKey) {
    xvatsim::brain::BrainOwnedPublisherFactInput publisherFacts;
    publisherFacts.workflowStage = workflowStage;
    publisherFacts.radios = radioStateSnapshot;
    publisherFacts.departureBoard = relevanceOutput.departureBoard;
    publisherFacts.arrivalBoard = relevanceOutput.arrivalBoard;
    publisherFacts.enrouteBoard = relevanceOutput.enrouteBoard;
    publisherFacts.completions = relevanceOutput.completions;
    publisherFacts.publishReason = "brain-owned-ui-publish";
    publisherFacts.productPlanKey = planKey;
    publisherFacts.productPlanKeySource =
        planKey.empty() ? "unavailable" : "live-product";
    publisherFacts.productPlanKeyMissingReason =
        planKey.empty() ? "plugin-plan-key-empty" : "";
    publisherFacts.sourceOwnedFallbackStableKeyLiveConsumptionEnabled =
        gPluginSettings.sourceOwnedFallbackStableKeyLiveConsumptionEnabled;
    publisherFacts.sourceOwnedFallbackStableKeyLiveConsumptionGateSource =
        gPluginSettings.sourceOwnedFallbackStableKeyLiveConsumptionGateSource;
    const auto& flightContext = gBrainOwnedRuntimeState.flightContext;
    publisherFacts.departureCtaf =
        BuildBrainOwnedCtafLookupFact(
            flightContext.departureIcao,
            departureCtafLookup);
    publisherFacts.arrivalCtaf =
        BuildBrainOwnedCtafLookupFact(
            flightContext.destinationIcao,
            arrivalCtafLookup);
    const auto publisherInput =
        xvatsim::brain::BuildBrainOwnedPublisherInputFromFacts(
            gBrainOwnedRuntimeState,
            publisherFacts);

    auto output =
        xvatsim::brain::RunBrainOwnedPublisher(
            &gBrainOwnedRuntimeState,
            publisherInput);

    LogCandidateCompletionTrace(workflowStage, planKey);

    if (DiagnosticJobsEnabled()) {
        RecordDiagnosticJob(
            "BrainDisplayIntentWorker",
            output.displayIntent.reason,
            0,
            "brain-display-intent",
            SummarizeBrainDisplayIntent(output.displayIntent),
            {},
            planKey);

        std::ostringstream phasePublishResult;
        phasePublishResult << output.phasePublish.statusLine
                           << ",state="
                           << output.phasePublisherStateSummary;
        RecordDiagnosticJob(
            "PhaseSnapshotPublisher",
            publisherFacts.publishReason,
            0,
            output.phasePublish.usedLastProven ? "ui-last-proven-reused"
                                               : "ui-candidate-published",
            phasePublishResult.str(),
            {},
            planKey);

        RecordDiagnosticJob(
            "BrainPublisher",
            "brain-approved-display-only",
            0,
            "ui-from-accepted-completions",
            SummarizeBrainPublisherOutput(output, relevanceOutput),
            {},
            planKey);
    }
    return output;
}

xvatsim::brain::BrainRadioRangeWorkerOutput RunBrainRadioRangeWorker(
    const xvatsim::brain::BrainRadioRangeWorkerInput& input,
    RefreshDiagnosticsFrame* diagnostics) {
    const auto started = std::chrono::steady_clock::now();
    const auto transceivers =
        gTransceiverResolver.Resolve(input.aircraft, input.controllerFeed);
    const auto resolveUs = ElapsedMicrosecondsSince(started);
    if (diagnostics != nullptr) {
        diagnostics->radioRangeResolveUs = resolveUs;
        diagnostics->radioRangeResolveMs = resolveUs / 1000;
    }

    return xvatsim::brain::BuildBrainRadioRangeWorkerOutput(
        input,
        transceivers,
        static_cast<double>(CurrentTickSeconds()));
}

xvatsim::brain::RadioReachableControllerSnapshot BuildEngineer3RadioSnapshot(
    const xvatsim::brain::AircraftStateSnapshot& aircraftState,
    const xvatsim::brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const std::string& planKey,
    RefreshDiagnosticsFrame* diagnostics) {
    const auto nowSeconds = CurrentTickSeconds();
    xvatsim::brain::BrainOwnedRadioBoardReuseInput reuseInput;
    reuseInput.nowSeconds = nowSeconds;
    reuseInput.refreshIntervalSeconds = kEngineer3RadioBoardRefreshSeconds;
    reuseInput.controllerGeneration = controllerFeedSnapshot.generation;
    const auto reuseOutput =
        xvatsim::brain::TryReuseBrainOwnedRadioBoard(
            gBrainOwnedRuntimeState,
            reuseInput);
    if (reuseOutput.canReuse) {
        if (diagnostics != nullptr) {
            diagnostics->radioRangeResolveUs = 0;
            diagnostics->radioRangeResolveMs = 0;
        }
        if (DiagnosticJobsEnabled()) {
            RecordDiagnosticJob(
                "Engineer3RadioBoard",
                reuseOutput.reason,
                0,
                reuseOutput.cacheStatus,
                reuseOutput.radioSnapshot.statusLine,
                {},
                planKey);
        }
        return reuseOutput.radioSnapshot;
    }

    xvatsim::brain::BrainRadioRangeWorkerInput workerInput;
    workerInput.aircraft = aircraftState;
    workerInput.controllerFeed = controllerFeedSnapshot;
    workerInput.planKey = planKey;
    auto workerOutput =
        RunBrainRadioRangeWorker(workerInput, diagnostics);
    const auto radioSnapshot = workerOutput.radioBoard;

    xvatsim::brain::BrainOwnedRadioBoardCommitInput commitInput;
    commitInput.nowSeconds = nowSeconds;
    commitInput.controllerGeneration = controllerFeedSnapshot.generation;
    commitInput.transceiverSnapshot = std::move(workerOutput.transceivers);
    commitInput.radioSnapshot = radioSnapshot;
    const auto commitOutput =
        xvatsim::brain::CommitBrainOwnedRadioBoardRefresh(
            &gBrainOwnedRuntimeState,
            std::move(commitInput));

    if (DiagnosticJobsEnabled()) {
        std::ostringstream result;
        result << commitOutput.radioSnapshot.statusLine << ","
               << commitOutput.diff.statusLine;
        RecordDiagnosticJob(
            "Engineer3RadioBoard",
            commitOutput.reason,
            diagnostics != nullptr ? diagnostics->radioRangeResolveMs : 0,
            commitOutput.cacheStatus,
            result.str(),
            {},
            planKey);
    }
    return commitOutput.radioSnapshot;
}


std::shared_ptr<const xvatsim::brain::RouteSectorSnapshot>
UnavailableRouteSnapshot(const char* reason) {
    const auto build = [](const char* cacheStatus, const char* detail) {
        auto route = std::make_shared<xvatsim::brain::RouteSectorSnapshot>();
        route->available = false;
        route->stale = true;
        route->diagnosticCacheStatus = cacheStatus;
        route->diagnosticReason = detail;
        route->statusLine = "ROUTE preparation pending";
        return std::shared_ptr<const xvatsim::brain::RouteSectorSnapshot>(
            std::move(route));
    };
    static const auto sourcePending = build(
        "route-source-unavailable", "immutable-route-source-unavailable");
    static const auto workerPending = build(
        "route-async-pending", "route-worker-pending-fail-closed");
    static const auto planUnavailable = build(
        "route-input-unavailable", "network-plan-unavailable");
    const std::string_view token = reason == nullptr ? "" : reason;
    if (token == "immutable-route-source-unavailable") {
        return sourcePending;
    }
    if (token == "network-plan-unavailable") {
        return planUnavailable;
    }
    return workerPending;
}

void ServiceDeferredRouteRetirements() {
    if (gRouteAsyncRuntime.invariantFailureRetirement != nullptr) {
        (void)gRoutePreparationWorker.RetireSharedRoute(
            &gRouteAsyncRuntime.invariantFailureRetirement);
    }
    for (auto& route : gRouteAsyncRuntime.deferredRouteRetirements) {
        if (route != nullptr) {
            (void)gRoutePreparationWorker.RetireSharedRoute(&route);
        }
    }
}

void QueueRouteRetirement(
    std::shared_ptr<const xvatsim::brain::RouteSectorSnapshot> route) {
    if (route == nullptr) {
        return;
    }
    ServiceDeferredRouteRetirements();
    if (gRoutePreparationWorker.RetireSharedRoute(&route)) {
        return;
    }
    for (auto& slot : gRouteAsyncRuntime.deferredRouteRetirements) {
        if (slot == nullptr) {
            slot = std::move(route);
            const auto backlog = static_cast<std::uint64_t>(std::count_if(
                gRouteAsyncRuntime.deferredRouteRetirements.begin(),
                gRouteAsyncRuntime.deferredRouteRetirements.end(),
                [](const auto& item) { return item != nullptr; }));
            gRouteAsyncRuntime.retirementBacklogPeak = std::max(
                gRouteAsyncRuntime.retirementBacklogPeak, backlog);
            return;
        }
    }
    // Sixty-four backlog slots plus sixty-four worker mailbox slots exceed
    // the exact 30-sector route sanity bound. Reaching this branch indicates
    // an internal ownership invariant failure. Preserve a final bounded
    // reference so even that failure cannot destroy the route on the callback.
    ++gRouteAsyncRuntime.failureCount;
    if (gRouteAsyncRuntime.invariantFailureRetirement == nullptr) {
        gRouteAsyncRuntime.invariantFailureRetirement = std::move(route);
    }
}

bool HasDeferredRouteRetirements() {
    return gRouteAsyncRuntime.invariantFailureRetirement != nullptr ||
        std::any_of(
            gRouteAsyncRuntime.deferredRouteRetirements.begin(),
            gRouteAsyncRuntime.deferredRouteRetirements.end(),
            [](const auto& route) { return route != nullptr; });
}

void DrainDeferredRouteRetirementsForStop() {
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(5);
    while (HasDeferredRouteRetirements() &&
           std::chrono::steady_clock::now() < deadline) {
        ServiceDeferredRouteRetirements();
        gRoutePreparationWorker.NotifyRetirement();
        std::this_thread::yield();
    }
}

void ResetBrainRoutePublication() {
    auto retiredRoute = gBrainOwnedRuntimeState.routePolygonSnapshot;
    xvatsim::brain::BrainOwnedRoutePolygonRefreshInput reset;
    (void)xvatsim::brain::BeginBrainOwnedRoutePolygonRefresh(
        &gBrainOwnedRuntimeState, reset);
    QueueRouteRetirement(std::move(retiredRoute));
}

void RetireAcceptedRoute(bool resetBrain) {
    if (gRouteAsyncRuntime.acceptedLease != nullptr) {
        gRouteAsyncRuntime.acceptedLease->retired.store(
            true, std::memory_order_release);
    }
    gRouteAsyncRuntime.acceptedRoute.reset();
    gRouteAsyncRuntime.acceptedLease.reset();
    gRouteAsyncRuntime.acceptedDigest = 0;
    gRouteAsyncRuntime.acceptedIdentity = {};
    gRoutePreparationWorker.NotifyRetirement();
    if (resetBrain) {
        ResetBrainRoutePublication();
    }
}

template <std::size_t Size>
void CopyRouteEvidenceToken(
    std::array<char, Size>* destination,
    std::string_view value) noexcept {
    if (destination == nullptr || Size == 0) {
        return;
    }
    destination->fill('\0');
    const auto count = std::min(value.size(), Size - 1);
    for (std::size_t index = 0; index < count; ++index) {
        const auto byte = static_cast<unsigned char>(value[index]);
        const auto character = value[index];
        (*destination)[index] =
            std::isspace(byte) != 0 || character == '"' || character == '\''
            ? '_'
            : character;
    }
}

xvatsim::modules::runtime_workers::RouteEvidenceWorkKind
ToRouteEvidenceWorkKind(
    xvatsim::modules::route_sector::RoutePreparationRequest::Kind kind) {
    return kind == xvatsim::modules::route_sector::
                       RoutePreparationRequest::Kind::ObserveExpandedFms
        ? xvatsim::modules::runtime_workers::
              RouteEvidenceWorkKind::ExpandedFmsObservation
        : xvatsim::modules::runtime_workers::
              RouteEvidenceWorkKind::RoutePreparation;
}

xvatsim::modules::runtime_workers::RouteEvidenceRecord
BuildRouteEvidenceRecord(
    xvatsim::modules::runtime_workers::RouteEvidenceRecordType recordType,
    xvatsim::modules::route_sector::RoutePreparationRequest::Kind kind,
    const xvatsim::modules::route_sector::RouteWorkerIdentity& identity,
    std::string_view reason) {
    xvatsim::modules::runtime_workers::RouteEvidenceRecord evidence;
    evidence.recordType = recordType;
    evidence.workKind = ToRouteEvidenceWorkKind(kind);
    evidence.requestId = identity.requestId;
    evidence.lifecycleEpoch = identity.lifecycleEpoch;
    evidence.networkDigest = identity.networkPlanDigest;
    evidence.sourceIdentity = identity.routeSourceDatasetIdentity;
    evidence.preflightIdentity = identity.preflightCandidateIdentity;
    evidence.policyIdentity = identity.routePolicyIdentity;
    evidence.anchorDigest = identity.routeAnchorDigest;
    evidence.fmsObservationIdentity =
        identity.expandedFmsObservationIdentity;
    CopyRouteEvidenceToken(&evidence.planKey, identity.planKey);
    CopyRouteEvidenceToken(&evidence.reason, reason);
    return evidence;
}

void SubmitRouteEvidence(
    xvatsim::modules::runtime_workers::RouteEvidenceRecord evidence) noexcept {
    (void)gDiagnosticsWriter.TryEnqueueRouteEvidence(std::move(evidence));
}

void InvalidateRouteAsyncRuntime(const char* reason) {
    gRoutePreparationWorker.CancelPending();
    RetireAcceptedRoute(true);
    gRouteAsyncRuntime.hasDesiredIdentity = false;
    gRouteAsyncRuntime.desiredIdentity = {};
    gRouteAsyncRuntime.hasEligibleRequest = false;
    gRouteAsyncRuntime.eligibleRequest = {};
    gRouteAsyncRuntime.eligibleKind =
        xvatsim::modules::route_sector::RoutePreparationRequest::Kind::PrepareRoute;
    gRouteAsyncRuntime.anchorSelection = {};
    gRouteAsyncRuntime.fmsObservationNetworkPlanDigest = 0;
    gRouteAsyncRuntime.fmsObservationIdentity = 0;
    gRouteAsyncRuntime.fmsObservationReady = false;
    gRouteAsyncRuntime.nextFmsObservationMonotonicMs = 0;
    gRouteAsyncRuntime.nextRouteRetryMonotonicMs = 0;
    ++gRouteAsyncRuntime.lifecycleEpoch;
    if (gRouteAsyncRuntime.lifecycleEpoch == 0) {
        gRouteAsyncRuntime.lifecycleEpoch = 1;
    }
    ++gRouteAsyncRuntime.invalidationCount;
    {
        xvatsim::modules::runtime_workers::RouteEvidenceRecord evidence;
        evidence.recordType = xvatsim::modules::runtime_workers::
            RouteEvidenceRecordType::Lifecycle;
        evidence.disposition = xvatsim::modules::runtime_workers::
            RouteEvidenceDisposition::Cancelled;
        evidence.lifecycleEpoch = gRouteAsyncRuntime.lifecycleEpoch;
        CopyRouteEvidenceToken(
            &evidence.reason,
            reason == nullptr ? "unspecified" : reason);
        SubmitRouteEvidence(std::move(evidence));
    }
    AppendDeferredDiagnosticsLogLine(
        [lifecycleEpoch = gRouteAsyncRuntime.lifecycleEpoch,
         reasonText = std::string{reason == nullptr ? "unspecified" : reason}]() {
            std::ostringstream line;
            line << "event=route-worker-cancelled"
                 << " lifecycleEpoch=" << lifecycleEpoch
                 << " reason=" << reasonText;
            return line.str();
        });
}

std::string BuildRouteRuntimeKey(
    const xvatsim::modules::route_sector::RouteWorkerIdentity& identity) {
    std::ostringstream key;
    key << identity.planKey
        << "|network=" << identity.networkPlanDigest
        << "|source=" << identity.routeSourceDatasetIdentity
        << "|preflight=" << identity.preflightCandidateIdentity
        << "|policy=" << identity.routePolicyIdentity
        << "|anchor=" << identity.routeAnchorDigest
        << "|fmsObservation="
        << identity.expandedFmsObservationIdentity
        << "|lifecycle=" << identity.lifecycleEpoch;
    return key.str();
}

xvatsim::brain::BrainRouteCompletionIdentity ToBrainRouteIdentity(
    const xvatsim::modules::route_sector::RouteWorkerIdentity& identity) {
    xvatsim::brain::BrainRouteCompletionIdentity output;
    output.requestId = identity.requestId;
    output.lifecycleEpoch = identity.lifecycleEpoch;
    output.planKey = identity.planKey;
    output.networkPlanDigest = identity.networkPlanDigest;
    output.routeSourceDatasetIdentity =
        identity.routeSourceDatasetIdentity;
    output.preflightCandidateIdentity =
        identity.preflightCandidateIdentity;
    output.routePolicyIdentity = identity.routePolicyIdentity;
    output.routeAnchorDigest = identity.routeAnchorDigest;
    output.expandedFmsObservationIdentity =
        identity.expandedFmsObservationIdentity;
    return output;
}

xvatsim::brain::BrainRoutePolygonWorkerOutput RefreshBrainRoutePolygonSnapshot(
    const xvatsim::brain::AircraftStateSnapshot& aircraftState,
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot,
    RefreshDiagnosticsFrame* diagnostics) {
    const auto completeStageStarted = std::chrono::steady_clock::now();
    ServiceDeferredRouteRetirements();
    const auto planKey =
        xvatsim::brain::BuildBrainOwnedNetworkPlanIdentityKey(
            networkPlanSnapshot);
    if (planKey.empty() || !networkPlanSnapshot.feedAvailable ||
        networkPlanSnapshot.stale || !networkPlanSnapshot.matched) {
        if (gRouteAsyncRuntime.hasDesiredIdentity ||
            gRouteAsyncRuntime.acceptedRoute != nullptr ||
            gRoutePreparationWorker.IsRunning()) {
            InvalidateRouteAsyncRuntime("network-plan-unavailable");
        }
        auto output = xvatsim::brain::BuildBrainRoutePolygonWorkerOutput(
            UnavailableRouteSnapshot("network-plan-unavailable"));
        if (diagnostics != nullptr) {
            diagnostics->routeResolved = false;
            diagnostics->routeStatus = output.route->statusLine;
            diagnostics->routeResolveUs =
                ElapsedMicrosecondsSince(completeStageStarted);
            diagnostics->routeResolveMs = diagnostics->routeResolveUs / 1000;
        }
        return output;
    }

    const auto desiredPathStarted = std::chrono::steady_clock::now();
    const auto nowMonotonicMs = CurrentTickMilliseconds();
    ApplyPreflightRouteCacheForPlanIfNeeded(networkPlanSnapshot);
    (void)gRouteSectorResolver.TryHarvestSourcePublications();
    const auto datasetPublication =
        gRouteSectorResolver.GetRouteSourceDatasetPublication();
    if (datasetPublication.dataset != nullptr &&
        datasetPublication.contentIdentity !=
            gRouteAsyncRuntime.observedSourceIdentity &&
        gRoutePreparationWorker.ObserveSourceDataset(
            datasetPublication.dataset)) {
        gRouteAsyncRuntime.observedSourceIdentity =
            datasetPublication.contentIdentity;
    }

    xvatsim::modules::route_sector::RouteWorkerIdentity desired;
    desired.lifecycleEpoch = gRouteAsyncRuntime.lifecycleEpoch;
    desired.planKey = planKey;
    desired.networkPlanDigest =
        xvatsim::modules::route_sector::HashRouteNetworkPlan(
            networkPlanSnapshot);
    desired.routeSourceDatasetIdentity = datasetPublication.identity;
    desired.preflightCandidateIdentity =
        xvatsim::modules::route_sector::HashRoutePreflightCandidate(
            gSelectedPreflightPlanKey == planKey
                ? gSelectedPreflightRouteCache.get()
                : nullptr,
            gSelectedPreflightPlanKey == planKey
                ? gSelectedPreflightValidationReason
                : "selection-pending");
    desired.routePolicyIdentity =
        xvatsim::modules::route_sector::RoutePreparationPolicyIdentity();
    if (gRouteAsyncRuntime.fmsObservationNetworkPlanDigest !=
        desired.networkPlanDigest) {
        gRouteAsyncRuntime.fmsObservationNetworkPlanDigest =
            desired.networkPlanDigest;
        gRouteAsyncRuntime.fmsObservationIdentity = 0;
        gRouteAsyncRuntime.fmsObservationReady = false;
        gRouteAsyncRuntime.nextFmsObservationMonotonicMs = 0;
        gRouteAsyncRuntime.anchorSelection = {};
    }
    const auto routeAnchor =
        xvatsim::modules::route_sector::SelectStickyRouteAnchor(
            &gRouteAsyncRuntime.anchorSelection,
            aircraftState,
            networkPlanSnapshot,
            desired.networkPlanDigest);
    desired.routeAnchorDigest =
        xvatsim::modules::route_sector::HashRouteAnchor(
            routeAnchor, networkPlanSnapshot);
    desired.expandedFmsObservationIdentity =
        gRouteAsyncRuntime.fmsObservationReady
            ? gRouteAsyncRuntime.fmsObservationIdentity
            : 0;

    xvatsim::modules::route_sector::RouteWorkerFact harvested;
    const auto harvestStarted = std::chrono::steady_clock::now();
    const auto harvestedFact = gRoutePreparationWorker.TryHarvest(&harvested);
    bool publishedThisCycle = false;
    xvatsim::brain::BrainOwnedRoutePolygonRuntimeOutput runtimeOutput;
    const auto harvestedObservation = harvestedFact &&
        harvested.kind == xvatsim::modules::route_sector::
            RoutePreparationRequest::Kind::ObserveExpandedFms;
    bool observationAccepted = false;
    bool observationRebuildDecision = false;
    std::string observationDispositionReason;
    if (harvestedObservation) {
        const auto eligible = gRouteAsyncRuntime.hasEligibleRequest &&
            gRouteAsyncRuntime.eligibleKind ==
                xvatsim::modules::route_sector::
                    RoutePreparationRequest::Kind::ObserveExpandedFms &&
            harvested.identity.requestId ==
                gRouteAsyncRuntime.eligibleRequest.requestId &&
            xvatsim::modules::route_sector::SameRouteWorkerSemanticIdentity(
                harvested.identity, gRouteAsyncRuntime.eligibleRequest) &&
            xvatsim::modules::route_sector::SameRouteWorkerSemanticIdentity(
                harvested.identity, desired);
        observationAccepted = eligible && harvested.completed &&
            !harvested.cancelled && !harvested.failed &&
            harvested.expandedFmsObservationIdentity != 0;
        if (observationAccepted) {
            const auto changed = !gRouteAsyncRuntime.fmsObservationReady ||
                gRouteAsyncRuntime.fmsObservationIdentity !=
                    harvested.expandedFmsObservationIdentity;
            gRouteAsyncRuntime.fmsObservationIdentity =
                harvested.expandedFmsObservationIdentity;
            gRouteAsyncRuntime.fmsObservationReady = true;
            gRouteAsyncRuntime.nextFmsObservationMonotonicMs =
                nowMonotonicMs + kExpandedFmsObservationIntervalMs;
            ++gRouteAsyncRuntime.fmsObservationAcceptedCount;
            if (changed) {
                ++gRouteAsyncRuntime.fmsObservationChangedCount;
            }
            observationRebuildDecision = changed;
            desired.expandedFmsObservationIdentity =
                harvested.expandedFmsObservationIdentity;
            observationDispositionReason = changed
                ? "expanded-fms-observation-changed"
                : "expanded-fms-observation-unchanged";
        } else {
            ++gRouteAsyncRuntime.staleRejectedCount;
            observationDispositionReason = harvested.failed
                ? harvested.reason
                : "stale-expanded-fms-observation";
        }
        if (gRouteAsyncRuntime.hasEligibleRequest &&
            harvested.identity.requestId ==
                gRouteAsyncRuntime.eligibleRequest.requestId) {
            gRouteAsyncRuntime.hasEligibleRequest = false;
            gRouteAsyncRuntime.eligibleRequest = {};
        }
    }

    const auto desiredChanged =
        !gRouteAsyncRuntime.hasDesiredIdentity ||
        !xvatsim::modules::route_sector::SameRouteWorkerSemanticIdentity(
            gRouteAsyncRuntime.desiredIdentity, desired);
    if (desiredChanged) {
        if (gRoutePreparationWorker.IsRunning()) {
            ++gRouteAsyncRuntime.pendingReplacementCount;
        }
        gRoutePreparationWorker.CancelPending();
        RetireAcceptedRoute(true);
        gRouteAsyncRuntime.desiredIdentity = desired;
        gRouteAsyncRuntime.hasDesiredIdentity = true;
        gRouteAsyncRuntime.hasEligibleRequest = false;
        gRouteAsyncRuntime.eligibleRequest = {};
        gRouteAsyncRuntime.nextRouteRetryMonotonicMs = nowMonotonicMs;
    }
    const auto routeRuntimeKey = BuildRouteRuntimeKey(desired);

    xvatsim::brain::BrainOwnedRoutePolygonRefreshInput refreshInput;
    refreshInput.aircraft = aircraftState;
    refreshInput.routeRuntimeKey = routeRuntimeKey;
    refreshInput.nowSeconds = CurrentTickSeconds();
    refreshInput.pendingRetrySeconds = kRadioBoardPendingRouteRetrySeconds;

    const auto recordTransitionDiagnostic =
        [&](const xvatsim::brain::BrainOwnedRoutePolygonRuntimeOutput& record) {
            if (!record.transitionEvaluated || !DiagnosticJobsEnabled()) {
                return;
            }
            RecordDiagnosticJob(
                "BrainRoutePolygonTransitionWorker",
                record.transitionReason,
                0,
                record.transitionCacheStatus,
                record.transitionDiagnosticResult,
                {},
                routeRuntimeKey);
        };

    if (harvestedFact && !harvestedObservation) {
        xvatsim::brain::BrainRouteCompletionValidationInput validation;
        if (gRouteAsyncRuntime.hasEligibleRequest) {
            validation.eligible = ToBrainRouteIdentity(
                gRouteAsyncRuntime.eligibleRequest);
        }
        if (gRouteAsyncRuntime.hasDesiredIdentity) {
            validation.desired = ToBrainRouteIdentity(
                gRouteAsyncRuntime.desiredIdentity);
        }
        validation.completed = ToBrainRouteIdentity(harvested.identity);
        const auto completionDecision =
            xvatsim::brain::DecideBrainRouteCompletion(validation);
        const auto publishable =
            harvested.kind == xvatsim::modules::route_sector::
                RoutePreparationRequest::Kind::PrepareRoute &&
            harvested.completed && !harvested.cancelled &&
            !harvested.failed && harvested.routeLease != nullptr &&
            harvested.routeLease->route != nullptr &&
            completionDecision.accepted;
        const auto terminalIdentity = harvested.identity;
        const auto terminalDigest = harvested.routeDigest;
        const auto terminalTimings = harvested.timings;
        const auto terminalReason = harvested.reason;
        const auto dispositionReason = publishable
            ? completionDecision.reason
            : !completionDecision.accepted
                ? completionDecision.reason
                : harvested.failed
                    ? harvested.reason
                    : harvested.cancelled
                        ? std::string{"route-worker-cancelled"}
                        : std::string{"route-worker-incomplete"};
        {
            auto evidence = BuildRouteEvidenceRecord(
                xvatsim::modules::runtime_workers::
                    RouteEvidenceRecordType::Terminal,
                harvested.kind,
                terminalIdentity,
                terminalReason);
            evidence.resultDigest = terminalDigest;
            evidence.completed = harvested.completed;
            evidence.cancelled = harvested.cancelled;
            evidence.failed = harvested.failed;
            evidence.workerWallUs = static_cast<std::uint64_t>(
                std::max<long long>(0, terminalTimings.totalUs));
            evidence.sourceUs = terminalTimings.sourceAcquisitionUs;
            evidence.navigationUs =
                terminalTimings.navigationInitializationUs;
            evidence.preflightUs = terminalTimings.preflightValidationUs;
            evidence.fmsUs = terminalTimings.fmsDiscoveryUs;
            evidence.procedureUs = terminalTimings.procedureMetadataUs;
            evidence.grammarUs = terminalTimings.grammarParsingUs;
            evidence.waypointUs = terminalTimings.waypointResolutionUs;
            evidence.traversalPreparationUs =
                terminalTimings.traversalPreparationUs;
            evidence.polygonUs = terminalTimings.polygonTraversalUs;
            evidence.prefixUs = terminalTimings.controllerPrefixUs;
            evidence.finalizeUs = terminalTimings.outputFinalizationUs;
            SubmitRouteEvidence(std::move(evidence));
        }
        AppendDeferredDiagnosticsLogLine(
            [terminalIdentity,
             terminalDigest,
             terminalTimings,
             completed = harvested.completed,
             cancelled = harvested.cancelled,
             failed = harvested.failed,
             terminalReason]() {
                std::ostringstream line;
                line << "event=route-worker-terminal"
                     << " requestId=" << terminalIdentity.requestId
                     << " lifecycleEpoch=" << terminalIdentity.lifecycleEpoch
                     << " networkDigest="
                     << terminalIdentity.networkPlanDigest
                     << " sourceIdentity="
                     << terminalIdentity.routeSourceDatasetIdentity
                     << " preflightIdentity="
                     << terminalIdentity.preflightCandidateIdentity
                     << " policyIdentity="
                     << terminalIdentity.routePolicyIdentity
                     << " anchorDigest="
                     << terminalIdentity.routeAnchorDigest
                     << " fmsObservationIdentity="
                     << terminalIdentity.expandedFmsObservationIdentity
                     << " resultDigest=" << terminalDigest
                     << " completed=" << (completed ? 1 : 0)
                     << " cancelled=" << (cancelled ? 1 : 0)
                     << " failed=" << (failed ? 1 : 0)
                     << " totalUs=" << terminalTimings.totalUs
                     << " sourceUs=" << terminalTimings.sourceAcquisitionUs
                     << " navUs=" << terminalTimings.navigationInitializationUs
                     << " preflightUs=" << terminalTimings.preflightValidationUs
                     << " fmsUs=" << terminalTimings.fmsDiscoveryUs
                     << " procedureUs=" << terminalTimings.procedureMetadataUs
                     << " grammarUs=" << terminalTimings.grammarParsingUs
                     << " waypointUs=" << terminalTimings.waypointResolutionUs
                     << " traversalPrepUs="
                     << terminalTimings.traversalPreparationUs
                     << " polygonUs=" << terminalTimings.polygonTraversalUs
                     << " prefixUs=" << terminalTimings.controllerPrefixUs
                     << " finalizeUs=" << terminalTimings.outputFinalizationUs
                     << " reason=" << terminalReason;
                return line.str();
            });
        if (!publishable && !completionDecision.accepted) {
            AppendDeferredDiagnosticsLogLine(
                [terminalIdentity,
                 reasonText = completionDecision.reason]() {
                    std::ostringstream line;
                    line << "event=route-worker-stale-rejected"
                         << " requestId=" << terminalIdentity.requestId
                         << " lifecycleEpoch="
                         << terminalIdentity.lifecycleEpoch
                         << " networkDigest="
                         << terminalIdentity.networkPlanDigest
                         << " sourceIdentity="
                         << terminalIdentity.routeSourceDatasetIdentity
                         << " preflightIdentity="
                         << terminalIdentity.preflightCandidateIdentity
                         << " policyIdentity="
                         << terminalIdentity.routePolicyIdentity
                         << " anchorDigest="
                         << terminalIdentity.routeAnchorDigest
                         << " fmsObservationIdentity="
                         << terminalIdentity.expandedFmsObservationIdentity
                         << " reason=" << reasonText;
                    return line.str();
                });
        }
        if (publishable) {
            RetireAcceptedRoute(false);
            gRouteAsyncRuntime.acceptedLease = harvested.routeLease;
            gRouteAsyncRuntime.acceptedRoute = harvested.routeLease->route;
            gRouteAsyncRuntime.acceptedDigest = harvested.routeDigest;
            gRouteAsyncRuntime.acceptedIdentity = harvested.identity;
            ++gRouteAsyncRuntime.acceptedCount;
            const auto workerOutput =
                xvatsim::brain::BuildBrainRoutePolygonWorkerOutput(
                    gRouteAsyncRuntime.acceptedRoute);
            runtimeOutput =
                xvatsim::brain::CommitBrainOwnedRoutePolygonRefresh(
                    &gBrainOwnedRuntimeState, refreshInput, workerOutput);
            publishedThisCycle = true;
            gRouteAsyncRuntime.nextRouteRetryMonotonicMs =
                gRouteAsyncRuntime.acceptedRoute->routeResolved &&
                        !gRouteAsyncRuntime.acceptedRoute->stale
                    ? 0
                    : nowMonotonicMs +
                        kRadioBoardPendingRouteRetrySeconds * 1000;
        } else {
            if (harvested.routeLease != nullptr) {
                harvested.routeLease->retired.store(
                    true, std::memory_order_release);
                gRoutePreparationWorker.NotifyRetirement();
            }
            ++gRouteAsyncRuntime.staleRejectedCount;
            if (harvested.failed) {
                ++gRouteAsyncRuntime.failureCount;
            }
            gRouteAsyncRuntime.nextRouteRetryMonotonicMs =
                nowMonotonicMs +
                kRadioBoardPendingRouteRetrySeconds * 1000;
        }
        if (gRouteAsyncRuntime.hasEligibleRequest &&
            harvested.identity.requestId ==
                gRouteAsyncRuntime.eligibleRequest.requestId) {
            gRouteAsyncRuntime.hasEligibleRequest = false;
            gRouteAsyncRuntime.eligibleRequest = {};
        }
        AppendDeferredDiagnosticsLogLine(
            [requestId = terminalIdentity.requestId,
             publishable,
             reasonText = dispositionReason]() {
                std::ostringstream line;
                line << "event=route-worker-disposition"
                     << " requestId=" << requestId
                     << " disposition="
                     << (publishable ? "published" : "rejected")
                     << " reason=" << reasonText;
                return line.str();
            });
        {
            auto evidence = BuildRouteEvidenceRecord(
                xvatsim::modules::runtime_workers::
                    RouteEvidenceRecordType::Disposition,
                harvested.kind,
                terminalIdentity,
                dispositionReason);
            evidence.resultDigest = terminalDigest;
            evidence.disposition = publishable
                ? xvatsim::modules::runtime_workers::
                      RouteEvidenceDisposition::Published
                : xvatsim::modules::runtime_workers::
                      RouteEvidenceDisposition::Rejected;
            SubmitRouteEvidence(std::move(evidence));
        }
    }
    if (harvestedObservation) {
        const auto terminalIdentity = harvested.identity;
        const auto terminalObservationIdentity =
            harvested.expandedFmsObservationIdentity;
        const auto terminalReason = harvested.reason;
        {
            auto evidence = BuildRouteEvidenceRecord(
                xvatsim::modules::runtime_workers::
                    RouteEvidenceRecordType::Terminal,
                harvested.kind,
                terminalIdentity,
                terminalReason);
            evidence.fmsObservationIdentity =
                terminalObservationIdentity;
            evidence.completed = harvested.completed;
            evidence.cancelled = harvested.cancelled;
            evidence.failed = harvested.failed;
            evidence.workerWallUs =
                harvested.expandedFmsObservationWorkerWallUs;
            evidence.workerCpuUs =
                harvested.expandedFmsObservationWorkerCpuUs;
            evidence.workerCpuAvailable =
                harvested.expandedFmsObservationWorkerCpuAvailable;
            SubmitRouteEvidence(std::move(evidence));
        }
        AppendDeferredDiagnosticsLogLine(
            [terminalIdentity,
             terminalObservationIdentity,
             workerWallUs =
                 harvested.expandedFmsObservationWorkerWallUs,
             workerCpuUs =
                 harvested.expandedFmsObservationWorkerCpuUs,
             workerCpuAvailable =
                 harvested.expandedFmsObservationWorkerCpuAvailable,
             completed = harvested.completed,
             cancelled = harvested.cancelled,
             failed = harvested.failed,
             terminalReason]() {
                std::ostringstream line;
                line << "event=route-worker-terminal"
                     << " kind=expanded-fms-observation"
                     << " requestId=" << terminalIdentity.requestId
                     << " lifecycleEpoch=" << terminalIdentity.lifecycleEpoch
                     << " networkDigest="
                     << terminalIdentity.networkPlanDigest
                     << " sourceIdentity="
                     << terminalIdentity.routeSourceDatasetIdentity
                     << " preflightIdentity="
                     << terminalIdentity.preflightCandidateIdentity
                     << " policyIdentity="
                     << terminalIdentity.routePolicyIdentity
                     << " anchorDigest="
                     << terminalIdentity.routeAnchorDigest
                     << " fmsObservationIdentity="
                     << terminalObservationIdentity
                     << " workerWallUs=" << workerWallUs
                     << " workerCpuStatus="
                     << (workerCpuAvailable ? "available" : "unavailable")
                     << " workerCpuUs=" << workerCpuUs
                     << " completed=" << (completed ? 1 : 0)
                     << " cancelled=" << (cancelled ? 1 : 0)
                     << " failed=" << (failed ? 1 : 0)
                     << " reason=" << terminalReason;
                return line.str();
            });
        if (!observationAccepted && !harvested.failed &&
            !harvested.cancelled) {
            AppendDeferredDiagnosticsLogLine(
                [terminalIdentity,
                 reasonText = observationDispositionReason]() {
                    std::ostringstream line;
                    line << "event=route-worker-stale-rejected"
                         << " kind=expanded-fms-observation"
                         << " requestId=" << terminalIdentity.requestId
                         << " lifecycleEpoch="
                         << terminalIdentity.lifecycleEpoch
                         << " networkDigest="
                         << terminalIdentity.networkPlanDigest
                         << " sourceIdentity="
                         << terminalIdentity.routeSourceDatasetIdentity
                         << " reason=" << reasonText;
                    return line.str();
                });
        }
        AppendDeferredDiagnosticsLogLine(
            [requestId = terminalIdentity.requestId,
             observationAccepted,
             reasonText = observationDispositionReason]() {
                std::ostringstream line;
                line << "event=route-worker-disposition"
                     << " kind=expanded-fms-observation"
                     << " requestId=" << requestId
                     << " disposition="
                     << (observationAccepted ? "observed" : "rejected")
                     << " reason=" << reasonText;
                return line.str();
            });
        {
            auto evidence = BuildRouteEvidenceRecord(
                xvatsim::modules::runtime_workers::
                    RouteEvidenceRecordType::Disposition,
                harvested.kind,
                terminalIdentity,
                observationDispositionReason);
            evidence.fmsObservationIdentity =
                terminalObservationIdentity;
            evidence.disposition = observationAccepted
                ? xvatsim::modules::runtime_workers::
                      RouteEvidenceDisposition::Observed
                : xvatsim::modules::runtime_workers::
                      RouteEvidenceDisposition::Rejected;
            evidence.rebuildDecisionApplicable = true;
            evidence.rebuildDecision =
                observationAccepted && observationRebuildDecision;
            SubmitRouteEvidence(std::move(evidence));
        }
    }
    const auto harvestUs = ElapsedMicrosecondsSince(harvestStarted);
    gRouteAsyncRuntime.maximumHarvestUs = std::max<std::uint64_t>(
        gRouteAsyncRuntime.maximumHarvestUs,
        static_cast<std::uint64_t>(std::max<long long>(0, harvestUs)));

    auto acceptedCurrent =
        gRouteAsyncRuntime.acceptedRoute != nullptr &&
        xvatsim::modules::route_sector::SameRouteWorkerSemanticIdentity(
            gRouteAsyncRuntime.acceptedIdentity, desired);

    if (!publishedThisCycle && acceptedCurrent) {
        const auto transitionStarted = std::chrono::steady_clock::now();
        runtimeOutput =
            xvatsim::brain::BeginBrainOwnedRoutePolygonRefresh(
                &gBrainOwnedRuntimeState, refreshInput);
        const auto transitionUs =
            ElapsedMicrosecondsSince(transitionStarted);
        gRouteAsyncRuntime.maximumTransitionUs = std::max<std::uint64_t>(
            gRouteAsyncRuntime.maximumTransitionUs,
            static_cast<std::uint64_t>(
                std::max<long long>(0, transitionUs)));
    }

    xvatsim::modules::route_sector::RouteCoordinatorDecisionInput
        coordinatorInput;
    coordinatorInput.sourceUsable = datasetPublication.dataset != nullptr &&
        datasetPublication.available && !datasetPublication.stale;
    coordinatorInput.fmsObservationReady =
        gRouteAsyncRuntime.fmsObservationReady;
    coordinatorInput.acceptedCurrent = acceptedCurrent;
    coordinatorInput.brainNeedsWorker = runtimeOutput.needsWorker;
    coordinatorInput.hasEligibleRequest =
        gRouteAsyncRuntime.hasEligibleRequest;
    coordinatorInput.workerBusy = gRoutePreparationWorker.IsRunning();
    coordinatorInput.retryDue =
        gRouteAsyncRuntime.nextRouteRetryMonotonicMs == 0 ||
        nowMonotonicMs >= gRouteAsyncRuntime.nextRouteRetryMonotonicMs;
    coordinatorInput.fmsObservationDue =
        gRouteAsyncRuntime.nextFmsObservationMonotonicMs == 0 ||
        nowMonotonicMs >=
            gRouteAsyncRuntime.nextFmsObservationMonotonicMs;
    const auto coordinatorDecision =
        xvatsim::modules::route_sector::DecideRouteCoordinatorAction(
            coordinatorInput);

    if (coordinatorDecision.action !=
        xvatsim::modules::route_sector::RouteCoordinatorAction::None) {
        auto requestIdentity = desired;
        requestIdentity.requestId = gRouteAsyncRuntime.nextRequestId++;
        if (gRouteAsyncRuntime.nextRequestId == 0) {
            gRouteAsyncRuntime.nextRequestId = 1;
        }
        xvatsim::modules::route_sector::RoutePreparationRequest request;
        request.kind = coordinatorDecision.action ==
                xvatsim::modules::route_sector::
                    RouteCoordinatorAction::ObserveExpandedFms
            ? xvatsim::modules::route_sector::
                RoutePreparationRequest::Kind::ObserveExpandedFms
            : xvatsim::modules::route_sector::
                RoutePreparationRequest::Kind::PrepareRoute;
        request.identity = requestIdentity;
        request.routeAnchor = routeAnchor;
        request.networkPlan =
            std::make_shared<const xvatsim::brain::NetworkPlanSnapshot>(
                networkPlanSnapshot);
        request.routeSourceDataset = datasetPublication.dataset;
        if (request.kind == xvatsim::modules::route_sector::
                RoutePreparationRequest::Kind::PrepareRoute &&
            gSelectedPreflightPlanKey == planKey) {
            request.preflightCandidate = gSelectedPreflightRouteCache;
            request.preflightValidationReason =
                gSelectedPreflightValidationReason;
        }
        const auto before = gRoutePreparationWorker.Snapshot();
        const auto requestKind = request.kind;
        const auto mailboxStarted = std::chrono::steady_clock::now();
        if (gRoutePreparationWorker.StartLatest(std::move(request))) {
            const auto mailboxUs =
                ElapsedMicrosecondsSince(mailboxStarted);
            gRouteAsyncRuntime.eligibleRequest = requestIdentity;
            gRouteAsyncRuntime.hasEligibleRequest = true;
            gRouteAsyncRuntime.eligibleKind = requestKind;
            if (requestKind == xvatsim::modules::route_sector::
                    RoutePreparationRequest::Kind::ObserveExpandedFms) {
                ++gRouteAsyncRuntime.fmsObservationDispatchCount;
            } else {
                ++gRouteAsyncRuntime.dispatchCount;
                if (runtimeOutput.needsWorker) {
                    ++gRouteAsyncRuntime.unresolvedRetryDispatchCount;
                }
                gRouteAsyncRuntime.nextRouteRetryMonotonicMs =
                    nowMonotonicMs +
                    kRadioBoardPendingRouteRetrySeconds * 1000;
            }
            const auto after = gRoutePreparationWorker.Snapshot();
            if (after.replacements > before.replacements) {
                AppendDeferredDiagnosticsLogLine(
                    [requestId = requestIdentity.requestId]() {
                        return std::string{"event=route-worker-pending-replaced requestId="} +
                            std::to_string(requestId);
                    });
            }
            const auto submitUs =
                ElapsedMicrosecondsSince(desiredPathStarted);
            gRouteAsyncRuntime.maximumSubmitUs = std::max<std::uint64_t>(
                gRouteAsyncRuntime.maximumSubmitUs,
                static_cast<std::uint64_t>(
                    std::max<long long>(0, submitUs)));
            gRouteAsyncRuntime.maximumMailboxExchangeUs =
                std::max<std::uint64_t>(
                    gRouteAsyncRuntime.maximumMailboxExchangeUs,
                    static_cast<std::uint64_t>(
                        std::max<long long>(0, mailboxUs)));
            {
                auto evidence = BuildRouteEvidenceRecord(
                    xvatsim::modules::runtime_workers::
                        RouteEvidenceRecordType::Dispatch,
                    requestKind,
                    requestIdentity,
                    coordinatorDecision.reason);
                evidence.running = after.running;
                evidence.pending = after.pending;
                evidence.maximumPendingDepth =
                    after.maximumPendingDepth;
                evidence.completeDesiredPackageSubmitUs =
                    static_cast<std::uint64_t>(
                        std::max<long long>(0, submitUs));
                evidence.mailboxExchangeUs =
                    static_cast<std::uint64_t>(
                        std::max<long long>(0, mailboxUs));
                SubmitRouteEvidence(std::move(evidence));
            }
            AppendDeferredDiagnosticsLogLine(
                [requestIdentity,
                 after,
                 submitUs,
                 mailboxUs,
                 kind = gRouteAsyncRuntime.eligibleKind,
                 reason = std::string{coordinatorDecision.reason}]() {
                    std::ostringstream line;
                    line << "event=route-worker-dispatch"
                         << " kind="
                         << (kind == xvatsim::modules::route_sector::
                                    RoutePreparationRequest::Kind::ObserveExpandedFms
                                 ? "expanded-fms-observation"
                                 : "route-preparation")
                         << " requestId=" << requestIdentity.requestId
                         << " lifecycleEpoch="
                         << requestIdentity.lifecycleEpoch
                         << " networkDigest="
                         << requestIdentity.networkPlanDigest
                         << " sourceIdentity="
                         << requestIdentity.routeSourceDatasetIdentity
                         << " preflightIdentity="
                         << requestIdentity.preflightCandidateIdentity
                         << " policyIdentity="
                         << requestIdentity.routePolicyIdentity
                         << " anchorDigest="
                         << requestIdentity.routeAnchorDigest
                         << " fmsObservationIdentity="
                         << requestIdentity.expandedFmsObservationIdentity
                         << " running=" << (after.running ? 1 : 0)
                         << " pending=" << (after.pending ? 1 : 0)
                         << " maxPendingDepth=" << after.maximumPendingDepth
                         << " completeDesiredPackageSubmitUs=" << submitUs
                         << " mailboxExchangeUs=" << mailboxUs
                         << " reason=" << reason;
                    return line.str();
                });
        }
    }

    if (!publishedThisCycle &&
        (!acceptedCurrent || runtimeOutput.needsWorker ||
         coordinatorDecision.failClosed)) {
        runtimeOutput.route =
            xvatsim::brain::BuildBrainRoutePolygonWorkerOutput(
                UnavailableRouteSnapshot(
                    !coordinatorInput.sourceUsable
                        ? "immutable-route-source-unavailable"
                        : "route-worker-pending"));
        runtimeOutput.reason = runtimeOutput.route.reason;
        runtimeOutput.cacheStatus =
            runtimeOutput.route.route->diagnosticCacheStatus;
    }

    recordTransitionDiagnostic(runtimeOutput);
    QueueRouteRetirement(std::move(runtimeOutput.retiredRoute));
    if (diagnostics != nullptr) {
        diagnostics->routeResolved = runtimeOutput.route.route != nullptr &&
            runtimeOutput.route.route->routeResolved;
        diagnostics->routeStatus = runtimeOutput.route.route != nullptr
            ? runtimeOutput.route.route->statusLine
            : "ROUTE preparation pending";
        diagnostics->routeResolveUs =
            ElapsedMicrosecondsSince(completeStageStarted);
        diagnostics->routeResolveMs = diagnostics->routeResolveUs / 1000;
    }
    if (DiagnosticJobsEnabled()) {
        RecordDiagnosticJob(
            "BrainRoutePolygonWorker",
            runtimeOutput.reason,
            diagnostics != nullptr ? diagnostics->routeResolveMs : 0,
            runtimeOutput.cacheStatus,
            runtimeOutput.diagnosticResult,
            {},
            routeRuntimeKey);
    }
    return runtimeOutput.route;
}

bool SameAuthorityCompletionInputs(
    const xvatsim::brain::BrainAuthorityCompletionIdentity& left,
    const xvatsim::brain::BrainAuthorityCompletionIdentity& right) {
    return left.lifecycleEpoch == right.lifecycleEpoch &&
           left.planKey == right.planKey &&
           left.routeDigest == right.routeDigest &&
           left.controllerDigest == right.controllerDigest &&
           left.transceiverDigest == right.transceiverDigest &&
           left.datasetIdentity == right.datasetIdentity;
}

xvatsim::brain::BrainAuthorityCompletionIdentity ToBrainAuthorityIdentity(
    const xvatsim::modules::route_sector::AuthorityWorkerIdentity& identity) {
    xvatsim::brain::BrainAuthorityCompletionIdentity output;
    output.requestId = identity.requestId;
    output.lifecycleEpoch = identity.lifecycleEpoch;
    output.planKey = identity.planKey;
    output.routeDigest = identity.routeDigest;
    output.controllerDigest = identity.controllerDigest;
    output.transceiverDigest = identity.transceiverDigest;
    output.datasetIdentity = identity.datasetIdentity;
    return output;
}

xvatsim::modules::route_sector::AuthorityWorkerIdentity ToWorkerAuthorityIdentity(
    const xvatsim::brain::BrainAuthorityCompletionIdentity& identity) {
    xvatsim::modules::route_sector::AuthorityWorkerIdentity output;
    output.requestId = identity.requestId;
    output.lifecycleEpoch = identity.lifecycleEpoch;
    output.planKey = identity.planKey;
    output.routeDigest = identity.routeDigest;
    output.controllerDigest = identity.controllerDigest;
    output.transceiverDigest = identity.transceiverDigest;
    output.datasetIdentity = identity.datasetIdentity;
    return output;
}

void InvalidateAuthorityAsyncRuntime(const char* reason) {
    gAuthorityRelevanceWorker.CancelPending();
    gAuthorityAsyncRuntime.hasExpectedIdentity = false;
    gAuthorityAsyncRuntime.expectedIdentity = {};
    if (gAuthorityAsyncRuntime.acceptedSnapshotLease != nullptr) {
        gAuthorityAsyncRuntime.acceptedSnapshotLease->retired.store(
            true, std::memory_order_release);
    }
    gAuthorityAsyncRuntime.acceptedSnapshot.reset();
    gAuthorityAsyncRuntime.acceptedSnapshotLease.reset();
    gAuthorityRelevanceWorker.NotifyRetirement();
    gAuthorityAsyncRuntime.acceptedSnapshotDigest = 0;
    gAuthorityAsyncRuntime.acceptedIdentity = {};
    gAuthorityAsyncRuntime.lastDispatchMonotonicMs = 0;
    gAuthorityAsyncRuntime.hasControllerDigest = false;
    gAuthorityAsyncRuntime.observedControllerGeneration = 0;
    gAuthorityAsyncRuntime.observedTransceiverGeneration = 0;
    ++gAuthorityAsyncRuntime.lifecycleEpoch;
    if (gAuthorityAsyncRuntime.lifecycleEpoch == 0) {
        gAuthorityAsyncRuntime.lifecycleEpoch = 1;
    }
    ++gAuthorityAsyncRuntime.invalidationCount;
    std::ostringstream line;
    line << "event=authority-invalidate"
         << " lifecycleEpoch=" << gAuthorityAsyncRuntime.lifecycleEpoch
         << " reason=" << (reason == nullptr ? "unspecified" : reason);
    AppendDiagnosticsLogLine(line.str());
}

std::shared_ptr<const xvatsim::brain::AuthorityRelevanceSnapshot>
UnavailableAuthoritySnapshot(const char* reason) {
    const auto build = [](const char* cacheStatus, const char* detail) {
        auto snapshot =
            std::make_shared<xvatsim::brain::AuthorityRelevanceSnapshot>();
        snapshot->available = false;
        snapshot->stale = true;
        snapshot->diagnosticCacheStatus = cacheStatus;
        snapshot->diagnosticReason = detail;
        snapshot->statusLine = "AUTHORITY verification pending";
        return std::shared_ptr<const xvatsim::brain::AuthorityRelevanceSnapshot>(
            std::move(snapshot));
    };
    static const auto datasetUnavailable = build(
        "authority-dataset-unavailable",
        "immutable-dataset-unavailable");
    static const auto workerPending = build(
        "authority-async-pending",
        "worker-pending-fail-closed");
    return reason != nullptr &&
                   std::string_view(reason) == "immutable-dataset-unavailable"
               ? datasetUnavailable
               : workerPending;
}

std::shared_ptr<const xvatsim::brain::AuthorityRelevanceSnapshot>
RefreshBrainAuthorityRelevanceSnapshot(
    const xvatsim::brain::AircraftStateSnapshot& aircraftState,
    const xvatsim::brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const xvatsim::brain::RouteSectorSnapshot& routeSectorSnapshot,
    const xvatsim::brain::TransceiverResolutionSnapshot& transceiverSnapshot,
    const std::string& planKey,
    RefreshDiagnosticsFrame* diagnostics) {
    const auto timingStarted = std::chrono::steady_clock::now();
    const auto datasetPublication =
        gRouteSectorResolver.GetAuthoritySourceDatasetPublication();
    const auto* dataset = datasetPublication.dataset;
    const auto& controllers = controllerFeedSnapshot.Controllers();
    auto authorityControllers =
        controllerFeedSnapshot.ownedAuthorityControllers;
    if (authorityControllers == nullptr &&
        controllerFeedSnapshot.controllers != nullptr) {
        authorityControllers = std::make_shared<
            const std::vector<xvatsim::brain::AuthorityControllerSnapshot>>(
                xvatsim::brain::BuildBrainAuthorityControllerEvidence(
                    controllers));
    }
    xvatsim::brain::BrainAuthorityCompletionIdentity currentIdentity;
    currentIdentity.lifecycleEpoch = gAuthorityAsyncRuntime.lifecycleEpoch;
    currentIdentity.planKey = planKey;
    currentIdentity.routeDigest =
        gBrainOwnedRuntimeState.authorityRouteDigest != 0
            ? gBrainOwnedRuntimeState.authorityRouteDigest
            : xvatsim::brain::HashBrainAuthorityRouteSnapshot(
                  routeSectorSnapshot);
    const auto authorityControllerContentDigest =
        authorityControllers != nullptr &&
                controllerFeedSnapshot.authorityControllerContentDigest != 0
            ? controllerFeedSnapshot.authorityControllerContentDigest
            : authorityControllers != nullptr
                ? xvatsim::brain::HashBrainAuthorityControllerEvidenceContent(
                      *authorityControllers)
                : xvatsim::brain::HashBrainAuthorityControllerEvidenceContent(
                      controllers);
    const auto controllerDigest =
        xvatsim::brain::HashBrainAuthorityControllerEvidenceFromContentDigest(
            authorityControllerContentDigest,
            controllerFeedSnapshot.available,
            controllerFeedSnapshot.stale);
    const auto controllerGenerationObserved =
        gAuthorityAsyncRuntime.observedControllerGeneration !=
            controllerFeedSnapshot.generation;
    if (controllerGenerationObserved &&
        gAuthorityAsyncRuntime.hasControllerDigest &&
        gAuthorityAsyncRuntime.controllerDigest == controllerDigest) {
        ++gAuthorityAsyncRuntime.controllerGenerationOnlyRefreshCount;
    }
    gAuthorityAsyncRuntime.observedControllerGeneration =
        controllerFeedSnapshot.generation;
    if (!gAuthorityAsyncRuntime.hasControllerDigest ||
        gAuthorityAsyncRuntime.controllerDigest != controllerDigest) {
        gAuthorityAsyncRuntime.controllerDigest = controllerDigest;
        gAuthorityAsyncRuntime.hasControllerDigest = true;
    }
    currentIdentity.controllerDigest = gAuthorityAsyncRuntime.controllerDigest;
    auto authorityTransceiverEvidence =
        gBrainOwnedRuntimeState.authorityTransceiverEvidence;
    if (authorityTransceiverEvidence == nullptr) {
        authorityTransceiverEvidence = std::make_shared<
            const xvatsim::brain::AuthorityTransceiverEvidenceSnapshot>(
                xvatsim::brain::BuildBrainAuthorityTransceiverEvidence(
                    transceiverSnapshot));
    }
    currentIdentity.transceiverDigest =
        gBrainOwnedRuntimeState.authorityTransceiverEvidence != nullptr
            ? gBrainOwnedRuntimeState.authorityTransceiverEvidenceDigest
            : xvatsim::brain::HashBrainAuthorityTransceiverEvidence(
                  *authorityTransceiverEvidence);
    const auto transceiverGenerationObserved =
        gAuthorityAsyncRuntime.observedTransceiverGeneration !=
            gBrainOwnedRuntimeState.transceiverObservationGeneration;
    if (transceiverGenerationObserved &&
        gAuthorityAsyncRuntime.hasExpectedIdentity &&
        gAuthorityAsyncRuntime.expectedIdentity.transceiverDigest ==
            currentIdentity.transceiverDigest) {
        ++gAuthorityAsyncRuntime.transceiverUnchangedRefreshCount;
    }
    gAuthorityAsyncRuntime.observedTransceiverGeneration =
        gBrainOwnedRuntimeState.transceiverObservationGeneration;
    currentIdentity.datasetIdentity = datasetPublication.identity;

    const auto currentInputsUs = ElapsedMicrosecondsSince(timingStarted);
    const auto expectedInputsChanged =
        gAuthorityAsyncRuntime.hasExpectedIdentity &&
        !SameAuthorityCompletionInputs(
            gAuthorityAsyncRuntime.expectedIdentity,
            currentIdentity);
    const auto acceptedInputsChanged =
        gAuthorityAsyncRuntime.acceptedSnapshot != nullptr &&
        !SameAuthorityCompletionInputs(
            gAuthorityAsyncRuntime.acceptedIdentity,
            currentIdentity);
    if (acceptedInputsChanged) {
        if (gAuthorityAsyncRuntime.acceptedSnapshotLease != nullptr) {
            gAuthorityAsyncRuntime.acceptedSnapshotLease->retired.store(
                true, std::memory_order_release);
        }
        gAuthorityAsyncRuntime.acceptedSnapshot.reset();
        gAuthorityAsyncRuntime.acceptedSnapshotLease.reset();
        gAuthorityRelevanceWorker.NotifyRetirement();
        gAuthorityAsyncRuntime.acceptedSnapshotDigest = 0;
        gAuthorityAsyncRuntime.acceptedIdentity = {};
        ++gAuthorityAsyncRuntime.invalidationCount;
    }
    const auto authorityInputsUsable =
        dataset != nullptr && currentIdentity.datasetIdentity != 0 &&
        !planKey.empty() && routeSectorSnapshot.available &&
        !routeSectorSnapshot.stale && routeSectorSnapshot.routeResolved;
    if (expectedInputsChanged && !authorityInputsUsable) {
        gAuthorityRelevanceWorker.CancelPending();
        gAuthorityAsyncRuntime.hasExpectedIdentity = false;
        gAuthorityAsyncRuntime.expectedIdentity = {};
        gAuthorityAsyncRuntime.lastDispatchMonotonicMs = 0;
    }

    xvatsim::modules::route_sector::AuthorityWorkerFact harvested;
    const auto harvestStarted = std::chrono::steady_clock::now();
    if (gAuthorityRelevanceWorker.TryHarvest(&harvested)) {
        xvatsim::brain::BrainAuthorityCompletionValidationInput validation;
        validation.expected = currentIdentity;
        validation.expected.requestId =
            gAuthorityAsyncRuntime.expectedIdentity.requestId;
        validation.completed = ToBrainAuthorityIdentity(harvested.identity);
        validation.currentAircraft = aircraftState;
        validation.dispatchedAircraft = harvested.dispatchedAircraft;
        validation.nowMonotonicMs = CurrentTickMilliseconds();
        validation.completedMonotonicMs = harvested.completedMonotonicMs;
        const auto decision =
            xvatsim::brain::DecideBrainAuthorityCompletion(validation);
        const auto publishable =
            decision.accepted && harvested.completed &&
            !harvested.cancelled && harvested.snapshotLease != nullptr &&
            harvested.snapshotLease->snapshot != nullptr;
        const auto terminalRequestId = harvested.identity.requestId;
        const auto terminalWorkerUs = harvested.workerElapsedUs;
        const auto terminalCompleted = harvested.completed;
        const auto terminalCancelled = harvested.cancelled;
        const auto terminalAccepted = decision.accepted;
        const auto terminalAgeMs = decision.ageMs;
        const auto terminalDisplacementNm = decision.displacementNm;
        const auto terminalReason = decision.reason;
        AppendDeferredDiagnosticsLogLine(
            [terminalRequestId,
             terminalWorkerUs,
             terminalCompleted,
             terminalCancelled,
             terminalAccepted,
             terminalAgeMs,
             terminalDisplacementNm,
             terminalReason]() {
                std::ostringstream terminal;
                terminal << "event=authority-terminal"
                         << " requestId=" << terminalRequestId
                         << " workerUs=" << terminalWorkerUs
                         << " completed=" << (terminalCompleted ? 1 : 0)
                         << " cancelled=" << (terminalCancelled ? 1 : 0)
                         << " accepted=" << (terminalAccepted ? 1 : 0)
                         << " ageMs=" << terminalAgeMs
                         << " displacementNm=" << terminalDisplacementNm
                         << " reason=" << terminalReason;
                return terminal.str();
            });
        AppendDeferredDiagnosticsLogLine(
            [terminalRequestId, publishable, terminalReason]() {
                std::ostringstream disposition;
                disposition << "event=authority-disposition"
                            << " requestId=" << terminalRequestId
                            << " disposition="
                            << (publishable ? "published" : "rejected")
                            << " reason=" << terminalReason;
                return disposition.str();
            });
        if (publishable) {
            if (gAuthorityAsyncRuntime.acceptedSnapshotLease != nullptr) {
                gAuthorityAsyncRuntime.acceptedSnapshotLease->retired.store(
                    true, std::memory_order_release);
            }
            gAuthorityAsyncRuntime.acceptedSnapshot.reset();
            gAuthorityAsyncRuntime.acceptedSnapshotLease.reset();
            gAuthorityAsyncRuntime.acceptedSnapshotLease =
                harvested.snapshotLease;
            gAuthorityAsyncRuntime.acceptedSnapshot =
                harvested.snapshotLease != nullptr
                ? harvested.snapshotLease->snapshot
                : nullptr;
            gAuthorityRelevanceWorker.NotifyRetirement();
            gAuthorityAsyncRuntime.acceptedSnapshotDigest =
                harvested.snapshotDigest;
            gAuthorityAsyncRuntime.acceptedIdentity = validation.completed;
            ++gAuthorityAsyncRuntime.acceptedCount;
        } else {
            if (harvested.snapshotLease != nullptr) {
                harvested.snapshotLease->retired.store(
                    true, std::memory_order_release);
                gAuthorityRelevanceWorker.NotifyRetirement();
            }
            ++gAuthorityAsyncRuntime.staleRejectedCount;
        }
    }
    const auto harvestUs = ElapsedMicrosecondsSince(harvestStarted);

    const auto nowMs = CurrentTickMilliseconds();
    const auto acceptedHasCenter =
        gAuthorityAsyncRuntime.acceptedSnapshot != nullptr &&
        std::any_of(
            gAuthorityAsyncRuntime.acceptedSnapshot->relevantAuthorities.begin(),
            gAuthorityAsyncRuntime.acceptedSnapshot->relevantAuthorities.end(),
            [](const auto& authority) {
                return authority.kind ==
                    xvatsim::brain::AuthorityRelevanceKind::Center;
            });
    const auto routineCadenceMs = acceptedHasCenter ? 60'000LL : 15'000LL;
    const auto routineDue =
        gAuthorityAsyncRuntime.lastDispatchMonotonicMs == 0 ||
        (nowMs - gAuthorityAsyncRuntime.lastDispatchMonotonicMs) >=
            routineCadenceMs;
    const auto needsDispatch =
        authorityInputsUsable &&
        (expectedInputsChanged || !gAuthorityAsyncRuntime.hasExpectedIdentity ||
         (routineDue && !gAuthorityRelevanceWorker.IsRunning()));
    if (needsDispatch) {
        currentIdentity.requestId = gAuthorityAsyncRuntime.nextRequestId++;
        if (gAuthorityAsyncRuntime.nextRequestId == 0) {
            gAuthorityAsyncRuntime.nextRequestId = 1;
        }
        xvatsim::modules::route_sector::AuthorityWorkerRequest request;
        request.identity = ToWorkerAuthorityIdentity(currentIdentity);
        request.aircraft = aircraftState;
        request.route =
            gBrainOwnedRuntimeState.authorityRouteSnapshot != nullptr &&
                    gBrainOwnedRuntimeState.authorityRouteDigest ==
                        currentIdentity.routeDigest
                ? gBrainOwnedRuntimeState.authorityRouteSnapshot
                : std::make_shared<
                      const xvatsim::brain::RouteSectorSnapshot>(
                          routeSectorSnapshot);
        request.controllers = authorityControllers;
        request.hasControllerEvidence =
            controllerFeedSnapshot.controllers != nullptr &&
            authorityControllers != nullptr;
        request.controllerFeedAvailable = controllerFeedSnapshot.available;
        request.controllerFeedStale = controllerFeedSnapshot.stale;
        request.controllerFeedGeneration = controllerFeedSnapshot.generation;
        request.controllerFeedConnectedControllers =
            controllerFeedSnapshot.connectedControllers;
        request.terminalBoundaryGeneration =
            datasetPublication.terminalBoundaryGeneration;
        request.scheduleReason = "controller-relevance-evidence-ledger";
        request.transceivers = authorityTransceiverEvidence;
        request.dataset = dataset;
        request.dispatchedMonotonicMs = nowMs;
        if (gAuthorityRelevanceWorker.StartLatest(std::move(request))) {
            gAuthorityAsyncRuntime.expectedIdentity = currentIdentity;
            gAuthorityAsyncRuntime.hasExpectedIdentity = true;
            gAuthorityAsyncRuntime.lastDispatchMonotonicMs = nowMs;
            ++gAuthorityAsyncRuntime.dispatchCount;
            const auto workerSnapshot = gAuthorityRelevanceWorker.Snapshot();
            const auto dispatchIdentity = currentIdentity;
            const auto dispatchSimulatorThreadUs =
                ElapsedMicrosecondsSince(timingStarted);
            const auto controllerGenerationOnlyRefreshes =
                gAuthorityAsyncRuntime.controllerGenerationOnlyRefreshCount;
            const auto transceiverUnchangedRefreshes =
                gAuthorityAsyncRuntime.transceiverUnchangedRefreshCount;
            AppendDeferredDiagnosticsLogLine(
                [dispatchIdentity,
                 workerRunning = workerSnapshot.running,
                 workerPending = workerSnapshot.pending,
                 maximumPendingDepth = workerSnapshot.maximumPendingDepth,
                 controllerGenerationOnlyRefreshes,
                 transceiverUnchangedRefreshes,
                 dispatchSimulatorThreadUs]() {
                    std::ostringstream dispatch;
                    dispatch << "event=authority-dispatch"
                             << " requestId=" << dispatchIdentity.requestId
                             << " lifecycleEpoch="
                             << dispatchIdentity.lifecycleEpoch
                             << " routeDigest=" << dispatchIdentity.routeDigest
                             << " controllerDigest="
                             << dispatchIdentity.controllerDigest
                             << " transceiverDigest="
                             << dispatchIdentity.transceiverDigest
                             << " datasetIdentity="
                             << dispatchIdentity.datasetIdentity
                             << " running=" << (workerRunning ? 1 : 0)
                             << " pending=" << (workerPending ? 1 : 0)
                             << " maxPendingDepth=" << maximumPendingDepth
                             << " controllerGenerationOnlyRefreshes="
                             << controllerGenerationOnlyRefreshes
                             << " transceiverUnchangedRefreshes="
                             << transceiverUnchangedRefreshes
                             << " simulatorThreadUs="
                             << dispatchSimulatorThreadUs;
                    return dispatch.str();
                });
        }
    }

    auto snapshot = gAuthorityAsyncRuntime.acceptedSnapshot;
    if (snapshot == nullptr ||
        !SameAuthorityCompletionInputs(
            gAuthorityAsyncRuntime.acceptedIdentity,
            currentIdentity)) {
        snapshot = UnavailableAuthoritySnapshot(
            dataset == nullptr ? "immutable-dataset-unavailable"
                               : "worker-pending-fail-closed");
    }
    const auto elapsedUs = ElapsedMicrosecondsSince(timingStarted);
    const auto hash =
        snapshot == gAuthorityAsyncRuntime.acceptedSnapshot
            ? gAuthorityAsyncRuntime.acceptedSnapshotDigest
            : xvatsim::brain::HashBrainAuthorityRelevanceSnapshot(*snapshot);

    if (diagnostics != nullptr) {
        diagnostics->authorityRelevanceUs = elapsedUs;
        diagnostics->authorityRelevanceMs = elapsedUs / 1000;
        diagnostics->authorityStatus = snapshot->statusLine;
        diagnostics->authorityCount =
            static_cast<int>(snapshot->relevantAuthorities.size());
        diagnostics->authoritySnapshotForDiagnostics = snapshot;
        diagnostics->hasAuthorityProofHash = true;
        diagnostics->authorityProofHash = hash;
    }

    if (DiagnosticJobsEnabled()) {
        std::ostringstream result;
        result << "authorities=" << snapshot->relevantAuthorities.size()
               << ",status=" << SanitizeLogText(snapshot->statusLine, 72)
               << ",proofRecords=" << snapshot->relevantAuthorities.size()
               << ",identityUs=" << currentInputsUs
               << ",harvestUs=" << harvestUs
               << ",running="
               << (gAuthorityRelevanceWorker.IsRunning() ? 1 : 0);
        RecordDiagnosticJob(
            "BrainAuthorityRelevanceWorker",
            snapshot->diagnosticReason.empty()
                ? snapshot->statusLine
                : snapshot->diagnosticReason,
            elapsedUs / 1000,
            snapshot->diagnosticCacheStatus,
            result.str(),
            FormatSourceGenerations(*snapshot),
            planKey);
    }
    return snapshot;
}

xvatsim::brain::FlightPlanSnapshot SampleFlightPlanForRuntime(
    const xvatsim::brain::AircraftStateSnapshot& aircraftState,
    RefreshDiagnosticsFrame* diagnostics) {
    const auto nowSeconds = CurrentTickSeconds();
    const auto& flightContext = gBrainOwnedRuntimeState.flightContext;
    xvatsim::brain::BrainOwnedFlightPlanSampleInput decisionInput;
    decisionInput.flightContextActive = flightContext.active;
    decisionInput.nowSeconds = nowSeconds;
    decisionInput.sampleCadenceSeconds = kActiveFlightPlanSampleCadenceSeconds;
    const auto decision =
        xvatsim::brain::DecideBrainOwnedFlightPlanSample(
            gBrainOwnedRuntimeState,
            decisionInput);
    if (!decision.shouldSample) {
        if (diagnostics != nullptr) {
            diagnostics->flightPlanUs = 0;
            diagnostics->flightPlanMs = 0;
        }
        if (DiagnosticJobsEnabled()) {
            RecordDiagnosticJob(
                "FlightPlanSampler",
                decision.reason,
                0,
                "flight-plan-cache-hit",
                "sample=skipped",
                {},
                flightContext.departureIcao + "->" +
                    flightContext.destinationIcao);
        }
        return decision.cachedSnapshot;
    }

    const auto timingStarted = std::chrono::steady_clock::now();
    auto snapshot = gFlightPlanSampler.Sample(aircraftState);
    if (diagnostics != nullptr) {
        diagnostics->flightPlanUs = ElapsedMicrosecondsSince(timingStarted);
        diagnostics->flightPlanMs = diagnostics->flightPlanUs / 1000;
    }
    xvatsim::brain::BrainOwnedFlightPlanSampleCommitInput commitInput;
    commitInput.nowSeconds = nowSeconds;
    commitInput.snapshot = snapshot;
    xvatsim::brain::CommitBrainOwnedFlightPlanSample(
        &gBrainOwnedRuntimeState,
        commitInput);
    return snapshot;
}

std::string SummarizeDiagnosticJobs(
    const RefreshDiagnosticsFrame& frame,
    std::size_t maxJobs = 12) {
    if (frame.jobs.empty()) {
        return "none";
    }

    std::ostringstream stream;
    const auto countToLog = std::min<std::size_t>(frame.jobs.size(), maxJobs);
    for (std::size_t index = 0; index < countToLog; ++index) {
        const auto& job = frame.jobs[index];
        if (index > 0) {
            stream << ";";
        }
        const auto stage = job.stage.empty() ? frame.stage : job.stage;
        stream << SanitizeLogText(job.name, 32)
               << "{ms=" << job.durationMs
               << ",stage=" << SanitizeLogText(stage, 12)
               << ",reason=" << SanitizeLogText(job.reason, 48)
               << ",cache=" << SanitizeLogText(job.cacheStatus, 48)
               << ",src=" << SanitizeLogText(job.sourceGenerations, 72)
               << ",key=" << SanitizeLogText(job.routeKey, 72)
               << ",result=" << SanitizeLogText(job.result, 120)
               << "}";
    }
    if (frame.jobs.size() > countToLog) {
        stream << ";+" << (frame.jobs.size() - countToLog);
    }
    return stream.str();
}

std::size_t HashAuthorityProofSummary(const std::string& summary) {
    std::size_t hash = 0;
    HashCombineString(&hash, summary);
    return hash;
}

std::filesystem::path ResolvePluginRootPath() {
    char pluginPath[1024] = {};
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, pluginPath, nullptr, nullptr);
    auto rootPath = std::filesystem::path(pluginPath).parent_path();
    const auto platformFolder = rootPath.filename().string();
    if (platformFolder == "win_x64" ||
        platformFolder == "mac_x64" ||
        platformFolder == "lin_x64") {
        rootPath = rootPath.parent_path();
    }
    return rootPath;
}

void AppendDiagnosticsLogLine(
    std::string line,
    xvatsim::modules::runtime_workers::DiagnosticsImportance importance) {
    (void)gDiagnosticsWriter.TryEnqueueLine(std::move(line), importance);
}

void AppendDeferredDiagnosticsLogLine(
    xvatsim::modules::runtime_workers::AsyncDiagnosticsWriter::DeferredFormatter
        formatter,
    xvatsim::modules::runtime_workers::DiagnosticsImportance importance) {
    (void)gDiagnosticsWriter.TryEnqueueDeferred(
        std::move(formatter), importance);
}

void RecordOperationalServiceCall(
    const xvatsim::brain::BrainOwnedOperationalActivationDecision& decision,
    xvatsim::brain::BrainOwnedOperationalServiceStage stage) {
    xvatsim::brain::RecordBrainOwnedOperationalServiceCall(
        &gOperationalActivationState, decision, stage);
}

void LogOperationalActivationTransition(
    const xvatsim::brain::BrainOwnedOperationalActivationDecision& decision,
    SessionBoundaryResult sessionBoundaryResult) {
    if (!decision.initialObservation && !decision.activationRisingEdge &&
        !decision.deactivationFallingEdge) {
        return;
    }
    const auto snapshot = gOperationalActivationState;
    AppendDeferredDiagnosticsLogLine(
        [decision, sessionBoundaryResult, snapshot]() {
            std::ostringstream stream;
            stream << "event=runtime-activation-transition"
                   << " reason=" << xvatsim::brain::ToString(decision.reason)
                   << " operational=" << (decision.operational ? 1 : 0)
                   << " initial=" << (decision.initialObservation ? 1 : 0)
                   << " rising="
                   << (decision.activationRisingEdge ? 1 : 0)
                   << " falling="
                   << (decision.deactivationFallingEdge ? 1 : 0)
                   << " disconnectFalling="
                   << (decision.disconnectFallingEdge ? 1 : 0)
                   << " sessionBoundary="
                   << static_cast<int>(sessionBoundaryResult)
                   << " callbacks=" << snapshot.callbacks
                   << " dormantCallbacks=" << snapshot.dormantCallbacks
                   << " operationalCallbacks="
                   << snapshot.operationalCallbacks
                   << " dormantOperationalAttempts="
                   << snapshot.dormantOperationalAttemptCount;
            return stream.str();
        },
        xvatsim::modules::runtime_workers::DiagnosticsImportance::Critical);
}

void LogOperationalActivationSummary() {
    const auto snapshot = gOperationalActivationState;
    const auto refreshGate =
        gBrainOwnedRuntimeState.operationalRefreshGate;
    AppendDeferredDiagnosticsLogLine(
        [snapshot, refreshGate]() {
            std::ostringstream stream;
            stream << "event=runtime-activation-gate-summary"
                   << " callbacks=" << snapshot.callbacks
                   << " dormantCallbacks=" << snapshot.dormantCallbacks
                   << " operationalCallbacks="
                   << snapshot.operationalCallbacks
                   << " enableImmediateWakeRequests="
                   << snapshot.enableImmediateWakeRequests
                   << " enableImmediateCallbacks="
                   << snapshot.enableImmediateCallbacks
                   << " enableImmediateOperationalCallbacks="
                   << snapshot.enableImmediateOperationalCallbacks
                   << " enableImmediateDormantCallbacks="
                   << snapshot.enableImmediateDormantCallbacks
                   << " enableImmediateWakePending="
                   << (snapshot.enableImmediateWakePending ? 1 : 0)
                   << " activationRisingEdges="
                   << snapshot.activationRisingEdges
                   << " deactivationFallingEdges="
                   << snapshot.deactivationFallingEdges
                   << " disconnectFallingEdges="
                   << snapshot.disconnectFallingEdges
                   << " dormantOperationalAttempts="
                   << snapshot.dormantOperationalAttemptCount
                   << " fullRefreshes=" << refreshGate.fullRefreshCount
                   << " settledFastPaths="
                   << refreshGate.settledFastPathCount
                   << " refreshGateReason="
                   << SanitizeLogText(refreshGate.lastDecisionReason, 64);
            for (std::size_t index = 0;
                 index < xvatsim::brain::kBrainOwnedOperationalServiceStageCount;
                 ++index) {
                const auto stage = static_cast<
                    xvatsim::brain::BrainOwnedOperationalServiceStage>(index);
                stream << " " << xvatsim::brain::ToString(stage)
                       << "Calls=" << snapshot.operationalServiceCalls[index]
                       << " " << xvatsim::brain::ToString(stage)
                       << "DormantAttempts="
                       << snapshot.dormantOperationalAttempts[index];
            }
            return stream.str();
        },
        xvatsim::modules::runtime_workers::DiagnosticsImportance::Critical);
}

void ResetDiagnosticsTraceState() {
    gDiagnosticsState.hasLastRadioBoardTraceHash = false;
    gDiagnosticsState.lastRadioBoardTraceHash = 0;
    gDiagnosticsState.hasLastCompletionTraceHash = false;
    gDiagnosticsState.lastCompletionTraceHash = 0;
}

std::string SanitizeLogToken(
    std::string value,
    std::size_t maxChars = kMaxLogFieldChars) {
    auto token = SanitizeLogText(std::move(value), maxChars);
    for (auto& character : token) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isspace(byte) != 0 ||
            character == '"' ||
            character == '\'' ||
            character == ',' ||
            character == ';') {
            character = '_';
        }
    }
    return token.empty() ? "none" : token;
}

std::string FormatTraceDistance(bool hasDistance, double distanceNm) {
    if (!hasDistance) {
        return "na";
    }

    return std::to_string(static_cast<int>(std::round(distanceNm)));
}

std::string JoinTraceTokens(
    const std::vector<std::string>& values,
    std::size_t maxItems,
    std::size_t maxChars) {
    if (values.empty()) {
        return "none";
    }

    std::ostringstream stream;
    const auto countToLog = std::min<std::size_t>(values.size(), maxItems);
    for (std::size_t index = 0; index < countToLog; ++index) {
        if (index > 0) {
            stream << ";";
        }
        stream << SanitizeLogToken(values[index], maxChars);
    }
    if (values.size() > countToLog) {
        stream << ";+" << (values.size() - countToLog);
    }
    return stream.str();
}

std::string FormatRadioCandidateTrace(
    const xvatsim::brain::RadioReachableControllerCandidate& candidate) {
    std::ostringstream stream;
    stream << SanitizeLogToken(candidate.callsign, 32)
           << "@"
           << SanitizeLogToken(candidate.frequency, 16)
           << ":facility=" << candidate.vatsimFacility
           << ":group=" << xvatsim::brain::ToString(candidate.group)
           << ":source=" << xvatsim::brain::ToString(candidate.source)
           << ":actionable=" << (candidate.actionable ? 1 : 0)
           << ":atis=" << (candidate.atis ? 1 : 0)
           << ":vr=" << candidate.visualRangeNm
           << ":dist="
           << FormatTraceDistance(
                  candidate.hasDistanceNm,
                  candidate.distanceNm)
           << ":stable=" << SanitizeLogToken(candidate.stableKey, 96);
    return stream.str();
}

std::string FormatRadioCandidateTraceList(
    const xvatsim::brain::RadioReachableControllerSnapshot& snapshot) {
    if (snapshot.candidates.empty()) {
        return "none";
    }

    std::ostringstream stream;
    const auto countToLog =
        std::min<std::size_t>(
            snapshot.candidates.size(),
            kDiagnosticsMaxTraceItems);
    for (std::size_t index = 0; index < countToLog; ++index) {
        if (index > 0) {
            stream << ";";
        }
        stream << FormatRadioCandidateTrace(snapshot.candidates[index]);
    }
    if (snapshot.candidates.size() > countToLog) {
        stream << ";+" << (snapshot.candidates.size() - countToLog);
    }
    return stream.str();
}

std::string FormatCompletionTrace(
    const xvatsim::brain::BrainOwnedCandidateCompletion& completion) {
    const auto completed =
        completion.decision !=
        xvatsim::brain::BrainOwnedCandidateDecision::Pending;
    std::ostringstream stream;
    stream << SanitizeLogToken(completion.callsign, 32)
           << "@"
           << SanitizeLogToken(completion.frequency, 16)
           << ":facility=" << xvatsim::brain::ToString(completion.facilityGroup)
           << ":decision="
           << SanitizeLogToken(xvatsim::brain::ToString(completion.decision), 24)
           << ":complete=" << (completed ? 1 : 0)
           << ":display=" << (completion.displayed ? "displayed" : "hidden")
           << ":relation="
           << SanitizeLogToken(
                  xvatsim::brain::ToString(completion.displayRelation),
                  32)
           << ":current="
           << SanitizeLogToken(completion.currentPolygonKey, 64)
           << ":matched="
           << SanitizeLogToken(completion.matchedPolygonKey, 64)
           << ":currentIndex=" << completion.currentPolygonIndex
           << ":inputHash=" << completion.radioBoardHash
           << "/" << completion.routePolygonHash
           << ":entryNm="
           << FormatTraceDistance(
                  completion.hasRouteEntryDistance,
                  completion.routeEntryDistanceNm)
           << ":reason=" << SanitizeLogToken(completion.reason, 80)
           << ":stable=" << SanitizeLogToken(completion.stableKey, 128);
    return stream.str();
}

std::string FormatCompletionTraceList(
    const std::vector<xvatsim::brain::BrainOwnedCandidateCompletion>&
        completions) {
    if (completions.empty()) {
        return "none";
    }

    std::ostringstream stream;
    const auto countToLog =
        std::min<std::size_t>(completions.size(), kDiagnosticsMaxTraceItems);
    for (std::size_t index = 0; index < countToLog; ++index) {
        if (index > 0) {
            stream << ";";
        }
        stream << FormatCompletionTrace(completions[index]);
    }
    if (completions.size() > countToLog) {
        stream << ";+" << (completions.size() - countToLog);
    }
    return stream.str();
}

bool ShouldEmitTraceHash(
    std::size_t hash,
    bool* hasLastHash,
    std::size_t* lastHash) {
    if (hasLastHash == nullptr || lastHash == nullptr) {
        return true;
    }

    if (*hasLastHash && *lastHash == hash) {
        return false;
    }

    *hasLastHash = true;
    *lastHash = hash;
    return true;
}

void LogRadioBoardCandidateDiffTrace(
    xvatsim::brain::WorkflowStage workflowStage,
    const std::string& planKey,
    const xvatsim::brain::RadioReachableControllerSnapshot& snapshot,
    const xvatsim::brain::RadioReachableCandidateDiff& diff) {
    const auto& state = gBrainOwnedRuntimeState;
    const auto diffMatchesSnapshot =
        diff.currentHash == snapshot.stableHash;
    const auto candidateSetUnchanged =
        diff.added == 0 && diff.removed == 0 &&
        diff.previousCandidates == diff.currentCandidates;
    if (candidateSetUnchanged && !kDiagnosticsVerboseUnchangedCandidateDiff) {
        return;
    }
    std::size_t traceHash = 0;
    HashCombine(&traceHash, static_cast<std::size_t>(workflowStage));
    HashCombineString(&traceHash, planKey);
    HashCombine(&traceHash, snapshot.stableHash);
    HashCombine(&traceHash, state.routePolygonHash);
    HashCombine(&traceHash, diff.previousHash);
    HashCombine(&traceHash, diff.currentHash);
    HashCombine(&traceHash, diff.added);
    HashCombine(&traceHash, diff.removed);
    if (!ShouldEmitTraceHash(
            traceHash,
            &gDiagnosticsState.hasLastRadioBoardTraceHash,
            &gDiagnosticsState.lastRadioBoardTraceHash)) {
        return;
    }

    const auto phase = std::string{WorkflowStageToken(workflowStage)};
    const auto route = gDiagnosticsState.frame.route;
    const auto currentPolygon = state.currentPolygonKey;
    const auto nextPolygon = state.nextPolygonKey;
    const auto arrivalPolygon = state.arrivalPolygonKey;
    const auto routePolygonHash = state.routePolygonHash;
    const auto snapshotForWriter = snapshot;
    const auto diffForWriter = diff;
    AppendDeferredDiagnosticsLogLine(
        [phase,
         planKey,
         route,
         currentPolygon,
         nextPolygon,
         arrivalPolygon,
         routePolygonHash,
         snapshot = std::move(snapshotForWriter),
         diff = std::move(diffForWriter),
         diffMatchesSnapshot]() {
            std::ostringstream stream;
            stream << "event=radio-board-candidate-diff"
                   << " phase=" << phase
                   << " plan=" << SanitizeLogToken(planKey, 128)
                   << " route=" << SanitizeLogToken(route, 32)
                   << " currentPolygon="
                   << SanitizeLogToken(currentPolygon, 64)
                   << " nextPolygon=" << SanitizeLogToken(nextPolygon, 64)
                   << " arrivalPolygon="
                   << SanitizeLogToken(arrivalPolygon, 64)
                   << " inputHash=" << snapshot.stableHash
                   << "/" << routePolygonHash
                   << " rawRadioHash=" << snapshot.stableHash
                   << " routeHash=" << routePolygonHash
                   << " generation=" << snapshot.generation
                   << " available=" << (snapshot.available ? 1 : 0)
                   << " stale=" << (snapshot.stale ? 1 : 0)
                   << " source=" << xvatsim::brain::ToString(snapshot.source)
                   << " previousHash=" << diff.previousHash
                   << " currentHash=" << diff.currentHash
                   << " diffMatchesSnapshot="
                   << (diffMatchesSnapshot ? 1 : 0)
                   << " previousCandidates=" << diff.previousCandidates
                   << " currentCandidates=" << diff.currentCandidates
                   << " added=" << diff.added
                   << " removed=" << diff.removed
                   << " unchanged=" << diff.unchanged
                   << " addedKeys="
                   << JoinTraceTokens(
                          diff.addedStableKeys,
                          kDiagnosticsMaxTraceItems,
                          128)
                   << " removedKeys="
                   << JoinTraceTokens(
                          diff.removedStableKeys,
                          kDiagnosticsMaxTraceItems,
                          128)
                   << " candidates="
                   << FormatRadioCandidateTraceList(snapshot)
                   << " status="
                   << SanitizeLogToken(snapshot.statusLine, 180)
                   << " diffStatus="
                   << SanitizeLogToken(diff.statusLine, 180);
            return stream.str();
        });
}

void LogCandidateCompletionTrace(
    xvatsim::brain::WorkflowStage workflowStage,
    const std::string& planKey) {
    const auto& state = gBrainOwnedRuntimeState;
    std::size_t traceHash = 0;
    HashCombine(&traceHash, static_cast<std::size_t>(workflowStage));
    HashCombineString(&traceHash, planKey);
    HashCombine(&traceHash, state.gatedRadioSnapshot.stableHash);
    HashCombine(&traceHash, state.radioSnapshot.stableHash);
    HashCombine(&traceHash, state.routePolygonHash);
    HashCombineBool(&traceHash, state.candidatesComplete);
    HashCombine(&traceHash, state.candidateCompletions.size());
    for (const auto& completion : state.candidateCompletions) {
        HashCombineString(&traceHash, completion.stableKey);
        HashCombine(
            &traceHash, static_cast<std::size_t>(completion.decision));
        HashCombineBool(&traceHash, completion.displayed);
        HashCombineString(&traceHash, completion.currentPolygonKey);
        HashCombineString(&traceHash, completion.matchedPolygonKey);
        HashCombineString(&traceHash, completion.reason);
    }
    if (!ShouldEmitTraceHash(
            traceHash,
            &gDiagnosticsState.hasLastCompletionTraceHash,
            &gDiagnosticsState.lastCompletionTraceHash)) {
        return;
    }

    const auto phase = std::string{WorkflowStageToken(workflowStage)};
    const auto route = gDiagnosticsState.frame.route;
    const auto currentPolygon = state.currentPolygonKey;
    const auto nextPolygon = state.nextPolygonKey;
    const auto arrivalPolygon = state.arrivalPolygonKey;
    const auto currentPolygonIndex = state.currentPolygonIndex;
    const auto gatedRadioHash = state.gatedRadioSnapshot.stableHash;
    const auto rawRadioHash = state.radioSnapshot.stableHash;
    const auto routePolygonHash = state.routePolygonHash;
    const auto candidatesComplete = state.candidatesComplete;
    const auto completions = state.candidateCompletions;
    AppendDeferredDiagnosticsLogLine(
        [phase,
         planKey,
         route,
         currentPolygon,
         nextPolygon,
         arrivalPolygon,
         currentPolygonIndex,
         gatedRadioHash,
         rawRadioHash,
         routePolygonHash,
         candidatesComplete,
         completions]() {
            std::ostringstream stream;
            stream << "event=candidate-completion-trace"
                   << " phase=" << phase
                   << " plan=" << SanitizeLogToken(planKey, 128)
                   << " route=" << SanitizeLogToken(route, 32)
                   << " currentPolygon="
                   << SanitizeLogToken(currentPolygon, 64)
                   << " nextPolygon=" << SanitizeLogToken(nextPolygon, 64)
                   << " arrivalPolygon="
                   << SanitizeLogToken(arrivalPolygon, 64)
                   << " currentIndex=" << currentPolygonIndex
                   << " inputHash=" << gatedRadioHash
                   << "/" << routePolygonHash
                   << " rawRadioHash=" << rawRadioHash
                   << " gatedRadioHash=" << gatedRadioHash
                   << " routeHash=" << routePolygonHash
                   << " candidatesComplete="
                   << (candidatesComplete ? 1 : 0)
                   << " completions=" << completions.size()
                   << " results=" << FormatCompletionTraceList(completions);
            return stream.str();
        });
}

long long SumTrackedRefreshMicroseconds(const RefreshDiagnosticsFrame& frame) {
    return frame.aircraftStateUs +
           frame.xpilotPollUs +
           frame.vatsimFeedUs +
           frame.vnasFeedUs +
           frame.controllerFeedUs +
           frame.flightPlanUs +
           frame.networkPlanUs +
           frame.radioUs +
           frame.pdcPrivateSourceUs +
           frame.manualQueryUs +
           frame.flightContextUs +
           frame.ctafUs +
           frame.routeResolveUs +
           frame.routeAuthorityPlanUs +
           frame.authorityStationsUs +
           frame.authorityRelevanceUs +
           frame.departureBoardUs +
           frame.arrivalBoardUs +
           frame.enrouteBoardUs +
           frame.workflowUs +
           frame.standbyAssistUs +
           frame.wakeDecisionUs +
           frame.radioRangeResolveUs +
           frame.overlayBuildUs +
           frame.overlayUpdateUs +
           frame.displayLoggingUs +
           frame.refreshGateUs;
}

void MaybeLogRefreshDiagnostics(long long totalRefreshMs, long long totalRefreshUs) {
    if (!gDiagnosticsState.frame.valid) {
        return;
    }

    auto& frame = gDiagnosticsState.frame;
    const auto nowSeconds = CurrentTickSeconds();
    const auto authorityHash =
        frame.hasAuthorityProofHash
            ? frame.authorityProofHash
            : HashAuthorityProofSummary(frame.authorityProofSummary);
    const auto authorityChanged =
        !gDiagnosticsState.hasLastAuthorityHash ||
        authorityHash != gDiagnosticsState.lastAuthorityHash;
    const auto slowRefresh = totalRefreshMs >= kDiagnosticsSlowRefreshThresholdMs;
    const auto shouldLogSlow =
        slowRefresh &&
        (nowSeconds - gDiagnosticsState.lastSlowRefreshSeconds) >=
            kDiagnosticsSlowRefreshLogIntervalSeconds;
    const auto shouldLogSummary =
        (nowSeconds - gDiagnosticsState.lastSummarySeconds) >=
        kDiagnosticsSummaryIntervalSeconds;

    if (!authorityChanged && !shouldLogSlow && !shouldLogSummary) {
        return;
    }

    if (authorityChanged) {
        gDiagnosticsState.hasLastAuthorityHash = true;
        gDiagnosticsState.lastAuthorityHash = authorityHash;
    }
    if (shouldLogSlow) {
        gDiagnosticsState.lastSlowRefreshSeconds = nowSeconds;
    }
    if (shouldLogSummary) {
        gDiagnosticsState.lastSummarySeconds = nowSeconds;
    }

    auto frameForWriter = std::move(frame);
    AppendDeferredDiagnosticsLogLine(
        [frame = std::move(frameForWriter),
         totalRefreshMs,
         totalRefreshUs,
         authorityChanged,
         slowRefresh]() mutable {
            if (frame.authoritySnapshotForDiagnostics != nullptr) {
                frame.authorityProofSummary = SummarizeAuthorityProofs(
                    *frame.authoritySnapshotForDiagnostics);
            }
            frame.authorityProofSummary =
                SanitizeLogText(std::move(frame.authorityProofSummary), 1200);
            const auto trackedRefreshUs =
                SumTrackedRefreshMicroseconds(frame);
            const auto untrackedRefreshUs =
                totalRefreshUs > trackedRefreshUs
                    ? totalRefreshUs - trackedRefreshUs
                    : 0;
            std::ostringstream stream;
            stream << "event="
                   << (authorityChanged
                           ? "authority-proof"
                           : (slowRefresh ? "slow-refresh" : "summary"))
                   << " totalMs=" << totalRefreshMs
                   << " totalUs=" << totalRefreshUs
                   << " stage=" << SanitizeLogText(frame.stage, 16)
                   << " reason=" << SanitizeLogText(frame.stageReason, 40)
                   << " wake=" << (frame.shouldWake ? 1 : 0)
                   << " wakeReason=" << SanitizeLogText(frame.wakeReason, 40)
                   << " refreshGate="
                   << SanitizeLogText(frame.refreshGateReason, 40)
                   << " xpilot=" << (frame.xpilotConnected ? 1 : 0)
                   << " battery=" << (frame.batteryOn ? 1 : 0)
                   << " ground=" << (frame.onGround ? 1 : 0)
                   << " callsign=" << SanitizeLogText(frame.callsign, 32)
                   << " route=" << SanitizeLogText(frame.route, 32)
                   << " controllers=" << frame.controllerCount
                   << " routeResolved=" << (frame.routeResolved ? 1 : 0)
                   << " authorities=" << frame.authorityCount
                   << " enrouteStations=" << frame.enrouteStationCount
                   << " timings=xpilot:" << frame.xpilotPollMs
                   << ",vatsim:" << frame.vatsimFeedMs
                   << ",vnas:" << frame.vnasFeedMs
                   << ",controllers:" << frame.controllerFeedMs
                   << ",flightPlan:" << frame.flightPlanMs
                   << ",networkPlan:" << frame.networkPlanMs
                   << ",radio:" << frame.radioMs
                   << ",ctaf:" << frame.ctafMs
                   << ",depBoard:" << frame.departureBoardMs
                   << ",arrBoard:" << frame.arrivalBoardMs
                   << ",route:" << frame.routeResolveMs
                   << ",routePlan:" << frame.routeAuthorityPlanMs
                   << ",authorityStations:" << frame.authorityStationsMs
                   << ",authorityRelevance:" << frame.authorityRelevanceMs
                   << ",enrBoard:" << frame.enrouteBoardMs
                   << ",workflow:" << frame.workflowMs
                   << ",radioRange:" << frame.radioRangeResolveMs
                   << ",overlayBuild:" << frame.overlayBuildMs
                   << ",overlayUpdate:" << frame.overlayUpdateMs
                   << " usTimings=aircraft:" << frame.aircraftStateUs
                   << ",xpilot:" << frame.xpilotPollUs
                   << ",vatsim:" << frame.vatsimFeedUs
                   << ",vnas:" << frame.vnasFeedUs
                   << ",controllers:" << frame.controllerFeedUs
                   << ",flightPlan:" << frame.flightPlanUs
                   << ",networkPlan:" << frame.networkPlanUs
                   << ",radio:" << frame.radioUs
                   << ",pdcPrivateSource:" << frame.pdcPrivateSourceUs
                   << ",manualQuery:" << frame.manualQueryUs
                   << ",context:" << frame.flightContextUs
                   << ",ctaf:" << frame.ctafUs
                   << ",route:" << frame.routeResolveUs
                   << ",routePlan:" << frame.routeAuthorityPlanUs
                   << ",authorityStations:" << frame.authorityStationsUs
                   << ",authorityRelevance:" << frame.authorityRelevanceUs
                   << ",departure:" << frame.departureBoardUs
                   << ",arrival:" << frame.arrivalBoardUs
                   << ",enroute:" << frame.enrouteBoardUs
                   << ",workflow:" << frame.workflowUs
                   << ",standbyAssist:" << frame.standbyAssistUs
                   << ",wakeDecision:" << frame.wakeDecisionUs
                   << ",radioRange:" << frame.radioRangeResolveUs
                   << ",overlayBuild:" << frame.overlayBuildUs
                   << ",overlayUpdate:" << frame.overlayUpdateUs
                   << ",displayLog:" << frame.displayLoggingUs
                   << ",refreshGate:" << frame.refreshGateUs
                   << ",tracked:" << trackedRefreshUs
                   << ",untracked:" << untrackedRefreshUs
                   << " routeStatus=\""
                   << SanitizeLogText(frame.routeStatus, 180)
                   << "\" authorityStatus=\""
                   << SanitizeLogText(frame.authorityStatus, 180)
                   << "\" vnasStatus=\""
                   << SanitizeLogText(frame.vnasStatus, 180)
                   << "\" authorityProofs=\""
                   << frame.authorityProofSummary
                   << "\" jobs=\""
                   << SummarizeDiagnosticJobs(frame, 16) << "\"";
            return stream.str();
        });
}

std::string SummarizeRouteSectors(
    const std::vector<xvatsim::brain::RouteSectorMatchSnapshot>& sectors) {
    if (sectors.empty()) {
        return "none";
    }

    std::ostringstream stream;
    const auto countToLog = std::min<std::size_t>(sectors.size(), 4);
    for (std::size_t index = 0; index < countToLog; ++index) {
        if (index > 0) {
            stream << ",";
        }
        stream << SanitizeLogText(sectors[index].identifier, 32);
    }
    if (sectors.size() > countToLog) {
        stream << ",+" << (sectors.size() - countToLog);
    }
    return stream.str();
}

std::string SummarizeRouteAuthorityPlan(
    const xvatsim::brain::RouteAuthorityPlan& plan) {
    if (plan.polygons.empty()) {
        return "none";
    }

    std::ostringstream stream;
    const auto countToLog = std::min<std::size_t>(plan.polygons.size(), 5);
    for (std::size_t index = 0; index < countToLog; ++index) {
        if (index > 0) {
            stream << ",";
        }
        const auto& polygon = plan.polygons[index];
        stream << polygon.sequence << ":"
               << SanitizeLogText(polygon.polygonKey, 32);
        if (polygon.current) {
            stream << ":cur";
        } else if (polygon.next) {
            stream << ":next";
        } else if (polygon.arrival) {
            stream << ":arr";
        }
    }
    if (plan.polygons.size() > countToLog) {
        stream << ",+" << (plan.polygons.size() - countToLog);
    }
    return stream.str();
}

std::string SummarizeAuthorityGapSectors(
    const xvatsim::brain::RouteSectorSnapshot& routeSectorSnapshot) {
    std::vector<std::string> gaps;
    auto appendGaps = [&](const auto& sectors, const char* label) {
        for (const auto& sector : sectors) {
            if (!sector.controllerCallsignPatterns.empty() ||
                !sector.controllerPrefixes.empty()) {
                continue;
            }
            gaps.push_back(
                std::string(label) + ":" +
                SanitizeLogText(sector.identifier, 32));
        }
    };

    appendGaps(routeSectorSnapshot.currentSectors, "current");
    appendGaps(routeSectorSnapshot.nextSectors, "next");
    if (gaps.empty()) {
        return "none";
    }

    std::ostringstream stream;
    const auto countToLog = std::min<std::size_t>(gaps.size(), 4);
    for (std::size_t index = 0; index < countToLog; ++index) {
        if (index > 0) {
            stream << ",";
        }
        stream << gaps[index];
    }
    if (gaps.size() > countToLog) {
        stream << ",+" << (gaps.size() - countToLog);
    }
    return stream.str();
}

void ResetPresentationStateForColdDark() {
    DiscardPendingTextEntryState();
    xvatsim::brain::ClearBrainOwnedManualQuery(&gBrainOwnedRuntimeState);
    ResetFlightScopedManualPlanState();
    ResetFlightProgressStateForNewContext();
    xvatsim::brain::ClearBrainOwnedFlightContext(&gBrainOwnedRuntimeState);
    xvatsim::brain::ClearBrainOwnedLastSampledFacts(&gBrainOwnedRuntimeState);
    ClearXPilotConnectionTracking();
    ClearFlightRecoveryState();
    xvatsim::brain::ClearBrainOwnedAircraftStateInvalidBoundary(
        &gBrainOwnedRuntimeState);
    xvatsim::brain::ResetBrainOwnedPdcProductState(
        &gBrainOwnedRuntimeState, false);
    xvatsim::brain::ClearBrainOwnedManualQuery(&gBrainOwnedRuntimeState);
    ResetBrainDisplayPublisherCache();
    ResetStandbyAssistLatch();
}

void DiscardPendingAccessoryClickFacts() {
    gAccessoryInputAccounting.lifecycleDiscards +=
        gOverlayWindow.DiscardPendingAccessoryClickFacts();
}

void ResetSessionState() {
    (void)ApplyBoundAsyncWorkerLifecycleBoundary(true);
    DiscardPendingAccessoryClickFacts();
    xvatsim::brain::ResetBrainOwnedAccessoryForSessionReset(
        &gBrainOwnedRuntimeState);
    ResetPluginRuntimeState(true, true, true);
    XPLMDebugString("[XVatsim] Session reset for next flight.\n");
    RefreshOverlayFromBrain();
}

void ApplyAircraftRuntimeBoundaryDecision(
    const xvatsim::brain::workflow::AircraftRuntimeBoundaryDecision& decision) {
    if (decision.shouldResetForInvalidAircraftState) {
        DiscardPendingAccessoryClickFacts();
        xvatsim::brain::CloseBrainOwnedAccessoryForInvalidAircraft(
            &gBrainOwnedRuntimeState);
        ResetPluginRuntimeState(false, false, true);
    }
    if (decision.shouldResetSessionRuntimeCaches) {
        (void)ApplyBoundAsyncWorkerLifecycleBoundary(true);
        DiscardPendingAccessoryClickFacts();
        xvatsim::brain::ResetBrainOwnedAccessoryForConfirmedColdDark(
            &gBrainOwnedRuntimeState);
        ResetSessionRuntimeCaches(true, true);
    }
    if (decision.shouldResetPresentationState) {
        ResetPresentationStateForColdDark();
    }
    if (!decision.logLine.empty()) {
        XPLMDebugString(decision.logLine.c_str());
    }

    xvatsim::brain::ApplyBrainOwnedAircraftRuntimeBoundaryDecision(
        &gBrainOwnedRuntimeState,
        decision);
}

void ResetFlightScopedStateForSessionBoundary(
    const char* reason,
    bool preserveDisconnectedAlert) {
    ResetPluginRuntimeState(true, true, true);
    if (preserveDisconnectedAlert) {
        xvatsim::brain::SetBrainOwnedXPilotConnectedSeen(
            &gBrainOwnedRuntimeState,
            true);
    }

    if (reason == nullptr || std::strlen(reason) == 0) {
        return;
    }

    std::string line = "[XVatsim] Session boundary reset: ";
    line += reason;
    line += "\n";
    XPLMDebugString(line.c_str());
}

void PreserveFlightStateForNetworkDisconnect() {
    (void)RunBoundAsyncFactCycle(
        false,
        gBrainOwnedRuntimeState.lastWorkflowStage);
    InvalidateRouteAsyncRuntime("xpilot-disconnect");
    InvalidateAuthorityAsyncRuntime("xpilot-disconnect");
    DiscardPendingAccessoryClickFacts();
    xvatsim::brain::CloseBrainOwnedAccessoryForTemporaryXPilotDisconnect(
        &gBrainOwnedRuntimeState);
    gVatsimDataFeedClient.Reset();
    gVnasDataClient.Reset();
    gNetworkPlanLink.Reset();
    gTransceiverResolver.Reset();
    gTerminalEvidenceRadioCache = {};
    ResetBrainDisplayPublisherCache();
    ResetStandbyAssistLatch();

    const auto& flightContext = gBrainOwnedRuntimeState.flightContext;
    std::string line = "[XVatsim] xPilot disconnected; ";
    if (flightContext.active) {
        line += "current flight context preserved for reconnect recovery";
        if (!flightContext.departureIcao.empty() ||
            !flightContext.destinationIcao.empty()) {
            line += " (";
            line += flightContext.departureIcao.empty()
                        ? "----"
                        : flightContext.departureIcao;
            line += " -> ";
            line += flightContext.destinationIcao.empty()
                        ? "----"
                        : flightContext.destinationIcao;
            line += ")";
        }
    } else {
        line += "no active flight context to preserve";
    }
    line += ".\n";
    XPLMDebugString(line.c_str());
}

SessionBoundaryResult HandleXPilotSessionBoundary(
    const xvatsim::brain::XPilotSessionSnapshot& xPilotSessionSnapshot,
    const xvatsim::brain::PilotIdentitySnapshot& pilotIdentitySnapshot) {
    xvatsim::brain::workflow::XPilotSessionBoundaryInput input;
    input.xPilotSession = xPilotSessionSnapshot;
    input.pilotIdentity = pilotIdentitySnapshot;
    input.state = gBrainOwnedRuntimeState.xPilotSessionBoundaryState;

    const auto decision =
        xvatsim::brain::workflow::ResolveXPilotSessionBoundary(input);

    if (decision.shouldPreserveFlightStateForDisconnect) {
        PreserveFlightStateForNetworkDisconnect();
    }
    if (decision.shouldResetFlightScopedState) {
        const auto previousCallsign =
            input.state.lastConnectedPilotCallsign.empty()
                ? input.state.disconnectedPilotCallsign
                : input.state.lastConnectedPilotCallsign;
        const auto nextCallsign = NormalizeCallsign(
            pilotIdentitySnapshot.normalizedCallsign.empty()
                ? xPilotSessionSnapshot.callsign
                : pilotIdentitySnapshot.normalizedCallsign);
        (void)ApplyBoundAsyncWorkerLifecycleBoundary(true);
        DiscardPendingAccessoryClickFacts();
        xvatsim::brain::ResetBrainOwnedAccessoryForCallsignChange(
            &gBrainOwnedRuntimeState,
            previousCallsign,
            nextCallsign);
        ResetFlightScopedStateForSessionBoundary(
            decision.resetReason.c_str(),
            false);
    }
    if (!decision.logLine.empty()) {
        XPLMDebugString(decision.logLine.c_str());
    }

    xvatsim::brain::ApplyBrainOwnedXPilotSessionBoundaryDecision(
        &gBrainOwnedRuntimeState,
        decision);
    return decision.action;
}

void BeginManualCtafEntry() {
    DiscardPendingTextEntryState();
    xvatsim::brain::ClearBrainOwnedManualQuery(&gBrainOwnedRuntimeState);
    xvatsim::brain::SetBrainOwnedPendingTextEntryMode(
        &gBrainOwnedRuntimeState,
        xvatsim::brain::BrainOwnedTextEntryMode::ManualCtaf);
    ShowTransientStatusLine("CTAF enter ICAO and press Enter");
    gOverlayWindow.BeginTextEntry(".ctaf ");
    RefreshOverlayFromBrain();
}

void BeginMetarAirportLookupEntry() {
    DiscardPendingTextEntryState();
    xvatsim::brain::ClearBrainOwnedManualQuery(&gBrainOwnedRuntimeState);
    xvatsim::brain::SetBrainOwnedPendingTextEntryMode(
        &gBrainOwnedRuntimeState,
        xvatsim::brain::BrainOwnedTextEntryMode::MetarAirportLookup);
    ShowTransientStatusLine("METAR enter four-character ICAO and press Enter");
    gOverlayWindow.BeginTextEntry("");
    RefreshOverlayFromBrain();
}

void BeginAtisAirportLookupEntry() {
    DiscardPendingTextEntryState();
    xvatsim::brain::ClearBrainOwnedManualQuery(&gBrainOwnedRuntimeState);
    xvatsim::brain::SetBrainOwnedPendingTextEntryMode(
        &gBrainOwnedRuntimeState,
        xvatsim::brain::BrainOwnedTextEntryMode::AtisAirportLookup);
    ShowTransientStatusLine("ATIS enter four-character ICAO and press Enter");
    gOverlayWindow.BeginTextEntry("");
    RefreshOverlayFromBrain();
}

xvatsim::modules::settings_store::StoredDisplayMode ToStoredDisplayMode(
    xvatsim::brain::BrainOwnedDisplayOverrideMode mode) {
    using xvatsim::modules::settings_store::StoredDisplayMode;
    using xvatsim::brain::BrainOwnedDisplayOverrideMode;
    switch (mode) {
        case BrainOwnedDisplayOverrideMode::ForcedOpen:
            return StoredDisplayMode::Open;
        case BrainOwnedDisplayOverrideMode::ForcedSleep:
            return StoredDisplayMode::Sleep;
        case BrainOwnedDisplayOverrideMode::Auto:
        default:
            return StoredDisplayMode::Auto;
    }
}

xvatsim::brain::BrainOwnedDisplayOverrideMode ToDisplayOverrideMode(
    xvatsim::modules::settings_store::StoredDisplayMode mode) {
    using xvatsim::modules::settings_store::StoredDisplayMode;
    using xvatsim::brain::BrainOwnedDisplayOverrideMode;
    switch (mode) {
        case StoredDisplayMode::Open:
            return BrainOwnedDisplayOverrideMode::ForcedOpen;
        case StoredDisplayMode::Sleep:
            return BrainOwnedDisplayOverrideMode::ForcedSleep;
        case StoredDisplayMode::Auto:
        default:
            return BrainOwnedDisplayOverrideMode::Auto;
    }
}

xvatsim::modules::settings_store::StoredOperatingMode ToStoredOperatingMode(
    xvatsim::brain::BrainOwnedOperatingMode mode) {
    using xvatsim::brain::BrainOwnedOperatingMode;
    using xvatsim::modules::settings_store::StoredOperatingMode;
    return mode == BrainOwnedOperatingMode::VFR
               ? StoredOperatingMode::VFR
               : StoredOperatingMode::IFR;
}

xvatsim::brain::BrainOwnedOperatingMode ToBrainOperatingMode(
    xvatsim::modules::settings_store::StoredOperatingMode mode) {
    using xvatsim::brain::BrainOwnedOperatingMode;
    using xvatsim::modules::settings_store::StoredOperatingMode;
    return mode == StoredOperatingMode::VFR
               ? BrainOwnedOperatingMode::VFR
               : BrainOwnedOperatingMode::IFR;
}

xvatsim::brain::BrainOwnedOperatingModeLoadStatus
ToBrainOperatingModeLoadStatus(
    xvatsim::modules::settings_store::StoredOperatingModeLoadStatus status) {
    using xvatsim::brain::BrainOwnedOperatingModeLoadStatus;
    using xvatsim::modules::settings_store::StoredOperatingModeLoadStatus;
    switch (status) {
        case StoredOperatingModeLoadStatus::Valid:
            return BrainOwnedOperatingModeLoadStatus::Valid;
        case StoredOperatingModeLoadStatus::Invalid:
            return BrainOwnedOperatingModeLoadStatus::Invalid;
        case StoredOperatingModeLoadStatus::Unavailable:
            return BrainOwnedOperatingModeLoadStatus::Unavailable;
        case StoredOperatingModeLoadStatus::Missing:
        default:
            return BrainOwnedOperatingModeLoadStatus::Missing;
    }
}

void RequestAccessoryFlightLoopWake(
    std::uint64_t notificationSequence,
    void* refcon) {
    (void)refcon;
    ++gAccessoryInputAccounting.wakeRequests;
    gAccessoryInputAccounting.wakeNotificationSequence =
        notificationSequence;
    if (!gPluginRuntimeEnabled || !gFlightLoopRegistered) return;
    XPLMSetFlightLoopCallbackInterval(
        FlightLoopCallback,
        kNextFlightLoopInterval,
        1,
        nullptr);
}

bool RequestImmediateGatedFlightLoopCallback() {
    if (!gPluginRuntimeEnabled || !gFlightLoopRegistered) {
        return false;
    }
    xvatsim::brain::RecordBrainOwnedOperationalEnableWakeRequest(
        &gOperationalActivationState);
    XPLMSetFlightLoopCallbackInterval(
        FlightLoopCallback,
        kNextFlightLoopInterval,
        1,
        nullptr);
    return true;
}

bool ServicePendingAccessoryInput() {
    if (!gPluginRuntimeEnabled) return false;
    bool performedWork = false;
    bool consumedClick = false;
    ++gAccessoryInputAccounting.brainCycles;
    DrainAccessoryPublicationFacts();

    xvatsim::modules::overlay::OverlayAccessoryClickFact fact;
    std::size_t cycleBudget = kAccessoryInputFactsPerCycle;
    while (cycleBudget != 0 &&
           gOverlayWindow.CanAcceptAccessoryPresentationCommand() &&
           gOverlayWindow.BeginAccessoryInputDispatch(&fact)) {
        --cycleBudget;
        consumedClick = true;
        performedWork = true;
        ++gAccessoryInputAccounting.factsConsumed;

        xvatsim::brain::BrainOwnedAccessorySelectionRequest request;
        request.drawer = fact.drawer;
        request.requestSequence = fact.requestSequence;
        request.clickAcceptedMicroseconds = fact.startedMicroseconds;
        request.mouseCallbackEnteredMicroseconds =
            fact.mouseCallbackEnteredMicroseconds;
        request.mouseCallbackExitedMicroseconds =
            fact.mouseCallbackExitedMicroseconds;
        const auto brainDecisionStarted =
            xvatsim::modules::overlay::OverlayWindow::
                AccessoryWallClockMicroseconds();
        if (fact.mouseCallbackExitedMicroseconds == 0 ||
            brainDecisionStarted < fact.mouseCallbackExitedMicroseconds) {
            ++gAccessoryInputAccounting.callbackOrderFailures;
        }
        const auto decision =
            xvatsim::brain::RequestBrainOwnedAccessoryDrawerSelection(
                &gBrainOwnedRuntimeState, request);
        const auto brainDecisionCompleted =
            xvatsim::modules::overlay::OverlayWindow::
                AccessoryWallClockMicroseconds();
        ++gAccessoryInputAccounting.brainDecisions;
        fact.dispatchStageWallMicroseconds[static_cast<std::size_t>(
            xvatsim::modules::overlay::AccessoryDispatchStage::BrainDecision)] =
                brainDecisionCompleted - brainDecisionStarted;
        xvatsim::modules::overlay::AccessoryDispatchStageWallTimings stages;
        stages.elapsedMicroseconds = fact.dispatchStageWallMicroseconds;
        const auto presentation = SynchronizeAccessoryPresentation(&stages);
        if (presentation.snapshot != nullptr &&
            presentation.snapshot->commandIdentity != 0) {
            ++gAccessoryInputAccounting.commandsIssued;
        }
        const auto synchronousCompleted =
            xvatsim::modules::overlay::OverlayWindow::
                AccessoryWallClockMicroseconds();
        const auto synchronousUs = synchronousCompleted - brainDecisionStarted;
        gAccessoryInputAccounting.maximumSynchronousMicroseconds = std::max(
            gAccessoryInputAccounting.maximumSynchronousMicroseconds,
            synchronousUs);
        if (synchronousUs > kAccessorySynchronousBudgetMicroseconds) {
            ++gAccessoryInputAccounting.synchronousBudgetFailures;
        }

        const auto brainCycle = gAccessoryInputAccounting.brainCycles;
        const auto presentationCommandIdentity =
            presentation.snapshot == nullptr
                ? 0
                : presentation.snapshot->commandIdentity;
        AppendDeferredDiagnosticsLogLine(
            [fact,
             decision,
             brainCycle,
             brainDecisionStarted,
             brainDecisionCompleted,
             presentationCommandIdentity,
             synchronousUs]() {
                std::ostringstream line;
                line << "event=accessory-input-decision"
                     << " clickSequence=" << fact.requestSequence
                     << " notificationSequence=" << fact.notificationSequence
                     << " mouseCallbackEntered="
                     << fact.mouseCallbackEnteredMicroseconds
                     << " mouseCallbackExited="
                     << fact.mouseCallbackExitedMicroseconds
                     << " brainCycle=" << brainCycle
                     << " brainDecisionStarted=" << brainDecisionStarted
                     << " brainDecisionCompleted=" << brainDecisionCompleted
                     << " capturedDrawer=" << static_cast<int>(fact.drawer)
                     << " decisionAction=" << static_cast<int>(decision.action)
                     << " previousDrawer="
                     << static_cast<int>(decision.previousDrawer)
                     << " resultingDrawer="
                     << static_cast<int>(decision.activeDrawer)
                     << " selectionGeneration="
                     << decision.selectionGeneration
                     << " presentationCommandIdentity="
                     << presentationCommandIdentity
                     << " synchronousUs=" << synchronousUs;
                return line.str();
            });
        DrainAccessoryPublicationFacts();
    }

    // A previously issued command may be waiting only for its mechanical
    // preparation result or first-frame terminal fact. Revisit that exact
    // brain command on the active next-cycle cadence without consuming or
    // inventing another input fact.
    if (!consumedClick && gOverlayWindow.HasPendingAccessoryWork()) {
        gOverlayWindow.ServicePendingAccessoryPresentation();
        DrainAccessoryPublicationFacts();
        performedWork = true;
    }
    gAccessoryInputAccounting.lastObservedNotificationSequence =
        gOverlayWindow.GetAccessoryInputNotificationSequence();
    return performedWork;
}

xvatsim::brain::BrainOwnedAccessoryPresentationHandle
SynchronizeAccessoryPresentation(
    xvatsim::modules::overlay::AccessoryDispatchStageWallTimings*
        acceptedActionStages) {
    DrainAccessoryPublicationFacts();
    if (!gOverlayWindow.CanAcceptAccessoryPresentationCommand()) {
        return {};
    }
    const auto semanticGeneration =
        xvatsim::brain::BrainOwnedAccessorySemanticPresentationGeneration(
            gBrainOwnedRuntimeState);
    if (semanticGeneration ==
            gLastAccessorySemanticPresentationGeneration) {
        gOverlayWindow.ServicePendingAccessoryPresentation();
        DrainAccessoryPublicationFacts();
        return {};
    }
    const auto projectionStarted = acceptedActionStages != nullptr
        ? xvatsim::modules::overlay::OverlayWindow::
            AccessoryWallClockMicroseconds()
        : 0;
    const auto presentation =
        xvatsim::brain::ProjectBrainOwnedAccessoryPresentation(
            &gBrainOwnedRuntimeState,
            &gAccessoryProjectionCounters);
    gLastAccessorySemanticPresentationGeneration = semanticGeneration;
    if (acceptedActionStages != nullptr) {
        acceptedActionStages->elapsedMicroseconds[static_cast<std::size_t>(
            xvatsim::modules::overlay::AccessoryDispatchStage::
                BrainProjectionHistoryCopy)] =
                    xvatsim::modules::overlay::OverlayWindow::
                        AccessoryWallClockMicroseconds() - projectionStarted;
    }
    gOverlayWindow.UpdateAccessory(presentation, acceptedActionStages);
    DrainAccessoryPublicationFacts();
    return presentation;
}

void DrainAccessoryPublicationFacts() {
    xvatsim::brain::BrainOwnedAccessoryPublicationFact fact;
    while (gOverlayWindow.ConsumeAccessoryPublicationFact(&fact)) {
        const auto decision =
            xvatsim::brain::ConsumeBrainOwnedAccessoryPublicationFact(
            &gBrainOwnedRuntimeState, fact);
        if (decision.consumed) {
            ++gAccessoryInputAccounting.terminalFacts;
        }
        if (decision.consumed && fact.originatingClickSequence != 0) {
            ++gAccessoryInputAccounting.clickTerminalFacts;
            gAccessoryInputAccounting.maximumClickToTerminalMicroseconds =
                std::max(
                    gAccessoryInputAccounting.
                        maximumClickToTerminalMicroseconds,
                    fact.clickToTerminalMicroseconds);
            if (fact.clickToTerminalMicroseconds >=
                kAccessoryTerminalBudgetMicroseconds) {
                ++gAccessoryInputAccounting.livenessFailures;
            }
        }
        xvatsim::modules::overlay::
            RecordAccessoryPublicationDiagnosticAccounting(
                decision, &gAccessoryInputAccounting.publication);
        ++gAccessoryInputAccounting.publication.diagnosticsSerialized;
        AppendDeferredDiagnosticsLogLine(
            [fact, decision]() {
                return xvatsim::modules::overlay::
                    SerializeAccessoryPublicationDiagnostic(
                        fact, decision, nullptr);
            });
    }
}

void UpdateOverlayWindow(
    const xvatsim::brain::OverlayViewModel& overlayModel) {
    gOverlayWindow.Update(overlayModel);
    SynchronizeAccessoryPresentation();
}

void LogAccessoryPerformanceSnapshot(const char* boundary) {
    xvatsim::modules::overlay::AccessoryPerformanceSnapshot snapshot;
    if (!gOverlayWindow.ConsumeAccessoryPerformancePublication(&snapshot)) {
        return;
    }
    const auto preparation = gOverlayWindow.GetAccessoryPreparationCounters();
    const auto integration = gOverlayWindow.GetAccessoryIntegrationCounters();
    const auto accounting = gAccessoryInputAccounting;
    const auto boundaryToken =
        std::string{boundary == nullptr ? "unknown" : boundary};
    AppendDeferredDiagnosticsLogLine(
        [snapshot,
         preparation,
         integration,
         accounting,
         boundaryToken]() {
    const auto appendTiming = [](
        std::ostringstream* stream,
        const char* label,
        const xvatsim::modules::overlay::AccessoryPerformanceSummary& timing) {
        *stream << " " << label << "Count=" << timing.count
                << " " << label << "P50Us=" << timing.p50Microseconds
                << " " << label << "P95Us=" << timing.p95Microseconds
                << " " << label << "MaxUs=" << timing.maximumMicroseconds;
    };
    std::ostringstream line;
    line << "event=step3-accessory-performance"
         << " boundary=" << boundaryToken
         << " epoch=" << snapshot.epoch
         << " measurementRevision=" << snapshot.measurementRevision
         << " thresholdFailure=" << (snapshot.thresholdFailure ? "true" : "false")
         << " firstViolationCategory="
         << xvatsim::modules::overlay::AccessoryPerformanceCategoryToken(
                snapshot.firstViolationCategory)
         << " firstViolationUs=" << snapshot.firstViolationMicroseconds
         << " warningCount=" << snapshot.warningCount
         << " publicationCount=" << snapshot.publicationCount
         << " actionsCompleted=" << snapshot.actionsCompleted
         << " synchronousWallWithinBudgetCount="
         << snapshot.synchronousWallWithinBudgetCount
         << " frameCadenceLimitedCount="
         << snapshot.frameCadenceLimitedCount
         << " preparationLimitedCount="
         << snapshot.preparationLimitedCount
         << " synchronousWallFailureCount="
         << snapshot.synchronousWallFailureCount
         << " renderWallFailureCount="
         << snapshot.renderWallFailureCount
         << " livenessFailureCount="
         << snapshot.livenessFailureCount
         << " timingUnavailableCount="
         << snapshot.timingUnavailableCount
         << " cadenceContractFailureCount="
         << snapshot.cadenceContractFailureCount
         << " missedEligibleDraws=" << snapshot.missedEligibleDraws
         << " uniqueDrawSamples=" << snapshot.uniqueDrawSamples
         << " drawSampleReferences=" << snapshot.drawSampleReferences
         << " coalescedActionCount=" << snapshot.coalescedActionCount
         << " maximumActionsPerDraw=" << snapshot.maximumActionsPerDraw
         << " retainedViolationRecordCount="
         << snapshot.firstViolationRecordCount
         << " droppedViolationRecordCount="
         << snapshot.droppedViolationRecordCount
         << " preparationJobsRequested=" << preparation.jobsRequested
         << " preparationJobsReplaced=" << preparation.jobsReplaced
         << " preparationJobsStarted=" << preparation.jobsStarted
         << " preparationJobsCompleted=" << preparation.jobsCompleted
         << " preparationJobsCancelled=" << preparation.jobsCancelled
         << " preparationStaleRejected=" << preparation.staleResultsRejected
         << " preparationMaxQueueDepth=" << preparation.maximumQueueDepth
         << " preparationMaxReadyCount=" << preparation.maximumReadyCacheCount
         << " preparationWakeCount=" << preparation.workerWakeCount
         << " preparationSleepCount=" << preparation.workerSleepCount
         << " preparationMaxSliceUs="
         << preparation.maximumContiguousExecutionMicroseconds
         << " preparationMetarTotalUs="
         << preparation.totalPreparationMicroseconds[0]
         << " preparationAtisTotalUs="
         << preparation.totalPreparationMicroseconds[1]
         << " preparationPdcTotalUs="
         << preparation.totalPreparationMicroseconds[2]
         << " preparationPublicationCount=" << preparation.publicationCount
         << " preparationReadinessChecks=" << preparation.readinessCheckCount
         << " preparationReadinessContentions="
         << preparation.readinessContentionCount
         << " preparationPublicationMaxUs="
         << preparation.maximumPublicationMicroseconds
         << " preparationEnqueueAttempts="
         << preparation.enqueueAttemptCount
         << " preparationEnqueueContentions="
         << preparation.enqueueContentionCount
         << " preparationEnqueueSuccesses="
         << preparation.enqueueSuccessCount
         << " preparationEnqueueReplacements="
         << preparation.enqueueReplacementCount
         << " preparationEnqueueMaxUs="
         << preparation.maximumEnqueueMicroseconds
         << " preparationStartupAttempts=" << preparation.startupAttemptCount
         << " preparationStartupSuccesses=" << preparation.startupSuccessCount
         << " preparationStartupFailures=" << preparation.startupFailureCount
         << " preparationFailureDiagnostics="
         << preparation.startupFailureDiagnosticCount
         << " preparationRequestsRejectedUnavailable="
         << preparation.requestsRejectedUnavailable
         << " preparationLifecycle="
         << xvatsim::modules::overlay::AccessoryPreparationWorkerStateToken(
                preparation.lifecycleState)
         << " preparationFailure="
         << xvatsim::modules::overlay::AccessoryPreparationWorkerFailureToken(
                preparation.failure)
         << " preparationPriorityRequested="
         << (preparation.priorityRequested ? "true" : "false")
         << " preparationPrioritySucceeded="
         << (preparation.prioritySucceeded ? "true" : "false")
         << " preparationWorkerThread=" << preparation.workerThreadIdentity
         << " preparationMainThread=" << preparation.mainThreadIdentity
         << " preparationProhibitedAccesses="
         << preparation.prohibitedAccessCount
         << " preparationRunningThreads=" << preparation.runningWorkerThreads;
    line << " accessoryClicksProduced=" << integration.clickFactsProduced
         << " accessoryClicksDropped=" << integration.clickFactsDropped
         << " accessoryClicksConsumed=" << integration.clickFactsConsumed
         << " accessoryClicksDiscarded=" << integration.clickFactsDiscarded
         << " accessoryClicksPending=" << integration.clickFactsPending
         << " accessoryClickMaximumQueueDepth="
         << integration.maximumClickQueueDepth
         << " accessoryNotificationSequence="
         << integration.clickNotificationSequence
         << " accessoryWakeRequests=" << integration.clickWakeRequests
         << " accessoryCallbackExitMarks="
         << integration.clickCallbackExitMarks
         << " accessoryBrainCycles="
         << accounting.brainCycles
         << " accessoryBrainFactsConsumed="
         << accounting.factsConsumed
         << " accessoryBrainDecisions="
         << accounting.brainDecisions
         << " accessoryCommandsIssued="
         << accounting.commandsIssued
         << " accessoryTerminalFacts="
         << accounting.terminalFacts
         << " accessoryPublicationFactsDequeued="
         << accounting.publication.factsDequeued
         << " accessoryPublicationFactsAccepted="
         << accounting.publication.factsAcceptedByBrain
         << " accessoryPublicationFactsRejected="
         << accounting.publication.factsRejectedByBrain
         << " accessoryCommandTerminalsAccepted="
         << accounting.publication.commandTerminalsAccepted
         << " accessoryVisibleAttemptTerminalsAccepted="
         << accounting.publication.visibleAttemptTerminalsAccepted
         << " accessoryCombinedTerminalsAccepted="
         << accounting.publication.combinedTerminalsAccepted
         << " accessoryStalePublicationFactsRejected="
         << accounting.publication.staleFactsRejected
         << " accessoryClickTerminalFacts="
         << accounting.clickTerminalFacts
         << " accessoryLifecycleDiscards="
         << accounting.lifecycleDiscards
         << " accessoryCallbackOrderFailures="
         << accounting.callbackOrderFailures
         << " accessorySynchronousBudgetFailures="
         << accounting.synchronousBudgetFailures
         << " accessoryLivenessFailures="
         << accounting.livenessFailures
         << " accessoryMaximumSynchronousUs="
         << accounting.maximumSynchronousMicroseconds
         << " accessoryMaximumClickToTerminalUs="
         << accounting.maximumClickToTerminalMicroseconds
         << " accessoryActiveCadenceReturns="
         << accounting.activeCadenceReturns
         << " accessoryNormalCadenceReturns="
         << accounting.normalCadenceReturns
         << " accessoryAccountingExact="
         << (integration.clickFactsProduced ==
                    integration.clickFactsConsumed +
                    integration.clickFactsPending +
                    integration.clickFactsDiscarded +
                    integration.clickFactsDropped
                ? "true" : "false")
         << " accessoryBehavioralInFlight=false";
    for (std::size_t index = 0;
         index < static_cast<std::size_t>(
             xvatsim::modules::overlay::AccessoryPerformanceCategory::Count);
         ++index) {
        const auto category = static_cast<
            xvatsim::modules::overlay::AccessoryPerformanceCategory>(index);
        appendTiming(
            &line,
            xvatsim::modules::overlay::AccessoryPerformanceCategoryToken(category),
            snapshot.categories[index]);
    }
    const char* actionStagePrefixes[]{"open", "switch", "close"};
    for (std::size_t actionIndex = 0;
         actionIndex < snapshot.actionStageWall.size(); ++actionIndex) {
        for (std::size_t stageIndex = 0;
             stageIndex < snapshot.actionStageWall[actionIndex].size();
             ++stageIndex) {
            const auto stage = static_cast<
                xvatsim::modules::overlay::AccessoryDispatchStage>(stageIndex);
            std::string label = std::string{actionStagePrefixes[actionIndex]} +
                "-" +
                xvatsim::modules::overlay::AccessoryDispatchStageToken(stage);
            appendTiming(
                &line,
                label.c_str(),
                snapshot.actionStageWall[actionIndex][stageIndex]);
        }
    }
    for (std::size_t index = 0;
         index < static_cast<std::size_t>(
             xvatsim::modules::overlay::AccessoryRasterReason::Count);
         ++index) {
        const auto reason = static_cast<
            xvatsim::modules::overlay::AccessoryRasterReason>(index);
        const auto* token =
            xvatsim::modules::overlay::AccessoryRasterReasonToken(reason);
        line << " railRasterReason-" << token << "="
             << snapshot.railRasterReasons[index]
             << " drawerRasterReason-" << token << "="
             << snapshot.drawerRasterReasons[index];
    }
    const auto& action = snapshot.lastAction;
    line << " lastActionAvailable=" << (action.available ? "true" : "false")
         << " lastActionCategory="
         << xvatsim::modules::overlay::AccessoryPerformanceCategoryToken(
                action.actionCategory)
         << " lastActionClassification="
         << xvatsim::modules::overlay::AccessoryActionTimingClassificationToken(
                action.classification)
         << " lastActionRequestSequence=" << action.requestSequence
         << " mouseFactAcceptedUs="
         << action.mouseFactAcceptedMicroseconds
         << " dispatchStartedUs=" << action.dispatchStartedMicroseconds
         << " dispatchCompletedUs=" << action.dispatchCompletedMicroseconds
         << " precedingDrawEnteredUs="
         << action.precedingDrawEnteredMicroseconds
         << " matchingDrawEnteredUs="
         << action.matchingDrawEnteredMicroseconds
         << " matchingDrawCompletedUs="
         << action.matchingDrawCompletedMicroseconds
         << " dispatchWallUs=" << action.dispatchWallMicroseconds
         << " queuedBeforeDispatchUs="
         << action.queuedBeforeDispatchMicroseconds
         << " callbackWaitUs=" << action.callbackWaitMicroseconds
         << " actionDrawWallUs=" << action.actionDrawWallMicroseconds
         << " matchingDrawCallbackElapsedUs="
         << action.matchingDrawCallbackElapsedMicroseconds
         << " combinedActionWallUs="
         << action.combinedActionWallMicroseconds
         << " preparationWaitUs=" << action.preparationWaitMicroseconds
         << " preparationRequestedUs="
         << action.preparationRequestedMicroseconds
         << " workerStartedUs=" << action.workerStartedMicroseconds
         << " workerCompletedUs=" << action.workerCompletedMicroseconds
         << " workerPublishedUs=" << action.workerPublishedMicroseconds
         << " readyCollectedUs=" << action.readyCollectedMicroseconds
         << " bindingStartedUs=" << action.bindingStartedMicroseconds
         << " workerQueueWaitUs=" << action.workerQueueWaitMicroseconds
         << " workerPreparationUs=" << action.workerPreparationMicroseconds
         << " workerPublicationHandoffUs="
         << action.workerPublicationHandoffMicroseconds
         << " publicationToReadyCollectionUs="
         << action.publicationToReadyCollectionMicroseconds
         << " readyToBindWaitUs=" << action.readyToBindWaitMicroseconds
         << " preparationUnattributedUs="
         << action.preparationUnattributedMicroseconds
         << " totalUnattributedUs=" << action.totalUnattributedMicroseconds
         << " totalOverlapUs=" << action.totalOverlapMicroseconds
         << " drawSampleId=" << action.drawSampleId
         << " sharedDrawSample="
         << (action.sharedDrawSample ? "true" : "false")
         << " drawSampleFanOut=" << action.drawSampleFanOut
         << " renderWallFailure="
         << (action.renderWallFailure ? "true" : "false")
         << " renderWallFailureCategory="
         << xvatsim::modules::overlay::AccessoryPerformanceCategoryToken(
                action.renderWallFailureCategory)
         << " renderWallFailureUs="
         << action.renderWallFailureMicroseconds
         << " endToEndUs=" << action.endToEndMicroseconds
         << " containingFrameIntervalUs="
         << action.containingFrameIntervalMicroseconds
         << " expectedDrawOrdinal=" << action.expectedDrawOrdinal
         << " matchingDrawOrdinal=" << action.matchingDrawOrdinal
         << " lastActionMissedEligibleDraws=" << action.missedEligibleDraws
         << " accountingExact="
         << (action.accountingExact ? "true" : "false");
    for (std::size_t stageIndex = 0;
         stageIndex < action.stages.elapsedMicroseconds.size(); ++stageIndex) {
        const auto stage = static_cast<
            xvatsim::modules::overlay::AccessoryDispatchStage>(stageIndex);
        line << " lastAction-"
             << xvatsim::modules::overlay::AccessoryDispatchStageToken(stage)
             << "Us=" << action.stages.elapsedMicroseconds[stageIndex];
    }
    line << " retainedViolationRecords=" << snapshot.firstViolationRecordCount
         << " serializedViolationRecords=" << snapshot.firstViolationRecordCount
         << " droppedViolationRecords=" << snapshot.droppedViolationRecordCount;
    for (std::size_t index = 0;
         index < snapshot.firstViolationRecordCount &&
         index < snapshot.firstViolationRecords.size(); ++index) {
        const auto& violation = snapshot.firstViolationRecords[index];
        line << " violation" << index << "={classification:"
             << xvatsim::modules::overlay::AccessoryActionTimingClassificationToken(
                    violation.classification)
             << ",category:"
             << xvatsim::modules::overlay::AccessoryPerformanceCategoryToken(
                    violation.actionCategory)
             << ",request:" << violation.requestSequence
             << ",dispatchWallUs:" << violation.dispatchWallMicroseconds
             << ",drawWallUs:" << violation.actionDrawWallMicroseconds
             << ",combinedWallUs:" << violation.combinedActionWallMicroseconds
             << ",preparationWaitUs:" << violation.preparationWaitMicroseconds
             << ",drawSampleId:" << violation.drawSampleId
             << ",fanOut:" << violation.drawSampleFanOut << "}";
    }
    return line.str();
        },
        xvatsim::modules::runtime_workers::DiagnosticsImportance::Critical);
}

std::string ResolveSettingsPath() {
    char systemPath[1024] = {};
    XPLMGetSystemPath(systemPath);
    return (std::filesystem::path(systemPath) / "Output" / "preferences" / "XVatsim.prf").string();
}

std::string ResolvePluginAssetPath(const std::string& fileName) {
    char pluginPath[1024] = {};
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, pluginPath, nullptr, nullptr);
    return (std::filesystem::path(pluginPath).parent_path() / fileName).string();
}

std::string ResolvePreflightRouteCachePath() {
    return ResolvePluginAssetPath(
        xvatsim::core::preflight::kPreflightRouteCacheFileName);
}

void LoadPreflightRouteCacheCandidate() {
    gPreflightRouteCacheCandidate.reset();
    gSelectedPreflightRouteCache.reset();
    gSelectedPreflightPlanKey.clear();
    gSelectedPreflightValidationReason.clear();
    xvatsim::brain::ClearBrainOwnedPreflightRouteCacheApplication(
        &gBrainOwnedRuntimeState);

    gPreflightRouteCachePath = ResolvePreflightRouteCachePath();
    xvatsim::core::preflight::PreflightRouteCache cache;
    std::string error;
    if (!xvatsim::core::preflight::LoadPreflightRouteCacheFile(
            gPreflightRouteCachePath,
            &cache,
            &error)) {
        std::string line =
            "[XVatsim] Preflight route cache not active: " + error + "\n";
        XPLMDebugString(line.c_str());
        return;
    }

    gPreflightRouteCacheCandidate =
        std::make_shared<const xvatsim::core::preflight::PreflightRouteCache>(
            std::move(cache));
    std::ostringstream stream;
    stream << "[XVatsim] Preflight route cache loaded: "
           << gPreflightRouteCacheCandidate->plan.departureIcao
           << "->"
           << gPreflightRouteCacheCandidate->plan.destinationIcao
           << " waypoints="
           << gPreflightRouteCacheCandidate->plan.waypoints.size()
           << " routeHash="
           << gPreflightRouteCacheCandidate->plan.routeIdentityHash
           << "\n";
    XPLMDebugString(stream.str().c_str());
}

void ApplyPreflightRouteCacheForPlanIfNeeded(
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    xvatsim::brain::BrainOwnedPreflightRouteCacheInput input;
    input.planKey = xvatsim::brain::BuildBrainOwnedNetworkPlanIdentityKey(networkPlanSnapshot);
    input.hasCandidate = gPreflightRouteCacheCandidate != nullptr;
    const auto decision =
        xvatsim::brain::BeginBrainOwnedPreflightRouteCacheApplication(
            &gBrainOwnedRuntimeState,
            input);

    if (decision.shouldClearRouteResolverCache) {
        gSelectedPreflightRouteCache.reset();
        gSelectedPreflightPlanKey = input.planKey;
        gSelectedPreflightValidationReason = "no-candidate";
    }
    if (!decision.logLine.empty()) {
        XPLMDebugString(decision.logLine.c_str());
    }
    if (!decision.shouldValidateCandidate) {
        return;
    }

    gSelectedPreflightRouteCache = gPreflightRouteCacheCandidate;
    gSelectedPreflightPlanKey = input.planKey;
    gSelectedPreflightValidationReason = "worker-validation-pending";

    // Gate B performs source-file verification and application exclusively in
    // the worker-owned engine. This simulator-thread step only selects the
    // immutable candidate for the semantic request identity.
    std::ostringstream stream;
    stream << "[XVatsim] Preflight route cache queued for worker validation for "
           << networkPlanSnapshot.departureIcao
           << "->"
           << networkPlanSnapshot.destinationIcao
           << " routeHash="
           << gPreflightRouteCacheCandidate->plan.routeIdentityHash
           << ". Authority evidence remains live-only.\n";
    XPLMDebugString(stream.str().c_str());
}

void SavePluginSettings() {
    gPluginSettings.displayMode =
        ToStoredDisplayMode(gBrainOwnedRuntimeState.displayOverrideMode);
    if (!gSettingsStore.Save(gPluginSettings)) {
        XPLMDebugString("[XVatsim] Settings save failed.\n");
    }
}

void SyncOperatingModeMenuChecks() {
    if (gPluginMenu == nullptr || gIfrModeMenuItemIndex < 0 ||
        gVfrModeMenuItemIndex < 0) {
        return;
    }
    const auto vfrActive =
        gBrainOwnedRuntimeState.operatingMode.mode ==
        xvatsim::brain::BrainOwnedOperatingMode::VFR;
    XPLMCheckMenuItem(
        gPluginMenu,
        gIfrModeMenuItemIndex,
        vfrActive ? xplm_Menu_Unchecked : xplm_Menu_Checked);
    XPLMCheckMenuItem(
        gPluginMenu,
        gVfrModeMenuItemIndex,
        vfrActive ? xplm_Menu_Checked : xplm_Menu_Unchecked);
}

void RequestOperatingModeSelection(
    xvatsim::brain::BrainOwnedOperatingMode requestedMode) {
    const auto result =
        xvatsim::brain::RequestBrainOwnedOperatingModeSelection(
            &gBrainOwnedRuntimeState,
            requestedMode);

    const char* persistence = "not-requested";
    if (result.persistenceRequested) {
        auto candidateSettings = gPluginSettings;
        candidateSettings.operatingMode =
            ToStoredOperatingMode(result.effectiveMode);
        candidateSettings.operatingModeLoadStatus =
            xvatsim::modules::settings_store::
                StoredOperatingModeLoadStatus::Valid;
        if (gSettingsStore.Save(candidateSettings)) {
            gPluginSettings = candidateSettings;
            persistence = "success";
        } else {
            persistence = "failed";
        }
        SyncOperatingModeMenuChecks();
    }

    std::ostringstream stream;
    stream << "event=operating-mode-selection"
           << " requested=" << xvatsim::brain::ToString(result.requestedMode)
           << " previous=" << xvatsim::brain::ToString(result.previousMode)
           << " effective=" << xvatsim::brain::ToString(result.effectiveMode)
           << " changed=" << (result.changed ? "true" : "false")
           << " stateSource=" << xvatsim::brain::ToString(result.stateSource)
           << " stateReason=" << result.stateReason
           << " requestSource=" << xvatsim::brain::ToString(result.requestSource)
           << " requestReason=" << result.requestReason
           << " generation=" << result.generation
           << " persistence=" << persistence;
    AppendDiagnosticsLogLine(stream.str());
}

std::string FormatUpdateDiagnosticLine(
    const xvatsim::modules::update_checker::UpdateCheckResult& result,
    bool noticeRequested,
    bool noticeVisible,
    bool dismissed) {
    std::ostringstream stream;
    stream << "updateCheck source="
           << xvatsim::modules::update_checker::ToString(result.source)
           << " installed="
           << SanitizeLogToken(result.installedVersion, 24)
           << " latest="
           << SanitizeLogToken(result.latestVersion, 24)
           << " status="
           << xvatsim::modules::update_checker::ToString(result.status)
           << " critical=" << (result.critical ? 1 : 0)
           << " noticeRequested=" << (noticeRequested ? 1 : 0)
           << " noticeVisible=" << (noticeVisible ? 1 : 0)
           << " dismissed=" << (dismissed ? 1 : 0)
           << " manifest="
           << SanitizeLogToken(result.manifestUrl, 140)
           << " error="
           << SanitizeLogToken(result.errorClass, 80);
    return stream.str();
}

xvatsim::modules::update_checker::UpdateCheckRequest BuildUpdateCheckRequest(
    xvatsim::modules::update_checker::UpdateCheckSource source) {
    xvatsim::modules::update_checker::UpdateCheckRequest request;
    request.installedVersion = kInstalledPluginVersion;
    request.manifestUrl = kUpdateManifestUrl;
    request.source = source;
    return request;
}

xvatsim::brain::OverlayUpdateStatus ToOverlayUpdateStatus(
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

xvatsim::modules::update_checker::UpdateCheckResult MakeUpdateInProgressResult(
    xvatsim::modules::update_checker::UpdateCheckSource source) {
    using xvatsim::modules::update_checker::UpdateCheckResult;
    using xvatsim::modules::update_checker::UpdateStatus;

    UpdateCheckResult result;
    result.status = UpdateStatus::InProgress;
    result.source = source;
    result.installedVersion = kInstalledPluginVersion;
    result.manifestUrl = kUpdateManifestUrl;
    result.errorClass = "none";
    return result;
}

bool AutomaticUpdateNoticeRequested() {
    using xvatsim::modules::update_checker::UpdateStatus;
    if (!gUpdateSessionResult.has_value() ||
        gUpdateSessionResult->status != UpdateStatus::Available) {
        return false;
    }
    return gDismissedUpdateNoticeVersion != gUpdateSessionResult->latestVersion;
}

xvatsim::brain::OverlayUpdateSnapshot BuildOverlayUpdateSnapshot() {
    xvatsim::brain::OverlayUpdateSnapshot snapshot;
    snapshot.installedVersion = kInstalledPluginVersion;
    snapshot.manualNoticeRequested = gManualUpdateNoticeOpen;
    snapshot.automaticNoticeRequested = AutomaticUpdateNoticeRequested();

    if (!gUpdateSessionResult.has_value()) {
        return snapshot;
    }

    snapshot.status = ToOverlayUpdateStatus(gUpdateSessionResult->status);
    snapshot.latestVersion = gUpdateSessionResult->latestVersion;
    snapshot.downloadPageUrl = gUpdateSessionResult->downloadPageUrl;
    snapshot.errorClass = gUpdateSessionResult->errorClass;
    snapshot.critical = gUpdateSessionResult->critical;
    return snapshot;
}

void HandleUpdateNoticeDismissRequest() {
    if (!gOverlayWindow.ConsumeSystemNoticeDismissRequest()) {
        return;
    }

    using xvatsim::modules::update_checker::UpdateStatus;
    if (gUpdateSessionResult.has_value() &&
        gUpdateSessionResult->status == UpdateStatus::Available) {
        gDismissedUpdateNoticeVersion = gUpdateSessionResult->latestVersion;
    }
    gManualUpdateNoticeOpen = false;

    if (gUpdateSessionResult.has_value()) {
        AppendDiagnosticsLogLine(
            FormatUpdateDiagnosticLine(
                *gUpdateSessionResult,
                false,
                false,
                true));
    }
}

void RequestUpdateCheck(
    xvatsim::modules::update_checker::UpdateCheckSource source,
    bool bypassCadence) {
    (void)bypassCadence;

    using xvatsim::modules::update_checker::UpdateCheckResult;
    using xvatsim::modules::update_checker::UpdateCheckSource;
    using xvatsim::modules::update_checker::UpdateStatus;

    const auto nowUnixSeconds = CurrentUnixSeconds();
    if (source == UpdateCheckSource::Automatic) {
        if (gUpdateSessionResult.has_value() || gUpdateChecker.InProgress()) {
            return;
        }
    }

    if (gUpdateChecker.InProgress()) {
        if (source == UpdateCheckSource::Manual) {
            gUpdateSessionResult = MakeUpdateInProgressResult(source);
            gManualUpdateNoticeOpen = true;
            AppendDiagnosticsLogLine(
                FormatUpdateDiagnosticLine(
                    *gUpdateSessionResult,
                    true,
                    true,
                    false));
        }
        return;
    }

    if (!gUpdateChecker.StartCheck(BuildUpdateCheckRequest(source))) {
        if (source == UpdateCheckSource::Manual) {
            UpdateCheckResult failedResult;
            failedResult.status = UpdateStatus::CheckFailed;
            failedResult.source = source;
            failedResult.installedVersion = kInstalledPluginVersion;
            failedResult.manifestUrl = kUpdateManifestUrl;
            failedResult.errorClass = "start-failed";
            gUpdateSessionResult = failedResult;
            gManualUpdateNoticeOpen = true;
            AppendDiagnosticsLogLine(
                FormatUpdateDiagnosticLine(
                    *gUpdateSessionResult,
                    true,
                    true,
                    false));
        }
        return;
    }

    if (source == UpdateCheckSource::Automatic) {
        gPluginSettings.lastUpdateCheckUnixSeconds = nowUnixSeconds;
        SavePluginSettings();
    } else {
        gUpdateSessionResult = MakeUpdateInProgressResult(source);
        gManualUpdateNoticeOpen = true;
        AppendDiagnosticsLogLine(
            FormatUpdateDiagnosticLine(
                *gUpdateSessionResult,
                true,
                true,
                false));
    }
}

void RequestAutomaticUpdateCheckIfDue() {
    RequestUpdateCheck(
        xvatsim::modules::update_checker::UpdateCheckSource::Automatic,
        false);
}

void RequestManualUpdateCheck() {
    RequestUpdateCheck(
        xvatsim::modules::update_checker::UpdateCheckSource::Manual,
        true);
    RefreshOverlayFromBrain();
}

void HarvestUpdateCheckerResult() {
    using xvatsim::modules::update_checker::UpdateCheckSource;
    using xvatsim::modules::update_checker::UpdateStatus;

    const auto result = gUpdateChecker.HarvestResult();
    if (!result.has_value()) {
        return;
    }

    gUpdateSessionResult = *result;
    const auto automatic =
        result->source == UpdateCheckSource::Automatic;
    const auto available =
        result->status == UpdateStatus::Available;
    if (!automatic) {
        gManualUpdateNoticeOpen = true;
    }

    const auto noticeRequested =
        gManualUpdateNoticeOpen ||
        (automatic && available && AutomaticUpdateNoticeRequested());
    AppendDiagnosticsLogLine(
        FormatUpdateDiagnosticLine(
            *result,
            noticeRequested,
            noticeRequested,
            false));
}

void ApplyDisplayOverrideMode(
    xvatsim::brain::BrainOwnedDisplayOverrideMode mode) {
    xvatsim::brain::SetBrainOwnedDisplayOverrideMode(
        &gBrainOwnedRuntimeState,
        mode);
    gOverlayWindow.SetAutomaticMode(
        gBrainOwnedRuntimeState.displayOverrideMode ==
        xvatsim::brain::BrainOwnedDisplayOverrideMode::Auto);
    SavePluginSettings();
    RefreshOverlayFromBrain();
}

void EnableStandbyAssist() {
    gPluginSettings.standbyAssistEnabled = true;
    ResetStandbyAssistLatch();
    SavePluginSettings();
    RefreshOverlayFromBrain();
}

void DisableStandbyAssist() {
    gPluginSettings.standbyAssistEnabled = false;
    ResetStandbyAssistLatch();
    SavePluginSettings();
    RefreshOverlayFromBrain();
}

void ApplyOverlayAppearanceSettings() {
    gPluginSettings.overlayOpacity =
        std::clamp(gPluginSettings.overlayOpacity, 0.45f, 1.0f);
    gPluginSettings.overlayScale =
        std::clamp(gPluginSettings.overlayScale, 0.85f, 1.35f);
    gPluginSettings.animationSpeed =
        std::clamp(gPluginSettings.animationSpeed, 0.60f, 1.60f);

    gOverlayWindow.SetOpacity(gPluginSettings.overlayOpacity);
    gOverlayWindow.SetScale(gPluginSettings.overlayScale);
    gOverlayWindow.SetAnimationSpeed(gPluginSettings.animationSpeed);
}

void AdjustOverlayOpacity(float delta) {
    gPluginSettings.overlayOpacity += delta;
    ApplyOverlayAppearanceSettings();
    SavePluginSettings();
}

void AdjustOverlayScale(float delta) {
    gPluginSettings.overlayScale += delta;
    ApplyOverlayAppearanceSettings();
    SavePluginSettings();
}

void AdjustAnimationSpeed(float delta) {
    gPluginSettings.animationSpeed += delta;
    ApplyOverlayAppearanceSettings();
    SavePluginSettings();
}

void ResetOverlayAppearance() {
    gPluginSettings.overlayOpacity = 1.0f;
    gPluginSettings.overlayScale = 1.0f;
    gPluginSettings.animationSpeed = 1.0f;
    ApplyOverlayAppearanceSettings();
    SavePluginSettings();
}

void PersistOverlayGeometryIfChanged() {
    int left = 0;
    int top = 0;
    float scale = 0.0f;
    const auto positionChanged = gOverlayWindow.ConsumePositionChanged(&left, &top);
    const auto scaleChanged = gOverlayWindow.ConsumeScaleChanged(&scale);
    if (!positionChanged && !scaleChanged) {
        return;
    }

    if (positionChanged) {
        gPluginSettings.hasWindowPosition = true;
        gPluginSettings.windowLeft = left;
        gPluginSettings.windowTop = top;
    }
    if (scaleChanged) {
        if (std::isfinite(scale)) {
            gPluginSettings.overlayScale = std::clamp(scale, 0.85f, 1.35f);
        } else {
            XPLMDebugString("[XVatsim] Ignored invalid overlay scale persistence value.\n");
        }
    }
    SavePluginSettings();
}

void RenderDormantBoundaryFrame(bool hideWindow) {
    xvatsim::brain::OverlayViewModel overlayModel;
    overlayModel.mode = xvatsim::brain::OverlayMode::Dormant;
    overlayModel.visible = false;

    if (hideWindow) {
        DiscardPendingAccessoryClickFacts();
        xvatsim::brain::CloseBrainOwnedAccessoryForTemporaryOverlaySleep(
            &gBrainOwnedRuntimeState);
        gOverlayWindow.Hide();
    } else {
        UpdateOverlayWindow(overlayModel);
    }
}

void RenderSessionBoundaryFrame(
    const xvatsim::brain::AircraftStateSnapshot& aircraftState,
    const xvatsim::brain::XPilotSessionSnapshot& xPilotSessionSnapshot,
    bool wakeForDisconnectAlert) {
    const auto displayOverrideMode =
        gBrainOwnedRuntimeState.displayOverrideMode;
    gOverlayWindow.SetAutomaticMode(
        displayOverrideMode ==
        xvatsim::brain::BrainOwnedDisplayOverrideMode::Auto);

    auto shouldWake = wakeForDisconnectAlert;
    if (displayOverrideMode ==
        xvatsim::brain::BrainOwnedDisplayOverrideMode::ForcedOpen) {
        shouldWake = true;
    } else if (displayOverrideMode ==
               xvatsim::brain::BrainOwnedDisplayOverrideMode::ForcedSleep) {
        shouldWake = false;
    }
    const auto updateSnapshot = BuildOverlayUpdateSnapshot();
    if (xvatsim::brain::OverlayUpdateRequestsWake(updateSnapshot)) {
        shouldWake = true;
    }

    if (!shouldWake) {
        RenderDormantBoundaryFrame(
            displayOverrideMode ==
            xvatsim::brain::BrainOwnedDisplayOverrideMode::Auto);
        return;
    }

    auto overlayModel = xvatsim::brain::BrainOrchestrator::BuildOverlayViewModel(
        xvatsim::brain::WorkflowStage::None,
        aircraftState,
        xPilotSessionSnapshot,
        xvatsim::brain::RadioStateSnapshot{},
        xvatsim::brain::NetworkPlanSnapshot{},
        xvatsim::brain::ControllerFeedSnapshot{},
        xvatsim::brain::TransceiverResolutionSnapshot{},
        xvatsim::brain::FinalDisplaySnapshot{},
        xvatsim::brain::ManualQuerySnapshot{},
        updateSnapshot);
    overlayModel.headerRightText.clear();
    UpdateOverlayWindow(overlayModel);
}

void ForceDisplayOpen() {
    ApplyDisplayOverrideMode(
        xvatsim::brain::BrainOwnedDisplayOverrideMode::ForcedOpen);
}

void ForceDisplaySleep() {
    DiscardPendingTextEntryState();
    DiscardPendingAccessoryClickFacts();
    xvatsim::brain::CloseBrainOwnedAccessoryForDisplayClose(
        &gBrainOwnedRuntimeState);
    ApplyDisplayOverrideMode(
        xvatsim::brain::BrainOwnedDisplayOverrideMode::ForcedSleep);
}

void ReturnDisplayToAuto() {
    DiscardPendingTextEntryState();
    ApplyDisplayOverrideMode(
        xvatsim::brain::BrainOwnedDisplayOverrideMode::Auto);
}

void RequestCurrentFlightRecovery() {
    DiscardPendingTextEntryState();
    xvatsim::brain::SetBrainOwnedManualFlightRecoveryRequested(
        &gBrainOwnedRuntimeState,
        true);
    ShowTransientStatusLine("RECOVER evaluating current flight");
    XPLMDebugString("[XVatsim] Manual current-flight recovery requested.\n");
    RefreshOverlayFromBrain();
}

void ApplyCruiseTargetFromCurrentAltitude() {
    const auto& lastAircraftState =
        gBrainOwnedRuntimeState.lastAircraftStateSnapshot;
    const auto& lastNetworkPlan =
        gBrainOwnedRuntimeState.lastNetworkPlanSnapshot;
    xvatsim::brain::BrainOwnedCruiseTargetCommandInput input;
    input.command = xvatsim::brain::BrainOwnedCruiseTargetCommand::CurrentAltitude;
    input.flightContextActive = gBrainOwnedRuntimeState.flightContext.active;
    input.planKey = xvatsim::brain::BuildBrainOwnedNetworkPlanIdentityKey(lastNetworkPlan);
    input.aircraftState = lastAircraftState;
    input.networkPlan = lastNetworkPlan;
    input.nowSeconds = XPLMGetElapsedTime();
    input.tuning.gateToleranceFt = kCruiseGateToleranceFt;
    input.tuning.stableVerticalSpeedFpm = kCruiseGateStableVsFpm;
    input.tuning.gateDwellSeconds = kCruiseGateDwellSeconds;

    const auto output =
        xvatsim::brain::ApplyBrainOwnedCruiseTargetCommand(
            &gBrainOwnedRuntimeState,
            input);
    ShowTransientStatusLine(output.statusLine);
    RefreshOverlayFromBrain();
}

void ResetCruiseTargetToFiledAltitude() {
    const auto& lastAircraftState =
        gBrainOwnedRuntimeState.lastAircraftStateSnapshot;
    const auto& lastNetworkPlan =
        gBrainOwnedRuntimeState.lastNetworkPlanSnapshot;
    xvatsim::brain::BrainOwnedCruiseTargetCommandInput input;
    input.command = xvatsim::brain::BrainOwnedCruiseTargetCommand::FiledAltitude;
    input.flightContextActive = gBrainOwnedRuntimeState.flightContext.active;
    input.planKey = xvatsim::brain::BuildBrainOwnedNetworkPlanIdentityKey(lastNetworkPlan);
    input.aircraftState = lastAircraftState;
    input.networkPlan = lastNetworkPlan;
    input.nowSeconds = XPLMGetElapsedTime();
    input.tuning.gateToleranceFt = kCruiseGateToleranceFt;
    input.tuning.stableVerticalSpeedFpm = kCruiseGateStableVsFpm;
    input.tuning.gateDwellSeconds = kCruiseGateDwellSeconds;

    const auto output =
        xvatsim::brain::ApplyBrainOwnedCruiseTargetCommand(
            &gBrainOwnedRuntimeState,
            input);
    ShowTransientStatusLine(output.statusLine);
    RefreshOverlayFromBrain();
}

void BeginDiversionEntry() {
    const auto& lastNetworkPlan =
        gBrainOwnedRuntimeState.lastNetworkPlanSnapshot;
    DiscardPendingTextEntryState();
    if (!gBrainOwnedRuntimeState.flightContext.active) {
        ShowTransientStatusLine("DIVERT unavailable without active flight");
        RefreshOverlayFromBrain();
        return;
    }

    if (xvatsim::brain::BuildBrainOwnedNetworkPlanIdentityKey(lastNetworkPlan)
            .empty()) {
        ShowTransientStatusLine("DIVERT unavailable until VATSIM plan matched");
        RefreshOverlayFromBrain();
        return;
    }

    xvatsim::brain::ClearBrainOwnedManualQuery(&gBrainOwnedRuntimeState);
    xvatsim::brain::SetBrainOwnedPendingTextEntryMode(
        &gBrainOwnedRuntimeState,
        xvatsim::brain::BrainOwnedTextEntryMode::DiversionAirport);
    ShowTransientStatusLine("DIVERT enter ICAO and press Enter");
    gOverlayWindow.BeginTextEntry("");
}

void ApplyDiversionFromSubmittedText(const std::string& submittedText) {
    const auto& lastAircraftState =
        gBrainOwnedRuntimeState.lastAircraftStateSnapshot;
    const auto& lastPilotIdentity =
        gBrainOwnedRuntimeState.lastPilotIdentitySnapshot;
    const auto& lastFlightPlan =
        gBrainOwnedRuntimeState.lastFlightPlanSnapshot;
    const auto& lastNetworkPlan =
        gBrainOwnedRuntimeState.lastNetworkPlanSnapshot;
    if (!gBrainOwnedRuntimeState.flightContext.active || !lastAircraftState.valid) {
        ShowTransientStatusLine("DIVERT unavailable without active flight");
        return;
    }

    const auto airportIcao = NormalizeIcaoInput(submittedText);
    if (airportIcao.empty()) {
        ShowTransientStatusLine("DIVERT invalid airport");
        return;
    }

    const auto sourcePlanKey = xvatsim::brain::BuildBrainOwnedNetworkPlanIdentityKey(lastNetworkPlan);
    if (sourcePlanKey.empty()) {
        ClearDiversionOverrideState();
        ShowTransientStatusLine("DIVERT unavailable until VATSIM plan matched");
        LogDiversionAction(
            "Diversion request rejected because VATSIM flight plan was stale or unmatched.");
        return;
    }

    xvatsim::brain::BrainOwnedDiversionOverrideInput overrideInput;
    overrideInput.hasOverride = gDiversionContextModule.HasOverride();
    overrideInput.sourcePlanKey = sourcePlanKey;
    const auto overrideDecision =
        xvatsim::brain::DecideBrainOwnedDiversionOverride(
            gBrainOwnedRuntimeState,
            overrideInput);
    if (overrideDecision.clearOverride) {
        ClearDiversionOverrideState();
        ShowTransientStatusLine("DIVERT cleared; flight plan changed");
        LogDiversionAction(
            "Diversion override cleared before update because source flight plan changed.");
        return;
    }

    const auto result = gDiversionContextModule.SetDiversionAirport(airportIcao);
    ShowTransientStatusLine(result.statusLine);
    if (!result.accepted) {
        return;
    }

    xvatsim::brain::SetBrainOwnedDiversionOverrideSourceKey(
        &gBrainOwnedRuntimeState,
        sourcePlanKey);
    if (!result.changed) {
        return;
    }

    const auto effectiveNetworkPlanSnapshot =
        BuildEffectiveNetworkPlanSnapshot(lastNetworkPlan);
    RetargetFlightContextToPlan(
        lastPilotIdentity,
        lastFlightPlan,
        effectiveNetworkPlanSnapshot);
    LogDiversionAction("Diversion override set: " + result.airportIcao);
}

void RevertToVatsimFlightPlan() {
    const auto& lastPilotIdentity =
        gBrainOwnedRuntimeState.lastPilotIdentitySnapshot;
    const auto& lastFlightPlan =
        gBrainOwnedRuntimeState.lastFlightPlanSnapshot;
    const auto& lastNetworkPlan =
        gBrainOwnedRuntimeState.lastNetworkPlanSnapshot;
    if (!gDiversionContextModule.HasOverride()) {
        ShowTransientStatusLine("DIVERT no override active");
        RefreshOverlayFromBrain();
        return;
    }

    if (xvatsim::brain::BuildBrainOwnedNetworkPlanIdentityKey(lastNetworkPlan)
            .empty() ||
        lastNetworkPlan.destinationIcao.empty()) {
        ShowTransientStatusLine("DIVERT revert unavailable");
        LogDiversionAction(
            "Diversion revert requested but the VATSIM flight plan was stale, unmatched, or missing a destination.");
        RefreshOverlayFromBrain();
        return;
    }

    ClearDiversionOverrideState();

    RetargetFlightContextToPlan(
        lastPilotIdentity,
        lastFlightPlan,
        lastNetworkPlan);
    ShowTransientStatusLine(
        "DIVERT reverted to " + lastNetworkPlan.destinationIcao);
    LogDiversionAction(
        "Diversion override cleared; reverted to VATSIM destination " +
        lastNetworkPlan.destinationIcao);
    RefreshOverlayFromBrain();
}

void ClearManualQueryIfExpired() {
    xvatsim::brain::ExpireBrainOwnedManualQuery(
        &gBrainOwnedRuntimeState,
        CurrentTickSeconds());
}

void RefreshManualQueryState() {
    ClearManualQueryIfExpired();

    std::string submittedCommand;
    if (!gOverlayWindow.ConsumeSubmittedText(&submittedCommand)) {
        return;
    }

    const auto pendingMode =
        xvatsim::brain::ConsumeBrainOwnedPendingTextEntryMode(
            &gBrainOwnedRuntimeState);

    if (pendingMode == xvatsim::brain::BrainOwnedTextEntryMode::None) {
        XPLMDebugString(
            "[XVatsim] Ignored submitted overlay text with no active prompt.\n");
        return;
    }

    if (pendingMode == xvatsim::brain::BrainOwnedTextEntryMode::DiversionAirport) {
        ApplyDiversionFromSubmittedText(submittedCommand);
        return;
    }

    if (pendingMode ==
        xvatsim::brain::BrainOwnedTextEntryMode::MetarAirportLookup) {
        xvatsim::brain::BrainOwnedTextEntryFact fact;
        fact.mode = pendingMode;
        fact.text = submittedCommand;
        fact.monotonicMs = CurrentTickMilliseconds();
        const auto decision = xvatsim::brain::CommitBrainOwnedTextEntryFact(
            &gBrainOwnedRuntimeState,
            fact);
        ShowTransientStatusLine(
            decision.accepted
                ? "METAR lookup accepted for " + decision.normalizedText
                : "METAR lookup rejected: enter one four-character ICAO");
        return;
    }

    if (pendingMode ==
        xvatsim::brain::BrainOwnedTextEntryMode::AtisAirportLookup) {
        xvatsim::brain::BrainOwnedTextEntryFact fact;
        fact.mode = pendingMode;
        fact.text = submittedCommand;
        fact.monotonicMs = CurrentTickMilliseconds();
        const auto decision =
            xvatsim::brain::CommitBrainOwnedAtisTextEntryFact(
                &gBrainOwnedRuntimeState, fact);
        ShowTransientStatusLine(
            decision.accepted
                ? "ATIS lookup accepted for " + decision.normalizedText
                : "ATIS lookup rejected: enter one four-character ICAO");
        return;
    }

    if (pendingMode == xvatsim::brain::BrainOwnedTextEntryMode::ManualCtaf &&
        submittedCommand.find(".ctaf") != 0 &&
        submittedCommand.find("ctaf") != 0) {
        submittedCommand = ".ctaf " + submittedCommand;
    }

    xvatsim::brain::CommitBrainOwnedManualQuerySnapshot(
        &gBrainOwnedRuntimeState,
        gCtafLookupService.RunManualCtafQuery(submittedCommand),
        CurrentTickSeconds() + kManualQueryVisibleSeconds);
}

void UpdateOverlayWakeTracking(
    const xvatsim::brain::AircraftStateSnapshot& aircraftState,
    const xvatsim::brain::XPilotSessionSnapshot& xPilotSessionSnapshot) {
    xvatsim::brain::MarkBrainOwnedXPilotConnectedIfConnected(
        &gBrainOwnedRuntimeState,
        xPilotSessionSnapshot);

    xvatsim::brain::BrainOwnedCruiseTargetProgressInput input;
    input.aircraftState = aircraftState;
    input.nowSeconds = XPLMGetElapsedTime();
    input.tuning.gateToleranceFt = kCruiseGateToleranceFt;
    input.tuning.stableVerticalSpeedFpm = kCruiseGateStableVsFpm;
    input.tuning.gateDwellSeconds = kCruiseGateDwellSeconds;
    xvatsim::brain::UpdateBrainOwnedCruiseTargetProgress(
        &gBrainOwnedRuntimeState,
        input);
}

bool ShouldHandleCommandBegin(XPLMCommandPhase phase) {
    return phase == xplm_CommandBegin && gPluginRuntimeEnabled;
}

void RegisterPluginCommand(
    XPLMCommandRef* commandRef,
    const char* commandName,
    const char* commandDescription,
    XPLMCommandCallback_f handler) {
    if (commandRef == nullptr || *commandRef != nullptr) {
        return;
    }

    *commandRef = XPLMCreateCommand(commandName, commandDescription);
    if (*commandRef != nullptr) {
        XPLMRegisterCommandHandler(*commandRef, handler, 0, nullptr);
    }
}

void UnregisterPluginCommand(
    XPLMCommandRef* commandRef,
    XPLMCommandCallback_f handler) {
    if (commandRef == nullptr || *commandRef == nullptr) {
        return;
    }

    XPLMUnregisterCommandHandler(*commandRef, handler, 0, nullptr);
    *commandRef = nullptr;
}

int ManualCtafCommandHandler(
    XPLMCommandRef inCommand,
    XPLMCommandPhase inPhase,
    void* inRefcon) {
    (void)inCommand;
    (void)inRefcon;

    if (ShouldHandleCommandBegin(inPhase)) {
        BeginManualCtafEntry();
        return 1;
    }

    return 1;
}

int DisplayOpenCommandHandler(
    XPLMCommandRef inCommand,
    XPLMCommandPhase inPhase,
    void* inRefcon) {
    (void)inCommand;
    (void)inRefcon;

    if (ShouldHandleCommandBegin(inPhase)) {
        ForceDisplayOpen();
        return 1;
    }

    return 1;
}

int DisplayCloseCommandHandler(
    XPLMCommandRef inCommand,
    XPLMCommandPhase inPhase,
    void* inRefcon) {
    (void)inCommand;
    (void)inRefcon;

    if (ShouldHandleCommandBegin(inPhase)) {
        ForceDisplaySleep();
        return 1;
    }

    return 1;
}

int DisplayAutoCommandHandler(
    XPLMCommandRef inCommand,
    XPLMCommandPhase inPhase,
    void* inRefcon) {
    (void)inCommand;
    (void)inRefcon;

    if (ShouldHandleCommandBegin(inPhase)) {
        ReturnDisplayToAuto();
        return 1;
    }

    return 1;
}

int CruiseTargetCurrentCommandHandler(
    XPLMCommandRef inCommand,
    XPLMCommandPhase inPhase,
    void* inRefcon) {
    (void)inCommand;
    (void)inRefcon;

    if (ShouldHandleCommandBegin(inPhase)) {
        ApplyCruiseTargetFromCurrentAltitude();
        return 1;
    }

    return 1;
}

int CruiseTargetFiledCommandHandler(
    XPLMCommandRef inCommand,
    XPLMCommandPhase inPhase,
    void* inRefcon) {
    (void)inCommand;
    (void)inRefcon;

    if (ShouldHandleCommandBegin(inPhase)) {
        ResetCruiseTargetToFiledAltitude();
        return 1;
    }

    return 1;
}

int ResetSessionCommandHandler(
    XPLMCommandRef inCommand,
    XPLMCommandPhase inPhase,
    void* inRefcon) {
    (void)inCommand;
    (void)inRefcon;

    if (ShouldHandleCommandBegin(inPhase)) {
        ResetSessionState();
        return 1;
    }

    return 1;
}

int RecoverCurrentFlightCommandHandler(
    XPLMCommandRef inCommand,
    XPLMCommandPhase inPhase,
    void* inRefcon) {
    (void)inCommand;
    (void)inRefcon;

    if (ShouldHandleCommandBegin(inPhase)) {
        RequestCurrentFlightRecovery();
        return 1;
    }

    return 1;
}

void PluginMenuHandler(void* inMenuRef, void* inItemRef) {
    (void)inMenuRef;

    if (!gPluginRuntimeEnabled) {
        return;
    }

    switch (reinterpret_cast<intptr_t>(inItemRef)) {
        case kIfrModeMenuItemRef:
            RequestOperatingModeSelection(
                xvatsim::brain::BrainOwnedOperatingMode::IFR);
            break;
        case kVfrModeMenuItemRef:
            RequestOperatingModeSelection(
                xvatsim::brain::BrainOwnedOperatingMode::VFR);
            break;
        case kManualCtafMenuItemRef:
            BeginManualCtafEntry();
            break;
        case kSelectMetarAirportMenuItemRef:
            BeginMetarAirportLookupEntry();
            break;
        case kSelectAtisAirportMenuItemRef:
            BeginAtisAirportLookupEntry();
            break;
        case kDisplayOpenMenuItemRef:
            ForceDisplayOpen();
            break;
        case kDisplayCloseMenuItemRef:
            ForceDisplaySleep();
            break;
        case kDisplayAutoMenuItemRef:
            ReturnDisplayToAuto();
            break;
        case kOpacityUpMenuItemRef:
            AdjustOverlayOpacity(kOpacityStep);
            break;
        case kOpacityDownMenuItemRef:
            AdjustOverlayOpacity(-kOpacityStep);
            break;
        case kScaleUpMenuItemRef:
            AdjustOverlayScale(kScaleStep);
            break;
        case kScaleDownMenuItemRef:
            AdjustOverlayScale(-kScaleStep);
            break;
        case kAnimationFasterMenuItemRef:
            AdjustAnimationSpeed(kAnimationSpeedStep);
            break;
        case kAnimationSlowerMenuItemRef:
            AdjustAnimationSpeed(-kAnimationSpeedStep);
            break;
        case kResetAppearanceMenuItemRef:
            ResetOverlayAppearance();
            break;
        case kCruiseTargetCurrentMenuItemRef:
            ApplyCruiseTargetFromCurrentAltitude();
            break;
        case kCruiseTargetFiledMenuItemRef:
            ResetCruiseTargetToFiledAltitude();
            break;
        case kResetSessionMenuItemRef:
            ResetSessionState();
            break;
        case kRecoverCurrentFlightMenuItemRef:
            RequestCurrentFlightRecovery();
            break;
        case kCheckForUpdatesMenuItemRef:
            RequestManualUpdateCheck();
            break;
        case kSetDiversionAirportMenuItemRef:
            BeginDiversionEntry();
            break;
        case kRevertToFlightPlanMenuItemRef:
            RevertToVatsimFlightPlan();
            break;
        case kStandbyAssistOnMenuItemRef:
            EnableStandbyAssist();
            break;
        case kStandbyAssistOffMenuItemRef:
            DisableStandbyAssist();
            break;
        default:
            break;
    }
}

void RegisterPluginMenu() {
    if (gPluginMenu != nullptr) {
        return;
    }

    const auto pluginsMenu = XPLMFindPluginsMenu();
    if (pluginsMenu == nullptr) {
        XPLMDebugString("[XVatsim] Plugin menu unavailable; menu registration skipped.\n");
        return;
    }

    gPluginMenuItemIndex = XPLMAppendMenuItem(pluginsMenu, "XVatsim 2.0.1", nullptr, 1);
    if (gPluginMenuItemIndex < 0) {
        gPluginMenuItemIndex = -1;
        XPLMDebugString("[XVatsim] Plugin menu item registration failed.\n");
        return;
    }

    gPluginMenu = XPLMCreateMenu(
        "XVatsim 2.0.1",
        pluginsMenu,
        gPluginMenuItemIndex,
        PluginMenuHandler,
        nullptr);

    if (gPluginMenu == nullptr) {
        XPLMRemoveMenuItem(pluginsMenu, gPluginMenuItemIndex);
        gPluginMenuItemIndex = -1;
        XPLMDebugString("[XVatsim] Plugin menu creation failed.\n");
        return;
    }

    gIfrModeMenuItemIndex = XPLMAppendMenuItem(
        gPluginMenu,
        "IFR Mode",
        reinterpret_cast<void*>(kIfrModeMenuItemRef),
        1);
    gVfrModeMenuItemIndex = XPLMAppendMenuItem(
        gPluginMenu,
        "VFR Mode",
        reinterpret_cast<void*>(kVfrModeMenuItemRef),
        1);
    XPLMAppendMenuSeparator(gPluginMenu);
    XPLMAppendMenuItem(
        gPluginMenu,
        "Manual CTAF Lookup",
        reinterpret_cast<void*>(kManualCtafMenuItemRef),
        1);
    XPLMAppendMenuItem(
        gPluginMenu,
        u8"Select METAR Airport\u2026",
        reinterpret_cast<void*>(kSelectMetarAirportMenuItemRef),
        1);
    XPLMAppendMenuItem(
        gPluginMenu,
        u8"Select ATIS Airport\u2026",
        reinterpret_cast<void*>(kSelectAtisAirportMenuItemRef),
        1);
    XPLMAppendMenuItem(
        gPluginMenu,
        "Open Display",
        reinterpret_cast<void*>(kDisplayOpenMenuItemRef),
        1);
    XPLMAppendMenuItem(
        gPluginMenu,
        "Close Display",
        reinterpret_cast<void*>(kDisplayCloseMenuItemRef),
        1);
    XPLMAppendMenuItem(
        gPluginMenu,
        "Auto Display",
        reinterpret_cast<void*>(kDisplayAutoMenuItemRef),
        1);
    XPLMAppendMenuSeparator(gPluginMenu);
    XPLMAppendMenuItem(
        gPluginMenu,
        "More Opacity",
        reinterpret_cast<void*>(kOpacityUpMenuItemRef),
        1);
    XPLMAppendMenuItem(
        gPluginMenu,
        "Less Opacity",
        reinterpret_cast<void*>(kOpacityDownMenuItemRef),
        1);
    XPLMAppendMenuItem(
        gPluginMenu,
        "Larger UI",
        reinterpret_cast<void*>(kScaleUpMenuItemRef),
        1);
    XPLMAppendMenuItem(
        gPluginMenu,
        "Smaller UI",
        reinterpret_cast<void*>(kScaleDownMenuItemRef),
        1);
    XPLMAppendMenuItem(
        gPluginMenu,
        "Faster Animation",
        reinterpret_cast<void*>(kAnimationFasterMenuItemRef),
        1);
    XPLMAppendMenuItem(
        gPluginMenu,
        "Slower Animation",
        reinterpret_cast<void*>(kAnimationSlowerMenuItemRef),
        1);
    XPLMAppendMenuItem(
        gPluginMenu,
        "Reset Appearance",
        reinterpret_cast<void*>(kResetAppearanceMenuItemRef),
        1);
    XPLMAppendMenuSeparator(gPluginMenu);
    XPLMAppendMenuItem(
        gPluginMenu,
        "Set Cruise Target To Current Altitude",
        reinterpret_cast<void*>(kCruiseTargetCurrentMenuItemRef),
        1);
    XPLMAppendMenuItem(
        gPluginMenu,
        "Reset Cruise Target To Filed Altitude",
        reinterpret_cast<void*>(kCruiseTargetFiledMenuItemRef),
        1);
    XPLMAppendMenuSeparator(gPluginMenu);
    XPLMAppendMenuItem(
        gPluginMenu,
        "Reset XVatsim Session",
        reinterpret_cast<void*>(kResetSessionMenuItemRef),
        1);
    XPLMAppendMenuItem(
        gPluginMenu,
        "Recover Current Flight",
        reinterpret_cast<void*>(kRecoverCurrentFlightMenuItemRef),
        1);
    XPLMAppendMenuItem(
        gPluginMenu,
        "Check for Updates",
        reinterpret_cast<void*>(kCheckForUpdatesMenuItemRef),
        1);
    XPLMAppendMenuItem(
        gPluginMenu,
        "Set Diversion Airport...",
        reinterpret_cast<void*>(kSetDiversionAirportMenuItemRef),
        1);
    XPLMAppendMenuItem(
        gPluginMenu,
        "Revert To VATSIM Flight Plan",
        reinterpret_cast<void*>(kRevertToFlightPlanMenuItemRef),
        1);
    XPLMAppendMenuSeparator(gPluginMenu);
    XPLMAppendMenuItem(
        gPluginMenu,
        "Standby Assist On",
        reinterpret_cast<void*>(kStandbyAssistOnMenuItemRef),
        1);
    XPLMAppendMenuItem(
        gPluginMenu,
        "Standby Assist Off",
        reinterpret_cast<void*>(kStandbyAssistOffMenuItemRef),
        1);
    XPLMAppendMenuSeparator(gPluginMenu);
    SyncOperatingModeMenuChecks();
}

void UnregisterPluginMenu() {
    if (gPluginMenu != nullptr) {
        XPLMDestroyMenu(gPluginMenu);
        gPluginMenu = nullptr;
    }
    gIfrModeMenuItemIndex = -1;
    gVfrModeMenuItemIndex = -1;

    if (gPluginMenuItemIndex >= 0) {
        XPLMRemoveMenuItem(XPLMFindPluginsMenu(), gPluginMenuItemIndex);
        gPluginMenuItemIndex = -1;
    }
}

void RegisterPluginCommands() {
    if (gManualCtafCommand != nullptr ||
        gDisplayOpenCommand != nullptr ||
        gDisplayCloseCommand != nullptr ||
        gDisplayAutoCommand != nullptr ||
        gCruiseTargetCurrentCommand != nullptr ||
        gCruiseTargetFiledCommand != nullptr ||
        gResetSessionCommand != nullptr ||
        gRecoverCurrentFlightCommand != nullptr) {
        return;
    }

    RegisterPluginCommand(
        &gManualCtafCommand,
        kManualCtafCommandName,
        kManualCtafCommandDesc,
        ManualCtafCommandHandler);

    RegisterPluginCommand(
        &gDisplayOpenCommand,
        kDisplayOpenCommandName,
        kDisplayOpenCommandDesc,
        DisplayOpenCommandHandler);

    RegisterPluginCommand(
        &gDisplayCloseCommand,
        kDisplayCloseCommandName,
        kDisplayCloseCommandDesc,
        DisplayCloseCommandHandler);

    RegisterPluginCommand(
        &gDisplayAutoCommand,
        kDisplayAutoCommandName,
        kDisplayAutoCommandDesc,
        DisplayAutoCommandHandler);

    RegisterPluginCommand(
        &gCruiseTargetCurrentCommand,
        kCruiseTargetCurrentCommandName,
        kCruiseTargetCurrentCommandDesc,
        CruiseTargetCurrentCommandHandler);

    RegisterPluginCommand(
        &gCruiseTargetFiledCommand,
        kCruiseTargetFiledCommandName,
        kCruiseTargetFiledCommandDesc,
        CruiseTargetFiledCommandHandler);

    RegisterPluginCommand(
        &gResetSessionCommand,
        kResetSessionCommandName,
        kResetSessionCommandDesc,
        ResetSessionCommandHandler);

    RegisterPluginCommand(
        &gRecoverCurrentFlightCommand,
        kRecoverCurrentFlightCommandName,
        kRecoverCurrentFlightCommandDesc,
        RecoverCurrentFlightCommandHandler);
}

void UnregisterPluginCommands() {
    UnregisterPluginCommand(&gManualCtafCommand, ManualCtafCommandHandler);
    UnregisterPluginCommand(&gDisplayOpenCommand, DisplayOpenCommandHandler);
    UnregisterPluginCommand(&gDisplayCloseCommand, DisplayCloseCommandHandler);
    UnregisterPluginCommand(&gDisplayAutoCommand, DisplayAutoCommandHandler);
    UnregisterPluginCommand(
        &gCruiseTargetCurrentCommand,
        CruiseTargetCurrentCommandHandler);
    UnregisterPluginCommand(
        &gCruiseTargetFiledCommand,
        CruiseTargetFiledCommandHandler);
    UnregisterPluginCommand(&gResetSessionCommand, ResetSessionCommandHandler);
    UnregisterPluginCommand(
        &gRecoverCurrentFlightCommand,
        RecoverCurrentFlightCommandHandler);
}

void RefreshOverlayFromBrainEngineer3() {
    if (!gPluginRuntimeEnabled) {
        return;
    }

    gDiagnosticsState.frame = {};
    auto& diagnostics = gDiagnosticsState.frame;
    diagnostics.valid = true;
    const auto diagnosticsNowSeconds = CurrentTickSeconds();
    diagnostics.collectJobs =
        !gBrainOwnedRuntimeState.operationalRefreshGate.initialized ||
        (diagnosticsNowSeconds - gDiagnosticsState.lastSummarySeconds) >=
            kDiagnosticsSummaryIntervalSeconds;
    HarvestUpdateCheckerResult();
    HandleUpdateNoticeDismissRequest();
    RequestAutomaticUpdateCheckIfDue();

    auto timingStarted = std::chrono::steady_clock::now();
    const auto aircraftState = gAircraftStateSampler.Sample();
    diagnostics.aircraftStateUs = ElapsedMicrosecondsSince(timingStarted);
    diagnostics.onGround = aircraftState.onGround;
    diagnostics.batteryOn = aircraftState.batteryOn;
    xvatsim::brain::workflow::AircraftRuntimeBoundaryInput
        aircraftBoundaryInput;
    aircraftBoundaryInput.aircraftState = aircraftState;
    aircraftBoundaryInput.coldDarkResetApplied =
        gBrainOwnedRuntimeState.coldDarkResetApplied;
    aircraftBoundaryInput.aircraftStateInvalidBoundaryActive =
        gBrainOwnedRuntimeState.aircraftStateInvalidBoundaryActive;
    const auto aircraftBoundaryDecision =
        xvatsim::brain::workflow::ResolveAircraftRuntimeBoundary(
            aircraftBoundaryInput);
    if (aircraftBoundaryDecision.aircraftStateInvalid) {
        xvatsim::brain::BrainOwnedOperationalActivationInput activationInput;
        activationInput.aircraftStateValid = false;
        activationInput.batteryOn = aircraftState.batteryOn;
        activationInput.xPilotConnected = false;
        const auto activationDecision =
            xvatsim::brain::DecideBrainOwnedOperationalActivation(
                gOperationalActivationState, activationInput);
        xvatsim::brain::CommitBrainOwnedOperationalActivationDecision(
            &gOperationalActivationState, activationDecision);
        LogOperationalActivationTransition(
            activationDecision, SessionBoundaryResult::None);
        ApplyAircraftRuntimeBoundaryDecision(aircraftBoundaryDecision);
        gOverlayWindow.Hide();
        diagnostics.valid = false;
        return;
    }
    if (!aircraftBoundaryDecision.coldDarkBoundaryActive) {
        ApplyAircraftRuntimeBoundaryDecision(aircraftBoundaryDecision);
    }

    timingStarted = std::chrono::steady_clock::now();
    const auto xPilotSessionSnapshot = gXPilotBridge.Poll();
    diagnostics.xpilotPollUs = ElapsedMicrosecondsSince(timingStarted);
    diagnostics.xpilotPollMs = diagnostics.xpilotPollUs / 1000;
    diagnostics.xpilotConnected = xPilotSessionSnapshot.connected;
    diagnostics.callsign = xPilotSessionSnapshot.callsign;

    const auto pilotIdentitySnapshot =
        gPilotIdentityResolver.Resolve(xPilotSessionSnapshot);
    const auto sessionBoundaryResult =
        HandleXPilotSessionBoundary(xPilotSessionSnapshot, pilotIdentitySnapshot);
    xvatsim::brain::BrainOwnedOperationalActivationInput activationInput;
    activationInput.aircraftStateValid = aircraftState.valid;
    activationInput.batteryOn = aircraftState.batteryOn;
    activationInput.xPilotConnected = xPilotSessionSnapshot.connected;
    const auto activationDecision =
        xvatsim::brain::DecideBrainOwnedOperationalActivation(
            gOperationalActivationState, activationInput);
    xvatsim::brain::CommitBrainOwnedOperationalActivationDecision(
        &gOperationalActivationState, activationDecision);
    LogOperationalActivationTransition(
        activationDecision, sessionBoundaryResult);

    if (!activationDecision.operational) {
        if (aircraftBoundaryDecision.coldDarkBoundaryActive) {
            ApplyAircraftRuntimeBoundaryDecision(aircraftBoundaryDecision);
        }
        RenderSessionBoundaryFrame(
            aircraftState,
            xPilotSessionSnapshot,
            sessionBoundaryResult == SessionBoundaryResult::ResetForDisconnect);
        diagnostics.valid = false;
        return;
    }
    if (sessionBoundaryResult != SessionBoundaryResult::None) {
        RenderSessionBoundaryFrame(
            aircraftState, xPilotSessionSnapshot, false);
        diagnostics.valid = false;
        return;
    }

    timingStarted = std::chrono::steady_clock::now();
    RecordOperationalServiceCall(
        activationDecision,
        xvatsim::brain::BrainOwnedOperationalServiceStage::VatsimFeed);
    const auto& vatsimDataFeedSnapshot = gVatsimDataFeedClient.Poll();
    diagnostics.vatsimFeedUs = ElapsedMicrosecondsSince(timingStarted);
    diagnostics.vatsimFeedMs = diagnostics.vatsimFeedUs / 1000;

    timingStarted = std::chrono::steady_clock::now();
    RecordOperationalServiceCall(
        activationDecision,
        xvatsim::brain::BrainOwnedOperationalServiceStage::Radio);
    auto radioStateSnapshot = gRadioStateSampler.Sample();
    diagnostics.radioUs = ElapsedMicrosecondsSince(timingStarted);
    diagnostics.radioMs = diagnostics.radioUs / 1000;
    radioStateSnapshot.standbyAssistEnabled = gPluginSettings.standbyAssistEnabled;

    timingStarted = std::chrono::steady_clock::now();
    (void)gOverlayWindow.ConsumeAcknowledgeRequest();
    (void)gOverlayWindow.ConsumeRecallRequest();
    diagnostics.pdcPrivateSourceUs = ElapsedMicrosecondsSince(timingStarted);

    timingStarted = std::chrono::steady_clock::now();
    RefreshManualQueryState();
    diagnostics.manualQueryUs = ElapsedMicrosecondsSince(timingStarted);

    timingStarted = std::chrono::steady_clock::now();
    RecordOperationalServiceCall(
        activationDecision,
        xvatsim::brain::BrainOwnedOperationalServiceStage::PdcPrivateSource);
    ServicePdcPrivateSource();
    diagnostics.pdcPrivateSourceUs += ElapsedMicrosecondsSince(timingStarted);

    const auto updateSnapshot = BuildOverlayUpdateSnapshot();
    timingStarted = std::chrono::steady_clock::now();
    xvatsim::brain::BrainOwnedOperationalRefreshGateInput refreshGateInput;
    refreshGateInput.monotonicMs = CurrentTickMilliseconds();
    refreshGateInput.settledRefreshIntervalMs =
        kSettledOperationalRefreshIntervalMs;
    refreshGateInput.vatsimGeneration = vatsimDataFeedSnapshot.generation;
    refreshGateInput.controllerContentDigest =
        vatsimDataFeedSnapshot.controllerContentDigest;
    refreshGateInput.radioIdentity =
        HashOperationalRadioIdentity(radioStateSnapshot);
    refreshGateInput.presentationIdentity =
        HashOperationalPresentationIdentity(updateSnapshot);
    refreshGateInput.pdcSemanticGeneration =
        gBrainOwnedRuntimeState.pdc.semanticGeneration;
    refreshGateInput.aircraftOnGround = aircraftState.onGround;
    refreshGateInput.activationRisingEdge =
        activationDecision.activationRisingEdge;
    refreshGateInput.routeWorkerRunning =
        gRoutePreparationWorker.IsRunning();
    refreshGateInput.authorityWorkerRunning =
        gAuthorityRelevanceWorker.IsRunning();
    refreshGateInput.forceRefresh = diagnostics.collectJobs;
    const auto refreshGateDecision =
        xvatsim::brain::DecideBrainOwnedOperationalRefresh(
            gBrainOwnedRuntimeState.operationalRefreshGate,
            refreshGateInput);
    xvatsim::brain::CommitBrainOwnedOperationalRefreshDecision(
        &gBrainOwnedRuntimeState.operationalRefreshGate,
        refreshGateInput,
        refreshGateDecision);
    diagnostics.refreshGateUs = ElapsedMicrosecondsSince(timingStarted);
    diagnostics.refreshGateReason = refreshGateDecision.reason;
    diagnostics.settledFastPath = refreshGateDecision.settledFastPath;
    if (!refreshGateDecision.shouldRunFullRefresh) {
        diagnostics.valid = false;
        return;
    }

    timingStarted = std::chrono::steady_clock::now();
    RecordOperationalServiceCall(
        activationDecision,
        xvatsim::brain::BrainOwnedOperationalServiceStage::ControllerSnapshot);
    const auto controllerFeedSnapshot =
        gControllerFeedClient.BuildSnapshot(vatsimDataFeedSnapshot);
    diagnostics.controllerFeedUs = ElapsedMicrosecondsSince(timingStarted);
    diagnostics.controllerFeedMs = diagnostics.controllerFeedUs / 1000;
    diagnostics.controllerCount = controllerFeedSnapshot.connectedControllers;

    RecordOperationalServiceCall(
        activationDecision,
        xvatsim::brain::BrainOwnedOperationalServiceStage::FlightPlan);
    const auto flightPlanSnapshot =
        SampleFlightPlanForRuntime(aircraftState, &diagnostics);

    timingStarted = std::chrono::steady_clock::now();
    RecordOperationalServiceCall(
        activationDecision,
        xvatsim::brain::BrainOwnedOperationalServiceStage::NetworkPlan);
    const auto networkPlanSnapshot =
        gNetworkPlanLink.Poll(pilotIdentitySnapshot, vatsimDataFeedSnapshot);
    diagnostics.networkPlanUs = ElapsedMicrosecondsSince(timingStarted);
    diagnostics.networkPlanMs = diagnostics.networkPlanUs / 1000;

    xvatsim::brain::CommitBrainOwnedLastSampledFacts(
        &gBrainOwnedRuntimeState,
        aircraftState,
        pilotIdentitySnapshot,
        flightPlanSnapshot,
        networkPlanSnapshot);
    SyncCruiseTargetFromNetworkPlan(networkPlanSnapshot);

    timingStarted = std::chrono::steady_clock::now();
    const auto effectiveNetworkPlanSnapshot =
        BuildEffectiveNetworkPlanSnapshot(networkPlanSnapshot);
    UpdateFlightContextIfNeeded(
        aircraftState,
        pilotIdentitySnapshot,
        flightPlanSnapshot,
        effectiveNetworkPlanSnapshot);
    AttemptPendingCurrentFlightRecovery(
        aircraftState,
        flightPlanSnapshot,
        effectiveNetworkPlanSnapshot);
    const auto& flightContext = gBrainOwnedRuntimeState.flightContext;
    diagnostics.flightContextUs = ElapsedMicrosecondsSince(timingStarted);
    diagnostics.flightContextActive = flightContext.active;
    if (flightContext.active) {
        diagnostics.route =
            flightContext.departureIcao + "->" +
                flightContext.destinationIcao;
    }

    xvatsim::modules::ctaf_lookup::CtafLookupEntry departureCtafLookup;
    xvatsim::modules::ctaf_lookup::CtafLookupEntry arrivalCtafLookup;
    timingStarted = std::chrono::steady_clock::now();
    if (flightContext.active) {
        RecordOperationalServiceCall(
            activationDecision,
            xvatsim::brain::BrainOwnedOperationalServiceStage::Ctaf);
        if (!flightContext.departureIcao.empty()) {
            departureCtafLookup =
                gCtafLookupService.Lookup(flightContext.departureIcao);
        }
        if (!flightContext.destinationIcao.empty()) {
            arrivalCtafLookup =
                gCtafLookupService.Lookup(flightContext.destinationIcao);
        }
    }
    diagnostics.ctafUs = ElapsedMicrosecondsSince(timingStarted);
    diagnostics.ctafMs = diagnostics.ctafUs / 1000;

    HandoffDecision workflowDecision;
    xvatsim::brain::FinalDisplaySnapshot finalDisplaySnapshot;
    xvatsim::brain::TransceiverResolutionSnapshot emptyTransceiverSnapshot;
    const xvatsim::brain::TransceiverResolutionSnapshot*
        transceiverResolutionSnapshot = &emptyTransceiverSnapshot;
    xvatsim::brain::RadioReachableControllerSnapshot gatedRadioSnapshot;
    xvatsim::brain::BrainOwnedPublisherOutput publisherOutput;
    bool hasPublisherOutput = false;
    const auto planKey = xvatsim::brain::BuildBrainOwnedNetworkPlanIdentityKey(effectiveNetworkPlanSnapshot);

    if (flightContext.active) {
        RecordOperationalServiceCall(
            activationDecision,
            xvatsim::brain::BrainOwnedOperationalServiceStage::Route);
        const auto routePolygonOutput =
            RefreshBrainRoutePolygonSnapshot(
                aircraftState,
                effectiveNetworkPlanSnapshot,
                &diagnostics);

        const auto radioSnapshot =
            BuildEngineer3RadioSnapshot(
                aircraftState,
                controllerFeedSnapshot,
                planKey,
                &diagnostics);
        if (gBrainOwnedRuntimeState.transceiverSnapshot != nullptr) {
            transceiverResolutionSnapshot =
                gBrainOwnedRuntimeState.transceiverSnapshot.get();
        }
        RecordOperationalServiceCall(
            activationDecision,
            xvatsim::brain::BrainOwnedOperationalServiceStage::Authority);
        const auto authorityRelevanceSnapshot =
            RefreshBrainAuthorityRelevanceSnapshot(
                aircraftState,
                controllerFeedSnapshot,
                *routePolygonOutput.route,
                *transceiverResolutionSnapshot,
                planKey,
                &diagnostics);

        const auto departureTerminalAuthority =
            xvatsim::brain::RefreshBrainOwnedDepartureTerminalAuthority(
                &gBrainOwnedRuntimeState,
                flightContext,
                CurrentTickSeconds(),
                &gTerminalAuthorityResolver);
        const auto arrivalTerminalAuthority =
            xvatsim::brain::RefreshBrainOwnedArrivalTerminalAuthority(
                &gBrainOwnedRuntimeState,
                flightContext,
                CurrentTickSeconds(),
                &gTerminalAuthorityResolver);
        (void)arrivalTerminalAuthority;

        xvatsim::brain::BrainOwnedWorkflowSelectionInput workflowInput;
        workflowInput.aircraft = aircraftState;
        workflowInput.radios = radioStateSnapshot;
        workflowInput.radioSnapshot = radioSnapshot;
        workflowInput.departureIcao = flightContext.departureIcao;
        workflowInput.arrivalIcao = flightContext.destinationIcao;
        workflowInput.departureTerminalAuthority =
            departureTerminalAuthority;
        workflowInput.nowSeconds = XPLMGetElapsedTime();
        workflowInput.tuning.arrivalWakeDistanceNm = kArrivalWakeDistanceNm;
        workflowInput.tuning.departureReleaseHoldSeconds =
            kDepartureReleaseHoldSeconds;
        const auto workflowOutput =
            xvatsim::brain::ResolveBrainOwnedWorkflowSelection(
                &gBrainOwnedRuntimeState,
                workflowInput);
        workflowDecision = workflowOutput.decision;
        diagnostics.stage = WorkflowStageToken(workflowDecision.stage);
        diagnostics.stageReason = workflowDecision.reason;
        LogRadioBoardCandidateDiffTrace(
            workflowDecision.stage,
            planKey,
            radioSnapshot,
            gBrainOwnedRuntimeState.radioDiff);

        const auto* relevanceRadioSnapshot = &radioSnapshot;
        if (gPluginSettings.terminalRelevanceV2Enabled &&
            (workflowDecision.stage ==
                 xvatsim::brain::WorkflowStage::Departure ||
             workflowDecision.stage ==
                 xvatsim::brain::WorkflowStage::Arrival)) {
            if (!gTerminalEvidenceRadioCache.valid ||
                gTerminalEvidenceRadioCache.baseRadioHash !=
                    radioSnapshot.stableHash ||
                gTerminalEvidenceRadioCache.transceiverIdentity !=
                    transceiverResolutionSnapshot) {
                gTerminalEvidenceRadioCache.augmented =
                    xvatsim::brain::
                        AugmentRadioReachableControllerSnapshotWithAppDepEvidence(
                            radioSnapshot, *transceiverResolutionSnapshot);
                gTerminalEvidenceRadioCache.baseRadioHash =
                    radioSnapshot.stableHash;
                gTerminalEvidenceRadioCache.transceiverIdentity =
                    transceiverResolutionSnapshot;
                gTerminalEvidenceRadioCache.valid = true;
            }
            relevanceRadioSnapshot = &gTerminalEvidenceRadioCache.augmented;
        }
        gatedRadioSnapshot =
            xvatsim::brain::RunBrainOwnedRadioPhaseGate(
                &gBrainOwnedRuntimeState,
                *relevanceRadioSnapshot,
                workflowDecision.stage,
                "engineer3-clean-runtime");

        timingStarted = std::chrono::steady_clock::now();
        // vNAS only applies to the endpoint currently being resolved. This
        // keeps a US departure from enabling vNAS while resolving a Canadian
        // arrival (and vice versa).
        const auto vnasDepartureIcao =
            workflowDecision.stage == xvatsim::brain::WorkflowStage::Departure
                ? flightContext.departureIcao
                : std::string{};
        const auto vnasArrivalIcao =
            workflowDecision.stage == xvatsim::brain::WorkflowStage::Arrival
                ? flightContext.destinationIcao
                : std::string{};
        const auto vnasTerminalEvidence = gVnasDataClient.Poll(
            vnasDepartureIcao,
            vnasArrivalIcao,
            gatedRadioSnapshot.candidates,
            gPluginSettings.terminalRelevanceV2Enabled);
        diagnostics.vnasFeedUs = ElapsedMicrosecondsSince(timingStarted);
        diagnostics.vnasFeedMs = diagnostics.vnasFeedUs / 1000;
        diagnostics.vnasStatus =
            vnasTerminalEvidence != nullptr
                ? vnasTerminalEvidence->statusLine
                : "vnas-no-snapshot";

        xvatsim::brain::BrainOwnedControllerRelevanceInputRequest
            relevanceRequest;
        relevanceRequest.workflowStage = workflowDecision.stage;
        relevanceRequest.radioSnapshot = gatedRadioSnapshot;
        relevanceRequest.departureIcao = flightContext.departureIcao;
        relevanceRequest.arrivalIcao = flightContext.destinationIcao;
        relevanceRequest.hasDepartureCoordinates =
            flightContext.hasDepartureCoordinates;
        relevanceRequest.departureLatitudeDeg = flightContext.departureLatDeg;
        relevanceRequest.departureLongitudeDeg = flightContext.departureLonDeg;
        relevanceRequest.hasArrivalCoordinates =
            flightContext.hasDestinationCoordinates;
        relevanceRequest.arrivalLatitudeDeg = flightContext.destinationLatDeg;
        relevanceRequest.arrivalLongitudeDeg = flightContext.destinationLonDeg;
        relevanceRequest.terminalRelevanceV2Enabled =
            gPluginSettings.terminalRelevanceV2Enabled;
        relevanceRequest.vnasSectorPrecedenceEnabled =
            gPluginSettings.terminalRelevanceV2Enabled &&
            gPluginSettings.vnasSectorPrecedenceEnabled;
        relevanceRequest.terminalTransmitterRadiusNm =
            kTerminalTransmitterRadiusNm;
        relevanceRequest.vnasTerminalEvidenceHash =
            vnasTerminalEvidence != nullptr
                ? vnasTerminalEvidence->stableHash
                : 0;
        relevanceRequest.vnasTerminalEvidence = vnasTerminalEvidence;
        relevanceRequest.authorityRelevanceHash =
            authorityRelevanceSnapshot ==
                    gAuthorityAsyncRuntime.acceptedSnapshot
                ? gAuthorityAsyncRuntime.acceptedSnapshotDigest
                : xvatsim::brain::HashBrainAuthorityRelevanceSnapshot(
                      *authorityRelevanceSnapshot);
        relevanceRequest.authorityRelevance = authorityRelevanceSnapshot;
        relevanceRequest.radios = radioStateSnapshot;
        const auto relevanceInput =
            xvatsim::brain::BuildBrainOwnedControllerRelevanceInput(
                gBrainOwnedRuntimeState,
                relevanceRequest);
        RecordOperationalServiceCall(
            activationDecision,
            xvatsim::brain::BrainOwnedOperationalServiceStage::ControllerRelevance);
        auto relevanceOutput =
            RefreshBrainControllerRelevance(
                relevanceInput,
                planKey,
                true);
        RecordOperationalServiceCall(
            activationDecision,
            xvatsim::brain::BrainOwnedOperationalServiceStage::WorkflowPublication);
        publisherOutput = RunBrainPublisher(
            workflowDecision.stage,
            relevanceOutput,
            departureCtafLookup,
            arrivalCtafLookup,
            radioStateSnapshot,
            planKey);
        hasPublisherOutput = true;
        finalDisplaySnapshot = publisherOutput.finalDisplay;

        if (DiagnosticJobsEnabled()) {
            RecordDiagnosticJob(
                "Engineer3Runtime",
                "clean-runtime-no-old-authority-path",
                0,
                "old-authority-quarantined",
                "routeResolve=0,airportCoverage=0,authorityProof=0,enrouteCollect=0,arrivalCollect=0,noHeavyFallback=1",
                {},
                planKey);
        }
    }

    const auto workflowStage = workflowDecision.stage;
    if (diagnostics.stage.empty()) {
        diagnostics.stage = WorkflowStageToken(workflowStage);
        diagnostics.stageReason = workflowDecision.reason;
    }
    RecordOperationalServiceCall(
        activationDecision,
        xvatsim::brain::BrainOwnedOperationalServiceStage::Atis);
    const auto atisDecision = RunAtisCycleFromSharedFeed(
        vatsimDataFeedSnapshot,
        xPilotSessionSnapshot.connected,
        workflowStage);
    (void)atisDecision;
    RecordOperationalServiceCall(
        activationDecision,
        xvatsim::brain::BrainOwnedOperationalServiceStage::Metar);
    const auto asyncFactOutput = RunBoundAsyncFactCycle(
        xPilotSessionSnapshot.connected,
        workflowStage);
    LogMetarAsyncDiagnostics(asyncFactOutput);
    const auto enrouteInitialHoldActive =
        UpdateEnrouteInitialDisplayHold(workflowStage);

    timingStarted = std::chrono::steady_clock::now();
    RecordOperationalServiceCall(
        activationDecision,
        xvatsim::brain::BrainOwnedOperationalServiceStage::StandbyAssist);
    ApplyStandbyRecommendation(
        workflowStage,
        effectiveNetworkPlanSnapshot,
        radioStateSnapshot,
        publisherOutput.ctafUnicomStandbyAdvisoryCandidates,
        &finalDisplaySnapshot);
    diagnostics.standbyAssistUs = ElapsedMicrosecondsSince(timingStarted);

    if (hasPublisherOutput) {
        xvatsim::brain::CommitBrainOwnedPublishedRuntimeFromPublisherOutput(
            &gBrainOwnedRuntimeState,
            workflowDecision.stage,
            planKey,
            gatedRadioSnapshot,
            publisherOutput,
            finalDisplaySnapshot);
    }

    timingStarted = std::chrono::steady_clock::now();
    UpdateOverlayWakeTracking(
        aircraftState,
        xPilotSessionSnapshot);
    const auto textEntryActive =
        xvatsim::brain::HasBrainOwnedPendingTextEntry(gBrainOwnedRuntimeState);
    xvatsim::brain::BrainOwnedOverlayWakeInput wakeInput;
    wakeInput.aircraftState = aircraftState;
    wakeInput.xPilotSession = xPilotSessionSnapshot;
    wakeInput.workflowStage = workflowStage;
    wakeInput.finalDisplay = finalDisplaySnapshot;
    wakeInput.displayOverrideMode = gBrainOwnedRuntimeState.displayOverrideMode;
    wakeInput.manualQueryVisible =
        gBrainOwnedRuntimeState.manualQuerySnapshot.visible;
    wakeInput.textEntryActive = textEntryActive;
    wakeInput.sawXPilotConnectedThisFlight =
        gBrainOwnedRuntimeState.sawXPilotConnectedThisFlight;
    wakeInput.enrouteInitialHoldActive = enrouteInitialHoldActive;
    const auto wakeDecision =
        xvatsim::brain::DecideBrainOwnedOverlayWake(wakeInput);
    const auto updateNoticeWake =
        xvatsim::brain::OverlayUpdateRequestsWake(updateSnapshot);
    const auto shouldWakeOverlay =
        wakeDecision.shouldWake || updateNoticeWake ||
        gBrainOwnedRuntimeState.accessory.activeDrawer !=
            xvatsim::brain::BrainOwnedAccessoryDrawerId::None;
    diagnostics.shouldWake = shouldWakeOverlay;

    gOverlayWindow.SetAutomaticMode(
        gBrainOwnedRuntimeState.displayOverrideMode ==
        xvatsim::brain::BrainOwnedDisplayOverrideMode::Auto);

    diagnostics.wakeReason =
        (updateNoticeWake && !wakeDecision.shouldWake)
            ? "update-notice"
            : wakeDecision.reason;
    diagnostics.wakeDecisionUs = ElapsedMicrosecondsSince(timingStarted);

    if (!shouldWakeOverlay) {
        xvatsim::brain::OverlayViewModel overlayModel;
        overlayModel.mode = xvatsim::brain::OverlayMode::Dormant;
        overlayModel.visible = false;

        if (wakeDecision.hideUntilXpilotConnect) {
            timingStarted = std::chrono::steady_clock::now();
            DiscardPendingAccessoryClickFacts();
            xvatsim::brain::CloseBrainOwnedAccessoryForTemporaryOverlaySleep(
                &gBrainOwnedRuntimeState);
            gOverlayWindow.Hide();
            diagnostics.overlayUpdateUs = ElapsedMicrosecondsSince(timingStarted);
            diagnostics.overlayUpdateMs = diagnostics.overlayUpdateUs / 1000;
            if (DiagnosticJobsEnabled()) {
                RecordDiagnosticJob(
                    "OverlayUpdate",
                    "hide-until-xpilot-connect",
                    diagnostics.overlayUpdateMs,
                    "ui-update",
                    FormatOverlayUpdateResult(false),
                    {},
                    diagnostics.route);
            }
            timingStarted = std::chrono::steady_clock::now();
            PersistOverlayGeometryIfChanged();
            diagnostics.displayLoggingUs += ElapsedMicrosecondsSince(timingStarted);
            return;
        }

        timingStarted = std::chrono::steady_clock::now();
        UpdateOverlayWindow(overlayModel);
        diagnostics.overlayUpdateUs = ElapsedMicrosecondsSince(timingStarted);
        diagnostics.overlayUpdateMs = diagnostics.overlayUpdateUs / 1000;
        if (DiagnosticJobsEnabled()) {
            RecordDiagnosticJob(
                "OverlayUpdate",
                "dormant-model",
                diagnostics.overlayUpdateMs,
                "ui-update",
                FormatOverlayUpdateResult(false),
                {},
                diagnostics.route);
        }
        timingStarted = std::chrono::steady_clock::now();
        PersistOverlayGeometryIfChanged();
        diagnostics.displayLoggingUs += ElapsedMicrosecondsSince(timingStarted);
        return;
    }

    timingStarted = std::chrono::steady_clock::now();
    auto overlayModel = xvatsim::brain::BrainOrchestrator::BuildOverlayViewModel(
        workflowStage,
        aircraftState,
        xPilotSessionSnapshot,
        radioStateSnapshot,
        effectiveNetworkPlanSnapshot,
        controllerFeedSnapshot,
        *transceiverResolutionSnapshot,
        finalDisplaySnapshot,
        gBrainOwnedRuntimeState.manualQuerySnapshot,
        updateSnapshot);
    diagnostics.overlayBuildUs = ElapsedMicrosecondsSince(timingStarted);
    diagnostics.overlayBuildMs = diagnostics.overlayBuildUs / 1000;
    if (DiagnosticJobsEnabled()) {
        RecordDiagnosticJob(
            "OverlayBuild",
            "build-view-model",
            diagnostics.overlayBuildMs,
            "ui-model-build",
            std::string("mode=") + WorkflowStageToken(workflowStage),
            {},
            diagnostics.route);
    }
    const auto cruiseHeaderText =
        xvatsim::brain::BuildBrainOwnedCruiseTargetHeaderText(
            gBrainOwnedRuntimeState);
    if (!cruiseHeaderText.empty()) {
        overlayModel.headerRightText = cruiseHeaderText;
    }
    overlayModel.showMessageAcknowledge = false;
    overlayModel.showMessageRecall = false;

    timingStarted = std::chrono::steady_clock::now();
    UpdateOverlayWindow(overlayModel);
    diagnostics.overlayUpdateUs = ElapsedMicrosecondsSince(timingStarted);
    diagnostics.overlayUpdateMs = diagnostics.overlayUpdateUs / 1000;
    if (DiagnosticJobsEnabled()) {
        RecordDiagnosticJob(
            "OverlayUpdate",
            "visible-model",
            diagnostics.overlayUpdateMs,
            "ui-update",
            FormatOverlayUpdateResult(true),
            {},
            diagnostics.route);
    }
    timingStarted = std::chrono::steady_clock::now();
    PersistOverlayGeometryIfChanged();
    diagnostics.displayLoggingUs += ElapsedMicrosecondsSince(timingStarted);
}

void RefreshOverlayFromBrain() {
    RefreshOverlayFromBrainEngineer3();
}


float FlightLoopCallback(
    float elapsedSinceLastCall,
    float elapsedTimeSinceLastFlightLoop,
    int counter,
    void* refcon) {
    (void)elapsedSinceLastCall;
    (void)elapsedTimeSinceLastFlightLoop;
    (void)counter;
    (void)refcon;

    const auto callbackStarted = std::chrono::steady_clock::now();

    const bool accessoryPendingAtEntry =
        gOverlayWindow.HasPendingAccessoryWork() ||
        gOverlayWindow.GetAccessoryInputNotificationSequence() !=
            gAccessoryInputAccounting.lastObservedNotificationSequence;
    if (accessoryPendingAtEntry) {
        (void)ServicePendingAccessoryInput();
        ++gAccessoryInputAccounting.activeCadenceReturns;
        const auto cadence =
            xvatsim::modules::overlay::ResolveAccessoryFlightLoopCadence(
                true, kUpdateIntervalSeconds);
        xvatsim::modules::runtime_workers::FlightLoopTimingRecord timing;
        timing.sequence = ++gFlightLoopTimingSequence;
        timing.completeBeforeTelemetryUs =
            ElapsedMicrosecondsSince(callbackStarted);
        timing.accessoryPath = true;
        timing.activeCadence = true;
        (void)gDiagnosticsWriter.TryEnqueueFlightLoopTiming(timing);
        return cadence.intervalSeconds;
    }

    const auto refreshStarted = std::chrono::steady_clock::now();
    RefreshOverlayFromBrain();
    const auto refreshElapsedUs = ElapsedMicrosecondsSince(refreshStarted);
    const auto refreshElapsedMs = refreshElapsedUs / 1000;
    const auto diagnosticsStarted = std::chrono::steady_clock::now();
    MaybeLogRefreshDiagnostics(refreshElapsedMs, refreshElapsedUs);
    const auto diagnosticsSubmissionUs =
        ElapsedMicrosecondsSince(diagnosticsStarted);
    if (refreshElapsedMs >= 40) {
        const auto nowSeconds = CurrentTickSeconds();
        if ((nowSeconds -
             gDiagnosticsState.lastFlightLoopPerfWarningSeconds) >= 30) {
            gDiagnosticsState.lastFlightLoopPerfWarningSeconds = nowSeconds;
            std::ostringstream stream;
            stream << "[XVatsim] Perf warning: refresh took "
                   << refreshElapsedMs
                   << "ms\n";
            XPLMDebugString(stream.str().c_str());
        }
    }
    const auto activeCadence = gOverlayWindow.HasPendingAccessoryWork();
    if (activeCadence) {
        ++gAccessoryInputAccounting.activeCadenceReturns;
    } else {
        ++gAccessoryInputAccounting.normalCadenceReturns;
    }
    const auto cadence =
        xvatsim::modules::overlay::ResolveAccessoryFlightLoopCadence(
            activeCadence, kUpdateIntervalSeconds);
    xvatsim::modules::runtime_workers::FlightLoopTimingRecord timing;
    timing.sequence = ++gFlightLoopTimingSequence;
    timing.completeBeforeTelemetryUs =
        ElapsedMicrosecondsSince(callbackStarted);
    timing.refreshUs = static_cast<std::uint64_t>(refreshElapsedUs);
    timing.diagnosticsSubmissionUs =
        static_cast<std::uint64_t>(diagnosticsSubmissionUs);
    timing.activeCadence = activeCadence;
    (void)gDiagnosticsWriter.TryEnqueueFlightLoopTiming(timing);
    return cadence.intervalSeconds;
}

void RegisterFlightLoop(float initialDelaySeconds = kUpdateIntervalSeconds) {
    if (gFlightLoopRegistered) {
        return;
    }

    XPLMRegisterFlightLoopCallback(FlightLoopCallback, initialDelaySeconds, nullptr);
    gFlightLoopRegistered = true;
}

void UnregisterFlightLoop() {
    if (!gFlightLoopRegistered) {
        return;
    }

    XPLMUnregisterFlightLoopCallback(FlightLoopCallback, nullptr);
    gFlightLoopRegistered = false;
}

void PreinitializeOverlayWindow() {
    const auto started = std::chrono::steady_clock::now();
    gOverlayWindow.Create();
    const auto elapsedUs = ElapsedMicrosecondsSince(started);
    AppendDiagnosticsLogLine(
        std::string{"event=overlay-preinit totalMs="} +
        std::to_string(elapsedUs / 1000) +
        " totalUs=" + std::to_string(elapsedUs) +
        " cache=window-create-hidden result=createAttempt=1");
}

void LogAccessoryPreparationStartupFailure(
    xvatsim::modules::overlay::AccessoryPreparationWorkerFailure failure,
    void*) {
    AppendDiagnosticsLogLine(
        std::string{"event=step3-accessory-preparation-startup-failed reason="} +
        xvatsim::modules::overlay::AccessoryPreparationWorkerFailureToken(
            failure));
}
}

PLUGIN_API int XPluginStart(char* outName, char* outSig, char* outDesc) {
    gPluginRuntimeEnabled = false;
    gOperationalActivationState = {};
    if (!gAuthorityRelevanceWorker.Start()) {
        XPLMDebugString(
            "[XVatsim] Authority worker startup failed; plugin not loaded.\n");
        return 0;
    }
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);
    char xplaneSystemPath[1024] = {};
    XPLMGetSystemPath(xplaneSystemPath);
    if (!gRoutePreparationWorker.Start(xplaneSystemPath)) {
        XPLMDebugString(
            "[XVatsim] Route worker startup failed; plugin not loaded.\n");
        gAuthorityRelevanceWorker.CancelAndJoin();
        return 0;
    }
    gRouteSectorResolver.StartSourceRefreshBeforeFlightLoop();
    xvatsim::modules::runtime_workers::DiagnosticsWriterOptions
        diagnosticsWriterOptions;
    diagnosticsWriterOptions.logDirectory = ResolvePluginRootPath() / "logs";
    diagnosticsWriterOptions.retainedDateLogCount =
        kDiagnosticsRetainedDateLogCount;
    if (!gDiagnosticsWriter.Start(std::move(diagnosticsWriterOptions))) {
        XPLMDebugString(
            "[XVatsim] Diagnostics writer startup failed; "
            "diagnostics disabled.\n");
    }
    gOverlayWindow.SetAccessoryInputWakeCallback(
        RequestAccessoryFlightLoopWake, nullptr);
    gOverlayWindow.SetAccessoryPreparationFailureCallback(
        LogAccessoryPreparationStartupFailure, nullptr);

    std::strcpy(outName, kPluginName);
    std::strcpy(outSig, kPluginSig);
    std::strcpy(outDesc, kPluginDesc);

    gSettingsStore.SetPath(ResolveSettingsPath());
    gPluginSettings = gSettingsStore.Load();
    xvatsim::brain::BrainOwnedOperatingModeInitializationInput
        operatingModeInput;
    operatingModeInput.loadStatus = ToBrainOperatingModeLoadStatus(
        gPluginSettings.operatingModeLoadStatus);
    operatingModeInput.storedMode =
        ToBrainOperatingMode(gPluginSettings.operatingMode);
    xvatsim::brain::InitializeBrainOwnedOperatingMode(
        &gBrainOwnedRuntimeState,
        operatingModeInput);
    xvatsim::brain::SetBrainOwnedDisplayOverrideMode(
        &gBrainOwnedRuntimeState,
        ToDisplayOverrideMode(gPluginSettings.displayMode));
    gOverlayWindow.SetTransitionSoundPath(ResolvePluginAssetPath("ui_transition.mp3"));
    LoadPreflightRouteCacheCandidate();
    ApplyOverlayAppearanceSettings();
    if (gPluginSettings.hasWindowPosition) {
        gOverlayWindow.SetWindowTopLeft(
            gPluginSettings.windowLeft,
            gPluginSettings.windowTop);
    }
    ResetPluginRuntimeState(true, true);
    xvatsim::brain::InitializeBrainOwnedPdcRuntime(&gBrainOwnedRuntimeState);
#if defined(XVATSIM_STEP3_LIVE_PROOF_FIXTURES)
    const auto fixtureSeed =
        xvatsim::plugin::step3_live_proof::SeedStep3LiveProofFixturesOnce(
            &gBrainOwnedRuntimeState,
            &gStep3LiveProofFixtureSession);
    if (!fixtureSeed.seeded) {
        XPLMDebugString(
            "[XVatsim] Step 3 live-proof fixture seeding failed.\n");
    }
#endif
    AppendDiagnosticsLogLine(
        std::string{"event=diagnostics-session-start version="} +
        kInstalledPluginVersion +
        " logPolicy=date-stamped-daily-retention retainedDates=" +
        std::to_string(kDiagnosticsRetainedDateLogCount) +
        " activeLogCap=bounded-1024",
        xvatsim::modules::runtime_workers::DiagnosticsImportance::Critical);
    AppendDiagnosticsLogLine(
        std::string{"event=operating-mode-initialized effective="} +
        xvatsim::brain::ToString(
            gBrainOwnedRuntimeState.operatingMode.mode) +
        " stateSource=" +
        xvatsim::brain::ToString(
            gBrainOwnedRuntimeState.operatingMode.source) +
        " stateReason=" +
        gBrainOwnedRuntimeState.operatingMode.reason +
        " generation=" +
        std::to_string(
            gBrainOwnedRuntimeState.operatingMode.generation));
    AppendDiagnosticsLogLine(
        std::string{"event=terminal-relevance-policy policy="} +
            (gPluginSettings.terminalRelevanceV2Enabled
                 ? "equal-source-v2"
                 : "legacy") +
            " endpointTransmitterRadiusNm=" +
            std::to_string(kTerminalTransmitterRadiusNm) +
            " vnas=us-only vnasSectorPrecedence=" +
            (gPluginSettings.vnasSectorPrecedenceEnabled ? "enabled"
                                                         : "disabled") +
            " rollbackSettings=terminal_relevance_v2:false|vnas_sector_precedence:false",
        xvatsim::modules::runtime_workers::DiagnosticsImportance::Critical);
    RegisterPluginCommands();
    RegisterPluginMenu();

    const auto overlayModel =
        xvatsim::brain::BrainOrchestrator::BuildOverlayViewModel(
            xvatsim::brain::WorkflowStage::None,
            xvatsim::brain::AircraftStateSnapshot{},
            xvatsim::brain::XPilotSessionSnapshot{},
            xvatsim::brain::RadioStateSnapshot{},
            xvatsim::brain::NetworkPlanSnapshot{},
            xvatsim::brain::ControllerFeedSnapshot{},
            xvatsim::brain::TransceiverResolutionSnapshot{},
            xvatsim::brain::FinalDisplaySnapshot{},
            xvatsim::brain::ManualQuerySnapshot{});
    std::string readyMessage = "[XVatsim] Display ready: " + overlayModel.title + "\n";
    XPLMDebugString(readyMessage.c_str());

    XPLMDebugString("[XVatsim] Plugin loaded.\n");
    return 1;
}

PLUGIN_API void XPluginStop() {
    gPluginRuntimeEnabled = false;
    UnregisterFlightLoop();
    InvalidateRouteAsyncRuntime("plugin-stop");
    PersistOverlayGeometryIfChanged();
    DiscardPendingAccessoryClickFacts();
    gOverlayWindow.Hide();
    DrainAccessoryPublicationFacts();
    LogAccessoryPerformanceSnapshot("plugin-stop");
    xvatsim::brain::StopBrainOwnedAccessoryRuntime(
        &gBrainOwnedRuntimeState);
    gXPilotPrivateObservationQueue.Clear();
    (void)ApplyBoundAsyncWorkerLifecycleBoundary(true);
    DrainDeferredRouteRetirementsForStop();
    gRoutePreparationWorker.CancelAndJoin();
    gRouteSectorResolver.StopSourceRefreshAfterFlightLoop();
    ResetPluginRuntimeState(true, true);
    gAuthorityRelevanceWorker.CancelAndJoin();
    gRouteSectorResolver.ResetSourceCaches();
    gOverlayWindow.Destroy();
    gOverlayWindow.SetAccessoryInputWakeCallback(nullptr, nullptr);
    UnregisterPluginMenu();
    UnregisterPluginCommands();
    {
        const auto routeWorkerSnapshot = gRoutePreparationWorker.Snapshot();
        std::ostringstream stream;
        stream << "event=route-worker-stop"
               << " started=" << (routeWorkerSnapshot.started ? 1 : 0)
               << " running=" << (routeWorkerSnapshot.running ? 1 : 0)
               << " pending=" << (routeWorkerSnapshot.pending ? 1 : 0)
               << " completed=" << (routeWorkerSnapshot.completed ? 1 : 0)
               << " starts=" << routeWorkerSnapshot.starts
               << " replacements=" << routeWorkerSnapshot.replacements
               << " completions=" << routeWorkerSnapshot.completions
               << " cancellations=" << routeWorkerSnapshot.cancellations
               << " failures=" << routeWorkerSnapshot.failures
               << " fmsObservations="
               << routeWorkerSnapshot.fmsObservations
               << " fmsObservationChanges="
               << routeWorkerSnapshot.fmsObservationChanges
               << " staleRetirements="
               << routeWorkerSnapshot.staleRetirements
               << " maxPendingDepth="
               << routeWorkerSnapshot.maximumPendingDepth
               << " requestNodesInUse="
               << routeWorkerSnapshot.requestNodesInUse
               << " requestNodesPeak="
               << routeWorkerSnapshot.requestNodesPeak
               << " leasesCreated=" << routeWorkerSnapshot.leasesCreated
               << " leasesRetired=" << routeWorkerSnapshot.leasesRetired
               << " leasesDestroyedOnWorker="
               << routeWorkerSnapshot.leasesDestroyedOnWorker
               << " outstandingLeases="
               << routeWorkerSnapshot.outstandingLeases
               << " peakRetainedRouteItems="
               << routeWorkerSnapshot.peakRetainedRouteItems
               << " retainedRouteBytes="
               << routeWorkerSnapshot.retainedRouteBytes
               << " peakRetainedRouteBytes="
               << routeWorkerSnapshot.peakRetainedRouteBytes
               << " sourceDatasetObservations="
               << routeWorkerSnapshot.sourceDatasetObservations
               << " sourceDatasetObservationRejections="
               << routeWorkerSnapshot.sourceDatasetObservationRejections
               << " retainedSourceDatasets="
               << routeWorkerSnapshot.retainedSourceDatasets
               << " peakRetainedSourceDatasets="
               << routeWorkerSnapshot.peakRetainedSourceDatasets
               << " retirementBacklogPeak="
               << gRouteAsyncRuntime.retirementBacklogPeak
               << " retirementBacklogRemaining="
               << (HasDeferredRouteRetirements() ? 1 : 0)
               << " dispatches=" << gRouteAsyncRuntime.dispatchCount
               << " fmsObservationDispatches="
               << gRouteAsyncRuntime.fmsObservationDispatchCount
               << " fmsObservationAccepted="
               << gRouteAsyncRuntime.fmsObservationAcceptedCount
               << " fmsObservationChanged="
               << gRouteAsyncRuntime.fmsObservationChangedCount
               << " unresolvedRetryDispatches="
               << gRouteAsyncRuntime.unresolvedRetryDispatchCount
               << " accepted=" << gRouteAsyncRuntime.acceptedCount
               << " staleRejected="
               << gRouteAsyncRuntime.staleRejectedCount
               << " invalidations=" << gRouteAsyncRuntime.invalidationCount
               << " submitMaxUs=" << gRouteAsyncRuntime.maximumSubmitUs
               << " mailboxExchangeMaxUs="
               << gRouteAsyncRuntime.maximumMailboxExchangeUs
               << " harvestMaxUs=" << gRouteAsyncRuntime.maximumHarvestUs
               << " transitionMaxUs="
               << gRouteAsyncRuntime.maximumTransitionUs;
        AppendDiagnosticsLogLine(
            stream.str(),
            xvatsim::modules::runtime_workers::DiagnosticsImportance::Critical);
    }
    LogOperationalActivationSummary();
    const auto diagnosticsDrainedBeforeStop =
        gDiagnosticsWriter.WaitUntilIdle(std::chrono::seconds(5));
    const auto writerSnapshot = gDiagnosticsWriter.Snapshot();
    {
        std::ostringstream stream;
        stream << "event=diagnostics-writer-stop"
               << " submittedRoutine=" << writerSnapshot.submittedRoutine
               << " submittedCritical=" << writerSnapshot.submittedCritical
               << " submittedTiming="
               << writerSnapshot.submittedFlightLoopTiming
               << " submittedRouteEvidence="
               << writerSnapshot.submittedRouteEvidence
               << " dequeued=" << writerSnapshot.dequeued
               << " dequeuedTiming="
               << writerSnapshot.dequeuedFlightLoopTiming
               << " dequeuedRouteEvidence="
               << writerSnapshot.dequeuedRouteEvidence
               << " written=" << writerSnapshot.written
               << " writtenTiming="
               << writerSnapshot.writtenFlightLoopTiming
               << " writtenRouteEvidence="
               << writerSnapshot.writtenRouteEvidence
               << " droppedContention="
               << writerSnapshot.droppedContention
               << " droppedRoutineContention="
               << writerSnapshot.droppedRoutineContention
               << " droppedCriticalContention="
               << writerSnapshot.droppedCriticalContention
               << " droppedRoutineFull="
               << writerSnapshot.droppedRoutineFull
               << " droppedCriticalFull="
               << writerSnapshot.droppedCriticalFull
               << " droppedTimingFull="
               << writerSnapshot.droppedFlightLoopTimingFull
               << " droppedRouteEvidenceFull="
               << writerSnapshot.droppedRouteEvidenceFull
               << " rejectedTimingNotRunning="
               << writerSnapshot.rejectedFlightLoopTimingNotRunning
               << " rejectedRouteEvidenceNotRunning="
               << writerSnapshot.rejectedRouteEvidenceNotRunning
               << " formattingFailures="
               << writerSnapshot.formattingFailures
               << " storageFailures=" << writerSnapshot.storageFailures
               << " maxDepth=" << writerSnapshot.maximumQueueDepth
               << " maxTimingDepth="
               << writerSnapshot.maximumFlightLoopTimingQueueDepth
               << " maxRouteEvidenceDepth="
               << writerSnapshot.maximumRouteEvidenceQueueDepth
               << " producerMaxUs="
               << writerSnapshot.maximumProducerMicroseconds
               << " timingProducerMaxUs="
               << writerSnapshot.maximumFlightLoopTimingProducerMicroseconds
               << " routeEvidenceProducerMaxUs="
               << writerSnapshot.maximumRouteEvidenceProducerMicroseconds
               << " lastSubmittedRouteEvidenceSequence="
               << writerSnapshot.lastSubmittedRouteEvidenceSequence
               << " lastWrittenRouteEvidenceSequence="
               << writerSnapshot.lastWrittenRouteEvidenceSequence
               << " drainedBeforeStop="
               << (diagnosticsDrainedBeforeStop ? 1 : 0)
               << " workerRunning=" << (writerSnapshot.running ? 1 : 0);
        AppendDiagnosticsLogLine(
            stream.str(),
            xvatsim::modules::runtime_workers::DiagnosticsImportance::Critical);
    }
    gDiagnosticsWriter.Stop();
    XPLMDebugString("[XVatsim] Plugin stopped.\n");
}

PLUGIN_API int XPluginEnable() {
    const auto xPilotSessionSnapshot = gXPilotBridge.Poll();
    const auto pilotIdentitySnapshot =
        gPilotIdentityResolver.Resolve(xPilotSessionSnapshot);
    (void)HandleXPilotSessionBoundary(
        xPilotSessionSnapshot, pilotIdentitySnapshot);
    const auto resume =
        xvatsim::brain::ResumeBrainOwnedRuntimeFromPluginAdmin(
            &gBrainOwnedRuntimeState);
    LoadPreflightRouteCacheCandidate();
    // Source refresh may create or harvest its compiler thread.  Keep that
    // lifecycle work ahead of flight-loop registration so Gate B never moves
    // thread creation or joining into an X-Plane callback after re-enable.
    gRouteSectorResolver.StartSourceRefreshBeforeFlightLoop();
    xvatsim::brain::SetBrainOwnedDisplayOverrideMode(
        &gBrainOwnedRuntimeState,
        ToDisplayOverrideMode(gPluginSettings.displayMode));
    gPluginRuntimeEnabled = true;
    RequestAutomaticUpdateCheckIfDue();
    PreinitializeOverlayWindow();
    SynchronizeAccessoryPresentation();
    RegisterFlightLoop(kInitialFlightLoopDelaySeconds);
    const auto immediateActivationCheckRequested =
        RequestImmediateGatedFlightLoopCallback();
    {
        std::ostringstream stream;
        stream << "event=plugin-admin-resume"
               << " changed=" << (resume.stateChanged ? 1 : 0)
               << " generation=" << resume.suspensionGeneration
               << " immediateActivationCheckRequested="
               << (immediateActivationCheckRequested ? 1 : 0)
               << " flightContext="
               << (gBrainOwnedRuntimeState.flightContext.active ? 1 : 0)
               << " stage="
               << WorkflowStageToken(gBrainOwnedRuntimeState.lastWorkflowStage)
               << " callsign="
               << SanitizeLogText(
                      gBrainOwnedRuntimeState.flightContext.callsign, 32)
               << " primary="
               << SanitizeLogText(
                      gBrainOwnedRuntimeState.metar.primaryAirportIcao, 4);
        AppendDiagnosticsLogLine(stream.str());
    }
    XPLMDebugString("[XVatsim] Plugin enabled.\n");
    return 1;
}

PLUGIN_API void XPluginDisable() {
    gPluginRuntimeEnabled = false;
    xvatsim::brain::SetBrainOwnedOperationalActivationSuspended(
        &gOperationalActivationState);
    UnregisterFlightLoop();
    gXPilotPrivateObservationQueue.Clear();
    PersistOverlayGeometryIfChanged();
    DiscardPendingAccessoryClickFacts();
    gOverlayWindow.Hide();
    DrainAccessoryPublicationFacts();
    LogAccessoryPerformanceSnapshot("plugin-disable");
    DiscardPendingTextEntryState();
    xvatsim::brain::ClearBrainOwnedManualQuery(&gBrainOwnedRuntimeState);
    const auto suspension =
        xvatsim::brain::SuspendBrainOwnedRuntimeForPluginAdmin(
            &gBrainOwnedRuntimeState,
            gAsyncFactWorkerHost.Bindings());
    LogMetarLifecycleDiagnostics(suspension.workerShutdown);
    InvalidateRouteAsyncRuntime("plugin-admin-disable");
    InvalidateAuthorityAsyncRuntime("plugin-admin-disable");
    gVatsimDataFeedClient.Reset();
    gVnasDataClient.Reset();
    gNetworkPlanLink.Reset();
    gTransceiverResolver.Reset();
    gTerminalEvidenceRadioCache = {};
    gLastAccessorySemanticPresentationGeneration = 0;
    xvatsim::brain::SetBrainOwnedDisplayOverrideMode(
        &gBrainOwnedRuntimeState,
        ToDisplayOverrideMode(gPluginSettings.displayMode));
    {
        std::ostringstream stream;
        stream << "event=plugin-admin-suspend"
               << " changed=" << (suspension.stateChanged ? 1 : 0)
               << " generation=" << suspension.suspensionGeneration
               << " flightContext="
               << (gBrainOwnedRuntimeState.flightContext.active ? 1 : 0)
               << " stage="
               << WorkflowStageToken(gBrainOwnedRuntimeState.lastWorkflowStage)
               << " callsign="
               << SanitizeLogText(
                      gBrainOwnedRuntimeState.flightContext.callsign, 32)
               << " primary="
               << SanitizeLogText(
                      gBrainOwnedRuntimeState.metar.primaryAirportIcao, 4)
               << " workerRunning="
               << (suspension.workerShutdown.running ? 1 : 0);
        AppendDiagnosticsLogLine(stream.str());
    }
    XPLMDebugString("[XVatsim] Plugin disabled.\n");
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFrom, int inMessage, void* inParam) {
    (void)inFrom;
    (void)inMessage;
    (void)inParam;
}
