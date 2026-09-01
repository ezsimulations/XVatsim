#pragma once

#include "XVatsim/brain/BrainTypes.h"
#include "XPLMDataAccess.h"
#include "XPLMPlugin.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <string>

namespace xvatsim::modules::xpilot_bridge {

enum class XPilotPrivateConnectionStatus {
    Unknown,
    Disconnected,
    Connecting,
    Connected,
};

enum class XPilotPrivateDataKind {
    Unknown,
    Integer,
    Bytes,
};

enum class XPilotPrivateMechanicalIssue : std::uint64_t {
    None = 0,
    PluginMissing = 1ULL << 0U,
    SignatureMismatch = 1ULL << 1U,
    VersionMismatch = 1ULL << 2U,
    BinaryFingerprintMismatch = 1ULL << 3U,
    CapabilityMissing = 1ULL << 4U,
    SequenceTypeMismatch = 1ULL << 5U,
    SenderTypeMismatch = 1ULL << 6U,
    BodyTypeMismatch = 1ULL << 7U,
    SourceChanged = 1ULL << 8U,
    SequenceReadFailed = 1ULL << 9U,
    SequenceTorn = 1ULL << 10U,
    SequenceInvalid = 1ULL << 11U,
    StatusChanged = 1ULL << 12U,
    CallsignChanged = 1ULL << 13U,
    SenderReadFailed = 1ULL << 14U,
    BodyReadFailed = 1ULL << 15U,
    SenderEmpty = 1ULL << 16U,
    BodyEmpty = 1ULL << 17U,
    SenderOverLimit = 1ULL << 18U,
    BodyOverLimit = 1ULL << 19U,
    SenderInvalidUtf8 = 1ULL << 20U,
    BodyInvalidUtf8 = 1ULL << 21U,
    SenderUnsafeControl = 1ULL << 22U,
    BodyUnsafeControl = 1ULL << 23U,
    PluginPathMissing = 1ULL << 24U,
    PluginPathCanonicalizationFailed = 1ULL << 25U,
    PluginFileUnreadable = 1ULL << 26U,
    PluginTargetNotFile = 1ULL << 27U,
    BinarySizeMismatch = 1ULL << 28U,
};

enum class XPilotPrivateAdvisoryIssue : std::uint64_t {
    None = 0,
    VersionDataRefMissing = 1ULL << 0U,
    VersionDataRefTypeMismatch = 1ULL << 1U,
    VersionValueMismatch = 1ULL << 2U,
};

constexpr std::uint64_t XPilotPrivateIssueBit(
    XPilotPrivateMechanicalIssue issue) {
    return static_cast<std::uint64_t>(issue);
}

constexpr std::uint64_t XPilotPrivateAdvisoryBit(
    XPilotPrivateAdvisoryIssue issue) {
    return static_cast<std::uint64_t>(issue);
}

const char* XPilotPrivateMechanicalIssueName(
    XPilotPrivateMechanicalIssue issue);
std::string XPilotPrivateMechanicalIssueNames(std::uint64_t mask);
const char* XPilotPrivateAdvisoryIssueName(XPilotPrivateAdvisoryIssue issue);
std::string XPilotPrivateAdvisoryIssueNames(std::uint64_t mask);

struct XPilotPrivateSourceIdentity {
    bool pluginPresent = false;
    bool signatureQualified = false;
    bool versionQualified = false;
    bool binaryFingerprintQualified = false;
    std::uint64_t pluginInstanceIdentity = 0;
    std::uint64_t capabilityGeneration = 0;
    std::string signature;
    std::string version;
    std::string binarySha256;
    bool pluginPathPresent = false;
    bool pluginPathCanonicalized = false;
    bool pluginFileReadable = false;
    bool pluginTargetRegularFile = false;
    bool binarySizeQualified = false;
    bool versionDataRefPresent = false;
    bool versionDataRefTypeQualified = false;
    bool versionValueQualified = false;
    std::uint64_t binarySize = 0;
    std::uint64_t fatalIssueMask = 0;
    std::uint64_t advisoryIssueMask = 0;
    std::string canonicalPluginPath;
};

struct XPilotPrivatePlatformPrimitives {
    bool pluginPresent = false;
    std::uint64_t pluginInstanceIdentity = 0;
    std::uint64_t capabilityGeneration = 0;
    std::string signature;
    std::string pluginPath;
    std::string xplaneSystemPath;
    bool versionDataRefPresent = false;
    XPilotPrivateDataKind versionDataKind = XPilotPrivateDataKind::Unknown;
    std::string versionValue;
    bool sequencePresent = false;
    bool senderPresent = false;
    bool bodyPresent = false;
    XPilotPrivateDataKind sequenceKind = XPilotPrivateDataKind::Unknown;
    XPilotPrivateDataKind senderKind = XPilotPrivateDataKind::Unknown;
    XPilotPrivateDataKind bodyKind = XPilotPrivateDataKind::Unknown;
};

struct XPilotPrivateFileInspection {
    bool pathPresent = false;
    bool canonicalizationSucceeded = false;
    bool fileReadable = false;
    bool regularFile = false;
    std::uint64_t actualSize = 0;
    std::string sha256;
    std::string canonicalPath;
    std::uint64_t hashAttempts = 0;
};

struct XPilotPrivateCapabilities {
    bool sequencePresent = false;
    bool senderPresent = false;
    bool bodyPresent = false;
    XPilotPrivateDataKind sequenceKind = XPilotPrivateDataKind::Unknown;
    XPilotPrivateDataKind senderKind = XPilotPrivateDataKind::Unknown;
    XPilotPrivateDataKind bodyKind = XPilotPrivateDataKind::Unknown;
};

struct XPilotPrivateQualificationResult {
    XPilotPrivateSourceIdentity source;
    XPilotPrivateCapabilities capabilities;
    std::uint64_t sourceFatalIssueMask = 0;
    std::uint64_t capabilityFatalIssueMask = 0;
    std::uint64_t fatalIssueMask = 0;
    std::uint64_t advisoryIssueMask = 0;
};

XPilotPrivateFileInspection InspectXPilotPrivatePluginFile(
    const std::string& pluginPath,
    const std::string& xplaneSystemPath);
XPilotPrivateQualificationResult EvaluateXPilotPrivateQualification(
    const XPilotPrivatePlatformPrimitives& primitives,
    const XPilotPrivateFileInspection& file);

struct XPilotPrivateCombinedObservation;

class XPilotPrivateQualificationEventLatch {
public:
    bool Observe(const XPilotPrivateCombinedObservation& observation);
    void Reset();
    std::uint64_t Emitted() const { return emitted_; }

private:
    bool initialized_ = false;
    std::uint64_t pluginInstanceIdentity_ = 0;
    std::uint64_t capabilityGeneration_ = 0;
    std::uint64_t fatalIssueMask_ = 0;
    std::uint64_t advisoryIssueMask_ = 0;
    bool sourceQualified_ = false;
    bool capabilitiesQualified_ = false;
    std::uint64_t emitted_ = 0;
};

std::string FormatXPilotPrivateQualificationEvent(
    const XPilotPrivateCombinedObservation& observation,
    std::int64_t brainObservedSequence,
    std::int64_t brainConnectedSequence);

struct XPilotPrivateSessionEvidence {
    XPilotPrivateConnectionStatus status =
        XPilotPrivateConnectionStatus::Unknown;
    std::string callsign;
};

struct XPilotPrivateSequenceFastPathToken {
    bool applicable = false;
    std::uint64_t pluginInstanceIdentity = 0;
    std::uint64_t capabilityGeneration = 0;
    std::int64_t brainOwnedSequence = 0;
};

struct XPilotPrivateObservationRequest {
    XPilotPrivateSequenceFastPathToken fastPathToken;
};

struct XPilotPrivateCombinedObservation {
    XPilotPrivateSourceIdentity sourceBefore;
    XPilotPrivateSourceIdentity sourceAfter;
    XPilotPrivateCapabilities capabilitiesBefore;
    XPilotPrivateCapabilities capabilitiesAfter;
    XPilotPrivateSessionEvidence sessionBefore;
    XPilotPrivateSessionEvidence sessionAfter;
    std::int64_t sequenceBefore = 0;
    std::int64_t sequenceAfter = 0;
    std::string sender;
    std::string body;
    std::uint64_t mechanicalIssueMask = 0;
    std::uint64_t elapsedMicroseconds = 0;
    std::size_t senderByteCount = 0;
    std::size_t bodyByteCount = 0;
    std::uint64_t sessionLocalDigest = 0;
    bool sourceQualified = false;
    bool capabilitiesQualified = false;
    bool senderBodyRead = false;
    bool sequenceOnlyFastPath = false;
    bool tupleMechanicallyComplete = false;
};

class XPilotPrivatePrimitiveReader {
public:
    virtual ~XPilotPrivatePrimitiveReader() = default;

