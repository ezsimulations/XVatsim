#include "XVatsim/modules/runtime_workers/PdcLogReader.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <charconv>
#include <limits>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <ShlObj.h>
#endif

namespace xvatsim::modules::runtime_workers {
namespace {

using xvatsim::brain::BrainPdcLogChannel;
using xvatsim::brain::BrainPdcLogFact;
using xvatsim::brain::BrainPdcLogFileFact;
using xvatsim::brain::BrainPdcLogIssue;
using xvatsim::brain::BrainPdcLogOperation;
using xvatsim::brain::BrainPdcLogRecordFact;
using xvatsim::brain::BrainPdcLogRecordKind;

bool EqualAsciiCaseInsensitive(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto upper = [](unsigned char value) {
            return static_cast<unsigned char>(std::toupper(value));
        };
        if (upper(static_cast<unsigned char>(left[index])) !=
            upper(static_cast<unsigned char>(right[index]))) return false;
    }
    return true;
}

std::string_view TakeField(std::string_view* remaining) {
    const auto separator = remaining->find(':');
    const auto field = remaining->substr(0, separator);
    *remaining = separator == std::string_view::npos
        ? std::string_view{}
        : remaining->substr(separator + 1);
    return field;
}

bool ValidTimestamp(std::string_view line) {
    if (line.size() < 14 || line.front() != '[' || line[13] != ']') return false;
    for (std::size_t index = 1; index < 13; ++index) {
        const char separator = index == 3 || index == 6
            ? ':'
            : (index == 9 ? '.' : '\0');
        if (separator != '\0') {
            if (line[index] != separator) return false;
        } else if (line[index] < '0' || line[index] > '9') {
            return false;
        }
    }
    return true;
}

bool ValidUtf8(std::string_view text) {
#if defined(_WIN32)
    if (text.empty()) return true;
    return MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0) > 0;
#else
    (void)text;
    return true;
#endif
}

bool MatchesXPilotLogFilename(std::string_view name) {
    constexpr std::string_view prefix = "NetworkLog-";
    constexpr std::string_view suffix = ".txt";
    if (name.size() != 30 || name.substr(0, prefix.size()) != prefix ||
        name.substr(name.size() - suffix.size()) != suffix || name[19] != '-') {
        return false;
    }
    for (std::size_t index = prefix.size(); index < name.size() - suffix.size();
         ++index) {
        if (index == 19) continue;
        if (name[index] < '0' || name[index] > '9') return false;
    }
    return true;
}

#if defined(_WIN32)

std::uint64_t FileTimeTicks(const FILETIME& value) {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32U) |
        static_cast<std::uint64_t>(value.dwLowDateTime);
}

class SharedReadHandle {
public:
    explicit SharedReadHandle(const std::filesystem::path& path)
        : handle_(CreateFileW(
              path.c_str(),
              GENERIC_READ,
              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
              nullptr,
              OPEN_EXISTING,
              FILE_ATTRIBUTE_NORMAL,
              nullptr)) {}

    ~SharedReadHandle() {
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
    }

    SharedReadHandle(const SharedReadHandle&) = delete;
    SharedReadHandle& operator=(const SharedReadHandle&) = delete;

    HANDLE get() const { return handle_; }
    bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

bool ReadFileMetadata(
    HANDLE handle,
    const std::filesystem::path& path,
    BrainPdcLogFileFact* fact) {
    if (fact == nullptr || handle == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(handle, &information) ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return false;
    }
    fact->path = path.u8string();
    fact->volumeSerial = information.dwVolumeSerialNumber;
    fact->fileIndex =
        (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32U) |
        information.nFileIndexLow;
    fact->creationTicks = FileTimeTicks(information.ftCreationTime);
    fact->modificationTicks = FileTimeTicks(information.ftLastWriteTime);
    fact->sizeBytes =
        (static_cast<std::uint64_t>(information.nFileSizeHigh) << 32U) |
        information.nFileSizeLow;
    fact->modifiedUnixSeconds = static_cast<std::int64_t>(
        fact->modificationTicks / 10'000'000ULL) - 11'644'473'600LL;
    fact->metadataAvailable = true;
    return true;
}

bool CurrentThreadCpuTicks(std::uint64_t* ticks) {
    if (ticks == nullptr) return false;
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user)) {
        return false;
    }
    *ticks = FileTimeTicks(kernel) + FileTimeTicks(user);
    return true;
}

