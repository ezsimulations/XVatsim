#pragma once

#include <memory>

#include "XVatsim/brain/BrainOwnedRuntime.h"

namespace xvatsim::modules::runtime_workers {

class AsyncFactWorkerHost {
public:
    AsyncFactWorkerHost();
    ~AsyncFactWorkerHost();
    AsyncFactWorkerHost(const AsyncFactWorkerHost&) = delete;
    AsyncFactWorkerHost& operator=(const AsyncFactWorkerHost&) = delete;

    brain::BrainOwnedAsyncWorkerBindings Bindings();

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

}  // namespace xvatsim::modules::runtime_workers
