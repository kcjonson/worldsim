#include "RoomDetection.h"

#include "ConstructionWorld.h"

#include <arrangement/Arrangement.h>
#include <arrangement/HalfEdge.h>
#include <core/Int128.h>
#include <core/Vec2i64.h>
#include <polygon/Polygon.h>
#include <predicates/Predicates.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

using namespace construction;
using geometry::Vec2i64;

namespace {

	// Commit a built wall between integer-mm endpoints and return its SegmentId. A
	// T-junction commit also creates the split halves of its host; all are built.
	SegmentId buildWall(ConstructionWorld& world, Vec2i64 a, Vec2i64 b) {
		SegmentCommitResult r = world.commitSegment(a, b, "wood", "standard", kInvalidFoundation);
		EXPECT_TRUE(r.ok());
		for (SegmentId id : r.createdSegments) {
			world.setSegmentState(id, FoundationState::Built);
		}
		return r.id;
	}

	void buildSquare(ConstructionWorld& world, Vec2i64 corner, std::int64_t side) {
		buildWall(world, corner, {corner.x + side, corner.y});
		buildWall(world, {corner.x + side, corner.y}, {corner.x + side, corner.y + side});
		buildWall(world, {corner.x + side, corner.y + side}, {corner.x, corner.y + side});
		buildWall(world, {corner.x, corner.y + side}, corner);
	}

	// The room whose outer ring (holes ignored) has the given area in m^2.
	const RoomFace* roomWithRingArea(const std::vector<RoomFace>& rooms, double squareMeters) {
		for (const RoomFace& room : rooms) {
			if (std::fabs(geometry::signedAreaSquareMeters(room.ring) - squareMeters) < 1e-6) {
				return &room;
			}
		}
		return nullptr;
	}

	// Without a free-standing inner wall group, detection is the plain bounded faces of
	// the arrangement, unchanged: same order, rings, areas, points, and provenance.
	void expectMatchesPlainFaces(const ConstructionWorld& world, const std::vector<RoomFace>& rooms) {
		std::vector<geometry::InputSegment> inputs;
		for (const WallSegment& seg : world.segments()) {
			if (seg.state == FoundationState::Built) {
				inputs.push_back({world.getVertex(seg.v0)->pos, world.getVertex(seg.v1)->pos, static_cast<std::int64_t>(seg.id)});
			}
		}
		const geometry::HalfEdgeMesh mesh = geometry::extractFaces(geometry::buildArrangement(inputs));
		std::size_t					 roomIndex = 0;
		for (const geometry::Face& face : mesh.faces) {
			if (face.outer || !face.representativePoint.has_value()) {
				continue;
			}
			ASSERT_LT(roomIndex, rooms.size());
			const RoomFace& room = rooms[roomIndex++];
			geometry::Ring	ring;
			for (std::size_t he : face.halfEdges) {
				ring.push_back(mesh.vertices[mesh.halfEdges[he].origin]);
			}
			std::vector<SegmentId> provenance;
			for (std::int64_t p : face.provenance) {
				provenance.push_back(static_cast<SegmentId>(p));
			}
			EXPECT_EQ(room.ring, ring);
			EXPECT_TRUE(room.holes.empty());
			EXPECT_EQ(room.areaDoubled, face.signedAreaDoubled);
			EXPECT_EQ(room.area, static_cast<float>(std::fabs(face.signedAreaDoubled.toDouble()) / 2.0e6));
			EXPECT_EQ(room.representativePoint, *face.representativePoint);
			EXPECT_EQ(room.boundingSegmentIds, provenance);
		}
		EXPECT_EQ(roomIndex, rooms.size());
	}

	using GridEdge = std::pair<Vec2i64, Vec2i64>;

	constexpr std::int64_t kGrid = 5;
	constexpr std::int64_t kCell = 1000;

	// The 1 m wall edges of a kGrid x kGrid lattice of points.
	std::vector<GridEdge> gridEdges() {
		std::vector<GridEdge> edges;
		for (std::int64_t i = 0; i < kGrid; ++i) {
			for (std::int64_t j = 0; j < kGrid; ++j) {
				if (i + 1 < kGrid) {
					edges.push_back({{i * kCell, j * kCell}, {(i + 1) * kCell, j * kCell}});
				}
				if (j + 1 < kGrid) {
					edges.push_back({{i * kCell, j * kCell}, {i * kCell, (j + 1) * kCell}});
				}
			}
		}
		return edges;
	}

