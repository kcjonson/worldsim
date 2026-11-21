#include "NavInputBuilder.h"

#include "construction/OpeningGeometry.h"

#include <assets/ConstructionRegistry.h>
#include <construction/ConstructionWorld.h>

#include <core/Int128.h>
#include <core/Vec2i64.h>
#include <nav/NavMesh.h>
#include <offset/WallOffset.h>
#include <polygon/Polygon.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

// nav::extractWalls pinned against a copy of itself as it stood before WOR-876, over the wall
// layouts that change touches. Each layout says how it may differ (Expectation): not at all, by a
// millimetre in the rings of a door's wall, or on purpose, which its own check then states.

namespace {

	namespace gnav = geometry::nav;

	using assets::ConstructionRegistry;
	using construction::ConstructionWorld;
	using construction::FoundationState;
	using construction::kInvalidFoundation;
	using construction::OpeningId;
	using construction::SegmentCommitResult;
	using construction::SegmentId;
	using geometry::Ring;
	using geometry::Vec2i64;

	namespace reference {

		void extractWalls(const ConstructionWorld& world, const ConstructionRegistry& registry,
						  std::vector<gnav::NavInputPolygon>& outPolys, std::vector<gnav::DoorPortal>& outDoors,
						  std::vector<nav::WallJunction>* outJunctions) {
			std::vector<geometry::WallSegment>	 geoSegments;
			std::vector<construction::SegmentId> segmentIds;
			std::vector<construction::VertexId>	 segmentStarts;
			std::vector<construction::VertexId>	 segmentEnds;
			for (const construction::WallSegment& seg : world.segments()) {
				if (seg.state != FoundationState::Built) {
					continue;
				}
				const construction::Vertex* v0 = world.getVertex(seg.v0);
				const construction::Vertex* v1 = world.getVertex(seg.v1);
				if (v0 == nullptr || v1 == nullptr) {
					continue;
				}
				const auto*		   preset = registry.getThicknessPreset(seg.material, seg.thicknessPreset);
				const std::int64_t half	  = (preset != nullptr) ? preset->halfThicknessMm : 0;
				if (half <= 0) {
					continue;
				}
				geoSegments.push_back({v0->pos, v1->pos, half});
				segmentIds.push_back(seg.id);
				segmentStarts.push_back(seg.v0);
				segmentEnds.push_back(seg.v1);
			}

			if (geoSegments.empty()) {
				return;
			}

			const geometry::WallBands bands	  = geometry::resolveWallBands(geoSegments, geometry::kDefaultMiterLimit);
			const bool				  bandsOk = bands.status == geometry::OffsetStatus::Ok && bands.bands.size() == geoSegments.size();

			std::unordered_map<construction::SegmentId, std::size_t> segIndexById;
			for (std::size_t i = 0; i < segmentIds.size(); ++i) {
				segIndexById[segmentIds[i]] = i;
			}

			struct DoorCut {
				construction::OpeningId openingId;
				std::int64_t			clearWidthMm;
				Vec2i64					jambA;
				Vec2i64					jambB;
				float					t0;
				float					t1;
			};
			std::unordered_map<std::size_t, std::vector<DoorCut>> cutsBySeg;

			for (const construction::Opening& op : world.openings()) {
				if (op.state != FoundationState::Built) {
					continue;
				}
				const auto* type = registry.getOpeningType(op.type);
				if (type == nullptr) {
					continue;
				}
				const Ring footprint = construction::openingFootprint(world, op);
				if (footprint.size() < 4) {
					continue;
				}
				const Vec2i64 aLeft	 = footprint[0];
				const Vec2i64 bLeft	 = footprint[1];
				const Vec2i64 bRight = footprint[2];
				const Vec2i64 aRight = footprint[3];
				const Vec2i64 jambA{(aLeft.x + aRight.x) / 2, (aLeft.y + aRight.y) / 2};
				const Vec2i64 jambB{(bLeft.x + bRight.x) / 2, (bLeft.y + bRight.y) / 2};

				if (!type->pathable) {
					outDoors.push_back({static_cast<std::int64_t>(op.id), jambA, jambB, 0});
					continue;
				}
				auto idxIt = segIndexById.find(op.segment);
				if (idxIt == segIndexById.end()) {
					continue;
				}

				const construction::WallSegment* seg = world.getSegment(op.segment);
				const construction::Vertex*		 v0	 = (seg != nullptr) ? world.getVertex(seg->v0) : nullptr;
				const construction::Vertex*		 v1	 = (seg != nullptr) ? world.getVertex(seg->v1) : nullptr;
				if (v0 == nullptr || v1 == nullptr) {
					continue;
				}
				const double dx		  = static_cast<double>(v1->pos.x - v0->pos.x);
				const double dy		  = static_cast<double>(v1->pos.y - v0->pos.y);
				const double lengthMm = std::sqrt(dx * dx + dy * dy);
				if (lengthMm <= 0.0) {
					continue;
				}
				const float halfExtent = static_cast<float>((static_cast<double>(type->widthMm) * 0.5) / lengthMm);
				const float t0		   = std::clamp(op.t - halfExtent, 0.0F, 1.0F);
				const float t1		   = std::clamp(op.t + halfExtent, 0.0F, 1.0F);

				cutsBySeg[idxIt->second].push_back({op.id, type->widthMm, jambA, jambB, t0, t1});
				outDoors.push_back({static_cast<std::int64_t>(op.id), jambA, jambB, type->widthMm});
			}

			for (std::size_t i = 0; i < geoSegments.size(); ++i) {
				const geometry::WallSegment& gs	  = geoSegments[i];
				const std::int64_t			 prov = static_cast<std::int64_t>(segmentIds[i]);

				auto cutIt = cutsBySeg.find(i);
				if (!bandsOk || cutIt == cutsBySeg.end()) {
					Ring ring = bandsOk ? bands.bands[i] : geometry::band(gs.a, gs.b, gs.halfThicknessMm);
					if (ring.size() >= 3) {
						outPolys.push_back({std::move(ring), true, prov});
					}
					continue;
				}

				std::vector<DoorCut> cuts = cutIt->second;
				std::sort(cuts.begin(), cuts.end(), [](const DoorCut& l, const DoorCut& r) { return l.t0 < r.t0; });

				auto lerp = [&](float t) -> Vec2i64 {
					const double ax = static_cast<double>(gs.a.x);
					const double ay = static_cast<double>(gs.a.y);
					const double bx = static_cast<double>(gs.b.x);
					const double by = static_cast<double>(gs.b.y);
					return {static_cast<std::int64_t>(std::llround(ax + (bx - ax) * t)),
							static_cast<std::int64_t>(std::llround(ay + (by - ay) * t))};
				};

				auto emitSpan = [&](float ta, float tb, std::int64_t openingId) {
					if (tb <= ta + 1e-6F) {
						return;
					}
					Ring ring = geometry::band(lerp(ta), lerp(tb), gs.halfThicknessMm);
					if (ring.size() >= 3 && geometry::windingOrder(ring) != geometry::Winding::Degenerate) {
						outPolys.push_back({std::move(ring), true, prov, openingId});
					}
				};

				float cursor = 0.0F;
				for (const DoorCut& cut : cuts) {
					emitSpan(cursor, cut.t0, gnav::kNoOpening);
					emitSpan(cut.t0, cut.t1, static_cast<std::int64_t>(cut.openingId));
					cursor = std::max(cursor, cut.t1);
				}
				emitSpan(cursor, 1.0F, gnav::kNoOpening);
			}

			if (bandsOk) {
				for (std::size_t ji = 0; ji < bands.junctions.size(); ++ji) {
					const Ring& j = bands.junctions[ji];
					if (j.size() < 3) {
						continue;
					}
					std::map<construction::VertexId, std::size_t> endsAt;
					gnav::NavInputPolygon						  polygon{.ring = j, .blocked = true};
					std::vector<construction::SegmentId>		  incident;
					if (ji < bands.junctionSegments.size()) {
						for (std::size_t segIdx : bands.junctionSegments[ji]) {
							if (segIdx < segmentIds.size()) {
								incident.push_back(segmentIds[segIdx]);
								polygon.blockerGroup.push_back(static_cast<std::int64_t>(segmentIds[segIdx]));
								++endsAt[segmentStarts[segIdx]];
								++endsAt[segmentEnds[segIdx]];
							}
						}
					}
					for (const auto& [vertex, count] : endsAt) {
						if (count == incident.size()) {
							polygon.provenanceId = nav::junctionTag(vertex);
						}
					}
					if (outJunctions != nullptr) {
						outJunctions->push_back({outPolys.size(), std::move(incident)});
					}
					outPolys.push_back(std::move(polygon));
				}
			}
		}

	} // namespace reference

