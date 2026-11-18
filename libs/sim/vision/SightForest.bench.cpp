// Step 1a of sight-softening.md (12, 13): the sight shape of 5 built from placed rings as nav holds
// them, and the seen record of 6 fed from it, headless, with no wall bodies and no rendering.
//
// BM_SightForestBuild builds one observer's shape at viewpoints in each stand: microseconds a
// build, rings in sight, opaque bodies in reach, vertices out, what convexity takes from the
// slope-limited ranges with and without the slope limit, and the depth the limit gives up.
//
// BM_SightForestWalk drives the seen record (vision::SeenArea) as PlayerVisibilitySystem does, with
// the bench's own stand in place of nav and vision: colonists walk the landing's forest
// as BM_PlayerVisibilityWalk walks open ground (the landing polygon first, 2 m/s, 20 m/s at 10x,
// courses turning up to 60 degrees every 2 to 15 s, turned back past 400 m), stepping round every
// placed ring and keeping their course. Vision builds every 5th tick for each colonist who moved a
// rebuild step since his last build: the open path, a leg, or the polygon path (5.1, 6.1). Every
// tick each colonist on a leg passes his position and his build's ranges (setOpenLeg), and a leg ends
// by the open leg's rules, 6.2's spread past softLegToleranceMm, or kSoftLegMostVertices, the next
// leg holding the shape drawn that frame (6.2, 6.3). On the polygon path each build is cut to its rim
// and queued with its walk, and the sweep builds its points' shapes with the bodies (6.4). After the
// walk every leg closes and the record runs until nothing waits.

#include "SeenArea.h"

#include "SightFeed.h"

