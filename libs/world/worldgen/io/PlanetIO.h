#pragma once

// Binary planet file format ("WSPL"), version 1.
//
// All multi-byte values are little-endian. Every target platform is
// little-endian; this is asserted at compile time in PlanetIO.cpp.
// All fields are written individually (never as raw struct memcpy) so
// compiler-inserted struct padding can never reach disk.
//
// Layout (offsets in bytes, sequential):
//   magic               char[4]   "WSPL"
//   formatVersion       uint32    = 1
//   --- PlanetParams ---
//   starMass            float64
//   starRadius          float64
//   starTemperature     float64
//   starAge             float64
//   planetRadius        float64
//   planetMass          float64
//   rotationRate        float64
//   tectonicPlateCount  int32
//   waterAmount         float64
//   atmosphereStrength  float64
//   planetAge           float64
//   semiMajorAxis       float64
//   eccentricity        float64
//   seed                uint64
//   gridSubdivision     uint32    (n; tile count = 10*n*n + 2)
//   --- scalars ---
//   seaLevelMeters      float32
//   validFields         uint32    (WorldField bits)
//   worldHash           uint64    (FNV-1a over valid arrays, see below)
//   --- WorldSummary ---
//   landFraction        float32
//   meanTemperatureC    float32
//   riverTileCount      uint32
//   habitability        float32
//   biomeHistogramCount uint32    (= Biome::Count; mismatch rejects the file)
//   biomeHistogram      uint32 x biomeHistogramCount
//   --- plates ---
//   plateCount          uint32
//   per plate:
//     eulerPole.x/y/z   float64 x 3
//     angularSpeed      float32
//     isContinental     uint8     (0 or 1)
//   --- WorldData ---
//   tileCount           uint32    (must equal 10 * gridSubdivision^2 + 2)
//   per WorldField, ascending bit order, ONLY if its validFields bit is set:
//     contiguous array of tileCount elements (element types from WorldData.h:
//     elevation f32, temperatureMean i16, temperatureRange i16,
//     precipitation u16, windDir u8, windSpeed u8, plateId u8,
//     boundaryType u8, boundaryDistance u16, biome u8, flags u8,
//     waterDepth u16, flowAccum f32, downhill u8, snowCover u8)
//
// Not serialized, rebuilt on load:
//   DerivedPlanetValues  — recomputed via derive(params)
//   SphereGrid           — reconstructed from gridSubdivision
//
// Integrity: loadPlanet recomputes the FNV-1a hash over the loaded valid
// arrays (same field order and algorithm as PlanetGenerator's worldHash)
// and rejects the file if it differs from the stored worldHash.

#include "worldgen/data/GeneratedWorld.h"

#include <filesystem>
#include <memory>

namespace worldgen {

// Serialize a generated world to a binary planet file.
// Writes to a temp file in the same directory, then renames over the target,
// so a crash mid-write never leaves a truncated file at `path`.
// Returns false (with LOG_ERROR) on any I/O failure.
bool savePlanet(const GeneratedWorld& world, const std::filesystem::path& path);

// Load a planet file written by savePlanet. Returns nullptr (with LOG_ERROR)
// on missing file, bad magic, unsupported version, structural mismatch,
// truncation, or hash mismatch.
std::shared_ptr<const GeneratedWorld> loadPlanet(const std::filesystem::path& path);

} // namespace worldgen
