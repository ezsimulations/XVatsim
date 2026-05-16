#pragma once

#include "XVatsim/brain/BrainTypes.h"
#include "XPLMDataAccess.h"
#include "XPLMPlugin.h"

namespace xvatsim::modules::xpilot_bridge {

class XPilotBridge {
public:
    XPilotBridge() = default;

    brain::XPilotSessionSnapshot Poll();
    brain::XPilotPrivateMessageSnapshot PollPrivateMessage();
    void Reset();

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
};

}  // namespace xvatsim::modules::xpilot_bridge