#include <visibility/ForestDepth.h>
#include <visibility/ForestShape.h>
#include <visibility/SightClip.h>
#include <visibility/SightPolygon.h>
#include <visibility/Visibility.h>
#include <world/generation/GroveField.h>

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <numeric>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

	using geometry::Vec2i64;
	using vision::SeenArea;
	using vision::SightRanges;
	using Origin = vision::SeenArea::Origin;

	constexpr std::int64_t	kSightMm = 30000;
	constexpr std::int64_t	kStepMm = 500;
	constexpr std::int64_t	kOccluderBucketMm = 16000;
	constexpr std::int64_t	kPlacedOpaqueLeastDiameterMm = 1400;
	constexpr std::int64_t	kRimEaseMm = 400;
	constexpr std::int64_t	kLandingMm = 150000;
	constexpr int			kVisionInterval = 5;
	constexpr int			kTicks = 60 * 60 * 10;
	constexpr int			kTicksAMinute = 60 * 60;
	constexpr double		kTick = 1.0 / 60.0;
	constexpr double		kWalkMps = 2.0;
	constexpr double		kTurnBackMeters = 400.0;
	// How far a colonist keeps to a heading taken round a ring before trying his course again.
	constexpr double		kDetourMm = 1000.0;
	constexpr std::uint64_t kWorldSeed = 424242;

	const geometry::ForestTunables kTunables{};

	// --- Placed rings, as nav holds them -----------------------------------------------------

	struct PlacedRing {
		std::vector<Vec2i64> outline; // counter-clockwise
		Vec2i64				 low;
		Vec2i64				 high;
		geometry::ForestBody body;
		bool				 opaque = false;
	};

	// A ring nav holds for a Rect collider, padded, at `angle` (trees take none).
	PlacedRing ringOf(Vec2i64 centre, std::int64_t widthMm, std::int64_t depthMm, double angle) {
		PlacedRing	 ring;
		const double cosine = std::cos(angle);
		const double sine = std::sin(angle);
		for (const auto& [across, along] : std::array<std::array<double, 2>, 4>{{{-1.0, -1.0}, {1.0, -1.0}, {1.0, 1.0}, {-1.0, 1.0}}}) {
			const double x = across * static_cast<double>(widthMm) / 2.0;
			const double y = along * static_cast<double>(depthMm) / 2.0;
			ring.outline.push_back(centre + Vec2i64{std::llround(x * cosine - y * sine), std::llround(x * sine + y * cosine)});
		}
		ring.low = ring.outline.front();
		ring.high = ring.outline.front();
		double		 perimeter = 0.0;
		std::int64_t mostSquared = 0;
		for (std::size_t k = 0; k < ring.outline.size(); ++k) {
			const Vec2i64& point = ring.outline[k];
			ring.low = {std::min(ring.low.x, point.x), std::min(ring.low.y, point.y)};
			ring.high = {std::max(ring.high.x, point.x), std::max(ring.high.y, point.y)};
			const Vec2i64 side = ring.outline[(k + 1) % ring.outline.size()] - point;
			perimeter += std::sqrt(static_cast<double>(side.x * side.x + side.y * side.y));
			for (const Vec2i64& other : ring.outline) {
				const Vec2i64 across = other - point;
				mostSquared = std::max(mostSquared, across.x * across.x + across.y * across.y);
			}
		}
		ring.body.centre = centre;
		ring.body.widthFactor = geometry::forestWidthFactor(static_cast<std::int64_t>(perimeter), kTunables);
		ring.opaque = mostSquared >= kPlacedOpaqueLeastDiameterMm * kPlacedOpaqueLeastDiameterMm;
		return ring;
	}

	class Stand {
	  public:
		void add(const PlacedRing& ring) {
			const auto index = static_cast<std::uint32_t>(rings.size());
			rings.push_back(ring);
			for (std::int64_t y = bucketOf(ring.low.y); y <= bucketOf(ring.high.y); ++y) {
				for (std::int64_t x = bucketOf(ring.low.x); x <= bucketOf(ring.high.x); ++x) {
					buckets[key(x, y)].push_back(index);
				}
			}
		}

		// Every ring whose box meets the box `radiusMm` round `centre`, once each.
		void near(const Vec2i64& centre, std::int64_t radiusMm, std::vector<std::uint32_t>& out) const {
			out.clear();
			const Vec2i64 low{centre.x - radiusMm, centre.y - radiusMm};
			const Vec2i64 high{centre.x + radiusMm, centre.y + radiusMm};
			for (std::int64_t y = bucketOf(low.y); y <= bucketOf(high.y); ++y) {
				for (std::int64_t x = bucketOf(low.x); x <= bucketOf(high.x); ++x) {
					const auto found = buckets.find(key(x, y));
					if (found == buckets.end()) {
						continue;
					}
					for (const std::uint32_t index : found->second) {
						const PlacedRing& ring = rings[index];
						if (ring.low.x <= high.x && low.x <= ring.high.x && ring.low.y <= high.y && low.y <= ring.high.y) {
							out.push_back(index);
						}
					}
				}
			}
			std::sort(out.begin(), out.end());
			out.erase(std::unique(out.begin(), out.end()), out.end());
		}

		// Whether a mover's centre at `point` lies within `clearanceMm` of a ring's box.
		[[nodiscard]] bool blocked(const Vec2i64& point, std::int64_t clearanceMm) const {
			const auto found = buckets.find(key(bucketOf(point.x), bucketOf(point.y)));
			if (found == buckets.end()) {
				return false;
			}
			return std::any_of(found->second.begin(), found->second.end(), [&](std::uint32_t index) {
				const PlacedRing& ring = rings[index];
				return point.x >= ring.low.x - clearanceMm && point.x <= ring.high.x + clearanceMm && point.y >= ring.low.y - clearanceMm &&
					   point.y <= ring.high.y + clearanceMm;
			});
		}

		std::vector<PlacedRing> rings;

	  private:
		static std::int64_t bucketOf(std::int64_t millimetres) {
			return millimetres >= 0 ? millimetres / kOccluderBucketMm : -((-millimetres + kOccluderBucketMm - 1) / kOccluderBucketMm);
		}
		static std::uint64_t key(std::int64_t x, std::int64_t y) {
			return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) | static_cast<std::uint32_t>(y);
		}
		std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> buckets;
	};

	// Ring sizes, padded as nav pads them (sight-softening.md 2.1): a trunk's Rect collider is its
	// trunk width at the age class (OakTree.xml, MapleTree.xml), plus kPlacedObstaclePadMm a side.
	struct TreeSize {
		std::int64_t sideMm;
	};
	constexpr TreeSize				  kPine{280};
	constexpr TreeSize				  kMatureOak{550};
	constexpr TreeSize				  kVeteranOak{820};
	constexpr std::array<TreeSize, 3> kOakAges{{{350}, kMatureOak, kVeteranOak}};
	constexpr std::array<TreeSize, 3> kMapleAges{{{290}, {440}, {600}}};
	constexpr std::array<double, 3>	  kAgeWeights{0.15, 0.60, 0.25};

	// The landing square's measured stand (tree-variety.md, D27): 78.3 trees per 1,000 m2, maple to
	// oak about 1.6 to 1.
	constexpr double kLandingTreesPerSquareMetre = 0.0783;
	constexpr double kLandingMapleShare = 1.6 / 2.6;

	struct StandSpec {
		std::string name;
		double		spacingMeters = 0.0; // a uniform stand of one tree at 1 / spacing^2; 0 for the landing's mix
		TreeSize	tree{0};
	};

	// BigRocks as placement puts them in TemperateDeciduousForest (BigRock.xml): one candidate a 4 m
	// block at spawnChance 0.004 scaled by the grove field, never within 15 m of another.
	std::vector<Vec2i64> placeRocks(Stand& stand, std::int64_t halfExtentMm, std::mt19937& random) {
		std::uniform_real_distribution<double> unit(0.0, 1.0);
		std::vector<Vec2i64>				   rocks;
		const std::int64_t					   blockMm = 4000;
		for (std::int64_t y = -halfExtentMm; y < halfExtentMm; y += blockMm) {
			for (std::int64_t x = -halfExtentMm; x < halfExtentMm; x += blockMm) {
				const float worldX = static_cast<float>(x) / 1000.0F;
				const float worldY = static_cast<float>(y) / 1000.0F;
				const float chance =
					0.004F * world::grove::spawnScale(world::grove::groveValue(worldX, worldY, kWorldSeed), worldX, worldY, kWorldSeed);
				if (unit(random) >= chance) {
					continue;
				}
				const Vec2i64 at{
					x + static_cast<std::int64_t>(unit(random) * blockMm), y + static_cast<std::int64_t>(unit(random) * blockMm)
				};
				if (std::any_of(rocks.begin(), rocks.end(), [&](const Vec2i64& rock) {
						const Vec2i64 gap = rock - at;
						return gap.x * gap.x + gap.y * gap.y < 15000LL * 15000LL;
					})) {
					continue;
				}
				rocks.push_back(at);
				stand.add(ringOf(at, 1400, 1070, unit(random) * std::numbers::pi));
			}
		}
		return rocks;
	}

	// Trees placed uniformly at random with no two centres nearer than `leastApartMm` and none within
	// 2 m of a rock centre, `count` of them; `pick` gives each its size.
	template <typename Pick>
	void placeTrees(
		Stand&						stand,
		const std::vector<Vec2i64>& rocks,
		std::int64_t				halfExtentMm,
		std::size_t					count,
		std::int64_t				leastApartMm,
		std::mt19937&				random,
		Pick						pick
	) {
		std::uniform_int_distribution<std::int64_t>				coordinate(-halfExtentMm, halfExtentMm);
		std::unordered_map<std::uint64_t, std::vector<Vec2i64>> cells;
		const std::int64_t										cellMm = 2000;
		auto													cellKey = [](std::int64_t x, std::int64_t y) {
			   return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) | static_cast<std::uint32_t>(y);
		};
		auto cellOf = [](std::int64_t millimetres) {
			return millimetres >= 0 ? millimetres / cellMm : -((-millimetres + cellMm - 1) / cellMm);
		};
		std::size_t placed = 0;
		for (std::size_t attempt = 0; placed < count && attempt < count * 20; ++attempt) {
			const Vec2i64 at{coordinate(random), coordinate(random)};
			if (std::any_of(rocks.begin(), rocks.end(), [&](const Vec2i64& rock) {
					const Vec2i64 gap = rock - at;
					return gap.x * gap.x + gap.y * gap.y < 2000LL * 2000LL;
				})) {
				continue;
			}
			bool crowded = false;
			for (std::int64_t y = cellOf(at.y) - 1; y <= cellOf(at.y) + 1 && !crowded; ++y) {
				for (std::int64_t x = cellOf(at.x) - 1; x <= cellOf(at.x) + 1 && !crowded; ++x) {
					const auto found = cells.find(cellKey(x, y));
					if (found != cells.end()) {
						crowded = std::any_of(found->second.begin(), found->second.end(), [&](const Vec2i64& other) {
							const Vec2i64 gap = other - at;
							return gap.x * gap.x + gap.y * gap.y < leastApartMm * leastApartMm;
						});
					}
				}
			}
			if (crowded) {
				continue;
			}
			cells[cellKey(cellOf(at.x), cellOf(at.y))].push_back(at);
			const TreeSize size = pick(at);
			stand.add(ringOf(at, size.sideMm, size.sideMm, 0.0));
			++placed;
		}
	}

	// A synthetic stand of 4.3, or the landing's mix: its trees thinned by the grove field as spaced
	// flora are (glades and gaps open, stands at full density), scaled so the 100 m square round the
	// drop holds the measured 78.3 per 1,000 m2.
	Stand makeStand(const StandSpec& spec, std::int64_t halfExtentMm, std::uint32_t seed) {
		Stand					   stand;
		std::mt19937			   random(seed);
		const std::vector<Vec2i64> rocks = placeRocks(stand, halfExtentMm, random);
		const double			   area = std::pow(2.0 * static_cast<double>(halfExtentMm) / 1000.0, 2.0);
		if (spec.spacingMeters > 0.0) {
			const auto count = static_cast<std::size_t>(area / (spec.spacingMeters * spec.spacingMeters));
			placeTrees(stand, rocks, halfExtentMm, count, 800, random, [&](const Vec2i64&) { return spec.tree; });
			return stand;
		}
		std::uniform_real_distribution<double> unit(0.0, 1.0);
		auto								   keepShare = [](const Vec2i64& at) {
			  const float worldX = static_cast<float>(at.x) / 1000.0F;
			  const float worldY = static_cast<float>(at.y) / 1000.0F;
			  return static_cast<double>(
				  world::grove::spawnScale(world::grove::groveValue(worldX, worldY, kWorldSeed), worldX, worldY, kWorldSeed)
			  );
		};
		// The grove field's mean share over the drop's square sets how many candidates to try.
		double share = 0.0;
		for (int y = -50; y < 50; ++y) {
			for (int x = -50; x < 50; ++x) {
				share += keepShare({x * 1000 + 500, y * 1000 + 500});
			}
		}
		share /= 10000.0;
		const double perSquareMetre = kLandingTreesPerSquareMetre / share;
		Stand		 candidates;
		const auto	 count = static_cast<std::size_t>(area * perSquareMetre);
		placeTrees(candidates, rocks, halfExtentMm, count, 1000, random, [&](const Vec2i64&) {
			double		age = unit(random);
			std::size_t index = 0;
			while (index + 1 < kAgeWeights.size() && age >= kAgeWeights[index]) {
				age -= kAgeWeights[index];
				++index;
			}
			return unit(random) < kLandingMapleShare ? kMapleAges[index] : kOakAges[index];
		});
		for (const PlacedRing& ring : candidates.rings) {
			if (unit(random) < keepShare(ring.body.centre)) {
				stand.add(ring);
			}
		}
		return stand;
	}

	// --- Vision's build (5.1) ----------------------------------------------------------------

	// What the sight shape takes in: the shipped shape has neither.
	struct ShapeRules {
		bool forestDepth = true;
		bool rocksOcclude = true;
	};

	enum class Path : std::uint8_t { Open, Leg, Polygon };

	struct Build {
		std::uint64_t		 serial = 0;
		Path				 path = Path::Open;
		Vec2i64				 eye;
		SightRanges			 rimRanges{}; // the forest shape of sightRadius round the eye
		std::vector<Vec2i64> polygon; // the polygon path's shape round the eye, cut by the forest shape of its reach
		std::size_t			 ringsInSight = 0;
		std::size_t			 opaqueInReach = 0;
		std::size_t			 vertices = 0;
	};

	struct BuildScratch {
		std::vector<std::uint32_t>			   near;
		std::vector<geometry::ForestBody>	   bodies;
		std::vector<geometry::OccluderSegment> occluders;
	};

	// The bodies round `eye`: the forest bodies with centres within sight, and, when rocks occlude, the
	// opaque rings' edges whose boxes come within `reachMm`.
	void gather(const Stand& stand, const Vec2i64& eye, std::int64_t reachMm, const ShapeRules& rules, BuildScratch& scratch, std::size_t& opaqueInReach) {
		stand.near(eye, reachMm, scratch.near);
		scratch.bodies.clear();
		scratch.occluders.clear();
		opaqueInReach = 0;
		for (const std::uint32_t index : scratch.near) {
			const PlacedRing& ring = stand.rings[index];
			const Vec2i64	  offset = ring.body.centre - eye;
			if (std::abs(offset.x) < kSightMm && std::abs(offset.y) < kSightMm) {
				scratch.bodies.push_back(ring.body);
			}
			if (!ring.opaque || !rules.rocksOcclude) {
				continue;
			}
			const std::int64_t gapX = std::max<std::int64_t>({ring.low.x - eye.x, eye.x - ring.high.x, 0});
			const std::int64_t gapY = std::max<std::int64_t>({ring.low.y - eye.y, eye.y - ring.high.y, 0});
			if (gapX * gapX + gapY * gapY > reachMm * reachMm) {
				continue;
			}
			++opaqueInReach;
			for (std::size_t k = 0; k < ring.outline.size(); ++k) {
				scratch.occluders.push_back({ring.outline[k], ring.outline[(k + 1) % ring.outline.size()]});
			}
		}
	}

	std::size_t ringsWithin(const std::vector<geometry::ForestBody>& bodies, const Vec2i64& eye) {
		return static_cast<std::size_t>(std::count_if(bodies.begin(), bodies.end(), [&](const geometry::ForestBody& body) {
			const Vec2i64 offset = body.centre - eye;
			return offset.x * offset.x + offset.y * offset.y < kSightMm * kSightMm;
		}));
	}

	SightRanges rangesAt(const Vec2i64& eye, std::int64_t radiusMm, const ShapeRules& rules, const BuildScratch& scratch) {
		return rules.forestDepth ? geometry::forestRanges(eye, kSightMm, radiusMm, scratch.bodies, kTunables) : vision::equalRanges(radiusMm);
	}

	Build buildAt(const Stand& stand, const Vec2i64& eye, std::int64_t reachMm, const ShapeRules& rules, BuildScratch& scratch) {
		Build build;
		build.eye = eye;
		gather(stand, eye, reachMm, rules, scratch, build.opaqueInReach);
		build.ringsInSight = ringsWithin(scratch.bodies, eye);
		build.rimRanges = rangesAt(eye, kSightMm, rules, scratch);
		if (build.opaqueInReach == 0) {
			build.path = build.ringsInSight == 0 || !rules.forestDepth ? Path::Open : Path::Leg;
			build.vertices = build.path == Path::Open ? geometry::kSightSides : geometry::forestShape(eye, build.rimRanges).size();
			return build;
		}
		build.path = Path::Polygon;
		const geometry::Ring hard = geometry::computeVisibilityPolygon(eye, reachMm, scratch.occluders);
		const geometry::Ring shape = geometry::starClippedToConvex(hard, eye, vision::rimOf(eye, rangesAt(eye, reachMm, rules, scratch)));
		build.vertices = shape.size();
		build.polygon = shape;
		return build;
	}

	// The shape at a sweep point or a resting point (6.4): its own forest depth's ranges, no wider
	// than `ranges`, with the opaque bodies standing, cut to that rim.
	SeenArea::BuiltSight
	sweepShape(const Stand& stand, const Vec2i64& centre, const SightRanges& ranges, const ShapeRules& rules, BuildScratch& scratch) {
		std::size_t opaque = 0;
		gather(stand, centre, kSightMm, rules, scratch, opaque);
		const SightRanges	 own = rangesAt(centre, kSightMm, rules, scratch);
		SeenArea::BuiltSight built;
		for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
			built.rimRanges[side] = std::max(geometry::kForestShapeLeastRangeMm, std::min(own[side], ranges[side]));
		}
		const std::int64_t most = *std::max_element(built.rimRanges.begin(), built.rimRanges.end());
		built.shape = geometry::starClippedToConvex(
			geometry::computeVisibilityPolygon(centre, most, scratch.occluders), centre, vision::rimOf(centre, built.rimRanges)
		);
		return built;
	}

	// --- The record's driver: each walker's vision::SightFeed, as PlayerVisibilitySystem holds one ----

	struct Observer {
		Vec2i64			  position;
		double			  course = 0.0;
		double			  detour = 0.0; // the heading he steps round a ring on, for detourLeftMm more
		double			  detourLeftMm = 0.0;
		double			  untilTurn = 0.0;
		Build			  build;
		bool			  built = false;
		vision::SightFeed feed;
	};

	struct WalkCounts {
		std::array<std::size_t, 3> buildsByPath{};
		double					   walkedMm = 0.0;
		std::int64_t			   mostRangeChange = 0;
	};

	// The bench's own walker at the speed: 2 m/s, so the leg tolerance at 1x is the start value.
	std::int64_t walkToleranceMm(double speed) { return vision::SightFeed::legToleranceMm(500, speed); }

	struct WalkSettings {
		int			 colonists = 50;
		double		 speed = 1.0;
		ShapeRules	 rules;
		std::int64_t toleranceMm = 500;
		// Sweep the polygon path past placed bodies, as if an opaque wall were in reach of every build.
		bool sweepPlacedBodies = false;
	};

	// The record's own counts for one tick: what it did and how long the tick took.
	struct TickRecord {
		double				 ms = 0.0;
		int					 tick = -1;
		SeenArea::TickWork	 work;
		SeenArea::Pending	 pending;
	};

	class Walk {
	  public:
		Walk(const Stand& stand, const WalkSettings& settings)
			: stand(stand),
			  settings(settings),
			  random(29) {
			// Vision's reach: the step and an interval's walk taken up to whole 100 mm, and 100 mm more.
			const double walkMm = kWalkMps * settings.speed * kVisionInterval * kTick * 1000.0;
			reachMm = kSightMm + kStepMm + static_cast<std::int64_t>(std::ceil(walkMm / 100.0)) * 100 + 100;
			seen.commitNow(geometry::sightPolygon({0, 0}, kLandingMm), {0, 0});
			countNewRings();
			seen.setSightBuilder([this](const Vec2i64& centre, std::int64_t /*sightRadiusMm*/, const SightRanges& ranges) {
				return sweepShape(this->stand, centre, ranges, this->settings.rules, sweepScratch);
			});
			for (int i = 0; i < settings.colonists; ++i) {
				Observer	 observer;
				const double angle = 2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(settings.colonists);
				observer.course = angle;
				observer.position = free({std::llround(std::cos(angle) * 3000.0), std::llround(std::sin(angle) * 3000.0)});
				observers.push_back(observer);
			}
		}

		void tick(int tickIndex) {
			for (Observer& observer : observers) {
				move(observer);
			}
			const auto visionStart = std::chrono::steady_clock::now();
			if (tickIndex % kVisionInterval == 0) {
				for (Observer& observer : observers) {
					rebuild(observer);
				}
			}
			const auto recordStart = std::chrono::steady_clock::now();
			for (std::size_t index = 0; index < observers.size(); ++index) {
				follow(index, observers[index]);
			}
			seen.update();
			const auto end = std::chrono::steady_clock::now();
			countNewRings();
			visionMs.push_back(std::chrono::duration<double, std::milli>(recordStart - visionStart).count());
			recordMs.push_back(std::chrono::duration<double, std::milli>(end - recordStart).count());
			const SeenArea::Pending queue = seen.pending();
			if (recordMs.back() > worstTick.ms) {
				worstTick = {recordMs.back(), tickIndex, seen.lastTickWork(), queue};
			}
			for (const auto field : kWorkFields) {
				mostWork.*field = std::max(mostWork.*field, seen.lastTickWork().*field);
			}
			if (queue.total() > atPeak.total()) {
				atPeak = queue;
			}
			mostParts.openLegs = std::max(mostParts.openLegs, queue.openLegs);
			mostParts.shapesWaiting = std::max(mostParts.shapesWaiting, queue.shapesWaiting);
			mostParts.piecesWaiting = std::max(mostParts.piecesWaiting, queue.piecesWaiting);
			mostSweepsWaiting = std::max(mostSweepsWaiting, seen.sweepsWaiting());
			backlogTicks = queue.shapesWaiting + queue.piecesWaiting > 0 ? backlogTicks + 1 : 0;
			mostBacklogTicks = std::max(mostBacklogTicks, backlogTicks);
			if (tickIndex + 1 == kTicks / 2) {
				atHalf = queue;
			}
			if ((tickIndex + 1) % kTicksAMinute == 0) {
				registeredByMinute.push_back(registrations());
			}
		}

		// Everyone stops, every leg closes, and the record runs until nothing waits: the ticks it takes.
		std::size_t drain() {
			openAtEnd = seen.pending().openLegs;
			for (std::size_t index = 0; index < observers.size(); ++index) {
				observers[index].feed.stop(seen, index);
			}
			std::size_t ticks = 0;
			while ((seen.pending().total() > 0 || seen.sweepsWaiting() > 0) && ticks < kDrainLimitTicks) {
				seen.update();
				countNewRings();
				++ticks;
			}
			return ticks;
		}

		[[nodiscard]] std::size_t legsOpened() const {
			std::size_t total = 0;
			for (const Observer& observer : observers) {
				total += observer.feed.legsOpened();
			}
			return total;
		}

		[[nodiscard]] std::size_t legsEndedBy(vision::LegEnd reason) const {
			std::size_t total = 0;
			for (const Observer& observer : observers) {
				total += observer.feed.legsEndedBy(reason);
			}
			return total;
		}

		[[nodiscard]] std::size_t registrations() const {
			return seen.registrations(Origin::Leg) + seen.registrations(Origin::Polygon) + seen.registrations(Origin::Sweep);
		}

		static constexpr std::size_t kDrainLimitTicks = 360000;
		static constexpr std::array<std::size_t SeenArea::TickWork::*, 10> kWorkFields{
			&SeenArea::TickWork::legJoins,			 &SeenArea::TickWork::legVerticesJoined, &SeenArea::TickWork::shapesTested,
			&SeenArea::TickWork::shapesCut,			 &SeenArea::TickWork::sweepFrontierTests, &SeenArea::TickWork::verticesCut,
			&SeenArea::TickWork::piecesRegistered,	 &SeenArea::TickWork::verticesRegistered, &SeenArea::TickWork::sweepPointsBuilt,
			&SeenArea::TickWork::sweepPointsDropped,
		};

		const Stand&			 stand;
		WalkSettings			 settings;
		std::int64_t			 reachMm = 0;
		SeenArea				 seen;
		std::vector<Observer>	 observers;
		WalkCounts				 counts;
		std::vector<double>		 visionMs;
		std::vector<double>		 recordMs;
		SeenArea::Pending		 atPeak;
		std::size_t				 backlogTicks = 0; // consecutive ticks with a shape or a piece waiting
		std::size_t				 mostBacklogTicks = 0;
		SeenArea::Pending		 mostParts;
		SeenArea::Pending		 atHalf;
		std::size_t				 mostSweepsWaiting = 0;
		std::vector<std::size_t> registeredByMinute;
		std::size_t				 openAtEnd = 0;
		TickRecord				 worstTick;
		SeenArea::TickWork		 mostWork; // each count's most in any tick
		// K (player-visibility.md 6.1): the registered rings whose boxes meet a new ring's box, counted
		// as each registers, outside the timed tick.
		std::vector<SeenArea::Box> registeredBoxes;
		std::size_t				   ringsMetTotal = 0;
		std::size_t				   ringsMetMost = 0;
		std::size_t				   ringsCounted = 0;

	  private:
		void countNewRings() {
			for (std::size_t index = registeredBoxes.size(); index < seen.ringsRegistered(); ++index) {
				const std::vector<Vec2i64>& ring = seen.registeredRing(index);
				SeenArea::Box				box{ring.front(), ring.front()};
				for (const Vec2i64& point : ring) {
					box.low = {std::min(box.low.x, point.x), std::min(box.low.y, point.y)};
					box.high = {std::max(box.high.x, point.x), std::max(box.high.y, point.y)};
				}
				const auto met = static_cast<std::size_t>(std::count_if(registeredBoxes.begin(), registeredBoxes.end(), [&box](const SeenArea::Box& other) {
					return box.low.x <= other.high.x && other.low.x <= box.high.x && box.low.y <= other.high.y && other.low.y <= box.high.y;
				}));
				// The landing's ring is not a registration of the walk's.
				if (index > 0) {
					ringsMetTotal += met;
					ringsMetMost = std::max(ringsMetMost, met);
					++ringsCounted;
				}
				registeredBoxes.push_back(box);
			}
		}

		Vec2i64 free(Vec2i64 at) const {
			for (int step = 0; step < 100 && stand.blocked(at, 300); ++step) {
				at.x += 250;
			}
			return at;
		}

		// A walk of 2 m/s times the speed, its course turning up to 60 degrees every 2 to 15 s, back
		// toward the drop past 400 m, and around every ring: a step that would come within 0.3 m of one
		// is taken 45 degrees off the course at a time until it does not, and he keeps that heading for
		// kDetourMm, or until it is blocked or his course turns, before trying his course again.
		void move(Observer& observer) {
			std::uniform_real_distribution<double> unit(0.0, 1.0);
			observer.untilTurn -= kTick * settings.speed;
			if (observer.untilTurn <= 0.0) {
				const double turn = (unit(random) - 0.5) * 2.0 * std::numbers::pi / 3.0;
				observer.course += turn;
				observer.detourLeftMm = 0.0;
				observer.untilTurn = 2.0 + 13.0 * unit(random);
				const double fromDrop =
					std::hypot(static_cast<double>(observer.position.x), static_cast<double>(observer.position.y)) / 1000.0;
				if (fromDrop > kTurnBackMeters) {
					observer.course = std::atan2(static_cast<double>(-observer.position.y), static_cast<double>(-observer.position.x)) + turn / 2.0;
				}
			}
			const double stepMm = kWalkMps * settings.speed * kTick * 1000.0;
			auto		 stepAlong = [&](double heading) {
				const Vec2i64 next =
					observer.position + Vec2i64{std::llround(std::cos(heading) * stepMm), std::llround(std::sin(heading) * stepMm)};
				if (stand.blocked(next, 300)) {
					return false;
				}
				counts.walkedMm += stepMm;
				observer.position = next;
				return true;
			};
			if (observer.detourLeftMm > 0.0) {
				observer.detourLeftMm -= stepMm;
				if (stepAlong(observer.detour)) {
					return;
				}
			} else if (stepAlong(observer.course)) {
				return;
			}
			for (int attempt = 1; attempt < 8; ++attempt) {
				const double heading = observer.course + static_cast<double>(attempt) * std::numbers::pi / 4.0;
				if (stepAlong(heading)) {
					observer.detour = heading;
					observer.detourLeftMm = kDetourMm - stepMm;
					return;
				}
			}
		}

		void rebuild(Observer& observer) {
			if (observer.built) {
				const Vec2i64 moved = observer.position - observer.build.eye;
				if (moved.x * moved.x + moved.y * moved.y <= kStepMm * kStepMm) {
					return;
				}
			}
			Build build = buildAt(stand, observer.position, reachMm, settings.rules, buildScratch);
			build.serial = ++serials;
			if (observer.built) {
				for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
					counts.mostRangeChange = std::max(counts.mostRangeChange, std::abs(build.rimRanges[side] - observer.build.rimRanges[side]));
				}
			}
			++counts.buildsByPath[static_cast<std::size_t>(build.path)];
			observer.build = std::move(build);
			observer.built = true;
		}

		// His feed takes this tick's position and his last build, as PlayerVisibilitySystem's does.
		void follow(std::size_t index, Observer& observer) {
			if (!observer.built) {
				return;
			}
			const Build&			 build = observer.build;
			const vision::SightBuild sight{
				.buildRevision	   = build.serial,
				.polygonRevision   = build.serial,
				.eye			   = build.eye,
				.sightMm		   = kSightMm,
				.ranges			   = &build.rimRanges,
				.forest			   = true,
				.polygon		   = build.path == Path::Polygon ? &build.polygon : nullptr,
				.opaqueWallInReach = settings.sweepPlacedBodies,
			};
			observer.feed.follow(seen, index, observer.position, sight, settings.toleranceMm, kRimEaseMm);
		}

		std::mt19937  random;
		BuildScratch  buildScratch;
		BuildScratch  sweepScratch;
		std::uint64_t serials = 0;
	};

	struct Times {
		double mean = 0.0;
		double p99 = 0.0;
		double most = 0.0;
	};

	Times timesOf(const std::vector<double>& values) {
		if (values.empty()) {
			return {};
		}
		std::vector<double> sorted = values;
		std::sort(sorted.begin(), sorted.end());
		return {
			std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size()),
			sorted[static_cast<std::size_t>(0.99 * static_cast<double>(sorted.size() - 1))],
			sorted.back()
		};
	}


	const Stand& landingForest() {
		static const Stand stand = makeStand({"landing"}, 450000, 7);
		return stand;
	}

	constexpr std::array<const char*, 4> kCaseNames{
		"(a) the shipped shape", "(b) forest depth only", "(c) rocks only", "(d) forest depth and rocks"
	};

	// Arguments: colonists, game speed, the case (0 the shipped shape: no forest depth, rocks not
	// occluding; 1 forest depth only; 2 rocks only; 3 both, as designed), softLegToleranceMm (0 for
	// 500 mm scaled by the distance between builds at the speed, as vision::SightFeed scales it), and 1
	// to skip the sweep for placed opaque bodies, as the feed does, 0 to sweep them.
	void BM_SightForestWalk(benchmark::State& state) {
		WalkSettings settings;
		settings.colonists = static_cast<int>(state.range(0));
		settings.speed = static_cast<double>(state.range(1));
		const auto shapeCase = static_cast<std::size_t>(state.range(2));
		settings.rules = {.forestDepth = shapeCase == 1 || shapeCase == 3, .rocksOcclude = shapeCase >= 2};
		settings.toleranceMm =
			state.range(3) > 0 ? state.range(3) : walkToleranceMm(settings.speed);
		settings.sweepPlacedBodies = state.range(4) == 0;
		const Stand& stand = landingForest();
		for (auto iteration : state) {
			Walk walk(stand, settings);
			for (int tick = 0; tick < kTicks; ++tick) {
				walk.tick(tick);
			}
			const SeenArea::Pending atEnd = walk.seen.pending();
			const std::size_t		sweepsAtEnd = walk.seen.sweepsWaiting();
			const Times				record = timesOf(walk.recordMs);
			const Times				visionTimes = timesOf(walk.visionMs);
			const std::size_t		registered = walk.registrations();
			const std::size_t		mostTicksToTest = walk.seen.mostTicksToTest();
			const std::size_t		mostTicksToCut = walk.seen.mostTicksToCut();
			const double ringsMetMean = walk.ringsCounted > 0 ? static_cast<double>(walk.ringsMetTotal) / static_cast<double>(walk.ringsCounted) : 0.0;
			const std::size_t ringsMetMost = walk.ringsMetMost;
			const auto		  byOrigin = [&walk](Origin origin) { return walk.seen.registrations(origin); };
			const std::array<std::size_t, 3> walkRegistrations{byOrigin(Origin::Leg), byOrigin(Origin::Polygon), byOrigin(Origin::Sweep)};
			const std::size_t drainTicks = walk.drain();
			const WalkCounts& counts = walk.counts;
			const double	  walkedKm = counts.walkedMm / 1e6;
			const std::size_t builds = counts.buildsByPath[0] + counts.buildsByPath[1] + counts.buildsByPath[2];
			std::size_t	 mostAMinute = 0;
			std::string	 perMinute;
			for (std::size_t minute = 0; minute < walk.registeredByMinute.size(); ++minute) {
				const std::size_t inMinute = walk.registeredByMinute[minute] - (minute > 0 ? walk.registeredByMinute[minute - 1] : 0);
				mostAMinute = std::max(mostAMinute, inMinute);
				perMinute += std::to_string(inMinute) + (minute + 1 < walk.registeredByMinute.size() ? ", " : "");
			}
			std::fprintf(
				stderr,
				"[SightForestWalk] %s, %d colonists at %.0fx, tolerance %lld mm, sweep %s, reach %lld mm\n",
				kCaseNames[shapeCase],
				settings.colonists,
				settings.speed,
				static_cast<long long>(settings.toleranceMm),
				settings.sweepPlacedBodies ? "on" : "skipped",
				static_cast<long long>(walk.reachMm)
			);
			std::fprintf(
				stderr,
				"  queue at its peak %zu: open legs %zu, shapes waiting %zu, pieces waiting %zu; each part's most %zu, %zu, %zu; at minute 5 "
				"%zu: %zu, %zu, %zu; at the end %zu: %zu, %zu, %zu\n",
				walk.atPeak.total(),
				walk.atPeak.openLegs,
				walk.atPeak.shapesWaiting,
				walk.atPeak.piecesWaiting,
				walk.mostParts.openLegs,
				walk.mostParts.shapesWaiting,
				walk.mostParts.piecesWaiting,
				walk.atHalf.total(),
				walk.atHalf.openLegs,
				walk.atHalf.shapesWaiting,
				walk.atHalf.piecesWaiting,
				atEnd.total(),
				atEnd.openLegs,
				atEnd.shapesWaiting,
				atEnd.piecesWaiting
			);
			std::fprintf(
				stderr,
				"  sweep points waiting at most %zu, at the end %zu, built %zu; the longest wait to be tested %zu ticks, to be cut %zu; the longest "
				"backlog %zu ticks; drained %zu ticks after the walk%s\n",
				walk.mostSweepsWaiting,
				sweepsAtEnd,
				walk.seen.sweepsBuilt(),
				mostTicksToTest,
				mostTicksToCut,
				walk.mostBacklogTicks,
				drainTicks,
				drainTicks >= Walk::kDrainLimitTicks ? " (not drained, stopped)" : ""
			);
			std::fprintf(
				stderr,
				"  registrations %zu: legs %zu, polygon path %zu, sweep %zu; most in a minute %zu (%s); K mean %.1f max %zu\n",
				registered,
				walkRegistrations[0],
				walkRegistrations[1],
				walkRegistrations[2],
				mostAMinute,
				perMinute.c_str(),
				ringsMetMean,
				ringsMetMost
			);
			std::fprintf(
				stderr,
				"  legs opened %zu (%.1f a km), ended by turn %zu, jump %zu, spread %zu, vertices %zu, path %zu, open at the end %zu\n",
				walk.legsOpened(),
				static_cast<double>(walk.legsOpened()) / walkedKm,
				walk.legsEndedBy(vision::LegEnd::Turn),
				walk.legsEndedBy(vision::LegEnd::Jump),
				walk.legsEndedBy(vision::LegEnd::Spread),
				walk.legsEndedBy(vision::LegEnd::Vertices),
				walk.legsEndedBy(vision::LegEnd::Path),
				walk.openAtEnd
			);
			std::fprintf(
				stderr,
				"  builds %zu: open %zu, leg %zu, polygon %zu (%.1f%%); greatest change of a side between builds %lld mm; walked %.1f km; seen "
				"%.0f m2\n",
				builds,
				counts.buildsByPath[0],
				counts.buildsByPath[1],
				counts.buildsByPath[2],
				builds > 0 ? 100.0 * static_cast<double>(counts.buildsByPath[2]) / static_cast<double>(builds) : 0.0,
				static_cast<long long>(counts.mostRangeChange),
				walkedKm,
				walk.seen.committedSquareMeters()
			);
			std::fprintf(
				stderr,
				"  record tick ms mean %.4f p99 %.4f max %.3f (K counter excluded); vision tick ms mean %.4f\n",
				record.mean,
				record.p99,
				record.most,
				visionTimes.mean
			);
			const auto printWork = [](const char* label, const SeenArea::TickWork& work) {
				std::fprintf(
					stderr,
					"  %s: leg joins %zu (%zu vertices), shapes tested %zu, cut %zu, sweep frontier tests %zu (%zu vertices cut), pieces "
					"registered %zu (%zu vertices), sweep points built %zu, dropped %zu\n",
					label,
					work.legJoins,
					work.legVerticesJoined,
					work.shapesTested,
					work.shapesCut,
					work.sweepFrontierTests,
					work.verticesCut,
					work.piecesRegistered,
					work.verticesRegistered,
					work.sweepPointsBuilt,
					work.sweepPointsDropped
				);
			};
			std::fprintf(
				stderr,
				"  the slowest record tick, %d: %.3f ms, queue then %zu (%zu, %zu, %zu)\n",
				walk.worstTick.tick,
				walk.worstTick.ms,
				walk.worstTick.pending.total(),
				walk.worstTick.pending.openLegs,
				walk.worstTick.pending.shapesWaiting,
				walk.worstTick.pending.piecesWaiting
			);
			printWork("its work", walk.worstTick.work);
			printWork("each count's most in any tick", walk.mostWork);
			state.counters["recordMeanMs"] = record.mean;
			state.counters["pendingMost"] = static_cast<double>(walk.atPeak.total());
			state.counters["kMean"] = ringsMetMean;
			benchmark::DoNotOptimize(iteration);
		}
	}

	// What convexity takes on each side: how far the forest shape's support along the side's normal
	// falls short of the side's range x cos(pi / kSightSides), in millimetres.
	void convexityLoss(
		const Vec2i64&							   eye,
		const SightRanges&						   ranges,
		const std::vector<Vec2i64>&				   shape,
		std::array<double, geometry::kSightSides>& loss
	) {
		const double cosine = static_cast<double>(geometry::kSightCosHalfSide) / static_cast<double>(geometry::kSightDirectionScale);
		const double scale = static_cast<double>(geometry::kSightSideNormalScale);
		for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
			const double normalX = static_cast<double>(geometry::kSightSideNormals[side][0]) / scale;
			const double normalY = static_cast<double>(geometry::kSightSideNormals[side][1]) / scale;
			double		 support = -1e300;
			for (const Vec2i64& vertex : shape) {
				support =
					std::max(support, normalX * static_cast<double>(vertex.x - eye.x) + normalY * static_cast<double>(vertex.y - eye.y));
			}
			loss[side] = std::max(0.0, static_cast<double>(ranges[side]) * cosine - support);
		}
	}

	void BM_SightForestBuild(benchmark::State& state) {
		static const std::vector<StandSpec> kStands{
			{"mature oaks 5 m apart", 5.0, kMatureOak},
			{"pines 3 m apart", 3.0, kPine},
			{"veteran oaks 4 m apart", 4.0, kVeteranOak},
			{"mature oaks 3 m apart", 3.0, kMatureOak},
			{"pines 2 m apart", 2.0, kPine},
			{"veteran oaks 3 m apart", 3.0, kVeteranOak},
			{"the landing's forest", 0.0, {0}},
		};
		const StandSpec& spec = kStands[static_cast<std::size_t>(state.range(0))];
		const auto		 halfExtentMm = std::int64_t{120000};
		for (auto iteration : state) {
			const Stand stand = spec.spacingMeters > 0.0 ? makeStand(spec, halfExtentMm, 11) : landingForest();
			std::size_t trees = 0;
			std::size_t rocks = 0;
			for (const PlacedRing& ring : stand.rings) {
				const bool central = std::abs(ring.body.centre.x) < 50000 && std::abs(ring.body.centre.y) < 50000;
				trees += central && !ring.opaque ? 1 : 0;
				rocks += ring.opaque && std::abs(ring.body.centre.x) < halfExtentMm && std::abs(ring.body.centre.y) < halfExtentMm ? 1 : 0;
			}
			std::mt19937								random(13);
			std::uniform_int_distribution<std::int64_t> coordinate(-60000, 60000);
			BuildScratch								scratch;
			std::vector<double>							buildUs;
			std::array<std::size_t, 3>					byPath{};
			double										ringsInSight = 0.0, opaqueInReach = 0.0, vertices = 0.0, rimMean = 0.0;
			double lossMean = 0.0, lossClearMost = 0.0, lossMost = 0.0, lossUnlimitedClearMost = 0.0, lossUnlimitedMost = 0.0,
				   givenUpMost = 0.0;
			std::size_t				 floorSides = 0;
			const int				 viewpoints = 400;
			geometry::ForestTunables unlimited = kTunables;
			unlimited.forestSlopeMm = 10000;
			for (int view = 0; view < viewpoints; ++view) {
				Vec2i64 eye{coordinate(random), coordinate(random)};
				while (stand.blocked(eye, 300)) {
					eye = {coordinate(random), coordinate(random)};
				}
				const auto	start = std::chrono::steady_clock::now();
				const Build build = buildAt(stand, eye, kSightMm + 667, ShapeRules{}, scratch);
				buildUs.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
				++byPath[static_cast<std::size_t>(build.path)];
				ringsInSight += static_cast<double>(build.ringsInSight);
				opaqueInReach += static_cast<double>(build.opaqueInReach);
				vertices += static_cast<double>(build.vertices);

				const SightRanges raw = geometry::forestRawDepth(eye, kSightMm, scratch.bodies, kTunables);
				const SightRanges limited = geometry::forestLimitedDepth(raw, kSightMm, kTunables);
				const SightRanges capped = geometry::forestLimitedDepth(raw, kSightMm, unlimited);
				SightRanges		  unlimitedRanges;
				for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
					rimMean += static_cast<double>(build.rimRanges[side]) / static_cast<double>(geometry::kSightSides);
					unlimitedRanges[side] = kSightMm - capped[side];
					givenUpMost = std::max(givenUpMost, static_cast<double>(capped[side] - limited[side]));
					floorSides += limited[side] >= kSightMm - geometry::forestFloorMm(kSightMm, kTunables) ? 1 : 0;
				}
				std::array<double, geometry::kSightSides> loss{};
				std::array<double, geometry::kSightSides> lossUnlimited{};
				convexityLoss(eye, build.rimRanges, geometry::forestShape(eye, build.rimRanges), loss);
				convexityLoss(eye, unlimitedRanges, geometry::forestShape(eye, unlimitedRanges), lossUnlimited);
				double sideSum = 0.0;
				for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
					sideSum += loss[side];
					lossMost = std::max(lossMost, loss[side]);
					lossUnlimitedMost = std::max(lossUnlimitedMost, lossUnlimited[side]);
					if (std::min<std::int64_t>(raw[side], kSightMm) == 0) {
						lossClearMost = std::max(lossClearMost, loss[side]);
						lossUnlimitedClearMost = std::max(lossUnlimitedClearMost, lossUnlimited[side]);
					}
				}
				lossMean += sideSum / static_cast<double>(geometry::kSightSides);
			}
			const Times	 times = timesOf(buildUs);
			const double count = static_cast<double>(viewpoints);
			const double side = 2.0 * static_cast<double>(halfExtentMm) / 1000.0;
			std::fprintf(
				stderr,
				"[SightForestBuild] %s: %.1f trees per 1,000 m2 in the central 100 m square, %.2f BigRocks per hectare; build us mean %.1f "
				"p99 %.1f max %.1f; "
				"paths open %zu leg %zu polygon %zu\n",
				spec.name.c_str(),
				static_cast<double>(trees) / 10.0,
				static_cast<double>(rocks) / (side * side / 10000.0),
				times.mean,
				times.p99,
				times.most,
				byPath[0],
				byPath[1],
				byPath[2]
			);
			std::fprintf(
				stderr,
				"  rings in sight %.0f, opaque in reach %.2f, vertices out %.1f, rim mean %.2f m; sides at the floor %.1f%%; convexity "
				"takes mean %.3f m, max %.2f m "
				"(clear directions %.2f m), without the slope limit max %.2f m (clear %.2f m); the limit gives up at most %.2f m\n",
				ringsInSight / count,
				opaqueInReach / count,
				vertices / count,
				rimMean / count / 1000.0,
				100.0 * static_cast<double>(floorSides) / (count * static_cast<double>(geometry::kSightSides)),
				lossMean / count / 1000.0,
				lossMost / 1000.0,
				lossClearMost / 1000.0,
				lossUnlimitedMost / 1000.0,
				lossUnlimitedClearMost / 1000.0,
				givenUpMost / 1000.0
			);
			state.counters["buildUs"] = times.mean;
			benchmark::DoNotOptimize(iteration);
		}
	}

} // namespace

