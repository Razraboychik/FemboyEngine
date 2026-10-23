// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <vector>
#include <future>
#include <atomic>
#include <memory>
#include <algorithm>
#include <chrono>
#include <exception>
#include <numeric>

namespace Fem
{

// Thread pool for parallel tasks.
class JobSystem
{
public:
    static JobSystem& Instance();
    void Initialize(size_t threads = 0); // 0 = hardware_concurrency
    void Shutdown();

    // Submit a task and return a future for its result. When the pool is not
    // running the task is executed inline, so the returned future is always valid.
    template <typename F> auto Submit(F&& f) -> std::future<decltype(f())>
    {
        using R = decltype(f());
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        std::future<R> res = task->get_future();

        bool queued = false;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (running_)
            {
                queue_.push([task]() { (*task)(); });
                queued = true;
            }
        }
        if (queued)
        {
            cv_.notify_one();
        }
        else
        {
            // No workers: run synchronously so the caller never hangs.
            (*task)();
        }
        return res;
    }

    void WaitAll();
    bool IsRunning() const;
    size_t ThreadCount() const;
    // True when called from one of the pool's worker threads.
    static bool IsWorkerThread();

    // Median cost of waking the pool and completing one small dispatch, in
    // nanoseconds. Measured once after initialize() and cached; used by systems
    // that decide whether a range is large enough to be worth parallelising.
    // The value is a property of the machine (core count, scheduler, power
    // plan), not of the caller, so systems do not need their own magic numbers.
    double DispatchCostNs() const;
    // Re-measures dispatchCostNs(); call after a thread-count change or when a
    // laptop moves between power plans. Cheap (a few microseconds).
    double CalibrateDispatch();

    // Items needed to amortise one dispatch at `costNsPerItem`. For an empty
    // range returns 0; the result is clamped to [1, 1<<24] so a caller can use
    // it directly as a threshold.
    size_t SuggestedParallelItems(double costNsPerItem) const;

    // Parallel reduction. `fn(i)` produces a partial value, `reduce(a, b)`
    // combines two partials. Partials are combined in chunk order, so the
    // result is deterministic for associative operations. The identity is used
    // for the first chunk and must be valid for `reduce`.
    template <typename T, typename F, typename R>
    T ParallelReduce(size_t begin, size_t end, T identity, F&& fn, R&& reduce, size_t minPerJob = 1)
    {
        const size_t total = end > begin ? end - begin : 0;
        if (total == 0)
        {
            return identity;
        }

        const size_t workers = ThreadCount();
        if (workers <= 1 || IsWorkerThread() || !IsRunning() || total <= minPerJob)
        {
            T result = identity;
            bool first = true;
            for (size_t i = begin; i < end; ++i)
            {
                T value = fn(i);
                if (first)
                {
                    result = std::move(value);
                    first = false;
                }
                else
                {
                    result = reduce(std::move(result), std::move(value));
                }
            }
            return result;
        }

        const size_t step = std::max<size_t>(1, minPerJob);
        const size_t wanted = std::max<size_t>(1, std::min(total / step, workers * 4));
        const size_t chunk = std::max(step, (total + wanted - 1) / wanted);
        const size_t chunks = (total + chunk - 1) / chunk;

        std::vector<T> partials(chunks, identity);
        ParallelFor(
            0, chunks,
            [&](size_t c)
            {
                const size_t start = begin + c * chunk;
                const size_t stop = std::min(end, start + chunk);
                T local = identity;
                bool first = true;
                for (size_t i = start; i < stop; ++i)
                {
                    T value = fn(i);
                    if (first)
                    {
                        local = std::move(value);
                        first = false;
                    }
                    else
                    {
                        local = reduce(std::move(local), std::move(value));
                    }
                }
                partials[c] = std::move(local);
            },
            step);

        T result = std::move(partials[0]);
        for (size_t c = 1; c < chunks; ++c)
        {
            result = reduce(std::move(result), std::move(partials[c]));
        }
        return result;
    }

