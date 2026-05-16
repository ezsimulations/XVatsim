#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "XVatsim/brain/BrainOrchestrator.h"
#include "XVatsim/core/WorkflowEngine.h"
#include "XVatsim/modules/aircraft_state/AircraftStateSampler.h"
#include "XVatsim/modules/arrival/ArrivalModule.h"
#include "XVatsim/modules/ctaf_lookup/CtafLookupService.h"
#include "XVatsim/modules/controller_feed/ControllerFeedClient.h"
#include "XVatsim/modules/departure/DepartureModule.h"
#include "XVatsim/modules/diversion_context/DiversionContextModule.h"
#include "XVatsim/modules/enroute/EnrouteModule.h"
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
constexpr double kArrivalWakeDistanceNm = 200.0;
constexpr float kDepartureReleaseHoldSeconds = 180.0f;
constexpr float kEnrouteInitialDisplaySeconds = 180.0f;
constexpr float kOpacityStep = 0.10f;
constexpr float kScaleStep = 0.05f;
constexpr float kAnimationSpeedStep = 0.10f;
constexpr bool kControllerMessageUiEnabled = XVATSIM_ENABLE_CONTROLLER_MESSAGES != 0;
constexpr std::size_t kMaxLogFieldChars = 80;
constexpr std::size_t kMaxLogStationsPerBoard = 12;

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

struct ModuleBoardCacheEntry {
    bool valid = false;
    std::size_t signature = 0;
    xvatsim::brain::ModuleBoardSnapshot snapshot;
};

struct PendingControllerMessageState {
    bool primed = false;
    int lastSequence = 0;
    bool visible = false;
    bool cachedAvailable = false;
    std::string from;
    std::string body;
};

xvatsim::brain::BrainOrchestrator gBrain;
xvatsim::modules::aircraft_state::AircraftStateSampler gAircraftStateSampler;
xvatsim::modules::arrival::ArrivalModule gArrivalModule;
xvatsim::modules::ctaf_lookup::CtafLookupService gCtafLookupService;
xvatsim::modules::controller_feed::ControllerFeedClient gControllerFeedClient;
xvatsim::modules::departure::DepartureModule gDepartureModule;
xvatsim::modules::diversion_context::DiversionContextModule gDiversionContextModule;
xvatsim::modules::enroute::EnrouteModule gEnrouteModule;
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
xvatsim::brain::NetworkPlanSnapshot gLastNetworkPlanSnapshot;
float gCruiseGateSatisfiedSinceSeconds = -1.0f;
std::string gStandbyAssistLatchKey;
bool gStandbyAssistWriteConsumed = false;
long long gLastFlightLoopPerfWarningSeconds = 0;
bool gHasLastDisplayDecisionHash = false;
std::size_t gLastDisplayDecisionHash = 0;
bool gHasLastBoardContentsHash = false;
std::size_t gLastBoardContentsHash = 0;
bool gLastXPilotConnected = false;
bool gColdDarkResetApplied = false;
bool gAircraftStateInvalidBoundaryActive = false;
std::string gLastConnectedPilotCallsign;
std::string gCruiseTargetSourceKey;
std::string gDiversionOverrideSourceKey;
ModuleBoardCacheEntry gDepartureBoardCache;
ModuleBoardCacheEntry gArrivalBoardCache;
ModuleBoardCacheEntry gEnrouteBoardCache;
PendingTextEntryMode gPendingTextEntryMode = PendingTextEntryMode::None;
PendingControllerMessageState gPendingControllerMessage;

void RefreshOverlayFromBrain();
void ResetPresentationStateForColdDark();

std::string BuildNetworkPlanIdentityKey(
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot);