	// Build a random connected subset of the grid edges, shifted by `offset`: one wall group.
	void buildConnectedLayout(ConstructionWorld& world, std::mt19937& rng, const std::vector<GridEdge>& edges, Vec2i64 offset) {
		const auto build = [&](const GridEdge& edge) {
			buildWall(world, {edge.first.x + offset.x, edge.first.y + offset.y}, {edge.second.x + offset.x, edge.second.y + offset.y});
		};
		const std::size_t	 target = 4 + rng() % 30;
		std::vector<bool>	 used(edges.size(), false);
		std::vector<Vec2i64> touched;

		const std::size_t first = rng() % edges.size();
		used[first] = true;
		build(edges[first]);
		touched.push_back(edges[first].first);
		touched.push_back(edges[first].second);

		for (std::size_t added = 1; added < target; ++added) {
			std::vector<std::size_t> frontier;
			for (std::size_t e = 0; e < edges.size(); ++e) {
				const bool attached = std::find(touched.begin(), touched.end(), edges[e].first) != touched.end() ||
									  std::find(touched.begin(), touched.end(), edges[e].second) != touched.end();
				if (!used[e] && attached) {
					frontier.push_back(e);
				}
			}
			if (frontier.empty()) {
				break;
			}
			const std::size_t pick = frontier[rng() % frontier.size()];
			used[pick] = true;
			build(edges[pick]);
			touched.push_back(edges[pick].first);
			touched.push_back(edges[pick].second);
		}
	}

	// Commit a wall left in Blueprint state.
	SegmentId blueprintWall(ConstructionWorld& world, Vec2i64 a, Vec2i64 b) {
		SegmentCommitResult r = world.commitSegment(a, b, "wood", "standard", kInvalidFoundation);
		EXPECT_TRUE(r.ok());
		return r.id;
	}

	bool boundsContain(const RoomFace& face, SegmentId id) {
		return std::find(face.boundingSegmentIds.begin(), face.boundingSegmentIds.end(), id) != face.boundingSegmentIds.end();
	}

} // namespace

TEST(RoomDetection, SingleRectangleIsOneRoom) {
	ConstructionWorld world;
	// 4m x 3m rectangle in mm.
	SegmentId s0 = buildWall(world, {0, 0}, {4000, 0});
	SegmentId s1 = buildWall(world, {4000, 0}, {4000, 3000});
	SegmentId s2 = buildWall(world, {4000, 3000}, {0, 3000});
	SegmentId s3 = buildWall(world, {0, 3000}, {0, 0});

	std::vector<RoomFace> rooms = detectRooms(world);
	ASSERT_EQ(rooms.size(), 1u);
	EXPECT_NEAR(rooms[0].area, 12.0f, 1e-3f); // 4 x 3 = 12 m^2

	EXPECT_EQ(rooms[0].boundingSegmentIds.size(), 4u);
	EXPECT_TRUE(boundsContain(rooms[0], s0));
	EXPECT_TRUE(boundsContain(rooms[0], s1));
	EXPECT_TRUE(boundsContain(rooms[0], s2));
	EXPECT_TRUE(boundsContain(rooms[0], s3));
}

TEST(RoomDetection, TwoAdjacentRectanglesShareWallTwoRooms) {
	ConstructionWorld world;
	// Left cell [0,1000]x[0,1000], right cell [1000,2000]x[0,1000], shared edge x=1000.
	buildWall(world, {0, 0}, {1000, 0});
	buildWall(world, {1000, 0}, {2000, 0});
	buildWall(world, {2000, 0}, {2000, 1000});
	buildWall(world, {2000, 1000}, {1000, 1000});
	buildWall(world, {1000, 1000}, {0, 1000});
	buildWall(world, {0, 1000}, {0, 0});
	buildWall(world, {1000, 0}, {1000, 1000}); // shared divider

	std::vector<RoomFace> rooms = detectRooms(world);
	EXPECT_EQ(rooms.size(), 2u);
}

TEST(RoomDetection, OpenChainIsNoRoom) {
	ConstructionWorld world;
	buildWall(world, {0, 0}, {1000, 0});
	buildWall(world, {1000, 0}, {1000, 1000});
	buildWall(world, {1000, 1000}, {0, 1000}); // three sides, not closed

	EXPECT_TRUE(detectRooms(world).empty());
}

