#include "XPilot4BridgeContractProbe.h"

#include "XVatsim/brain/BrainOwnedRuntime.h"
#include "XVatsim/brain/BrainOwnedWorkerTypes.h"
#include "XVatsim/modules/runtime_workers/XPilot4BridgeClient.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace xvatsim::tools::xpilot4_bridge_contract {
namespace {

using namespace xvatsim::brain;
using xvatsim::modules::runtime_workers::XPilot4BridgeClient;

constexpr std::uint32_t kMagic = 0x34425658U;
constexpr std::size_t kHeaderBytes = 44;
constexpr std::size_t kFramePayloadLimit = 256U * 1024U;

bool Require(bool condition, std::string message, std::string* failure) {
    if (condition) return true;
    if (failure != nullptr) *failure = std::move(message);
    return false;
}

void AppendU16(std::vector<std::uint8_t>* bytes, std::uint16_t value) {
    bytes->push_back(static_cast<std::uint8_t>(value & 0xffU));
    bytes->push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void AppendU32(std::vector<std::uint8_t>* bytes, std::uint32_t value) {
    for (unsigned index = 0; index < 4; ++index) {
        bytes->push_back(static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xffU));
    }
}

void AppendU64(std::vector<std::uint8_t>* bytes, std::uint64_t value) {
    for (unsigned index = 0; index < 8; ++index) {
        bytes->push_back(static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xffU));
    }
}

