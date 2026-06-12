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

// Icosahedral hex grid: 20 icosahedron faces paired into 10 rhombi, each an
// n x n lattice of tile centers projected to the sphere. Total tiles = 10*n*n.
//
// Each rhombus is a pair of equilateral faces, so its (u,v) cell basis vectors
// have equal length at 60 degrees: the tile centers form a triangular lattice
// and a tile is the Voronoi cell of its center — a near-regular hexagon.
// Assignment is nearest-center via cube rounding in axial coords (hexRound),
// measured in the unskewed lattice metric; the fragment shader in planet-view
// mirrors the same math so picking matches rendering.
//
// TileId encoding (frozen): rhombus * n*n + j*n + i
//   rhombus in [0,10), i in [0,n), j in [0,n)
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
// No tile center sits on the 12 icosahedron vertices (centers are at
// half-integer lattice offsets), so there are no literal pentagon tiles;
// each vertex is a degree-5 Voronoi vertex with a 5-cell pinwheel of
// slightly pinched hexagons around it. Tiles in that pinwheel can have
// 5 distinct neighbors; neighbors() deduplicates them.
class SphereGrid {
  public:
    explicit SphereGrid(uint32_t newN);

    // Total tile count = 10 * n * n
    uint32_t tileCount() const { return 10u * n * n; }
    uint32_t subdivision() const { return n; }

    // Convert lat/lon (degrees) to the nearest tile.
    TileId fromLatLon(double latDeg, double lonDeg) const;

    // Convert a unit direction vector to the nearest tile.
    TileId fromUnitVector(Vec3d dir) const;

    // Map (rhombus, u, v) in [0,1]^2 to a unit-sphere direction using the same
    // icosahedral barycentric mapping used internally for tile placement.
    // planet-view uses this to build vertex positions that share icosahedron-edge
    // points across neighboring rhombi, eliminating seam artifacts.
    // Contract: rhombusPointOnSphere(r, (i+0.5)/n, (j+0.5)/n) == tileCenter(r*n*n + j*n + i)
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
    // Uses sqrt of the tile's spherical area as a proxy for width. Area is
    // computed from the (u,v) cell's 4 corners; the cell parallelogram is the
    // lattice fundamental domain, which has the same area as the hex Voronoi
    // cell, so this is exact-in-area for hexes too.
    float tileWidthMeters(TileId t, double planetRadiusMeters) const;

    // Fill out[] with up to 6 hex lattice neighbors in the fixed offset order
    // (+1,0)(-1,0)(0,+1)(0,-1)(+1,-1)(-1,+1), canonicalized across rhombus
    // edges, deduplicated, kInvalidTile-free. Returns actual neighbor count.
    // The order is load-bearing: WorldData::downhill indexes into it.
    // Interior tiles: 6. Tiles in an icosahedron-vertex pinwheel: 5.
    uint32_t neighbors(TileId t, std::array<TileId, 6>& out) const;

    // Map a possibly out-of-range (rhombus, i, j) lattice coordinate to its
    // canonical TileId by hopping across rhombus edges. Returns kInvalidTile
    // if the position is unmappable. planet-view uses this to bake texture
    // border texels that agree with CPU assignment across seams.
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
    // Edges: 0=u=0 (i=0), 1=u=1 (i=n-1), 2=v=0 (j=0), 3=v=1 (j=n-1)
    struct EdgeAdj {
        uint8_t neighborRhombus;
        uint8_t neighborEdge;
    };
    std::array<std::array<EdgeAdj, 4>, 10> edgeAdj;

    // Precomputed neighbor lists for the seam tiles (i or j on a rhombus edge),
    // where lattice offsets don't close cleanly and the live computation is
    // expensive. Built once in the constructor, sorted by tile for binary
    // search. Interior tiles never appear here; their neighbors are the 6 fixed
    // lattice offsets computed on the fly.
    struct SeamNeighbors {
        TileId tile;
        uint8_t count;
        std::array<TileId, 6> nbrs;
    };
    std::vector<SeamNeighbors> seamCache;

    // --- private helpers ---

    // Encode/decode TileId
    TileId encode(uint32_t rh, uint32_t i, uint32_t j) const {
        return rh * n * n + j * n + i;
    }
    void decode(TileId t, uint32_t& rh, uint32_t& i, uint32_t& j) const {
        rh = t / (n * n);
        uint32_t rem = t % (n * n);
        j = rem / n;
        i = rem % n;
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

    // Map an (rhombus,i,j) that may be out of range to canonical (rh,i,j).
    // Returns false if the position doesn't map to a valid tile.
    bool canonicalize(int rh, int i, int j,
                      uint32_t& outRh, uint32_t& outI, uint32_t& outJ) const;

    // Build edge adjacency table from the icosahedron structure.
    void buildEdgeAdj();

    // Boundary-sampled neighbor candidates for a tile whose hex crosses a
    // rhombus seam (where the two triangular lattices interlock and no integer
    // offset is correct). Returns up to 6 distinct tiles bordering this hex,
    // before the mutual-adjacency filter applied when building the seam cache.
    uint32_t seamNeighborCandidates(uint32_t rh, uint32_t i, uint32_t j,
                                    std::array<TileId, 6>& out) const;

    // Compute and store mutual neighbor lists for every seam tile (one-time).
    void buildSeamCache();
};

} // namespace worldgen
