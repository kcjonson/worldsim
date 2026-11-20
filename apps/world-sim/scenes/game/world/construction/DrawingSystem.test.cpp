#include "scenes/game/world/construction/DrawingSystem.h"
#include "scenes/game/world/construction/DrawingStatusHint.h"

#include "scenes/game/world/CreekWorldFixture.test.h"
#include "scenes/game/world/PlayerVisibilityFixture.test.h"
#include "scenes/game/world/WorldPointer.h"
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
#include <cmath>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <sstream>
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

	// The drawing settings these cases are written against, set explicitly so they don't follow
	// the player's defaults: angle steps, align to existing buildings, world-axis guides, and
	// crossings on, lengths unrounded.
	world_sim::DrawingSettings testedSettings() {
		return world_sim::DrawingSettings{
			.angleSteps = true,
			.alignToExistingBuildings = true,
			.guidesFromWorldAxes = true,
			.guideCrossings = true,
		};
	}

	class DrawingVisibilityTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			assets::ConstructionRegistry::Get().clear();
			ASSERT_TRUE(assets::ConstructionRegistry::Get().load(constructionConfigFolder()));
			playerSettings = world_sim::UserSettings::Get().drawing;
			world_sim::UserSettings::Get().drawing = testedSettings();
			drawing = std::make_unique<world_sim::DrawingSystem>(world_sim::DrawingSystem::Args{
				.world = &seen.world,
				.camera = &seen.camera,
				.navigation = nullptr,
				.callbacks = {.onToast = [this](const std::string& title, const std::string& message) { toasts.emplace_back(title, message); }}
			});
		}
		void TearDown() override {
			world_sim::UserSettings::Get().drawing = playerSettings;
			assets::ConstructionRegistry::Get().clear();
		}

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
		world_sim::DrawingSettings								   playerSettings;
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

