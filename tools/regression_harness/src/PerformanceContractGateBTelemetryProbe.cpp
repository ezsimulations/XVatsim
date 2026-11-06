#include "PerformanceContractGateBTelemetryProbe.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "XVatsim/modules/runtime_workers/AsyncDiagnosticsWriter.h"
#include "XVatsim/brain/BrainOwnedRuntime.h"
#include "XVatsim/modules/transceiver_resolver/RadioBoardFactWorker.h"
#include "XVatsim/modules/transceiver_resolver/TransceiverResolver.h"

namespace xvatsim::tools::performance_contract_gate_b_telemetry {
namespace {

using modules::runtime_workers::AsyncDiagnosticsWriter;
using modules::runtime_workers::DiagnosticsWriterOptions;
using modules::runtime_workers::RouteEvidenceDisposition;
using modules::runtime_workers::RouteEvidenceRecord;
using modules::runtime_workers::RouteEvidenceRecordType;
using modules::runtime_workers::RouteEvidenceWorkKind;

std::filesystem::path UniqueProbeRoot() {
    const auto token = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() /
           ("xvatsim_gate_b_telemetry_" + std::to_string(token));
}

std::uint64_t Percentile99(std::vector<std::uint64_t> samples) {
    if (samples.empty()) {
        return 0;
    }
    std::sort(samples.begin(), samples.end());
    const auto index = std::min<std::size_t>(
        samples.size() - 1,
        (samples.size() * 99 + 99) / 100 - 1);
    return samples[index];
}

std::string ReadAllLogs(const std::filesystem::path& root) {
    std::ostringstream content;
    std::error_code error;
    if (!std::filesystem::exists(root, error) || error) {
        return {};
    }
    for (std::filesystem::directory_iterator iterator(root, error), end;
         !error && iterator != end;
         iterator.increment(error)) {
        if (!iterator->is_regular_file(error) || error) {
            error.clear();
            continue;
        }
        std::ifstream input(iterator->path());
        content << input.rdbuf();
    }
    return content.str();
}

std::string FieldValue(const std::string& line, std::string_view key) {
    const auto marker = std::string{key} + "=";
    const auto start = line.find(marker);
    if (start == std::string::npos) {
        return {};
    }
    const auto valueStart = start + marker.size();
    const auto end = line.find(' ', valueStart);
    return line.substr(valueStart, end - valueStart);
}

std::string DecodeBase64(std::string_view encoded) {
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string decoded;
    decoded.reserve((encoded.size() / 4) * 3);
    std::uint32_t accumulator = 0;
    int bits = 0;
    for (const auto character : encoded) {
        if (character == '=') {
            break;
        }
        const auto value = alphabet.find(character);
        if (value == std::string_view::npos) {
            return {};
        }
        accumulator = (accumulator << 6U) |
            static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            decoded.push_back(static_cast<char>(
                (accumulator >> static_cast<unsigned int>(bits)) & 0xffU));
        }
    }
    return decoded;
}

std::uint64_t LogicalRecordDigest(std::string_view value) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto character : value) {
        hash ^= static_cast<std::uint64_t>(
            static_cast<unsigned char>(character));
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string HexDigest(std::uint64_t value) {
    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << value;
    return stream.str();
}

std::string MakeIntegrityRecord(std::size_t bytes) {
    std::string record =
        "event=diagnostic-integrity-" + std::to_string(bytes) +
        " quoted=\"yes\" slash=\\ utf8=\xCF\x80\xF0\x9F\x9A\x80"
        " embedded=event=faux newline=\nnext=";
    if (record.size() < bytes) {
        record.resize(bytes, 'x');
    } else if (record.size() > bytes) {
        record.resize(bytes);
    }
    return record;
}

std::vector<std::uint64_t> RouteEvidenceSequences(
    const std::string& content) {
    std::vector<std::uint64_t> sequences;
    std::istringstream lines(content);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.find("event=route-evidence ") == std::string::npos) {
            continue;
        }
        const auto marker = line.find(" sequence=");
        if (marker == std::string::npos) {
            continue;
        }
        const auto start = marker + std::string{" sequence="}.size();
        const auto end = line.find(' ', start);
        sequences.push_back(static_cast<std::uint64_t>(
            std::stoull(line.substr(start, end - start))));
    }
    return sequences;
}

