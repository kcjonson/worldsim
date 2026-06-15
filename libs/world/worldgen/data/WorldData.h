#pragma once

#include <utils/WorldHash.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace worldgen {

// Bit flags for the flags field (uint8_t per tile)
inline constexpr uint8_t kFlagOcean           = 0x01;
inline constexpr uint8_t kFlagLake            = 0x02;
inline constexpr uint8_t kFlagRiver           = 0x04;
// 0x08: reserved (was kFlagCoast — removed; BiomeStage computes coastness locally)
inline constexpr uint8_t kFlagPermanentSnow   = 0x10;
// 0x20: reserved (was kFlagGlacier — removed; glacier modeling is a future epic)
// Set by CrustStage on tiles with continental crust (including shelves).
// Driven by the simulated TectonicHistory crustType field; oceanic tiles never carry this.
inline constexpr uint8_t kFlagContinentalCrust = 0x40;

// Fraction of LAND tiles flagged as river (kFlagRiver). PrecipitationStage
// computes the flowAccum quantile at (1 - kRiverLandFraction) over land tiles
// and sets kFlagRiver where flowAccum >= that value, so the river fraction is
// pinned to approximately this constant regardless of grid resolution. A fixed
// flow-magnitude threshold cannot do this: flowAccum is a precip-weighted
// upstream tile count that scales with total tile count, so the same magnitude
// flags a growing fraction of land as resolution rises.
//
// 0.04 = top 4% of land by drainage = major rivers + main tributaries.
// See W-1.5 in .claude/plans/water-hydrology.md.
inline constexpr float kRiverLandFraction = 0.04f;

// Boundary type enum for data.boundaryType (uint8_t per tile).
// Written by TerrainStage; read by any consumer that interprets plate boundaries.
// Values are stored as uint8_t; cast via static_cast<BoundaryType>(data.boundaryType[t]).
enum class BoundaryType : uint8_t {
    None         = 0,  // interior tile — no plate boundary within BFS reach
    ConvergentCC = 1,  // both sides continental crust (collision mountain belt)
    ConvergentCO = 2,  // one continental, one oceanic (subduction zone + arc)
    ConvergentOO = 3,  // both oceanic (island arc + trench)
    Divergent    = 4,  // spreading center (oceanic ridge or continental rift)
    Transform    = 5,  // strike-slip shear boundary
};

// Bit flags identifying which SoA arrays have valid data.
// One bit per field; set by each stage after writing.
enum class WorldField : uint32_t {
    Elevation         = 1u << 0,
    TemperatureMean   = 1u << 1,
    TemperatureRange  = 1u << 2,
    Precipitation     = 1u << 3,
    WindDir           = 1u << 4,
    WindSpeed         = 1u << 5,
    PlateId           = 1u << 6,
    BoundaryType      = 1u << 7,
    BoundaryDistance  = 1u << 8,
    Biome             = 1u << 9,
    Flags             = 1u << 10,
    WaterDepth        = 1u << 11,
    FlowAccum         = 1u << 12,
    Downhill          = 1u << 13,
    SnowCover         = 1u << 14,
    // Tectonic-history fields (M-T3). Appended last to keep IO layout append-only.
    CrustAge          = 1u << 15, // oceanic seafloor age or continental crust age, Myr
    OrogenyAge        = 1u << 16, // Myr since last orogeny (65535 = never)
};

inline constexpr uint32_t kAllWorldFields = 0x1FFFFu; // bits 0..16

