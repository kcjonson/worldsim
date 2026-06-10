#pragma once

// TaskPool: minimal thread pool for data-parallel world generation work.
//
// Key design choices:
//   - ParallelFor partitions [begin,end) into FIXED slabs of grainSize regardless of
//     thread count. Workers claim slabs via an atomic counter. This means the set of
//     (slab start, slab end) pairs is identical at 1 or N threads, so any computation
//     that is a pure function of (slab begin, slab end) produces bit-identical results
//     independent of parallelism — the property required for cross-platform WorldHash.
//   - Exceptions in workers: the first exception is captured; after all slabs finish
//     the stored exception is rethrown on the calling thread.
//   - NOT reentrant: do not call ParallelFor from within a worker lambda.
//   - Thread count: constructor default 0 → hardware_concurrency - 1, minimum 1.

#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace foundation {

class TaskPool {
  public:
    explicit TaskPool(unsigned threadCount = 0);
    ~TaskPool();

    // Non-copyable, non-movable (owns threads)
    TaskPool(const TaskPool&) = delete;
    TaskPool& operator=(const TaskPool&) = delete;
    TaskPool(TaskPool&&) = delete;
    TaskPool& operator=(TaskPool&&) = delete;

    // ParallelFor: execute fn(slabBegin, slabEnd) for each slab of grainSize within
    // [begin, end). Blocks until all slabs complete. Rethrows first worker exception.
    // fn must be thread-safe across different slab ranges.
    void parallelFor(size_t begin, size_t end, size_t grainSize,
                     const std::function<void(size_t, size_t)>& fn);

    unsigned threadCount() const { return static_cast<unsigned>(workers.size()); }

  private:
    struct Job {
        size_t totalSlabs{};
        size_t begin{};
        size_t end{};
        size_t grainSize{};
        const std::function<void(size_t, size_t)>* fn{};
    };

    void workerLoop();

    std::vector<std::thread> workers;
    std::mutex               mutex;
    std::condition_variable  cv;
    std::condition_variable  doneCv;

    Job                      currentJob{};
    std::atomic<size_t>      nextSlab{0};
    std::atomic<size_t>      completedSlabs{0};
    std::exception_ptr       firstException{};
    bool                     shutdown{false};
    bool                     jobReady{false};
};

} // namespace foundation