template <std::size_t Size>
void CopyToken(std::array<char, Size>* destination, std::string_view value) {
    destination->fill('\0');
    const auto count = std::min(value.size(), Size - 1);
    std::copy_n(value.data(), count, destination->data());
}

RouteEvidenceRecord MakeEvidence(
    RouteEvidenceRecordType type,
    RouteEvidenceWorkKind kind,
    std::uint64_t requestId,
    std::uint64_t lifecycleEpoch,
    std::string_view reason) {
    RouteEvidenceRecord evidence;
    evidence.recordType = type;
    evidence.workKind = kind;
    evidence.requestId = requestId;
    evidence.lifecycleEpoch = lifecycleEpoch;
    evidence.networkDigest = 101;
    evidence.sourceIdentity = 102;
    evidence.preflightIdentity = 103;
    evidence.policyIdentity = 104;
    evidence.anchorDigest = 105;
    evidence.fmsObservationIdentity = 106;
    evidence.resultDigest = 107;
    evidence.completeDesiredPackageSubmitUs = 12;
    evidence.mailboxExchangeUs = 2;
    evidence.maximumPendingDepth = 1;
    evidence.workerWallUs = 321;
    evidence.workerCpuUs = 123;
    evidence.workerCpuAvailable = true;
    evidence.completed = type == RouteEvidenceRecordType::Terminal;
    CopyToken(&evidence.planKey, "UAL472|KAAA|KBBB");
    CopyToken(&evidence.reason, reason);
    return evidence;
}

bool WaitForDequeued(
    const AsyncDiagnosticsWriter& writer,
    std::uint64_t minimum,
    std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (writer.Snapshot().dequeued >= minimum) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return writer.Snapshot().dequeued >= minimum;
}

}  // namespace

