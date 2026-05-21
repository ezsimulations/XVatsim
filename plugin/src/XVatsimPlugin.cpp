#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
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
#include "XVatsim/brain/BrainWorkflow.h"
#include "XVatsim/brain/RadioReachableSnapshot.h"
#include "XVatsim/brain/BrainWorkScheduler.h"
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

enum class PendingTextEntryMode {
    None,
    ManualCtaf,
    DiversionAirport,
};

using HandoffDecision = xvatsim::brain::workflow::HandoffDecision;
using SessionBoundaryResult =
    xvatsim::brain::workflow::XPilotSessionBoundaryAction;

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
xvatsim::brain::AircraftStateSnapshot gLastAircraftStateSnapshot;
xvatsim::brain::PilotIdentitySnapshot gLastPilotIdentitySnapshot;
xvatsim::brain::FlightPlanSnapshot gLastFlightPlanSnapshot;
xvatsim::brain::NetworkPlanSnapshot gLastNetworkPlanSnapshot;
xvatsim::brain::BrainOwnedRuntimeState gBrainOwnedRuntimeState;
long long gLastFlightLoopPerfWarningSeconds = 0;
long long gLastDiagnosticsSlowRefreshSeconds = 0;
long long gLastDiagnosticsSummarySeconds = 0;
bool gHasLastDiagnosticsAuthorityHash = false;
std::size_t gLastDiagnosticsAuthorityHash = 0;
PendingTextEntryMode gPendingTextEntryMode = PendingTextEntryMode::None;
PendingControllerMessageState gPendingControllerMessage;
RefreshDiagnosticsFrame gRefreshDiagnosticsFrame;
std::optional<xvatsim::core::preflight::PreflightRouteCache> gPreflightRouteCacheCandidate;
std::string gPreflightRouteCachePath;

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
    gRadioStateSampler.Reset();
    gRouteSectorResolver.ResetRuntimeState();
    gRouteSectorResolver.ClearPreflightRouteCache();
    ResetBrainOwnedRuntimeCache();
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
        xvatsim::brain::SetBrainOwnedColdDarkResetApplied(
            &gBrainOwnedRuntimeState,
            false);
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

void ResetStandbyAssistLatch() {
    xvatsim::brain::ResetBrainOwnedStandbyAssistLatch(
        &gBrainOwnedRuntimeState);
}

void ApplyStandbyRecommendation(
    xvatsim::brain::WorkflowStage workflowStage,
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot,
    const xvatsim::brain::RadioStateSnapshot& radioStateSnapshot,
    xvatsim::brain::ModuleBoardSnapshot* boardSnapshot) {
    if (boardSnapshot == nullptr) {
        return;
    }

    xvatsim::brain::BrainOwnedStandbyAssistPlanInput standbyInput;
    standbyInput.workflowStage = workflowStage;
    standbyInput.planKey = BuildNetworkPlanIdentityKey(networkPlanSnapshot);
    standbyInput.radios = radioStateSnapshot;
    standbyInput.board = *boardSnapshot;
    const auto standbyPlan =
        xvatsim::brain::BuildBrainOwnedStandbyAssistPlan(standbyInput);
    *boardSnapshot = standbyPlan.board;

    const auto sideEffectDecision =
        xvatsim::brain::DecideBrainOwnedStandbyAssistSideEffect(
            &gBrainOwnedRuntimeState,
            standbyPlan,
            gPluginSettings.standbyAssistEnabled);

    auto standbyLoaded = sideEffectDecision.standbyLoaded;
    if (sideEffectDecision.shouldWriteCom1Standby) {
        standbyLoaded =
            gRadioStateSampler.SetCom1StandbyFrequency(
                sideEffectDecision.targetFrequency);
    }

    *boardSnapshot =
        xvatsim::brain::ApplyBrainOwnedStandbyAssistResult(
            standbyPlan,
            standbyLoaded);
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
    xvatsim::brain::ResetBrainOwnedCruiseTarget(
        &gBrainOwnedRuntimeState);
}

