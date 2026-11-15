#include "scenes/game/world/construction/DrawingSystem.h"

#include "scenes/game/world/CreekWorldFixture.test.h"
#include "scenes/game/world/PlayerVisibilityFixture.test.h"

#include <assets/ConstructionRegistry.h>
#include <construction/ConstructionValidator.h>
#include <construction/ConstructionWorld.h>
#include <construction/RoomDetection.h>

#include <glm/geometric.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

	using world_sim::testing::kSeenPoint;
	using world_sim::testing::kUnseenPoint;
	using world_sim::testing::kViewportHeight;
	using world_sim::testing::kViewportWidth;

	// <root>/apps/world-sim/scenes/game/world/construction/DrawingSystem.test.cpp
	std::string constructionConfigFolder() {
		std::filesystem::path root = __FILE__;
		for (int level = 0; level < 7; ++level) {
			root = root.parent_path();
		}
		return (root / "assets" / "config" / "construction").string();
	}

	class DrawingVisibilityTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			assets::ConstructionRegistry::Get().clear();
			ASSERT_TRUE(assets::ConstructionRegistry::Get().load(constructionConfigFolder()));
			drawing = std::make_unique<world_sim::DrawingSystem>(world_sim::DrawingSystem::Args{
				.world = &seen.world,
				.camera = &seen.camera,
				.navigation = nullptr,
				.callbacks = {.onToast = [this](const std::string& title, const std::string& message) { toasts.emplace_back(title, message); }}
			});
		}
		void TearDown() override { assets::ConstructionRegistry::Get().clear(); }

		bool click(glm::vec2 meters) {
			const glm::vec2 screen = seen.screenOf(meters);
			return drawing->handleClick(screen.x, screen.y, kViewportWidth, kViewportHeight, false);
		}
		void hover(glm::vec2 meters) {
			const glm::vec2 screen = seen.screenOf(meters);
			drawing->handleMouseMove(screen.x, screen.y, kViewportWidth, kViewportHeight, false);
		}
		[[nodiscard]] bool lastToastSaysUnexplored() const {
			return !toasts.empty() && toasts.back().second.find("unexplored") != std::string::npos;
		}

		// A 30 m by 20 m foundation across the edge of what is seen, and a built wall along its middle.
		void buildAcrossTheEdge() {
			construction::ConstructionWorld& built = drawing->world();
			const auto foundation = built.commitFoundation(std::vector<Foundation::Vec2>{{30.0F, -10.0F}, {60.0F, -10.0F}, {60.0F, 10.0F}, {30.0F, 10.0F}}, "Wood");
			ASSERT_TRUE(foundation.ok());
			built.setState(foundation.id, construction::FoundationState::Built);
			const auto wall = built.commitSegment(geometry::quantize(Foundation::Vec2{32.0F, 0.0F}), geometry::quantize(Foundation::Vec2{58.0F, 0.0F}), "Wood", "Standard", foundation.id);
			ASSERT_TRUE(wall.ok());
			for (const construction::SegmentId id : wall.createdSegments) {
				built.setSegmentState(id, construction::FoundationState::Built);
			}
		}

		world_sim::testing::SeenWorld							   seen;
		std::vector<std::pair<std::string, std::string>>		   toasts;
		std::unique_ptr<world_sim::DrawingSystem> drawing;
	};

} // namespace

TEST_F(DrawingVisibilityTest, FoundationPointOnUnseenGroundIsRefusedAsUnexplored) {
	drawing->activateFoundationTool();
	click(kSeenPoint);
	ASSERT_EQ(drawing->status().pointCount, 1);
	hover(kUnseenPoint);
	EXPECT_FALSE(drawing->status().valid);
	EXPECT_TRUE(drawing->status().message.find("unexplored") != std::string::npos) << drawing->status().message;
	click(kUnseenPoint);
	EXPECT_EQ(drawing->status().pointCount, 1);
	EXPECT_TRUE(lastToastSaysUnexplored());
}

TEST_F(DrawingVisibilityTest, FoundationOnSeenGroundCommits) {
	drawing->activateFoundationTool();
	for (const glm::vec2 corner : {glm::vec2{0.0F, 0.0F}, glm::vec2{8.0F, 0.0F}, glm::vec2{8.0F, 8.0F}, glm::vec2{0.0F, 8.0F}}) {
		click(corner);
	}
	click({0.0F, 0.0F});
	EXPECT_EQ(drawing->world().foundations().size(), 1U) << (toasts.empty() ? "no toast" : toasts.back().second);
}