	std::filesystem::path projectRoot() {
		std::filesystem::path p = __FILE__;
		return p.parent_path().parent_path().parent_path().parent_path();
	}

	SegmentId buildWall(ConstructionWorld& world, Vec2i64 a, Vec2i64 b, const std::string& thickness = "Standard") {
		const SegmentCommitResult result = world.commitSegment(a, b, "Wood", thickness, kInvalidFoundation, world.version());
		EXPECT_TRUE(result.ok());
		for (const SegmentId id : result.createdSegments) {
			world.setSegmentState(id, FoundationState::Built);
		}
		return result.id;
	}

	void buildOpening(ConstructionWorld& world, SegmentId segment, float t, const std::string& type) {
		const OpeningId opening = world.addOpening(segment, t, type, "Wood");
		ASSERT_NE(opening, construction::kInvalidOpening);
		world.setOpeningState(opening, FoundationState::Built);
	}

	void buildRoom(ConstructionWorld& world, Vec2i64 corner, std::vector<SegmentId>& walls) {
		const Vec2i64 corners[] = {corner, {corner.x + 4000, corner.y}, {corner.x + 4000, corner.y + 3000}, {corner.x, corner.y + 3000}};
		for (std::size_t i = 0; i < 4; ++i) {
			walls.push_back(buildWall(world, corners[i], corners[(i + 1) % 4]));
		}
	}

