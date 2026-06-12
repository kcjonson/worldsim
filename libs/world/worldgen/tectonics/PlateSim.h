#pragma once

#include "worldgen/grid/SphereGrid.h"
#include "worldgen/tectonics/TectonicHistory.h"
#include "worldgen/tectonics/TectonicParams.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace worldgen::tectonics {

// Per-plate-local crust cell. Indexed by coarse TileId in the plate's BASELINE
// frame (the world frame at the moment the plate's rotation quaternion was last
// reset to identity; in M-T1 every plate's baseline is the sim start frame).
//
// Deviation from the plan sketch (which used u16 birth): birthMyr is int32
// because pre-aged ocean floor starts negative (born before t=0) so that initial
// seafloor age is non-uniform. orogenyMyr stays at kOrogenyNever in M-T1.
struct CrustCell {
    CrustType type{CrustType::None};
    int32_t   birthMyr{0};                 // can be negative for pre-aged ocean
    int32_t   orogenyMyr{kOrogenyNever};   // M-T2
    float     thicknessKm{0.0f};
    float     volcanism{0.0f};             // M-T2
};

// One simulated plate.
struct SimPlate {
    bool   alive{true};
    bool   isContinental{false};
    double rotation[4]{1, 0, 0, 0}; // cumulative quaternion (w,x,y,z), fixed op order
    Vec3d  eulerPole{0, 0, 1};      // unit vector
    double omegaRadPerMyr{0.0};     // signed angular speed about the pole
    std::vector<CrustCell> crust;   // sized coarseTileCount, baseline-frame raster
    // Occupied local cells in ascending TileId. Kept in sync with `crust`:
    // entries with type==None are skipped (lazy deletion; compacted each step).
    std::vector<TileId> occupied;
};

// Inputs to PlateSim construction. The stage builds these from a StageContext;
// tests can hand-build a small case.
struct PlateSimParams {
    uint32_t coarseN{kCoarseN};
    uint64_t seed{0};
    int      plateCount{12};       // K
    double   waterAmount{0.70};
    double   planetAge{4.5e9};     // years
    double   dtMyr{kDtMyr};
    double   historyMyr{0.0};      // 0 = derive from planetAge + kHistoryBaseMyr
    double   planetRadiusKm{kEarthRadiusKm};
};

// Optional hand-placed plate overrides for tests. When set, normal seeding /
// Dijkstra / continental painting / Euler assignment are skipped and these
// plates + ownership are installed verbatim.
struct PlateSimTestOverride {
    std::vector<SimPlate> plates;     // plates[i].crust already populated in baseline frame
    std::vector<uint8_t>  owner;      // length coarseTileCount: initial plate id per world cell
};

// Deterministic, single-threaded time-stepped tectonic core. See step() for the
// fixed operation order (the determinism contract). All transcendentals go
// through det_math; RNG is Pcg32 with derived stream seeds.
class PlateSim {
  public:
    explicit PlateSim(const PlateSimParams& params,
                      const PlateSimTestOverride* override = nullptr);

    // Number of dt-sized steps for the configured history.
    int stepCount() const { return stepCount_; }
    double historyMyr() const { return historyMyr_; }
    double nowMyr() const { return nowMyr_; }
    uint32_t coarseTileCount() const { return tileCount_; }
    const SphereGrid& grid() const { return *grid_; }

    // Advance one step. Optional callbacks fire once per step (cancel first, then
    // progress 0..1). Throws nothing; cancel is signalled by the callback itself.
    using CancelFn   = std::function<void()>;          // may throw to cancel
    using ProgressFn = std::function<void(float)>;     // 0..1 fraction
    void step(const CancelFn& cancel = nullptr,
              const ProgressFn& progress = nullptr);

    // Run all remaining steps then finalize. Returns the output product.
    std::shared_ptr<TectonicHistory> run(const CancelFn& cancel = nullptr,
                                         const ProgressFn& progress = nullptr);

    // Build the output product from current state (final rasterize + boundary scan).
    std::shared_ptr<TectonicHistory> finalize();

    // --- introspection for tests / stats ---
    const std::vector<SimPlate>& plates() const { return plates_; }
    const std::vector<uint8_t>&  owner()  const { return owner_; }
    // Resolved crust at each world cell after the last rasterize (for transects).
    const std::vector<CrustCell>& resolvedCrust() const { return resolved_; }
    const std::vector<uint8_t>& boundaryType() const { return bndType_; }
    uint32_t continentalCellCount() const;
    uint64_t continentalContinentalOverlaps() const { return ccOverlaps_; }

  private:
    // --- init helpers ---
    void seedAndGrowPlates(uint64_t seed);
    void paintContinents(uint64_t seed);
    void assignEulerPoles(uint64_t seed);
    void initPlateRasters();

    // --- per-step pipeline ---
    void advanceRotations();
    void forwardRasterize();
    void resolveOwnership();
    void applyEraseList();
    void gapFill();
    void boundaryScan();

    // quaternion helpers (doubles, fixed op order)
    static void quatFromAxisAngle(const Vec3d& axis, double angle, double q[4]);
    static void quatMul(const double a[4], const double b[4], double out[4]);
    static Vec3d quatRotate(const double q[4], const Vec3d& v);

    const Vec3d& baselineCenter(TileId cell) const { return centers_[cell]; }

    std::shared_ptr<const SphereGrid> grid_;
    std::vector<Vec3d> centers_; // cached tile centers (baseline frame, constant)
    PlateSimParams cfg_;
    uint32_t tileCount_{};
    int      stepCount_{};
    double   dtMyr_{};
    double   historyMyr_{};
    double   nowMyr_{0.0};
    int      step_{0};

    std::vector<SimPlate> plates_;

    // World-cell ownership + resolved crust after rasterize.
    std::vector<uint8_t>  owner_;      // plate id, 255 = unowned
    std::vector<CrustCell> resolved_;  // resolved crust per world cell

    // Forward-rasterize scratch: per-world-cell candidate lists, reused each step.
    struct Candidate { uint32_t plate; TileId localCell; };
    std::vector<Candidate>          candPool_;   // flat pool of all candidates
    std::vector<uint32_t>           candHead_;   // per-cell head index into linked list
    std::vector<uint32_t>           candNext_;   // intrusive next per candidate

    // Subduction erase list (plate, localCell) collected in scan order.
    std::vector<Candidate> eraseList_;

    // Boundary scratch (per world cell).
    std::vector<uint8_t> bndType_;
    std::vector<uint8_t> bndSide_;
    std::vector<float>   bndConv_;

    // Transient: continental tiles from paintContinents, consumed by initPlateRasters.
    std::vector<bool> continentalMask_;

    uint64_t ccOverlaps_{0}; // continental-continental overlap count (stats)
};

} // namespace worldgen::tectonics
