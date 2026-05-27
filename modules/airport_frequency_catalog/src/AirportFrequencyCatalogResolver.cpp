#include "XVatsim/modules/airport_frequency_catalog/AirportFrequencyCatalogResolver.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace xvatsim::modules::airport_frequency_catalog {
namespace {

std::string Trim(std::string value) {
    const auto notSpace = [](unsigned char ch) { return std::isspace(ch) == 0; };
    value.erase(
        value.begin(),
        std::find_if(value.begin(), value.end(), notSpace));
    value.erase(
        std::find_if(value.rbegin(), value.rend(), notSpace).base(),
        value.end());
    return value;
}

std::string ToUpper(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

std::string NormalizeAirportId(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const auto character : value) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0) {
            continue;
        }
        normalized.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(character))));
    }

    if (normalized.size() == 4 &&
        (normalized[0] == 'K' || normalized[0] == 'P' ||
         normalized[0] == 'C')) {
        return normalized.substr(1);
    }
    return normalized;
}

std::string NormalizeIcao(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const auto character : value) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0) {
            continue;
        }
        normalized.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(character))));
    }
    return normalized;
}

std::string BuildRequestKey(
    const brain::BrainAirportFrequencyWorkerInput& input) {
    const auto departure = NormalizeAirportId(input.departureIcao);
    const auto arrival = NormalizeAirportId(input.arrivalIcao);
    if (departure.empty() && arrival.empty()) {
        return {};
    }
    return departure + "|" + arrival;
}

std::vector<std::string> ParseCsvLine(const std::string& line) {
    std::vector<std::string> columns;
    std::string current;
    bool inQuotes = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const auto character = line[index];
        if (character == '"') {
            if (inQuotes && index + 1 < line.size() && line[index + 1] == '"') {
                current.push_back('"');
                ++index;
            } else {
                inQuotes = !inQuotes;
            }
            continue;
        }
        if (character == ',' && !inQuotes) {
            columns.push_back(Trim(current));
            current.clear();
            continue;
        }
        current.push_back(character);
    }
    columns.push_back(Trim(current));
    return columns;
}

std::unordered_map<std::string, std::size_t> BuildHeaderMap(
    const std::vector<std::string>& header) {
    std::unordered_map<std::string, std::size_t> columns;
    for (std::size_t index = 0; index < header.size(); ++index) {
        columns[ToUpper(Trim(header[index]))] = index;
    }
    return columns;
}

std::string ColumnValue(
    const std::vector<std::string>& row,
    const std::unordered_map<std::string, std::size_t>& header,
    const std::string& columnName) {
    const auto found = header.find(columnName);
    if (found == header.end() || found->second >= row.size()) {
        return {};
    }
    return row[found->second];
}

std::string NormalizeFrequency(std::string frequency) {
    frequency = Trim(std::move(frequency));
    if (frequency.empty()) {
        return {};
    }

    try {
        const auto parsed = std::stod(frequency);
        if (parsed < 118.0 || parsed > 136.975) {
            return {};
        }
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(3) << parsed;
        return stream.str();
    } catch (...) {
        return {};
    }
}

bool ContainsToken(const std::string& value, const std::string& token) {
    return value.find(token) != std::string::npos;
}

brain::StationRole ClassifyFrequencyUse(std::string use) {
    use = ToUpper(Trim(std::move(use)));
    if (use.empty() || ContainsToken(use, "EMERG") ||
        ContainsToken(use, "ANG OPS") || ContainsToken(use, "VOT")) {
        return brain::StationRole::Other;
    }
    if (ContainsToken(use, "D-ATIS") || ContainsToken(use, "ATIS") ||
        ContainsToken(use, "ASOS") || ContainsToken(use, "AWOS")) {
        return brain::StationRole::Atis;
    }
    if (ContainsToken(use, "UNICOM")) {
        return brain::StationRole::Unicom;
    }
    if (ContainsToken(use, "CTAF")) {
        return brain::StationRole::Ctaf;
    }
    if (ContainsToken(use, "GND")) {
        return brain::StationRole::Ground;
    }
    if (ContainsToken(use, "LCL")) {
        return brain::StationRole::Tower;
    }
    if (ContainsToken(use, "CD") || ContainsToken(use, "CLNC") ||
        ContainsToken(use, "CLEARANCE")) {
        return brain::StationRole::Delivery;
    }
    if (ContainsToken(use, "APCH") || ContainsToken(use, "DEP") ||
        ContainsToken(use, " DP") || ContainsToken(use, "STAR") ||
        ContainsToken(use, "CLASS B") || ContainsToken(use, "CLASS C")) {
        return brain::StationRole::Approach;
    }
    return brain::StationRole::Other;
}

