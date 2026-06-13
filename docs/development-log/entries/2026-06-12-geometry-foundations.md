# 2026-06-12 - Geometry Foundations (libs/geometry)

## Summary

Epic A of the Building & Construction system: a new pure-math library, `libs/geometry`, built entirely in-house per the architecture doc's D1-D3. Integer-millimeter coordinates, exact predicates, polygon constraint primitives, a planar arrangement with half-edge face extraction, ring booleans for foundation Add/Subtract, and wall band offsetting with junction trimming. 157 unit tests, the densest suite in the codebase, with shared-edge boolean cases and adversarial oracle tests enumerated first as the spec required. Depends only on `foundation`; no new third-party libraries (Clipper2, earcut.hpp, CGAL, and CavalierContours recorded as considered-and-rejected in library-decisions.md).

## Details

**Modules (headers/cpp side-by-side under `libs/geometry/`):**
- `core/` — `Vec2i64` (int64 mm) with meter quantization, and a minimal portable `Int128` (native `__int128` on GCC/Clang, `_mul128` two-limb arithmetic on MSVC x64, plus an exact 256-bit `compareSquareToProduct` for rational-magnitude comparisons).
- `predicates/` — exact orientation, segment intersection classification (Disjoint / ProperCrossing / EndpointTouch / CollinearOverlap, with the overlap subsegment reported), point-in-polygon with OnBoundary, and exact point-to-segment distance threshold tests. The single deliberate inexactness: a proper crossing's reported point rounds to the nearest millimeter (the Clipper2-style robustness model).
- `polygon/` — signed area (128-bit accumulation), winding, simplicity, and the constraint primitives the editor validates against: min interior angle, min vertex spacing, edge-to-edge clearance, each returning measured values for UI violation messages.
- `arrangement/` — planar subdivision via split-to-fixpoint (rounded crossing points re-fed through the exact pipeline until stable; coincident edges dedup into single edges carrying merged provenance) and half-edge face extraction with an exact no-float angular comparator, signed areas, exact-validated representative interior points, and adjacency queries. This is the shared core that room detection (D6) and booleans (D2) both ride.
- `boolean/` — `unionRings` / `subtractRings` / `ringsInteriorOverlap` by arrangement insert + face classification + boundary walk. Reject-don't-repair: pinch vertices, holes, splits, consumed inputs, and disjoint inputs all return typed statuses; success is always a single simple CCW hole-free ring, simplified at 1 mm and re-validated.
- `offset/` — per-segment wall bands, junction resolution (miter wedge within the limit, square bevel beyond, star-polygon fans for degree 3+, mixed thicknesses), and trim distances such that trimmed bands + junction polygons tile with no overlap and no gap (shared edges coincide by exact integer equality because both consume the same rounded corner points). Includes the post-op `simplifyRing` pass.

**Technical decisions:**
- Snap-rounding in the arrangement: a rounded crossing point can land off both exact segment lines; both contributors split at an order-independent canonical rounded point clamped onto each segment. Found by the adversarial review pass (it was the architecture doc's risk 3) and locked in with random-batch cleanliness tests.
- Determinism: no unordered containers anywhere in the library; angular sorting is integer-exact (quadrant + cross sign); shuffled-insertion-order tests assert identical canonical output.
- `squaredDistanceToSegment` returns nullopt when the true distance is rational (interior foot); exact threshold comparisons cross-multiply in 256-bit instead.

**Process:** built by parallel implementation agents (core/predicates first, then arrangement and offsetting concurrently in isolated worktrees, then booleans on top), followed by an adversarial review agent that brute-forced the predicates against small-domain oracles, audited the MSVC two-limb carry chains, and verified `angleLess` is a strict weak ordering.

## Related Documentation

- [Building & Construction Architecture](../../technical/building-construction-architecture.md) — D1-D3 (the spec for this library), D6
- [Building & Construction design spec](../../design/game-systems/world/building-construction.md)
- [Library Decisions](../../technical/library-decisions.md) — rejected geometry libraries recorded
- [Pathfinding Architecture](../../technical/pathfinding-architecture.md) — consumes the band polygons as nav obstacles
- [Vision Architecture](../../technical/vision-architecture.md) — consumes the same segments as occluders

## Next Steps

- Epic C (Foundations end-to-end): ConstructionWorld, FoundationTool + SnapEngine + ConstructionValidator, blueprint lifecycle, construction goals, progressive rendering, selection/panels, `assets/config/construction/`.
- Vision System and Navigation P1-P3 epics can start in parallel; they consume this library through the GeometryIndex and the obstacle publication contract.