	using PolygonKey = std::tuple<std::int64_t, std::int64_t, bool, bool, bool, Ring, std::vector<std::int64_t>>;
	using JunctionKey = std::pair<Ring, std::vector<SegmentId>>;
	using PortalKey	 = std::tuple<std::int64_t, Vec2i64, Vec2i64, std::int64_t>;

	// What extractWalls gives nav, independent of the order it lists things in. A junction is
	// named by its ring rather than its index in the polygon list.
	struct WallOutput {
		std::vector<PolygonKey>	 polygons;
		std::vector<JunctionKey> junctions;
		std::vector<PortalKey>	 portals;
	};

	using Extractor = std::function<void(const ConstructionWorld&, const ConstructionRegistry&, std::vector<gnav::NavInputPolygon>&,
										 std::vector<gnav::DoorPortal>&, std::vector<nav::WallJunction>*)>;

	// How a layout's output may differ from the reference's.
	enum class Expectation {
		Identical,
		// On the layouts marked so, the rings of a segment hosting a door move by up to a millimetre
		// per axis (the reference cuts the door gap at float fractions of the segment, nav at whole
		// millimetres along it). That holds for a door clear of the corners; nav clamps a door span
		// to the trimmed band and may widen it by up to 6 mm over a sliver, which the Changed layouts
		// check. The rest is identical.
		DoorRingsWithinMillimetre,
		// Drawn differently on purpose; `checkChange` says how.
		Changed,
	};

	struct Layout {
		std::string								name;
		std::function<void(ConstructionWorld&)> build;
		Expectation								expectation = Expectation::Identical;
		std::function<void(const Layout&)>		checkChange;
	};

	WallOutput outputOf(const ConstructionWorld& world, const Extractor& extract) {
		std::vector<gnav::NavInputPolygon> polygons;
		std::vector<gnav::DoorPortal>	   portals;
		std::vector<nav::WallJunction>	   junctions;
		extract(world, ConstructionRegistry::Get(), polygons, portals, &junctions);

		WallOutput output;
		for (const gnav::NavInputPolygon& polygon : polygons) {
			std::vector<std::int64_t> group = polygon.blockerGroup;
			std::sort(group.begin(), group.end());
			output.polygons.emplace_back(polygon.provenanceId, polygon.openingId, polygon.blocked, polygon.holeCapable, polygon.coverOnly,
										 polygon.ring, std::move(group));
		}
		for (const nav::WallJunction& junction : junctions) {
			EXPECT_LT(junction.polygon, polygons.size());
			if (junction.polygon < polygons.size()) {
				output.junctions.emplace_back(polygons[junction.polygon].ring, junction.walls);
			}
		}
		for (const gnav::DoorPortal& portal : portals) {
			output.portals.emplace_back(portal.openingId, portal.a, portal.b, portal.clearWidthMm);
		}
		std::sort(output.polygons.begin(), output.polygons.end());
		std::sort(output.junctions.begin(), output.junctions.end());
		std::sort(output.portals.begin(), output.portals.end());
		return output;
	}

	// Every segment, built or not, as the offsetter takes it, in the world's order.
	std::vector<geometry::WallSegment> wallSegments(const ConstructionWorld& world) {
		std::vector<geometry::WallSegment> segments;
		for (const construction::WallSegment& seg : world.segments()) {
			const auto* preset = ConstructionRegistry::Get().getThicknessPreset(seg.material, seg.thicknessPreset);
			segments.push_back({world.getVertex(seg.v0)->pos, world.getVertex(seg.v1)->pos, preset->halfThicknessMm});
		}
		return segments;
	}

	std::set<SegmentId> doorSegments(const ConstructionWorld& world) {
		std::set<SegmentId> segments;
		for (const construction::Opening& opening : world.openings()) {
			const auto* type = ConstructionRegistry::Get().getOpeningType(opening.type);
			if (opening.state == FoundationState::Built && type != nullptr && type->pathable) {
				segments.insert(opening.segment);
			}
		}
		return segments;
	}

	std::int64_t provenanceOf(const PolygonKey& polygon) {
		return std::get<0>(polygon);
	}
	std::int64_t openingOf(const PolygonKey& polygon) {
		return std::get<1>(polygon);
	}
	const Ring& ringOf(const PolygonKey& polygon) {
		return std::get<5>(polygon);
	}

	// The rings of segments hosting a door, junctions aside, and every other polygon.
	struct SplitOutput {
		std::vector<PolygonKey> doorSegmentRings;
		std::vector<PolygonKey> rest;
	};