bool SameRecord(
    const brain::BrainAirportFrequencyRecord& lhs,
    const brain::BrainAirportFrequencyRecord& rhs) {
    return lhs.airportIcao == rhs.airportIcao &&
           lhs.role == rhs.role &&
           lhs.frequency == rhs.frequency &&
           lhs.frequencyUse == rhs.frequencyUse &&
           lhs.sectorization == rhs.sectorization;
}

void AppendRecordUnique(
    const brain::BrainAirportFrequencyRecord& record,
    std::vector<brain::BrainAirportFrequencyRecord>* records) {
    if (records == nullptr || record.frequency.empty() ||
        record.role == brain::StationRole::Other) {
        return;
    }
    if (std::any_of(
            records->begin(),
            records->end(),
            [&](const auto& existing) { return SameRecord(existing, record); })) {
        return;
    }
    records->push_back(record);
}

void SortRecords(std::vector<brain::BrainAirportFrequencyRecord>* records) {
    if (records == nullptr) {
        return;
    }
    std::sort(
        records->begin(),
        records->end(),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.airportIcao != rhs.airportIcao) {
                return lhs.airportIcao < rhs.airportIcao;
            }
            if (lhs.role != rhs.role) {
                return static_cast<int>(lhs.role) < static_cast<int>(rhs.role);
            }
            if (lhs.frequency != rhs.frequency) {
                return lhs.frequency < rhs.frequency;
            }
            return lhs.frequencyUse < rhs.frequencyUse;
        });
}

bool HasRequiredColumns(
    const std::unordered_map<std::string, std::size_t>& header) {
    return header.find("SERVICED_FACILITY") != header.end() &&
           header.find("FREQ") != header.end() &&
           header.find("FREQ_USE") != header.end();
}

