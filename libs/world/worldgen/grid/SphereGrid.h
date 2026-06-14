#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace worldgen {

using TileId = uint32_t;
inline constexpr TileId kInvalidTile = 0xFFFFFFFFu;

// A simple 3D vector using doubles for deterministic geometry.
struct Vec3d {
    double x{}, y{}, z{};
};

// Icosahedral Goldberg grid: 20 icosahedron faces paired into 10 rhombi, each
// an n x n chart whose VERTICES are tile centers projected to the sphere. Tile
// centers sit at chart vertices (i/n, j/n), so the hex lattice closes cleanly
// across rhombus seams (chart vertices are shared exactly across edges — the
// same property that makes the render mesh seam-free). Exactly 12 pentagon
// tiles remain (the 12 icosahedron vertices, including both poles); every other
// tile is a hexagon. Total tiles = 10*n*n + 2.
//
// Each rhombus is a pair of equilateral faces, so its (u,v) basis vectors have
// equal length at 60 degrees: the chart vertices form a triangular lattice and
// each tile is the Voronoi cell of its center — a near-regular hexagon.
// Assignment is nearest-center via cube rounding in axial coords (hexRound),
// measured in the unskewed lattice metric; the fragment shader in planet-view
// mirrors the same math so picking matches rendering.
//
// Vertex ownership (one TileId per physical vertex):
//   Rhombus r owns vertices i in [1..n], j in [0..n-1]  (n*n vertices each).
//   This excludes the u=0 seam (i=0), the v=1 seam (j=n), and corner C=(n,n).
//   Those unowned vertices are the same physical points as owned vertices in a
//   neighbor chart and canonicalize to that owner.
//   North pole A=(0,0) of the 5 northern rhombi and south pole C=(n,n) of the
//   southern rhombi fall outside every owned range, so they get two special
//   TileIds: kNorthPole = 10*n*n and kSouthPole = 10*n*n + 1.
//
// TileId encoding (Goldberg, amended from the cell-centered contract):
//   owned vertex: rhombus * n*n + j*n + (i-1)
//     rhombus in [0,10), i in [1,n], j in [0,n)
//   north pole:   10*n*n
//   south pole:   10*n*n + 1
//   i = u-axis (A->B direction), j = v-axis (A->D direction)
//
// Rhombus corners in (u,v) space:
//   A=(0,0), B=(1,0), C=(1,1), D=(0,1)
// Triangle T1 (u+v <= 1): vertices A,B,D
// Triangle T2 (u+v >  1): vertices B,C,D — the fold/short diagonal is B-D.
//
// The 10 rhombi cover all 20 icosahedron faces exactly once.
// 5 northern rhombi (r=0..4): pair north-cap + mid faces.
// 5 southern rhombi (r=5..9): pair mid + south-cap faces.
//
// The 12 icosahedron-vertex tiles are pentagons with exactly 5 neighbors; all
// other tiles are hexagons with exactly 6.
class SphereGrid {
  public:
    explicit SphereGrid(uint32_t newN);

    // Special TileIds for the two pole vertices (outside every owned range).
    TileId northPole() const { return 10u * n * n; }
    TileId southPole() const { return 10u * n * n + 1u; }

    // Total tile count = 10 * n * n + 2 (the +2 are the poles).
    uint32_t tileCount() const { return 10u * n * n + 2u; }
    uint32_t subdivision() const { return n; }

    // Convert lat/lon (degrees) to the nearest tile.
    TileId fromLatLon(double latDeg, double lonDeg) const;

    // Convert a unit direction vector to the nearest tile.
    TileId fromUnitVector(Vec3d dir) const;

    // Hinted variant for hot loops with temporal coherence (e.g. rigidly rotating
    // plates): tries rhombus rhombusHint first and skips the 10-way sort when the
    // barycentric solve lands inside it, then updates rhombusHint to the resolved
    // rhombus so the next call (a nearby direction) starts warm.
    //
    // Determinism note: for a direction exactly on a rhombus seam, more than one
    // rhombus can satisfy the eps-tolerant solve, so the chosen tile near a seam
    // can differ from the unhinted fromUnitVector (which always tries rhombi in
    // dot-sorted order). The result is still fully deterministic for a fixed hint
    // sequence; callers that need run-to-run stability must drive the hint from a
    // deterministic iteration order (PlateSim does). Do not mix hinted and unhinted
    // lookups expecting identical seam assignments.
    TileId fromUnitVectorHinted(Vec3d dir, uint32_t& rhombusHint) const;

