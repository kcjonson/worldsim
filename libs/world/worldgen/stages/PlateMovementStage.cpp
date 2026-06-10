// PlateMovementStage — M3a implementation.
//
// Assigns a random Euler pole + angular speed to each plate and momentum-balances
// the result so the area-weighted mean angular velocity vector ≈ 0 (no net rotation).
//
// Velocity at tile t: v = omega × tileCenter(t), computed lazily by consumers.
// This stage stores omega = pole * angularSpeed in each PlateInfo.
//
// Momentum balance rationale:
//   Real plates drift rather than the planet spinning collectively. We subtract the
//   area-weighted mean of all omegas so net rotation is zero to numerical precision.
//   Any non-zero residual would bias boundary velocity calculations globally.
//
// Planet-age scaling: younger planets have faster plates (hotter mantle, higher
//   convection). Factor = clamp(2.0 - age/4.5e9, 0.5, 2.0) gives:
//     age=0   → factor=2.0 (maximum speed)
//     age=4.5e9 (Earth) → factor=1.0 (nominal)
//     age=9e9+ → factor=0.5 (old, slow)

#include "worldgen/stages/PlateMovementStage.h"

#include "worldgen/data/WorldData.h"

#include <math/DeterministicMath.h>
#include <random/Pcg32.h>

#include <cmath>

namespace worldgen {

void PlateMovementStage::run(StageContext& ctx) {
    const int K = static_cast<int>(ctx.world.plates.size());
    if (K == 0) return;

    // Planet-age speed multiplier: clamp(2.0 - age/4.5e9, 0.5, 2.0)
    double ageFactor = 2.0 - ctx.params.planetAge / 4.5e9;
    if (ageFactor < 0.5) ageFactor = 0.5;
    if (ageFactor > 2.0) ageFactor = 2.0;

    foundation::Pcg32 rng(ctx.stageSeed);

    // Assign random Euler poles and angular speeds.
    // Pole: uniform on sphere via rejection sampling.
    // Speed: uniform in [0.4, 1.0] * ageFactor (radians/Ma, nominal Earth range).
    for (int p = 0; p < K; ++p) {
        double px{}, py{}, pz{};
        for (;;) {
            px = rng.nextDouble() * 2.0 - 1.0;
            py = rng.nextDouble() * 2.0 - 1.0;
            pz = rng.nextDouble() * 2.0 - 1.0;
            double r2 = px*px + py*py + pz*pz;
            if (r2 > 0.0001 && r2 <= 1.0) {
                double inv = 1.0 / foundation::det_math::sqrt(r2);
                px *= inv; py *= inv; pz *= inv;
                break;
            }
        }
        double speed = (0.4 + rng.nextDouble() * 0.6) * ageFactor;

        ctx.world.plates[static_cast<size_t>(p)].eulerPole    = {px, py, pz};
        ctx.world.plates[static_cast<size_t>(p)].angularSpeed = static_cast<float>(speed);
    }

    ctx.reportProgress(0.4f);

    // Count tiles per plate for area weighting.
    const uint32_t N = ctx.grid.tileCount();
    std::vector<uint32_t> plateArea(static_cast<size_t>(K), 0u);
    for (uint32_t t = 0; t < N; ++t) {
        uint8_t pid = ctx.data.plateId[t];
        if (pid < static_cast<uint8_t>(K)) {
            plateArea[static_cast<size_t>(pid)]++;
        }
    }

    // Compute area-weighted mean omega = sum(area_p * omega_p) / sum(area_p).
    // omega_p = eulerPole_p * angularSpeed_p (vector form, stored in PlateInfo fields).
    double sumArea = 0.0;
    double meanOx = 0.0, meanOy = 0.0, meanOz = 0.0;
    for (int p = 0; p < K; ++p) {
        double area = static_cast<double>(plateArea[static_cast<size_t>(p)]);
        const auto& pl = ctx.world.plates[static_cast<size_t>(p)];
        double omega = static_cast<double>(pl.angularSpeed);
        meanOx += area * pl.eulerPole.x * omega;
        meanOy += area * pl.eulerPole.y * omega;
        meanOz += area * pl.eulerPole.z * omega;
        sumArea += area;
    }
    if (sumArea > 0.0) {
        meanOx /= sumArea;
        meanOy /= sumArea;
        meanOz /= sumArea;
    }

    // Subtract the mean omega vector from each plate's omega.
    // New omega_p' = omega_p - mean_omega.
    // Decompose back into pole + speed (pole = normalized omega', speed = |omega'|).
    // If speed drops to zero or negative (unlikely with physical parameters), clamp to a
    // tiny positive value so the pole direction remains defined.
    for (int p = 0; p < K; ++p) {
        auto& pl = ctx.world.plates[static_cast<size_t>(p)];
        double omega = static_cast<double>(pl.angularSpeed);
        double ox = pl.eulerPole.x * omega - meanOx;
        double oy = pl.eulerPole.y * omega - meanOy;
        double oz = pl.eulerPole.z * omega - meanOz;

        double newSpeed = foundation::det_math::sqrt(ox*ox + oy*oy + oz*oz);
        if (newSpeed < 1e-12) {
            // Vanishingly rare: just keep original pole, zero speed
            pl.angularSpeed = 0.0f;
        } else {
            double inv = 1.0 / newSpeed;
            pl.eulerPole    = {ox*inv, oy*inv, oz*inv};
            pl.angularSpeed = static_cast<float>(newSpeed);
        }
    }

    // BoundaryType and BoundaryDistance arrays are initialized to 0 (interior, distance=0)
    // by WorldData::allocate. Real classification is M3b's job; mark them valid here so
    // downstream stubs (which depend on the field-validity bitmask) can proceed.
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::BoundaryType);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::BoundaryDistance);

    ctx.reportProgress(1.0f);
}

} // namespace worldgen