std::filesystem::path DefaultXPilotLogDirectory() {
    PWSTR localAppData = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(
            FOLDERID_LocalAppData,
            KF_FLAG_DONT_VERIFY,
            nullptr,
            &localAppData)) && localAppData != nullptr) {
        result = std::filesystem::path(localAppData) /
            L"org.vatsim.xpilot" / L"NetworkLogs";
    }
    if (localAppData != nullptr) CoTaskMemFree(localAppData);
    return result;
}

#endif

}  // namespace

bool ParseXPilotNetworkLogLine(
    std::string_view line,
    std::uint64_t beginOffset,
    std::uint64_t endOffset,
    BrainPdcLogRecordFact* fact) {
    if (fact == nullptr) return false;
    *fact = {};
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.size() < 22 || !ValidTimestamp(line) || line[14] != ' ' ||
        line[18] != ' ' ||
        (line.substr(15, 3) != "<<<" && line.substr(15, 3) != ">>>")) {
        return false;
    }

    auto packet = line.substr(19);
    if (packet.size() < 3) return false;
    const auto packetType = packet.substr(0, 3);
    if (packetType != "#TM" && packetType != "#AP" &&
        packetType != "#AA" && packetType != "#DP" &&
        packetType != "$ID") {
        return false;
    }

    fact->incoming = line.substr(15, 3) == "<<<";
    fact->beginOffset = beginOffset;
    fact->endOffset = endOffset;
    fact->timeUtc.assign(line.substr(1, 12));
    packet.remove_prefix(3);

    const auto senderSeparator = packet.find(':');
    if (senderSeparator == std::string_view::npos) return true;
    const auto sender = TakeField(&packet);
    if (sender.size() <= 64) fact->sender.assign(sender);

    if (packetType == "#AP" || packetType == "#AA" || packetType == "#DP") {
        fact->kind = packetType == "#DP"
            ? BrainPdcLogRecordKind::SessionClosed
            : BrainPdcLogRecordKind::SessionOpened;
        fact->fieldsComplete = !sender.empty() && sender.size() <= 64;
        return true;
    }

    if (packetType == "$ID") {
        fact->kind = BrainPdcLogRecordKind::ClientIdentity;
        (void)TakeField(&packet);
        (void)TakeField(&packet);
        const auto clientName = TakeField(&packet);
        if (clientName.size() <= 64) fact->clientName.assign(clientName);
        fact->fieldsComplete = !sender.empty() && sender.size() <= 64 &&
            !clientName.empty() && clientName.size() <= 64;
        return true;
    }

    fact->kind = BrainPdcLogRecordKind::Text;
    const bool bodyFieldPresent = packet.find(':') != std::string_view::npos;
    const auto recipient = TakeField(&packet);
    if (recipient.size() <= 64) fact->recipient.assign(recipient);
    fact->fieldsComplete = bodyFieldPresent && !sender.empty() &&
        sender.size() <= 64 && !recipient.empty() && recipient.size() <= 64;
    if (EqualAsciiCaseInsensitive(sender, "SERVER")) {
        fact->channel = BrainPdcLogChannel::Server;
    } else if (recipient == "*") {
        fact->channel = BrainPdcLogChannel::Broadcast;
    } else if (recipient == "*s") {
        fact->channel = BrainPdcLogChannel::Wallop;
    } else if (!recipient.empty() && recipient.front() == '@') {
        fact->channel = BrainPdcLogChannel::Radio;
    } else {
        fact->channel = BrainPdcLogChannel::Direct;
    }
    fact->originalBodyBytes = static_cast<std::uint32_t>(std::min<std::size_t>(
        packet.size(), std::numeric_limits<std::uint32_t>::max()));
    fact->bodyWithinLimits = packet.size() <= 4096;
    fact->utf8Valid = ValidUtf8(sender) && ValidUtf8(recipient) && ValidUtf8(packet);
    if (fact->bodyWithinLimits && fact->utf8Valid) fact->body.assign(packet);
    return true;
}