    // Map (rhombus, u, v) in [0,1]^2 to a unit-sphere direction using the same
    // icosahedral barycentric mapping used internally for tile placement.
    // planet-view uses this to build vertex positions that share icosahedron-edge
    // points across neighboring rhombi, eliminating seam artifacts.
    // Contract: rhombusPointOnSphere(r, i/n, j/n) == tileCenter(r*n*n + j*n + (i-1))
    // for owned vertices (i in [1..n], j in [0..n-1]).
    Vec3d rhombusPointOnSphere(uint32_t rhombus, double u, double v) const;

    // Return the unit vector pointing to the center of a tile.
    Vec3d tileCenter(TileId t) const;

    // Return lat (degrees, -90..90) and lon (degrees, -180..180) of a tile center.
    void latLonOf(TileId t, double& latDeg, double& lonDeg) const;

    // Result of locating a position within the hex grid.
    struct HexSample {
        TileId tile;         // nearest tile center (the containing hex)
        TileId neighbor;     // second-nearest center = the blend partner
        float  edgeDistance; // 0.5*(d2-d1) in lattice units; 0 on the Voronoi
                             // edge, ~0.5 at the cell center
    };

    // Locate a lat/lon in the hex grid: containing tile, second-nearest
    // neighbor (deterministic tie-break: smaller TileId), and distance to
    // the shared Voronoi edge in lattice units.
    HexSample locateHex(double latDeg, double lonDeg) const;

    // Approximate tile width in meters on a sphere of the given radius.
    // Uses sqrt of the tile's spherical area as a proxy for width. The vertex's
    // hex cell area is approximated by the quad (i-0.5,j-0.5)..(i+0.5,j+0.5)/n
    // clamped into the [0,1] chart range — the lattice fundamental domain, equal
    // in area to the hex Voronoi cell. Pole/pentagon tiles get a 5/6-area
    // approximation (one rhombus-quadrant of the cell is missing across the seam).
    float tileWidthMeters(TileId t, double planetRadiusMeters) const;

    // Fill out[] with the hex lattice neighbors in the fixed offset order
    // (+1,0)(-1,0)(0,+1)(0,-1)(+1,-1)(-1,+1), canonicalized across rhombus
    // edges, deduplicated, kInvalidTile-free. Returns actual neighbor count.
    // The order is load-bearing: WorldData::downhill indexes into it.
    // Hexagon tiles: 6. The 12 icosahedron-vertex pentagon tiles: 5.
    uint32_t neighbors(TileId t, std::array<TileId, 6>& out) const;

    // Map a unit direction to the rhombus and (u,v) in [0,1]^2 that contains it.
    // planet-view's detail-page scheduler uses this to turn screen-visible rays
    // into per-rhombus uv rects. (Same solve as fromUnitVector, minus rounding.)
    void locateRhombusUV(Vec3d dir, uint32_t& rhombus, double& u, double& v) const {
        dirToRhombusUV(dir, rhombus, u, v);
    }

    // Map a possibly out-of-range (rhombus, i, j) lattice VERTEX to its
    // canonical TileId by hopping across rhombus edges and resolving ownership.
    // Returns kInvalidTile if the position is unmappable. planet-view uses this
    // to bake texture border texels that agree with CPU assignment across seams.
    TileId canonicalTile(uint32_t rhombus, int i, int j) const;

  private:
    uint32_t n;

    // 12 icosahedron vertices (unit vectors, doubles for determinism)
    std::array<Vec3d, 12> verts;

    // 20 icosahedron faces, each as 3 vertex indices (CCW winding from outside)
    struct Face {
        uint8_t v[3];
    };
    std::array<Face, 20> faces;

