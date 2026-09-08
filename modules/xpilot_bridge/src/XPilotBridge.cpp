#include "XVatsim/modules/xpilot_bridge/XPilotBridge.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include <windows.h>
#include <bcrypt.h>
#include "XPLMUtilities.h"

namespace xvatsim::modules::xpilot_bridge {

namespace {

constexpr char kXPilotPluginSignature[] = "org.vatsim.xpilot";
constexpr char kLoginStatusDataRefName[] = "xpilot/login/status";
constexpr char kLoginCallsignDataRefName[] = "xpilot/login/callsign";
constexpr char kVersionDataRefName[] = "xpilot/version";
constexpr char kPrivateMessageSeqDataRefName[] = "xpilot/messages/private/latest_seq";
constexpr char kPrivateMessageFromDataRefName[] = "xpilot/messages/private/latest_from";
constexpr char kPrivateMessageBodyDataRefName[] = "xpilot/messages/private/latest_body";
constexpr int kMaxDataRefStringBytes = 16 * 1024;
constexpr std::size_t kPrivateSenderByteLimit = 64;
constexpr std::size_t kPrivateBodyByteLimit = 4096;
constexpr std::uint64_t kQualifiedXPilotBinarySize = 4'990'976;
constexpr char kQualifiedXPilotBinarySha256[] =
    "56AEC8CADC0CB36AAF36DC122C330FB782C76FC80190391B8D4A1F717602A8AF";

std::string HexDigest(const unsigned char* bytes, std::size_t count) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(count * 2);
    for (std::size_t index = 0; index < count; ++index) {
        result.push_back(digits[(bytes[index] >> 4U) & 0x0fU]);
        result.push_back(digits[bytes[index] & 0x0fU]);
    }
    return result;
}

bool HashFileSha256(
    const std::filesystem::path& path,
    std::uint64_t* size,
    std::string* digest) {
    if (size == nullptr || digest == nullptr || path.empty()) return false;
    *size = 0;
    digest->clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ |
        FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file, &length) || length.QuadPart < 0) {
        CloseHandle(file);
        return false;
    }
    *size = static_cast<std::uint64_t>(length.QuadPart);
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectBytes = 0;
    DWORD resultBytes = 0;
    DWORD digestBytes = 0;
    bool okay = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes),
            &resultBytes, 0) == 0 &&
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&digestBytes), sizeof(digestBytes),
            &resultBytes, 0) == 0;
    std::vector<unsigned char> object(objectBytes);
    std::vector<unsigned char> output(digestBytes);
    okay = okay && BCryptCreateHash(algorithm, &hash, object.data(),
        static_cast<ULONG>(object.size()), nullptr, 0, 0) == 0;
    std::array<unsigned char, 64 * 1024> buffer{};
    while (okay) {
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                      &read, nullptr)) {
            okay = false;
            break;
        }
        if (read == 0) break;
        okay = BCryptHashData(hash, buffer.data(), read, 0) == 0;
    }
    okay = okay && BCryptFinishHash(hash, output.data(),
        static_cast<ULONG>(output.size()), 0) == 0;
    if (hash != nullptr) BCryptDestroyHash(hash);
    if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    if (okay) *digest = HexDigest(output.data(), output.size());
    return okay;
}

std::string TrimString(std::string value) {
    const auto nullPosition = value.find('\0');
    if (nullPosition != std::string::npos) {
        value.resize(nullPosition);
    }

    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }

    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string ToLowerCopy(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

bool SameSourceIdentity(
    const XPilotPrivateSourceIdentity& left,
    const XPilotPrivateSourceIdentity& right) {
    return left.pluginPresent == right.pluginPresent &&
           left.signatureQualified == right.signatureQualified &&
           left.versionQualified == right.versionQualified &&
           left.binaryFingerprintQualified ==
               right.binaryFingerprintQualified &&
           left.pluginInstanceIdentity == right.pluginInstanceIdentity &&
           left.capabilityGeneration == right.capabilityGeneration &&
           left.signature == right.signature &&
           left.version == right.version &&
           left.binarySha256 == right.binarySha256 &&
           left.pluginPathPresent == right.pluginPathPresent &&
           left.pluginPathCanonicalized == right.pluginPathCanonicalized &&
           left.pluginFileReadable == right.pluginFileReadable &&
           left.pluginTargetRegularFile == right.pluginTargetRegularFile &&
           left.binarySizeQualified == right.binarySizeQualified &&
           left.versionDataRefPresent == right.versionDataRefPresent &&
           left.versionDataRefTypeQualified == right.versionDataRefTypeQualified &&
           left.versionValueQualified == right.versionValueQualified &&
           left.binarySize == right.binarySize &&
           left.fatalIssueMask == right.fatalIssueMask &&
           left.advisoryIssueMask == right.advisoryIssueMask &&
           left.canonicalPluginPath == right.canonicalPluginPath;
}

bool SameCapabilities(
    const XPilotPrivateCapabilities& left,
    const XPilotPrivateCapabilities& right) {
    return left.sequencePresent == right.sequencePresent &&
           left.senderPresent == right.senderPresent &&
           left.bodyPresent == right.bodyPresent &&
           left.sequenceKind == right.sequenceKind &&
           left.senderKind == right.senderKind &&
           left.bodyKind == right.bodyKind;
}

bool ValidUtf8(const std::string& value) {
    std::size_t index = 0;
    while (index < value.size()) {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7fU) {
            ++index;
            continue;
        }
        std::size_t continuationCount = 0;
        if (first >= 0xc2U && first <= 0xdfU) {
            continuationCount = 1;
        } else if (first >= 0xe0U && first <= 0xefU) {
            continuationCount = 2;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            continuationCount = 3;
        } else {
            return false;
        }
        if (index + continuationCount >= value.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset <= continuationCount; ++offset) {
            const auto next =
                static_cast<unsigned char>(value[index + offset]);
            if ((next & 0xc0U) != 0x80U) {
                return false;
            }
        }
        if ((first == 0xe0U &&
             static_cast<unsigned char>(value[index + 1]) < 0xa0U) ||
            (first == 0xedU &&
             static_cast<unsigned char>(value[index + 1]) >= 0xa0U) ||
            (first == 0xf0U &&
             static_cast<unsigned char>(value[index + 1]) < 0x90U) ||
            (first == 0xf4U &&
             static_cast<unsigned char>(value[index + 1]) >= 0x90U)) {
            return false;
        }
        index += continuationCount + 1;
    }
    return true;
}

