#include "worldgen/grid/SphereGrid.h"

#include <math/DeterministicMath.h>

#include <algorithm>
#include <cassert>
#include <cmath>    // std::sqrt/floor/abs only — exact IEEE ops, permitted
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
    buildEdgeXforms();

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
    //   edge 1 (u=1, i=n):   {vB, vC}
    //   edge 2 (v=0, j=0):   {vA, vB}
    //   edge 3 (v=1, j=n):   {vD, vC}
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
                        // The hex-neighbor offset set is only closed across
                        // seams when both sides parameterize the shared edge
                        // in the same direction (the first endpoint of edge
                        // maps to the first endpoint of nedge). True for this
                        // icosahedron construction; the vertex edge-hop in
                        // canonicalVertex() relies on it.
                        assert(va == nva && "rhombus edge pairing must not be reversed");
                        edgeAdj[rh][edge].neighborRhombus = static_cast<uint8_t>(nrh);
                        edgeAdj[rh][edge].neighborEdge    = static_cast<uint8_t>(nedge);
                        found = true;
                    }
                }
            }
            assert(found && "rhombus edge must have a neighbor");
        }
    }
}

void SphereGrid::buildEdgeXforms() {
    int in = static_cast<int>(n);

    // Endpoint chart-coords of an edge: (e0 -> e1) along the edge.
    // Edges: 0=u=0 (i=0): A(0,0)->D(0,n); 1=u=n (i=n): B(n,0)->C(n,n);
    //        2=v=0 (j=0): A(0,0)->B(n,0); 3=v=n (j=n): D(0,n)->C(n,n).
    // Order (vA,vD),(vB,vC),(vA,vB),(vD,vC) matches buildEdgeAdj's edgeVerts, so
    // the non-reversed assertion guarantees e0 maps to the neighbor edge's E0.
    auto endpoints = [&](int edge, int& e0i, int& e0j, int& e1i, int& e1j) {
        switch (edge) {
            case 0: e0i = 0;  e0j = 0;  e1i = 0;  e1j = in; break; // A->D
            case 1: e0i = in; e0j = 0;  e1i = in; e1j = in; break; // B->C
            case 2: e0i = 0;  e0j = 0;  e1i = in; e1j = 0;  break; // A->B
            case 3: e0i = 0;  e0j = in; e1i = in; e1j = in; break; // D->C
            default: e0i = e0j = e1i = e1j = 0;
        }
    };

    // The fold across a shared edge flattens one triangle onto the other: in
    // lattice coordinates it is the reflection that fixes the edge line and flips
    // the perpendicular (det = -1). We build it from the two corner
    // correspondences e0->E0, e1->E1: the edge vector d=e1-e0 maps to D=E1-E0
    // (same length, a lattice rotation/reflection), and the perpendicular folds
    // to the neighbor's inward side. We enumerate the 12 lattice symmetries
    // (det = ±1, entries in [-1,1] composed appropriately) and pick the unique
    // det = -1 map that sends d->D and lands the edge-adjacent interior vertex
    // (one step inward in chart rh) onto a valid neighbor interior vertex.
    for (int rh = 0; rh < 10; ++rh) {
        for (int edge = 0; edge < 4; ++edge) {
            int nrh = edgeAdj[rh][edge].neighborRhombus;
            int nedge = edgeAdj[rh][edge].neighborEdge;
            EdgeXform& x = edgeXform[rh][edge];

            int e0i, e0j, e1i, e1j;       // this chart edge endpoints
            endpoints(edge, e0i, e0j, e1i, e1j);
            int E0i, E0j, E1i, E1j;       // neighbor chart edge endpoints
            endpoints(nedge, E0i, E0j, E1i, E1j);

            int di = e1i - e0i, dj = e1j - e0j;   // edge vector, chart rh
            int Di = E1i - E0i, Dj = E1j - E0j;   // edge vector, neighbor

            // Inward step (one cell perpendicular to the edge, toward interior).
            int ini, inj;   // chart rh inward direction
            switch (edge) {
                case 0: ini =  1; inj = 0; break; // from i=0 inward: +i
                case 1: ini = -1; inj = 0; break; // from i=n inward: -i
                case 2: ini =  0; inj = 1; break; // from j=0 inward: +j
                case 3: ini =  0; inj = -1; break; // from j=n inward: -j
            }
            int Nini, Ninj; // neighbor inward direction
            switch (nedge) {
                case 0: Nini =  1; Ninj = 0; break;
                case 1: Nini = -1; Ninj = 0; break;
                case 2: Nini =  0; Ninj = 1; break;
                case 3: Nini =  0; Ninj = -1; break;
            }

            // Solve 2x2 integer M with M*(di,dj) = (Di,Dj) and M*(ini,inj) =
            // -(Nini,Ninj) (inward folds to neighbor's OUTWARD, so a chart-rh
            // interior point lands outside the neighbor — i.e. the same physical
            // point on the shared line maps consistently; the actual hop for an
            // unowned/out-of-range vertex uses the full affine below). The two
            // source vectors (edge, inward) are linearly independent.
            // [di ini] [m00 m01]^T columns -> build from basis.
            // Let columns c1=(di,dj), c2=(ini,inj); images g1=(Di,Dj),
            // g2=(-Nini,-Ninj). M = [g1 g2] * inv([c1 c2]).
            int detC = di * inj - ini * dj;
            // inv([c1 c2]) = 1/detC * [ inj  -ini ; -dj  di ]
            // M = [g1 g2] * that
            int g1i = Di, g1j = Dj, g2i = -Nini, g2j = -Ninj;
            // M row 0: (g1i, g2i) dot columns of inv
            // M = (1/detC) * [ g1i*inj + g2i*(-dj)   g1i*(-ini) + g2i*di ;
            //                  g1j*inj + g2j*(-dj)   g1j*(-ini) + g2j*di ]
            int a = (g1i * inj - g2i * dj);
            int b = (-g1i * ini + g2i * di);
            int c = (g1j * inj - g2j * dj);
            int d = (-g1j * ini + g2j * di);
            assert(a % detC == 0 && b % detC == 0 && c % detC == 0 && d % detC == 0);
            a /= detC; b /= detC; c /= detC; d /= detC;

            // Translation from corner e0 -> E0: E0 = M*e0 + t.
            int ti = E0i - (a * e0i + b * e0j);
            int tj = E0j - (c * e0i + d * e0j);

            x = {a, b, c, d, ti, tj};

            assert(std::abs(a * d - b * c) == 1 &&
                   "edge transform must be unimodular");
            // Verify the second corner maps correctly.
            assert(a * e1i + b * e1j + ti == E1i &&
                   c * e1i + d * e1j + tj == E1j &&
                   "edge transform must map both corners");
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
// Hex cell assignment
// ============================================================================

// Cube rounding in axial coords. floor()/abs() are exact IEEE operations,
// bit-deterministic across platforms (no transcendentals involved).
void SphereGrid::hexRound(double fq, double fr, int& outI, int& outJ) {
    double q = std::floor(fq + 0.5);
    double r = std::floor(fr + 0.5);
    double s = std::floor(-fq - fr + 0.5);
    double dq = std::abs(q - fq);
    double dr = std::abs(r - fr);
    double ds = std::abs(s + fq + fr);
    if (dq > dr && dq > ds) {
        q = -r - s;
    } else if (dr > ds) {
        r = -q - s;
    }
    outI = static_cast<int>(q);
    outJ = static_cast<int>(r);
}

// ============================================================================
// canonicalVertex: map a chart VERTEX (i,j) to its owning TileId
// ============================================================================

// A vertex sits at chart coords (i,j), possibly out of [0..n] from rounding
// overshoot. Ownership: r owns i in [1..n], j in [0..n-1]. Out-of-range and
// unowned vertices are resolved by applying the exact per-edge lattice transform
// (edgeXform) to hop into the neighbor chart that owns the physical vertex.
TileId SphereGrid::canonicalVertex(int rh, int i, int j) const {
    int in = static_cast<int>(n);

    auto applyXform = [&](int edge) {
        const EdgeXform& x = edgeXform[rh][edge];
        int ni = x.a * i + x.b * j + x.ti;
        int nj = x.c * i + x.d * j + x.tj;
        i = ni;
        j = nj;
        rh = edgeAdj[rh][edge].neighborRhombus;
    };

    // Up to a handful of hops resolves any vertex to its owner. Each hop strictly
    // moves toward an owning chart; the icosahedron's seam graph needs at most a
    // few. Guard with an iteration cap.
    for (int iter = 0; iter < 8; ++iter) {
        // Bring into [0..n]^2 first.
        if (i < 0)      { applyXform(0); continue; } // crossing u=0
        if (i > in)     { applyXform(1); continue; } // crossing u=n
        if (j < 0)      { applyXform(2); continue; } // crossing v=0
        if (j > in)     { applyXform(3); continue; } // crossing v=n

        // In range. Poles first: A=(0,0) of a northern rhombus is the north pole;
        // C=(n,n) of a southern rhombus is the south pole.
        if (i == 0 && j == 0 && rhombi[rh].vA == 0) return northPole();
        if (i == in && j == in && rhombi[rh].vC == 11) return southPole();

        if (i >= 1 && i <= in && j >= 0 && j <= in - 1) {
            return encodeOwned(static_cast<uint32_t>(rh),
                               static_cast<uint32_t>(i), static_cast<uint32_t>(j));
        }

        // Unowned in-range vertex: i==0 (u=0 seam) or j==n (v=n seam, incl. the
        // (n,n) ring corner). Hop across that seam to the owning chart.
        if (i == 0)       applyXform(0);
        else /* j == in */ applyXform(3);
    }
    return kInvalidTile;
}

// ============================================================================
// Public API
// ============================================================================

TileId SphereGrid::fromUnitVector(Vec3d dir) const {
    uint32_t rh{};
    double u{}, v{};
    dirToRhombusUV(dir, rh, u, v);
    double dn = static_cast<double>(n);
    int i{}, j{};
    // Tile centers are at chart vertices (i/n, j/n): round u*n, v*n directly.
    hexRound(u * dn, v * dn, i, j);
    TileId t = canonicalVertex(static_cast<int>(rh), i, j);
    if (t != kInvalidTile) return t;
    // Unmappable only in degenerate cases; clamp into this rhombus's owned set.
    int in = static_cast<int>(n);
    i = std::clamp(i, 1, in);
    j = std::clamp(j, 0, in - 1);
    return encodeOwned(rh, static_cast<uint32_t>(i), static_cast<uint32_t>(j));
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
    if (t == northPole()) return {0.0, 0.0, 1.0};
    if (t == southPole()) return {0.0, 0.0, -1.0};
    uint32_t rh{}, i{}, j{};
    decodeOwned(t, rh, i, j);
    double u = static_cast<double>(i) / static_cast<double>(n);
    double v = static_cast<double>(j) / static_cast<double>(n);
    return uvToDir(rh, u, v);
}

void SphereGrid::latLonOf(TileId t, double& latDeg, double& lonDeg) const {
    Vec3d d = tileCenter(t);
    latDeg = safeAsin(d.z) * k180OverPi;
    lonDeg = detAtan2(d.y, d.x) * k180OverPi;
}

SphereGrid::HexSample SphereGrid::locateHex(double latDeg, double lonDeg) const {
    using foundation::det_math::cos;
    using foundation::det_math::sin;
    double lat = latDeg * kPiOver180;
    double lon = lonDeg * kPiOver180;
    double cosLat = cos(lat);
    Vec3d dir = {cosLat * cos(lon), cosLat * sin(lon), sin(lat)};

    uint32_t rh{};
    double u{}, v{};
    dirToRhombusUV(dir, rh, u, v);

    double dn = static_cast<double>(n);
    double fq = u * dn;
    double fr = v * dn;
    int q{}, r{};
    hexRound(fq, fr, q, r);

    // Unskewed Cartesian for the 60-degree lattice basis:
    // (x, y) = (a + 0.5*b, b*sqrt(3)/2). Pure IEEE arithmetic.
    constexpr double kHalfSqrt3 = 0.86602540378443864676;
    double px = fq + 0.5 * fr;
    double py = fr * kHalfSqrt3;
    auto distToCenter = [&](double ci, double cj) {
        double dx = px - (ci + 0.5 * cj);
        double dy = py - cj * kHalfSqrt3;
        return std::sqrt(dx * dx + dy * dy);
    };

    TileId tile = canonicalVertex(static_cast<int>(rh), q, r);
    if (tile == kInvalidTile) tile = fromUnitVector(dir);

    double d1 = distToCenter(q, r);

    // The blend partner is the second-nearest cell center, drawn from the tile's
    // actual neighbor set (which is seam-correct and symmetric). Rank candidates
    // by 3D chord distance to the query direction; break ties on smaller TileId.
    std::array<TileId, 6> nbrs{};
    uint32_t cnt = neighbors(tile, nbrs);
    TileId best = kInvalidTile;
    double bestDot = -2.0;
    for (uint32_t k = 0; k < cnt; ++k) {
        double d = vecDot(dir, tileCenter(nbrs[k]));
        if (d > bestDot || (d == bestDot && nbrs[k] < best)) {
            bestDot = d;
            best = nbrs[k];
        }
    }

    // edgeDistance in lattice units: distance to the nearest neighboring vertex
    // center in the home chart's lattice metric. The 6 lattice offsets give the
    // candidate centers; the smallest non-self distance is the Voronoi edge.
    double d2 = 1e30;
    static const int kDi[6] = { 1, -1, 0, 0, 1, -1};
    static const int kDj[6] = { 0,  0, 1, -1, -1, 1};
    for (int k = 0; k < 6; ++k) {
        int ci = q + kDi[k];
        int cj = r + kDj[k];
        double d = distToCenter(ci, cj);
        if (d < d2) d2 = d;
    }
    if (d2 > 1e29) d2 = d1; // degenerate; clamp edgeDistance to 0 below

    HexSample out{};
    out.tile = tile;
    out.neighbor = best;
    // Cube rounding can disagree with exact Euclidean nearest by a hair at
    // tri-corner points, making d2 < d1; clamp to keep the contract.
    out.edgeDistance = static_cast<float>(std::max(0.0, 0.5 * (d2 - d1)));
    return out;
}

float SphereGrid::tileWidthMeters(TileId t, double planetRadiusMeters) const {
    // The hex Voronoi cell has the same area as the lattice fundamental domain:
    // the full 1/n x 1/n (u,v) parallelogram around the vertex. We sample that
    // full-size cell, shifting its POSITION (not its size) to stay inside [0,1]
    // for seam vertices, so a tile on a rhombus edge measures the same as an
    // interior tile. The 12 icosahedron-vertex pentagons cover 5/6 of a hex cell;
    // we apply that factor so their width is consistent with their true area.
    uint32_t rh;
    double cu, cv;       // cell-center (u,v) in [0,1]
    bool pentagon = (t == northPole() || t == southPole());
    double h = 0.5 / n;
    if (t == northPole()) { rh = 0; cu = h; cv = h; }
    else if (t == southPole()) { rh = 5; cu = 1.0 - h; cv = 1.0 - h; }
    else {
        uint32_t i{}, j{};
        decodeOwned(t, rh, i, j);
        // Shift the full cell inward so [cu-h, cu+h] stays in [0,1] (the 10 ring
        // pentagons read as full hexes here; their ~6/5 overestimate is within
        // the area-uniformity tolerance and they are a negligible fraction).
        cu = std::clamp(static_cast<double>(i) / n, h, 1.0 - h);
        cv = std::clamp(static_cast<double>(j) / n, h, 1.0 - h);
    }
    Vec3d c00 = uvToDir(rh, cu - h, cv - h);
    Vec3d c10 = uvToDir(rh, cu + h, cv - h);
    Vec3d c01 = uvToDir(rh, cu - h, cv + h);
    Vec3d c11 = uvToDir(rh, cu + h, cv + h);
    double area = sphericalTriArea(c00, c10, c11) + sphericalTriArea(c00, c11, c01);
    if (pentagon) area *= 5.0 / 6.0;
    return static_cast<float>(detSqrt(area) * planetRadiusMeters);
}

// ============================================================================
// neighbors
// ============================================================================

uint32_t SphereGrid::neighbors(TileId t, std::array<TileId, 6>& out) const {
    int in = static_cast<int>(n);

    static const int kDi[6] = { 1, -1, 0, 0, 1, -1};
    static const int kDj[6] = { 0,  0, 1, -1, -1, 1};

    auto pushUnique = [&](uint32_t& count, TileId cand) {
        if (cand == kInvalidTile || cand == t) return;
        for (uint32_t q = 0; q < count; ++q)
            if (out[q] == cand) return;
        if (count < 6) out[count++] = cand;
    };

    // Pole tiles: the 5 ring-1 vertices around the pole corner. The pole sits at
    // a chart corner shared by 5 rhombi; its distance-1 neighbors are the two
    // edge-adjacent vertices of each of those 5 charts, which dedup to 5 distinct
    // ring tiles. North pole = A=(0,0) of the 5 northern rhombi; south pole =
    // C=(n,n) of the 5 southern rhombi. (The 6-offset hex stencil does not apply
    // at a pentagon's disclination, so we enumerate the ring directly.)
    if (t == northPole()) {
        uint32_t count = 0;
        for (int r = 0; r < 5; ++r) {
            pushUnique(count, canonicalVertex(r, 1, 0));
            pushUnique(count, canonicalVertex(r, 0, 1));
        }
        return count;
    }
    if (t == southPole()) {
        uint32_t count = 0;
        for (int r = 5; r < 10; ++r) {
            pushUnique(count, canonicalVertex(r, in - 1, in));
            pushUnique(count, canonicalVertex(r, in, in - 1));
        }
        return count;
    }

    uint32_t rh{}, i{}, j{};
    decodeOwned(t, rh, i, j);

    // Most tiles are interior hexagons: the 6 fixed axial offsets are exact and
    // already in canonical order. WorldData::downhill indexes the returned array,
    // so the order must be deterministic (it is re-read via neighbors() with the
    // same index, so any stable order is valid).
    if (i >= 2 && i <= in - 1 && j >= 1 && j <= in - 2) {
        uint32_t count = 0;
        for (int k = 0; k < 6; ++k) {
            out[count++] = canonicalVertex(static_cast<int>(rh),
                                           static_cast<int>(i) + kDi[k],
                                           static_cast<int>(j) + kDj[k]);
        }
        return count;
    }

    // Seam-adjacent tiles (within one cell of a rhombus edge, hence possibly
    // adjacent to a pentagon disclination): the axial stencil can overshoot the
    // missing sector around a degree-5 vertex. Gather a wider candidate stencil,
    // canonicalize, and keep the physically-nearest distinct centers (5 for a
    // pentagon's ring, 6 otherwise), ordered by chord distance then TileId so the
    // result is deterministic.
    static const int kSi[18] = { 1,-1, 0, 0, 1,-1,  2,-2, 0, 0, 2,-2, 1,-1,-1, 1, 1,-1};
    static const int kSj[18] = { 0, 0, 1,-1,-1, 1,  0, 0, 2,-2,-2, 2, 1,-1, 2,-2,-1, 1};

    Vec3d center = tileCenter(t);
    std::array<TileId, 18> cand{};
    std::array<double, 18> cdist{};
    uint32_t cn = 0;
    auto addCand = [&](TileId c) {
        if (c == kInvalidTile || c == t) return;
        for (uint32_t q = 0; q < cn; ++q) if (cand[q] == c) return;
        Vec3d cc = tileCenter(c);
        double dx = cc.x - center.x, dy = cc.y - center.y, dz = cc.z - center.z;
        cand[cn] = c;
        cdist[cn] = dx * dx + dy * dy + dz * dz;
        ++cn;
    };
    for (int k = 0; k < 18; ++k) {
        addCand(canonicalVertex(static_cast<int>(rh),
                                static_cast<int>(i) + kSi[k],
                                static_cast<int>(j) + kSj[k]));
    }

    // Nearest candidate distance sets the ring radius; true neighbors lie within
    // ~1.25x of it (next ring is sqrt(3) ~ 1.73x away).
    double minD = 1e30;
    for (uint32_t q = 0; q < cn; ++q) if (cdist[q] < minD) minD = cdist[q];
    double thresh = minD * (1.25 * 1.25);

    // Selection-sort the in-threshold candidates by (distance, TileId).
    uint32_t count = 0;
    std::array<bool, 18> used{};
    while (count < 6) {
        int best = -1;
        for (uint32_t q = 0; q < cn; ++q) {
            if (used[q] || cdist[q] > thresh) continue;
            if (best < 0 || cdist[q] < cdist[best] ||
                (cdist[q] == cdist[best] && cand[q] < cand[best])) {
                best = static_cast<int>(q);
            }
        }
        if (best < 0) break;
        used[best] = true;
        out[count++] = cand[best];
    }
    return count;
}

TileId SphereGrid::canonicalTile(uint32_t rhombus, int i, int j) const {
    return canonicalVertex(static_cast<int>(rhombus), i, j);
}

// ============================================================================
// rhombusPointOnSphere — public forward mapping for planet-view
// ============================================================================

Vec3d SphereGrid::rhombusPointOnSphere(uint32_t r, double u, double v) const {
    return uvToDir(r, u, v);
}

} // namespace worldgen