    // Per-rhombus precomputed inverse matrices for direct barycentric solve
    // T1 matrix columns: A, B, D  (T1 region u+v <= 1)
    // T2 matrix columns: B, C, D  (T2 region u+v >  1)
    struct Mat3x3 { double m[9]; };
    std::array<Mat3x3, 10> rhombiInvT1;
    std::array<Mat3x3, 10> rhombiInvT2;

    // Per-rhombus center for fast candidate ordering
    std::array<Vec3d, 10> rhombiCenters;

    // Per-face center (normalized centroid) for fallback face search
    std::array<Vec3d, 20> faceCenters;

    // Rhombus definitions: each rhombus = 4 vertex indices (A,B,C,D)
    // and the two face indices composing it.
    struct Rhombus {
        uint8_t vA, vB, vC, vD; // icosahedron vertex indices
        uint8_t faceT1;          // face for T1 region (A,B,D)
        uint8_t faceT2;          // face for T2 region (B,C,D)
    };
    std::array<Rhombus, 10> rhombi;

    // Edge adjacency table: for each rhombus (10) and each edge (4),
    // which neighbor rhombus + edge. All 40 pairings share their edge
    // parameterization direction (asserted in buildEdgeAdj); a reversed
    // pairing would break the hex-neighbor offset closure across seams.
    // Edges: 0=u=0 (i=0), 1=u=1 (i=n), 2=v=0 (j=0), 3=v=1 (j=n)
    struct EdgeAdj {
        uint8_t neighborRhombus;
        uint8_t neighborEdge;
    };
    std::array<std::array<EdgeAdj, 4>, 10> edgeAdj;

    // Per-(rhombus, edge) integer affine transform that maps a vertex (i,j) in
    // this chart to the identical physical vertex (i',j') in the neighbor chart
    // across that edge: i' = a*i + b*j + ti, j' = c*i + d*j + tj. The 2x2 block
    // is a triangular-lattice automorphism (the two charts meet at a 60-degree
    // fold, so the seam is a rotation/reflection, not the identity). Solved in
    // the constructor from physical vertex correspondences. Used to canonicalize
    // out-of-range and unowned vertices across seams.
    struct EdgeXform {
        int a, b, c, d;   // 2x2 lattice automorphism
        int ti, tj;       // translation (scales with n)
    };
    std::array<std::array<EdgeXform, 4>, 10> edgeXform;

    // --- private helpers ---

    // Encode an OWNED vertex (i in [1..n], j in [0..n-1]) to its TileId.
    TileId encodeOwned(uint32_t rh, uint32_t i, uint32_t j) const {
        return rh * n * n + j * n + (i - 1u);
    }
    // Decode a non-pole TileId back to its owned (rh, i, j).
    void decodeOwned(TileId t, uint32_t& rh, uint32_t& i, uint32_t& j) const {
        rh = t / (n * n);
        uint32_t rem = t % (n * n);
        j = rem / n;
        i = rem % n + 1u;
    }

    // Map (u,v) in [0,1]^2 to unit sphere direction via rhombus r
    Vec3d uvToDir(uint32_t r, double u, double v) const;

    // Round fractional axial coords to the nearest hex center (cube rounding).
    // Ties broken by floor(x+0.5) round-half-up — planet.frag mirrors this
    // exactly so GPU cell assignment matches CPU.
    static void hexRound(double fq, double fr, int& outI, int& outJ);

    // Find which rhombus contains dir, return rhombus index and u,v coords.
    // Tries rhombi sorted by center dot product; uses precomputed T1/T2 inverse
    // matrices for a direct barycentric solve with no face lookup.
    void dirToRhombusUV(Vec3d dir, uint32_t& rh, double& u, double& v) const;

    // Map an (rhombus,i,j) VERTEX (i,j in [0..n], possibly out of range) to its
    // canonical owning rhombus and owned coordinates, or to a pole. Returns the
    // resolved TileId, or kInvalidTile if unmappable.
    TileId canonicalVertex(int rh, int i, int j) const;

    // Build edge adjacency table from the icosahedron structure.
    void buildEdgeAdj();

    // Solve the per-edge integer affine transforms (edgeXform) by matching
    // physical vertex positions across each shared seam. Depends on n.
    void buildEdgeXforms();
};

} // namespace worldgen