bool HasUnsafeControl(const std::string& value) {
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto byte = static_cast<unsigned char>(value[index]);
        if (byte == 0U) {
            return true;
        }
        if (byte < 0x20U && byte != '\r' && byte != '\n' && byte != '\t') {
            return true;
        }
        if (byte == 0x7fU ||
            (byte == 0xc2U && index + 1 < value.size() &&
             static_cast<unsigned char>(value[index + 1]) >= 0x80U &&
             static_cast<unsigned char>(value[index + 1]) <= 0x9fU)) {
            return true;
        }
    }
    return false;
}

std::string NormalizePlainText(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto character = value[index];
        if (character == '\r') {
            if (index + 1 < value.size() && value[index + 1] == '\n') ++index;
            result.push_back('\n');
        } else if (character == '\t') {
            result.push_back(' ');
        } else {
            result.push_back(character);
        }
    }
    return result;
}

std::uint64_t SessionLocalDigest(
    const XPilotPrivateCombinedObservation& observation) {
    std::uint64_t digest = 1469598103934665603ULL ^
        observation.sourceBefore.pluginInstanceIdentity ^
        (observation.sourceBefore.capabilityGeneration << 1U);
    const auto append = [&digest](const std::string& value) {
        for (const auto character : value) {
            digest ^= static_cast<unsigned char>(character);
            digest *= 1099511628211ULL;
        }
    };
    append(observation.sessionBefore.callsign);
    append(observation.sender);
    append(observation.body);
    return digest;
}

void AddIssue(
    XPilotPrivateMechanicalIssue issue,
    XPilotPrivateCombinedObservation* observation) {
    if (observation == nullptr) {
        return;
    }
    observation->mechanicalIssueMask |= XPilotPrivateIssueBit(issue);
}

void ValidateSource(
    const XPilotPrivateSourceIdentity& source,
    XPilotPrivateCombinedObservation* observation) {
    if (!source.pluginPresent) {
        AddIssue(XPilotPrivateMechanicalIssue::PluginMissing, observation);
    }
    if (!source.signatureQualified) {
        AddIssue(XPilotPrivateMechanicalIssue::SignatureMismatch, observation);
    }
    if (!source.binaryFingerprintQualified) {
        AddIssue(
            XPilotPrivateMechanicalIssue::BinaryFingerprintMismatch,
            observation);
    }
    observation->mechanicalIssueMask |= source.fatalIssueMask;
}

void ValidateCapabilities(
    const XPilotPrivateCapabilities& capabilities,
    XPilotPrivateCombinedObservation* observation) {
    if (!capabilities.sequencePresent || !capabilities.senderPresent ||
        !capabilities.bodyPresent) {
        AddIssue(XPilotPrivateMechanicalIssue::CapabilityMissing, observation);
    }
    if (capabilities.sequenceKind != XPilotPrivateDataKind::Integer) {
        AddIssue(
            XPilotPrivateMechanicalIssue::SequenceTypeMismatch,
            observation);
    }
    if (capabilities.senderKind != XPilotPrivateDataKind::Bytes) {
        AddIssue(
            XPilotPrivateMechanicalIssue::SenderTypeMismatch,
            observation);
    }
    if (capabilities.bodyKind != XPilotPrivateDataKind::Bytes) {
        AddIssue(
            XPilotPrivateMechanicalIssue::BodyTypeMismatch,
            observation);
    }
}

void ValidateBoundedText(
    const std::string& value,
    std::size_t byteLimit,
    XPilotPrivateMechanicalIssue emptyIssue,
    XPilotPrivateMechanicalIssue overLimitIssue,
    XPilotPrivateMechanicalIssue utf8Issue,
    XPilotPrivateMechanicalIssue controlIssue,
    XPilotPrivateCombinedObservation* observation) {
    if (value.empty()) {
        AddIssue(emptyIssue, observation);
    }
    if (value.size() > byteLimit) {
        AddIssue(overLimitIssue, observation);
    }
    if (!ValidUtf8(value)) {
        AddIssue(utf8Issue, observation);
    }
    if (HasUnsafeControl(value)) {
        AddIssue(controlIssue, observation);
    }
}

}  // namespace

