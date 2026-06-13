// Unit tests for the detail-tier page math, LRU eviction, texel mapping
// (including borders and poles), and the scheduler's pixels-per-tile estimate.
// All pure CPU — no GL context required.

#include "PlanetLru.h"
#include "PlanetPageMath.h"
#include "PlanetScheduler.h"

#include <world/worldgen/grid/SphereGrid.h>

#include <gtest/gtest.h>

#include <set>

using namespace planetview;

// ── Page math ──────────────────────────────────────────────────────────────

TEST(PlanetPageMath, PagesPerSide) {
    EXPECT_EQ(pagesPerSide(128), 1u);
    EXPECT_EQ(pagesPerSide(129), 2u);
    EXPECT_EQ(pagesPerSide(256), 2u);
    EXPECT_EQ(pagesPerSide(257), 3u);
    EXPECT_EQ(pagesPerSide(1449), 12u); // ceil(1449/128)
}

TEST(PlanetPageMath, TexelToVertexInterior) {
    // Page (0,0): interior texel (1,1) -> first owned vertex i=1, j=0.
    int i, j;
    pageTexelToVertex(0, 0, 1, 1, i, j);
    EXPECT_EQ(i, 1);
    EXPECT_EQ(j, 0);
    // Texel (128,128) -> i=128, j=127 (last owned tile in the page).
    pageTexelToVertex(0, 0, 128, 128, i, j);
    EXPECT_EQ(i, 128);
    EXPECT_EQ(j, 127);
    // Page (1,1): interior texel (1,1) -> i=129, j=128.
    pageTexelToVertex(1, 1, 1, 1, i, j);
    EXPECT_EQ(i, 129);
    EXPECT_EQ(j, 128);
}

TEST(PlanetPageMath, BorderTexels) {
    // Left border (tx=0) of page (0,0) -> i=0 (unowned u=0 seam).
    int i, j;
    pageTexelToVertex(0, 0, 0, 1, i, j);
    EXPECT_EQ(i, 0);
    EXPECT_EQ(j, 0);
    // Top border (ty=0) -> j=-1 (off-chart, resolved by canonicalTile).
    pageTexelToVertex(0, 0, 1, 0, i, j);
    EXPECT_EQ(i, 1);
    EXPECT_EQ(j, -1);
    // Right border (tx=129) of page (0,0) -> i=129.
    pageTexelToVertex(0, 0, 129, 1, i, j);
    EXPECT_EQ(i, 129);
}

TEST(PlanetPageMath, VertexToPageRoundTrip) {
    for (int i : {1, 64, 128, 129, 200, 256}) {
        for (int j : {0, 63, 127, 128, 255}) {
            uint32_t pi, pj;
            vertexToPage(i, j, pi, pj);
            // The interior texel of that page must map back to (i, j).
            int tx = i - static_cast<int>(pi) * kPageTiles;
            int ty = j - static_cast<int>(pj) * kPageTiles + kPageBorder;
            int ri, rj;
            pageTexelToVertex(pi, pj, tx, ty, ri, rj);
            EXPECT_EQ(ri, i) << "i=" << i << " j=" << j;
            EXPECT_EQ(rj, j) << "i=" << i << " j=" << j;
        }
    }
}

// ── LRU eviction ───────────────────────────────────────────────────────────

TEST(PlanetLru, AllocateAndEvictLeastRecentlyUsed) {
    PlanetLru lru;
    lru.init(2);
    uint64_t evicted = PlanetLru::kNoKey;

    int l0 = lru.allocate(10, evicted);
    EXPECT_EQ(evicted, PlanetLru::kNoKey);
    int l1 = lru.allocate(20, evicted);
    EXPECT_EQ(evicted, PlanetLru::kNoKey);
    EXPECT_NE(l0, l1);
    EXPECT_TRUE(lru.resident(10));
    EXPECT_TRUE(lru.resident(20));

    // Touch 10 so 20 becomes LRU; allocating a third evicts 20.
    lru.touch(10);
    int l2 = lru.allocate(30, evicted);
    EXPECT_EQ(evicted, 20u);
    EXPECT_EQ(l2, l1); // reuses the freed layer
    EXPECT_TRUE(lru.resident(10));
    EXPECT_FALSE(lru.resident(20));
    EXPECT_TRUE(lru.resident(30));
}

TEST(PlanetLru, ReallocateResidentReturnsSameLayer) {
    PlanetLru lru;
    lru.init(4);
    uint64_t evicted = PlanetLru::kNoKey;
    int l = lru.allocate(7, evicted);
    int again = lru.allocate(7, evicted);
    EXPECT_EQ(l, again);
    EXPECT_EQ(evicted, PlanetLru::kNoKey);
}

TEST(PlanetLru, ClearDropsAll) {
    PlanetLru lru;
    lru.init(2);
    uint64_t evicted = PlanetLru::kNoKey;
    lru.allocate(1, evicted);
    lru.allocate(2, evicted);
    lru.clear();
    EXPECT_FALSE(lru.resident(1));
    EXPECT_FALSE(lru.resident(2));
}

