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
    int32_t   orogenyMyr{kOrogenyNever};   // M-T2: time of last orogenic stamp
    float     thicknessKm{0.0f};
    float     orogenyIntensity{0.0f};      // M-T2: 0..1 accumulator, mountain-belt amplitude
    float     volcanism{0.0f};             // M-T2: 0..1 accumulator, arc + hotspot magmatism
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

    // --- M-T2 event introspection / stats ---
    uint32_t aliveCount() const;
    uint32_t mergeCount() const { return mergeCount_; }
    uint32_t riftCount() const { return riftCount_; }
    uint32_t accretionCount() const { return accretionCount_; }

  private:
    // --- init helpers ---
    void seedAndGrowPlates(uint64_t seed);
    void paintContinents(uint64_t seed);
    void assignEulerPoles(uint64_t seed);
    void initPlateRasters();

    // --- per-step pipeline ---
    void advanceRotations();
    void evolvePoles();        // M-T2: slow pole/speed drift on a schedule
    void slabPull();           // M-T2.6: scale omega by mean subducting-floor age
    void forwardRasterize();
    void resolveOwnership();
    void applyEraseList();
    void gapFill();
    void boundaryScan();
    void collisionProcessing();  // M-T2: CC thicken+orogeny, CO/OO arc volcanism
    void terraneAccretion();     // M-T2: microcontinent transfer at trenches
    void erosionProxy();         // M-T2: continental thickness relaxation
    void hotspots();             // M-T2: plume volcanism
    void plateEvents();          // M-T2: merge + rift
    void rebalanceMomentum();    // area-weighted net-rotation cancel (init + post-event)

    // M-T2 event helpers
    void initHotspots(uint64_t seed);
    void mergePlates(uint32_t keep, uint32_t donor); // donor -> keep in keep's frame
    void pruneStaleCrust(uint32_t pid);              // drop subducted oceanic raster crust
    // split plate pid; returns true on success. oversized=true forces the oceanic
    // young/ridge-biased great-circle cut (a plate-motion reorganization of a runaway
    // plate) even on a continental plate, and ignores the min-area-factor gate.
    bool tryRift(uint32_t pid, uint64_t stepSalt, bool oversized = false);
    uint32_t allocPlateId();                          // reuse a dead id or grow the vector
    // World cell -> a plate's local baseline cell (inverse-rotate + nearest).
    TileId worldToLocal(uint32_t pid, TileId worldCell) const;
    // Per-plate occupied-tile count from its raster (for area-based decisions).
    uint32_t plateArea(uint32_t pid) const;

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

    // --- M-T2 state ---
    // Previous-step ownership, used for the CC-tie stickiness rule (prev owner of a
    // contested continental cell wins, so collision boundaries stay coherent while
    // the per-pair collision score builds). Updated at the end of resolveOwnership.
    std::vector<uint8_t> prevOwner_;

    // Per-plate-pair collision score (key = packed (min<<8 | max) plate ids).
    // Deterministic ascending iteration via a sorted-on-use vector of (key, score).
    std::vector<std::pair<uint32_t, double>> collisionScore_;

    // Hotspots: fixed plume unit vectors in the WORLD frame (plumes are mantle-
    // anchored; plates drift over them, leaving chains).
    std::vector<Vec3d> hotspots_;

    // Per-plate next pole-evolution time (Myr). Staggered so plates don't all
    // re-pole on the same step.
    std::vector<double> nextPoleEvolveMyr_;

    // M-T2.6 slab pull: per-plate applied speed multiplier, relaxed each step toward a
    // target derived from the mean age of the plate's subducting oceanic floor. Old
    // cold slabs pull hardest, so the plate accelerates trenchward and recycles old
    // basins. Stored as state so the relaxation is smooth across steps.
    std::vector<double> slabPull_;

    uint64_t poleEvolveStream_{0}; // base stream for per-plate pole re-draws
    uint64_t riftStream_{0};       // base stream for rift decisions/paths
    uint64_t marginStream_{0};     // base stream for margin-progradation rolls (M-T2.5)
    uint64_t matureStream_{0};     // base stream for island-arc maturation rolls (M-T2.5)

    // --- M-T2.5 continental-area feedback controller ---
    // Resolved continental cell count from the last resolveOwnership (set there,
    // read by collisionProcessing to compute the production-rate factor). The target
    // is (1-water) * kCrustAreaFactor * tileCount, computed once.
    uint32_t resolvedContinentalCount_{0};
    double   continentalTarget_{0.0};
    // Smooth bounded scale on the arc volcanism accumulation rate this step. >1 when
    // continental area is below target (produce more juvenile crust), <1 when above.
    double   areaControllerFactor_{1.0};

    uint32_t mergeCount_{0};
    uint32_t riftCount_{0};
    uint32_t accretionCount_{0};

    // Scratch reused across steps to avoid per-step allocation.
    std::vector<int32_t> ringScratch_; // BFS ring distance buffer (per world cell)
    std::vector<TileId>  bfsScratch_;   // BFS frontier

    // Terrane-accretion flood-fill scratch, reused across stride-calls (M-T2.5: the
    // pass now does real transfer work each call, so per-call heap churn matters).
    std::vector<int32_t> terraneComp_;  // per-world-cell component id (-1 = unvisited)
    std::vector<TileId>  terraneStack_; // BFS frontier
    std::vector<TileId>  terraneCells_; // current component's cells

    // Per-plate collision-block factor [0,1] from CC contact (set in
    // collisionProcessing, applied in advanceRotations next step). Continental crust
    // is buoyant: a plate deep in continent-continent collision slows so continents
    // suture edge-to-edge instead of interpenetrating (conserves continental area).
    std::vector<float> collisionBlock_;
};

} // namespace worldgen::tectonics