	SplitOutput splitDoorSegmentRings(const WallOutput& output, const std::set<SegmentId>& doors) {
		SplitOutput split;
		for (const PolygonKey& polygon : output.polygons) {
			const bool junction = std::any_of(output.junctions.begin(), output.junctions.end(),
											  [&](const JunctionKey& j) { return j.first == ringOf(polygon); });
			const bool onDoorSegment = provenanceOf(polygon) > 0 && doors.contains(static_cast<SegmentId>(provenanceOf(polygon)));
			(onDoorSegment && !junction ? split.doorSegmentRings : split.rest).push_back(polygon);
		}
		return split;
	}

	// The largest per-axis difference between two rings with the same corners, whichever corner each
	// starts from; none when their corner counts differ.
	std::optional<std::int64_t> largestDifference(const Ring& actual, const Ring& expected) {
		if (actual.size() != expected.size()) {
			return std::nullopt;
		}
		std::int64_t best = std::numeric_limits<std::int64_t>::max();
		for (std::size_t start = 0; start < actual.size(); ++start) {
			std::int64_t worst = 0;
			for (std::size_t i = 0; i < actual.size(); ++i) {
				const Vec2i64& a = actual[(start + i) % actual.size()];
				const Vec2i64& e = expected[i];
				worst			 = std::max(worst, std::max<std::int64_t>(std::llabs(a.x - e.x), std::llabs(a.y - e.y)));
			}
			best = std::min(best, worst);
		}
		return best;
	}

	std::string describe(const Ring& ring) {
		std::string text;
		for (const Vec2i64& corner : ring) {
			text += "(" + std::to_string(corner.x) + ", " + std::to_string(corner.y) + ") ";
		}
		return text;
	}

	void expectDoorRingsWithinMillimetre(const WallOutput& actual, const WallOutput& expected, const std::set<SegmentId>& doors) {
		SplitOutput actualSplit	  = splitDoorSegmentRings(actual, doors);
		SplitOutput expectedSplit = splitDoorSegmentRings(expected, doors);
		EXPECT_EQ(actualSplit.rest, expectedSplit.rest);
		EXPECT_EQ(actual.junctions, expected.junctions);
		EXPECT_EQ(actual.portals, expected.portals);

		// Paired by wall, opening, and lowest corner, which a millimetre cannot reorder on these layouts.
		auto byPlace = [](const PolygonKey& first, const PolygonKey& second) {
			const Vec2i64 lowestOfFirst	 = *std::min_element(ringOf(first).begin(), ringOf(first).end());
			const Vec2i64 lowestOfSecond = *std::min_element(ringOf(second).begin(), ringOf(second).end());
			return std::tuple(provenanceOf(first), openingOf(first), lowestOfFirst) <
				   std::tuple(provenanceOf(second), openingOf(second), lowestOfSecond);
		};
		std::sort(actualSplit.doorSegmentRings.begin(), actualSplit.doorSegmentRings.end(), byPlace);
		std::sort(expectedSplit.doorSegmentRings.begin(), expectedSplit.doorSegmentRings.end(), byPlace);
		ASSERT_EQ(actualSplit.doorSegmentRings.size(), expectedSplit.doorSegmentRings.size());
		ASSERT_FALSE(actualSplit.doorSegmentRings.empty());
		for (std::size_t i = 0; i < actualSplit.doorSegmentRings.size(); ++i) {
			const PolygonKey& a = actualSplit.doorSegmentRings[i];
			const PolygonKey& e = expectedSplit.doorSegmentRings[i];
			EXPECT_EQ(std::get<0>(a), std::get<0>(e));
			EXPECT_EQ(std::get<1>(a), std::get<1>(e));
			EXPECT_EQ(std::get<2>(a), std::get<2>(e));
			EXPECT_EQ(std::get<3>(a), std::get<3>(e));
			EXPECT_EQ(std::get<4>(a), std::get<4>(e));
			EXPECT_EQ(std::get<6>(a), std::get<6>(e));
			const std::optional<std::int64_t> difference = largestDifference(ringOf(a), ringOf(e));
			ASSERT_TRUE(difference.has_value()) << describe(ringOf(a)) << "against " << describe(ringOf(e));
			EXPECT_LE(*difference, 1) << describe(ringOf(a)) << "against " << describe(ringOf(e));
		}
	}

	struct PlanarPoint {
		double x = 0.0;
		double y = 0.0;
	};

	// Left of the line a->b is positive.
	double side(PlanarPoint a, PlanarPoint b, PlanarPoint p) {
		return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
	}

