#include "XVatsim/modules/departure/DepartureModule.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

namespace xvatsim::modules::departure {

namespace {

constexpr char kUnicomFallbackFrequency[] = "122.800";
constexpr int kVatsimDeliveryFacility = 2;
constexpr int kVatsimGroundFacility = 3;
constexpr int kVatsimTowerFacility = 4;
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

std::string NormalizeIcao(std::string airportIcao) {
    std::string normalized;
    normalized.reserve(airportIcao.size());
    for (const auto character : airportIcao) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0) {
            continue;
        }

        normalized.push_back(static_cast<char>(std::toupper(
            static_cast<unsigned char>(character))));
    }
    return normalized;
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

std::vector<std::string> BuildAirportTokens(const std::string& airportIcao) {
    std::vector<std::string> tokens;
    const auto normalized = NormalizeIcao(airportIcao);
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

bool TokenMatchesControllerPrefix(
    const std::vector<std::string>& airportTokens,
    const std::string& controllerPrefix) {
    return std::any_of(
        airportTokens.begin(),
        airportTokens.end(),
        [&](const auto& airportToken) {
            return controllerPrefix == airportToken ||
                   (controllerPrefix.size() > airportToken.size() &&
                    controllerPrefix.compare(0, airportToken.size(), airportToken) == 0 &&
                    controllerPrefix[airportToken.size()] == '_');
        });
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

brain::StationRole ParseLocalRole(const std::string& suffix) {
    if (suffix == "DEL" || suffix == "CLR" || suffix == "CLNC" || suffix == "CD") {
        return brain::StationRole::Delivery;
    }
    if (suffix == "GND") {
        return brain::StationRole::Ground;
    }
    if (suffix == "TWR") {
        return brain::StationRole::Tower;
    }
    return brain::StationRole::Other;
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

bool IsActionableLocalController(
    const brain::ControllerSnapshot& controller,
    brain::StationRole role) {
    if (!controller.actionable || controller.atis) {
        return false;
    }

    switch (role) {
        case brain::StationRole::Delivery:
            return controller.facility == kVatsimDeliveryFacility;
        case brain::StationRole::Ground:
            return controller.facility == kVatsimGroundFacility;
        case brain::StationRole::Tower:
            return controller.facility == kVatsimTowerFacility;
        default:
            return false;
    }
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

}  // namespace

brain::ModuleBoardSnapshot DepartureModule::Collect(
    const brain::XPilotSessionSnapshot& xPilotSessionSnapshot,
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const brain::RadioStateSnapshot& radioStateSnapshot,
    const std::string& departureAirportIcao,
    const brain::AirportSectorSnapshot& airportSectorSnapshot,
    xvatsim::modules::ctaf_lookup::CtafLookupService* ctafLookupService) const {
    brain::ModuleBoardSnapshot snapshot;
    snapshot.source = brain::BoardSource::Departure;

    if (!xPilotSessionSnapshot.connected || departureAirportIcao.empty()) {
        return snapshot;
    }

    snapshot.airportIcao = departureAirportIcao;

    const auto airportTokens = BuildAirportTokens(departureAirportIcao);
    std::unordered_set<std::string> insertedKeys;

    for (const auto& controller : controllerFeedSnapshot.Controllers()) {
        std::string prefix;
        std::string suffix;
        if (!SplitControllerCallsign(controller.callsign, &prefix, &suffix)) {
            continue;
        }

        if (!TokenMatchesControllerPrefix(airportTokens, prefix)) {
            continue;
        }

        const auto role = ParseLocalRole(suffix);
        if (role == brain::StationRole::Other) {
            continue;
        }
        if (!IsActionableLocalController(controller, role)) {
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

        const auto airportTokenMatch =
            TokenMatchesControllerPrefix(airportTokens, prefix);
        const auto sectorCoverageMatch =
            HasFreshAirportSectorCoverage(airportSectorSnapshot) &&
            SectorSetMatchesController(
                airportSectorSnapshot.coveringSectors,
                controller.callsign,
                role);

        const auto hasRoleSpecificAirspaceData =
            HasRoleSpecificAirspaceData(airportSectorSnapshot, role);
        if (hasRoleSpecificAirspaceData) {
            if (!sectorCoverageMatch) {
                continue;
            }
        } else if (airportSectorSnapshot.hasTerminalCoverageData) {
            continue;
        } else if (!airportTokenMatch && !sectorCoverageMatch) {
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

    if (ctafLookupService != nullptr) {
        const auto ctaf = ctafLookupService->Lookup(departureAirportIcao);
        brain::BoardStationSnapshot ctafStation;
        if (ctaf.available) {
            ctafStation.role = brain::StationRole::Ctaf;
            ctafStation.callsign = departureAirportIcao;
            ctafStation.frequency = ctaf.frequency;
            ctafStation.tuned = IsFrequencyTuned(ctaf.frequency, radioStateSnapshot);
        } else if (ctaf.resolved) {
            ctafStation.role = brain::StationRole::Unicom;
            ctafStation.frequency = kUnicomFallbackFrequency;
            ctafStation.tuned =
                IsFrequencyTuned(kUnicomFallbackFrequency, radioStateSnapshot);
        } else {
            ctafStation.role = brain::StationRole::Ctaf;
            ctafStation.callsign = departureAirportIcao;
            ctafStation.annotation = "lookup";
        }
        AppendStation(ctafStation, &snapshot.stations, &insertedKeys);
    }

    snapshot.available = !snapshot.stations.empty();
    return snapshot;
}

}  // namespace xvatsim::modules::departure
