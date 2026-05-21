#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

#include "XVatsim/brain/BrainOrchestrator.h"
#include "XVatsim/brain/BrainDisplayIntent.h"
#include "XVatsim/brain/BrainOwnedRuntime.h"
#include "XVatsim/brain/BrainOwnedWorkerTypes.h"
#include "XVatsim/brain/RadioReachableSnapshot.h"
#include "XVatsim/brain/BrainWorkScheduler.h"
#include "XVatsim/brain/RoutePolygonTransition.h"
#include "XVatsim/core/PreflightRouteCache.h"
#include "XVatsim/core/WorkflowEngine.h"
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
#include "XVatsim/modules/settings_store/SettingsStore.h"
#include "XVatsim/modules/transceiver_resolver/TransceiverResolver.h"
#include "XVatsim/modules/vatsim_data_feed/VatsimDataFeedClient.h"
#include "XVatsim/modules/xpilot_bridge/XPilotBridge.h"
#include "XPLMMenus.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#ifndef XVATSIM_ENABLE_CONTROLLER_MESSAGES
#define XVATSIM_ENABLE_CONTROLLER_MESSAGES 0
#endif

namespace {
constexpr char kPluginName[] = "XVatsim";
constexpr char kPluginSig[] = "org.xvatsim.plugin";
constexpr char kPluginDesc[] = "XVatsim VATSIM workflow display for X-Plane 12.";
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
constexpr double kArrivalWakeDistanceNm = 200.0;
constexpr float kDepartureReleaseHoldSeconds = 180.0f;
constexpr float kEnrouteInitialDisplaySeconds = 180.0f;
constexpr float kOpacityStep = 0.10f;
constexpr float kScaleStep = 0.05f;
constexpr float kAnimationSpeedStep = 0.10f;
constexpr bool kControllerMessageUiEnabled = XVATSIM_ENABLE_CONTROLLER_MESSAGES != 0;
constexpr std::size_t kMaxLogFieldChars = 80;
constexpr long long kDiagnosticsSlowRefreshThresholdMs = 33;
constexpr long long kDiagnosticsSlowRefreshLogIntervalSeconds = 10;
constexpr long long kDiagnosticsSummaryIntervalSeconds = 30;
constexpr std::uintmax_t kDiagnosticsMaxLogBytes = 5ull * 1024ull * 1024ull;
constexpr long long kRadioBoardSnapshotCadenceSeconds = 1;
constexpr long long kRadioBoardPendingRouteRetrySeconds = 2;
constexpr long long kActiveFlightPlanSampleCadenceSeconds = 15;
constexpr long long kEngineer3RadioBoardRefreshSeconds = 5;

enum class DisplayOverrideMode {
    Auto,
    ForcedOpen,
    ForcedSleep,
};

enum class PendingTextEntryMode {
    None,
    ManualCtaf,
    DiversionAirport,
};

enum class SessionBoundaryResult {
    None,
    ResetForDisconnect,
    ResetForCallsignChange,
};

using FlightContext = xvatsim::core::workflow::FlightContext;
using HandoffDecision = xvatsim::core::workflow::HandoffDecision;

std::string FormatFixed(double value, int precision) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

struct PendingControllerMessageState {
    bool primed = false;
    int lastSequence = 0;
    bool visible = false;
    bool cachedAvailable = false;
    std::string from;
    std::string body;
};

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
    int controllerCount = 0;
    int authorityCount = 0;
    int enrouteStationCount = 0;
    std::string authorityProofSummary;
    bool hasAuthorityProofHash = false;
    std::size_t authorityProofHash = 0;
    long long xpilotPollMs = 0;
    long long vatsimFeedMs = 0;
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
    long long activeTransceiverResolveMs = 0;
    long long overlayBuildMs = 0;
    long long overlayUpdateMs = 0;
    long long aircraftStateUs = 0;
    long long xpilotPollUs = 0;
    long long vatsimFeedUs = 0;
    long long controllerFeedUs = 0;
    long long flightPlanUs = 0;
    long long networkPlanUs = 0;
    long long radioUs = 0;
    long long controllerMessageUs = 0;
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
    long long activeTransceiverResolveUs = 0;
    long long overlayBuildUs = 0;
    long long overlayUpdateUs = 0;
    long long displayLoggingUs = 0;
    std::vector<DiagnosticJobRecord> jobs;
};

xvatsim::brain::BrainOrchestrator gBrain;
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
xvatsim::modules::settings_store::SettingsStore gSettingsStore;
xvatsim::modules::transceiver_resolver::TransceiverResolver gTransceiverResolver;
xvatsim::modules::vatsim_data_feed::VatsimDataFeedClient gVatsimDataFeedClient;
xvatsim::modules::xpilot_bridge::XPilotBridge gXPilotBridge;
xvatsim::modules::settings_store::PluginSettings gPluginSettings;
xvatsim::brain::ManualQuerySnapshot gManualQuerySnapshot;
long long gManualQueryVisibleUntilSeconds = 0;
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
bool gFlightLoopRegistered = false;
bool gPluginRuntimeEnabled = false;
DisplayOverrideMode gDisplayOverrideMode = DisplayOverrideMode::Auto;
bool gCruiseAltitudeReachedThisFlight = false;
bool gCruiseTargetManualOverride = false;
bool gHasActiveCruiseTarget = false;
double gActiveCruiseTargetFt = 0.0;
bool gSawXPilotConnectedThisFlight = false;
bool gDepartureReleasedThisFlight = false;
bool gArrivalAwakeThisFlight = false;
float gAirborneSinceSeconds = -1.0f;
bool gEnrouteInitialDisplayStarted = false;
float gEnrouteInitialDisplayUntilSeconds = -1.0f;
FlightContext gFlightContext;
xvatsim::brain::AircraftStateSnapshot gLastAircraftStateSnapshot;
xvatsim::brain::PilotIdentitySnapshot gLastPilotIdentitySnapshot;
xvatsim::brain::FlightPlanSnapshot gLastFlightPlanSnapshot;
bool gHasRuntimeFlightPlanSnapshot = false;
long long gLastRuntimeFlightPlanSampleSeconds = 0;
xvatsim::brain::NetworkPlanSnapshot gLastNetworkPlanSnapshot;
xvatsim::brain::BrainOwnedRuntimeState gBrainOwnedRuntimeState;
float gCruiseGateSatisfiedSinceSeconds = -1.0f;
std::string gStandbyAssistLatchKey;
bool gStandbyAssistWriteConsumed = false;
long long gLastFlightLoopPerfWarningSeconds = 0;
long long gLastDiagnosticsSlowRefreshSeconds = 0;
long long gLastDiagnosticsSummarySeconds = 0;
bool gHasLastDiagnosticsAuthorityHash = false;
std::size_t gLastDiagnosticsAuthorityHash = 0;
bool gLastXPilotConnected = false;
bool gColdDarkResetApplied = false;
bool gAircraftStateInvalidBoundaryActive = false;
std::string gLastConnectedPilotCallsign;
std::string gDisconnectedPilotCallsign;
bool gPendingAutomaticFlightRecovery = false;
bool gManualFlightRecoveryRequested = false;
std::string gCruiseTargetSourceKey;
std::string gDiversionOverrideSourceKey;
PendingTextEntryMode gPendingTextEntryMode = PendingTextEntryMode::None;
PendingControllerMessageState gPendingControllerMessage;
RefreshDiagnosticsFrame gRefreshDiagnosticsFrame;
std::optional<xvatsim::core::preflight::PreflightRouteCache> gPreflightRouteCacheCandidate;
std::string gPreflightRouteCachePath;
std::string gPreflightRouteCacheAppliedPlanKey;

void RefreshOverlayFromBrain();
void RefreshOverlayFromBrainEngineer3();
void ResetPresentationStateForColdDark();
void ClearFlightRecoveryState();

std::string BuildNetworkPlanIdentityKey(
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot);
std::string SummarizeRouteAuthorityPlan(
    const xvatsim::brain::RouteAuthorityPlan& plan);
void ApplyPreflightRouteCacheForPlanIfNeeded(
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot);

