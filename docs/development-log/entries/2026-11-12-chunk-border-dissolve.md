# 2026-11-12 - Chunk borders dissolve: no more slivers along the shared side

## Summary

Where two chunk squares of open ground are both meshed, the side they share is no longer a
constrained edge. It is dissolved: no constraint, no mesh edge, and the mesh triangulates across it
like any open ground. Before, the 64 m pieces of every side were fixed edges, and long thin
triangles bunched along them. The border band now measures the same as the interior.

This is WOR-809 (chunk-border slivers). Not closed: it closes when the branch merges.

## Details

In the order it was built, on `fix/WOR-809-chunk-border-slivers`:

- Point sites (`libs/geometry/arrangement/Arrangement.{h,cpp}`). Both `buildArrangement` overloads take a list of point sites as a last argument. A point strictly inside a segment splits it; an isolated point is a vertex with no edges. Zero-length segment inputs are still rejected.
- One ground token (`geometry::nav::kGroundToken` in `PatchableMesh.h`, and `NavMesh.{h,cpp}`). A cover no longer names open squares: all open-ground rings map to one entry, with parity counted per token in `Patch::toggle` and `toggledInto` (now a template taking `isBounds`), the face flood, `classifyCover`, `assignCovers` and validate. The two `cover()` readers in `NavigationSystem.cpp` skip the token. Removing or moving a bounds ring finds its triangles by centroid containment or nearness to the old outline, since a cover lookup can no longer find them.
- The shared rule (`NavMesh.{h,cpp}`). `sameSegment`, `BoundsSeams`, `findBoundsSeams(rings)` (partners and pin counts), the template `ringsArrangementInput` (rings to segments and point sites), and `boundsContainSample`, which moves the sample by (e, e^2) so a point on a shared side or corner is held by exactly one square. `buildNavMesh` and the test model both call the helper.
- Dissolve on the patch path (`PatchableMesh.{h,cpp}`). `RegisteredRing::partners` (one `EdgeSource` per edge) and `pins` (point to pair count); `Patch::findPartners` at the end of `begin`; `partnerAfter` and `pinAfter` answer for the state after the patch; `arrange` skips an edge whose partner is live and passes pins as point sites; `collectFixed` keeps hole vertices that are pins; `splice` writes partners and pin counts; `touchSegment` is shared with `closeS` for an edge that gets its constraint back. Removing, re-adding or moving a square with a partner passes from-scratch equality.
- Validate. Dissolved edges have no pieces; pins are vertices; partners and pins equal what `findBoundsSeams` gives over the live rings; live bounds interiors do not overlap; every live vertex is a pin or an end of a constrained edge.
- Pin lookup off the interior path. `PagedVector<std::uint8_t> vertexPinned`, set in `splice`; `collectFixed` calls `pinAfter` only when the patch changes pins or the vertex is flagged.
- Long bounds edges are cut from their lexicographically lower end point (`splitLongEdges`), so two neighbours cut a shared side at the same points whatever its length.
- `RingEdgeRef` was deleted; `RingId`, `kNoRing` and `EdgeSource` moved to `NavMesh.h`.

The nav system (`NavigationSystem.cpp`) needed no change beyond the token skip.

### Measurements

`BM_ChunkSeam`: two squares, 5,135 placed obstacles, RelWithDebInfo. Before is the bench commit's numbers; after was taken at 6 to 16% machine load.

| | Before, within 16 m of the shared side | After, within 16 m | Interior |
|---|---|---|---|
| Longest edge mean / p99 / max (m) | 4.43 / 22.1 / 64.0 | 3.71 / 4.53 / 4.72 | 3.71 / 4.53 / 4.72 |
| Aspect mean / p99 / max | 28.8 / 358 / 25,099 | 4.79 / 10.3 / 11.3 | 4.79 / 10.3 / 11.3 |
| Triangles with an edge over 32 m | 14 | 0 | 0 |
| locate mean (ns) | 120 | 102 | 96 to 100 |
| Route expansions, 40 m pairs, mean | 77 | 66 | 64 |
| Route time mean (us) | 55 | 44 | 43 to 44 |
| refreshWidths per cell (ms) | 0.91 to 0.94 | 0.82 | 0.82 to 0.84 |

