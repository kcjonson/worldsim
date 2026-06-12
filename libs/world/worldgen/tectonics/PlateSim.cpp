// PlateSim — coarse time-stepped tectonic core (M-T1).
//
// State lives per-plate in plate-local rasters (Tectonics.js style). Each plate
// carries a cumulative rotation quaternion; every step re-rasterizes world state
// from scratch (forward), so there is no incremental resampling drift.
//
// Determinism: single-threaded, fixed iteration order, det_math transcendentals,
// Pcg32 with derived stream seeds. See step() for the operation-order contract.

#include "worldgen/tectonics/PlateSim.h"

#include <math/DeterministicMath.h>
#include <random/HashNoise.h>
#include <random/Pcg32.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <queue>

namespace worldgen::tectonics {

namespace {

// Derive an independent 64-bit stream seed from a base seed and a salt.
// SplitMix-style: the repo's stages split stageSeed by xor with large odd
// constants (see PlateStage); we extend that to 64-bit streams here.
uint64_t deriveSeed(uint64_t base, uint64_t salt) {
    uint64_t z = base + salt + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// cm/yr surface speed -> rad/Myr angular rate on a sphere of radius R (km).
// v[cm/yr] = 1e-5 km/yr = 10 km/Myr per cm/yr; omega = v / R.
double cmYrToRadPerMyr(double cmYr, double radiusKm) {
    constexpr double kKmPerMyrPerCmYr = 10.0; // 1 cm/yr = 10 km/Myr
    return (cmYr * kKmPerMyrPerCmYr) / radiusKm;
}

struct HeapEntry {
    float    cost;
    uint32_t plate;
    uint32_t tile;
    bool operator>(const HeapEntry& o) const {
        if (cost != o.cost) return cost > o.cost;
        if (plate != o.plate) return plate > o.plate;
        return tile > o.tile;
    }
};

inline float boundaryNoise(float x, float y, float z, uint32_t seed) {
    float n = foundation::fractalNoise3(x, y, z, seed, 3, 2.0f, 0.5f);
    if (n < -1.0f) n = -1.0f;
    if (n > 1.0f) n = 1.0f;
    return 0.5f + 0.5f * n;
}

constexpr float kBoundaryNoiseFreq = 3.5f;
constexpr float kCratonNoiseFreq   = 5.5f;
constexpr uint32_t kUnowned        = 255u;

} // namespace

// ============================================================================
// Quaternion helpers
// ============================================================================

void PlateSim::quatFromAxisAngle(const Vec3d& axis, double angle, double q[4]) {
    double half = angle * 0.5;
    double s = foundation::det_math::sin(half);
    double c = foundation::det_math::cos(half);
    q[0] = c;
    q[1] = axis.x * s;
    q[2] = axis.y * s;
    q[3] = axis.z * s;
}

// out = a * b (Hamilton product, fixed term order).
void PlateSim::quatMul(const double a[4], const double b[4], double out[4]) {
    double w = a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
    double x = a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2];
    double y = a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1];
    double z = a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0];
    out[0] = w; out[1] = x; out[2] = y; out[3] = z;
}

Vec3d PlateSim::quatRotate(const double q[4], const Vec3d& v) {
    // v' = q * (0,v) * conj(q), expanded for fixed op order.
    double w = q[0], x = q[1], y = q[2], z = q[3];
    // t = 2 * cross(q.xyz, v)
    double tx = 2.0 * (y * v.z - z * v.y);
    double ty = 2.0 * (z * v.x - x * v.z);
    double tz = 2.0 * (x * v.y - y * v.x);
    // v' = v + w*t + cross(q.xyz, t)
    double rx = v.x + w * tx + (y * tz - z * ty);
    double ry = v.y + w * ty + (z * tx - x * tz);
    double rz = v.z + w * tz + (x * ty - y * tx);
    return {rx, ry, rz};
}

// ============================================================================
// Construction + init
// ============================================================================

