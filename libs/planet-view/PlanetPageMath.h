#pragma once

// Pure page/texel math for the two-tier hex renderer. No GL dependency so the
// page table indexing, visible-rect computation, and LRU eviction policy are
// unit-testable without a context.
//
// Detail tier layout (must stay coherent with SphereGrid's owned-vertex
// encoding: rhombus r owns chart vertices i in [1..n], j in [0..n-1]):
//
//   A PAGE covers a 128x128 block of TILES plus a 1-texel border ring = 130x130
//   texels. Page (pi, pj) of rhombus r covers owned vertices:
//       i in [pi*128 + 1 .. pi*128 + 128]   (i is 1-based)
//       j in [pj*128 + 0 .. pj*128 + 127]   (j is 0-based)
//   Page texel (tx, ty), tx,ty in [0..129], maps to chart vertex:
//       i = pi*128 + tx          (interior tx in [1..128])
//       j = pj*128 + ty - 1      (interior ty in [1..128])
//   The border ring (tx or ty == 0 or 129) holds the neighbor vertex resolved
//   through SphereGrid::canonicalTile, so cross-page / cross-rhombus fetches
//   read the same tile the CPU assigns — no seams.
//
//   Pages per rhombus side = ceil(n / 128). Page table entry stores the resident
//   atlas layer (or a not-resident sentinel).

#include <cstdint>

namespace planetview {

inline constexpr int kPageTiles  = 128;        // tiles per page side
inline constexpr int kPageBorder = 1;          // border ring width
inline constexpr int kPageTexels = kPageTiles + 2 * kPageBorder; // 130

// Pages along one rhombus side for subdivision n.
inline uint32_t pagesPerSide(uint32_t n) {
    return (n + static_cast<uint32_t>(kPageTiles) - 1U) / static_cast<uint32_t>(kPageTiles);
}

// Total page-table entries for one rhombus.
inline uint32_t pageTableEntries(uint32_t n) {
    uint32_t s = pagesPerSide(n);
    return s * s;
}

// Map a page texel (tx, ty) in [0..129]^2 to the chart vertex (i, j) it samples.
// i, j may be out of the owned range (e.g. i=0 on the left border) — the caller
// resolves them through SphereGrid::canonicalTile.
inline void pageTexelToVertex(uint32_t pi, uint32_t pj, int tx, int ty,
                              int& outI, int& outJ) {
    outI = static_cast<int>(pi) * kPageTiles + tx;
    outJ = static_cast<int>(pj) * kPageTiles + ty - 1;
}

// Flatten a page coordinate to a page-table index (row-major, pagesPerSide wide).
inline uint32_t pageIndex(uint32_t pi, uint32_t pj, uint32_t n) {
    return pj * pagesPerSide(n) + pi;
}

// Which page owns chart vertex (i, j)? i in [1..n], j in [0..n-1].
inline void vertexToPage(int i, int j, uint32_t& outPi, uint32_t& outPj) {
    // i is 1-based; tile 1 belongs to page 0. j is 0-based.
    outPi = static_cast<uint32_t>((i - 1) / kPageTiles);
    outPj = static_cast<uint32_t>(j / kPageTiles);
}

// Page-table entry encoding for the GL_R16UI table:
//   0           => not resident
//   layer + 1   => resident in atlas layer `layer`
inline uint16_t encodePageEntry(int atlasLayer) {
    return static_cast<uint16_t>(atlasLayer + 1);
}
inline bool pageEntryResident(uint16_t entry) { return entry != 0; }
inline int  pageEntryLayer(uint16_t entry)    { return static_cast<int>(entry) - 1; }

} // namespace planetview
