#include "TaskPool.h"

#include <cassert>
#include <stdexcept>

namespace foundation {

TaskPool::TaskPool(unsigned threadCount) {
    if (threadCount == 0) {
        unsigned hw = std::thread::hardware_concurrency();
        threadCount = hw > 1 ? hw - 1 : 1;
    }
    workers.reserve(threadCount);
    for (unsigned i = 0; i < threadCount; ++i) {
        workers.emplace_back([this] { workerLoop(); });
    }
}

TaskPool::~TaskPool() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        // Destroying the pool while a parallelFor is in flight is a caller bug;
        // jobReady is only true between parallelFor entry and exit.
        assert(!jobReady && "TaskPool destroyed during parallelFor");
        shutdown = true;
        jobReady = true;
    }
    cv.notify_all();
    for (auto& t : workers) {
        t.join();
    }
}

void TaskPool::parallelFor(size_t begin, size_t end, size_t grainSize,
                            const std::function<void(size_t, size_t)>& fn) {
    if (begin >= end) return;
    if (grainSize == 0) grainSize = 1;

    size_t range = end - begin;
    size_t totalSlabs = (range + grainSize - 1) / grainSize;

    firstException = nullptr;
    nextSlab.store(0, std::memory_order_relaxed);
    completedSlabs.store(0, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(mutex);
        currentJob.totalSlabs = totalSlabs;
        currentJob.begin = begin;
        currentJob.end = end;
        currentJob.grainSize = grainSize;
        currentJob.fn = &fn;
        jobReady = true;
    }
    cv.notify_all();

    // Calling thread also participates as a worker to avoid wasting a core.
    size_t slabIdx{};
    while ((slabIdx = nextSlab.fetch_add(1, std::memory_order_relaxed)) < totalSlabs) {
        size_t slabBegin = begin + slabIdx * grainSize;
        size_t slabEnd = slabBegin + grainSize;
        if (slabEnd > end) slabEnd = end;
        try {
            fn(slabBegin, slabEnd);
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex);
            if (!firstException) firstException = std::current_exception();
        }
        completedSlabs.fetch_add(1, std::memory_order_acq_rel);
    }

    // Wait for background workers to finish their slabs.
    std::unique_lock<std::mutex> lock(mutex);
    doneCv.wait(lock, [&] {
        return completedSlabs.load(std::memory_order_acquire) == totalSlabs;
    });
    jobReady = false;

    if (firstException) std::rethrow_exception(firstException);
}

void TaskPool::workerLoop() {
    while (true) {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this] { return jobReady; });

        if (shutdown) break;

        // Snapshot job parameters under lock, then release.
        Job job = currentJob;
        const auto* fn = job.fn;
        lock.unlock();

        size_t slabIdx{};
        while ((slabIdx = nextSlab.fetch_add(1, std::memory_order_relaxed)) < job.totalSlabs) {
            size_t slabBegin = job.begin + slabIdx * job.grainSize;
            size_t slabEnd = slabBegin + job.grainSize;
            if (slabEnd > job.end) slabEnd = job.end;
            try {
                (*fn)(slabBegin, slabEnd);
            } catch (...) {
                std::lock_guard<std::mutex> eLock(mutex);
                if (!firstException) firstException = std::current_exception();
            }
            completedSlabs.fetch_add(1, std::memory_order_acq_rel);
        }
        // The final increment above is not under the mutex, so without this
        // lock the calling thread can evaluate the doneCv predicate (seeing
        // N-1), have us increment+notify while it still holds the mutex, and
        // then sleep forever. Acquiring the mutex orders our increment before
        // its predicate re-check.
        {
            std::lock_guard<std::mutex> doneLock(mutex);
        }
        doneCv.notify_one();
    }
}

} // namespace foundation