TEST_F(DrawingVisibilityTest, WallPointOnUnseenGroundIsRefusedAsUnexplored) {
	buildAcrossTheEdge();
	drawing->activateWallTool();
	click({36.0F, 4.0F});
	ASSERT_EQ(drawing->status().pointCount, 1);
	click({50.0F, 4.0F});
	EXPECT_EQ(drawing->status().pointCount, 1);
	EXPECT_TRUE(lastToastSaysUnexplored());
}

TEST_F(DrawingVisibilityTest, OpeningOnUnseenGroundIsRefusedAsUnexplored) {
	buildAcrossTheEdge();
	drawing->activateOpeningTool("Door");
	click({35.0F, 0.0F});
	EXPECT_EQ(drawing->world().openings().size(), 1U) << (toasts.empty() ? "no toast" : toasts.back().second);
	click({55.0F, 0.0F});
	EXPECT_EQ(drawing->world().openings().size(), 1U);
	EXPECT_TRUE(lastToastSaysUnexplored());
}

TEST_F(DrawingVisibilityTest, RevealTurnsTheRulesOff) {
	seen.visibility.setRevealed(true);
	buildAcrossTheEdge();
	drawing->activateFoundationTool();
	click(kUnseenPoint);
	EXPECT_EQ(drawing->status().pointCount, 1);
	drawing->activateWallTool();
	click({36.0F, 4.0F});
	click({50.0F, 4.0F});
	EXPECT_EQ(drawing->status().pointCount, 2);
	drawing->activateOpeningTool("Door");
	click({55.0F, 0.0F});
	EXPECT_EQ(drawing->world().openings().size(), 1U);
}

namespace {

	// The wall tool drawing a room inside a built foundation on seen ground, through mouse input
	// with the ordinary snaps on.
	class DrawingWallChainTest : public DrawingVisibilityTest {
	  protected:
		static constexpr glm::vec2 kRoom[] = {{-8.0F, -8.0F}, {8.0F, -8.0F}, {8.0F, 8.0F}, {-8.0F, 8.0F}};

		static construction::FoundationId hostFoundation(world_sim::DrawingSystem& tool) {
			const auto foundation =
				tool.world().commitFoundation(std::vector<Foundation::Vec2>{{-20.0F, -15.0F}, {20.0F, -15.0F}, {20.0F, 15.0F}, {-20.0F, 15.0F}}, "Wood");
			EXPECT_TRUE(foundation.ok());
			tool.world().setState(foundation.id, construction::FoundationState::Built);
			return foundation.id;
		}
		bool clickWith(world_sim::DrawingSystem& tool, glm::vec2 meters) const {
			const glm::vec2 screen = seen.screenOf(meters);
			return tool.handleClick(screen.x, screen.y, kViewportWidth, kViewportHeight, false);
		}
		void clickRoomCorners(world_sim::DrawingSystem& tool) const {
			for (const glm::vec2 corner : kRoom) {
				clickWith(tool, corner);
			}
		}

		// Vertex positions and segment end positions, each sorted, so two worlds compare by shape alone.
		struct Topology {
			std::vector<geometry::Vec2i64>									   vertices;
			std::vector<std::pair<geometry::Vec2i64, geometry::Vec2i64>> segments;
			bool operator==(const Topology&) const = default;
		};
		static Topology topologyOf(const construction::ConstructionWorld& built) {
			Topology topology;
			for (const construction::Vertex& vertex : built.vertices()) {
				topology.vertices.push_back(vertex.pos);
			}
			for (const construction::WallSegment& segment : built.segments()) {
				const geometry::Vec2i64 a = built.getVertex(segment.v0)->pos;
				const geometry::Vec2i64 b = built.getVertex(segment.v1)->pos;
				topology.segments.emplace_back(std::min(a, b), std::max(a, b));
			}
			std::sort(topology.vertices.begin(), topology.vertices.end());
			std::sort(topology.segments.begin(), topology.segments.end());
			return topology;
		}
		static void buildEverySegment(construction::ConstructionWorld& built) {
			std::vector<construction::SegmentId> ids;
			for (const construction::WallSegment& segment : built.segments()) {
				ids.push_back(segment.id);
			}
			for (const construction::SegmentId id : ids) {
				built.setSegmentState(id, construction::FoundationState::Built);
			}
		}
		[[nodiscard]] std::string lastToast() const { return toasts.empty() ? "no toast" : toasts.back().first + ": " + toasts.back().second; }
	};

} // namespace

