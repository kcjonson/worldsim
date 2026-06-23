#pragma once

// PondNetwork2D - synthesizes sparse standing-water ponds at gameplay scale,
// influenced by the 3D climate/hydrology and biome. This replaces the old per-tile
// noise ponds (which flooded ~15-18% of grassland/forest). Ponds sit on a
// deterministic fine lattice so density is ~0-2 per chunk rather than a noise
// flood; each cell's spawn chance is weighted by the biome and precipitation
// sampled from the 3D world at that position (wetter biomes/areas get more, deserts
// and tundra almost none). A cell that falls far from any 3D water (river/lake/
// ocean) can instead spawn a spring-fed pond -- an oasis -- so no region is left
// waterless. All geometry is a pure function of world position + seed, so ponds are
// identical across chunk seams with nothing stored.
//
// Unit contract matches RiverNetwork2D/PlanetSampler: 2D positions are meters from
// the landing site (+x east, +y north).

#include "worldgen/data/GeneratedWorld.h"
#include "worldgen/sampling/SphericalProjection.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace worldgen {

// Reusable scratch for the distance-to-water ring BFS, owned by a gather so the
// per-cell searches don't each allocate. Defined in the .cpp.
struct PondWaterSearch;

class PondNetwork2D {
  public:
    // A pond as a wobble-edged blob in 2D world meters. The rim is a closed loop:
    // radius modulated by two angle sinusoids (phaseA/phaseB), so it tiles seamlessly
    // and never notches.
    struct Pond {
        double  cx{}, cy{};   // center, meters from the landing origin
        float   radius{};     // base radius (meters)
        float   phaseA{};     // rim sinusoid phases
        float   phaseB{};
        uint8_t depth{};      // cosmetic water-depth byte (0..255); fresh water
    };

    // Requires Elevation, Flags, FlowAccum, Downhill, Precipitation, and Biome valid.
    PondNetwork2D(std::shared_ptr<const GeneratedWorld> world,
                  double landingLatDeg, double landingLonDeg);

    // Append every pond whose footprint may intersect the box [minX,maxX] x
    // [minY,maxY] (meters). Deterministic and self-contained: no global index,
    // no mutable state.
    void gatherPonds(double minX, double minY, double maxX, double maxY,
                     std::vector<Pond>& out) const;

    // Cosmetic water-depth byte at (x,y): >0 inside a pond, 0 elsewhere (widest
    // covering pond wins). Standalone query that gathers a small box; the chunk
    // path uses the gathered list directly via sampleDepth.
    [[nodiscard]] uint8_t depthAt(double xMeters, double yMeters) const;

    // Depth byte of `pond` at (x,y), or 0 outside its wobbling rim. Shared by the
    // chunk rasterizer (ChunkSampleResult) and depthAt so they always agree.
    [[nodiscard]] static uint8_t sampleDepth(const Pond& pond, double xMeters, double yMeters);

  private:
    [[nodiscard]] TileId tileAt(double xMeters, double yMeters) const;
    [[nodiscard]] bool   isWaterTile(TileId t) const;
    // Great-circle distance (meters) from `from`'s center to the nearest 3D water
    // tile, via a bounded ring BFS over the grid (returns a definitively-far value
    // if none is within the ring cap). `scratch` is cleared and reused per call.
    [[nodiscard]] double nearestWaterMeters(TileId from, PondWaterSearch& scratch) const;
    // Decide and build the pond for lattice cell (i,j); false if no pond there.
    [[nodiscard]] bool   cellPond(long i, long j, Pond& out, PondWaterSearch& scratch) const;

    std::shared_ptr<const GeneratedWorld> world;
    SphericalProjection projection;
    float seaLevelMeters{};
};

} // namespace worldgen