PlateSim::PlateSim(const PlateSimParams& params, const PlateSimTestOverride* override)
    : cfg_(params) {
    grid_ = std::make_shared<const SphereGrid>(params.coarseN);
    tileCount_ = grid_->tileCount();
    dtMyr_ = params.dtMyr;

    centers_.resize(tileCount_);
    for (TileId t = 0; t < tileCount_; ++t) centers_[t] = grid_->tileCenter(t);

    // History length: base * clamp(planetAge/4.5e9, 0.4, 1.3) unless overridden.
    if (params.historyMyr > 0.0) {
        historyMyr_ = params.historyMyr;
    } else {
        double ageFactor = params.planetAge / kHistoryAgeRefYrs;
        if (ageFactor < kHistoryAgeMin) ageFactor = kHistoryAgeMin;
        if (ageFactor > kHistoryAgeMax) ageFactor = kHistoryAgeMax;
        historyMyr_ = kHistoryBaseMyr * ageFactor;
    }
    stepCount_ = static_cast<int>(historyMyr_ / dtMyr_ + 0.5);
    if (stepCount_ < 1) stepCount_ = 1;

    owner_.assign(tileCount_, static_cast<uint8_t>(kUnowned));
    resolved_.assign(tileCount_, CrustCell{});
    candHead_.assign(tileCount_, 0xFFFFFFFFu);
    candNext_.clear();
    candPool_.clear();
    candPool_.reserve(static_cast<size_t>(tileCount_) * 2u);
    candNext_.reserve(static_cast<size_t>(tileCount_) * 2u);
    eraseList_.reserve(tileCount_ / 4u);
    bndType_.assign(tileCount_, static_cast<uint8_t>(BoundaryType::None));
    bndSide_.assign(tileCount_, kSideSymmetric);
    bndConv_.assign(tileCount_, 0.0f);

    if (override != nullptr) {
        plates_ = override->plates;
        owner_  = override->owner;
        assert(owner_.size() == tileCount_);
        // Build occupied lists from the supplied rasters.
        for (auto& pl : plates_) {
            pl.occupied.clear();
            if (pl.crust.size() != tileCount_) pl.crust.resize(tileCount_);
            for (TileId t = 0; t < tileCount_; ++t)
                if (pl.crust[t].type != CrustType::None) pl.occupied.push_back(t);
        }
        return;
    }

    plates_.assign(static_cast<size_t>(params.plateCount), SimPlate{});

    // Stream seeds, one per init phase, all derived from the master seed.
    seedAndGrowPlates(deriveSeed(params.seed, 0x01));
    paintContinents(deriveSeed(params.seed, 0x02));
    assignEulerPoles(deriveSeed(params.seed, 0x03));
    initPlateRasters();
}

// ---- K seed tiles + multi-source Dijkstra (compact port of PlateStage) ----
void PlateSim::seedAndGrowPlates(uint64_t seed) {
    const uint32_t N = tileCount_;
    const int K = cfg_.plateCount;
    foundation::Pcg32 rng(seed);
    const auto seed32 = static_cast<uint32_t>(seed ^ (seed >> 32));
    const uint32_t noiseSeed = seed32 ^ 0x9E3779B9u;

    // Jittered spherical-Fibonacci seeds with a random rotation, snapped to tiles.
    auto randUnit = [&]() -> Vec3d {
        for (;;) {
            double x = rng.nextDouble() * 2.0 - 1.0;
            double y = rng.nextDouble() * 2.0 - 1.0;
            double z = rng.nextDouble() * 2.0 - 1.0;
            double r2 = x * x + y * y + z * z;
            if (r2 > 1e-4 && r2 <= 1.0) {
                double inv = 1.0 / foundation::det_math::sqrt(r2);
                return {x * inv, y * inv, z * inv};
            }
        }
    };
    // Build an orthonormal rotation frame from two random unit vectors.
    Vec3d u = randUnit();
    Vec3d a = randUnit();
    double dotua = u.x * a.x + u.y * a.y + u.z * a.z;
    Vec3d v{a.x - dotua * u.x, a.y - dotua * u.y, a.z - dotua * u.z};
    double vl = foundation::det_math::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (vl < 1e-9) { v = {0, 1, 0}; vl = 1.0; }
    v = {v.x / vl, v.y / vl, v.z / vl};
    Vec3d w{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};

    constexpr double kGoldenAngle = 2.3999632297286531;
    constexpr double kTwoPiRecip  = 0.15915494309189534;

    std::vector<TileId> seedTile(static_cast<size_t>(K), kInvalidTile);
    std::vector<bool> used(N, false);
    for (int p = 0; p < K; ++p) {
        double t = (static_cast<double>(p) + 0.5) / static_cast<double>(K);
        double z = 1.0 - 2.0 * t;
        double sinTheta = foundation::det_math::sqrt(1.0 - z * z);
        double phi = kGoldenAngle * static_cast<double>(p);
        double phiR = phi - static_cast<double>(static_cast<int64_t>(phi * kTwoPiRecip)) * 6.2831853071795865;
        double bx = sinTheta * foundation::det_math::cos(phiR);
        double by = sinTheta * foundation::det_math::sin(phiR);
        double bz = z;
        // rotate base into the random frame: columns u,v,w
        Vec3d dir{u.x * bx + v.x * by + w.x * bz,
                  u.y * bx + v.y * by + w.y * bz,
                  u.z * bx + v.z * by + w.z * bz};
        TileId tile = grid_->fromUnitVector(dir);
        uint32_t iter = 0;
        while (tile == kInvalidTile || used[tile]) {
            ++iter;
            uint32_t h = foundation::hash3(p, static_cast<int32_t>(iter), 0, seed32);
            double jx = static_cast<double>(static_cast<int32_t>(h)) * (1.0 / 2147483648.0) * 0.05;
            double jy = static_cast<double>(static_cast<int32_t>(h >> 1)) * (1.0 / 2147483648.0) * 0.05;
            double jz = static_cast<double>(static_cast<int32_t>(h >> 2)) * (1.0 / 2147483648.0) * 0.05;
            double nx = dir.x + jx, ny = dir.y + jy, nz = dir.z + jz;
            double nl = foundation::det_math::sqrt(nx * nx + ny * ny + nz * nz);
            if (nl < 1e-12) continue;
            tile = grid_->fromUnitVector({nx / nl, ny / nl, nz / nl});
        }
        seedTile[static_cast<size_t>(p)] = tile;
        used[tile] = true;
    }

    // Per-plate growth rate [0.6, 2.4] -> lopsided Voronoi areas.
    std::vector<float> growth(static_cast<size_t>(K));
    for (int p = 0; p < K; ++p) growth[static_cast<size_t>(p)] = 0.6f + rng.nextFloat() * 1.8f;

    std::vector<float> best(N, 1e30f);
    std::vector<bool> visited(N, false);
    std::fill(owner_.begin(), owner_.end(), static_cast<uint8_t>(kUnowned));
    std::priority_queue<HeapEntry, std::vector<HeapEntry>, std::greater<HeapEntry>> pq;
    for (int p = 0; p < K; ++p) {
        TileId t = seedTile[static_cast<size_t>(p)];
        best[t] = 0.0f;
        owner_[t] = static_cast<uint8_t>(p);
        pq.push({0.0f, static_cast<uint32_t>(p), t});
    }
    std::array<TileId, 6> nbrs{};
    while (!pq.empty()) {
        HeapEntry cur = pq.top();
        pq.pop();
        if (visited[cur.tile]) continue;
        visited[cur.tile] = true;
        owner_[cur.tile] = static_cast<uint8_t>(cur.plate);
        uint32_t cnt = grid_->neighbors(cur.tile, nbrs);
        for (uint32_t k = 0; k < cnt; ++k) {
            TileId nb = nbrs[k];
            if (visited[nb]) continue;
            Vec3d c = grid_->tileCenter(nb);
            float noise = boundaryNoise(static_cast<float>(c.x) * kBoundaryNoiseFreq,
                                        static_cast<float>(c.y) * kBoundaryNoiseFreq,
                                        static_cast<float>(c.z) * kBoundaryNoiseFreq, noiseSeed);
            float step = (1.0f + 0.9f * noise) / growth[cur.plate];
            float nc = cur.cost + step;
            if (nc < best[nb]) { best[nb] = nc; pq.push({nc, cur.plate, nb}); }
        }
    }
}

