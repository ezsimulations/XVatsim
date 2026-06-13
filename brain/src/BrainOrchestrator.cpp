#include "XVatsim/brain/BrainOrchestrator.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace xvatsim::brain {

namespace {

constexpr std::size_t kMaxDisplayStatusChars = 80;
constexpr std::size_t kMaxDisplayCallsignChars = 32;
constexpr std::size_t kMaxDisplayFrequencyChars = 16;
constexpr std::size_t kMaxDisplayAnnotationChars = 32;
constexpr std::size_t kMaxDisplayLineChars = 96;
constexpr std::size_t kMaxDisplayedStations = 40;

std::string FormatFixed(double value, int precision) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

std::string SanitizeDisplayText(std::string value, std::size_t maxChars) {
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

std::string FormatXPilotLine(const XPilotSessionSnapshot& xPilotSessionSnapshot) {
    if (!xPilotSessionSnapshot.statusLine.empty()) {
        auto statusLine = xPilotSessionSnapshot.statusLine;
        if (statusLine.rfind("XP ", 0) == 0) {
            statusLine.erase(0, 3);
        }
        const auto disconnectedPos = statusLine.find("disconnected");
        if (disconnectedPos != std::string::npos) {
            statusLine.replace(disconnectedPos, std::strlen("disconnected"), "Disconnected");
        }
        return SanitizeDisplayText(std::move(statusLine), kMaxDisplayStatusChars);
    }

    if (xPilotSessionSnapshot.connected) {
        if (!xPilotSessionSnapshot.callsign.empty()) {
            return "xPilot connected " +
                   SanitizeDisplayText(
                       xPilotSessionSnapshot.callsign,
                       kMaxDisplayCallsignChars);
        }

        return "xPilot connected";
    }

    if (xPilotSessionSnapshot.loaded) {
        return "xPilot loaded";
    }

    return "xPilot not loaded";
}

std::string FormatReceivableLine(
    const TransceiverResolutionSnapshot& transceiverResolutionSnapshot) {
    if (transceiverResolutionSnapshot.stale) {
        return transceiverResolutionSnapshot.statusLine.empty()
                   ? "RX feed stale"
                   : SanitizeDisplayText(
                         transceiverResolutionSnapshot.statusLine,
                         kMaxDisplayStatusChars);
    }

    if (!transceiverResolutionSnapshot.available && !transceiverResolutionSnapshot.statusLine.empty()) {
        return SanitizeDisplayText(
            transceiverResolutionSnapshot.statusLine,
            kMaxDisplayStatusChars);
    }

    if (transceiverResolutionSnapshot.candidates.empty()) {
        return "RX 0 controllers in range";
    }

    return "RX " +
           std::to_string(transceiverResolutionSnapshot.receivableControllers) +
           " controller" +
           (transceiverResolutionSnapshot.receivableControllers == 1 ? "" : "s") +
           " in range";
}

std::string FormatManualQueryLine(const ManualQuerySnapshot& manualQuerySnapshot) {
    if (!manualQuerySnapshot.visible || manualQuerySnapshot.line.empty()) {
        return {};
    }

    return SanitizeDisplayText(manualQuerySnapshot.line, kMaxDisplayLineChars);
}

double ToRadians(double degrees) {
    return degrees * 3.14159265358979323846 / 180.0;
}

double GreatCircleDistanceNm(
    double latitudeDegA,
    double longitudeDegA,
    double latitudeDegB,
    double longitudeDegB) {
    const auto latitudeRadA = ToRadians(latitudeDegA);
    const auto latitudeRadB = ToRadians(latitudeDegB);
    const auto deltaLatitude = ToRadians(latitudeDegB - latitudeDegA);
    const auto deltaLongitude = ToRadians(longitudeDegB - longitudeDegA);

    const auto sinLatitude = std::sin(deltaLatitude / 2.0);
    const auto sinLongitude = std::sin(deltaLongitude / 2.0);
    const auto a = sinLatitude * sinLatitude +
                   std::cos(latitudeRadA) * std::cos(latitudeRadB) *
                       sinLongitude * sinLongitude;
    const auto c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return 3440.065 * c;
}

std::string FormatNetworkPlanLine(
    const AircraftStateSnapshot& aircraftState,
    const NetworkPlanSnapshot& networkPlanSnapshot) {
    if (networkPlanSnapshot.statusLine.empty()) {
        return {};
    }

    if (!networkPlanSnapshot.hasDestinationCoordinates || !aircraftState.valid) {
        return SanitizeDisplayText(
            networkPlanSnapshot.statusLine,
            kMaxDisplayStatusChars);
    }

    const auto distanceNm = GreatCircleDistanceNm(
        aircraftState.latitudeDeg,
        aircraftState.longitudeDeg,
        networkPlanSnapshot.destinationLatDeg,
        networkPlanSnapshot.destinationLonDeg);
    return SanitizeDisplayText(
        networkPlanSnapshot.statusLine + " " + FormatFixed(distanceNm, 0) + "nm",
        kMaxDisplayLineChars);
}

std::string FormatHeaderRightText(const NetworkPlanSnapshot& networkPlanSnapshot) {
    if (networkPlanSnapshot.stale ||
        !networkPlanSnapshot.hasFiledCruiseAltitude ||
        networkPlanSnapshot.filedCruiseAltitudeFt <= 0.0) {
        return {};
    }

    const auto roundedAltitudeFt =
        static_cast<int>(std::round(networkPlanSnapshot.filedCruiseAltitudeFt / 100.0)) * 100;
    if (roundedAltitudeFt >= 18000) {
        return "FL" + std::to_string(roundedAltitudeFt / 100);
    }

    return std::to_string(roundedAltitudeFt);
}

std::string StageLabel(WorkflowStage workflowStage) {
    switch (workflowStage) {
        case WorkflowStage::Departure:
            return "departure";
        case WorkflowStage::Enroute:
            return "enroute";
        case WorkflowStage::Arrival:
            return "arrival";
        case WorkflowStage::None:
        default:
            return "ready";
    }
}

std::string RoleLabel(StationRole role) {
    switch (role) {
        case StationRole::Delivery:
            return "DEL";
        case StationRole::Ground:
            return "GND";
        case StationRole::Tower:
            return "TWR";
        case StationRole::Departure:
            return "DEP";
        case StationRole::Approach:
            return "APP";
        case StationRole::Center:
            return "CTR";
        case StationRole::Atis:
            return "ATIS";
        case StationRole::Ctaf:
            return "CTAF";
        case StationRole::Unicom:
            return "NO CTAF / UNICOM";
        case StationRole::Other:
        default:
            return "ATC";
    }
}

bool HasDisplayRelationTone(DisplayRelation relation) {
    return relation == DisplayRelation::CurrentPolygon ||
           relation == DisplayRelation::NextPolygon ||
           relation == DisplayRelation::ArrivalPrep;
}

OverlayTone ToneForDisplayRelation(DisplayRelation relation) {
    if (relation == DisplayRelation::CurrentPolygon) {
        return OverlayTone::Active;
    }
    if (relation == DisplayRelation::NextPolygon ||
        relation == DisplayRelation::ArrivalPrep) {
        return OverlayTone::Next;
    }
    return OverlayTone::Normal;
}

OverlayTextLine FormatBoardLine(
    const FinalDisplayStationSnapshot& station,
    BoardSource boardSource) {
    OverlayTextLine line;
    if (HasDisplayRelationTone(station.displayRelation)) {
        line.tone = ToneForDisplayRelation(station.displayRelation);
    } else {
        line.tone = OverlayTone::Normal;
    }

    std::string text = SanitizeDisplayText(RoleLabel(station.role), 24);
    const auto callsign =
        SanitizeDisplayText(station.callsign, kMaxDisplayCallsignChars);
    if (!callsign.empty()) {
        text += " " + callsign;
    }

    const auto frequency =
        SanitizeDisplayText(station.frequency, kMaxDisplayFrequencyChars);
    if (!frequency.empty()) {
        text += " " + frequency;
    }

    const auto annotation =
        SanitizeDisplayText(station.annotation, kMaxDisplayAnnotationChars);
    if (!annotation.empty()) {
        text += " " + annotation;
    }

    if (station.tuned) {
        text += " *Active*";
    } else if (station.standby) {
        text += " *Standby*";
    }

    line.text = SanitizeDisplayText(std::move(text), kMaxDisplayLineChars);
    return line;
}

std::string FormatVersionText(const std::string& version) {
    const auto sanitizedVersion = SanitizeDisplayText(version, 16);
    if (sanitizedVersion.empty()) {
        return {};
    }
    return "V" + sanitizedVersion;
}

OverlayVersionSnapshot BuildVersionSnapshot(
    const OverlayUpdateSnapshot& updateSnapshot) {
    OverlayVersionSnapshot version;
    version.text = FormatVersionText(updateSnapshot.installedVersion);
    version.tone = OverlayVersionTone::Unknown;

    switch (updateSnapshot.status) {
        case OverlayUpdateStatus::Current:
            version.tone = OverlayVersionTone::Current;
            break;
        case OverlayUpdateStatus::Available:
            version.tone = OverlayVersionTone::UpdateAvailable;
            version.alternateText = "UPDATE";
            version.rotateAlternate = true;
            break;
        case OverlayUpdateStatus::Failed:
            version.tone = updateSnapshot.manualNoticeRequested
                ? OverlayVersionTone::Error
                : OverlayVersionTone::Unknown;
            break;
        case OverlayUpdateStatus::Checking:
        case OverlayUpdateStatus::Unknown:
        default:
            version.tone = OverlayVersionTone::Unknown;
            break;
    }

    return version;
}

OverlayNoticeSnapshot BuildSystemNotice(
    const OverlayUpdateSnapshot& updateSnapshot) {
    const auto noticeRequested =
        updateSnapshot.manualNoticeRequested ||
        updateSnapshot.automaticNoticeRequested;
    if (!noticeRequested) {
        return {};
    }

    OverlayNoticeSnapshot notice;
    notice.visible = true;
    notice.dismissible = true;
    notice.dismissText = "Close";

    const auto installedVersion = FormatVersionText(updateSnapshot.installedVersion);
    const auto latestVersion = FormatVersionText(updateSnapshot.latestVersion);
    const auto installedLine = installedVersion.empty()
        ? std::string{"Installed: unknown"}
        : "Installed: " + installedVersion;

    switch (updateSnapshot.status) {
        case OverlayUpdateStatus::Available:
            notice.severity = updateSnapshot.critical
                ? OverlayNoticeSeverity::Error
                : OverlayNoticeSeverity::Warning;
            notice.title = "XVatsim update available";
            notice.bodyLines.push_back(installedLine);
            notice.bodyLines.push_back(
                latestVersion.empty()
                    ? std::string{"Latest: available"}
                    : "Latest: " + latestVersion);
            notice.bodyLines.push_back("Download: X-Plane.org");
            break;
        case OverlayUpdateStatus::Current:
            notice.severity = OverlayNoticeSeverity::Success;
            notice.title = "XVatsim is up to date";
            notice.bodyLines.push_back(installedLine);
            notice.bodyLines.push_back("No update is available.");
            break;
        case OverlayUpdateStatus::Failed:
            notice.severity = OverlayNoticeSeverity::Error;
            notice.title = "Update check failed";
            notice.bodyLines.push_back(installedLine);
            notice.bodyLines.push_back("Try again from Plugins > XVatsim.");
            if (!updateSnapshot.errorClass.empty()) {
                notice.bodyLines.push_back(
                    "Reason: " +
                    SanitizeDisplayText(updateSnapshot.errorClass, 42));
            }
            break;
        case OverlayUpdateStatus::Checking:
            notice.severity = OverlayNoticeSeverity::Info;
            notice.title = "Checking for updates";
            notice.bodyLines.push_back(installedLine);
            notice.bodyLines.push_back("Contacting the update manifest.");
            break;
        case OverlayUpdateStatus::Unknown:
        default:
            notice.severity = OverlayNoticeSeverity::Info;
            notice.title = "Update status unavailable";
            notice.bodyLines.push_back(installedLine);
            break;
    }

    return notice;
}

void AssignBodyLines(
    const std::vector<OverlayTextLine>& lines,
    OverlayViewModel* model) {
    if (model == nullptr) {
        return;
    }

    std::vector<OverlayTextLine> compactLines;
    compactLines.reserve(lines.size());
    for (const auto& line : lines) {
        auto sanitizedLine = line;
        sanitizedLine.text =
            SanitizeDisplayText(sanitizedLine.text, kMaxDisplayLineChars);
        if (!sanitizedLine.text.empty()) {
            compactLines.push_back(std::move(sanitizedLine));
        }
    }
    model->bodyLines = std::move(compactLines);
}

}  // namespace

OverlayViewModel BrainOrchestrator::BuildOverlayViewModel(
    WorkflowStage workflowStage,
    const AircraftStateSnapshot& aircraftState,
    const XPilotSessionSnapshot& xPilotSessionSnapshot,
    const RadioStateSnapshot& radioStateSnapshot,
    const NetworkPlanSnapshot& networkPlanSnapshot,
    const ControllerFeedSnapshot& controllerFeedSnapshot,
    const TransceiverResolutionSnapshot& transceiverResolutionSnapshot,
    const FinalDisplaySnapshot& finalDisplaySnapshot,
    const ManualQuerySnapshot& manualQuerySnapshot,
    const OverlayUpdateSnapshot& updateSnapshot) {
    (void)controllerFeedSnapshot;

    OverlayViewModel model;
    model.mode = aircraftState.onGround ? OverlayMode::Prewarm : OverlayMode::Active;
    model.visible = true;
    model.title = "XVatsim " + StageLabel(workflowStage);
    model.headerRightText = FormatHeaderRightText(networkPlanSnapshot);
    model.radioState = radioStateSnapshot;
    model.version = BuildVersionSnapshot(updateSnapshot);
    model.systemNotice = BuildSystemNotice(updateSnapshot);

    std::vector<OverlayTextLine> bodyLines = {
        {FormatXPilotLine(xPilotSessionSnapshot), OverlayTone::Normal},
    };

    if (finalDisplaySnapshot.available || !finalDisplaySnapshot.stations.empty()) {
        const auto countToDisplay =
            std::min(finalDisplaySnapshot.stations.size(), kMaxDisplayedStations);
        for (std::size_t index = 0; index < countToDisplay; ++index) {
            const auto& station = finalDisplaySnapshot.stations[index];
            bodyLines.push_back(FormatBoardLine(station, finalDisplaySnapshot.source));
        }
        if (finalDisplaySnapshot.stations.size() > countToDisplay) {
            bodyLines.push_back(
                {"+" + std::to_string(
                     finalDisplaySnapshot.stations.size() - countToDisplay) +
                     " more ATC",
                 OverlayTone::Normal});
        }
    } else if (workflowStage == WorkflowStage::Enroute) {
        bodyLines.push_back(
            {"Route ATC offline", OverlayTone::Normal});
    } else {
        bodyLines.push_back(
            {FormatReceivableLine(transceiverResolutionSnapshot), OverlayTone::Normal});
    }

    bodyLines.push_back(
        {manualQuerySnapshot.visible
             ? FormatManualQueryLine(manualQuerySnapshot)
             : FormatNetworkPlanLine(aircraftState, networkPlanSnapshot),
         OverlayTone::Normal});
    AssignBodyLines(bodyLines, &model);
    return model;
}

bool OverlayUpdateRequestsWake(const OverlayUpdateSnapshot& updateSnapshot) {
    return BuildSystemNotice(updateSnapshot).visible;
}

}  // namespace xvatsim::brain