const char* XPilotPrivateMechanicalIssueName(
    XPilotPrivateMechanicalIssue issue) {
    switch (issue) {
        case XPilotPrivateMechanicalIssue::None: return "none";
        case XPilotPrivateMechanicalIssue::PluginMissing: return "plugin-missing";
        case XPilotPrivateMechanicalIssue::SignatureMismatch: return "signature-mismatch";
        case XPilotPrivateMechanicalIssue::VersionMismatch: return "version-mismatch";
        case XPilotPrivateMechanicalIssue::BinaryFingerprintMismatch:
            return "binary-fingerprint-mismatch";
        case XPilotPrivateMechanicalIssue::CapabilityMissing: return "capability-missing";
        case XPilotPrivateMechanicalIssue::SequenceTypeMismatch:
            return "sequence-type-mismatch";
        case XPilotPrivateMechanicalIssue::SenderTypeMismatch:
            return "sender-type-mismatch";
        case XPilotPrivateMechanicalIssue::BodyTypeMismatch: return "body-type-mismatch";
        case XPilotPrivateMechanicalIssue::SourceChanged: return "source-changed";
        case XPilotPrivateMechanicalIssue::SequenceReadFailed:
            return "sequence-read-failed";
        case XPilotPrivateMechanicalIssue::SequenceTorn: return "sequence-torn";
        case XPilotPrivateMechanicalIssue::SequenceInvalid: return "sequence-invalid";
        case XPilotPrivateMechanicalIssue::StatusChanged: return "status-changed";
        case XPilotPrivateMechanicalIssue::CallsignChanged: return "callsign-changed";
        case XPilotPrivateMechanicalIssue::SenderReadFailed:
            return "sender-read-failed";
        case XPilotPrivateMechanicalIssue::BodyReadFailed: return "body-read-failed";
        case XPilotPrivateMechanicalIssue::SenderEmpty: return "sender-empty";
        case XPilotPrivateMechanicalIssue::BodyEmpty: return "body-empty";
        case XPilotPrivateMechanicalIssue::SenderOverLimit: return "sender-over-limit";
        case XPilotPrivateMechanicalIssue::BodyOverLimit: return "body-over-limit";
        case XPilotPrivateMechanicalIssue::SenderInvalidUtf8:
            return "sender-invalid-utf8";
        case XPilotPrivateMechanicalIssue::BodyInvalidUtf8: return "body-invalid-utf8";
        case XPilotPrivateMechanicalIssue::SenderUnsafeControl:
            return "sender-unsafe-control";
        case XPilotPrivateMechanicalIssue::BodyUnsafeControl:
            return "body-unsafe-control";
        case XPilotPrivateMechanicalIssue::PluginPathMissing:
            return "plugin-path-missing";
        case XPilotPrivateMechanicalIssue::PluginPathCanonicalizationFailed:
            return "plugin-path-canonicalization-failed";
        case XPilotPrivateMechanicalIssue::PluginFileUnreadable:
            return "plugin-file-unreadable";
        case XPilotPrivateMechanicalIssue::PluginTargetNotFile:
            return "plugin-target-not-file";
        case XPilotPrivateMechanicalIssue::BinarySizeMismatch:
            return "binary-size-mismatch";
        default: return "unknown";
    }
}

std::string XPilotPrivateMechanicalIssueNames(std::uint64_t mask) {
    if (mask == 0) return "none";
    std::ostringstream out;
    bool first = true;
    for (unsigned shift = 0; shift <= 28; ++shift) {
        const auto bit = 1ULL << shift;
        if ((mask & bit) == 0) continue;
        if (!first) out << ',';
        first = false;
        out << XPilotPrivateMechanicalIssueName(
            static_cast<XPilotPrivateMechanicalIssue>(bit));
    }
    return out.str();
}

const char* XPilotPrivateAdvisoryIssueName(XPilotPrivateAdvisoryIssue issue) {
    switch (issue) {
        case XPilotPrivateAdvisoryIssue::None: return "none";
        case XPilotPrivateAdvisoryIssue::VersionDataRefMissing:
            return "version-dataref-missing";
        case XPilotPrivateAdvisoryIssue::VersionDataRefTypeMismatch:
            return "version-dataref-type-mismatch";
        case XPilotPrivateAdvisoryIssue::VersionValueMismatch:
            return "version-value-mismatch";
        default: return "unknown";
    }
}

std::string XPilotPrivateAdvisoryIssueNames(std::uint64_t mask) {
    if (mask == 0) return "none";
    std::ostringstream out;
    bool first = true;
    for (unsigned shift = 0; shift <= 2; ++shift) {
        const auto bit = 1ULL << shift;
        if ((mask & bit) == 0) continue;
        if (!first) out << ',';
        first = false;
        out << XPilotPrivateAdvisoryIssueName(
            static_cast<XPilotPrivateAdvisoryIssue>(bit));
    }
    return out.str();
}

XPilotPrivateFileInspection InspectXPilotPrivatePluginFile(
    const std::string& pluginPath,
    const std::string& xplaneSystemPath) {
    XPilotPrivateFileInspection result;
    result.pathPresent = !pluginPath.empty();
    if (!result.pathPresent) return result;

    std::error_code error;
    auto candidate = std::filesystem::u8path(pluginPath);
    if (!candidate.is_absolute()) {
        if (xplaneSystemPath.empty()) return result;
        candidate = std::filesystem::u8path(xplaneSystemPath) / candidate;
    }
    const auto canonical = std::filesystem::canonical(candidate, error);
    if (error || canonical.empty() || !canonical.is_absolute()) return result;
    result.canonicalizationSucceeded = true;
    result.canonicalPath = canonical.u8string();
    const auto status = std::filesystem::status(canonical, error);
    if (error) return result;
    result.regularFile = std::filesystem::is_regular_file(status);
    if (!result.regularFile) return result;
    result.hashAttempts = 1;
    result.fileReadable = HashFileSha256(
        canonical, &result.actualSize, &result.sha256);
    return result;
}

