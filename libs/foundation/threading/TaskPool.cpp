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
        ++jobSeq;
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

    // Wait until every slab is done AND no worker still holds a snapshot of
    // this job. Without the participant count, a worker that snapshotted the
    // job but was preempted before claiming a slab could outlive this call:
    // the next parallelFor resets nextSlab, and the stale worker would claim
    // a slab of the NEW job and invoke the DEAD fn pointer of the old one.
    std::unique_lock<std::mutex> lock(mutex);
    doneCv.wait(lock, [&] {
        return completedSlabs.load(std::memory_order_acquire) == totalSlabs &&
               activeParticipants == 0;
    });
    jobReady = false;

    if (firstException) std::rethrow_exception(firstException);
}

void TaskPool::workerLoop() {
    uint64_t lastSeenSeq = 0;
    while (true) {
        std::unique_lock<std::mutex> lock(mutex);
        // Each worker joins a given job at most once (jobSeq check) — without
        // it, a worker finding no slabs left would deregister and immediately
        // re-register for the same still-published job, livelocking the
        // caller's wait for activeParticipants == 0.
        cv.wait(lock, [&] { return shutdown || (jobReady && jobSeq != lastSeenSeq); });

        if (shutdown) break;

        // Snapshot job parameters and register as a participant under the
        // lock. parallelFor cannot return (and fn cannot die) while
        // activeParticipants > 0, because its done-predicate is evaluated
        // under this same mutex.
        Job job = currentJob;
        const auto* fn = job.fn;
        lastSeenSeq = jobSeq;
        ++activeParticipants;
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

        // Deregister under the mutex, then notify. Holding the mutex here
        // also orders our final completedSlabs increment before the calling
        // thread's predicate re-check (lost-wakeup prevention).
        lock.lock();
        --activeParticipants;
        lock.unlock();
        doneCv.notify_one();
    }
}

} // namespace foundation