	// The part of `subject` inside the convex counter-clockwise `clip` (Sutherland-Hodgman).
	std::vector<PlanarPoint> clipToConvex(const Ring& subject, const Ring& clip) {
		std::vector<PlanarPoint> kept;
		for (const Vec2i64& p : subject) {
			kept.push_back({static_cast<double>(p.x), static_cast<double>(p.y)});
		}
		for (std::size_t i = 0; i < clip.size() && !kept.empty(); ++i) {
			const PlanarPoint		 a{static_cast<double>(clip[i].x), static_cast<double>(clip[i].y)};
			const Vec2i64&			 following = clip[(i + 1) % clip.size()];
			const PlanarPoint		 b{static_cast<double>(following.x), static_cast<double>(following.y)};
			std::vector<PlanarPoint> next;
			for (std::size_t k = 0; k < kept.size(); ++k) {
				const PlanarPoint p	 = kept[k];
				const PlanarPoint q	 = kept[(k + 1) % kept.size()];
				const double	  sp = side(a, b, p);
				const double	  sq = side(a, b, q);
				if (sp >= 0.0) {
					next.push_back(p);
				}
				if ((sp >= 0.0) != (sq >= 0.0)) {
					const double along = sp / (sp - sq);
					next.push_back({p.x + (q.x - p.x) * along, p.y + (q.y - p.y) * along});
				}
			}
			kept = std::move(next);
		}
		return kept;
	}

	// How deep a convex ring overlaps the convex counter-clockwise `convexRing`: the least width of
	// their intersection, zero where they only touch.
	double overlapDepth(const Ring& ring, const Ring& convexRing) {
		const std::vector<PlanarPoint> overlap = clipToConvex(ring, convexRing);
		if (overlap.size() < 3) {
			return 0.0;
		}
		double least = std::numeric_limits<double>::max();
		for (std::size_t i = 0; i < overlap.size(); ++i) {
			const PlanarPoint a		 = overlap[i];
			const PlanarPoint b		 = overlap[(i + 1) % overlap.size()];
			const double	  length = std::hypot(b.x - a.x, b.y - a.y);
			if (length < 1e-9) {
				continue;
			}
			double widest = 0.0;
			for (const PlanarPoint p : overlap) {
				widest = std::max(widest, std::abs(side(a, b, p)) / length);
			}
			least = std::min(least, widest);
		}
		return least == std::numeric_limits<double>::max() ? 0.0 : least;
	}

	double deepestIntoJunction(const std::vector<PolygonKey>& rings, const WallOutput& output) {
		double deepest = 0.0;
		for (const PolygonKey& ring : rings) {
			for (const JunctionKey& junction : output.junctions) {
				deepest = std::max(deepest, overlapDepth(junction.first, ringOf(ring)));
			}
		}
		return deepest;
	}

	// The offsetter's seam tolerance (kSeamToleranceMm in WallOffset.cpp): how far two of its rings may
	// meet off one another.
	constexpr double kSeamToleranceMm = 3.0;

	// The shortest run of wall the offsetter keeps beside a gap (kMinPieceFeatureMm in WallOffset.cpp).
	constexpr double kMinPieceFeatureMm = 2.0 * kSeamToleranceMm;