TEST_F(DrawingWallChainTest, ClickingTheFirstPointClosesAndCommitsTheLoop) {
	hostFoundation(*drawing);
	drawing->activateWallTool();
	clickRoomCorners(*drawing);
	ASSERT_EQ(drawing->status().pointCount, 4) << lastToast();
	hover(kRoom[0]);
	EXPECT_TRUE(drawing->status().valid) << drawing->status().message;
	clickWith(*drawing, kRoom[0]);
	EXPECT_EQ(drawing->status().pointCount, 0) << lastToast();
	EXPECT_EQ(drawing->world().segments().size(), 4U) << lastToast();
	EXPECT_EQ(drawing->world().vertices().size(), 4U);
	buildEverySegment(drawing->world());
	EXPECT_EQ(construction::detectRooms(drawing->world()).size(), 1U);
}

TEST_F(DrawingWallChainTest, ClosedLoopMatchesTheTwoChainWorkaround) {
	hostFoundation(*drawing);
	drawing->activateWallTool();
	clickRoomCorners(*drawing);
	clickWith(*drawing, kRoom[0]);

	world_sim::DrawingSystem workaround(world_sim::DrawingSystem::Args{.world = &seen.world, .camera = &seen.camera, .navigation = nullptr});
	hostFoundation(workaround);
	workaround.activateWallTool();
	clickRoomCorners(workaround);
	ASSERT_TRUE(workaround.finishChain());
	clickWith(workaround, kRoom[3]);
	clickWith(workaround, kRoom[0]);
	ASSERT_TRUE(workaround.finishChain());

	ASSERT_EQ(workaround.world().segments().size(), 4U);
	EXPECT_TRUE(topologyOf(drawing->world()) == topologyOf(workaround.world()));
}

TEST_F(DrawingWallChainTest, ClosingAcrossTheChainIsRefused) {
	hostFoundation(*drawing);
	drawing->activateWallTool();
	// The closing segment runs along y = 0 and crosses the third segment at x = 0.
	for (const glm::vec2 point : {glm::vec2{-10.0F, 0.0F}, glm::vec2{-10.0F, 8.0F}, glm::vec2{0.0F, 8.0F}, glm::vec2{0.0F, -8.0F},
								  glm::vec2{10.0F, -8.0F}, glm::vec2{10.0F, 0.0F}}) {
		clickWith(*drawing, point);
	}
	ASSERT_EQ(drawing->status().pointCount, 6) << lastToast();
	clickWith(*drawing, {-10.0F, 0.0F});
	EXPECT_EQ(drawing->status().pointCount, 6);
	EXPECT_TRUE(drawing->world().segments().empty());
	EXPECT_EQ(toasts.back().second, construction::validationReason(construction::ValidationCode::SelfIntersects));
}

TEST_F(DrawingWallChainTest, TwoPointChainClickedOnItsStartIsRefused) {
	hostFoundation(*drawing);
	drawing->activateWallTool();
	clickWith(*drawing, kRoom[0]);
	clickWith(*drawing, kRoom[1]);
	ASSERT_EQ(drawing->status().pointCount, 2) << lastToast();
	clickWith(*drawing, kRoom[0]);
	EXPECT_EQ(drawing->status().pointCount, 2);
	EXPECT_TRUE(drawing->world().segments().empty());
	EXPECT_EQ(toasts.back().second, construction::validationReason(construction::ValidationCode::AngleTooSharp));
}

TEST_F(DrawingWallChainTest, ClickingAMiddleVertexIsRefused) {
	hostFoundation(*drawing);
	drawing->activateWallTool();
	clickRoomCorners(*drawing);
	clickWith(*drawing, kRoom[1]);
	EXPECT_EQ(drawing->status().pointCount, 4);
	EXPECT_TRUE(drawing->world().segments().empty());
	EXPECT_EQ(toasts.back().second, construction::validationReason(construction::ValidationCode::SelfIntersects));
}