int RunPerformanceContractGateBTelemetryProbe() {
    std::vector<std::string> failures;
    const auto require = [&](bool condition, std::string failure) {
        if (!condition) {
            failures.push_back(std::move(failure));
        }
    };

    const auto probeRoot = UniqueProbeRoot();
    std::error_code error;
    std::filesystem::create_directories(probeRoot, error);
    require(!error, "failed to create telemetry probe root");

    std::uint64_t producerP99Us = 0;
    std::uint64_t producerMaxUs = 0;
    std::uint64_t writtenEvidence = 0;
    std::uint64_t maximumEvidenceDepth = 0;
    {
        AsyncDiagnosticsWriter writer;
        DiagnosticsWriterOptions options;
        options.logDirectory = probeRoot / "physical-line-integrity";
        options.routineQueueCapacity = 16;
        options.criticalQueueCapacity = 4;
        options.flightLoopTimingQueueCapacity = 4;
        options.routeEvidenceQueueCapacity = 4;
        require(writer.Start(options), "physical-line writer failed to start");

        const std::array<std::size_t, 5> recordSizes{
            4095, 4096, 4097, 8347, 65536};
        std::vector<std::string> expectedRecords;
        for (const auto size : recordSizes) {
            expectedRecords.push_back(MakeIntegrityRecord(size));
            require(
                writer.TryEnqueueLine(expectedRecords.back()),
                "physical-line test record was not accepted");
        }
        require(
            writer.WaitUntilIdle(std::chrono::seconds(10)),
            "physical-line test records did not drain");
        const auto snapshot = writer.Snapshot();
        require(
            snapshot.written == expectedRecords.size() &&
                snapshot.splitLogicalRecords == expectedRecords.size() &&
                snapshot.maximumLogicalRecordBytes == recordSizes.back() &&
                snapshot.maximumPhysicalLineBytes <=
                    options.maximumPhysicalLineBytes &&
                snapshot.formattingFailures == 0,
            "writer physical-line accounting was not exact");
        writer.Stop();

        const auto logs = ReadAllLogs(options.logDirectory);
        struct Reassembly {
            std::size_t originalBytes = 0;
            std::size_t partCount = 0;
            std::string digest;
            std::map<std::size_t, std::string> parts;
        };
        std::map<std::uint64_t, Reassembly> reassembly;
        std::istringstream lines(logs);
        std::string line;
        std::size_t physicalLines = 0;
        while (std::getline(lines, line)) {
            ++physicalLines;
            require(
                line.size() + 1 <= options.maximumPhysicalLineBytes,
                "writer emitted a physical line beyond 4096 bytes");
            if (line.find("event=diagnostic-record-oversize ") !=
                std::string::npos) {
                const auto sequence = FieldValue(line, "recordSequence");
                const auto bytes = FieldValue(line, "originalBytes");
                const auto count = FieldValue(line, "partCount");
                require(
                    !sequence.empty() && !bytes.empty() && !count.empty(),
                    "oversize header omitted reassembly identity");
                if (!sequence.empty() && !bytes.empty() && !count.empty()) {
                    auto& group = reassembly[std::stoull(sequence)];
                    group.originalBytes = std::stoull(bytes);
                    group.partCount = std::stoull(count);
                    group.digest = FieldValue(line, "digest");
                }
            } else if (line.find("event=diagnostic-record-part ") !=
                       std::string::npos) {
                const auto sequence = FieldValue(line, "recordSequence");
                const auto part = FieldValue(line, "part");
                const auto payload = FieldValue(line, "payload");
                require(
                    !sequence.empty() && !part.empty() && !payload.empty(),
                    "oversize part omitted reassembly data");
                if (!sequence.empty() && !part.empty() && !payload.empty()) {
                    reassembly[std::stoull(sequence)].parts[std::stoull(part)] =
                        DecodeBase64(payload);
                }
            }
        }
        require(
            reassembly.size() == expectedRecords.size(),
            "oversize record header count differed");
        require(
            physicalLines == snapshot.writtenPhysicalLines,
            "physical-line counter differed from persisted lines");
        for (std::size_t index = 0; index < expectedRecords.size(); ++index) {
            const auto sequence = static_cast<std::uint64_t>(index + 1);
            const auto found = reassembly.find(sequence);
            require(found != reassembly.end(), "oversize sequence was missing");
            if (found == reassembly.end()) {
                continue;
            }
            std::string reconstructed;
            for (std::size_t part = 1; part <= found->second.partCount; ++part) {
                const auto payload = found->second.parts.find(part);
                require(
                    payload != found->second.parts.end(),
                    "oversize payload part was missing");
                if (payload != found->second.parts.end()) {
                    reconstructed += payload->second;
                }
            }
            require(
                reconstructed == expectedRecords[index] &&
                    found->second.originalBytes == expectedRecords[index].size() &&
                    found->second.digest ==
                        HexDigest(LogicalRecordDigest(expectedRecords[index])),
                "oversize record did not reconstruct byte-for-byte");
        }
    }

    {
        brain::BrainOwnedRadioBoardWorkerCompletionInput input;
        input.completed = true;
        input.resultComplete = true;
        input.currentLifecycleEpoch = 7;
        input.completedLifecycleEpoch = 7;
        input.currentControllerGeneration = 11;
        input.completedControllerGeneration = 11;
        input.currentControllerContentDigest = 101;
        input.completedControllerContentDigest = 101;
        input.currentSourceGeneration = 3;
        input.completedSourceGeneration = 3;
        input.currentSourceContentDigest = 303;
        input.completedSourceContentDigest = 303;
        input.currentMonotonicMs = 10'500;
        input.completedMonotonicMs = 10'000;
        input.currentAircraft.valid = true;
        input.currentAircraft.latitudeDeg = 33.0;
        input.currentAircraft.longitudeDeg = -117.0;
        input.currentAircraft.altitudeAglFt = 20'000.0;
        input.completedAircraft = input.currentAircraft;
        require(
            brain::DecideBrainOwnedRadioBoardWorkerCompletion(input).accepted,
            "exact radio-board worker completion was not accepted");

        const auto rejectedAs = [&](auto mutate, std::string_view reason) {
            auto changed = input;
            mutate(&changed);
            const auto decision =
                brain::DecideBrainOwnedRadioBoardWorkerCompletion(changed);
            require(
                !decision.accepted && decision.reason == reason,
                "radio-board worker stale identity was not rejected as " +
                    std::string{reason});
        };
        rejectedAs(
            [](auto* value) { ++value->completedLifecycleEpoch; },
            "lifecycle-epoch-mismatch");
        rejectedAs(
            [](auto* value) { ++value->completedControllerGeneration; },
            "controller-generation-mismatch");
        rejectedAs(
            [](auto* value) { ++value->completedControllerContentDigest; },
            "controller-digest-mismatch");
        rejectedAs(
            [](auto* value) { ++value->completedSourceGeneration; },
            "transceiver-source-generation-mismatch");
        rejectedAs(
            [](auto* value) { ++value->completedSourceContentDigest; },
            "transceiver-source-digest-mismatch");
        rejectedAs(
            [](auto* value) { value->completedMonotonicMs = 9'000; },
            "completion-age-exceeded");
        rejectedAs(
            [](auto* value) {
                value->completedAircraft.latitudeDeg += 0.01;
            },
            "aircraft-displacement-exceeded");
        rejectedAs(
            [](auto* value) {
                value->completedAircraft.altitudeAglFt -= 501.0;
            },
            "aircraft-altitude-delta-exceeded");
    }

    {
        using modules::transceiver_resolver::CachedTransceiver;
        std::vector<brain::ControllerSnapshot> controllers;
        controllers.reserve(1'000);
        std::vector<CachedTransceiver> transceivers;
        transceivers.reserve(50'000);
        for (std::size_t controllerIndex = 0;
             controllerIndex < 1'000;
             ++controllerIndex) {
            std::ostringstream frequency;
            frequency << 118 + (controllerIndex % 18) << "."
                      << std::setw(3) << std::setfill('0')
                      << ((controllerIndex * 5) % 1'000);
            brain::ControllerSnapshot controller;
            controller.callsign =
                "STRESS_" + std::to_string(controllerIndex) + "_CTR";
            controller.frequency = frequency.str();
            controller.facility = 6;
            controller.visualRangeNm = 300;
            controllers.push_back(controller);
            for (std::size_t station = 0; station < 50; ++station) {
                CachedTransceiver transceiver;
                transceiver.callsign = controller.callsign;
                transceiver.frequency = controller.frequency;
                transceiver.latitudeDeg =
                    33.0 + static_cast<double>(station % 10) * 0.001;
                transceiver.longitudeDeg =
                    -117.0 + static_cast<double>(station / 10) * 0.001;
                transceiver.heightAglFt = 100.0;
                transceivers.push_back(std::move(transceiver));
            }
        }
        auto ownedControllers =
            std::make_shared<const std::vector<brain::ControllerSnapshot>>(
                std::move(controllers));
        brain::ControllerFeedSnapshot controllerFeed;
        controllerFeed.available = true;
        controllerFeed.stale = false;
        controllerFeed.controllerComponentPresent = true;
        controllerFeed.controllerComponentComplete = true;
        controllerFeed.connectedControllers = 1'000;
        controllerFeed.retainedControllerRecords = 1'000;
        controllerFeed.ownedControllers = ownedControllers;
        controllerFeed.controllers = ownedControllers.get();
        brain::AircraftStateSnapshot aircraft;
        aircraft.valid = true;
        aircraft.latitudeDeg = 33.0;
        aircraft.longitudeDeg = -117.0;
        aircraft.altitudeAglFt = 20'000.0;

        brain::TransceiverResolutionSnapshot resolved;
        modules::transceiver_resolver::TransceiverSourceIdentity sourceIdentity;
        std::uint64_t sourceThreadIdentity = 0;
        const auto callerIdentity = static_cast<std::uint64_t>(
            std::hash<std::thread::id>{}(std::this_thread::get_id()));
        std::thread sourceWorker([
            &resolved,
            &sourceIdentity,
            &sourceThreadIdentity,
            aircraft,
            controllerFeed,
            transceivers = std::move(transceivers)]() mutable {
            sourceThreadIdentity = static_cast<std::uint64_t>(
                std::hash<std::thread::id>{}(std::this_thread::get_id()));
            modules::transceiver_resolver::TransceiverResolver resolver;
            resolver.SeedFeedCacheForTesting(
                std::move(transceivers), 0, true);
            resolved = resolver.Resolve(aircraft, controllerFeed);
            sourceIdentity = resolver.SourceIdentity();
        });
        sourceWorker.join();
        require(
            sourceThreadIdentity != 0 && sourceThreadIdentity != callerIdentity &&
                sourceIdentity.generation == 1 &&
                sourceIdentity.contentDigest != 0 &&
                resolved.sourceEvidence.cachedTransceiverCount == 50'000 &&
                resolved.controllerEvidence.size() == 1'000,
            "50k/1k transceiver source preparation did not execute completely "
            "off the simulator thread");
    }

    {
        using modules::transceiver_resolver::RadioBoardFact;
        using modules::transceiver_resolver::RadioBoardFactRequest;
        using modules::transceiver_resolver::RadioBoardFactWorker;
        RadioBoardFactWorker worker;
        require(worker.Start(), "radio-board stress worker failed to start");
        const auto callerIdentity = static_cast<std::uint64_t>(
            std::hash<std::thread::id>{}(std::this_thread::get_id()));
        for (std::uint64_t requestId = 1; requestId <= 10'000; ++requestId) {
            RadioBoardFactRequest request;
            request.identity.requestId = requestId;
            request.identity.lifecycleEpoch = 9;
            request.identity.controllerGeneration = requestId;
            request.identity.controllerContentDigest = requestId * 17;
            request.identity.aircraftValid = true;
            request.identity.aircraftLatitudeDeg =
                33.0 + static_cast<double>(requestId % 100) * 0.00001;
            request.identity.aircraftLongitudeDeg = -117.0;
            request.identity.aircraftAltitudeAglFt = 20'000.0;
            request.input.aircraft.valid = true;
            request.input.aircraft.latitudeDeg =
                request.identity.aircraftLatitudeDeg;
            request.input.aircraft.longitudeDeg = -117.0;
            request.input.aircraft.altitudeAglFt = 20'000.0;
            require(
                worker.StartLatest(std::move(request)),
                "radio-board stress request was not accepted");
        }

        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline &&
               worker.HasWork()) {
            RadioBoardFact fact;
            if (!worker.TryHarvest(&fact)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        require(!worker.HasWork(), "radio-board stress worker did not drain");
        worker.Stop();
        const auto stopped = worker.Snapshot();
        require(
            !stopped.started && !stopped.running && !stopped.pending &&
                !stopped.ready && stopped.submissions == 10'000 &&
                stopped.replacements > 0 &&
                stopped.maximumPendingDepth == 1 &&
                stopped.completions + stopped.failures +
                        stopped.replacements ==
                    stopped.submissions &&
                stopped.harvests == stopped.completions + stopped.failures &&
                stopped.workerThreadIdentity != 0 &&
                stopped.workerThreadIdentity != callerIdentity,
            "radio-board one-running/one-pending stress accounting failed");
    }

    {
        AsyncDiagnosticsWriter writer;
        DiagnosticsWriterOptions options;
        options.logDirectory = probeRoot / "contention-delay-lifecycle";
        options.routineQueueCapacity = 32;
        options.criticalQueueCapacity = 8;
        options.flightLoopTimingQueueCapacity = 32;
        // The delayed writer cannot consume this lane while the complete
        // 99-record lifecycle fixture is submitted, so the last accepted
        // record fills the fixed-capacity ring exactly without loss.
        options.routeEvidenceQueueCapacity = 99;
        options.artificialWorkerDelayForTesting = std::chrono::seconds(2);
        options.artificialWorkerDelayRecordCountForTesting = 1;
        require(writer.Start(options), "telemetry writer failed to start");
        require(
            writer.TryEnqueueLine("event=route-evidence-delay-anchor"),
            "delay anchor was not accepted");
        require(
            WaitForDequeued(writer, 1, std::chrono::seconds(2)),
            "writer did not enter its forced delay");

        require(
            writer.TryEnqueueLine(
                "event=route-evidence-routine-spsc-proof"),
            "single-producer routine proof record was not accepted");

        std::vector<RouteEvidenceRecord> records;
        records.reserve(100);
        auto disable = MakeEvidence(
            RouteEvidenceRecordType::Lifecycle,
            RouteEvidenceWorkKind::None,
            0,
            4,
            "plugin-admin-disable");
        disable.disposition = RouteEvidenceDisposition::Cancelled;
        records.push_back(disable);
        for (std::uint64_t requestId = 1; requestId <= 32; ++requestId) {
            const auto kind = requestId % 5 == 0
                ? RouteEvidenceWorkKind::RoutePreparation
                : RouteEvidenceWorkKind::ExpandedFmsObservation;
            auto dispatch = MakeEvidence(
                RouteEvidenceRecordType::Dispatch,
                kind,
                requestId,
                4,
                requestId == 1
                    ? "expanded-fms-observation-required"
                    : "expanded-fms-quiet-observation-due");
            records.push_back(dispatch);
            auto terminal = MakeEvidence(
                RouteEvidenceRecordType::Terminal,
                kind,
                requestId,
                4,
                kind == RouteEvidenceWorkKind::ExpandedFmsObservation
                    ? "expanded-fms-match"
                    : "route-key-changed");
            records.push_back(terminal);
            auto disposition = MakeEvidence(
                RouteEvidenceRecordType::Disposition,
                kind,
                requestId,
                4,
                kind == RouteEvidenceWorkKind::ExpandedFmsObservation
                    ? "expanded-fms-observation-unchanged"
                    : "exact-route-completion-identity");
            disposition.disposition =
                kind == RouteEvidenceWorkKind::ExpandedFmsObservation
                ? RouteEvidenceDisposition::Observed
                : RouteEvidenceDisposition::Published;
            disposition.rebuildDecisionApplicable =
                kind == RouteEvidenceWorkKind::ExpandedFmsObservation;
            disposition.rebuildDecision = false;
            records.push_back(disposition);
        }
        auto disconnect = MakeEvidence(
            RouteEvidenceRecordType::Lifecycle,
            RouteEvidenceWorkKind::None,
            0,
            5,
            "network-plan-unavailable");
        disconnect.disposition = RouteEvidenceDisposition::Cancelled;
        records.push_back(disconnect);
        auto shutdown = MakeEvidence(
            RouteEvidenceRecordType::Lifecycle,
            RouteEvidenceWorkKind::None,
            0,
            6,
            "plugin-stop");
        shutdown.disposition = RouteEvidenceDisposition::Cancelled;
        records.push_back(shutdown);

        std::vector<std::uint64_t> producerSamples;
        producerSamples.reserve(records.size());
        for (auto record : records) {
            const auto started = std::chrono::steady_clock::now();
            const auto accepted = writer.TryEnqueueRouteEvidence(record);
            producerSamples.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - started)
                    .count()));
            require(accepted, "route-evidence record was not accepted");
        }
        producerP99Us = Percentile99(producerSamples);
        producerMaxUs = producerSamples.empty()
            ? 0
            : *std::max_element(
                  producerSamples.begin(), producerSamples.end());
        require(
            producerP99Us < 250 && producerMaxUs <= 1000,
            "route-evidence producer exceeded its nonblocking budget");
        require(
            writer.WaitUntilIdle(std::chrono::seconds(20)),
            "route-evidence lane did not drain after forced delay");
        const auto snapshot = writer.Snapshot();
        writtenEvidence = snapshot.writtenRouteEvidence;
        maximumEvidenceDepth = snapshot.maximumRouteEvidenceQueueDepth;
        require(
            snapshot.submittedRouteEvidence == records.size() &&
                snapshot.dequeuedRouteEvidence == records.size() &&
                snapshot.writtenRouteEvidence == records.size(),
            "route-evidence submitted/dequeued/written accounting differed");
        require(
            snapshot.droppedRouteEvidenceFull == 0 &&
                snapshot.rejectedRouteEvidenceNotRunning == 0,
            "route-evidence lane lost a record");
        require(
            snapshot.droppedRoutineContention == 0 &&
                snapshot.droppedCriticalContention == 0 &&
                snapshot.droppedContention == 0,
            "single-producer lanes reported impossible mutex contention");
        require(
            snapshot.lastSubmittedRouteEvidenceSequence == records.size() &&
                snapshot.lastWrittenRouteEvidenceSequence == records.size(),
            "route-evidence sequence accounting was not exact");
        require(
            snapshot.maximumRouteEvidenceQueueDepth ==
                options.routeEvidenceQueueCapacity,
            "route-evidence queue did not reach its exact fixed capacity");
        require(
            snapshot.maximumRouteEvidenceProducerMicroseconds <= 1000,
            "route-evidence internal producer maximum exceeded 1 ms");
        writer.Stop();
        require(!writer.Snapshot().running, "telemetry writer did not stop");

        auto afterStop = MakeEvidence(
            RouteEvidenceRecordType::Lifecycle,
            RouteEvidenceWorkKind::None,
            0,
            7,
            "session-runtime-reset");
        require(
            !writer.TryEnqueueRouteEvidence(afterStop) &&
                writer.Snapshot().rejectedRouteEvidenceNotRunning == 1,
            "post-stop route evidence was not rejected and counted");

        const auto logs = ReadAllLogs(options.logDirectory);
        const auto sequences = RouteEvidenceSequences(logs);
        require(
            sequences.size() == records.size(),
            "persisted route-evidence record count differed");
        for (std::size_t index = 0; index < sequences.size(); ++index) {
            require(
                sequences[index] == index + 1,
                "persisted route-evidence sequence contained a gap");
        }
        require(
            logs.find("workerWallUs=321 workerCpuStatus=available ") !=
                    std::string::npos &&
                logs.find("rebuildDecision=0 ") != std::string::npos &&
                logs.find("reason=plugin-admin-disable") !=
                    std::string::npos &&
                logs.find("reason=network-plan-unavailable") !=
                    std::string::npos &&
                logs.find("reason=plugin-stop") != std::string::npos,
            "route-evidence payload omitted timing, rebuild, or lifecycle data");
    }

    {
        AsyncDiagnosticsWriter writer;
        DiagnosticsWriterOptions options;
        options.logDirectory = probeRoot / "shutdown-drain";
        options.routineQueueCapacity = 8;
        options.criticalQueueCapacity = 4;
        options.flightLoopTimingQueueCapacity = 8;
        options.routeEvidenceQueueCapacity = 32;
        options.artificialWorkerDelayForTesting =
            std::chrono::milliseconds(20);
        options.artificialWorkerDelayRecordCountForTesting = 8;
        require(writer.Start(options), "shutdown-drain writer failed to start");
        for (std::uint64_t requestId = 1; requestId <= 8; ++requestId) {
            require(
                writer.TryEnqueueRouteEvidence(MakeEvidence(
                    RouteEvidenceRecordType::Terminal,
                    RouteEvidenceWorkKind::ExpandedFmsObservation,
                    requestId,
                    9,
                    "expanded-fms-match")),
                "shutdown-drain route evidence was not accepted");
        }
        writer.Stop();
        const auto stopped = writer.Snapshot();
        require(
            stopped.submittedRouteEvidence == 8 &&
                stopped.dequeuedRouteEvidence == 8 &&
                stopped.writtenRouteEvidence == 8 &&
                stopped.lastWrittenRouteEvidenceSequence == 8 &&
                stopped.droppedRouteEvidenceFull == 0,
            "shutdown did not drain every accepted route-evidence record");
        const auto sequences =
            RouteEvidenceSequences(ReadAllLogs(options.logDirectory));
        require(
            sequences.size() == 8 && sequences.front() == 1 &&
                sequences.back() == 8,
            "shutdown-drain persistence sequence was incomplete");
    }

    std::filesystem::remove_all(probeRoot, error);
    require(!error, "failed to clean telemetry probe root");

    std::cout << "GATE_B_TELEMETRY_PROBE"
              << " routeEvidenceWritten=" << writtenEvidence
              << " producerP99Us=" << producerP99Us
              << " producerMaxUs=" << producerMaxUs
              << " maxDepth=" << maximumEvidenceDepth
              << " fullDrops=0 notRunningDuringSession=0 sequenceGaps=0"
              << " shutdownDrain=8/8\n";
    if (!failures.empty()) {
        for (const auto& failure : failures) {
            std::cerr << "GATE_B_TELEMETRY_ASSERTION_FAILED: "
                      << failure << "\n";
        }
        return 1;
    }
    std::cout << "PERFORMANCE_CONTRACT_GATE_B_TELEMETRY_PASSED\n";
    return 0;
}

}  // namespace xvatsim::tools::performance_contract_gate_b_telemetry