void SyncCruiseTargetFromNetworkPlan(
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    xvatsim::brain::BrainOwnedCruiseTargetPlanInput input;
    input.flightContextActive = gBrainOwnedRuntimeState.flightContext.active;
    input.planKey = BuildNetworkPlanIdentityKey(networkPlanSnapshot);
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

    input.sourcePlanKey = BuildNetworkPlanIdentityKey(networkPlanSnapshot);
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

xvatsim::brain::BrainOwnedCtafLookupFact BuildBrainOwnedCtafLookupFact(
    const std::string& airportIcao,
    const xvatsim::modules::ctaf_lookup::CtafLookupEntry& ctafLookup) {
    xvatsim::brain::BrainOwnedCtafLookupFact fact;
    fact.airportIcao = airportIcao;
    fact.resolved = ctafLookup.resolved;
    fact.available = ctafLookup.available;
    fact.frequency = ctafLookup.frequency;
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

    if (recordDiagnostics) {
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
    return output;
}

HandoffDecision ResolveEngineer3WorkflowStage(
    const xvatsim::brain::AircraftStateSnapshot& aircraftState,
    const xvatsim::brain::RadioStateSnapshot& radioStateSnapshot,
    const xvatsim::brain::ModuleBoardSnapshot& departureBoardSnapshot,
    const xvatsim::brain::ModuleBoardSnapshot& enrouteBoardSnapshot) {
    auto state =
        xvatsim::brain::BuildBrainOwnedWorkflowState(
            gBrainOwnedRuntimeState);

    xvatsim::brain::workflow::WorkflowTuning tuning;
    tuning.arrivalWakeDistanceNm = kArrivalWakeDistanceNm;
    tuning.departureReleaseHoldSeconds = kDepartureReleaseHoldSeconds;

    const auto decision = xvatsim::brain::workflow::ResolveWorkflowStage(
        aircraftState,
        radioStateSnapshot,
        false,
        false,
        departureBoardSnapshot,
        enrouteBoardSnapshot,
        XPLMGetElapsedTime(),
        &state,
        tuning);

    xvatsim::brain::CommitBrainOwnedWorkflowState(
        &gBrainOwnedRuntimeState,
        state);
    return decision;
}

xvatsim::brain::BrainRadioRangeWorkerOutput RunBrainRadioRangeWorker(
    const xvatsim::brain::BrainRadioRangeWorkerInput& input,
    RefreshDiagnosticsFrame* diagnostics) {
    const auto started = std::chrono::steady_clock::now();
    const auto transceivers =
        gTransceiverResolver.Resolve(input.aircraft, input.controllerFeed);
    const auto resolveUs = ElapsedMicrosecondsSince(started);
    if (diagnostics != nullptr) {
        diagnostics->activeTransceiverResolveUs = resolveUs;
        diagnostics->activeTransceiverResolveMs = resolveUs / 1000;
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
            diagnostics->activeTransceiverResolveUs = 0;
            diagnostics->activeTransceiverResolveMs = 0;
        }
        RecordDiagnosticJob(
            "Engineer3RadioBoard",
            reuseOutput.reason,
            0,
            reuseOutput.cacheStatus,
            reuseOutput.radioSnapshot.statusLine,
            {},
            planKey);
        return reuseOutput.radioSnapshot;
    }

    xvatsim::brain::BrainRadioRangeWorkerInput workerInput;
    workerInput.aircraft = aircraftState;
    workerInput.controllerFeed = controllerFeedSnapshot;
    workerInput.planKey = planKey;
    const auto workerOutput =
        RunBrainRadioRangeWorker(workerInput, diagnostics);
    const auto radioSnapshot = workerOutput.radioBoard;

    xvatsim::brain::BrainOwnedRadioBoardCommitInput commitInput;
    commitInput.nowSeconds = nowSeconds;
    commitInput.controllerGeneration = controllerFeedSnapshot.generation;
    commitInput.transceiverSnapshot = workerOutput.transceivers;
    commitInput.radioSnapshot = radioSnapshot;
    const auto commitOutput =
        xvatsim::brain::CommitBrainOwnedRadioBoardRefresh(
            &gBrainOwnedRuntimeState,
            commitInput);

    std::ostringstream result;
    result << commitOutput.radioSnapshot.statusLine << ","
           << commitOutput.diff.statusLine;
    RecordDiagnosticJob(
        "Engineer3RadioBoard",
        commitOutput.reason,
        diagnostics != nullptr ? diagnostics->activeTransceiverResolveMs : 0,
        commitOutput.cacheStatus,
        result.str(),
        {},
        planKey);
    return commitOutput.radioSnapshot;
}


std::string BuildRadioBoardRouteRuntimeKey(
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot) {
    const auto planKey = BuildNetworkPlanIdentityKey(networkPlanSnapshot);
    if (planKey.empty()) {
        return {};
    }
    return planKey + "|route=" + networkPlanSnapshot.routeText;
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
    const auto route =
        gRouteSectorResolver.Resolve(input.aircraft, input.networkPlan);
    const auto routeResolveUs = ElapsedMicrosecondsSince(timingStarted);
    output = xvatsim::brain::BuildBrainRoutePolygonWorkerOutput(route);
    if (diagnostics != nullptr) {
        diagnostics->routeResolveUs = routeResolveUs;
        diagnostics->routeResolveMs = routeResolveUs / 1000;
        diagnostics->routeResolved = output.route.routeResolved;
        diagnostics->routeStatus = output.route.statusLine;
    }
    return output;
}

xvatsim::brain::BrainRoutePolygonWorkerOutput RefreshBrainRoutePolygonSnapshot(
    const xvatsim::brain::AircraftStateSnapshot& aircraftState,
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot,
    RefreshDiagnosticsFrame* diagnostics) {
    const auto planKey = BuildNetworkPlanIdentityKey(networkPlanSnapshot);
    const auto routeRuntimeKey = BuildRadioBoardRouteRuntimeKey(networkPlanSnapshot);

    xvatsim::brain::BrainOwnedRoutePolygonRefreshInput refreshInput;
    refreshInput.aircraft = aircraftState;
    refreshInput.routeRuntimeKey = routeRuntimeKey;
    refreshInput.nowSeconds = CurrentTickSeconds();
    refreshInput.pendingRetrySeconds = kRadioBoardPendingRouteRetrySeconds;

    auto runtimeOutput =
        xvatsim::brain::BeginBrainOwnedRoutePolygonRefresh(
            &gBrainOwnedRuntimeState,
            refreshInput);

    if (runtimeOutput.reset) {
        RecordDiagnosticJob(
            "BrainRoutePolygonWorker",
            runtimeOutput.reason,
            0,
            runtimeOutput.cacheStatus,
            runtimeOutput.diagnosticResult,
            {},
            planKey);
        return runtimeOutput.route;
    }

    const auto recordTransitionDiagnostic =
        [&](const xvatsim::brain::BrainOwnedRoutePolygonRuntimeOutput& record) {
            if (!record.transitionEvaluated) {
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

    if (!runtimeOutput.needsWorker) {
        if (diagnostics != nullptr) {
            diagnostics->routeResolved = runtimeOutput.route.route.routeResolved;
            diagnostics->routeStatus = runtimeOutput.route.route.statusLine;
        }
        recordTransitionDiagnostic(runtimeOutput);
        RecordDiagnosticJob(
            "BrainRoutePolygonWorker",
            runtimeOutput.reason,
            0,
            runtimeOutput.cacheStatus,
            runtimeOutput.diagnosticResult,
            {},
            routeRuntimeKey);
        return runtimeOutput.route;
    }

    xvatsim::brain::BrainRoutePolygonWorkerInput input;
    input.aircraft = aircraftState;
    input.networkPlan = networkPlanSnapshot;
    input.planKey = planKey;
    const auto workerOutput = RunBrainRoutePolygonWorker(input, diagnostics);
    runtimeOutput =
        xvatsim::brain::CommitBrainOwnedRoutePolygonRefresh(
            &gBrainOwnedRuntimeState,
            refreshInput,
            workerOutput);
    recordTransitionDiagnostic(runtimeOutput);
    RecordDiagnosticJob(
        "BrainRoutePolygonWorker",
        runtimeOutput.reason,
        diagnostics != nullptr ? diagnostics->routeResolveMs : 0,
        runtimeOutput.cacheStatus,
        runtimeOutput.diagnosticResult,
        {},
        routeRuntimeKey);
    return runtimeOutput.route;
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
        RecordDiagnosticJob(
            "FlightPlanSampler",
            decision.reason,
            0,
            "flight-plan-cache-hit",
            "sample=skipped",
            {},
            flightContext.departureIcao + "->" +
                flightContext.destinationIcao);
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
    ResetFlightProgressStateForNewContext();
    xvatsim::brain::ClearBrainOwnedFlightContext(&gBrainOwnedRuntimeState);
    gLastAircraftStateSnapshot = {};
    gLastPilotIdentitySnapshot = {};
    gLastFlightPlanSnapshot = {};
    gLastNetworkPlanSnapshot = {};
    ClearXPilotConnectionTracking();
    ClearFlightRecoveryState();
    xvatsim::brain::ClearBrainOwnedAircraftStateInvalidBoundary(
        &gBrainOwnedRuntimeState);
    gPendingControllerMessage = {};
    ResetBrainDisplayPublisherCache();
    ResetStandbyAssistLatch();
}

void ResetSessionState() {
    ResetPluginRuntimeState(true, true);
    XPLMDebugString("[XVatsim] Session reset for next flight.\n");
    RefreshOverlayFromBrain();
}

void ApplyAircraftRuntimeBoundaryDecision(
    const xvatsim::brain::workflow::AircraftRuntimeBoundaryDecision& decision) {
    if (decision.shouldResetForInvalidAircraftState) {
        ResetPluginRuntimeState(false, false);
    }
    if (decision.shouldResetSessionRuntimeCaches) {
        ResetSessionRuntimeCaches(true);
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
    ResetPluginRuntimeState(true, true);
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
    gVatsimDataFeedClient.Reset();
    gNetworkPlanLink.Reset();
    gTransceiverResolver.Reset();
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
    gManualQuerySnapshot = {};
    gManualQueryVisibleUntilSeconds = 0;
    gPendingTextEntryMode = PendingTextEntryMode::ManualCtaf;
    ShowTransientStatusLine("CTAF enter ICAO and press Enter");
    gOverlayWindow.BeginTextEntry(".ctaf ");
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
    xvatsim::brain::ClearBrainOwnedPreflightRouteCacheApplication(
        &gBrainOwnedRuntimeState);
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
    xvatsim::brain::BrainOwnedPreflightRouteCacheInput input;
    input.planKey = BuildNetworkPlanIdentityKey(networkPlanSnapshot);
    input.hasCandidate = gPreflightRouteCacheCandidate.has_value();
    const auto decision =
        xvatsim::brain::BeginBrainOwnedPreflightRouteCacheApplication(
            &gBrainOwnedRuntimeState,
            input);

    if (decision.shouldClearRouteResolverCache) {
        gRouteSectorResolver.ClearPreflightRouteCache();
    }
    if (!decision.logLine.empty()) {
        XPLMDebugString(decision.logLine.c_str());
    }
    if (!decision.shouldValidateCandidate) {
        return;
    }

    const auto validation =
        xvatsim::core::preflight::ValidatePreflightRouteCacheForNetworkPlan(
            *gPreflightRouteCacheCandidate,
            networkPlanSnapshot,
            true);
    xvatsim::brain::BrainOwnedPreflightRouteCacheValidationInput
        validationInput;
    validationInput.accepted = validation.accepted;
    validationInput.reason = validation.reason;
    const auto validationDecision =
        xvatsim::brain::DecideBrainOwnedPreflightRouteCacheValidation(
            validationInput);
    if (!validationDecision.logLine.empty()) {
        XPLMDebugString(validationDecision.logLine.c_str());
    }
    if (!validationDecision.shouldApplyRouteResolverCache) {
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
    gPluginSettings.displayMode =
        ToStoredDisplayMode(gBrainOwnedRuntimeState.displayOverrideMode);
    if (!gSettingsStore.Save(gPluginSettings)) {
        XPLMDebugString("[XVatsim] Settings save failed.\n");
    }
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

    if (!shouldWake) {
        RenderDormantBoundaryFrame(
            displayOverrideMode ==
            xvatsim::brain::BrainOwnedDisplayOverrideMode::Auto);
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
    ApplyDisplayOverrideMode(
        xvatsim::brain::BrainOwnedDisplayOverrideMode::ForcedOpen);
}

void ForceDisplaySleep() {
    DiscardPendingTextEntryState();
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
    xvatsim::brain::BrainOwnedCruiseTargetCommandInput input;
    input.command = xvatsim::brain::BrainOwnedCruiseTargetCommand::CurrentAltitude;
    input.flightContextActive = gBrainOwnedRuntimeState.flightContext.active;
    input.planKey = BuildNetworkPlanIdentityKey(gLastNetworkPlanSnapshot);
    input.aircraftState = gLastAircraftStateSnapshot;
    input.networkPlan = gLastNetworkPlanSnapshot;
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
    xvatsim::brain::BrainOwnedCruiseTargetCommandInput input;
    input.command = xvatsim::brain::BrainOwnedCruiseTargetCommand::FiledAltitude;
    input.flightContextActive = gBrainOwnedRuntimeState.flightContext.active;
    input.planKey = BuildNetworkPlanIdentityKey(gLastNetworkPlanSnapshot);
    input.aircraftState = gLastAircraftStateSnapshot;
    input.networkPlan = gLastNetworkPlanSnapshot;
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
    DiscardPendingTextEntryState();
    if (!gBrainOwnedRuntimeState.flightContext.active) {
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
    if (!gBrainOwnedRuntimeState.flightContext.active || !gLastAircraftStateSnapshot.valid) {
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
        ApplyAircraftRuntimeBoundaryDecision(aircraftBoundaryDecision);
        gOverlayWindow.Hide();
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
    if (aircraftBoundaryDecision.coldDarkBoundaryActive) {
        ApplyAircraftRuntimeBoundaryDecision(aircraftBoundaryDecision);
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
    xvatsim::brain::ModuleBoardSnapshot departureBoardSnapshot;
    xvatsim::brain::ModuleBoardSnapshot arrivalBoardSnapshot;
    xvatsim::brain::ModuleBoardSnapshot enrouteBoardSnapshot;
    xvatsim::brain::ModuleBoardSnapshot activeBoardSnapshot;
    xvatsim::brain::TransceiverResolutionSnapshot transceiverResolutionSnapshot;
    xvatsim::brain::RadioReachableControllerSnapshot gatedRadioSnapshot;
    xvatsim::brain::BrainOwnedPublisherOutput publisherOutput;
    bool hasPublisherOutput = false;
    const auto planKey = BuildNetworkPlanIdentityKey(effectiveNetworkPlanSnapshot);

    if (flightContext.active) {
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

        xvatsim::brain::BrainOwnedControllerRelevanceInputRequest
            provisionalRequest;
        provisionalRequest.workflowStage = xvatsim::brain::WorkflowStage::None;
        provisionalRequest.radioSnapshot = radioSnapshot;
        provisionalRequest.departureIcao = flightContext.departureIcao;
        provisionalRequest.arrivalIcao = flightContext.destinationIcao;
        provisionalRequest.radios = radioStateSnapshot;
        const auto provisionalInput =
            xvatsim::brain::BuildBrainOwnedControllerRelevanceInput(
                gBrainOwnedRuntimeState,
                provisionalRequest);
        const auto provisionalRelevance =
            xvatsim::brain::RunBrainControllerRelevanceWorker(provisionalInput);
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

        gatedRadioSnapshot =
            xvatsim::brain::RunBrainOwnedRadioPhaseGate(
                &gBrainOwnedRuntimeState,
                radioSnapshot,
                workflowDecision.stage,
                "engineer3-clean-runtime");

        xvatsim::brain::BrainOwnedControllerRelevanceInputRequest
            relevanceRequest;
        relevanceRequest.workflowStage = workflowDecision.stage;
        relevanceRequest.radioSnapshot = gatedRadioSnapshot;
        relevanceRequest.departureIcao = flightContext.departureIcao;
        relevanceRequest.arrivalIcao = flightContext.destinationIcao;
        relevanceRequest.radios = radioStateSnapshot;
        const auto relevanceInput =
            xvatsim::brain::BuildBrainOwnedControllerRelevanceInput(
                gBrainOwnedRuntimeState,
                relevanceRequest);
        auto relevanceOutput =
            RefreshBrainControllerRelevance(
                relevanceInput,
                planKey,
                true);
        publisherOutput = RunBrainPublisher(
            workflowDecision.stage,
            relevanceOutput,
            departureCtafLookup,
            arrivalCtafLookup,
            radioStateSnapshot,
            planKey);
        hasPublisherOutput = true;
        departureBoardSnapshot = publisherOutput.departureBoard;
        arrivalBoardSnapshot = publisherOutput.arrivalBoard;
        enrouteBoardSnapshot = publisherOutput.enrouteBoard;
        activeBoardSnapshot = publisherOutput.finalDisplay;

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
    const auto enrouteInitialHoldActive =
        UpdateEnrouteInitialDisplayHold(workflowStage);

    timingStarted = std::chrono::steady_clock::now();
    ApplyStandbyRecommendation(
        workflowStage,
        effectiveNetworkPlanSnapshot,
        radioStateSnapshot,
        &activeBoardSnapshot);
    diagnostics.standbyAssistUs = ElapsedMicrosecondsSince(timingStarted);

    if (hasPublisherOutput) {
        xvatsim::brain::CommitBrainOwnedPublishedRuntimeFromPublisherOutput(
            &gBrainOwnedRuntimeState,
            workflowDecision.stage,
            planKey,
            gatedRadioSnapshot,
            publisherOutput,
            activeBoardSnapshot);
    }

    timingStarted = std::chrono::steady_clock::now();
    UpdateOverlayWakeTracking(
        aircraftState,
        xPilotSessionSnapshot);
    const auto controllerMessageVisible =
        kControllerMessageUiEnabled &&
        gPendingControllerMessage.visible &&
        !gManualQuerySnapshot.visible &&
        gPendingTextEntryMode == PendingTextEntryMode::None;
    const auto textEntryActive = gPendingTextEntryMode != PendingTextEntryMode::None;
    xvatsim::brain::BrainOwnedOverlayWakeInput wakeInput;
    wakeInput.aircraftState = aircraftState;
    wakeInput.xPilotSession = xPilotSessionSnapshot;
    wakeInput.workflowStage = workflowStage;
    wakeInput.finalDisplay = activeBoardSnapshot;
    wakeInput.displayOverrideMode = gBrainOwnedRuntimeState.displayOverrideMode;
    wakeInput.manualQueryVisible = gManualQuerySnapshot.visible;
    wakeInput.textEntryActive = textEntryActive;
    wakeInput.controllerMessageVisible = controllerMessageVisible;
    wakeInput.sawXPilotConnectedThisFlight =
        gBrainOwnedRuntimeState.sawXPilotConnectedThisFlight;
    wakeInput.enrouteInitialHoldActive = enrouteInitialHoldActive;
    const auto wakeDecision =
        xvatsim::brain::DecideBrainOwnedOverlayWake(wakeInput);
    diagnostics.shouldWake = wakeDecision.shouldWake;

    gOverlayWindow.SetAutomaticMode(
        gBrainOwnedRuntimeState.displayOverrideMode ==
        xvatsim::brain::BrainOwnedDisplayOverrideMode::Auto);

    diagnostics.wakeReason = wakeDecision.reason;
    diagnostics.wakeDecisionUs = ElapsedMicrosecondsSince(timingStarted);

    if (!wakeDecision.shouldWake) {
        xvatsim::brain::OverlayViewModel overlayModel;
        overlayModel.mode = xvatsim::brain::OverlayMode::Dormant;
        overlayModel.visible = false;

        if (wakeDecision.hideUntilXpilotConnect) {
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
    const auto cruiseHeaderText =
        xvatsim::brain::BuildBrainOwnedCruiseTargetHeaderText(
            gBrainOwnedRuntimeState);
    if (!cruiseHeaderText.empty()) {
        overlayModel.headerRightText = cruiseHeaderText;
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
    xvatsim::brain::SetBrainOwnedDisplayOverrideMode(
        &gBrainOwnedRuntimeState,
        ToDisplayOverrideMode(gPluginSettings.displayMode));
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
    xvatsim::brain::SetBrainOwnedDisplayOverrideMode(
        &gBrainOwnedRuntimeState,
        ToDisplayOverrideMode(gPluginSettings.displayMode));
    gOverlayWindow.Hide();
    XPLMDebugString("[XVatsim] Plugin disabled.\n");
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFrom, int inMessage, void* inParam) {
    (void)inFrom;
    (void)inMessage;
    (void)inParam;
}