TEST_F(DrawingWallChainTest, DevClosedLoopBuiltFormsARoom) {
	const construction::FoundationId host = hostFoundation(*drawing);
	std::vector<Foundation::Vec2> loop;
	for (const glm::vec2 corner : kRoom) {
		loop.push_back({corner.x, corner.y});
	}
	loop.push_back(loop.front());
	EXPECT_EQ(drawing->devCommitWalls(loop, "Wood", "Standard", host, true), 4);
	EXPECT_EQ(drawing->world().vertices().size(), 4U);
	EXPECT_EQ(construction::detectRooms(drawing->world()).size(), 1U);
}

namespace {

	// A chain started on a plain point of the host's bottom edge (y = -15), so its first point is the edge snap
	// inset by the wall's half-thickness, not a corner or a round number.
	class DrawingWallEdgeChainTest : public DrawingWallChainTest {
	  protected:
		static constexpr glm::vec2 kNearBottomEdge = {0.0F, -15.2F};

		// First point on the edge, then (10, firstY) on the same edge, (10, 0), and (0, 0). Returns firstY.
		float clickEdgeStartedRoom(world_sim::DrawingSystem& tool) const {
			for (const glm::vec2 point : {kNearBottomEdge, glm::vec2{10.0F, -15.2F}, glm::vec2{10.0F, 0.0F}, glm::vec2{0.0F, 0.0F}}) {
				clickWith(tool, point);
			}
			return 20.0F - tool.status().totalLengthMeters;
		}

		// The close radius the tool uses at the fixture's zoom of 1.
		static float originCloseRadiusMeters() {
			const auto& registry = assets::ConstructionRegistry::Get();
			return std::max(registry.snapping().originCloseRadiusMeters,
							registry.rendering().preview.originHaloMinRadiusPx / world_sim::testing::kPixelsPerMeter);
		}
	};

} // namespace

TEST_F(DrawingWallEdgeChainTest, ClickNearTheFirstPointClosesTheLoop) {
	hostFoundation(*drawing);
	drawing->activateWallTool();
	const float firstY = clickEdgeStartedRoom(*drawing);
	ASSERT_EQ(drawing->status().pointCount, 4) << lastToast();
	const glm::vec2 nearFirst{0.4F, -14.5F};
	ASSERT_LT(glm::length(nearFirst - glm::vec2{0.0F, firstY}), originCloseRadiusMeters());
	hover(nearFirst);
	EXPECT_TRUE(drawing->status().valid) << drawing->status().message;
	EXPECT_NEAR(drawing->status().segmentLengthMeters, -firstY, 1e-3F);
	clickWith(*drawing, nearFirst);
	EXPECT_EQ(drawing->status().pointCount, 0) << lastToast();
	EXPECT_EQ(drawing->world().segments().size(), 4U) << lastToast();
	buildEverySegment(drawing->world());
	EXPECT_EQ(construction::detectRooms(drawing->world()).size(), 1U);
}

TEST_F(DrawingWallEdgeChainTest, ClickJustOutsideTheRadiusAddsAPoint) {
	hostFoundation(*drawing);
	drawing->activateWallTool();
	const float firstY = clickEdgeStartedRoom(*drawing);
	ASSERT_EQ(drawing->status().pointCount, 4) << lastToast();
	const glm::vec2 outside{0.0F, firstY + originCloseRadiusMeters() + 0.1F};
	hover(outside);
	EXPECT_NEAR(drawing->status().segmentLengthMeters, -outside.y, 1e-3F);
	clickWith(*drawing, outside);
	EXPECT_EQ(drawing->status().pointCount, 5) << lastToast();
	EXPECT_TRUE(drawing->world().segments().empty());
}

TEST_F(DrawingWallEdgeChainTest, TwoPointChainDoesNotSnapToItsFirstPoint) {
	hostFoundation(*drawing);
	drawing->activateWallTool();
	clickWith(*drawing, kNearBottomEdge);
	clickWith(*drawing, {10.0F, -15.2F});
	ASSERT_EQ(drawing->status().pointCount, 2) << lastToast();
	const glm::vec2 nearFirst{0.4F, -14.2F};
	hover(nearFirst);
	// Snapped onto the first point, the rubber band would be the first segment's 10 m.
	EXPECT_LT(drawing->status().segmentLengthMeters, 9.9F);
	clickWith(*drawing, nearFirst);
	EXPECT_EQ(drawing->status().pointCount, 2);
	EXPECT_TRUE(drawing->world().segments().empty());
}

namespace {

	using world_sim::testing::kCreekY;