| Row | Before | After |
|---|---|---|
| `BM_LargeBoundsTree squares_shared_side` (us) | 180 | 22.6 |
| Interior placed obstacle, add / remove (us), pins present | 31.3 / 29.6 | 30.9 / 28.4 |
| `BM_ChunkEnters open512_side` (us) | 58 (spec, another day) | 66.0 |
| `BM_ChunkEnters` water neighbour (ms) | 0.9 | 0.996 |
| `BM_ChunkEnters` tree neighbour (ms) | 2.5 | 2.651 |
| `BM_PlacedObstacleBytes` (B) | | 5,803 |

No before exists for `BM_PatchAcrossBounds shared512` (add 23.6, remove 19.7, footprint 70.6, wall add 37.3 us), `BM_ColdSettleNineChunks/0` (ground max 2.2 ms, job max 4.0 ms) or `BM_RealChunksGround/0` (ground max 4.16 ms). The ground budget counts input vertices, 32 per square plus water, and pins keep all 32 side points, so it is unchanged. Partners and pins add about nothing per placed obstacle; `sizeof(RegisteredRing)` is 248 B.

### Tests

New in `PatchableMesh.test.cpp`: `LoneSquareKeepsFourBoundarySides`, `WaterAcrossTheSharedSideStaysConstrained`, `ObstacleEdgeThroughAPinIsSplitThere`, `SharedSideMeshesLikeTheInterior`, `SquaresInAnyOrderMeshAlike`, `SquaresArriveAmongObstaclesMatchFreshBuild`, `OverlappingBoundsFailValidate`, `BoundsContainSampleHoldsSharedOutlineOnce`, `UnevenlyCutSharedSideDissolves`, `SquareReplacedInOnePatchMatchesFreshBuild`, `TwoSquaresLeaveBesideACornerPin`, `BoundsContainSampleHoldsASlantedSideOnce`, `SlantedSharedSideMatchesFreshBuild`, `ObstacleCornerOnAFourWayCorner`; `AdjacentSquaresShareOneGroundCover` and `ObstacleInsideASquareShowsInItsCover` for the token; `HangingRingCompletedByNeighbourBounds` extended; `SplitLongEdgesCutsOnTheEdge` and `expectBoundsSplitInvisible` updated. In the arrangement tests, 8 point-site tests and `Arrangement.PointSiteAtACrossingAddsNothing`. In `NavigationSystem.test.cpp`: `RouteThroughPostsAcrossABorderIsAsLongAsInsideAChunk`, `BorderInAPostFieldMeshesLikeTheInterior`, `WaterStretchOfABorderStaysConstrained`; `ArrivingChunkJoinsItsNeighbour` now asserts 8 constrained boundary pieces before and none between land faces after.

Results on the final head: geometry 413 and sim 1033, in RelWithDebInfo and in Debug. The other suites' results will be in the pull request.

### Not done

- The one-sided band. A mover's obstacle cells can reach a chunk side before the chunk across is meshed; the side is the constrained boundary and long triangles bunch there until the neighbour arrives.
- Borders through water stay constrained.
- Open-ground fans inside chunks with few obstacles are unchanged.
- Slanted outlines in the library, WOR-812: slanted chunk-style outlines with obstacles crossing at shallow angles can differ from a from-scratch build by a millimetre sliver. The game's axis-aligned squares are not affected.
- A chunk arriving beside a neighbour measured 66 us against 58 us recorded earlier on another day; unconfirmed.
- Not in the game yet: a capture of a chunk border with the nav overlay.

## Related Documentation

- [Nav mesh: one mesh, variable resolution](../../technical/nav-mesh-variable-resolution.md): section 1.1, 4.3 and 4.5, section 6, section 10 entry 115 (and entry 51, which it changes).
- [2026-11-12 - Nav correction](./2026-11-12-nav-correction.md).

## Next Steps

- Slanted-outline mismatch, WOR-812.
- Decide whether to mesh the chunk across when a mover's obstacle cell touches a chunk side, which would end the one-sided band.
- WOR-801 (Nav: cut the mesh cost of a placed obstacle from about 7.2 KB to about 1.25 KB) and WOR-808 (bench fixes after the geometry clean-up) stay open.