XPilotPrivateQualificationResult EvaluateXPilotPrivateQualification(
    const XPilotPrivatePlatformPrimitives& primitives,
    const XPilotPrivateFileInspection& file) {
    XPilotPrivateQualificationResult result;
    auto& source = result.source;
    source.pluginPresent = primitives.pluginPresent;
    source.pluginInstanceIdentity = primitives.pluginInstanceIdentity;
    source.capabilityGeneration = primitives.capabilityGeneration;
    source.signature = primitives.signature;
    source.signatureQualified = primitives.pluginPresent &&
        primitives.signature == kXPilotPluginSignature;
    source.version = primitives.versionValue;
    source.versionDataRefPresent = primitives.versionDataRefPresent;
    source.versionDataRefTypeQualified =
        primitives.versionDataKind == XPilotPrivateDataKind::Bytes;
    source.versionValueQualified =
        source.version.rfind("3.0.2", 0) == 0;
    source.versionQualified = source.versionDataRefPresent &&
        source.versionDataRefTypeQualified && source.versionValueQualified;
    source.pluginPathPresent = file.pathPresent;
    source.pluginPathCanonicalized = file.canonicalizationSucceeded;
    source.pluginFileReadable = file.fileReadable;
    source.pluginTargetRegularFile = file.regularFile;
    source.binarySize = file.actualSize;
    source.binarySizeQualified = file.actualSize == kQualifiedXPilotBinarySize;
    source.binarySha256 = file.sha256;
    source.binaryFingerprintQualified = source.pluginFileReadable &&
        source.pluginTargetRegularFile && source.binarySizeQualified &&
        source.binarySha256 == kQualifiedXPilotBinarySha256;
    source.canonicalPluginPath = file.canonicalPath;

    const auto sourceFatal = [&](XPilotPrivateMechanicalIssue issue, bool failed) {
        if (failed) result.sourceFatalIssueMask |= XPilotPrivateIssueBit(issue);
    };
    sourceFatal(XPilotPrivateMechanicalIssue::PluginMissing, !source.pluginPresent);
    sourceFatal(XPilotPrivateMechanicalIssue::SignatureMismatch,
          !source.signatureQualified);
    sourceFatal(XPilotPrivateMechanicalIssue::PluginPathMissing, !file.pathPresent);
    sourceFatal(XPilotPrivateMechanicalIssue::PluginPathCanonicalizationFailed,
          file.pathPresent && !file.canonicalizationSucceeded);
    sourceFatal(XPilotPrivateMechanicalIssue::PluginFileUnreadable,
          file.canonicalizationSucceeded && file.regularFile && !file.fileReadable);
    sourceFatal(XPilotPrivateMechanicalIssue::PluginTargetNotFile,
          file.canonicalizationSucceeded && !file.regularFile);
    sourceFatal(XPilotPrivateMechanicalIssue::BinarySizeMismatch,
          file.fileReadable && !source.binarySizeQualified);
    sourceFatal(XPilotPrivateMechanicalIssue::BinaryFingerprintMismatch,
          !source.binaryFingerprintQualified);
    source.fatalIssueMask = result.sourceFatalIssueMask;
    const auto advisory = [&](XPilotPrivateAdvisoryIssue issue, bool present) {
        if (present) result.advisoryIssueMask |= XPilotPrivateAdvisoryBit(issue);
    };
    advisory(XPilotPrivateAdvisoryIssue::VersionDataRefMissing,
        !source.versionDataRefPresent);
    advisory(XPilotPrivateAdvisoryIssue::VersionDataRefTypeMismatch,
        source.versionDataRefPresent && !source.versionDataRefTypeQualified);
    advisory(XPilotPrivateAdvisoryIssue::VersionValueMismatch,
        source.versionDataRefPresent && source.versionDataRefTypeQualified &&
        !source.versionValueQualified);
    source.advisoryIssueMask = result.advisoryIssueMask;

    auto& capabilities = result.capabilities;
    capabilities.sequencePresent = primitives.sequencePresent;
    capabilities.senderPresent = primitives.senderPresent;
    capabilities.bodyPresent = primitives.bodyPresent;
    capabilities.sequenceKind = primitives.sequenceKind;
    capabilities.senderKind = primitives.senderKind;
    capabilities.bodyKind = primitives.bodyKind;
    const auto capabilityFatal = [&](XPilotPrivateMechanicalIssue issue,
                                     bool failed) {
        if (failed) {
            result.capabilityFatalIssueMask |= XPilotPrivateIssueBit(issue);
        }
    };
    capabilityFatal(XPilotPrivateMechanicalIssue::CapabilityMissing,
        !capabilities.sequencePresent || !capabilities.senderPresent ||
        !capabilities.bodyPresent);
    capabilityFatal(XPilotPrivateMechanicalIssue::SequenceTypeMismatch,
        capabilities.sequenceKind != XPilotPrivateDataKind::Integer);
    capabilityFatal(XPilotPrivateMechanicalIssue::SenderTypeMismatch,
        capabilities.senderKind != XPilotPrivateDataKind::Bytes);
    capabilityFatal(XPilotPrivateMechanicalIssue::BodyTypeMismatch,
        capabilities.bodyKind != XPilotPrivateDataKind::Bytes);
    result.fatalIssueMask =
        result.sourceFatalIssueMask | result.capabilityFatalIssueMask;
    return result;
}

bool XPilotPrivateQualificationEventLatch::Observe(
    const XPilotPrivateCombinedObservation& observation) {
    const auto advisory = observation.sourceBefore.advisoryIssueMask |
        observation.sourceAfter.advisoryIssueMask;
    const bool changed = !initialized_ ||
        pluginInstanceIdentity_ !=
            observation.sourceBefore.pluginInstanceIdentity ||
        capabilityGeneration_ != observation.sourceBefore.capabilityGeneration ||
        fatalIssueMask_ != observation.mechanicalIssueMask ||
        advisoryIssueMask_ != advisory ||
        sourceQualified_ != observation.sourceQualified ||
        capabilitiesQualified_ != observation.capabilitiesQualified;
    if (!changed) return false;
    initialized_ = true;
    pluginInstanceIdentity_ = observation.sourceBefore.pluginInstanceIdentity;
    capabilityGeneration_ = observation.sourceBefore.capabilityGeneration;
    fatalIssueMask_ = observation.mechanicalIssueMask;
    advisoryIssueMask_ = advisory;
    sourceQualified_ = observation.sourceQualified;
    capabilitiesQualified_ = observation.capabilitiesQualified;
    ++emitted_;
    return true;
}

void XPilotPrivateQualificationEventLatch::Reset() {
    initialized_ = false;
    pluginInstanceIdentity_ = 0;
    capabilityGeneration_ = 0;
    fatalIssueMask_ = 0;
    advisoryIssueMask_ = 0;
    sourceQualified_ = false;
    capabilitiesQualified_ = false;
    emitted_ = 0;
}

