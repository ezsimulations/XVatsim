#include "ProductCalm1Probe.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "XVatsim/modules/flight_plan/FlightPlanSampler.h"

namespace xvatsim::tools::product_calm_1 {
namespace {

using modules::flight_plan::detail::FmsAirportEndpoint;
using modules::flight_plan::detail::FmsAirportEndpoints;
using modules::flight_plan::detail::FindFmsAirportEndpointsBidirectionally;
using modules::flight_plan::detail::kMaximumFmsEntries;

struct FixtureEntry {
    bool valid = false;
    std::string airportIcao;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
};

struct ReaderContext {
    const std::vector<FixtureEntry>* entries = nullptr;
    std::vector<int>* visits = nullptr;
};

bool ReadFixtureEntry(int index, FmsAirportEndpoint* endpoint, void* opaque) {
    auto* context = static_cast<ReaderContext*>(opaque);
    if (context == nullptr || context->entries == nullptr || endpoint == nullptr ||
        index < 0 || index >= static_cast<int>(context->entries->size())) {
        return false;
    }
    if (context->visits != nullptr) {
        context->visits->push_back(index);
    }
    const auto& entry = (*context->entries)[static_cast<std::size_t>(index)];
    if (!entry.valid) {
        return false;
    }
    endpoint->airportIcao = entry.airportIcao;
    endpoint->latitudeDeg = entry.latitudeDeg;
    endpoint->longitudeDeg = entry.longitudeDeg;
    return true;
}

void SetAirport(
    std::vector<FixtureEntry>* entries,
    int index,
    std::string airportIcao,
    double latitudeDeg,
    double longitudeDeg) {
    if (entries == nullptr || index < 0 ||
        index >= static_cast<int>(entries->size())) {
        return;
    }
    auto& entry = (*entries)[static_cast<std::size_t>(index)];
    entry.valid = true;
    entry.airportIcao = std::move(airportIcao);
    entry.latitudeDeg = latitudeDeg;
    entry.longitudeDeg = longitudeDeg;
}

bool HasUniqueVisits(const std::vector<int>& visits) {
    return std::set<int>(visits.begin(), visits.end()).size() == visits.size();
}

std::uint64_t Percentile99(std::vector<std::uint64_t> samples) {
    if (samples.empty()) {
        return 0;
    }
    std::sort(samples.begin(), samples.end());
    const auto index = static_cast<std::size_t>(
        (static_cast<double>(samples.size() - 1)) * 0.99);
    return samples[index];
}

FmsAirportEndpoints ReferenceForwardScan(
    const std::vector<FixtureEntry>& entries,
    int entryCount) {
    FmsAirportEndpoints endpoints;
    const auto boundedEntryCount =
        std::clamp(entryCount, 0, kMaximumFmsEntries);
    for (int index = 0; index < boundedEntryCount; ++index) {
        const auto& entry = entries[static_cast<std::size_t>(index)];
        if (!entry.valid) {
            continue;
        }
        FmsAirportEndpoint endpoint;
        endpoint.index = index;
        endpoint.airportIcao = entry.airportIcao;
        endpoint.latitudeDeg = entry.latitudeDeg;
        endpoint.longitudeDeg = entry.longitudeDeg;
        if (!endpoints.departure) {
            endpoints.departure = endpoint;
        }
        endpoints.destination = std::move(endpoint);
    }
    return endpoints;
}

bool SameEndpoint(
    const std::optional<FmsAirportEndpoint>& actual,
    const std::optional<FmsAirportEndpoint>& expected) {
    if (actual.has_value() != expected.has_value()) {
        return false;
    }
    if (!actual) {
        return true;
    }
    return actual->index == expected->index &&
           actual->airportIcao == expected->airportIcao &&
           actual->latitudeDeg == expected->latitudeDeg &&
           actual->longitudeDeg == expected->longitudeDeg;
}

}  // namespace

int RunProductCalm1Probe() {
    std::vector<std::string> failures;
    const auto require = [&](bool condition, const char* failure) {
        if (!condition) {
            failures.emplace_back(failure);
        }
    };

    std::vector<FixtureEntry> typicalEntries(kMaximumFmsEntries);
    SetAirport(&typicalEntries, 0, "MMGL", 20.5218, -103.3112);
    SetAirport(&typicalEntries, 511, "KLAX", 33.9425, -118.4081);
    std::vector<int> typicalVisits;
    ReaderContext typicalContext{&typicalEntries, &typicalVisits};
    const auto typical = FindFmsAirportEndpointsBidirectionally(
        static_cast<int>(typicalEntries.size()),
        &ReadFixtureEntry,
        &typicalContext);
    require(
        typical.departure && typical.departure->index == 0 &&
            typical.departure->airportIcao == "MMGL",
        "typical departure endpoint was not exact");
    require(
        typical.destination && typical.destination->index == 511 &&
            typical.destination->airportIcao == "KLAX" &&
            typical.destination->latitudeDeg == 33.9425 &&
            typical.destination->longitudeDeg == -118.4081,
        "typical destination endpoint was not exact");
    require(
        typical.entriesRead == 2 && typicalVisits == std::vector<int>({0, 511}),
        "typical 512-entry lookup did not stop after two reads");

    std::vector<FixtureEntry> interiorEntries(kMaximumFmsEntries);
    SetAirport(&interiorEntries, 3, "KSEA", 47.4489, -122.3094);
    SetAirport(&interiorEntries, 500, "KORD", 41.9742, -87.9073);
    std::vector<int> interiorVisits;
    ReaderContext interiorContext{&interiorEntries, &interiorVisits};
    const auto interior = FindFmsAirportEndpointsBidirectionally(
        static_cast<int>(interiorEntries.size()),
        &ReadFixtureEntry,
        &interiorContext);
    require(
        interior.departure && interior.departure->index == 3 &&
            interior.destination && interior.destination->index == 500,
        "interior endpoint selection did not preserve first/last semantics");
    require(
        interior.entriesRead == static_cast<int>(interiorVisits.size()) &&
            HasUniqueVisits(interiorVisits),
        "interior lookup read an entry more than once");

    std::vector<FixtureEntry> singleEntries(kMaximumFmsEntries);
    SetAirport(&singleEntries, 255, "KDEN", 39.8561, -104.6737);
    std::vector<int> singleVisits;
    ReaderContext singleContext{&singleEntries, &singleVisits};
    const auto single = FindFmsAirportEndpointsBidirectionally(
        static_cast<int>(singleEntries.size()),
        &ReadFixtureEntry,
        &singleContext);
    require(
        single.departure && single.destination &&
            single.departure->index == 255 &&
            single.destination->index == 255 &&
            single.departure->airportIcao == "KDEN" &&
            single.destination->airportIcao == "KDEN",
        "single-airport route did not populate both historical endpoints");
    require(
        single.entriesRead == kMaximumFmsEntries && HasUniqueVisits(singleVisits),
        "single-airport exhaustive lookup was not bounded and unique");

    std::vector<FixtureEntry> emptyEntries(kMaximumFmsEntries);
    std::vector<int> emptyVisits;
    ReaderContext emptyContext{&emptyEntries, &emptyVisits};
    const auto empty = FindFmsAirportEndpointsBidirectionally(
        static_cast<int>(emptyEntries.size()),
        &ReadFixtureEntry,
        &emptyContext);
    require(
        !empty.departure && !empty.destination &&
            empty.entriesRead == kMaximumFmsEntries &&
            HasUniqueVisits(emptyVisits),
        "empty FMS lookup was not an exact bounded scan");

    std::vector<FixtureEntry> oversizedEntries(600);
    SetAirport(&oversizedEntries, 0, "KBOS", 42.3656, -71.0096);
    SetAirport(&oversizedEntries, 511, "KORD", 41.9742, -87.9073);
    SetAirport(&oversizedEntries, 599, "KLAX", 33.9425, -118.4081);
    std::vector<int> oversizedVisits;
    ReaderContext oversizedContext{&oversizedEntries, &oversizedVisits};
    const auto oversized = FindFmsAirportEndpointsBidirectionally(
        static_cast<int>(oversizedEntries.size()),
        &ReadFixtureEntry,
        &oversizedContext);
    require(
        oversized.departure && oversized.departure->index == 0 &&
            oversized.destination && oversized.destination->index == 511,
        "defensive 512-entry bound changed endpoint selection");
    require(
        oversized.entriesRead == 2 &&
            std::all_of(
                oversizedVisits.begin(),
                oversizedVisits.end(),
                [](int index) { return index < kMaximumFmsEntries; }),
        "defensive bound allowed a read beyond entry 511");

    std::vector<FixtureEntry> lateEntries(kMaximumFmsEntries);
    SetAirport(&lateEntries, 0, "MMGL", 20.5218, -103.3112);
    ReaderContext lateContext{&lateEntries, nullptr};
    const auto beforeEnrichment = FindFmsAirportEndpointsBidirectionally(
        static_cast<int>(lateEntries.size()),
        &ReadFixtureEntry,
        &lateContext);
    SetAirport(&lateEntries, 511, "KLAX", 33.9425, -118.4081);
    const auto afterEnrichment = FindFmsAirportEndpointsBidirectionally(
        static_cast<int>(lateEntries.size()),
        &ReadFixtureEntry,
        &lateContext);
    require(
        beforeEnrichment.departure && beforeEnrichment.destination &&
            beforeEnrichment.departure->airportIcao == "MMGL" &&
            beforeEnrichment.destination->airportIcao == "MMGL",
        "pre-enrichment single-airport behavior changed");
    require(
        afterEnrichment.departure && afterEnrichment.destination &&
            afterEnrichment.departure->airportIcao == "MMGL" &&
            afterEnrichment.destination->airportIcao == "KLAX" &&
            afterEnrichment.entriesRead == 2,
        "late FMS enrichment was not observed by the next lookup");

    const auto nullReader = FindFmsAirportEndpointsBidirectionally(
        kMaximumFmsEntries,
        nullptr,
        nullptr);
    require(
        !nullReader.departure && !nullReader.destination &&
            nullReader.entriesRead == 0,
        "null reader did not fail closed without work");

    std::uint64_t equivalenceCases = 0;
    bool equivalencePassed = true;
    for (int entryCount = 0; entryCount <= 12; ++entryCount) {
        const auto patternCount = 1U << entryCount;
        for (unsigned int pattern = 0; pattern < patternCount; ++pattern) {
            std::vector<FixtureEntry> entries(
                static_cast<std::size_t>(entryCount));
            for (int index = 0; index < entryCount; ++index) {
                if ((pattern & (1U << index)) == 0) {
                    continue;
                }
                SetAirport(
                    &entries,
                    index,
                    "APT" + std::to_string(index),
                    static_cast<double>(index),
                    -static_cast<double>(index));
            }
            std::vector<int> visits;
            ReaderContext context{&entries, &visits};
            const auto actual = FindFmsAirportEndpointsBidirectionally(
                entryCount,
                &ReadFixtureEntry,
                &context);
            const auto expected = ReferenceForwardScan(entries, entryCount);
            equivalencePassed = equivalencePassed &&
                SameEndpoint(actual.departure, expected.departure) &&
                SameEndpoint(actual.destination, expected.destination) &&
                actual.entriesRead <= entryCount &&
                actual.entriesRead == static_cast<int>(visits.size()) &&
                HasUniqueVisits(visits);
            ++equivalenceCases;
        }
    }
    require(
        equivalencePassed,
        "bidirectional lookup diverged from reference forward-scan semantics");

    constexpr int kBenchmarkIterations = 50'000;
    ReaderContext benchmarkContext{&typicalEntries, nullptr};
    std::vector<std::uint64_t> elapsedNanoseconds;
    elapsedNanoseconds.reserve(kBenchmarkIterations);
    std::uint64_t benchmarkReads = 0;
    for (int iteration = 0; iteration < kBenchmarkIterations; ++iteration) {
        const auto started = std::chrono::steady_clock::now();
        const auto endpoints = FindFmsAirportEndpointsBidirectionally(
            static_cast<int>(typicalEntries.size()),
            &ReadFixtureEntry,
            &benchmarkContext);
        elapsedNanoseconds.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started)
                .count()));
        benchmarkReads += static_cast<std::uint64_t>(endpoints.entriesRead);
    }
    const auto benchmarkP99Ns = Percentile99(elapsedNanoseconds);
    const auto benchmarkMaximumNs =
        *std::max_element(elapsedNanoseconds.begin(), elapsedNanoseconds.end());
    require(
        benchmarkReads == static_cast<std::uint64_t>(kBenchmarkIterations) * 2,
        "typical benchmark performed more than two reads per lookup");
    require(
        benchmarkP99Ns < 250'000,
        "typical endpoint lookup P99 exceeded 250 us");

    std::cout << "PRODUCT_CALM_1_PROBE"
              << " typicalEntries=" << kMaximumFmsEntries
              << " typicalReads=" << typical.entriesRead
              << " emptyReads=" << empty.entriesRead
              << " singleReads=" << single.entriesRead
              << " lateEnrichmentReads=" << afterEnrichment.entriesRead
              << " equivalenceCases=" << equivalenceCases
              << " benchmarkIterations=" << kBenchmarkIterations
              << " benchmarkP99Ns=" << benchmarkP99Ns
              << " benchmarkMaxNs=" << benchmarkMaximumNs << "\n";

    if (!failures.empty()) {
        for (const auto& failure : failures) {
            std::cerr << "PRODUCT_CALM_1_FAILURE: " << failure << "\n";
        }
        return 1;
    }
    std::cout << "PRODUCT_CALM_1_PASSED\n";
    return 0;
}

}  // namespace xvatsim::tools::product_calm_1
