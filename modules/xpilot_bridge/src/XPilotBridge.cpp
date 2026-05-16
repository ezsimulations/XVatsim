#include "XVatsim/modules/xpilot_bridge/XPilotBridge.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

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

}  // namespace

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

brain::XPilotPrivateMessageSnapshot XPilotBridge::PollPrivateMessage() {
    brain::XPilotPrivateMessageSnapshot snapshot;

    const auto pluginId = XPLMFindPluginBySignature(kXPilotPluginSignature);
    if (pluginId == XPLM_NO_PLUGIN_ID) {
        ResetDataRefs();
        return snapshot;
    }

    snapshot.loaded = true;
    if (pluginId_ != pluginId ||
        loginStatusRef_ == nullptr ||
        callsignRef_ == nullptr ||
        versionRef_ == nullptr ||
        privateMessageSeqRef_ == nullptr ||
        privateMessageFromRef_ == nullptr ||
        privateMessageBodyRef_ == nullptr) {
        pluginId_ = pluginId;
        ResolveDataRefs();
        ResolvePrivateMessageDataRefs();
    }

    if (privateMessageSeqRef_ != nullptr &&
        (XPLMGetDataRefTypes(privateMessageSeqRef_) & xplmType_Int) != 0) {
        snapshot.sequence = XPLMGetDatai(privateMessageSeqRef_);
    }

    snapshot.from = ReadDataRefString(privateMessageFromRef_);
    snapshot.body = ReadDataRefString(privateMessageBodyRef_);
    snapshot.available =
        snapshot.sequence > 0 && !snapshot.from.empty() && !snapshot.body.empty();
    return snapshot;
}

void XPilotBridge::Reset() {
    ResetDataRefs();
}

void XPilotBridge::ResetDataRefs() {
    pluginId_ = XPLM_NO_PLUGIN_ID;
    loginStatusRef_ = nullptr;
    callsignRef_ = nullptr;
    versionRef_ = nullptr;
    privateMessageSeqRef_ = nullptr;
    privateMessageFromRef_ = nullptr;
    privateMessageBodyRef_ = nullptr;
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
