#pragma once

#include <vector>

#include "XVatsim/brain/BrainWorkModel.h"

namespace xvatsim::brain {

struct BrainWorkSchedulerTuning {
    int maxHeavyJobsPerCycle = 1;
};

struct BrainWorkCyclePlan {
    std::vector<BrainWorkItem> requestedItems;
    std::vector<BrainWorkItem> runnableItems;
    std::vector<BrainWorkItem> deferredItems;
    int requestedHeavyCount = 0;
    int runnableHeavyCount = 0;
    int deferredHeavyCount = 0;

    bool WouldDeferHeavyWork() const {
        return deferredHeavyCount > 0;
    }

    bool RequestedMultipleHeavyJobs() const {
        return requestedHeavyCount > 1;
    }
};

class BrainWorkScheduler {
public:
    BrainWorkCyclePlan PlanCycle(
        std::vector<BrainWorkItem> requestedItems,
        const BrainWorkSchedulerTuning& tuning = {}) const;
};

}  // namespace xvatsim::brain
