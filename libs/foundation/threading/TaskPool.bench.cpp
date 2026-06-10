#include "TaskPool.h"
#include <benchmark/benchmark.h>
#include <atomic>
#include <numeric>
#include <vector>

using namespace foundation;

// Trivial sum of 1M elements: measures scheduling overhead vs serial
static void BM_ParallelForVsSerial_1M(benchmark::State& state) {
    constexpr size_t kN = 1000000;
    const size_t grain = static_cast<size_t>(state.range(0));
    std::vector<int64_t> data(kN, 1);

    unsigned threads = static_cast<unsigned>(state.range(1));
    TaskPool pool(threads);

    for (auto _ : state) {
        std::atomic<int64_t> sum{0};
        pool.parallelFor(0, kN, grain, [&](size_t b, size_t e) {
            int64_t local = 0;
            for (size_t i = b; i < e; ++i) local += data[i]; // NOLINT
            sum.fetch_add(local, std::memory_order_relaxed);
        });
        benchmark::DoNotOptimize(sum.load());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(kN));
}
// grain=1000, 1 thread (baseline) vs 4 threads
BENCHMARK(BM_ParallelForVsSerial_1M)->Args({1000, 1})->Args({1000, 4});

static void BM_SerialSum_1M(benchmark::State& state) {
    constexpr size_t kN = 1000000;
    std::vector<int64_t> data(kN, 1);
    for (auto _ : state) {
        int64_t sum = 0;
        for (size_t i = 0; i < kN; ++i) sum += data[i]; // NOLINT
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(kN));
}
BENCHMARK(BM_SerialSum_1M);
