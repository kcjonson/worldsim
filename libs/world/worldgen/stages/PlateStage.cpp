// STUB stage: replaced in M3a — produces placeholder data so downstream tracks can build.

#include "worldgen/stages/PlateStage.h"

#include <random/HashNoise.h>
#include <random/Pcg32.h>
#include <math/DeterministicMath.h>

#include <cmath>

namespace worldgen {

namespace {
constexpr size_t kGrainSize = 4096;
}

void PlateStage::run(StageContext& ctx) {
    const uint32_t totalTiles = ctx.grid.tileCount();
    const int plateCount = ctx.params.tectonicPlateCount;
    const auto seed32 = static_cast<uint32_t>(ctx.stageSeed ^ (ctx.stageSeed >> 32));

    ctx.pool.parallelFor(0, totalTiles, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        for (size_t t = begin; t < end; ++t) {
            // Assign plate id: hash of tile / (tileCount/plateCount), giving roughly
            // equal-sized fake plates. 10 plates maximum for stub.
            uint32_t tid = static_cast<uint32_t>(t);
            // Use rhombus index (upper bits of tid) to get 10 coarse regions,
            // then sub-assign within each to spread across plateCount.
            uint32_t coarse = foundation::hash3(
                static_cast<int32_t>(tid & 0xFFFF),
                static_cast<int32_t>(tid >> 16),
                0, seed32);
            ctx.data.plateId[t] = static_cast<uint8_t>(coarse % static_cast<uint32_t>(plateCount));
        }
        // Report progress at end of each slab
        ctx.reportProgress(static_cast<float>(end) / static_cast<float>(totalTiles));
    });

    // Build fake PlateInfo entries (random Euler poles)
    ctx.world.plates.resize(static_cast<size_t>(plateCount));
    foundation::Pcg32 rng(ctx.stageSeed);
    constexpr double kPi = 3.14159265358979323846;
    for (int p = 0; p < plateCount; ++p) {
        // Random point on sphere via rejection sampling
        double x{}, y{}, z{};
        for (;;) {
            x = rng.nextFloat() * 2.0 - 1.0;
            y = rng.nextFloat() * 2.0 - 1.0;
            z = rng.nextFloat() * 2.0 - 1.0;
            double r2 = x*x + y*y + z*z;
            if (r2 > 0.0001 && r2 <= 1.0) {
                double inv = 1.0 / foundation::det_math::sqrt(r2);
                x *= inv; y *= inv; z *= inv;
                break;
            }
        }
        ctx.world.plates[p].eulerPole    = {x, y, z};
        ctx.world.plates[p].angularSpeed = rng.nextFloat() * 0.05f + 0.005f;
        ctx.world.plates[p].isContinental = (rng.nextUInt32() & 1) != 0;
    }

    ctx.world.validFields |= static_cast<uint32_t>(WorldField::PlateId);
}

} // namespace worldgen