	// The wall tool over a real nav mesh crossed by a fordable creek. A wall starts on a foundation,
	// and the foundation tool never lays one over a ford, so each test commits its host foundation
	// straight into the construction world: across the creek to reach the ford, on the bank to
	// reach dry ground. What these cover is the wall tool's own ground gate, at a vertex and at
	// commit; how a foundation came to span a ford is not theirs.
	class DrawingFordTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			assets::ConstructionRegistry::Get().clear();
			ASSERT_TRUE(assets::ConstructionRegistry::Get().load(constructionConfigFolder()));
			navigation = &world_sim::testing::meshCreekWorld(seen.world, emptyPlacement);
			drawing = std::make_unique<world_sim::DrawingSystem>(world_sim::DrawingSystem::Args{
				.world = &seen.world,
				.camera = &seen.camera,
				.navigation = navigation,
				.callbacks = {.onToast = [this](const std::string& title, const std::string& message) { toasts.emplace_back(title, message); }}
			});
		}
		void TearDown() override { assets::ConstructionRegistry::Get().clear(); }

		void hostFoundation(float southY, float northY) {
			const auto foundation =
				drawing->world().commitFoundation(std::vector<Foundation::Vec2>{{4.0F, southY}, {16.0F, southY}, {16.0F, northY}, {4.0F, northY}}, "Wood");
			ASSERT_TRUE(foundation.ok());
			drawing->world().setState(foundation.id, construction::FoundationState::Built);
		}
		bool click(glm::vec2 meters) {
			const glm::vec2 screen = seen.screenOf(meters);
			return drawing->handleClick(screen.x, screen.y, kViewportWidth, kViewportHeight, true);
		}
		void hover(glm::vec2 meters) {
			const glm::vec2 screen = seen.screenOf(meters);
			drawing->handleMouseMove(screen.x, screen.y, kViewportWidth, kViewportHeight, true);
		}
		[[nodiscard]] bool lastToastSaysNotBuildable() const {
			return !toasts.empty() && toasts.back().second.find("not on buildable ground") != std::string::npos;
		}

		world_sim::testing::SeenWorld							   seen;
		ecs::EmptyPlacement										   emptyPlacement;
		ecs::NavigationSystem*									   navigation = nullptr;
		std::vector<std::pair<std::string, std::string>>		   toasts;
		std::unique_ptr<world_sim::DrawingSystem> drawing;
	};

} // namespace

TEST_F(DrawingFordTest, WallVertexInTheCreekIsRefused) {
	hostFoundation(kCreekY - 6.0F, kCreekY + 6.0F);
	drawing->activateWallTool();
	click({10.0F, kCreekY - 3.0F});
	ASSERT_EQ(drawing->status().pointCount, 1) << (toasts.empty() ? "no toast" : toasts.back().second);
	hover({10.0F, kCreekY});
	EXPECT_FALSE(drawing->status().valid);
	EXPECT_EQ(drawing->status().message, "not on buildable ground");
	click({10.0F, kCreekY});
	EXPECT_EQ(drawing->status().pointCount, 1);
	EXPECT_TRUE(lastToastSaysNotBuildable());
}

TEST_F(DrawingFordTest, WallChainAcrossTheCreekIsRefusedAtCommit) {
	hostFoundation(kCreekY - 6.0F, kCreekY + 6.0F);
	drawing->activateWallTool();
	click({10.0F, kCreekY - 3.0F});
	click({10.0F, kCreekY + 3.0F});
	ASSERT_EQ(drawing->status().pointCount, 2) << "both ends are on the bank" << (toasts.empty() ? "" : ": " + toasts.back().second);
	ASSERT_TRUE(drawing->finishChain());
	EXPECT_TRUE(drawing->world().segments().empty());
	EXPECT_TRUE(lastToastSaysNotBuildable()) << (toasts.empty() ? "no toast" : toasts.back().second);
}

TEST_F(DrawingFordTest, WallChainOnTheBankIsCommitted) {
	hostFoundation(kCreekY - 14.0F, kCreekY - 2.0F);
	drawing->activateWallTool();
	click({10.0F, kCreekY - 11.0F});
	click({10.0F, kCreekY - 5.0F});
	ASSERT_EQ(drawing->status().pointCount, 2) << (toasts.empty() ? "no toast" : toasts.back().second);
	ASSERT_TRUE(drawing->finishChain());
	EXPECT_FALSE(drawing->world().segments().empty()) << (toasts.empty() ? "no toast" : toasts.back().second);
}
