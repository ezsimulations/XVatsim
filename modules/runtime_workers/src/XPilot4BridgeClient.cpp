#include "XVatsim/modules/runtime_workers/XPilot4BridgeClient.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace xvatsim::modules::runtime_workers {
namespace {

constexpr std::uint32_t kMagic = 0x34425658U;
constexpr std::size_t kHeaderBytes = 44;
constexpr std::size_t kMaximumFramePayloadBytes = 256U * 1024U;
constexpr std::size_t kMaximumObservationBytes = 4U * 1024U * 1024U;
constexpr std::size_t kIncomingCapacity = 1024;
constexpr std::size_t kOutgoingCapacity = 8;
constexpr DWORD kReconnectMilliseconds = 1000;

std::uint16_t ReadU16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(bytes[0]) |
        (static_cast<std::uint16_t>(bytes[1]) << 8U);
}

std::uint32_t ReadU32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::uint64_t ReadU64(const std::uint8_t* bytes) {
    std::uint64_t value = 0;
    for (unsigned index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

void WriteU16(std::uint8_t* bytes, std::uint16_t value) {
    bytes[0] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

void WriteU32(std::uint8_t* bytes, std::uint32_t value) {
    for (unsigned index = 0; index < 4; ++index) {
        bytes[index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xffU);
    }
}

void WriteU64(std::uint8_t* bytes, std::uint64_t value) {
    for (unsigned index = 0; index < 8; ++index) {
        bytes[index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xffU);
    }
}

std::int64_t CurrentUnixMicroseconds() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

struct FrameHeader {
    brain::BrainXPilot4EventKind kind =
        brain::BrainXPilot4EventKind::TransportDisconnected;
    std::uint16_t version = 0;
    std::uint32_t payloadLength = 0;
    std::uint64_t processEpoch = 0;
    std::uint64_t connectionEpoch = 0;
    std::uint64_t sequence = 0;
    std::int64_t receivedUnixMicroseconds = 0;
};

bool DecodeHeader(
    const std::array<std::uint8_t, kHeaderBytes>& bytes,
    FrameHeader* header) {
    if (header == nullptr || ReadU32(bytes.data()) != kMagic) return false;
    header->version = ReadU16(bytes.data() + 4);
    header->kind = static_cast<brain::BrainXPilot4EventKind>(
        ReadU16(bytes.data() + 6));
    header->payloadLength = ReadU32(bytes.data() + 8);
    header->processEpoch = ReadU64(bytes.data() + 12);
    header->connectionEpoch = ReadU64(bytes.data() + 20);
    header->sequence = ReadU64(bytes.data() + 28);
    header->receivedUnixMicroseconds = static_cast<std::int64_t>(
        ReadU64(bytes.data() + 36));
    return header->version == brain::kXPilot4BridgeProtocolVersion &&
        header->payloadLength <= kMaximumFramePayloadBytes;
}

class PayloadReader {
public:
    explicit PayloadReader(const std::vector<std::uint8_t>& bytes)
        : bytes_(bytes) {}

    bool ReadBool(bool* value) {
        std::uint8_t byte = 0;
        if (!ReadBytes(&byte, 1)) return false;
        *value = byte != 0;
        return true;
    }

    bool ReadI32(int* value) {
        std::array<std::uint8_t, 4> bytes{};
        if (!ReadBytes(bytes.data(), bytes.size())) return false;
        *value = static_cast<int>(ReadU32(bytes.data()));
        return true;
    }

    bool ReadU32Value(std::uint32_t* value) {
        std::array<std::uint8_t, 4> bytes{};
        if (!ReadBytes(bytes.data(), bytes.size())) return false;
        *value = ReadU32(bytes.data());
        return true;
    }

    bool ReadU64Value(std::uint64_t* value) {
        std::array<std::uint8_t, 8> bytes{};
        if (!ReadBytes(bytes.data(), bytes.size())) return false;
        *value = ReadU64(bytes.data());
        return true;
    }

    bool ReadDouble(double* value) {
        std::array<std::uint8_t, 8> bytes{};
        if (!ReadBytes(bytes.data(), bytes.size())) return false;
        const auto bits = ReadU64(bytes.data());
        static_assert(sizeof(bits) == sizeof(*value));
        std::memcpy(value, &bits, sizeof(bits));
        return true;
    }

    bool ReadString(std::string* value) {
        std::uint32_t length = 0;
        if (value == nullptr || !ReadU32Value(&length) ||
            length > bytes_.size() - offset_) {
            return false;
        }
        value->assign(
            reinterpret_cast<const char*>(bytes_.data() + offset_),
            length);
        offset_ += length;
        return true;
    }

    bool Complete() const { return offset_ == bytes_.size(); }

private:
    bool ReadBytes(void* destination, std::size_t count) {
        if (destination == nullptr || count > bytes_.size() - offset_) {
            return false;
        }
        std::memcpy(destination, bytes_.data() + offset_, count);
        offset_ += count;
        return true;
    }

    const std::vector<std::uint8_t>& bytes_;
    std::size_t offset_ = 0;
};

bool ReadController(
    PayloadReader* reader,
    brain::BrainXPilot4ControllerFact* controller) {
    return reader != nullptr && controller != nullptr &&
        reader->ReadString(&controller->callsign) &&
        reader->ReadI32(&controller->frequencyHz) &&
        reader->ReadDouble(&controller->latitudeDeg) &&
        reader->ReadDouble(&controller->longitudeDeg);
}

bool DecodeObservation(
    const FrameHeader& header,
    const std::vector<std::uint8_t>& payload,
    brain::BrainXPilot4Observation* observation) {
    if (observation == nullptr) return false;
    brain::BrainXPilot4Observation value;
    value.kind = header.kind;
    value.protocolVersion = header.version;
    value.processEpoch = header.processEpoch;
    value.connectionEpoch = header.connectionEpoch;
    value.sourceSequence = header.sequence;
    value.receivedUnixMicroseconds = header.receivedUnixMicroseconds;
    PayloadReader reader(payload);

    switch (header.kind) {
        case brain::BrainXPilot4EventKind::Hello:
            if (!reader.ReadBool(&value.compatible) ||
                !reader.ReadString(&value.companionVersion) ||
                !reader.ReadString(&value.apiVersion) ||
                !reader.ReadString(&value.sourceKind) ||
                !reader.ReadString(&value.sourceFamily) ||
                !reader.ReadString(&value.reason)) return false;
            break;
        case brain::BrainXPilot4EventKind::SourceAvailable:
        case brain::BrainXPilot4EventKind::SourceUnavailable:
            if (!reader.ReadString(&value.apiVersion) ||
                !reader.ReadString(&value.reason)) return false;
            break;
        case brain::BrainXPilot4EventKind::ConnectionStateChanged:
            if (!reader.ReadBool(&value.connected) ||
                !reader.ReadString(&value.callsign)) return false;
            break;
        case brain::BrainXPilot4EventKind::IncomingPrivateMessage:
            if (!reader.ReadString(&value.sender) ||
                !reader.ReadString(&value.message)) return false;
            break;
        case brain::BrainXPilot4EventKind::ControllerSnapshotStarted:
        case brain::BrainXPilot4EventKind::ControllerSnapshotCompleted:
            if (!reader.ReadU64Value(&value.snapshotRequestId) ||
                !reader.ReadU32Value(&value.snapshotEntryCount)) return false;
            break;
        case brain::BrainXPilot4EventKind::ControllerSnapshotEntry:
        case brain::BrainXPilot4EventKind::ControllerAdded:
            if (!reader.ReadU64Value(&value.snapshotRequestId) ||
                !ReadController(&reader, &value.controller)) return false;
            value.controller.hasFrequency = true;
            value.controller.hasLocation = true;
            break;
        case brain::BrainXPilot4EventKind::ControllerDeleted:
            if (!reader.ReadString(&value.controller.callsign)) return false;
            break;
        case brain::BrainXPilot4EventKind::ControllerFrequencyChanged:
            if (!reader.ReadString(&value.controller.callsign) ||
                !reader.ReadI32(&value.controller.frequencyHz)) return false;
            value.controller.hasFrequency = true;
            break;
        case brain::BrainXPilot4EventKind::ControllerLocationChanged:
            if (!reader.ReadString(&value.controller.callsign) ||
                !reader.ReadDouble(&value.controller.latitudeDeg) ||
                !reader.ReadDouble(&value.controller.longitudeDeg)) return false;
            value.controller.hasLocation = true;
            break;
        case brain::BrainXPilot4EventKind::CapacityLoss:
            if (!reader.ReadU64Value(&value.lostCount) ||
                !reader.ReadU64Value(&value.firstLostSequence) ||
                !reader.ReadU64Value(&value.lastLostSequence) ||
                !reader.ReadString(&value.reason)) return false;
            break;
        case brain::BrainXPilot4EventKind::CorruptEnvelope:
            if (!reader.ReadString(&value.reason)) return false;
            break;
        case brain::BrainXPilot4EventKind::SessionEnded:
            break;
        default:
            return false;
    }
    if (!reader.Complete()) return false;
    *observation = std::move(value);
    return true;
}

}  // namespace

struct XPilot4BridgeClient::Implementation {
    struct ChunkAccumulator {
        bool active = false;
        FrameHeader header;
        brain::BrainXPilot4EventKind originalKind =
            brain::BrainXPilot4EventKind::CorruptEnvelope;
        std::uint32_t nextIndex = 0;
        std::uint32_t chunkCount = 0;
        std::uint32_t totalBytes = 0;
        std::vector<std::uint8_t> bytes;
    };

    std::wstring pipeName;
    HANDLE stopEvent = nullptr;
    HANDLE commandEvent = nullptr;
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> connected{false};
    mutable std::mutex incomingMutex;
    std::deque<brain::BrainXPilot4Observation> incoming;
    std::uint64_t pendingLostCount = 0;
    std::uint64_t pendingFirstLostSequence = 0;
    std::uint64_t pendingLastLostSequence = 0;
    mutable std::mutex outgoingMutex;
    std::deque<brain::BrainXPilot4Command> outgoing;
    ChunkAccumulator chunks;
    std::atomic<std::uint64_t> connectAttempts{0};
    std::atomic<std::uint64_t> connections{0};
    std::atomic<std::uint64_t> disconnects{0};
    std::atomic<std::uint64_t> framesReceived{0};
    std::atomic<std::uint64_t> observationsPublished{0};
    std::atomic<std::uint64_t> commandsSubmitted{0};
    std::atomic<std::uint64_t> commandsWritten{0};
    std::atomic<std::uint64_t> corruptFrames{0};
    std::atomic<std::uint64_t> localCapacityLoss{0};
    std::atomic<std::uint64_t> maximumIncomingDepth{0};
    std::atomic<std::uint64_t> maximumOutgoingDepth{0};
    std::atomic<std::uint64_t> maximumHarvestMicroseconds{0};

    static void UpdateMaximum(
        std::atomic<std::uint64_t>* destination,
        std::uint64_t value) {
        auto observed = destination->load(std::memory_order_relaxed);
        while (value > observed &&
               !destination->compare_exchange_weak(
                   observed,
                   value,
                   std::memory_order_relaxed,
                   std::memory_order_relaxed)) {
        }
    }

    void Publish(brain::BrainXPilot4Observation observation) {
        std::lock_guard<std::mutex> lock(incomingMutex);
        if (incoming.size() >= kIncomingCapacity) {
            ++pendingLostCount;
            if (pendingFirstLostSequence == 0) {
                pendingFirstLostSequence = observation.sourceSequence;
            }
            pendingLastLostSequence = observation.sourceSequence;
            localCapacityLoss.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        incoming.push_back(std::move(observation));
        observationsPublished.fetch_add(1, std::memory_order_relaxed);
        UpdateMaximum(&maximumIncomingDepth, incoming.size());
    }

    void MaterializeLossLocked() {
        if (pendingLostCount == 0 || incoming.size() >= kIncomingCapacity) return;
        brain::BrainXPilot4Observation loss;
        loss.kind = brain::BrainXPilot4EventKind::LocalCapacityLoss;
        loss.receivedUnixMicroseconds = CurrentUnixMicroseconds();
        loss.lostCount = pendingLostCount;
        loss.firstLostSequence = pendingFirstLostSequence;
        loss.lastLostSequence = pendingLastLostSequence;
        loss.reason = "xvatsim-xpilot4-incoming-queue-capacity";
        incoming.push_back(std::move(loss));
        observationsPublished.fetch_add(1, std::memory_order_relaxed);
        pendingLostCount = 0;
        pendingFirstLostSequence = 0;
        pendingLastLostSequence = 0;
    }

    void PublishTransport(brain::BrainXPilot4EventKind kind, std::string reason) {
        brain::BrainXPilot4Observation observation;
        observation.kind = kind;
        observation.receivedUnixMicroseconds = CurrentUnixMicroseconds();
        observation.reason = std::move(reason);
        Publish(std::move(observation));
    }

    bool WriteExact(
        HANDLE pipe,
        const std::uint8_t* bytes,
        std::size_t count) {
        HANDLE completion = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (completion == nullptr) return false;
        std::size_t offset = 0;
        bool success = true;
        while (success && offset < count) {
            OVERLAPPED overlapped{};
            overlapped.hEvent = completion;
            ResetEvent(completion);
            DWORD written = 0;
            const auto request = static_cast<DWORD>(std::min<std::size_t>(
                count - offset,
                std::numeric_limits<DWORD>::max()));
            if (!WriteFile(
                    pipe,
                    bytes + offset,
                    request,
                    &written,
                    &overlapped)) {
                if (GetLastError() != ERROR_IO_PENDING) {
                    success = false;
                    break;
                }
                const HANDLE events[]{stopEvent, completion};
                const auto wait = WaitForMultipleObjects(2, events, FALSE, INFINITE);
                if (wait == WAIT_OBJECT_0) {
                    CancelIoEx(pipe, &overlapped);
                    success = false;
                    break;
                }
                if (wait != WAIT_OBJECT_0 + 1 ||
                    !GetOverlappedResult(pipe, &overlapped, &written, FALSE)) {
                    success = false;
                    break;
                }
            }
            if (written == 0) {
                success = false;
                break;
            }
            offset += written;
        }
        CloseHandle(completion);
        return success;
    }

    bool FlushCommands(HANDLE pipe) {
        for (;;) {
            brain::BrainXPilot4Command command;
            {
                std::lock_guard<std::mutex> lock(outgoingMutex);
                if (outgoing.empty()) return true;
                command = outgoing.front();
            }
            std::array<std::uint8_t, kHeaderBytes + 8> frame{};
            WriteU32(frame.data(), kMagic);
            WriteU16(frame.data() + 4, brain::kXPilot4BridgeProtocolVersion);
            WriteU16(
                frame.data() + 6,
                static_cast<std::uint16_t>(command.kind));
            WriteU32(frame.data() + 8, 8);
            WriteU64(frame.data() + 28, command.requestId);
            WriteU64(
                frame.data() + 36,
                static_cast<std::uint64_t>(CurrentUnixMicroseconds()));
            WriteU64(frame.data() + kHeaderBytes, command.requestId);
            if (!WriteExact(pipe, frame.data(), frame.size())) return false;
            {
                std::lock_guard<std::mutex> lock(outgoingMutex);
                if (!outgoing.empty() &&
                    outgoing.front().requestId == command.requestId) {
                    outgoing.pop_front();
                }
            }
            commandsWritten.fetch_add(1, std::memory_order_relaxed);
        }
    }

    bool ReadExact(HANDLE pipe, std::uint8_t* bytes, std::size_t count) {
        HANDLE completion = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (completion == nullptr) return false;
        std::size_t offset = 0;
        bool success = true;
        while (success && offset < count) {
            OVERLAPPED overlapped{};
            overlapped.hEvent = completion;
            ResetEvent(completion);
            DWORD read = 0;
            const auto request = static_cast<DWORD>(std::min<std::size_t>(
                count - offset,
                std::numeric_limits<DWORD>::max()));
            if (!ReadFile(pipe, bytes + offset, request, &read, &overlapped)) {
                if (GetLastError() != ERROR_IO_PENDING) {
                    success = false;
                    break;
                }
                bool pending = true;
                while (pending) {
                    const HANDLE events[]{stopEvent, completion, commandEvent};
                    const auto wait =
                        WaitForMultipleObjects(3, events, FALSE, INFINITE);
                    if (wait == WAIT_OBJECT_0) {
                        CancelIoEx(pipe, &overlapped);
                        success = false;
                        pending = false;
                    } else if (wait == WAIT_OBJECT_0 + 1) {
                        if (!GetOverlappedResult(
                                pipe, &overlapped, &read, FALSE)) {
                            success = false;
                        }
                        pending = false;
                    } else if (wait == WAIT_OBJECT_0 + 2) {
                        if (!FlushCommands(pipe)) {
                            CancelIoEx(pipe, &overlapped);
                            success = false;
                            pending = false;
                        }
                    } else {
                        CancelIoEx(pipe, &overlapped);
                        success = false;
                        pending = false;
                    }
                }
            }
            if (!success || read == 0) {
                success = false;
                break;
            }
            offset += read;
        }
        CloseHandle(completion);
        return success;
    }

    bool ConsumeChunk(
        const FrameHeader& header,
        const std::vector<std::uint8_t>& payload,
        std::optional<std::pair<FrameHeader, std::vector<std::uint8_t>>>* complete) {
        if (complete == nullptr || payload.size() < 14) return false;
        const auto originalKind = static_cast<brain::BrainXPilot4EventKind>(
            ReadU16(payload.data()));
        const auto index = ReadU32(payload.data() + 2);
        const auto count = ReadU32(payload.data() + 6);
        const auto total = ReadU32(payload.data() + 10);
        if (count < 2 || index >= count || total > kMaximumObservationBytes) {
            return false;
        }
        if (index == 0) {
            chunks = {};
            chunks.active = true;
            chunks.header = header;
            chunks.originalKind = originalKind;
            chunks.nextIndex = 0;
            chunks.chunkCount = count;
            chunks.totalBytes = total;
            chunks.bytes.reserve(total);
        }
        if (!chunks.active || index != chunks.nextIndex ||
            chunks.originalKind != originalKind || chunks.chunkCount != count ||
            chunks.totalBytes != total ||
            chunks.header.processEpoch != header.processEpoch ||
            chunks.header.connectionEpoch != header.connectionEpoch ||
            chunks.header.sequence != header.sequence ||
            payload.size() - 14 > total - chunks.bytes.size()) {
            chunks = {};
            return false;
        }
        chunks.bytes.insert(chunks.bytes.end(), payload.begin() + 14, payload.end());
        ++chunks.nextIndex;
        if (chunks.nextIndex == chunks.chunkCount) {
            if (chunks.bytes.size() != chunks.totalBytes) {
                chunks = {};
                return false;
            }
            auto completeHeader = chunks.header;
            completeHeader.kind = chunks.originalKind;
            completeHeader.payloadLength = chunks.totalBytes;
            complete->emplace(
                std::move(completeHeader),
                std::move(chunks.bytes));
            chunks = {};
        }
        return true;
    }

    bool ReadObservation(
        HANDLE pipe,
        std::optional<brain::BrainXPilot4Observation>* observation,
        bool* corrupt) {
        if (corrupt != nullptr) *corrupt = false;
        std::array<std::uint8_t, kHeaderBytes> headerBytes{};
        if (!ReadExact(pipe, headerBytes.data(), headerBytes.size())) return false;
        FrameHeader header;
        if (!DecodeHeader(headerBytes, &header)) {
            if (corrupt != nullptr) *corrupt = true;
            return false;
        }
        std::vector<std::uint8_t> payload(header.payloadLength);
        if (!payload.empty() && !ReadExact(pipe, payload.data(), payload.size())) {
            return false;
        }
        framesReceived.fetch_add(1, std::memory_order_relaxed);

        if (static_cast<std::uint16_t>(header.kind) ==
                static_cast<std::uint16_t>(
                    brain::BrainXPilot4CommandKind::RequestControllerSnapshot) ||
            header.kind == brain::BrainXPilot4EventKind::TransportConnected ||
            header.kind == brain::BrainXPilot4EventKind::TransportDisconnected ||
            header.kind == brain::BrainXPilot4EventKind::LocalCapacityLoss) {
            if (corrupt != nullptr) *corrupt = true;
            return false;
        }

        if (static_cast<std::uint16_t>(header.kind) == 200U) {
            std::optional<std::pair<FrameHeader, std::vector<std::uint8_t>>> complete;
            if (!ConsumeChunk(header, payload, &complete)) {
                if (corrupt != nullptr) *corrupt = true;
                return false;
            }
            if (!complete.has_value()) {
                observation->reset();
                return true;
            }
            header = std::move(complete->first);
            payload = std::move(complete->second);
        } else if (chunks.active) {
            chunks = {};
            if (corrupt != nullptr) *corrupt = true;
            return false;
        }

        brain::BrainXPilot4Observation decoded;
        if (!DecodeObservation(header, payload, &decoded)) {
            if (corrupt != nullptr) *corrupt = true;
            return false;
        }
        observation->emplace(std::move(decoded));
        return true;
    }

    void Run() {
        while (WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0) {
            connectAttempts.fetch_add(1, std::memory_order_relaxed);
            const std::wstring fullName = L"\\\\.\\pipe\\" + pipeName;
            HANDLE pipe = CreateFileW(
                fullName.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED,
                nullptr);
            if (pipe == INVALID_HANDLE_VALUE) {
                if (WaitForSingleObject(stopEvent, kReconnectMilliseconds) ==
                    WAIT_OBJECT_0) {
                    break;
                }
                continue;
            }

            connected.store(true, std::memory_order_release);
            connections.fetch_add(1, std::memory_order_relaxed);
            PublishTransport(
                brain::BrainXPilot4EventKind::TransportConnected,
                "xpilot4-local-pipe-connected");
            bool healthy = FlushCommands(pipe);
            while (healthy &&
                   WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0) {
                std::optional<brain::BrainXPilot4Observation> observation;
                bool corrupt = false;
                healthy = ReadObservation(pipe, &observation, &corrupt);
                if (healthy && observation.has_value()) {
                    Publish(std::move(*observation));
                }
                if (!healthy && corrupt) {
                    corruptFrames.fetch_add(1, std::memory_order_relaxed);
                    PublishTransport(
                        brain::BrainXPilot4EventKind::CorruptEnvelope,
                        "xpilot4-local-pipe-frame-rejected");
                }
            }
            CancelIoEx(pipe, nullptr);
            CloseHandle(pipe);
            chunks = {};
            connected.store(false, std::memory_order_release);
            disconnects.fetch_add(1, std::memory_order_relaxed);
            PublishTransport(
                brain::BrainXPilot4EventKind::TransportDisconnected,
                "xpilot4-local-pipe-disconnected");
        }
        connected.store(false, std::memory_order_release);
        running.store(false, std::memory_order_release);
    }
};

XPilot4BridgeClient::XPilot4BridgeClient()
    : implementation_(std::make_unique<Implementation>()) {}

XPilot4BridgeClient::~XPilot4BridgeClient() {
    Stop();
}

bool XPilot4BridgeClient::Start(std::wstring pipeName) {
    auto& state = *implementation_;
    bool expected = false;
    if (!state.running.compare_exchange_strong(expected, true)) return true;
    state.pipeName = std::move(pipeName);
    state.stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    state.commandEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (state.stopEvent == nullptr || state.commandEvent == nullptr) {
        if (state.stopEvent != nullptr) CloseHandle(state.stopEvent);
        if (state.commandEvent != nullptr) CloseHandle(state.commandEvent);
        state.stopEvent = nullptr;
        state.commandEvent = nullptr;
        state.running.store(false, std::memory_order_release);
        return false;
    }
    try {
        state.worker = std::thread([&state]() { state.Run(); });
    } catch (...) {
        CloseHandle(state.stopEvent);
        CloseHandle(state.commandEvent);
        state.stopEvent = nullptr;
        state.commandEvent = nullptr;
        state.running.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

void XPilot4BridgeClient::Stop() {
    auto& state = *implementation_;
    if (state.stopEvent != nullptr) SetEvent(state.stopEvent);
    if (state.commandEvent != nullptr) SetEvent(state.commandEvent);
    if (state.worker.joinable()) state.worker.join();
    if (state.stopEvent != nullptr) CloseHandle(state.stopEvent);
    if (state.commandEvent != nullptr) CloseHandle(state.commandEvent);
    state.stopEvent = nullptr;
    state.commandEvent = nullptr;
    state.running.store(false, std::memory_order_release);
}

bool XPilot4BridgeClient::TryHarvestXPilot4Observation(
    brain::BrainXPilot4Observation* observation) {
    if (observation == nullptr) return false;
    const auto started = std::chrono::steady_clock::now();
    auto& state = *implementation_;
    bool harvested = false;
    {
        std::lock_guard<std::mutex> lock(state.incomingMutex);
        if (!state.incoming.empty()) {
            *observation = std::move(state.incoming.front());
            state.incoming.pop_front();
            state.MaterializeLossLocked();
            harvested = true;
        }
    }
    const auto elapsed = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count());
    Implementation::UpdateMaximum(&state.maximumHarvestMicroseconds, elapsed);
    return harvested;
}

bool XPilot4BridgeClient::TrySubmitXPilot4Command(
    const brain::BrainXPilot4Command& command) {
    auto& state = *implementation_;
    {
        std::lock_guard<std::mutex> lock(state.outgoingMutex);
        if (!state.running.load(std::memory_order_acquire) ||
            state.outgoing.size() >= kOutgoingCapacity) {
            return false;
        }
        state.outgoing.push_back(command);
        state.commandsSubmitted.fetch_add(1, std::memory_order_relaxed);
        Implementation::UpdateMaximum(
            &state.maximumOutgoingDepth, state.outgoing.size());
    }
    if (state.commandEvent != nullptr) SetEvent(state.commandEvent);
    return true;
}

brain::BrainXPilot4TransportSnapshot
XPilot4BridgeClient::XPilot4TransportSnapshot() const {
    const auto& state = *implementation_;
    brain::BrainXPilot4TransportSnapshot snapshot;
    snapshot.running = state.running.load(std::memory_order_acquire);
    snapshot.connected = state.connected.load(std::memory_order_acquire);
    snapshot.connectAttempts = state.connectAttempts.load(std::memory_order_relaxed);
    snapshot.connections = state.connections.load(std::memory_order_relaxed);
    snapshot.disconnects = state.disconnects.load(std::memory_order_relaxed);
    snapshot.framesReceived = state.framesReceived.load(std::memory_order_relaxed);
    snapshot.observationsPublished =
        state.observationsPublished.load(std::memory_order_relaxed);
    snapshot.commandsSubmitted =
        state.commandsSubmitted.load(std::memory_order_relaxed);
    snapshot.commandsWritten =
        state.commandsWritten.load(std::memory_order_relaxed);
    snapshot.corruptFrames = state.corruptFrames.load(std::memory_order_relaxed);
    snapshot.localCapacityLoss =
        state.localCapacityLoss.load(std::memory_order_relaxed);
    snapshot.maximumIncomingDepth =
        state.maximumIncomingDepth.load(std::memory_order_relaxed);
    snapshot.maximumOutgoingDepth =
        state.maximumOutgoingDepth.load(std::memory_order_relaxed);
    snapshot.maximumHarvestMicroseconds =
        state.maximumHarvestMicroseconds.load(std::memory_order_relaxed);
    return snapshot;
}

}  // namespace xvatsim::modules::runtime_workers
