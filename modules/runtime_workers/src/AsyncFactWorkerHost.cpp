#include "XVatsim/modules/runtime_workers/AsyncFactWorkerHost.h"

#include "XVatsim/modules/metar/VatsimMetarClient.h"
#include "XVatsim/modules/runtime_workers/XPilot4BridgeClient.h"

namespace xvatsim::modules::runtime_workers {

struct AsyncFactWorkerHost::Implementation {
    metar::VatsimMetarClient metar;
    XPilot4BridgeClient xpilot4;
};

AsyncFactWorkerHost::AsyncFactWorkerHost()
    : implementation_(std::make_unique<Implementation>()) {}

AsyncFactWorkerHost::~AsyncFactWorkerHost() = default;

bool AsyncFactWorkerHost::Start() {
    return implementation_->xpilot4.Start();
}

void AsyncFactWorkerHost::Stop() {
    implementation_->xpilot4.Stop();
}

brain::BrainOwnedAsyncWorkerBindings AsyncFactWorkerHost::Bindings() {
    brain::BrainOwnedAsyncWorkerBindings bindings;
    bindings.metar = &implementation_->metar;
    bindings.xpilot4 = &implementation_->xpilot4;
    return bindings;
}

}  // namespace xvatsim::modules::runtime_workers
