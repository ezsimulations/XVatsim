#include "XVatsim/brain/BrainDisplayIntent.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace xvatsim::brain {
namespace {

constexpr double kCurrentPolygonDistanceToleranceNm = 0.5;

void HashCombine(std::uint64_t* seed, std::uint64_t value) {
    if (seed == nullptr) {
        return;
    }
    *seed ^= value + 0x9e3779b97f4a7c15ULL + (*seed << 6) + (*seed >> 2);
}

void HashCombine(std::uint64_t* seed, const std::string& value) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : value) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    HashCombine(seed, hash);
}

void HashCombine(std::uint64_t* seed, double value) {
    const auto quantized =
        static_cast<std::uint64_t>(std::llround(std::max(0.0, value) * 10.0));
    HashCombine(seed, quantized);
}

std::string NormalizeKey(std::string value) {
    value.erase(
        std::remove_if(
            value.begin(),
            value.end(),
            [](unsigned char ch) { return std::isspace(ch) != 0; }),
        value.end());
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return value;
}

bool KeysEqual(const std::string& left, const std::string& right) {
    const auto normalizedLeft = NormalizeKey(left);
    const auto normalizedRight = NormalizeKey(right);
    return !normalizedLeft.empty() && normalizedLeft == normalizedRight;
}

bool IsCenter(const BoardStationSnapshot& station) {
    return station.role == StationRole::Center;
}

bool IsDisplayableStation(const BoardStationSnapshot& station) {
    return !station.offline && !station.frequency.empty();
}

std::string StationKey(const BoardStationSnapshot& station) {
    return std::to_string(static_cast<int>(station.role)) + "|" +
           NormalizeKey(station.callsign) + "|" +
           NormalizeKey(station.frequency);
}

std::string FormatDistanceAnnotation(double distanceNm) {
    const auto rounded =
        static_cast<int>(std::round(std::max(0.0, distanceNm)));
    return std::to_string(rounded) + "nm";
}

void AppendUnique(
    const BoardStationSnapshot& station,
    ModuleBoardSnapshot* board,
    std::unordered_set<std::string>* keys) {
    if (board == nullptr || keys == nullptr) {
        return;
    }
    if (keys->insert(StationKey(station)).second) {
        board->stations.push_back(station);
        board->available = true;
        board->displayStations = true;
    }
}

DisplayRelation InferCenterRelation(
    const BoardStationSnapshot& station,
    const BrainDisplayIntentInput& input) {
    if (station.displayRelation != DisplayRelation::Unknown) {
        return station.displayRelation;
    }
    if (station.sectorActive || station.tuned ||
        KeysEqual(station.polygonKey, input.currentPolygonKey)) {
        return DisplayRelation::CurrentPolygon;
    }
    if (station.next || KeysEqual(station.polygonKey, input.nextPolygonKey)) {
        return DisplayRelation::NextPolygon;
    }
    if (KeysEqual(station.polygonKey, input.arrivalPolygonKey)) {
        return DisplayRelation::ArrivalPrep;
    }
    if (station.hasRouteEntryDistance) {
        return station.routeEntryDistanceNm <=
                       input.routeProgressDistanceNm +
                           kCurrentPolygonDistanceToleranceNm
                   ? DisplayRelation::CurrentPolygon
                   : DisplayRelation::NextPolygon;
    }
    return DisplayRelation::Hidden;
}

BoardStationSnapshot BuildDisplayStation(
    BoardStationSnapshot station,
    DisplayRelation relation,
    const BrainDisplayIntentInput& input) {
    station.displayRelation = relation;
    station.next = false;
    station.standby = false;

    if (!IsCenter(station)) {
        return station;
    }

    if (relation == DisplayRelation::CurrentPolygon ||
        relation == DisplayRelation::ArrivalPrep) {
        station.sectorActive = true;
        station.next = false;
        station.annotation.clear();
        station.hasRouteEntryDistance = false;
        station.routeEntryDistanceNm = 0.0;
        return station;
    }

    if (relation == DisplayRelation::NextPolygon) {
        const auto routeEntryDistanceNm =
            station.hasRouteEntryDistance
                ? std::max(0.0, station.routeEntryDistanceNm)
                : 0.0;
        const auto remainingDistanceNm =
            station.hasRouteEntryDistance
                ? std::max(
                      0.0,
                      routeEntryDistanceNm - input.routeProgressDistanceNm)
                : 0.0;
        station.sectorActive = false;
        station.next = true;
        station.annotation = FormatDistanceAnnotation(remainingDistanceNm);
        station.hasRouteEntryDistance = true;
        station.routeEntryDistanceNm = routeEntryDistanceNm;
        return station;
    }

    return station;
}

