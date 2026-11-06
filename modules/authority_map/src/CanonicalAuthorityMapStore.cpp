#include "XVatsim/modules/authority_map/CanonicalAuthorityMapStore.h"

#include <utility>

namespace xvatsim::modules::authority_map {

void CanonicalAuthorityMapStore::Publish(
    std::shared_ptr<core::authority::CanonicalAuthorityMapDataset> dataset) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_ = std::move(dataset);
    if (current_ != nullptr) {
        retained_.push_back(current_);
    }
    published_.store(current_.get(), std::memory_order_release);
}

const core::authority::CanonicalAuthorityMapDataset*
CanonicalAuthorityMapStore::Current() const {
    return published_.load(std::memory_order_acquire);
}

void CanonicalAuthorityMapStore::ResetAfterReadersStopped() {
    std::lock_guard<std::mutex> lock(mutex_);
    published_.store(nullptr, std::memory_order_release);
    current_.reset();
    retained_.clear();
}

}  // namespace xvatsim::modules::authority_map
