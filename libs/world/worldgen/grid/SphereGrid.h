#pragma once

#include <array>
#include <cstdint>

namespace worldgen {

using TileId = uint32_t;
inline constexpr TileId kInvalidTile = 0xFFFFFFFFu;

// A simple 3D vector using doubles for deterministic geometry.
struct Vec3d {
    double x{}, y{}, z{};
};

// Icosahedral rhombus grid: 20 icosahedron faces paired into 10 rhombi,
// each an n x n quad grid projected to the sphere. Total tiles = 10*n*n.
//
// TileId encoding: rhombus * n*n + j*n + i
//   rhombus in [0,10), i in [0,n), j in [0,n)
//   i = u-axis (A->B direction), j = v-axis (A->D direction)
//
// Rhombus corners in (u,v) space:
//   A=(0,0), B=(1,0), C=(1,1), D=(0,1)
// Triangle T1 (u+v <= 1): vertices A,B,D
// Triangle T2 (u+v >  1): vertices B,C,D
//
// The 10 rhombi cover all 20 icosahedron faces exactly once.
// 5 northern rhombi (r=0..4): pair north-cap + mid faces.
// 5 southern rhombi (r=5..9): pair mid + south-cap faces.
//
// The 12 icosahedron vertex tiles (corners shared across rhombi) have 5
// distinct neighbors rather than 8; neighbors() deduplicates them.
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

    // Return the unit vector pointing to the center of a tile.
    Vec3d tileCenter(TileId t) const;

    // Return lat (degrees, -90..90) and lon (degrees, -180..180) of a tile center.
    void latLonOf(TileId t, double& latDeg, double& lonDeg) const;

    // Locate a lat/lon within a tile, returning the containing TileId
    // and fractional (u,v) within that tile in [0,1).
    void locate(double latDeg, double lonDeg, TileId& tileOut,
                float& uOut, float& vOut) const;

    // Approximate tile width in meters on a sphere of the given radius.
    // Uses sqrt of the tile's spherical area as a proxy for width.
    // Area computed from 4 tile corner points via two spherical triangles.
    float tileWidthMeters(TileId t, double planetRadiusMeters) const;

    // Fill out[] with up to 8 neighbors (4 edge + up to 4 diagonal),
    // deduplicated, kInvalidTile-free. Returns actual neighbor count.
    // Interior tiles: 8. Edge tiles: 5 or 6. The 12 icosahedron vertex tiles: 5.
    uint32_t neighbors(TileId t, std::array<TileId, 8>& out) const;

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
    // which neighbor rhombus + edge + whether the coord is reversed.
    // Edges: 0=u=0 (i=0), 1=u=1 (i=n-1), 2=v=0 (j=0), 3=v=1 (j=n-1)
    struct EdgeAdj {
        uint8_t neighborRhombus;
        uint8_t neighborEdge;
        bool    reversed;
    };
    std::array<std::array<EdgeAdj, 4>, 10> edgeAdj;

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
};

} // namespace worldgen
