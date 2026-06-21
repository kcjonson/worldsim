#pragma once

// RiverNetwork2D - synthesizes 2D river geometry at gameplay scale from the
// coarse 3D drainage graph. Worldgen stores only water *amount* (flowAccum) and
// drainage *direction* (downhill) per spherical tile, several km across; the
// actual channel is a deterministic function of that coarse data plus world
// position, computed here at chunk load (no fine geometry stored). See the
// chunk-time model in the 2026-06-15 water-availability dev-log entry.
//
// Each river tile drains one step to neighbors()[downhill]; projecting that
// tile-center chain through the landing SphericalProjection gives a coarse
// polyline (~km between vertices). We meander it deterministically at sub-tile
// scale and size each point by accumulated flow (hydraulic geometry, w ~ sqrt Q).
// The result is a set of 2D channel segments the chunk layer rasterizes to water.
//
// Unit contract matches PlanetSampler: 2D positions are meters from the landing
// site (+x east, +y north); widths are meters.

#include "worldgen/data/GeneratedWorld.h"
#include "worldgen/sampling/SphericalProjection.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace worldgen {

class RiverNetwork2D {
  public:
    // A flattened channel segment in 2D world meters. Half-width varies linearly
    // from end 0 to end 1 (rivers widen downstream).
    struct Segment {
        double x0{}, y0{}, x1{}, y1{};
        float  halfWidth0{}, halfWidth1{};
    };

    struct PointSample {
        bool  isRiver{false};
        float widthMeters{0.0f};
    };

    // Requires Elevation, Flags, FlowAccum, and Downhill valid in world.validFields.
    RiverNetwork2D(std::shared_ptr<const GeneratedWorld> world,
                   double landingLatDeg, double landingLonDeg);

    // Append every channel sub-segment whose footprint may intersect the
    // axis-aligned box [minX,maxX] x [minY,maxY] (meters). Deterministic and
    // self-contained: no global index, no mutable state.
    void gatherSegments(double minX, double minY, double maxX, double maxY,
                        std::vector<Segment>& out) const;

    // Point query: is (x,y) inside a river channel, and how wide is it there.
    [[nodiscard]] PointSample sampleAt(double xMeters, double yMeters) const;

    // Channel width (meters) for a tile's accumulated flow. Hydraulic geometry:
    // width scales with sqrt of discharge, clamped to a realistic band. Public
    // so tests and tuning can assert the curve.
    [[nodiscard]] static float channelWidthMeters(float flowAccum);

  private:
    [[nodiscard]] WorldPos2d projectTile(TileId t) const;
    [[nodiscard]] TileId     downstreamTile(TileId t) const;
    [[nodiscard]] bool       isOceanTile(TileId t) const;
    [[nodiscard]] bool       isRiverTile(TileId t) const;

    // Collect river tiles whose downstream segment may reach the box, by a small
    // bounded BFS over the grid from the box-center tile.
    void collectRiverTiles(double minX, double minY, double maxX, double maxY,
                           std::vector<TileId>& out) const;

    // Emit the meandered, flow-sized sub-segments for the segment tile->downstream,
    // culled to those whose footprint intersects the (grown) query box.
    void emitSegment(TileId tile, double minX, double minY, double maxX, double maxY,
                     std::vector<Segment>& out) const;

    std::shared_ptr<const GeneratedWorld> world;
    SphericalProjection projection;
    float seaLevelMeters{};
};

} // namespace worldgen
