#include "XVatsim/brain/BrainWorkScheduler.h"

namespace xvatsim::brain {

BrainWorkCyclePlan BrainWorkScheduler::PlanCycle(
    std::vector<BrainWorkItem> requestedItems,
    const BrainWorkSchedulerTuning& tuning) const {
    SortBrainWorkQueue(&requestedItems);

    BrainWorkCyclePlan plan;
    plan.requestedItems = requestedItems;

    const auto maxHeavyJobs =
        tuning.maxHeavyJobsPerCycle < 0 ? 0 : tuning.maxHeavyJobsPerCycle;
    int acceptedHeavyJobs = 0;
    for (const auto& item : requestedItems) {
        if (!IsHeavyBrainWork(item)) {
            plan.runnableItems.push_back(item);
            continue;
        }

        ++plan.requestedHeavyCount;
        if (acceptedHeavyJobs < maxHeavyJobs) {
            ++acceptedHeavyJobs;
            ++plan.runnableHeavyCount;
            plan.runnableItems.push_back(item);
        } else {
            ++plan.deferredHeavyCount;
            plan.deferredItems.push_back(item);
        }
    }

    return plan;
}

}  // namespace xvatsim::brain
