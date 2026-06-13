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

// Smallest-eigenvalue eigenvector of a symmetric 3x3 matrix (column-major m[9],
// m[0..8] = [a b c; b d e; c e f]). Used to find a great-circle band's axis: for
// points lying near a plane through the origin, the band normal is the direction of
// least variance (smallest eigenvalue of the scatter matrix). Analytic symmetric
// eigen via trig (Smith 1961); deterministic, det_math only.
Vec3d minEigenVector(const double m[9]) {
    const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[8];
    // Eigenvalues of symmetric 3x3 (Smith's method).
    double p1 = b*b + c*c + e*e;
    Vec3d evals;
    if (p1 < 1e-30) {
        evals = {a, d, f};
    } else {
        double q = (a + d + f) / 3.0;
        double p2 = (a-q)*(a-q) + (d-q)*(d-q) + (f-q)*(f-q) + 2.0*p1;
        double p = foundation::det_math::sqrt(p2 / 6.0);
        // B = (A - qI)/p
        double b00=(a-q)/p, b11=(d-q)/p, b22=(f-q)/p, b01=b/p, b02=c/p, b12=e/p;
        double detB = b00*(b11*b22 - b12*b12) - b01*(b01*b22 - b12*b02) + b02*(b01*b12 - b11*b02);
        double r = detB / 2.0;
        if (r < -1.0) r = -1.0; else if (r > 1.0) r = 1.0;
        double phi = 0.0;
        // acos via asin: acos(r) = pi/2 - asin(r)
        phi = (1.5707963267948966 - foundation::det_math::asin(r)) / 3.0;
        double e1 = q + 2.0*p*foundation::det_math::cos(phi);
        double e3 = q + 2.0*p*foundation::det_math::cos(phi + 2.0943951023931953); // +2pi/3
        double e2 = 3.0*q - e1 - e3;
        evals = {e1, e2, e3};
    }
    // smallest eigenvalue
    double lam = evals.x;
    if (evals.y < lam) lam = evals.y;
    if (evals.z < lam) lam = evals.z;
    // Eigenvector of lam: null space of (A - lam I) via cross products of its rows.
    double r0[3] = {a-lam, b, c};
    double r1[3] = {b, d-lam, e};
    double r2[3] = {c, e, f-lam};
    auto cross = [](const double* u, const double* v) -> Vec3d {
        return {u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]};
    };
    Vec3d c0 = cross(r0, r1), c1 = cross(r1, r2), c2 = cross(r2, r0);
    double n0 = c0.x*c0.x+c0.y*c0.y+c0.z*c0.z;
    double n1 = c1.x*c1.x+c1.y*c1.y+c1.z*c1.z;
    double n2 = c2.x*c2.x+c2.y*c2.y+c2.z*c2.z;
    Vec3d best = c0; double bn = n0;
    if (n1 > bn) { best = c1; bn = n1; }
    if (n2 > bn) { best = c2; bn = n2; }
    if (bn < 1e-20) return {0, 0, 1};
    double inv = 1.0 / foundation::det_math::sqrt(bn);
    return {best.x*inv, best.y*inv, best.z*inv};
}

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
    prevOwner_.assign(tileCount_, static_cast<uint8_t>(kUnowned));
    ringScratch_.assign(tileCount_, -1);
    bfsScratch_.reserve(tileCount_ / 2u);
    collisionBlock_.assign(static_cast<size_t>(params.plateCount), 0.0f);
    slabPull_.assign(static_cast<size_t>(params.plateCount), 1.0);

    // Event RNG streams (independent of init streams).
    poleEvolveStream_ = deriveSeed(params.seed, 0x10);
    riftStream_       = deriveSeed(params.seed, 0x11);
    marginStream_     = deriveSeed(params.seed, 0x14);
    matureStream_     = deriveSeed(params.seed, 0x15);

    // Continental-area controller set-point (M-T2.5): the same area target used to
    // paint the initial continents. Arc crust production is nudged toward it.
    continentalTarget_ = (1.0 - params.waterAmount) * kCrustAreaFactor *
                         static_cast<double>(tileCount_);

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
        prevOwner_ = owner_;
        initHotspots(deriveSeed(params.seed, 0x12));
        nextPoleEvolveMyr_.assign(plates_.size(), kPoleEvolutionPeriodMyr);
        collisionBlock_.assign(plates_.size(), 0.0f);
        slabPull_.assign(plates_.size(), 1.0);
        return;
    }

    plates_.assign(static_cast<size_t>(params.plateCount), SimPlate{});

    // Stream seeds, one per init phase, all derived from the master seed.
    seedAndGrowPlates(deriveSeed(params.seed, 0x01));
    paintContinents(deriveSeed(params.seed, 0x02));
    assignEulerPoles(deriveSeed(params.seed, 0x03));
    initPlateRasters();
    prevOwner_ = owner_;
    initHotspots(deriveSeed(params.seed, 0x12));

    // Stagger first pole-evolution per plate across the first period so they don't
    // all re-pole on the same step.
    nextPoleEvolveMyr_.assign(plates_.size(), 0.0);
    foundation::Pcg32 prng(deriveSeed(params.seed, 0x13));
    for (size_t i = 0; i < plates_.size(); ++i) {
        nextPoleEvolveMyr_[i] = kPoleEvolutionPeriodMyr * (0.5 + 0.5 * prng.nextDouble());
    }
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

    rebalanceMomentum();
}