std::string FormatXPilotPrivateQualificationEvent(
    const XPilotPrivateCombinedObservation& observation,
    std::int64_t brainObservedSequence,
    std::int64_t brainConnectedSequence) {
    const auto& source = observation.sourceBefore;
    const auto& capabilities = observation.capabilitiesBefore;
    const auto advisory = source.advisoryIssueMask |
        observation.sourceAfter.advisoryIssueMask;
    std::ostringstream out;
    out << "event=xpilot-private-source-qualification"
        << " pluginInstance=" << source.pluginInstanceIdentity
        << " capabilityGeneration=" << source.capabilityGeneration
        << " pluginPresent=" << (source.pluginPresent ? 1 : 0)
        << " signatureQualified=" << (source.signatureQualified ? 1 : 0)
        << " pathPresent=" << (source.pluginPathPresent ? 1 : 0)
        << " pathCanonical=" << (source.pluginPathCanonicalized ? 1 : 0)
        << " fileReadable=" << (source.pluginFileReadable ? 1 : 0)
        << " regularFile=" << (source.pluginTargetRegularFile ? 1 : 0)
        << " fileSize=" << source.binarySize
        << " sizeQualified=" << (source.binarySizeQualified ? 1 : 0)
        << " hashQualified=" << (source.binaryFingerprintQualified ? 1 : 0)
        << " versionPresent=" << (source.versionDataRefPresent ? 1 : 0)
        << " versionType=" << (source.versionDataRefTypeQualified ? 1 : 0)
        << " versionMatch=" << (source.versionValueQualified ? 1 : 0)
        << " seqPresent=" << (capabilities.sequencePresent ? 1 : 0)
        << " seqType="
        << (capabilities.sequenceKind == XPilotPrivateDataKind::Integer ? 1 : 0)
        << " fromPresent=" << (capabilities.senderPresent ? 1 : 0)
        << " fromType="
        << (capabilities.senderKind == XPilotPrivateDataKind::Bytes ? 1 : 0)
        << " bodyPresent=" << (capabilities.bodyPresent ? 1 : 0)
        << " bodyType="
        << (capabilities.bodyKind == XPilotPrivateDataKind::Bytes ? 1 : 0)
        << " fatalMask=0x" << std::hex << observation.mechanicalIssueMask
        << " advisoryMask=0x" << advisory << std::dec
        << " fatalNames="
        << XPilotPrivateMechanicalIssueNames(observation.mechanicalIssueMask)
        << " advisoryNames=" << XPilotPrivateAdvisoryIssueNames(advisory)
        << " rawSequenceBefore=" << observation.sequenceBefore
        << " rawSequenceAfter=" << observation.sequenceAfter
        << " sourceAccepted=" << (observation.sourceQualified ? 1 : 0)
        << " capabilityAccepted="
        << (observation.capabilitiesQualified ? 1 : 0)
        << " tupleAccepted="
        << (observation.tupleMechanicallyComplete ? 1 : 0)
        << " samplerUs=" << observation.elapsedMicroseconds
        << " brainObservedSequence=" << brainObservedSequence
        << " brainConnectedSequence=" << brainConnectedSequence;
    return out.str();
}

XPilotPrivateCombinedObservation SampleXPilotPrivateMessageCombined(
    XPilotPrivatePrimitiveReader* reader,
    const XPilotPrivateObservationRequest& request) {
    XPilotPrivateCombinedObservation observation;
    if (reader == nullptr) {
        AddIssue(XPilotPrivateMechanicalIssue::PluginMissing, &observation);
        return observation;
    }

    const auto started = std::chrono::steady_clock::now();
    observation.sourceBefore = reader->ReadSourceIdentity();
    observation.capabilitiesBefore = reader->ReadCapabilities();
    observation.sessionBefore = reader->ReadSessionEvidence();
    ValidateSource(observation.sourceBefore, &observation);
    ValidateCapabilities(observation.capabilitiesBefore, &observation);

    if (!reader->ReadSequence(&observation.sequenceBefore)) {
        AddIssue(XPilotPrivateMechanicalIssue::SequenceReadFailed, &observation);
    }

    const auto& token = request.fastPathToken;
    const bool tokenMechanicallyMatches =
        token.applicable && observation.sequenceBefore > 0 &&
        token.pluginInstanceIdentity ==
            observation.sourceBefore.pluginInstanceIdentity &&
        token.capabilityGeneration ==
            observation.sourceBefore.capabilityGeneration &&
        token.brainOwnedSequence == observation.sequenceBefore;

    if (observation.sequenceBefore > 0 && !tokenMechanicallyMatches) {
        observation.senderBodyRead = true;
        if (!reader->ReadSender(&observation.sender)) {
            AddIssue(XPilotPrivateMechanicalIssue::SenderReadFailed, &observation);
        }
        if (!reader->ReadBody(&observation.body)) {
            AddIssue(XPilotPrivateMechanicalIssue::BodyReadFailed, &observation);
        }
        if (!observation.sender.empty()) {
            ValidateBoundedText(
                observation.sender,
                kPrivateSenderByteLimit,
                XPilotPrivateMechanicalIssue::SenderEmpty,
                XPilotPrivateMechanicalIssue::SenderOverLimit,
                XPilotPrivateMechanicalIssue::SenderInvalidUtf8,
                XPilotPrivateMechanicalIssue::SenderUnsafeControl,
                &observation);
        }
        ValidateBoundedText(
            observation.body,
            kPrivateBodyByteLimit,
            XPilotPrivateMechanicalIssue::BodyEmpty,
            XPilotPrivateMechanicalIssue::BodyOverLimit,
            XPilotPrivateMechanicalIssue::BodyInvalidUtf8,
            XPilotPrivateMechanicalIssue::BodyUnsafeControl,
            &observation);
    }

    if (!reader->ReadSequence(&observation.sequenceAfter)) {
        AddIssue(XPilotPrivateMechanicalIssue::SequenceReadFailed, &observation);
    }
    observation.sessionAfter = reader->ReadSessionEvidence();
    observation.capabilitiesAfter = reader->ReadCapabilities();
    observation.sourceAfter = reader->ReadSourceIdentity();

    ValidateSource(observation.sourceAfter, &observation);
    ValidateCapabilities(observation.capabilitiesAfter, &observation);
    if (!SameSourceIdentity(observation.sourceBefore, observation.sourceAfter) ||
        !SameCapabilities(
            observation.capabilitiesBefore, observation.capabilitiesAfter)) {
        AddIssue(XPilotPrivateMechanicalIssue::SourceChanged, &observation);
    }
    if (observation.sequenceBefore != observation.sequenceAfter) {
        AddIssue(XPilotPrivateMechanicalIssue::SequenceTorn, &observation);
    }
    if (observation.sequenceBefore < 0 || observation.sequenceAfter < 0) {
        AddIssue(XPilotPrivateMechanicalIssue::SequenceInvalid, &observation);
    }
    if (observation.sessionBefore.status != observation.sessionAfter.status) {
        AddIssue(XPilotPrivateMechanicalIssue::StatusChanged, &observation);
    }
    if (observation.sessionBefore.callsign != observation.sessionAfter.callsign) {
        AddIssue(XPilotPrivateMechanicalIssue::CallsignChanged, &observation);
    }

    observation.sourceQualified =
        observation.sourceBefore.pluginPresent &&
        observation.sourceBefore.signatureQualified &&
        observation.sourceBefore.binaryFingerprintQualified &&
        SameSourceIdentity(observation.sourceBefore, observation.sourceAfter);
    observation.capabilitiesQualified =
        observation.capabilitiesBefore.sequencePresent &&
        observation.capabilitiesBefore.senderPresent &&
        observation.capabilitiesBefore.bodyPresent &&
        observation.capabilitiesBefore.sequenceKind ==
            XPilotPrivateDataKind::Integer &&
        observation.capabilitiesBefore.senderKind ==
            XPilotPrivateDataKind::Bytes &&
        observation.capabilitiesBefore.bodyKind ==
            XPilotPrivateDataKind::Bytes &&
        SameCapabilities(
            observation.capabilitiesBefore, observation.capabilitiesAfter);
    observation.sequenceOnlyFastPath =
        tokenMechanicallyMatches &&
        observation.sessionBefore.status == observation.sessionAfter.status &&
        observation.sessionBefore.callsign == observation.sessionAfter.callsign &&
        observation.sequenceBefore == observation.sequenceAfter &&
        observation.sourceQualified && observation.capabilitiesQualified;
    observation.tupleMechanicallyComplete =
        observation.mechanicalIssueMask == 0 &&
        (observation.sequenceBefore == 0 || observation.senderBodyRead ||
         observation.sequenceOnlyFastPath);
    observation.senderByteCount = observation.sender.size();
    observation.bodyByteCount = observation.body.size();
    if (observation.senderBodyRead && observation.tupleMechanicallyComplete) {
        observation.sender = NormalizePlainText(observation.sender);
        observation.body = NormalizePlainText(observation.body);
        observation.sessionLocalDigest = SessionLocalDigest(observation);
    }
    observation.elapsedMicroseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
    return observation;
}

