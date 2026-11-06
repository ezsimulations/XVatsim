#include "XVatsim/modules/transceiver_resolver/RadioBoardFactWorker.h"

#include "XVatsim/modules/transceiver_resolver/TransceiverResolver.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace xvatsim::modules::transceiver_resolver {
namespace {

std::uint64_t MonotonicMilliseconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

double MonotonicSeconds() {
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::uint64_t ElapsedMicroseconds(
    const std::chrono::steady_clock::time_point& started) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
}

bool CurrentThreadCpuMicroseconds(std::uint64_t* value) noexcept {
    if (value == nullptr) return false;
#if defined(_WIN32)
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetThreadTimes(
            GetCurrentThread(), &creation, &exit, &kernel, &user)) {
        return false;
    }
    ULARGE_INTEGER kernelTicks{};
    kernelTicks.LowPart = kernel.dwLowDateTime;
    kernelTicks.HighPart = kernel.dwHighDateTime;
    ULARGE_INTEGER userTicks{};
    userTicks.LowPart = user.dwLowDateTime;
    userTicks.HighPart = user.dwHighDateTime;
    *value = (kernelTicks.QuadPart + userTicks.QuadPart) / 10ULL;
    return true;
#else
    *value = 0;
    return false;
#endif
}

}  // namespace

struct RadioBoardFactWorker::Implementation {
    mutable std::mutex mutex;
    std::condition_variable ready;
    std::thread worker;
    bool started = false;
    bool stopping = false;
    bool running = false;
    bool priorityRequested = false;
    bool prioritySucceeded = false;
    std::optional<RadioBoardFactRequest> pending;
    std::optional<RadioBoardFact> completed;
    TransceiverResolver resolver;
    RadioBoardFactWorkerSnapshot counters;

    void WorkerMain() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            priorityRequested = true;
#if defined(_WIN32)
            prioritySucceeded =
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL) !=
                0;
#endif
            counters.workerThreadIdentity = static_cast<std::uint64_t>(
                std::hash<std::thread::id>{}(std::this_thread::get_id()));
        }

        for (;;) {
            RadioBoardFactRequest request;
            {
                std::unique_lock<std::mutex> lock(mutex);
                ready.wait(lock, [this]() {
                    return stopping || (pending.has_value() && !completed.has_value());
                });
                if (stopping) break;
                request = std::move(*pending);
                pending.reset();
                running = true;
            }

            RadioBoardFact fact;
            fact.identity = request.identity;
            const auto wallStarted = std::chrono::steady_clock::now();
            std::uint64_t cpuStartedUs = 0;
            const auto cpuAvailable =
                CurrentThreadCpuMicroseconds(&cpuStartedUs);
            try {
                auto transceivers = resolver.Resolve(
                    request.input.aircraft,
                    request.input.controllerFeed);
                fact.output = brain::BuildBrainRadioRangeWorkerOutput(
                    request.input,
                    transceivers,
                    MonotonicSeconds());
                const auto sourceIdentity = resolver.SourceIdentity();
                fact.sourceGeneration = sourceIdentity.generation;
                fact.sourceContentDigest = sourceIdentity.contentDigest;
                fact.completed = true;
            } catch (...) {
                fact.failed = true;
                fact.failureReason = "radio-board-worker-exception";
            }
            fact.workerWallUs = ElapsedMicroseconds(wallStarted);
            std::uint64_t cpuFinishedUs = 0;
            fact.workerCpuAvailable = cpuAvailable &&
                CurrentThreadCpuMicroseconds(&cpuFinishedUs) &&
                cpuFinishedUs >= cpuStartedUs;
            if (fact.workerCpuAvailable) {
                fact.workerCpuUs = cpuFinishedUs - cpuStartedUs;
            }
            fact.completedMonotonicMs = MonotonicMilliseconds();

            {
                std::lock_guard<std::mutex> lock(mutex);
                running = false;
                if (fact.failed) {
                    ++counters.failures;
                } else {
                    ++counters.completions;
                }
                counters.sourceGeneration = fact.sourceGeneration;
                counters.sourceContentDigest = fact.sourceContentDigest;
                completed.emplace(std::move(fact));
            }
            ready.notify_all();
        }

        resolver.Reset();
        std::lock_guard<std::mutex> lock(mutex);
        running = false;
    }
};

RadioBoardFactWorker::RadioBoardFactWorker()
    : implementation_(std::make_unique<Implementation>()) {}

RadioBoardFactWorker::~RadioBoardFactWorker() {
    Stop();
}

bool RadioBoardFactWorker::Start() {
    auto& state = *implementation_;
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.started) return true;
    state.stopping = false;
    state.running = false;
    state.pending.reset();
    state.completed.reset();
    state.counters = {};
    try {
        state.worker = std::thread([&state]() { state.WorkerMain(); });
    } catch (...) {
        return false;
    }
    state.started = true;
    return true;
}

void RadioBoardFactWorker::Stop() {
    auto& state = *implementation_;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.started && !state.worker.joinable()) return;
        state.stopping = true;
        state.pending.reset();
    }
    state.ready.notify_all();
    if (state.worker.joinable()) state.worker.join();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.started = false;
    state.stopping = false;
    state.running = false;
    state.pending.reset();
    state.completed.reset();
}

bool RadioBoardFactWorker::StartLatest(RadioBoardFactRequest request) {
    auto& state = *implementation_;
    const auto& controllerFeed = request.input.controllerFeed;
    if (controllerFeed.available && !controllerFeed.stale &&
        controllerFeed.ownedControllers == nullptr) {
        return false;
    }
    if (request.input.controllerFeed.ownedControllers != nullptr) {
        request.input.controllerFeed.controllers =
            request.input.controllerFeed.ownedControllers.get();
    }

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.started || state.stopping) return false;
        if (state.pending.has_value()) ++state.counters.replacements;
        state.pending.emplace(std::move(request));
        ++state.counters.submissions;
        state.counters.maximumPendingDepth = 1;
    }
    state.ready.notify_one();
    return true;
}

bool RadioBoardFactWorker::TryHarvest(RadioBoardFact* fact) {
    if (fact == nullptr) return false;
    auto& state = *implementation_;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.completed.has_value()) return false;
        *fact = std::move(*state.completed);
        state.completed.reset();
        ++state.counters.harvests;
    }
    state.ready.notify_one();
    return true;
}

bool RadioBoardFactWorker::HasWork() const {
    const auto& state = *implementation_;
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.running || state.pending.has_value() || state.completed.has_value();
}

RadioBoardFactWorkerSnapshot RadioBoardFactWorker::Snapshot() const {
    const auto& state = *implementation_;
    std::lock_guard<std::mutex> lock(state.mutex);
    auto snapshot = state.counters;
    snapshot.started = state.started;
    snapshot.running = state.running;
    snapshot.pending = state.pending.has_value();
    snapshot.ready = state.completed.has_value();
    snapshot.priorityRequested = state.priorityRequested;
    snapshot.prioritySucceeded = state.prioritySucceeded;
    return snapshot;
}

}  // namespace xvatsim::modules::transceiver_resolver
