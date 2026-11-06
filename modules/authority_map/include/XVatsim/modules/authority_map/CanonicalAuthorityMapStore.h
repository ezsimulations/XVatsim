#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "XVatsim/core/ControllerAuthority.h"

namespace xvatsim::modules::authority_map {

// Publication owner for immutable canonical map generations. Raw handles are
// safe for the worker mailbox because every published generation is retained
// until teardown occurs after all readers have stopped.
class CanonicalAuthorityMapStore {
public:
    void Publish(
        std::shared_ptr<core::authority::CanonicalAuthorityMapDataset> dataset);

    const core::authority::CanonicalAuthorityMapDataset* Current() const;

    // Call only after authority workers/readers have stopped.
    void ResetAfterReadersStopped();

private:
    mutable std::mutex mutex_;
    std::shared_ptr<core::authority::CanonicalAuthorityMapDataset> current_;
    std::vector<std::shared_ptr<const
        core::authority::CanonicalAuthorityMapDataset>> retained_;
    std::atomic<const core::authority::CanonicalAuthorityMapDataset*>
        published_{nullptr};
};

}  // namespace xvatsim::modules::authority_map