TEST(RoomDetection, BlueprintWallsDoNotEncloseUntilBuilt) {
	ConstructionWorld world;
	// Commit a full loop but leave every segment in Blueprint state.
	blueprintWall(world, {0, 0}, {1000, 0});
	blueprintWall(world, {1000, 0}, {1000, 1000});
	blueprintWall(world, {1000, 1000}, {0, 1000});
	blueprintWall(world, {0, 1000}, {0, 0});

	EXPECT_TRUE(detectRooms(world).empty());

	// Flip them all to Built: now the loop encloses.
	for (const WallSegment& seg : world.segments()) {
		world.setSegmentState(seg.id, FoundationState::Built);
	}
	EXPECT_EQ(detectRooms(world).size(), 1u);
}

TEST(RoomDetection, OpeningOnWallStillEncloses) {
	ConstructionWorld world;
	SegmentId		  withDoor = buildWall(world, {0, 0}, {2000, 0});
	buildWall(world, {2000, 0}, {2000, 2000});
	buildWall(world, {2000, 2000}, {0, 2000});
	buildWall(world, {0, 2000}, {0, 0});

	// A door on a wall does not split its centerline; the loop still encloses.
	world.addOpening(withDoor, 0.5f, "door", "wood");

	std::vector<RoomFace> rooms = detectRooms(world);
	ASSERT_EQ(rooms.size(), 1u);
	EXPECT_TRUE(boundsContain(rooms[0], withDoor));
}

TEST(RoomDetection, LoopInsideLoopIsAHoleOfTheOuterRoom) {
	ConstructionWorld world;
	// Inner 1m x 1m loop.
	buildWall(world, {2000, 2000}, {3000, 2000});
	buildWall(world, {3000, 2000}, {3000, 3000});
	buildWall(world, {3000, 3000}, {2000, 3000});
	buildWall(world, {2000, 3000}, {2000, 2000});
	// Outer 5m x 5m loop around it.
	buildSquare(world, {0, 0}, 5000);

	std::vector<RoomFace> rooms = detectRooms(world);
	ASSERT_EQ(rooms.size(), 2u);

	const RoomFace* outer = roomWithRingArea(rooms, 25.0);
	const RoomFace* inner = roomWithRingArea(rooms, 1.0);
	ASSERT_NE(outer, nullptr);
	ASSERT_NE(inner, nullptr);
	EXPECT_NEAR(outer->area, 24.0f, 1e-3f);
	EXPECT_EQ(outer->areaDoubled, geometry::Int128(48'000'000));
	ASSERT_EQ(outer->holes.size(), 1u);
	EXPECT_EQ(geometry::windingOrder(outer->holes[0]), geometry::Winding::Clockwise);
	EXPECT_EQ(geometry::signedAreaDoubled(outer->holes[0]), geometry::Int128(-2'000'000));
	EXPECT_TRUE(inner->holes.empty());
	EXPECT_NEAR(inner->area, 1.0f, 1e-3f);
	EXPECT_EQ(pointInRoom(outer->representativePoint, outer->ring, outer->holes), geometry::PointInPolygon::Inside);
	EXPECT_EQ(pointInRoom(inner->representativePoint, inner->ring, inner->holes), geometry::PointInPolygon::Inside);
}

TEST(RoomDetection, ThreeLevelNestAttachesEachHoleToItsImmediateParent) {
	ConstructionWorld world;
	buildSquare(world, {0, 0}, 9000);
	buildSquare(world, {2000, 2000}, 5000);
	buildSquare(world, {4000, 4000}, 1000);

	std::vector<RoomFace> rooms = detectRooms(world);
	ASSERT_EQ(rooms.size(), 3u);
	const RoomFace* outer = roomWithRingArea(rooms, 81.0);
	const RoomFace* middle = roomWithRingArea(rooms, 25.0);
	const RoomFace* inner = roomWithRingArea(rooms, 1.0);
	ASSERT_NE(outer, nullptr);
	ASSERT_NE(middle, nullptr);
	ASSERT_NE(inner, nullptr);

	ASSERT_EQ(outer->holes.size(), 1u);
	EXPECT_EQ(geometry::signedAreaDoubled(outer->holes[0]), geometry::Int128(-50'000'000));
	EXPECT_NEAR(outer->area, 56.0f, 1e-3f);
	ASSERT_EQ(middle->holes.size(), 1u);
	EXPECT_EQ(geometry::signedAreaDoubled(middle->holes[0]), geometry::Int128(-2'000'000));
	EXPECT_NEAR(middle->area, 24.0f, 1e-3f);
	EXPECT_TRUE(inner->holes.empty());
	EXPECT_NEAR(inner->area, 1.0f, 1e-3f);

	for (const RoomFace& room : rooms) {
		EXPECT_EQ(pointInRoom(room.representativePoint, room.ring, room.holes), geometry::PointInPolygon::Inside);
	}
}