std::uint64_t ReadU64(const std::uint8_t* bytes) {
    std::uint64_t value = 0;
    for (unsigned index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

void AppendString(std::vector<std::uint8_t>* bytes, const std::string& value) {
    AppendU32(bytes, static_cast<std::uint32_t>(value.size()));
    bytes->insert(bytes->end(), value.begin(), value.end());
}

void AppendDouble(std::vector<std::uint8_t>* bytes, double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    AppendU64(bytes, bits);
}

std::vector<std::uint8_t> Frame(
    std::uint16_t kind,
    std::uint64_t processEpoch,
    std::uint64_t connectionEpoch,
    std::uint64_t sequence,
    const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> frame;
    frame.reserve(kHeaderBytes + payload.size());
    AppendU32(&frame, kMagic);
    AppendU16(&frame, kXPilot4BridgeProtocolVersion);
    AppendU16(&frame, kind);
    AppendU32(&frame, static_cast<std::uint32_t>(payload.size()));
    AppendU64(&frame, processEpoch);
    AppendU64(&frame, connectionEpoch);
    AppendU64(&frame, sequence);
    AppendU64(&frame, sequence * 1'000);
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

std::vector<std::vector<std::uint8_t>> ChunkedFrames(
    std::uint16_t originalKind,
    std::uint64_t processEpoch,
    std::uint64_t connectionEpoch,
    std::uint64_t sequence,
    const std::vector<std::uint8_t>& payload) {
    constexpr std::size_t metadata = 14;
    const auto perChunk = kFramePayloadLimit - metadata;
    const auto count = static_cast<std::uint32_t>(
        (payload.size() + perChunk - 1U) / perChunk);
    std::vector<std::vector<std::uint8_t>> frames;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto offset = static_cast<std::size_t>(index) * perChunk;
        const auto size = std::min(perChunk, payload.size() - offset);
        std::vector<std::uint8_t> chunk;
        chunk.reserve(metadata + size);
        AppendU16(&chunk, originalKind);
        AppendU32(&chunk, index);
        AppendU32(&chunk, count);
        AppendU32(&chunk, static_cast<std::uint32_t>(payload.size()));
        chunk.insert(
            chunk.end(), payload.begin() + offset,
            payload.begin() + offset + size);
        frames.push_back(Frame(
            200, processEpoch, connectionEpoch, sequence, chunk));
    }
    return frames;
}

bool WriteAll(HANDLE pipe, const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        const auto request = static_cast<DWORD>(std::min<std::size_t>(
            bytes.size() - offset, 64U * 1024U));
        if (!WriteFile(pipe, bytes.data() + offset, request, &written, nullptr) ||
            written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

bool ReadAll(HANDLE pipe, std::uint8_t* bytes, std::size_t count) {
    std::size_t offset = 0;
    while (offset < count) {
        DWORD read = 0;
        if (!ReadFile(
                pipe,
                bytes + offset,
                static_cast<DWORD>(count - offset),
                &read,
                nullptr) ||
            read == 0) {
            return false;
        }
        offset += read;
    }
    return true;
}

class ProtocolServer {
public:
    ProtocolServer() {
        const auto token = static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        name = L"XVatsim.XPilot4Bridge.Probe." + std::to_wstring(token);
        largeMessage.assign(300'000, 'Q');
        largeMessage.replace(123, 4, "UTF8");
    }

    ~ProtocolServer() {
        allowBurst.store(true);
        allowCorrupt.store(true);
        if (worker.joinable()) worker.join();
    }

    void Start() { worker = std::thread([this]() { Run(); }); }

    std::wstring name;
    std::string largeMessage;
    std::atomic<bool> ready{false};
    std::atomic<bool> requestRead{false};
    std::atomic<bool> snapshotSent{false};
    std::atomic<bool> allowBurst{false};
    std::atomic<bool> burstSent{false};
    std::atomic<bool> allowCorrupt{false};
    std::atomic<bool> complete{false};
    std::atomic<bool> failed{false};
    std::uint64_t requestId = 0;

private:
    void Fail() { failed.store(true); }

    bool Send(
        HANDLE pipe,
        BrainXPilot4EventKind kind,
        std::uint64_t sequence,
        const std::vector<std::uint8_t>& payload,
        std::uint64_t connectionEpoch = 44) {
        return WriteAll(
            pipe,
            Frame(
                static_cast<std::uint16_t>(kind),
                33,
                connectionEpoch,
                sequence,
                payload));
    }

    void Run() {
        const auto fullName = L"\\\\.\\pipe\\" + name;
        HANDLE pipe = CreateNamedPipeW(
            fullName.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            1U * 1024U * 1024U,
            1U * 1024U * 1024U,
            0,
            nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            Fail();
            ready.store(true);
            return;
        }
        ready.store(true);
        if (!ConnectNamedPipe(pipe, nullptr) &&
            GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(pipe);
            Fail();
            return;
        }

        std::vector<std::uint8_t> payload;
        payload.push_back(1);
        AppendString(&payload, "0.1.0-probe");
        AppendString(&payload, "0.1.0");
        AppendString(&payload, "xpilot4_plugin_sdk");
        AppendString(&payload, "xpilot_fsd");
        AppendString(&payload, "probe-compatible");
        if (!Send(pipe, BrainXPilot4EventKind::Hello, 0, payload, 0)) {
            CloseHandle(pipe); Fail(); return;
        }
        payload.clear();
        AppendString(&payload, "0.1.0");
        AppendString(&payload, "probe-source-ready");
        if (!Send(pipe, BrainXPilot4EventKind::SourceAvailable, 1, payload, 0)) {
            CloseHandle(pipe); Fail(); return;
        }
        payload.clear();
        payload.push_back(1);
        AppendString(&payload, "DAL250");
        if (!Send(
                pipe,
                BrainXPilot4EventKind::ConnectionStateChanged,
                2,
                payload)) {
            CloseHandle(pipe); Fail(); return;
        }
        payload.clear();
        AppendString(&payload, "FTW_CTR");
        AppendString(&payload, "PDC \xE2\x9C\x88\nLINE TWO");
        if (!Send(
                pipe,
                BrainXPilot4EventKind::IncomingPrivateMessage,
                3,
                payload)) {
            CloseHandle(pipe); Fail(); return;
        }
        payload.clear();
        AppendString(&payload, "AUTO");
        AppendString(&payload, largeMessage);
        for (const auto& frame : ChunkedFrames(
                 static_cast<std::uint16_t>(
                     BrainXPilot4EventKind::IncomingPrivateMessage),
                 33,
                 44,
                 4,
                 payload)) {
            if (!WriteAll(pipe, frame)) {
                CloseHandle(pipe); Fail(); return;
            }
        }

        std::array<std::uint8_t, kHeaderBytes + 8> request{};
        if (!ReadAll(pipe, request.data(), request.size()) ||
            request[0] != 'X' || request[1] != 'V' ||
            request[2] != 'B' || request[3] != '4' ||
            request[6] != 100 || request[7] != 0) {
            CloseHandle(pipe); Fail(); return;
        }
        requestId = ReadU64(request.data() + kHeaderBytes);
        requestRead.store(true);

        payload.clear();
        AppendU64(&payload, requestId);
        AppendU32(&payload, 1);
        if (!Send(
                pipe,
                BrainXPilot4EventKind::ControllerSnapshotStarted,
                5,
                payload)) {
            CloseHandle(pipe); Fail(); return;
        }
        payload.clear();
        AppendU64(&payload, requestId);
        AppendString(&payload, "ML_TWR");
        AppendU32(&payload, 120'500'000);
        AppendDouble(&payload, 0.0);
        AppendDouble(&payload, 0.0);
        if (!Send(
                pipe,
                BrainXPilot4EventKind::ControllerSnapshotEntry,
                6,
                payload)) {
            CloseHandle(pipe); Fail(); return;
        }
        payload.clear();
        AppendU64(&payload, requestId);
        AppendU32(&payload, 1);
        if (!Send(
                pipe,
                BrainXPilot4EventKind::ControllerSnapshotCompleted,
                7,
                payload)) {
            CloseHandle(pipe); Fail(); return;
        }
        snapshotSent.store(true);

        while (!allowBurst.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        for (std::uint64_t index = 0; index < 1100; ++index) {
            payload.clear();
            AppendString(&payload, "BURST_" + std::to_string(index));
            AppendU32(&payload, static_cast<std::uint32_t>(120'000'000 + index));
            if (!Send(
                    pipe,
                    BrainXPilot4EventKind::ControllerFrequencyChanged,
                    8 + index,
                    payload)) {
                CloseHandle(pipe); Fail(); return;
            }
        }
        burstSent.store(true);
        while (!allowCorrupt.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::vector<std::uint8_t> corrupt(kHeaderBytes, 0);
        (void)WriteAll(pipe, corrupt);
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        complete.store(true);
    }

    std::thread worker;
};

template <typename Predicate>
bool WaitFor(Predicate predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return predicate();
}

std::uint64_t ProcessCpu100ns() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(
            GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        return 0;
    }
    ULARGE_INTEGER kernelValue{}, userValue{};
    kernelValue.LowPart = kernel.dwLowDateTime;
    kernelValue.HighPart = kernel.dwHighDateTime;
    userValue.LowPart = user.dwLowDateTime;
    userValue.HighPart = user.dwHighDateTime;
    return kernelValue.QuadPart + userValue.QuadPart;
}

bool CheckAbsentIdleAndLifecycle(std::uint64_t* idleCpuUs, std::string* failure) {
    DWORD handlesBefore = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handlesBefore);
    const auto token = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    XPilot4BridgeClient client;
    if (!client.Start(
            L"XVatsim.XPilot4Bridge.Absent." + std::to_wstring(token))) {
        return Require(false, "absent companion client did not start", failure);
    }
    const auto cpuBefore = ProcessCpu100ns();
    std::this_thread::sleep_for(std::chrono::seconds(3));
    const auto cpuAfter = ProcessCpu100ns();
    const auto running = client.XPilot4TransportSnapshot();
    client.Stop();
    DWORD handlesAfter = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handlesAfter);
    *idleCpuUs = (cpuAfter - cpuBefore) / 10U;
    return Require(
        running.running && running.connectAttempts >= 2 &&
            *idleCpuUs <= 25'000 && handlesAfter <= handlesBefore + 2,
        "absent companion retry, CPU, or handle bound failed",
        failure);
}

bool CheckRealPipe(
    std::uint64_t* harvestP99Us,
    std::uint64_t* harvestMaxUs,
    std::uint64_t* lossCount,
    std::string* failure) {
    ProtocolServer server;
    server.Start();
    if (!WaitFor([&]() { return server.ready.load(); }, std::chrono::seconds(2)) ||
        server.failed.load()) {
        return Require(false, "probe pipe server did not start", failure);
    }
    XPilot4BridgeClient client;
    if (!client.Start(server.name)) {
        return Require(false, "xPilot 4 transport client did not start", failure);
    }

    std::vector<BrainXPilot4Observation> initial;
    const bool initialReady = WaitFor(
        [&]() {
            BrainXPilot4Observation observation;
            while (client.TryHarvestXPilot4Observation(&observation)) {
                initial.push_back(std::move(observation));
            }
            return std::any_of(initial.begin(), initial.end(), [&](const auto& item) {
                return item.kind == BrainXPilot4EventKind::IncomingPrivateMessage &&
                    item.message == server.largeMessage;
            });
        },
        std::chrono::seconds(5));
    const auto exactUnicode = std::any_of(
        initial.begin(), initial.end(), [](const auto& item) {
            return item.kind == BrainXPilot4EventKind::IncomingPrivateMessage &&
                item.sender == "FTW_CTR" &&
                item.message == "PDC \xE2\x9C\x88\nLINE TWO";
        });
    if (!initialReady || !exactUnicode) {
        client.Stop();
        return Require(false, "raw or chunked message round-trip failed", failure);
    }

    BrainXPilot4Command command;
    command.requestId = 77;
    if (!client.TrySubmitXPilot4Command(command)) {
        client.Stop();
        return Require(false, "snapshot command was not accepted", failure);
    }
    std::vector<BrainXPilot4Observation> snapshot;
    const bool snapshotReady = WaitFor(
        [&]() {
            BrainXPilot4Observation observation;
            while (client.TryHarvestXPilot4Observation(&observation)) {
                snapshot.push_back(std::move(observation));
            }
            return std::any_of(snapshot.begin(), snapshot.end(), [](const auto& item) {
                return item.kind ==
                    BrainXPilot4EventKind::ControllerSnapshotCompleted;
            });
        },
        std::chrono::seconds(5));
    const auto zeroController = std::find_if(
        snapshot.begin(), snapshot.end(), [](const auto& item) {
            return item.kind == BrainXPilot4EventKind::ControllerSnapshotEntry;
        });
    if (!snapshotReady || !server.requestRead.load() ||
        server.requestId != 77 || zeroController == snapshot.end() ||
        zeroController->controller.callsign != "ML_TWR" ||
        zeroController->controller.frequencyHz != 120'500'000 ||
        zeroController->controller.latitudeDeg != 0.0 ||
        zeroController->controller.longitudeDeg != 0.0) {
        client.Stop();
        return Require(false, "snapshot command or raw controller failed", failure);
    }

    const auto beforeBurst = client.XPilot4TransportSnapshot();
    server.allowBurst.store(true);
    if (!WaitFor([&]() { return server.burstSent.load(); }, std::chrono::seconds(5))) {
        client.Stop();
        return Require(false, "bounded queue burst did not finish", failure);
    }
    std::vector<std::uint64_t> harvestTimes;
    BrainXPilot4Observation observation;
    bool sawLoss = false;
    std::uint64_t observedLoss = 0;
    const bool burstReceived = WaitFor(
        [&]() {
            return client.XPilot4TransportSnapshot().framesReceived >=
                beforeBurst.framesReceived + 1100U;
        },
        std::chrono::seconds(5));
    // framesReceived advances immediately after a full frame read. Give the
    // same worker time to publish that final frame before examining the
    // bounded queue, then drain once while the probe server is intentionally
    // idle and waiting for the corrupt-frame phase.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    for (;;) {
        const auto started = std::chrono::steady_clock::now();
        if (!client.TryHarvestXPilot4Observation(&observation)) break;
        harvestTimes.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started).count()));
        if (observation.kind == BrainXPilot4EventKind::LocalCapacityLoss) {
            sawLoss = true;
            observedLoss += observation.lostCount;
        }
    }
    const bool lossReady = burstReceived && sawLoss;
    const auto bounded = client.XPilot4TransportSnapshot();
    if (!lossReady || observedLoss == 0 ||
        bounded.maximumIncomingDepth > 1024 ||
        bounded.localCapacityLoss != observedLoss) {
        client.Stop();
        return Require(false,
            "bounded incoming loss accounting failed lossReady=" +
                std::to_string(lossReady ? 1 : 0) +
                " observedLoss=" + std::to_string(observedLoss) +
                " localCapacityLoss=" +
                std::to_string(bounded.localCapacityLoss) +
                " framesReceived=" + std::to_string(bounded.framesReceived) +
                " beforeFrames=" + std::to_string(beforeBurst.framesReceived) +
                " maximumIncomingDepth=" +
                std::to_string(bounded.maximumIncomingDepth),
            failure);
    }
    std::sort(harvestTimes.begin(), harvestTimes.end());
    *harvestP99Us = harvestTimes.empty()
        ? 0
        : harvestTimes[(harvestTimes.size() - 1U) * 99U / 100U];
    *harvestMaxUs = harvestTimes.empty() ? 0 : harvestTimes.back();
    *lossCount = observedLoss;

    server.allowCorrupt.store(true);
    bool sawCorruptFact = false;
    const bool corruptReported = WaitFor(
        [&]() {
            BrainXPilot4Observation item;
            while (client.TryHarvestXPilot4Observation(&item)) {
                sawCorruptFact |=
                    item.kind == BrainXPilot4EventKind::CorruptEnvelope;
            }
            const auto health = client.XPilot4TransportSnapshot();
            return sawCorruptFact && health.corruptFrames == 1 &&
                health.disconnects >= 1;
        },
        std::chrono::seconds(5));
    const auto finalHealth = client.XPilot4TransportSnapshot();
    client.Stop();
    if (!corruptReported || finalHealth.maximumHarvestMicroseconds > 2'000 ||
        *harvestP99Us > 200 || *harvestMaxUs > 2'000) {
        return Require(false, "corruption fact or harvest timing failed", failure);
    }
    return Require(!server.failed.load(), "probe pipe server failed", failure);
}

class QueueTransport final : public BrainXPilot4Transport {
public:
    std::deque<BrainXPilot4Observation> observations;
    std::vector<BrainXPilot4Command> commands;

    bool TryHarvestXPilot4Observation(
        BrainXPilot4Observation* observation) override {
        if (observation == nullptr || observations.empty()) return false;
        *observation = std::move(observations.front());
        observations.pop_front();
        return true;
    }

    bool TrySubmitXPilot4Command(
        const BrainXPilot4Command& command) override {
        commands.push_back(command);
        return true;
    }

    BrainXPilot4TransportSnapshot XPilot4TransportSnapshot() const override {
        BrainXPilot4TransportSnapshot result;
        result.running = true;
        result.connected = true;
        return result;
    }
};

BrainXPilot4Observation Observation(
    BrainXPilot4EventKind kind,
    std::uint64_t sequence,
    std::uint64_t connectionEpoch = 55) {
    BrainXPilot4Observation observation;
    observation.kind = kind;
    observation.processEpoch = 44;
    observation.connectionEpoch = connectionEpoch;
    observation.sourceSequence = sequence;
    return observation;
}

bool HasXPilotCorrelation(
    const BrainOwnedCandidateCompletion& completion,
    const std::string& reason) {
    return std::any_of(
        completion.evidenceVotes.begin(),
        completion.evidenceVotes.end(),
        [&](const auto& vote) {
            return vote.source == "xpilot-fsd-correlation" &&
                vote.score == 0 && vote.reason == reason;
        });
}

void QueueConnectedBridgeSession(
    QueueTransport* transport,
    std::string callsign) {
    transport->observations.push_back(Observation(
        BrainXPilot4EventKind::TransportConnected, 0, 0));
    auto hello = Observation(BrainXPilot4EventKind::Hello, 0, 0);
    hello.compatible = true;
    hello.protocolVersion = kXPilot4BridgeProtocolVersion;
    hello.companionVersion = "0.1.0-probe";
    hello.apiVersion = "0.1.0";
    hello.reason = "probe-compatible";
    transport->observations.push_back(std::move(hello));
    auto available = Observation(
        BrainXPilot4EventKind::SourceAvailable, 1, 0);
    available.apiVersion = "0.1.0";
    transport->observations.push_back(std::move(available));
    auto connected = Observation(
        BrainXPilot4EventKind::ConnectionStateChanged, 2);
    connected.connected = true;
    connected.callsign = std::move(callsign);
    transport->observations.push_back(std::move(connected));
}

bool CheckBrainDeferredMessageAdmission(std::string* failure) {
    BrainOwnedRuntimeState state;
    InitializeBrainOwnedPdcRuntime(&state);
    QueueTransport transport;
    QueueConnectedBridgeSession(&transport, "UAL200");
    auto message = Observation(
        BrainXPilot4EventKind::IncomingPrivateMessage, 3);
    message.sender = "DEN_DEL";
    message.message = "PDC FOR UAL200";
    transport.observations.push_back(std::move(message));

    BrainXPilot4BridgeContext unavailableContext;
    (void)ServiceBrainOwnedXPilot4Bridge(
        &state, unavailableContext, &transport, 1'000'000, 8);
    if (BrainOwnedPdcMessageCount(state.pdc) != 0 ||
        state.xpilot4Bridge.pendingPrivateMessages.size() != 1 ||
        state.xpilot4Bridge.counters.privateMessagesEvaluated != 1 ||
        state.xpilot4Bridge.counters.privateMessagesDeferred != 1 ||
        state.xpilot4Bridge.counters.privateMessagesRejected != 0) {
        return Require(
            false,
            "Brain did not preserve startup private message evidence",
            failure);
    }

    BrainXPilot4BridgeContext readyContext;
    readyContext.flightContextActive = true;
    readyContext.flightCallsign = "UAL200";
    readyContext.departureIcao = "KDEN";
    readyContext.destinationIcao = "KLAX";

    state.flightContext.active = true;
    state.flightContext.callsign = "UAL200";
    state.flightContext.departureIcao = "KDEN";
    state.flightContext.destinationIcao = "KLAX";
    (void)ResetBrainOwnedAccessoryForConfirmedNewFlight(&state);
    ResetBrainOwnedRuntimeCachePreservingFlightContext(&state, true);
    if (state.xpilot4Bridge.pendingPrivateMessages.size() != 1 ||
        !state.xpilot4Bridge.transportConnected ||
        !state.xpilot4Bridge.sourceAvailable ||
        !state.xpilot4Bridge.sdkCompatible ||
        !state.xpilot4Bridge.networkConnected ||
        state.xpilot4Bridge.callsign != "UAL200") {
        return Require(
            false,
            "Confirmed flight activation erased the live xPilot 4 session or pending message",
            failure);
    }
    (void)ServiceBrainOwnedXPilot4Bridge(
        &state, readyContext, &transport, 1'100'000, 8);
    if (state.xpilot4Bridge.pendingPrivateMessages.size() != 0 ||
        state.xpilot4Bridge.counters.privateMessagesAdmitted != 1 ||
        state.xpilot4Bridge.counters.privateMessagesEvaluated != 1 ||
        BrainOwnedPdcMessageCount(state.pdc) != 1 ||
        BrainOwnedPdcUnreadCount(state.pdc) != 1 ||
        !state.pdc.capturedArtifact.has_value() ||
        state.pdc.capturedArtifact->body != "PDC FOR UAL200") {
        return Require(
            false,
            "Brain did not admit deferred startup message after context arrived",
            failure);
    }
    const auto startupPresentation =
        ProjectBrainOwnedAccessoryPresentation(&state, nullptr);
    const BrainOwnedAccessoryOrbPresentation* startupPdcOrb = nullptr;
    if (startupPresentation.snapshot != nullptr) {
        const auto found = std::find_if(
            startupPresentation.snapshot->orbs.begin(),
            startupPresentation.snapshot->orbs.end(),
            [](const auto& orb) {
                return orb.drawer == BrainOwnedAccessoryDrawerId::Pdc;
            });
        if (found != startupPresentation.snapshot->orbs.end()) {
            startupPdcOrb = &*found;
        }
    }
    if (startupPdcOrb == nullptr ||
        startupPdcOrb->tone !=
            BrainOwnedAccessoryOrbPresentation::Tone::Amber ||
        startupPdcOrb->categoryText != "NEW") {
        return Require(
            false,
            "Deferred startup message did not produce Brain-owned NEW Orb",
            failure);
    }

    auto delayedForCallsign = Observation(
        BrainXPilot4EventKind::IncomingPrivateMessage, 4);
    delayedForCallsign.sender = "DEN_CTR";
    delayedForCallsign.message = "PRIVATE FOR UAL200";
    transport.observations.push_back(std::move(delayedForCallsign));
    auto wrongContext = readyContext;
    wrongContext.flightCallsign = "SWA100";
    (void)ServiceBrainOwnedXPilot4Bridge(
        &state, wrongContext, &transport, 1'200'000, 8);
    if (state.xpilot4Bridge.pendingPrivateMessages.size() != 1 ||
        BrainOwnedPdcMessageCount(state.pdc) != 1) {
        return Require(
            false,
            "Brain did not defer a current-session callsign mismatch",
            failure);
    }
    (void)ServiceBrainOwnedXPilot4Bridge(
        &state, readyContext, &transport, 1'300'000, 8);
    if (state.xpilot4Bridge.pendingPrivateMessages.size() != 0 ||
        BrainOwnedPdcMessageCount(state.pdc) != 2) {
        return Require(
            false,
            "Brain did not re-evaluate callsign-bound pending evidence",
            failure);
    }

    auto pendingAtDisconnect = Observation(
        BrainXPilot4EventKind::IncomingPrivateMessage, 5);
    pendingAtDisconnect.sender = "DEN_DEL";
    pendingAtDisconnect.message = "MUST NOT CROSS SESSION";
    transport.observations.push_back(std::move(pendingAtDisconnect));
    (void)ServiceBrainOwnedXPilot4Bridge(
        &state, unavailableContext, &transport, 1'400'000, 8);
    auto disconnected = Observation(
        BrainXPilot4EventKind::ConnectionStateChanged, 6);
    disconnected.connected = false;
    transport.observations.push_back(std::move(disconnected));
    (void)ServiceBrainOwnedXPilot4Bridge(
        &state, unavailableContext, &transport, 1'500'000, 8);
    if (!state.xpilot4Bridge.pendingPrivateMessages.empty() ||
        state.xpilot4Bridge.counters
                .privateMessagesDiscardedOnSessionChange != 1 ||
        BrainOwnedPdcMessageCount(state.pdc) != 2) {
        return Require(
            false,
            "Brain did not invalidate pending evidence on disconnect",
            failure);
    }

    BrainOwnedRuntimeState processState;
    InitializeBrainOwnedPdcRuntime(&processState);
    QueueTransport processTransport;
    QueueConnectedBridgeSession(&processTransport, "UAL200");
    auto processPending = Observation(
        BrainXPilot4EventKind::IncomingPrivateMessage, 3);
    processPending.sender = "DEN_DEL";
    processPending.message = "OLD PROCESS";
    processTransport.observations.push_back(std::move(processPending));
    (void)ServiceBrainOwnedXPilot4Bridge(
        &processState,
        unavailableContext,
        &processTransport,
        2'000'000,
        8);
    auto newProcess = Observation(BrainXPilot4EventKind::Hello, 0, 0);
    newProcess.processEpoch = 99;
    newProcess.compatible = true;
    newProcess.protocolVersion = kXPilot4BridgeProtocolVersion;
    newProcess.companionVersion = "0.1.0-probe";
    newProcess.apiVersion = "0.1.0";
    processTransport.observations.push_back(std::move(newProcess));
    (void)ServiceBrainOwnedXPilot4Bridge(
        &processState,
        unavailableContext,
        &processTransport,
        2'100'000,
        8);
    if (!processState.xpilot4Bridge.pendingPrivateMessages.empty() ||
        processState.xpilot4Bridge.counters
                .privateMessagesDiscardedOnSessionChange != 1) {
        return Require(
            false,
            "Brain did not invalidate pending evidence on process change",
            failure);
    }

    BrainOwnedRuntimeState expiryState;
    InitializeBrainOwnedPdcRuntime(&expiryState);
    QueueTransport expiryTransport;
    QueueConnectedBridgeSession(&expiryTransport, "UAL200");
    auto expiryMessage = Observation(
        BrainXPilot4EventKind::IncomingPrivateMessage, 3);
    expiryMessage.sender = "DEN_DEL";
    expiryMessage.message = "EXPIRES WITHOUT CONTEXT";
    expiryTransport.observations.push_back(std::move(expiryMessage));
    (void)ServiceBrainOwnedXPilot4Bridge(
        &expiryState, unavailableContext, &expiryTransport, 1'000'000, 8);
    (void)ServiceBrainOwnedXPilot4Bridge(
        &expiryState,
        unavailableContext,
        &expiryTransport,
        1'000'000 +
            kXPilot4PendingPrivateMessageLifetimeMicroseconds,
        8);
    if (!expiryState.xpilot4Bridge.pendingPrivateMessages.empty() ||
        expiryState.xpilot4Bridge.counters.privateMessagesExpired != 1 ||
        expiryState.xpilot4Bridge.counters.privateMessagesRejected != 1 ||
        !expiryState.pdc.preCaptureUncertain) {
        return Require(
            false,
            "Brain pending-message expiry accounting failed",
            failure);
    }

    BrainOwnedRuntimeState capacityState;
    InitializeBrainOwnedPdcRuntime(&capacityState);
    QueueTransport capacityTransport;
    QueueConnectedBridgeSession(&capacityTransport, "UAL200");
    for (std::size_t index = 0;
         index < kXPilot4MaximumPendingPrivateMessages + 1U;
         ++index) {
        auto pending = Observation(
            BrainXPilot4EventKind::IncomingPrivateMessage,
            3U + static_cast<std::uint64_t>(index));
        pending.sender = "DEN_DEL";
        pending.message = "BOUNDED " + std::to_string(index);
        capacityTransport.observations.push_back(std::move(pending));
    }
    (void)ServiceBrainOwnedXPilot4Bridge(
        &capacityState,
        unavailableContext,
        &capacityTransport,
        3'000'000,
        128);
    if (capacityState.xpilot4Bridge.pendingPrivateMessages.size() !=
            kXPilot4MaximumPendingPrivateMessages ||
        capacityState.xpilot4Bridge.counters.privateMessagesDeferred !=
            kXPilot4MaximumPendingPrivateMessages + 1U ||
        capacityState.xpilot4Bridge.counters
                .privateMessagesCapacityEvicted != 1 ||
        capacityState.xpilot4Bridge.counters.privateMessagesRejected != 1 ||
        !capacityState.pdc.preCaptureUncertain) {
        return Require(
            false,
            "Brain pending-message capacity accounting failed",
            failure);
    }
    return true;
}

bool CheckBrain(std::uint64_t* maxServiceUs, std::string* failure) {
    if (!CheckBrainDeferredMessageAdmission(failure)) return false;
    BrainOwnedRuntimeState state;
    InitializeBrainOwnedPdcRuntime(&state);
    QueueTransport transport;
    transport.observations.push_back(Observation(
        BrainXPilot4EventKind::TransportConnected, 0, 0));
    auto hello = Observation(BrainXPilot4EventKind::Hello, 0, 0);
    hello.compatible = true;
    hello.protocolVersion = kXPilot4BridgeProtocolVersion;
    hello.companionVersion = "0.1.0-probe";
    hello.apiVersion = "0.1.0";
    hello.reason = "probe-compatible";
    transport.observations.push_back(std::move(hello));
    auto available = Observation(BrainXPilot4EventKind::SourceAvailable, 1, 0);
    available.apiVersion = "0.1.0";
    transport.observations.push_back(std::move(available));
    auto connected = Observation(
        BrainXPilot4EventKind::ConnectionStateChanged, 2);
    connected.connected = true;
    connected.callsign = "DAL250";
    transport.observations.push_back(std::move(connected));
    auto message = Observation(BrainXPilot4EventKind::IncomingPrivateMessage, 3);
    message.sender = "FTW_CTR";
    message.message = "PDC \xE2\x9C\x88\nLINE TWO";
    transport.observations.push_back(std::move(message));

    BrainXPilot4BridgeContext context;
    context.flightContextActive = true;
    context.flightCallsign = "DAL250";
    context.departureIcao = "KDFW";
    context.destinationIcao = "KPHX";
    const auto first = ServiceBrainOwnedXPilot4Bridge(
        &state, context, &transport, 1'000'000, 8);
    if (!first.commandSubmitted || transport.commands.size() != 1 ||
        BrainOwnedPdcMessageCount(state.pdc) != 1 ||
        BrainOwnedPdcUnreadCount(state.pdc) != 1 ||
        !state.pdc.capturedArtifact.has_value() ||
        state.pdc.capturedArtifact->sourceKind != "xpilot4_plugin_sdk" ||
        state.pdc.capturedArtifact->body != "PDC \xE2\x9C\x88\nLINE TWO") {
        return Require(false, "Brain xPilot 4 message admission failed", failure);
    }

    const auto snapshotRequest = transport.commands.back().requestId;
    auto started = Observation(
        BrainXPilot4EventKind::ControllerSnapshotStarted, 4);
    started.snapshotRequestId = snapshotRequest;
    started.snapshotEntryCount = 1;
    transport.observations.push_back(std::move(started));
    auto entry = Observation(
        BrainXPilot4EventKind::ControllerSnapshotEntry, 5);
    entry.snapshotRequestId = snapshotRequest;
    entry.controller.callsign = "ML_TWR";
    entry.controller.frequencyHz = 120'500'000;
    entry.controller.latitudeDeg = 0.0;
    entry.controller.longitudeDeg = 0.0;
    entry.controller.hasFrequency = true;
    entry.controller.hasLocation = true;
    transport.observations.push_back(std::move(entry));
    auto completed = Observation(
        BrainXPilot4EventKind::ControllerSnapshotCompleted, 6);
    completed.snapshotRequestId = snapshotRequest;
    completed.snapshotEntryCount = 1;
    transport.observations.push_back(std::move(completed));
    (void)ServiceBrainOwnedXPilot4Bridge(
        &state, context, &transport, 1'100'000, 8);
    const auto controllers = state.xpilot4Bridge.controllerSnapshot;
    if (controllers == nullptr || !controllers->complete ||
        controllers->controllers.size() != 1 ||
        controllers->controllers.front().callsign != "ML_TWR" ||
        controllers->controllers.front().latitudeDeg != 0.0 ||
        controllers->controllers.front().longitudeDeg != 0.0) {
        return Require(false, "Brain raw controller snapshot failed", failure);
    }

    auto reconnectHello = Observation(BrainXPilot4EventKind::Hello, 0);
    reconnectHello.compatible = true;
    reconnectHello.protocolVersion = kXPilot4BridgeProtocolVersion;
    reconnectHello.companionVersion = "0.1.0-probe";
    reconnectHello.apiVersion = "0.1.0";
    transport.observations.push_back(std::move(reconnectHello));
    auto replay = Observation(BrainXPilot4EventKind::IncomingPrivateMessage, 3);
    replay.sender = "FTW_CTR";
    replay.message = "PDC \xE2\x9C\x88\nLINE TWO";
    transport.observations.push_back(std::move(replay));
    (void)ServiceBrainOwnedXPilot4Bridge(
        &state, context, &transport, 1'150'000, 8);
    if (BrainOwnedPdcMessageCount(state.pdc) != 1 ||
        state.xpilot4Bridge.counters.staleObservations != 1) {
        return Require(false, "Brain transport replay rejection failed", failure);
    }

    BrainControllerRelevanceWorkerInput controllerInput;
    controllerInput.workflowStage = WorkflowStage::Departure;
    controllerInput.radioBoardHash = 11;
    controllerInput.routePolygonHash = 22;
    controllerInput.currentPolygonKey = "YBLA";
    controllerInput.departureIcao = "YMML";
    controllerInput.arrivalIcao = "YSSY";
    controllerInput.hasDepartureCoordinates = true;
    controllerInput.departureLatitudeDeg = -37.6733;
    controllerInput.departureLongitudeDeg = 144.8433;
    controllerInput.hasArrivalCoordinates = true;
    controllerInput.arrivalLatitudeDeg = -33.9461;
    controllerInput.arrivalLongitudeDeg = 151.1772;
    auto route = std::make_shared<RouteSectorSnapshot>();
    route->available = true;
    route->routeResolved = true;
    route->airportCallsignAliases.push_back(
        {"YMML", "ML", "YBLA", "VATSPY_AIRPORT", "probe"});
    controllerInput.route = route;
    RadioReachableControllerCandidate candidate;
    candidate.callsign = "ML_TWR";
    candidate.frequency = "120.500";
    candidate.group = RadioReachableFacilityGroup::Tower;
    candidate.actionable = true;
    candidate.stationCoordinates.push_back(
        {candidate.frequency, -37.6733, 144.8433});
    controllerInput.candidates.push_back(candidate);
    auto baselineInput = controllerInput;
    const auto baseline = RunBrainControllerRelevanceWorker(baselineInput);
    controllerInput.xpilot4ControllerHash = controllers->stableHash;
    controllerInput.xpilot4Controllers = controllers;
    const auto correlated = RunBrainControllerRelevanceWorker(controllerInput);
    if (baseline.completions.size() != 1 || correlated.completions.size() != 1 ||
        baseline.completions.front().decision !=
            correlated.completions.front().decision ||
        baseline.completions.front().positiveVotes !=
            correlated.completions.front().positiveVotes ||
        baseline.completions.front().negativeVotes !=
            correlated.completions.front().negativeVotes ||
        correlated.completions.front().neutralVotes !=
            baseline.completions.front().neutralVotes + 1 ||
        !HasXPilotCorrelation(
            correlated.completions.front(),
            "correlated-controller-confirmed")) {
        return Require(false, "Brain neutral controller receipt failed", failure);
    }

    auto second = Observation(BrainXPilot4EventKind::IncomingPrivateMessage, 7);
    second.sender = "FTW_DEL";
    second.message = "CLEARED KDFW KPHX";
    transport.observations.push_back(std::move(second));
    (void)ServiceBrainOwnedXPilot4Bridge(
        &state, context, &transport, 1'200'000, 8);
    if (BrainOwnedPdcMessageCount(state.pdc) != 2 ||
        BrainOwnedPdcUnreadCount(state.pdc) != 2 ||
        !state.pdc.capturedArtifact.has_value() ||
        state.pdc.capturedArtifact->body != "CLEARED KDFW KPHX") {
        return Require(false, "Brain newest-first xPilot 4 history failed", failure);
    }
    if (AdmitBrainOwnedPdcMessage(
            &state,
            "FTW_DEL",
            "CLEARED KDFW KPHX",
            "LOG-DUPLICATE",
            99,
            {},
            1'300'000,
            "xpilot3_network_log") ||
        BrainOwnedPdcMessageCount(state.pdc) != 2) {
        return Require(false, "Brain cross-source correlation failed", failure);
    }
    if (!AcknowledgeBrainOwnedPdcAllEntries(&state) ||
        BrainOwnedPdcUnreadCount(state.pdc) != 0) {
        return Require(false, "Brain unread acknowledgement failed", failure);
    }
    const auto presentation = ProjectBrainOwnedAccessoryPresentation(&state, nullptr);
    const BrainOwnedAccessoryOrbPresentation* pdcOrb = nullptr;
    if (presentation.snapshot != nullptr) {
        const auto found = std::find_if(
            presentation.snapshot->orbs.begin(),
            presentation.snapshot->orbs.end(),
            [](const auto& orb) {
                return orb.drawer == BrainOwnedAccessoryDrawerId::Pdc;
            });
        if (found != presentation.snapshot->orbs.end()) pdcOrb = &*found;
    }
    if (pdcOrb == nullptr ||
        pdcOrb->tone != BrainOwnedAccessoryOrbPresentation::Tone::Cyan ||
        pdcOrb->categoryText != "IDLE") {
        return Require(false, "Brain PDC Orb idle projection failed", failure);
    }

    auto loss = Observation(BrainXPilot4EventKind::CapacityLoss, 20);
    loss.lostCount = 12;
    loss.firstLostSequence = 8;
    loss.lastLostSequence = 19;
    transport.observations.push_back(std::move(loss));
    (void)ServiceBrainOwnedXPilot4Bridge(
        &state, context, &transport, 2'000'000, 8);
    if (state.xpilot4Bridge.counters.capacityLosses != 12 ||
        state.xpilot4Bridge.counters.sequenceGaps != 1 ||
        !state.pdc.preCaptureUncertain || transport.commands.size() != 2) {
        return Require(false, "Brain explicit loss recovery failed", failure);
    }
    (void)ServiceBrainOwnedXPilot4Bridge(
        &state, context, &transport, 8'000'001, 8);
    if (state.xpilot4Bridge.counters.incompleteSnapshots == 0 ||
        transport.commands.size() != 3) {
        return Require(false, "Brain snapshot timeout recovery failed", failure);
    }

    auto mismatched = Observation(
        BrainXPilot4EventKind::IncomingPrivateMessage, 21);
    mismatched.sender = "PHX_CTR";
    mismatched.message = "MUST NOT BIND";
    transport.observations.push_back(std::move(mismatched));
    auto wrongContext = context;
    wrongContext.flightCallsign = "SWA100";
    (void)ServiceBrainOwnedXPilot4Bridge(
        &state, wrongContext, &transport, 8'100'000, 8);
    if (BrainOwnedPdcMessageCount(state.pdc) != 2) {
        return Require(false, "Brain callsign binding failed", failure);
    }

    ResetBrainOwnedPdcProductState(&state, false);
    (void)ServiceBrainOwnedXPilot4Bridge(
        &state, context, &transport, 8'200'000, 8);
    if (!state.pdc.xpilot4SourceAvailable || !state.pdc.sourceAvailable) {
        return Require(false, "Brain source availability recovery failed", failure);
    }
    SetBrainOwnedPdcSourceAvailability(
        &state, BrainPdcEvidenceSource::XPilot3NetworkLog, true);
    SetBrainOwnedPdcSourceAvailability(
        &state, BrainPdcEvidenceSource::XPilot4PluginSdk, false);
    if (!state.pdc.sourceAvailable || !state.pdc.logSourceAvailable) {
        return Require(false, "Brain source coexistence failed", failure);
    }

    *maxServiceUs = state.xpilot4Bridge.counters.maximumServiceMicroseconds;
    return Require(
        *maxServiceUs <= 2'000,
        "Brain service exceeded the predeclared two-millisecond limit",
        failure);
}

}  // namespace

