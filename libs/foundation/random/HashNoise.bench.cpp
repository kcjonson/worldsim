#include "HashNoise.h"
#include <benchmark/benchmark.h>

using namespace foundation;

static void BM_Hash3(benchmark::State& state) {
    int32_t x = 12345, y = -67890, z = 11111;
    uint32_t seed = 42;
    for (auto _ : state) {
        benchmark::DoNotOptimize(hash3(x++, y, z, seed));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Hash3);

static void BM_ValueNoise3(benchmark::State& state) {
    float x = 0.5F, y = 1.7F, z = -0.3F;
    for (auto _ : state) {
        benchmark::DoNotOptimize(valueNoise3(x, y, z, 0));
        x += 0.001F;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ValueNoise3);

static void BM_GradientNoise3(benchmark::State& state) {
    float x = 0.5F, y = 1.7F, z = -0.3F;
    for (auto _ : state) {
        benchmark::DoNotOptimize(gradientNoise3(x, y, z, 0));
        x += 0.001F;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_GradientNoise3);

static void BM_FractalNoise3_4Oct(benchmark::State& state) {
    float x = 0.5F, y = 1.7F, z = -0.3F;
    for (auto _ : state) {
        benchmark::DoNotOptimize(fractalNoise3(x, y, z, 0, 4, 2.0F, 0.5F));
        x += 0.001F;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_FractalNoise3_4Oct);

static void BM_RidgedNoise3_4Oct(benchmark::State& state) {
    float x = 0.5F, y = 1.7F, z = -0.3F;
    for (auto _ : state) {
        benchmark::DoNotOptimize(ridgedNoise3(x, y, z, 0, 4, 2.0F, 0.5F));
        x += 0.001F;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RidgedNoise3_4Oct);
