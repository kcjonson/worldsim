#include "scenes/game/world/construction/DrawingSystem.h"

#include "scenes/game/world/CreekWorldFixture.test.h"
#include "scenes/game/world/PlayerVisibilityFixture.test.h"
#include "UserSettings.h"

#include <assets/ConstructionRegistry.h>
#include <construction/ConstructionValidator.h>
#include <construction/ConstructionWorld.h>
#include <construction/RoomDetection.h>
#include <ecs/GoalTaskRegistry.h>
#include <ecs/components/StructureBlueprint.h>
#include <ecs/systems/ConstructionSystem.h>

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
			const auto wall = built.commitSegment(
				geometry::quantize(Foundation::Vec2{32.0F, 0.0F}),
				geometry::quantize(Foundation::Vec2{58.0F, 0.0F}),
				"Wood",
				"Standard",
				foundation.id,
				built.version()
			);
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

// The pointer on and near the first point: the click closes and commits a valid shape, so the
// preview reads valid and draws the halo, though the point the pointer is on would be refused.
TEST_F(DrawingVisibilityTest, AClosePreviewReadsTheShapeTheClickCommits) {
	drawing->activateFoundationTool();
	for (const glm::vec2 corner : {glm::vec2{0.0F, 0.0F}, glm::vec2{4.0F, 0.0F}, glm::vec2{4.0F, -4.0F}}) {
		click(corner);
	}
	ASSERT_EQ(drawing->status().pointCount, 3);
	for (const glm::vec2 pointer : {glm::vec2{0.0F, 0.0F}, glm::vec2{0.2F, -0.1F}}) {
		SCOPED_TRACE(::testing::Message() << "pointer at " << pointer.x << ", " << pointer.y);
		hover(pointer);
		const world_sim::DrawingSystem::PreviewVerdict verdict = drawing->previewVerdict();
		EXPECT_TRUE(verdict.closeHalo);
		EXPECT_TRUE(verdict.valid);
	}
	click({0.0F, 0.0F});
	EXPECT_EQ(drawing->world().foundations().size(), 1U) << (toasts.empty() ? "no toast" : toasts.back().second);
}

// The closing edge crosses a built floor: the click refuses the close, so no halo draws and
// the preview reads invalid.
TEST_F(DrawingVisibilityTest, AClosePreviewTheClickRefusesDrawsNoHaloAndReadsInvalid) {
	construction::ConstructionWorld& built = drawing->world();
	const auto floor = built.commitFoundation(std::vector<Foundation::Vec2>{{8.0F, -2.0F}, {12.0F, -2.0F}, {12.0F, 2.0F}, {8.0F, 2.0F}}, "Wood");
	ASSERT_TRUE(floor.ok());
	built.setState(floor.id, construction::FoundationState::Built);
	drawing->activateFoundationTool();
	for (const glm::vec2 corner : {glm::vec2{0.0F, -6.0F}, glm::vec2{20.0F, -6.0F}, glm::vec2{20.0F, 6.0F}}) {
		click(corner);
	}
	ASSERT_EQ(drawing->status().pointCount, 3) << (toasts.empty() ? "no toast" : toasts.back().second);
	hover({0.0F, -6.0F});
	const world_sim::DrawingSystem::PreviewVerdict verdict = drawing->previewVerdict();
	EXPECT_FALSE(verdict.closeHalo);
	EXPECT_FALSE(verdict.valid);
	EXPECT_TRUE(verdict.markerInvalid) << "the pointer is on good ground, but the close is refused";
	EXPECT_FALSE(drawing->status().valid) << "the strip reads the close, not the point";
	const std::string shown = drawing->status().message;
	click({0.0F, -6.0F});
	EXPECT_EQ(built.foundations().size(), 1U);
	ASSERT_FALSE(toasts.empty());
	EXPECT_EQ(toasts.back().first, "Cannot close");
	EXPECT_EQ(toasts.back().second, shown);
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

	// The snap runs every frame while a tool is active; it is evaluated again only when
	// one of its inputs moved. Each case changes one input and counts evaluations.
	class DrawingSnapMemoTest : public DrawingVisibilityTest {
	  protected:
		void SetUp() override {
			DrawingVisibilityTest::SetUp();
			savedSettings = world_sim::UserSettings::Get().drawing;
		}
		void TearDown() override {
			world_sim::UserSettings::Get().drawing = savedSettings;
			DrawingVisibilityTest::TearDown();
		}

		// The viewport's centre, which maps to the camera's position (the world origin)
		// at every zoom, so a zoom change moves nothing else.
		void hoverCentre(bool freeform = false) {
			drawing->handleMouseMove(kViewportWidth / 2.0F, kViewportHeight / 2.0F, kViewportWidth, kViewportHeight, freeform);
		}
		// Hover twice, so the count after it is one evaluation past `before` exactly when
		// the change forced one and the frame after it reused it.
		std::uint64_t evaluationsAfterHoveringTwice(glm::vec2 meters) {
			hover(meters);
			hover(meters);
			return drawing->snapEvaluationCount();
		}

		world_sim::DrawingSettings savedSettings;
	};

} // namespace

TEST_F(DrawingSnapMemoTest, AnUnchangedFrameReusesTheSnap) {
	drawing->activateFoundationTool();
	hover(kSeenPoint);
	const std::uint64_t first = drawing->snapEvaluationCount();
	EXPECT_EQ(first, 1U);
	hover(kSeenPoint);
	hover(kSeenPoint);
	EXPECT_EQ(drawing->snapEvaluationCount(), first);
}

TEST_F(DrawingSnapMemoTest, ACursorMoveRecomputes) {
	drawing->activateFoundationTool();
	hover(kSeenPoint);
	const std::uint64_t before = drawing->snapEvaluationCount();
	EXPECT_EQ(evaluationsAfterHoveringTwice(kSeenPoint + glm::vec2{1.0F, 0.0F}), before + 1);
}