// ---- Continental crust: supercontinent + microcontinents via noisy Dijkstra ----
void PlateSim::paintContinents(uint64_t seed) {
    const uint32_t N = tileCount_;
    const int K = cfg_.plateCount;
    foundation::Pcg32 rng(seed);
    const auto seed32 = static_cast<uint32_t>(seed ^ (seed >> 32));

    // Total continental area target. (1 - water) * 1.12 * N.
    const double target = (1.0 - cfg_.waterAmount) * kCrustAreaFactor * static_cast<double>(N);
    const uint32_t budget = static_cast<uint32_t>(target);

    // Craton seeds: one supercontinent + (1 + K/6) microcontinents. Place the
    // supercontinent seed first, then microcontinent seeds at random tiles. The
    // supercontinent gets the lion's share of the budget via a larger growth weight.
    int microCount = 1 + K / 6;
    int cratonCount = 1 + microCount; // supercontinent + micros
    if (cratonCount < 1) cratonCount = 1;

    struct Craton { TileId seed; double weight; };
    std::vector<Craton> cratons;
    cratons.reserve(static_cast<size_t>(cratonCount));

    // Supercontinent seed: a random tile.
    cratons.push_back({rng.nextRange(N), 1.0});
    // Microcontinent seeds.
    for (int m = 0; m < microCount; ++m) {
        cratons.push_back({rng.nextRange(N), 0.18 + rng.nextDouble() * 0.22});
    }

    // Weighted budget split: supercontinent ~ 60-70%, micros share the rest.
    double weightSum = 0.0;
    for (const auto& c : cratons) weightSum += c.weight;
    std::vector<uint32_t> cratonBudget(cratons.size(), 0u);
    for (size_t i = 0; i < cratons.size(); ++i) {
        cratonBudget[i] = static_cast<uint32_t>(static_cast<double>(budget) * cratons[i].weight / weightSum);
    }

    std::vector<float> cost(N, 1e30f);
    std::vector<bool>  isCont(N, false);
    std::vector<uint32_t> accepted(cratons.size(), 0u);
    std::priority_queue<HeapEntry, std::vector<HeapEntry>, std::greater<HeapEntry>> pq;
    for (size_t i = 0; i < cratons.size(); ++i) {
        TileId t = cratons[i].seed;
        if (cost[t] > 0.0f) { cost[t] = 0.0f; pq.push({0.0f, static_cast<uint32_t>(i), t}); }
    }
    std::array<TileId, 6> nbrs{};
    while (!pq.empty()) {
        HeapEntry cur = pq.top();
        pq.pop();
        if (isCont[cur.tile]) continue;
        uint32_t ci = cur.plate;
        if (accepted[ci] >= cratonBudget[ci]) continue;
        isCont[cur.tile] = true;
        accepted[ci]++;
        uint32_t cnt = grid_->neighbors(cur.tile, nbrs);
        for (uint32_t k = 0; k < cnt; ++k) {
            TileId nb = nbrs[k];
            if (isCont[nb]) continue;
            if (accepted[ci] >= cratonBudget[ci]) break;
            Vec3d c = grid_->tileCenter(nb);
            float noise = boundaryNoise(static_cast<float>(c.x) * kCratonNoiseFreq,
                                        static_cast<float>(c.y) * kCratonNoiseFreq,
                                        static_cast<float>(c.z) * kCratonNoiseFreq, seed32);
            float step = 1.0f + 0.7f * noise;
            float nc = cur.cost + step;
            if (nc < cost[nb]) { cost[nb] = nc; pq.push({nc, ci, nb}); }
        }
    }

    // Stash continental flag transiently in resolved_ crust type so initPlateRasters
    // can read it. We use a temporary array instead to keep resolved_ clean.
    continentalMask_ = std::move(isCont);

    // Majority crust per plate determines isContinental.
    std::vector<uint32_t> contCount(static_cast<size_t>(K), 0u);
    std::vector<uint32_t> totCount(static_cast<size_t>(K), 0u);
    for (uint32_t t = 0; t < N; ++t) {
        uint8_t pid = owner_[t];
        if (pid >= static_cast<uint8_t>(K)) continue;
        totCount[pid]++;
        if (continentalMask_[t]) contCount[pid]++;
    }
    for (int p = 0; p < K; ++p) {
        plates_[static_cast<size_t>(p)].isContinental =
            (totCount[static_cast<size_t>(p)] > 0 &&
             contCount[static_cast<size_t>(p)] * 2u > totCount[static_cast<size_t>(p)]);
    }
}