// SoA world data storage — 30 bytes per tile.
// All arrays allocated together via allocate(); never resized after that.
//
// Units:
//   elevation:         float, meters above mean radius
//   temperatureMean:   int16, 0.1 °C (divide by 10 for Celsius)
//   temperatureRange:  int16, 0.1 °C (seasonal range, half-amplitude)
//   precipitation:     uint16, mm/yr
//   windDir:           uint8, 0=N, 64=E, 128=S, 192=W (256 = 360 deg)
//   windSpeed:         uint8, m/s (0..255)
//   plateId:           uint8, 0..254 (255 = unassigned)
//   boundaryType:      uint8 — BType enum from TerrainStage:
//                       0=None (interior), 1=ConvergentCC, 2=ConvergentCO,
//                       3=ConvergentOO, 4=Divergent, 5=Transform.
//                       Written by TerrainStage; consumers must use the same enum values.
//   boundaryDistance:  uint16, tiles from nearest plate boundary
//   biome:             uint8, Biome enum value
//   flags:             uint8, kFlag* bits
//   waterDepth:        uint16, meters (0 = land or dry)
//   flowAccum:         float, accumulated upstream drainage area (tile count)
//   downhill:          uint8, neighbor direction index 0..5 (0xFF = none/sink)
//   snowCover:         uint8, 0..255 (0 = bare, 255 = full permanent snow)
//   crustAge:          uint16, Myr — oceanic = seafloor age; continental = crust age; 65534 = max cap
//   orogenyAge:        uint16, Myr since last orogeny; 65535 = never
struct WorldData {
    std::vector<float>    elevation;
    std::vector<int16_t>  temperatureMean;
    std::vector<int16_t>  temperatureRange;
    std::vector<uint16_t> precipitation;
    std::vector<uint8_t>  windDir;
    std::vector<uint8_t>  windSpeed;
    std::vector<uint8_t>  plateId;
    std::vector<uint8_t>  boundaryType;
    std::vector<uint16_t> boundaryDistance;
    std::vector<uint8_t>  biome;
    std::vector<uint8_t>  flags;
    std::vector<uint16_t> waterDepth;
    std::vector<float>    flowAccum;
    std::vector<uint8_t>  downhill;
    std::vector<uint8_t>  snowCover;
    // Tectonic-history fields — appended last (defines IO layout; append-only).
    std::vector<uint16_t> crustAge;   // Myr, cap 65534; 65534 = maximum stored age
    std::vector<uint16_t> orogenyAge; // Myr since last orogeny; 65535 = never

    void allocate(uint32_t tileCount) {
        elevation.assign(tileCount, 0.0f);
        temperatureMean.assign(tileCount, 0);
        temperatureRange.assign(tileCount, 0);
        precipitation.assign(tileCount, 0);
        windDir.assign(tileCount, 0);
        windSpeed.assign(tileCount, 0);
        plateId.assign(tileCount, 255);
        boundaryType.assign(tileCount, 0);
        boundaryDistance.assign(tileCount, 0);
        biome.assign(tileCount, 0);
        flags.assign(tileCount, 0);
        waterDepth.assign(tileCount, 0);
        flowAccum.assign(tileCount, 0.0f);
        downhill.assign(tileCount, 0xFF);
        snowCover.assign(tileCount, 0);
        crustAge.assign(tileCount, 0);
        orogenyAge.assign(tileCount, 65535u);
    }
};

// Visit every SoA array with its WorldField bit, in ascending bit order.
// Single source of truth for field iteration order: PlanetIO derives the
// on-disk array layout from it and both PlanetIO and PlanetGenerator derive
// the worldHash field order from it, so the two can never drift. Adding a
// WorldField means adding it here (and bumping the planet file version).
template <typename WorldDataT, typename Fn>
void forEachFieldArray(WorldDataT& d, Fn&& fn) {
    fn(WorldField::Elevation, d.elevation);
    fn(WorldField::TemperatureMean, d.temperatureMean);
    fn(WorldField::TemperatureRange, d.temperatureRange);
    fn(WorldField::Precipitation, d.precipitation);
    fn(WorldField::WindDir, d.windDir);
    fn(WorldField::WindSpeed, d.windSpeed);
    fn(WorldField::PlateId, d.plateId);
    fn(WorldField::BoundaryType, d.boundaryType);
    fn(WorldField::BoundaryDistance, d.boundaryDistance);
    fn(WorldField::Biome, d.biome);
    fn(WorldField::Flags, d.flags);
    fn(WorldField::WaterDepth, d.waterDepth);
    fn(WorldField::FlowAccum, d.flowAccum);
    fn(WorldField::Downhill, d.downhill);
    fn(WorldField::SnowCover, d.snowCover);
    // Tectonic-history fields appended last — IO layout is defined by this order.
    fn(WorldField::CrustAge, d.crustAge);
    fn(WorldField::OrogenyAge, d.orogenyAge);
}

// worldHash: FNV-1a over each valid array in WorldField bit order, folded
// with hashCombine. Computed by PlanetGenerator at publish and recomputed by
// PlanetIO on load to validate file integrity.
inline uint64_t computeWorldDataHash(uint32_t validFields, const WorldData& data) {
    uint64_t h = foundation::kFnvOffset;
    forEachFieldArray(data, [&](WorldField field, const auto& arr) {
        if (validFields & static_cast<uint32_t>(field)) {
            h = foundation::hashCombine(h, foundation::hashSpan(arr));
        }
    });
    return h;
}

} // namespace worldgen
