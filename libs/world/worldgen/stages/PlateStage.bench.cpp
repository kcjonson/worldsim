// PlateStage benchmark — M3a.
// Measures the real Dijkstra-based plate generation at n=256 and n=512
// to project n=1024 performance.

#include "worldgen/pipeline/PlanetGenerator.h"
#include "worldgen/data/PlanetParams.h"

#include <benchmark/benchmark.h>

#include <chrono>
#include <thread>

namespace worldgen {

namespace {

void runPipeline(uint32_t n, uint64_t seed) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision    = n;
    params.seed               = seed;
    params.tectonicPlateCount = 12;

    PlanetGenerator gen;
    gen.start(params);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(600);
    while (std::chrono::steady_clock::now() < deadline) {
        auto prog = gen.progress();
        if (prog.state == GenerationProgress::State::Complete ||
            prog.state == GenerationProgress::State::Failed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

} // namespace

// n=256: 655,360 tiles
static void BM_PlateStage_n256(benchmark::State& state) {
    for (auto _ : state) {
        runPipeline(256, 0xDEADC0DEBEEFC0DEULL);
    }
    state.SetLabel("n=256, 655360 tiles (real Dijkstra plates)");
}
BENCHMARK(BM_PlateStage_n256)->Unit(benchmark::kSecond)->Iterations(1);

// n=512: 2,621,440 tiles
static void BM_PlateStage_n512(benchmark::State& state) {
    for (auto _ : state) {
        runPipeline(512, 0xFEEDFACECAFEBABEULL);
    }
    state.SetLabel("n=512, 2621440 tiles (real Dijkstra plates)");
}
BENCHMARK(BM_PlateStage_n512)->Unit(benchmark::kSecond)->Iterations(1);

} // namespace worldgen