brain::BrainPdcMechanicalObservation ToBrainPdcMechanicalObservation(
    const XPilotPrivateCombinedObservation& value,
    std::uint64_t observedMonotonicMicroseconds) {
    brain::BrainPdcMechanicalObservation result;
    result.sourceQualified = value.sourceQualified;
    result.capabilitiesQualified = value.capabilitiesQualified;
    result.tupleMechanicallyComplete = value.tupleMechanicallyComplete;
    result.senderBodyRead = value.senderBodyRead;
    result.sequenceOnlyFastPath = value.sequenceOnlyFastPath;
    result.pluginInstanceIdentity = value.sourceBefore.pluginInstanceIdentity;
    result.capabilityGeneration = value.sourceBefore.capabilityGeneration;
    result.mechanicalIssueMask = value.mechanicalIssueMask;
    result.observedMonotonicMicroseconds = observedMonotonicMicroseconds;
    result.samplerElapsedMicroseconds = value.elapsedMicroseconds;
    result.senderByteCount = value.senderByteCount;
    result.bodyByteCount = value.bodyByteCount;
    result.sessionLocalDigest = value.sessionLocalDigest;
    result.sequenceBefore = value.sequenceBefore;
    result.sequenceAfter = value.sequenceAfter;
    const auto status = [](XPilotPrivateConnectionStatus source) {
        switch (source) {
            case XPilotPrivateConnectionStatus::Disconnected:
                return brain::BrainPdcSourceConnectionStatus::Disconnected;
            case XPilotPrivateConnectionStatus::Connecting:
                return brain::BrainPdcSourceConnectionStatus::Connecting;
            case XPilotPrivateConnectionStatus::Connected:
                return brain::BrainPdcSourceConnectionStatus::Connected;
            case XPilotPrivateConnectionStatus::Unknown:
            default: return brain::BrainPdcSourceConnectionStatus::Unknown;
        }
    };
    result.statusBefore = status(value.sessionBefore.status);
    result.statusAfter = status(value.sessionAfter.status);
    result.callsignBefore = value.sessionBefore.callsign;
    result.callsignAfter = value.sessionAfter.callsign;
    result.sender = value.sender;
    result.body = value.body;
    return result;
}

bool XPilotPrivateObservationQueue::Produce(
    const brain::BrainPdcMechanicalObservation& observation) {
    ++counters_.produced;
    if (count_ < kCapacity) {
        facts_[(head_ + count_) % kCapacity] = observation;
        ++count_;
        ++counters_.queued;
        return true;
    }
    if (!retained_.has_value()) {
        retained_ = observation;
        ++counters_.retained;
        return true;
    }
    ++counters_.capacityLoss;
    return false;
}

bool XPilotPrivateObservationQueue::Consume(
    brain::BrainPdcMechanicalObservation* observation) {
    if (observation == nullptr || count_ == 0) return false;
    *observation = std::move(facts_[head_]);
    facts_[head_] = {};
    head_ = (head_ + 1) % kCapacity;
    --count_;
    ++counters_.consumed;
    return true;
}

bool XPilotPrivateObservationQueue::ServiceRetained() {
    if (!retained_.has_value() || count_ >= kCapacity) return false;
    facts_[(head_ + count_) % kCapacity] = std::move(*retained_);
    retained_.reset();
    ++count_;
    ++counters_.queued;
    return true;
}

void XPilotPrivateObservationQueue::Clear() {
    facts_ = {};
    head_ = 0;
    count_ = 0;
    retained_.reset();
}

