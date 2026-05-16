#include "XVatsim/modules/arrival/ArrivalAirspaceModule.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

namespace xvatsim::modules::arrival {

namespace {

constexpr int kVatsimApproachFacility = 5;

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

std::string ToUpperCopy(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
    return value;
}

bool SplitControllerCallsign(
    const std::string& callsign,
    std::string* outPrefix,
    std::string* outSuffix) {
    const auto separatorIndex = callsign.rfind('_');
    if (separatorIndex == std::string::npos || separatorIndex == 0 ||
        separatorIndex >= (callsign.size() - 1)) {
        return false;
    }

    if (outPrefix != nullptr) {
        *outPrefix = ToUpperCopy(callsign.substr(0, separatorIndex));
    }
    if (outSuffix != nullptr) {
        *outSuffix = ToUpperCopy(callsign.substr(separatorIndex + 1));
    }
    return true;
}

bool IsFrequencyTuned(
    const std::string& targetFrequency,
    const brain::RadioStateSnapshot& radioStateSnapshot) {
    const auto normalizedTarget = NormalizeFrequency(targetFrequency);
    if (normalizedTarget.empty()) {
        return false;
    }

    return NormalizeFrequency(radioStateSnapshot.com1ActiveFrequency) == normalizedTarget ||
           NormalizeFrequency(radioStateSnapshot.com2ActiveFrequency) == normalizedTarget;
}

bool IsGuardFrequency(const std::string& frequency) {
    const auto normalizedFrequency = NormalizeFrequency(frequency);
    return normalizedFrequency == "121500" || normalizedFrequency == "199998";
}

brain::StationRole ParseAirspaceRole(const std::string& suffix) {
    if (suffix == "APP") {
        return brain::StationRole::Approach;
    }
    if (suffix == "DEP") {
        return brain::StationRole::Departure;
    }
    return brain::StationRole::Other;
}

bool IsActionableTerminalAirspaceController(
    const brain::ControllerSnapshot& controller) {
    return controller.actionable &&
           !controller.atis &&
           controller.facility == kVatsimApproachFacility;
}

std::string ControllerRegionKey(const std::string& callsign) {
    std::string prefix;
    if (!SplitControllerCallsign(callsign, &prefix, nullptr) || prefix.empty()) {
        return {};
    }

    const auto separator = prefix.find('_');
    if (separator == std::string::npos) {
        return prefix;
    }

    return prefix.substr(0, separator);
}

std::string RoleSuffix(brain::StationRole role) {
    switch (role) {
        case brain::StationRole::Approach:
            return "APP";
        case brain::StationRole::Departure:
            return "DEP";
        default:
            return {};
    }
}

bool EndsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool HasRoleSpecificAirspaceData(
    const brain::AirportSectorSnapshot& airportSectorSnapshot,
    brain::StationRole role) {
    if (!airportSectorSnapshot.available ||
        airportSectorSnapshot.stale ||
        airportSectorSnapshot.coveringSectors.empty()) {
        return false;
    }

    if (!airportSectorSnapshot.hasTerminalCoverageData) {
        return false;
    }

    const auto roleSuffix = RoleSuffix(role);
    if (roleSuffix.empty()) {
        return false;
    }

    const auto tokenSuffix = "_" + roleSuffix;
    for (const auto& sector : airportSectorSnapshot.coveringSectors) {
        if (!sector.terminalCoverage) {
            continue;
        }
        for (const auto& token : sector.matchTokens) {
            if (EndsWith(token, tokenSuffix)) {
                return true;
            }
        }
    }

    return false;
}

bool HasFreshAirportSectorCoverage(
    const brain::AirportSectorSnapshot& airportSectorSnapshot) {
    return airportSectorSnapshot.available &&
           !airportSectorSnapshot.stale &&
           !airportSectorSnapshot.coveringSectors.empty();
}

bool SectorSetMatchesController(
    const std::vector<brain::RouteSectorMatchSnapshot>& sectors,
    const std::string& callsign,
    brain::StationRole role) {
    std::string prefix;
    if (!SplitControllerCallsign(callsign, &prefix, nullptr) || prefix.empty()) {
        return false;
    }

    const auto regionKey = ControllerRegionKey(callsign);
    const auto roleSuffix = RoleSuffix(role);
    const auto prefixRoleKey =
        roleSuffix.empty() ? std::string{} : prefix + "_" + roleSuffix;
    const auto regionRoleKey =
        roleSuffix.empty() || regionKey.empty()
            ? std::string{}
            : regionKey + "_" + roleSuffix;

    for (const auto& sector : sectors) {
        if (!sector.terminalCoverage) {
            continue;
        }
        for (const auto& token : sector.matchTokens) {
            if (!prefixRoleKey.empty() && token == prefixRoleKey) {
                return true;
            }
            if (!regionRoleKey.empty() && token == regionRoleKey) {
                return true;
            }
        }
    }

    return false;
}

void AppendStation(
    const brain::BoardStationSnapshot& station,
    std::vector<brain::BoardStationSnapshot>* stations,
    std::unordered_set<std::string>* keys) {
    if (stations == nullptr || keys == nullptr) {
        return;
    }

    if (IsGuardFrequency(station.frequency)) {
        return;
    }

    std::string key = std::to_string(static_cast<int>(station.role)) + "|";
    if (!station.frequency.empty()) {
        key += NormalizeFrequency(station.frequency);
    } else {
        key += station.callsign;
    }

    if (!keys->insert(key).second) {
        return;
    }

    stations->push_back(station);
}

}  // namespace

brain::ModuleBoardSnapshot ArrivalAirspaceModule::Collect(
    const brain::XPilotSessionSnapshot& xPilotSessionSnapshot,
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const brain::RadioStateSnapshot& radioStateSnapshot,
    const std::string& arrivalAirportIcao,
    const brain::AirportSectorSnapshot& airportSectorSnapshot) const {
    brain::ModuleBoardSnapshot snapshot;
    snapshot.source = brain::BoardSource::Arrival;

    if (!xPilotSessionSnapshot.connected || arrivalAirportIcao.empty()) {
        return snapshot;
    }

    snapshot.available = true;
    snapshot.airportIcao = arrivalAirportIcao;

    std::unordered_set<std::string> insertedKeys;

    for (const auto& controller : controllerFeedSnapshot.Controllers()) {
        if (!IsActionableTerminalAirspaceController(controller)) {
            continue;
        }

        std::string prefix;
        std::string suffix;
        if (!SplitControllerCallsign(controller.callsign, &prefix, &suffix)) {
            continue;
        }

        const auto role = ParseAirspaceRole(suffix);
        if (role == brain::StationRole::Other) {
            continue;
        }

        const auto sectorCoverageMatch =
            HasFreshAirportSectorCoverage(airportSectorSnapshot) &&
            SectorSetMatchesController(
                airportSectorSnapshot.coveringSectors,
                controller.callsign,
                role);

        if (!sectorCoverageMatch ||
            !HasRoleSpecificAirspaceData(airportSectorSnapshot, role)) {
            continue;
        }

        AppendStation(
            {
                role,
                controller.callsign,
                controller.frequency,
                {},
                IsFrequencyTuned(controller.frequency, radioStateSnapshot),
                false,
            },
            &snapshot.stations,
            &insertedKeys);
    }

    snapshot.available = !snapshot.stations.empty();
    return snapshot;
}

}  // namespace xvatsim::modules::arrival
