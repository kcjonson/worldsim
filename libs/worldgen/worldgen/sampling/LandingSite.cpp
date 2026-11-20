#include "worldgen/sampling/LandingSite.h"

#include "worldgen/data/GeneratedWorld.h"

#include <math/DeterministicMath.h>
#include <utils/Log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <vector>

namespace worldgen {

namespace {

constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;

// The habitability score's terms (assessLandingTile) and the least score of each bucket.
constexpr int kFreshWaterScore = 3;
constexpr int kCoastalScore = 1;
constexpr int kAmpleRainfallScore = 2;
constexpr int kModerateRainfallScore = 1;
constexpr int kDesertRainfallScore = -1;
constexpr int kUnlivableTemperatureScore = -1;
constexpr int kBrutalTemperatureScore = -2;
constexpr int kNoFoodScore = -1;
constexpr int kNoWoodScore = -1;
constexpr int kAmpleProvisionsScore = 1;
constexpr int kEasyScoreMinimum = 4;
constexpr int kModerateScoreMinimum = 2;
constexpr int kHardScoreMinimum = 0;

bool hasField(const GeneratedWorld& world, WorldField f) {
    return (world.validFields & static_cast<uint32_t>(f)) != 0;
}

// A tile below sea level is water whatever its flags say.
bool belowSeaLevel(const GeneratedWorld& world, TileId tile) {
    return hasField(world, WorldField::Elevation) && world.data.elevation[tile] < world.seaLevelMeters;
}

// Coast = land tile with at least one OCEAN neighbor. Computed from the grid
// rather than kFlagCoast so it works on worlds saved before BiomeStage set the
// flag. Ocean-only (not lake) so it maps to "saltwater" in the signal.
bool hasOceanNeighbor(const GeneratedWorld& world, TileId t) {
    std::array<TileId, 6> nbrs{};
    uint32_t count = world.grid->neighbors(t, nbrs);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t flags = world.data.flags[nbrs[i]];
        if ((flags & kFlagOcean) != 0) return true;
        if ((flags & kFlagLake) == 0 && belowSeaLevel(world, nbrs[i])) return true;
    }
    return false;
}

// True if the tile itself or any neighbor carries the given flag.
bool tileOrNeighborHasFlag(const GeneratedWorld& world, TileId t, uint8_t flag) {
    if ((world.data.flags[t] & flag) != 0) return true;
    std::array<TileId, 6> nbrs{};
    uint32_t count = world.grid->neighbors(t, nbrs);
    for (uint32_t i = 0; i < count; ++i) {
        if ((world.data.flags[nbrs[i]] & flag) != 0) return true;
    }
    return false;
}

double dotProduct(const Vec3d& first, const Vec3d& second) {
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

// Through SphereGrid::latLonOf for a tile centre, so an anchor on a tile's centre is bit-identical
// to the lat/lon every other caller has for that tile.
LatLon centreOf(const SphereGrid& grid, TileId tile) {
    LatLon centre;
    grid.latLonOf(tile, centre.latDeg, centre.lonDeg);
    return centre;
}

// On the great circle from the land tile's centre toward the lake tile's centre, kLakeShoreInsetMeters
// short of where the grid stops calling the point the land tile (the 2D lake shore). The grid decides
// containment in its lattice chart, so that edge is found by bisection on its own lookup, not taken
// as the centres' midpoint.
LatLon lakeAnchor(const GeneratedWorld& world, TileId land, TileId lake) {
    const SphereGrid& grid = *world.grid;
    const Vec3d from = grid.tileCenter(land);
    const Vec3d to = grid.tileCenter(lake);
    const double cosArc = std::clamp(dotProduct(from, to), -1.0, 1.0);
    const Vec3d across{to.x - cosArc * from.x, to.y - cosArc * from.y, to.z - cosArc * from.z};
    const double acrossLength = foundation::det_math::sqrt(dotProduct(across, across));
    const double arc = foundation::det_math::atan2(acrossLength, cosArc);
    const double radius = world.derived.planetRadiusMeters;

    const auto pointAt = [&](double angle) {
        const double along = foundation::det_math::cos(angle);
        const double side = foundation::det_math::sin(angle) / acrossLength;
        return Vec3d{along * from.x + side * across.x, along * from.y + side * across.y,
                     along * from.z + side * across.z};
    };

    double inside = 0.0;
    double outside = arc;
    while ((outside - inside) * radius > kLakeEdgeToleranceMeters) {
        const double middle = 0.5 * (inside + outside);
        if (grid.fromUnitVector(pointAt(middle)) == land) {
            inside = middle;
        } else {
            outside = middle;
        }
    }

    LatLon anchor;
    SphereGrid::latLonFromUnitVector(pointAt(std::max(0.0, inside - kLakeShoreInsetMeters / radius)), anchor.latDeg,
                                     anchor.lonDeg);
    return anchor;
}

// Neighbours of `tile` that pass `keep`, nearest centre to the pick first, ties to the lowest TileId.
template <typename Keep>
std::vector<TileId> neighboursNearestFirst(const SphereGrid& grid, TileId tile, const Vec3d& pick, Keep keep) {
    std::array<TileId, 6> neighbours{};
    const uint32_t count = grid.neighbors(tile, neighbours);
    std::vector<std::pair<double, TileId>> ranked;
    for (uint32_t i = 0; i < count; ++i) {
        if (keep(neighbours[i])) ranked.emplace_back(-dotProduct(grid.tileCenter(neighbours[i]), pick), neighbours[i]);
    }
    std::sort(ranked.begin(), ranked.end());
    std::vector<TileId> out;
    out.reserve(ranked.size());
    for (const auto& [distance, neighbour] : ranked) out.push_back(neighbour);
    return out;
}

// The landing at an anchor, when its spawn search finds fresh water and a clear drop.
std::optional<LandingSite> landAt(const std::shared_ptr<const GeneratedWorld>& world, const BiomeProvisionTable& provisions,
                                  LatLon anchor) {
    const SpawnSite spawn = findWatersideSpawn(world, anchor.latDeg, anchor.lonDeg);
    if (!spawn.clear || (spawn.water != SpawnWater::River && spawn.water != SpawnWater::Lake)) return std::nullopt;
    const bool river = spawn.water == SpawnWater::River;
    const TileId tile = world->grid->fromLatLon(anchor.latDeg, anchor.lonDeg);
    return LandingSite{anchor, tile, river ? FreshWaterFeature::River : FreshWaterFeature::Lake, spawn,
                       assessLandingTile(*world, provisions, tile, river ? WaterClass::River : WaterClass::Lake)};
}

// The anchors inside a land tile: its centre when it is a river tile, then a lake anchor toward each lake
// neighbour, nearest to the pick first. Every landing found here lies on `tile`.
std::optional<LandingSite> landOnTile(const std::shared_ptr<const GeneratedWorld>& world,
                                      const BiomeProvisionTable& provisions, TileId tile, const Vec3d& pickDirection) {
    const GeneratedWorld& planet = *world;
    const SphereGrid& grid = *planet.grid;
    if ((planet.data.flags[tile] & kFlagRiver) != 0) {
        if (auto site = landAt(world, provisions, centreOf(grid, tile))) return site;
    }
    const auto isLake = [&](TileId neighbour) { return (planet.data.flags[neighbour] & kFlagLake) != 0; };
    for (const TileId lake : neighboursNearestFirst(grid, tile, pickDirection, isLake)) {
        if (auto site = landAt(world, provisions, lakeAnchor(planet, tile, lake))) return site;
    }
    return std::nullopt;
}

constexpr uint32_t kStopCheckTileInterval = 65536;

enum class CandidateKind : uint8_t { River, Lake, Count };
constexpr std::size_t kCandidateKindCount = static_cast<std::size_t>(CandidateKind::Count);

// A tile whose own resolution anchors on itself and that meets the rest of the bar on its own data.
struct Candidate {
    TileId        tile{kInvalidTile};
    CandidateKind kind{CandidateKind::River};
    uint8_t       biome{};
    bool          preferred{};
    int           habitabilityScore{};
};

bool ranksBefore(const Candidate& first, const Candidate& second) {
    if (first.preferred != second.preferred) return first.preferred;
    if (first.kind != second.kind) return first.kind < second.kind;
    if (first.habitabilityScore != second.habitabilityScore) return first.habitabilityScore > second.habitabilityScore;
    return first.tile < second.tile;
}

bool bordersLake(const GeneratedWorld& world, TileId tile) {
    std::array<TileId, 6> neighbours{};
    const uint32_t count = world.grid->neighbors(tile, neighbours);
    for (uint32_t i = 0; i < count; ++i) {
        if ((world.data.flags[neighbours[i]] & kFlagLake) != 0) return true;
    }
    return false;
}

// The best kCandidatesPerSlot of each (bin, kind, biome) slot. A total order with no ties, so what is
// kept does not depend on the order candidates arrive in.
class CandidateSlots {
  public:
    explicit CandidateSlots(uint32_t binCount)
        : slots(static_cast<std::size_t>(binCount) * kCandidateKindCount * static_cast<std::size_t>(Biome::Count)) {}

    void offer(uint32_t bin, const Candidate& candidate) {
        Slot& slot = slots[(static_cast<std::size_t>(bin) * kCandidateKindCount + static_cast<std::size_t>(candidate.kind)) *
                               static_cast<std::size_t>(Biome::Count) +
                           candidate.biome];
        if (slot.count < kCandidatesPerSlot) {
            slot.kept[slot.count++] = candidate;
            return;
        }
        Candidate* worst = &slot.kept[0];
        for (Candidate& kept : slot.kept) {
            if (ranksBefore(*worst, kept)) worst = &kept;
        }
        if (ranksBefore(candidate, *worst)) *worst = candidate;
    }

    [[nodiscard]] std::vector<Candidate> ranked() const {
        std::vector<Candidate> out;
        for (const Slot& slot : slots) out.insert(out.end(), slot.kept.begin(), slot.kept.begin() + slot.count);
        std::sort(out.begin(), out.end(), ranksBefore);
        return out;
    }

  private:
    struct Slot {
        std::array<Candidate, kCandidatesPerSlot> kept{};
        std::size_t                               count{};
    };
    std::vector<Slot> slots;
};
} // namespace

bool isWaterTile(const GeneratedWorld& world, TileId tile) {
    if (hasField(world, WorldField::Flags) && (world.data.flags[tile] & (kFlagOcean | kFlagLake)) != 0) return true;
    return belowSeaLevel(world, tile);
}

WaterClass classifyWater(const GeneratedWorld& world, TileId tile) {
    if (!world.grid) {
        LOG_ERROR(World, "classifyWater: world has no grid");
        return WaterClass::RainFed;
    }

    // River and lake flags only exist when Flags were written. Without them we
    // can only distinguish coast (from elevation) vs rain-fed.
    if (hasField(world, WorldField::Flags)) {
        if (tileOrNeighborHasFlag(world, tile, kFlagRiver)) return WaterClass::River;
        if (tileOrNeighborHasFlag(world, tile, kFlagLake))  return WaterClass::Lake;
    }
    if (hasOceanNeighbor(world, tile)) return WaterClass::Coastal;
    return WaterClass::RainFed;
}

const char* waterClassToString(WaterClass w) {
    switch (w) {
        case WaterClass::River:   return "River";
        case WaterClass::Lake:    return "Lake";
        case WaterClass::Coastal: return "Coastal";
        case WaterClass::RainFed: return "Rain-fed";
    }
    return "Unknown";
}

const char* habitabilityToString(Habitability h) {
    switch (h) {
        case Habitability::Easy:     return "Easy";
        case Habitability::Moderate: return "Moderate";
        case Habitability::Hard:     return "Hard";
        case Habitability::Harsh:    return "Harsh";
    }
    return "Unknown";
}

TileAssessment assessLandingTile(const GeneratedWorld& world, const BiomeProvisionTable& provisions,
                                 TileId tile, WaterClass verifiedWater) {
    TileAssessment assessment;
    assessment.water = verifiedWater;

    bool ampleProvisions = false;
    if (hasField(world, WorldField::Biome) && tile < world.data.biome.size()) {
        const std::size_t biome = world.data.biome[tile];
        if (biome < provisions.size()) {
            const BiomeProvisions& offered = provisions[biome];
            assessment.hasFood = offered.foodSourcesPerSquareKilometer >= kMinimumFoodSourcesPerSquareKilometer;
            assessment.hasWood = offered.woodSourcesPerSquareKilometer >= kMinimumWoodSourcesPerSquareKilometer;
            ampleProvisions = offered.foodSourcesPerSquareKilometer >= kAmpleFoodSourcesPerSquareKilometer &&
                              offered.woodSourcesPerSquareKilometer >= kAmpleWoodSourcesPerSquareKilometer;
        }
    }

    const bool haveMean = hasField(world, WorldField::TemperatureMean) && tile < world.data.temperatureMean.size();
    const bool haveRange = hasField(world, WorldField::TemperatureRange) && tile < world.data.temperatureRange.size();
    bool brutal = false;
    if (haveMean) {
        const double mean = world.data.temperatureMean[tile] / 10.0;
        const double halfAmplitude = haveRange ? world.data.temperatureRange[tile] / 10.0 : 0.0;
        assessment.livableTemperature = haveRange && mean >= kLivableMeanMinimumCelsius &&
                                        mean <= kLivableMeanMaximumCelsius &&
                                        mean - halfAmplitude >= kLivableColdestSeasonCelsius &&
                                        mean + halfAmplitude <= kLivableHottestSeasonCelsius;
        brutal = mean < kBrutalMeanMinimumCelsius || mean > kBrutalMeanMaximumCelsius ||
                 mean - halfAmplitude < kBrutalColdestSeasonCelsius ||
                 mean + halfAmplitude > kBrutalHottestSeasonCelsius;
    }

    int score = 0;
    switch (verifiedWater) {
        case WaterClass::River:
        case WaterClass::Lake:    score += kFreshWaterScore; break;
        case WaterClass::Coastal: score += kCoastalScore; break;
        case WaterClass::RainFed: break;
    }

    uint16_t rainMillimeters = 0;
    if (hasField(world, WorldField::Precipitation) && tile < world.data.precipitation.size()) {
        rainMillimeters = world.data.precipitation[tile];
    }
    if (rainMillimeters >= kAmpleRainfallMillimeters) {
        score += kAmpleRainfallScore;
    } else if (rainMillimeters >= kModerateRainfallMillimeters) {
        score += kModerateRainfallScore;
    } else if (rainMillimeters < kDesertRainfallMillimeters && verifiedWater == WaterClass::RainFed) {
        score += kDesertRainfallScore;
    }

    if (!assessment.livableTemperature) score += brutal ? kBrutalTemperatureScore : kUnlivableTemperatureScore;
    if (!assessment.hasFood) score += kNoFoodScore;
    if (!assessment.hasWood) score += kNoWoodScore;
    if (ampleProvisions) score += kAmpleProvisionsScore;

    assessment.habitabilityScore = score;
    assessment.habitability = score >= kEasyScoreMinimum     ? Habitability::Easy
                            : score >= kModerateScoreMinimum ? Habitability::Moderate
                            : score >= kHardScoreMinimum     ? Habitability::Hard
                                                             : Habitability::Harsh;
    return assessment;
}

LandingSite resolveLandingSite(const std::shared_ptr<const GeneratedWorld>& world,
                               const BiomeProvisionTable& provisions, LatLon pick) {
    const GeneratedWorld& planet = *world;
    const SphereGrid& grid = *planet.grid;
    const TileId picked = grid.fromLatLon(pick.latDeg, pick.lonDeg);

    if (hasField(planet, WorldField::Flags) && !isWaterTile(planet, picked)) {
        const Vec3d pickDirection = SphereGrid::unitVectorFromLatLon(pick.latDeg, pick.lonDeg);
        if (auto site = landOnTile(world, provisions, picked, pickDirection)) return *site;
        const auto isLandRiver = [&](TileId neighbour) {
            return (planet.data.flags[neighbour] & kFlagRiver) != 0 && !isWaterTile(planet, neighbour);
        };
        for (const TileId river : neighboursNearestFirst(grid, picked, pickDirection, isLandRiver)) {
            if (auto site = landAt(world, provisions, centreOf(grid, river))) return *site;
        }
    }

    const WaterClass water = hasOceanNeighbor(planet, picked) ? WaterClass::Coastal : WaterClass::RainFed;
    return LandingSite{pick, picked, FreshWaterFeature::None, findWatersideSpawn(world, pick.latDeg, pick.lonDeg),
                       assessLandingTile(planet, provisions, picked, water)};
}

SuggestedSiteQuery quickStartSiteQuery() {
    SuggestedSiteQuery query;
    query.preferredBiome = Biome::TemperateDeciduousForest;
    query.maximumSites = 1;
    return query;
}

std::vector<LandingSite> findSuggestedLandingSites(const std::shared_ptr<const GeneratedWorld>& world,
                                                   const BiomeProvisionTable& provisions,
                                                   const SuggestedSiteQuery& query) {
    const GeneratedWorld& planet = *world;
    constexpr uint32_t kRequiredFields = static_cast<uint32_t>(WorldField::Elevation) |
                                         static_cast<uint32_t>(WorldField::Biome) |
                                         static_cast<uint32_t>(WorldField::Flags);
    if (!planet.grid || (planet.validFields & kRequiredFields) != kRequiredFields || query.maximumSites == 0) {
        return {};
    }
    const auto checkpoint = [&query] {
        if (query.checkpoint) query.checkpoint();
        return !query.stopToken.stop_requested();
    };

    const SphereGrid& grid = *planet.grid;
    const SphereGrid  bins(kSiteBinSubdivision);
    CandidateSlots    slots(bins.tileCount());
    const uint32_t    tileCount = grid.tileCount();
    for (TileId tile = 0; tile < tileCount; ++tile) {
        if (tile % kStopCheckTileInterval == 0 && !checkpoint()) return {};
        if (isWaterTile(planet, tile)) continue;
        const uint8_t biome = planet.data.biome[tile];
        if (biome >= provisions.size() ||
            provisions[biome].foodSourcesPerSquareKilometer < kMinimumFoodSourcesPerSquareKilometer ||
            provisions[biome].woodSourcesPerSquareKilometer < kMinimumWoodSourcesPerSquareKilometer) {
            continue;
        }
        const LatLon centre = centreOf(grid, tile);
        if (std::abs(centre.latDeg) > kMaximumSuggestedLatitudeDegrees) continue;
        const TileAssessment assessment = assessLandingTile(planet, provisions, tile, WaterClass::River);
        if (!assessment.livableTemperature) continue;

        CandidateKind kind = CandidateKind::River;
        if ((planet.data.flags[tile] & kFlagRiver) == 0) {
            if (!bordersLake(planet, tile)) continue;
            kind = CandidateKind::Lake;
        }
        const bool preferred = query.preferredBiome && static_cast<uint8_t>(*query.preferredBiome) == biome;
        slots.offer(bins.fromUnitVector(grid.tileCenter(tile)), {tile, kind, biome, preferred, assessment.habitabilityScore});
    }

    const std::vector<Candidate> candidates = slots.ranked();

    enum class Verification : uint8_t { Unchecked, Passed, Failed };
    std::vector<Verification> verification(candidates.size(), Verification::Unchecked);
    std::vector<LandingSite>  resolved(candidates.size());
    std::vector<bool>         taken(candidates.size(), false);

    std::vector<LandingSite> sites;
    std::vector<Vec3d>       siteCentres;
    std::vector<uint8_t>     siteBiomes;
    const double             separationCosine =
        foundation::det_math::cos(kMinimumSiteSeparationDegrees * kRadiansPerDegree);
    const auto countOf = [&sites](FreshWaterFeature feature) {
        return static_cast<std::size_t>(
            std::count_if(sites.begin(), sites.end(), [feature](const LandingSite& site) { return site.feature == feature; }));
    };
    const auto kindOf = [](CandidateKind kind) {
        return kind == CandidateKind::River ? FreshWaterFeature::River : FreshWaterFeature::Lake;
    };

    for (std::size_t round = 1; round <= kSuggestedSiteCount + 1; ++round) {
        const bool finalPass = round > kSuggestedSiteCount;
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            if (sites.size() >= query.maximumSites) return sites;
            if (taken[index] || verification[index] == Verification::Failed) continue;
            const Candidate& candidate = candidates[index];

            const Vec3d centre = grid.tileCenter(candidate.tile);
            const bool  tooClose = std::any_of(siteCentres.begin(), siteCentres.end(),
                                               [&](const Vec3d& site) { return dotProduct(centre, site) > separationCosine; });
            if (tooClose) continue;
            if (!finalPass) {
                const auto sameBiome = static_cast<std::size_t>(std::count(siteBiomes.begin(), siteBiomes.end(), candidate.biome));
                if (sameBiome >= round) continue;
                const FreshWaterFeature expected =
                    verification[index] == Verification::Passed ? resolved[index].feature : kindOf(candidate.kind);
                if (countOf(expected) >= kMaximumSitesOfOneKind) continue;
            }

            if (verification[index] == Verification::Unchecked) {
                if (!checkpoint()) return sites;
                // The candidate's own anchors only: resolving its centre would go on to its neighbours, whose
                // landings lie on another tile and are never this candidate's site.
                const LatLon                     pick = centreOf(grid, candidate.tile);
                const std::optional<LandingSite> landing = landOnTile(
                    world, provisions, candidate.tile, SphereGrid::unitVectorFromLatLon(pick.latDeg, pick.lonDeg));
                const bool passes = landing && landing->recommended() && landing->tile == candidate.tile;
                if (landing) resolved[index] = *landing;
                verification[index] = passes ? Verification::Passed : Verification::Failed;
                if (!passes) continue;
                if (!finalPass && countOf(resolved[index].feature) >= kMaximumSitesOfOneKind) continue;
            }

            taken[index] = true;
            sites.push_back(resolved[index]);
            siteCentres.push_back(centre);
            siteBiomes.push_back(candidate.biome);
        }
    }
    return sites;
}

} // namespace worldgen
