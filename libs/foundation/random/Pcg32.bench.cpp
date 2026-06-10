#include "Pcg32.h"
#include <benchmark/benchmark.h>

using namespace foundation;

static void BM_Pcg32NextUInt32(benchmark::State& state) {
    Pcg32 rng(42);
    for (auto _ : state) {
        benchmark::DoNotOptimize(rng.nextUInt32());
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Pcg32NextUInt32);

static void BM_Pcg32NextFloat(benchmark::State& state) {
    Pcg32 rng(42);
    for (auto _ : state) {
        benchmark::DoNotOptimize(rng.nextFloat());
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Pcg32NextFloat);

static void BM_Pcg32NextRange(benchmark::State& state) {
    Pcg32 rng(42);
    for (auto _ : state) {
        benchmark::DoNotOptimize(rng.nextRange(100));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Pcg32NextRange);