	// A door segment beside a corner is drawn from its trimmed band: its rings tile the trimmed band
	// and reach no deeper into a junction than the seam tolerance, the door span carries the door and
	// the solid spans no opening, and every other polygon, junction, and portal is the reference's.
	void expectDoorSegmentsFromTrimmedBands(const Layout& layout) {
		ConstructionWorld world;
		layout.build(world);
		const WallOutput		  expected = outputOf(world, reference::extractWalls);
		const WallOutput		  actual   = outputOf(world, nav::extractWalls);
		const std::set<SegmentId> doors	   = doorSegments(world);
		ASSERT_FALSE(doors.empty());
		const SplitOutput actualSplit	= splitDoorSegmentRings(actual, doors);
		const SplitOutput expectedSplit = splitDoorSegmentRings(expected, doors);
		EXPECT_EQ(actualSplit.rest, expectedSplit.rest);
		EXPECT_EQ(actual.junctions, expected.junctions);
		EXPECT_EQ(actual.portals, expected.portals);

		EXPECT_GT(deepestIntoJunction(expectedSplit.doorSegmentRings, expected), kSeamToleranceMm) << "the reference overlaps a junction";
		EXPECT_LE(deepestIntoJunction(actualSplit.doorSegmentRings, actual), kSeamToleranceMm);

		const geometry::WallBands bands = geometry::resolveWallBands(wallSegments(world), geometry::kDefaultMiterLimit);
		ASSERT_EQ(bands.status, geometry::OffsetStatus::Ok);
		for (std::size_t index = 0; index < world.segments().size(); ++index) {
			const SegmentId segment = world.segments()[index].id;
			if (!doors.contains(segment)) {
				continue;
			}
			std::set<std::int64_t> doorOpenings;
			for (const construction::Opening& opening : world.openings()) {
				if (opening.segment == segment && ConstructionRegistry::Get().getOpeningType(opening.type)->pathable) {
					doorOpenings.insert(static_cast<std::int64_t>(opening.id));
				}
			}
			const Vec2i64 start	 = world.getVertex(world.segments()[index].v0)->pos;
			const Vec2i64 finish = world.getVertex(world.segments()[index].v1)->pos;
			const double  length = std::hypot(static_cast<double>(finish.x - start.x), static_cast<double>(finish.y - start.y));
			// Distance along the wall from its v0 end, in mm.
			auto along = [&](const Vec2i64& point) {
				return (static_cast<double>(point.x - start.x) * static_cast<double>(finish.x - start.x) +
						static_cast<double>(point.y - start.y) * static_cast<double>(finish.y - start.y)) /
					   length;
			};
			const double bandFrom = static_cast<double>(bands.trimAtA[index]);
			const double bandTo	  = length - static_cast<double>(bands.trimAtB[index]);

			// Exact: the spans share their corners in whole millimetres, so their doubled areas sum to the band's.
			geometry::Int128			areaDoubled(0);
			int							solid = 0;
			std::map<std::int64_t, int> spansOfOpening;
			for (const PolygonKey& ring : actualSplit.doorSegmentRings) {
				if (provenanceOf(ring) != static_cast<std::int64_t>(segment)) {
					continue;
				}
				areaDoubled = areaDoubled + geometry::signedAreaDoubled(ringOf(ring));
				if (openingOf(ring) == gnav::kNoOpening) {
					++solid;
					continue;
				}
				EXPECT_TRUE(doorOpenings.contains(openingOf(ring))) << "a span tagged with an opening not a door on its wall";
				++spansOfOpening[openingOf(ring)];

				// The door span sits over its door: between the portal's jambs, clamped to the trimmed band.
				const auto portal = std::find_if(actual.portals.begin(), actual.portals.end(),
												 [&](const PortalKey& key) { return std::get<0>(key) == openingOf(ring); });
				ASSERT_NE(portal, actual.portals.end());
				const double jambs[] = {along(std::get<1>(*portal)), along(std::get<2>(*portal))};
				const double jambFrom = std::max(std::min(jambs[0], jambs[1]), bandFrom);
				const double jambTo	  = std::min(std::max(jambs[0], jambs[1]), bandTo);
				// A run of wall under the shortest kept between a jamb and the band's end is the door span's.
				const double from = jambFrom - bandFrom < kMinPieceFeatureMm ? bandFrom : jambFrom;
				const double to	  = bandTo - jambTo < kMinPieceFeatureMm ? bandTo : jambTo;
				double		 spanFrom = std::numeric_limits<double>::max();
				double		 spanTo	  = std::numeric_limits<double>::lowest();
				for (const Vec2i64& corner : ringOf(ring)) {
					spanFrom = std::min(spanFrom, along(corner));
					spanTo	 = std::max(spanTo, along(corner));
				}
				EXPECT_NEAR(spanFrom, from, 1.0) << "door " << openingOf(ring) << " starts where its jamb does";
				EXPECT_NEAR(spanTo, to, 1.0) << "door " << openingOf(ring) << " ends where its jamb does";
			}
			EXPECT_GE(solid, 1);
			for (const std::int64_t opening : doorOpenings) {
				EXPECT_EQ(spansOfOpening[opening], 1) << "door " << opening;
			}
			EXPECT_TRUE(areaDoubled == geometry::signedAreaDoubled(bands.bands[index]))
				<< "the rings tile the trimmed band: " << areaDoubled.toDouble() << " against "
				<< geometry::signedAreaDoubled(bands.bands[index]).toDouble();
		}
	}

	// Each wall graph comes out as the reference draws that graph with no other wall built, so the
	// graph the offsetter rejects takes nothing from the healthy one.
	void expectEachGraphAsDrawnAlone(const Layout& layout) {
		ConstructionWorld world;
		layout.build(world);
		const WallOutput actual = outputOf(world, nav::extractWalls);

		const std::vector<geometry::WallSegment>	segments = wallSegments(world);
		const std::vector<std::vector<std::size_t>> graphs	 = geometry::connectedWallGraphs(segments);
		ASSERT_GE(graphs.size(), 2u);
		WallOutput expected;
		int		   rejected = 0;
		for (const std::vector<std::size_t>& graph : graphs) {
			ConstructionWorld alone;
			layout.build(alone);
			std::vector<geometry::WallSegment> members;
			for (std::size_t index = 0; index < segments.size(); ++index) {
				if (std::find(graph.begin(), graph.end(), index) == graph.end()) {
					alone.setSegmentState(alone.segments()[index].id, FoundationState::Blueprint);
				} else {
					members.push_back(segments[index]);
				}
			}
			if (geometry::resolveWallBands(members, geometry::kDefaultMiterLimit).status != geometry::OffsetStatus::Ok) {
				++rejected;
			}
			const WallOutput part = outputOf(alone, reference::extractWalls);
			expected.polygons.insert(expected.polygons.end(), part.polygons.begin(), part.polygons.end());
			expected.junctions.insert(expected.junctions.end(), part.junctions.begin(), part.junctions.end());
		}
		std::sort(expected.polygons.begin(), expected.polygons.end());
		std::sort(expected.junctions.begin(), expected.junctions.end());
		EXPECT_EQ(rejected, 1);
		EXPECT_FALSE(expected.junctions.empty());
		EXPECT_EQ(actual.polygons, expected.polygons);
		EXPECT_EQ(actual.junctions, expected.junctions);
		EXPECT_EQ(actual.portals, outputOf(world, reference::extractWalls).portals);
	}