// ---- Euler poles uniform on sphere + area-weighted momentum balance ----
void PlateSim::assignEulerPoles(uint64_t seed) {
    const uint32_t N = tileCount_;
    const int K = cfg_.plateCount;
    foundation::Pcg32 rng(seed);

    double ageFactor = 2.0 - cfg_.planetAge / kHistoryAgeRefYrs;
    if (ageFactor < kSpeedAgeMin) ageFactor = kSpeedAgeMin;
    if (ageFactor > kSpeedAgeMax) ageFactor = kSpeedAgeMax;

    for (int p = 0; p < K; ++p) {
        double px{}, py{}, pz{};
        for (;;) {
            px = rng.nextDouble() * 2.0 - 1.0;
            py = rng.nextDouble() * 2.0 - 1.0;
            pz = rng.nextDouble() * 2.0 - 1.0;
            double r2 = px * px + py * py + pz * pz;
            if (r2 > 1e-4 && r2 <= 1.0) {
                double inv = 1.0 / foundation::det_math::sqrt(r2);
                px *= inv; py *= inv; pz *= inv;
                break;
            }
        }
        bool cont = plates_[static_cast<size_t>(p)].isContinental;
        double lo = cont ? kContinentalSpeedMinCmYr : kOceanicSpeedMinCmYr;
        double hi = cont ? kContinentalSpeedMaxCmYr : kOceanicSpeedMaxCmYr;
        double cmYr = (lo + rng.nextDouble() * (hi - lo)) * ageFactor;
        double omega = cmYrToRadPerMyr(cmYr, cfg_.planetRadiusKm);
        plates_[static_cast<size_t>(p)].eulerPole = {px, py, pz};
        plates_[static_cast<size_t>(p)].omegaRadPerMyr = omega;
    }

    // Area-weighted momentum balance: subtract the area-weighted mean omega vector
    // so net rotation is ~0. (Ports PlateMovementStage.)
    std::vector<uint32_t> area(static_cast<size_t>(K), 0u);
    for (uint32_t t = 0; t < N; ++t) {
        uint8_t pid = owner_[t];
        if (pid < static_cast<uint8_t>(K)) area[pid]++;
    }
    double sumA = 0.0, mx = 0.0, my = 0.0, mz = 0.0;
    for (int p = 0; p < K; ++p) {
        double a = static_cast<double>(area[static_cast<size_t>(p)]);
        const auto& pl = plates_[static_cast<size_t>(p)];
        mx += a * pl.eulerPole.x * pl.omegaRadPerMyr;
        my += a * pl.eulerPole.y * pl.omegaRadPerMyr;
        mz += a * pl.eulerPole.z * pl.omegaRadPerMyr;
        sumA += a;
    }
    if (sumA > 0.0) { mx /= sumA; my /= sumA; mz /= sumA; }
    for (int p = 0; p < K; ++p) {
        auto& pl = plates_[static_cast<size_t>(p)];
        double ox = pl.eulerPole.x * pl.omegaRadPerMyr - mx;
        double oy = pl.eulerPole.y * pl.omegaRadPerMyr - my;
        double oz = pl.eulerPole.z * pl.omegaRadPerMyr - mz;
        double sp = foundation::det_math::sqrt(ox * ox + oy * oy + oz * oz);
        if (sp < 1e-15) {
            pl.omegaRadPerMyr = 0.0;
        } else {
            pl.eulerPole = {ox / sp, oy / sp, oz / sp};
            pl.omegaRadPerMyr = sp;
        }
    }
}

