#include "XVatsim/modules/route_sector/RouteSectorResolver.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>

namespace xvatsim::modules::route_sector {
namespace {

std::uint64_t EstimateRouteSnapshotBytes(
    const brain::RouteSectorSnapshot& route) {
    std::uint64_t bytes = sizeof(route) + route.statusLine.capacity() +
        route.diagnosticCacheStatus.capacity() +
        route.diagnosticReason.capacity() + route.departureIcao.capacity() +
        route.destinationIcao.capacity();
    for (const auto& waypoint : route.waypoints) {
        bytes += sizeof(waypoint) + waypoint.ident.capacity();
    }
    const auto addSectors = [&](const auto& sectors) {
        for (const auto& sector : sectors) {
            bytes += sizeof(sector) + sector.identifier.capacity();
            for (const auto& token : sector.matchTokens) {
                bytes += token.capacity();
            }
            for (const auto& pattern : sector.controllerCallsignPatterns) {
                bytes += pattern.capacity();
            }
            for (const auto& prefix : sector.controllerPrefixes) {
                bytes += prefix.capacity();
            }
        }
    };
    addSectors(route.currentSectors);
    addSectors(route.nextSectors);
    return bytes;
}

bool CurrentThreadCpuMicroseconds(std::uint64_t* value) noexcept {
    if (value == nullptr) {
        return false;
    }
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetThreadTimes(
            GetCurrentThread(), &creation, &exit, &kernel, &user) == 0) {
        return false;
    }
    ULARGE_INTEGER kernelTime{};
    kernelTime.LowPart = kernel.dwLowDateTime;
    kernelTime.HighPart = kernel.dwHighDateTime;
    ULARGE_INTEGER userTime{};
    userTime.LowPart = user.dwLowDateTime;
    userTime.HighPart = user.dwHighDateTime;
    *value = (kernelTime.QuadPart + userTime.QuadPart) / 10;
    return true;
}

class ExpandedFmsObservationTimingScope {
public:
    explicit ExpandedFmsObservationTimingScope(RouteWorkerFact* fact) noexcept
        : fact_(fact), wallStarted_(std::chrono::steady_clock::now()) {
        cpuStartedAvailable_ = CurrentThreadCpuMicroseconds(&cpuStartedUs_);
    }

    ~ExpandedFmsObservationTimingScope() {
        if (fact_ == nullptr) {
            return;
        }
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - wallStarted_)
                .count();
        fact_->expandedFmsObservationWorkerWallUs =
            static_cast<std::uint64_t>(std::max<long long>(1, elapsed));

        std::uint64_t cpuFinishedUs = 0;
        fact_->expandedFmsObservationWorkerCpuAvailable =
            cpuStartedAvailable_ &&
            CurrentThreadCpuMicroseconds(&cpuFinishedUs) &&
            cpuFinishedUs >= cpuStartedUs_;
        fact_->expandedFmsObservationWorkerCpuUs =
            fact_->expandedFmsObservationWorkerCpuAvailable
            ? cpuFinishedUs - cpuStartedUs_
            : 0;
    }

private:
    RouteWorkerFact* fact_ = nullptr;
    std::chrono::steady_clock::time_point wallStarted_;
    bool cpuStartedAvailable_ = false;
    std::uint64_t cpuStartedUs_ = 0;
};

}  // namespace

struct RoutePreparationWorker::Implementation {
    static_assert(
        std::atomic<RoutePreparationRequest*>::is_always_lock_free,
        "Gate B request mailbox requires lock-free pointer atomics");
    static_assert(
        std::atomic<RouteWorkerFact*>::is_always_lock_free,
        "Gate B completion mailbox requires lock-free pointer atomics");