long long CurrentTickSeconds() {
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
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

std::size_t HashRouteSectorMatch(const xvatsim::brain::RouteSectorMatchSnapshot& match) {
    std::size_t hash = 0;
    HashCombineString(&hash, match.identifier);
    HashCombineDouble(&hash, match.entryDistanceNm);
    for (const auto& token : match.matchTokens) {
        HashCombineString(&hash, token);
    }
    for (const auto& pattern : match.controllerCallsignPatterns) {
        HashCombineString(&hash, pattern);
    }
    for (const auto& prefix : match.controllerPrefixes) {
        HashCombineString(&hash, prefix);
    }
    HashCombineBool(&hash, match.centerCoverage);
    HashCombineBool(&hash, match.terminalCoverage);
    return hash;
}

std::size_t HashRouteSectorSnapshot(const xvatsim::brain::RouteSectorSnapshot& snapshot) {
    std::size_t hash = 0;
    HashCombineBool(&hash, snapshot.available);
    HashCombineBool(&hash, snapshot.stale);
    HashCombineBool(&hash, snapshot.routeResolved);
    HashCombineString(&hash, snapshot.statusLine);
    HashCombine(&hash, snapshot.centerBoundaryGeneration);
    HashCombine(&hash, snapshot.authorityCatalogGeneration);
    HashCombineString(&hash, snapshot.departureIcao);
    HashCombineString(&hash, snapshot.destinationIcao);
    HashCombine(&hash, snapshot.currentSectors.size());
    for (const auto& sector : snapshot.currentSectors) {
        HashCombine(&hash, HashRouteSectorMatch(sector));
    }
    HashCombine(&hash, snapshot.nextSectors.size());
    for (const auto& sector : snapshot.nextSectors) {
        HashCombine(&hash, HashRouteSectorMatch(sector));
    }
    return hash;
}

std::size_t HashRelevantAuthority(
    const xvatsim::brain::RelevantAuthoritySnapshot& authority) {
    std::size_t hash = 0;
    HashCombineString(&hash, authority.callsign);
    HashCombineString(&hash, authority.frequency);
    HashCombineString(&hash, authority.authorityId);
    HashCombineString(&hash, authority.polygonId);
    HashCombineString(&hash, authority.polygonKey);
    HashCombineString(&hash, authority.matchedPattern);
    HashCombineString(&hash, authority.proofSource);
    HashCombineString(&hash, authority.proofDetail);
    HashCombine(&hash, static_cast<std::size_t>(authority.kind));
    HashCombineBool(&hash, authority.aircraftInside);
    HashCombineBool(&hash, authority.routeIntersects);
    HashCombineDouble(&hash, authority.routeEntryDistanceNm);
    return hash;
}

std::size_t HashAuthorityRelevanceSnapshot(
    const xvatsim::brain::AuthorityRelevanceSnapshot& snapshot) {
    std::size_t hash = 0;
    HashCombineBool(&hash, snapshot.available);
    HashCombineBool(&hash, snapshot.stale);
    HashCombineString(&hash, snapshot.statusLine);
    HashCombine(&hash, snapshot.diagnostics.size());
    for (const auto& diagnostic : snapshot.diagnostics) {
        HashCombineString(&hash, diagnostic);
    }
    HashCombine(&hash, snapshot.relevantAuthorities.size());
    for (const auto& authority : snapshot.relevantAuthorities) {
        HashCombine(&hash, HashRelevantAuthority(authority));
    }
    return hash;
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

std::size_t HashRadioBoardInputs(const xvatsim::brain::RadioStateSnapshot& snapshot) {
    std::size_t hash = 0;
    HashCombineBool(&hash, snapshot.valid);
    HashCombineString(&hash, NormalizeFrequency(snapshot.com1ActiveFrequency));
    HashCombineString(&hash, NormalizeFrequency(snapshot.com2ActiveFrequency));
    return hash;
}

std::size_t HashCtafLookupEntry(
    const xvatsim::modules::ctaf_lookup::CtafLookupEntry& entry) {
    std::size_t hash = 0;
    HashCombineBool(&hash, entry.resolved);
    HashCombineBool(&hash, entry.available);
    HashCombineString(&hash, NormalizeFrequency(entry.frequency));
    return hash;
}

bool NeedsTransceiverResolution(
    xvatsim::brain::WorkflowStage workflowStage,
    const xvatsim::brain::XPilotSessionSnapshot& xPilotSessionSnapshot,
    const xvatsim::brain::ModuleBoardSnapshot& activeBoardSnapshot) {
    return xPilotSessionSnapshot.connected &&
           workflowStage != xvatsim::brain::WorkflowStage::Enroute &&
           !activeBoardSnapshot.available &&
           !activeBoardSnapshot.displayStations;
}

void ResetBrainDisplayPublisherCache() {
    xvatsim::brain::ResetBrainOwnedDisplayPublisherState(
        &gBrainOwnedRuntimeState);
}

void ResetBrainOwnedRuntimeCache() {
    xvatsim::brain::ResetBrainOwnedRuntimeState(&gBrainOwnedRuntimeState);
}

void DiscardPendingTextEntryState() {
    gOverlayWindow.CancelTextEntry();
    std::string discardedSubmission;
    (void)gOverlayWindow.ConsumeSubmittedText(&discardedSubmission);
    gPendingTextEntryMode = PendingTextEntryMode::None;
}

void ResetSessionRuntimeCaches(bool resetVatsimFeed) {
    gAircraftStateSampler.Reset();
    gCtafLookupService.Reset();
    gXPilotBridge.Reset();
    if (resetVatsimFeed) {
        gVatsimDataFeedClient.Reset();
    }
    gNetworkPlanLink.Reset();
    gFlightPlanSampler.Reset();
    gHasRuntimeFlightPlanSnapshot = false;
    gLastRuntimeFlightPlanSampleSeconds = 0;
    gRadioStateSampler.Reset();
    gRouteSectorResolver.ResetRuntimeState();
    gRouteSectorResolver.ClearPreflightRouteCache();
    ResetBrainOwnedRuntimeCache();
    gPreflightRouteCacheAppliedPlanKey.clear();
    gTransceiverResolver.Reset();
    ResetBrainDisplayPublisherCache();
}

void ResetPluginRuntimeState(bool resetVatsimFeed, bool resetColdDarkLatch) {
    DiscardPendingTextEntryState();
    gManualQuerySnapshot = {};
    gManualQueryVisibleUntilSeconds = 0;
    ClearFlightRecoveryState();
    ResetSessionRuntimeCaches(resetVatsimFeed);
    ResetPresentationStateForColdDark();
    if (resetColdDarkLatch) {
        gColdDarkResetApplied = false;
    }
}

void ShowTransientStatusLine(const std::string& line) {
    gManualQuerySnapshot = {};
    if (line.empty()) {
        gManualQueryVisibleUntilSeconds = 0;
        return;
    }

    gManualQuerySnapshot.visible = true;
    gManualQuerySnapshot.line = line;
    gManualQueryVisibleUntilSeconds =
        CurrentTickSeconds() + kManualQueryVisibleSeconds;
}

void ClearControllerMessage() {
    gPendingControllerMessage.visible = false;
}

void ResetControllerMessageState() {
    gPendingControllerMessage = {};
}

void AcknowledgeControllerMessage() {
    ClearControllerMessage();
}

void RecallControllerMessage() {
    if (!gPendingControllerMessage.cachedAvailable) {
        return;
    }

    gPendingControllerMessage.visible = true;
}

void UpdateControllerMessageState(
    const xvatsim::brain::XPilotPrivateMessageSnapshot& messageSnapshot) {
    if (!kControllerMessageUiEnabled) {
        ResetControllerMessageState();
        return;
    }

    if (!messageSnapshot.loaded) {
        ResetControllerMessageState();
        return;
    }

    if (!gPendingControllerMessage.primed) {
        gPendingControllerMessage.primed = true;
        gPendingControllerMessage.lastSequence = messageSnapshot.sequence;
        return;
    }

    if (messageSnapshot.sequence < gPendingControllerMessage.lastSequence) {
        gPendingControllerMessage.lastSequence = messageSnapshot.sequence;
        gPendingControllerMessage.visible = false;
        gPendingControllerMessage.cachedAvailable = false;
        gPendingControllerMessage.from.clear();
        gPendingControllerMessage.body.clear();
        return;
    }

    if (messageSnapshot.sequence == gPendingControllerMessage.lastSequence) {
        return;
    }

    gPendingControllerMessage.lastSequence = messageSnapshot.sequence;
    if (!messageSnapshot.available) {
        return;
    }

    gPendingControllerMessage.cachedAvailable = true;
    gPendingControllerMessage.visible = true;
    gPendingControllerMessage.from = messageSnapshot.from;
    gPendingControllerMessage.body = messageSnapshot.body;
}

std::vector<std::string> WrapOverlayMessageText(
    const std::string& text,
    std::size_t maxColumns = 38) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string paragraph;

    while (std::getline(stream, paragraph)) {
        paragraph.erase(
            std::remove(paragraph.begin(), paragraph.end(), '\r'),
            paragraph.end());

        if (paragraph.empty()) {
            lines.emplace_back();
            continue;
        }

        std::istringstream wordStream(paragraph);
        std::string word;
        std::string currentLine;
        while (wordStream >> word) {
            while (word.size() > maxColumns) {
                if (!currentLine.empty()) {
                    lines.push_back(currentLine);
                    currentLine.clear();
                }

                lines.push_back(word.substr(0, maxColumns));
                word.erase(0, maxColumns);
            }

            if (currentLine.empty()) {
                currentLine = word;
                continue;
            }

            if ((currentLine.size() + 1 + word.size()) <= maxColumns) {
                currentLine += " " + word;
                continue;
            }

            lines.push_back(currentLine);
            currentLine = word;
        }

        if (!currentLine.empty()) {
            lines.push_back(currentLine);
        }
    }

    if (lines.empty()) {
        lines.push_back(text);
    }

    return lines;
}

void ApplyControllerMessageCard(
    const PendingControllerMessageState& pendingMessage,
    xvatsim::brain::OverlayViewModel* overlayModel) {
    if (overlayModel == nullptr || overlayModel->bodyLines.empty()) {
        return;
    }

    std::vector<xvatsim::brain::OverlayTextLine> lines;
    lines.reserve(overlayModel->bodyLines.size() + 8);
    lines.push_back(overlayModel->bodyLines.front());
    lines.push_back({"CONTROLLER MESSAGE", xvatsim::brain::OverlayTone::Active});

    if (!pendingMessage.from.empty()) {
        lines.push_back(
            {"FROM " + pendingMessage.from, xvatsim::brain::OverlayTone::Next});
    }

    for (const auto& wrappedLine : WrapOverlayMessageText(pendingMessage.body)) {
        lines.push_back({wrappedLine, xvatsim::brain::OverlayTone::Normal});
    }

    const auto footerLine = overlayModel->bodyLines.back();
    lines.push_back(footerLine);
    overlayModel->bodyLines = std::move(lines);
}

std::string StandbyAssistWorkflowKey(
    xvatsim::brain::WorkflowStage workflowStage,
    const std::string& sourcePlanKey,
    const xvatsim::brain::RadioStateSnapshot& radioStateSnapshot,
    const xvatsim::brain::BoardStationSnapshot& targetStation) {
    return sourcePlanKey + "|" +
           std::to_string(static_cast<int>(workflowStage)) + "|" +
           NormalizeFrequency(radioStateSnapshot.com1ActiveFrequency) + "|" +
           NormalizeCallsign(targetStation.callsign) + "|" +
           NormalizeFrequency(targetStation.frequency);
}

bool IsBlockedControllerFrequency(const std::string& frequency) {
    const auto normalizedFrequency = NormalizeFrequency(frequency);
    return normalizedFrequency == "121500" || normalizedFrequency == "199998";
}

void ResetStandbyAssistLatch() {
    gStandbyAssistLatchKey.clear();
    gStandbyAssistWriteConsumed = false;
}

bool IsStandbyEligibleRole(xvatsim::brain::StationRole role) {
    using xvatsim::brain::StationRole;
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

int StandbyRoleRank(
    xvatsim::brain::WorkflowStage workflowStage,
    xvatsim::brain::StationRole role) {
    using xvatsim::brain::StationRole;
    if (workflowStage == xvatsim::brain::WorkflowStage::Arrival) {
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

void ApplyStandbyRecommendation(
    xvatsim::brain::WorkflowStage workflowStage,
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot,
    const xvatsim::brain::RadioStateSnapshot& radioStateSnapshot,
    xvatsim::brain::ModuleBoardSnapshot* boardSnapshot) {
    if (boardSnapshot == nullptr) {
        return;
    }

    if (workflowStage != xvatsim::brain::WorkflowStage::Departure &&
        workflowStage != xvatsim::brain::WorkflowStage::Arrival &&
        workflowStage != xvatsim::brain::WorkflowStage::Enroute) {
        ResetStandbyAssistLatch();
        return;
    }

    for (auto& station : boardSnapshot->stations) {
        station.next = false;
        station.standby = false;
    }

    const auto sourcePlanKey = BuildNetworkPlanIdentityKey(networkPlanSnapshot);
    if (sourcePlanKey.empty()) {
        ResetStandbyAssistLatch();
        return;
    }

    if (!radioStateSnapshot.valid) {
        ResetStandbyAssistLatch();
        return;
    }

    std::vector<std::size_t> orderedEligibleIndices;
    orderedEligibleIndices.reserve(boardSnapshot->stations.size());
    for (std::size_t index = 0; index < boardSnapshot->stations.size(); ++index) {
        const auto& station = boardSnapshot->stations[index];
        if (station.offline ||
            !IsStandbyEligibleRole(station.role) ||
            station.frequency.empty() ||
            IsBlockedControllerFrequency(station.frequency)) {
            continue;
        }
        orderedEligibleIndices.push_back(index);
    }

    if (workflowStage != xvatsim::brain::WorkflowStage::Enroute) {
        std::stable_sort(
            orderedEligibleIndices.begin(),
            orderedEligibleIndices.end(),
            [&](std::size_t leftIndex, std::size_t rightIndex) {
                const auto& left = boardSnapshot->stations[leftIndex];
                const auto& right = boardSnapshot->stations[rightIndex];
                const auto leftRank = StandbyRoleRank(workflowStage, left.role);
                const auto rightRank = StandbyRoleRank(workflowStage, right.role);
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
        ResetStandbyAssistLatch();
        return;
    }

    std::size_t targetPosition = 0;
    for (std::size_t position = 0; position < orderedEligibleIndices.size(); ++position) {
        if (boardSnapshot->stations[orderedEligibleIndices[position]].tuned) {
            targetPosition = position + 1;
        }
    }

    while (targetPosition < orderedEligibleIndices.size() &&
           boardSnapshot->stations[orderedEligibleIndices[targetPosition]].tuned) {
        ++targetPosition;
    }

    if (targetPosition >= orderedEligibleIndices.size()) {
        ResetStandbyAssistLatch();
        return;
    }

    auto& targetStation = boardSnapshot->stations[orderedEligibleIndices[targetPosition]];
    if (targetStation.frequency.empty()) {
        ResetStandbyAssistLatch();
        return;
    }

    const auto latchKey =
        StandbyAssistWorkflowKey(
            workflowStage,
            sourcePlanKey,
            radioStateSnapshot,
            targetStation);
    if (latchKey != gStandbyAssistLatchKey) {
        gStandbyAssistLatchKey = latchKey;
        gStandbyAssistWriteConsumed = false;
    }

    bool standbyLoaded = false;
    if (gPluginSettings.standbyAssistEnabled) {
        const auto normalizedTarget = NormalizeFrequency(targetStation.frequency);
        standbyLoaded =
            !normalizedTarget.empty() &&
            NormalizeFrequency(radioStateSnapshot.com1StandbyFrequency) == normalizedTarget;
        if (!standbyLoaded && !gStandbyAssistWriteConsumed) {
            standbyLoaded = gRadioStateSampler.SetCom1StandbyFrequency(targetStation.frequency);
        }
        gStandbyAssistWriteConsumed = true;
    }

    if (workflowStage != xvatsim::brain::WorkflowStage::Enroute) {
        if (standbyLoaded) {
            targetStation.standby = true;
        } else {
            targetStation.next = true;
        }
    }
}

double ResolveCruiseComparisonAltitudeFt(
    const xvatsim::brain::AircraftStateSnapshot& aircraftState,
    double cruiseTargetFt) {
    auto comparisonAltitudeFt = aircraftState.altitudeOperationalFt;
    if (cruiseTargetFt >= 18000.0 && aircraftState.hasAltimeterSetting) {
        comparisonAltitudeFt += (29.92 - aircraftState.altimeterSettingInHg) * 1000.0;
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

std::string NormalizeIcao(std::string airportIcao) {
    std::string normalized;
    normalized.reserve(airportIcao.size());
    for (const auto character : airportIcao) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0) {
            continue;
        }

        normalized.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(character))));
    }
    return normalized;
}

bool AirportsMatch(const std::string& left, const std::string& right) {
    return !left.empty() && !right.empty() && NormalizeIcao(left) == NormalizeIcao(right);
}

std::string BuildPlanIdentityKey(
    std::string callsign,
    std::string departureIcao,
    std::string destinationIcao) {
    callsign = NormalizeCallsign(std::move(callsign));
    departureIcao = NormalizeIcao(std::move(departureIcao));
    destinationIcao = NormalizeIcao(std::move(destinationIcao));

    if (callsign.empty() || departureIcao.empty() || destinationIcao.empty()) {
        return {};
    }

    return callsign + "|" + departureIcao + "|" + destinationIcao;
}

std::string BuildNetworkPlanIdentityKey(
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    if (networkPlanSnapshot.stale || !networkPlanSnapshot.matched) {
        return {};
    }

    return BuildPlanIdentityKey(
        networkPlanSnapshot.matchedCallsign,
        networkPlanSnapshot.departureIcao,
        networkPlanSnapshot.destinationIcao);
}

bool HasFreshMatchedNetworkPlan(
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    return !BuildNetworkPlanIdentityKey(networkPlanSnapshot).empty();
}

void ResetCruiseTargetState() {
    gCruiseAltitudeReachedThisFlight = false;
    gCruiseTargetManualOverride = false;
    gHasActiveCruiseTarget = false;
    gActiveCruiseTargetFt = 0.0;
    gCruiseGateSatisfiedSinceSeconds = -1.0f;
    gCruiseTargetSourceKey.clear();
}

void ClearCruiseTargetIfSourceInvalid(
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    if (!gHasActiveCruiseTarget && gCruiseTargetSourceKey.empty()) {
        return;
    }

    const auto sourcePlanKey = BuildNetworkPlanIdentityKey(networkPlanSnapshot);
    if (!sourcePlanKey.empty() &&
        !gCruiseTargetSourceKey.empty() &&
        sourcePlanKey == gCruiseTargetSourceKey) {
        return;
    }

    ResetCruiseTargetState();
    XPLMDebugString(
        "[XVatsim] Cruise target cleared because source VATSIM flight plan was stale, unmatched, or changed.\n");
}

void ClearDiversionOverrideState() {
    gDiversionContextModule.Reset();
    gDiversionOverrideSourceKey.clear();
}

bool CoordinatesDiffer(
    double leftLatitudeDeg,
    double leftLongitudeDeg,
    double rightLatitudeDeg,
    double rightLongitudeDeg) {
    constexpr double kCoordinateToleranceDeg = 1e-4;
    return std::fabs(leftLatitudeDeg - rightLatitudeDeg) > kCoordinateToleranceDeg ||
           std::fabs(leftLongitudeDeg - rightLongitudeDeg) > kCoordinateToleranceDeg;
}

bool ApplyMissingAirportCoordinates(
    const std::string& targetAirportIcao,
    const std::string& sourceAirportIcao,
    bool sourceHasCoordinates,
    double sourceLatitudeDeg,
    double sourceLongitudeDeg,
    double* inOutLatitudeDeg,
    double* inOutLongitudeDeg,
    bool* inOutHasCoordinates) {
    if (inOutLatitudeDeg == nullptr ||
        inOutLongitudeDeg == nullptr ||
        inOutHasCoordinates == nullptr ||
        *inOutHasCoordinates ||
        !sourceHasCoordinates ||
        !AirportsMatch(targetAirportIcao, sourceAirportIcao)) {
        return false;
    }

    *inOutLatitudeDeg = sourceLatitudeDeg;
    *inOutLongitudeDeg = sourceLongitudeDeg;
    *inOutHasCoordinates = true;
    return true;
}

bool ApplyAuthoritativeAirportCoordinates(
    const std::string& targetAirportIcao,
    const std::string& sourceAirportIcao,
    bool sourceHasCoordinates,
    double sourceLatitudeDeg,
    double sourceLongitudeDeg,
    double* inOutLatitudeDeg,
    double* inOutLongitudeDeg,
    bool* inOutHasCoordinates) {
    if (inOutLatitudeDeg == nullptr ||
        inOutLongitudeDeg == nullptr ||
        inOutHasCoordinates == nullptr ||
        !sourceHasCoordinates ||
        !AirportsMatch(targetAirportIcao, sourceAirportIcao)) {
        return false;
    }

    if (*inOutHasCoordinates &&
        !CoordinatesDiffer(
            *inOutLatitudeDeg,
            *inOutLongitudeDeg,
            sourceLatitudeDeg,
            sourceLongitudeDeg)) {
        return false;
    }

    *inOutLatitudeDeg = sourceLatitudeDeg;
    *inOutLongitudeDeg = sourceLongitudeDeg;
    *inOutHasCoordinates = true;
    return true;
}

bool IsInsideArrivalWakeDistance(const xvatsim::brain::AircraftStateSnapshot& aircraftState) {
    xvatsim::core::workflow::WorkflowTuning tuning;
    tuning.arrivalWakeDistanceNm = kArrivalWakeDistanceNm;
    return xvatsim::core::workflow::IsInsideArrivalWakeDistance(
        aircraftState,
        gFlightContext,
        tuning);
}

bool IsOnGroundAtDestination(const xvatsim::brain::AircraftStateSnapshot& aircraftState) {
    xvatsim::core::workflow::WorkflowTuning tuning;
    tuning.destinationGroundDistanceNm = 5.0;
    return xvatsim::core::workflow::IsOnGroundAtDestination(
        aircraftState,
        gFlightContext,
        tuning);
}

bool IsLiveRouteCenterStation(const xvatsim::brain::BoardStationSnapshot& station) {
    return station.role == xvatsim::brain::StationRole::Center &&
           !station.offline &&
           !station.frequency.empty();
}

bool HasLiveRouteCenters(
    const xvatsim::brain::ModuleBoardSnapshot& enrouteBoardSnapshot) {
    for (const auto& station : enrouteBoardSnapshot.stations) {
        if (IsLiveRouteCenterStation(station)) {
            return true;
        }
    }

    return false;
}

bool CanConfirmDepartureLocation(
    const xvatsim::brain::AircraftStateSnapshot& aircraftState,
    const xvatsim::brain::FlightPlanSnapshot& flightPlanSnapshot,
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    xvatsim::core::workflow::WorkflowTuning tuning;
    tuning.departureConfirmDistanceNm = 10.0;
    return xvatsim::core::workflow::CanConfirmDepartureLocation(
        aircraftState,
        flightPlanSnapshot,
        networkPlanSnapshot,
        tuning);
}

void ResetFlightScopedManualPlanState() {
    ResetCruiseTargetState();
    ClearDiversionOverrideState();
}

void ResetEnrouteInitialDisplayHold() {
    gEnrouteInitialDisplayStarted = false;
    gEnrouteInitialDisplayUntilSeconds = -1.0f;
}

void ResetFlightProgressStateForNewContext() {
    gDepartureReleasedThisFlight = false;
    gArrivalAwakeThisFlight = false;
    gAirborneSinceSeconds = -1.0f;
    ResetBrainOwnedRuntimeCache();
    ResetEnrouteInitialDisplayHold();
}

void ClearXPilotConnectionTracking() {
    gLastXPilotConnected = false;
    gLastConnectedPilotCallsign.clear();
}

void ClearFlightRecoveryState() {
    gDisconnectedPilotCallsign.clear();
    gPendingAutomaticFlightRecovery = false;
    gManualFlightRecoveryRequested = false;
}

void LockFlightContextFromNetworkPlan(
    const xvatsim::brain::PilotIdentitySnapshot& pilotIdentitySnapshot,
    const xvatsim::brain::FlightPlanSnapshot& flightPlanSnapshot,
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    FlightContext nextFlightContext = {};
    nextFlightContext.active = true;
    nextFlightContext.callsign = pilotIdentitySnapshot.callsign;
    nextFlightContext.departureIcao = networkPlanSnapshot.departureIcao;
    nextFlightContext.departureLatDeg = networkPlanSnapshot.departureLatDeg;
    nextFlightContext.departureLonDeg = networkPlanSnapshot.departureLonDeg;
    nextFlightContext.hasDepartureCoordinates = networkPlanSnapshot.hasDepartureCoordinates;
    nextFlightContext.destinationIcao = networkPlanSnapshot.destinationIcao;
    nextFlightContext.destinationLatDeg = networkPlanSnapshot.destinationLatDeg;
    nextFlightContext.destinationLonDeg = networkPlanSnapshot.destinationLonDeg;
    nextFlightContext.hasDestinationCoordinates = networkPlanSnapshot.hasDestinationCoordinates;
    nextFlightContext.routeText = networkPlanSnapshot.routeText;

    ApplyMissingAirportCoordinates(
        nextFlightContext.departureIcao,
        flightPlanSnapshot.departureIcao,
        flightPlanSnapshot.hasDepartureCoordinates,
        flightPlanSnapshot.departureLatDeg,
        flightPlanSnapshot.departureLonDeg,
        &nextFlightContext.departureLatDeg,
        &nextFlightContext.departureLonDeg,
        &nextFlightContext.hasDepartureCoordinates);

    ApplyMissingAirportCoordinates(
        nextFlightContext.destinationIcao,
        flightPlanSnapshot.destinationIcao,
        flightPlanSnapshot.hasDestinationCoordinates,
        flightPlanSnapshot.destinationLatDeg,
        flightPlanSnapshot.destinationLonDeg,
        &nextFlightContext.destinationLatDeg,
        &nextFlightContext.destinationLonDeg,
        &nextFlightContext.hasDestinationCoordinates);

    ApplyMissingAirportCoordinates(
        nextFlightContext.departureIcao,
        gFlightContext.departureIcao,
        gFlightContext.hasDepartureCoordinates,
        gFlightContext.departureLatDeg,
        gFlightContext.departureLonDeg,
        &nextFlightContext.departureLatDeg,
        &nextFlightContext.departureLonDeg,
        &nextFlightContext.hasDepartureCoordinates);

    ApplyMissingAirportCoordinates(
        nextFlightContext.destinationIcao,
        gFlightContext.destinationIcao,
        gFlightContext.hasDestinationCoordinates,
        gFlightContext.destinationLatDeg,
        gFlightContext.destinationLonDeg,
        &nextFlightContext.destinationLatDeg,
        &nextFlightContext.destinationLonDeg,
        &nextFlightContext.hasDestinationCoordinates);

    gFlightContext = std::move(nextFlightContext);

    ResetFlightScopedManualPlanState();
    ResetFlightProgressStateForNewContext();
    ResetBrainDisplayPublisherCache();
    ResetStandbyAssistLatch();
}

void InvalidateFlightContextPresentationCaches();

void RetargetFlightContextToPlan(
    const xvatsim::brain::PilotIdentitySnapshot& pilotIdentitySnapshot,
    const xvatsim::brain::FlightPlanSnapshot& flightPlanSnapshot,
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    if (!gFlightContext.active) {
        return;
    }

    FlightContext nextFlightContext = gFlightContext;
    nextFlightContext.callsign =
        pilotIdentitySnapshot.callsign.empty()
            ? gFlightContext.callsign
            : pilotIdentitySnapshot.callsign;
    nextFlightContext.routeText = networkPlanSnapshot.routeText;

    if (!networkPlanSnapshot.departureIcao.empty()) {
        nextFlightContext.departureIcao = networkPlanSnapshot.departureIcao;
    }
    if (networkPlanSnapshot.hasDepartureCoordinates) {
        nextFlightContext.departureLatDeg = networkPlanSnapshot.departureLatDeg;
        nextFlightContext.departureLonDeg = networkPlanSnapshot.departureLonDeg;
        nextFlightContext.hasDepartureCoordinates = true;
    } else if (!nextFlightContext.hasDepartureCoordinates &&
               AirportsMatch(flightPlanSnapshot.departureIcao, nextFlightContext.departureIcao) &&
               flightPlanSnapshot.hasDepartureCoordinates) {
        nextFlightContext.departureLatDeg = flightPlanSnapshot.departureLatDeg;
        nextFlightContext.departureLonDeg = flightPlanSnapshot.departureLonDeg;
        nextFlightContext.hasDepartureCoordinates = true;
    }

    nextFlightContext.destinationIcao = networkPlanSnapshot.destinationIcao;
    nextFlightContext.destinationLatDeg = networkPlanSnapshot.destinationLatDeg;
    nextFlightContext.destinationLonDeg = networkPlanSnapshot.destinationLonDeg;
    nextFlightContext.hasDestinationCoordinates = networkPlanSnapshot.hasDestinationCoordinates;
    if (!nextFlightContext.hasDestinationCoordinates &&
        AirportsMatch(flightPlanSnapshot.destinationIcao, nextFlightContext.destinationIcao) &&
        flightPlanSnapshot.hasDestinationCoordinates) {
        nextFlightContext.destinationLatDeg = flightPlanSnapshot.destinationLatDeg;
        nextFlightContext.destinationLonDeg = flightPlanSnapshot.destinationLonDeg;
        nextFlightContext.hasDestinationCoordinates = true;
    }

    gFlightContext = std::move(nextFlightContext);
    gArrivalAwakeThisFlight = false;
    ResetEnrouteInitialDisplayHold();
    InvalidateFlightContextPresentationCaches();
}

xvatsim::brain::NetworkPlanSnapshot BuildEffectiveNetworkPlanSnapshot(
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    if (!gDiversionContextModule.HasOverride()) {
        return networkPlanSnapshot;
    }

    const auto sourcePlanKey = BuildNetworkPlanIdentityKey(networkPlanSnapshot);

    if (sourcePlanKey.empty() ||
        gDiversionOverrideSourceKey.empty() ||
        sourcePlanKey != gDiversionOverrideSourceKey) {
        ClearDiversionOverrideState();
        XPLMDebugString(
            "[XVatsim] Diversion override cleared because source VATSIM flight plan was stale, unmatched, or changed.\n");
        return networkPlanSnapshot;
    }

    return gDiversionContextModule.BuildEffectivePlan(networkPlanSnapshot);
}

void InvalidateFlightContextPresentationCaches() {
    ResetBrainDisplayPublisherCache();
    ResetStandbyAssistLatch();
}

void RefreshFlightContextMetadataIfNeeded(
    const xvatsim::brain::PilotIdentitySnapshot& pilotIdentitySnapshot,
    const xvatsim::brain::FlightPlanSnapshot& flightPlanSnapshot,
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    if (!gFlightContext.active) {
        return;
    }

    FlightContext nextFlightContext = gFlightContext;
    bool changed = false;

    if (!pilotIdentitySnapshot.callsign.empty() &&
        nextFlightContext.callsign != pilotIdentitySnapshot.callsign) {
        nextFlightContext.callsign = pilotIdentitySnapshot.callsign;
        changed = true;
    }

    if (!networkPlanSnapshot.routeText.empty() &&
        nextFlightContext.routeText != networkPlanSnapshot.routeText) {
        nextFlightContext.routeText = networkPlanSnapshot.routeText;
        changed = true;
    }

    changed =
        ApplyAuthoritativeAirportCoordinates(
            nextFlightContext.departureIcao,
            networkPlanSnapshot.departureIcao,
            networkPlanSnapshot.hasDepartureCoordinates,
            networkPlanSnapshot.departureLatDeg,
            networkPlanSnapshot.departureLonDeg,
            &nextFlightContext.departureLatDeg,
            &nextFlightContext.departureLonDeg,
            &nextFlightContext.hasDepartureCoordinates) ||
        changed;
    changed =
        ApplyMissingAirportCoordinates(
            nextFlightContext.departureIcao,
            flightPlanSnapshot.departureIcao,
            flightPlanSnapshot.hasDepartureCoordinates,
            flightPlanSnapshot.departureLatDeg,
            flightPlanSnapshot.departureLonDeg,
            &nextFlightContext.departureLatDeg,
            &nextFlightContext.departureLonDeg,
            &nextFlightContext.hasDepartureCoordinates) ||
        changed;

    changed =
        ApplyAuthoritativeAirportCoordinates(
            nextFlightContext.destinationIcao,
            networkPlanSnapshot.destinationIcao,
            networkPlanSnapshot.hasDestinationCoordinates,
            networkPlanSnapshot.destinationLatDeg,
            networkPlanSnapshot.destinationLonDeg,
            &nextFlightContext.destinationLatDeg,
            &nextFlightContext.destinationLonDeg,
            &nextFlightContext.hasDestinationCoordinates) ||
        changed;
    changed =
        ApplyMissingAirportCoordinates(
            nextFlightContext.destinationIcao,
            flightPlanSnapshot.destinationIcao,
            flightPlanSnapshot.hasDestinationCoordinates,
            flightPlanSnapshot.destinationLatDeg,
            flightPlanSnapshot.destinationLonDeg,
            &nextFlightContext.destinationLatDeg,
            &nextFlightContext.destinationLonDeg,
            &nextFlightContext.hasDestinationCoordinates) ||
        changed;

    if (!changed) {
        return;
    }

    gFlightContext = std::move(nextFlightContext);
    InvalidateFlightContextPresentationCaches();
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
    if (!pilotIdentitySnapshot.connected ||
        !networkPlanSnapshot.matched ||
        networkPlanSnapshot.stale) {
        return;
    }

    const auto callsignChanged =
        gFlightContext.active && !pilotIdentitySnapshot.callsign.empty() &&
        gFlightContext.callsign != pilotIdentitySnapshot.callsign;
    const auto routeChanged =
        gFlightContext.active &&
        (!AirportsMatch(gFlightContext.departureIcao, networkPlanSnapshot.departureIcao) ||
         !AirportsMatch(gFlightContext.destinationIcao, networkPlanSnapshot.destinationIcao));

    if (!gFlightContext.active || callsignChanged || routeChanged) {
        if (CanConfirmDepartureLocation(aircraftState, flightPlanSnapshot, networkPlanSnapshot)) {
            LockFlightContextFromNetworkPlan(
                pilotIdentitySnapshot,
                flightPlanSnapshot,
                networkPlanSnapshot);
        }
        return;
    }

    RefreshFlightContextMetadataIfNeeded(
        pilotIdentitySnapshot,
        flightPlanSnapshot,
        networkPlanSnapshot);
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
    gFlightContext = {};
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
    const xvatsim::core::workflow::RecoveryDecision& decision) {
    if (!decision.accepted) {
        return;
    }

    gFlightContext = decision.flightContext;
    switch (decision.stage) {
        case xvatsim::brain::WorkflowStage::Departure:
            gDepartureReleasedThisFlight = false;
            gArrivalAwakeThisFlight = false;
            gAirborneSinceSeconds = -1.0f;
            break;
        case xvatsim::brain::WorkflowStage::Enroute:
            gDepartureReleasedThisFlight = true;
            gArrivalAwakeThisFlight = false;
            if (gAirborneSinceSeconds < 0.0f) {
                gAirborneSinceSeconds = XPLMGetElapsedTime();
            }
            ResetEnrouteInitialDisplayHold();
            break;
        case xvatsim::brain::WorkflowStage::Arrival:
            gDepartureReleasedThisFlight = true;
            gArrivalAwakeThisFlight = true;
            if (gAirborneSinceSeconds < 0.0f) {
                gAirborneSinceSeconds = XPLMGetElapsedTime();
            }
            ResetEnrouteInitialDisplayHold();
            break;
        case xvatsim::brain::WorkflowStage::None:
        default:
            break;
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

    xvatsim::core::workflow::WorkflowTuning tuning;
    tuning.arrivalWakeDistanceNm = kArrivalWakeDistanceNm;
    tuning.departureConfirmDistanceNm = 10.0;
    const auto decision = xvatsim::core::workflow::ResolveCurrentFlightRecovery(
        aircraftState,
        flightPlanSnapshot,
        networkPlanSnapshot,
        gFlightContext,
        manual
            ? xvatsim::core::workflow::RecoveryRequestMode::Manual
            : xvatsim::core::workflow::RecoveryRequestMode::AutomaticReconnect,
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
        gPendingAutomaticFlightRecovery = false;
        gManualFlightRecoveryRequested = false;
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
        gPendingAutomaticFlightRecovery = false;
    } else if (!manual && decision.reason != "plan-unavailable") {
        gPendingAutomaticFlightRecovery = false;
    }

    if (manual) {
        gManualFlightRecoveryRequested = false;
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
    if (gPendingAutomaticFlightRecovery) {
        (void)AttemptCurrentFlightRecovery(
            false,
            aircraftState,
            flightPlanSnapshot,
            networkPlanSnapshot);
    }
    if (gManualFlightRecoveryRequested) {
        (void)AttemptCurrentFlightRecovery(
            true,
            aircraftState,
            flightPlanSnapshot,
            networkPlanSnapshot);
    }
}

HandoffDecision ResolveWorkflowStage(
    const xvatsim::brain::AircraftStateSnapshot& aircraftState,
    const xvatsim::brain::RadioStateSnapshot& radioStateSnapshot,
    const xvatsim::brain::AirportSectorSnapshot& departureAirportSectorSnapshot,
    const xvatsim::brain::ModuleBoardSnapshot& departureBoardSnapshot,
    const xvatsim::brain::ModuleBoardSnapshot& enrouteBoardSnapshot) {
    xvatsim::core::workflow::WorkflowState state;
    state.flightContext = gFlightContext;
    state.departureReleasedThisFlight = gDepartureReleasedThisFlight;
    state.arrivalAwakeThisFlight = gArrivalAwakeThisFlight;
    state.airborneSinceSeconds = gAirborneSinceSeconds;

    xvatsim::core::workflow::WorkflowTuning tuning;
    tuning.arrivalWakeDistanceNm = kArrivalWakeDistanceNm;
    tuning.departureReleaseHoldSeconds = kDepartureReleaseHoldSeconds;
    bool departureTerminalCoverageKnown = false;
    bool insideDepartureTerminalCoverage = false;
    const auto departureTerminalGeometryCanAffectDecision =
        !aircraftState.onGround &&
        !gArrivalAwakeThisFlight &&
        !gDepartureReleasedThisFlight;
    if (departureTerminalGeometryCanAffectDecision) {
        departureTerminalCoverageKnown =
            gRouteSectorResolver.CanEvaluateAirportTerminalCoverage(
                departureAirportSectorSnapshot);
        if (departureTerminalCoverageKnown) {
            insideDepartureTerminalCoverage =
                gRouteSectorResolver.IsInsideAirportTerminalCoverage(
                    departureAirportSectorSnapshot,
                    aircraftState.latitudeDeg,
                    aircraftState.longitudeDeg);
        }
    }

    const auto decision = xvatsim::core::workflow::ResolveWorkflowStage(
        aircraftState,
        radioStateSnapshot,
        departureTerminalCoverageKnown,
        insideDepartureTerminalCoverage,
        departureBoardSnapshot,
        enrouteBoardSnapshot,
        XPLMGetElapsedTime(),
        &state,
        tuning);

    gDepartureReleasedThisFlight = state.departureReleasedThisFlight;
    gArrivalAwakeThisFlight = state.arrivalAwakeThisFlight;
    gAirborneSinceSeconds = static_cast<float>(state.airborneSinceSeconds);
    return decision;
}

void UpdateEnrouteInitialDisplayHold(xvatsim::brain::WorkflowStage workflowStage) {
    if (workflowStage != xvatsim::brain::WorkflowStage::Enroute) {
        return;
    }

    if (gEnrouteInitialDisplayStarted) {
        return;
    }

    gEnrouteInitialDisplayStarted = true;
    gEnrouteInitialDisplayUntilSeconds =
        XPLMGetElapsedTime() + kEnrouteInitialDisplaySeconds;
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
    if (!gRefreshDiagnosticsFrame.valid) {
        return;
    }

    DiagnosticJobRecord job;
    job.name = std::move(name);
    job.reason = std::move(reason);
    job.stage = gRefreshDiagnosticsFrame.stage;
    job.cacheStatus = std::move(cacheStatus);
    job.result = std::move(result);
    job.sourceGenerations = std::move(sourceGenerations);
    job.routeKey = std::move(routeKey);
    job.durationMs = durationMs;
    gRefreshDiagnosticsFrame.jobs.push_back(std::move(job));
}

bool LegacyAuthorityRuntimeAllowed(
    const char* caller,
    const std::string& planKey) {
    std::ostringstream result;
    result << "allowed=0"
           << ",engineer3Live=1"
           << ",legacyEnabled=0"
           << ",heavyFallbackRequested="
           << (gBrainOwnedRuntimeState.heavyFallbackRequested ? 1 : 0)
           << ",heavyFallbackRunning="
           << (gBrainOwnedRuntimeState.heavyFallbackRunning ? 1 : 0);
    RecordDiagnosticJob(
        "LegacyAuthorityQuarantine",
        caller == nullptr ? "legacy-authority-runtime" : caller,
        0,
        "old-authority-quarantined",
        result.str(),
        {},
        planKey);
    return false;
}

std::vector<std::string> BuildEngineer3AirportTokens(const std::string& airportIcao) {
    std::vector<std::string> tokens;
    const auto normalized = NormalizeIcaoInput(airportIcao);
    if (normalized.empty()) {
        return tokens;
    }

    tokens.push_back(normalized);
    if (normalized.size() == 4) {
        tokens.push_back(normalized.substr(1));
    }
    if (normalized.size() >= 3) {
        tokens.push_back(normalized.substr(normalized.size() - 3));
    }

    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
    return tokens;
}

bool SplitEngineer3ControllerCallsign(
    const std::string& callsign,
    std::string* outPrefix,
    std::string* outSuffix) {
    const auto normalized = NormalizeCallsign(callsign);
    const auto separatorIndex = normalized.rfind('_');
    if (separatorIndex == std::string::npos || separatorIndex == 0 ||
        separatorIndex >= (normalized.size() - 1)) {
        return false;
    }

    if (outPrefix != nullptr) {
        *outPrefix = normalized.substr(0, separatorIndex);
    }
    if (outSuffix != nullptr) {
        *outSuffix = normalized.substr(separatorIndex + 1);
    }
    return true;
}

bool Engineer3ControllerMatchesAirport(
    const std::string& callsign,
    const std::vector<std::string>& airportTokens) {
    std::string prefix;
    if (!SplitEngineer3ControllerCallsign(callsign, &prefix, nullptr)) {
        return false;
    }

    return std::any_of(
        airportTokens.begin(),
        airportTokens.end(),
        [&](const auto& token) {
            return prefix == token ||
                   (prefix.size() > token.size() &&
                    prefix.compare(0, token.size(), token) == 0 &&
                    (prefix[token.size()] == '_' || prefix[token.size()] == '-'));
        });
}

bool Engineer3FrequencyTuned(
    const std::string& frequency,
    const xvatsim::brain::RadioStateSnapshot& radioStateSnapshot) {
    const auto normalizedTarget = NormalizeFrequency(frequency);
    if (normalizedTarget.empty()) {
        return false;
    }

    return NormalizeFrequency(radioStateSnapshot.com1ActiveFrequency) ==
               normalizedTarget ||
           NormalizeFrequency(radioStateSnapshot.com2ActiveFrequency) ==
               normalizedTarget;
}

bool Engineer3GuardFrequency(const std::string& frequency) {
    const auto normalizedFrequency = NormalizeFrequency(frequency);
    return normalizedFrequency == "121500" || normalizedFrequency == "199998";
}

xvatsim::brain::StationRole Engineer3RoleFromRadioCandidate(
    const xvatsim::brain::RadioReachableControllerCandidate& candidate) {
    using xvatsim::brain::RadioReachableFacilityGroup;
    using xvatsim::brain::StationRole;

    switch (candidate.group) {
        case RadioReachableFacilityGroup::Delivery:
            return StationRole::Delivery;
        case RadioReachableFacilityGroup::Ground:
            return StationRole::Ground;
        case RadioReachableFacilityGroup::Tower:
            return StationRole::Tower;
        case RadioReachableFacilityGroup::AppDep: {
            std::string suffix;
            if (SplitEngineer3ControllerCallsign(
                    candidate.callsign,
                    nullptr,
                    &suffix) &&
                suffix == "DEP") {
                return StationRole::Departure;
            }
            return StationRole::Approach;
        }
        case RadioReachableFacilityGroup::Center:
            return StationRole::Center;
        case RadioReachableFacilityGroup::Atis:
            return StationRole::Atis;
        case RadioReachableFacilityGroup::Other:
        default:
            return StationRole::Other;
    }
}

void Engineer3AppendStationUnique(
    const xvatsim::brain::BoardStationSnapshot& station,
    xvatsim::brain::ModuleBoardSnapshot* board,
    std::unordered_set<std::string>* insertedKeys) {
    if (board == nullptr || insertedKeys == nullptr || station.frequency.empty() ||
        Engineer3GuardFrequency(station.frequency)) {
        return;
    }

    const auto key =
        std::to_string(static_cast<int>(station.role)) + "|" +
        NormalizeCallsign(station.callsign) + "|" + NormalizeFrequency(station.frequency);
    if (!insertedKeys->insert(key).second) {
        return;
    }

    board->stations.push_back(station);
    board->available = true;
}

bool Engineer3BuildCtafStation(
    const std::string& airportIcao,
    const xvatsim::modules::ctaf_lookup::CtafLookupEntry& ctafLookup,
    const xvatsim::brain::RadioStateSnapshot& radioStateSnapshot,
    xvatsim::brain::BoardStationSnapshot* station) {
    if (airportIcao.empty() || station == nullptr) {
        return false;
    }

    *station = {};
    station->callsign = airportIcao;
    if (ctafLookup.available) {
        station->role = xvatsim::brain::StationRole::Ctaf;
        station->frequency = ctafLookup.frequency;
        station->tuned = Engineer3FrequencyTuned(ctafLookup.frequency, radioStateSnapshot);
    } else if (ctafLookup.resolved) {
        station->role = xvatsim::brain::StationRole::Unicom;
        station->frequency = "122.800";
        station->tuned = Engineer3FrequencyTuned("122.800", radioStateSnapshot);
    } else {
        station->role = xvatsim::brain::StationRole::Ctaf;
        station->annotation = "lookup";
    }
    return true;
}

std::string BrainControllerCandidateStableKey(
    const xvatsim::brain::RadioReachableControllerCandidate& candidate) {
    if (!candidate.stableKey.empty()) {
        return candidate.stableKey;
    }

    return NormalizeCallsign(candidate.callsign) + "|" +
           NormalizeFrequency(candidate.frequency) + "|" +
           std::to_string(static_cast<int>(candidate.group));
}

void BrainRecordCandidateDecision(
    const xvatsim::brain::BrainControllerRelevanceWorkerInput& input,
    const xvatsim::brain::RadioReachableControllerCandidate& candidate,
    xvatsim::brain::BrainOwnedCandidateDecision decision,
    const std::string& reason,
    const xvatsim::brain::BoardStationSnapshot& station,
    std::vector<xvatsim::brain::BrainOwnedCandidateCompletion>* completions) {
    if (completions == nullptr) {
        return;
    }

    auto keyedCandidate = candidate;
    keyedCandidate.stableKey = BrainControllerCandidateStableKey(candidate);

    xvatsim::brain::BrainOwnedCandidateCompletion completion;
    completion.radioBoardHash = input.radioBoardHash;
    completion.routePolygonHash = input.routePolygonHash;
    completion.workflowStage = input.workflowStage;
    completion.currentPolygonIndex = input.currentPolygonIndex;
    completion.currentPolygonKey = input.currentPolygonKey;
    completion.matchedPolygonKey = station.polygonKey;
    completion.callsign = candidate.callsign;
    completion.frequency = candidate.frequency;
    completion.facilityGroup = candidate.group;
    completion.displayRelation = station.displayRelation;
    completion.decision = decision;
    completion.displayed = false;
    completion.hasRouteEntryDistance = station.hasRouteEntryDistance;
    completion.routeEntryDistanceNm = station.routeEntryDistanceNm;
    completion.reason = reason;
    completion.stableKey = xvatsim::brain::BuildBrainOwnedCandidateCompletionKey(
        input.radioBoardHash,
        input.routePolygonHash,
        input.workflowStage,
        input.currentPolygonKey,
        keyedCandidate);
    if (completion.frequency.empty()) {
        completion.frequency = station.frequency;
    }
    completions->push_back(std::move(completion));
}

bool BrainWildcardMatch(std::string pattern, std::string value) {
    if (pattern.empty()) {
        return false;
    }

    std::size_t patternIndex = 0;
    std::size_t valueIndex = 0;
    std::size_t starIndex = std::string::npos;
    std::size_t valueRetryIndex = 0;
    while (valueIndex < value.size()) {
        if (patternIndex < pattern.size() &&
            pattern[patternIndex] == value[valueIndex]) {
            ++patternIndex;
            ++valueIndex;
            continue;
        }

        if (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
            starIndex = patternIndex++;
            valueRetryIndex = valueIndex;
            continue;
        }

        if (starIndex != std::string::npos) {
            patternIndex = starIndex + 1;
            valueIndex = ++valueRetryIndex;
            continue;
        }

        return false;
    }

    while (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
        ++patternIndex;
    }
    return patternIndex == pattern.size();
}

bool BrainCallsignMatchesPattern(
    const std::string& rawPattern,
    const std::string& rawCallsign) {
    const auto pattern = NormalizeCallsign(rawPattern);
    const auto callsign = NormalizeCallsign(rawCallsign);
    if (pattern.empty() || callsign.empty()) {
        return false;
    }
    if (pattern.find('*') != std::string::npos) {
        return BrainWildcardMatch(pattern, callsign);
    }
    return pattern == callsign;
}

std::vector<std::string> BrainCallsignPrefixCandidates(
    const std::string& rawCallsign) {
    std::vector<std::string> prefixes;
    const auto callsign = NormalizeCallsign(rawCallsign);
    if (callsign.empty()) {
        return prefixes;
    }

    prefixes.push_back(callsign);
    const auto firstSeparator = callsign.find('_');
    if (firstSeparator != std::string::npos && firstSeparator > 0) {
        prefixes.push_back(callsign.substr(0, firstSeparator));
    }
    const auto lastSeparator = callsign.rfind('_');
    if (lastSeparator != std::string::npos && lastSeparator > 0) {
        prefixes.push_back(callsign.substr(0, lastSeparator));
    }

    std::sort(prefixes.begin(), prefixes.end());
    prefixes.erase(std::unique(prefixes.begin(), prefixes.end()), prefixes.end());
    return prefixes;
}

bool BrainCallsignMatchesPrefix(
    const std::string& rawPrefix,
    const std::string& rawCallsign) {
    const auto prefix = NormalizeCallsign(rawPrefix);
    if (prefix.empty() || prefix.size() < 2) {
        return false;
    }

    for (const auto& candidatePrefix : BrainCallsignPrefixCandidates(rawCallsign)) {
        if (candidatePrefix == prefix) {
            return true;
        }
        if (candidatePrefix.size() > prefix.size() &&
            candidatePrefix.compare(0, prefix.size(), prefix) == 0 &&
            (candidatePrefix[prefix.size()] == '_' ||
             candidatePrefix[prefix.size()] == '-')) {
            return true;
        }
    }
    return false;
}

bool BrainSectorHasCenterMetadata(
    const xvatsim::brain::RouteSectorMatchSnapshot& sector) {
    return !sector.controllerCallsignPatterns.empty() ||
           !sector.controllerPrefixes.empty() ||
           !sector.matchTokens.empty();
}

bool BrainSectorsHaveCenterMetadata(
    const std::vector<xvatsim::brain::RouteSectorMatchSnapshot>& sectors) {
    return std::any_of(
        sectors.begin(),
        sectors.end(),
        [](const auto& sector) { return BrainSectorHasCenterMetadata(sector); });
}

bool BrainSectorMatchesCenterCandidate(
    const xvatsim::brain::RouteSectorMatchSnapshot& sector,
    const xvatsim::brain::RadioReachableControllerCandidate& candidate,
    std::string* reason) {
    for (const auto& pattern : sector.controllerCallsignPatterns) {
        if (BrainCallsignMatchesPattern(pattern, candidate.callsign)) {
            if (reason != nullptr) {
                *reason = "pattern:" + NormalizeCallsign(pattern);
            }
            return true;
        }
    }

    for (const auto& prefix : sector.controllerPrefixes) {
        if (BrainCallsignMatchesPrefix(prefix, candidate.callsign)) {
            if (reason != nullptr) {
                *reason = "prefix:" + NormalizeCallsign(prefix);
            }
            return true;
        }
    }

    for (const auto& token : sector.matchTokens) {
        if (BrainCallsignMatchesPrefix(token, candidate.callsign)) {
            if (reason != nullptr) {
                *reason = "token:" + NormalizeCallsign(token);
            }
            return true;
        }
    }

    if (BrainCallsignMatchesPrefix(sector.identifier, candidate.callsign)) {
        if (reason != nullptr) {
            *reason = "sector:" + NormalizeCallsign(sector.identifier);
        }
        return true;
    }
    return false;
}

struct BrainCenterRouteMatch {
    bool hasRouteMetadata = false;
    bool matched = false;
    std::string polygonKey;
    xvatsim::brain::DisplayRelation displayRelation =
        xvatsim::brain::DisplayRelation::Unknown;
    bool hasRouteEntryDistance = false;
    double routeEntryDistanceNm = 0.0;
    std::string reason;
};

BrainCenterRouteMatch BrainMatchCenterToRoutePolygon(
    const xvatsim::brain::BrainControllerRelevanceWorkerInput& input,
    const xvatsim::brain::RadioReachableControllerCandidate& candidate) {
    BrainCenterRouteMatch match;
    match.hasRouteMetadata =
        BrainSectorsHaveCenterMetadata(input.currentSectors) ||
        BrainSectorsHaveCenterMetadata(input.nextSectors);

    for (const auto& sector : input.currentSectors) {
        std::string proof;
        if (BrainSectorMatchesCenterCandidate(sector, candidate, &proof)) {
            match.matched = true;
            match.polygonKey =
                !sector.identifier.empty() ? sector.identifier : input.currentPolygonKey;
            match.displayRelation =
                xvatsim::brain::DisplayRelation::CurrentPolygon;
            match.hasRouteEntryDistance = false;
            match.routeEntryDistanceNm = 0.0;
            match.reason = "center-current-polygon-match:" + proof;
            return match;
        }
    }

    for (const auto& sector : input.nextSectors) {
        std::string proof;
        if (BrainSectorMatchesCenterCandidate(sector, candidate, &proof)) {
            match.matched = true;
            match.polygonKey =
                !sector.identifier.empty() ? sector.identifier : input.nextPolygonKey;
            match.displayRelation =
                xvatsim::brain::DisplayRelation::NextPolygon;
            match.hasRouteEntryDistance = true;
            match.routeEntryDistanceNm = std::max(
                0.0,
                sector.entryDistanceNm);
            match.reason = "center-next-polygon-match:" + proof;
            return match;
        }
    }

    match.reason = match.hasRouteMetadata ? "center-not-route-polygon-match"
                                          : "center-route-metadata-unavailable";
    return match;
}

struct Engineer3CenterCandidate {
    xvatsim::brain::BoardStationSnapshot station;
    xvatsim::brain::RadioReachableControllerCandidate candidate;
    bool hasDistanceNm = false;
    double distanceNm = 0.0;
};

void Engineer3AppendSelectedCenterStations(
    const std::vector<Engineer3CenterCandidate>& centerCandidates,
    xvatsim::brain::ModuleBoardSnapshot* enrouteBoard,
    std::unordered_set<std::string>* enrouteKeys) {
    if (enrouteBoard == nullptr || enrouteKeys == nullptr ||
        centerCandidates.empty()) {
        return;
    }

    for (const auto& candidate : centerCandidates) {
        auto station = candidate.station;
        Engineer3AppendStationUnique(station, enrouteBoard, enrouteKeys);
    }
}

xvatsim::brain::BrainControllerRelevanceWorkerOutput
RunBrainControllerRelevanceWorker(
    const xvatsim::brain::BrainControllerRelevanceWorkerInput& input) {
    using xvatsim::brain::BrainOwnedCandidateDecision;
    using xvatsim::brain::RadioReachableFacilityGroup;
    using xvatsim::brain::StationRole;
    using xvatsim::brain::WorkflowStage;

    xvatsim::brain::BrainControllerRelevanceWorkerOutput output;
    output.available = true;
    output.stale = false;
    output.reason = "controller-relevance-worker";
    output.departureBoard.source = xvatsim::brain::BoardSource::Departure;
    output.arrivalBoard.source = xvatsim::brain::BoardSource::Arrival;
    output.enrouteBoard.source = xvatsim::brain::BoardSource::Enroute;
    output.departureBoard.airportIcao = input.departureIcao;
    output.arrivalBoard.airportIcao = input.arrivalIcao;

    const auto departureTokens = BuildEngineer3AirportTokens(input.departureIcao);
    const auto arrivalTokens = BuildEngineer3AirportTokens(input.arrivalIcao);
    std::unordered_set<std::string> departureKeys;
    std::unordered_set<std::string> arrivalKeys;
    std::unordered_set<std::string> enrouteKeys;
    std::vector<Engineer3CenterCandidate> centerCandidates;

    const auto includeDepartureGroups =
        input.workflowStage == WorkflowStage::None ||
        input.workflowStage == WorkflowStage::Departure;
    const auto includeEnrouteGroups =
        input.workflowStage == WorkflowStage::None ||
        input.workflowStage == WorkflowStage::Departure ||
        input.workflowStage == WorkflowStage::Enroute ||
        input.workflowStage == WorkflowStage::Arrival;
    const auto includeArrivalGroups =
        input.workflowStage == WorkflowStage::None ||
        input.workflowStage == WorkflowStage::Arrival;

    for (const auto& candidate : input.candidates) {
        const auto role = Engineer3RoleFromRadioCandidate(candidate);
        xvatsim::brain::BoardStationSnapshot station;
        station.role = role;
        station.callsign = candidate.callsign;
        station.frequency = candidate.frequency;
        station.tuned = Engineer3FrequencyTuned(candidate.frequency, input.radios);
        station.online = true;
        station.sectorActive =
            role == StationRole::Center && station.tuned;
        station.hasRouteEntryDistance = candidate.hasDistanceNm;
        station.routeEntryDistanceNm = candidate.distanceNm;

        if (role == StationRole::Other ||
            candidate.group == RadioReachableFacilityGroup::Atis) {
            BrainRecordCandidateDecision(
                input,
                candidate,
                BrainOwnedCandidateDecision::Rejected,
                "facility-not-ui-relevant",
                station,
                &output.completions);
            continue;
        }

        if (Engineer3GuardFrequency(candidate.frequency)) {
            BrainRecordCandidateDecision(
                input,
                candidate,
                BrainOwnedCandidateDecision::Rejected,
                "guard-frequency-rejected",
                station,
                &output.completions);
            continue;
        }

        bool accepted = false;
        std::string reason;
        const auto localRole =
            role == StationRole::Delivery ||
            role == StationRole::Ground ||
            role == StationRole::Tower;
        const auto appDepRole =
            role == StationRole::Approach ||
            role == StationRole::Departure;

        if (role == StationRole::Center) {
            if (includeEnrouteGroups) {
                const auto routeMatch =
                    BrainMatchCenterToRoutePolygon(input, candidate);
                if (routeMatch.matched || !routeMatch.hasRouteMetadata ||
                    station.tuned) {
                    station.polygonKey = routeMatch.matched
                                             ? routeMatch.polygonKey
                                             : input.currentPolygonKey;
                    station.displayRelation =
                        routeMatch.matched
                            ? routeMatch.displayRelation
                            : xvatsim::brain::DisplayRelation::CurrentPolygon;
                    station.sectorActive =
                        station.displayRelation ==
                            xvatsim::brain::DisplayRelation::CurrentPolygon ||
                        station.tuned;
                    station.next =
                        station.displayRelation ==
                        xvatsim::brain::DisplayRelation::NextPolygon;
                    station.hasRouteEntryDistance =
                        routeMatch.matched &&
                        routeMatch.displayRelation ==
                            xvatsim::brain::DisplayRelation::NextPolygon &&
                        routeMatch.hasRouteEntryDistance;
                    station.routeEntryDistanceNm =
                        station.hasRouteEntryDistance
                            ? routeMatch.routeEntryDistanceNm
                            : 0.0;
                    centerCandidates.push_back(
                        {station,
                         candidate,
                         station.hasRouteEntryDistance,
                         station.routeEntryDistanceNm});
                    accepted = true;
                    reason = routeMatch.matched
                                 ? routeMatch.reason
                                 : (station.tuned
                                        ? "center-tuned-current-radio"
                                        : "center-route-metadata-unavailable-reachable");
                } else {
                    reason = routeMatch.reason;
                }
            } else {
                reason = "center-not-needed-for-phase";
            }
        } else if (includeDepartureGroups &&
                   localRole &&
                   Engineer3ControllerMatchesAirport(
                       candidate.callsign,
                       departureTokens)) {
            station.polygonKey = input.currentPolygonKey;
            station.displayRelation =
                xvatsim::brain::DisplayRelation::CurrentPolygon;
            Engineer3AppendStationUnique(
                station,
                &output.departureBoard,
                &departureKeys);
            accepted = true;
            reason = "departure-airport-match";
        } else if (includeDepartureGroups && appDepRole) {
            station.polygonKey = input.currentPolygonKey;
            station.displayRelation =
                xvatsim::brain::DisplayRelation::CurrentPolygon;
            Engineer3AppendStationUnique(
                station,
                &output.departureBoard,
                &departureKeys);
            accepted = true;
            reason = "departure-terminal-reachable";
        } else if (includeArrivalGroups &&
                   (localRole || appDepRole) &&
                   Engineer3ControllerMatchesAirport(
                       candidate.callsign,
                       arrivalTokens)) {
            station.polygonKey = input.arrivalPolygonKey;
            station.displayRelation =
                xvatsim::brain::DisplayRelation::ArrivalPrep;
            Engineer3AppendStationUnique(
                station,
                &output.arrivalBoard,
                &arrivalKeys);
            accepted = true;
            reason = "arrival-airport-match";
        } else {
            reason = "phase-or-airport-filter-rejected";
        }

        BrainRecordCandidateDecision(
            input,
            candidate,
            accepted ? BrainOwnedCandidateDecision::Accepted
                     : BrainOwnedCandidateDecision::Rejected,
            reason,
            station,
            &output.completions);
    }

    Engineer3AppendSelectedCenterStations(
        centerCandidates,
        &output.enrouteBoard,
        &enrouteKeys);
    for (auto& completion : output.completions) {
        if (completion.decision != BrainOwnedCandidateDecision::Accepted ||
            completion.facilityGroup != RadioReachableFacilityGroup::Center) {
            continue;
        }
        xvatsim::brain::BoardStationSnapshot station;
        station.role = StationRole::Center;
        station.callsign = completion.callsign;
        station.frequency = completion.frequency;
        completion.displayed = false;
    }
    return output;
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
    const auto canReuse =
        gBrainOwnedRuntimeState.candidatesComplete &&
        gBrainOwnedRuntimeState.hasRadioBoard &&
        gBrainOwnedRuntimeState.lastRadioBoardHash == input.radioBoardHash &&
        gBrainOwnedRuntimeState.routePolygonHash == input.routePolygonHash &&
        gBrainOwnedRuntimeState.lastWorkflowStage == input.workflowStage &&
        gBrainOwnedRuntimeState.currentPolygonKey == input.currentPolygonKey;

    if (canReuse) {
        xvatsim::brain::BrainControllerRelevanceWorkerOutput output;
        output.available = true;
        output.stale = false;
        output.reason = "board-unchanged-no-relevance-work";
        output.departureBoard =
            gBrainOwnedRuntimeState.relevanceDepartureBoardSnapshot;
        output.arrivalBoard =
            gBrainOwnedRuntimeState.relevanceArrivalBoardSnapshot;
        output.enrouteBoard =
            gBrainOwnedRuntimeState.relevanceEnrouteBoardSnapshot;
        output.completions = gBrainOwnedRuntimeState.candidateCompletions;
        gBrainOwnedRuntimeState.lastIdleReason =
            "board-unchanged-no-relevance-work";
        if (recordDiagnostics) {
            RecordDiagnosticJob(
                "BrainControllerRelevanceWorker",
                output.reason,
                0,
                "brain-controller-relevance-cache-hit",
                SummarizeBrainControllerRelevance(output),
                {},
                planKey);
        }
        return output;
    }

    const auto started = std::chrono::steady_clock::now();
    auto output = RunBrainControllerRelevanceWorker(input);
    const auto elapsedMs = ElapsedMicrosecondsSince(started) / 1000;

    gBrainOwnedRuntimeState.relevanceDepartureBoardSnapshot =
        output.departureBoard;
    gBrainOwnedRuntimeState.relevanceArrivalBoardSnapshot =
        output.arrivalBoard;
    gBrainOwnedRuntimeState.relevanceEnrouteBoardSnapshot =
        output.enrouteBoard;

    gBrainOwnedRuntimeState.candidateCompletions.clear();
    for (const auto& completion : output.completions) {
        xvatsim::brain::RecordBrainOwnedCandidateCompletion(
            &gBrainOwnedRuntimeState,
            completion);
    }
    gBrainOwnedRuntimeState.candidatesComplete = true;
    gBrainOwnedRuntimeState.lastIdleReason.clear();

    if (recordDiagnostics) {
        RecordDiagnosticJob(
            "BrainControllerRelevanceWorker",
            output.reason,
            elapsedMs,
            "brain-controller-relevance-ran",
            SummarizeBrainControllerRelevance(output),
            {},
            planKey);
    }
    return output;
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
    xvatsim::brain::BrainOwnedPublisherInput publisherInput;
    publisherInput.workflowStage = workflowStage;
    publisherInput.routeProgressDistanceNm =
        gBrainOwnedRuntimeState.routeProgressDistanceNm;
    publisherInput.currentPolygonKey =
        gBrainOwnedRuntimeState.currentPolygonKey;
    publisherInput.nextPolygonKey = gBrainOwnedRuntimeState.nextPolygonKey;
    publisherInput.arrivalPolygonKey =
        gBrainOwnedRuntimeState.arrivalPolygonKey;
    publisherInput.departureBoard = relevanceOutput.departureBoard;
    publisherInput.arrivalBoard = relevanceOutput.arrivalBoard;
    publisherInput.enrouteBoard = relevanceOutput.enrouteBoard;
    publisherInput.completions = relevanceOutput.completions;
    publisherInput.publishReason = "brain-owned-ui-publish";
    publisherInput.hasDepartureCtafStation =
        Engineer3BuildCtafStation(
            gFlightContext.departureIcao,
            departureCtafLookup,
            radioStateSnapshot,
            &publisherInput.departureCtafStation);
    publisherInput.hasArrivalCtafStation =
        Engineer3BuildCtafStation(
            gFlightContext.destinationIcao,
            arrivalCtafLookup,
            radioStateSnapshot,
            &publisherInput.arrivalCtafStation);

    auto output =
        xvatsim::brain::RunBrainOwnedPublisher(
            &gBrainOwnedRuntimeState,
            publisherInput);

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
        publisherInput.publishReason,
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
    return output;
}

HandoffDecision ResolveEngineer3WorkflowStage(
    const xvatsim::brain::AircraftStateSnapshot& aircraftState,
    const xvatsim::brain::RadioStateSnapshot& radioStateSnapshot,
    const xvatsim::brain::ModuleBoardSnapshot& departureBoardSnapshot,
    const xvatsim::brain::ModuleBoardSnapshot& enrouteBoardSnapshot) {
    xvatsim::core::workflow::WorkflowState state;
    state.flightContext = gFlightContext;
    state.departureReleasedThisFlight = gDepartureReleasedThisFlight;
    state.arrivalAwakeThisFlight = gArrivalAwakeThisFlight;
    state.airborneSinceSeconds = gAirborneSinceSeconds;

    xvatsim::core::workflow::WorkflowTuning tuning;
    tuning.arrivalWakeDistanceNm = kArrivalWakeDistanceNm;
    tuning.departureReleaseHoldSeconds = kDepartureReleaseHoldSeconds;

    const auto decision = xvatsim::core::workflow::ResolveWorkflowStage(
        aircraftState,
        radioStateSnapshot,
        false,
        false,
        departureBoardSnapshot,
        enrouteBoardSnapshot,
        XPLMGetElapsedTime(),
        &state,
        tuning);

    gDepartureReleasedThisFlight = state.departureReleasedThisFlight;
    gArrivalAwakeThisFlight = state.arrivalAwakeThisFlight;
    gAirborneSinceSeconds = static_cast<float>(state.airborneSinceSeconds);
    return decision;
}

xvatsim::brain::BrainRadioRangeWorkerOutput RunBrainRadioRangeWorker(
    const xvatsim::brain::BrainRadioRangeWorkerInput& input,
    RefreshDiagnosticsFrame* diagnostics) {
    xvatsim::brain::BrainRadioRangeWorkerOutput output;

    const auto started = std::chrono::steady_clock::now();
    output.transceivers =
        gTransceiverResolver.Resolve(input.aircraft, input.controllerFeed);
    const auto resolveUs = ElapsedMicrosecondsSince(started);
    if (diagnostics != nullptr) {
        diagnostics->activeTransceiverResolveUs = resolveUs;
        diagnostics->activeTransceiverResolveMs = resolveUs / 1000;
    }

    xvatsim::brain::RadioReachableBuildOptions options;
    options.available = output.transceivers.available;
    options.stale = output.transceivers.stale;
    options.generation = input.controllerFeed.generation;
    options.source = xvatsim::brain::RadioReachableSource::AFVRadioRange;
    options.changeReason = "brain-radio-range-worker";
    options.nowSeconds = static_cast<double>(CurrentTickSeconds());
    output.radioBoard =
        xvatsim::brain::BuildRadioReachableControllerSnapshotFromTransceivers(
            output.transceivers,
            input.controllerFeed,
            options);
    output.available = output.radioBoard.available;
    output.stale = output.radioBoard.stale;
    output.reason = output.radioBoard.statusLine;
    return output;
}

xvatsim::brain::RadioReachableControllerSnapshot BuildEngineer3RadioSnapshot(
    const xvatsim::brain::AircraftStateSnapshot& aircraftState,
    const xvatsim::brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const std::string& planKey,
    RefreshDiagnosticsFrame* diagnostics) {
    const auto nowSeconds = CurrentTickSeconds();
    const auto canReuse =
        gBrainOwnedRuntimeState.hasRadioBoard &&
        gBrainOwnedRuntimeState.lastControllerGeneration ==
            controllerFeedSnapshot.generation &&
        (nowSeconds - gBrainOwnedRuntimeState.lastRadioBoardRefreshSeconds) <
            kEngineer3RadioBoardRefreshSeconds;
    if (canReuse) {
        if (diagnostics != nullptr) {
            diagnostics->activeTransceiverResolveUs = 0;
            diagnostics->activeTransceiverResolveMs = 0;
        }
        RecordDiagnosticJob(
            "Engineer3RadioBoard",
            "board-unchanged-no-authority-work",
            0,
            "clean-runtime-cache-hit",
            gBrainOwnedRuntimeState.radioSnapshot.statusLine,
            {},
            planKey);
        return gBrainOwnedRuntimeState.radioSnapshot;
    }

    xvatsim::brain::BrainRadioRangeWorkerInput workerInput;
    workerInput.aircraft = aircraftState;
    workerInput.controllerFeed = controllerFeedSnapshot;
    workerInput.planKey = planKey;
    const auto workerOutput =
        RunBrainRadioRangeWorker(workerInput, diagnostics);
    const auto radioSnapshot = workerOutput.radioBoard;

    const auto previousRadioSnapshot = gBrainOwnedRuntimeState.radioSnapshot;
    const auto diff =
        xvatsim::brain::DiffRadioReachableSnapshots(
            previousRadioSnapshot,
            radioSnapshot);
    const auto boardChanged =
        !gBrainOwnedRuntimeState.hasRadioBoard ||
        previousRadioSnapshot.stableHash != radioSnapshot.stableHash;

    gBrainOwnedRuntimeState.hasRadioBoard = true;
    gBrainOwnedRuntimeState.lastRadioBoardRefreshSeconds = nowSeconds;
    gBrainOwnedRuntimeState.lastControllerGeneration =
        controllerFeedSnapshot.generation;
    gBrainOwnedRuntimeState.transceiverSnapshot = workerOutput.transceivers;
    gBrainOwnedRuntimeState.radioSnapshot = radioSnapshot;
    gBrainOwnedRuntimeState.radioDiff = diff;
    gBrainOwnedRuntimeState.lastWakeReason =
        boardChanged ? "radio-board-changed" : "radio-board-refresh";
    if (boardChanged) {
        gBrainOwnedRuntimeState.candidateCompletions.clear();
        gBrainOwnedRuntimeState.candidatesComplete = false;
    }

    std::ostringstream result;
    result << radioSnapshot.statusLine << "," << diff.statusLine;
    RecordDiagnosticJob(
        "Engineer3RadioBoard",
        boardChanged ? "radio-board-changed" : "board-unchanged-no-authority-work",
        diagnostics != nullptr ? diagnostics->activeTransceiverResolveMs : 0,
        boardChanged ? "clean-runtime-board-refresh"
                     : "radio-board-runtime-idle",
        result.str(),
        {},
        planKey);
    return radioSnapshot;
}


std::string BuildRadioBoardRouteRuntimeKey(
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    const auto planKey = BuildNetworkPlanIdentityKey(networkPlanSnapshot);
    if (planKey.empty()) {
        return {};
    }
    return planKey + "|route=" + networkPlanSnapshot.routeText;
}

std::string FirstRoutePolygonKey(
    const std::vector<xvatsim::brain::RouteSectorMatchSnapshot>& sectors) {
    for (const auto& sector : sectors) {
        if (!sector.identifier.empty()) {
            return sector.identifier;
        }
    }
    return {};
}

std::string LastRoutePolygonKey(
    const xvatsim::brain::RouteSectorSnapshot& route) {
    std::string lastKey = FirstRoutePolygonKey(route.currentSectors);
    double lastEntryDistanceNm = -1.0;
    for (const auto& sector : route.currentSectors) {
        if (!sector.identifier.empty() &&
            sector.entryDistanceNm >= lastEntryDistanceNm) {
            lastKey = sector.identifier;
            lastEntryDistanceNm = sector.entryDistanceNm;
        }
    }
    for (const auto& sector : route.nextSectors) {
        if (!sector.identifier.empty() &&
            sector.entryDistanceNm >= lastEntryDistanceNm) {
            lastKey = sector.identifier;
            lastEntryDistanceNm = sector.entryDistanceNm;
        }
    }
    return lastKey;
}

bool ApplyRoutePolygonTransitionToOutput(
    const xvatsim::brain::AircraftStateSnapshot& aircraftState,
    const std::string& routeRuntimeKey,
    xvatsim::brain::BrainRoutePolygonWorkerOutput* output) {
    if (output == nullptr || !output->route.available ||
        output->route.stale || !output->route.routeResolved) {
        return false;
    }

    xvatsim::brain::RoutePolygonTransitionWorkerInput input;
    input.aircraft = aircraftState;
    input.route = output->route;
    input.previousPolygonKey = gBrainOwnedRuntimeState.currentPolygonKey;
    const auto transition =
        xvatsim::brain::RunRoutePolygonTransitionWorker(input);

    if (transition.available && transition.routeResolved) {
        output->route = transition.route;
        output->routePolygonHash =
            static_cast<std::uint64_t>(HashRouteSectorSnapshot(output->route));
        output->currentPolygonIndex = transition.currentPolygonIndex;
        output->currentPolygonKey = transition.currentPolygonKey;
        output->nextPolygonKey = transition.nextPolygonKey;
        output->arrivalPolygonKey = transition.finalRoutePolygonKey.empty()
                                        ? output->arrivalPolygonKey
                                        : transition.finalRoutePolygonKey;
        output->finalRoutePolygonKey = transition.finalRoutePolygonKey;
        gBrainOwnedRuntimeState.routeProgressDistanceNm =
            transition.progressDistanceNm;
        gBrainOwnedRuntimeState.finalRoutePolygonKey =
            transition.finalRoutePolygonKey;
        gBrainOwnedRuntimeState.lastRoutePolygonTransitionReason =
            transition.reason;
        gBrainOwnedRuntimeState.lastRoutePolygonTransitionChanged =
            transition.changed;
    }

    std::ostringstream result;
    result << "available=" << (transition.available ? 1 : 0)
           << ",stale=" << (transition.stale ? 1 : 0)
           << ",resolved=" << (transition.routeResolved ? 1 : 0)
           << ",changed=" << (transition.changed ? 1 : 0)
           << ",wakeUi=" << (transition.shouldWakeUi ? 1 : 0)
           << ",final=" << (transition.enteredFinalRoutePolygon ? 1 : 0)
           << ",progressNm=" << FormatFixed(transition.progressDistanceNm, 1)
           << ",previous=" << transition.previousPolygonKey
           << ",current=" << transition.currentPolygonKey
           << ",next=" << transition.nextPolygonKey
           << ",finalKey=" << transition.finalRoutePolygonKey;
    RecordDiagnosticJob(
        "BrainRoutePolygonTransitionWorker",
        transition.reason,
        0,
        transition.changed ? "route-polygon-transition"
                           : "route-polygon-stable",
        result.str(),
        {},
        routeRuntimeKey);
    return transition.changed;
}

xvatsim::brain::BrainRoutePolygonWorkerOutput RunBrainRoutePolygonWorker(
    const xvatsim::brain::BrainRoutePolygonWorkerInput& input,
    RefreshDiagnosticsFrame* diagnostics) {
    xvatsim::brain::BrainRoutePolygonWorkerOutput output;
    if (input.planKey.empty()) {
        output.reason = "route-plan-key-unavailable";
        return output;
    }

    ApplyPreflightRouteCacheForPlanIfNeeded(input.networkPlan);

    const auto timingStarted = std::chrono::steady_clock::now();
    output.route =
        gRouteSectorResolver.Resolve(input.aircraft, input.networkPlan);
    const auto routeResolveUs = ElapsedMicrosecondsSince(timingStarted);
    if (diagnostics != nullptr) {
        diagnostics->routeResolveUs = routeResolveUs;
        diagnostics->routeResolveMs = routeResolveUs / 1000;
        diagnostics->routeResolved = output.route.routeResolved;
        diagnostics->routeStatus = output.route.statusLine;
    }

    output.available = output.route.available;
    output.stale = output.route.stale;
    output.routePolygonHash =
        static_cast<std::uint64_t>(HashRouteSectorSnapshot(output.route));
    output.currentPolygonIndex = output.route.currentSectors.empty() ? 0 : 1;
    output.currentPolygonKey = FirstRoutePolygonKey(output.route.currentSectors);
    output.nextPolygonKey = FirstRoutePolygonKey(output.route.nextSectors);
    output.arrivalPolygonKey = LastRoutePolygonKey(output.route);
    output.finalRoutePolygonKey = output.arrivalPolygonKey;
    output.reason = output.route.diagnosticReason.empty()
                        ? "route-polygon-worker"
                        : output.route.diagnosticReason;
    return output;
}

xvatsim::brain::BrainRoutePolygonWorkerOutput RefreshBrainRoutePolygonSnapshot(
    const xvatsim::brain::AircraftStateSnapshot& aircraftState,
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot,
    RefreshDiagnosticsFrame* diagnostics) {
    xvatsim::brain::BrainRoutePolygonWorkerOutput output;
    const auto planKey = BuildNetworkPlanIdentityKey(networkPlanSnapshot);
    const auto routeRuntimeKey = BuildRadioBoardRouteRuntimeKey(networkPlanSnapshot);
    if (routeRuntimeKey.empty()) {
        gBrainOwnedRuntimeState.hasRoutePolygonSnapshot = false;
        gBrainOwnedRuntimeState.routePlanKey.clear();
        gBrainOwnedRuntimeState.routePolygonSnapshot = {};
        gBrainOwnedRuntimeState.routePolygonHash = 0;
        gBrainOwnedRuntimeState.currentPolygonIndex = 0;
        gBrainOwnedRuntimeState.currentPolygonKey.clear();
        gBrainOwnedRuntimeState.nextPolygonKey.clear();
        gBrainOwnedRuntimeState.arrivalPolygonKey.clear();
        gBrainOwnedRuntimeState.finalRoutePolygonKey.clear();
        gBrainOwnedRuntimeState.routeProgressDistanceNm = 0.0;
        gBrainOwnedRuntimeState.lastRoutePolygonTransitionReason.clear();
        gBrainOwnedRuntimeState.lastRoutePolygonTransitionChanged = false;
        RecordDiagnosticJob(
            "BrainRoutePolygonWorker",
            "route-plan-unavailable",
            0,
            "route-polygon-input-unavailable",
            "available=0,stale=1,resolved=0",
            {},
            planKey);
        return output;
    }

    const auto nowSeconds = CurrentTickSeconds();
    const auto sameRoute =
        gBrainOwnedRuntimeState.hasRoutePolygonSnapshot &&
        gBrainOwnedRuntimeState.routePlanKey == routeRuntimeKey;
    const auto cachedRouteUsable =
        sameRoute &&
        gBrainOwnedRuntimeState.routePolygonSnapshot.available &&
        !gBrainOwnedRuntimeState.routePolygonSnapshot.stale &&
        gBrainOwnedRuntimeState.routePolygonSnapshot.routeResolved;
    const auto pendingRetryDue =
        sameRoute &&
        !cachedRouteUsable &&
        (nowSeconds - gBrainOwnedRuntimeState.lastRoutePolygonRefreshSeconds) >=
            kRadioBoardPendingRouteRetrySeconds;
    if (sameRoute && (cachedRouteUsable || !pendingRetryDue)) {
        output.available = gBrainOwnedRuntimeState.routePolygonSnapshot.available;
        output.stale = gBrainOwnedRuntimeState.routePolygonSnapshot.stale;
        output.route = gBrainOwnedRuntimeState.routePolygonSnapshot;
        output.routePolygonHash = gBrainOwnedRuntimeState.routePolygonHash;
        output.currentPolygonIndex = gBrainOwnedRuntimeState.currentPolygonIndex;
        output.currentPolygonKey = gBrainOwnedRuntimeState.currentPolygonKey;
        output.nextPolygonKey = gBrainOwnedRuntimeState.nextPolygonKey;
        output.arrivalPolygonKey = gBrainOwnedRuntimeState.arrivalPolygonKey;
        output.finalRoutePolygonKey = gBrainOwnedRuntimeState.finalRoutePolygonKey;
        output.reason = cachedRouteUsable ? "route-polygon-unchanged"
                                          : "route-polygon-pending-retry";
        const auto previousPolygonKey =
            gBrainOwnedRuntimeState.currentPolygonKey;
        const auto transitionChanged = cachedRouteUsable &&
            ApplyRoutePolygonTransitionToOutput(
                aircraftState,
                routeRuntimeKey,
                &output);

        if (transitionChanged) {
            gBrainOwnedRuntimeState.routePolygonSnapshot = output.route;
            gBrainOwnedRuntimeState.routePolygonHash = output.routePolygonHash;
            gBrainOwnedRuntimeState.currentPolygonIndex =
                output.currentPolygonIndex;
            gBrainOwnedRuntimeState.currentPolygonKey = output.currentPolygonKey;
            gBrainOwnedRuntimeState.nextPolygonKey = output.nextPolygonKey;
            gBrainOwnedRuntimeState.arrivalPolygonKey = output.arrivalPolygonKey;
            gBrainOwnedRuntimeState.lastRoutePolygonHash =
                output.routePolygonHash;
            gBrainOwnedRuntimeState.candidateCompletions.clear();
            gBrainOwnedRuntimeState.candidatesComplete = false;
            gBrainOwnedRuntimeState.lastWakeReason =
                output.currentPolygonKey == output.finalRoutePolygonKey
                    ? "route-polygon-transition-final"
                    : "route-polygon-transition";
        }

        if (diagnostics != nullptr) {
            diagnostics->routeResolved =
                gBrainOwnedRuntimeState.routePolygonSnapshot.routeResolved;
            diagnostics->routeStatus =
                gBrainOwnedRuntimeState.routePolygonSnapshot.statusLine;
        }

        std::ostringstream result;
        result << "available=" << (output.available ? 1 : 0)
               << ",stale=" << (output.stale ? 1 : 0)
               << ",resolved="
               << (output.route.routeResolved ? 1 : 0)
               << ",current=" << output.currentPolygonKey
               << ",next=" << output.nextPolygonKey
               << ",final=" << output.finalRoutePolygonKey
               << ",hash=" << output.routePolygonHash
               << ",transition=" << (transitionChanged ? 1 : 0)
               << ",previous=" << previousPolygonKey
               << ",progressNm="
               << FormatFixed(gBrainOwnedRuntimeState.routeProgressDistanceNm, 1);
        RecordDiagnosticJob(
            "BrainRoutePolygonWorker",
            transitionChanged ? "route-polygon-transition-applied"
                              : output.reason,
            0,
            cachedRouteUsable ? "route-polygon-cache-hit"
                              : "route-polygon-cache-wait",
            result.str(),
            {},
            routeRuntimeKey);
        return output;
    }

    xvatsim::brain::BrainRoutePolygonWorkerInput input;
    input.aircraft = aircraftState;
    input.networkPlan = networkPlanSnapshot;
    input.planKey = planKey;
    output = RunBrainRoutePolygonWorker(input, diagnostics);
    const auto transitionChanged =
        ApplyRoutePolygonTransitionToOutput(
            aircraftState,
            routeRuntimeKey,
            &output);

    const auto routeChanged =
        !gBrainOwnedRuntimeState.hasRoutePolygonSnapshot ||
        gBrainOwnedRuntimeState.routePlanKey != routeRuntimeKey ||
        gBrainOwnedRuntimeState.routePolygonHash != output.routePolygonHash ||
        gBrainOwnedRuntimeState.currentPolygonKey != output.currentPolygonKey ||
        transitionChanged;

    gBrainOwnedRuntimeState.hasRoutePolygonSnapshot = true;
    gBrainOwnedRuntimeState.lastRoutePolygonRefreshSeconds = nowSeconds;
    gBrainOwnedRuntimeState.routePlanKey = routeRuntimeKey;
    gBrainOwnedRuntimeState.routePolygonSnapshot = output.route;
    gBrainOwnedRuntimeState.routePolygonHash = output.routePolygonHash;
    gBrainOwnedRuntimeState.currentPolygonIndex = output.currentPolygonIndex;
    gBrainOwnedRuntimeState.currentPolygonKey = output.currentPolygonKey;
    gBrainOwnedRuntimeState.nextPolygonKey = output.nextPolygonKey;
    gBrainOwnedRuntimeState.arrivalPolygonKey = output.arrivalPolygonKey;
    gBrainOwnedRuntimeState.finalRoutePolygonKey = output.finalRoutePolygonKey;
    gBrainOwnedRuntimeState.lastRoutePolygonHash = output.routePolygonHash;
    if (routeChanged) {
        gBrainOwnedRuntimeState.candidateCompletions.clear();
        gBrainOwnedRuntimeState.candidatesComplete = false;
        gBrainOwnedRuntimeState.lastWakeReason =
            transitionChanged
                ? (output.currentPolygonKey == output.finalRoutePolygonKey
                       ? "route-polygon-transition-final"
                       : "route-polygon-transition")
                : "route-polygon-changed";
    }

    std::ostringstream result;
    result << "available=" << (output.available ? 1 : 0)
           << ",stale=" << (output.stale ? 1 : 0)
           << ",resolved=" << (output.route.routeResolved ? 1 : 0)
           << ",current=" << output.currentPolygonKey
           << ",next=" << output.nextPolygonKey
           << ",final=" << output.finalRoutePolygonKey
           << ",hash=" << output.routePolygonHash
           << ",changed=" << (routeChanged ? 1 : 0)
           << ",transition=" << (transitionChanged ? 1 : 0)
           << ",progressNm="
           << FormatFixed(gBrainOwnedRuntimeState.routeProgressDistanceNm, 1);
    RecordDiagnosticJob(
        "BrainRoutePolygonWorker",
        output.reason,
        diagnostics != nullptr ? diagnostics->routeResolveMs : 0,
        output.route.diagnosticCacheStatus.empty()
            ? "route-polygon-worker"
            : output.route.diagnosticCacheStatus,
        result.str(),
        {},
        routeRuntimeKey);
    return output;
}

xvatsim::brain::FlightPlanSnapshot SampleFlightPlanForRuntime(
    const xvatsim::brain::AircraftStateSnapshot& aircraftState,
    RefreshDiagnosticsFrame* diagnostics) {
    const auto nowSeconds = CurrentTickSeconds();
    const auto canReuseActiveFlightPlan =
        gFlightContext.active &&
        gHasRuntimeFlightPlanSnapshot &&
        (nowSeconds - gLastRuntimeFlightPlanSampleSeconds) <
            kActiveFlightPlanSampleCadenceSeconds;
    if (canReuseActiveFlightPlan) {
        if (diagnostics != nullptr) {
            diagnostics->flightPlanUs = 0;
            diagnostics->flightPlanMs = 0;
        }
        RecordDiagnosticJob(
            "FlightPlanSampler",
            "active-flight-context-cadence-hit",
            0,
            "flight-plan-cache-hit",
            "sample=skipped",
            {},
            gFlightContext.departureIcao + "->" + gFlightContext.destinationIcao);
        return gLastFlightPlanSnapshot;
    }

    const auto timingStarted = std::chrono::steady_clock::now();
    auto snapshot = gFlightPlanSampler.Sample(aircraftState);
    if (diagnostics != nullptr) {
        diagnostics->flightPlanUs = ElapsedMicrosecondsSince(timingStarted);
        diagnostics->flightPlanMs = diagnostics->flightPlanUs / 1000;
    }
    gHasRuntimeFlightPlanSnapshot = true;
    gLastRuntimeFlightPlanSampleSeconds = nowSeconds;
    return snapshot;
}

bool RadioBoardDiffChanged(
    const xvatsim::brain::RadioReachableCandidateDiff& diff,
    bool firstSnapshot) {
    return firstSnapshot ||
           diff.previousHash != diff.currentHash ||
           diff.added > 0 ||
           diff.removed > 0;
}

bool IsRadioBoardRouteMapReady(
    const xvatsim::brain::RouteSectorSnapshot& routeSectorSnapshot,
    const xvatsim::brain::RouteAuthorityPlan& routeAuthorityPlan) {
    return routeSectorSnapshot.available &&
           !routeSectorSnapshot.stale &&
           routeSectorSnapshot.routeResolved &&
           routeAuthorityPlan.available &&
           !routeAuthorityPlan.stale &&
           routeAuthorityPlan.routeResolved;
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

xvatsim::brain::WorkflowStage WorkflowStageFromDiagnosticToken(
    const std::string& stage) {
    if (stage == "DEP") {
        return xvatsim::brain::WorkflowStage::Departure;
    }
    if (stage == "ENR") {
        return xvatsim::brain::WorkflowStage::Enroute;
    }
    if (stage == "ARR") {
        return xvatsim::brain::WorkflowStage::Arrival;
    }
    return xvatsim::brain::WorkflowStage::None;
}

bool ContainsDiagnosticText(const std::string& value, const std::string& token) {
    return value.find(token) != std::string::npos;
}

xvatsim::brain::BrainWorkReason BrainWorkReasonFromDiagnosticJob(
    const DiagnosticJobRecord& job) {
    using xvatsim::brain::BrainWorkReason;
    if (ContainsDiagnosticText(job.reason, "route-key") ||
        ContainsDiagnosticText(job.reason, "route-identity")) {
        return BrainWorkReason::RouteIdentityChanged;
    }
    if (ContainsDiagnosticText(job.reason, "source") ||
        ContainsDiagnosticText(job.reason, "signature")) {
        return BrainWorkReason::SourceGenerationChanged;
    }
    if (ContainsDiagnosticText(job.reason, "movement")) {
        return BrainWorkReason::AircraftMovementThreshold;
    }
    if (ContainsDiagnosticText(job.reason, "arrival") ||
        ContainsDiagnosticText(job.name, "Arrival")) {
        return BrainWorkReason::ArrivalWakeDistance;
    }
    if (ContainsDiagnosticText(job.reason, "departure") ||
        ContainsDiagnosticText(job.name, "Departure")) {
        return BrainWorkReason::NewFlightPlan;
    }
    if (ContainsDiagnosticText(job.reason, "next") ||
        ContainsDiagnosticText(job.reason, "lookahead")) {
        return BrainWorkReason::NextPolygonLookahead;
    }
    if (ContainsDiagnosticText(job.reason, "empty")) {
        return BrainWorkReason::EmptyPolygonRecheck;
    }
    if (ContainsDiagnosticText(job.reason, "reconnect")) {
        return BrainWorkReason::ReconnectRecovery;
    }
    if (ContainsDiagnosticText(job.reason, "manual")) {
        return BrainWorkReason::ManualRecovery;
    }
    if (ContainsDiagnosticText(job.reason, "board") ||
        ContainsDiagnosticText(job.name, "Board")) {
        return BrainWorkReason::BoardSnapshot;
    }
    if (ContainsDiagnosticText(job.reason, "ui") ||
        ContainsDiagnosticText(job.name, "Overlay")) {
        return BrainWorkReason::UiRefresh;
    }
    if (ContainsDiagnosticText(job.reason, "future")) {
        return BrainWorkReason::FutureRoutePrep;
    }
    if (ContainsDiagnosticText(job.name, "Authority") ||
        ContainsDiagnosticText(job.name, "Center")) {
        return BrainWorkReason::CurrentPolygonChanged;
    }
    return BrainWorkReason::Unknown;
}

xvatsim::brain::BrainWorkType BrainWorkTypeFromDiagnosticJob(
    const DiagnosticJobRecord& job) {
    using xvatsim::brain::BrainWorkType;
    if (job.name == "RouteResolve" || job.name == "RouteAuthorityPlan") {
        return BrainWorkType::BuildRouteScopedMap;
    }
    if (job.name == "DepartureAirportCoverage") {
        return BrainWorkType::ResolveDepartureAirportLocal;
    }
    if (job.name == "ArrivalAirportCoverage") {
        return BrainWorkType::ResolveArrivalAirportLocal;
    }
    if (job.name == "DepartureBoard") {
        return BrainWorkType::BuildDepartureSnapshot;
    }
    if (job.name == "BrainDepartureSnapshot") {
        return BrainWorkType::BuildDepartureSnapshot;
    }
    if (job.name == "BrainDepartureCurrentCenter") {
        return BrainWorkType::ResolveCurrentCenter;
    }
    if (job.name == "ArrivalBoard") {
        return BrainWorkType::BuildArrivalSnapshot;
    }
    if (job.name == "EnrouteBoard") {
        return BrainWorkType::BuildEnrouteSnapshot;
    }
    if (job.name == "AuthorityRelevance") {
        return ContainsDiagnosticText(job.cacheStatus, "proof")
                   ? BrainWorkType::RunAuthorityProof
                   : BrainWorkType::RunAuthorityFastPath;
    }
    if (job.name == "BrainAuthoritySchedule") {
        return BrainWorkType::RunAuthorityProof;
    }
    if (job.name == "AuthorityStationResolve" ||
        job.name == "ActiveTransceiverResolve") {
        return BrainWorkType::RunAuthorityFastPath;
    }
    if (job.name == "OverlayBuild" || job.name == "OverlayUpdate") {
        return BrainWorkType::PublishUiSnapshot;
    }
    return BrainWorkType::Diagnostics;
}

xvatsim::brain::BrainWorkPriority BrainWorkPriorityFromDiagnosticJob(
    const DiagnosticJobRecord& job,
    xvatsim::brain::WorkflowStage stage) {
    using xvatsim::brain::BrainWorkPriority;
    if (job.name == "RouteResolve" || job.name == "RouteAuthorityPlan") {
        return BrainWorkPriority::SafetyCurrentPosition;
    }
    if (job.name == "DepartureAirportCoverage" ||
        job.name == "DepartureBoard" ||
        job.name == "BrainDepartureSnapshot" ||
        job.name == "BrainDepartureCurrentCenter" ||
        job.name == "BrainDepartureSchedule") {
        return BrainWorkPriority::DepartureAuthority;
    }
    if (job.name == "ArrivalAirportCoverage" ||
        job.name == "ArrivalBoard") {
        return BrainWorkPriority::ArrivalAuthority;
    }
    if (job.name == "AuthorityRelevance" ||
        job.name == "AuthorityStationResolve" ||
        job.name == "ActiveTransceiverResolve" ||
        job.name == "BrainAuthoritySchedule") {
        if (stage == xvatsim::brain::WorkflowStage::Departure) {
            return BrainWorkPriority::DepartureAuthority;
        }
        if (stage == xvatsim::brain::WorkflowStage::Arrival) {
            return BrainWorkPriority::ArrivalAuthority;
        }
        return BrainWorkPriority::CurrentEnrouteAuthority;
    }
    if (job.name == "EnrouteBoard") {
        return BrainWorkPriority::CurrentEnrouteAuthority;
    }
    return BrainWorkPriority::Diagnostics;
}

xvatsim::brain::BrainWorkBudget BrainWorkBudgetFromDiagnosticJob(
    const DiagnosticJobRecord& job) {
    using xvatsim::brain::BrainWorkBudget;
    if (job.durationMs >= 80) {
        return BrainWorkBudget::Heavy;
    }
    if (job.name == "RouteResolve" &&
        !ContainsDiagnosticText(job.cacheStatus, "cache-hit")) {
        return BrainWorkBudget::Heavy;
    }
    if (job.name == "AuthorityRelevance" &&
        ContainsDiagnosticText(job.cacheStatus, "authority-proof-build")) {
        return BrainWorkBudget::Heavy;
    }
    if (job.durationMs >= 30) {
        return BrainWorkBudget::Medium;
    }
    if (job.name == "DepartureAirportCoverage" ||
        job.name == "ArrivalAirportCoverage" ||
        job.name == "AuthorityStationResolve" ||
        job.name == "ActiveTransceiverResolve") {
        return ContainsDiagnosticText(job.cacheStatus, "cache-hit")
                   ? BrainWorkBudget::Light
                   : BrainWorkBudget::Medium;
    }
    if (job.name == "BrainDepartureSnapshot") {
        return ContainsDiagnosticText(job.cacheStatus, "cache-hit")
                   ? BrainWorkBudget::Light
                   : BrainWorkBudget::Medium;
    }
    if (job.name == "BrainDepartureCurrentCenter" ||
        job.name == "BrainDepartureSchedule" ||
        job.name == "BrainAuthoritySchedule") {
        return BrainWorkBudget::Light;
    }
    if (ContainsDiagnosticText(job.name, "Board")) {
        return ContainsDiagnosticText(job.cacheStatus, "cache-hit")
                   ? BrainWorkBudget::Light
                   : BrainWorkBudget::Medium;
    }
    return BrainWorkBudget::Light;
}

std::vector<xvatsim::brain::BrainWorkItem> BuildShadowBrainWorkItems(
    const RefreshDiagnosticsFrame& frame) {
    std::vector<xvatsim::brain::BrainWorkItem> items;
    items.reserve(frame.jobs.size());

    std::uint64_t enqueueSequence = 0;
    for (const auto& job : frame.jobs) {
        xvatsim::brain::BrainWorkItem item;
        item.type = BrainWorkTypeFromDiagnosticJob(job);
        item.reason = BrainWorkReasonFromDiagnosticJob(job);
        item.budget = BrainWorkBudgetFromDiagnosticJob(job);
        item.target.stage = WorkflowStageFromDiagnosticToken(
            job.stage.empty() ? frame.stage : job.stage);
        item.priority = BrainWorkPriorityFromDiagnosticJob(job, item.target.stage);
        item.cacheKey = job.routeKey.empty()
                            ? job.name + ":" + job.cacheStatus
                            : job.routeKey;
        item.enqueueSequence = enqueueSequence++;
        items.push_back(std::move(item));
    }

    return items;
}

std::string CompactBrainWorkLabel(const xvatsim::brain::BrainWorkItem& item) {
    std::ostringstream stream;
    stream << xvatsim::brain::ToString(item.type)
           << ":" << xvatsim::brain::ToString(item.priority)
           << ":" << xvatsim::brain::ToString(item.budget);
    return stream.str();
}

std::string SummarizeBrainWorkItems(
    const std::vector<xvatsim::brain::BrainWorkItem>& items,
    std::size_t maxItems = 8) {
    if (items.empty()) {
        return "none";
    }

    std::ostringstream stream;
    const auto countToLog = std::min<std::size_t>(items.size(), maxItems);
    for (std::size_t index = 0; index < countToLog; ++index) {
        if (index > 0) {
            stream << ";";
        }
        stream << CompactBrainWorkLabel(items[index]);
    }
    if (items.size() > countToLog) {
        stream << ";+" << (items.size() - countToLog);
    }
    return stream.str();
}

std::string SummarizeShadowBrainScheduler(const RefreshDiagnosticsFrame& frame) {
    const auto requestedItems = BuildShadowBrainWorkItems(frame);
    if (requestedItems.empty()) {
        return "none";
    }

    xvatsim::brain::BrainWorkScheduler scheduler;
    const auto plan = scheduler.PlanCycle(requestedItems);

    std::ostringstream stream;
    stream << "mode=shadow"
           << " requested=" << plan.requestedItems.size()
           << " heavy=" << plan.requestedHeavyCount
           << " runnable=" << plan.runnableItems.size()
           << " heavyRun=" << plan.runnableHeavyCount
           << " deferred=" << plan.deferredItems.size()
           << " heavyDeferred=" << plan.deferredHeavyCount
           << " multiHeavy=" << (plan.RequestedMultipleHeavyJobs() ? 1 : 0)
           << " run=[" << SummarizeBrainWorkItems(plan.runnableItems) << "]"
           << " defer=[" << SummarizeBrainWorkItems(plan.deferredItems) << "]";
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

std::filesystem::path ResolveDiagnosticsLogPath() {
    return ResolvePluginRootPath() / "logs" / "xvatsim_diagnostics.log";
}

void AppendDiagnosticsLogLine(const std::string& line) {
    const auto logPath = ResolveDiagnosticsLogPath();
    std::error_code error;
    std::filesystem::create_directories(logPath.parent_path(), error);
    if (error) {
        return;
    }

    if (std::filesystem::exists(logPath, error) && !error &&
        std::filesystem::file_size(logPath, error) > kDiagnosticsMaxLogBytes &&
        !error) {
        const auto archivePath = logPath.parent_path() / "xvatsim_diagnostics.log.1";
        std::filesystem::remove(archivePath, error);
        error.clear();
        std::filesystem::rename(logPath, archivePath, error);
        if (error) {
            error.clear();
            std::filesystem::remove(logPath, error);
        }
    }

    std::ofstream output(logPath, std::ios::app);
    if (!output) {
        return;
    }

    output << "tick=" << CurrentTickSeconds() << " " << line << "\n";
}

long long SumTrackedRefreshMicroseconds(const RefreshDiagnosticsFrame& frame) {
    return frame.aircraftStateUs +
           frame.xpilotPollUs +
           frame.vatsimFeedUs +
           frame.controllerFeedUs +
           frame.flightPlanUs +
           frame.networkPlanUs +
           frame.radioUs +
           frame.controllerMessageUs +
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
           frame.activeTransceiverResolveUs +
           frame.overlayBuildUs +
           frame.overlayUpdateUs +
           frame.displayLoggingUs;
}

void MaybeLogRefreshDiagnostics(long long totalRefreshMs, long long totalRefreshUs) {
    if (!gRefreshDiagnosticsFrame.valid) {
        return;
    }

    auto& frame = gRefreshDiagnosticsFrame;
    frame.authorityProofSummary =
        SanitizeLogText(frame.authorityProofSummary, 1200);
    const auto nowSeconds = CurrentTickSeconds();
    const auto authorityHash =
        frame.hasAuthorityProofHash
            ? frame.authorityProofHash
            : HashAuthorityProofSummary(frame.authorityProofSummary);
    const auto authorityChanged =
        !gHasLastDiagnosticsAuthorityHash ||
        authorityHash != gLastDiagnosticsAuthorityHash;
    const auto slowRefresh = totalRefreshMs >= kDiagnosticsSlowRefreshThresholdMs;
    const auto shouldLogSlow =
        slowRefresh &&
        (nowSeconds - gLastDiagnosticsSlowRefreshSeconds) >=
            kDiagnosticsSlowRefreshLogIntervalSeconds;
    const auto shouldLogSummary =
        (nowSeconds - gLastDiagnosticsSummarySeconds) >=
        kDiagnosticsSummaryIntervalSeconds;

    if (!authorityChanged && !shouldLogSlow && !shouldLogSummary) {
        return;
    }

    if (authorityChanged) {
        gHasLastDiagnosticsAuthorityHash = true;
        gLastDiagnosticsAuthorityHash = authorityHash;
    }
    if (shouldLogSlow) {
        gLastDiagnosticsSlowRefreshSeconds = nowSeconds;
    }
    if (shouldLogSummary) {
        gLastDiagnosticsSummarySeconds = nowSeconds;
    }

    std::ostringstream stream;
    const auto trackedRefreshUs = SumTrackedRefreshMicroseconds(frame);
    const auto untrackedRefreshUs =
        totalRefreshUs > trackedRefreshUs ? totalRefreshUs - trackedRefreshUs : 0;
    stream << "event="
           << (authorityChanged ? "authority-proof" : (slowRefresh ? "slow-refresh" : "summary"))
           << " totalMs=" << totalRefreshMs
           << " totalUs=" << totalRefreshUs
           << " stage=" << SanitizeLogText(frame.stage, 16)
           << " reason=" << SanitizeLogText(frame.stageReason, 40)
           << " wake=" << (frame.shouldWake ? 1 : 0)
           << " wakeReason=" << SanitizeLogText(frame.wakeReason, 40)
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
           << ",activeTx:" << frame.activeTransceiverResolveMs
           << ",overlayBuild:" << frame.overlayBuildMs
           << ",overlayUpdate:" << frame.overlayUpdateMs
           << " usTimings=aircraft:" << frame.aircraftStateUs
           << ",xpilot:" << frame.xpilotPollUs
           << ",vatsim:" << frame.vatsimFeedUs
           << ",controllers:" << frame.controllerFeedUs
           << ",flightPlan:" << frame.flightPlanUs
           << ",networkPlan:" << frame.networkPlanUs
           << ",radio:" << frame.radioUs
           << ",messages:" << frame.controllerMessageUs
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
           << ",activeTx:" << frame.activeTransceiverResolveUs
           << ",overlayBuild:" << frame.overlayBuildUs
           << ",overlayUpdate:" << frame.overlayUpdateUs
           << ",displayLog:" << frame.displayLoggingUs
           << ",tracked:" << trackedRefreshUs
           << ",untracked:" << untrackedRefreshUs
           << " routeStatus=\"" << SanitizeLogText(frame.routeStatus, 180)
           << "\" authorityStatus=\"" << SanitizeLogText(frame.authorityStatus, 180)
           << "\" authorityProofs=\"" << frame.authorityProofSummary
           << "\" jobs=\"" << SummarizeDiagnosticJobs(frame, 16)
           << "\" shadowScheduler=\""
           << SanitizeLogText(SummarizeShadowBrainScheduler(frame), 1600) << "\"";
    AppendDiagnosticsLogLine(stream.str());
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
    gManualQuerySnapshot = {};
    gManualQueryVisibleUntilSeconds = 0;
    ResetFlightScopedManualPlanState();
    gSawXPilotConnectedThisFlight = false;
    ResetFlightProgressStateForNewContext();
    gFlightContext = {};
    gLastAircraftStateSnapshot = {};
    gLastPilotIdentitySnapshot = {};
    gLastFlightPlanSnapshot = {};
    gLastNetworkPlanSnapshot = {};
    ClearXPilotConnectionTracking();
    ClearFlightRecoveryState();
    gAircraftStateInvalidBoundaryActive = false;
    gPendingControllerMessage = {};
    ResetBrainDisplayPublisherCache();
    ResetStandbyAssistLatch();
}

void ResetForInvalidAircraftStateFrame() {
    if (gAircraftStateInvalidBoundaryActive) {
        return;
    }

    ResetPluginRuntimeState(false, false);
    gAircraftStateInvalidBoundaryActive = true;
    XPLMDebugString(
        "[XVatsim] Aircraft state unavailable; runtime presentation reset.\n");
}

void ResetSessionState() {
    ResetPluginRuntimeState(true, true);
    XPLMDebugString("[XVatsim] Session reset for next flight.\n");
    RefreshOverlayFromBrain();
}

void ResetFlightScopedStateForSessionBoundary(
    const char* reason,
    bool preserveDisconnectedAlert) {
    ResetPluginRuntimeState(true, true);
    if (preserveDisconnectedAlert) {
        gSawXPilotConnectedThisFlight = true;
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
    gVatsimDataFeedClient.Reset();
    gNetworkPlanLink.Reset();
    gTransceiverResolver.Reset();
    ResetBrainDisplayPublisherCache();
    ResetStandbyAssistLatch();

    std::string line = "[XVatsim] xPilot disconnected; ";
    if (gFlightContext.active) {
        line += "current flight context preserved for reconnect recovery";
        if (!gFlightContext.departureIcao.empty() ||
            !gFlightContext.destinationIcao.empty()) {
            line += " (";
            line += gFlightContext.departureIcao.empty()
                        ? "----"
                        : gFlightContext.departureIcao;
            line += " -> ";
            line += gFlightContext.destinationIcao.empty()
                        ? "----"
                        : gFlightContext.destinationIcao;
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
    auto connectedCallsign =
        NormalizeCallsign(
            pilotIdentitySnapshot.normalizedCallsign.empty()
                ? pilotIdentitySnapshot.callsign
                : pilotIdentitySnapshot.normalizedCallsign);
    if (connectedCallsign.empty()) {
        connectedCallsign = NormalizeCallsign(xPilotSessionSnapshot.callsign);
    }

    if (gLastXPilotConnected && !xPilotSessionSnapshot.connected) {
        gDisconnectedPilotCallsign = gLastConnectedPilotCallsign;
        PreserveFlightStateForNetworkDisconnect();
        gPendingAutomaticFlightRecovery = false;
        gManualFlightRecoveryRequested = false;
        gLastXPilotConnected = false;
        return SessionBoundaryResult::ResetForDisconnect;
    }

    if (!xPilotSessionSnapshot.connected) {
        gLastXPilotConnected = false;
        return SessionBoundaryResult::None;
    }

    const auto reconnectCallsignChanged =
        !gDisconnectedPilotCallsign.empty() &&
        !connectedCallsign.empty() &&
        connectedCallsign != gDisconnectedPilotCallsign;
    if (reconnectCallsignChanged) {
        ResetFlightScopedStateForSessionBoundary("pilot callsign changed after reconnect", false);
        gDisconnectedPilotCallsign.clear();
        return SessionBoundaryResult::ResetForCallsignChange;
    }

    const auto callsignChanged =
        gLastXPilotConnected &&
        !connectedCallsign.empty() &&
        !gLastConnectedPilotCallsign.empty() &&
        connectedCallsign != gLastConnectedPilotCallsign;
    auto result = SessionBoundaryResult::None;
    if (callsignChanged) {
        ResetFlightScopedStateForSessionBoundary("pilot callsign changed", false);
        result = SessionBoundaryResult::ResetForCallsignChange;
    }

    if (result == SessionBoundaryResult::None && !gDisconnectedPilotCallsign.empty()) {
        gPendingAutomaticFlightRecovery = true;
        gDisconnectedPilotCallsign.clear();
        XPLMDebugString(
            "[XVatsim] xPilot reconnect detected; waiting for fresh matched plan to recover current flight.\n");
    }

    gLastXPilotConnected = true;
    if (!connectedCallsign.empty()) {
        gLastConnectedPilotCallsign = connectedCallsign;
    }
    gSawXPilotConnectedThisFlight = true;
    return result;
}

void BeginManualCtafEntry() {
    DiscardPendingTextEntryState();
    gManualQuerySnapshot = {};
    gManualQueryVisibleUntilSeconds = 0;
    gPendingTextEntryMode = PendingTextEntryMode::ManualCtaf;
    ShowTransientStatusLine("CTAF enter ICAO and press Enter");
    gOverlayWindow.BeginTextEntry(".ctaf ");
    RefreshOverlayFromBrain();
}

xvatsim::modules::settings_store::StoredDisplayMode ToStoredDisplayMode(
    DisplayOverrideMode mode) {
    using xvatsim::modules::settings_store::StoredDisplayMode;
    switch (mode) {
        case DisplayOverrideMode::ForcedOpen:
            return StoredDisplayMode::Open;
        case DisplayOverrideMode::ForcedSleep:
            return StoredDisplayMode::Sleep;
        case DisplayOverrideMode::Auto:
        default:
            return StoredDisplayMode::Auto;
    }
}

DisplayOverrideMode ToDisplayOverrideMode(
    xvatsim::modules::settings_store::StoredDisplayMode mode) {
    using xvatsim::modules::settings_store::StoredDisplayMode;
    switch (mode) {
        case StoredDisplayMode::Open:
            return DisplayOverrideMode::ForcedOpen;
        case StoredDisplayMode::Sleep:
            return DisplayOverrideMode::ForcedSleep;
        case StoredDisplayMode::Auto:
        default:
            return DisplayOverrideMode::Auto;
    }
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
    gPreflightRouteCacheAppliedPlanKey.clear();
    gRouteSectorResolver.ClearPreflightRouteCache();

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

    gPreflightRouteCacheCandidate = std::move(cache);
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
    const auto planKey = BuildNetworkPlanIdentityKey(networkPlanSnapshot);
    if (planKey.empty()) {
        return;
    }
    if (planKey == gPreflightRouteCacheAppliedPlanKey) {
        return;
    }

    gRouteSectorResolver.ClearPreflightRouteCache();
    gPreflightRouteCacheAppliedPlanKey = planKey;

    if (!gPreflightRouteCacheCandidate.has_value()) {
        XPLMDebugString(
            "[XVatsim] Preflight route cache unavailable; using normal route preparation.\n");
        return;
    }

    const auto validation =
        xvatsim::core::preflight::ValidatePreflightRouteCacheForNetworkPlan(
            *gPreflightRouteCacheCandidate,
            networkPlanSnapshot,
            true);
    if (!validation.accepted) {
        std::string line =
            "[XVatsim] Preflight route cache rejected: " + validation.reason +
            ". Falling back to normal route preparation.\n";
        XPLMDebugString(line.c_str());
        return;
    }

    gRouteSectorResolver.SetPreflightRouteCache(
        *gPreflightRouteCacheCandidate,
        validation.reason);
    std::ostringstream stream;
    stream << "[XVatsim] Preflight route cache accepted for "
           << networkPlanSnapshot.departureIcao
           << "->"
           << networkPlanSnapshot.destinationIcao
           << " routeHash="
           << gPreflightRouteCacheCandidate->plan.routeIdentityHash
           << ". Authority evidence remains live-only.\n";
    XPLMDebugString(stream.str().c_str());
}

void SavePluginSettings() {
    gPluginSettings.displayMode = ToStoredDisplayMode(gDisplayOverrideMode);
    if (!gSettingsStore.Save(gPluginSettings)) {
        XPLMDebugString("[XVatsim] Settings save failed.\n");
    }
}

void ApplyDisplayOverrideMode(DisplayOverrideMode mode) {
    gDisplayOverrideMode = mode;
    gOverlayWindow.SetAutomaticMode(gDisplayOverrideMode == DisplayOverrideMode::Auto);
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

bool ApplyColdDarkSessionBoundary(
    const xvatsim::brain::AircraftStateSnapshot& aircraftState) {
    if (aircraftState.batteryOn) {
        gColdDarkResetApplied = false;
        return false;
    }

    if (!gColdDarkResetApplied) {
        ResetSessionRuntimeCaches(true);
        gColdDarkResetApplied = true;
    }
    ResetPresentationStateForColdDark();
    return true;
}

void RenderDormantBoundaryFrame(bool hideWindow) {
    xvatsim::brain::OverlayViewModel overlayModel;
    overlayModel.mode = xvatsim::brain::OverlayMode::Dormant;
    overlayModel.visible = false;

    if (hideWindow) {
        gOverlayWindow.Hide();
    } else {
        gOverlayWindow.Update(overlayModel);
    }
    PersistOverlayGeometryIfChanged();
}

void RenderSessionBoundaryFrame(
    const xvatsim::brain::AircraftStateSnapshot& aircraftState,
    const xvatsim::brain::XPilotSessionSnapshot& xPilotSessionSnapshot,
    bool wakeForDisconnectAlert) {
    gOverlayWindow.SetAutomaticMode(gDisplayOverrideMode == DisplayOverrideMode::Auto);

    auto shouldWake = wakeForDisconnectAlert;
    if (gDisplayOverrideMode == DisplayOverrideMode::ForcedOpen) {
        shouldWake = true;
    } else if (gDisplayOverrideMode == DisplayOverrideMode::ForcedSleep) {
        shouldWake = false;
    }

    if (!shouldWake) {
        RenderDormantBoundaryFrame(gDisplayOverrideMode == DisplayOverrideMode::Auto);
        return;
    }

    auto overlayModel = gBrain.BuildOverlayViewModel(
        xvatsim::brain::WorkflowStage::None,
        aircraftState,
        xPilotSessionSnapshot,
        xvatsim::brain::RadioStateSnapshot{},
        xvatsim::brain::NetworkPlanSnapshot{},
        xvatsim::brain::ControllerFeedSnapshot{},
        xvatsim::brain::TransceiverResolutionSnapshot{},
        xvatsim::brain::ModuleBoardSnapshot{},
        xvatsim::brain::ManualQuerySnapshot{});
    overlayModel.headerRightText.clear();
    gOverlayWindow.Update(overlayModel);
    PersistOverlayGeometryIfChanged();
}

void ForceDisplayOpen() {
    ApplyDisplayOverrideMode(DisplayOverrideMode::ForcedOpen);
}

void ForceDisplaySleep() {
    DiscardPendingTextEntryState();
    ApplyDisplayOverrideMode(DisplayOverrideMode::ForcedSleep);
}

void ReturnDisplayToAuto() {
    DiscardPendingTextEntryState();
    ApplyDisplayOverrideMode(DisplayOverrideMode::Auto);
}

void RequestCurrentFlightRecovery() {
    DiscardPendingTextEntryState();
    gManualFlightRecoveryRequested = true;
    ShowTransientStatusLine("RECOVER evaluating current flight");
    XPLMDebugString("[XVatsim] Manual current-flight recovery requested.\n");
    RefreshOverlayFromBrain();
}

void SyncCruiseTargetFromNetworkPlan(
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    if (gCruiseTargetManualOverride) {
        return;
    }

    if (!gFlightContext.active) {
        return;
    }

    const auto sourcePlanKey = BuildNetworkPlanIdentityKey(networkPlanSnapshot);
    if (sourcePlanKey.empty()) {
        return;
    }

    if (!networkPlanSnapshot.hasFiledCruiseAltitude) {
        return;
    }

    const auto normalizedAltitudeFt =
        NormalizeCruiseAltitudeFt(networkPlanSnapshot.filedCruiseAltitudeFt);
    if (normalizedAltitudeFt <= 0.0) {
        return;
    }

    gActiveCruiseTargetFt = normalizedAltitudeFt;
    gHasActiveCruiseTarget = true;
    gCruiseTargetSourceKey = sourcePlanKey;
}

void ApplyCruiseTargetFromCurrentAltitude() {
    if (!gFlightContext.active) {
        ShowTransientStatusLine("CRUISE unavailable without active flight");
        RefreshOverlayFromBrain();
        return;
    }

    if (!gLastAircraftStateSnapshot.valid) {
        ShowTransientStatusLine("CRUISE unavailable without aircraft state");
        RefreshOverlayFromBrain();
        return;
    }

    const auto sourcePlanKey = BuildNetworkPlanIdentityKey(gLastNetworkPlanSnapshot);
    if (sourcePlanKey.empty()) {
        ResetCruiseTargetState();
        ShowTransientStatusLine("CRUISE unavailable until VATSIM plan matched");
        RefreshOverlayFromBrain();
        return;
    }

    const auto normalizedAltitudeFt =
        NormalizeCruiseAltitudeFt(
            ResolveCruiseComparisonAltitudeFt(
                gLastAircraftStateSnapshot,
                gLastAircraftStateSnapshot.altitudeOperationalFt));
    if (normalizedAltitudeFt <= 0.0) {
        ResetCruiseTargetState();
        ShowTransientStatusLine("CRUISE invalid altitude");
        RefreshOverlayFromBrain();
        return;
    }

    gActiveCruiseTargetFt = normalizedAltitudeFt;
    gHasActiveCruiseTarget = true;
    gCruiseTargetManualOverride = true;
    gCruiseTargetSourceKey = sourcePlanKey;
    gCruiseAltitudeReachedThisFlight =
        std::fabs(
            ResolveCruiseComparisonAltitudeFt(
                gLastAircraftStateSnapshot,
                gActiveCruiseTargetFt) - gActiveCruiseTargetFt) <=
        kCruiseGateToleranceFt;
    gCruiseGateSatisfiedSinceSeconds = gCruiseAltitudeReachedThisFlight ? XPLMGetElapsedTime() : -1.0f;
    ShowTransientStatusLine("CRUISE target " + FormatCruiseTargetText(gActiveCruiseTargetFt) + " current");
    RefreshOverlayFromBrain();
}

void ResetCruiseTargetToFiledAltitude() {
    if (!gFlightContext.active) {
        ShowTransientStatusLine("CRUISE unavailable without active flight");
        RefreshOverlayFromBrain();
        return;
    }

    const auto sourcePlanKey = BuildNetworkPlanIdentityKey(gLastNetworkPlanSnapshot);
    if (sourcePlanKey.empty()) {
        ResetCruiseTargetState();
        ShowTransientStatusLine("CRUISE unavailable until VATSIM plan matched");
        RefreshOverlayFromBrain();
        return;
    }

    if (!gLastNetworkPlanSnapshot.hasFiledCruiseAltitude) {
        ResetCruiseTargetState();
        ShowTransientStatusLine("CRUISE filed altitude unavailable");
        RefreshOverlayFromBrain();
        return;
    }

    const auto normalizedAltitudeFt =
        NormalizeCruiseAltitudeFt(gLastNetworkPlanSnapshot.filedCruiseAltitudeFt);
    if (normalizedAltitudeFt <= 0.0) {
        ResetCruiseTargetState();
        ShowTransientStatusLine("CRUISE invalid filed altitude");
        RefreshOverlayFromBrain();
        return;
    }

    gActiveCruiseTargetFt = normalizedAltitudeFt;
    gHasActiveCruiseTarget = true;
    gCruiseTargetManualOverride = false;
    gCruiseTargetSourceKey = sourcePlanKey;
    if (gLastAircraftStateSnapshot.valid) {
        const auto comparisonAltitudeFt = ResolveCruiseComparisonAltitudeFt(
            gLastAircraftStateSnapshot,
            gActiveCruiseTargetFt);
        gCruiseAltitudeReachedThisFlight =
            std::fabs(comparisonAltitudeFt - gActiveCruiseTargetFt) <=
            kCruiseGateToleranceFt;
        gCruiseGateSatisfiedSinceSeconds =
            gCruiseAltitudeReachedThisFlight ? XPLMGetElapsedTime() : -1.0f;
    } else {
        gCruiseAltitudeReachedThisFlight = false;
        gCruiseGateSatisfiedSinceSeconds = -1.0f;
    }
    ShowTransientStatusLine("CRUISE target " + FormatCruiseTargetText(gActiveCruiseTargetFt) + " filed");
    RefreshOverlayFromBrain();
}

void BeginDiversionEntry() {
    DiscardPendingTextEntryState();
    if (!gFlightContext.active) {
        ShowTransientStatusLine("DIVERT unavailable without active flight");
        RefreshOverlayFromBrain();
        return;
    }

    if (!HasFreshMatchedNetworkPlan(gLastNetworkPlanSnapshot)) {
        ShowTransientStatusLine("DIVERT unavailable until VATSIM plan matched");
        RefreshOverlayFromBrain();
        return;
    }

    gManualQuerySnapshot = {};
    gManualQueryVisibleUntilSeconds = 0;
    gPendingTextEntryMode = PendingTextEntryMode::DiversionAirport;
    ShowTransientStatusLine("DIVERT enter ICAO and press Enter");
    gOverlayWindow.BeginTextEntry("");
}

void ApplyDiversionFromSubmittedText(const std::string& submittedText) {
    if (!gFlightContext.active || !gLastAircraftStateSnapshot.valid) {
        ShowTransientStatusLine("DIVERT unavailable without active flight");
        return;
    }

    const auto airportIcao = NormalizeIcaoInput(submittedText);
    if (airportIcao.empty()) {
        ShowTransientStatusLine("DIVERT invalid airport");
        return;
    }

    const auto sourcePlanKey = BuildNetworkPlanIdentityKey(gLastNetworkPlanSnapshot);
    if (sourcePlanKey.empty()) {
        ClearDiversionOverrideState();
        ShowTransientStatusLine("DIVERT unavailable until VATSIM plan matched");
        LogDiversionAction(
            "Diversion request rejected because VATSIM flight plan was stale or unmatched.");
        return;
    }

    if (gDiversionContextModule.HasOverride() &&
        !gDiversionOverrideSourceKey.empty() &&
        gDiversionOverrideSourceKey != sourcePlanKey) {
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

    gDiversionOverrideSourceKey = sourcePlanKey;
    if (!result.changed) {
        return;
    }

    const auto effectiveNetworkPlanSnapshot =
        BuildEffectiveNetworkPlanSnapshot(gLastNetworkPlanSnapshot);
    RetargetFlightContextToPlan(
        gLastPilotIdentitySnapshot,
        gLastFlightPlanSnapshot,
        effectiveNetworkPlanSnapshot);
    LogDiversionAction("Diversion override set: " + result.airportIcao);
}

void RevertToVatsimFlightPlan() {
    if (!gDiversionContextModule.HasOverride()) {
        ShowTransientStatusLine("DIVERT no override active");
        RefreshOverlayFromBrain();
        return;
    }

    if (!HasFreshMatchedNetworkPlan(gLastNetworkPlanSnapshot) ||
        gLastNetworkPlanSnapshot.destinationIcao.empty()) {
        ShowTransientStatusLine("DIVERT revert unavailable");
        LogDiversionAction(
            "Diversion revert requested but the VATSIM flight plan was stale, unmatched, or missing a destination.");
        RefreshOverlayFromBrain();
        return;
    }

    ClearDiversionOverrideState();

    RetargetFlightContextToPlan(
        gLastPilotIdentitySnapshot,
        gLastFlightPlanSnapshot,
        gLastNetworkPlanSnapshot);
    ShowTransientStatusLine(
        "DIVERT reverted to " + gLastNetworkPlanSnapshot.destinationIcao);
    LogDiversionAction(
        "Diversion override cleared; reverted to VATSIM destination " +
        gLastNetworkPlanSnapshot.destinationIcao);
    RefreshOverlayFromBrain();
}

void ClearManualQueryIfExpired() {
    if (!gManualQuerySnapshot.visible) {
        return;
    }

    if (CurrentTickSeconds() < gManualQueryVisibleUntilSeconds) {
        return;
    }

    gManualQuerySnapshot = {};
    gManualQueryVisibleUntilSeconds = 0;
}

void RefreshManualQueryState() {
    ClearManualQueryIfExpired();

    std::string submittedCommand;
    if (!gOverlayWindow.ConsumeSubmittedText(&submittedCommand)) {
        return;
    }

    const auto pendingMode = gPendingTextEntryMode;
    gPendingTextEntryMode = PendingTextEntryMode::None;

    if (pendingMode == PendingTextEntryMode::None) {
        XPLMDebugString(
            "[XVatsim] Ignored submitted overlay text with no active prompt.\n");
        return;
    }

    if (pendingMode == PendingTextEntryMode::DiversionAirport) {
        ApplyDiversionFromSubmittedText(submittedCommand);
        return;
    }

    if (pendingMode == PendingTextEntryMode::ManualCtaf &&
        submittedCommand.find(".ctaf") != 0 &&
        submittedCommand.find("ctaf") != 0) {
        submittedCommand = ".ctaf " + submittedCommand;
    }

    gManualQuerySnapshot = gCtafLookupService.RunManualCtafQuery(submittedCommand);
    if (gManualQuerySnapshot.visible) {
        gManualQueryVisibleUntilSeconds =
            CurrentTickSeconds() + kManualQueryVisibleSeconds;
    }
}

bool ShouldAutoWakeOverlay(
    const xvatsim::brain::AircraftStateSnapshot& aircraftState,
    const xvatsim::brain::XPilotSessionSnapshot& xPilotSessionSnapshot,
    xvatsim::brain::WorkflowStage workflowStage,
    const xvatsim::brain::ModuleBoardSnapshot& enrouteBoardSnapshot) {
    if (xPilotSessionSnapshot.connected) {
        gSawXPilotConnectedThisFlight = true;
    }

    if (gHasActiveCruiseTarget && !gCruiseAltitudeReachedThisFlight) {
        const auto comparisonAltitudeFt = ResolveCruiseComparisonAltitudeFt(
            aircraftState,
            gActiveCruiseTargetFt);
        const auto withinCruiseBand =
            std::fabs(comparisonAltitudeFt - gActiveCruiseTargetFt) <=
            kCruiseGateToleranceFt;
        const auto verticallyStable =
            std::fabs(aircraftState.verticalSpeedFpm) <= kCruiseGateStableVsFpm;
        const auto nowSeconds = XPLMGetElapsedTime();

        if (withinCruiseBand && verticallyStable) {
            if (gCruiseGateSatisfiedSinceSeconds < 0.0f) {
                gCruiseGateSatisfiedSinceSeconds = nowSeconds;
            } else if ((nowSeconds - gCruiseGateSatisfiedSinceSeconds) >=
                       kCruiseGateDwellSeconds) {
                gCruiseAltitudeReachedThisFlight = true;
            }
        } else {
            gCruiseGateSatisfiedSinceSeconds = -1.0f;
        }
    }

    const auto xPilotDisconnectedAlert =
        gSawXPilotConnectedThisFlight && !xPilotSessionSnapshot.connected;

    if (gManualQuerySnapshot.visible || xPilotDisconnectedAlert) {
        return true;
    }

    if (!xPilotSessionSnapshot.connected) {
        return false;
    }

    if (workflowStage == xvatsim::brain::WorkflowStage::Arrival) {
        return true;
    }

    if (workflowStage == xvatsim::brain::WorkflowStage::Enroute) {
        const auto enrouteInitialHoldActive =
            gEnrouteInitialDisplayUntilSeconds >= XPLMGetElapsedTime();
        return HasLiveRouteCenters(enrouteBoardSnapshot) ||
               enrouteInitialHoldActive;
    }

    if (workflowStage == xvatsim::brain::WorkflowStage::Departure) {
        return true;
    }

    return true;
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
        case kManualCtafMenuItemRef:
            BeginManualCtafEntry();
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

    gPluginMenuItemIndex = XPLMAppendMenuItem(pluginsMenu, "XVatsim", nullptr, 1);
    if (gPluginMenuItemIndex < 0) {
        gPluginMenuItemIndex = -1;
        XPLMDebugString("[XVatsim] Plugin menu item registration failed.\n");
        return;
    }

    gPluginMenu = XPLMCreateMenu(
        "XVatsim",
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

    XPLMAppendMenuItem(
        gPluginMenu,
        "Manual CTAF Lookup",
        reinterpret_cast<void*>(kManualCtafMenuItemRef),
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
}

void UnregisterPluginMenu() {
    if (gPluginMenu != nullptr) {
        XPLMDestroyMenu(gPluginMenu);
        gPluginMenu = nullptr;
    }

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

    gRefreshDiagnosticsFrame = {};
    auto& diagnostics = gRefreshDiagnosticsFrame;
    diagnostics.valid = true;

    auto timingStarted = std::chrono::steady_clock::now();
    const auto aircraftState = gAircraftStateSampler.Sample();
    diagnostics.aircraftStateUs = ElapsedMicrosecondsSince(timingStarted);
    diagnostics.onGround = aircraftState.onGround;
    diagnostics.batteryOn = aircraftState.batteryOn;
    if (!aircraftState.valid) {
        ResetForInvalidAircraftStateFrame();
        gOverlayWindow.Hide();
        return;
    }
    gAircraftStateInvalidBoundaryActive = false;

    timingStarted = std::chrono::steady_clock::now();
    const auto xPilotSessionSnapshot = gXPilotBridge.Poll();
    diagnostics.xpilotPollUs = ElapsedMicrosecondsSince(timingStarted);
    diagnostics.xpilotPollMs = diagnostics.xpilotPollUs / 1000;
    diagnostics.xpilotConnected = xPilotSessionSnapshot.connected;
    diagnostics.callsign = xPilotSessionSnapshot.callsign;
    if (ApplyColdDarkSessionBoundary(aircraftState)) {
        RenderSessionBoundaryFrame(aircraftState, xPilotSessionSnapshot, false);
        return;
    }

    const auto pilotIdentitySnapshot =
        gPilotIdentityResolver.Resolve(xPilotSessionSnapshot);
    const auto sessionBoundaryResult =
        HandleXPilotSessionBoundary(xPilotSessionSnapshot, pilotIdentitySnapshot);
    if (sessionBoundaryResult != SessionBoundaryResult::None) {
        RenderSessionBoundaryFrame(
            aircraftState,
            xPilotSessionSnapshot,
            sessionBoundaryResult == SessionBoundaryResult::ResetForDisconnect);
        return;
    }

    timingStarted = std::chrono::steady_clock::now();
    const auto& vatsimDataFeedSnapshot = gVatsimDataFeedClient.Poll();
    diagnostics.vatsimFeedUs = ElapsedMicrosecondsSince(timingStarted);
    diagnostics.vatsimFeedMs = diagnostics.vatsimFeedUs / 1000;

    timingStarted = std::chrono::steady_clock::now();
    const auto controllerFeedSnapshot =
        gControllerFeedClient.BuildSnapshot(vatsimDataFeedSnapshot);
    diagnostics.controllerFeedUs = ElapsedMicrosecondsSince(timingStarted);
    diagnostics.controllerFeedMs = diagnostics.controllerFeedUs / 1000;
    diagnostics.controllerCount = controllerFeedSnapshot.connectedControllers;

    const auto flightPlanSnapshot =
        SampleFlightPlanForRuntime(aircraftState, &diagnostics);

    timingStarted = std::chrono::steady_clock::now();
    const auto networkPlanSnapshot =
        gNetworkPlanLink.Poll(pilotIdentitySnapshot, vatsimDataFeedSnapshot);
    diagnostics.networkPlanUs = ElapsedMicrosecondsSince(timingStarted);
    diagnostics.networkPlanMs = diagnostics.networkPlanUs / 1000;

    gLastAircraftStateSnapshot = aircraftState;
    gLastPilotIdentitySnapshot = pilotIdentitySnapshot;
    gLastFlightPlanSnapshot = flightPlanSnapshot;
    gLastNetworkPlanSnapshot = networkPlanSnapshot;
    ClearCruiseTargetIfSourceInvalid(networkPlanSnapshot);
    SyncCruiseTargetFromNetworkPlan(networkPlanSnapshot);

    timingStarted = std::chrono::steady_clock::now();
    auto radioStateSnapshot = gRadioStateSampler.Sample();
    diagnostics.radioUs = ElapsedMicrosecondsSince(timingStarted);
    diagnostics.radioMs = diagnostics.radioUs / 1000;
    radioStateSnapshot.standbyAssistEnabled = gPluginSettings.standbyAssistEnabled;

    timingStarted = std::chrono::steady_clock::now();
    if (kControllerMessageUiEnabled) {
        const auto xPilotPrivateMessageSnapshot = gXPilotBridge.PollPrivateMessage();
        UpdateControllerMessageState(xPilotPrivateMessageSnapshot);
        if (gOverlayWindow.ConsumeAcknowledgeRequest()) {
            AcknowledgeControllerMessage();
        }
        if (gOverlayWindow.ConsumeRecallRequest()) {
            RecallControllerMessage();
        }
    } else {
        ResetControllerMessageState();
        (void)gOverlayWindow.ConsumeAcknowledgeRequest();
        (void)gOverlayWindow.ConsumeRecallRequest();
    }
    diagnostics.controllerMessageUs = ElapsedMicrosecondsSince(timingStarted);

    timingStarted = std::chrono::steady_clock::now();
    RefreshManualQueryState();
    diagnostics.manualQueryUs = ElapsedMicrosecondsSince(timingStarted);

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
    diagnostics.flightContextUs = ElapsedMicrosecondsSince(timingStarted);
    diagnostics.flightContextActive = gFlightContext.active;
    if (gFlightContext.active) {
        diagnostics.route =
            gFlightContext.departureIcao + "->" + gFlightContext.destinationIcao;
    }

    xvatsim::modules::ctaf_lookup::CtafLookupEntry departureCtafLookup;
    xvatsim::modules::ctaf_lookup::CtafLookupEntry arrivalCtafLookup;
    timingStarted = std::chrono::steady_clock::now();
    if (gFlightContext.active) {
        if (!gFlightContext.departureIcao.empty()) {
            departureCtafLookup = gCtafLookupService.Lookup(gFlightContext.departureIcao);
        }
        if (!gFlightContext.destinationIcao.empty()) {
            arrivalCtafLookup = gCtafLookupService.Lookup(gFlightContext.destinationIcao);
        }
    }
    diagnostics.ctafUs = ElapsedMicrosecondsSince(timingStarted);
    diagnostics.ctafMs = diagnostics.ctafUs / 1000;

    HandoffDecision workflowDecision;
    xvatsim::brain::ModuleBoardSnapshot departureBoardSnapshot;
    xvatsim::brain::ModuleBoardSnapshot arrivalBoardSnapshot;
    xvatsim::brain::ModuleBoardSnapshot enrouteBoardSnapshot;
    xvatsim::brain::ModuleBoardSnapshot activeBoardSnapshot;
    xvatsim::brain::TransceiverResolutionSnapshot transceiverResolutionSnapshot;
    const auto planKey = BuildNetworkPlanIdentityKey(effectiveNetworkPlanSnapshot);

    if (gFlightContext.active) {
        const auto routePolygonOutput =
            RefreshBrainRoutePolygonSnapshot(
                aircraftState,
                effectiveNetworkPlanSnapshot,
                &diagnostics);
        (void)routePolygonOutput;

        const auto radioSnapshot =
            BuildEngineer3RadioSnapshot(
                aircraftState,
                controllerFeedSnapshot,
                planKey,
                &diagnostics);
        transceiverResolutionSnapshot =
            gBrainOwnedRuntimeState.transceiverSnapshot;

        xvatsim::brain::BrainControllerRelevanceWorkerInput provisionalInput;
        provisionalInput.workflowStage = xvatsim::brain::WorkflowStage::None;
        provisionalInput.radioBoardHash = radioSnapshot.stableHash;
        provisionalInput.routePolygonHash =
            gBrainOwnedRuntimeState.routePolygonHash;
        provisionalInput.currentPolygonIndex =
            gBrainOwnedRuntimeState.currentPolygonIndex;
        provisionalInput.currentPolygonKey =
            gBrainOwnedRuntimeState.currentPolygonKey;
        provisionalInput.nextPolygonKey = gBrainOwnedRuntimeState.nextPolygonKey;
        provisionalInput.arrivalPolygonKey =
            gBrainOwnedRuntimeState.arrivalPolygonKey;
        provisionalInput.routeProgressDistanceNm =
            gBrainOwnedRuntimeState.routeProgressDistanceNm;
        provisionalInput.departureIcao = gFlightContext.departureIcao;
        provisionalInput.arrivalIcao = gFlightContext.destinationIcao;
        provisionalInput.radios = radioStateSnapshot;
        provisionalInput.currentSectors =
            gBrainOwnedRuntimeState.routePolygonSnapshot.currentSectors;
        provisionalInput.nextSectors =
            gBrainOwnedRuntimeState.routePolygonSnapshot.nextSectors;
        provisionalInput.candidates = radioSnapshot.candidates;
        const auto provisionalRelevance =
            RunBrainControllerRelevanceWorker(provisionalInput);
        departureBoardSnapshot = provisionalRelevance.departureBoard;
        arrivalBoardSnapshot = provisionalRelevance.arrivalBoard;
        enrouteBoardSnapshot = provisionalRelevance.enrouteBoard;

        workflowDecision =
            ResolveEngineer3WorkflowStage(
                aircraftState,
                radioStateSnapshot,
                departureBoardSnapshot,
                enrouteBoardSnapshot);
        diagnostics.stage = WorkflowStageToken(workflowDecision.stage);
        diagnostics.stageReason = workflowDecision.reason;

        xvatsim::brain::RadioReachablePhaseGateOptions gateOptions;
        gateOptions.stage = workflowDecision.stage;
        gateOptions.reason = "engineer3-clean-runtime";
        const auto gatedRadioSnapshot =
            xvatsim::brain::ApplyRadioReachablePhaseGate(
                radioSnapshot,
                gateOptions);
        gBrainOwnedRuntimeState.gatedRadioSnapshot = gatedRadioSnapshot;

        xvatsim::brain::BrainControllerRelevanceWorkerInput relevanceInput;
        relevanceInput.workflowStage = workflowDecision.stage;
        relevanceInput.radioBoardHash = gatedRadioSnapshot.stableHash;
        relevanceInput.routePolygonHash =
            gBrainOwnedRuntimeState.routePolygonHash;
        relevanceInput.currentPolygonIndex =
            gBrainOwnedRuntimeState.currentPolygonIndex;
        relevanceInput.currentPolygonKey =
            gBrainOwnedRuntimeState.currentPolygonKey;
        relevanceInput.nextPolygonKey = gBrainOwnedRuntimeState.nextPolygonKey;
        relevanceInput.arrivalPolygonKey =
            gBrainOwnedRuntimeState.arrivalPolygonKey;
        relevanceInput.routeProgressDistanceNm =
            gBrainOwnedRuntimeState.routeProgressDistanceNm;
        relevanceInput.departureIcao = gFlightContext.departureIcao;
        relevanceInput.arrivalIcao = gFlightContext.destinationIcao;
        relevanceInput.radios = radioStateSnapshot;
        relevanceInput.currentSectors =
            gBrainOwnedRuntimeState.routePolygonSnapshot.currentSectors;
        relevanceInput.nextSectors =
            gBrainOwnedRuntimeState.routePolygonSnapshot.nextSectors;
        relevanceInput.candidates = gatedRadioSnapshot.candidates;
        auto relevanceOutput =
            RefreshBrainControllerRelevance(
                relevanceInput,
                planKey,
                true);
        const auto publisherOutput = RunBrainPublisher(
            workflowDecision.stage,
            relevanceOutput,
            departureCtafLookup,
            arrivalCtafLookup,
            radioStateSnapshot,
            planKey);
        departureBoardSnapshot = publisherOutput.departureBoard;
        arrivalBoardSnapshot = publisherOutput.arrivalBoard;
        enrouteBoardSnapshot = publisherOutput.enrouteBoard;
        activeBoardSnapshot = publisherOutput.finalDisplay;

        gBrainOwnedRuntimeState.departureBoardSnapshot = departureBoardSnapshot;
        gBrainOwnedRuntimeState.arrivalBoardSnapshot = arrivalBoardSnapshot;
        gBrainOwnedRuntimeState.enrouteBoardSnapshot = enrouteBoardSnapshot;
        gBrainOwnedRuntimeState.activeBoardSnapshot = activeBoardSnapshot;
        gBrainOwnedRuntimeState.finalDisplaySnapshot = activeBoardSnapshot;
        gBrainOwnedRuntimeState.lastWorkflowStage = workflowDecision.stage;
        gBrainOwnedRuntimeState.lastPlanKey = planKey;
        gBrainOwnedRuntimeState.lastRadioBoardHash = gatedRadioSnapshot.stableHash;

        RecordDiagnosticJob(
            "Engineer3Runtime",
            "clean-runtime-no-old-authority-path",
            0,
            "old-authority-quarantined",
            "routeResolve=0,airportCoverage=0,authorityProof=0,enrouteCollect=0,arrivalCollect=0,noHeavyFallback=1",
            {},
            planKey);
    }

    const auto workflowStage = workflowDecision.stage;
    if (diagnostics.stage.empty()) {
        diagnostics.stage = WorkflowStageToken(workflowStage);
        diagnostics.stageReason = workflowDecision.reason;
    }
    UpdateEnrouteInitialDisplayHold(workflowStage);

    timingStarted = std::chrono::steady_clock::now();
    ApplyStandbyRecommendation(
        workflowStage,
        effectiveNetworkPlanSnapshot,
        radioStateSnapshot,
        &activeBoardSnapshot);
    diagnostics.standbyAssistUs = ElapsedMicrosecondsSince(timingStarted);

    timingStarted = std::chrono::steady_clock::now();
    const auto autoWake = ShouldAutoWakeOverlay(
        aircraftState,
        xPilotSessionSnapshot,
        workflowStage,
        activeBoardSnapshot);
    const auto controllerMessageVisible =
        kControllerMessageUiEnabled &&
        gPendingControllerMessage.visible &&
        !gManualQuerySnapshot.visible &&
        gPendingTextEntryMode == PendingTextEntryMode::None;
    const auto controllerMessageWake =
        controllerMessageVisible &&
        gDisplayOverrideMode != DisplayOverrideMode::ForcedSleep;
    const auto textEntryActive = gPendingTextEntryMode != PendingTextEntryMode::None;
    const auto criticalWake =
        gManualQuerySnapshot.visible ||
        textEntryActive ||
        (gSawXPilotConnectedThisFlight && !xPilotSessionSnapshot.connected);

    auto shouldWake = autoWake;
    if (gDisplayOverrideMode == DisplayOverrideMode::ForcedOpen) {
        shouldWake = true;
    } else if (gDisplayOverrideMode == DisplayOverrideMode::ForcedSleep) {
        shouldWake = false;
    }
    if (criticalWake || controllerMessageWake) {
        shouldWake = true;
    }
    diagnostics.shouldWake = shouldWake;

    gOverlayWindow.SetAutomaticMode(gDisplayOverrideMode == DisplayOverrideMode::Auto);

    const auto hideUntilXpilotConnect =
        gDisplayOverrideMode == DisplayOverrideMode::Auto &&
        !gManualQuerySnapshot.visible &&
        !textEntryActive &&
        !xPilotSessionSnapshot.connected &&
        !gSawXPilotConnectedThisFlight;

    const char* wakeReason = "enroute-empty";
    if (gDisplayOverrideMode == DisplayOverrideMode::ForcedOpen) {
        wakeReason = "manual-open";
    } else if (gDisplayOverrideMode == DisplayOverrideMode::ForcedSleep) {
        wakeReason = "manual-sleep";
    } else if (gManualQuerySnapshot.visible) {
        wakeReason = "manual-query";
    } else if (textEntryActive) {
        wakeReason = "text-entry";
    } else if (kControllerMessageUiEnabled && controllerMessageVisible) {
        wakeReason = "controller-message";
    } else if (hideUntilXpilotConnect) {
        wakeReason = "xpilot-waiting";
    } else if (gSawXPilotConnectedThisFlight && !xPilotSessionSnapshot.connected) {
        wakeReason = "xpilot-disconnected";
    } else if (!aircraftState.batteryOn) {
        wakeReason = "battery-off";
    } else if (workflowStage == xvatsim::brain::WorkflowStage::Departure) {
        wakeReason = "departure-board";
    } else if (workflowStage == xvatsim::brain::WorkflowStage::Arrival) {
        wakeReason = "arrival-board";
    } else if (workflowStage == xvatsim::brain::WorkflowStage::Enroute &&
               !activeBoardSnapshot.stations.empty()) {
        wakeReason = "enroute-board";
    } else if (workflowStage == xvatsim::brain::WorkflowStage::None) {
        wakeReason = "startup";
    }
    diagnostics.wakeReason = wakeReason;
    diagnostics.wakeDecisionUs = ElapsedMicrosecondsSince(timingStarted);

    if (!shouldWake) {
        xvatsim::brain::OverlayViewModel overlayModel;
        overlayModel.mode = xvatsim::brain::OverlayMode::Dormant;
        overlayModel.visible = false;

        if (hideUntilXpilotConnect) {
            timingStarted = std::chrono::steady_clock::now();
            gOverlayWindow.Hide();
            diagnostics.overlayUpdateUs = ElapsedMicrosecondsSince(timingStarted);
            diagnostics.overlayUpdateMs = diagnostics.overlayUpdateUs / 1000;
            RecordDiagnosticJob(
                "OverlayUpdate",
                "hide-until-xpilot-connect",
                diagnostics.overlayUpdateMs,
                "ui-update",
                "hidden=1",
                {},
                diagnostics.route);
            timingStarted = std::chrono::steady_clock::now();
            PersistOverlayGeometryIfChanged();
            diagnostics.displayLoggingUs += ElapsedMicrosecondsSince(timingStarted);
            return;
        }

        timingStarted = std::chrono::steady_clock::now();
        gOverlayWindow.Update(overlayModel);
        diagnostics.overlayUpdateUs = ElapsedMicrosecondsSince(timingStarted);
        diagnostics.overlayUpdateMs = diagnostics.overlayUpdateUs / 1000;
        RecordDiagnosticJob(
            "OverlayUpdate",
            "dormant-model",
            diagnostics.overlayUpdateMs,
            "ui-update",
            "visible=0",
            {},
            diagnostics.route);
        timingStarted = std::chrono::steady_clock::now();
        PersistOverlayGeometryIfChanged();
        diagnostics.displayLoggingUs += ElapsedMicrosecondsSince(timingStarted);
        return;
    }

    timingStarted = std::chrono::steady_clock::now();
    auto overlayModel = gBrain.BuildOverlayViewModel(
        workflowStage,
        aircraftState,
        xPilotSessionSnapshot,
        radioStateSnapshot,
        effectiveNetworkPlanSnapshot,
        controllerFeedSnapshot,
        transceiverResolutionSnapshot,
        activeBoardSnapshot,
        gManualQuerySnapshot);
    diagnostics.overlayBuildUs = ElapsedMicrosecondsSince(timingStarted);
    diagnostics.overlayBuildMs = diagnostics.overlayBuildUs / 1000;
    RecordDiagnosticJob(
        "OverlayBuild",
        "build-view-model",
        diagnostics.overlayBuildMs,
        "ui-model-build",
        std::string("mode=") + WorkflowStageToken(workflowStage),
        {},
        diagnostics.route);
    if (gHasActiveCruiseTarget) {
        overlayModel.headerRightText = FormatCruiseTargetText(gActiveCruiseTargetFt);
    }
    overlayModel.showMessageAcknowledge =
        kControllerMessageUiEnabled && controllerMessageVisible;
    overlayModel.showMessageRecall =
        kControllerMessageUiEnabled &&
        !controllerMessageVisible &&
        gPendingControllerMessage.cachedAvailable &&
        !gManualQuerySnapshot.visible &&
        gPendingTextEntryMode == PendingTextEntryMode::None;
    if (kControllerMessageUiEnabled && controllerMessageVisible) {
        ApplyControllerMessageCard(gPendingControllerMessage, &overlayModel);
    }

    timingStarted = std::chrono::steady_clock::now();
    gOverlayWindow.Update(overlayModel);
    diagnostics.overlayUpdateUs = ElapsedMicrosecondsSince(timingStarted);
    diagnostics.overlayUpdateMs = diagnostics.overlayUpdateUs / 1000;
    RecordDiagnosticJob(
        "OverlayUpdate",
        "visible-model",
        diagnostics.overlayUpdateMs,
        "ui-update",
        "visible=1",
        {},
        diagnostics.route);
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

    const auto refreshStarted = std::chrono::steady_clock::now();
    RefreshOverlayFromBrain();
    const auto refreshElapsedUs = ElapsedMicrosecondsSince(refreshStarted);
    const auto refreshElapsedMs = refreshElapsedUs / 1000;
    MaybeLogRefreshDiagnostics(refreshElapsedMs, refreshElapsedUs);
    if (refreshElapsedMs >= 40) {
        const auto nowSeconds = CurrentTickSeconds();
        if ((nowSeconds - gLastFlightLoopPerfWarningSeconds) >= 30) {
            gLastFlightLoopPerfWarningSeconds = nowSeconds;
            std::ostringstream stream;
            stream << "[XVatsim] Perf warning: refresh took "
                   << refreshElapsedMs
                   << "ms\n";
            XPLMDebugString(stream.str().c_str());
        }
    }
    return kUpdateIntervalSeconds;
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
}

PLUGIN_API int XPluginStart(char* outName, char* outSig, char* outDesc) {
    gPluginRuntimeEnabled = false;
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);

    std::strcpy(outName, kPluginName);
    std::strcpy(outSig, kPluginSig);
    std::strcpy(outDesc, kPluginDesc);

    gSettingsStore.SetPath(ResolveSettingsPath());
    gPluginSettings = gSettingsStore.Load();
    gDisplayOverrideMode = ToDisplayOverrideMode(gPluginSettings.displayMode);
    gOverlayWindow.SetTransitionSoundPath(ResolvePluginAssetPath("ui_transition.mp3"));
    LoadPreflightRouteCacheCandidate();
    ApplyOverlayAppearanceSettings();
    if (gPluginSettings.hasWindowPosition) {
        gOverlayWindow.SetWindowTopLeft(
            gPluginSettings.windowLeft,
            gPluginSettings.windowTop);
    }
    ResetPluginRuntimeState(true, true);
    RegisterPluginCommands();
    RegisterPluginMenu();

    const auto overlayModel = gBrain.BuildOverlayViewModel(
        xvatsim::brain::WorkflowStage::None,
        xvatsim::brain::AircraftStateSnapshot{},
        xvatsim::brain::XPilotSessionSnapshot{},
        xvatsim::brain::RadioStateSnapshot{},
        xvatsim::brain::NetworkPlanSnapshot{},
        xvatsim::brain::ControllerFeedSnapshot{},
        xvatsim::brain::TransceiverResolutionSnapshot{},
        xvatsim::brain::ModuleBoardSnapshot{},
        xvatsim::brain::ManualQuerySnapshot{});
    std::string readyMessage = "[XVatsim] Display ready: " + overlayModel.title + "\n";
    XPLMDebugString(readyMessage.c_str());

    XPLMDebugString("[XVatsim] Plugin loaded.\n");
    return 1;
}

PLUGIN_API void XPluginStop() {
    gPluginRuntimeEnabled = false;
    UnregisterFlightLoop();
    PersistOverlayGeometryIfChanged();
    ResetPluginRuntimeState(true, true);
    gOverlayWindow.Destroy();
    UnregisterPluginMenu();
    UnregisterPluginCommands();
    XPLMDebugString("[XVatsim] Plugin stopped.\n");
}

PLUGIN_API int XPluginEnable() {
    ResetPluginRuntimeState(true, true);
    LoadPreflightRouteCacheCandidate();
    gDisplayOverrideMode = ToDisplayOverrideMode(gPluginSettings.displayMode);
    gPluginRuntimeEnabled = true;
    RegisterFlightLoop(kInitialFlightLoopDelaySeconds);
    XPLMDebugString("[XVatsim] Plugin enabled.\n");
    return 1;
}

PLUGIN_API void XPluginDisable() {
    gPluginRuntimeEnabled = false;
    UnregisterFlightLoop();
    PersistOverlayGeometryIfChanged();
    ResetPluginRuntimeState(true, true);
    gDisplayOverrideMode = ToDisplayOverrideMode(gPluginSettings.displayMode);
    gOverlayWindow.Hide();
    XPLMDebugString("[XVatsim] Plugin disabled.\n");
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFrom, int inMessage, void* inParam) {
    (void)inFrom;
    (void)inMessage;
    (void)inParam;
}