	// Junction.ShortMiddleSegmentBetweenSharpCornersRejectsOverrun at half its size, so Heavy's
	// 150 mm half thickness stands in for the fixture's 300. Returns the first wall.
	SegmentId buildRejectedGraph(ConstructionWorld& world) {
		const SegmentId first = buildWall(world, {19000, 500}, {20000, 0}, "Heavy");
		buildWall(world, {20000, 0}, {20050, 0}, "Heavy");
		buildWall(world, {20050, 0}, {21050, 500}, "Heavy");
		return first;
	}

	std::vector<Layout> layouts() {
		return {
			{"L", [](ConstructionWorld& world) {
				 buildWall(world, {0, 0}, {4000, 0});
				 buildWall(world, {4000, 0}, {4000, 3000});
			 }},
			{"T", [](ConstructionWorld& world) {
				 buildWall(world, {0, 0}, {2000, 0}, "Heavy");
				 buildWall(world, {2000, 0}, {4000, 0}, "Heavy");
				 buildWall(world, {2000, 0}, {2000, 2000});
			 }},
			{"ClosedRoom", [](ConstructionWorld& world) {
				 std::vector<SegmentId> walls;
				 buildRoom(world, {0, 0}, walls);
			 }},
			{"NearStraightLightIntoHeavy", [](ConstructionWorld& world) {
				 buildWall(world, {0, 0}, {2000, 0}, "Light");
				 buildWall(world, {2000, 0}, {3997, 105}, "Heavy");
			 }},
			{"TwoSeparateGraphs", [](ConstructionWorld& world) {
				 buildWall(world, {0, 0}, {4000, 0});
				 buildWall(world, {4000, 0}, {4000, 3000});
				 buildWall(world, {10000, 0}, {14000, 0}, "Heavy");
			 }},
			{"DoorMidWallFreeEnds",
			 [](ConstructionWorld& world) { buildOpening(world, buildWall(world, {0, 0}, {4000, 0}), 0.5F, "Door"); },
			 Expectation::DoorRingsWithinMillimetre},
			// Listed from its larger endpoint to its smaller, so its gap is cut from the other end.
			{"DoorOnWallRunningBackwards",
			 [](ConstructionWorld& world) { buildOpening(world, buildWall(world, {4000, 0}, {0, 0}), 0.25F, "Door"); },
			 Expectation::DoorRingsWithinMillimetre},
			// A length and a door position that round at both ends, so the millimetre bound is met off the axes.
			{"DoorOnDiagonalWall",
			 [](ConstructionWorld& world) { buildOpening(world, buildWall(world, {0, 0}, {2917, 3851}), 0.43F, "Door"); },
			 Expectation::DoorRingsWithinMillimetre},
			{"WindowMidWallFreeEnds",
			 [](ConstructionWorld& world) { buildOpening(world, buildWall(world, {0, 0}, {4000, 0}), 0.5F, "Window"); }},
			{"TwoDoorsOneWallFreeEnds",
			 [](ConstructionWorld& world) {
				 const SegmentId wall = buildWall(world, {0, 0}, {6000, 0});
				 buildOpening(world, wall, 0.25F, "Door");
				 buildOpening(world, wall, 0.75F, "Door");
			 },
			 Expectation::DoorRingsWithinMillimetre},
			{"DoorAndWindowOneWallFreeEnds",
			 [](ConstructionWorld& world) {
				 const SegmentId wall = buildWall(world, {0, 0}, {6000, 0});
				 buildOpening(world, wall, 0.25F, "Door");
				 buildOpening(world, wall, 0.75F, "Window");
			 },
			 Expectation::DoorRingsWithinMillimetre},
			{"RoomDoorBesideCorner",
			 [](ConstructionWorld& world) {
				 std::vector<SegmentId> walls;
				 buildRoom(world, {0, 0}, walls);
				 // The door's near edge 0.35 m from the corner, just past the opening margin.
				 buildOpening(world, walls[0], 0.2F, "Door");
			 },
			 Expectation::Changed, expectDoorSegmentsFromTrimmedBands},
			{"RoomDoorIntoCornerTrim",
			 [](ConstructionWorld& world) {
				 std::vector<SegmentId> walls;
				 buildRoom(world, {0, 0}, walls);
				 // The door's near edge 30 mm from the corner, inside the 100 mm corner trim, where the
				 // world takes it though the opening tool would not.
				 buildOpening(world, walls[0], 0.12F, "Door");
			 },
			 Expectation::Changed, expectDoorSegmentsFromTrimmedBands},
			{"RoomTwoDoorsOneWall",
			 [](ConstructionWorld& world) {
				 std::vector<SegmentId> walls;
				 buildRoom(world, {0, 0}, walls);
				 buildOpening(world, walls[0], 0.2F, "Door");
				 buildOpening(world, walls[0], 0.7F, "Door");
			 },
			 Expectation::Changed, expectDoorSegmentsFromTrimmedBands},
			{"RoomDoorJambBesideTrimEnd",
			 [](ConstructionWorld& world) {
				 std::vector<SegmentId> walls;
				 buildRoom(world, {0, 0}, walls);
				 // The door's near jamb 3 mm past the 100 mm corner trim: the door span takes that sliver.
				 buildOpening(world, walls[0], 0.13825F, "Door");
			 },
			 Expectation::Changed, expectDoorSegmentsFromTrimmedBands},
			{"RejectedGraphBesideHealthyGraph",
			 [](ConstructionWorld& world) {
				 buildWall(world, {0, 0}, {4000, 0});
				 buildWall(world, {4000, 0}, {4000, 3000});
				 buildRejectedGraph(world);
			 },
			 Expectation::Changed, expectEachGraphAsDrawnAlone},
			// A rejected graph keeps its raw bands, a door's wall too, whole.
			{"DoorOnRejectedGraph", [](ConstructionWorld& world) { buildOpening(world, buildRejectedGraph(world), 0.5F, "Door"); }},
		};
	}

