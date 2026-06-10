#include "TaskPool.h"
#include <gtest/gtest.h>
#include <atomic>
#include <stdexcept>
#include <vector>
#include <numeric>

using namespace foundation;

// TaskPool is NOT reentrant: do not call parallelFor from within a worker lambda.

// ============================================================================
// Basic correctness: sum must equal serial computation
// ============================================================================

TEST(TaskPoolTests, SumEqualsSerial) {
    TaskPool pool(4);
    constexpr size_t kN = 1000000;
    std::vector<int64_t> data(kN, 1);

    std::atomic<int64_t> sum{0};
    pool.parallelFor(0, kN, 1000, [&](size_t b, size_t e) {
        int64_t local = 0;
        for (size_t i = b; i < e; ++i) local += data[i]; // NOLINT
        sum.fetch_add(local, std::memory_order_relaxed);
    });
    EXPECT_EQ(sum.load(), static_cast<int64_t>(kN));
}

TEST(TaskPoolTests, SumVariousGrainSizes) {
    TaskPool pool(4);
    constexpr size_t kN = 999999; // not a multiple of common grain sizes
    std::atomic<int64_t> sum{0};

    const size_t kGrains[] = {1, 7, 100, 999, 10000, kN};
    for (size_t grain : kGrains) {
        sum.store(0);
        pool.parallelFor(0, kN, grain, [&](size_t b, size_t e) {
            int64_t local = 0;
            for (size_t i = b; i < e; ++i) local += static_cast<int64_t>(i) + 1;
            sum.fetch_add(local, std::memory_order_relaxed);
        });
        // Serial reference: sum of (1..N) = N*(N+1)/2
        int64_t expected = static_cast<int64_t>(kN) * (static_cast<int64_t>(kN) + 1) / 2;
        EXPECT_EQ(sum.load(), expected) << "grain=" << grain;
    }
}

// ============================================================================
// Determinism: identical results at 1 vs N threads
// ============================================================================

TEST(TaskPoolTests, ResultIdenticalAt1And4Threads) {
    constexpr size_t kN = 10000;
    constexpr size_t kGrain = 64;

    std::vector<uint64_t> out1(kN), out4(kN);

    // Fill with a hash of the slab boundaries (pure function of (b, e))
    auto fill = [&](std::vector<uint64_t>& out, unsigned threads) {
        TaskPool pool(threads);
        pool.parallelFor(0, kN, kGrain, [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                // Deterministic function of position only — no shared mutable state
                out[i] = static_cast<uint64_t>(b) * 1000003ULL ^ static_cast<uint64_t>(i); // NOLINT
            }
        });
    };
    fill(out1, 1);
    fill(out4, 4);
    EXPECT_EQ(out1, out4);
}

// ============================================================================
// Empty and edge cases
// ============================================================================

TEST(TaskPoolTests, EmptyRangeNoWork) {
    TaskPool pool(2);
    bool called = false;
    pool.parallelFor(5, 5, 10, [&](size_t, size_t) { called = true; });
    EXPECT_FALSE(called);
}

TEST(TaskPoolTests, SingleElement) {
    TaskPool pool(4);
    bool called = false;
    pool.parallelFor(0, 1, 1, [&](size_t b, size_t e) {
        EXPECT_EQ(b, 0u);
        EXPECT_EQ(e, 1u);
        called = true;
    });
    EXPECT_TRUE(called);
}

TEST(TaskPoolTests, GrainLargerThanRange) {
    TaskPool pool(4);
    std::atomic<int> callCount{0};
    pool.parallelFor(0, 3, 1000, [&](size_t b, size_t e) {
        EXPECT_EQ(b, 0u);
        EXPECT_EQ(e, 3u);
        callCount++;
    });
    EXPECT_EQ(callCount.load(), 1);
}

// ============================================================================
// Exception propagation
// ============================================================================

TEST(TaskPoolTests, ExceptionPropagates) {
    TaskPool pool(4);
    EXPECT_THROW(
        pool.parallelFor(0, 100, 10, [](size_t b, size_t) {
            if (b == 20) throw std::runtime_error("worker error");
        }),
        std::runtime_error
    );
}

TEST(TaskPoolTests, AfterExceptionPoolStillUsable) {
    TaskPool pool(2);
    try {
        pool.parallelFor(0, 50, 5, [](size_t b, size_t) {
            if (b == 25) throw std::logic_error("test");
        });
    } catch (const std::logic_error&) {}

    // Pool should still function after an exception
    std::atomic<int64_t> sum{0};
    pool.parallelFor(0, 100, 10, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) sum.fetch_add(1, std::memory_order_relaxed);
    });
    EXPECT_EQ(sum.load(), 100);
}

// ============================================================================
// Thread count
// ============================================================================

TEST(TaskPoolTests, ThreadCountRespected) {
    TaskPool pool4(4);
    EXPECT_EQ(pool4.threadCount(), 4u);
    TaskPool pool1(1);
    EXPECT_EQ(pool1.threadCount(), 1u);
}

TEST(TaskPoolTests, DefaultThreadCountAtLeast1) {
    TaskPool pool; // threadCount=0 → hardware_concurrency-1, min 1
    EXPECT_GE(pool.threadCount(), 1u);
}

// ============================================================================
// Slab boundary correctness: slabs must be contiguous and cover the whole range
// ============================================================================

TEST(TaskPoolTests, SlabsCoverEntireRange) {
    TaskPool pool(4);
    constexpr size_t kN = 1000;
    constexpr size_t kGrain = 77;
    std::vector<int> hits(kN, 0);

    pool.parallelFor(0, kN, kGrain, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            hits[i]++; // NOLINT
        }
    });

    for (size_t i = 0; i < kN; ++i) {
        EXPECT_EQ(hits[i], 1) << "element " << i << " hit " << hits[i] << " times";
    }
}

// ============================================================================
// Regression: stale-worker ABA across rapid successive jobs.
// A worker that snapshots job N but is preempted before claiming a slab must
// not execute against job N+1's reset slab counter (it would call job N's
// destroyed fn). Many tiny back-to-back jobs maximize the window.
// ============================================================================

TEST(TaskPoolTests, RapidSuccessiveJobsNoStaleWorkerCrash) {
    TaskPool pool; // full thread count to maximize contention
    constexpr int kJobs = 4000;
    constexpr size_t kN = 64;

    std::vector<uint64_t> out(kN, 0);
    for (int j = 0; j < kJobs; ++j) {
        const uint64_t sentinel = static_cast<uint64_t>(j) + 1;
        pool.parallelFor(0, kN, 8, [&out, sentinel](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                out[i] = sentinel; // NOLINT
            }
        });
        for (size_t i = 0; i < kN; ++i) {
            ASSERT_EQ(out[i], sentinel) << "job " << j << " element " << i;
        }
    }
}

TEST(TaskPoolTests, PoolChurnConstructDestroy) {
    for (int i = 0; i < 50; ++i) {
        TaskPool pool(8);
        std::atomic<size_t> sum{0};
        pool.parallelFor(0, 100, 7, [&](size_t b, size_t e) {
            sum.fetch_add(e - b, std::memory_order_relaxed);
        });
        ASSERT_EQ(sum.load(), 100u);
    }
}