TEST_F(DrawingSnapMemoTest, PlacingOrRemovingAPointRecomputes) {
	drawing->activateFoundationTool();
	hover(kSeenPoint);
	click(kSeenPoint);
	ASSERT_EQ(drawing->status().pointCount, 1);
	const std::uint64_t placed = drawing->snapEvaluationCount();
	EXPECT_EQ(evaluationsAfterHoveringTwice(kSeenPoint), placed + 1);
	ASSERT_TRUE(drawing->removeLastPoint());
	EXPECT_EQ(evaluationsAfterHoveringTwice(kSeenPoint), placed + 2);
}

TEST_F(DrawingSnapMemoTest, ChangingAnAidRecomputes) {
	drawing->activateFoundationTool();
	hover(kSeenPoint);
	const std::uint64_t before = drawing->snapEvaluationCount();
	world_sim::UserSettings::Get().drawing.angleSteps = !world_sim::UserSettings::Get().drawing.angleSteps;
	EXPECT_EQ(evaluationsAfterHoveringTwice(kSeenPoint), before + 1);
}

TEST_F(DrawingSnapMemoTest, ADisplayOnlySettingDoesNotRecompute) {
	drawing->activateFoundationTool();
	hover(kSeenPoint);
	const std::uint64_t before = drawing->snapEvaluationCount();
	world_sim::UserSettings::Get().drawing.guideDistances = !world_sim::UserSettings::Get().drawing.guideDistances;
	EXPECT_EQ(evaluationsAfterHoveringTwice(kSeenPoint), before);
}

TEST_F(DrawingSnapMemoTest, AConstructionWorldChangeRecomputes) {
	drawing->activateFoundationTool();
	hover(kSeenPoint);
	const std::uint64_t before = drawing->snapEvaluationCount();
	ASSERT_TRUE(drawing->world()
					.commitFoundation(std::vector<Foundation::Vec2>{{-30.0F, -30.0F}, {-25.0F, -30.0F}, {-25.0F, -25.0F}, {-30.0F, -25.0F}}, "Wood")
					.ok());
	EXPECT_EQ(evaluationsAfterHoveringTwice(kSeenPoint), before + 1);
}

TEST_F(DrawingSnapMemoTest, AZoomChangeRecomputes) {
	drawing->activateFoundationTool();
	hoverCentre();
	const std::uint64_t before = drawing->snapEvaluationCount();
	seen.camera.setZoom(2.0F);
	hoverCentre();
	hoverCentre();
	EXPECT_EQ(drawing->snapEvaluationCount(), before + 1);
}

TEST_F(DrawingSnapMemoTest, HoldingAltRecomputes) {
	drawing->activateFoundationTool();
	hoverCentre();
	const std::uint64_t before = drawing->snapEvaluationCount();
	hoverCentre(/*freeform=*/true);
	hoverCentre(/*freeform=*/true);
	EXPECT_EQ(drawing->snapEvaluationCount(), before + 1);
}

TEST_F(DrawingSnapMemoTest, SwitchingToolsRecomputes) {
	drawing->activateFoundationTool();
	hover(kSeenPoint);
	const std::uint64_t before = drawing->snapEvaluationCount();
	drawing->activateWallTool();
	EXPECT_EQ(evaluationsAfterHoveringTwice(kSeenPoint), before + 1);
	drawing->setActiveThicknessPreset("Heavy");
	EXPECT_EQ(evaluationsAfterHoveringTwice(kSeenPoint), before + 2);
}

TEST_F(DrawingSnapMemoTest, SwitchingToolsRecomputesWhenNothingElseInTheKeyChanged) {
	drawing->activateFoundationTool();
	drawing->setActiveThicknessPreset("NoSuchPreset");
	hover(kSeenPoint);
	const std::uint64_t before = drawing->snapEvaluationCount();
	drawing->activateWallTool();
	EXPECT_EQ(evaluationsAfterHoveringTwice(kSeenPoint), before + 1);
}

namespace {

	// The wall tool drawing a room inside a built foundation on seen ground, through mouse input
	// with the ordinary snaps on.
	class DrawingWallChainTest : public DrawingVisibilityTest {
	  protected:
		static constexpr glm::vec2 kRoom[] = {{-8.0F, -8.0F}, {8.0F, -8.0F}, {8.0F, 8.0F}, {-8.0F, 8.0F}};

