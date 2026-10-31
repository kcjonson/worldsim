#include "worldgen/grid/SphereGrid.h"
#include "worldgen/data/PlanetParams.h"
#include "worldgen/pipeline/PlanetGenerator.h"

#include <benchmark/benchmark.h>

#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

namespace worldgen {

namespace {

// Simple LCG for generating test vectors in the benchmark (not worldgen code)
std::vector<Vec3d> makeRandomDirs(size_t count, uint64_t seed) {
    uint64_t s = seed;
    std::vector<Vec3d> dirs;
    dirs.reserve(count);
    while (dirs.size() < count) {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        double x = static_cast<double>(static_cast<int64_t>(s)) / 9.2e18;
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        double y = static_cast<double>(static_cast<int64_t>(s)) / 9.2e18;
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        double z = static_cast<double>(static_cast<int64_t>(s)) / 9.2e18;
        double r2 = x*x + y*y + z*z;
        if (r2 > 0.01 && r2 <= 1.0) {
            double inv = 1.0 / std::sqrt(r2);
            dirs.push_back({x*inv, y*inv, z*inv});
        }
    }
    return dirs;
}

} // namespace

// ============================================================================
// fromUnitVector
// ============================================================================

static void BM_FromUnitVector_n1024(benchmark::State& state) {
    SphereGrid g(1024);
    auto dirs = makeRandomDirs(4096, 0xCAFEBABE);
    size_t i = 0;
    for (auto _ : state) {
        TileId t = g.fromUnitVector(dirs[i % dirs.size()]);
        benchmark::DoNotOptimize(t);
        ++i;
    }
}
BENCHMARK(BM_FromUnitVector_n1024);

static void BM_FromUnitVector_n256(benchmark::State& state) {
    SphereGrid g(256);
    auto dirs = makeRandomDirs(4096, 0xDEADC0DE);
    size_t i = 0;
    for (auto _ : state) {
        TileId t = g.fromUnitVector(dirs[i % dirs.size()]);
        benchmark::DoNotOptimize(t);
        ++i;
    }
}
BENCHMARK(BM_FromUnitVector_n256);

// ============================================================================
// tileCenter
// ============================================================================

static void BM_TileCenter_n1024(benchmark::State& state) {
    SphereGrid g(1024);
    uint32_t total = g.tileCount();
    uint32_t i = 0;
    for (auto _ : state) {
        Vec3d c = g.tileCenter(i % total);
        benchmark::DoNotOptimize(c);
        ++i;
    }
}
BENCHMARK(BM_TileCenter_n1024);

// ============================================================================
// neighbors
// ============================================================================

static void BM_Neighbors_n1024(benchmark::State& state) {
    SphereGrid g(1024);
    uint32_t total = g.tileCount();
    uint32_t i = 0;
    for (auto _ : state) {
        std::array<TileId, 6> nbrs{};
        uint32_t cnt = g.neighbors(i % total, nbrs);
        benchmark::DoNotOptimize(cnt);
        ++i;
    }
}
BENCHMARK(BM_Neighbors_n1024);

// ============================================================================
// Full stub pipeline at n=256
// ============================================================================

static void BM_PipelineN256(benchmark::State& state) {
    for (auto _ : state) {
        PlanetParams params = PlanetParams::preset(Preset::EarthLike);
        params.gridSubdivision = 256;
        params.seed = 0x1234ABCD5678EFULL;

        PlanetGenerator gen;
        gen.start(params);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        while (std::chrono::steady_clock::now() < deadline) {
            auto prog = gen.progress();
            if (prog.state == GenerationProgress::State::Complete ||
                prog.state == GenerationProgress::State::Failed) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    state.SetLabel("n=256, 655360 tiles");
}
BENCHMARK(BM_PipelineN256)->Unit(benchmark::kSecond)->Iterations(1);

} // namespace worldgen