PdcLogReader::PdcLogReader(std::filesystem::path directoryOverride)
    : directoryOverride_(std::move(directoryOverride)) {}

BrainPdcLogFact PdcLogReader::Read(
    const brain::BrainPdcLogRequest& request) const noexcept {
    const auto wallStarted = std::chrono::steady_clock::now();
    std::uint64_t cpuStarted = 0;
#if defined(_WIN32)
    const bool cpuStartedAvailable = CurrentThreadCpuTicks(&cpuStarted);
#else
    const bool cpuStartedAvailable = false;
#endif
    BrainPdcLogFact result;
    result.request = request;
    result.nextOffset = request.beginOffset;
    result.discardLeadingFragment = request.discardLeadingFragment;
    result.observedUnixSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    try {
#if defined(_WIN32)
        if (request.operation == BrainPdcLogOperation::Discover) {
            const auto directory = directoryOverride_.empty()
                ? DefaultXPilotLogDirectory()
                : directoryOverride_;
            if (directory.empty()) {
                result.issue = BrainPdcLogIssue::DirectoryUnavailable;
            } else {
                std::error_code error;
                std::vector<std::filesystem::path> candidates;
                std::filesystem::directory_iterator iterator(directory, error);
                const std::filesystem::directory_iterator end;
                std::size_t visited = 0;
                while (!error && iterator != end) {
                    ++visited;
                    const auto path = iterator->path();
                    if (MatchesXPilotLogFilename(path.filename().u8string())) {
                        candidates.push_back(path);
                    }
                    iterator.increment(error);
                    if (visited >= 256) {
                        result.issue = BrainPdcLogIssue::DiscoveryLimitReached;
                        break;
                    }
                }
                if (error) {
                    result.issue = BrainPdcLogIssue::DirectoryUnavailable;
                } else {
                    std::sort(candidates.begin(), candidates.end(),
                        [](const auto& left, const auto& right) {
                            return left.filename().u8string() >
                                right.filename().u8string();
                        });
                    if (candidates.size() > brain::kPdcLogMaximumFileCandidates) {
                        candidates.resize(brain::kPdcLogMaximumFileCandidates);
                        if (result.issue == BrainPdcLogIssue::None) {
                            result.issue = BrainPdcLogIssue::DiscoveryLimitReached;
                        }
                    }
                    for (const auto& path : candidates) {
                        BrainPdcLogFileFact file;
                        file.path = path.u8string();
                        SharedReadHandle handle(path);
                        if (handle.valid()) {
                            (void)ReadFileMetadata(handle.get(), path, &file);
                        }
                        result.files.push_back(std::move(file));
                    }
                }
            }
        } else if (request.endOffset < request.beginOffset ||
                   request.file.path.empty() || request.file.path.size() > 4096) {
            result.issue = BrainPdcLogIssue::InvalidRequest;
        } else {
            const auto path = std::filesystem::u8path(request.file.path);
            SharedReadHandle handle(path);
            BrainPdcLogFileFact before;
            if (!handle.valid() || !ReadFileMetadata(handle.get(), path, &before)) {
                result.issue = BrainPdcLogIssue::FileOpenFailed;
            } else if (!brain::SameBrainPdcLogFile(before, request.file) ||
                       before.sizeBytes < request.endOffset) {
                result.issue = BrainPdcLogIssue::FileIdentityChanged;
                result.fileAfter = before;
            } else {
                const auto requestedBytes = request.endOffset - request.beginOffset;
                const auto boundedBytes = static_cast<DWORD>(std::min<std::uint64_t>(
                    requestedBytes, brain::kPdcLogMaximumReadBytes));
                std::vector<char> bytes(boundedBytes);
                LARGE_INTEGER position{};
                position.QuadPart = static_cast<LONGLONG>(request.beginOffset);
                DWORD bytesRead = 0;
                if (!SetFilePointerEx(handle.get(), position, nullptr, FILE_BEGIN) ||
                    (boundedBytes != 0 && !ReadFile(
                        handle.get(), bytes.data(), boundedBytes, &bytesRead, nullptr))) {
                    result.issue = BrainPdcLogIssue::ReadFailed;
                } else {
                    result.bytesRead = bytesRead;
                    const std::string_view block(bytes.data(), bytesRead);
                    std::size_t lineBegin = 0;
                    while (lineBegin < block.size() &&
                           result.records.size() <
                               brain::kPdcLogMaximumRecordsPerFact) {
                        const auto newline = block.find('\n', lineBegin);
                        if (newline == std::string_view::npos) {
                            if (result.discardLeadingFragment ||
                                block.size() - lineBegin >=
                                    brain::kPdcLogMaximumLineBytes) {
                                if (!result.discardLeadingFragment) {
                                    ++result.oversizedLines;
                                }
                                result.discardLeadingFragment = true;
                                result.nextOffset = request.beginOffset + block.size();
                            } else {
                                result.partialLine = true;
                            }
                            break;
                        }

                        const auto nextOffset = request.beginOffset + newline + 1;
                        ++result.completeLines;
                        if (result.discardLeadingFragment) {
                            result.discardLeadingFragment = false;
                        } else if (newline - lineBegin >=
                                   brain::kPdcLogMaximumLineBytes) {
                            ++result.oversizedLines;
                        } else {
                            BrainPdcLogRecordFact record;
                            const auto line = block.substr(
                                lineBegin, newline - lineBegin);
                            if (ParseXPilotNetworkLogLine(
                                    line,
                                    request.beginOffset + lineBegin,
                                    nextOffset,
                                    &record)) {
                                result.records.push_back(std::move(record));
                            } else if (line.size() >= 19 && ValidTimestamp(line)) {
                                ++result.ignoredProtocolLines;
                            } else {
                                ++result.malformedLines;
                            }
                        }
                        result.nextOffset = nextOffset;
                        lineBegin = newline + 1;
                    }

                    result.reachedRequestedEnd =
                        result.nextOffset == request.endOffset;
                    if (!ReadFileMetadata(handle.get(), path, &result.fileAfter) ||
                        !brain::SameBrainPdcLogFile(before, result.fileAfter) ||
                        result.fileAfter.sizeBytes < request.endOffset) {
                        result.issue = BrainPdcLogIssue::FileIdentityChanged;
                        result.records.clear();
                    }
                }
            }
        }
#else
        (void)request;
        result.issue = BrainPdcLogIssue::DirectoryUnavailable;
#endif
    } catch (...) {
        result.issue = BrainPdcLogIssue::ResourceFailure;
        result.files.clear();
        result.records.clear();
    }

#if defined(_WIN32)
    std::uint64_t cpuFinished = 0;
    result.workerCpuAvailable = cpuStartedAvailable &&
        CurrentThreadCpuTicks(&cpuFinished) && cpuFinished >= cpuStarted;
    if (result.workerCpuAvailable) {
        result.workerCpuMicroseconds = (cpuFinished - cpuStarted) / 10ULL;
    }
#endif
    result.workerWallMicroseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - wallStarted).count());
    return result;
}

}  // namespace xvatsim::modules::runtime_workers