int RunXPilot4BridgeContractProbe() {
    std::string failure;
    std::uint64_t idleCpuUs = 0;
    std::uint64_t harvestP99Us = 0;
    std::uint64_t harvestMaxUs = 0;
    std::uint64_t lossCount = 0;
    std::uint64_t brainMaxUs = 0;
    if (!CheckAbsentIdleAndLifecycle(&idleCpuUs, &failure) ||
        !CheckRealPipe(
            &harvestP99Us,
            &harvestMaxUs,
            &lossCount,
            &failure) ||
        !CheckBrain(&brainMaxUs, &failure)) {
        std::cerr << "XPILOT4_BRIDGE_CONTRACT_FAILED: " << failure << '\n';
        return 1;
    }
    std::cout << "XPILOT4_BRIDGE_CONTRACT_PASSED"
              << " idleCpuUs=" << idleCpuUs
              << " harvestP99Us=" << harvestP99Us
              << " harvestMaxUs=" << harvestMaxUs
              << " brainMaxUs=" << brainMaxUs
              << " explicitLoss=" << lossCount
              << " workerDecisionFields=0"
              << " controllerVote=neutral-correlated"
              << " orb=idle-after-ack\n";
    return 0;
}

}  // namespace xvatsim::tools::xpilot4_bridge_contract
