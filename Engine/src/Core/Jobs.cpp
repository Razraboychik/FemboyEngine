// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Jobs.hpp"
#include "FemboyEngine/Boy.hpp"

namespace Fem
{

namespace
{
// Identifies worker threads so waitAll() can avoid waiting on itself.
thread_local bool tls_isWorker = false;
} // namespace

JobSystem& JobSystem::Instance()
{
    static JobSystem j;
    return j;
}

JobSystem::~JobSystem()
{
    // Static destruction order is not guaranteed. Join any workers while the
    // condition variables and mutex are still alive; otherwise a worker can
    // wake after they are destroyed and abort the process during shutdown.
    Shutdown();
}

void JobSystem::Initialize(size_t threads)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (running_)
    {
        return;
    }
    if (threads == 0)
    {
        threads = std::thread::hardware_concurrency();
    }
    if (threads == 0)
    {
        threads = 4;
    }
    running_ = true;
    for (size_t i = 0; i < threads; ++i)
    {
        workers_.emplace_back([this]() { WorkerLoop(); });
    }
}

void JobSystem::WorkerLoop()
{
    tls_isWorker = true;
    while (true)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [this]() { return !running_ || !queue_.empty(); });
            if (!running_ && queue_.empty())
            {
                return;
            }
            task = std::move(queue_.front());
            queue_.pop();
            // Count the task as active while still holding the lock, so waitAll()
            // can never observe an empty queue with a zero active count in between.
            activeTasks_.fetch_add(1, std::memory_order_acq_rel);
        }

        try
        {
            task();
        }
        catch (...)
        {
        }

        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (activeTasks_.fetch_sub(1, std::memory_order_acq_rel) == 1 && queue_.empty())
            {
                cvDone_.notify_all();
            }
        }
    }
}

bool JobSystem::Enqueue(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!running_)
        {
            return false;
        }
        queue_.push(std::move(task));
    }
    cv_.notify_one();
    return true;
}

void JobSystem::Shutdown()
{
    {
        std::lock_guard<std::mutex> lk(mtx_);
        running_ = false;
    }
    cv_.notify_all();
    for (auto& t : workers_)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
    workers_.clear();
}

bool JobSystem::IsRunning() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return running_;
}

size_t JobSystem::ThreadCount() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return workers_.size();
}

bool JobSystem::IsWorkerThread()
{
    return tls_isWorker;
}

double JobSystem::DispatchCostNs() const
{
    return dispatchCostNs_.load(std::memory_order_relaxed);
}

double JobSystem::CalibrateDispatch()
{
    if (!IsRunning())
    {
        dispatchCostNs_.store(0.0, std::memory_order_relaxed);
        return 0.0;
    }

    const size_t items = std::max<size_t>(2, ThreadCount() * 4);
    std::vector<double> samples;
    samples.reserve(7);
    volatile std::size_t sink = 0;

    for (int sample = 0; sample < 7; ++sample)
    {
        const auto start = std::chrono::steady_clock::now();
        ParallelFor(0, items, [&sink](size_t i) { sink = sink + i; }, 1);
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
        samples.push_back(ns);
    }

    std::sort(samples.begin(), samples.end());
    const double median = samples[samples.size() / 2];
    dispatchCostNs_.store(median, std::memory_order_relaxed);
    return median;
}

size_t JobSystem::SuggestedParallelItems(double costNsPerItem) const
{
    if (!(costNsPerItem > 0.0))
    {
        return 0;
    }
    const double dispatch = DispatchCostNs();
    if (!(dispatch > 0.0))
    {
        return 0;
    }
    const double items = dispatch / costNsPerItem;
    if (items <= 1.0)
    {
        return 1;
    }
    return static_cast<size_t>(std::min(items, static_cast<double>(1u << 24)));
}

void JobSystem::WaitAll()
{
    if (tls_isWorker)
    {
        // A worker cannot wait for itself; waiting here would deadlock.
        BOY_WARN("Jobs", "waitAll() called from a worker thread; ignoring to avoid deadlock");
        return;
    }
    std::unique_lock<std::mutex> lk(mtx_);
    cvDone_.wait(lk, [this]() { return queue_.empty() && activeTasks_.load(std::memory_order_acquire) == 0; });
}

} // namespace Fem