		static construction::FoundationId hostFoundation(world_sim::DrawingSystem& tool) {
			const auto foundation = tool.world().commitFoundation(
				std::vector<Foundation::Vec2>{{-20.0F, -15.0F}, {20.0F, -15.0F}, {20.0F, 15.0F}, {-20.0F, 15.0F}}, "Wood"
			);
			EXPECT_TRUE(foundation.ok());
			tool.world().setState(foundation.id, construction::FoundationState::Built);
			return foundation.id;
		}
		bool clickWith(world_sim::DrawingSystem& tool, glm::vec2 meters) const {
			const glm::vec2 screen = seen.screenOf(meters);
			return tool.handleClick(screen.x, screen.y, kViewportWidth, kViewportHeight, false);
		}
		void clickFreeform(glm::vec2 meters) const {
			const glm::vec2 screen = seen.screenOf(meters);
			drawing->handleClick(screen.x, screen.y, kViewportWidth, kViewportHeight, true);
		}
		void hoverFreeform(glm::vec2 meters) const {
			const glm::vec2 screen = seen.screenOf(meters);
			drawing->handleMouseMove(screen.x, screen.y, kViewportWidth, kViewportHeight, true);
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
		[[nodiscard]] std::string lastToast() const {
			return toasts.empty() ? "no toast" : toasts.back().first + ": " + toasts.back().second;
		}
	};

} // namespace

TEST_F(DrawingWallChainTest, ClickingTheFirstPointClosesAndCommitsTheLoop) {
	hostFoundation(*drawing);
	drawing->activateWallTool();
	clickRoomCorners(*drawing);
	ASSERT_EQ(drawing->status().pointCount, 4) << lastToast();
	hover(kRoom[0]);
	EXPECT_TRUE(drawing->status().valid) << drawing->status().message;
	EXPECT_TRUE(drawing->previewVerdict().valid);
	EXPECT_TRUE(drawing->previewVerdict().closeHalo);
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

	world_sim::DrawingSystem workaround(
		world_sim::DrawingSystem::Args{.world = &seen.world, .camera = &seen.camera, .navigation = nullptr}
	);
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

namespace {

	// A built 2 m wall along y = 0 inside the host, and chains drawn freeform around it.
	class DrawingWallCloseTest : public DrawingWallChainTest {
	  protected:
		void SetUp() override {
			DrawingWallChainTest::SetUp();
			const construction::FoundationId host = hostFoundation(*drawing);
			const auto wall = drawing->world().commitSegment(
				geometry::quantize(Foundation::Vec2{-1.0F, 0.0F}),
				geometry::quantize(Foundation::Vec2{1.0F, 0.0F}),
				"Wood",
				"Standard",
				host,
				drawing->world().version()
			);
			ASSERT_TRUE(wall.ok());
			buildEverySegment(drawing->world());
			drawing->activateWallTool();
		}
		void placeFreeform(std::initializer_list<glm::vec2> chain) const {
			for (const glm::vec2 point : chain) {
				clickFreeform(point);
			}
		}
		[[nodiscard]] std::size_t segmentCount() const { return drawing->world().segments().size(); }
	};

} // namespace

// The closing segment crosses the wall. The two placed segments would build and the closing
// one would not, so there is no halo and the band reads invalid; the click still builds the two.
TEST_F(DrawingWallCloseTest, ClosingSegmentAcrossAWallDrawsNoHaloAndTheClickBuildsTheRest) {
	placeFreeform({{1.6F, 1.2F}, {1.6F, -0.8F}, {-0.6F, -1.1F}});
	ASSERT_EQ(drawing->status().pointCount, 3) << lastToast();
	hover({1.6F, 1.2F});
	const world_sim::DrawingSystem::PreviewVerdict verdict = drawing->previewVerdict();
	EXPECT_FALSE(verdict.closeHalo);
	EXPECT_FALSE(verdict.valid);
	EXPECT_TRUE(verdict.markerInvalid);
	EXPECT_FALSE(drawing->status().valid);
	EXPECT_EQ(drawing->status().message, "wall: " + construction::validationReason(construction::ValidationCode::XCrossing));
	clickWith(*drawing, {1.6F, 1.2F});
	EXPECT_EQ(drawing->status().pointCount, 0);
	EXPECT_EQ(segmentCount(), 1U + 2U) << lastToast();
	EXPECT_EQ(toasts.back().second, "2 segments (Wood)");
}

// The close that built part of the chain cleared the tool: before any move, the strip and
// the marker read as an empty tool's, not as the refused close.
TEST_F(DrawingWallCloseTest, RightAfterAPartialCloseThePictureReadsValid) {
	placeFreeform({{1.6F, 1.2F}, {1.6F, -0.8F}, {-0.6F, -1.1F}});
	ASSERT_EQ(drawing->status().pointCount, 3) << lastToast();
	clickWith(*drawing, {1.6F, 1.2F});
	ASSERT_EQ(drawing->status().pointCount, 0);
	EXPECT_TRUE(drawing->status().valid) << drawing->status().message;
	EXPECT_FALSE(drawing->previewVerdict().markerInvalid);
}

// An earlier segment crosses the wall and the closing one does not: the same.
TEST_F(DrawingWallCloseTest, EarlierSegmentAcrossAWallDrawsNoHaloAndTheClickBuildsTheRest) {
	placeFreeform({{0.0F, 1.5F}, {0.0F, -1.5F}, {3.0F, -1.0F}});
	ASSERT_EQ(drawing->status().pointCount, 3) << lastToast();
	hover({0.0F, 1.5F});
	EXPECT_FALSE(drawing->previewVerdict().closeHalo);
	EXPECT_FALSE(drawing->previewVerdict().valid);
	EXPECT_FALSE(drawing->status().valid);
	clickWith(*drawing, {0.0F, 1.5F});
	EXPECT_EQ(drawing->status().pointCount, 0);
	EXPECT_EQ(segmentCount(), 1U + 2U) << lastToast();
}

// Clear of the wall, the whole chain builds: the halo, and three new segments.
TEST_F(DrawingWallCloseTest, ChainClearOfTheWallDrawsTheHaloAndBuildsWhole) {
	placeFreeform({{3.0F, 1.0F}, {3.0F, -2.0F}, {6.0F, -1.0F}});
	ASSERT_EQ(drawing->status().pointCount, 3) << lastToast();
	hover({3.0F, 1.0F});
	EXPECT_TRUE(drawing->previewVerdict().closeHalo);
	EXPECT_TRUE(drawing->previewVerdict().valid);
	EXPECT_TRUE(drawing->status().valid) << drawing->status().message;
	clickWith(*drawing, {3.0F, 1.0F});
	EXPECT_EQ(segmentCount(), 1U + 3U) << lastToast();
}

// A wall committed across the closing segment between two hovers at the same spot: the
// chain's segment checks are not reused from before it.
TEST_F(DrawingWallCloseTest, AWallAddedBetweenHoversDropsTheHalo) {
	placeFreeform({{3.0F, 1.0F}, {3.0F, -2.0F}, {6.0F, -1.0F}});
	ASSERT_EQ(drawing->status().pointCount, 3) << lastToast();
	hover({3.0F, 1.0F});
	ASSERT_TRUE(drawing->previewVerdict().closeHalo);
	const construction::FoundationId host = drawing->world().foundations().front().id;
	const auto wall = drawing->world().commitSegment(
		geometry::quantize(Foundation::Vec2{4.5F, 1.0F}),
		geometry::quantize(Foundation::Vec2{4.5F, -1.0F}),
		"Wood",
		"Standard",
		host,
		drawing->world().version()
	);
	ASSERT_TRUE(wall.ok());
	hover({3.0F, 1.0F});
	EXPECT_FALSE(drawing->previewVerdict().closeHalo);
	EXPECT_FALSE(drawing->status().valid);
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

TEST_F(DrawingWallChainTest, DevCommittedChainSharesOnePlacementOrderEqualToTheVersionBefore) {
	const construction::FoundationId host = hostFoundation(*drawing);
	std::vector<Foundation::Vec2>	 loop;
	for (const glm::vec2 corner : kRoom) {
		loop.push_back({corner.x, corner.y});
	}
	loop.push_back(loop.front());
	const std::uint64_t versionBefore = drawing->world().version();
	ASSERT_EQ(drawing->devCommitWalls(loop, "Wood", "Standard", host, false), 4);
	ASSERT_EQ(drawing->world().segments().size(), 4U);
	for (const construction::WallSegment& segment : drawing->world().segments()) {
		EXPECT_EQ(segment.placementOrder, versionBefore);
	}
}

TEST_F(DrawingWallChainTest, ToolDrawnChainSharesOnePlacementOrder) {
	hostFoundation(*drawing);
	drawing->activateWallTool();
	clickRoomCorners(*drawing);
	clickWith(*drawing, kRoom[0]);
	ASSERT_EQ(drawing->world().segments().size(), 4U) << lastToast();
	const std::uint64_t order = drawing->world().segments().front().placementOrder;
	for (const construction::WallSegment& segment : drawing->world().segments()) {
		EXPECT_EQ(segment.placementOrder, order);
	}
}

TEST_F(DrawingWallChainTest, ALaterChainTakesAGreaterPlacementOrder) {
	hostFoundation(*drawing);
	drawing->activateWallTool();
	clickRoomCorners(*drawing);
	clickWith(*drawing, kRoom[0]);
	ASSERT_EQ(drawing->world().segments().size(), 4U) << lastToast();
	const std::uint64_t firstOrder = drawing->world().segments().front().placementOrder;

	for (const glm::vec2 point : {glm::vec2{-18.0F, -12.0F}, glm::vec2{-12.0F, -12.0F}, glm::vec2{-12.0F, -4.0F}}) {
		clickWith(*drawing, point);
	}
	ASSERT_TRUE(drawing->finishChain());
	ASSERT_EQ(drawing->world().segments().size(), 6U) << lastToast();
	std::size_t laterChain = 0;
	for (const construction::WallSegment& segment : drawing->world().segments()) {
		if (segment.placementOrder == firstOrder) {
			continue;
		}
		EXPECT_GT(segment.placementOrder, firstOrder);
		++laterChain;
	}
	EXPECT_EQ(laterChain, 2U);
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

TEST_F(DrawingWallEdgeChainTest, AltClickNearTheFirstPointAddsAnOrdinaryPoint) {
	hostFoundation(*drawing);
	drawing->activateWallTool();
	const float firstY = clickEdgeStartedRoom(*drawing);
	ASSERT_EQ(drawing->status().pointCount, 4) << lastToast();
	const glm::vec2 nearFirst{0.0F, firstY + originCloseRadiusMeters() - 0.1F};
	hoverFreeform(nearFirst);
	EXPECT_TRUE(drawing->status().valid) << drawing->status().message;
	EXPECT_NEAR(drawing->status().segmentLengthMeters, -nearFirst.y, 1e-3F);
	clickFreeform(nearFirst);
	EXPECT_EQ(drawing->status().pointCount, 5) << lastToast();
	EXPECT_TRUE(drawing->world().segments().empty());
}

TEST_F(DrawingWallChainTest, AltClickAfterARefusedCloseStillPlacesAPoint) {
	hostFoundation(*drawing);
	drawing->activateWallTool();
	// The closing segment would meet the first segment at under the minimum junction angle.
	for (const glm::vec2 point : {glm::vec2{-8.0F, -8.0F}, glm::vec2{8.0F, -8.0F}, glm::vec2{2.0F, -3.0F}}) {
		clickWith(*drawing, point);
	}
	ASSERT_EQ(drawing->status().pointCount, 3) << lastToast();
	clickWith(*drawing, {-8.0F, -8.0F});
	EXPECT_EQ(drawing->status().pointCount, 3);
	EXPECT_EQ(toasts.back().second, construction::validationReason(construction::ValidationCode::AngleTooSharp));
	clickFreeform({-7.5F, -7.4F});
	EXPECT_EQ(drawing->status().pointCount, 4) << lastToast();
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
		// With the snaps on, so a pointer on the first point snaps to it and closes.
		void hoverToClose(glm::vec2 meters) {
			const glm::vec2 screen = seen.screenOf(meters);
			drawing->handleMouseMove(screen.x, screen.y, kViewportWidth, kViewportHeight, false);
		}
		void clickToClose(glm::vec2 meters) {
			const glm::vec2 screen = seen.screenOf(meters);
			drawing->handleClick(screen.x, screen.y, kViewportWidth, kViewportHeight, false);
		}
		[[nodiscard]] std::string lastToast() const {
			return toasts.empty() ? "no toast" : toasts.back().first + ": " + toasts.back().second;
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

TEST_F(DrawingFordTest, WallCloseAcrossTheCreekIsRefused) {
	hostFoundation(kCreekY - 6.0F, kCreekY + 6.0F);
	drawing->activateWallTool();
	for (const glm::vec2 point : {glm::vec2{6.0F, kCreekY - 3.0F}, glm::vec2{14.0F, kCreekY - 3.0F}, glm::vec2{10.0F, kCreekY + 3.0F}}) {
		click(point);
	}
	ASSERT_EQ(drawing->status().pointCount, 3) << lastToast();
	hoverToClose({6.0F, kCreekY - 3.0F});
	EXPECT_FALSE(drawing->previewVerdict().closeHalo);
	EXPECT_FALSE(drawing->previewVerdict().valid);
	clickToClose({6.0F, kCreekY - 3.0F});
	EXPECT_TRUE(drawing->world().segments().empty());
	EXPECT_EQ(lastToast(), "Can't build here: wall: not on buildable ground");
	EXPECT_EQ(drawing->status().pointCount, 0);
}

TEST_F(DrawingFordTest, WallCloseOnTheBankIsAccepted) {
	hostFoundation(kCreekY - 14.0F, kCreekY - 2.0F);
	drawing->activateWallTool();
	for (const glm::vec2 point : {glm::vec2{6.0F, kCreekY - 12.0F}, glm::vec2{14.0F, kCreekY - 12.0F}, glm::vec2{10.0F, kCreekY - 4.0F}}) {
		click(point);
	}
	ASSERT_EQ(drawing->status().pointCount, 3) << lastToast();
	hoverToClose({6.0F, kCreekY - 12.0F});
	EXPECT_TRUE(drawing->previewVerdict().closeHalo);
	EXPECT_TRUE(drawing->previewVerdict().valid);
	clickToClose({6.0F, kCreekY - 12.0F});
	EXPECT_EQ(drawing->world().segments().size(), 3U) << lastToast();
}

// The Add's outline is valid, but it would build across the creek: the outline reads invalid
// and the reason is drawn, as the strip says, and the click refuses it with the same words.
TEST_F(DrawingFordTest, AddAcrossTheCreekIsRefused) {
	hostFoundation(kCreekY - 14.0F, kCreekY - 2.0F);
	const construction::FoundationId target = drawing->world().foundations().front().id;
	drawing->activateFoundationEditTool(target, construction::FoundationEditMode::Add);
	for (const glm::vec2 point :
		 {glm::vec2{4.0F, kCreekY - 2.0F}, glm::vec2{16.0F, kCreekY - 2.0F}, glm::vec2{16.0F, kCreekY + 3.0F}, glm::vec2{4.0F, kCreekY + 3.0F}}) {
		click(point);
	}
	ASSERT_EQ(drawing->status().pointCount, 4) << lastToast();
	hoverToClose({4.0F, kCreekY - 2.0F});
	const world_sim::DrawingSystem::PreviewVerdict verdict = drawing->previewVerdict();
	EXPECT_FALSE(verdict.closeHalo);
	EXPECT_FALSE(verdict.valid);
	EXPECT_FALSE(verdict.editOutlineValid);
	EXPECT_EQ(verdict.editReason, "not on buildable ground");
	EXPECT_FALSE(drawing->status().valid);
	EXPECT_EQ(drawing->status().message, "not on buildable ground");
	clickToClose({4.0F, kCreekY - 2.0F});
	EXPECT_EQ(lastToast(), "Can't edit foundation: not on buildable ground");
	EXPECT_EQ(drawing->status().pointCount, 4);
}

// The dev verb's path refuses the same Add, with no tool and no close verdict before it.
TEST_F(DrawingFordTest, AddAcrossTheCreekIsRefusedThroughTheDevVerb) {
	hostFoundation(kCreekY - 14.0F, kCreekY - 2.0F);
	const construction::FoundationId target = drawing->world().foundations().front().id;
	const world_sim::DrawingSystem::FoundationEditOutcome outcome = drawing->applyFoundationEdit(
		target,
		{{4.0F, kCreekY - 2.0F}, {16.0F, kCreekY - 2.0F}, {16.0F, kCreekY + 3.0F}, {4.0F, kCreekY + 3.0F}},
		construction::FoundationEditMode::Add
	);
	EXPECT_FALSE(outcome.ok);
	EXPECT_EQ(outcome.reason, "not on buildable ground");
	EXPECT_EQ(drawing->world().foundations().size(), 1U);
}

TEST_F(DrawingFordTest, FoundationCloseAcrossTheCreekIsRefused) {
	drawing->activateFoundationTool();
	for (const glm::vec2 point :
		 {glm::vec2{4.0F, kCreekY - 3.0F}, glm::vec2{16.0F, kCreekY - 3.0F}, glm::vec2{16.0F, kCreekY + 3.0F}, glm::vec2{4.0F, kCreekY + 3.0F}}) {
		click(point);
	}
	ASSERT_EQ(drawing->status().pointCount, 4) << lastToast();
	hoverToClose({4.0F, kCreekY - 3.0F});
	EXPECT_FALSE(drawing->previewVerdict().closeHalo);
	EXPECT_FALSE(drawing->previewVerdict().valid);
	EXPECT_FALSE(drawing->status().valid);
	EXPECT_EQ(drawing->status().message, "foundation: not on buildable ground");
	clickToClose({4.0F, kCreekY - 3.0F});
	EXPECT_TRUE(drawing->world().foundations().empty());
	EXPECT_EQ(lastToast(), "Can't build here: foundation: not on buildable ground");
}

// A rectangle whose first corner lands in the creek, then closed 0.2 m off its first placed
// corner: every refused click toasts once.
TEST_F(DrawingFordTest, ARefusedClickToastsOnce) {
	drawing->activateFoundationTool();
	const auto clickCountingToasts = [this](glm::vec2 point) {
		const std::size_t before = toasts.size();
		click(point);
		return toasts.size() - before;
	};
	EXPECT_EQ(clickCountingToasts({6.0F, kCreekY}), 1U) << "the corner in the creek";
	for (const glm::vec2 point : {glm::vec2{4.0F, kCreekY - 3.0F}, glm::vec2{16.0F, kCreekY - 3.0F}, glm::vec2{16.0F, kCreekY + 3.0F},
								  glm::vec2{4.0F, kCreekY + 3.0F}}) {
		EXPECT_EQ(clickCountingToasts(point), 0U) << lastToast();
	}
	const std::size_t before = toasts.size();
	clickToClose({4.2F, kCreekY - 3.0F});
	EXPECT_EQ(toasts.size() - before, 1U);
	EXPECT_EQ(lastToast(), "Can't build here: foundation: not on buildable ground");
}

namespace {

	// Closes with every tool on seen ground, and next to the edge of what is seen.
	class DrawingCloseTest : public DrawingVisibilityTest {
	  protected:
		void SetUp() override {
			DrawingVisibilityTest::SetUp();
			// Reshaping a blueprint resets its goals through the construction system.
			seen.world.registerSystem<ecs::ConstructionSystem>();
		}

		construction::FoundationId builtFloor(const std::vector<Foundation::Vec2>& ring) {
			const auto floor = drawing->world().commitFoundation(ring, "Wood");
			EXPECT_TRUE(floor.ok());
			drawing->world().setState(floor.id, construction::FoundationState::Built);
			return floor.id;
		}
		// Freeform, so no snap moves a point.
		void place(std::initializer_list<glm::vec2> corners) {
			for (const glm::vec2 corner : corners) {
				const glm::vec2 screen = seen.screenOf(corner);
				drawing->handleClick(screen.x, screen.y, kViewportWidth, kViewportHeight, true);
			}
		}
		void hoverFreeform(glm::vec2 meters) {
			const glm::vec2 screen = seen.screenOf(meters);
			drawing->handleMouseMove(screen.x, screen.y, kViewportWidth, kViewportHeight, true);
		}
		// A blueprint foundation, 8 m square, drawn with the tool.
		construction::FoundationId blueprintFloor() {
			drawing->activateFoundationTool();
			for (const glm::vec2 corner : {glm::vec2{0.0F, 0.0F}, glm::vec2{8.0F, 0.0F}, glm::vec2{8.0F, 8.0F}, glm::vec2{0.0F, 8.0F}}) {
				click(corner);
			}
			click({0.0F, 0.0F});
			EXPECT_EQ(drawing->world().foundations().size(), 1U) << lastToast();
			return drawing->world().foundations().front().id;
		}
		[[nodiscard]] std::string lastToast() const {
			return toasts.empty() ? "no toast" : toasts.back().first + ": " + toasts.back().second;
		}
	};

} // namespace

TEST_F(DrawingCloseTest, AddOntoABuiltFloorDrawsTheHaloAndPlacesAnExtension) {
	const construction::FoundationId target = builtFloor({{0.0F, 0.0F}, {8.0F, 0.0F}, {8.0F, 8.0F}, {0.0F, 8.0F}});
	drawing->activateFoundationEditTool(target, construction::FoundationEditMode::Add);
	place({{8.0F, 0.0F}, {12.0F, 0.0F}, {12.0F, 8.0F}, {8.0F, 8.0F}});
	ASSERT_EQ(drawing->status().pointCount, 4) << lastToast();
	hover({8.0F, 0.0F});
	EXPECT_TRUE(drawing->previewVerdict().closeHalo);
	EXPECT_TRUE(drawing->previewVerdict().valid);
	EXPECT_TRUE(drawing->previewVerdict().editReason.empty());
	click({8.0F, 0.0F});
	EXPECT_EQ(drawing->status().pointCount, 0) << lastToast();
	EXPECT_EQ(toasts.back().first, "Extension placed") << lastToast();
}

TEST_F(DrawingCloseTest, AddApartFromTheFloorIsRefused) {
	const construction::FoundationId target = builtFloor({{0.0F, 0.0F}, {8.0F, 0.0F}, {8.0F, 8.0F}, {0.0F, 8.0F}});
	drawing->activateFoundationEditTool(target, construction::FoundationEditMode::Add);
	place({{20.0F, 0.0F}, {24.0F, 0.0F}, {24.0F, 4.0F}});
	ASSERT_EQ(drawing->status().pointCount, 3) << lastToast();
	hover({20.0F, 0.0F});
	EXPECT_FALSE(drawing->previewVerdict().closeHalo);
	EXPECT_FALSE(drawing->previewVerdict().valid);
	const std::string reason = drawing->previewVerdict().editReason;
	EXPECT_FALSE(reason.empty());
	click({20.0F, 0.0F});
	EXPECT_EQ(drawing->status().pointCount, 3);
	EXPECT_EQ(lastToast(), "Can't edit foundation: " + reason);
}

TEST_F(DrawingCloseTest, AddOntoUnexploredGroundIsRefused) {
	seen.visibility.revealLanding({60.0F, 0.0F}, 15.0F);
	const construction::FoundationId target = builtFloor({{20.0F, -5.0F}, {30.0F, -5.0F}, {30.0F, 5.0F}, {20.0F, 5.0F}});
	drawing->activateFoundationEditTool(target, construction::FoundationEditMode::Add);
	place({{30.0F, -2.0F}, {65.0F, -2.0F}, {65.0F, 2.0F}, {30.0F, 2.0F}});
	ASSERT_EQ(drawing->status().pointCount, 4) << lastToast();
	hover({30.0F, -2.0F});
	EXPECT_FALSE(drawing->previewVerdict().closeHalo);
	EXPECT_FALSE(drawing->previewVerdict().valid);
	EXPECT_FALSE(drawing->previewVerdict().editOutlineValid);
	EXPECT_EQ(drawing->previewVerdict().editReason, "the area is unexplored");
	click({30.0F, -2.0F});
	EXPECT_EQ(drawing->status().pointCount, 4);
	EXPECT_EQ(lastToast(), "Can't edit foundation: the area is unexplored");
}

TEST_F(DrawingCloseTest, SubtractFromABuiltFloorIsRefused) {
	const construction::FoundationId target = builtFloor({{0.0F, 0.0F}, {8.0F, 0.0F}, {8.0F, 8.0F}, {0.0F, 8.0F}});
	drawing->activateFoundationEditTool(target, construction::FoundationEditMode::Subtract);
	place({{4.0F, 4.0F}, {12.0F, 4.0F}, {12.0F, 12.0F}});
	ASSERT_EQ(drawing->status().pointCount, 3) << lastToast();
	hover({4.0F, 4.0F});
	EXPECT_FALSE(drawing->previewVerdict().closeHalo);
	EXPECT_FALSE(drawing->previewVerdict().valid);
	click({4.0F, 4.0F});
	EXPECT_EQ(drawing->status().pointCount, 3);
	EXPECT_EQ(toasts.back().first, "Can't edit foundation") << lastToast();
}

TEST_F(DrawingCloseTest, SubtractReachingUnexploredGroundIsRefused) {
	seen.visibility.revealLanding({60.0F, 0.0F}, 15.0F);
	const construction::FoundationId target = builtFloor({{20.0F, -5.0F}, {30.0F, -5.0F}, {30.0F, 5.0F}, {20.0F, 5.0F}});
	drawing->activateFoundationEditTool(target, construction::FoundationEditMode::Subtract);
	place({{25.0F, -2.0F}, {65.0F, -2.0F}, {65.0F, 2.0F}, {25.0F, 2.0F}});
	ASSERT_EQ(drawing->status().pointCount, 4) << lastToast();
	hover({25.0F, -2.0F});
	EXPECT_FALSE(drawing->previewVerdict().closeHalo);
	EXPECT_FALSE(drawing->previewVerdict().valid);
	click({25.0F, -2.0F});
	EXPECT_EQ(lastToast(), "Can't edit foundation: the area is unexplored");
}

TEST_F(DrawingCloseTest, SubtractFromABlueprintDrawsTheHaloAndReshapesIt) {
	const construction::FoundationId target = blueprintFloor();
	drawing->activateFoundationEditTool(target, construction::FoundationEditMode::Subtract);
	place({{4.0F, 4.0F}, {12.0F, 4.0F}, {12.0F, 12.0F}, {4.0F, 12.0F}});
	ASSERT_EQ(drawing->status().pointCount, 4) << lastToast();
	hover({4.0F, 4.0F});
	EXPECT_TRUE(drawing->previewVerdict().closeHalo);
	EXPECT_TRUE(drawing->previewVerdict().valid);
	click({4.0F, 4.0F});
	EXPECT_EQ(lastToast(), "Foundation reshaped: area removed");
	EXPECT_NEAR(drawing->world().areaSquareMeters(target), 48.0F, 1e-3F);
}

// Only the closing segment reaches past what is seen.
TEST_F(DrawingCloseTest, WallCloseWhoseClosingSegmentAloneIsUnexploredIsRefused) {
	seen.visibility.revealLanding({50.0F, 0.0F}, 15.0F);
	builtFloor({{20.0F, -5.0F}, {64.0F, -5.0F}, {64.0F, 30.0F}, {20.0F, 30.0F}});
	drawing->activateWallTool();
	place({{30.0F, 25.0F}, {38.0F, 0.0F}, {55.0F, 12.0F}});
	ASSERT_EQ(drawing->status().pointCount, 3) << lastToast();
	hover({30.0F, 25.0F});
	EXPECT_FALSE(drawing->previewVerdict().closeHalo);
	EXPECT_FALSE(drawing->previewVerdict().valid);
	click({30.0F, 25.0F});
	EXPECT_EQ(lastToast(), "Can't build here: wall: the area is unexplored");
	EXPECT_TRUE(drawing->world().segments().empty());
}

// The ground checks read state the snap memo's key does not cover, so they run on every move:
// ground seen after the last move shows the halo on the next one, with the snap reused.
TEST_F(DrawingCloseTest, GroundSeenAfterAMoveReachesThePictureOnTheNext) {
	seen.visibility.revealLanding({50.0F, 0.0F}, 15.0F);
	builtFloor({{20.0F, -5.0F}, {64.0F, -5.0F}, {64.0F, 30.0F}, {20.0F, 30.0F}});
	drawing->activateWallTool();
	place({{30.0F, 25.0F}, {38.0F, 0.0F}, {55.0F, 12.0F}});
	ASSERT_EQ(drawing->status().pointCount, 3) << lastToast();
	hover({30.0F, 25.0F});
	ASSERT_FALSE(drawing->previewVerdict().closeHalo);
	const std::uint64_t evaluations = drawing->snapEvaluationCount();
	seen.visibility.setRevealed(true);
	hover({30.0F, 25.0F});
	EXPECT_EQ(drawing->snapEvaluationCount(), evaluations) << "the snap and the chain's segment checks were reused";
	EXPECT_TRUE(drawing->previewVerdict().closeHalo);
	EXPECT_TRUE(drawing->status().valid) << drawing->status().message;
}

// An edit's outline reads the target's blueprint, which the memo's key does not cover: work
// done on the blueprint after the last move takes the halo away on the next one.
TEST_F(DrawingCloseTest, WorkOnTheBlueprintAfterAMoveReachesThePictureOnTheNext) {
	const construction::FoundationId target = blueprintFloor();
	drawing->activateFoundationEditTool(target, construction::FoundationEditMode::Subtract);
	place({{4.0F, 4.0F}, {12.0F, 4.0F}, {12.0F, 12.0F}, {4.0F, 12.0F}});
	hover({4.0F, 4.0F});
	ASSERT_TRUE(drawing->previewVerdict().closeHalo);
	const std::uint64_t evaluations = drawing->snapEvaluationCount();
	auto* blueprint = seen.world.getComponent<ecs::StructureBlueprint>(drawing->world().get(target)->entity);
	ASSERT_NE(blueprint, nullptr);
	blueprint->workDone = 1.0F;
	hover({4.0F, 4.0F});
	EXPECT_EQ(drawing->snapEvaluationCount(), evaluations);
	EXPECT_FALSE(drawing->previewVerdict().closeHalo);
	EXPECT_FALSE(drawing->previewVerdict().valid);
}

// A foundation committed inside the drawn ring between two hovers at the same spot: the
// closed ring's check is not reused from before it.
TEST_F(DrawingCloseTest, AFoundationAddedBetweenHoversDropsTheHalo) {
	drawing->activateFoundationTool();
	place({{0.0F, 0.0F}, {8.0F, 0.0F}, {8.0F, 8.0F}, {0.0F, 8.0F}});
	ASSERT_EQ(drawing->status().pointCount, 4) << lastToast();
	hover({0.0F, 0.0F});
	ASSERT_TRUE(drawing->previewVerdict().closeHalo);
	ASSERT_TRUE(drawing->world().commitFoundation(std::vector<Foundation::Vec2>{{2.0F, 2.0F}, {4.0F, 2.0F}, {4.0F, 4.0F}, {2.0F, 4.0F}}, "Wood").ok());
	hover({0.0F, 0.0F});
	EXPECT_FALSE(drawing->previewVerdict().closeHalo);
	EXPECT_FALSE(drawing->status().valid);
	click({0.0F, 0.0F});
	EXPECT_EQ(lastToast(), "Cannot close: " + construction::validationReason(construction::ValidationCode::OverlapsExisting));
}

// Zoomed in, the close catch (0.5 m) is smaller than the rescue's reach (1 m), so a pointer
// 0.8 m from the first corner, just outside the shape, is past the catch, refused as a point
// (its edge crosses the first edge), and inside the rescue's reach.
namespace {
	constexpr float		kRescueZoom = 2.0F;
	constexpr glm::vec2 kPastTheCatch{0.7F, -0.4F};
} // namespace

// The rescue closes only a shape that would close: once the ring takes in another foundation,
// the click refuses the point instead of closing.
TEST_F(DrawingCloseTest, TheFoundationRescueRefusesARingThatFailsItsCheck) {
	seen.camera.setZoom(kRescueZoom);
	drawing->activateFoundationTool();
	const glm::vec2 firstCorner{0.0F, 0.0F};
	place({firstCorner, {8.0F, 0.0F}, {8.0F, 8.0F}, {0.0F, 8.0F}});
	ASSERT_EQ(drawing->status().pointCount, 4) << lastToast();
	hoverFreeform(firstCorner + kPastTheCatch);
	ASSERT_TRUE(drawing->previewVerdict().closeHalo) << "the rescue reaches the pointer: " << drawing->status().message;
	ASSERT_TRUE(drawing->world().commitFoundation(std::vector<Foundation::Vec2>{{2.0F, 2.0F}, {4.0F, 2.0F}, {4.0F, 4.0F}, {2.0F, 4.0F}}, "Wood").ok());
	hoverFreeform(firstCorner + kPastTheCatch);
	EXPECT_FALSE(drawing->previewVerdict().closeHalo);
	place({firstCorner + kPastTheCatch});
	EXPECT_EQ(drawing->status().pointCount, 4);
	EXPECT_EQ(drawing->world().foundations().size(), 1U);
	EXPECT_EQ(toasts.back().first, "Invalid point") << lastToast();
}

// The same for an edit: once work on the blueprint makes the Subtract fail, the click refuses
// the point instead of closing.
TEST_F(DrawingCloseTest, TheEditRescueRefusesAnEditThatFailsItsCheck) {
	seen.camera.setZoom(kRescueZoom);
	const construction::FoundationId target = blueprintFloor();
	drawing->activateFoundationEditTool(target, construction::FoundationEditMode::Subtract);
	const glm::vec2 firstCorner{4.0F, 4.0F};
	place({firstCorner, {12.0F, 4.0F}, {12.0F, 12.0F}, {4.0F, 12.0F}});
	ASSERT_EQ(drawing->status().pointCount, 4) << lastToast();
	hoverFreeform(firstCorner + kPastTheCatch);
	ASSERT_TRUE(drawing->previewVerdict().closeHalo) << "the rescue reaches the pointer: " << drawing->status().message;
	auto* blueprint = seen.world.getComponent<ecs::StructureBlueprint>(drawing->world().get(target)->entity);
	ASSERT_NE(blueprint, nullptr);
	blueprint->workDone = 1.0F;
	hoverFreeform(firstCorner + kPastTheCatch);
	EXPECT_FALSE(drawing->previewVerdict().closeHalo);
	place({firstCorner + kPastTheCatch});
	EXPECT_EQ(drawing->status().pointCount, 4);
	EXPECT_NEAR(drawing->world().areaSquareMeters(target), 64.0F, 1e-3F);
	EXPECT_EQ(toasts.back().first, "Invalid point") << lastToast();
}