	class ExtractWallsReferenceTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			ConstructionRegistry::Get().clear();
			ASSERT_TRUE(ConstructionRegistry::Get().load((projectRoot() / "assets" / "config" / "construction").string()));
		}
		void TearDown() override { ConstructionRegistry::Get().clear(); }
	};

	TEST_F(ExtractWallsReferenceTest, LayoutsMatchTheReferenceAsMarked) {
		for (const Layout& layout : layouts()) {
			SCOPED_TRACE(layout.name);
			ConstructionWorld world;
			layout.build(world);
			const WallOutput expected = outputOf(world, reference::extractWalls);
			const WallOutput actual	  = outputOf(world, nav::extractWalls);
			EXPECT_FALSE(actual.polygons.empty());
			switch (layout.expectation) {
				case Expectation::Identical:
					EXPECT_EQ(actual.polygons, expected.polygons);
					EXPECT_EQ(actual.junctions, expected.junctions);
					EXPECT_EQ(actual.portals, expected.portals);
					break;
				case Expectation::DoorRingsWithinMillimetre:
					expectDoorRingsWithinMillimetre(actual, expected, doorSegments(world));
					break;
				case Expectation::Changed:
					ASSERT_TRUE(layout.checkChange);
					layout.checkChange(layout);
					break;
			}
		}
	}

	// The layouts are what their names say: the openings land, and only the layouts built to hold a
	// graph the offsetter rejects do.
	TEST_F(ExtractWallsReferenceTest, LayoutsHoldWhatTheirNamesSay) {
		for (const Layout& layout : layouts()) {
			SCOPED_TRACE(layout.name);
			ConstructionWorld world;
			layout.build(world);
			const geometry::WallBands bands = geometry::resolveWallBands(wallSegments(world), geometry::kDefaultMiterLimit);
			EXPECT_EQ(bands.status == geometry::OffsetStatus::Ok, layout.name.find("Rejected") == std::string::npos);
			const WallOutput output	 = outputOf(world, reference::extractWalls);
			const bool		 opening = layout.name.find("Door") != std::string::npos || layout.name.find("Window") != std::string::npos;
			EXPECT_EQ(opening, !world.openings().empty());
			EXPECT_EQ(output.portals.size(), world.openings().size());
			if (layout.name == "RoomDoorIntoCornerTrim") {
				const construction::OpeningGaps gaps = construction::openingGaps(world, world.segments().front(), {});
				ASSERT_EQ(gaps.gaps.size(), 1u);
				EXPECT_LT(gaps.gaps.front().fromMm, bands.trimAtA.front()) << "the door reaches into the corner trim";
			}
			if (layout.name == "RoomDoorJambBesideTrimEnd") {
				const construction::OpeningGaps gaps = construction::openingGaps(world, world.segments().front(), {});
				ASSERT_EQ(gaps.gaps.size(), 1u);
				const std::int64_t sliver = gaps.gaps.front().fromMm - bands.trimAtA.front();
				EXPECT_GT(sliver, 0) << "the door's jamb lies past the corner trim";
				EXPECT_LT(static_cast<double>(sliver), kMinPieceFeatureMm) << "by less than the shortest run of wall kept";
			}
			// A door on an accepted graph comes out as a polygon carrying its opening. A rejected graph
			// keeps its walls' raw bands whole, so its door has none.
			if (layout.name.find("Door") != std::string::npos && layout.name.find("Rejected") == std::string::npos) {
				const WallOutput extracted = outputOf(world, nav::extractWalls);
				for (const construction::Opening& opening : world.openings()) {
					if (!ConstructionRegistry::Get().getOpeningType(opening.type)->pathable) {
						continue;
					}
					const bool carried = std::any_of(extracted.polygons.begin(), extracted.polygons.end(), [&](const PolygonKey& polygon) {
						return openingOf(polygon) == static_cast<std::int64_t>(opening.id);
					});
					EXPECT_TRUE(carried) << "no polygon carries door " << opening.id;
				}
			}
		}
	}

} // namespace
