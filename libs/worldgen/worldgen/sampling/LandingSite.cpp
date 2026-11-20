#include "worldgen/sampling/LandingSite.h"

#include "worldgen/data/GeneratedWorld.h"

#include <math/DeterministicMath.h>
#include <utils/Log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace worldgen {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRadiansPerDegree = kPi / 180.0;
constexpr double kDegreesPerRadian = 180.0 / kPi;

bool hasField(const GeneratedWorld& world, WorldField f) {
    return (world.validFields & static_cast<uint32_t>(f)) != 0;
}

bool isWaterTile(const GeneratedWorld& world, TileId t) {
    if ((world.data.flags[t] & (kFlagOcean | kFlagLake)) != 0) return true;
    return world.data.elevation[t] < world.seaLevelMeters;
}

// Coast = land tile with at least one OCEAN neighbor. Computed from the grid
// rather than kFlagCoast so it works on worlds saved before BiomeStage set the
// flag. Ocean-only (not lake) so it maps to "saltwater" in the signal.
bool hasOceanNeighbor(const GeneratedWorld& world, TileId t) {
    std::array<TileId, 6> nbrs{};
    uint32_t count = world.grid->neighbors(t, nbrs);
    for (uint32_t i = 0; i < count; ++i) {
        if ((world.data.flags[nbrs[i]] & kFlagOcean) != 0) return true;
        // Unflagged ocean fallback for worlds without flags written.
        if ((world.data.flags[nbrs[i]] & (kFlagOcean | kFlagLake)) == 0 &&
            world.data.elevation[nbrs[i]] < world.seaLevelMeters) {
            return true;
        }
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

Vec3d unitVectorOf(LatLon point) {
    const double lat = point.latDeg * kRadiansPerDegree;
    const double lon = point.lonDeg * kRadiansPerDegree;
    const double cosLat = foundation::det_math::cos(lat);
    return {cosLat * foundation::det_math::cos(lon), cosLat * foundation::det_math::sin(lon),
            foundation::det_math::sin(lat)};
}

double dot(const Vec3d& a, const Vec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

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
    const double cosArc = std::clamp(dot(from, to), -1.0, 1.0);
    const Vec3d across{to.x - cosArc * from.x, to.y - cosArc * from.y, to.z - cosArc * from.z};
    const double acrossLength = foundation::det_math::sqrt(dot(across, across));
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

    const Vec3d point = pointAt(std::max(0.0, inside - kLakeShoreInsetMeters / radius));
    return {foundation::det_math::asin(std::clamp(point.z, -1.0, 1.0)) * kDegreesPerRadian,
            foundation::det_math::atan2(point.y, point.x) * kDegreesPerRadian};
}

// Neighbours of `tile` that pass `keep`, nearest centre to the pick first, ties to the lowest TileId.
template <typename Keep>
std::vector<TileId> neighboursNearestFirst(const SphereGrid& grid, TileId tile, const Vec3d& pick, Keep keep) {
    std::array<TileId, 6> neighbours{};
    const uint32_t count = grid.neighbors(tile, neighbours);
    std::vector<std::pair<double, TileId>> ranked;
    for (uint32_t i = 0; i < count; ++i) {
        if (keep(neighbours[i])) ranked.emplace_back(-dot(grid.tileCenter(neighbours[i]), pick), neighbours[i]);
    }
    std::sort(ranked.begin(), ranked.end());
    std::vector<TileId> out;
    out.reserve(ranked.size());
    for (const auto& [distance, neighbour] : ranked) out.push_back(neighbour);
    return out;
}

constexpr uint32_t kStopCheckTileInterval = 65536;

enum class CandidateKind : uint8_t { River, Lake };

// A tile whose own resolution anchors on itself and that meets the rest of the bar on its own data.
struct Candidate {
    TileId        tile{kInvalidTile};
    CandidateKind kind{CandidateKind::River};
    uint8_t       biome{};
    bool          preferred{};
    int           habitabilityScore{};
};

bool ranksBefore(const Candidate& a, const Candidate& b) {
    if (a.preferred != b.preferred) return a.preferred;
    if (a.kind != b.kind) return a.kind < b.kind;
    if (a.habitabilityScore != b.habitabilityScore) return a.habitabilityScore > b.habitabilityScore;
    return a.tile < b.tile;
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
        : slots(static_cast<std::size_t>(binCount) * 2 * static_cast<std::size_t>(Biome::Count)) {}

    void offer(uint32_t bin, const Candidate& candidate) {
        Slot& slot = slots[(static_cast<std::size_t>(bin) * 2 + static_cast<std::size_t>(candidate.kind)) *
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
        case WaterClass::Lake:    score += 3; break;
        case WaterClass::Coastal: score += 1; break;
        case WaterClass::RainFed: break;
    }

    uint16_t rainMillimeters = 0;
    if (hasField(world, WorldField::Precipitation) && tile < world.data.precipitation.size()) {
        rainMillimeters = world.data.precipitation[tile];
    }
    if (rainMillimeters >= kAmpleRainfallMillimeters) {
        score += 2;
    } else if (rainMillimeters >= kModerateRainfallMillimeters) {
        score += 1;
    } else if (rainMillimeters < kDesertRainfallMillimeters && verifiedWater == WaterClass::RainFed) {
        score -= 1;
    }

    if (!assessment.livableTemperature) score -= brutal ? 2 : 1;
    if (!assessment.hasFood) score -= 1;
    if (!assessment.hasWood) score -= 1;
    if (ampleProvisions) score += 1;

    assessment.habitabilityScore = score;
    assessment.habitability = score >= 4   ? Habitability::Easy
                            : score >= 2   ? Habitability::Moderate
                            : score >= 0   ? Habitability::Hard
                                           : Habitability::Harsh;
    return assessment;
}

LandingSite resolveLandingSite(const std::shared_ptr<const GeneratedWorld>& world,
                               const BiomeProvisionTable& provisions, LatLon pick) {
    const GeneratedWorld& planet = *world;
    const SphereGrid& grid = *planet.grid;
    const TileId picked = grid.fromLatLon(pick.latDeg, pick.lonDeg);

    auto landAt = [&](LatLon anchor) -> std::optional<LandingSite> {
        const SpawnSite spawn = findWatersideSpawn(world, anchor.latDeg, anchor.lonDeg);
        if (spawn.water != SpawnWater::River && spawn.water != SpawnWater::Lake) return std::nullopt;
        const bool river = spawn.water == SpawnWater::River;
        const TileId tile = grid.fromLatLon(anchor.latDeg, anchor.lonDeg);
        return LandingSite{anchor, tile, river ? FreshWaterFeature::River : FreshWaterFeature::Lake, spawn,
                           assessLandingTile(planet, provisions, tile, river ? WaterClass::River : WaterClass::Lake)};
    };

    if (hasField(planet, WorldField::Flags) && !isWaterTile(planet, picked)) {
        const Vec3d pickDirection = unitVectorOf(pick);

        if ((planet.data.flags[picked] & kFlagRiver) != 0) {
            if (auto site = landAt(centreOf(grid, picked))) return *site;
        }
        const auto isLake = [&](TileId t) { return (planet.data.flags[t] & kFlagLake) != 0; };
        for (const TileId lake : neighboursNearestFirst(grid, picked, pickDirection, isLake)) {
            if (auto site = landAt(lakeAnchor(planet, picked, lake))) return *site;
        }
        const auto isLandRiver = [&](TileId t) {
            return (planet.data.flags[t] & kFlagRiver) != 0 && !isWaterTile(planet, t);
        };
        for (const TileId river : neighboursNearestFirst(grid, picked, pickDirection, isLandRiver)) {
            if (auto site = landAt(centreOf(grid, river))) return *site;
        }
    }

    const WaterClass water = hasOceanNeighbor(planet, picked) ? WaterClass::Coastal : WaterClass::RainFed;
    return LandingSite{pick, picked, FreshWaterFeature::None, findWatersideSpawn(world, pick.latDeg, pick.lonDeg),
                       assessLandingTile(planet, provisions, picked, water)};
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
                                               [&](const Vec3d& site) { return dot(centre, site) > separationCosine; });
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
                resolved[index] = resolveLandingSite(world, provisions, centreOf(grid, candidate.tile));
                const bool passes = resolved[index].recommended() && resolved[index].tile == candidate.tile;
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
