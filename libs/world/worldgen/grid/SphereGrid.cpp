#include "worldgen/grid/SphereGrid.h"

#include <math/DeterministicMath.h>

#include <algorithm>
#include <cassert>
#include <cmath>    // std::sqrt only — permitted
#include <cstdlib>

namespace worldgen {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kPiOver180 = kPi / 180.0;
constexpr double k180OverPi = 180.0 / kPi;

double vecLength(Vec3d v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vec3d vecNorm(Vec3d v) {
    double len = vecLength(v);
    if (len < 1e-300) return {0.0, 0.0, 1.0};
    return {v.x / len, v.y / len, v.z / len};
}

double vecDot(Vec3d a, Vec3d b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3d vecCross(Vec3d a, Vec3d b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

// Deterministic atan2/asin — wrap foundation det_math
double detAtan2(double y, double x) {
    return foundation::det_math::atan2(y, x);
}

double detAsin(double x) {
    return foundation::det_math::asin(x);
}

double detSqrt(double x) {
    return foundation::det_math::sqrt(x);
}

double safeAsin(double x) {
    if (x >= 1.0) return kPi * 0.5;
    if (x <= -1.0) return -kPi * 0.5;
    return detAsin(x);
}

// Spherical triangle area via l'Huilier's theorem
double sphericalTriArea(Vec3d a, Vec3d b, Vec3d c) {
    Vec3d crossBC = vecCross(b, c);
    Vec3d crossAC = vecCross(a, c);
    Vec3d crossAB = vecCross(a, b);
    double cosA = vecDot(b, c);
    double cosB = vecDot(a, c);
    double cosC = vecDot(a, b);
    if (cosA > 1.0) cosA = 1.0; if (cosA < -1.0) cosA = -1.0;
    if (cosB > 1.0) cosB = 1.0; if (cosB < -1.0) cosB = -1.0;
    if (cosC > 1.0) cosC = 1.0; if (cosC < -1.0) cosC = -1.0;
    double sinA = vecLength(crossBC);
    double sinB = vecLength(crossAC);
    double sinC = vecLength(crossAB);
    double sideA = detAtan2(sinA, cosA);
    double sideB = detAtan2(sinB, cosB);
    double sideC = detAtan2(sinC, cosC);
    double s = (sideA + sideB + sideC) * 0.5;
    using foundation::det_math::sin;
    using foundation::det_math::cos;
    double cs = cos(s * 0.5);
    double csA = cos((s - sideA) * 0.5);
    double csB = cos((s - sideB) * 0.5);
    double csC = cos((s - sideC) * 0.5);
    if (cs == 0.0 || csA == 0.0 || csB == 0.0 || csC == 0.0) return 0.0;
    double t0 = sin(s * 0.5) / cs;
    double t1 = sin((s - sideA) * 0.5) / csA;
    double t2 = sin((s - sideB) * 0.5) / csB;
    double t3 = sin((s - sideC) * 0.5) / csC;
    double product = t0 * t1 * t2 * t3;
    if (product <= 0.0) return 0.0;
    double tanE4 = detSqrt(product);
    return 4.0 * detAtan2(tanE4, 1.0);
}

// 3x3 matrix inverse (row major)
bool mat3Invert(const double* m, double* inv) {
    double det = m[0] * (m[4]*m[8] - m[5]*m[7])
               - m[1] * (m[3]*m[8] - m[5]*m[6])
               + m[2] * (m[3]*m[7] - m[4]*m[6]);
    if (det == 0.0) return false;
    double id = 1.0 / det;
    inv[0] = (m[4]*m[8] - m[5]*m[7]) * id;
    inv[1] = (m[2]*m[7] - m[1]*m[8]) * id;
    inv[2] = (m[1]*m[5] - m[2]*m[4]) * id;
    inv[3] = (m[5]*m[6] - m[3]*m[8]) * id;
    inv[4] = (m[0]*m[8] - m[2]*m[6]) * id;
    inv[5] = (m[2]*m[3] - m[0]*m[5]) * id;
    inv[6] = (m[3]*m[7] - m[4]*m[6]) * id;
    inv[7] = (m[1]*m[6] - m[0]*m[7]) * id;
    inv[8] = (m[0]*m[4] - m[1]*m[3]) * id;
    return true;
}

} // namespace

// ============================================================================
// Construction
// ============================================================================

SphereGrid::SphereGrid(uint32_t newN) : n(newN) {
    assert(newN >= 1 && "subdivision must be >= 1");

    using foundation::det_math::sin;
    using foundation::det_math::cos;

    // Build 12 icosahedron vertices with explicit pole construction.
    // Ring vertices: z = ±1/sqrt(5), xy-radius = 2/sqrt(5)
    constexpr double kInvSqrt5  = 0.4472135954999579392818347337462;
    constexpr double k2InvSqrt5 = 0.8944271909999158785636694674925;

    verts[0]  = {0.0, 0.0,  1.0};   // north pole
    verts[11] = {0.0, 0.0, -1.0};   // south pole

    for (int r = 0; r < 5; ++r) {
        double lon = r * 72.0 * kPiOver180;
        verts[1 + r] = {k2InvSqrt5 * cos(lon), k2InvSqrt5 * sin(lon),  kInvSqrt5};
    }
    for (int r = 0; r < 5; ++r) {
        double lon = (36.0 + r * 72.0) * kPiOver180;
        verts[6 + r] = {k2InvSqrt5 * cos(lon), k2InvSqrt5 * sin(lon), -kInvSqrt5};
    }

    // 10 rhombi: 5 northern (A=north, B=upper[r], C=lower[r], D=upper[r+1])
    //            5 southern (A=upper[r+1], B=lower[r], C=south, D=lower[r+1])
    for (int r = 0; r < 5; ++r) {
        rhombi[r].vA = 0;
        rhombi[r].vB = static_cast<uint8_t>(1 + r);
        rhombi[r].vC = static_cast<uint8_t>(6 + r);
        rhombi[r].vD = static_cast<uint8_t>(1 + (r + 1) % 5);
    }
    for (int r = 0; r < 5; ++r) {
        rhombi[5 + r].vA = static_cast<uint8_t>(1 + (r + 1) % 5);
        rhombi[5 + r].vB = static_cast<uint8_t>(6 + r);
        rhombi[5 + r].vC = 11;
        rhombi[5 + r].vD = static_cast<uint8_t>(6 + (r + 1) % 5);
    }

    // 20 icosahedron faces (CCW from outside)
    for (int r = 0; r < 5; ++r) {  // north cap: (N, upper[r], upper[r+1])
        faces[r].v[0] = 0;
        faces[r].v[1] = static_cast<uint8_t>(1 + r);
        faces[r].v[2] = static_cast<uint8_t>(1 + (r + 1) % 5);
    }
    for (int r = 0; r < 5; ++r) {  // upper mid: (upper[r], lower[r], upper[r+1])
        faces[5 + r].v[0] = static_cast<uint8_t>(1 + r);
        faces[5 + r].v[1] = static_cast<uint8_t>(6 + r);
        faces[5 + r].v[2] = static_cast<uint8_t>(1 + (r + 1) % 5);
    }
    for (int r = 0; r < 5; ++r) {  // lower mid: (lower[r], lower[r+1], upper[r+1])
        faces[10 + r].v[0] = static_cast<uint8_t>(6 + r);
        faces[10 + r].v[1] = static_cast<uint8_t>(6 + (r + 1) % 5);
        faces[10 + r].v[2] = static_cast<uint8_t>(1 + (r + 1) % 5);
    }
    for (int r = 0; r < 5; ++r) {  // south cap: (S, lower[r+1], lower[r])
        faces[15 + r].v[0] = 11;
        faces[15 + r].v[1] = static_cast<uint8_t>(6 + (r + 1) % 5);
        faces[15 + r].v[2] = static_cast<uint8_t>(6 + r);
    }

    // Assign face indices to rhombi
    // Northern rhombus r: T1=(A,B,D)=(N,upper[r],upper[r+1]) = north-cap face r
    //                     T2=(B,C,D)=(upper[r],lower[r],upper[r+1]) = upper-mid face r
    for (int r = 0; r < 5; ++r) {
        rhombi[r].faceT1 = static_cast<uint8_t>(r);
        rhombi[r].faceT2 = static_cast<uint8_t>(5 + r);
    }
    // Southern rhombus r (5+r): T1=(A,B,D)=(upper[r+1],lower[r],lower[r+1]) = lower-mid face r
    //                            T2=(B,C,D)=(lower[r],S,lower[r+1]) = south-cap face r
    for (int r = 0; r < 5; ++r) {
        rhombi[5 + r].faceT1 = static_cast<uint8_t>(10 + r);
        rhombi[5 + r].faceT2 = static_cast<uint8_t>(15 + r);
    }

    // Precompute per-rhombus inverse matrices for T1 and T2.
    // T1 uses vertices (A, B, D), T2 uses vertices (B, C, D).
    // The matrix columns are the 3D vertex coords; we solve:
    //   dir_normalized = b0*VA + b1*VB + b2*VD  for T1
    //   dir_normalized = b0*VB + b1*VC + b2*VD  for T2
    for (int r = 0; r < 10; ++r) {
        const Rhombus& rh = rhombi[r];
        // T1: columns = A, B, D
        {
            Vec3d vA = verts[rh.vA], vB = verts[rh.vB], vD = verts[rh.vD];
            double mat[9] = {vA.x, vB.x, vD.x,
                             vA.y, vB.y, vD.y,
                             vA.z, vB.z, vD.z};
            bool ok = mat3Invert(mat, rhombiInvT1[r].m);
            assert(ok); (void)ok;
        }
        // T2: columns = B, C, D
        {
            Vec3d vB = verts[rh.vB], vC = verts[rh.vC], vD = verts[rh.vD];
            double mat[9] = {vB.x, vC.x, vD.x,
                             vB.y, vC.y, vD.y,
                             vB.z, vC.z, vD.z};
            bool ok = mat3Invert(mat, rhombiInvT2[r].m);
            assert(ok); (void)ok;
        }
        // Rhombus center (for fast search)
        {
            Vec3d vA = verts[rh.vA], vB = verts[rh.vB],
                  vC = verts[rh.vC], vD = verts[rh.vD];
            rhombiCenters[r] = vecNorm({(vA.x+vB.x+vC.x+vD.x)/4.0,
                                        (vA.y+vB.y+vC.y+vD.y)/4.0,
                                        (vA.z+vB.z+vC.z+vD.z)/4.0});
        }
    }

    // Face centers for fallback face search
    for (int f = 0; f < 20; ++f) {
        Vec3d v0 = verts[faces[f].v[0]];
        Vec3d v1 = verts[faces[f].v[1]];
        Vec3d v2 = verts[faces[f].v[2]];
        faceCenters[f] = vecNorm({(v0.x+v1.x+v2.x)/3.0,
                                  (v0.y+v1.y+v2.y)/3.0,
                                  (v0.z+v1.z+v2.z)/3.0});
    }

    buildEdgeAdj();

#ifndef NDEBUG
    double totalArea = 0.0;
    for (int f = 0; f < 20; ++f) {
        totalArea += sphericalTriArea(verts[faces[f].v[0]],
                                      verts[faces[f].v[1]],
                                      verts[faces[f].v[2]]);
    }
    assert(totalArea > 12.554 && totalArea < 12.580 &&
           "20 faces must tile the unit sphere");
#endif
}

// ============================================================================
// Edge adjacency
// ============================================================================

void SphereGrid::buildEdgeAdj() {
    // Edge vertex pairs:
    //   edge 0 (u=0, i=0):   {vA, vD}
    //   edge 1 (u=1, i=n-1): {vB, vC}
    //   edge 2 (v=0, j=0):   {vA, vB}
    //   edge 3 (v=1, j=n-1): {vD, vC}
    auto edgeVerts = [&](int rh, int edge, uint8_t& va, uint8_t& vb) {
        const Rhombus& r = rhombi[rh];
        switch (edge) {
            case 0: va = r.vA; vb = r.vD; break;
            case 1: va = r.vB; vb = r.vC; break;
            case 2: va = r.vA; vb = r.vB; break;
            case 3: va = r.vD; vb = r.vC; break;
            default: va = 0; vb = 0;
        }
    };

    for (int rh = 0; rh < 10; ++rh) {
        for (int edge = 0; edge < 4; ++edge) {
            uint8_t va{}, vb{};
            edgeVerts(rh, edge, va, vb);
            bool found = false;
            for (int nrh = 0; nrh < 10 && !found; ++nrh) {
                if (nrh == rh) continue;
                for (int nedge = 0; nedge < 4 && !found; ++nedge) {
                    uint8_t nva{}, nvb{};
                    edgeVerts(nrh, nedge, nva, nvb);
                    if ((va == nva && vb == nvb) || (va == nvb && vb == nva)) {
                        edgeAdj[rh][edge].neighborRhombus = static_cast<uint8_t>(nrh);
                        edgeAdj[rh][edge].neighborEdge    = static_cast<uint8_t>(nedge);
                        edgeAdj[rh][edge].reversed = (va == nvb);
                        found = true;
                    }
                }
            }
            assert(found && "rhombus edge must have a neighbor");
        }
    }
}

// ============================================================================
// Geometry: uvToDir
// ============================================================================

Vec3d SphereGrid::uvToDir(uint32_t r, double u, double v) const {
    const Rhombus& rh = rhombi[r];
    Vec3d vA = verts[rh.vA];
    Vec3d vB = verts[rh.vB];
    Vec3d vC = verts[rh.vC];
    Vec3d vD = verts[rh.vD];

    Vec3d p{};
    if (u + v <= 1.0) {
        // T1: A, B, D with bary (1-u-v, u, v)
        double wA = 1.0 - u - v;
        p = {wA*vA.x + u*vB.x + v*vD.x,
             wA*vA.y + u*vB.y + v*vD.y,
             wA*vA.z + u*vB.z + v*vD.z};
    } else {
        // T2: B, C, D; p = (1-v)*B + (u+v-1)*C + (1-u)*D
        double wB = 1.0 - v;
        double wC = u + v - 1.0;
        double wD = 1.0 - u;
        p = {wB*vB.x + wC*vC.x + wD*vD.x,
             wB*vB.y + wC*vC.y + wD*vD.y,
             wB*vB.z + wC*vC.z + wD*vD.z};
    }
    return vecNorm(p);
}

// ============================================================================
// dirToRhombusUV: direct per-rhombus barycentric solve
// ============================================================================

void SphereGrid::dirToRhombusUV(Vec3d dir, uint32_t& outRh,
                                  double& outU, double& outV) const {
    // Fast path: sort rhombi by dot with center, try top-3 first
    int order[10];
    double dots[10];
    for (int r = 0; r < 10; ++r) {
        order[r] = r;
        dots[r] = vecDot(dir, rhombiCenters[r]);
    }
    // Partial sort: bring the best 3 to front
    for (int i = 0; i < 3; ++i) {
        for (int j = i+1; j < 10; ++j) {
            if (dots[order[j]] > dots[order[i]]) {
                int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
            }
        }
    }

    constexpr double kEps = -1e-7;

    // Try rhombi in order of proximity
    for (int idx = 0; idx < 10; ++idx) {
        int r = order[idx];
        // Try T1: dir = bA*A + bB*B + bD*D
        {
            const double* inv = rhombiInvT1[r].m;
            double bA = inv[0]*dir.x + inv[1]*dir.y + inv[2]*dir.z;
            double bB = inv[3]*dir.x + inv[4]*dir.y + inv[5]*dir.z;
            double bD = inv[6]*dir.x + inv[7]*dir.y + inv[8]*dir.z;
            if (bA >= kEps && bB >= kEps && bD >= kEps) {
                double sum = bA + bB + bD;
                if (sum > 0.0) { bA /= sum; bB /= sum; bD /= sum; }
                // T1: u=bB, v=bD, check u+v <= 1 (should be, but clamp)
                double u = bB, v = bD;
                if (u < 0.0) u = 0.0; if (v < 0.0) v = 0.0;
                if (u > 1.0) u = 1.0; if (v > 1.0) v = 1.0;
                if (u + v > 1.0) { double s = u+v; u /= s; v /= s; }
                outRh = static_cast<uint32_t>(r); outU = u; outV = v;
                return;
            }
        }
        // Try T2: dir = bB*B + bC*C + bD*D
        {
            const double* inv = rhombiInvT2[r].m;
            double bB = inv[0]*dir.x + inv[1]*dir.y + inv[2]*dir.z;
            double bC = inv[3]*dir.x + inv[4]*dir.y + inv[5]*dir.z;
            double bD = inv[6]*dir.x + inv[7]*dir.y + inv[8]*dir.z;
            if (bB >= kEps && bC >= kEps && bD >= kEps) {
                double sum = bB + bC + bD;
                if (sum > 0.0) { bB /= sum; bC /= sum; bD /= sum; }
                // T2: uv = bB*(1,0)+bC*(1,1)+bD*(0,1) = (bB+bC, bC+bD)
                double u = bB + bC, v = bC + bD;
                if (u < 0.0) u = 0.0; if (v < 0.0) v = 0.0;
                if (u > 1.0) u = 1.0; if (v > 1.0) v = 1.0;
                if (u + v < 1.0) u = 1.0 - v; // keep in T2 region
                outRh = static_cast<uint32_t>(r); outU = u; outV = v;
                return;
            }
        }
    }

    // Fallback: best rhombus center match
    outRh = static_cast<uint32_t>(order[0]);
    outU = 0.5; outV = 0.5;
}

// ============================================================================
// Public API
// ============================================================================

TileId SphereGrid::fromUnitVector(Vec3d dir) const {
    uint32_t rh{};
    double u{}, v{};
    dirToRhombusUV(dir, rh, u, v);
    auto i = static_cast<uint32_t>(u * static_cast<double>(n));
    auto j = static_cast<uint32_t>(v * static_cast<double>(n));
    if (i >= n) i = n - 1;
    if (j >= n) j = n - 1;
    return encode(rh, i, j);
}

TileId SphereGrid::fromLatLon(double latDeg, double lonDeg) const {
    using foundation::det_math::cos;
    using foundation::det_math::sin;
    double lat = latDeg * kPiOver180;
    double lon = lonDeg * kPiOver180;
    double cosLat = cos(lat);
    Vec3d dir = {cosLat * cos(lon), cosLat * sin(lon), sin(lat)};
    return fromUnitVector(dir);
}

Vec3d SphereGrid::tileCenter(TileId t) const {
    uint32_t rh{}, i{}, j{};
    decode(t, rh, i, j);
    double u = (static_cast<double>(i) + 0.5) / static_cast<double>(n);
    double v = (static_cast<double>(j) + 0.5) / static_cast<double>(n);
    return uvToDir(rh, u, v);
}

void SphereGrid::latLonOf(TileId t, double& latDeg, double& lonDeg) const {
    Vec3d d = tileCenter(t);
    latDeg = safeAsin(d.z) * k180OverPi;
    lonDeg = detAtan2(d.y, d.x) * k180OverPi;
}

void SphereGrid::locate(double latDeg, double lonDeg,
                         TileId& tileOut, float& uOut, float& vOut) const {
    using foundation::det_math::cos;
    using foundation::det_math::sin;
    double lat = latDeg * kPiOver180;
    double lon = lonDeg * kPiOver180;
    double cosLat = cos(lat);
    Vec3d dir = {cosLat * cos(lon), cosLat * sin(lon), sin(lat)};

    uint32_t rh{};
    double u{}, v{};
    dirToRhombusUV(dir, rh, u, v);

    double tileU = u * static_cast<double>(n);
    double tileV = v * static_cast<double>(n);
    auto i = static_cast<uint32_t>(tileU);
    auto j = static_cast<uint32_t>(tileV);
    if (i >= n) i = n - 1;
    if (j >= n) j = n - 1;
    tileOut = encode(rh, i, j);
    uOut = static_cast<float>(tileU - static_cast<double>(i));
    vOut = static_cast<float>(tileV - static_cast<double>(j));
    if (uOut < 0.0f) uOut = 0.0f; if (uOut >= 1.0f) uOut = 0.9999f;
    if (vOut < 0.0f) vOut = 0.0f; if (vOut >= 1.0f) vOut = 0.9999f;
}

float SphereGrid::tileWidthMeters(TileId t, double planetRadiusMeters) const {
    uint32_t rh{}, i{}, j{};
    decode(t, rh, i, j);
    double u0 = static_cast<double>(i) / static_cast<double>(n);
    double v0 = static_cast<double>(j) / static_cast<double>(n);
    double u1 = static_cast<double>(i + 1) / static_cast<double>(n);
    double v1 = static_cast<double>(j + 1) / static_cast<double>(n);
    Vec3d c00 = uvToDir(rh, u0, v0);
    Vec3d c10 = uvToDir(rh, u1, v0);
    Vec3d c01 = uvToDir(rh, u0, v1);
    Vec3d c11 = uvToDir(rh, u1, v1);
    double area = sphericalTriArea(c00, c10, c11) + sphericalTriArea(c00, c11, c01);
    return static_cast<float>(detSqrt(area) * planetRadiusMeters);
}

// ============================================================================
// canonicalize: map out-of-range (rh,i,j) to valid tile
// ============================================================================

bool SphereGrid::canonicalize(int rh, int i, int j,
                               uint32_t& outRh, uint32_t& outI, uint32_t& outJ) const {
    int in = static_cast<int>(n);

    for (int iter = 0; iter < 4; ++iter) {
        if (i >= 0 && i < in && j >= 0 && j < in) {
            outRh = static_cast<uint32_t>(rh);
            outI  = static_cast<uint32_t>(i);
            outJ  = static_cast<uint32_t>(j);
            return true;
        }

        // Select the first out-of-range edge and hop
        if (i < 0) {
            const EdgeAdj& adj = edgeAdj[rh][0]; // u=0 edge, coord=j
            int coord = adj.reversed ? (in - 1 - j) : j;
            if (coord < 0) coord = 0;
            if (coord >= in) coord = in - 1;
            rh = adj.neighborRhombus;
            switch (adj.neighborEdge) {
                case 0: i = 0;      j = coord; break;
                case 1: i = in - 1; j = coord; break;
                case 2: i = coord;  j = 0;     break;
                case 3: i = coord;  j = in - 1; break;
                default: return false;
            }
        } else if (i >= in) {
            const EdgeAdj& adj = edgeAdj[rh][1]; // u=1 edge
            int coord = adj.reversed ? (in - 1 - j) : j;
            if (coord < 0) coord = 0;
            if (coord >= in) coord = in - 1;
            rh = adj.neighborRhombus;
            switch (adj.neighborEdge) {
                case 0: i = 0;      j = coord; break;
                case 1: i = in - 1; j = coord; break;
                case 2: i = coord;  j = 0;     break;
                case 3: i = coord;  j = in - 1; break;
                default: return false;
            }
        } else if (j < 0) {
            const EdgeAdj& adj = edgeAdj[rh][2]; // v=0 edge, coord=i
            int coord = adj.reversed ? (in - 1 - i) : i;
            if (coord < 0) coord = 0;
            if (coord >= in) coord = in - 1;
            rh = adj.neighborRhombus;
            switch (adj.neighborEdge) {
                case 0: i = 0;      j = coord; break;
                case 1: i = in - 1; j = coord; break;
                case 2: i = coord;  j = 0;     break;
                case 3: i = coord;  j = in - 1; break;
                default: return false;
            }
        } else { // j >= in
            const EdgeAdj& adj = edgeAdj[rh][3]; // v=1 edge
            int coord = adj.reversed ? (in - 1 - i) : i;
            if (coord < 0) coord = 0;
            if (coord >= in) coord = in - 1;
            rh = adj.neighborRhombus;
            switch (adj.neighborEdge) {
                case 0: i = 0;      j = coord; break;
                case 1: i = in - 1; j = coord; break;
                case 2: i = coord;  j = 0;     break;
                case 3: i = coord;  j = in - 1; break;
                default: return false;
            }
        }
    }
    return false;
}

// ============================================================================
// neighbors
// ============================================================================

uint32_t SphereGrid::neighbors(TileId t, std::array<TileId, 8>& out) const {
    uint32_t rh{}, i{}, j{};
    decode(t, rh, i, j);
    int si = static_cast<int>(i);
    int sj = static_cast<int>(j);

    static const int kDi[8] = {-1, 1, 0, 0, -1, -1, 1, 1};
    static const int kDj[8] = { 0, 0,-1, 1, -1,  1,-1, 1};

    uint32_t count = 0;
    for (int k = 0; k < 8; ++k) {
        uint32_t nrh{}, ni{}, nj{};
        if (canonicalize(static_cast<int>(rh), si + kDi[k], sj + kDj[k],
                         nrh, ni, nj)) {
            TileId cand = encode(nrh, ni, nj);
            if (cand == t) continue;
            bool dup = false;
            for (uint32_t q = 0; q < count; ++q) {
                if (out[q] == cand) { dup = true; break; }
            }
            if (!dup && count < 8) out[count++] = cand;
        }
    }
    return count;
}

// ============================================================================
// rhombusPointOnSphere — public forward mapping for planet-view
// ============================================================================

Vec3d SphereGrid::rhombusPointOnSphere(uint32_t r, double u, double v) const {
    return uvToDir(r, u, v);
}

} // namespace worldgen