TEST(RoomDetection, TwoInnerRoomsAreTwoHoles) {
	ConstructionWorld world;
	buildSquare(world, {0, 0}, 6000);
	buildSquare(world, {1000, 1000}, 1000);
	buildSquare(world, {4000, 4000}, 1000);

	std::vector<RoomFace> rooms = detectRooms(world);
	ASSERT_EQ(rooms.size(), 3u);
	const RoomFace* outer = roomWithRingArea(rooms, 36.0);
	ASSERT_NE(outer, nullptr);
	EXPECT_EQ(outer->holes.size(), 2u);
	EXPECT_NEAR(outer->area, 34.0f, 1e-3f);
	for (const RoomFace& room : rooms) {
		if (&room != outer) {
			EXPECT_TRUE(room.holes.empty());
		}
	}
}

TEST(RoomDetection, InnerRoomTouchingTheOuterWallAtACornerHasNoHole) {
	ConstructionWorld world;
	buildSquare(world, {0, 0}, 5000);
	// A diamond whose bottom corner lands on the outer room's bottom wall.
	buildWall(world, {2500, 0}, {3500, 1000});
	buildWall(world, {3500, 1000}, {2500, 2000});
	buildWall(world, {2500, 2000}, {1500, 1000});
	buildWall(world, {1500, 1000}, {2500, 0});

	std::vector<RoomFace> rooms = detectRooms(world);
	ASSERT_EQ(rooms.size(), 2u);
	expectMatchesPlainFaces(world, rooms);
	const RoomFace* outer = roomWithRingArea(rooms, 23.0);
	ASSERT_NE(outer, nullptr);
	EXPECT_EQ(std::count(outer->ring.begin(), outer->ring.end(), Vec2i64{2500, 0}), 2);
}

TEST(RoomDetection, InnerRoomSharingAWallWithTheOuterRoomHasNoHole) {
	ConstructionWorld world;
	buildSquare(world, {0, 0}, 5000);
	buildWall(world, {1000, 0}, {1000, 1000});
	buildWall(world, {1000, 1000}, {0, 1000});

	std::vector<RoomFace> rooms = detectRooms(world);
	ASSERT_EQ(rooms.size(), 2u);
	expectMatchesPlainFaces(world, rooms);
	EXPECT_NE(roomWithRingArea(rooms, 24.0), nullptr);
	EXPECT_NE(roomWithRingArea(rooms, 1.0), nullptr);
}

TEST(RoomDetection, InnerRoomJoinedByASpurWallHasNoHole) {
	ConstructionWorld world;
	buildSquare(world, {0, 0}, 5000);
	buildSquare(world, {2000, 2000}, 1000);
	buildWall(world, {0, 2500}, {2000, 2500});

	std::vector<RoomFace> rooms = detectRooms(world);
	ASSERT_EQ(rooms.size(), 2u);
	expectMatchesPlainFaces(world, rooms);
	EXPECT_NE(roomWithRingArea(rooms, 24.0), nullptr);
	EXPECT_NE(roomWithRingArea(rooms, 1.0), nullptr);
}

TEST(RoomDetection, OuterRoomKeepsItsPointWhenTheHoleMissesIt) {
	ConstructionWorld world;
	buildSquare(world, {0, 0}, 5000);
	const Vec2i64 alonePoint = detectRooms(world).at(0).representativePoint;

	// A 1m loop in whichever corner region is farthest from the point.
	const Vec2i64 corner{alonePoint.x < 2500 ? 3500 : 500, alonePoint.y < 2500 ? 3500 : 500};
	buildSquare(world, corner, 1000);

	std::vector<RoomFace> rooms = detectRooms(world);
	const RoomFace*		  outer = roomWithRingArea(rooms, 25.0);
	ASSERT_NE(outer, nullptr);
	ASSERT_EQ(outer->holes.size(), 1u);
	EXPECT_EQ(outer->representativePoint, alonePoint);
}

TEST(RoomDetection, OuterRoomPointMovesOutOfAHoleBuiltAroundIt) {
	ConstructionWorld world;
	buildSquare(world, {0, 0}, 5000);
	const Vec2i64 alonePoint = detectRooms(world).at(0).representativePoint;

	buildSquare(world, {alonePoint.x - 500, alonePoint.y - 500}, 1000);

	std::vector<RoomFace> rooms = detectRooms(world);
	const RoomFace*		  outer = roomWithRingArea(rooms, 25.0);
	ASSERT_NE(outer, nullptr);
	ASSERT_EQ(outer->holes.size(), 1u);
	EXPECT_NE(outer->representativePoint, alonePoint);
	EXPECT_EQ(pointInRoom(outer->representativePoint, outer->ring, outer->holes), geometry::PointInPolygon::Inside);
	EXPECT_EQ(geometry::pointInPolygon(outer->representativePoint, outer->holes[0]), geometry::PointInPolygon::Outside);
}

