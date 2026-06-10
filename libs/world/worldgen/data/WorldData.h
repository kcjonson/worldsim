#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace worldgen {

// Bit flags for the flags field (uint8_t per tile)
inline constexpr uint8_t kFlagOcean           = 0x01;
inline constexpr uint8_t kFlagLake            = 0x02;
inline constexpr uint8_t kFlagRiver           = 0x04;
inline constexpr uint8_t kFlagCoast           = 0x08;
inline constexpr uint8_t kFlagPermanentSnow   = 0x10;
inline constexpr uint8_t kFlagGlacier         = 0x20;
// Set by PlateStage on tiles with continental crust (including shelves).
// Continental plates have craton cores; oceanic margins of continental plates
// and small mixed-plate areas also carry this flag when within the craton growth radius.
inline constexpr uint8_t kFlagContinentalCrust = 0x40;

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
};

inline constexpr uint32_t kAllWorldFields = 0x7FFFu; // bits 0..14

// SoA world data storage — 26 bytes per tile.
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
//   boundaryType:      uint8 (0=interior, 1=divergent, 2=convergent, 3=transform)
//   boundaryDistance:  uint16, tiles from nearest plate boundary
//   biome:             uint8, Biome enum value
//   flags:             uint8, kFlag* bits
//   waterDepth:        uint16, meters (0 = land or dry)
//   flowAccum:         float, accumulated upstream drainage area (tile count)
//   downhill:          uint8, neighbor direction index 0..7 (0xFF = none/sink)
//   snowCover:         uint8, 0..255 (0 = bare, 255 = full permanent snow)
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
    }
};

} // namespace worldgen