    struct RequestNode {
        std::atomic<bool> inUse{false};
        RoutePreparationRequest request;
    };
    struct FactNode {
        std::atomic<bool> inUse{false};
        RouteWorkerFact fact;
    };
    struct SharedRouteRetirementNode {
        std::atomic<bool> inUse{false};
        std::shared_ptr<const brain::RouteSectorSnapshot> route;
        SharedRouteRetirementNode* next = nullptr;
    };
    struct DatasetObservationNode {
        std::atomic<bool> inUse{false};
        std::shared_ptr<const RouteSourceDataset> dataset;
        DatasetObservationNode* next = nullptr;
    };

    HANDLE wakeEvent = nullptr;
    std::thread worker;
    std::unique_ptr<RoutePreparationEngine> engine;
    std::array<RequestNode, 4> requestNodes;
    std::array<FactNode, 4> factNodes;
    // The route sanity limit is 30 sectors. Sixty-four nodes therefore cover
    // every possible transition from one accepted route plus replacement and
    // lifecycle retirement, even if the worker is not scheduled meanwhile.
    std::array<SharedRouteRetirementNode, 64> sharedRouteRetirementNodes;
    std::array<DatasetObservationNode, 4> datasetObservationNodes;
    std::atomic<RoutePreparationRequest*> pending{nullptr};
    std::atomic<RouteWorkerFact*> completed{nullptr};
    std::atomic<RoutePreparationRequest*> retiredRequests{nullptr};
    std::atomic<RouteWorkerFact*> retiredFacts{nullptr};
    std::atomic<SharedRouteRetirementNode*> retiredSharedRoutes{nullptr};
    std::atomic<DatasetObservationNode*> datasetObservations{nullptr};
    std::vector<std::shared_ptr<RouteSnapshotLease>> retainedLeases;
    std::vector<std::shared_ptr<const RouteSourceDataset>> retainedDatasets;
    std::atomic<bool> running{false};
    std::atomic<bool> started{false};
    std::atomic<bool> stopping{false};
    std::atomic<std::uint64_t> latestMailboxSequence{0};
    std::atomic<std::uint64_t> activeMailboxSequence{0};
    std::atomic<std::uint64_t> starts{0};
    std::atomic<std::uint64_t> replacements{0};
    std::atomic<std::uint64_t> completions{0};
    std::atomic<std::uint64_t> cancellations{0};
    std::atomic<std::uint64_t> failures{0};
    std::atomic<std::uint64_t> staleRetirements{0};
    std::atomic<std::uint64_t> maximumPendingDepth{0};
    std::atomic<std::uint64_t> requestNodesInUse{0};
    std::atomic<std::uint64_t> requestNodesPeak{0};
    std::atomic<std::uint64_t> leasesCreated{0};
    std::atomic<std::uint64_t> leasesRetired{0};
    std::atomic<std::uint64_t> leasesDestroyedOnWorker{0};
    std::atomic<std::uint64_t> outstandingLeases{0};
    std::atomic<std::uint64_t> peakRetainedRouteItems{0};
    std::atomic<std::uint64_t> retainedRouteBytes{0};
    std::atomic<std::uint64_t> peakRetainedRouteBytes{0};
    std::atomic<std::uint64_t> sourceDatasetObservations{0};
    std::atomic<std::uint64_t> sourceDatasetObservationRejections{0};
    std::atomic<std::uint64_t> retainedSourceDatasets{0};
    std::atomic<std::uint64_t> peakRetainedSourceDatasets{0};
    std::atomic<std::uint64_t> fmsObservations{0};
    std::atomic<std::uint64_t> fmsObservationChanges{0};
    std::uint64_t lastFmsObservationIdentity = 0;

    ~Implementation() {
        Stop();
    }

    static long long MonotonicMilliseconds() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    void Signal() const {
        if (wakeEvent != nullptr) {
            SetEvent(wakeEvent);
        }
    }

    static void PushRetiredRequest(
        std::atomic<RoutePreparationRequest*>* stack,
        RoutePreparationRequest* request) {
        if (stack == nullptr || request == nullptr) {
            return;
        }
        auto* head = stack->load(std::memory_order_relaxed);
        do {
            request->retirementNext = head;
        } while (!stack->compare_exchange_weak(
            head,
            request,
            std::memory_order_release,
            std::memory_order_relaxed));
    }