brain::XPilotSessionSnapshot XPilotBridge::Poll() {
    brain::XPilotSessionSnapshot snapshot;

    const auto pluginId = XPLMFindPluginBySignature(kXPilotPluginSignature);
    if (pluginId == XPLM_NO_PLUGIN_ID) {
        ResetDataRefs();
        snapshot.statusLine = "xPilot not loaded";
        return snapshot;
    }

    snapshot.loaded = true;
    if (pluginId_ != pluginId || loginStatusRef_ == nullptr || callsignRef_ == nullptr) {
        pluginId_ = pluginId;
        ResolveDataRefs();
    }

    snapshot.callsign = ReadDataRefString(callsignRef_);

    bool statusKnown = false;
    if (loginStatusRef_ != nullptr) {
        const auto dataTypes = XPLMGetDataRefTypes(loginStatusRef_);
        if ((dataTypes & xplmType_Int) != 0) {
            snapshot.connected = XPLMGetDatai(loginStatusRef_) != 0;
            snapshot.rawStatus = snapshot.connected ? "connected" : "disconnected";
            statusKnown = true;
        } else if ((dataTypes & xplmType_Data) != 0) {
            snapshot.rawStatus = ReadDataRefString(loginStatusRef_);
            snapshot.connected = ParseConnectedStatus(snapshot.rawStatus);
            statusKnown = !snapshot.rawStatus.empty();
        }
    }

    if (!statusKnown && !snapshot.connected && !snapshot.callsign.empty()) {
        snapshot.connected = true;
        if (snapshot.rawStatus.empty()) {
            snapshot.rawStatus = "connected";
        }
    }

    if (snapshot.connected) {
        snapshot.statusLine = "xPilot connected";
        if (!snapshot.callsign.empty()) {
            snapshot.statusLine += " " + snapshot.callsign;
        }
        return snapshot;
    }

    if (!snapshot.rawStatus.empty()) {
        snapshot.statusLine =
            "xPilot " + (snapshot.rawStatus == "disconnected" ? "Disconnected" : snapshot.rawStatus);
        return snapshot;
    }

    const auto version = ReadDataRefString(versionRef_);
    snapshot.statusLine = "xPilot loaded";
    if (!version.empty()) {
        snapshot.statusLine += " v" + version;
    }

    return snapshot;
}

XPilotPrivateCombinedObservation XPilotBridge::SamplePrivateMessage(
    const XPilotPrivateObservationRequest& request) {
    return SampleXPilotPrivateMessageCombined(this, request);
}

XPilotPrivateSourceIdentity XPilotBridge::ReadSourceIdentity() {
    const auto id = XPLMFindPluginBySignature(kXPilotPluginSignature);
    const bool pluginPresent = id != XPLM_NO_PLUGIN_ID;
    std::array<char, 256> pluginName{};
    std::array<char, 2048> pluginPath{};
    std::array<char, 256> pluginSignature{};
    std::array<char, 256> pluginDescription{};
    if (pluginPresent) {
        XPLMGetPluginInfo(id, pluginName.data(), pluginPath.data(),
            pluginSignature.data(), pluginDescription.data());
    }
    if (!pluginPresent) return {};
    if (pluginId_ != id || !privateDataRefsResolved_) {
        pluginId_ = id;
        ResolveDataRefs();
        ResolvePrivateMessageDataRefs();
        privateDataRefsResolved_ = true;
    }
    const std::string currentPath = pluginPath.data();
    if (privateBinaryPluginId_ != id || privateBinaryPath_ != currentPath ||
        privateBinaryCapabilityGeneration_ != privateCapabilityGeneration_) {
        std::array<char, 2048> systemPath{};
        XPLMGetSystemPath(systemPath.data());
        const auto inspection = InspectXPilotPrivatePluginFile(
            currentPath, systemPath.data());
        privateBinaryPluginId_ = id;
        privateBinaryPath_ = currentPath;
        privateBinaryCapabilityGeneration_ = privateCapabilityGeneration_;
        privateBinaryPathPresent_ = inspection.pathPresent;
        privateBinaryCanonicalized_ = inspection.canonicalizationSucceeded;
        privateBinaryReadable_ = inspection.fileReadable;
        privateBinaryRegularFile_ = inspection.regularFile;
        privateBinarySize_ = inspection.actualSize;
        privateBinarySha256_ = inspection.sha256;
        privateBinaryCanonicalPath_ = inspection.canonicalPath;
        privateBinaryHashAttempts_ += inspection.hashAttempts;
    }
    XPilotPrivatePlatformPrimitives primitives;
    primitives.pluginPresent = true;
    primitives.pluginInstanceIdentity = static_cast<std::uint64_t>(id + 1);
    primitives.capabilityGeneration = privateCapabilityGeneration_;
    primitives.signature = pluginSignature.data();
    primitives.pluginPath = currentPath;
    primitives.versionDataRefPresent = versionRef_ != nullptr;
    if (primitives.versionDataRefPresent &&
        (XPLMGetDataRefTypes(versionRef_) & xplmType_Data) != 0) {
        primitives.versionDataKind = XPilotPrivateDataKind::Bytes;
        primitives.versionValue = ReadDataRefString(versionRef_);
    }
    const auto capabilities = ReadCapabilities();
    primitives.sequencePresent = capabilities.sequencePresent;
    primitives.senderPresent = capabilities.senderPresent;
    primitives.bodyPresent = capabilities.bodyPresent;
    primitives.sequenceKind = capabilities.sequenceKind;
    primitives.senderKind = capabilities.senderKind;
    primitives.bodyKind = capabilities.bodyKind;
    XPilotPrivateFileInspection file;
    file.pathPresent = privateBinaryPathPresent_;
    file.canonicalizationSucceeded = privateBinaryCanonicalized_;
    file.fileReadable = privateBinaryReadable_;
    file.regularFile = privateBinaryRegularFile_;
    file.actualSize = privateBinarySize_;
    file.sha256 = privateBinarySha256_;
    file.canonicalPath = privateBinaryCanonicalPath_;
    return EvaluateXPilotPrivateQualification(primitives, file).source;
}

