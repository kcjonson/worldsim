#pragma once

#include "worldgen/data/Biome.h"

#include <array>
#include <cstdint>

namespace worldgen {

struct Rgb {
    uint8_t r, g, b;
};

// Fixed 21-color palette for the Biome enum.
// Index matches Biome::* integer value.
// Used by DebugImageExporter and later by planet-view's colorizer.
inline constexpr std::array<Rgb, static_cast<size_t>(Biome::Count)> kBiomeColors = {{
    {  0,  80, 160},  // Ocean
    { 70, 130, 200},  // Lake
    {  0, 120,  30},  // TropicalRainforest
    { 60, 140,  50},  // TropicalSeasonalForest
    { 80, 160,  60},  // TemperateDeciduousForest
    { 30, 100,  60},  // TemperateRainforest
    { 20,  80,  50},  // BorealForest
    { 50, 110,  70},  // MontaneForest
    {180, 160,  50},  // TropicalSavanna
    {190, 180,  80},  // TemperateGrassland
    {150, 170, 100},  // AlpineGrassland
    {230, 190,  80},  // HotDesert
    {160, 140, 110},  // ColdDesert
    {200, 170, 110},  // SemiDesert
    {170, 150,  90},  // XericShrubland
    {150, 160, 140},  // ArcticTundra
    {130, 140, 130},  // AlpineTundra
    {240, 240, 250},  // PolarDesert
    { 60, 120, 100},  // TemperateWetland
    { 30,  80,  60},  // TropicalWetland
    {230, 210, 140},  // Beach
}};

// Hypsometric tint for elevation mode.
// Stops: deep ocean (-6000m) → shelf (-200m) → sea level (0m) → lowland → highland → peak
inline Rgb elevationColor(float elevMeters, float seaLevelMeters) {
    float e = elevMeters - seaLevelMeters;
    if (e < 0.0f) {
        // Ocean: deep blue → light blue
        float t = e / -6000.0f;
        if (t > 1.0f) t = 1.0f;
        auto r = static_cast<uint8_t>(0   + t * 0);
        auto g = static_cast<uint8_t>(80  + static_cast<int>((1.0f - t) * 80));
        auto b = static_cast<uint8_t>(160 + static_cast<int>((1.0f - t) * 80));
        return {r, g, b};
    }
    // Land: green → brown → white
    if (e < 500.0f) {
        float t = e / 500.0f;
        return {static_cast<uint8_t>(30  + static_cast<int>(t * 100)),
                static_cast<uint8_t>(120 + static_cast<int>(t * 20)),
                static_cast<uint8_t>(30)};
    }
    if (e < 2000.0f) {
        float t = (e - 500.0f) / 1500.0f;
        return {static_cast<uint8_t>(130 + static_cast<int>(t * 60)),
                static_cast<uint8_t>(140 - static_cast<int>(t * 60)),
                static_cast<uint8_t>(30)};
    }
    // >2000m: brown → white
    float t = (e - 2000.0f) / 3000.0f;
    if (t > 1.0f) t = 1.0f;
    auto v = static_cast<uint8_t>(190 + static_cast<int>(t * 65));
    return {v, v, v};
}

// Temperature: blue (cold, -50 C) → white (0 C) → red (hot, +50 C)
inline Rgb temperatureColor(float tempC) {
    float t = (tempC + 50.0f) / 100.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    if (t < 0.5f) {
        float s = t * 2.0f;
        return {static_cast<uint8_t>(s * 255), static_cast<uint8_t>(s * 255), 255};
    }
    float s = (t - 0.5f) * 2.0f;
    return {255, static_cast<uint8_t>(255 - s * 255), static_cast<uint8_t>(255 - s * 255)};
}

// Precipitation: brown (dry, 0) → blue (wet, 3000mm+)
inline Rgb precipitationColor(float precipMmYr) {
    float t = precipMmYr / 3000.0f;
    if (t > 1.0f) t = 1.0f;
    return {static_cast<uint8_t>(160 - static_cast<int>(t * 160)),
            static_cast<uint8_t>(100 + static_cast<int>(t * 100)),
            static_cast<uint8_t>(50  + static_cast<int>(t * 200))};
}

// Plate ID: distinct hue per plate via golden-ratio hue stepping
inline Rgb plateColor(uint8_t plateId) {
    // Golden ratio gives evenly-spaced hues regardless of plate count
    float hue = static_cast<float>(plateId) * 137.508f; // golden angle degrees
    // Simple HSV→RGB with S=0.8, V=0.9
    float h = hue - static_cast<float>(static_cast<int>(hue / 360.0f)) * 360.0f;
    float s = 0.8f;
    float v = 0.9f;
    int hi = static_cast<int>(h / 60.0f) % 6;
    float f = h / 60.0f - static_cast<float>(static_cast<int>(h / 60.0f));
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float tv = v * (1.0f - (1.0f - f) * s);
    float r{}, g{}, b{};
    switch (hi) {
        case 0: r = v;  g = tv; b = p;  break;
        case 1: r = q;  g = v;  b = p;  break;
        case 2: r = p;  g = v;  b = tv; break;
        case 3: r = p;  g = q;  b = v;  break;
        case 4: r = tv; g = p;  b = v;  break;
        default:r = v;  g = p;  b = q;  break;
    }
    return {static_cast<uint8_t>(r * 255), static_cast<uint8_t>(g * 255), static_cast<uint8_t>(b * 255)};
}

} // namespace worldgen