    virtual XPilotPrivateSourceIdentity ReadSourceIdentity() = 0;
    virtual XPilotPrivateCapabilities ReadCapabilities() = 0;
    virtual XPilotPrivateSessionEvidence ReadSessionEvidence() = 0;
    virtual bool ReadSequence(std::int64_t* sequence) = 0;
    virtual bool ReadSender(std::string* sender) = 0;
    virtual bool ReadBody(std::string* body) = 0;
};

XPilotPrivateCombinedObservation SampleXPilotPrivateMessageCombined(
    XPilotPrivatePrimitiveReader* reader,
    const XPilotPrivateObservationRequest& request = {});

brain::BrainPdcMechanicalObservation ToBrainPdcMechanicalObservation(
    const XPilotPrivateCombinedObservation& observation,
    std::uint64_t observedMonotonicMicroseconds);

struct XPilotPrivateObservationQueueCounters {
    std::uint64_t produced = 0;
    std::uint64_t queued = 0;
    std::uint64_t retained = 0;
    std::uint64_t consumed = 0;
    std::uint64_t capacityLoss = 0;
};

class XPilotPrivateObservationQueue {
public:
    static constexpr std::size_t kCapacity = 8;
    bool Produce(const brain::BrainPdcMechanicalObservation& observation);
    bool Consume(brain::BrainPdcMechanicalObservation* observation);
    bool ServiceRetained();
    void Clear();
    std::size_t Pending() const { return count_; }
    bool Retained() const { return retained_.has_value(); }
    const XPilotPrivateObservationQueueCounters& Counters() const {
        return counters_;
    }

private:
    std::array<brain::BrainPdcMechanicalObservation, kCapacity> facts_{};
    std::size_t head_ = 0;
    std::size_t count_ = 0;
    std::optional<brain::BrainPdcMechanicalObservation> retained_;
    XPilotPrivateObservationQueueCounters counters_;
};

class XPilotBridge : public XPilotPrivatePrimitiveReader {
public:
    XPilotBridge() = default;

