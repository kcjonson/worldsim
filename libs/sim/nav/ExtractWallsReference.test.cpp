#include "NavInputBuilder.h"

#include "construction/OpeningGeometry.h"

#include <assets/ConstructionRegistry.h>
#include <construction/ConstructionWorld.h>

#include <core/Vec2i64.h>
#include <nav/NavMesh.h>
#include <offset/WallOffset.h>
#include <polygon/Polygon.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

// nav::extractWalls pinned against a copy of itself as it stood before WOR-876, over the wall
// layouts that change touches. A layout whose `mustMatch` is false is one the change is allowed
// to draw differently; every other layout must come out exactly as the reference draws it.

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

		void extractWalls(const ConstructionWorld& world, const ConstructionRegistry& registry, std::vector<gnav::NavInputPolygon>& outPolys,
						  std::vector<gnav::DoorPortal>& outDoors, std::vector<nav::WallJunction>* outJunctions) {
			std::vector<geometry::WallSegment>	 geoSegments;
			std::vector<construction::SegmentId> segmentIds;
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
					std::int64_t						 prov = nav::kProvenanceJunction;
					std::vector<construction::SegmentId> incident;
					if (ji < bands.junctionSegments.size()) {
						for (std::size_t segIdx : bands.junctionSegments[ji]) {
							if (segIdx < segmentIds.size()) {
								incident.push_back(segmentIds[segIdx]);
								const std::int64_t id = static_cast<std::int64_t>(segmentIds[segIdx]);
								if (prov == nav::kProvenanceJunction || id < prov) {
									prov = id;
								}
							}
						}
					}
					if (outJunctions != nullptr) {
						outJunctions->push_back({outPolys.size(), std::move(incident)});
					}
					outPolys.push_back({j, true, prov, gnav::kNoOpening});
				}
			}
		}

	} // namespace reference

	std::filesystem::path projectRoot() {
		std::filesystem::path p = __FILE__;
		return p.parent_path().parent_path().parent_path().parent_path();
	}

	SegmentId buildWall(ConstructionWorld& world, Vec2i64 a, Vec2i64 b, const std::string& thickness = "Standard") {
		const SegmentCommitResult result = world.commitSegment(a, b, "Wood", thickness, kInvalidFoundation);
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

	struct Layout {
		std::string							  name;
		std::function<void(ConstructionWorld&)> build;
		bool								  mustMatch = true;
	};

	using PolygonKey = std::tuple<std::int64_t, std::int64_t, bool, bool, bool, Ring>;
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

	WallOutput outputOf(const ConstructionWorld& world, const Extractor& extract) {
		std::vector<gnav::NavInputPolygon> polygons;
		std::vector<gnav::DoorPortal>	   portals;
		std::vector<nav::WallJunction>	   junctions;
		extract(world, ConstructionRegistry::Get(), polygons, portals, &junctions);

		WallOutput output;
		for (const gnav::NavInputPolygon& polygon : polygons) {
			output.polygons.emplace_back(polygon.provenanceId, polygon.openingId, polygon.blocked, polygon.holeCapable, polygon.coverOnly,
										 polygon.ring);
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

	std::vector<geometry::WallSegment> builtWallSegments(const ConstructionWorld& world) {
		std::vector<geometry::WallSegment> segments;
		for (const construction::WallSegment& seg : world.segments()) {
			const auto* preset = ConstructionRegistry::Get().getThicknessPreset(seg.material, seg.thicknessPreset);
			segments.push_back({world.getVertex(seg.v0)->pos, world.getVertex(seg.v1)->pos, preset->halfThicknessMm});
		}
		return segments;
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
			{"DoorMidWallFreeEnds", [](ConstructionWorld& world) {
				 buildOpening(world, buildWall(world, {0, 0}, {4000, 0}), 0.5F, "Door");
			 }},
			{"WindowMidWallFreeEnds", [](ConstructionWorld& world) {
				 buildOpening(world, buildWall(world, {0, 0}, {4000, 0}), 0.5F, "Window");
			 }},
			{"RoomDoorBesideCorner", [](ConstructionWorld& world) {
				 std::vector<SegmentId> walls;
				 buildRoom(world, {0, 0}, walls);
				 // The door's near edge 0.35 m from the corner, just past the opening margin.
				 buildOpening(world, walls[0], 0.2F, "Door");
			 }},
			{"RejectedGraphBesideHealthyGraph", [](ConstructionWorld& world) {
				 buildWall(world, {0, 0}, {4000, 0});
				 buildWall(world, {4000, 0}, {4000, 3000});
				 // Junction.ShortMiddleSegmentBetweenSharpCornersRejectsOverrun at half its size, so
				 // Heavy's 150 mm half thickness stands in for the fixture's 300.
				 buildWall(world, {19000, 500}, {20000, 0}, "Heavy");
				 buildWall(world, {20000, 0}, {20050, 0}, "Heavy");
				 buildWall(world, {20050, 0}, {21050, 500}, "Heavy");
			 }},
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

	TEST_F(ExtractWallsReferenceTest, LayoutsMatchTheReferenceUnlessMarkedOtherwise) {
		const Extractor production = [](const ConstructionWorld& world, const ConstructionRegistry& registry,
										std::vector<gnav::NavInputPolygon>& polygons, std::vector<gnav::DoorPortal>& portals,
										std::vector<nav::WallJunction>* junctions) {
			nav::extractWalls(world, registry, polygons, portals, junctions);
		};
		for (const Layout& layout : layouts()) {
			SCOPED_TRACE(layout.name);
			ConstructionWorld world;
			layout.build(world);
			const WallOutput expected = outputOf(world, reference::extractWalls);
			const WallOutput actual	  = outputOf(world, production);
			EXPECT_FALSE(actual.polygons.empty());
			if (!layout.mustMatch) {
				continue;
			}
			EXPECT_EQ(actual.polygons, expected.polygons);
			EXPECT_EQ(actual.junctions, expected.junctions);
			EXPECT_EQ(actual.portals, expected.portals);
		}
	}

	// The layouts are what their names say: the openings land, and only the one layout built to
	// hold a graph the offsetter rejects does.
	TEST_F(ExtractWallsReferenceTest, LayoutsHoldWhatTheirNamesSay) {
		for (const Layout& layout : layouts()) {
			SCOPED_TRACE(layout.name);
			ConstructionWorld world;
			layout.build(world);
			const geometry::WallBands bands = geometry::resolveWallBands(builtWallSegments(world), geometry::kDefaultMiterLimit);
			EXPECT_EQ(bands.status == geometry::OffsetStatus::Ok, layout.name != "RejectedGraphBesideHealthyGraph");
			const WallOutput output = outputOf(world, reference::extractWalls);
			const bool		 opening = layout.name.find("Door") != std::string::npos || layout.name.find("Window") != std::string::npos;
			EXPECT_EQ(output.portals.size(), opening ? 1u : 0u);
		}
	}

} // namespace
