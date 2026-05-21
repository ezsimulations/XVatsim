#include "XVatsim/modules/arrival/ArrivalModule.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>

namespace xvatsim::modules::arrival {

namespace {

constexpr char kUnicomFallbackFrequency[] = "122.800";

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

bool IsGuardFrequency(const std::string& frequency) {
    const auto normalizedFrequency = NormalizeFrequency(frequency);
    return normalizedFrequency == "121500" || normalizedFrequency == "199998";
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

brain::ModuleBoardSnapshot ArrivalModule::Collect(
    const brain::XPilotSessionSnapshot& xPilotSessionSnapshot,
    const brain::ControllerFeedSnapshot& controllerFeedSnapshot,
    const brain::RadioStateSnapshot& radioStateSnapshot,
    const std::string& arrivalAirportIcao,
    const brain::AirportSectorSnapshot& airportSectorSnapshot,
    const brain::AuthorityRelevanceSnapshot* authorityRelevanceSnapshot,
    xvatsim::modules::ctaf_lookup::CtafLookupService* ctafLookupService) const {
    brain::ModuleBoardSnapshot snapshot;
    snapshot.source = brain::BoardSource::Arrival;

    if (!xPilotSessionSnapshot.connected || arrivalAirportIcao.empty()) {
        return snapshot;
    }

    snapshot.available = true;
    snapshot.airportIcao = arrivalAirportIcao;

    std::unordered_set<std::string> insertedKeys;
    const auto localSnapshot = localModule_.Collect(
        xPilotSessionSnapshot,
        controllerFeedSnapshot,
        radioStateSnapshot,
        arrivalAirportIcao,
        authorityRelevanceSnapshot);
    for (const auto& station : localSnapshot.stations) {
        AppendStation(station, &snapshot.stations, &insertedKeys);
    }

    const auto airspaceSnapshot = airspaceModule_.Collect(
        xPilotSessionSnapshot,
        controllerFeedSnapshot,
        radioStateSnapshot,
        arrivalAirportIcao,
        airportSectorSnapshot,
        authorityRelevanceSnapshot);
    for (const auto& station : airspaceSnapshot.stations) {
        AppendStation(station, &snapshot.stations, &insertedKeys);
    }

    if (ctafLookupService != nullptr) {
        const auto ctaf = ctafLookupService->Lookup(arrivalAirportIcao);
        brain::BoardStationSnapshot ctafStation;
        if (ctaf.available) {
            ctafStation.role = brain::StationRole::Ctaf;
            ctafStation.callsign = arrivalAirportIcao;
            ctafStation.frequency = ctaf.frequency;
            ctafStation.tuned = IsFrequencyTuned(ctaf.frequency, radioStateSnapshot);
        } else if (ctaf.resolved) {
            ctafStation.role = brain::StationRole::Unicom;
            ctafStation.frequency = kUnicomFallbackFrequency;
            ctafStation.tuned =
                IsFrequencyTuned(kUnicomFallbackFrequency, radioStateSnapshot);
        } else {
            ctafStation.role = brain::StationRole::Ctaf;
            ctafStation.callsign = arrivalAirportIcao;
            ctafStation.annotation = "lookup";
        }
        AppendStation(ctafStation, &snapshot.stations, &insertedKeys);
    }

    snapshot.available = !snapshot.stations.empty();
    return snapshot;
}

}  // namespace xvatsim::modules::arrival