XPilotPrivateCapabilities XPilotBridge::ReadCapabilities() {
    XPilotPrivateCapabilities value;
    value.sequencePresent = privateMessageSeqRef_ != nullptr;
    value.senderPresent = privateMessageFromRef_ != nullptr;
    value.bodyPresent = privateMessageBodyRef_ != nullptr;
    if (value.sequencePresent &&
        (XPLMGetDataRefTypes(privateMessageSeqRef_) & xplmType_Int) != 0) {
        value.sequenceKind = XPilotPrivateDataKind::Integer;
    }
    if (value.senderPresent &&
        (XPLMGetDataRefTypes(privateMessageFromRef_) & xplmType_Data) != 0) {
        value.senderKind = XPilotPrivateDataKind::Bytes;
    }
    if (value.bodyPresent &&
        (XPLMGetDataRefTypes(privateMessageBodyRef_) & xplmType_Data) != 0) {
        value.bodyKind = XPilotPrivateDataKind::Bytes;
    }
    return value;
}

XPilotPrivateSessionEvidence XPilotBridge::ReadSessionEvidence() {
    XPilotPrivateSessionEvidence value;
    value.callsign = ReadDataRefString(callsignRef_);
    if (loginStatusRef_ == nullptr) return value;
    const auto types = XPLMGetDataRefTypes(loginStatusRef_);
    if ((types & xplmType_Int) != 0) {
        value.status = XPLMGetDatai(loginStatusRef_) != 0
            ? XPilotPrivateConnectionStatus::Connected
            : XPilotPrivateConnectionStatus::Disconnected;
    } else if ((types & xplmType_Data) != 0) {
        const auto raw = ToLowerCopy(ReadDataRefString(loginStatusRef_));
        if (ParseConnectedStatus(raw)) {
            value.status = XPilotPrivateConnectionStatus::Connected;
        } else if (raw.find("connect") != std::string::npos &&
                   raw.find("disconnect") == std::string::npos) {
            value.status = XPilotPrivateConnectionStatus::Connecting;
        } else if (!raw.empty()) {
            value.status = XPilotPrivateConnectionStatus::Disconnected;
        }
    }
    return value;
}

bool XPilotBridge::ReadSequence(std::int64_t* sequence) {
    if (sequence == nullptr || privateMessageSeqRef_ == nullptr ||
        (XPLMGetDataRefTypes(privateMessageSeqRef_) & xplmType_Int) == 0) {
        return false;
    }
    *sequence = XPLMGetDatai(privateMessageSeqRef_);
    return true;
}

bool XPilotBridge::ReadSender(std::string* sender) {
    if (sender == nullptr || privateMessageFromRef_ == nullptr) return false;
    *sender = ReadDataRefString(privateMessageFromRef_);
    return true;
}

bool XPilotBridge::ReadBody(std::string* body) {
    if (body == nullptr || privateMessageBodyRef_ == nullptr) return false;
    *body = ReadDataRefString(privateMessageBodyRef_);
    return true;
}

void XPilotBridge::Reset() {
    ResetDataRefs();
}

void XPilotBridge::ResetDataRefs() {
    pluginId_ = XPLM_NO_PLUGIN_ID;
    privateBinaryPluginId_ = XPLM_NO_PLUGIN_ID;
    privateBinaryPath_.clear();
    privateBinarySize_ = 0;
    privateBinarySha256_.clear();
    privateBinaryCanonicalPath_.clear();
    privateBinaryPathPresent_ = false;
    privateBinaryCanonicalized_ = false;
    privateBinaryReadable_ = false;
    privateBinaryRegularFile_ = false;
    privateBinaryCapabilityGeneration_ = 0;
    privateBinaryHashAttempts_ = 0;
    loginStatusRef_ = nullptr;
    callsignRef_ = nullptr;
    versionRef_ = nullptr;
    privateMessageSeqRef_ = nullptr;
    privateMessageFromRef_ = nullptr;
    privateMessageBodyRef_ = nullptr;
    privateDataRefsResolved_ = false;
    ++privateCapabilityGeneration_;
    if (privateCapabilityGeneration_ == 0) privateCapabilityGeneration_ = 1;
}

void XPilotBridge::ResolveDataRefs() {
    loginStatusRef_ = XPLMFindDataRef(kLoginStatusDataRefName);
    callsignRef_ = XPLMFindDataRef(kLoginCallsignDataRefName);
    versionRef_ = XPLMFindDataRef(kVersionDataRefName);
}

void XPilotBridge::ResolvePrivateMessageDataRefs() {
    privateMessageSeqRef_ = XPLMFindDataRef(kPrivateMessageSeqDataRefName);
    privateMessageFromRef_ = XPLMFindDataRef(kPrivateMessageFromDataRefName);
    privateMessageBodyRef_ = XPLMFindDataRef(kPrivateMessageBodyDataRefName);
    ++privateCapabilityGeneration_;
    if (privateCapabilityGeneration_ == 0) privateCapabilityGeneration_ = 1;
}

std::string XPilotBridge::ReadDataRefString(XPLMDataRef dataRef) {
    if (dataRef == nullptr) {
        return {};
    }

    const auto byteCount = XPLMGetDatab(dataRef, nullptr, 0, 0);
    if (byteCount <= 0) {
        return {};
    }
    if (byteCount > kMaxDataRefStringBytes) {
        return {};
    }

    std::vector<char> buffer(static_cast<std::size_t>(byteCount), '\0');
    XPLMGetDatab(dataRef, buffer.data(), 0, byteCount);
    return TrimString(std::string(buffer.begin(), buffer.end()));
}

bool XPilotBridge::ParseConnectedStatus(const std::string& rawStatus) {
    const auto normalizedStatus = ToLowerCopy(TrimString(rawStatus));
    if (normalizedStatus.empty()) {
        return false;
    }

    if (normalizedStatus == "1") {
        return true;
    }

    if (normalizedStatus == "0") {
        return false;
    }

    if (normalizedStatus.find("not connected") != std::string::npos ||
        normalizedStatus.find("disconnected") != std::string::npos ||
        normalizedStatus.find("offline") != std::string::npos) {
        return false;
    }

    return normalizedStatus.find("connected") != std::string::npos ||
           normalizedStatus.find("logged") != std::string::npos;
}

}  // namespace xvatsim::modules::xpilot_bridge