// Area-weighted momentum balance: subtract the area-weighted mean omega vector so
// net rotation is ~0. (Ports PlateMovementStage; reused after merge/rift events.)
void PlateSim::rebalanceMomentum() {
    const int K = static_cast<int>(plates_.size());
    std::vector<uint32_t> area(static_cast<size_t>(K), 0u);
    for (uint32_t t = 0; t < tileCount_; ++t) {
        uint8_t pid = owner_[t];
        if (pid < static_cast<uint8_t>(K)) area[pid]++;
    }
    double sumA = 0.0, mx = 0.0, my = 0.0, mz = 0.0;
    for (int p = 0; p < K; ++p) {
        const auto& pl = plates_[static_cast<size_t>(p)];
        if (!pl.alive) continue;
        double a = static_cast<double>(area[static_cast<size_t>(p)]);
        mx += a * pl.eulerPole.x * pl.omegaRadPerMyr;
        my += a * pl.eulerPole.y * pl.omegaRadPerMyr;
        mz += a * pl.eulerPole.z * pl.omegaRadPerMyr;
        sumA += a;
    }
    if (sumA > 0.0) { mx /= sumA; my /= sumA; mz /= sumA; }
    for (int p = 0; p < K; ++p) {
        auto& pl = plates_[static_cast<size_t>(p)];
        if (!pl.alive) continue;
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
//   1. Evolve poles:         plates past their schedule re-draw pole + speed with
//                            continuity, then momentum re-balance (fixes stranded
//                            basins / runaway seafloor age).
//   2. Advance quaternions:  Qp = dQ(pole_p, omega_p*dt) * Qp.
//   3. Forward rasterize:    each alive plate ascending id, each occupied local
//                            cell -> world cell candidate list. (Cell order within
//                            a plate does not affect the result: the resolve
//                            comparator is a total order and the erase list is
//                            built in ascending world-cell order in step 4.)
//   4. Resolve ownership:    per world cell ascending, sort candidates
//                            (continental > oceanic; younger oceanic wins; CC tie
//                            previous-owner-sticky then smaller plateId). Losing
//                            oceanic -> erase list. prevOwner_ updated at the end.
//   5. Apply erase list to plate-local rasters.
//   6. Gap fill ascending:   empty cells inherit majority neighbor owner and get
//                            NEW oceanic crust at birth=now (spreading ridges).
//   6.5 Absorb speckle:      weld isolated ownership islands (<= kAbsorbMaxCells) into the
//                            dominant surrounding plate so spurious specks never reach the
//                            boundary scan (M-T2.7). Continental crust is conserved.
//   6.6 Absorb crust speckle: revert tiny isolated continental components (<=
//                            kCrustSpeckleMaxCells) within each plate's world footprint back
//                            to oceanic (M-T3.5). Prevents isolated arc-maturation conversions
//                            from persisting as confetti single-cell continents.
//   7. Boundary scan:        Euler-pole relative velocity, convergence, classify.
//   7.5 Slab pull:           scale each plate's omega toward a target factor set by
//                            the mean age of its subducting oceanic floor (old cold
//                            slabs pull hardest), clamped to the surface-speed bounds.
//                            Runs after the scan (needs boundary side) so the new omega
//                            takes effect at next step's advanceRotations.
//   8. Collision processing: CC thicken + orogeny stamp + per-pair score; CO/OO
//                            arc volcanism + modest thickening (ascending order).
//   9. Terrane accretion:    small continental fragments transfer at trenches.
//  10. Erosion proxy:        continental thickness relaxes toward equilibrium.
//  11. Hotspots:             fixed world-frame plumes deposit volcanism.
//  12. Plate events:         merge (score > threshold) then rift (alive < K).
//  13. cancel + progress callbacks.

void PlateSim::step(const CancelFn& cancel, const ProgressFn& progress) {
    evolvePoles();        // 1
    advanceRotations();   // 2
    forwardRasterize();   // 3
    resolveOwnership();   // 4
    applyEraseList();     // 5
    gapFill();            // 6
    absorbOwnershipSpeckle(); // 6.5 (M-T2.7): weld isolated ownership islands
    absorbCrustSpeckle();     // 6.6 (M-T3.5): revert isolated continental specks to oceanic
    boundaryScan();       // 7
    slabPull();           // 7.5 (M-T2.6): scale omega by mean subducting-floor age
    collisionProcessing();// 8
    terraneAccretion();   // 9
    erosionProxy();       // 10
    hotspots();           // 11
    plateEvents();        // 12
    ++step_;
    nowMyr_ += dtMyr_;
    if (cancel) cancel();                                            // 13
    if (progress) progress(static_cast<float>(step_) / static_cast<float>(stepCount_));
}

void PlateSim::advanceRotations() {
    for (size_t pi = 0; pi < plates_.size(); ++pi) {
        SimPlate& pl = plates_[pi];
        if (!pl.alive || pl.omegaRadPerMyr == 0.0) continue;
        // Continental collision damping: a plate deep in CC collision rotates slower
        // (buoyant continents resist subduction and stop interpenetrating).
        double block = (pi < collisionBlock_.size()) ? collisionBlock_[pi] : 0.0;
        double eff = pl.omegaRadPerMyr * dtMyr_ * (1.0 - block);
        double dq[4];
        quatFromAxisAngle(pl.eulerPole, eff, dq);
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
    resolvedContinentalCount_ = 0;

    // Candidate priority: continental beats oceanic; among oceanic, younger
    // birthMyr wins (older subducts); among continental, the cell's previous owner
    // wins (CC-tie stickiness — keeps collision boundaries coherent so the per-pair
    // collision score can build instead of the front dithering); final tie smaller
    // plateId. `stickyPrev` is prevOwner_[w] for the cell being resolved.
    uint8_t stickyPrev = kUnowned;
    auto better = [&](const Candidate& a, const Candidate& b) -> bool {
        const CrustCell& ca = plates_[a.plate].crust[a.localCell];
        const CrustCell& cb = plates_[b.plate].crust[b.localCell];
        bool aCont = ca.type == CrustType::Continental;
        bool bCont = cb.type == CrustType::Continental;
        if (aCont != bCont) return aCont; // continental wins
        if (!aCont) {
            if (ca.birthMyr != cb.birthMyr) return ca.birthMyr > cb.birthMyr; // younger wins
        } else {
            bool aPrev = a.plate == stickyPrev;
            bool bPrev = b.plate == stickyPrev;
            if (aPrev != bPrev) return aPrev; // previous owner sticks
        }
        return a.plate < b.plate; // tie: smaller plateId
    };

    // No per-cell allocation: walk the intrusive candidate list twice.
    for (TileId w = 0; w < tileCount_; ++w) {
        stickyPrev = prevOwner_[w];
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
        if (winCont) ++resolvedContinentalCount_;

        // Pass 2: losers. Oceanic -> erase (subducts). Continental CC overlap -> count
        // only (the loser keeps its crust; it is mostly coarse-rounding bleed from an
        // adjacent plate's real margin, not duplicate material — erasing it would
        // destroy real continent). Stickiness keeps the rendered boundary coherent;
        // merge accounting handles genuine overlap via pruneStaleCrust at merge time.
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
    // Snapshot ownership for next step's CC-tie stickiness. absorbOwnershipSpeckle runs
    // next and re-snapshots after it edits owner_ on the steps it runs; on strided steps it
    // doesn't run, so this snapshot stands.
    prevOwner_ = owner_;
}

// ============================================================================
// Ownership coherence filter (M-T2.7): weld isolated ownership islands into the
// dominant surrounding plate.
// ============================================================================
//
// Forward-rasterize rounding bleed, post-rift fringe interleave, and accretion debris
// leave plate interiors peppered with 1-4 cell islands of a foreign plate. Left alone the
// boundary scan classifies each speck as a boundary and orogeny gets stamped mid-plate
// (coverage inflates past Earth's ~30-50%). This pass floods same-owner connected
// components over the world owner map; any component of <= kAbsorbMaxCells whose entire
// outer boundary is dominated by a single foreign plate is welded into that plate. The
// crust at each absorbed cell transfers into the absorber's baseline raster (continental
// crust is never deleted — area is conserved), the donor cell is cleared, and owner_ /
// resolved_ are updated so the subsequent boundary scan sees clean interiors.

// Transfer the crust at world cell w from its current owner into dst's baseline frame.
// Continental crust is conserved (copied, not dropped); owner_/resolved_ updated.
void PlateSim::absorbCellInto(TileId w, uint32_t dst) {
    uint8_t src = owner_[w];
    if (src == static_cast<uint8_t>(kUnowned) || src == static_cast<uint8_t>(dst)) return;
    if (!plates_[dst].alive) return;
    SimPlate& donor = plates_[src];
    SimPlate& keep  = plates_[dst];
    TileId srcLocal = worldToLocal(src, w);
    CrustCell moved = (srcLocal != kInvalidTile) ? donor.crust[srcLocal] : resolved_[w];
    if (moved.type == CrustType::None) moved = resolved_[w];
    TileId dstLocal = worldToLocal(dst, w);
    if (dstLocal != kInvalidTile) {
        CrustCell& kc = keep.crust[dstLocal];
        if (kc.type == CrustType::None) {
            kc = moved;
            keep.occupied.push_back(dstLocal);
        }
        resolved_[w] = kc;
    } else {
        resolved_[w] = moved;
    }
    // Donor loses its raster cell at this world position. If dst already had crust at the
    // same world cell (its local was occupied), the donor cell was a duplicate of that
    // world position, so clearing it conserves world-resolved area (same dedup rule as
    // mergePlates). The world cell still shows dst's crust.
    if (srcLocal != kInvalidTile) donor.crust[srcLocal] = CrustCell{};
    owner_[w] = static_cast<uint8_t>(dst);
}

void PlateSim::absorbOwnershipSpeckle() {
    const int K = static_cast<int>(plates_.size());
    // Per-plate owned-cell count, so we never absorb a plate's ENTIRE body (a small plate is
    // not a speckle of itself — that would silently delete a live plate the controller is
    // counting). An island is only welded if its donor still has cells left over.
    std::vector<uint32_t> plateArea(static_cast<size_t>(K), 0u);
    for (TileId t = 0; t < tileCount_; ++t) {
        uint8_t pid = owner_[t];
        if (pid < static_cast<uint8_t>(K)) plateArea[pid]++;
    }
    // speckComp_ marks cells visited THIS sweep: -1 unvisited, >=0 component id of a small
    // island, -2 part of an aborted (over-cap, i.e. real plate body) flood. The -2 marker
    // lets us bail out of a body flood without re-walking it from another of its cells.
    speckComp_.assign(tileCount_, -1);
    std::array<TileId, 6> nbrs{};
    int compCount = 0;
    // Cap the flood a hair above the island size cap: as soon as a flood exceeds the cap it
    // is a plate body, not a speck, so we abandon it. Only cells with a FOREIGN neighbor
    // seed a flood — deep interior cells (all same-owner neighbors) are never flooded, so
    // giant plate bodies cost ~their boundary length, not their area.
    for (TileId s = 0; s < tileCount_; ++s) {
        if (speckComp_[s] != -1) continue;
        uint8_t pid = owner_[s];
        if (pid >= static_cast<uint8_t>(K)) continue;
        // Cheap pre-filter: skip cells with no foreign neighbor (cannot be an island cell).
        uint32_t cnt0 = grid_->neighbors(s, nbrs);
        bool anyForeign = false;
        for (uint32_t k = 0; k < cnt0; ++k) {
            uint8_t np = owner_[nbrs[k]];
            if (np != pid) { anyForeign = true; break; }
        }
        if (!anyForeign) continue;

        int cid = compCount++;
        speckStack_.clear();
        speckStack_.push_back(s);
        speckCells_.clear();
        speckComp_[s] = cid;
        bool overCap = false;
        while (!speckStack_.empty()) {
            TileId cur = speckStack_.back(); speckStack_.pop_back();
            speckCells_.push_back(cur);
            if (speckCells_.size() > kAbsorbMaxCells) { overCap = true; break; }
            uint32_t cnt = grid_->neighbors(cur, nbrs);
            for (uint32_t k = 0; k < cnt; ++k) {
                TileId v = nbrs[k];
                if (speckComp_[v] != -1) continue;
                if (owner_[v] != pid) continue;
                speckComp_[v] = cid;
                speckStack_.push_back(v);
            }
        }
        if (overCap) {
            // Real plate body: mark every cell we touched (and any still queued) as -2 so it
            // is not re-flooded from another boundary cell of the same body this sweep.
            for (TileId c : speckCells_) speckComp_[c] = -2;
            for (TileId c : speckStack_) speckComp_[c] = -2;
            continue;
        }

        // Dominant foreign plate around the island's outer boundary. Tie -> smaller id.
        std::array<uint32_t, 256> tally{};
        uint8_t dom = static_cast<uint8_t>(kUnowned);
        uint32_t domCnt = 0;
        for (TileId c : speckCells_) {
            uint32_t cnt = grid_->neighbors(c, nbrs);
            for (uint32_t k = 0; k < cnt; ++k) {
                uint8_t np = owner_[nbrs[k]];
                if (np == pid || np >= static_cast<uint8_t>(K)) continue;
                uint32_t t = ++tally[np];
                if (t > domCnt || (t == domCnt && np < dom)) { domCnt = t; dom = np; }
            }
        }
        if (dom == static_cast<uint8_t>(kUnowned)) continue; // island has no foreign border
        // Don't dissolve a whole plate: if this island IS the donor's entire footprint,
        // leave it (it is a small plate, not a stray fragment of a larger one).
        if (plateArea[pid] <= speckCells_.size()) continue;
        // Weld the whole island into dom (ascending world-cell order for determinism).
        std::sort(speckCells_.begin(), speckCells_.end());
        for (TileId c : speckCells_) absorbCellInto(c, dom);
    }

    // Snapshot post-absorption ownership for next step's CC-tie stickiness.
    prevOwner_ = owner_;
}

// ============================================================================
// Crust-type coherence filter (M-T3.5): revert tiny isolated continental
// components within a plate's world footprint back to oceanic crust.
// ============================================================================
//
// Arc maturation (collisionProcessing) converts individual oceanic cells to continental
// crust along subduction arc bands. Most conversions happen on real arc fronts and are
// geographically coherent; however, plate churn scatters a fraction into open ocean as
// isolated one- to three-cell specks. These specks persist and accumulate because
// absorbOwnershipSpeckle welds OWNERSHIP islands but does not touch crust type, so a
// single plate owning isolated continental cells in its oceanic interior is perfectly
// legal from an ownership standpoint.
//
// This pass finds, for each plate's world footprint, connected components of continental
// cells (geographic adjacency in world space). Components of size <= kCrustSpeckleMaxCells
// are reverted to oceanic (type=oceanic, thickness=kOceanicThicknessKm, birthMyr=now,
// volcanism kept — the magmatic signal stays, only the irreversible type flip is undone).
// The area controller then redirects production to coherent margins on subsequent steps.
//
// Same optimization as absorbOwnershipSpeckle: only cells with a foreign-crust-type
// neighbor seed a flood, so interior cells of large continents are never walked.
// Deterministic: plates ascending, component cells sorted ascending before reversion.

void PlateSim::absorbCrustSpeckle() {
    const int K = static_cast<int>(plates_.size());
    const int32_t nowI = static_cast<int32_t>(nowMyr_ + dtMyr_ + 0.5);
    crustSpeckRevertedThisStep_ = 0;

    // We work in world space via resolved_[]/owner_ so the component adjacency matches
    // what the boundary scan and future rasterizes will see. The revert writes back to
    // the plate's local raster (via worldToLocal) so the change persists next step.
    //
    // speckComp_ is reused scratch (already sized to tileCount_ by absorbOwnershipSpeckle).
    // We reset it here to -1 for continental cells we encounter.
    speckComp_.assign(tileCount_, -1);
    std::array<TileId, 6> nbrs{};
    int compCount = 0;

    for (TileId s = 0; s < tileCount_; ++s) {
        if (speckComp_[s] != -1) continue;
        if (resolved_[s].type != CrustType::Continental) continue;
        uint8_t pid = owner_[s];
        if (pid >= static_cast<uint8_t>(K)) continue;
        if (!plates_[static_cast<size_t>(pid)].alive) continue;

        // Cheap pre-filter: skip cells with no different-type or foreign-plate neighbor.
        // These are deep interior cells that can never be a speck boundary on their own.
        // We do NOT mark them visited here — the flood from a neighboring boundary cell
        // MUST be able to expand into them to correctly measure component size.
        uint32_t cnt0 = grid_->neighbors(s, nbrs);
        bool anyDifferent = false;
        for (uint32_t k = 0; k < cnt0; ++k) {
            TileId nb = nbrs[k];
            if (resolved_[nb].type != CrustType::Continental || owner_[nb] != pid) {
                anyDifferent = true;
                break;
            }
        }
        if (!anyDifferent) continue; // skip as seed, but leave speckComp_[s] = -1

        int cid = compCount++;
        speckComp_[s] = cid;
        speckStack_.clear();
        speckStack_.push_back(s);
        speckCells_.clear();
        bool overCap = false;
        while (!speckStack_.empty()) {
            TileId cur = speckStack_.back(); speckStack_.pop_back();
            speckCells_.push_back(cur);
            if (speckCells_.size() > kCrustSpeckleMaxCells) { overCap = true; break; }
            uint32_t cnt = grid_->neighbors(cur, nbrs);
            for (uint32_t k = 0; k < cnt; ++k) {
                TileId v = nbrs[k];
                if (speckComp_[v] == -2) {
                    // This cell's continental neighbor is already confirmed part of a large
                    // body. The current seed is adjacent to a large body, so it cannot be
                    // a speck — abort immediately and mark all cells visited as large body.
                    overCap = true;
                    break;
                }
                if (speckComp_[v] != -1) continue;
                if (resolved_[v].type != CrustType::Continental) continue;
                if (owner_[v] != pid) continue; // same plate only
                speckComp_[v] = cid;
                speckStack_.push_back(v);
            }
            if (overCap) break;
        }
        if (overCap) {
            // Large component (or adjacent to one): mark all touched cells as -2 so
            // subsequent seeds from the same body also abort immediately.
            for (TileId c : speckCells_) speckComp_[c] = -2;
            for (TileId c : speckStack_) speckComp_[c] = -2;
            continue;
        }

        // Arc-adjacent exemption: if any component cell has a non-continental neighbor with
        // volcanism >= kCrustSpeckleVolcExempt, the active arc band passes near this fragment
        // and will grow to encompass it. Reverting now would cause oscillation (nucleation→
        // maturation, filter→revert, controller→produce, repeat). Only the currently-ACTIVE
        // arc (high-volcanism oceanic cells) signals adjacency — the component cells' own
        // volcanism is a historical record, not evidence of an active arc passing through.
        {
            bool arcAdjacent = false;
            for (TileId c : speckCells_) {
                uint32_t cntA = grid_->neighbors(c, nbrs);
                for (uint32_t k = 0; k < cntA; ++k) {
                    const CrustCell& nc = resolved_[nbrs[k]];
                    if (nc.type != CrustType::Continental && nc.volcanism >= kCrustSpeckleVolcExempt) {
                        arcAdjacent = true;
                        break;
                    }
                }
                if (arcAdjacent) break;
            }
            if (arcAdjacent) continue;
        }

        // Small continental component: revert to oceanic in ascending world-cell order.
        std::sort(speckCells_.begin(), speckCells_.end());
        for (TileId c : speckCells_) {
            // Write back to the plate's local raster.
            TileId local = worldToLocal(static_cast<uint32_t>(pid), c);
            if (local != kInvalidTile) {
                CrustCell& lc = plates_[static_cast<size_t>(pid)].crust[local];
                if (lc.type == CrustType::Continental) {
                    lc.type = CrustType::Oceanic;
                    lc.thicknessKm = static_cast<float>(kOceanicThicknessKm);
                    lc.birthMyr = nowI; // fresh young floor
                    lc.orogenyMyr = kOrogenyNever;
                    // volcanism kept: the magmatic signal survives; the cell will age
                    // out of arc activity through erosionProxy's volcanism decay.
                }
            }
            // Update the resolved world view so subsequent passes this step see the revert.
            CrustCell& rc = resolved_[c];
            rc.type = CrustType::Oceanic;
            rc.thicknessKm = static_cast<float>(kOceanicThicknessKm);
            rc.birthMyr = nowI;
            rc.orogenyMyr = kOrogenyNever;
            ++crustSpeckRevertedThisStep_;
        }
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
// M-T2 helpers
// ============================================================================

uint32_t PlateSim::aliveCount() const {
    uint32_t n = 0;
    for (const auto& pl : plates_) if (pl.alive) ++n;
    return n;
}

uint32_t PlateSim::plateArea(uint32_t pid) const {
    if (pid >= plates_.size()) return 0;
    uint32_t n = 0;
    for (const auto& c : plates_[pid].crust) if (c.type != CrustType::None) ++n;
    return n;
}

// World cell -> the plate's local baseline cell (inverse-rotate world center).
TileId PlateSim::worldToLocal(uint32_t pid, TileId worldCell) const {
    const SimPlate& pl = plates_[pid];
    double q[4] = {pl.rotation[0], -pl.rotation[1], -pl.rotation[2], -pl.rotation[3]};
    Vec3d localPos = quatRotate(q, centers_[worldCell]);
    return grid_->fromUnitVector(localPos);
}

// Look up (creating if absent) a per-pair collision score slot. Keys packed with
// min plate id in the high byte so iteration over the vector is order-stable.
static inline uint32_t packPair(uint8_t a, uint8_t b) {
    uint8_t lo = a < b ? a : b, hi = a < b ? b : a;
    return (static_cast<uint32_t>(lo) << 8) | static_cast<uint32_t>(hi);
}

// ============================================================================
// Collision processing: CC thickening + orogeny, CO/OO arc volcanism.
// ============================================================================
//
// Walks boundary tiles in ascending world-cell order (deterministic). For each
// convergent boundary tile it finds the dominant foreign plate (same derivation
// as boundaryScan) and acts within a small ring band:
//   CC: thicken both sides, stamp orogeny + intensity, add to the pair score.
//   CO/OO: deposit arc volcanism + modest thickening on the overriding side a few
//          rings inland from the trench; ensure the subducting ocean keeps eroding.

void PlateSim::collisionProcessing() {
    const int K = static_cast<int>(plates_.size());
    const int32_t nowI = static_cast<int32_t>(nowMyr_ + dtMyr_ + 0.5);

    // M-T2.5 continental-area controller: scale arc crust production toward the area
    // set-point. error > 0 when below target -> factor > 1 (produce more juvenile
    // crust); below 1 when in surplus. Linear in the fractional error, clamped.
    {
        double setPoint = continentalTarget_ * (1.0 - kAreaControllerSetpointBias);
        double error = continentalTarget_ > 0.0
            ? (setPoint - static_cast<double>(resolvedContinentalCount_)) /
              continentalTarget_
            : 0.0;
        double f = 1.0 + kAreaControllerGain * error;
        if (f < kAreaControllerFactorMin) f = kAreaControllerFactorMin;
        if (f > kAreaControllerFactorMax) f = kAreaControllerFactorMax;
        areaControllerFactor_ = f;
    }

    std::array<TileId, 6> nbrs{};

    // Recompute the dominant foreign plate per boundary tile (boundaryScan only
    // stored the classification, not the partner). Same majority rule as the scan.
    auto domForeign = [&](TileId t, uint8_t pid) -> uint8_t {
        uint32_t cnt = grid_->neighbors(t, nbrs);
        uint8_t dom = static_cast<uint8_t>(kUnowned);
        uint32_t domCnt = 0;
        for (uint32_t k = 0; k < cnt; ++k) {
            uint8_t np = owner_[nbrs[k]];
            if (np != pid && np < static_cast<uint8_t>(K)) {
                uint32_t c = 0;
                for (uint32_t k2 = k; k2 < cnt; ++k2) if (owner_[nbrs[k2]] == np) ++c;
                if (c > domCnt) { domCnt = c; dom = np; }
            }
        }
        return dom;
    };

    // Orogeny stamp hygiene (M-T2.7): a convergent boundary tile only seeds an orogeny /
    // arc band if it belongs to a COHERENT boundary SEGMENT — i.e. enough of its neighbors
    // are convergent-boundary cells of the same broad kind (CC vs subduction). A real
    // collision/subduction front is a continuous line, so its cells have >= 2 such
    // neighbors; a residual one- or two-cell ownership speck (that appeared this step,
    // before next step's absorber sweeps it) has 0-1 and is rejected, so it cannot stamp
    // spurious mid-plate orogeny. `wantCC` selects the kind: CC neighbors for a CC tile,
    // CO/OO neighbors for a subduction tile.
    auto coherentSegment = [&](TileId t, bool wantCC) -> bool {
        uint32_t cnt = grid_->neighbors(t, nbrs);
        uint32_t same = 0;
        for (uint32_t k = 0; k < cnt; ++k) {
            uint8_t nbt = bndType_[nbrs[k]];
            bool match = wantCC
                ? (nbt == static_cast<uint8_t>(BoundaryType::ConvergentCC))
                : (nbt == static_cast<uint8_t>(BoundaryType::ConvergentCO) ||
                   nbt == static_cast<uint8_t>(BoundaryType::ConvergentOO));
            if (match) ++same;
        }
        return same >= kBoundarySegmentMinNeighbors;
    };

    // BFS ring distance from a set of seed world cells, restricted to one plate's
    // territory, bounded to maxRing. ringScratch_ is reset for touched cells only.
    // Returns the list of (worldCell, ring) reached, appended to `out`.
    auto bandBFS = [&](const std::vector<TileId>& seeds, uint8_t plate, int maxRing,
                       std::vector<std::pair<TileId,int>>& out) {
        bfsScratch_.clear();
        std::array<TileId, 6> nb{};
        for (TileId s : seeds) {
            if (owner_[s] != plate) continue;
            if (ringScratch_[s] >= 0) continue;
            ringScratch_[s] = 0;
            bfsScratch_.push_back(s);
            out.push_back({s, 0});
        }
        size_t head = 0;
        while (head < bfsScratch_.size()) {
            TileId cur = bfsScratch_[head++];
            int d = ringScratch_[cur];
            if (d >= maxRing) continue;
            uint32_t cnt = grid_->neighbors(cur, nb);
            for (uint32_t k = 0; k < cnt; ++k) {
                TileId v = nb[k];
                if (owner_[v] != plate) continue;
                if (ringScratch_[v] >= 0) continue;
                ringScratch_[v] = d + 1;
                bfsScratch_.push_back(v);
                out.push_back({v, d + 1});
            }
        }
        // reset touched cells
        for (auto& pr : out) ringScratch_[pr.first] = -1;
    };

    // ---- Collect convergent boundary tiles, grouped per (side plate). ----
    // CC: thicken a band on BOTH plates around the boundary tile. To keep this
    // O(N) we gather, per plate, the boundary seed cells, then BFS once per plate.
    // Seeds + their convergence are accumulated; per-pair score is summed.

    // Per-plate seed lists for CC bands and CO arc bands (overriding side).
    std::vector<std::vector<TileId>> ccSeeds(static_cast<size_t>(K));
    std::vector<std::vector<TileId>> arcSeeds(static_cast<size_t>(K));
    // Per-plate accumulated convergence at the boundary (for thickening magnitude).
    // We use the max boundary convergence touching the plate as the band's rate.
    std::vector<float> ccConv(static_cast<size_t>(K), 0.0f);
    std::vector<float> arcConv(static_cast<size_t>(K), 0.0f);

    // This-step per-pair score contributions (key -> conv*dt summed over contact).
    std::vector<std::pair<uint32_t, double>> stepScore;

    for (TileId t = 0; t < tileCount_; ++t) {
        uint8_t bt = bndType_[t];
        if (bt != static_cast<uint8_t>(BoundaryType::ConvergentCC) &&
            bt != static_cast<uint8_t>(BoundaryType::ConvergentCO) &&
            bt != static_cast<uint8_t>(BoundaryType::ConvergentOO)) continue;
        uint8_t pid = owner_[t];
        if (pid >= static_cast<uint8_t>(K)) continue;
        float conv = bndConv_[t];
        if (conv <= 0.0f) conv = 0.0f;

        if (bt == static_cast<uint8_t>(BoundaryType::ConvergentCC)) {
            if (!coherentSegment(t, /*wantCC=*/true)) continue; // speck, not a real front
            uint8_t dom = domForeign(t, pid);
            ccSeeds[pid].push_back(t);
            if (conv > ccConv[pid]) ccConv[pid] = conv;
            // Per-pair collision score uses the PEAK convergence on the shared
            // contact this step (scale-invariant: independent of coarse-grid contact
            // length, so the same merge threshold works at any coarseN).
            if (dom < static_cast<uint8_t>(K) && pid < dom) {
                uint32_t key = packPair(pid, dom);
                double add = static_cast<double>(conv) * dtMyr_;
                bool found = false;
                for (auto& kv : stepScore) {
                    if (kv.first == key) { if (add > kv.second) kv.second = add; found = true; break; }
                }
                if (!found) stepScore.push_back({key, add});
            }
        } else {
            // CO/OO: arc band only on the OVERRIDING side, and only along a coherent
            // subduction front (a speck overriding cell can't seed an arc).
            if (bndSide_[t] == kSideOverriding && coherentSegment(t, /*wantCC=*/false)) {
                arcSeeds[pid].push_back(t);
                if (conv > arcConv[pid]) arcConv[pid] = conv;
            }
        }
    }

    // Fold this-step contributions into the persistent collision score. Pairs that
    // collided this step grow; pairs that did NOT decay (only SUSTAINED collisions
    // accumulate toward the merge threshold). A pair is removed once it decays out.
    for (auto& kv : collisionScore_) {
        bool touched = false;
        for (const auto& sk : stepScore) {
            if (sk.first == kv.first) { kv.second += sk.second; touched = true; break; }
        }
        if (!touched) kv.second *= kCollisionScoreDecay;
    }
    for (const auto& sk : stepScore) {
        bool present = false;
        for (const auto& kv : collisionScore_) if (kv.first == sk.first) { present = true; break; }
        if (!present) collisionScore_.push_back(sk);
    }
    collisionScore_.erase(
        std::remove_if(collisionScore_.begin(), collisionScore_.end(),
                       [](const auto& kv) { return kv.second < 1e-6; }),
        collisionScore_.end());

    // ---- Apply CC bands ----
    std::vector<std::pair<TileId,int>> band;
    for (int p = 0; p < K; ++p) {
        if (ccSeeds[static_cast<size_t>(p)].empty()) continue;
        if (!plates_[static_cast<size_t>(p)].alive) continue;
        band.clear();
        bandBFS(ccSeeds[static_cast<size_t>(p)], static_cast<uint8_t>(p),
                kCollisionBandRings, band);
        float conv = ccConv[static_cast<size_t>(p)];
        double thickenFull = kCcThickenPerConvMyr * static_cast<double>(conv) * dtMyr_;
        // Convergence factor for the intensity stamp: a fast head-on contact stamps at
        // full strength, a slow/oblique one proportionally weaker (clamped to 1). Coverage
        // then tracks where REAL fast convergence happened, not every cell that grazed a
        // boundary. (Thickening already scales by conv via thickenFull above.)
        double convFactor = static_cast<double>(conv) / kOrogenyConvRefRadPerMyr;
        if (convFactor > 1.0) convFactor = 1.0;
        if (convFactor < 0.0) convFactor = 0.0;
        for (const auto& pr : band) {
            TileId w = pr.first;
            int ring = pr.second;
            // Thickening falloff is relative to the THICKEN band, so thickening at rings
            // 1..thickenRings is independent of how far the stamp band extends (the stamp
            // band must not perturb the thickness field, hence the motion trajectory).
            double thFalloff = 1.0 - 0.5 * (static_cast<double>(ring) /
                               static_cast<double>(kCollisionThickenRings + 1));
            if (thFalloff < 0.0) thFalloff = 0.0;
            // Intensity falloff is relative to the FULL stamp band, squared so intensity
            // concentrates on the boundary core and tapers to a faint apron, reading as a
            // thin linear range, not a flat plateau.
            double lin = 1.0 - static_cast<double>(ring) /
                         static_cast<double>(kCollisionBandRings + 1);
            if (lin < 0.0) lin = 0.0;
            double intenFalloff = lin * lin;
            TileId local = worldToLocal(static_cast<uint32_t>(p), w);
            if (local == kInvalidTile) continue;
            CrustCell& cell = plates_[static_cast<size_t>(p)].crust[local];
            if (cell.type != CrustType::Continental) continue;
            // Accumulate intensity first so the coverage gate below can test the built-up
            // value (not just this step's increment).
            float inten = cell.orogenyIntensity +
                          kOrogenyIntensityPerStep *
                          static_cast<float>(intenFalloff * convFactor);
            cell.orogenyIntensity = inten > 1.0f ? 1.0f : inten;

            // Thicken the inner rings (the real stacking the area controller balances).
            // Outer rings record the orogeny stamp + intensity for coverage / M-T4 texture
            // but add no crust, so the wide stamp band does not deepen continental drain.
            if (ring <= kCollisionThickenRings) {
                double th = cell.thicknessKm + thickenFull * thFalloff;
                if (th > kMaxCrustThicknessKm) th = kMaxCrustThicknessKm;
                cell.thicknessKm = static_cast<float>(th);
            }
            if (ring <= kOrogenyRecentDateRings &&
                cell.orogenyIntensity >= kOrogenyCoverageFloor) {
                // Young crest: a recent, active suture (rifts may re-open it). Gated on built
                // intensity so a faint grazing touch in the band does not register as a young
                // orogen — coverage tracks where a belt genuinely built, and the band stays a
                // line, not a swath. Thickening above is unconditional (it builds the crust
                // and the rift-relevant suture set is driven by these dated cells).
                cell.orogenyMyr = nowI;
            } else if ((cell.orogenyMyr == kOrogenyNever ||
                        (nowI - cell.orogenyMyr) >= kRiftSutureRecentMyr) &&
                       cell.orogenyIntensity >= kOrogenyCoverageFloor) {
                // Flank/apron: record an orogeny for coverage / M-T4 texture ONLY once the
                // cell has accumulated real intensity (a faint single-touch cell stays
                // unstamped, keeping coverage on cells that actually built a belt). Date it
                // OLD (just past the recent-suture window) so it does NOT bias rift cuts and
                // so TerrainStage's ageDecay subdues it to a low flank, not a second wall.
                // A cell already carrying a recent core stamp keeps it (don't age a live
                // orogen).
                cell.orogenyMyr = nowI - kRiftSutureRecentMyr;
            }
        }
    }

    // ---- Apply CO/OO arc bands (overriding side) ----
    for (int p = 0; p < K; ++p) {
        if (arcSeeds[static_cast<size_t>(p)].empty()) continue;
        if (!plates_[static_cast<size_t>(p)].alive) continue;
        band.clear();
        bandBFS(arcSeeds[static_cast<size_t>(p)], static_cast<uint8_t>(p),
                kArcBandRingMax, band);
        const float volcRate = kArcVolcanismPerStep *
                               static_cast<float>(areaControllerFactor_);
        // Deficit-driven arc crust production rates (0 at/above target). Production lives
        // here, in the arc band, so juvenile continental crust is minted only at real
        // subduction zones — never from mid-plate hotspot volcanism (which would speckle
        // basin interiors with confetti).
        const bool deficit = areaControllerFactor_ > 1.0;
        const double matureProb = deficit
            ? kArcMatureProbPerStep * (areaControllerFactor_ - 1.0) : 0.0;
        const double marginProb = deficit
            ? kMarginAccretionProb * (areaControllerFactor_ - 1.0) : 0.0;
        for (const auto& pr : band) {
            TileId w = pr.first;
            int ring = pr.second;
            if (ring < kArcBandRingMin) continue; // volcanic front sits inland
            TileId local = worldToLocal(static_cast<uint32_t>(p), w);
            if (local == kInvalidTile) continue;
            CrustCell& cell = plates_[static_cast<size_t>(p)].crust[local];
            if (cell.type == CrustType::None) continue;

            float v = cell.volcanism + volcRate;
            cell.volcanism = v > 1.0f ? 1.0f : v;

            if (deficit && cell.type == CrustType::Oceanic) {
                // Island-arc maturation (dominant producer): an arc cell that has built
                // up enough volcanism converts to juvenile continental crust. Eligibility
                // is the volcanism floor; the flip is deficit-gated and probabilistic so
                // an irreversible conversion drains a deficit gradually and settles near
                // target instead of overshooting.
                //
                // M-T3.5 nucleation rule: a cell may only mature if it has at least
                // kArcMaturationMinNeighbors neighbors that are already continental OR
                // have volcanism >= kArcMaturationSupportVolcFrac * kArcMatureVolcThreshold.
                // Arc bands are linear structures; cells on a real arc front have supportive
                // neighbors. Isolated cells dispersed by plate churn into open ocean have no
                // continental or strongly-volcanic neighbors and are rejected, so they cannot
                // produce isolated continental specks.
                bool matured = false;
                if (cell.volcanism >= kArcMatureVolcThreshold) {
                    // Count supporting neighbors before the maturation roll.
                    const float supportVolcMin = kArcMaturationSupportVolcFrac * kArcMatureVolcThreshold;
                    uint32_t supportN = 0;
                    {
                        std::array<TileId, 6> wNbrs{};
                        uint32_t nCnt = grid_->neighbors(w, wNbrs);
                        for (uint32_t ni = 0; ni < nCnt; ++ni) {
                            TileId wn = wNbrs[ni];
                            const CrustCell& nc = resolved_[wn];
                            if (nc.type == CrustType::Continental ||
                                nc.volcanism >= supportVolcMin) {
                                ++supportN;
                            }
                        }
                    }
                    if (supportN >= kArcMaturationMinNeighbors) {
                        uint32_t roll = foundation::hash3(static_cast<int32_t>(w), p, step_,
                            static_cast<uint32_t>(matureStream_ ^ (matureStream_ >> 32)));
                        double u = static_cast<double>(roll) * (1.0 / 4294967296.0);
                        if (u < matureProb) {
                            cell.type = CrustType::Continental;
                            cell.thicknessKm = kJuvenileArcThicknessKm;
                            cell.birthMyr = nowI;            // juvenile crust, born now
                            cell.orogenyMyr = kOrogenyNever; // an arc, not an orogen
                            matured = true;                  // keep volcanism: stays active
                        }
                    }
                }
                // Continental-margin progradation (secondary): right at the trench-
                // adjacent forearc, a margin oceanic cell occasionally becomes thin
                // juvenile continental crust, so established margins creep seaward.
                // M-T3.5: apply the same neighbor support check so isolated margin cells
                // (forearc cells stranded by plate churn) also cannot nucleate specks.
                if (!matured && ring <= kMarginAccretionMaxRing) {
                    const float supportVolcMin = kArcMaturationSupportVolcFrac * kArcMatureVolcThreshold;
                    uint32_t supportN = 0;
                    {
                        std::array<TileId, 6> wNbrs{};
                        uint32_t nCnt = grid_->neighbors(w, wNbrs);
                        for (uint32_t ni = 0; ni < nCnt; ++ni) {
                            TileId wn = wNbrs[ni];
                            const CrustCell& nc = resolved_[wn];
                            if (nc.type == CrustType::Continental ||
                                nc.volcanism >= supportVolcMin) {
                                ++supportN;
                            }
                        }
                    }
                    if (supportN >= kArcMaturationMinNeighbors) {
                        uint32_t roll = foundation::hash3(static_cast<int32_t>(w), p,
                            step_ ^ 0x5A5A,
                            static_cast<uint32_t>(marginStream_ ^ (marginStream_ >> 32)));
                        double u = static_cast<double>(roll) * (1.0 / 4294967296.0);
                        if (u < marginProb) {
                            cell.type = CrustType::Continental;
                            cell.thicknessKm = kJuvenileArcThicknessKm;
                            cell.birthMyr = nowI;
                            cell.orogenyMyr = kOrogenyNever;
                        }
                    }
                }
            }

            // Modest thickening (arc crust grows).
            double th = cell.thicknessKm + kArcThickenKmPerStep;
            if (th > kMaxCrustThicknessKm) th = kMaxCrustThicknessKm;
            cell.thicknessKm = static_cast<float>(th);
        }
    }

    // ---- Update per-plate collision-block factor from CC contact extent ----
    // A plate's block rises with the fraction of its boundary in CC collision; deep
    // collision (block -> kMaxCollisionBlock) nearly halts the plate so continents
    // suture instead of interpenetrating. Decays when collision eases.
    if (collisionBlock_.size() < plates_.size()) collisionBlock_.resize(plates_.size(), 0.0f);
    for (int p = 0; p < K; ++p) {
        if (!plates_[static_cast<size_t>(p)].alive) { collisionBlock_[static_cast<size_t>(p)] = 0.0f; continue; }
        uint32_t ccTiles = static_cast<uint32_t>(ccSeeds[static_cast<size_t>(p)].size());
        // Target block from contact extent (saturating).
        float target = static_cast<float>(ccTiles) / static_cast<float>(kCollisionBlockRefTiles);
        if (target > kMaxCollisionBlock) target = static_cast<float>(kMaxCollisionBlock);
        float& b = collisionBlock_[static_cast<size_t>(p)];
        // Smooth toward target (rise fast, fall slower).
        b = (target > b) ? (b + (target - b) * 0.5f) : (b * 0.8f);
    }
}

// ============================================================================
// Terrane accretion: small continental fragments riding a subducting plate get
// plastered onto the overriding plate when they reach a CO trench.
// ============================================================================
//
// We identify continental connected components (in WORLD ownership) smaller than
// kTerraneMaxAreaFraction*N that touch a CO boundary where their own plate is the
// subducting side. Such a fragment transfers: its cells are copied into the
// overriding plate's baseline frame and erased from the donor. Cells, not crust,
// move — area is conserved.

void PlateSim::terraneAccretion() {
    const int K = static_cast<int>(plates_.size());
    const uint32_t maxArea = static_cast<uint32_t>(kTerraneMaxAreaFraction *
                                                   static_cast<double>(tileCount_));
    if (maxArea < 1) return;
    // The full continental flood-fill is the costliest event pass and terranes mature
    // slowly; run it on a stride to stay in budget without changing the dynamics
    // materially. Deterministic (driven by step index).
    if ((step_ % kTerraneStride) != 0) return;

    // Flood continental components over world ownership. comp/stack/cells are reused
    // across calls to avoid per-stride heap churn now that fragments actually dock.
    terraneComp_.assign(tileCount_, -1);
    std::vector<int32_t>& comp = terraneComp_;
    std::array<TileId, 6> nbrs{};
    std::vector<TileId>& stack = terraneStack_;
    std::vector<TileId>& cells = terraneCells_;
    int compCount = 0;
    for (TileId s = 0; s < tileCount_; ++s) {
        if (comp[s] >= 0) continue;
        if (resolved_[s].type != CrustType::Continental) continue;
        // BFS this component.
        int cid = compCount++;
        comp[s] = cid;
        stack.clear();
        stack.push_back(s);
        cells.clear();
        uint8_t pid = owner_[s];
        bool homogeneous = true;
        while (!stack.empty()) {
            TileId cur = stack.back(); stack.pop_back();
            cells.push_back(cur);
            if (owner_[cur] != pid) homogeneous = false;
            uint32_t cnt = grid_->neighbors(cur, nbrs);
            for (uint32_t k = 0; k < cnt; ++k) {
                TileId v = nbrs[k];
                if (comp[v] >= 0) continue;
                if (resolved_[v].type != CrustType::Continental) continue;
                comp[v] = cid;
                stack.push_back(v);
            }
        }
        if (cells.size() > maxArea) continue;     // too big to be a terrane
        if (!homogeneous) continue;               // spans plates already; skip
        if (pid >= static_cast<uint8_t>(K)) continue;
        // Only exotic blocks riding an OCEANIC plate dock: a microcontinent or matured
        // intra-oceanic arc that is a passenger on a down-going ocean plate. A coastal
        // arc on a continental-majority plate is part of that continent's own margin and
        // stays put.
        if (plates_[pid].isContinental) continue;
        // A live magmatic arc is still building in place; a terrane docks only once it
        // has drifted clear and gone magmatically quiet. Require the fragment's mean
        // volcanism to be low so freshly-matured arc cells don't shuttle the moment they
        // form — they must age out of arc activity first. (Exotic terranes are old,
        // amalgamated blocks by the time they suture, Coney et al. 1980.)
        {
            double volcSum = 0.0;
            for (TileId c : cells) {
                TileId lc = worldToLocal(pid, c);
                if (lc != kInvalidTile) volcSum += plates_[pid].crust[lc].volcanism;
            }
            if (volcSum > kTerraneMaxMeanVolcanism * static_cast<double>(cells.size()))
                continue; // still an active arc, not a docking terrane
        }

        // Is the fragment riding a plate whose ocean is subducting at a CO trench, and
        // who is the overrider there? A continental cell at a CO boundary is always the
        // OVERRIDING side by classification, so the down-going trench sits on the
        // fragment plate's OCEANIC margin: look one ring out from the fragment for a
        // CO-subducting cell that pid still owns (its own subducting seafloor), and take
        // the foreign overrider across that trench. (Without this, the fragment's own
        // continental boundary cells are never tagged subducting and accretion can never
        // fire — the matured island arcs that M-T2.5 produces would never dock.)
        uint8_t overrider = static_cast<uint8_t>(kUnowned);
        std::array<TileId, 6> trenchNbrs{};
        for (TileId c : cells) {
            uint32_t cnt = grid_->neighbors(c, nbrs);
            for (uint32_t k = 0; k < cnt; ++k) {
                TileId trench = nbrs[k];
                if (owner_[trench] != pid) continue; // pid's own oceanic margin
                if (bndType_[trench] != static_cast<uint8_t>(BoundaryType::ConvergentCO)) continue;
                if (bndSide_[trench] != kSideSubducting) continue;
                uint32_t tc = grid_->neighbors(trench, trenchNbrs);
                for (uint32_t k2 = 0; k2 < tc; ++k2) {
                    uint8_t np = owner_[trenchNbrs[k2]];
                    if (np != pid && np < static_cast<uint8_t>(K)) {
                        if (overrider == kUnowned || np < overrider) overrider = np;
                    }
                }
            }
        }
        if (overrider == kUnowned || overrider == pid) continue;
        if (!plates_[overrider].alive) continue;

        // Transfer: copy each cell's crust into the overrider's baseline frame,
        // erase from the donor. Area conserved.
        SimPlate& donor = plates_[pid];
        SimPlate& dst   = plates_[overrider];
        bool moved = false;
        for (TileId c : cells) {
            TileId srcLocal = worldToLocal(pid, c);
            if (srcLocal == kInvalidTile) continue;
            CrustCell cc = donor.crust[srcLocal];
            if (cc.type != CrustType::Continental) continue;
            TileId dstLocal = worldToLocal(overrider, c);
            if (dstLocal == kInvalidTile) continue;
            if (dst.crust[dstLocal].type == CrustType::None) {
                dst.crust[dstLocal] = cc;
                dst.occupied.push_back(dstLocal);
            }
            donor.crust[srcLocal] = CrustCell{};
            owner_[c] = overrider;
            moved = true;
        }
        if (moved) ++accretionCount_;
    }
}

// ============================================================================
// Erosion proxy: continental thickness relaxes toward equilibrium.
// ============================================================================

void PlateSim::erosionProxy() {
    // decay = exp(-dt/tau); precompute once.
    const double decay = foundation::det_math::exp(-dtMyr_ / kErosionTauMyr);
    const double eq = kErosionEqThicknessKm;
    // Volcanism decays toward 0 with its own e-folding time: arc magmatism shuts off when
    // subduction moves on (extinct arcs go quiet). Active arcs re-receive their deposit
    // each step (collisionProcessing/hotspots run before this), so they stay hot; only
    // abandoned arcs cool. This lets a matured arc age out of arc activity so it can later
    // dock as a quiet exotic terrane, and keeps volcanism from pinning the whole margin at
    // 1.0 for M-T4 to read. Arc crust production itself lives in collisionProcessing's arc
    // band so it fires only at real subduction zones, not mid-plate hotspots.
    const double volcDecay = foundation::det_math::exp(-dtMyr_ / kVolcanismTauMyr);
    for (auto& pl : plates_) {
        if (!pl.alive) continue;
        for (TileId local : pl.occupied) {
            CrustCell& cell = pl.crust[local];
            if (cell.type == CrustType::None) continue;
            cell.volcanism = static_cast<float>(cell.volcanism * volcDecay);
            if (cell.type == CrustType::Continental) {
                double th = eq + (static_cast<double>(cell.thicknessKm) - eq) * decay;
                cell.thicknessKm = static_cast<float>(th);
            }
        }
    }
}

// ============================================================================
// Hotspots: fixed world-frame plumes deposit volcanism on the owning plate.
// ============================================================================

void PlateSim::initHotspots(uint64_t seed) {
    int H = kHotspotBase + cfg_.plateCount / kHotspotPerPlateDiv;
    if (H > kHotspotCap) H = kHotspotCap;
    if (H < 1) H = 1;
    foundation::Pcg32 rng(seed);
    hotspots_.clear();
    hotspots_.reserve(static_cast<size_t>(H));
    for (int i = 0; i < H; ++i) {
        double x{}, y{}, z{};
        for (;;) {
            x = rng.nextDouble() * 2.0 - 1.0;
            y = rng.nextDouble() * 2.0 - 1.0;
            z = rng.nextDouble() * 2.0 - 1.0;
            double r2 = x * x + y * y + z * z;
            if (r2 > 1e-4 && r2 <= 1.0) {
                double inv = 1.0 / foundation::det_math::sqrt(r2);
                hotspots_.push_back({x * inv, y * inv, z * inv});
                break;
            }
        }
    }
}

void PlateSim::hotspots() {
    std::array<TileId, 6> nbrs{};
    for (const Vec3d& plume : hotspots_) {
        TileId w = grid_->fromUnitVector(plume);
        if (w == kInvalidTile) continue;
        uint8_t pid = owner_[w];
        if (pid >= static_cast<uint8_t>(plates_.size())) continue;
        if (!plates_[pid].alive) continue;
        auto deposit = [&](TileId worldCell, float rate) {
            uint8_t op = owner_[worldCell];
            if (op >= static_cast<uint8_t>(plates_.size()) || !plates_[op].alive) return;
            TileId local = worldToLocal(op, worldCell);
            if (local == kInvalidTile) return;
            CrustCell& cell = plates_[op].crust[local];
            if (cell.type == CrustType::None) return;
            float v = cell.volcanism + rate;
            cell.volcanism = v > 1.0f ? 1.0f : v;
        };
        deposit(w, kHotspotVolcPerStep);
        uint32_t cnt = grid_->neighbors(w, nbrs);
        for (uint32_t k = 0; k < cnt; ++k) deposit(nbrs[k], kHotspotVolcPerStep * 0.5f);
    }
}

// ============================================================================
// Slow Euler-pole evolution: re-draw pole + speed with continuity on a schedule.
// ============================================================================

void PlateSim::evolvePoles() {
    const int K = static_cast<int>(plates_.size());
    bool any = false;
    for (int p = 0; p < K; ++p) {
        SimPlate& pl = plates_[static_cast<size_t>(p)];
        if (!pl.alive) continue;
        if (static_cast<size_t>(p) >= nextPoleEvolveMyr_.size()) continue;
        if (nowMyr_ < nextPoleEvolveMyr_[static_cast<size_t>(p)]) continue;

        // Per-plate, per-event RNG derived from base stream, plate id, and the
        // event index, so the sequence is deterministic and independent.
        uint64_t evIdx = static_cast<uint64_t>(nowMyr_ / kPoleEvolutionPeriodMyr) + 1u;
        foundation::Pcg32 rng(deriveSeed(poleEvolveStream_,
            (static_cast<uint64_t>(p) << 32) ^ (evIdx * 0x9E3779B97F4A7C15ULL)));

        // Bounded random rotation of the pole: build a random axis, rotate the
        // pole by up to the max nudge. Continuity preserved (small angle).
        double ax{}, ay{}, az{};
        for (;;) {
            ax = rng.nextDouble() * 2.0 - 1.0;
            ay = rng.nextDouble() * 2.0 - 1.0;
            az = rng.nextDouble() * 2.0 - 1.0;
            double r2 = ax * ax + ay * ay + az * az;
            if (r2 > 1e-4 && r2 <= 1.0) { double inv = 1.0 / foundation::det_math::sqrt(r2);
                ax *= inv; ay *= inv; az *= inv; break; }
        }
        double angle = (rng.nextDouble() * 2.0 - 1.0) * kPoleEvolutionMaxNudgeRad;
        double rq[4];
        quatFromAxisAngle({ax, ay, az}, angle, rq);
        Vec3d np = quatRotate(rq, pl.eulerPole);
        double nl = foundation::det_math::sqrt(np.x * np.x + np.y * np.y + np.z * np.z);
        if (nl > 1e-12) pl.eulerPole = {np.x / nl, np.y / nl, np.z / nl};

        // Bounded fractional speed random-walk.
        double jit = 1.0 + (rng.nextDouble() * 2.0 - 1.0) * kPoleEvolutionSpeedJitter;
        pl.omegaRadPerMyr *= jit;

        nextPoleEvolveMyr_[static_cast<size_t>(p)] += kPoleEvolutionPeriodMyr;
        any = true;
    }
    if (any) rebalanceMomentum();
}

// ============================================================================
// Slab pull (M-T2.6): scale each plate's omega by the age of its subducting slab.
// ============================================================================
//
// Slab pull is the dominant plate driving force; the pull scales with slab age
// because older ocean floor is colder, denser, and sinks harder (Forsyth & Uyeda
// 1975; Conrad & Lithgow-Bertelloni 2002). For each plate we gather the crustAge of
// its OWN oceanic cells that sit on the subducting side of a convergent boundary (the
// slab it is feeding into a trench), take the mean, and map it to a target speed
// factor anchored at 1.0 for a reference-age slab. The applied multiplier relaxes
// toward the target each step (smooth, deterministic), then omega is rescaled and the
// resulting surface speed clamped to the init cm/yr bounds times a slack. A plate with
// old subducting floor accelerates trenchward, consuming that old floor — which is the
// physical recycling that keeps basin ages near Earth's.
//
// Slab pull supplies SPEED; this pass also supplies DIRECTION. Fixed/random poles
// strand basin interiors so seafloor ages past Earth's ceiling. Each step we steer each
// plate's Euler pole a small amount so its OLDEST ocean floor drifts toward the nearest
// trench it subducts into. The steer is small per step (continuous, deterministic) and
// lives here rather than in evolvePoles so the momentum rebalance there does not clobber
// it. Together: old floor is both sped up and aimed at a trench, so it recycles.
void PlateSim::slabPull() {
    const int K = static_cast<int>(plates_.size());
    if (static_cast<int>(slabPull_.size()) < K) slabPull_.resize(K, 1.0);

    const int32_t nowI = static_cast<int32_t>(nowMyr_ + dtMyr_ + 0.5);

    // Per-plate accumulation of subducting-floor age (for slab pull) plus age-weighted
    // oceanic-floor centroid and subducting-trench centroid (for steering).
    std::vector<double>   ageSum(static_cast<size_t>(K), 0.0);
    std::vector<uint32_t> ageCnt(static_cast<size_t>(K), 0u);
    std::vector<Vec3d>    oldCtr(static_cast<size_t>(K), Vec3d{0, 0, 0});
    std::vector<double>   oldW(static_cast<size_t>(K), 0.0);
    std::vector<Vec3d>    trenchCtr(static_cast<size_t>(K), Vec3d{0, 0, 0});
    std::vector<uint32_t> trenchN(static_cast<size_t>(K), 0u);
    for (TileId t = 0; t < tileCount_; ++t) {
        uint8_t pid = owner_[t];
        if (pid >= static_cast<uint8_t>(K)) continue;
        const CrustCell& cc = resolved_[t];
        // Age-weighted oceanic-floor centroid (steering target source). Square the age
        // so the centroid is pulled hard toward the OLDEST floor, the floor most in need
        // of recycling.
        if (cc.type == CrustType::Oceanic) {
            int32_t age = nowI - cc.birthMyr;
            if (age > 0) {
                double w = static_cast<double>(age) * static_cast<double>(age);
                oldCtr[pid].x += w * centers_[t].x;
                oldCtr[pid].y += w * centers_[t].y;
                oldCtr[pid].z += w * centers_[t].z;
                oldW[pid] += w;
            }
        }
        uint8_t bt = bndType_[t];
        bool subducting = (bt == static_cast<uint8_t>(BoundaryType::ConvergentCO) ||
                           bt == static_cast<uint8_t>(BoundaryType::ConvergentOO)) &&
                          bndSide_[t] == kSideSubducting;
        if (!subducting) continue;
        trenchCtr[pid].x += centers_[t].x;
        trenchCtr[pid].y += centers_[t].y;
        trenchCtr[pid].z += centers_[t].z;
        ++trenchN[pid];
        if (cc.type != CrustType::Oceanic) continue; // only ocean slabs pull
        int32_t age = nowI - cc.birthMyr;
        if (age < 0) age = 0;
        ageSum[pid] += static_cast<double>(age);
        ageCnt[pid] += 1u;
    }

    // Normalize each plate's age-weighted old-floor centroid (steering source).
    std::vector<Vec3d> oldDir(static_cast<size_t>(K), Vec3d{0, 0, 0});
    std::vector<bool>  hasOld(static_cast<size_t>(K), false);
    for (int p = 0; p < K; ++p) {
        if (oldW[static_cast<size_t>(p)] <= 0.0) continue;
        double oi = 1.0 / oldW[static_cast<size_t>(p)];
        Vec3d O{oldCtr[static_cast<size_t>(p)].x * oi, oldCtr[static_cast<size_t>(p)].y * oi,
                oldCtr[static_cast<size_t>(p)].z * oi};
        double ol = foundation::det_math::sqrt(O.x*O.x + O.y*O.y + O.z*O.z);
        if (ol < 1e-9) continue;
        oldDir[static_cast<size_t>(p)] = {O.x/ol, O.y/ol, O.z/ol};
        hasOld[static_cast<size_t>(p)] = true;
    }

    // Find each plate's subducting trench cell NEAREST its old-floor centroid. Averaging
    // all trench cells can point to a meaningless mid-plate midpoint when a plate subducts
    // on opposite margins; steering toward the closest trench is geometrically correct.
    std::vector<Vec3d> nearTrench(static_cast<size_t>(K), Vec3d{0, 0, 0});
    std::vector<double> nearDot(static_cast<size_t>(K), -2.0); // max dot = nearest on sphere
    std::vector<bool>  hasTrench(static_cast<size_t>(K), false);
    for (TileId t = 0; t < tileCount_; ++t) {
        uint8_t pid = owner_[t];
        if (pid >= static_cast<uint8_t>(K)) continue;
        if (!hasOld[pid]) continue;
        uint8_t bt = bndType_[t];
        bool subducting = (bt == static_cast<uint8_t>(BoundaryType::ConvergentCO) ||
                           bt == static_cast<uint8_t>(BoundaryType::ConvergentOO)) &&
                          bndSide_[t] == kSideSubducting;
        if (!subducting) continue;
        const Vec3d& O = oldDir[pid];
        double dot = O.x*centers_[t].x + O.y*centers_[t].y + O.z*centers_[t].z;
        if (dot > nearDot[pid]) { nearDot[pid] = dot; nearTrench[pid] = centers_[t]; hasTrench[pid] = true; }
    }

    // Surface-speed clamp (rad/Myr) from the init cm/yr bounds times slack.
    const double maxOmega = cmYrToRadPerMyr(kOceanicSpeedMaxCmYr * kSlabSpeedSlack,
                                            cfg_.planetRadiusKm);

    for (int p = 0; p < K; ++p) {
        SimPlate& pl = plates_[static_cast<size_t>(p)];
        if (!pl.alive) { slabPull_[static_cast<size_t>(p)] = 1.0; continue; }

        // Target factor: 1.0 for a reference-age slab, ramps with the mean slab age.
        // A plate with too few trench cells has no attached slab -> target 1.0.
        double target;
        if (ageCnt[static_cast<size_t>(p)] >= kSlabPullMinTrenchCells) {
            double meanAge = ageSum[static_cast<size_t>(p)] /
                             static_cast<double>(ageCnt[static_cast<size_t>(p)]);
            target = 1.0 + kSlabPullPerAgeMyr * (meanAge - kSlabPullRefAgeMyr);
            if (target < kSlabPullFactorMin) target = kSlabPullFactorMin;
            if (target > kSlabPullFactorMax) target = kSlabPullFactorMax;
        } else {
            target = 1.0;
        }

        double& applied = slabPull_[static_cast<size_t>(p)];
        double prev = applied;
        applied += (target - applied) * kSlabPullRelax;

        // Rescale omega by the change in the applied factor, then clamp surface speed.
        if (prev > 1e-9) {
            pl.omegaRadPerMyr *= applied / prev;
        }
        double mag = pl.omegaRadPerMyr < 0 ? -pl.omegaRadPerMyr : pl.omegaRadPerMyr;
        if (mag > maxOmega) {
            double s = pl.omegaRadPerMyr < 0 ? -1.0 : 1.0;
            pl.omegaRadPerMyr = s * maxOmega;
        }

        // Steering: nudge the pole so the plate's oldest floor heads toward the nearest
        // trench. Needs a real attached slab (>= min trench cells), an old-floor centroid,
        // and a nearest-trench target.
        if (trenchN[static_cast<size_t>(p)] < kSlabPullMinTrenchCells) continue;
        if (!hasOld[static_cast<size_t>(p)] || !hasTrench[static_cast<size_t>(p)]) continue;
        Vec3d O = oldDir[static_cast<size_t>(p)];
        Vec3d T = nearTrench[static_cast<size_t>(p)];
        double tl = foundation::det_math::sqrt(T.x*T.x + T.y*T.y + T.z*T.z);
        if (tl < 1e-9) continue;
        T = {T.x/tl, T.y/tl, T.z/tl};
        // Desired surface direction at O: toward T, projected onto O's tangent plane.
        Vec3d d{T.x - O.x, T.y - O.y, T.z - O.z};
        double dn = d.x*O.x + d.y*O.y + d.z*O.z;
        d = {d.x - dn*O.x, d.y - dn*O.y, d.z - dn*O.z};
        double dl = foundation::det_math::sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
        if (dl < 1e-6) continue;
        d = {d.x/dl, d.y/dl, d.z/dl};
        // Pole carrying O toward d is axis = O x d. Align its hemisphere with the current
        // omega sign so steering does not flip the plate's spin sense, then rotate the
        // current pole a small bounded angle toward it (continuous, deterministic).
        Vec3d tgt{O.y*d.z - O.z*d.y, O.z*d.x - O.x*d.z, O.x*d.y - O.y*d.x};
        double gl = foundation::det_math::sqrt(tgt.x*tgt.x + tgt.y*tgt.y + tgt.z*tgt.z);
        if (gl < 1e-9) continue;
        tgt = {tgt.x/gl, tgt.y/gl, tgt.z/gl};
        if (tgt.x*pl.eulerPole.x + tgt.y*pl.eulerPole.y + tgt.z*pl.eulerPole.z < 0.0)
            tgt = {-tgt.x, -tgt.y, -tgt.z};
        double a = kPoleSteerPerStep;
        Vec3d blended{(1.0-a)*pl.eulerPole.x + a*tgt.x,
                      (1.0-a)*pl.eulerPole.y + a*tgt.y,
                      (1.0-a)*pl.eulerPole.z + a*tgt.z};
        double bl = foundation::det_math::sqrt(blended.x*blended.x + blended.y*blended.y +
                                               blended.z*blended.z);
        if (bl > 1e-9) pl.eulerPole = {blended.x/bl, blended.y/bl, blended.z/bl};
    }
}

// ============================================================================
// Plate events: merge then rift.
// ============================================================================

uint32_t PlateSim::allocPlateId() {
    const int K = static_cast<int>(plates_.size());
    for (int p = 0; p < K; ++p) {
        if (!plates_[static_cast<size_t>(p)].alive) {
            // Reuse a dead slot: reset it fully.
            SimPlate fresh;
            fresh.crust.assign(tileCount_, CrustCell{});
            plates_[static_cast<size_t>(p)] = std::move(fresh);
            if (static_cast<size_t>(p) < nextPoleEvolveMyr_.size())
                nextPoleEvolveMyr_[static_cast<size_t>(p)] = nowMyr_ + kPoleEvolutionPeriodMyr;
            return static_cast<uint32_t>(p);
        }
    }
    if (plates_.size() >= 254) return kUnowned; // id space exhausted (u8, 255 reserved)
    uint32_t id = static_cast<uint32_t>(plates_.size());
    SimPlate fresh;
    fresh.crust.assign(tileCount_, CrustCell{});
    plates_.push_back(std::move(fresh));
    nextPoleEvolveMyr_.push_back(nowMyr_ + kPoleEvolutionPeriodMyr);
    return id;
}

// Merge/rift never rebaseline a plate to identity: forward-rotating a long-drifted
// raster into the world frame would compress cells (many locals -> one world cell)
// and lose crust. They map cells directly between plate frames via world
// coordinates instead, so the only continental cells lost are genuine suture
// overlaps.
//
// Drop ONLY oceanic crust from a plate's raster at local cells whose world position
// the plate no longer owns (that ocean subducted under a neighbor). Continental
// crust is never dropped here: it is buoyant and cannot subduct, and dropping it
// would destroy continental cells (the conservation contract requires cells to move,
// never vanish). Stale continental crust is harmless duplicate bookkeeping that the
// merge logic deduplicates by world position.
void PlateSim::pruneStaleCrust(uint32_t pid) {
    SimPlate& pl = plates_[pid];
    const double* q = pl.rotation;
    auto& occ = pl.occupied;
    size_t w2 = 0;
    for (size_t i = 0; i < occ.size(); ++i) {
        TileId local = occ[i];
        CrustCell& cc = pl.crust[local];
        if (cc.type == CrustType::None) continue;
        if (cc.type == CrustType::Oceanic) {
            Vec3d wp = quatRotate(q, centers_[local]);
            TileId w = grid_->fromUnitVector(wp);
            if (w == kInvalidTile || owner_[w] != static_cast<uint8_t>(pid)) {
                cc = CrustCell{}; // subducted ocean
                continue;
            }
        }
        occ[w2++] = local;
    }
    occ.resize(w2);
}

// Merge donor into keep WITHOUT rebaselining. We map each donor OWNED world cell
// into keep's frame and write donor's crust there. donor owns these cells so keep
// does not — any crust keep already has at that keep-local index is STALE duplicate
// bookkeeping (keep placed it once, then lost the world cell), so we overwrite it
// rather than treating it as a suture; this dedups phantom crust without destroying
// real donor cells. A genuine continent-continent suture is stamped where keep's and
// donor's REAL footprints abut, detected via the resolved continental neighbor.
void PlateSim::mergePlates(uint32_t keep, uint32_t donor) {
    pruneStaleCrust(keep);
    pruneStaleCrust(donor);
    SimPlate& k = plates_[keep];
    const int32_t nowI = static_cast<int32_t>(nowMyr_ + dtMyr_ + 0.5);

    double kqInv[4] = {k.rotation[0], -k.rotation[1], -k.rotation[2], -k.rotation[3]};
    std::array<TileId, 6> nbrs{};

    for (TileId w = 0; w < tileCount_; ++w) {
        if (owner_[w] != static_cast<uint8_t>(donor)) continue;
        const CrustCell& dc = resolved_[w]; // donor's resolved crust at world cell w
        if (dc.type == CrustType::None) continue;
        Vec3d lp = quatRotate(kqInv, centers_[w]); // world -> keep-local
        TileId kl = grid_->fromUnitVector(lp);
        if (kl == kInvalidTile) continue;
        CrustCell& kc = k.crust[kl];
        bool wasOccupied = (kc.type != CrustType::None);
        kc = dc;                       // donor real crust wins (keep's here was stale)
        if (!wasOccupied) k.occupied.push_back(kl);
        // Suture stamp: if a keep-owned continental cell neighbors this world cell,
        // the two real footprints abut here — record an orogeny.
        bool sutured = false;
        uint32_t cnt = grid_->neighbors(w, nbrs);
        for (uint32_t n = 0; n < cnt; ++n) {
            if (owner_[nbrs[n]] == static_cast<uint8_t>(keep) &&
                resolved_[nbrs[n]].type == CrustType::Continental &&
                dc.type == CrustType::Continental) { sutured = true; break; }
        }
        if (sutured) {
            kc.orogenyMyr = nowI;
            float inten = kc.orogenyIntensity + kSutureOrogenyIntensity;
            kc.orogenyIntensity = inten > 1.0f ? 1.0f : inten;
        }
    }

    // keep's continentality may flip after absorbing the donor.
    uint32_t cont = 0, tot = 0;
    for (const auto& c : k.crust) {
        if (c.type == CrustType::None) continue;
        ++tot;
        if (c.type == CrustType::Continental) ++cont;
    }
    k.isContinental = (tot > 0 && cont * 2u > tot);

    SimPlate& d = plates_[donor];
    d.alive = false;
    d.crust.clear();
    d.crust.shrink_to_fit();
    d.occupied.clear();
    d.occupied.shrink_to_fit();
    // Reassign owner_ for donor cells to keep so subsequent passes are consistent.
    for (TileId t = 0; t < tileCount_; ++t)
        if (owner_[t] == static_cast<uint8_t>(donor)) owner_[t] = static_cast<uint8_t>(keep);

    ++mergeCount_;
}

// Split plate pid into two. A continental rift's cut favors recent sutures (rifts
// re-open inherited weaknesses); an oversized-plate reorganization (oversized=true)
// takes a noisy great-circle cut biased toward young/ridge-adjacent oceanic crust
// instead (no sutures in ocean; reorganizations reactivate weak young lithosphere,
// Farallon/Pacific breakups). The far side of the cut becomes a new plate with an
// opposing Euler pole.
bool PlateSim::tryRift(uint32_t pid, uint64_t stepSalt, bool oversized) {
    SimPlate& src = plates_[pid];
    if (!src.alive) return false;

    // Work in the plate's CURRENT world footprint. Collect owned world cells.
    std::vector<TileId> owned;
    owned.reserve(src.occupied.size());
    for (TileId t = 0; t < tileCount_; ++t) if (owner_[t] == static_cast<uint8_t>(pid)) owned.push_back(t);
    if (owned.size() < 16) return false;

    foundation::Pcg32 rng(deriveSeed(riftStream_, stepSalt ^ (static_cast<uint64_t>(pid) << 40)));
    const uint32_t N = tileCount_;
    const int32_t nowI = static_cast<int32_t>(nowMyr_ + 0.5);

    // Plate centroid (mean of owned cell centers, renormalized).
    Vec3d centroid{0, 0, 0};
    for (TileId c : owned) { centroid.x += centers_[c].x; centroid.y += centers_[c].y; centroid.z += centers_[c].z; }
    { double l = foundation::det_math::sqrt(centroid.x*centroid.x + centroid.y*centroid.y + centroid.z*centroid.z);
      if (l < 1e-9) return false; centroid.x/=l; centroid.y/=l; centroid.z/=l; }

    // Recent-suture cells: owned cells carrying a recent orogeny stamp. If enough
    // exist, the rift cut plane is set to the great circle that best fits them (the
    // suture line) so the rift re-opens the inherited weakness. The fit plane's
    // normal is the least-variance axis of the suture cells' scatter matrix (a band
    // along a great circle has its smallest spread along that circle's axis).
    // Detect a recent suture. A normal (deficit-driven) continental rift re-opens it. An
    // oversized reorganization skips suture detection entirely and takes a great-circle
    // cut (young-biased for ocean): its job is to halve a runaway plate, and a stray
    // orogeny stamp must not divert that cut into a lopsided sliver that leaves the plate
    // oversized. So suture detection runs only for a normal rift, never an oversized one.
    Vec3d sutureMean{0, 0, 0};
    uint32_t sutureN = 0;
    if (!oversized) {
        for (TileId c : owned) {
            TileId local = worldToLocal(pid, c);
            if (local == kInvalidTile) continue;
            const CrustCell& cc = src.crust[local];
            if (cc.orogenyMyr != kOrogenyNever && (nowI - cc.orogenyMyr) < kRiftSutureRecentMyr) {
                sutureMean.x += centers_[c].x; sutureMean.y += centers_[c].y; sutureMean.z += centers_[c].z;
                ++sutureN;
            }
        }
    }

    Vec3d normal;
    bool sutureBiased = false;
    if (sutureN >= 4) {
        double inv = 1.0 / static_cast<double>(sutureN);
        Vec3d mu{sutureMean.x*inv, sutureMean.y*inv, sutureMean.z*inv};
        double m[9] = {0,0,0,0,0,0,0,0,0};
        for (TileId c : owned) {
            TileId local = worldToLocal(pid, c);
            if (local == kInvalidTile) continue;
            const CrustCell& cc = src.crust[local];
            if (cc.orogenyMyr == kOrogenyNever || (nowI - cc.orogenyMyr) >= kRiftSutureRecentMyr) continue;
            Vec3d d{centers_[c].x - mu.x, centers_[c].y - mu.y, centers_[c].z - mu.z};
            m[0]+=d.x*d.x; m[1]+=d.x*d.y; m[2]+=d.x*d.z;
            m[4]+=d.y*d.y; m[5]+=d.y*d.z; m[8]+=d.z*d.z;
        }
        m[3]=m[1]; m[6]=m[2]; m[7]=m[5];
        normal = minEigenVector(m);
        sutureBiased = true;
    }
    if (!sutureBiased) {
        // Random tangent normal at the centroid.
        Vec3d r;
        for (;;) {
            r = {rng.nextDouble()*2.0-1.0, rng.nextDouble()*2.0-1.0, rng.nextDouble()*2.0-1.0};
            double d = r.x*centroid.x + r.y*centroid.y + r.z*centroid.z;
            r = {r.x - d*centroid.x, r.y - d*centroid.y, r.z - d*centroid.z};
            double l = foundation::det_math::sqrt(r.x*r.x + r.y*r.y + r.z*r.z);
            if (l > 1e-3) { normal = {r.x/l, r.y/l, r.z/l}; break; }
        }
    } else {
        double l = foundation::det_math::sqrt(normal.x*normal.x + normal.y*normal.y + normal.z*normal.z);
        if (l < 1e-9) return false;
        normal = {normal.x/l, normal.y/l, normal.z/l};
    }

    // Plane point: the great-circle suture passes through the sphere center, so a
    // suture-biased cut splits by sign of dot(center, normal) (plane through origin).
    // An unbiased cut passes through the plate centroid.
    Vec3d planePt = sutureBiased ? Vec3d{0, 0, 0} : centroid;

    // Split owned cells by the plane, with a per-cell noise jog on the boundary so
    // the rift margin is irregular (not a clean great circle).
    const auto noiseSeed = static_cast<uint32_t>(deriveSeed(riftStream_, stepSalt) ^ 0xABCDu);
    std::vector<int8_t> region(N, -1);
    uint32_t r1 = 0, r2 = 0;
    for (TileId c : owned) {
        Vec3d rel{centers_[c].x - planePt.x, centers_[c].y - planePt.y, centers_[c].z - planePt.z};
        double s = rel.x*normal.x + rel.y*normal.y + rel.z*normal.z;
        float n = boundaryNoise(static_cast<float>(centers_[c].x) * kBoundaryNoiseFreq,
                                static_cast<float>(centers_[c].y) * kBoundaryNoiseFreq,
                                static_cast<float>(centers_[c].z) * kBoundaryNoiseFreq, noiseSeed);
        s += static_cast<double>((n - 0.5f) * kRiftPathNoise) * 0.05; // jog the margin
        // Oversized-plate (oceanic) reorganization: pull the cut margin toward young /
        // ridge-adjacent crust so the split runs through weak, freshly-accreted floor
        // rather than slicing old cratonic interior. A young cell near the plane gets
        // its |s| reduced (more likely to flip across the cut), an old one stiffened.
        // Only in the oceanic fallback (no suture); a suture-biased cut keeps its plane.
        if (oversized && !sutureBiased) {
            TileId local = worldToLocal(pid, c);
            if (local != kInvalidTile) {
                const CrustCell& cc = src.crust[local];
                int32_t age = nowI - cc.birthMyr;
                if (age < 0) age = 0;
                // ageNorm in [0,1] over a typical basin span; young -> ~0, old -> ~1.
                double ageNorm = static_cast<double>(age) / 200.0;
                if (ageNorm > 1.0) ageNorm = 1.0;
                // Bias toward 0 (the cut) for young crust: shift |s| by a young-weighted
                // amount, sign-preserving so cells don't teleport across the plate.
                double bias = static_cast<double>(kOceanicRiftYoungBias) * (1.0 - ageNorm) * 0.05;
                s += (s >= 0.0 ? -bias : bias);
            }
        }
        if (s >= 0.0) { region[c] = 1; ++r1; } else { region[c] = 2; ++r2; }
    }
    std::array<TileId, 6> nbrs{};
    if (r1 < 8 || r2 < 8) return false; // degenerate split
    // Region 2 is the moved (new) plate; keep the larger side as src for stability.
    int8_t moveRegion = (r2 <= r1) ? 2 : 1;

    // Allocate the new plate; transfer region 2 cells. The new plate inherits src's
    // CURRENT rotation, so for any world cell the src-local and new-local indices are
    // identical (same frame) — no rebaseline, no rotation-rounding loss. The halves
    // then drift apart via opposing Euler poles.
    uint32_t nid = allocPlateId();
    if (nid == kUnowned) return false;

    // Re-fetch refs: allocPlateId may have grown plates_ and invalidated `src`.
    SimPlate& s = plates_[pid];
    SimPlate& nw = plates_[nid];
    nw.alive = true;
    nw.isContinental = s.isContinental;
    nw.rotation[0] = s.rotation[0]; nw.rotation[1] = s.rotation[1];
    nw.rotation[2] = s.rotation[2]; nw.rotation[3] = s.rotation[3];

    for (TileId c : owned) {
        if (region[c] != moveRegion) continue; // stays with src
        // moved region -> new plate. Move the cell at its src-local index to the SAME
        // local index in nw (shared frame).
        TileId local = worldToLocal(pid, c);
        if (local == kInvalidTile) continue;
        CrustCell cc = s.crust[local];
        if (cc.type == CrustType::None) continue;
        if (nw.crust[local].type == CrustType::None) {
            nw.crust[local] = cc;
            nw.occupied.push_back(local);
        }
        s.crust[local] = CrustCell{};
        owner_[c] = static_cast<uint8_t>(nid);
    }
    // Compact src.occupied (some entries now cleared).
    {
        auto& occ = s.occupied;
        size_t w2 = 0;
        for (size_t i = 0; i < occ.size(); ++i)
            if (s.crust[occ[i]].type != CrustType::None) occ[w2++] = occ[i];
        occ.resize(w2);
    }
    // Recompute continentality for both.
    auto contMajority = [&](const SimPlate& pl) {
        uint32_t cont = 0, tot = 0;
        for (const auto& c : pl.crust) { if (c.type == CrustType::None) continue; ++tot;
            if (c.type == CrustType::Continental) ++cont; }
        return tot > 0 && cont * 2u > tot;
    };
    s.isContinental = contMajority(s);
    nw.isContinental = contMajority(nw);

    // New plate gets an Euler pole that drives it AWAY from src across the rift: a
    // rotation about (centroid x normal) moves the new half along -normal, opening
    // the rift. src keeps its pole. Speed scaled to a typical drift rate.
    Vec3d axis{centroid.y*normal.z - centroid.z*normal.y,
               centroid.z*normal.x - centroid.x*normal.z,
               centroid.x*normal.y - centroid.y*normal.x};
    double al = foundation::det_math::sqrt(axis.x*axis.x + axis.y*axis.y + axis.z*axis.z);
    if (al > 1e-9) {
        nw.eulerPole = {axis.x/al, axis.y/al, axis.z/al};
        double sp = s.omegaRadPerMyr < 0 ? -s.omegaRadPerMyr : s.omegaRadPerMyr;
        if (sp < 1e-9) sp = cmYrToRadPerMyr(4.0, cfg_.planetRadiusKm);
        nw.omegaRadPerMyr = sp;
    } else {
        nw.eulerPole = {-s.eulerPole.x, -s.eulerPole.y, -s.eulerPole.z};
        nw.omegaRadPerMyr = s.omegaRadPerMyr;
    }
    if (static_cast<size_t>(nid) < nextPoleEvolveMyr_.size())
        nextPoleEvolveMyr_[nid] = nowMyr_ + kPoleEvolutionPeriodMyr;

    ++riftCount_;
    return true;
}

// ============================================================================
// Stranded-floor resurfacing (M-T2.7): coherent ridge nucleation.
// ============================================================================
//
// Replaces M-T2.6's per-cell resurfacing lottery, which reset over-old floor as
// salt-and-pepper and buried the coherent ridge-stripe structure under dither. Physically,
// new spreading nucleates as a coherent rift LINE, not random cells. We work per plate in
// its baseline raster: flood connected components of over-old oceanic floor, and for each
// region large enough to be a genuine stranded basin, with a per-REGION (not per-cell)
// probability that ramps with the region's mean over-age, carve a noisy great-circle swath
// through it (cut plane = least-variance axis of the region's scatter, so the ridge runs
// ALONG the basin's long axis) and stamp those cells to age 0 in one shot.
// Two design choices kill the dither the per-cell lottery produced:
//   - per-REGION probability: a basin of N cells no longer gets N independent coin flips;
//   - a high reset-age threshold + a large min-region size: only genuinely over-old, large
//     basins resurface (the >220 Myr tail), as coherent swaths, leaving the bulk of the
//     ridge-to-abyssal age gradient untouched.
// Deterministic: plates ascending, regions keyed by their min local cell; rolls hashed from
// (min cell, plate, step).
void PlateSim::resurfaceStrandedRidges(uint64_t stepSalt) {
    const int32_t nowI = static_cast<int32_t>(nowMyr_ + 0.5);
    const uint32_t jumpSeed = static_cast<uint32_t>(
        deriveSeed(riftStream_, 0x91D3E7C0FFEEULL ^ stepSalt));
    std::array<TileId, 6> nbrs{};

    for (uint32_t p = 0; p < plates_.size(); ++p) {
        SimPlate& pl = plates_[p];
        if (!pl.alive) continue;
        // Reset the per-local component map over this plate's occupied cells only.
        if (strandComp_.size() != tileCount_) strandComp_.assign(tileCount_, -1);
        for (TileId local : pl.occupied) strandComp_[local] = -1;

        int compCount = 0;
        for (TileId seed : pl.occupied) {
            if (strandComp_[seed] >= 0) continue;
            const CrustCell& sc = pl.crust[seed];
            if (sc.type != CrustType::Oceanic) continue;
            if (nowI - sc.birthMyr < kRidgeJumpResetAgeMyr) continue;
            // Flood the connected region of over-old oceanic floor on this plate's raster.
            int cid = compCount++;
            strandComp_[seed] = cid;
            strandStack_.clear();
            strandStack_.push_back(seed);
            strandCells_.clear();
            TileId minCell = seed;
            while (!strandStack_.empty()) {
                TileId cur = strandStack_.back(); strandStack_.pop_back();
                strandCells_.push_back(cur);
                if (cur < minCell) minCell = cur;
                uint32_t cnt = grid_->neighbors(cur, nbrs);
                for (uint32_t k = 0; k < cnt; ++k) {
                    TileId v = nbrs[k];
                    if (strandComp_[v] >= 0) continue;
                    const CrustCell& vc = pl.crust[v];
                    if (vc.type != CrustType::Oceanic) continue;
                    if (nowI - vc.birthMyr < kRidgeJumpResetAgeMyr) continue;
                    strandComp_[v] = cid;
                    strandStack_.push_back(v);
                }
            }
            if (strandCells_.size() < kRidgeJumpMinRegionCells) continue;

            // Per-REGION nucleation roll (not per cell — that was the salt-and-pepper bug).
            // Probability ramps with the region's mean over-age, so the oldest basins jump
            // first. A region that does NOT roll this step keeps aging untouched; the roll
            // is independent each step, so it eventually fires.
            double ageSum = 0.0;
            for (TileId c : strandCells_) ageSum += static_cast<double>(nowI - pl.crust[c].birthMyr);
            double meanOver = ageSum / static_cast<double>(strandCells_.size()) -
                              static_cast<double>(kRidgeJumpResetAgeMyr);
            if (meanOver < 0.0) meanOver = 0.0;
            double prob = kRidgeJumpRegionProb + kRidgeJumpRegionAgeGain * meanOver;
            if (prob > kRidgeJumpRegionProbMax) prob = kRidgeJumpRegionProbMax;
            uint32_t roll = foundation::hash3(static_cast<int32_t>(minCell),
                static_cast<int32_t>(p), step_, jumpSeed);
            if ((static_cast<double>(roll) * (1.0 / 4294967296.0)) >= prob) continue;

            // Cut plane: the least-variance axis of the region's local-cell scatter, so the
            // ridge runs ALONG the basin's long axis (a spreading center bisects the basin,
            // it doesn't slice it short). Plane point = region centroid.
            Vec3d mu{0, 0, 0};
            for (TileId c : strandCells_) {
                mu.x += centers_[c].x; mu.y += centers_[c].y; mu.z += centers_[c].z;
            }
            double inv = 1.0 / static_cast<double>(strandCells_.size());
            mu = {mu.x * inv, mu.y * inv, mu.z * inv};
            double m[9] = {0,0,0,0,0,0,0,0,0};
            for (TileId c : strandCells_) {
                Vec3d d{centers_[c].x - mu.x, centers_[c].y - mu.y, centers_[c].z - mu.z};
                m[0]+=d.x*d.x; m[1]+=d.x*d.y; m[2]+=d.x*d.z;
                m[4]+=d.y*d.y; m[5]+=d.y*d.z; m[8]+=d.z*d.z;
            }
            m[3]=m[1]; m[6]=m[2]; m[7]=m[5];
            Vec3d normal = minEigenVector(m);
            double nl = foundation::det_math::sqrt(normal.x*normal.x + normal.y*normal.y +
                                                   normal.z*normal.z);
            if (nl < 1e-9) continue;
            normal = {normal.x/nl, normal.y/nl, normal.z/nl};

            // Mark all region cells whose noisy signed distance to the plane falls inside a
            // band a few cells wide: the resurfaced spreading swath. Stamping a coherent
            // multi-cell band in ONE shot (rather than a one-cell thread widened over time)
            // is what makes the lineament read as a clean ridge instead of dither. The band
            // half-width is ~ kRidgeJumpBandHalfCells coarse cells.
            const uint32_t noiseSeed = jumpSeed ^ (static_cast<uint32_t>(minCell) * 0x9E3779B9u);
            const double cellArc = 2.0 / static_cast<double>(cfg_.coarseN); // ~chord per cell
            const double bandHalf = kRidgeJumpBandHalfCells * cellArc;
            // Mark via strandStack_ as scratch list of cells to resurface, then apply (so a
            // partial band still connects through a guaranteed-nucleus seed if it missed).
            strandStack_.clear();
            for (TileId c : strandCells_) {
                Vec3d rel{centers_[c].x - mu.x, centers_[c].y - mu.y, centers_[c].z - mu.z};
                double s = rel.x*normal.x + rel.y*normal.y + rel.z*normal.z;
                float n = boundaryNoise(static_cast<float>(centers_[c].x) * kBoundaryNoiseFreq,
                                        static_cast<float>(centers_[c].y) * kBoundaryNoiseFreq,
                                        static_cast<float>(centers_[c].z) * kBoundaryNoiseFreq,
                                        noiseSeed);
                double jog = static_cast<double>((n - 0.5f) * kRidgeJumpArcNoise) * bandHalf;
                if (s + jog < bandHalf && s + jog > -bandHalf) strandStack_.push_back(c);
            }
            if (strandStack_.empty()) {
                // Degenerate scatter: stamp the centroid-nearest cell so a swath exists.
                TileId best = strandCells_[0]; double bestDot = -2.0;
                for (TileId c : strandCells_) {
                    double dot = centers_[c].x*mu.x + centers_[c].y*mu.y + centers_[c].z*mu.z;
                    if (dot > bestDot) { bestDot = dot; best = c; }
                }
                strandStack_.push_back(best);
            }
            for (TileId c : strandStack_) {
                CrustCell& cc = pl.crust[c];
                cc.birthMyr = nowI;
                cc.thicknessKm = static_cast<float>(kOceanicThicknessKm);
            }
        }
    }
}

void PlateSim::plateEvents() {
    const int K = static_cast<int>(plates_.size());

    // ---- Reap plates squeezed to zero area (subducted/eroded away). They free
    // their id for reuse and stop counting against the controller set-point. ----
    {
        std::vector<uint32_t> areaNow(static_cast<size_t>(K), 0u);
        for (TileId t = 0; t < tileCount_; ++t) {
            uint8_t pid = owner_[t];
            if (pid < static_cast<uint8_t>(K)) areaNow[pid]++;
        }
        for (int p = 0; p < K; ++p) {
            SimPlate& pl = plates_[static_cast<size_t>(p)];
            if (pl.alive && areaNow[static_cast<size_t>(p)] == 0) {
                pl.alive = false;
                pl.crust.clear(); pl.crust.shrink_to_fit();
                pl.occupied.clear(); pl.occupied.shrink_to_fit();
            }
        }
    }

    // ---- Merge: any pair whose collision score exceeds threshold. ----
    // Iterate the score vector in a stable order (sort by key) so merges fire
    // deterministically.
    std::sort(collisionScore_.begin(), collisionScore_.end(),
              [](const auto& x, const auto& y) { return x.first < y.first; });
    for (auto& kv : collisionScore_) {
        if (kv.second < kMergeScoreThreshold) continue;
        uint8_t a = static_cast<uint8_t>(kv.first >> 8);
        uint8_t b = static_cast<uint8_t>(kv.first & 0xFF);
        if (a >= K || b >= K) { kv.second = 0.0; continue; }
        if (!plates_[a].alive || !plates_[b].alive) { kv.second = 0.0; continue; }
        // Merge smaller into larger.
        uint32_t aa = plateArea(a), ab = plateArea(b);
        uint32_t keep = aa >= ab ? a : b;
        uint32_t donor = aa >= ab ? b : a;
        mergePlates(keep, donor);
        kv.second = 0.0; // consumed
        // Drop any scores referencing the donor (it no longer exists).
        for (auto& kv2 : collisionScore_) {
            uint8_t x = static_cast<uint8_t>(kv2.first >> 8);
            uint8_t y = static_cast<uint8_t>(kv2.first & 0xFF);
            if (x == donor || y == donor) kv2.second = 0.0;
        }
    }
    // Compact consumed/zero scores out occasionally to keep the vector small.
    collisionScore_.erase(
        std::remove_if(collisionScore_.begin(), collisionScore_.end(),
                       [](const auto& kv) { return kv.second <= 0.0; }),
        collisionScore_.end());

    // Per-plate areas (owned world cells), computed once for both rift paths below.
    uint64_t areaSum = 0; uint32_t aliveN = 0;
    std::vector<uint32_t> areas(plates_.size(), 0u);
    for (uint32_t p = 0; p < plates_.size(); ++p) {
        if (!plates_[p].alive) continue;
        areas[p] = plateArea(p);
        areaSum += areas[p];
        ++aliveN;
    }

    // ---- Oversized-plate reorganization (M-T2.6): any plate (oceanic or continental)
    // larger than kMaxPlateAreaFrac of the sphere must break up, even when alive >= K.
    // Without this an oceanic plate is never rift-eligible (rifting was continental-
    // only) and runs away to ~half the sphere (Farallon/Pacific reorganizations are the
    // Earth analog). High-priority, independent probability; oceanic young-biased cut. --
    const uint32_t maxPlateTiles = static_cast<uint32_t>(kMaxPlateAreaFrac *
                                                         static_cast<double>(tileCount_));
    {
        // Only reorganize when the world has enough plates that "oversized" is a real
        // imbalance, not a necessity. With very few plates each one is huge by definition
        // (a degenerate state, e.g. just after a supercontinent merge or in a contrived
        // 2-plate case); breaking them there would fight the merge/deficit dynamics. Real
        // runs sit at ~7-14 plates, so this never gates a genuine runaway.
        uint32_t bigP = kUnowned; uint32_t bigA = 0;
        if (aliveN >= kMinPlatesForOversizedRift) {
            for (uint32_t p = 0; p < plates_.size(); ++p) {
                if (!plates_[p].alive) continue;
                if (areas[p] <= maxPlateTiles) continue;
                if (areas[p] > bigA) { bigA = areas[p]; bigP = p; }
            }
        }
        if (bigP != kUnowned) {
            foundation::Pcg32 rng(deriveSeed(riftStream_,
                0x05AB1EAF00000000ULL ^ (static_cast<uint64_t>(step_) * 0x100000001B3ULL)));
            if (rng.nextDouble() < kOversizedRiftProb) {
                if (tryRift(bigP, static_cast<uint64_t>(step_) * 0xD1B54A32D192ED03ULL + 11u,
                            /*oversized=*/true)) {
                    // Recompute the split plate's area; a successful rift invalidated it.
                    if (bigP < plates_.size()) areas[bigP] = plateArea(bigP);
                }
            }
        }
    }

    // ---- Stranded-floor resurfacing (M-T2.7): coherent ridge nucleation. ----
    resurfaceStrandedRidges(static_cast<uint64_t>(step_));

    // ---- Rift: when alive < K, with rising probability, split a big plate. ----
    uint32_t alive = aliveCount();
    int target = cfg_.plateCount;
    if (static_cast<int>(alive) < target) {
        int deficit = target - static_cast<int>(alive);
        double prob = kRiftBaseProb + kRiftDeficitProb * static_cast<double>(deficit);
        foundation::Pcg32 rng(deriveSeed(riftStream_,
            0x9E37001F00000000ULL ^ (static_cast<uint64_t>(step_) * 0x100000001B3ULL)));
        if (rng.nextDouble() < prob) {
            double meanArea = aliveN ? static_cast<double>(areaSum) / aliveN : 0.0;
            uint32_t bestP = kUnowned; uint32_t bestA = 0;
            for (uint32_t p = 0; p < plates_.size(); ++p) {
                if (!plates_[p].alive) continue;
                if (static_cast<double>(areas[p]) < kRiftMinAreaFactor * meanArea) continue;
                bool prefer = areas[p] > bestA;
                if (prefer) { bestA = areas[p]; bestP = p; }
            }
            if (bestP != kUnowned) {
                tryRift(bestP, static_cast<uint64_t>(step_) * 0x9E3779B97F4A7C15ULL + 7u);
            }
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

    // World-cell area per plate (current owner map).
    std::vector<uint32_t> area(static_cast<size_t>(K), 0u);
    for (TileId t = 0; t < tileCount_; ++t) {
        uint8_t pid = owner_[t];
        if (pid < static_cast<uint8_t>(K)) area[pid]++;
    }

    // Compact plate ids ascending over alive plates that still own tiles. A plate
    // squeezed to zero area (subducted/eroded away while still flagged alive) is
    // excluded from the output and its tiles, if any leaked, fall through to 255.
    std::vector<int> remap(static_cast<size_t>(K), -1);
    int next = 0;
    for (int p = 0; p < K; ++p) {
        if (!plates_[static_cast<size_t>(p)].alive) continue;
        if (area[static_cast<size_t>(p)] == 0) continue;
        remap[static_cast<size_t>(p)] = next++;
    }

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
        // orogenyAge = Myr since the last orogenic stamp (kOrogenyNever if never).
        if (cell.orogenyMyr == kOrogenyNever) {
            h->orogenyAge[t] = kOrogenyNever;
        } else {
            int32_t oage = nowI - cell.orogenyMyr;
            if (oage < 0) oage = 0;
            h->orogenyAge[t] = oage;
        }
        h->orogenyIntensity[t] = cell.orogenyIntensity;
        h->volcanism[t] = cell.volcanism;
        h->boundaryType[t] = bndType_[t];
        h->boundarySide[t] = bndSide_[t];
        h->convergence[t] = bndConv_[t];
    }

    // Per-plate summary, area-weighted majority crust.
    h->plates.reserve(static_cast<size_t>(next));
    for (int p = 0; p < K; ++p) {
        if (remap[static_cast<size_t>(p)] < 0) continue;
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