    brain::XPilotSessionSnapshot Poll();
    XPilotPrivateCombinedObservation SamplePrivateMessage(
        const XPilotPrivateObservationRequest& request = {});
    void Reset();

    XPilotPrivateSourceIdentity ReadSourceIdentity() override;
    XPilotPrivateCapabilities ReadCapabilities() override;
    XPilotPrivateSessionEvidence ReadSessionEvidence() override;
    bool ReadSequence(std::int64_t* sequence) override;
    bool ReadSender(std::string* sender) override;
    bool ReadBody(std::string* body) override;

private:
    void ResetDataRefs();
    void ResolveDataRefs();
    void ResolvePrivateMessageDataRefs();
    static std::string ReadDataRefString(XPLMDataRef dataRef);
    static bool ParseConnectedStatus(const std::string& rawStatus);

    XPLMPluginID pluginId_ = XPLM_NO_PLUGIN_ID;
    XPLMDataRef loginStatusRef_ = nullptr;
    XPLMDataRef callsignRef_ = nullptr;
    XPLMDataRef versionRef_ = nullptr;
    XPLMDataRef privateMessageSeqRef_ = nullptr;
    XPLMDataRef privateMessageFromRef_ = nullptr;
    XPLMDataRef privateMessageBodyRef_ = nullptr;
    bool privateDataRefsResolved_ = false;
    std::uint64_t privateCapabilityGeneration_ = 1;
    XPLMPluginID privateBinaryPluginId_ = XPLM_NO_PLUGIN_ID;
    std::string privateBinaryPath_;
    std::uint64_t privateBinarySize_ = 0;
    std::string privateBinarySha256_;
    std::string privateBinaryCanonicalPath_;
    bool privateBinaryPathPresent_ = false;
    bool privateBinaryCanonicalized_ = false;
    bool privateBinaryReadable_ = false;
    bool privateBinaryRegularFile_ = false;
    std::uint64_t privateBinaryCapabilityGeneration_ = 0;
    std::uint64_t privateBinaryHashAttempts_ = 0;
};

}  // namespace xvatsim::modules::xpilot_bridge
