#include "XVatsim/modules/runtime_workers/AsyncFactWorkerHost.h"

#include "XVatsim/modules/metar/VatsimMetarClient.h"

namespace xvatsim::modules::runtime_workers {

struct AsyncFactWorkerHost::Implementation {
    metar::VatsimMetarClient metar;
};

AsyncFactWorkerHost::AsyncFactWorkerHost()
    : implementation_(std::make_unique<Implementation>()) {}

AsyncFactWorkerHost::~AsyncFactWorkerHost() = default;

brain::BrainOwnedAsyncWorkerBindings AsyncFactWorkerHost::Bindings() {
    brain::BrainOwnedAsyncWorkerBindings bindings;
    bindings.metar = &implementation_->metar;
    return bindings;
}

}  // namespace xvatsim::modules::runtime_workers
