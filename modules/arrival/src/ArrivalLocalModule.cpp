#include "XVatsim/modules/arrival/ArrivalLocalModule.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

namespace xvatsim::modules::arrival {

namespace {

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

brain::StationRole ParseLocalRole(const std::string& suffix) {
    if (suffix == "GND") {
        return brain::StationRole::Ground;
    }
    if (suffix == "TWR") {
        return brain::StationRole::Tower;
    }
    return brain::StationRole::Other;
}

void AppendStation(
    const brain::BoardStationSnapshot& station,
    std::vector<brain::BoardStationSnapshot>* stations,
    std::unordered_set<std::string>* keys);

std::string NormalizeAuthorityKey(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const auto character : value) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0 ||
            character == '_' || character == '-') {
            normalized.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(character))));
        }
    }
    return normalized;
}

bool AuthorityValueMatchesAny(
    const std::string& rawAuthorityValue,
    const std::vector<std::string>& rawValues) {
    const auto authorityValue = NormalizeAuthorityKey(rawAuthorityValue);
    if (authorityValue.empty()) {
        return false;
    }

    return std::any_of(
        rawValues.begin(),
        rawValues.end(),
        [&](const auto& rawValue) {
            return NormalizeAuthorityKey(rawValue) == authorityValue;
        });
}

bool CanUseCentralTerminalAuthority(
    const brain::AuthorityRelevanceSnapshot* authorityRelevanceSnapshot) {
    return authorityRelevanceSnapshot != nullptr &&
           authorityRelevanceSnapshot->available &&
           !authorityRelevanceSnapshot->stale;
}

bool LocalAuthorityMatchesAirport(
    const brain::RelevantAuthoritySnapshot& authority,
    const std::vector<std::string>& airportTokens,
    brain::StationRole role) {
    if (authority.kind != brain::AuthorityRelevanceKind::Terminal ||
        authority.proofSource != "AIRPORT_LOCAL_FACILITY") {
        return false;
    }

    if (!AuthorityValueMatchesAny(authority.polygonKey, airportTokens)) {
        return false;
    }

    std::string prefix;
    std::string suffix;
    if (!SplitControllerCallsign(authority.callsign, &prefix, &suffix)) {
        return false;
    }
    if (!TokenMatchesControllerPrefix(airportTokens, prefix)) {
        return false;
    }

    return ParseLocalRole(suffix) == role;
}

void CollectAuthorityLocalControllers(
    const brain::AuthorityRelevanceSnapshot& authorityRelevanceSnapshot,
    const brain::RadioStateSnapshot& radioStateSnapshot,
    const std::vector<std::string>& airportTokens,
    std::vector<brain::BoardStationSnapshot>* stations,
    std::unordered_set<std::string>* insertedKeys) {
    for (const auto& authority : authorityRelevanceSnapshot.relevantAuthorities) {
        for (const auto role : {
                 brain::StationRole::Ground,
                 brain::StationRole::Tower,
             }) {
            if (!LocalAuthorityMatchesAirport(authority, airportTokens, role)) {
                continue;
            }

            AppendStation(
                {
                    role,
                    authority.callsign,
                    authority.frequency,
                    {},
                    IsFrequencyTuned(authority.frequency, radioStateSnapshot),
                    false,
                },
                stations,
                insertedKeys);
            break;
        }
    }
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

brain::ModuleBoardSnapshot ArrivalLocalModule::Collect(
    const brain::XPilotSessionSnapshot& xPilotSessionSnapshot,
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const brain::RadioStateSnapshot& radioStateSnapshot,
    const std::string& arrivalAirportIcao,
    const brain::AuthorityRelevanceSnapshot* authorityRelevanceSnapshot) const {
    (void)controllerFeedSnapshot;

    brain::ModuleBoardSnapshot snapshot;
    snapshot.source = brain::BoardSource::Arrival;

    if (!xPilotSessionSnapshot.connected || arrivalAirportIcao.empty()) {
        return snapshot;
    }

    snapshot.available = true;
    snapshot.airportIcao = arrivalAirportIcao;

    const auto airportTokens = BuildAirportTokens(arrivalAirportIcao);
    std::unordered_set<std::string> insertedKeys;

    if (CanUseCentralTerminalAuthority(authorityRelevanceSnapshot)) {
        CollectAuthorityLocalControllers(
            *authorityRelevanceSnapshot,
            radioStateSnapshot,
            airportTokens,
            &snapshot.stations,
            &insertedKeys);
    }

    snapshot.available = !snapshot.stations.empty();
    return snapshot;
}

}  // namespace xvatsim::modules::arrival