// ---- Initialize each plate's local raster from world ownership ----
void PlateSim::initPlateRasters() {
    const uint32_t N = tileCount_;
    const int K = cfg_.plateCount;
    const auto seed32 = static_cast<uint32_t>(deriveSeed(cfg_.seed, 0x04));

    for (int p = 0; p < K; ++p) {
        plates_[static_cast<size_t>(p)].crust.assign(N, CrustCell{});
        plates_[static_cast<size_t>(p)].alive = true;
        plates_[static_cast<size_t>(p)].rotation[0] = 1.0;
        plates_[static_cast<size_t>(p)].rotation[1] = 0.0;
        plates_[static_cast<size_t>(p)].rotation[2] = 0.0;
        plates_[static_cast<size_t>(p)].rotation[3] = 0.0;
    }

    for (uint32_t t = 0; t < N; ++t) {
        uint8_t pid = owner_[t];
        if (pid >= static_cast<uint8_t>(K)) continue;
        CrustCell& cell = plates_[pid].crust[t];
        Vec3d c = grid_->tileCenter(t);
        if (continentalMask_[t]) {
            cell.type = CrustType::Continental;
            // thickness 38 +/- 6 km via noise, birthMyr 0.
            float noise = foundation::gradientNoise3(static_cast<float>(c.x) * 4.0f,
                                                     static_cast<float>(c.y) * 4.0f,
                                                     static_cast<float>(c.z) * 4.0f, seed32);
            cell.thicknessKm = static_cast<float>(kContinentalThicknessMeanKm) +
                               noise * static_cast<float>(kContinentalThicknessSpreadKm);
            cell.birthMyr = 0;
        } else {
            cell.type = CrustType::Oceanic;
            cell.thicknessKm = static_cast<float>(kOceanicThicknessKm);
            // Pre-age: birthMyr = -(hash(tile) % maxAge) so the start isn't uniform.
            uint32_t h = foundation::hash3(static_cast<int32_t>(t), 0, 0, seed32);
            cell.birthMyr = -static_cast<int32_t>(h % static_cast<uint32_t>(kOceanInitMaxAgeMyr));
        }
        cell.orogenyMyr = kOrogenyNever;
        plates_[pid].occupied.push_back(t); // ascending t (scan order)
    }
    continentalMask_.clear();
    continentalMask_.shrink_to_fit();
}

// ============================================================================
// Per-step pipeline (the operation order IS the determinism contract)
// ============================================================================
//
// step() order:
//   1. Advance quaternions:  Qp = dQ(pole_p, omega_p*dt) * Qp.
//   2. Forward rasterize:    each alive plate ascending id, each occupied local
//                            cell -> world cell candidate list. (Cell order within
//                            a plate does not affect the result: the resolve
//                            comparator is a total order and the erase list is
//                            built in ascending world-cell order in step 3.)
//   3. Resolve ownership:    per world cell ascending, sort candidates
//                            (continental > oceanic; younger oceanic wins; tie
//                            smaller plateId). Losing oceanic -> erase list.
//   4. Apply erase list to plate-local rasters.
//   5. Gap fill ascending:   empty cells inherit majority neighbor owner and get
//                            NEW oceanic crust at birth=now (spreading ridges).
//   6. Boundary scan:        Euler-pole relative velocity, convergence, classify.
//   7. (M-T2 events)         no-op in M-T1.
//   8. cancel + progress callbacks.

void PlateSim::step(const CancelFn& cancel, const ProgressFn& progress) {
    advanceRotations();   // 1
    forwardRasterize();   // 2
    resolveOwnership();   // 3
    applyEraseList();     // 4
    gapFill();            // 5
    boundaryScan();       // 6
    // 7: M-T2 events placeholder.
    ++step_;
    nowMyr_ += dtMyr_;
    if (cancel) cancel();                                            // 8
    if (progress) progress(static_cast<float>(step_) / static_cast<float>(stepCount_));
}

void PlateSim::advanceRotations() {
    for (auto& pl : plates_) {
        if (!pl.alive || pl.omegaRadPerMyr == 0.0) continue;
        double dq[4];
        quatFromAxisAngle(pl.eulerPole, pl.omegaRadPerMyr * dtMyr_, dq);
        double out[4];
        quatMul(dq, pl.rotation, out);
        // Renormalize to keep the quaternion unit over many steps.
        double n = foundation::det_math::sqrt(out[0] * out[0] + out[1] * out[1] +
                                              out[2] * out[2] + out[3] * out[3]);
        double inv = (n > 1e-15) ? 1.0 / n : 1.0;
        pl.rotation[0] = out[0] * inv;
        pl.rotation[1] = out[1] * inv;
        pl.rotation[2] = out[2] * inv;
        pl.rotation[3] = out[3] * inv;
    }
}