    static void PushRetiredFact(
        std::atomic<RouteWorkerFact*>* stack,
        RouteWorkerFact* fact) {
        if (stack == nullptr || fact == nullptr) {
            return;
        }
        auto* head = stack->load(std::memory_order_relaxed);
        do {
            fact->retirementNext = head;
        } while (!stack->compare_exchange_weak(
            head,
            fact,
            std::memory_order_release,
            std::memory_order_relaxed));
    }

    RequestNode* FindRequestNode(RoutePreparationRequest* request) {
        for (auto& node : requestNodes) {
            if (&node.request == request) {
                return &node;
            }
        }
        return nullptr;
    }

    FactNode* FindFactNode(RouteWorkerFact* fact) {
        for (auto& node : factNodes) {
            if (&node.fact == fact) {
                return &node;
            }
        }
        return nullptr;
    }

    RequestNode* AcquireRequestNode() {
        for (auto& node : requestNodes) {
            bool expected = false;
            if (!node.inUse.compare_exchange_strong(
                    expected,
                    true,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
                continue;
            }
            const auto inUse = requestNodesInUse.fetch_add(
                                   1, std::memory_order_relaxed) +
                               1;
            auto peak = requestNodesPeak.load(std::memory_order_relaxed);
            while (inUse > peak &&
                   !requestNodesPeak.compare_exchange_weak(
                       peak,
                       inUse,
                       std::memory_order_relaxed,
                       std::memory_order_relaxed)) {
            }
            return &node;
        }
        return nullptr;
    }

    FactNode* AcquireFactNode() {
        for (auto& node : factNodes) {
            bool expected = false;
            if (node.inUse.compare_exchange_strong(
                    expected,
                    true,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
                return &node;
            }
        }
        return nullptr;
    }

    void ReleaseRequestOnWorker(RoutePreparationRequest* request) {
        auto* node = FindRequestNode(request);
        if (node == nullptr) {
            return;
        }
        node->request = {};
        node->inUse.store(false, std::memory_order_release);
        requestNodesInUse.fetch_sub(1, std::memory_order_relaxed);
    }

    void ReleaseFactOnWorker(RouteWorkerFact* fact) {
        auto* node = FindFactNode(fact);
        if (node == nullptr) {
            return;
        }
        if (node->fact.routeLease != nullptr) {
            node->fact.routeLease->retired.store(
                true, std::memory_order_release);
        }
        node->fact = {};
        node->inUse.store(false, std::memory_order_release);
    }

    void DrainRetiredObjects() {
        auto* request = retiredRequests.exchange(
            nullptr, std::memory_order_acquire);
        while (request != nullptr) {
            auto* next = request->retirementNext;
            ReleaseRequestOnWorker(request);
            request = next;
        }
        auto* fact = retiredFacts.exchange(
            nullptr, std::memory_order_acquire);
        while (fact != nullptr) {
            auto* next = fact->retirementNext;
            ReleaseFactOnWorker(fact);
            fact = next;
        }
        auto* sharedRoute = retiredSharedRoutes.exchange(
            nullptr, std::memory_order_acquire);
        while (sharedRoute != nullptr) {
            auto* next = sharedRoute->next;
            sharedRoute->route.reset();
            sharedRoute->next = nullptr;
            sharedRoute->inUse.store(false, std::memory_order_release);
            sharedRoute = next;
        }
        auto* observation = datasetObservations.exchange(
            nullptr, std::memory_order_acquire);
        while (observation != nullptr) {
            auto* next = observation->next;
            RetainDataset(observation->dataset);
            observation->dataset.reset();
            observation->next = nullptr;
            observation->inUse.store(false, std::memory_order_release);
            observation = next;
        }
        for (auto lease = retainedLeases.begin();
             lease != retainedLeases.end();) {
            if (*lease == nullptr ||
                (*lease)->retired.load(std::memory_order_acquire)) {
                if (*lease != nullptr) {
                    retainedRouteBytes.fetch_sub(
                        (*lease)->retainedBytes,
                        std::memory_order_relaxed);
                }
                lease = retainedLeases.erase(lease);
                leasesRetired.fetch_add(1, std::memory_order_relaxed);
                leasesDestroyedOnWorker.fetch_add(
                    1, std::memory_order_relaxed);
                outstandingLeases.fetch_sub(1, std::memory_order_relaxed);
            } else {
                ++lease;
            }
        }
        for (auto dataset = retainedDatasets.begin();
             dataset != retainedDatasets.end();) {
            if (*dataset == nullptr || dataset->use_count() == 1) {
                dataset = retainedDatasets.erase(dataset);
            } else {
                ++dataset;
            }
        }
        retainedSourceDatasets.store(
            static_cast<std::uint64_t>(retainedDatasets.size()),
            std::memory_order_relaxed);
    }

    void RetainDataset(
        const std::shared_ptr<const RouteSourceDataset>& dataset) {
        if (dataset == nullptr) {
            return;
        }
        const auto found = std::find_if(
            retainedDatasets.begin(),
            retainedDatasets.end(),
            [&](const auto& retained) {
                return retained.get() == dataset.get();
            });
        if (found == retainedDatasets.end()) {
            retainedDatasets.push_back(dataset);
            const auto retained = static_cast<std::uint64_t>(
                retainedDatasets.size());
            retainedSourceDatasets.store(
                retained, std::memory_order_relaxed);
            auto peak = peakRetainedSourceDatasets.load(
                std::memory_order_relaxed);
            while (retained > peak &&
                   !peakRetainedSourceDatasets.compare_exchange_weak(
                       peak,
                       retained,
                       std::memory_order_relaxed,
                       std::memory_order_relaxed)) {
            }
        }
    }

    bool StartThread(
        std::string xplaneRootPath,
        std::shared_ptr<const RoutePreparationSource> source,
        bool verifyPreflightSourceFile) {
        if (started.load(std::memory_order_acquire)) {
            return true;
        }
        if (stopping.load(std::memory_order_acquire)) {
            return false;
        }
        try {
            engine = std::make_unique<RoutePreparationEngine>(
                std::move(xplaneRootPath),
                std::move(source),
                verifyPreflightSourceFile);
        } catch (...) {
            return false;
        }
        wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (wakeEvent == nullptr) {
            engine.reset();
            return false;
        }
        try {
            worker = std::thread([this]() { Run(); });
        } catch (...) {
            CloseHandle(wakeEvent);
            wakeEvent = nullptr;
            engine.reset();
            return false;
        }
        started.store(true, std::memory_order_release);
        return true;
    }

    void PublishFact(RouteWorkerFact fact) {
        auto* node = AcquireFactNode();
        if (node == nullptr) {
            auto* obsolete = completed.exchange(
                nullptr, std::memory_order_acq_rel);
            if (obsolete != nullptr) {
                ReleaseFactOnWorker(obsolete);
                staleRetirements.fetch_add(1, std::memory_order_relaxed);
            }
            node = AcquireFactNode();
        }
        if (node == nullptr) {
            return;
        }
        node->fact = std::move(fact);
        auto* obsolete = completed.exchange(
            &node->fact, std::memory_order_acq_rel);
        if (obsolete != nullptr) {
            ReleaseFactOnWorker(obsolete);
            staleRetirements.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void Run() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        for (;;) {
            WaitForSingleObject(wakeEvent, INFINITE);
            DrainRetiredObjects();
            if (stopping.load(std::memory_order_acquire)) {
                ReleaseRequestOnWorker(pending.exchange(
                    nullptr, std::memory_order_acq_rel));
                ReleaseFactOnWorker(completed.exchange(
                    nullptr, std::memory_order_acq_rel));
                DrainRetiredObjects();
                retainedLeases.clear();
                retainedDatasets.clear();
                outstandingLeases.store(0, std::memory_order_relaxed);
                retainedRouteBytes.store(0, std::memory_order_relaxed);
                retainedSourceDatasets.store(0, std::memory_order_relaxed);
                running.store(false, std::memory_order_release);
                activeMailboxSequence.store(0, std::memory_order_release);
                return;
            }

            auto* request = pending.exchange(
                nullptr, std::memory_order_acq_rel);
            if (request == nullptr) {
                continue;
            }
            running.store(true, std::memory_order_release);
            activeMailboxSequence.store(
                request->mailboxSequence, std::memory_order_release);
            RetainDataset(request->routeSourceDataset);

            if (request->workerStartRaceDelayForTestingMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(
                    request->workerStartRaceDelayForTestingMs));
            }

            RouteWorkerFact fact;
            fact.kind = request->kind;
            fact.identity = request->identity;
            const RouteCancellationToken cancellation(
                &latestMailboxSequence,
                request->mailboxSequence,
                &stopping);
            const auto supersededBeforeStart =
                cancellation.IsCancellationRequested();
            auto remainingDelayMs = request->cooperativeDelayForTestingMs;
            while (remainingDelayMs > 0 &&
                   !cancellation.IsCancellationRequested()) {
                const auto slice =
                    std::min<std::uint32_t>(remainingDelayMs, 10);
                std::this_thread::sleep_for(std::chrono::milliseconds(slice));
                remainingDelayMs -= slice;
            }

            try {
                if (supersededBeforeStart ||
                    cancellation.IsCancellationRequested()) {
                    fact.cancelled = true;
                    fact.reason = "cooperative-cancellation";
                } else if (request->forceFailureForTesting) {
                    throw std::runtime_error("forced-route-worker-failure");
                } else if (engine == nullptr) {
                    fact.failed = true;
                    fact.reason = "route-engine-unavailable";
                } else if (request->kind ==
                           RoutePreparationRequest::Kind::ObserveExpandedFms) {
                    ExpandedFmsObservationTimingScope observationTiming(
                        &fact);
                    const auto observation = engine->ObserveExpandedFms(
                        *request->networkPlan, &cancellation);
                    fact.expandedFmsObservationIdentity =
                        observation.identity;
                    fact.expandedFmsMatch = observation.hasMatchingPlan;
                    fact.reason = observation.reason;
                    fact.cancelled = observation.cancelled ||
                        cancellation.IsCancellationRequested();
                    if (!fact.cancelled && observation.identity != 0) {
                        fmsObservations.fetch_add(1, std::memory_order_relaxed);
                        if (lastFmsObservationIdentity !=
                            observation.identity) {
                            lastFmsObservationIdentity = observation.identity;
                            fmsObservationChanges.fetch_add(
                                1, std::memory_order_relaxed);
                        }
                    }
                } else {
                    auto result = engine->Prepare(
                        *request, &cancellation);
                    fact.timings = result.timings;
                    fact.routeDigest = result.comprehensiveDigest;
                    fact.reason = result.reason;
                    fact.cancelled =
                        cancellation.IsCancellationRequested() ||
                        result.reason == "cooperative-cancellation" ||
                        request->mailboxSequence !=
                            latestMailboxSequence.load(
                                std::memory_order_acquire);
                    if (!fact.cancelled && result.route != nullptr) {
                        auto lease = std::make_shared<RouteSnapshotLease>();
                        lease->route = std::move(result.route);
                        lease->retainedBytes =
                            EstimateRouteSnapshotBytes(*lease->route);
                        fact.routeLease = lease;
                        retainedLeases.push_back(std::move(lease));
                        leasesCreated.fetch_add(1, std::memory_order_relaxed);
                        const auto retainedItems =
                            outstandingLeases.fetch_add(
                                1, std::memory_order_relaxed) + 1;
                        auto itemPeak = peakRetainedRouteItems.load(
                            std::memory_order_relaxed);
                        while (retainedItems > itemPeak &&
                               !peakRetainedRouteItems.compare_exchange_weak(
                                   itemPeak,
                                   retainedItems,
                                   std::memory_order_relaxed,
                                   std::memory_order_relaxed)) {
                        }
                        const auto retainedBytes =
                            retainedRouteBytes.fetch_add(
                                fact.routeLease->retainedBytes,
                                std::memory_order_relaxed) +
                            fact.routeLease->retainedBytes;
                        auto peak = peakRetainedRouteBytes.load(
                            std::memory_order_relaxed);
                        while (retainedBytes > peak &&
                               !peakRetainedRouteBytes.compare_exchange_weak(
                                   peak,
                                   retainedBytes,
                                   std::memory_order_relaxed,
                                   std::memory_order_relaxed)) {
                        }
                    }
                }
            } catch (const std::exception& exception) {
                fact.failed = true;
                fact.reason = std::string{"route-worker-exception:"} +
                    exception.what();
            } catch (...) {
                fact.failed = true;
                fact.reason = "route-worker-exception:unknown";
            }

            fact.cancelled = fact.cancelled ||
                cancellation.IsCancellationRequested() ||
                request->mailboxSequence !=
                    latestMailboxSequence.load(std::memory_order_acquire);
            fact.completed = !fact.cancelled && !fact.failed &&
                (request->kind ==
                         RoutePreparationRequest::Kind::ObserveExpandedFms
                     ? fact.expandedFmsObservationIdentity != 0
                     : fact.routeLease != nullptr);
            fact.completedMonotonicMs = MonotonicMilliseconds();
            if (fact.cancelled) {
                cancellations.fetch_add(1, std::memory_order_relaxed);
                if (fact.routeLease != nullptr) {
                    fact.routeLease->retired.store(
                        true, std::memory_order_release);
                }
            } else if (fact.failed) {
                failures.fetch_add(1, std::memory_order_relaxed);
            } else {
                completions.fetch_add(1, std::memory_order_relaxed);
            }

            activeMailboxSequence.store(0, std::memory_order_release);
            running.store(false, std::memory_order_release);
            ReleaseRequestOnWorker(request);
            PublishFact(std::move(fact));
            DrainRetiredObjects();
            if (pending.load(std::memory_order_acquire) != nullptr) {
                Signal();
            }
        }
    }

    void Stop() {
        if (!started.load(std::memory_order_acquire) ||
            stopping.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        latestMailboxSequence.fetch_add(1, std::memory_order_acq_rel);
        PushRetiredRequest(
            &retiredRequests,
            pending.exchange(nullptr, std::memory_order_acq_rel));
        auto* abandoned = completed.exchange(
            nullptr, std::memory_order_acq_rel);
        if (abandoned != nullptr && abandoned->routeLease != nullptr) {
            abandoned->routeLease->retired.store(
                true, std::memory_order_release);
        }
        PushRetiredFact(&retiredFacts, abandoned);
        Signal();
        if (worker.joinable()) {
            worker.join();
        }
        started.store(false, std::memory_order_release);
        if (wakeEvent != nullptr) {
            CloseHandle(wakeEvent);
            wakeEvent = nullptr;
        }
        engine.reset();
    }
};

RoutePreparationWorker::RoutePreparationWorker()
    : implementation_(std::make_unique<Implementation>()) {}

RoutePreparationWorker::~RoutePreparationWorker() = default;

bool RoutePreparationWorker::Start(
    std::string xplaneRootPath,
    std::shared_ptr<const RoutePreparationSource> source,
    bool verifyPreflightSourceFile) {
    return implementation_ != nullptr &&
           implementation_->StartThread(
               std::move(xplaneRootPath),
               std::move(source),
               verifyPreflightSourceFile);
}

bool RoutePreparationWorker::StartLatest(RoutePreparationRequest request) {
    if (implementation_ == nullptr || request.networkPlan == nullptr ||
        request.routeSourceDataset == nullptr ||
        !implementation_->started.load(std::memory_order_acquire) ||
        implementation_->stopping.load(std::memory_order_acquire)) {
        return false;
    }
    auto* node = implementation_->AcquireRequestNode();
    if (node == nullptr) {
        return false;
    }
    node->request = std::move(request);
    node->request.mailboxSequence =
        implementation_->latestMailboxSequence.fetch_add(
            1, std::memory_order_acq_rel) +
        1;
    auto* replaced = implementation_->pending.exchange(
        &node->request, std::memory_order_acq_rel);
    if (replaced != nullptr) {
        implementation_->replacements.fetch_add(
            1, std::memory_order_relaxed);
        Implementation::PushRetiredRequest(
            &implementation_->retiredRequests, replaced);
    }
    implementation_->maximumPendingDepth.store(1, std::memory_order_relaxed);
    implementation_->starts.fetch_add(1, std::memory_order_relaxed);
    implementation_->Signal();
    return true;
}

bool RoutePreparationWorker::TryHarvest(RouteWorkerFact* fact) {
    if (implementation_ == nullptr || fact == nullptr) {
        return false;
    }
    auto* completed = implementation_->completed.exchange(
        nullptr, std::memory_order_acq_rel);
    if (completed == nullptr) {
        return false;
    }
    if (fact->routeLease != nullptr) {
        fact->routeLease->retired.store(true, std::memory_order_release);
    }
    *fact = std::move(*completed);
    Implementation::PushRetiredFact(
        &implementation_->retiredFacts, completed);
    implementation_->Signal();
    return true;
}

bool RoutePreparationWorker::IsRunning() const {
    return implementation_ != nullptr &&
           (implementation_->running.load(std::memory_order_acquire) ||
            implementation_->pending.load(std::memory_order_acquire) !=
                nullptr);
}

void RoutePreparationWorker::NotifyRetirement() {
    if (implementation_ != nullptr) {
        implementation_->Signal();
    }
}

bool RoutePreparationWorker::RetireSharedRoute(
    std::shared_ptr<const brain::RouteSectorSnapshot>* route) {
    if (route == nullptr || *route == nullptr || implementation_ == nullptr ||
        !implementation_->started.load(std::memory_order_acquire) ||
        implementation_->stopping.load(std::memory_order_acquire)) {
        return route != nullptr && *route == nullptr;
    }
    for (auto& node : implementation_->sharedRouteRetirementNodes) {
        bool expected = false;
        if (!node.inUse.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel,
                std::memory_order_relaxed)) {
            continue;
        }
        node.route = std::move(*route);
        auto* head = implementation_->retiredSharedRoutes.load(
            std::memory_order_relaxed);
        do {
            node.next = head;
        } while (!implementation_->retiredSharedRoutes.compare_exchange_weak(
            head,
            &node,
            std::memory_order_release,
            std::memory_order_relaxed));
        implementation_->Signal();
        return true;
    }
    return false;
}

bool RoutePreparationWorker::ObserveSourceDataset(
    std::shared_ptr<const RouteSourceDataset> dataset) {
    if (dataset == nullptr || implementation_ == nullptr ||
        !implementation_->started.load(std::memory_order_acquire) ||
        implementation_->stopping.load(std::memory_order_acquire)) {
        return dataset == nullptr;
    }
    for (auto& node : implementation_->datasetObservationNodes) {
        bool expected = false;
        if (!node.inUse.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel,
                std::memory_order_relaxed)) {
            continue;
        }
        node.dataset = std::move(dataset);
        auto* head = implementation_->datasetObservations.load(
            std::memory_order_relaxed);
        do {
            node.next = head;
        } while (!implementation_->datasetObservations.compare_exchange_weak(
            head,
            &node,
            std::memory_order_release,
            std::memory_order_relaxed));
        implementation_->Signal();
        implementation_->sourceDatasetObservations.fetch_add(
            1, std::memory_order_relaxed);
        return true;
    }
    implementation_->sourceDatasetObservationRejections.fetch_add(
        1, std::memory_order_relaxed);
    return false;
}

void RoutePreparationWorker::CancelPending() {
    if (implementation_ == nullptr) {
        return;
    }
    implementation_->latestMailboxSequence.fetch_add(
        1, std::memory_order_acq_rel);
    Implementation::PushRetiredRequest(
        &implementation_->retiredRequests,
        implementation_->pending.exchange(
            nullptr, std::memory_order_acq_rel));
    auto* completed = implementation_->completed.exchange(
        nullptr, std::memory_order_acq_rel);
    if (completed != nullptr && completed->routeLease != nullptr) {
        completed->routeLease->retired.store(
            true, std::memory_order_release);
    }
    Implementation::PushRetiredFact(
        &implementation_->retiredFacts, completed);
    implementation_->Signal();
}

void RoutePreparationWorker::CancelAndJoin() {
    if (implementation_ != nullptr) {
        implementation_->Stop();
    }
}

RouteWorkerSnapshot RoutePreparationWorker::Snapshot() const {
    RouteWorkerSnapshot snapshot;
    if (implementation_ == nullptr) {
        return snapshot;
    }
    snapshot.started =
        implementation_->started.load(std::memory_order_acquire);
    snapshot.running =
        implementation_->running.load(std::memory_order_acquire);
    snapshot.pending =
        implementation_->pending.load(std::memory_order_acquire) != nullptr;
    snapshot.completed =
        implementation_->completed.load(std::memory_order_acquire) != nullptr;
    snapshot.starts = implementation_->starts.load(std::memory_order_relaxed);
    snapshot.replacements =
        implementation_->replacements.load(std::memory_order_relaxed);
    snapshot.completions =
        implementation_->completions.load(std::memory_order_relaxed);
    snapshot.cancellations =
        implementation_->cancellations.load(std::memory_order_relaxed);
    snapshot.failures = implementation_->failures.load(std::memory_order_relaxed);
    snapshot.staleRetirements =
        implementation_->staleRetirements.load(std::memory_order_relaxed);
    snapshot.maximumPendingDepth =
        implementation_->maximumPendingDepth.load(std::memory_order_relaxed);
    snapshot.requestNodesInUse =
        implementation_->requestNodesInUse.load(std::memory_order_relaxed);
    snapshot.requestNodesPeak =
        implementation_->requestNodesPeak.load(std::memory_order_relaxed);
    snapshot.leasesCreated =
        implementation_->leasesCreated.load(std::memory_order_relaxed);
    snapshot.leasesRetired =
        implementation_->leasesRetired.load(std::memory_order_relaxed);
    snapshot.leasesDestroyedOnWorker =
        implementation_->leasesDestroyedOnWorker.load(
            std::memory_order_relaxed);
    snapshot.outstandingLeases =
        implementation_->outstandingLeases.load(std::memory_order_relaxed);
    snapshot.peakRetainedRouteItems =
        implementation_->peakRetainedRouteItems.load(
            std::memory_order_relaxed);
    snapshot.retainedRouteBytes =
        implementation_->retainedRouteBytes.load(std::memory_order_relaxed);
    snapshot.peakRetainedRouteBytes =
        implementation_->peakRetainedRouteBytes.load(
            std::memory_order_relaxed);
    snapshot.sourceDatasetObservations =
        implementation_->sourceDatasetObservations.load(
            std::memory_order_relaxed);
    snapshot.sourceDatasetObservationRejections =
        implementation_->sourceDatasetObservationRejections.load(
            std::memory_order_relaxed);
    snapshot.retainedSourceDatasets =
        implementation_->retainedSourceDatasets.load(
            std::memory_order_relaxed);
    snapshot.peakRetainedSourceDatasets =
        implementation_->peakRetainedSourceDatasets.load(
            std::memory_order_relaxed);
    snapshot.fmsObservations =
        implementation_->fmsObservations.load(std::memory_order_relaxed);
    snapshot.fmsObservationChanges =
        implementation_->fmsObservationChanges.load(
            std::memory_order_relaxed);
    return snapshot;
}

}  // namespace xvatsim::modules::route_sector