// ── Texel -> tile mapping against the grid (borders + poles) ────────────────

namespace {
// The tile a page texel (tx,ty) resolves to via the same canonicalTile call the
// CPU baker uses.
worldgen::TileId pageTexelTile(const worldgen::SphereGrid& grid,
                               uint32_t r, uint32_t pi, uint32_t pj, int tx, int ty) {
    int i, j;
    pageTexelToVertex(pi, pj, tx, ty, i, j);
    return grid.canonicalTile(r, i, j);
}
} // namespace

TEST(PlanetDetailCacheTexels, InteriorTexelMatchesVertexTile) {
    worldgen::SphereGrid grid(64); // 1 page per side
    for (uint32_t r = 0; r < 10; ++r) {
        // Interior owned vertex i=10, j=20 -> texel (10, 21).
        worldgen::TileId direct = grid.canonicalTile(r, 10, 20);
        worldgen::TileId viaTexel = pageTexelTile(grid, r, 0, 0, 10, 21);
        EXPECT_EQ(direct, viaTexel) << "rhombus " << r;
        EXPECT_NE(direct, worldgen::kInvalidTile);
    }
}

TEST(PlanetDetailCacheTexels, CrossPageBorderAgrees) {
    // n=256 -> 2 pages per side. The right-border texel of page (0,pj) must
    // resolve to the same tile as the left-interior texel of page (1,pj).
    worldgen::SphereGrid grid(256);
    for (uint32_t r = 0; r < 10; ++r) {
        for (uint32_t pj = 0; pj < 2; ++pj) {
            // Page0 right border tx=129 -> i = 0*128+129 = 129.
            worldgen::TileId border = pageTexelTile(grid, r, 0, pj, 129, 5);
            // Page1 first interior tx=1 -> i = 1*128+1 = 129. Same vertex.
            worldgen::TileId interior = pageTexelTile(grid, r, 1, pj, 1, 5);
            EXPECT_EQ(border, interior) << "rhombus " << r << " pj " << pj;
        }
    }
}

TEST(PlanetDetailCacheTexels, NorthPoleResolvesAtCorner) {
    worldgen::SphereGrid grid(64);
    // North pole is A=(0,0) of the 5 northern rhombi. Page (0,0) top-left border
    // texel (tx=0, ty=0) -> vertex (0,-1); the corner vertex (i=0,j=0) is the
    // pole for a northern rhombus. Check the (0,0) vertex directly.
    worldgen::TileId pole = grid.canonicalTile(0, 0, 0);
    EXPECT_EQ(pole, grid.northPole());
    // And it is reachable as a border texel of page (0,0): texel (0,1) -> (0,0).
    worldgen::TileId viaTexel = pageTexelTile(grid, 0, 0, 0, 0, 1);
    EXPECT_EQ(viaTexel, grid.northPole());
}

TEST(PlanetDetailCacheTexels, AllTexelsInRangeResolveValid) {
    worldgen::SphereGrid grid(128); // 1 page per side, covers full chart
    for (uint32_t r = 0; r < 10; ++r) {
        std::set<worldgen::TileId> seen;
        for (int ty = 0; ty < kPageTexels; ++ty) {
            for (int tx = 0; tx < kPageTexels; ++tx) {
                worldgen::TileId t = pageTexelTile(grid, r, 0, 0, tx, ty);
                EXPECT_NE(t, worldgen::kInvalidTile)
                    << "r=" << r << " tx=" << tx << " ty=" << ty;
                if (t != worldgen::kInvalidTile) {
                    EXPECT_LT(t, grid.tileCount());
                    seen.insert(t);
                }
            }
        }
        // The interior (128x128) must cover every owned tile of the rhombus once.
        EXPECT_GE(seen.size(), 128u * 128u);
    }
}

// ── Scheduler estimate ──────────────────────────────────────────────────────

TEST(PlanetScheduler, PixelsPerTileMonotoneInDistance) {
    float near = estimatePixelsPerTile(1.02f, 45.0f, 1080, 1449);
    float mid  = estimatePixelsPerTile(1.5f, 45.0f, 1080, 1449);
    float far  = estimatePixelsPerTile(4.0f, 45.0f, 1080, 1449);
    EXPECT_GT(near, mid);
    EXPECT_GT(mid, far);
    // Zoomed in close at high n, tiles should be clearly multi-pixel.
    EXPECT_GT(near, 2.0f);
}

TEST(PlanetScheduler, PixelsPerTileShrinksWithN) {
    float lowN  = estimatePixelsPerTile(1.05f, 45.0f, 1080, 256);
    float highN = estimatePixelsPerTile(1.05f, 45.0f, 1080, 4096);
    EXPECT_GT(lowN, highN);
}
