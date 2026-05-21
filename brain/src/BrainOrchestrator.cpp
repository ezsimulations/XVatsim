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

int DepartureRoleRank(StationRole role) {
    switch (role) {
        case StationRole::Delivery:
            return 0;
        case StationRole::Ground:
            return 1;
        case StationRole::Tower:
            return 2;
        case StationRole::Departure:
            return 3;
        case StationRole::Center:
            return 4;
        case StationRole::Atis:
            return 5;
        case StationRole::Ctaf:
        case StationRole::Unicom:
            return 6;
        case StationRole::Approach:
        case StationRole::Other:
        default:
            return 7;
    }
}

int ArrivalRoleRank(StationRole role) {
    switch (role) {
        case StationRole::Center:
            return 0;
        case StationRole::Approach:
            return 1;
        case StationRole::Departure:
            return 1;
        case StationRole::Tower:
            return 2;
        case StationRole::Ground:
            return 3;
        case StationRole::Atis:
            return 4;
        case StationRole::Ctaf:
        case StationRole::Unicom:
            return 5;
        case StationRole::Delivery:
        case StationRole::Other:
        default:
            return 6;
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

std::vector<BoardStationSnapshot> OrderStations(
    const ModuleBoardSnapshot& boardSnapshot) {
    auto orderedStations = boardSnapshot.stations;
    if (boardSnapshot.source == BoardSource::Enroute) {
        std::stable_sort(
            orderedStations.begin(),
            orderedStations.end(),
            [](const auto& left, const auto& right) {
                if (left.hasRouteEntryDistance != right.hasRouteEntryDistance) {
                    return left.hasRouteEntryDistance && !right.hasRouteEntryDistance;
                }

                if (left.hasRouteEntryDistance && right.hasRouteEntryDistance &&
                    left.routeEntryDistanceNm != right.routeEntryDistanceNm) {
                    return left.routeEntryDistanceNm < right.routeEntryDistanceNm;
                }

                if (left.sectorActive != right.sectorActive) {
                    return left.sectorActive && !right.sectorActive;
                }

                if (left.tuned != right.tuned) {
                    return left.tuned && !right.tuned;
                }

                return left.callsign < right.callsign;
            });
        return orderedStations;
    }

    std::stable_sort(
        orderedStations.begin(),
        orderedStations.end(),
        [&boardSnapshot](const auto& left, const auto& right) {
            const auto leftRank =
                boardSnapshot.source == BoardSource::Arrival
                    ? ArrivalRoleRank(left.role)
                    : DepartureRoleRank(left.role);
            const auto rightRank =
                boardSnapshot.source == BoardSource::Arrival
                    ? ArrivalRoleRank(right.role)
                    : DepartureRoleRank(right.role);

            if (leftRank != rightRank) {
                return leftRank < rightRank;
            }

            if (left.tuned != right.tuned) {
                return left.tuned && !right.tuned;
            }

            if (left.frequency != right.frequency) {
                return left.frequency < right.frequency;
            }

            return left.callsign < right.callsign;
        });
    return orderedStations;
}

OverlayTextLine FormatBoardLine(
    const BoardStationSnapshot& station,
    BoardSource boardSource) {
    OverlayTextLine line;
    const auto isEnrouteCenter = station.role == StationRole::Center;
    if (isEnrouteCenter) {
        if (station.offline) {
            line.tone = OverlayTone::Normal;
        } else if (station.sectorActive) {
            line.tone = OverlayTone::Active;
        } else if (station.hasRouteEntryDistance) {
            line.tone = OverlayTone::Next;
        } else {
            line.tone = station.tuned ? OverlayTone::Active : OverlayTone::Normal;
        }
    } else {
        line.tone = (station.next || station.standby) ? OverlayTone::Next
                                 : ((station.tuned || station.sectorActive)
                                        ? OverlayTone::Active
                                        : OverlayTone::Normal);
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

    if (isEnrouteCenter) {
        if (station.tuned) {
            text += " *Active*";
        } else if (station.offline) {
            text += " *OFFLINE*";
        }
    } else if (station.standby) {
        text += " *Standby*";
    } else if (station.next) {
        text += " *NEXT*";
    } else if (station.sectorActive) {
        text += " *ACTIVE*";
    } else if (station.online) {
        text += " *ONLINE*";
    } else if (station.offline) {
        text += " *OFFLINE*";
    } else if (station.tuned) {
        text += " *Active*";
    }

    line.text = SanitizeDisplayText(std::move(text), kMaxDisplayLineChars);
    return line;
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
    const ModuleBoardSnapshot& activeBoardSnapshot,
    const ManualQuerySnapshot& manualQuerySnapshot) {
    (void)controllerFeedSnapshot;

    OverlayViewModel model;
    model.mode = aircraftState.onGround ? OverlayMode::Prewarm : OverlayMode::Active;
    model.visible = true;
    model.title = "XVatsim " + StageLabel(workflowStage);
    model.headerRightText = FormatHeaderRightText(networkPlanSnapshot);
    model.radioState = radioStateSnapshot;

    std::vector<OverlayTextLine> bodyLines = {
        {FormatXPilotLine(xPilotSessionSnapshot), OverlayTone::Normal},
    };

    if (activeBoardSnapshot.available || activeBoardSnapshot.displayStations) {
        const auto orderedStations = OrderStations(activeBoardSnapshot);
        const auto countToDisplay =
            std::min(orderedStations.size(), kMaxDisplayedStations);
        for (std::size_t index = 0; index < countToDisplay; ++index) {
            const auto& station = orderedStations[index];
            bodyLines.push_back(FormatBoardLine(station, activeBoardSnapshot.source));
        }
        if (orderedStations.size() > countToDisplay) {
            bodyLines.push_back(
                {"+" + std::to_string(orderedStations.size() - countToDisplay) +
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

}  // namespace xvatsim::brain
