#pragma once

// Generated patch histories for PatchableMesh and a hash of the whole mesh state after each
// patch: what a change that must leave the mesh unchanged is checked by. Every draw is an
// integer from std::mt19937 taken modulo, so a seed gives the same history on every
// toolchain.

#include "../predicates/Predicates.h"
#include "LimitPolygon.h"
#include "PatchableMesh.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace geometry::nav::testdata {

	constexpr std::int64_t kHistoryWater	= -1;
	constexpr std::int64_t kHistoryObstacle = -2;
	constexpr std::int64_t kHistoryBorder	= -3;

	inline std::uint64_t historyPick(std::mt19937& rng, std::uint64_t count) {
		return static_cast<std::uint64_t>(rng()) % count;
	}

	inline std::int64_t historyBetween(std::mt19937& rng, std::int64_t low, std::int64_t high) {
		return low + static_cast<std::int64_t>(historyPick(rng, static_cast<std::uint64_t>(high - low + 1)));
	}

	inline std::vector<Vec2i64> historyRect(std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) {
		return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
	}

	// Folds values into an FNV-style hash.
	struct StateHash {
		std::uint64_t value = 0xCBF29CE484222325ULL;

		void add(std::int64_t item) { value = (value ^ static_cast<std::uint64_t>(item)) * 0x100000001B3ULL; }
		void add(const Vec2i64& point) {
			add(point.x);
			add(point.y);
		}
	};

	// Everything a caller can read of the mesh after a patch and its width refresh: every
	// vertex and triangle slot, edge sources, covers, component labels, the registry, both
	// reports, the epochs as counted from the mesh's first, and the answers of groundCovers,
	// locate and meshEdgesNear round `probe`.
	inline std::uint64_t historyStateHash(const PatchableMesh& mesh, RingId ringBound, const PatchReport& patch, const PatchReport& refresh,
										  std::uint64_t firstEpoch, std::uint64_t firstComponentEpoch, const Vec2i64& probe) {
		StateHash hash;
		hash.add(static_cast<std::int64_t>(mesh.vertices().size()));
		for (std::size_t v = 0; v < mesh.vertices().size(); ++v) {
			hash.add(mesh.vertices()[v]);
		}
		hash.add(static_cast<std::int64_t>(mesh.triangles().size()));
		hash.add(mesh.liveTriangleCount());
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			hash.add(mesh.triangleLive(t) ? 1 : 0);
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
			for (int e = 0; e < 3; ++e) {
				const std::size_t corner = static_cast<std::size_t>(e);
				hash.add(tri.v[corner]);
				hash.add(tri.neighbor[corner]);
				hash.add(tri.edgeProvenance[corner]);
				hash.add(tri.edgeOpening[corner]);
				hash.add(tri.edgePairWidthMm[corner]);
				hash.add(tri.edgeClearWidthMm[corner]);
				for (const EdgeSource& source : mesh.edgeSources(t, e)) {
					hash.add(source.ring);
					hash.add(source.edge);
				}
				hash.add(-1);
			}
			hash.add(tri.faceBlocker);
			hash.add(tri.faceOpening);
			for (const RingId ring : mesh.cover(t)) {
				hash.add(ring);
			}
			hash.add(static_cast<std::int64_t>(mesh.components().label(t)));
		}
		for (RingId r = 0; r < ringBound; ++r) {
			const RegisteredRing& reg = mesh.ring(r);
			hash.add(reg.live ? 1 : 0);
			if (!reg.live) {
				continue;
			}
			for (const Vec2i64& point : reg.polygon.ring) {
				hash.add(point);
			}
			hash.add(reg.polygon.blocked ? 1 : 0);
			hash.add(reg.polygon.provenanceId);
			hash.add(reg.polygon.openingId);
			hash.add(reg.polygon.holeCapable ? 1 : 0);
			hash.add(reg.lo);
			hash.add(reg.hi);
			for (const std::int32_t id : reg.edgeIds) {
				hash.add(id);
			}
		}
		for (const PatchReport* report : {&patch, &refresh}) {
			for (const RingId ring : report->added) {
				hash.add(ring);
			}
			for (const ChangedTriangle& change : report->changes) {
				hash.add(change.id);
				hash.add(change.why);
			}
			if (!report->changes.empty()) {
				hash.add(report->lo);
				hash.add(report->hi);
			}
			hash.add(report->passes);
			hash.add(report->unreadCovers);
			hash.add(report->refusedRings);
			hash.add(report->opens ? 1 : 0);
		}
		hash.add(static_cast<std::int64_t>(mesh.epoch() - firstEpoch));
		hash.add(static_cast<std::int64_t>(mesh.components().epoch() - firstComponentEpoch));
		hash.add(mesh.groundCovers(limitPolygon(probe, 2500)) ? 1 : 0);
		hash.add(mesh.groundCovers(historyRect(probe.x - 300, probe.y - 300, probe.x + 300, probe.y + 300)) ? 1 : 0);
		for (std::int64_t offset = -6000; offset <= 6000; offset += 1500) {
			hash.add(mesh.locate({probe.x + offset, probe.y - offset / 2}));
		}
		std::vector<std::int32_t>				 edgeIds;
		std::vector<std::pair<Vec2i64, Vec2i64>> edges;
		mesh.meshEdgesNear(probe - Vec2i64{3000, 3000}, probe + Vec2i64{3000, 3000}, edgeIds, edges);
		for (const auto& [start, end] : edges) {
			hash.add(start);
			hash.add(end);
		}
		return hash.value;
	}

	// A point of a history's ground, on a 250 mm lattice one time in three so edges share
	// lines and corners.
	inline Vec2i64 historyPoint(std::mt19937& rng, std::int64_t reach) {
		const bool		   lattice = historyPick(rng, 3) == 0;
		const std::int64_t x	   = historyBetween(rng, -reach, reach);
		const std::int64_t y	   = historyBetween(rng, -reach, reach);
		return lattice ? Vec2i64{x / 250 * 250, y / 250 * 250} : Vec2i64{x, y};
	}

	// A beam of sight: an apex and two to five of the limit directions at a reach.
	inline std::vector<Vec2i64> historyBeam(std::mt19937& rng, std::int64_t spread) {
		const Vec2i64		 apex  = historyPoint(rng, spread);
		const std::size_t	 first = historyPick(rng, kLimitSides);
		const std::size_t	 span  = 1 + historyPick(rng, 4);
		const std::int64_t	 reach = historyBetween(rng, 3000, 14000);
		std::vector<Vec2i64> ring{apex};
		for (std::size_t k = 0; k <= span; ++k) {
			const auto& [cosine, sine] = kLimitDirections[(first + k) % kLimitSides];
			ring.push_back({apex.x + roundedAway(cosine * reach, kLimitDirectionScale), apex.y + roundedAway(sine * reach, kLimitDirectionScale)});
		}
		return ring;
	}

	inline std::int64_t historySquareRoot(std::int64_t value) {
		std::int64_t root = 0;
		for (std::int64_t step = std::int64_t{1} << 31; step > 0; step >>= 1) {
			if ((root + step) * (root + step) <= value) {
				root += step;
			}
		}
		return root;
	}

	// Whether no two edges of the ring meet but neighbours at their shared corner.
	inline bool historySimple(const std::vector<Vec2i64>& ring) {
		const std::size_t corners = ring.size();
		if (corners < 3) {
			return false;
		}
		for (std::size_t i = 0; i < corners; ++i) {
			if (ring[i] == ring[(i + 1) % corners]) {
				return false;
			}
			for (std::size_t j = i + 1; j < corners; ++j) {
				const bool neighbours = j == i + 1 || (i == 0 && j == corners - 1);
				const SegmentRelation relation = intersectSegments(ring[i], ring[(i + 1) % corners], ring[j], ring[(j + 1) % corners]).relation;
				if (relation == SegmentRelation::Disjoint || (neighbours && relation == SegmentRelation::EndpointTouch)) {
					continue;
				}
				return false;
			}
		}
		return true;
	}

	// About `count` corners in angular order round `centre`, at radii between `nearest` and
	// `farthest`: most on the rim at `farthest`, runs pulled in as a shadow pulls a sight
	// polygon in. Corners in one direction from the centre are dropped, so the ring is
	// star-shaped round it; one with a gap of half a turn or more is drawn again.
	inline std::vector<Vec2i64> historyStar(std::mt19937& rng, const Vec2i64& centre, std::size_t count, std::int64_t nearest, std::int64_t farthest) {
		for (;;) {
			std::vector<Vec2i64> directions;
			for (std::size_t i = 0; i < count; ++i) {
				const std::int64_t towardX = historyBetween(rng, -1000, 1000);
				const std::int64_t towardY = historyBetween(rng, -1000, 1000);
				if (towardX != 0 || towardY != 0) {
					directions.push_back({towardX, towardY});
				}
			}
			std::vector<Vec2i64> ring;
			std::int64_t		 radius = farthest;
			for (const Vec2i64& toward : directions) {
				if (historyPick(rng, 6) == 0) {
					radius = radius == farthest ? historyBetween(rng, nearest, farthest - 1) : farthest;
				}
				const std::int64_t length = historySquareRoot(toward.x * toward.x + toward.y * toward.y);
				ring.push_back({centre.x + toward.x * radius / length, centre.y + toward.y * radius / length});
			}
			std::sort(ring.begin(), ring.end(), [&](const Vec2i64& first, const Vec2i64& second) { return angleLess(first - centre, second - centre); });
			ring.erase(std::unique(ring.begin(), ring.end(),
								   [&](const Vec2i64& first, const Vec2i64& second) { return !angleLess(first - centre, second - centre) && !angleLess(second - centre, first - centre); }),
					   ring.end());
			bool everyGapUnderHalf = ring.size() >= 3;
			for (std::size_t i = 0; i < ring.size() && everyGapUnderHalf; ++i) {
				everyGapUnderHalf = orientation(centre, ring[i], ring[(i + 1) % ring.size()]) == Orientation::CounterClockwise;
			}
			if (everyGapUnderHalf) {
				return ring;
			}
		}
	}

	// A beam of sight with many corners: an apex and a star's corners round it that span
	// under half a turn, in order.
	inline std::vector<Vec2i64> historyWideBeam(std::mt19937& rng, std::int64_t spread) {
		const Vec2i64			   apex	 = historyPoint(rng, spread);
		const std::size_t		   count = 34 + historyPick(rng, 60);
		const std::int64_t		   reach = historyBetween(rng, 4000, 14000);
		const std::vector<Vec2i64> star	 = historyStar(rng, apex, count, reach / 3, reach);
		const std::size_t		   first = historyPick(rng, star.size());
		std::vector<Vec2i64>	   ring{apex, star[first]};
		for (std::size_t i = 1; i < star.size(); ++i) {
			const Vec2i64& next = star[(first + i) % star.size()];
			if (orientation(apex, star[first], next) != Orientation::CounterClockwise) {
				break;
			}
			ring.push_back(next);
		}
		return ring;
	}

	// Rings whose corners and centres lie within `spread` of the origin: limits, beams,
	// lattice rectangles and quadrilaterals as ground, and rings of 17 to 70
	// corners, beams of many corners and sight-sized pieces of about 136; small squares and
	// limit-shaped polygons as obstacles; and square water.
	inline NavInputPolygon historyRing(std::mt19937& rng, std::int64_t spread) {
		switch (historyPick(rng, 10)) {
			case 0: {
				const Vec2i64	   centre = historyPoint(rng, spread);
				const std::int64_t range  = historyBetween(rng, 1500, 9000);
				return NavInputPolygon{limitPolygon(centre, range), false, kHistoryBorder};
			}
			case 1:
				return NavInputPolygon{historyBeam(rng, spread), false, kHistoryBorder};
			case 2: {
				const Vec2i64	   corner = historyPoint(rng, spread);
				const std::int64_t width  = 250 * historyBetween(rng, 1, 16);
				const std::int64_t height = 250 * historyBetween(rng, 1, 16);
				return NavInputPolygon{historyRect(corner.x, corner.y, corner.x + width, corner.y + height), false, kHistoryBorder};
			}
			case 3: {
				const Vec2i64		 centre = historyPoint(rng, spread);
				std::vector<Vec2i64> ring;
				for (const auto& [signX, signY] : std::array<std::pair<int, int>, 4>{{{1, 1}, {-1, 1}, {-1, -1}, {1, -1}}}) {
					const std::int64_t along  = historyBetween(rng, 200, 3000);
					const std::int64_t across = historyBetween(rng, 200, 3000);
					ring.push_back({centre.x + signX * along, centre.y + signY * across});
				}
				return NavInputPolygon{ring, false, kHistoryBorder};
			}
			case 4: {
				const Vec2i64	   corner = historyPoint(rng, spread + 1000);
				const std::int64_t side	  = historyBetween(rng, 150, 1200);
				return NavInputPolygon{historyRect(corner.x, corner.y, corner.x + side, corner.y + side), true, kHistoryObstacle};
			}
			case 5: {
				const Vec2i64	   centre = historyPoint(rng, spread + 1000);
				const std::int64_t range  = historyBetween(rng, 150, 900);
				return NavInputPolygon{limitPolygon(centre, range), true, kHistoryObstacle};
			}
			case 6: {
				const Vec2i64	   corner = historyPoint(rng, spread);
				const std::int64_t side	  = 250 * historyBetween(rng, 4, 16);
				return NavInputPolygon{historyRect(corner.x, corner.y, corner.x + side, corner.y + side), true, kHistoryWater, kNoOpening, true};
			}
			case 7: {
				const Vec2i64	  centre = historyPoint(rng, spread);
				const std::size_t count	 = 17 + historyPick(rng, 54);
				const std::int64_t reach  = historyBetween(rng, 1500, 9000);
				return NavInputPolygon{historyStar(rng, centre, count, 800, reach), false, kHistoryBorder};
			}
			case 8:
				return NavInputPolygon{historyWideBeam(rng, spread), false, kHistoryBorder};
			default: {
				const Vec2i64	  centre = historyPoint(rng, spread);
				const std::size_t count	 = 130 + historyPick(rng, 12);
				const std::int64_t reach  = historyBetween(rng, 6000, 14000);
				return NavInputPolygon{historyStar(rng, centre, count, 2000, reach), false, kHistoryBorder};
			}
		}
	}

	// Grows or shrinks the ring by one corner, when it stays simple.
	inline std::vector<Vec2i64> historyReshaped(std::mt19937& rng, std::vector<Vec2i64> ring) {
		const std::size_t at = historyPick(rng, ring.size());
		if (historyPick(rng, 2) == 0 && ring.size() > 3) {
			ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(at));
		} else {
			const Vec2i64&	   start  = ring[at];
			const Vec2i64&	   end	  = ring[(at + 1) % ring.size()];
			const std::int64_t nudgeX = historyBetween(rng, -2, 2);
			const std::int64_t nudgeY = historyBetween(rng, -2, 2);
			const Vec2i64	   middle{(start.x + end.x) / 2 + nudgeX, (start.y + end.y) / 2 + nudgeY};
			ring.insert(ring.begin() + static_cast<std::ptrdiff_t>(at + 1), middle);
		}
		return ring;
	}

	// One generated history: patches of new rings, now and then removing or moving up to
	// three live ones (a move may also add or drop a corner), each followed by a width
	// refresh. One seed in 200 stacks eight to sixteen rings a patch within 1.5 m of the
	// origin, a few hundred over one spot by the end. `step` takes the state hash and
	// validate()'s answer after each patch.
	template <typename Step> void runHistory(std::uint32_t seed, int patches, Step step) {
		std::mt19937		rng(seed);
		PatchableMesh		mesh;
		std::vector<RingId> live;
		RingId				ringBound			= 0;
		const bool			stacked				= seed % 200 == 0;
		const std::int64_t	spread				= stacked ? 1500 : 9000;
		const std::uint64_t firstEpoch			= mesh.epoch();
		const std::uint64_t firstComponentEpoch = mesh.components().epoch();
		for (int patch = 0; patch < patches; ++patch) {
			PatchEdits		  edits;
			const std::size_t adds = stacked ? 8 + historyPick(rng, 9) : 1 + historyPick(rng, 3);
			for (std::size_t i = 0; i < adds; ++i) {
				edits.adds.push_back(historyRing(rng, spread));
			}
			if (live.size() > 6 && historyPick(rng, 3) == 0) {
				const std::size_t removes = 1 + historyPick(rng, 3);
				for (std::size_t i = 0; i < removes; ++i) {
					const std::size_t at = historyPick(rng, live.size());
					edits.removes.push_back(live[at]);
					live.erase(live.begin() + static_cast<std::ptrdiff_t>(at));
				}
			}
			if (live.size() > 6 && historyPick(rng, 3) == 0) {
				const std::size_t moves = 1 + historyPick(rng, 3);
				for (std::size_t i = 0; i < moves; ++i) {
					const RingId ring = live[historyPick(rng, live.size())];
					if (std::any_of(edits.moves.begin(), edits.moves.end(), [&](const RingMove& move) { return move.ring == ring; })) {
						continue;
					}
					const bool		   lattice = historyPick(rng, 2) == 0;
					const std::int64_t shiftX  = lattice ? 250 * historyBetween(rng, -4, 4) : historyBetween(rng, -1500, 1500);
					const std::int64_t shiftY  = lattice ? 250 * historyBetween(rng, -4, 4) : historyBetween(rng, -1500, 1500);
					std::vector<Vec2i64> moved = mesh.ring(ring).polygon.ring;
					for (Vec2i64& point : moved) {
						point = point + Vec2i64{shiftX, shiftY};
					}
					if (historyPick(rng, 3) == 0) {
						std::vector<Vec2i64> reshaped = historyReshaped(rng, moved);
						if (historySimple(reshaped)) {
							moved = std::move(reshaped);
						}
					}
					edits.moves.push_back({ring, std::move(moved)});
				}
			}
			const PatchReport report  = mesh.applyPatch(edits);
			const PatchReport refresh = mesh.refreshWidths();
			for (const RingId ring : report.added) {
				live.push_back(ring);
				ringBound = std::max(ringBound, ring + 1);
			}
			const Vec2i64 probe = historyPoint(rng, spread);
			step(historyStateHash(mesh, ringBound, report, refresh, firstEpoch, firstComponentEpoch, probe), mesh.validate());
		}
	}

} // namespace geometry::nav::testdata
