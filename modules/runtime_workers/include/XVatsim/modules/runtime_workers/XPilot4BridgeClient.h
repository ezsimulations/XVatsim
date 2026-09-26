#pragma once

#include "XVatsim/brain/BrainXPilot4BridgeTypes.h"

#include <memory>
#include <string>

namespace xvatsim::modules::runtime_workers {

class XPilot4BridgeClient final : public brain::BrainXPilot4Transport {
public:
    XPilot4BridgeClient();
    ~XPilot4BridgeClient() override;
    XPilot4BridgeClient(const XPilot4BridgeClient&) = delete;
    XPilot4BridgeClient& operator=(const XPilot4BridgeClient&) = delete;

    bool Start(std::wstring pipeName = L"XVatsim.XPilot4Bridge.v1");
    void Stop();

    bool TryHarvestXPilot4Observation(
        brain::BrainXPilot4Observation* observation) override;
    bool TrySubmitXPilot4Command(
        const brain::BrainXPilot4Command& command) override;
    brain::BrainXPilot4TransportSnapshot XPilot4TransportSnapshot()
        const override;

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

}  // namespace xvatsim::modules::runtime_workers