brain::BrainAirportFrequencyWorkerOutput ParseFrqCsv(
    const std::string& payload,
    const brain::BrainAirportFrequencyWorkerInput& input,
    std::uint64_t sourceGeneration) {
    brain::BrainAirportFrequencyWorkerOutput output;
    output.available = !payload.empty();
    output.resolved = false;
    output.stale = false;
    output.departureIcao = NormalizeIcao(input.departureIcao);
    output.arrivalIcao = NormalizeIcao(input.arrivalIcao);
    output.source = "FAA_NASR_FRQ_CSV";
    output.sourceGeneration = sourceGeneration;

    if (payload.empty()) {
        output.status = "airport-frequency-source-unavailable";
        output.cacheStatus = "airport-frequency-source-miss";
        return output;
    }

    const auto departureAirportId = NormalizeAirportId(input.departureIcao);
    const auto arrivalAirportId = NormalizeAirportId(input.arrivalIcao);
    if (departureAirportId.empty() && arrivalAirportId.empty()) {
        output.status = "airport-frequency-missing-airports";
        output.cacheStatus = "airport-frequency-idle";
        return output;
    }

    std::istringstream lines(payload);
    std::string line;
    std::unordered_map<std::string, std::size_t> header;
    bool headerLoaded = false;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (Trim(line).empty()) {
            continue;
        }

        const auto row = ParseCsvLine(line);
        if (!headerLoaded) {
            header = BuildHeaderMap(row);
            headerLoaded = true;
            if (!HasRequiredColumns(header)) {
                output.status = "airport-frequency-invalid-frq-csv";
                output.cacheStatus = "airport-frequency-parse-failed";
                return output;
            }
            continue;
        }

        const auto servicedFacility =
            NormalizeAirportId(ColumnValue(row, header, "SERVICED_FACILITY"));
        const auto endpoint =
            !departureAirportId.empty() && servicedFacility == departureAirportId
                ? brain::BrainAirportFrequencyEndpoint::Departure
                : (!arrivalAirportId.empty() && servicedFacility == arrivalAirportId
                       ? brain::BrainAirportFrequencyEndpoint::Arrival
                       : brain::BrainAirportFrequencyEndpoint::Unknown);
        if (endpoint == brain::BrainAirportFrequencyEndpoint::Unknown) {
            continue;
        }

        const auto frequency =
            NormalizeFrequency(ColumnValue(row, header, "FREQ"));
        const auto frequencyUse = ToUpper(ColumnValue(row, header, "FREQ_USE"));
        const auto role = ClassifyFrequencyUse(frequencyUse);
        if (frequency.empty() || role == brain::StationRole::Other) {
            continue;
        }

        brain::BrainAirportFrequencyRecord record;
        record.endpoint = endpoint;
        record.airportIcao = endpoint == brain::BrainAirportFrequencyEndpoint::Departure
                                 ? output.departureIcao
                                 : output.arrivalIcao;
        record.role = role;
        record.frequency = frequency;
        record.frequencyUse = frequencyUse;
        record.sectorization = ToUpper(ColumnValue(row, header, "SECTORIZATION"));
        record.facility = ToUpper(ColumnValue(row, header, "FACILITY"));
        record.servicedFacility = servicedFacility;
        record.towerOrCommCall =
            ToUpper(ColumnValue(row, header, "TOWER_OR_COMM_CALL"));
        record.primaryApproachRadioCall =
            ToUpper(ColumnValue(row, header, "PRIMARY_APPROACH_RADIO_CALL"));

        if (endpoint == brain::BrainAirportFrequencyEndpoint::Departure) {
            AppendRecordUnique(record, &output.departureFrequencies);
        } else {
            AppendRecordUnique(record, &output.arrivalFrequencies);
        }
    }

    SortRecords(&output.departureFrequencies);
    SortRecords(&output.arrivalFrequencies);
    output.available = true;
    output.resolved = true;
    output.status = "airport-frequency-resolved";
    output.cacheStatus = "airport-frequency-build";
    return output;
}

}  // namespace

brain::BrainAirportFrequencyWorkerOutput
AirportFrequencyCatalogResolver::ResolveAirportFrequencies(
    const brain::BrainAirportFrequencyWorkerInput& input) {
    const auto started = std::chrono::steady_clock::now();
    const auto requestKey = BuildRequestKey(input);

    std::lock_guard<std::mutex> lock(mutex_);
    if (requestKey.empty()) {
        brain::BrainAirportFrequencyWorkerOutput output;
        output.status = "airport-frequency-missing-airports";
        output.cacheStatus = "airport-frequency-idle";
        return output;
    }

    const auto cached = airportPairCache_.find(requestKey);
    if (cached != airportPairCache_.end()) {
        auto output = cached->second;
        output.cacheStatus = "airport-frequency-cache-hit";
        const auto elapsed = std::chrono::steady_clock::now() - started;
        output.lookupUs =
            std::chrono::duration_cast<std::chrono::microseconds>(elapsed)
                .count();
        return output;
    }

    auto output = ParseFrqCsv(frqCsvPayload_, input, sourceGeneration_);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    output.lookupUs =
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    airportPairCache_[requestKey] = output;
    return output;
}

void AirportFrequencyCatalogResolver::LoadFrqCsvPayloadForTesting(
    const std::string& frqCsvPayload,
    std::uint64_t sourceGeneration) {
    std::lock_guard<std::mutex> lock(mutex_);
    frqCsvPayload_ = frqCsvPayload;
    sourceGeneration_ = sourceGeneration;
    airportPairCache_.clear();
}

void AirportFrequencyCatalogResolver::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    frqCsvPayload_.clear();
    sourceGeneration_ = 0;
    airportPairCache_.clear();
}

}  // namespace xvatsim::modules::airport_frequency_catalog