void PlateSim::forwardRasterize() {
    // Reset candidate structure.
    std::fill(candHead_.begin(), candHead_.end(), 0xFFFFFFFFu);
    candPool_.clear();
    candNext_.clear();

    const int K = static_cast<int>(plates_.size());
    for (int p = 0; p < K; ++p) {
        SimPlate& pl = plates_[static_cast<size_t>(p)];
        if (!pl.alive) continue;
        const auto& crust = pl.crust;

        // Compact the occupied list in place: drop cells erased last step
        // (type None). No re-sort needed: forward-rasterize order does not affect
        // the resolved result (the resolve comparator is a total order, and the
        // erase list is built in ascending world-cell order in resolveOwnership).
        // The list stays nearly sorted (init is ascending; gapFill appends a small
        // unsorted tail), which is enough for the warm rhombus hint to hit. A cell
        // erased then re-stamped can appear twice; duplicate candidates are
        // identical and collapse harmlessly in resolveOwnership.
        auto& occ = pl.occupied;
        size_t w2 = 0;
        for (size_t i = 0; i < occ.size(); ++i) {
            if (crust[occ[i]].type != CrustType::None) occ[w2++] = occ[i];
        }
        occ.resize(w2);

        // Fold the plate's rotation quaternion into a 3x3 matrix once per step, so
        // each cell costs 9 mults instead of quaternion-rotate's ~30. Bit-identical
        // to applying the quaternion (the matrix is the exact quaternion rotation).
        const double* q = pl.rotation;
        const double w0 = q[0], xx = q[1], yy = q[2], zz = q[3];
        const double m00 = 1 - 2 * (yy * yy + zz * zz);
        const double m01 = 2 * (xx * yy - w0 * zz);
        const double m02 = 2 * (xx * zz + w0 * yy);
        const double m10 = 2 * (xx * yy + w0 * zz);
        const double m11 = 1 - 2 * (xx * xx + zz * zz);
        const double m12 = 2 * (yy * zz - w0 * xx);
        const double m20 = 2 * (xx * zz - w0 * yy);
        const double m21 = 2 * (yy * zz + w0 * xx);
        const double m22 = 1 - 2 * (xx * xx + yy * yy);

        // Warm rhombus hint, reset per plate for a deterministic warm sequence.
        uint32_t hint = 0;
        for (TileId cell : occ) {
            const Vec3d& c = centers_[cell];
            Vec3d pos{m00 * c.x + m01 * c.y + m02 * c.z,
                      m10 * c.x + m11 * c.y + m12 * c.z,
                      m20 * c.x + m21 * c.y + m22 * c.z};
            TileId w = grid_->fromUnitVectorHinted(pos, hint);
            if (w == kInvalidTile) continue;
            uint32_t idx = static_cast<uint32_t>(candPool_.size());
            candPool_.push_back({static_cast<uint32_t>(p), cell});
            candNext_.push_back(candHead_[w]);
            candHead_[w] = idx;
        }
    }
}

void PlateSim::resolveOwnership() {
    eraseList_.clear();
    ccOverlaps_ = 0;

    // Candidate priority: continental beats oceanic; among oceanic, younger
    // birthMyr wins (older subducts); final tie smaller plateId.
    auto better = [&](const Candidate& a, const Candidate& b) -> bool {
        const CrustCell& ca = plates_[a.plate].crust[a.localCell];
        const CrustCell& cb = plates_[b.plate].crust[b.localCell];
        bool aCont = ca.type == CrustType::Continental;
        bool bCont = cb.type == CrustType::Continental;
        if (aCont != bCont) return aCont; // continental wins
        if (!aCont) {
            if (ca.birthMyr != cb.birthMyr) return ca.birthMyr > cb.birthMyr; // younger wins
        }
        return a.plate < b.plate; // tie: smaller plateId
    };

    // No per-cell allocation: walk the intrusive candidate list twice.
    for (TileId w = 0; w < tileCount_; ++w) {
        uint32_t head = candHead_[w];
        if (head == 0xFFFFFFFFu) {
            owner_[w] = static_cast<uint8_t>(kUnowned);
            resolved_[w] = CrustCell{};
            continue;
        }
        // Pass 1: find the winner.
        uint32_t winIdx = head;
        for (uint32_t idx = candNext_[head]; idx != 0xFFFFFFFFu; idx = candNext_[idx]) {
            if (better(candPool_[idx], candPool_[winIdx])) winIdx = idx;
        }
        const Candidate win = candPool_[winIdx];
        const CrustCell& winCell = plates_[win.plate].crust[win.localCell];
        const bool winCont = winCell.type == CrustType::Continental;
        owner_[w] = static_cast<uint8_t>(win.plate);
        resolved_[w] = winCell;

        // Pass 2: losers. Oceanic -> erase (subducts); CC overlap -> count.
        for (uint32_t idx = head; idx != 0xFFFFFFFFu; idx = candNext_[idx]) {
            if (idx == winIdx) continue;
            const Candidate& c = candPool_[idx];
            const CrustCell& cc = plates_[c.plate].crust[c.localCell];
            if (cc.type == CrustType::Oceanic) {
                eraseList_.push_back(c);
            } else if (cc.type == CrustType::Continental && winCont) {
                ++ccOverlaps_;
            }
        }
    }
}

void PlateSim::applyEraseList() {
    for (const auto& c : eraseList_) {
        plates_[c.plate].crust[c.localCell] = CrustCell{}; // type None = empty
    }
}