TEST(RoomDetection, PointInRoomTreatsHolesAsOutsideAndTheirEdgesAsBoundary) {
	const geometry::Ring			  ring{{0, 0}, {4000, 0}, {4000, 4000}, {0, 4000}};
	const std::vector<geometry::Ring> holes{{{1000, 1000}, {1000, 2000}, {2000, 2000}, {2000, 1000}}};
	EXPECT_EQ(pointInRoom({3000, 3000}, ring, holes), geometry::PointInPolygon::Inside);
	EXPECT_EQ(pointInRoom({1500, 1500}, ring, holes), geometry::PointInPolygon::Outside);
	EXPECT_EQ(pointInRoom({1000, 1500}, ring, holes), geometry::PointInPolygon::OnBoundary);
	EXPECT_EQ(pointInRoom({0, 1500}, ring, holes), geometry::PointInPolygon::OnBoundary);
	EXPECT_EQ(pointInRoom({5000, 1500}, ring, holes), geometry::PointInPolygon::Outside);
}

TEST(RoomDetection, ConnectedLayoutsMatchPlainFaceExtraction) {
	// Every layout is one wall group (rectangles sharing walls, T and L shapes, spurs), so
	// no room can have a hole and detection must be exactly the plain face extraction.
	const std::vector<GridEdge> edges = gridEdges();
	std::mt19937				rng(512);
	std::size_t					layoutsWithRooms = 0;
	for (int layout = 0; layout < 200; ++layout) {
		ConstructionWorld world;
		buildConnectedLayout(world, rng, edges, {0, 0});

		const std::vector<RoomFace> rooms = detectRooms(world);
		if (!rooms.empty()) {
			++layoutsWithRooms;
		}
		for (const RoomFace& room : rooms) {
			EXPECT_TRUE(room.holes.empty());
			EXPECT_EQ(room.areaDoubled, geometry::signedAreaDoubled(room.ring));
		}
		expectMatchesPlainFaces(world, rooms);
	}
	EXPECT_GT(layoutsWithRooms, 50u);
}

TEST(RoomDetection, SeparateLayoutsSideBySideMatchPlainFaceExtraction) {
	// Two or three connected layouts in their own grids a gap apart: separate wall groups,
	// none inside another, often a group with rooms beside an open chain. Still no room
	// has a hole and detection is exactly the plain face extraction.
	const std::vector<GridEdge> edges = gridEdges();
	constexpr std::int64_t		kStride = kGrid * kCell + 1500;
	std::mt19937				rng(1024);
	std::size_t					layoutsWithRoomsInSeveralGroups = 0;
	std::size_t					layoutsWithRoomsBesideAnOpenGroup = 0;
	for (int layout = 0; layout < 150; ++layout) {
		const std::size_t groupCount = 2 + rng() % 2;
		ConstructionWorld world;
		for (std::size_t group = 0; group < groupCount; ++group) {
			buildConnectedLayout(world, rng, edges, {static_cast<std::int64_t>(group) * kStride, 0});
		}

		const std::vector<RoomFace> rooms = detectRooms(world);
		std::vector<bool>			groupHasRoom(groupCount, false);
		for (const RoomFace& room : rooms) {
			EXPECT_TRUE(room.holes.empty());
			EXPECT_EQ(room.areaDoubled, geometry::signedAreaDoubled(room.ring));
			groupHasRoom[static_cast<std::size_t>(room.representativePoint.x / kStride)] = true;
		}
		const auto groupsWithRooms = static_cast<std::size_t>(std::count(groupHasRoom.begin(), groupHasRoom.end(), true));
		if (groupsWithRooms > 1) {
			++layoutsWithRoomsInSeveralGroups;
		}
		if (groupsWithRooms > 0 && groupsWithRooms < groupCount) {
			++layoutsWithRoomsBesideAnOpenGroup;
		}
		expectMatchesPlainFaces(world, rooms);
	}
	EXPECT_GT(layoutsWithRoomsInSeveralGroups, 60u);
	EXPECT_GT(layoutsWithRoomsBesideAnOpenGroup, 25u);
}
