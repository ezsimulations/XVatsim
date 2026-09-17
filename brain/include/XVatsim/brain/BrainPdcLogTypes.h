#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xvatsim::brain {

inline constexpr std::size_t kPdcLogMaximumReadBytes = 256U * 1024U;
inline constexpr std::size_t kPdcLogMaximumLineBytes = 8192U;
inline constexpr std::size_t kPdcLogMaximumRecordsPerFact = 32U;
inline constexpr std::size_t kPdcLogMaximumFileCandidates = 16U;

enum class BrainPdcLogOperation {
    Discover,
    ReadRange,
};

enum class BrainPdcLogIssue {
    None,
    DirectoryUnavailable,
    DiscoveryLimitReached,
    FileOpenFailed,
    FileIdentityChanged,
    ReadFailed,
    InvalidRequest,
    ResourceFailure,
};

enum class BrainPdcLogRecordKind {
    SessionOpened,
    SessionClosed,
    ClientIdentity,
    Text,
};

enum class BrainPdcLogChannel {
    Direct,
    Radio,
    Broadcast,
    Wallop,
    Server,
};

struct BrainPdcLogFileFact {
    bool metadataAvailable = false;
    std::string path;
    std::uint64_t volumeSerial = 0;
    std::uint64_t fileIndex = 0;
    std::uint64_t creationTicks = 0;
    std::uint64_t modificationTicks = 0;
    std::uint64_t sizeBytes = 0;
    std::int64_t modifiedUnixSeconds = 0;
};

inline bool SameBrainPdcLogFile(
    const BrainPdcLogFileFact& left,
    const BrainPdcLogFileFact& right) {
    return left.volumeSerial == right.volumeSerial &&
        left.fileIndex == right.fileIndex &&
        left.creationTicks == right.creationTicks;
}

struct BrainPdcLogRecordFact {
    BrainPdcLogRecordKind kind = BrainPdcLogRecordKind::Text;
    BrainPdcLogChannel channel = BrainPdcLogChannel::Direct;
    bool incoming = false;
    bool fieldsComplete = false;
    bool bodyWithinLimits = false;
    bool utf8Valid = false;
    std::uint64_t beginOffset = 0;
    std::uint64_t endOffset = 0;
    std::uint32_t originalBodyBytes = 0;
    std::string timeUtc;
    std::string sender;
    std::string recipient;
    std::string body;
    std::string clientName;
};

struct BrainPdcLogRequest {
    BrainPdcLogOperation operation = BrainPdcLogOperation::Discover;
    std::uint64_t requestId = 0;
    std::uint64_t productEpoch = 0;
    std::uint64_t connectionEpoch = 0;
    BrainPdcLogFileFact file;
    std::uint64_t beginOffset = 0;
    std::uint64_t endOffset = 0;
    bool discardLeadingFragment = false;
};

struct BrainPdcLogFact {
    BrainPdcLogRequest request;
    BrainPdcLogIssue issue = BrainPdcLogIssue::None;
    std::vector<BrainPdcLogFileFact> files;
    BrainPdcLogFileFact fileAfter;
    std::vector<BrainPdcLogRecordFact> records;
    std::uint64_t nextOffset = 0;
    std::uint64_t bytesRead = 0;
    std::uint64_t completeLines = 0;
    std::uint64_t ignoredProtocolLines = 0;
    std::uint64_t malformedLines = 0;
    std::uint64_t oversizedLines = 0;
    bool discardLeadingFragment = false;
    bool partialLine = false;
    bool reachedRequestedEnd = false;
    std::int64_t observedUnixSeconds = 0;
    std::uint64_t workerWallMicroseconds = 0;
    std::uint64_t workerCpuMicroseconds = 0;
    bool workerCpuAvailable = false;
};

class BrainPdcLogTransport {
public:
    virtual ~BrainPdcLogTransport() = default;
    virtual bool TrySubmitPdcLogRequest(
        const BrainPdcLogRequest& request) = 0;
    virtual bool TryHarvestPdcLogFact(BrainPdcLogFact* fact) = 0;
};

}  // namespace xvatsim::brain
