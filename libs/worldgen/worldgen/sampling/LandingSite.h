#pragma once

// What makes a good place to start a colony, and how a pick on the globe becomes the point the crew
// lands on. docs/technical/landing-sites.md is the contract.

#include "worldgen/data/Biome.h"
#include "worldgen/grid/SphereGrid.h" // TileId
#include "worldgen/sampling/SpawnSite.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>

namespace worldgen {

struct GeneratedWorld;

// What a biome's flora offers a colony, read off the flora definitions by the caller (worldgen
// can't see them): food and wood sources a square kilometer.
struct BiomeProvisions {
    float foodSourcesPerSquareKilometer{};
    float woodSourcesPerSquareKilometer{};
};
using BiomeProvisionTable = std::array<BiomeProvisions, static_cast<std::size_t>(Biome::Count)>;

struct LatLon {
    double latDeg{};
    double lonDeg{};
};

inline constexpr double kLivableMeanMinimumCelsius = 0.0;
inline constexpr double kLivableMeanMaximumCelsius = 28.0;
inline constexpr double kLivableColdestSeasonCelsius = -15.0;
inline constexpr double kLivableHottestSeasonCelsius = 38.0;
inline constexpr double kBrutalMeanMinimumCelsius = -10.0;
inline constexpr double kBrutalMeanMaximumCelsius = 35.0;
inline constexpr double kBrutalColdestSeasonCelsius = -30.0;
inline constexpr double kBrutalHottestSeasonCelsius = 45.0;
inline constexpr uint16_t kAmpleRainfallMillimeters = 600;
inline constexpr uint16_t kModerateRainfallMillimeters = 250;
inline constexpr uint16_t kDesertRainfallMillimeters = 100;
inline constexpr float kMinimumFoodSourcesPerSquareKilometer = 100.0f;
inline constexpr float kMinimumWoodSourcesPerSquareKilometer = 100.0f;
inline constexpr float kAmpleFoodSourcesPerSquareKilometer = 700.0f;
inline constexpr float kAmpleWoodSourcesPerSquareKilometer = 1000.0f;
inline constexpr double kLakeShoreInsetMeters = 20.0;
inline constexpr double kLakeEdgeToleranceMeters = 0.5;
inline constexpr std::size_t kSuggestedSiteCount = 10;
inline constexpr double kMinimumSiteSeparationDegrees = 15.0;
inline constexpr std::size_t kMaximumSitesOfOneKind = 7;
inline constexpr uint32_t kSiteBinSubdivision = 8;
inline constexpr std::size_t kCandidatesPerSlot = 2;
inline constexpr double kMaximumSuggestedLatitudeDegrees = 45.0;

// A tile's water situation. River and Lake are fresh, Coastal is salt, RainFed has no surface water.
enum class WaterClass : uint8_t {
    River,
    Lake,
    Coastal,
    RainFed,
};

inline constexpr bool isFreshwater(WaterClass w) {
    return w == WaterClass::River || w == WaterClass::Lake;
}

// The tile-level screen: where to look for fresh water (kFlagRiver or kFlagLake on the tile or a
// neighbour), else Coastal on an ocean neighbour, else RainFed. Tile flags promise water somewhere
// in a 3.5 km tile; only resolveLandingSite turns that into a verified class.
WaterClass classifyWater(const GeneratedWorld& world, TileId tile);

// Habitability rating, coarsest-to-harshest. A guide for the player, not a sim.
enum class Habitability : uint8_t {
    Easy,
    Moderate,
    Hard,
    Harsh,
};

const char* habitabilityToString(Habitability h);
const char* waterClassToString(WaterClass w);

struct TileAssessment {
    WaterClass   water{WaterClass::RainFed}; // verified: River or Lake only when the resolution found it
    bool         hasFood{};
    bool         hasWood{};
    bool         livableTemperature{};
    int          habitabilityScore{}; // the sum habitability is bucketed from
    Habitability habitability{Habitability::Harsh};

    [[nodiscard]] bool meetsBar() const { return isFreshwater(water) && hasFood && hasWood && livableTemperature; }
};

// The one bar for a good start: fresh water, food, wood, and a livable temperature (mean, coldest
// season, and hottest season). verifiedWater is what the resolution found, never the tile flags.
[[nodiscard]] TileAssessment assessLandingTile(const GeneratedWorld& world, const BiomeProvisionTable& provisions,
                                               TileId tile, WaterClass verifiedWater);

enum class FreshWaterFeature : uint8_t { None, River, Lake };

struct LandingSite {
    LatLon            origin;             // the 2D world origin
    TileId            tile{kInvalidTile}; // tile containing origin: what the pane describes
    FreshWaterFeature feature{FreshWaterFeature::None};
    SpawnSite         spawn;              // the drop, meters from origin
    TileAssessment    assessment;

    [[nodiscard]] bool recommended() const { return feature != FreshWaterFeature::None && assessment.meetsBar(); }
};

// Every pick, a marker or a free click, becomes a landing here. The picked tile and its neighbours
// are the area a pick names; the origin moves to the first anchor in it whose spawn search lands
// beside a river or a lake (the picked land river tile's centre, then a lake anchor toward each lake
// neighbour, then each land river neighbour's centre), else stays at the pick. Idempotent: resolving
// a landing's origin returns the same landing.
[[nodiscard]] LandingSite resolveLandingSite(const std::shared_ptr<const GeneratedWorld>& world,
                                             const BiomeProvisionTable& provisions, LatLon pick);

struct SuggestedSiteQuery {
    std::optional<Biome> preferredBiome;
    std::size_t          maximumSites{kSuggestedSiteCount};
    std::stop_token      stopToken;
    // Called on the finder's thread wherever it checks stopToken, so the caller can serve other work
    // (a player's pick) there between the finder's steps.
    std::function<void()> checkpoint;
};

// Up to maximumSites good places to start a colony, best first, each one a resolved landing that is
// recommended() and lies on the tile it was found on: river tiles and tiles beside a lake that meet the
// bar on their own data within kMaximumSuggestedLatitudeDegrees of the equator, ranked (preferred biome,
// river before lake, higher habitabilityScore, lowest TileId), kept per (coarse bin, kind, biome) slot, and
// taken in rounds that keep sites kMinimumSiteSeparationDegrees apart and vary their biomes and kinds.
// The same world always gives the same list. Returns what it has when stopToken is set.
[[nodiscard]] std::vector<LandingSite> findSuggestedLandingSites(const std::shared_ptr<const GeneratedWorld>& world,
                                                                 const BiomeProvisionTable& provisions,
                                                                 const SuggestedSiteQuery& query);

} // namespace worldgen