BENCHMARK(BM_SightForestBuild)->DenseRange(0, 6)->Iterations(1)->Unit(benchmark::kMillisecond);


// The four cases at 50 colonists at 1x and 10x and at 10 colonists at 10x, as the record runs them;
// then the go without its scaled tolerance (500 mm at 10x), and with the sweep on for placed bodies.
BENCHMARK(BM_SightForestWalk)
	->ArgsProduct({{50}, {1, 10}, {0, 1, 2, 3}, {0}, {1}})
	->ArgsProduct({{10}, {10}, {0, 1, 2, 3}, {0}, {1}})
	->Args({50, 10, 3, 500, 1})
	->Args({50, 10, 3, 0, 0})
	->Args({50, 1, 3, 0, 0})
	->Iterations(1)
	->Unit(benchmark::kMillisecond);

// The cases 6.6's bars read: the go (the design) and its control (the shipped shape) at 50
// colonists at 1x and 10x and at 10 colonists at 10x.
BENCHMARK(BM_SightForestWalk)
	->Name("BM_SightForestScheduler")
	->ArgsProduct({{50}, {1, 10}, {0, 3}, {0}, {1}})
	->ArgsProduct({{10}, {10}, {0, 3}, {0}, {1}})
	->Iterations(1)
	->Unit(benchmark::kMillisecond);