void AddBoardStations(
    const ModuleBoardSnapshot& source,
    ModuleBoardSnapshot* target,
    std::unordered_set<std::string>* keys) {
    if (target == nullptr || keys == nullptr) {
        return;
    }
    for (const auto& station : source.stations) {
        if (!IsDisplayableStation(station)) {
            continue;
        }
        AppendUnique(station, target, keys);
    }
}

ModuleBoardSnapshot MakeDisplayShell(
    const ModuleBoardSnapshot& source,
    BoardSource boardSource) {
    auto display = source;
    display.source = boardSource;
    display.stations.clear();
    display.available = false;
    display.displayStations = false;
    return display;
}

ModuleBoardSnapshot BuildDisplayBoard(
    WorkflowStage stage,
    const ModuleBoardSnapshot& departureBoard,
    const ModuleBoardSnapshot& arrivalBoard,
    const ModuleBoardSnapshot& enrouteBoard) {
    ModuleBoardSnapshot display;
    std::unordered_set<std::string> keys;

    if (stage == WorkflowStage::Arrival) {
        display = MakeDisplayShell(arrivalBoard, BoardSource::Arrival);
        keys.reserve(arrivalBoard.stations.size() + enrouteBoard.stations.size());
        AddBoardStations(arrivalBoard, &display, &keys);
        AddBoardStations(enrouteBoard, &display, &keys);
        return display;
    }

    if (stage == WorkflowStage::Departure) {
        display = MakeDisplayShell(departureBoard, BoardSource::Departure);
        keys.reserve(departureBoard.stations.size() + enrouteBoard.stations.size());
        AddBoardStations(departureBoard, &display, &keys);
        for (const auto& station : enrouteBoard.stations) {
            if (station.displayRelation == DisplayRelation::CurrentPolygon &&
                IsDisplayableStation(station)) {
                AppendUnique(station, &display, &keys);
            }
        }
        return display;
    }

    if (stage == WorkflowStage::Enroute) {
        display = MakeDisplayShell(enrouteBoard, BoardSource::Enroute);
        keys.reserve(enrouteBoard.stations.size());
        AddBoardStations(enrouteBoard, &display, &keys);
        return display;
    }

    return display;
}

void SortEnrouteStations(ModuleBoardSnapshot* board) {
    if (board == nullptr) {
        return;
    }
    std::stable_sort(
        board->stations.begin(),
        board->stations.end(),
        [](const auto& left, const auto& right) {
            if (left.displayRelation != right.displayRelation) {
                const auto rank = [](DisplayRelation relation) {
                    switch (relation) {
                        case DisplayRelation::CurrentPolygon:
                            return 0;
                        case DisplayRelation::NextPolygon:
                            return 1;
                        case DisplayRelation::ArrivalPrep:
                            return 2;
                        default:
                            return 3;
                    }
                };
                return rank(left.displayRelation) < rank(right.displayRelation);
            }
            if (left.hasRouteEntryDistance != right.hasRouteEntryDistance) {
                return left.hasRouteEntryDistance && !right.hasRouteEntryDistance;
            }
            if (left.hasRouteEntryDistance && right.hasRouteEntryDistance &&
                left.routeEntryDistanceNm != right.routeEntryDistanceNm) {
                return left.routeEntryDistanceNm < right.routeEntryDistanceNm;
            }
            if (left.tuned != right.tuned) {
                return left.tuned && !right.tuned;
            }
            return left.callsign < right.callsign;
        });
}

void AddDiagnostic(
    const BoardStationSnapshot& station,
    DisplayRelation relation,
    const char* action,
    std::vector<std::string>* diagnostics) {
    if (diagnostics == nullptr) {
        return;
    }
    std::ostringstream stream;
    stream << action
           << ":callsign=" << station.callsign
           << ",freq=" << station.frequency
           << ",role=" << static_cast<int>(station.role)
           << ",polygon=" << station.polygonKey
           << ",relation=" << ToString(relation);
    if (station.hasRouteEntryDistance) {
        stream << ",distanceNm="
               << static_cast<int>(std::round(station.routeEntryDistanceNm));
    }
    diagnostics->push_back(stream.str());
}