    // Runs two or more callables concurrently. The caller executes the first
    // one, the rest go to the pool; every callable is finished when this
    // returns. Exceptions from helpers are rethrown after the join.
    template <typename F, typename... Rest> void ParallelInvoke(F&& first, Rest&&... rest)
    {
        if (!IsRunning() || IsWorkerThread())
        {
            first();
            (rest(), ...);
            return;
        }
        std::vector<std::future<void>> futures;
        futures.reserve(sizeof...(Rest));
        (futures.push_back(submit(std::forward<Rest>(rest))), ...);
        first();
        for (auto& f : futures)
        {
            f.get();
        }
    }

    // Runs fn(i) for every i in [begin, end), split across the pool. Small
    // ranges (or calls from a worker, where waiting could deadlock) run inline.
    // Exceptions from a chunk are rethrown after every chunk has finished, so no
    // task can outlive the captured callable.
    //
    // This deliberately does not build one future per chunk: a std::future per
    // dispatch costs a heap allocation and a shared state, which dominates the
    // work for the short ranges a game submits every frame. Instead the chunks
    // are claimed from a single atomic counter, and completion is counted on a
    // stack-local latch, so a dispatch is a handful of enqueues and one wait.
    template <typename F> void ParallelFor(size_t begin, size_t end, F&& fn, size_t minPerJob = 1)
    {
        const size_t total = end > begin ? end - begin : 0;
        if (total == 0)
        {
            return;
        }

        const size_t workers = ThreadCount();
        if (workers <= 1 || IsWorkerThread() || total <= minPerJob || !IsRunning())
        {
            for (size_t i = begin; i < end; ++i)
            {
                fn(i);
            }
            return;
        }

        // About four chunks per worker balances the tail without turning the
        // dispatch into thousands of tiny tasks.
        const size_t step = std::max<size_t>(1, minPerJob);
        const size_t wanted = std::max<size_t>(1, std::min(total / step, workers * 4));
        const size_t chunk = std::max(step, (total + wanted - 1) / wanted);
        const size_t chunks = (total + chunk - 1) / chunk;

        if (chunks <= 1)
        {
            for (size_t i = begin; i < end; ++i)
            {
                fn(i);
            }
            return;
        }

        std::atomic<size_t> next{0};
        std::atomic<size_t> remaining{chunks};
        std::mutex doneMtx;
        std::condition_variable doneCv;
        std::exception_ptr error;
        std::mutex errorMtx;

        const auto run = [&]()
        {
            for (;;)
            {
                const size_t index = next.fetch_add(1, std::memory_order_relaxed);
                if (index >= chunks)
                {
                    return;
                }
                const size_t start = begin + index * chunk;
                const size_t stop = std::min(end, start + chunk);
                try
                {
                    for (size_t i = start; i < stop; ++i)
                    {
                        fn(i);
                    }
                }
                catch (...)
                {
                    std::lock_guard<std::mutex> lk(errorMtx);
                    if (!error)
                    {
                        error = std::current_exception();
                    }
                }
                if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
                {
                    std::lock_guard<std::mutex> lk(doneMtx);
                    doneCv.notify_all();
                }
            }
        };

        const size_t helperCount = std::min(workers, chunks - 1);
        for (size_t i = 0; i < helperCount; ++i)
        {
            if (!Enqueue(run))
            {
                break;
            }
        }

        // The calling thread works too, so one core is never idle while it waits.
        run();

        {
            std::unique_lock<std::mutex> lk(doneMtx);
            doneCv.wait(lk, [&remaining]() { return remaining.load(std::memory_order_acquire) == 0; });
        }

        if (error)
        {
            std::rethrow_exception(error);
        }
    }

private:
    JobSystem() = default;
    ~JobSystem();
    void WorkerLoop();
    // Queues a task without allocating a future. Returns false when the pool is
    // not running, in which case the caller must execute the task inline.
    bool Enqueue(std::function<void()> task);

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> queue_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::condition_variable cvDone_;
    std::atomic<int> activeTasks_{0};
    mutable std::atomic<double> dispatchCostNs_{0.0};
    bool running_ = false;
};

} // namespace Fem