void PlateSim::gapFill() {
    const int K = static_cast<int>(plates_.size());
    // step() advances nowMyr_ AFTER the pipeline, so during gapFill nowMyr_ holds
    // the step-start time; new crust is born at the step-end time.
    const int32_t stepEndMyr = static_cast<int32_t>(nowMyr_ + dtMyr_ + 0.5);
    std::array<TileId, 6> nbrs{};
    for (TileId w = 0; w < tileCount_; ++w) {
        if (owner_[w] != static_cast<uint8_t>(kUnowned)) continue;
        // Majority of already-resolved neighbors; tie -> smaller plateId.
        uint32_t cnt = grid_->neighbors(w, nbrs);
        std::array<uint32_t, 256> tally{};
        uint32_t bestId = kUnowned, bestCnt = 0;
        for (uint32_t k = 0; k < cnt; ++k) {
            uint8_t np = owner_[nbrs[k]];
            if (np >= static_cast<uint8_t>(K)) continue;
            uint32_t c = ++tally[np];
            if (c > bestCnt || (c == bestCnt && np < bestId)) { bestCnt = c; bestId = np; }
        }
        if (bestId == kUnowned) continue; // no resolved neighbor yet; leave for a later pass
        owner_[w] = static_cast<uint8_t>(bestId);

        // Stamp NEW oceanic crust (age 0) into the winner's local raster at the
        // inverse-rotated local cell. These gaps are the spreading ridges.
        SimPlate& pl = plates_[bestId];
        // inverse rotation: conj(q) applied to world center.
        double q[4] = {pl.rotation[0], -pl.rotation[1], -pl.rotation[2], -pl.rotation[3]};
        Vec3d localPos = quatRotate(q, centers_[w]);
        TileId localCell = grid_->fromUnitVector(localPos);
        if (localCell == kInvalidTile) continue;
        CrustCell& cell = pl.crust[localCell];
        if (cell.type != CrustType::None) {
            // local cell already occupied; still set resolved so the world cell
            // shows the inherited owner's crust (use the neighbor crust as proxy).
            resolved_[w] = cell;
            continue;
        }
        cell.type = CrustType::Oceanic;
        cell.birthMyr = stepEndMyr;
        cell.thicknessKm = static_cast<float>(kRidgeCrustThicknessKm);
        cell.orogenyMyr = kOrogenyNever;
        pl.occupied.push_back(localCell); // forwardRasterize re-sorts next step
        resolved_[w] = cell;
    }
}

void PlateSim::boundaryScan() {
    const int K = static_cast<int>(plates_.size());
    std::fill(bndType_.begin(), bndType_.end(), static_cast<uint8_t>(BoundaryType::None));
    std::fill(bndSide_.begin(), bndSide_.end(), kSideSymmetric);
    std::fill(bndConv_.begin(), bndConv_.end(), 0.0f);

    std::array<TileId, 6> nbrs{};
    for (TileId t = 0; t < tileCount_; ++t) {
        uint8_t pid = owner_[t];
        if (pid >= static_cast<uint8_t>(K)) continue;
        uint32_t cnt = grid_->neighbors(t, nbrs);

        // Dominant foreign plate among neighbors.
        uint8_t dom = kUnowned;
        uint32_t domCnt = 0;
        bool anyForeign = false;
        for (uint32_t k = 0; k < cnt; ++k) {
            uint8_t np = owner_[nbrs[k]];
            if (np != pid && np < static_cast<uint8_t>(K)) {
                anyForeign = true;
                uint32_t c = 0;
                for (uint32_t k2 = k; k2 < cnt; ++k2) if (owner_[nbrs[k2]] == np) ++c;
                if (c > domCnt) { domCnt = c; dom = np; }
            }
        }
        if (!anyForeign) continue;

        const Vec3d& ctr = centers_[t];
        const SimPlate& pa = plates_[pid];
        const SimPlate& pb = plates_[dom];
        Vec3d oa{pa.eulerPole.x * pa.omegaRadPerMyr, pa.eulerPole.y * pa.omegaRadPerMyr,
                 pa.eulerPole.z * pa.omegaRadPerMyr};
        Vec3d ob{pb.eulerPole.x * pb.omegaRadPerMyr, pb.eulerPole.y * pb.omegaRadPerMyr,
                 pb.eulerPole.z * pb.omegaRadPerMyr};
        Vec3d va{oa.y * ctr.z - oa.z * ctr.y, oa.z * ctr.x - oa.x * ctr.z, oa.x * ctr.y - oa.y * ctr.x};
        Vec3d vb{ob.y * ctr.z - ob.z * ctr.y, ob.z * ctr.x - ob.x * ctr.z, ob.x * ctr.y - ob.y * ctr.x};
        Vec3d vrel{va.x - vb.x, va.y - vb.y, va.z - vb.z};

        // Outward normal: mean direction toward dom neighbors.
        Vec3d nrm{0, 0, 0};
        uint32_t df = 0;
        for (uint32_t k = 0; k < cnt; ++k) {
            if (owner_[nbrs[k]] == dom) {
                const Vec3d& nc = centers_[nbrs[k]];
                nrm.x += nc.x - ctr.x; nrm.y += nc.y - ctr.y; nrm.z += nc.z - ctr.z;
                ++df;
            }
        }
        double nl = foundation::det_math::sqrt(nrm.x * nrm.x + nrm.y * nrm.y + nrm.z * nrm.z);
        if (nl > 1e-12) { nrm.x /= nl; nrm.y /= nl; nrm.z /= nl; }

        double convergence = -(vrel.x * nrm.x + vrel.y * nrm.y + vrel.z * nrm.z);
        bndConv_[t] = static_cast<float>(convergence);
        double vmag = foundation::det_math::sqrt(vrel.x * vrel.x + vrel.y * vrel.y + vrel.z * vrel.z);
        double absConv = convergence < 0.0 ? -convergence : convergence;
        bool convergent = absConv > kConvergenceFraction * vmag;
        bool approaching = convergence > 0.0;

        bool tCont = resolved_[t].type == CrustType::Continental;
        // Foreign crust type: majority of dom neighbors' resolved crust.
        uint32_t fc = 0, ft = 0;
        for (uint32_t k = 0; k < cnt; ++k) {
            if (owner_[nbrs[k]] == dom) { ++ft; if (resolved_[nbrs[k]].type == CrustType::Continental) ++fc; }
        }
        bool fCont = ft > 0 && fc * 2u > ft;

        if (!convergent) {
            bndType_[t] = static_cast<uint8_t>(BoundaryType::Transform);
        } else if (!approaching) {
            bndType_[t] = static_cast<uint8_t>(BoundaryType::Divergent);
        } else if (tCont && fCont) {
            bndType_[t] = static_cast<uint8_t>(BoundaryType::ConvergentCC);
        } else if (tCont && !fCont) {
            bndType_[t] = static_cast<uint8_t>(BoundaryType::ConvergentCO);
            bndSide_[t] = kSideOverriding;
        } else if (!tCont && fCont) {
            bndType_[t] = static_cast<uint8_t>(BoundaryType::ConvergentCO);
            bndSide_[t] = kSideSubducting;
        } else {
            bndType_[t] = static_cast<uint8_t>(BoundaryType::ConvergentOO);
            bndSide_[t] = (pid < dom) ? kSideOverriding : kSideSubducting;
        }
    }
}