long long CurrentTickSeconds() {
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
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

std::size_t HashAirportSectorSnapshot(const xvatsim::brain::AirportSectorSnapshot& snapshot) {
    std::size_t hash = 0;
    HashCombineBool(&hash, snapshot.available);
    HashCombineBool(&hash, snapshot.stale);
    HashCombineBool(&hash, snapshot.hasCenterCoverageData);
    HashCombineBool(&hash, snapshot.hasTerminalCoverageData);
    HashCombine(&hash, snapshot.centerBoundaryGeneration);
    HashCombine(&hash, snapshot.authorityCatalogGeneration);
    HashCombine(&hash, snapshot.terminalCoverageGeneration);
    HashCombineString(&hash, snapshot.airportIcao);
    HashCombine(&hash, snapshot.coveringSectors.size());
    for (const auto& sector : snapshot.coveringSectors) {
        HashCombine(&hash, HashRouteSectorMatch(sector));
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

std::size_t HashBoardStation(const xvatsim::brain::BoardStationSnapshot& station) {
    std::size_t hash = 0;
    HashCombine(&hash, static_cast<std::size_t>(station.role));
    HashCombineString(&hash, station.callsign);
    HashCombineString(&hash, NormalizeFrequency(station.frequency));
    HashCombineString(&hash, station.annotation);
    HashCombineBool(&hash, station.tuned);
    HashCombineBool(&hash, station.next);
    HashCombineBool(&hash, station.standby);
    HashCombineBool(&hash, station.sectorActive);
    HashCombineBool(&hash, station.online);
    HashCombineBool(&hash, station.offline);
    HashCombineBool(&hash, station.hasRouteEntryDistance);
    if (station.hasRouteEntryDistance) {
        HashCombineDouble(&hash, station.routeEntryDistanceNm);
    }
    return hash;
}

std::size_t HashBoardSnapshot(const xvatsim::brain::ModuleBoardSnapshot& snapshot) {
    std::size_t hash = 0;
    HashCombineBool(&hash, snapshot.available);
    HashCombineBool(&hash, snapshot.displayStations);
    HashCombine(&hash, static_cast<std::size_t>(snapshot.source));
    HashCombineString(&hash, snapshot.airportIcao);
    HashCombine(&hash, snapshot.stations.size());
    for (const auto& station : snapshot.stations) {
        HashCombine(&hash, HashBoardStation(station));
    }
    return hash;
}

std::size_t BuildDepartureBoardSignature(
    const xvatsim::brain::XPilotSessionSnapshot& xPilotSessionSnapshot,
    const xvatsim::brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const xvatsim::brain::RadioStateSnapshot& radioStateSnapshot,
    const std::string& airportIcao,
    const xvatsim::brain::AirportSectorSnapshot& airportSectorSnapshot,
    const xvatsim::modules::ctaf_lookup::CtafLookupEntry& ctafLookupEntry) {
    std::size_t hash = 0;
    HashCombine(&hash, HashPilotSessionBoardInputs(xPilotSessionSnapshot));
    HashCombine(&hash, HashControllerFeedIdentity(controllerFeedSnapshot));
    HashCombine(&hash, HashRadioBoardInputs(radioStateSnapshot));
    HashCombineString(&hash, NormalizeIcaoInput(airportIcao));
    HashCombine(&hash, HashAirportSectorSnapshot(airportSectorSnapshot));
    HashCombine(&hash, HashCtafLookupEntry(ctafLookupEntry));
    return hash;
}

std::size_t BuildArrivalBoardSignature(
    const xvatsim::brain::XPilotSessionSnapshot& xPilotSessionSnapshot,
    const xvatsim::brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const xvatsim::brain::RadioStateSnapshot& radioStateSnapshot,
    const std::string& airportIcao,
    const xvatsim::brain::AirportSectorSnapshot& airportSectorSnapshot,
    const xvatsim::modules::ctaf_lookup::CtafLookupEntry& ctafLookupEntry) {
    return BuildDepartureBoardSignature(
        xPilotSessionSnapshot,
        controllerFeedSnapshot,
        radioStateSnapshot,
        airportIcao,
        airportSectorSnapshot,
        ctafLookupEntry);
}

std::size_t BuildEnrouteBoardSignature(
    const xvatsim::brain::XPilotSessionSnapshot& xPilotSessionSnapshot,
    const xvatsim::brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const xvatsim::brain::RadioStateSnapshot& radioStateSnapshot,
    const xvatsim::brain::RouteSectorSnapshot& routeSectorSnapshot) {
    std::size_t hash = 0;
    HashCombine(&hash, HashPilotSessionBoardInputs(xPilotSessionSnapshot));
    HashCombine(&hash, HashControllerFeedIdentity(controllerFeedSnapshot));
    HashCombine(&hash, HashRadioBoardInputs(radioStateSnapshot));
    HashCombine(&hash, HashRouteSectorSnapshot(routeSectorSnapshot));
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

void ResetBoardCaches() {
    gDepartureBoardCache = {};
    gArrivalBoardCache = {};
    gEnrouteBoardCache = {};
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
    gTransceiverResolver.Reset();
    gEnrouteModule.Reset();
    ResetBoardCaches();
}

void ResetPluginRuntimeState(bool resetVatsimFeed, bool resetColdDarkLatch) {
    DiscardPendingTextEntryState();
    gManualQuerySnapshot = {};
    gManualQueryVisibleUntilSeconds = 0;
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
    gDepartureReleasedThisFlight = false;
    gArrivalAwakeThisFlight = false;
    gAirborneSinceSeconds = -1.0f;
    gEnrouteInitialDisplayStarted = false;
    gEnrouteInitialDisplayUntilSeconds = -1.0f;
    ResetBoardCaches();
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
    gEnrouteInitialDisplayStarted = false;
    gEnrouteInitialDisplayUntilSeconds = -1.0f;
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
    ResetBoardCaches();
    ResetStandbyAssistLatch();
    gHasLastBoardContentsHash = false;
    gLastBoardContentsHash = 0;
    gHasLastDisplayDecisionHash = false;
    gLastDisplayDecisionHash = 0;
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
    const auto departureTerminalCoverageKnown =
        gRouteSectorResolver.CanEvaluateAirportTerminalCoverage(
            departureAirportSectorSnapshot);
    const auto insideDepartureTerminalCoverage =
        gRouteSectorResolver.IsInsideAirportTerminalCoverage(
            departureAirportSectorSnapshot,
            aircraftState.latitudeDeg,
            aircraftState.longitudeDeg);

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

xvatsim::brain::ModuleBoardSnapshot BuildDisplayBoard(
    xvatsim::brain::WorkflowStage workflowStage,
    const xvatsim::brain::ModuleBoardSnapshot& departureBoardSnapshot,
    const xvatsim::brain::ModuleBoardSnapshot& arrivalBoardSnapshot,
    const xvatsim::brain::ModuleBoardSnapshot& enrouteBoardSnapshot) {
    return xvatsim::core::workflow::BuildDisplayBoard(
        workflowStage,
        departureBoardSnapshot,
        arrivalBoardSnapshot,
        enrouteBoardSnapshot);
}

const xvatsim::brain::ModuleBoardSnapshot& CollectDepartureBoardCached(
    const xvatsim::brain::XPilotSessionSnapshot& xPilotSessionSnapshot,
    const xvatsim::brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const xvatsim::brain::RadioStateSnapshot& radioStateSnapshot,
    const std::string& departureAirportIcao,
    const xvatsim::brain::AirportSectorSnapshot& airportSectorSnapshot,
    const xvatsim::modules::ctaf_lookup::CtafLookupEntry& ctafLookupEntry) {
    const auto signature = BuildDepartureBoardSignature(
        xPilotSessionSnapshot,
        controllerFeedSnapshot,
        radioStateSnapshot,
        departureAirportIcao,
        airportSectorSnapshot,
        ctafLookupEntry);
    if (gDepartureBoardCache.valid && gDepartureBoardCache.signature == signature) {
        return gDepartureBoardCache.snapshot;
    }

    gDepartureBoardCache.snapshot = gDepartureModule.Collect(
        xPilotSessionSnapshot,
        controllerFeedSnapshot,
        radioStateSnapshot,
        departureAirportIcao,
        airportSectorSnapshot,
        &gCtafLookupService);
    gDepartureBoardCache.signature = signature;
    gDepartureBoardCache.valid = true;
    return gDepartureBoardCache.snapshot;
}

const xvatsim::brain::ModuleBoardSnapshot& CollectArrivalBoardCached(
    const xvatsim::brain::XPilotSessionSnapshot& xPilotSessionSnapshot,
    const xvatsim::brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const xvatsim::brain::RadioStateSnapshot& radioStateSnapshot,
    const std::string& arrivalAirportIcao,
    const xvatsim::brain::AirportSectorSnapshot& airportSectorSnapshot,
    const xvatsim::modules::ctaf_lookup::CtafLookupEntry& ctafLookupEntry) {
    const auto signature = BuildArrivalBoardSignature(
        xPilotSessionSnapshot,
        controllerFeedSnapshot,
        radioStateSnapshot,
        arrivalAirportIcao,
        airportSectorSnapshot,
        ctafLookupEntry);
    if (gArrivalBoardCache.valid && gArrivalBoardCache.signature == signature) {
        return gArrivalBoardCache.snapshot;
    }

    gArrivalBoardCache.snapshot = gArrivalModule.Collect(
        xPilotSessionSnapshot,
        controllerFeedSnapshot,
        radioStateSnapshot,
        arrivalAirportIcao,
        airportSectorSnapshot,
        &gCtafLookupService);
    gArrivalBoardCache.signature = signature;
    gArrivalBoardCache.valid = true;
    return gArrivalBoardCache.snapshot;
}

const xvatsim::brain::ModuleBoardSnapshot& CollectEnrouteBoardCached(
    const xvatsim::brain::XPilotSessionSnapshot& xPilotSessionSnapshot,
    const xvatsim::brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const xvatsim::brain::RadioStateSnapshot& radioStateSnapshot,
    const xvatsim::brain::RouteSectorSnapshot& routeSectorSnapshot) {
    const auto signature = BuildEnrouteBoardSignature(
        xPilotSessionSnapshot,
        controllerFeedSnapshot,
        radioStateSnapshot,
        routeSectorSnapshot);
    if (gEnrouteBoardCache.valid && gEnrouteBoardCache.signature == signature) {
        return gEnrouteBoardCache.snapshot;
    }

    gEnrouteBoardCache.snapshot = gEnrouteModule.Collect(
        xPilotSessionSnapshot,
        controllerFeedSnapshot,
        radioStateSnapshot,
        routeSectorSnapshot);
    gEnrouteBoardCache.signature = signature;
    gEnrouteBoardCache.valid = true;
    return gEnrouteBoardCache.snapshot;
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

std::string SummarizeAuthorityGapSectors(
    const xvatsim::brain::RouteSectorSnapshot& routeSectorSnapshot) {
    std::vector<std::string> gaps;
    auto appendGaps = [&](const auto& sectors, const char* label) {
        for (const auto& sector : sectors) {
            if (!sector.controllerPrefixes.empty()) {
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

void LogDisplayDecisionIfChanged(
    xvatsim::brain::WorkflowStage workflowStage,
    const char* stageReason,
    const xvatsim::brain::NetworkPlanSnapshot& networkPlanSnapshot,
    const xvatsim::brain::RouteSectorSnapshot& routeSectorSnapshot,
    const xvatsim::brain::AirportSectorSnapshot& airportSectorSnapshot,
    const xvatsim::brain::ModuleBoardSnapshot& departureBoardSnapshot,
    const xvatsim::brain::ModuleBoardSnapshot& arrivalBoardSnapshot,
    const xvatsim::brain::ModuleBoardSnapshot& enrouteBoardSnapshot,
    bool shouldWake,
    const char* wakeReason) {
    std::size_t decisionHash = 0;
    HashCombine(&decisionHash, static_cast<std::size_t>(workflowStage));
    HashCombineBool(&decisionHash, shouldWake);
    HashCombineString(&decisionHash, stageReason == nullptr ? std::string{} : stageReason);
    HashCombineString(&decisionHash, wakeReason == nullptr ? std::string{} : wakeReason);
    HashCombine(&decisionHash, HashBoardSnapshot(departureBoardSnapshot));
    HashCombine(&decisionHash, HashBoardSnapshot(arrivalBoardSnapshot));
    HashCombine(&decisionHash, HashBoardSnapshot(enrouteBoardSnapshot));
    HashCombineBool(&decisionHash, gHasActiveCruiseTarget);
    if (gHasActiveCruiseTarget) {
        HashCombineDouble(&decisionHash, gActiveCruiseTargetFt);
        HashCombineBool(&decisionHash, gCruiseTargetManualOverride);
    }
    HashCombineBool(&decisionHash, networkPlanSnapshot.hasFiledCruiseAltitude);
    if (networkPlanSnapshot.hasFiledCruiseAltitude) {
        HashCombineDouble(&decisionHash, networkPlanSnapshot.filedCruiseAltitudeFt);
    }
    HashCombineBool(&decisionHash, routeSectorSnapshot.available);
    HashCombineBool(&decisionHash, routeSectorSnapshot.stale);
    HashCombineBool(&decisionHash, routeSectorSnapshot.routeResolved);
    HashCombineString(&decisionHash, routeSectorSnapshot.statusLine);
    if (routeSectorSnapshot.available) {
        HashCombine(&decisionHash, HashRouteSectorSnapshot(routeSectorSnapshot));
    }
    if ((workflowStage == xvatsim::brain::WorkflowStage::Departure ||
         workflowStage == xvatsim::brain::WorkflowStage::Arrival) &&
        (!airportSectorSnapshot.statusLine.empty() ||
         airportSectorSnapshot.available ||
         airportSectorSnapshot.stale)) {
        HashCombineBool(&decisionHash, airportSectorSnapshot.available);
        HashCombineBool(&decisionHash, airportSectorSnapshot.stale);
        HashCombineString(&decisionHash, airportSectorSnapshot.statusLine);
    }
    if (gHasLastDisplayDecisionHash && decisionHash == gLastDisplayDecisionHash) {
        return;
    }

    std::ostringstream stream;
    stream << "wake=" << (shouldWake ? "1" : "0")
           << " reason=" << wakeReason
           << " stage=" << WorkflowStageToken(workflowStage)
           << " stageReason=" << (stageReason == nullptr ? "unknown" : stageReason)
           << " dep=" << departureBoardSnapshot.stations.size()
           << " arr=" << arrivalBoardSnapshot.stations.size()
           << " enr=" << enrouteBoardSnapshot.stations.size();
    if (gHasActiveCruiseTarget) {
        stream << " activeCruiseFt="
               << static_cast<int>(std::round(gActiveCruiseTargetFt));
        stream << " cruiseTargetMode="
               << (gCruiseTargetManualOverride ? "manual" : "filed");
    }
    if (networkPlanSnapshot.hasFiledCruiseAltitude) {
        stream << " filedCruiseFt="
               << static_cast<int>(std::round(networkPlanSnapshot.filedCruiseAltitudeFt));
    }
    if (routeSectorSnapshot.available ||
        routeSectorSnapshot.stale ||
        routeSectorSnapshot.routeResolved ||
        !routeSectorSnapshot.statusLine.empty()) {
        stream << " routeAvailable=" << (routeSectorSnapshot.available ? "1" : "0")
               << " routeFresh=" << (!routeSectorSnapshot.stale ? "1" : "0")
               << " routeResolved=" << (routeSectorSnapshot.routeResolved ? "1" : "0");
    }
    if (routeSectorSnapshot.available) {
        stream << " routeCurrent=" << routeSectorSnapshot.currentSectors.size()
               << " routeNext=" << routeSectorSnapshot.nextSectors.size()
               << " routeCurSectors="
               << SummarizeRouteSectors(routeSectorSnapshot.currentSectors)
               << " routeNextSectors="
               << SummarizeRouteSectors(routeSectorSnapshot.nextSectors)
               << " routeAuthorityGaps="
               << SummarizeAuthorityGapSectors(routeSectorSnapshot);
    }
    if (!routeSectorSnapshot.statusLine.empty()) {
        stream << " routeStatus=\""
               << SanitizeLogText(routeSectorSnapshot.statusLine)
               << "\"";
    }
    if ((workflowStage == xvatsim::brain::WorkflowStage::Departure ||
         workflowStage == xvatsim::brain::WorkflowStage::Arrival) &&
        (!airportSectorSnapshot.statusLine.empty() ||
         airportSectorSnapshot.available ||
         airportSectorSnapshot.stale)) {
        stream << " airportCoverageAvailable="
               << (airportSectorSnapshot.available ? "1" : "0")
               << " airportCoverageFresh="
               << (!airportSectorSnapshot.stale ? "1" : "0");
    }
    if ((workflowStage == xvatsim::brain::WorkflowStage::Departure ||
         workflowStage == xvatsim::brain::WorkflowStage::Arrival) &&
        !airportSectorSnapshot.statusLine.empty()) {
        stream << " airportCoverage=\""
               << SanitizeLogText(airportSectorSnapshot.statusLine)
               << "\"";
    }
    const auto signature = stream.str();
    gLastDisplayDecisionHash = decisionHash;
    gHasLastDisplayDecisionHash = true;
    std::string line = "[XVatsim] Display: " + signature + "\n";
    XPLMDebugString(line.c_str());
}

std::string SummarizeBoardStations(const xvatsim::brain::ModuleBoardSnapshot& snapshot) {
    if ((!snapshot.available && !snapshot.displayStations) || snapshot.stations.empty()) {
        return "none";
    }

    std::ostringstream stream;
    const auto countToLog =
        std::min(snapshot.stations.size(), kMaxLogStationsPerBoard);
    for (std::size_t index = 0; index < countToLog; ++index) {
        const auto& station = snapshot.stations[index];
        if (index > 0) {
            stream << " | ";
        }

        stream << SanitizeLogText(station.callsign, 32);
        if (!station.frequency.empty()) {
            stream << " " << SanitizeLogText(station.frequency, 16);
        }
        if (!station.annotation.empty() && !station.hasRouteEntryDistance) {
            stream << " " << SanitizeLogText(station.annotation, 32);
        }
        if (station.sectorActive) {
            stream << " sector-active";
        }
        if (station.tuned) {
            stream << " active";
        }
        if (station.next) {
            stream << " next";
        }
        if (station.standby) {
            stream << " standby";
        }
        if (station.online) {
            stream << " online";
        }
        if (station.offline) {
            stream << " offline";
        }
    }
    if (snapshot.stations.size() > countToLog) {
        stream << " | +" << (snapshot.stations.size() - countToLog);
    }

    return stream.str();
}

void LogBoardContentsIfChanged(
    const xvatsim::brain::ModuleBoardSnapshot& departureBoardSnapshot,
    const xvatsim::brain::ModuleBoardSnapshot& arrivalBoardSnapshot,
    const xvatsim::brain::ModuleBoardSnapshot& enrouteBoardSnapshot) {
    std::size_t boardHash = 0;
    HashCombine(&boardHash, HashBoardSnapshot(departureBoardSnapshot));
    HashCombine(&boardHash, HashBoardSnapshot(arrivalBoardSnapshot));
    HashCombine(&boardHash, HashBoardSnapshot(enrouteBoardSnapshot));
    if (gHasLastBoardContentsHash && boardHash == gLastBoardContentsHash) {
        return;
    }

    std::ostringstream stream;
    stream << "DEP[" << SummarizeBoardStations(departureBoardSnapshot) << "] "
           << "ARR[" << SummarizeBoardStations(arrivalBoardSnapshot) << "] "
           << "ENR[" << SummarizeBoardStations(enrouteBoardSnapshot) << "]";

    const auto signature = stream.str();
    gLastBoardContentsHash = boardHash;
    gHasLastBoardContentsHash = true;
    std::string line = "[XVatsim] Boards: " + signature + "\n";
    XPLMDebugString(line.c_str());
}

void ResetPresentationStateForColdDark() {
    DiscardPendingTextEntryState();
    gManualQuerySnapshot = {};
    gManualQueryVisibleUntilSeconds = 0;
    ResetFlightScopedManualPlanState();
    gSawXPilotConnectedThisFlight = false;
    gDepartureReleasedThisFlight = false;
    gArrivalAwakeThisFlight = false;
    gAirborneSinceSeconds = -1.0f;
    gEnrouteInitialDisplayStarted = false;
    gEnrouteInitialDisplayUntilSeconds = -1.0f;
    gFlightContext = {};
    gHasLastBoardContentsHash = false;
    gLastBoardContentsHash = 0;
    gLastAircraftStateSnapshot = {};
    gLastPilotIdentitySnapshot = {};
    gLastFlightPlanSnapshot = {};
    gLastNetworkPlanSnapshot = {};
    gCruiseGateSatisfiedSinceSeconds = -1.0f;
    gHasLastDisplayDecisionHash = false;
    gLastDisplayDecisionHash = 0;
    gLastXPilotConnected = false;
    gLastConnectedPilotCallsign.clear();
    gAircraftStateInvalidBoundaryActive = false;
    gPendingControllerMessage = {};
    ResetBoardCaches();
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
        ResetFlightScopedStateForSessionBoundary("xPilot disconnected", true);
        gLastXPilotConnected = false;
        gLastConnectedPilotCallsign.clear();
        return SessionBoundaryResult::ResetForDisconnect;
    }

    if (!xPilotSessionSnapshot.connected) {
        gLastXPilotConnected = false;
        gLastConnectedPilotCallsign.clear();
        return SessionBoundaryResult::None;
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
        gResetSessionCommand != nullptr) {
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
}

void RefreshOverlayFromBrain() {
    if (!gPluginRuntimeEnabled) {
        return;
    }

    const auto aircraftState = gAircraftStateSampler.Sample();
    if (!aircraftState.valid) {
        ResetForInvalidAircraftStateFrame();
        gOverlayWindow.Hide();
        return;
    }
    gAircraftStateInvalidBoundaryActive = false;

    const auto xPilotSessionSnapshot = gXPilotBridge.Poll();
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

    const auto& vatsimDataFeedSnapshot = gVatsimDataFeedClient.Poll();
    const auto controllerFeedSnapshot =
        gControllerFeedClient.BuildSnapshot(vatsimDataFeedSnapshot);
    const auto flightPlanSnapshot = gFlightPlanSampler.Sample(aircraftState);
    const auto networkPlanSnapshot =
        gNetworkPlanLink.Poll(pilotIdentitySnapshot, vatsimDataFeedSnapshot);
    gLastAircraftStateSnapshot = aircraftState;
    gLastPilotIdentitySnapshot = pilotIdentitySnapshot;
    gLastFlightPlanSnapshot = flightPlanSnapshot;
    gLastNetworkPlanSnapshot = networkPlanSnapshot;
    ClearCruiseTargetIfSourceInvalid(networkPlanSnapshot);
    SyncCruiseTargetFromNetworkPlan(networkPlanSnapshot);
    auto radioStateSnapshot = gRadioStateSampler.Sample();
    radioStateSnapshot.standbyAssistEnabled = gPluginSettings.standbyAssistEnabled;
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
    RefreshManualQueryState();
    const auto effectiveNetworkPlanSnapshot =
        BuildEffectiveNetworkPlanSnapshot(networkPlanSnapshot);
    UpdateFlightContextIfNeeded(
        aircraftState,
        pilotIdentitySnapshot,
        flightPlanSnapshot,
        effectiveNetworkPlanSnapshot);
    xvatsim::modules::ctaf_lookup::CtafLookupEntry departureCtafLookup;
    xvatsim::modules::ctaf_lookup::CtafLookupEntry arrivalCtafLookup;
    if (gFlightContext.active) {
        if (!gFlightContext.departureIcao.empty()) {
            departureCtafLookup = gCtafLookupService.Lookup(gFlightContext.departureIcao);
        }
        if (!gFlightContext.destinationIcao.empty()) {
            arrivalCtafLookup = gCtafLookupService.Lookup(gFlightContext.destinationIcao);
        }
    }

    xvatsim::brain::RouteSectorSnapshot routeSectorSnapshot;
    xvatsim::brain::AirportSectorSnapshot departureAirportSectorSnapshot;
    xvatsim::brain::AirportSectorSnapshot arrivalAirportSectorSnapshot;
    xvatsim::brain::ModuleBoardSnapshot departureBoardSnapshot;
    xvatsim::brain::ModuleBoardSnapshot arrivalBoardSnapshot;
    xvatsim::brain::ModuleBoardSnapshot enrouteBoardSnapshot;
    xvatsim::brain::ModuleBoardSnapshot activeBoardSnapshot;
    HandoffDecision workflowDecision;

    if (gFlightContext.active) {
        if (IsOnGroundAtDestination(aircraftState) ||
            (!aircraftState.onGround && IsInsideArrivalWakeDistance(aircraftState))) {
            gArrivalAwakeThisFlight = true;
        }

        if (!gDepartureReleasedThisFlight && !gArrivalAwakeThisFlight) {
            departureAirportSectorSnapshot = gRouteSectorResolver.ResolveAirportCoverage(
                gFlightContext.departureIcao,
                gFlightContext.hasDepartureCoordinates,
                gFlightContext.departureLatDeg,
                gFlightContext.departureLonDeg);
            departureBoardSnapshot = CollectDepartureBoardCached(
                xPilotSessionSnapshot,
                controllerFeedSnapshot,
                radioStateSnapshot,
                gFlightContext.departureIcao,
                departureAirportSectorSnapshot,
                departureCtafLookup);
        }

        if (gArrivalAwakeThisFlight) {
            arrivalAirportSectorSnapshot = gRouteSectorResolver.ResolveAirportCoverage(
                gFlightContext.destinationIcao,
                gFlightContext.hasDestinationCoordinates,
                gFlightContext.destinationLatDeg,
                gFlightContext.destinationLonDeg);
            arrivalBoardSnapshot = CollectArrivalBoardCached(
                xPilotSessionSnapshot,
                controllerFeedSnapshot,
                radioStateSnapshot,
                gFlightContext.destinationIcao,
                arrivalAirportSectorSnapshot,
                arrivalCtafLookup);
        }

        routeSectorSnapshot = gRouteSectorResolver.Resolve(
            aircraftState,
            effectiveNetworkPlanSnapshot);
        enrouteBoardSnapshot = CollectEnrouteBoardCached(
            xPilotSessionSnapshot,
            controllerFeedSnapshot,
            radioStateSnapshot,
            routeSectorSnapshot);

        workflowDecision = ResolveWorkflowStage(
            aircraftState,
            radioStateSnapshot,
            departureAirportSectorSnapshot,
            departureBoardSnapshot,
            enrouteBoardSnapshot);

        activeBoardSnapshot = BuildDisplayBoard(
            workflowDecision.stage,
            departureBoardSnapshot,
            arrivalBoardSnapshot,
            enrouteBoardSnapshot);
    }

    const auto workflowStage = workflowDecision.stage;
    UpdateEnrouteInitialDisplayHold(workflowStage);

    ApplyStandbyRecommendation(
        workflowStage,
        effectiveNetworkPlanSnapshot,
        radioStateSnapshot,
        &activeBoardSnapshot);

    const auto autoWake = ShouldAutoWakeOverlay(
        aircraftState,
        xPilotSessionSnapshot,
        workflowStage,
        enrouteBoardSnapshot);
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

    if (criticalWake) {
        shouldWake = true;
    }

    if (controllerMessageWake) {
        shouldWake = true;
    }

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
               !enrouteBoardSnapshot.stations.empty()) {
        wakeReason = "enroute-board";
    } else if (workflowStage == xvatsim::brain::WorkflowStage::None) {
        wakeReason = "startup";
    }

    if (!shouldWake) {
        xvatsim::brain::OverlayViewModel overlayModel;
        overlayModel = {};
        overlayModel.mode = xvatsim::brain::OverlayMode::Dormant;
        overlayModel.visible = false;
        const auto& loggedAirportSectorSnapshot =
            workflowStage == xvatsim::brain::WorkflowStage::Departure
                ? departureAirportSectorSnapshot
                : arrivalAirportSectorSnapshot;
        LogDisplayDecisionIfChanged(
            workflowStage,
            workflowDecision.reason.c_str(),
            effectiveNetworkPlanSnapshot,
            routeSectorSnapshot,
            loggedAirportSectorSnapshot,
            departureBoardSnapshot,
            arrivalBoardSnapshot,
            enrouteBoardSnapshot,
            shouldWake,
            wakeReason);
        LogBoardContentsIfChanged(
            departureBoardSnapshot,
            arrivalBoardSnapshot,
            enrouteBoardSnapshot);

        if (hideUntilXpilotConnect) {
            gOverlayWindow.Hide();
            PersistOverlayGeometryIfChanged();
            return;
        }

        gOverlayWindow.Update(overlayModel);
        PersistOverlayGeometryIfChanged();
        return;
    }

    xvatsim::brain::TransceiverResolutionSnapshot transceiverResolutionSnapshot;
    if (NeedsTransceiverResolution(
            workflowStage,
            xPilotSessionSnapshot,
            activeBoardSnapshot)) {
        transceiverResolutionSnapshot =
            gTransceiverResolver.Resolve(aircraftState, controllerFeedSnapshot);
    }

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

    const auto& loggedAirportSectorSnapshot =
        workflowStage == xvatsim::brain::WorkflowStage::Departure
            ? departureAirportSectorSnapshot
            : arrivalAirportSectorSnapshot;
    LogDisplayDecisionIfChanged(
        workflowStage,
        workflowDecision.reason.c_str(),
        effectiveNetworkPlanSnapshot,
        routeSectorSnapshot,
        loggedAirportSectorSnapshot,
        departureBoardSnapshot,
        arrivalBoardSnapshot,
        enrouteBoardSnapshot,
        shouldWake,
        wakeReason);
    LogBoardContentsIfChanged(
        departureBoardSnapshot,
        arrivalBoardSnapshot,
        enrouteBoardSnapshot);

    gOverlayWindow.Update(overlayModel);
    PersistOverlayGeometryIfChanged();
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
    const auto refreshElapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - refreshStarted)
            .count();
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