void HashStation(std::uint64_t* hash, const BoardStationSnapshot& station) {
    HashCombine(hash, static_cast<std::uint64_t>(station.role));
    HashCombine(hash, station.callsign);
    HashCombine(hash, station.frequency);
    HashCombine(hash, station.annotation);
    HashCombine(hash, station.polygonKey);
    HashCombine(hash, static_cast<std::uint64_t>(station.displayRelation));
    HashCombine(hash, static_cast<std::uint64_t>(station.tuned ? 1u : 0u));
    HashCombine(hash, static_cast<std::uint64_t>(station.next ? 1u : 0u));
    HashCombine(hash, static_cast<std::uint64_t>(station.standby ? 1u : 0u));
    HashCombine(hash, static_cast<std::uint64_t>(station.sectorActive ? 1u : 0u));
    HashCombine(hash, static_cast<std::uint64_t>(station.online ? 1u : 0u));
    HashCombine(hash, static_cast<std::uint64_t>(station.offline ? 1u : 0u));
    HashCombine(hash, static_cast<std::uint64_t>(station.hasRouteEntryDistance ? 1u : 0u));
    if (station.hasRouteEntryDistance) {
        HashCombine(hash, station.routeEntryDistanceNm);
    }
}

std::uint64_t HashBoard(const ModuleBoardSnapshot& board) {
    std::uint64_t hash = 0;
    HashCombine(&hash, static_cast<std::uint64_t>(board.source));
    HashCombine(&hash, board.airportIcao);
    HashCombine(&hash, static_cast<std::uint64_t>(board.stations.size()));
    for (const auto& station : board.stations) {
        HashStation(&hash, station);
    }
    return hash;
}

}  // namespace

const char* ToString(DisplayRelation relation) {
    switch (relation) {
        case DisplayRelation::CurrentPolygon:
            return "CURRENT_POLYGON";
        case DisplayRelation::NextPolygon:
            return "NEXT_POLYGON";
        case DisplayRelation::ArrivalPrep:
            return "ARRIVAL_PREP";
        case DisplayRelation::Filtered:
            return "FILTERED";
        case DisplayRelation::Hidden:
            return "HIDDEN";
        case DisplayRelation::Unknown:
        default:
            return "UNKNOWN";
    }
}

BrainDisplayIntentOutput RunBrainDisplayIntentWorker(
    const BrainDisplayIntentInput& input) {
    BrainDisplayIntentOutput output;
    output.reason = "brain-display-intent";
    output.departureBoard = input.departureBoard;
    output.arrivalBoard = input.arrivalBoard;
    output.enrouteBoard = input.enrouteBoard;
    output.enrouteBoard.stations.clear();
    output.enrouteBoard.available = false;
    output.enrouteBoard.displayStations = false;

    std::unordered_set<std::string> enrouteKeys;
    for (const auto& station : input.enrouteBoard.stations) {
        if (!IsCenter(station)) {
            continue;
        }
        if (!IsDisplayableStation(station)) {
            ++output.hidden;
            AddDiagnostic(
                station,
                DisplayRelation::Hidden,
                "hidden",
                &output.diagnostics);
            continue;
        }
        const auto relation = InferCenterRelation(station, input);
        if (relation == DisplayRelation::Hidden ||
            relation == DisplayRelation::Filtered ||
            relation == DisplayRelation::Unknown) {
            ++output.hidden;
            AddDiagnostic(station, relation, "hidden", &output.diagnostics);
            continue;
        }

        auto displayStation = BuildDisplayStation(station, relation, input);
        AppendUnique(displayStation, &output.enrouteBoard, &enrouteKeys);
        ++output.displayed;
        AddDiagnostic(
            displayStation,
            relation,
            "display",
            &output.diagnostics);
    }

    SortEnrouteStations(&output.enrouteBoard);

    output.finalDisplay = BuildDisplayBoard(
        input.workflowStage,
        output.departureBoard,
        output.arrivalBoard,
        output.enrouteBoard);
    output.stableHash = HashBoard(output.finalDisplay);

    return output;
}

}  // namespace xvatsim::brain