TEST_F(DrawingWallChainTest, ToolDrawnChainsPlacementOrderIsTheVersionBeforeTheClosingClick) {
	hostFoundation(*drawing);
	drawing->activateWallTool();
	clickRoomCorners(*drawing);
	const std::uint64_t versionBefore = drawing->world().version();
	clickWith(*drawing, kRoom[0]);
	ASSERT_EQ(drawing->world().segments().size(), 4U) << lastToast();
	for (const construction::WallSegment& segment : drawing->world().segments()) {
		EXPECT_EQ(segment.placementOrder, versionBefore);
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
			playerSettings = world_sim::UserSettings::Get().drawing;
			world_sim::UserSettings::Get().drawing = testedSettings();
			navigation = &world_sim::testing::meshCreekWorld(seen.world, emptyPlacement);
			drawing = std::make_unique<world_sim::DrawingSystem>(world_sim::DrawingSystem::Args{
				.world = &seen.world,
				.camera = &seen.camera,
				.navigation = navigation,
				.callbacks = {.onToast = [this](const std::string& title, const std::string& message) { toasts.emplace_back(title, message); }}
			});
		}
		void TearDown() override {
			world_sim::UserSettings::Get().drawing = playerSettings;
			assets::ConstructionRegistry::Get().clear();
		}

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
		world_sim::DrawingSettings								   playerSettings;
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

// Over the creek the Rectangle tool's marker reads red before the press, and the press is
// refused: no first corner.
TEST_F(DrawingFordTest, ARectangleCornerInTheCreekIsRefusedOnThePress) {
	drawing->activateRectangleTool();
	const glm::vec2 inCreek = seen.screenOf({6.0F, kCreekY});
	drawing->handleMouseMove(inCreek.x, inCreek.y, kViewportWidth, kViewportHeight, false);
	EXPECT_TRUE(drawing->previewVerdict().markerInvalid);
	const world_sim::DrawingFeedbackInput input = drawing->feedbackInput(
		assets::ConstructionRegistry::Get().rendering().preview, kViewportWidth, kViewportHeight, 1.0F, world_sim::measureFeedbackText
	);
	EXPECT_TRUE(input.snappedPointInvalid);
	const world_sim::DrawingFeedbackPlan plan = world_sim::planSnapFeedback(input, world_sim::measureFeedbackText);
	ASSERT_FALSE(plan.dots.empty());
	EXPECT_EQ(plan.dots.front().fill.r, input.invalidColor.r);
	EXPECT_EQ(plan.dots.front().fill.g, input.invalidColor.g);

	drawing->handlePress(inCreek.x, inCreek.y, kViewportWidth, kViewportHeight, false);
	EXPECT_EQ(drawing->status().pointCount, 0);
	EXPECT_FALSE(drawing->hasPendingPress());
	EXPECT_EQ(lastToast(), "Can't build here: foundation: not on buildable ground");
}

// Both corners on the banks, the rectangle across the creek: its whole footprint is judged, so it
// previews red with the reason and the release commits nothing.
TEST_F(DrawingFordTest, ARectangleAcrossTheCreekPreviewsRedAndDoesNotCommit) {
	drawing->activateRectangleTool();
	const glm::vec2 first = seen.screenOf({4.0F, kCreekY - 3.0F});
	const glm::vec2 second = seen.screenOf({16.0F, kCreekY + 3.0F});
	drawing->handlePress(first.x, first.y, kViewportWidth, kViewportHeight, false);
	drawing->handleClick(first.x, first.y, kViewportWidth, kViewportHeight, false);
	ASSERT_EQ(drawing->status().pointCount, 1) << lastToast();
	drawing->handleMouseMove(second.x, second.y, kViewportWidth, kViewportHeight, false);
	EXPECT_FALSE(drawing->previewVerdict().valid);
	EXPECT_TRUE(drawing->previewVerdict().markerInvalid);
	EXPECT_EQ(drawing->status().message, "foundation: not on buildable ground");
	drawing->handlePress(second.x, second.y, kViewportWidth, kViewportHeight, false);
	drawing->handleClick(second.x, second.y, kViewportWidth, kViewportHeight, false);
	EXPECT_TRUE(drawing->world().foundations().empty());
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
		// The goals it posts outlive the world in the registry singleton.
		void TearDown() override {
			ecs::GoalTaskRegistry::Get().clear();
			DrawingVisibilityTest::TearDown();
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

// At twice the interface size the reason draws twice as large and twice as far off the
// pointer, and the snap labels keep off the box it draws in.
TEST_F(DrawingCloseTest, TheBoxTheLabelsKeepOffIsWhereTheReasonDrawsAtTheUiScale) {
	const construction::FoundationId target = builtFloor({{0.0F, 0.0F}, {8.0F, 0.0F}, {8.0F, 8.0F}, {0.0F, 8.0F}});
	drawing->activateFoundationEditTool(target, construction::FoundationEditMode::Add);
	place({{20.0F, 0.0F}, {24.0F, 0.0F}, {24.0F, 4.0F}});
	hoverFreeform({22.0F, 6.0F});
	ASSERT_FALSE(drawing->previewVerdict().editReason.empty());

	constexpr float kUiScale = 2.0F;
	const auto		measure = [](const std::string& text, float sizePx, world_sim::FeedbackTextWeight /*weight*/) {
		 return Foundation::Vec2{0.5F * sizePx * static_cast<float>(text.size()), 1.25F * sizePx};
	};
	const std::optional<world_sim::DrawingSystem::EditReason> reason = drawing->editReason(kViewportWidth, kViewportHeight, kUiScale, measure);
	ASSERT_TRUE(reason.has_value());
	const glm::vec2 pointer = seen.screenOf({22.0F, 6.0F});
	EXPECT_FLOAT_EQ(reason->position.x, pointer.x + 14.0F * kUiScale);
	EXPECT_FLOAT_EQ(reason->position.y, pointer.y - 22.0F * kUiScale);
	EXPECT_FLOAT_EQ(reason->textScale, 0.8F * kUiScale);

	const world_sim::DrawingFeedbackInput input = drawing->feedbackInput(
		assets::ConstructionRegistry::Get().rendering().preview, kViewportWidth, kViewportHeight, kUiScale, measure
	);
	EXPECT_FLOAT_EQ(input.uiScale, kUiScale);
	ASSERT_TRUE(input.keepClear.has_value());
	const Foundation::Vec2 drawnSize = measure(reason->text, reason->textScale * world_sim::kRendererFontBaseSizePx, world_sim::FeedbackTextWeight::Regular);
	constexpr float		   kShadowPx = 1.0F;
	EXPECT_FLOAT_EQ(input.keepClear->left, reason->position.x);
	EXPECT_FLOAT_EQ(input.keepClear->top, reason->position.y);
	EXPECT_FLOAT_EQ(input.keepClear->right, reason->position.x + drawnSize.x + kShadowPx);
	EXPECT_FLOAT_EQ(input.keepClear->bottom, reason->position.y + drawnSize.y + kShadowPx);
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

// The preview's input carries the scene's accent, the registry's merge distance, Alt as the
// snap had it, and the tool's own close, the near-start rescue included.
TEST_F(DrawingCloseTest, TheFeedbackInputCarriesTheAccentTheMergeDistanceAltAndTheToolsClose) {
	const Foundation::Color accent{0.25F, 0.5F, 0.75F, 1.0F};
	drawing = std::make_unique<world_sim::DrawingSystem>(
		world_sim::DrawingSystem::Args{.world = &seen.world, .camera = &seen.camera, .navigation = nullptr, .accentColor = accent}
	);
	seen.camera.setZoom(kRescueZoom);
	drawing->activateFoundationTool();
	const glm::vec2 firstCorner{0.0F, 0.0F};
	place({firstCorner, {8.0F, 0.0F}, {8.0F, 8.0F}, {0.0F, 8.0F}});
	ASSERT_EQ(drawing->status().pointCount, 4) << lastToast();
	const assets::PreviewStyle& style = assets::ConstructionRegistry::Get().rendering().preview;
	const auto					input = [&] {
		 return drawing->feedbackInput(style, kViewportWidth, kViewportHeight, 1.0F, world_sim::measureFeedbackText);
	};

	hover({4.0F, 4.0F});
	const world_sim::DrawingFeedbackInput open = input();
	EXPECT_EQ(open.accentColor.r, accent.r);
	EXPECT_EQ(open.accentColor.g, accent.g);
	EXPECT_EQ(open.accentColor.b, accent.b);
	EXPECT_EQ(open.accentColor.a, accent.a);
	EXPECT_GT(open.commonLengthMergeMeters, 0.0F);
	EXPECT_EQ(open.commonLengthMergeMeters, assets::ConstructionRegistry::Get().snapping().commonLengthMergeMeters);
	EXPECT_FALSE(open.freeform);
	EXPECT_FALSE(open.clickCloses);

	hoverFreeform(firstCorner + kPastTheCatch);
	ASSERT_TRUE(drawing->previewVerdict().closeHalo) << "the rescue reaches the pointer: " << drawing->status().message;
	const world_sim::DrawingFeedbackInput rescue = input();
	EXPECT_TRUE(rescue.freeform);
	EXPECT_NE(rescue.snap.kind, construction::SnapKind::Origin) << "the snap is off the first point";
	EXPECT_TRUE(rescue.clickCloses);
}

namespace {

	// The Rectangle tool through its input: presses and releases at screen points, as GameScene
	// hands them over. The player's drawing settings are the designed defaults; the grid is 1 m.
	class DrawingRectangleTest : public DrawingCloseTest {
	  protected:
		void SetUp() override {
			DrawingCloseTest::SetUp();
			world_sim::UserSettings::Get().drawing = world_sim::DrawingSettings{.roundLengths = true, .showKeyGuide = true};
			drawing->activateRectangleTool();
		}

		void pressAt(glm::vec2 screen) { drawing->handlePress(screen.x, screen.y, kViewportWidth, kViewportHeight, false); }
		void releaseAt(glm::vec2 screen) { drawing->handleClick(screen.x, screen.y, kViewportWidth, kViewportHeight, false); }
		void press(glm::vec2 meters) { pressAt(seen.screenOf(meters)); }
		void release(glm::vec2 meters) { releaseAt(seen.screenOf(meters)); }
		void clickCorner(glm::vec2 meters) {
			press(meters);
			release(meters);
		}
		void drag(glm::vec2 from, glm::vec2 to) {
			press(from);
			hover(to);
			release(to);
		}
		[[nodiscard]] std::vector<geometry::Vec2i64> onlyFoundationRing() const {
			const auto& foundations = drawing->world().foundations();
			return foundations.size() == 1 ? foundations.front().ring : std::vector<geometry::Vec2i64>{};
		}
		static std::vector<geometry::Vec2i64> ringOf(std::initializer_list<Foundation::Vec2> corners) {
			std::vector<geometry::Vec2i64> ring;
			for (const Foundation::Vec2 corner : corners) {
				ring.push_back(geometry::quantize(corner));
			}
			return ring;
		}
	};

} // namespace

TEST_F(DrawingRectangleTest, TwoClicksCommitTheRectangleOnTheGrid) {
	clickCorner({2.3F, 0.8F});
	EXPECT_EQ(drawing->status().pointCount, 1);
	EXPECT_TRUE(drawing->world().foundations().empty());
	clickCorner({6.4F, 4.2F});
	EXPECT_EQ(onlyFoundationRing(), ringOf({{2.0F, 1.0F}, {6.0F, 1.0F}, {6.0F, 4.0F}, {2.0F, 4.0F}})) << lastToast();
	EXPECT_EQ(drawing->status().pointCount, 0);
	EXPECT_TRUE(drawing->isRectangleTool()) << "the tool stays on for the next one";
}

TEST_F(DrawingRectangleTest, ADragCommitsTheRectangle) {
	drag({2.0F, 1.0F}, {6.0F, 4.0F});
	EXPECT_EQ(onlyFoundationRing(), ringOf({{2.0F, 1.0F}, {6.0F, 1.0F}, {6.0F, 4.0F}, {2.0F, 4.0F}})) << lastToast();
	EXPECT_EQ(drawing->status().pointCount, 0);
}

// The press places the first corner, before any release; within doubleClickDistancePx of its
// press, the release is a click and the corner waits.
TEST_F(DrawingRectangleTest, APressReleasedInPlaceIsTheFirstClick) {
	const float		threshold = assets::ConstructionRegistry::Get().snapping().doubleClickDistancePx;
	const glm::vec2 corner = seen.screenOf({2.0F, 1.0F});
	pressAt(corner);
	EXPECT_EQ(drawing->status().pointCount, 1) << "the corner shows on the press";
	releaseAt(corner + glm::vec2{threshold, 0.0F});
	EXPECT_EQ(drawing->status().pointCount, 1);
	EXPECT_TRUE(drawing->world().foundations().empty());
	EXPECT_TRUE(toasts.empty()) << "a click tries no rectangle: " << lastToast();
	clickCorner({6.0F, 4.0F});
	EXPECT_EQ(onlyFoundationRing(), ringOf({{2.0F, 1.0F}, {6.0F, 1.0F}, {6.0F, 4.0F}, {2.0F, 4.0F}})) << lastToast();
}

// GameScene hands the tool no press that lands on the interface, so the release that follows
// ends no press: it places no corner, and with a corner waiting it commits nothing.
TEST_F(DrawingRectangleTest, AReleaseWhosePressTheToolNeverSawDoesNothing) {
	release({6.0F, 4.0F});
	EXPECT_EQ(drawing->status().pointCount, 0);
	clickCorner({2.0F, 1.0F});
	release({6.0F, 4.0F});
	EXPECT_EQ(drawing->status().pointCount, 1);
	EXPECT_TRUE(drawing->world().foundations().empty());
}

// A drag released over the interface or outside the window commits nothing, and the corner its
// press placed waits for a later click, as a corner placed before the press does.
TEST_F(DrawingRectangleTest, AReleaseOffTheWorldKeepsTheFirstCorner) {
	press({2.0F, 1.0F});
	hover({6.0F, 4.0F});
	drawing->cancelPress();
	EXPECT_EQ(drawing->status().pointCount, 1);
	EXPECT_FALSE(drawing->hasPendingPress());
	EXPECT_TRUE(drawing->world().foundations().empty());
	clickCorner({6.0F, 4.0F});
	EXPECT_EQ(onlyFoundationRing(), ringOf({{2.0F, 1.0F}, {6.0F, 1.0F}, {6.0F, 4.0F}, {2.0F, 4.0F}})) << lastToast();

	clickCorner({10.0F, 1.0F});
	press({14.0F, 4.0F});
	drawing->cancelPress();
	EXPECT_EQ(drawing->status().pointCount, 1);
	EXPECT_EQ(drawing->world().foundations().size(), 1U);
}

// Right after the first click the pointer is on the first corner, and a second corner level with
// it in one axis spans nothing: there is no rectangle, so nothing reads refused, and a release
// there places nothing and says nothing, by click or by a short drag back onto the same grid point.
TEST_F(DrawingRectangleTest, LevelCornersMakeNoRectangleSilently) {
	clickCorner({2.0F, 1.0F});
	for (const glm::vec2 level : {glm::vec2{2.0F, 1.0F}, glm::vec2{6.0F, 1.0F}, glm::vec2{2.0F, 4.0F}}) {
		SCOPED_TRACE(::testing::Message() << level.x << ", " << level.y);
		hover(level);
		EXPECT_TRUE(drawing->status().valid) << drawing->status().message;
		EXPECT_TRUE(drawing->status().message.empty()) << drawing->status().message;
		EXPECT_TRUE(drawing->previewVerdict().valid);
		clickCorner(level);
		EXPECT_EQ(drawing->status().pointCount, 1);
	}
	drawing->activateRectangleTool();
	// Pressed 0.4 m short of the grid point, released 0.625 m on, past the click distance: the
	// same grid point.
	const glm::vec2 corner = seen.screenOf({1.6F, 1.0F});
	pressAt(corner);
	ASSERT_EQ(drawing->status().pointCount, 1);
	releaseAt(corner + glm::vec2{1.25F * assets::ConstructionRegistry::Get().snapping().doubleClickDistancePx, 0.0F});
	EXPECT_EQ(drawing->status().pointCount, 1);
	EXPECT_TRUE(drawing->world().foundations().empty());
	EXPECT_TRUE(toasts.empty()) << lastToast();
}

// Past doubleClickDistancePx, and well inside twice it, a release ends a drag: the rectangle it
// spans, too small here, is tried and refused.
TEST_F(DrawingRectangleTest, AReleaseFourToEightPixelsFromThePressIsADrag) {
	const float		threshold = assets::ConstructionRegistry::Get().snapping().doubleClickDistancePx;
	const glm::vec2 corner = seen.screenOf({2.0F, 1.0F});
	const glm::vec2 released = corner + glm::vec2{threshold, threshold} * 1.1F;
	ASSERT_GT(glm::length(released - corner), threshold);
	ASSERT_LT(glm::length(released - corner), 2.0F * threshold);
	drawing->handlePress(corner.x, corner.y, kViewportWidth, kViewportHeight, true);
	drawing->handleClick(released.x, released.y, kViewportWidth, kViewportHeight, true);
	ASSERT_EQ(toasts.size(), 1U) << "the drag tried its rectangle";
	EXPECT_EQ(toasts.back().first, "Cannot place");
}

// Alt places the first corner at the pointer, off the grid.
TEST_F(DrawingRectangleTest, AltOnTheFirstPressPlacesTheRawPointer) {
	const glm::vec2 raw{2.3F, 0.8F};
	const glm::vec2 corner = seen.screenOf(raw);
	drawing->handlePress(corner.x, corner.y, kViewportWidth, kViewportHeight, true);
	drawing->handleClick(corner.x, corner.y, kViewportWidth, kViewportHeight, true);
	clickCorner({6.0F, 4.0F});
	EXPECT_EQ(onlyFoundationRing(), ringOf({{raw.x, raw.y}, {6.0F, raw.y}, {6.0F, 4.0F}, {raw.x, 4.0F}})) << lastToast();
}

TEST_F(DrawingRectangleTest, OnlyTheRectangleToolTakesAPress) {
	const glm::vec2 corner = seen.screenOf({2.0F, 1.0F});
	drawing->activateFoundationTool();
	EXPECT_FALSE(drawing->handlePress(corner.x, corner.y, kViewportWidth, kViewportHeight, false));
	drawing->activateWallTool();
	EXPECT_FALSE(drawing->handlePress(corner.x, corner.y, kViewportWidth, kViewportHeight, false));
	EXPECT_EQ(drawing->status().pointCount, 0);
	drawing->activateRectangleTool();
	EXPECT_TRUE(drawing->handlePress(corner.x, corner.y, kViewportWidth, kViewportHeight, false));
}

TEST_F(DrawingRectangleTest, EscDropsTheFirstCornerAndASecondEscExits) {
	clickCorner({2.0F, 1.0F});
	ASSERT_EQ(drawing->status().pointCount, 1);
	drawing->cancel();
	EXPECT_EQ(drawing->status().pointCount, 0);
	EXPECT_TRUE(drawing->isRectangleTool());
	drawing->cancel();
	EXPECT_FALSE(drawing->isActive());
	EXPECT_TRUE(drawing->world().foundations().empty());
}

// A right release reaches cancel(), as Esc does (drawingPointerAction). Mid-drag it drops the
// corner the press placed, and the left release that ends the drag commits nothing.
TEST_F(DrawingRectangleTest, CancelMidDragDropsTheCornerAndTheReleaseCommitsNothing) {
	press({2.0F, 1.0F});
	hover({6.0F, 4.0F});
	drawing->cancel();
	EXPECT_EQ(drawing->status().pointCount, 0);
	EXPECT_FALSE(drawing->hasPendingPress());
	EXPECT_TRUE(drawing->isRectangleTool());
	release({6.0F, 4.0F});
	EXPECT_EQ(drawing->status().pointCount, 0);
	EXPECT_TRUE(drawing->world().foundations().empty());
}

// The next click after Backspace places a first corner again, not the second.
TEST_F(DrawingRectangleTest, BackspaceDropsTheFirstCorner) {
	clickCorner({2.0F, 1.0F});
	EXPECT_TRUE(drawing->removeLastPoint());
	EXPECT_EQ(drawing->status().pointCount, 0);
	EXPECT_TRUE(drawing->isRectangleTool());
	clickCorner({6.0F, 4.0F});
	EXPECT_EQ(drawing->status().pointCount, 1);
	EXPECT_TRUE(drawing->world().foundations().empty());
}

// What placed the corner reaches the preview's plan, which draws the cross only for the grid.
TEST_F(DrawingRectangleTest, TheFeedbackKnowsWhatPlacedTheCorner) {
	builtFloor({{20.0F, 0.0F}, {24.0F, 0.0F}, {24.0F, 4.0F}, {20.0F, 4.0F}});
	const assets::PreviewStyle style;
	const auto				   kindAt = [&](glm::vec2 meters, bool freeform) {
		  const glm::vec2 screen = seen.screenOf(meters);
		  drawing->handleMouseMove(screen.x, screen.y, kViewportWidth, kViewportHeight, freeform);
		  return drawing->feedbackInput(style, kViewportWidth, kViewportHeight, 1.0F, world_sim::measureFeedbackText).rectangleSnap;
	};
	EXPECT_EQ(kindAt({6.3F, 0.8F}, false), construction::RectangleSnapKind::Grid);
	EXPECT_EQ(kindAt({20.1F, 0.1F}, false), construction::RectangleSnapKind::Vertex);
	EXPECT_EQ(kindAt({22.3F, -0.1F}, false), construction::RectangleSnapKind::Edge);
	EXPECT_EQ(kindAt({6.3F, 0.8F}, true), construction::RectangleSnapKind::None);
	EXPECT_EQ(kindAt({6.3F, 0.8F}, false), construction::RectangleSnapKind::Grid) << "from the snap memo";
}

namespace {

	// The Build menu's Foundation entry and the strip's tool picker, through a DrawingSystem new
	// to the session.
	class DrawingFoundationPickTest : public DrawingCloseTest {};

} // namespace

TEST_F(DrawingFoundationPickTest, TheFirstFoundationPickThisSessionOpensRectangle) {
	EXPECT_EQ(drawing->lastFoundationTool(), world_sim::FoundationTool::Rectangle);
	drawing->pickFoundationTool(drawing->lastFoundationTool());
	EXPECT_TRUE(drawing->isRectangleTool());
	EXPECT_EQ(drawing->status().foundationTool, world_sim::FoundationTool::Rectangle);
}

TEST_F(DrawingFoundationPickTest, AfterPickingFreeformTheFoundationEntryOpensFreeform) {
	drawing->pickFoundationTool(drawing->lastFoundationTool());
	drawing->pickFoundationTool(world_sim::FoundationTool::Freeform);
	EXPECT_EQ(drawing->status().foundationTool, world_sim::FoundationTool::Freeform);
	drawing->deactivate();
	drawing->activateWallTool();
	drawing->deactivate();
	EXPECT_EQ(drawing->lastFoundationTool(), world_sim::FoundationTool::Freeform);
	drawing->pickFoundationTool(drawing->lastFoundationTool());
	EXPECT_EQ(drawing->status().foundationTool, world_sim::FoundationTool::Freeform);
	EXPECT_FALSE(drawing->isRectangleTool());
	drawing->activateRectangleTool();
	drawing->deactivate();
	EXPECT_EQ(drawing->lastFoundationTool(), world_sim::FoundationTool::Rectangle) << "activating it records the pick too";
}

// Switching tools drops the shape in progress, as the prototype's tool buttons do; picking the
// tool already on keeps it.
TEST_F(DrawingFoundationPickTest, ThePickerSwitchesToolsAndDropsTheShapeInProgress) {
	drawing->pickFoundationTool(world_sim::FoundationTool::Rectangle);
	const glm::vec2 corner = seen.screenOf({2.0F, 1.0F});
	drawing->handlePress(corner.x, corner.y, kViewportWidth, kViewportHeight, false);
	drawing->handleClick(corner.x, corner.y, kViewportWidth, kViewportHeight, false);
	ASSERT_EQ(drawing->status().pointCount, 1);
	drawing->pickFoundationTool(world_sim::FoundationTool::Rectangle);
	EXPECT_EQ(drawing->status().pointCount, 1) << "the same tool keeps its corner";

	drawing->pickFoundationTool(world_sim::FoundationTool::Freeform);
	EXPECT_EQ(drawing->status().foundationTool, world_sim::FoundationTool::Freeform);
	EXPECT_EQ(drawing->status().pointCount, 0);
	place({{0.0F, 0.0F}, {6.0F, 0.0F}});
	ASSERT_EQ(drawing->status().pointCount, 2) << lastToast();
	drawing->pickFoundationTool(world_sim::FoundationTool::Freeform);
	EXPECT_EQ(drawing->status().pointCount, 2);

	drawing->pickFoundationTool(world_sim::FoundationTool::Rectangle);
	EXPECT_TRUE(drawing->isRectangleTool());
	EXPECT_EQ(drawing->status().pointCount, 0);
	EXPECT_TRUE(drawing->world().foundations().empty());
}

// Each refused rectangle previews red with the strip's reason, and its release commits nothing
// and toasts that reason; the corner waits. The preview's verdict is what the release does.
TEST_F(DrawingRectangleTest, ARefusedRectanglePreviewsRedAndDoesNotCommit) {
	builtFloor({{20.0F, 0.0F}, {24.0F, 0.0F}, {24.0F, 4.0F}, {20.0F, 4.0F}});
	const struct {
		const char* what;
		glm::vec2	first;
		glm::vec2	second;
	} refused[] = {
		{"too small", {0.0F, 0.0F}, {1.0F, 1.0F}},
		{"over a building", {18.0F, 1.0F}, {22.0F, 3.0F}},
		{"onto unexplored ground", {30.0F, 0.0F}, {50.0F, 5.0F}},
	};
	for (const auto& rectangle : refused) {
		SCOPED_TRACE(rectangle.what);
		drawing->activateRectangleTool();
		clickCorner(rectangle.first);
		ASSERT_EQ(drawing->status().pointCount, 1) << lastToast();
		hover(rectangle.second);
		EXPECT_FALSE(drawing->previewVerdict().valid);
		EXPECT_FALSE(drawing->previewVerdict().closeHalo);
		EXPECT_FALSE(drawing->status().valid);
		const std::string shown = drawing->status().message;
		EXPECT_FALSE(shown.empty());
		const std::size_t toastsBefore = toasts.size();
		clickCorner(rectangle.second);
		EXPECT_EQ(drawing->world().foundations().size(), 1U);
		EXPECT_EQ(drawing->status().pointCount, 1);
		ASSERT_EQ(toasts.size(), toastsBefore + 1);
		EXPECT_EQ(toasts.back().second, shown);
	}
	drawing->activateRectangleTool();
	clickCorner({0.0F, 0.0F});
	hover({4.0F, 3.0F});
	EXPECT_TRUE(drawing->previewVerdict().valid) << drawing->status().message;
	EXPECT_FALSE(drawing->previewVerdict().closeHalo) << "a rectangle closes onto no first point";
	EXPECT_TRUE(drawing->status().valid);
	clickCorner({4.0F, 3.0F});
	EXPECT_EQ(drawing->world().foundations().size(), 2U) << lastToast();
}

namespace {

	// The strip reads what the preview reads: three corners of a 6 by 3 m rectangle placed and
	// the pointer on the fourth, with every helper off, so nothing closes the shape.
	class DrawingStripVerdictTest : public DrawingCloseTest {
	  protected:
		void SetUp() override {
			DrawingCloseTest::SetUp();
			world_sim::UserSettings::Get().drawing = world_sim::DrawingSettings{};
		}
		void expectTheStripReadsThePreview() {
			const world_sim::DrawingStatus status = drawing->status();
			EXPECT_TRUE(status.valid) << status.message;
			EXPECT_TRUE(status.message.empty()) << status.message;
			EXPECT_EQ(status.valid, drawing->previewVerdict().valid);
		}

		static constexpr glm::vec2 kThreeCorners[] = {{0.0F, 0.0F}, {6.0F, 0.0F}, {6.0F, 3.0F}};
		static constexpr glm::vec2 kFourthCorner{0.0F, 3.0F};
	};

} // namespace

// WOR-973: the three placed points alone make a 26.6 degree corner, but the band to the pointer
// is valid and the click places a point, so the strip reads valid.
TEST_F(DrawingStripVerdictTest, AFreeformStripReadsThePointNotTheOpenShape) {
	drawing->activateFoundationTool();
	place({kThreeCorners[0], kThreeCorners[1], kThreeCorners[2]});
	ASSERT_EQ(drawing->status().pointCount, 3) << lastToast();
	hoverFreeform(kFourthCorner);
	expectTheStripReadsThePreview();
}

// After each click the pointer rests where it placed the point: nothing is proposed yet, so
// nothing reads refused, and a click there places nothing and says nothing. With Round lengths
// on, as a new player has it, the point placed is the pointer's rounded a few centimeters along,
// and the pointer's own spot, that close to it, is still no proposal; with Alt, the point is the
// pointer's.
TEST_F(DrawingStripVerdictTest, AFreeformPointerOnThePointJustPlacedProposesNothing) {
	world_sim::UserSettings::Get().drawing = world_sim::DrawingSettings{.roundLengths = true, .showKeyGuide = true};
	for (const bool freeform : {false, true}) {
		SCOPED_TRACE(::testing::Message() << "Alt " << freeform);
		drawing->activateFoundationTool();
		for (const glm::vec2 corner : {glm::vec2{0.0F, 0.0F}, glm::vec2{6.03F, 0.02F}, glm::vec2{6.04F, 3.03F}}) {
			const glm::vec2 screen = seen.screenOf(corner);
			drawing->handleClick(screen.x, screen.y, kViewportWidth, kViewportHeight, freeform);
			const int placed = drawing->status().pointCount;
			drawing->handleMouseMove(screen.x, screen.y, kViewportWidth, kViewportHeight, freeform);
			expectTheStripReadsThePreview();
			EXPECT_FALSE(drawing->snapshot().verdict.proposed);
			drawing->handleClick(screen.x, screen.y, kViewportWidth, kViewportHeight, freeform);
			EXPECT_EQ(drawing->status().pointCount, placed);
		}
		EXPECT_EQ(drawing->status().pointCount, 3);
		EXPECT_TRUE(toasts.empty()) << lastToast();
	}
}

// The rest silences only a too-close refusal. With angle steps and Round lengths on, a pointer at
// (10, 1) lands the second point at (10, 0) on the 0 degree step; the still pointer then snaps to
// (10, 1), a valid point on the 90 degree step, which the strip reads valid and the click places.
// Alt leaves the pointer where it is, with the same result.
TEST_F(DrawingStripVerdictTest, AFreeformPointerRestingOnAValidSnappedPointProposesItAndTheClickPlacesIt) {
	world_sim::UserSettings::Get().drawing = world_sim::DrawingSettings{.angleSteps = true, .roundLengths = true, .showKeyGuide = true};
	for (const bool freeform : {false, true}) {
		SCOPED_TRACE(::testing::Message() << "Alt " << freeform);
		drawing->activateFoundationTool();
		const glm::vec2 origin{0.0F, 0.0F};
		const glm::vec2 pointer{10.0F, 1.0F};
		ASSERT_TRUE(click(origin));
		ASSERT_TRUE(click(pointer));
		ASSERT_EQ(drawing->status().pointCount, 2) << lastToast();

		const glm::vec2 screen = seen.screenOf(pointer);
		drawing->handleMouseMove(screen.x, screen.y, kViewportWidth, kViewportHeight, freeform);
		expectTheStripReadsThePreview();
		ASSERT_TRUE(drawing->handleClick(screen.x, screen.y, kViewportWidth, kViewportHeight, freeform));
		EXPECT_EQ(drawing->status().pointCount, 3) << lastToast();
		EXPECT_FLOAT_EQ(drawing->status().areaSquareMeters, 5.0F);
	}
}

// The rest belongs to the point just placed, so once that point is removed the same pointer is
// judged afresh and its too-close refusal shows.
TEST_F(DrawingStripVerdictTest, AfterBackspaceTheRestNoLongerHidesATooCloseRefusal) {
	world_sim::UserSettings::Get().drawing = world_sim::DrawingSettings{.roundLengths = true, .showKeyGuide = true};
	drawing->activateFoundationTool();
	ASSERT_TRUE(click({0.0F, 0.0F}));
	ASSERT_TRUE(click({0.45F, 0.0F}));
	ASSERT_EQ(drawing->status().pointCount, 2) << lastToast();
	ASSERT_TRUE(drawing->removeLastPoint());
	hoverFreeform({0.45F, 0.0F});
	EXPECT_FALSE(drawing->status().valid);
	EXPECT_FALSE(drawing->status().message.empty());
}

// Add and Subtract rest as the Freeform tool does: after each click, the pointer still on the point
// just placed proposes nothing, the strip and the reason beside the cursor refuse nothing, and a
// click there places nothing and says nothing. From the third point the outline is the placed
// points' own. A move of a few centimeters brings the too-close refusal back, with its toast.
TEST_F(DrawingStripVerdictTest, AnEditPointerOnThePointJustPlacedProposesNothing) {
	const construction::FoundationId target = blueprintFloor();
	// A triangle across the floor's east edge, its 45 degree tip inside the floor.
	const glm::vec2 corners[] = {{6.0F, 2.0F}, {12.0F, 2.0F}, {12.0F, 8.0F}};
	for (const construction::FoundationEditMode mode :
		 {construction::FoundationEditMode::Add, construction::FoundationEditMode::Subtract}) {
		SCOPED_TRACE(mode == construction::FoundationEditMode::Add ? "Add" : "Subtract");
		toasts.clear();
		drawing->activateFoundationEditTool(target, mode);
		for (const glm::vec2 corner : corners) {
			const glm::vec2 screen = seen.screenOf(corner);
			drawing->handleClick(screen.x, screen.y, kViewportWidth, kViewportHeight, true);
			const int placed = drawing->status().pointCount;
			drawing->handleMouseMove(screen.x, screen.y, kViewportWidth, kViewportHeight, true);
			expectTheStripReadsThePreview();
			EXPECT_TRUE(drawing->previewVerdict().editReason.empty()) << drawing->previewVerdict().editReason;
			EXPECT_FALSE(drawing->snapshot().verdict.proposed);
			drawing->handleClick(screen.x, screen.y, kViewportWidth, kViewportHeight, true);
			EXPECT_EQ(drawing->status().pointCount, placed);
		}
		ASSERT_EQ(drawing->status().pointCount, 3);
		EXPECT_TRUE(toasts.empty()) << lastToast();
		EXPECT_TRUE(drawing->pointerPreview().trialOutline);
		EXPECT_NE(drawing->status().areaDeltaSquareMeters, 0.0F) << "the placed points' outline, valid";

		const glm::vec2 nudged = corners[2] + glm::vec2{0.2F, 0.0F};
		hoverFreeform(nudged);
		EXPECT_FALSE(drawing->status().valid);
		EXPECT_EQ(drawing->status().message, "points too close");
		EXPECT_TRUE(drawing->snapshot().verdict.proposed);
		const glm::vec2 screen = seen.screenOf(nudged);
		drawing->handleClick(screen.x, screen.y, kViewportWidth, kViewportHeight, true);
		EXPECT_EQ(drawing->status().pointCount, 3);
		EXPECT_EQ(lastToast(), "Invalid point: points too close");
	}
}

// The rest silences only the refusal of being on what was just placed. Angle steps turn the pointer
// at 83 degrees from (20, 0) onto 90, so the point lands at (20, 34), inside the seen disc; the still
// pointer then snaps a 4.1 m step east from it, out of the disc, and that refusal shows.
TEST_F(DrawingStripVerdictTest, AFreeformPointerRestingWhereItsSnapIsUnexploredReadsTheRefusal) {
	world_sim::UserSettings::Get().drawing = world_sim::DrawingSettings{.angleSteps = true, .roundLengths = true};
	drawing->activateFoundationTool();
	const glm::vec2 pointer{24.14F, 33.75F};
	ASSERT_TRUE(click({20.0F, 0.0F}));
	ASSERT_TRUE(click(pointer));
	ASSERT_EQ(drawing->status().pointCount, 2) << lastToast();
	ASSERT_TRUE(toasts.empty()) << lastToast();
	hover(pointer);
	EXPECT_FALSE(drawing->status().valid);
	EXPECT_EQ(drawing->status().message, "the area is unexplored");
	EXPECT_TRUE(drawing->snapshot().verdict.proposed);
	ASSERT_TRUE(click(pointer));
	EXPECT_EQ(drawing->status().pointCount, 2);
	EXPECT_TRUE(lastToastSaysUnexplored()) << lastToast();
}

// The rest silences only the refusal of being on what was just placed. The chain runs (22, 25),
// (22, 0), (20, 0), and a pointer 1.7 m east of the line up from (20, 0) places its point on that
// line; the still pointer then snaps east of the point, close beside the first edge, and that
// refusal shows.
TEST_F(DrawingStripVerdictTest, AFreeformPointerRestingWhereItsSnapCrowdsAnEarlierEdgeReadsTheRefusal) {
	world_sim::UserSettings::Get().drawing = world_sim::DrawingSettings{.angleSteps = true, .roundLengths = true};
	drawing->activateFoundationTool();
	const glm::vec2 pointer{21.7F, 20.0F};
	ASSERT_TRUE(click({22.0F, 25.0F}));
	ASSERT_TRUE(click({22.0F, 0.0F}));
	ASSERT_TRUE(click({20.0F, 0.0F}));
	ASSERT_TRUE(click(pointer));
	ASSERT_EQ(drawing->status().pointCount, 4) << lastToast();
	ASSERT_TRUE(toasts.empty()) << lastToast();
	hover(pointer);
	EXPECT_FALSE(drawing->status().valid);
	EXPECT_EQ(drawing->status().message, "edges too close");
	EXPECT_TRUE(drawing->snapshot().verdict.proposed);
	ASSERT_TRUE(click(pointer));
	EXPECT_EQ(drawing->status().pointCount, 4);
	EXPECT_EQ(lastToast(), "Invalid point: " + drawing->status().message);
}

// An edit's cursor joins the outline once it is proposed: two points and the pointer on a third.
TEST_F(DrawingStripVerdictTest, AnEditPointerOffItsTwoPointsDrawsTheTrialOutline) {
	const construction::FoundationId target = blueprintFloor();
	drawing->activateFoundationEditTool(target, construction::FoundationEditMode::Add);
	place({{6.0F, 2.0F}, {12.0F, 2.0F}});
	ASSERT_EQ(drawing->status().pointCount, 2) << lastToast();
	hoverFreeform({12.0F, 8.0F});
	EXPECT_TRUE(drawing->pointerPreview().trialOutline);
}

// The rest ends for good when the pointer leaves the point: back on it, the refusal shows.
TEST_F(DrawingStripVerdictTest, APointerThatLeavesThePointJustPlacedAndComesBackReadsTheRefusal) {
	drawing->activateFoundationTool();
	place({kThreeCorners[0], kThreeCorners[1]});
	ASSERT_EQ(drawing->status().pointCount, 2) << lastToast();
	hoverFreeform(kThreeCorners[2]);
	hoverFreeform(kThreeCorners[1]);
	EXPECT_FALSE(drawing->status().valid);
	EXPECT_EQ(drawing->status().message, "points too close");
}

TEST_F(DrawingStripVerdictTest, AWallStripReadsThePreview) {
	builtFloor({{-10.0F, -10.0F}, {10.0F, -10.0F}, {10.0F, 10.0F}, {-10.0F, 10.0F}});
	drawing->activateWallTool();
	place({kThreeCorners[0], kThreeCorners[1], kThreeCorners[2]});
	ASSERT_EQ(drawing->status().pointCount, 3) << lastToast();
	hoverFreeform(kFourthCorner);
	expectTheStripReadsThePreview();
}

TEST_F(DrawingStripVerdictTest, AnEditStripReadsThePreview) {
	const construction::FoundationId target = blueprintFloor();
	drawing->activateFoundationEditTool(target, construction::FoundationEditMode::Add);
	const glm::vec2 across{6.0F, 2.0F};
	place({kThreeCorners[0] + across, kThreeCorners[1] + across, kThreeCorners[2] + across});
	ASSERT_EQ(drawing->status().pointCount, 3) << lastToast();
	hoverFreeform(kFourthCorner + across);
	expectTheStripReadsThePreview();
}

namespace {

	// The pointer leaves the tool (GameScene pauses it) and comes back (the next move resumes it).
	// The Rectangle tool's input and the designed default settings, with a wall host 40 by 30 m.
	class DrawingPauseTest : public DrawingRectangleTest {
	  protected:
		void hostWalls() { builtFloor({{-20.0F, -15.0F}, {20.0F, -15.0F}, {20.0F, 15.0F}, {-20.0F, 15.0F}}); }

		// A left release on the world as GameScene hands it to the tool on: the wall double-click
		// decides whether it is a click or the end of the chain.
		void releaseWithTool(glm::vec2 meters, double timeSeconds) {
			const glm::vec2	   screen = seen.screenOf(meters);
			const auto&		   snapping = assets::ConstructionRegistry::Get().snapping();
			const std::optional<world_sim::ToolKind> tool = drawing->activeTool();
			ASSERT_TRUE(tool.has_value());
			const world_sim::ReleaseAction action = doubleClick.release(
				{.timeSeconds = timeSeconds, .position = screen},
				*tool,
				{.seconds = snapping.doubleClickSeconds, .pixels = snapping.doubleClickDistancePx}
			);
			if (action == world_sim::ReleaseAction::EndChain) {
				drawing->finishChain();
			} else {
				releaseAt(screen);
			}
		}

		// Nothing the pointer proposed shows: no halo, no red marker, no edit reason, nothing drawn
		// that follows the pointer, and the strip refuses nothing, so it shows the tool's hint.
		void expectNothingProposed() {
			const world_sim::DrawingSystem::PreviewVerdict verdict = drawing->previewVerdict();
			EXPECT_TRUE(verdict.valid);
			EXPECT_FALSE(verdict.closeHalo);
			EXPECT_FALSE(verdict.markerInvalid);
			EXPECT_TRUE(verdict.editReason.empty()) << verdict.editReason;
			const world_sim::DrawingSystem::PointerPreview preview = drawing->pointerPreview();
			EXPECT_FALSE(preview.snapFeedback);
			EXPECT_FALSE(preview.trialOutline);
			EXPECT_FALSE(preview.openingGhost);
			EXPECT_TRUE(drawing->status().message.empty()) << drawing->status().message;
		}

		// Everything the strip and the preview read, to compare a frame before a pause with one after.
		[[nodiscard]] std::string proposal() const {
			const world_sim::DrawingStatus							 status = drawing->status();
			const world_sim::DrawingSystem::PreviewVerdict verdict = drawing->previewVerdict();
			const world_sim::DrawingSystem::PointerPreview preview = drawing->pointerPreview();
			std::ostringstream										 out;
			out << "strip " << status.valid << " '" << status.message << "' points " << status.pointCount << " area "
				<< status.areaSquareMeters << " delta " << status.areaDeltaSquareMeters << " segment " << status.segmentLengthMeters
				<< " total " << status.totalLengthMeters << "; preview " << verdict.valid << verdict.closeHalo << verdict.markerInvalid
				<< verdict.editOutlineValid << " '" << verdict.editReason << "' drawn " << preview.snapFeedback << preview.trialOutline
				<< preview.openingGhost;
			return out.str();
		}

		world_sim::WallDoubleClick doubleClick;
	};

} // namespace

// Each tool with the pointer on a refused spot, then paused: the refusal goes with the pointer, and
// so do the snap feedback, an edit's trial outline, and the opening ghost.
TEST_F(DrawingPauseTest, APausedToolProposesNothing) {
	{
		SCOPED_TRACE("Add");
		const construction::FoundationId target = blueprintFloor();
		drawing->activateFoundationEditTool(target, construction::FoundationEditMode::Add);
		place({{6.0F, 2.0F}, {12.0F, 2.0F}, {12.0F, 5.0F}});
		hoverFreeform({7.0F, 6.0F});
		ASSERT_TRUE(drawing->pointerPreview().trialOutline) << lastToast();
		click({12.0F, 5.0F});
		hover({12.0F, 5.0F});
		ASSERT_FALSE(drawing->status().valid);
		drawing->pause();
		expectNothingProposed();
		EXPECT_TRUE(drawing->status().valid);
	}
	{
		SCOPED_TRACE("Freeform");
		drawing->activateFoundationTool();
		click(kSeenPoint);
		hover(kUnseenPoint);
		ASSERT_FALSE(drawing->status().valid);
		ASSERT_TRUE(drawing->pointerPreview().snapFeedback);
		drawing->pause();
		expectNothingProposed();
		EXPECT_TRUE(drawing->status().valid);
	}
	{
		SCOPED_TRACE("Rectangle");
		drawing->activateRectangleTool();
		clickCorner({0.0F, 0.0F});
		hover({1.0F, 1.0F});
		ASSERT_FALSE(drawing->status().valid);
		drawing->pause();
		expectNothingProposed();
		EXPECT_TRUE(drawing->status().valid);
	}
	{
		SCOPED_TRACE("Wall");
		buildAcrossTheEdge();
		drawing->activateWallTool();
		click({36.0F, 4.0F});
		hover({50.0F, 4.0F});
		ASSERT_FALSE(drawing->status().valid);
		drawing->pause();
		expectNothingProposed();
		EXPECT_TRUE(drawing->status().valid);
		EXPECT_FLOAT_EQ(drawing->status().segmentLengthMeters, 0.0F);
	}
	{
		SCOPED_TRACE("Door");
		drawing->activateOpeningTool("Door");
		hover({55.0F, 0.0F});
		ASSERT_FALSE(drawing->status().message.empty());
		ASSERT_TRUE(drawing->pointerPreview().openingGhost);
		drawing->pause();
		expectNothingProposed();
		EXPECT_FALSE(drawing->status().valid) << "the strip's hint to hover a wall, not a refusal";
	}
}

// Paused with the pointer on a closing spot, the close's halo goes, and paused on a refused one,
// so does its red marker.
TEST_F(DrawingPauseTest, APauseOnAClosingSpotLeavesNoHaloAndNoRedMarker) {
	{
		SCOPED_TRACE("the close halo");
		drawing->activateFoundationTool();
		place({{0.0F, 0.0F}, {8.0F, 0.0F}, {8.0F, 8.0F}});
		hover({0.0F, 0.0F});
		ASSERT_TRUE(drawing->previewVerdict().closeHalo) << lastToast();
		drawing->pause();
		expectNothingProposed();
	}
	{
		SCOPED_TRACE("a refused close");
		const construction::FoundationId target = builtFloor({{0.0F, 20.0F}, {8.0F, 20.0F}, {8.0F, 28.0F}, {0.0F, 28.0F}});
		drawing->activateFoundationEditTool(target, construction::FoundationEditMode::Add);
		place({{20.0F, 20.0F}, {24.0F, 20.0F}, {24.0F, 24.0F}});
		hover({20.0F, 20.0F});
		ASSERT_TRUE(drawing->previewVerdict().markerInvalid) << lastToast();
		drawing->pause();
		expectNothingProposed();
	}
}

// A tool starts paused: switched to while the last tool's pointer was on a refused spot, before
// any move it proposes nothing, and the strip shows its hint.
TEST_F(DrawingPauseTest, AToolStartsPausedUntilItsFirstMove) {
	const construction::FoundationId target = blueprintFloor();
	buildAcrossTheEdge();
	drawing->activateFoundationEditTool(target, construction::FoundationEditMode::Add);
	place({{6.0F, 2.0F}, {12.0F, 2.0F}, {12.0F, 5.0F}});
	hoverFreeform({12.2F, 5.0F});
	ASSERT_FALSE(drawing->status().valid);
	ASSERT_TRUE(drawing->pointerPreview().trialOutline);
	{
		SCOPED_TRACE("Door, after Add");
		drawing->activateOpeningTool("Door");
		expectNothingProposed();
		EXPECT_FALSE(drawing->status().valid) << "the strip's hint to hover a wall, not a refusal";
	}
	hover({55.0F, 0.0F});
	ASSERT_TRUE(drawing->pointerPreview().openingGhost);
	{
		SCOPED_TRACE("Freeform, after Door");
		drawing->activateFoundationTool();
		expectNothingProposed();
		EXPECT_TRUE(drawing->status().valid);
	}
	click(kSeenPoint);
	hover(kUnseenPoint);
	ASSERT_FALSE(drawing->status().valid);
	{
		SCOPED_TRACE("Wall, after Freeform");
		drawing->activateWallTool();
		expectNothingProposed();
		EXPECT_TRUE(drawing->status().valid);
	}
	click({36.0F, 4.0F});
	hover({50.0F, 4.0F});
	ASSERT_FALSE(drawing->status().valid);
	{
		SCOPED_TRACE("Rectangle, after Wall");
		drawing->activateRectangleTool();
		expectNothingProposed();
		EXPECT_TRUE(drawing->status().valid);
	}
	clickCorner({0.0F, 0.0F});
	hover({1.0F, 1.0F});
	ASSERT_FALSE(drawing->status().valid);
	{
		SCOPED_TRACE("Add, after Rectangle");
		drawing->activateFoundationEditTool(target, construction::FoundationEditMode::Add);
		expectNothingProposed();
		EXPECT_TRUE(drawing->status().valid);
	}
}

// The chain keeps its points through a pause, and the next move snaps from where the pointer is.
TEST_F(DrawingPauseTest, PlacedPointsSurviveAPauseAndTheToolResumesAtThePointer) {
	hostWalls();
	drawing->activateWallTool();
	click({-5.0F, 0.0F});
	click({0.0F, 0.0F});
	hover({0.0F, 3.0F});
	ASSERT_NEAR(drawing->status().segmentLengthMeters, 3.0F, 1e-3F) << lastToast();
	drawing->pause();
	EXPECT_EQ(drawing->status().pointCount, 2);
	EXPECT_FLOAT_EQ(drawing->status().segmentLengthMeters, 0.0F);
	EXPECT_NEAR(drawing->status().totalLengthMeters, 5.0F, 1e-3F);
	hover({0.0F, 4.0F});
	EXPECT_NEAR(drawing->status().segmentLengthMeters, 4.0F, 1e-3F);
	click({0.0F, 4.0F});
	EXPECT_EQ(drawing->status().pointCount, 3) << lastToast();
}

// The Rectangle tool's first corner waits through a pause; the pending rectangle goes with the
// pointer and comes back with it, the same snap the memo held.
TEST_F(DrawingPauseTest, TheRectanglesWaitingCornerSurvivesAPause) {
	clickCorner({2.0F, 1.0F});
	hover({6.0F, 4.0F});
	ASSERT_FLOAT_EQ(drawing->status().areaSquareMeters, 12.0F);
	drawing->pause();
	EXPECT_EQ(drawing->status().pointCount, 1);
	EXPECT_FLOAT_EQ(drawing->status().areaSquareMeters, 0.0F);
	hover({6.0F, 4.0F});
	EXPECT_FLOAT_EQ(drawing->status().areaSquareMeters, 12.0F);
	clickCorner({6.0F, 4.0F});
	EXPECT_EQ(onlyFoundationRing(), ringOf({{2.0F, 1.0F}, {6.0F, 1.0F}, {6.0F, 4.0F}, {2.0F, 4.0F}})) << lastToast();
}

// A press-drag paused before its release keeps the press: the release, back at the same point,
// commits the rectangle it drags out.
TEST_F(DrawingPauseTest, APendingRectanglePressSurvivesAPause) {
	press({2.0F, 1.0F});
	hover({6.0F, 4.0F});
	ASSERT_TRUE(drawing->hasPendingPress());
	drawing->pause();
	EXPECT_TRUE(drawing->hasPendingPress());
	EXPECT_EQ(drawing->status().pointCount, 1);
	hover({6.0F, 4.0F});
	release({6.0F, 4.0F});
	EXPECT_EQ(onlyFoundationRing(), ringOf({{2.0F, 1.0F}, {6.0F, 1.0F}, {6.0F, 4.0F}, {2.0F, 4.0F}})) << lastToast();
}

// Back at the same pointer after a pause, each tool proposes what it proposed before it.
TEST_F(DrawingPauseTest, ResumingAtTheSamePointerRestoresTheProposal) {
	buildAcrossTheEdge();
	builtFloor({{-30.0F, -30.0F}, {-14.0F, -30.0F}, {-14.0F, -14.0F}, {-30.0F, -14.0F}});
	const construction::FoundationId target = builtFloor({{0.0F, 20.0F}, {8.0F, 20.0F}, {8.0F, 28.0F}, {0.0F, 28.0F}});
	struct Case {
		const char*			  name;
		std::function<void()> start; // leaves the pointer where the round trip resumes
		glm::vec2			  pointer;
		bool				  freeform = false;
	};
	const std::vector<Case> cases{
		{.name = "Freeform on its close halo",
		 .start =
			 [this] {
				 drawing->activateFoundationTool();
				 place({{-10.0F, -10.0F}, {-2.0F, -10.0F}, {-2.0F, -2.0F}});
			 },
		 .pointer = {-10.0F, -10.0F}},
		{.name = "Freeform on unseen ground",
		 .start =
			 [this] {
				 drawing->activateFoundationTool();
				 click(kSeenPoint);
			 },
		 .pointer = kUnseenPoint},
		{.name = "Wall",
		 .start =
			 [this] {
				 drawing->activateWallTool();
				 click({-26.0F, -22.0F});
				 click({-21.0F, -22.0F});
			 },
		 .pointer = {-21.0F, -19.0F}},
		{.name = "Add",
		 .start =
			 [this, target] {
				 drawing->activateFoundationEditTool(target, construction::FoundationEditMode::Add);
				 place({{8.0F, 20.0F}, {12.0F, 20.0F}, {12.0F, 28.0F}});
			 },
		 .pointer = {9.0F, 30.0F},
		 .freeform = true},
		{.name = "Door",
		 .start = [this] { drawing->activateOpeningTool("Door"); },
		 .pointer = {55.0F, 0.0F}},
		{.name = "Rectangle",
		 .start =
			 [this] {
				 drawing->activateRectangleTool();
				 clickCorner({-12.0F, 4.0F});
			 },
		 .pointer = {-8.0F, 7.0F}},
	};
	for (const Case& round : cases) {
		SCOPED_TRACE(round.name);
		round.start();
		const glm::vec2 screen = seen.screenOf(round.pointer);
		drawing->handleMouseMove(screen.x, screen.y, kViewportWidth, kViewportHeight, round.freeform);
		const std::string before = proposal();
		drawing->pause();
		ASSERT_NE(proposal(), before);
		drawing->handleMouseMove(screen.x, screen.y, kViewportWidth, kViewportHeight, round.freeform);
		EXPECT_EQ(proposal(), before);
	}
}

// A double-click's first click places the last point and its second ends the chain, as Enter does:
// the chain commits, and the second click, on the point just placed, places nothing and is not
// refused as a wall too short.
TEST_F(DrawingPauseTest, ADoubleClickEndsTheChainAsEnterDoes) {
	hostWalls();
	drawing->activateWallTool();
	releaseWithTool({-8.0F, -8.0F}, 1.0);
	releaseWithTool({8.0F, -8.0F}, 2.0);
	releaseWithTool({8.0F, 8.0F}, 3.0);
	releaseWithTool({8.0F, 8.0F}, 3.25);
	EXPECT_EQ(drawing->status().pointCount, 0);
	EXPECT_EQ(drawing->world().segments().size(), 2U) << lastToast();
	ASSERT_EQ(toasts.size(), 1U) << lastToast();
	EXPECT_EQ(toasts.back().first, "Wall placed");
	EXPECT_TRUE(drawing->isWallTool() && drawing->isActive()) << "the tool stays on for the next chain";
}

// After each chain point, the first included, the pointer still on it proposes nothing: the band
// is not red, the strip shows the chain's hint, and a slow second click there places nothing and
// says nothing. A move a few centimeters off (under the shortest wall) brings "wall too short"
// back, with its toast.
TEST_F(DrawingPauseTest, AWallPointerOnThePointJustPlacedProposesNothing) {
	hostWalls();
	drawing->activateWallTool();
	for (const glm::vec2 corner : {glm::vec2{-8.0F, -8.0F}, glm::vec2{8.0F, -8.0F}, glm::vec2{8.0F, 8.0F}}) {
		SCOPED_TRACE(::testing::Message() << "at " << corner.x << ", " << corner.y);
		click(corner);
		const int placed = drawing->status().pointCount;
		hover(corner);
		const world_sim::DrawingStatus status = drawing->status();
		EXPECT_TRUE(status.valid) << status.message;
		EXPECT_TRUE(status.message.empty()) << status.message;
		EXPECT_EQ(world_sim::statusHint(status).text, "Right-click / Enter to finish");
		EXPECT_TRUE(drawing->previewVerdict().valid);
		EXPECT_FALSE(drawing->snapshot().verdict.proposed);
		click(corner);
		EXPECT_EQ(drawing->status().pointCount, placed);
	}
	ASSERT_EQ(drawing->status().pointCount, 3);
	EXPECT_TRUE(toasts.empty()) << lastToast();

	const glm::vec2 nudged{8.0F, 8.2F};
	const glm::vec2 screen = seen.screenOf(nudged);
	drawing->handleMouseMove(screen.x, screen.y, kViewportWidth, kViewportHeight, true);
	EXPECT_FALSE(drawing->status().valid);
	EXPECT_EQ(drawing->status().message, "wall too short");
	EXPECT_FALSE(drawing->previewVerdict().valid);
	drawing->handleClick(screen.x, screen.y, kViewportWidth, kViewportHeight, true);
	EXPECT_EQ(drawing->status().pointCount, 3);
	EXPECT_EQ(lastToast(), "Invalid point: wall too short");
}

// The camera panning under a still mouse moves the pointer in the world. Under half a screen pixel
// in all, at any zoom, the pointer still rests and a click there says nothing; a pixel ends the rest.
TEST_F(DrawingPauseTest, ACameraPanUnderAStillPointerEndsTheRest) {
	hostWalls();
	for (const float zoom : {1.0F, 3.0F}) {
		SCOPED_TRACE(::testing::Message() << "zoom " << zoom);
		toasts.clear();
		seen.camera.setZoom(zoom);
		seen.camera.setPosition({0.0F, 0.0F});
		drawing->activateWallTool();
		click({-8.0F, -8.0F});
		click({8.0F, -8.0F});
		const glm::vec2 screen = seen.screenOf({8.0F, -8.0F});
		hover({8.0F, -8.0F});
		ASSERT_TRUE(drawing->status().valid) << drawing->status().message;

		const float pixelMeters = 1.0F / (world_sim::testing::kPixelsPerMeter * zoom);
		for (int tenths = 1; tenths <= 4; ++tenths) {
			seen.camera.setPosition({0.0F, static_cast<float>(tenths) * 0.1F * pixelMeters});
			drawing->handleMouseMove(screen.x, screen.y, kViewportWidth, kViewportHeight, true);
			EXPECT_TRUE(drawing->status().valid) << tenths << " tenths: " << drawing->status().message;
			EXPECT_FALSE(drawing->snapshot().verdict.proposed) << tenths << " tenths";
		}
		drawing->handleClick(screen.x, screen.y, kViewportWidth, kViewportHeight, true);
		EXPECT_EQ(drawing->status().pointCount, 2);
		EXPECT_TRUE(toasts.empty()) << lastToast();

		seen.camera.setPosition({0.0F, pixelMeters});
		drawing->handleMouseMove(screen.x, screen.y, kViewportWidth, kViewportHeight, true);
		EXPECT_FALSE(drawing->status().valid);
		EXPECT_EQ(drawing->status().message, "wall too short");
		EXPECT_TRUE(drawing->snapshot().verdict.proposed);
		drawing->deactivate();
	}
}

// The rest is measured from where the pointer was at the click, not from the last frame: a camera
// creeping 0.2 px a frame stays at rest for the first two frames (0.4 px in all) and has ended it by
// the third (0.6 px), though no single step is half a pixel.
TEST_F(DrawingPauseTest, ACameraCreepingInSmallStepsEndsTheRestOnceTheTotalPassesHalfAPixel) {
	hostWalls();
	drawing->activateWallTool();
	click({-8.0F, -8.0F});
	click({8.0F, -8.0F});
	const glm::vec2 screen = seen.screenOf({8.0F, -8.0F});
	hover({8.0F, -8.0F});
	ASSERT_TRUE(drawing->status().valid) << drawing->status().message;

	const float pixelMeters = 1.0F / (world_sim::testing::kPixelsPerMeter * seen.camera.zoom());
	for (int nudge = 1; nudge <= 6; ++nudge) {
		SCOPED_TRACE(::testing::Message() << nudge << " nudges, " << 0.2F * static_cast<float>(nudge) << " px in all");
		seen.camera.setPosition({0.0F, static_cast<float>(nudge) * 0.2F * pixelMeters});
		drawing->handleMouseMove(screen.x, screen.y, kViewportWidth, kViewportHeight, true);
		const bool resting = !drawing->snapshot().verdict.proposed;
		EXPECT_EQ(resting, nudge <= 2);
		EXPECT_EQ(drawing->status().valid, nudge <= 2) << drawing->status().message;
	}
	EXPECT_EQ(drawing->status().message, "wall too short");
}

// A keyboard pan eases in, so the camera is still creeping a fraction of a pixel a frame after it
// looks still. A click in that tail rests, and goes on resting while the camera settles.
TEST_F(DrawingPauseTest, AClickWhileTheCameraSettlesFromAPanRests) {
	hostWalls();
	drawing->activateWallTool();
	click({-8.0F, -8.0F});

	constexpr float kFrameSeconds = 1.0F / 60.0F;
	const float		pixelMeters = 1.0F / (world_sim::testing::kPixelsPerMeter * seen.camera.zoom());
	const float		targetX = seen.camera.position().x + seen.camera.panSpeed() / seen.camera.zoom() * kFrameSeconds;
	seen.camera.move(1.0F, 0.0F, kFrameSeconds);
	int frames = 0;
	while ((targetX - seen.camera.position().x) / pixelMeters >= 0.5F) {
		seen.camera.update(kFrameSeconds);
		ASSERT_LT(++frames, 600);
	}
	ASSERT_GT(seen.camera.position().x, 1.0F) << "the pan moved the world";
	ASSERT_GT(targetX - seen.camera.position().x, 0.0F) << "the camera is still settling";

	click({8.0F, -8.0F});
	ASSERT_EQ(drawing->status().pointCount, 2) << lastToast();
	const glm::vec2 screen = seen.screenOf({8.0F, -8.0F});
	float			settled = std::numeric_limits<float>::quiet_NaN();
	for (int frame = 0; frame < 600 && seen.camera.position().x != settled; ++frame) {
		settled = seen.camera.position().x;
		seen.camera.update(kFrameSeconds);
		drawing->handleMouseMove(screen.x, screen.y, kViewportWidth, kViewportHeight, false);
		EXPECT_TRUE(drawing->status().valid) << "frame " << frame << ": " << drawing->status().message;
		EXPECT_FALSE(drawing->snapshot().verdict.proposed) << "frame " << frame;
	}
	drawing->handleClick(screen.x, screen.y, kViewportWidth, kViewportHeight, false);
	EXPECT_EQ(drawing->status().pointCount, 2);
	EXPECT_TRUE(toasts.empty()) << lastToast();
}

// A double-click as the first action places the point and drops it: no chain, no wall, no toast.
TEST_F(DrawingPauseTest, ADoubleClickAsTheFirstActionLeavesNothing) {
	hostWalls();
	drawing->activateWallTool();
	releaseWithTool({-8.0F, -8.0F}, 1.0);
	ASSERT_EQ(drawing->status().pointCount, 1) << lastToast();
	releaseWithTool({-8.0F, -8.0F}, 1.25);
	EXPECT_EQ(drawing->status().pointCount, 0);
	EXPECT_TRUE(drawing->world().segments().empty());
	EXPECT_TRUE(toasts.empty()) << lastToast();
}

// A first click on the chain's first point closes and commits the loop; the second click finds no
// chain in progress and places nothing, so no new chain starts there.
TEST_F(DrawingPauseTest, ADoubleClickOnTheFirstPointClosesOnItsFirstClick) {
	hostWalls();
	drawing->activateWallTool();
	double time = 1.0;
	for (const glm::vec2 corner : {glm::vec2{-8.0F, -8.0F}, glm::vec2{8.0F, -8.0F}, glm::vec2{8.0F, 8.0F}, glm::vec2{-8.0F, 8.0F}}) {
		releaseWithTool(corner, time);
		time += 1.0;
	}
	releaseWithTool({-8.0F, -8.0F}, time);
	ASSERT_EQ(drawing->world().segments().size(), 4U) << lastToast();
	const std::size_t toastsAfterClose = toasts.size();
	releaseWithTool({-8.0F, -8.0F}, time + 0.25);
	EXPECT_EQ(drawing->status().pointCount, 0);
	EXPECT_EQ(drawing->world().segments().size(), 4U);
	EXPECT_EQ(toasts.size(), toastsAfterClose) << lastToast();
}

// A first click the tool refuses places nothing; the second still ends the chain, which commits
// without it, as the prototype's double-click commits the chain it has.
TEST_F(DrawingPauseTest, ADoubleClickOnARefusedPointCommitsTheChainWithoutIt) {
	hostWalls();
	drawing->activateWallTool();
	releaseWithTool({-8.0F, 0.0F}, 1.0);
	releaseWithTool({0.0F, 0.0F}, 2.0);
	releaseWithTool({30.0F, 0.0F}, 3.0);
	ASSERT_EQ(drawing->status().pointCount, 2);
	ASSERT_EQ(toasts.back().first, "Off host foundation");
	releaseWithTool({30.0F, 0.0F}, 3.25);
	EXPECT_EQ(drawing->status().pointCount, 0);
	EXPECT_EQ(drawing->world().segments().size(), 1U);
	EXPECT_EQ(toasts.back().first, "Wall placed");
}

// Every way GameScene ends a chain or breaks the pointer's continuity forgets the first click, so a
// quick click on the same spot after it is a click: Backspace, Enter, Esc, a tool switch, a pause.
TEST_F(DrawingPauseTest, WhatEndsAChainForgetsTheFirstClick) {
	hostWalls();
	const glm::vec2 first{-8.0F, -8.0F};
	const glm::vec2 second{8.0F, -8.0F};
	const glm::vec2 third{8.0F, 8.0F};
	{
		SCOPED_TRACE("Backspace, then the point again");
		drawing->activateWallTool();
		releaseWithTool(first, 1.0);
		releaseWithTool(second, 2.0);
		releaseWithTool(third, 3.0);
		doubleClick.forget();
		ASSERT_TRUE(drawing->removeLastPoint());
		releaseWithTool(third, 3.25);
		EXPECT_EQ(drawing->status().pointCount, 3) << lastToast();
		EXPECT_TRUE(drawing->world().segments().empty());
		drawing->deactivate();
		doubleClick.forget();
	}
	{
		SCOPED_TRACE("Enter, then a new chain where it ended");
		drawing->activateWallTool();
		releaseWithTool({-8.0F, 4.0F}, 1.0);
		releaseWithTool({8.0F, 4.0F}, 2.0);
		doubleClick.forget();
		drawing->finishChain();
		const std::size_t walls = drawing->world().segments().size();
		releaseWithTool({8.0F, 4.0F}, 2.25);
		EXPECT_EQ(drawing->status().pointCount, 1) << lastToast();
		EXPECT_EQ(drawing->world().segments().size(), walls);
		drawing->deactivate();
		doubleClick.forget();
	}
	{
		SCOPED_TRACE("Esc, then a new chain where it ended");
		drawing->activateWallTool();
		releaseWithTool({-8.0F, 10.0F}, 1.0);
		releaseWithTool({8.0F, 10.0F}, 2.0);
		doubleClick.forget();
		ASSERT_TRUE(drawing->cancel());
		releaseWithTool({8.0F, 10.0F}, 2.25);
		EXPECT_EQ(drawing->status().pointCount, 1) << lastToast();
		drawing->deactivate();
		doubleClick.forget();
	}
	{
		SCOPED_TRACE("a tool switch and back, then the same spot");
		drawing->activateWallTool();
		releaseWithTool({-12.0F, 0.0F}, 1.0);
		drawing->activateFoundationTool();
		doubleClick.forget();
		drawing->activateWallTool();
		doubleClick.forget();
		releaseWithTool({-12.0F, 0.0F}, 1.25);
		EXPECT_EQ(drawing->status().pointCount, 1) << lastToast();
		drawing->deactivate();
		doubleClick.forget();
	}
	{
		SCOPED_TRACE("a pause, then the point just placed");
		drawing->activateWallTool();
		releaseWithTool({-12.0F, 2.0F}, 1.0);
		releaseWithTool({-4.0F, 2.0F}, 2.0);
		doubleClick.forget();
		drawing->pause();
		const std::size_t walls = drawing->world().segments().size();
		const std::size_t toastsBefore = toasts.size();
		releaseWithTool({-4.0F, 2.0F}, 2.25);
		EXPECT_EQ(drawing->status().pointCount, 2) << "the pointer rests on the point: nothing placed, the chain still in progress";
		EXPECT_EQ(drawing->world().segments().size(), walls);
		EXPECT_EQ(toasts.size(), toastsBefore) << lastToast();
	}
}

namespace {

	// The dev state view's snapshot: what the tool holds, read from the state the strip and the
	// preview read.
	class DrawingSnapshotTest : public DrawingRectangleTest {
	  protected:
		static void expectPoint(Foundation::Vec2 actual, Foundation::Vec2 expected) {
			EXPECT_FLOAT_EQ(actual.x, expected.x);
			EXPECT_FLOAT_EQ(actual.y, expected.y);
		}
	};

} // namespace

TEST_F(DrawingSnapshotTest, NoToolHoldsNothingButTheLastFoundationPick) {
	drawing->deactivate();
	const world_sim::DrawingSnapshot shot = drawing->snapshot();
	EXPECT_FALSE(shot.tool.has_value());
	EXPECT_EQ(shot.lastFoundationTool, world_sim::FoundationTool::Rectangle);
	EXPECT_FALSE(shot.followingPointer);
	EXPECT_TRUE(shot.points.empty());
	EXPECT_FALSE(shot.snap.has_value());
	EXPECT_FALSE(shot.status.active);
	EXPECT_FALSE(shot.verdict.proposed);
}

TEST_F(DrawingSnapshotTest, FreeformWithTwoPointsAndThePointerOnAThirdReportsTheProposal) {
	drawing->activateFoundationTool();
	place({{0.0F, 0.0F}, {4.0F, 0.0F}});
	hoverFreeform({4.0F, -4.0F});
	const world_sim::DrawingSnapshot shot = drawing->snapshot();
	EXPECT_EQ(shot.tool, world_sim::ToolKind::Foundation);
	EXPECT_EQ(shot.lastFoundationTool, world_sim::FoundationTool::Freeform);
	EXPECT_TRUE(shot.followingPointer);
	ASSERT_EQ(shot.points.size(), 2U);
	expectPoint(shot.points[0], {0.0F, 0.0F});
	expectPoint(shot.points[1], {4.0F, 0.0F});
	ASSERT_TRUE(shot.snap.has_value());
	expectPoint(shot.snap->point, {4.0F, -4.0F});
	EXPECT_EQ(shot.snap->kind, construction::SnapKind::None) << "Alt: the raw pointer";
	EXPECT_FALSE(shot.snap->closesShape);
	EXPECT_TRUE(shot.verdict.valid);
	EXPECT_TRUE(shot.verdict.proposed);
	EXPECT_EQ(shot.status.pointCount, 2);
}

TEST_F(DrawingSnapshotTest, ARefusedPointReportsTheReasonTheStripShows) {
	drawing->activateFoundationTool();
	place({kSeenPoint});
	hoverFreeform(kUnseenPoint);
	const world_sim::DrawingSnapshot shot = drawing->snapshot();
	EXPECT_FALSE(shot.verdict.valid);
	EXPECT_TRUE(shot.verdict.proposed);
	EXPECT_FALSE(shot.verdict.reason.empty());
	EXPECT_EQ(shot.verdict.reason, drawing->status().message);
	EXPECT_EQ(shot.status.message, drawing->status().message);
	EXPECT_NE(shot.verdict.reason.find("unexplored"), std::string::npos) << shot.verdict.reason;
}

TEST_F(DrawingSnapshotTest, TheRectangleToolReportsItsCornerAndThePendingRectangle) {
	{
		SCOPED_TRACE("before the first press");
		const world_sim::DrawingSnapshot shot = drawing->snapshot();
		EXPECT_EQ(shot.tool, world_sim::ToolKind::FoundationRectangle);
		EXPECT_TRUE(shot.points.empty());
		EXPECT_FALSE(shot.pressPending);
		EXPECT_FALSE(shot.snap.has_value()) << "no move yet";
	}
	press({2.0F, 1.0F});
	{
		SCOPED_TRACE("a press, before its release");
		const world_sim::DrawingSnapshot shot = drawing->snapshot();
		EXPECT_TRUE(shot.pressPending);
		ASSERT_EQ(shot.points.size(), 1U);
		expectPoint(shot.points.front(), {2.0F, 1.0F});
	}
	hover({6.3F, 4.2F});
	const world_sim::DrawingSnapshot shot = drawing->snapshot();
	ASSERT_TRUE(shot.snap.has_value());
	expectPoint(shot.snap->point, {6.0F, 4.0F}); // the grid's, not the pointer's
	EXPECT_EQ(shot.snap->rectangleKind, construction::RectangleSnapKind::Grid);
	EXPECT_TRUE(shot.snap->closesShape) << "the next release commits the rectangle";
	ASSERT_EQ(shot.rectangleCorners.size(), 4U);
	expectPoint(shot.rectangleCorners[0], {2.0F, 1.0F});
	expectPoint(shot.rectangleCorners[1], {6.0F, 1.0F});
	expectPoint(shot.rectangleCorners[2], {6.0F, 4.0F});
	expectPoint(shot.rectangleCorners[3], {2.0F, 4.0F});
	EXPECT_FALSE(shot.rectangleLabel.empty());
	const world_sim::DrawingFeedbackInput feedback =
		drawing->feedbackInput(assets::PreviewStyle{}, kViewportWidth, kViewportHeight, 1.0F, world_sim::measureFeedbackText);
	EXPECT_EQ(shot.rectangleLabel, feedback.sizeLabel);
	EXPECT_FLOAT_EQ(shot.status.areaSquareMeters, 12.0F);
}

TEST_F(DrawingSnapshotTest, AWallChainReportsItsHostAndPoints) {
	const construction::FoundationId host = builtFloor({{-20.0F, -15.0F}, {20.0F, -15.0F}, {20.0F, 15.0F}, {-20.0F, 15.0F}});
	drawing->activateWallTool();
	place({{-12.0F, 0.0F}, {-4.0F, 0.0F}});
	hoverFreeform({-4.0F, 6.0F});
	const world_sim::DrawingSnapshot shot = drawing->snapshot();
	EXPECT_EQ(shot.tool, world_sim::ToolKind::Wall);
	EXPECT_EQ(shot.wallHost, host);
	ASSERT_EQ(shot.points.size(), 2U);
	ASSERT_TRUE(shot.snap.has_value());
	expectPoint(shot.snap->point, {-4.0F, 6.0F});
	EXPECT_TRUE(shot.status.wall);
	EXPECT_FLOAT_EQ(shot.status.segmentLengthMeters, 6.0F);
	EXPECT_FLOAT_EQ(shot.status.totalLengthMeters, 14.0F);
}

TEST_F(DrawingSnapshotTest, APausedToolProposesNothingButKeepsItsPlacedPoints) {
	drawing->activateFoundationTool();
	place({{0.0F, 0.0F}, {4.0F, 0.0F}});
	hoverFreeform({4.0F, -4.0F});
	drawing->pause();
	const world_sim::DrawingSnapshot shot = drawing->snapshot();
	EXPECT_EQ(shot.tool, world_sim::ToolKind::Foundation);
	EXPECT_FALSE(shot.followingPointer);
	EXPECT_EQ(shot.points.size(), 2U);
	EXPECT_FALSE(shot.snap.has_value());
	EXPECT_FALSE(shot.verdict.proposed);
	EXPECT_TRUE(shot.verdict.reason.empty());
}

// A wall chain starts on a foundation: before the first point, a pointer off every one proposes
// nothing, and the strip, the preview and the click all agree (a neutral hint, no red, a toast, no point).
TEST_F(DrawingSnapshotTest, AWallToolProposesNothingOffAFoundationBeforeItsFirstPoint) {
	builtFloor({{-20.0F, -15.0F}, {20.0F, -15.0F}, {20.0F, 15.0F}, {-20.0F, 15.0F}});
	drawing->activateWallTool();
	hover({25.0F, 0.0F});
	const world_sim::DrawingSnapshot off = drawing->snapshot();
	EXPECT_FALSE(off.verdict.proposed);
	EXPECT_TRUE(off.verdict.valid);
	EXPECT_TRUE(off.verdict.reason.empty());
	EXPECT_EQ(off.wallHost, construction::kInvalidFoundation);
	EXPECT_TRUE(drawing->status().valid);
	EXPECT_TRUE(drawing->status().message.empty());
	EXPECT_TRUE(drawing->previewVerdict().valid);
	EXPECT_FALSE(drawing->previewVerdict().markerInvalid);
	click({25.0F, 0.0F});
	EXPECT_EQ(drawing->status().pointCount, 0);
	EXPECT_EQ(lastToast(), "No foundation: walls must start on a foundation");

	hover({0.0F, 0.0F});
	const world_sim::DrawingSnapshot on = drawing->snapshot();
	EXPECT_TRUE(on.verdict.proposed);
	EXPECT_TRUE(on.verdict.valid);
	click({0.0F, 0.0F});
	EXPECT_EQ(drawing->status().pointCount, 1);
}

TEST_F(DrawingSnapshotTest, WithNoToolOnlyWhatIsMeaningfulThenIsReportedWhateverToolWasLast) {
	const construction::FoundationId target = builtFloor({{0.0F, 0.0F}, {8.0F, 0.0F}, {8.0F, 8.0F}, {0.0F, 8.0F}});
	const auto expectNoToolExtras = [this] {
		const world_sim::DrawingSnapshot shot = drawing->snapshot();
		EXPECT_FALSE(shot.tool.has_value());
		EXPECT_FALSE(shot.status.active);
		EXPECT_FALSE(shot.status.wall);
		EXPECT_FALSE(shot.status.foundationEdit);
		EXPECT_FALSE(shot.status.opening);
		EXPECT_TRUE(shot.status.thicknessPreset.empty());
		EXPECT_TRUE(shot.status.editLabel.empty());
		EXPECT_TRUE(shot.status.openingType.empty());
		EXPECT_FLOAT_EQ(shot.status.segmentLengthMeters, 0.0F);
		EXPECT_FLOAT_EQ(shot.status.wallCost, 0.0F);
		EXPECT_FLOAT_EQ(shot.status.wallWork, 0.0F);
		EXPECT_FLOAT_EQ(shot.status.areaDeltaSquareMeters, 0.0F);
		EXPECT_FLOAT_EQ(shot.status.openingWidthMeters, 0.0F);
		EXPECT_FALSE(shot.pointer.has_value());
	};
	{
		SCOPED_TRACE("after the wall tool");
		drawing->activateWallTool();
		hover({2.0F, 2.0F});
		drawing->deactivate();
		expectNoToolExtras();
	}
	{
		SCOPED_TRACE("after an edit");
		drawing->activateFoundationEditTool(target, construction::FoundationEditMode::Add);
		hover({2.0F, 2.0F});
		drawing->deactivate();
		expectNoToolExtras();
	}
	{
		SCOPED_TRACE("after the opening tool");
		drawing->activateOpeningTool("Door");
		hover({2.0F, 2.0F});
		drawing->deactivate();
		expectNoToolExtras();
	}
}

namespace {

	// A built floor with a built wall from (1, -26) to (7, -26) on it.
	class DrawingOpeningSnapshotTest : public DrawingSnapshotTest {
	  protected:
		void SetUp() override {
			DrawingSnapshotTest::SetUp();
			host = builtFloor({{-10.0F, -30.0F}, {10.0F, -30.0F}, {10.0F, -20.0F}, {-10.0F, -20.0F}});
			construction::ConstructionWorld& built = drawing->world();
			const auto						 placed = built.commitSegment(
				geometry::quantize(Foundation::Vec2{1.0F, -26.0F}), geometry::quantize(Foundation::Vec2{7.0F, -26.0F}), "Wood", "Standard", host, built.version()
			);
			ASSERT_TRUE(placed.ok());
			wall = placed.id;
			built.setSegmentState(wall, construction::FoundationState::Built);
			drawing->activateOpeningTool("Door");
		}

		construction::FoundationId host = construction::kInvalidFoundation;
		construction::SegmentId	   wall = construction::kInvalidSegment;
	};

} // namespace

TEST_F(DrawingOpeningSnapshotTest, TheOpeningToolReportsItsTypeAndTheWallItSnapsTo) {
	hover({4.0F, -26.0F});
	const world_sim::DrawingSnapshot shot = drawing->snapshot();
	EXPECT_EQ(shot.tool, world_sim::ToolKind::Opening);
	EXPECT_EQ(shot.openingType, "Door");
	ASSERT_TRUE(shot.openingSnap.has_value());
	EXPECT_TRUE(shot.openingSnap->valid);
	EXPECT_EQ(shot.openingSnap->segment, wall);
	EXPECT_NEAR(shot.openingSnap->t, 0.5F, 1e-4F);
	expectPoint(shot.openingSnap->point, {4.0F, -26.0F});
	EXPECT_TRUE(shot.verdict.valid) << shot.verdict.reason;
	EXPECT_TRUE(shot.verdict.proposed);
	EXPECT_FALSE(shot.snap.has_value());
}

TEST_F(DrawingOpeningSnapshotTest, OffAnyWallThereIsNoSnapAndTheStripsReason) {
	hover({-8.0F, -22.0F});
	const world_sim::DrawingSnapshot shot = drawing->snapshot();
	EXPECT_FALSE(shot.openingSnap.has_value() && shot.openingSnap->valid);
	EXPECT_FALSE(shot.verdict.valid);
	EXPECT_TRUE(shot.verdict.proposed);
	EXPECT_EQ(shot.verdict.reason, "no wall in range");
	EXPECT_EQ(shot.verdict.reason, drawing->status().message);
}

// The pointer moved a little along the wall from the door it just placed, still over it.
TEST_F(DrawingOpeningSnapshotTest, OnASpotAnOpeningOccupiesTheVerdictIsRefusedWithTheStripsReason) {
	click({4.0F, -26.0F});
	ASSERT_EQ(drawing->world().openings().size(), 1U) << lastToast();
	hover({4.3F, -26.0F});
	const world_sim::DrawingSnapshot shot = drawing->snapshot();
	ASSERT_TRUE(shot.openingSnap.has_value());
	EXPECT_TRUE(shot.openingSnap->valid);
	EXPECT_FALSE(shot.verdict.valid);
	EXPECT_TRUE(shot.verdict.proposed);
	EXPECT_EQ(shot.verdict.reason, "openings overlap");
	EXPECT_EQ(shot.verdict.reason, drawing->status().message);
	EXPECT_TRUE(drawing->pointerPreview().openingGhost);
	click({4.3F, -26.0F});
	EXPECT_EQ(drawing->world().openings().size(), 1U);
	EXPECT_EQ(lastToast(), "Invalid opening: openings overlap");
}

// The pointer still where it placed a door proposes nothing: no ghost (the door placed shows there),
// no reason, the strip's hint, and a second click places no second door and says nothing.
TEST_F(DrawingOpeningSnapshotTest, ThePointerRestingOnTheOpeningJustPlacedProposesNothing) {
	click({4.0F, -26.0F});
	ASSERT_EQ(drawing->world().openings().size(), 1U) << lastToast();
	const std::size_t toastsAfterPlacing = toasts.size();
	hover({4.0F, -26.0F});
	const world_sim::DrawingSnapshot shot = drawing->snapshot();
	EXPECT_TRUE(shot.verdict.valid);
	EXPECT_FALSE(shot.verdict.proposed);
	EXPECT_TRUE(shot.verdict.reason.empty()) << shot.verdict.reason;
	EXPECT_TRUE(shot.status.valid);
	EXPECT_EQ(world_sim::statusHint(shot.status).text, "Click a wall to place");
	EXPECT_FALSE(drawing->pointerPreview().openingGhost);
	click({4.0F, -26.0F});
	EXPECT_EQ(drawing->world().openings().size(), 1U);
	EXPECT_EQ(toasts.size(), toastsAfterPlacing) << lastToast();
}

// The record belongs to the tool that placed: the opening tool started again, or switched away
// from and back, judges the same still pointer afresh.
TEST_F(DrawingOpeningSnapshotTest, ARestartedOpeningToolDoesNotRestOnTheLastOpening) {
	click({4.0F, -26.0F});
	ASSERT_EQ(drawing->world().openings().size(), 1U) << lastToast();
	for (const bool switchAway : {false, true}) {
		SCOPED_TRACE(switchAway ? "the wall tool and back" : "started again");
		if (switchAway) {
			drawing->activateWallTool();
		}
		drawing->activateOpeningTool("Door");
		hover({4.0F, -26.0F});
		EXPECT_TRUE(drawing->snapshot().verdict.proposed);
		EXPECT_EQ(drawing->status().message, "openings overlap");
	}
}

TEST_F(DrawingOpeningSnapshotTest, ADoorClickOnABlueprintWallPlacesAnOpeningBlueprint) {
	ASSERT_TRUE(drawing->world().setSegmentState(wall, construction::FoundationState::Blueprint));
	hover({4.0F, -26.0F});
	EXPECT_TRUE(drawing->snapshot().verdict.valid) << drawing->snapshot().verdict.reason;
	EXPECT_EQ(drawing->status().message, "");
	EXPECT_EQ(world_sim::statusHint(drawing->status()).text, "Click a wall to place");
	click({4.0F, -26.0F});
	ASSERT_EQ(drawing->world().openings().size(), 1U) << lastToast();
	const construction::Opening& placed = drawing->world().openings().front();
	EXPECT_EQ(placed.segment, wall);
	EXPECT_EQ(placed.state, construction::FoundationState::Blueprint);
	EXPECT_EQ(lastToast(), "Opening placed: Door blueprint");
}

TEST_F(DrawingOpeningSnapshotTest, AWallMarkedForDemolitionTakesNoNewOpening) {
	ecs::World&		 ecsWorld = seen.world;
	const ecs::EntityID wallEntity = ecsWorld.createEntity();
	ecs::StructureBlueprint demolishing;
	demolishing.demolishing = true;
	ecsWorld.addComponent<ecs::StructureBlueprint>(wallEntity, std::move(demolishing));
	ASSERT_TRUE(drawing->world().setSegmentEntity(wall, wallEntity));
	hover({4.0F, -26.0F});
	EXPECT_FALSE(drawing->snapshot().verdict.valid);
	EXPECT_EQ(drawing->status().message, "the wall is being demolished");
	click({4.0F, -26.0F});
	EXPECT_TRUE(drawing->world().openings().empty());
	EXPECT_EQ(lastToast(), "Can't place here: the wall is being demolished");
}

TEST_F(DrawingOpeningSnapshotTest, ThePointerIsReportedBeforeTheWallSnapsIt) {
	hover({4.2F, -25.8F});
	const world_sim::DrawingSnapshot shot = drawing->snapshot();
	ASSERT_TRUE(shot.pointer.has_value());
	EXPECT_NEAR(shot.pointer->x, 4.2F, 1e-3F);
	EXPECT_NEAR(shot.pointer->y, -25.8F, 1e-3F);
	ASSERT_TRUE(shot.openingSnap.has_value());
	EXPECT_NEAR(shot.openingSnap->point.x, 4.2F, 1e-3F);
	EXPECT_NEAR(shot.openingSnap->point.y, -26.0F, 1e-3F);
}

TEST_F(DrawingSnapshotTest, AnAddThatClosesAValidShapeReportsItsTargetModeAndTheAreaItAdds) {
	const construction::FoundationId target = builtFloor({{0.0F, 0.0F}, {8.0F, 0.0F}, {8.0F, 8.0F}, {0.0F, 8.0F}});
	drawing->activateFoundationEditTool(target, construction::FoundationEditMode::Add);
	place({{8.0F, 0.0F}, {12.0F, 0.0F}, {12.0F, 8.0F}, {8.0F, 8.0F}});
	hover({8.0F, 0.0F});
	const world_sim::DrawingSnapshot shot = drawing->snapshot();
	EXPECT_EQ(shot.tool, world_sim::ToolKind::FoundationEdit);
	EXPECT_EQ(shot.editTarget, target);
	EXPECT_EQ(shot.editMode, construction::FoundationEditMode::Add);
	ASSERT_TRUE(shot.snap.has_value());
	EXPECT_TRUE(shot.snap->closesShape);
	EXPECT_TRUE(shot.verdict.valid) << shot.verdict.reason;
	EXPECT_TRUE(shot.status.foundationEdit);
	EXPECT_GT(shot.status.areaDeltaSquareMeters, 0.0F);
}

TEST_F(DrawingSnapshotTest, AnAddApartFromTheFloorReportsTheReasonItIsRefused) {
	const construction::FoundationId target = builtFloor({{0.0F, 0.0F}, {8.0F, 0.0F}, {8.0F, 8.0F}, {0.0F, 8.0F}});
	drawing->activateFoundationEditTool(target, construction::FoundationEditMode::Add);
	place({{20.0F, 0.0F}, {24.0F, 0.0F}, {24.0F, 4.0F}});
	hover({20.0F, 0.0F});
	const world_sim::DrawingSnapshot shot = drawing->snapshot();
	EXPECT_EQ(shot.editMode, construction::FoundationEditMode::Add);
	ASSERT_TRUE(shot.snap.has_value());
	EXPECT_TRUE(shot.snap->closesShape);
	EXPECT_FALSE(shot.verdict.valid);
	EXPECT_FALSE(shot.verdict.reason.empty());
	EXPECT_EQ(shot.verdict.reason, drawing->previewVerdict().editReason);
}

TEST_F(DrawingSnapshotTest, ASubtractFromABuiltFloorReportsItsModeAndTheReasonItIsRefused) {
	const construction::FoundationId target = builtFloor({{0.0F, 0.0F}, {8.0F, 0.0F}, {8.0F, 8.0F}, {0.0F, 8.0F}});
	drawing->activateFoundationEditTool(target, construction::FoundationEditMode::Subtract);
	place({{4.0F, 4.0F}, {12.0F, 4.0F}, {12.0F, 12.0F}});
	hover({4.0F, 4.0F});
	const world_sim::DrawingSnapshot shot = drawing->snapshot();
	EXPECT_EQ(shot.editTarget, target);
	EXPECT_EQ(shot.editMode, construction::FoundationEditMode::Subtract);
	EXPECT_FALSE(shot.verdict.valid);
	EXPECT_FALSE(shot.verdict.reason.empty());
	EXPECT_EQ(shot.verdict.reason, drawing->status().message);
}

TEST_F(DrawingSnapshotTest, APointerOnTheFirstPointReportsTheOriginSnapThatClosesTheShape) {
	{
		SCOPED_TRACE("a freeform shape");
		drawing->activateFoundationTool();
		place({{0.0F, 0.0F}, {8.0F, 0.0F}, {8.0F, 8.0F}});
		hover({0.0F, 0.0F});
		const world_sim::DrawingSnapshot shot = drawing->snapshot();
		ASSERT_TRUE(shot.snap.has_value());
		EXPECT_EQ(shot.snap->kind, construction::SnapKind::Origin);
		EXPECT_TRUE(shot.snap->closesShape);
		drawing->deactivate();
	}
	{
		SCOPED_TRACE("a wall chain");
		builtFloor({{-20.0F, -15.0F}, {20.0F, -15.0F}, {20.0F, 15.0F}, {-20.0F, 15.0F}});
		drawing->activateWallTool();
		place({{-12.0F, 0.0F}, {-4.0F, 0.0F}, {-4.0F, 6.0F}});
		hover({-12.0F, 0.0F});
		const world_sim::DrawingSnapshot shot = drawing->snapshot();
		ASSERT_TRUE(shot.snap.has_value());
		EXPECT_EQ(shot.snap->kind, construction::SnapKind::Origin);
		EXPECT_TRUE(shot.snap->closesShape);
	}
}

TEST_F(DrawingSnapshotTest, ARescueCloseClosesTheShapeWithoutAnOriginSnap) {
	seen.camera.setZoom(kRescueZoom);
	drawing->activateFoundationTool();
	const glm::vec2 firstCorner{0.0F, 0.0F};
	place({firstCorner, {8.0F, 0.0F}, {8.0F, 8.0F}, {0.0F, 8.0F}});
	hoverFreeform(firstCorner + kPastTheCatch);
	const world_sim::DrawingSnapshot shot = drawing->snapshot();
	ASSERT_TRUE(shot.snap.has_value());
	EXPECT_EQ(shot.snap->kind, construction::SnapKind::None);
	EXPECT_TRUE(shot.snap->closesShape);
}

TEST_F(DrawingSnapshotTest, AGuideAndARoundedLengthAreReportedWithWhatTheyCameFrom) {
	world_sim::UserSettings::Get().drawing = world_sim::DrawingSettings{
		.angleSteps = true, .alignToExistingBuildings = true, .guidesFromWorldAxes = true, .guideCrossings = true, .roundLengths = true
	};
	drawing->activateFoundationTool();
	place({{0.0F, 0.0F}, {8.0F, 0.0F}});
	hover({8.2F, -4.1F});
	const world_sim::DrawingSnapshot shot = drawing->snapshot();
	ASSERT_TRUE(shot.snap.has_value());
	expectPoint(shot.snap->point, {8.0F, -4.1F});
	EXPECT_EQ(shot.snap->kind, construction::SnapKind::GuideLine);
	ASSERT_EQ(shot.snap->guides.size(), 1U);
	EXPECT_EQ(shot.snap->guides[0].source, construction::GuideSource::WorldAxes);
	EXPECT_EQ(shot.snap->guides[0].relation, construction::GuideRelation::Perpendicular);
	expectPoint(shot.snap->guides[0].from, {8.0F, 0.0F});
	EXPECT_NEAR(shot.snap->guides[0].distanceFromNodeMeters, 4.1F, 1e-3F);
	EXPECT_EQ(shot.snap->lengthSnap, construction::LengthSnapKind::Rounded);
	EXPECT_NEAR(shot.snap->lengthMeters, 4.1F, 1e-3F);
}

// The length step reports what it set; the segment's length is whatever the point makes it.
TEST_F(DrawingSnapshotTest, TheSegmentLengthIsMeasuredWhateverTheLengthStepDid) {
	world_sim::UserSettings::Get().drawing = world_sim::DrawingSettings{
		.angleSteps = true, .alignToExistingBuildings = true, .guidesFromWorldAxes = true, .guideCrossings = true, .roundLengths = true
	};
	drawing->activateFoundationTool();
	hover({3.0F, 2.0F});
	{
		SCOPED_TRACE("no placed point");
		const world_sim::DrawingSnapshot shot = drawing->snapshot();
		ASSERT_TRUE(shot.snap.has_value());
		EXPECT_FALSE(shot.snap->segmentLengthMeters.has_value());
	}
	place({{0.0F, 0.0F}, {8.0F, 0.0F}});
	hover({12.2F, 0.1F});
	{
		SCOPED_TRACE("a length measured from the guide's node, not the last point");
		const world_sim::DrawingSnapshot shot = drawing->snapshot();
		ASSERT_TRUE(shot.snap.has_value());
		EXPECT_EQ(shot.snap->lengthSnap, construction::LengthSnapKind::RoundedAlongGuide);
		EXPECT_NEAR(shot.snap->lengthMeters, 12.2F, 1e-3F);
		ASSERT_TRUE(shot.snap->segmentLengthMeters.has_value());
		EXPECT_NEAR(*shot.snap->segmentLengthMeters, 4.2F, 1e-3F);
	}
	world_sim::UserSettings::Get().drawing = world_sim::DrawingSettings{};
	hoverFreeform({8.0F, 4.0F});
	{
		SCOPED_TRACE("no length step ran");
		const world_sim::DrawingSnapshot shot = drawing->snapshot();
		ASSERT_TRUE(shot.snap.has_value());
		EXPECT_EQ(shot.snap->lengthSnap, construction::LengthSnapKind::None);
		EXPECT_FLOAT_EQ(shot.snap->lengthMeters, 0.0F);
		ASSERT_TRUE(shot.snap->segmentLengthMeters.has_value());
		EXPECT_NEAR(*shot.snap->segmentLengthMeters, 4.0F, 1e-3F);
	}
}

TEST_F(DrawingSnapshotTest, ThePointerIsTheRawOneAndGoneWhileTheToolIsPaused) {
	drawing->activateFoundationTool();
	{
		SCOPED_TRACE("before any move");
		EXPECT_FALSE(drawing->snapshot().pointer.has_value());
	}
	world_sim::UserSettings::Get().drawing = testedSettings();
	place({{0.0F, 0.0F}, {8.0F, 0.0F}});
	hover({8.2F, -4.1F});
	{
		SCOPED_TRACE("after a move that snaps");
		const world_sim::DrawingSnapshot shot = drawing->snapshot();
		ASSERT_TRUE(shot.pointer.has_value());
		EXPECT_NEAR(shot.pointer->x, 8.2F, 1e-3F);
		EXPECT_NEAR(shot.pointer->y, -4.1F, 1e-3F);
		ASSERT_TRUE(shot.snap.has_value());
		EXPECT_NEAR(shot.snap->point.x, 8.0F, 1e-3F) << "the guide moved the point";
	}
	drawing->pause();
	{
		SCOPED_TRACE("paused");
		EXPECT_FALSE(drawing->snapshot().pointer.has_value());
	}
}