// ============================================================================
// run + finalize
// ============================================================================

std::shared_ptr<TectonicHistory> PlateSim::run(const CancelFn& cancel, const ProgressFn& progress) {
    while (step_ < stepCount_) step(cancel, progress);
    return finalize();
}

uint32_t PlateSim::continentalCellCount() const {
    uint32_t n = 0;
    for (const auto& pl : plates_) {
        if (!pl.alive) continue;
        for (const auto& c : pl.crust) if (c.type == CrustType::Continental) ++n;
    }
    return n;
}

std::shared_ptr<TectonicHistory> PlateSim::finalize() {
    // Final rasterize + boundary scan reflect current rotations.
    forwardRasterize();
    resolveOwnership();
    applyEraseList();
    gapFill();
    boundaryScan();

    auto h = std::make_shared<TectonicHistory>();
    h->coarseN = cfg_.coarseN;
    h->grid = grid_;
    h->historyMyr = historyMyr_;
    h->allocate(tileCount_);

    const int K = static_cast<int>(plates_.size());

    // Compact plate ids ascending over alive plates.
    std::vector<int> remap(static_cast<size_t>(K), -1);
    int next = 0;
    for (int p = 0; p < K; ++p) if (plates_[static_cast<size_t>(p)].alive) remap[static_cast<size_t>(p)] = next++;

    const int32_t nowI = static_cast<int32_t>(nowMyr_ + 0.5);
    for (TileId t = 0; t < tileCount_; ++t) {
        uint8_t pid = owner_[t];
        if (pid >= static_cast<uint8_t>(K) || remap[pid] < 0) {
            h->plateId[t] = 255;
            continue;
        }
        h->plateId[t] = static_cast<uint8_t>(remap[pid]);
        const CrustCell& cell = resolved_[t];
        h->crustType[t] = static_cast<uint8_t>(cell.type);
        int32_t age = nowI - cell.birthMyr;
        if (age < 0) age = 0;
        if (age > static_cast<int32_t>(kMaxStoredAgeMyr)) age = kMaxStoredAgeMyr;
        h->crustAge[t] = static_cast<uint16_t>(age);
        h->thicknessKm[t] = cell.thicknessKm;
        h->orogenyAge[t] = kOrogenyNever; // M-T1: no orogeny
        h->orogenyIntensity[t] = 0.0f;
        h->volcanism[t] = 0.0f;
        h->boundaryType[t] = bndType_[t];
        h->boundarySide[t] = bndSide_[t];
        h->convergence[t] = bndConv_[t];
    }

    // Per-plate summary, area-weighted majority crust.
    h->plates.reserve(static_cast<size_t>(next));
    std::vector<uint32_t> area(static_cast<size_t>(K), 0u);
    for (TileId t = 0; t < tileCount_; ++t) {
        uint8_t pid = owner_[t];
        if (pid < static_cast<uint8_t>(K)) area[pid]++;
    }
    for (int p = 0; p < K; ++p) {
        if (!plates_[static_cast<size_t>(p)].alive) continue;
        const SimPlate& pl = plates_[static_cast<size_t>(p)];
        TectonicPlate tp;
        tp.id = remap[static_cast<size_t>(p)];
        tp.isContinental = pl.isContinental;
        tp.eulerPole = pl.eulerPole;
        tp.omegaRadPerMyr = pl.omegaRadPerMyr;
        tp.rotation[0] = pl.rotation[0];
        tp.rotation[1] = pl.rotation[1];
        tp.rotation[2] = pl.rotation[2];
        tp.rotation[3] = pl.rotation[3];
        tp.area = area[static_cast<size_t>(p)];
        h->plates.push_back(tp);
    }
    // Sort by compacted id (already ascending, but be explicit).
    std::sort(h->plates.begin(), h->plates.end(),
              [](const TectonicPlate& a, const TectonicPlate& b) { return a.id < b.id; });

    return h;
}

} // namespace worldgen::tectonics
