#include "PlanetTileColor.h"

#include <world/worldgen/data/GeneratedWorld.h>
#include <world/worldgen/data/WorldData.h>
#include <world/worldgen/debug/ColorMaps.h>

#include <algorithm>
#include <cmath>

namespace planetview {

namespace {

RGBA8 toRGBA(worldgen::Rgb c) { return {c.r, c.g, c.b, 255}; }
RGBA8 neutralGray() { return {128, 128, 128, 255}; }

bool hasField(uint32_t validFields, worldgen::WorldField f) {
    return (validFields & static_cast<uint32_t>(f)) != 0;
}

// Overlay the cryosphere on a base color. Two distinct surfaces:
//   - Solid ice (sea ice / glacier): an OPAQUE surface that hides the water or
//     ground, with a hard edge against open water (no feather). Thickness shades
//     it from thin pale pack ice to a thick bright ice sheet.
//   - Permanent snow (land only): a near-opaque year-round snowfield (white), so
//     polar/alpine caps read solid. Snow never sits on water. (Thin, see-through
//     SEASONAL snow is future work, driven by the planned season system.)
// One helper serves Terrain, Combined, and the Hydrology ocean branch.
RGBA8 applyIceOverlay(RGBA8 base, uint32_t tileId,
                      const worldgen::WorldData& data, uint32_t validFields) {
    if (!hasField(validFields, worldgen::WorldField::Flags))
        return base;
    const uint8_t flags = data.flags[tileId];

    auto lerp = [](uint8_t a, float to, float op) {
        return static_cast<uint8_t>(static_cast<float>(a) * (1.0f - op) + to * op);
    };

    // Solid ice: opaque, hard-edged. Thin sea ice (~1-3 m) reads pale icy blue;
    // a thick ice sheet reads bright white. Replaces the base entirely.
    if ((flags & (worldgen::kFlagSeaIce | worldgen::kFlagGlacier)) != 0 &&
        hasField(validFields, worldgen::WorldField::IceThickness)) {
        float thick = static_cast<float>(data.iceThickness[tileId]);
        float s = std::clamp(thick / 800.0f, 0.0f, 1.0f); // thin -> thick (>=800 m)
        auto mix = [&](float thinC, float thickC) {
            return static_cast<uint8_t>(thinC + (thickC - thinC) * s);
        };
        return {mix(206.0f, 242.0f), mix(226.0f, 247.0f), mix(240.0f, 252.0f), base.a};
    }

    // Permanent snow on land: a near-opaque year-round snowfield, so caps read
    // solid rather than letting the ground show through. Coverage only nudges the
    // last bit of opacity. (Seasonal, see-through snow will come with the season
    // system; this field is the permanent baseline.)
    if ((flags & worldgen::kFlagPermanentSnow) != 0 &&
        hasField(validFields, worldgen::WorldField::SnowCover)) {
        float cov = static_cast<float>(data.snowCover[tileId]) / 255.0f;
        float op = std::clamp(0.85f + 0.15f * cov, 0.0f, 1.0f);
        base.r = lerp(base.r, 247.0f, op);
        base.g = lerp(base.g, 250.0f, op);
        base.b = lerp(base.b, 255.0f, op);
    }
    return base;
}

} // namespace

RGBA8 colorForTile(uint32_t tileId, ColorMode mode,
                   const worldgen::GeneratedWorld& world) {
    const worldgen::WorldData& data = world.data;
    const uint32_t validFields = world.validFields;
    const float seaLevelMeters = world.seaLevelMeters;

    // Guard against ids beyond the allocated arrays (e.g. a stale border texel).
    // Pole ids index the same arrays (tileCount includes the 2 poles), so valid
    // poles are in range; only truly bad ids fail here.
    if (tileId == worldgen::kInvalidTile || tileId >= data.elevation.size())
        return neutralGray();

    switch (mode) {
        case ColorMode::Terrain: {
            if (!hasField(validFields, worldgen::WorldField::Elevation))
                return neutralGray();
            RGBA8 base = toRGBA(worldgen::elevationColor(data.elevation[tileId], seaLevelMeters));
            return applyIceOverlay(base, tileId, data, validFields);
        }
        case ColorMode::Temperature: {
            if (!hasField(validFields, worldgen::WorldField::TemperatureMean))
                return neutralGray();
            float tempC = static_cast<float>(data.temperatureMean[tileId]) * 0.1f;
            return toRGBA(worldgen::temperatureColor(tempC));
        }
        case ColorMode::Precipitation: {
            if (!hasField(validFields, worldgen::WorldField::Precipitation))
                return neutralGray();
            float precip = static_cast<float>(data.precipitation[tileId]);
            return toRGBA(worldgen::precipitationColor(precip));
        }
        case ColorMode::Biome: {
            if (!hasField(validFields, worldgen::WorldField::Biome))
                return neutralGray();
            uint8_t b = data.biome[tileId];
            if (b >= static_cast<uint8_t>(worldgen::Biome::Count))
                return neutralGray();
            auto c = worldgen::kBiomeColors[b];
            return {c.r, c.g, c.b, 255};
        }
        case ColorMode::Plates: {
            if (!hasField(validFields, worldgen::WorldField::PlateId))
                return neutralGray();
            uint8_t plateId = data.plateId[tileId];
            if (plateId == 255) return neutralGray();
            RGBA8 base = toRGBA(worldgen::plateColor(plateId));
            if (hasField(validFields, worldgen::WorldField::BoundaryDistance)) {
                uint16_t dist = data.boundaryDistance[tileId];
                if (dist <= 2) {
                    base.r = static_cast<uint8_t>(base.r * 0.6f);
                    base.g = static_cast<uint8_t>(base.g * 0.6f);
                    base.b = static_cast<uint8_t>(base.b * 0.6f);
                }
            }
            return base;
        }
        case ColorMode::Snow: {
            if (!hasField(validFields, worldgen::WorldField::SnowCover))
                return neutralGray();
            uint8_t snow = data.snowCover[tileId];
            if (snow == 0) return {60, 100, 50, 255};
            float t = static_cast<float>(snow) / 255.0f;
            auto r = static_cast<uint8_t>(60 + (240 - 60) * t);
            auto g = static_cast<uint8_t>(100 + (245 - 100) * t);
            auto b = static_cast<uint8_t>(50 + (255 - 50) * t);
            return {r, g, b, 255};
        }
        case ColorMode::Combined: {
            if (!hasField(validFields, worldgen::WorldField::Biome))
                return neutralGray();
            uint8_t biomeIdx = data.biome[tileId];
            if (biomeIdx >= static_cast<uint8_t>(worldgen::Biome::Count))
                return neutralGray();
            auto bc = worldgen::kBiomeColors[biomeIdx];
            RGBA8 base = {bc.r, bc.g, bc.b, 255};

            bool isOcean = (biomeIdx == static_cast<uint8_t>(worldgen::Biome::Ocean) ||
                            biomeIdx == static_cast<uint8_t>(worldgen::Biome::Lake));
            if (isOcean && hasField(validFields, worldgen::WorldField::Elevation)) {
                float elev = data.elevation[tileId];
                float depth = seaLevelMeters - elev;
                float t = std::clamp(depth / 4000.0f, 0.0f, 1.0f) * 0.7f;
                base.r = static_cast<uint8_t>(base.r * (1.0f - t) + 10 * t);
                base.g = static_cast<uint8_t>(base.g * (1.0f - t) + 30 * t);
                base.b = static_cast<uint8_t>(base.b * (1.0f - t) + 80 * t);
            }

            return applyIceOverlay(base, tileId, data, validFields);
        }
        case ColorMode::Hydrology: {
            // Oceans: depth-shaded blue, same as Terrain, so oceans read normally;
            // frozen ocean gets the sea-ice overlay so caps show in this view too.
            if (hasField(validFields, worldgen::WorldField::Elevation)) {
                float elev = data.elevation[tileId];
                if (elev < seaLevelMeters) {
                    RGBA8 c = toRGBA(worldgen::elevationColor(elev, seaLevelMeters));
                    return applyIceOverlay(c, tileId, data, validFields);
                }
            }

            // Flags-driven water features, checked before the drainage gradient.
            if (hasField(validFields, worldgen::WorldField::Flags)) {
                uint8_t fl = data.flags[tileId];
                if (fl & worldgen::kFlagLake) {
                    // Lake: bright cyan-blue, distinct from ocean and rivers.
                    return {60, 190, 230, 255};
                }
                if (fl & worldgen::kFlagRiver) {
                    // River: strong saturated blue.
                    return {30, 100, 220, 255};
                }
            }

            // Dry land: muted warm-gray base tinted by log(flowAccum+1) so
            // sub-river tributaries appear as a faint drainage gradient.
            // log scale: flowAccum of ~1 -> t~0 (neutral), ~1000 -> t~0.5, ~1e6 -> t~1.
            float flow = 0.0f;
            if (hasField(validFields, worldgen::WorldField::FlowAccum) &&
                tileId < data.flowAccum.size()) {
                flow = data.flowAccum[tileId];
            }
            float t = std::log(flow + 1.0f) / 14.0f; // log(1e6)~13.8
            if (t > 1.0f) t = 1.0f;
            // Neutral land (105,100,90) -> faint blue-teal tint at high drainage.
            auto r = static_cast<uint8_t>(105 - static_cast<int>(t * 50));
            auto g = static_cast<uint8_t>(100 - static_cast<int>(t * 10));
            auto b = static_cast<uint8_t>( 90 + static_cast<int>(t * 80));
            return {r, g, b, 255};
        }
        case ColorMode::Ice: {
            // Cryosphere analyst view: sea-ice + glacier thickness on a log ramp
            // (thin white -> thick deep blue); bare land/ocean is dark so ice pops.
            if (!hasField(validFields, worldgen::WorldField::Flags))
                return neutralGray();
            uint8_t fl = data.flags[tileId];
            if ((fl & (worldgen::kFlagSeaIce | worldgen::kFlagGlacier)) != 0 &&
                hasField(validFields, worldgen::WorldField::IceThickness)) {
                float thick = static_cast<float>(data.iceThickness[tileId]);
                float s = std::log(thick + 1.0f) / std::log(4000.0f);
                if (s > 1.0f) s = 1.0f;
                auto r = static_cast<uint8_t>(235 + (30 - 235) * s);
                auto g = static_cast<uint8_t>(245 + (90 - 245) * s);
                auto b = static_cast<uint8_t>(252 + (190 - 252) * s);
                return {r, g, b, 255};
            }
            if (fl & worldgen::kFlagOcean) return {18, 28, 45, 255};
            return {55, 55, 60, 255};
        }
        default:
            return neutralGray();
    }
}

} // namespace planetview
