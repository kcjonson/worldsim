#include "scenes/game/world/placement/PlacementSystem.h"

#include "scenes/game/world/CreekWorldFixture.test.h"
#include "scenes/game/world/PlayerVisibilityFixture.test.h"

#include <assets/AssetRegistry.h>
#include <assets/AssetRegistry.test.h>
#include <assets/ConstructionRegistry.h>
#include <construction/ConstructionWorld.h>
#include <core/Vec2i64.h>
#include <assets/TurnQuanta.h>
#include <ecs/GoalTaskRegistry.h>
#include <ecs/components/AgentRadius.h>
#include <ecs/components/Appearance.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Packaged.h>
#include <ecs/components/Transform.h>
#include <ecs/systems/NavigationSystem.h>
#include <ecs/systems/PlacedItemSystem.h>
#include <ecs/systems/PlacedNothing.test.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/SyntheticWorldSampler.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>

namespace {

	using world_sim::testing::kSeenPoint;
	using world_sim::testing::kUnseenPoint;
	using world_sim::testing::kViewportHeight;
	using world_sim::testing::kViewportWidth;

	constexpr const char* kCrate = "Test_PlacementCrate";
	constexpr float		  kCrateHalfExtent = 3.0F;

	// All land, the chunks round the origin generated once and shared by every test here.
	world::ChunkManager* landChunks() {
		static const std::unique_ptr<world::ChunkManager> chunks = [] {
			auto manager = std::make_unique<world::ChunkManager>(std::make_unique<world::SyntheticWorldSampler>(7, world::SyntheticMap{}));
			manager->setLoadRadius(2);
			manager->setUnloadRadius(4);
			manager->update({0.0F, 0.0F});
			manager->finishPendingGeneration();
			return manager;
		}();
		return chunks.get();
	}

	class PlacementVisibilityTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			assets::AssetDefinition crate;
			crate.defName = kCrate;
			crate.setCollision(0, {.type = assets::CollisionShapeType::Rect, .halfExtentsMeters = {kCrateHalfExtent, kCrateHalfExtent}});
			assets::AssetRegistry::Get().registerTestDefinition(crate);

			navigation = &seen.world.registerSystem<ecs::NavigationSystem>();
			seen.world.registerSystem<ecs::PlacedItemSystem>();
			navigation->setChunkManager(landChunks());
			navigation->setPlacementData(&emptyPlacement.executor, &emptyPlacement.processed);
			const ecs::EntityID mesher = seen.world.createEntity();
			seen.world.addComponent<ecs::Position>(mesher, ecs::Position{{0.0F, 0.0F}});
			seen.world.addComponent<ecs::AgentRadius>(mesher, ecs::AgentRadius{});
			ecs::Memory memory;
			memory.sightRadius = 100.0F;
			seen.world.addComponent<ecs::Memory>(mesher, std::move(memory));
			seen.world.update(0.016F);
			navigation->finishPendingBuilds();

			placement = std::make_unique<world_sim::PlacementSystem>(world_sim::PlacementSystem::Args{
				.world = &seen.world, .camera = &seen.camera, .placementExecutor = nullptr, .navigation = navigation, .worldSeed = 7, .callbacks = {}
			});
			placement->selectBuildItem(kCrate);
		}

		void hover(glm::vec2 meters) {
			const glm::vec2 screen = seen.screenOf(meters);
			placement->handleMouseMove(screen.x, screen.y, kViewportWidth, kViewportHeight, false);
		}

		world_sim::testing::SeenWorld seen;
		ecs::EmptyPlacement			  emptyPlacement;
		ecs::NavigationSystem*		  navigation = nullptr;
		std::unique_ptr<world_sim::PlacementSystem> placement;
	};

} // namespace

TEST_F(PlacementVisibilityTest, GhostIsValidOnSeenGroundAndInvalidOnUnseenGround) {
	ASSERT_TRUE(navigation->isValidPosition(kSeenPoint));
	ASSERT_TRUE(navigation->isValidPosition(kUnseenPoint)) << "the mesh reaches past what is seen";
	hover(kSeenPoint);
	EXPECT_TRUE(placement->ghostValid());
	hover(kUnseenPoint);
	EXPECT_FALSE(placement->ghostValid());
}

TEST_F(PlacementVisibilityTest, FootprintHalfOnUnseenGroundIsInvalid) {
	const glm::vec2 edge{world_sim::testing::kSeenRadiusMeters - 2.0F, 0.0F};
	ASSERT_TRUE(seen.visibility.isSeen(edge));
	hover(edge);
	EXPECT_FALSE(placement->ghostValid());
}

TEST_F(PlacementVisibilityTest, ClickRefusesOnUnseenGroundAndPlacesOnSeenGround) {
	hover(kUnseenPoint);
	EXPECT_FALSE(placement->handleRelease());
	EXPECT_EQ(placement->state(), world_sim::PlacementState::Placing);
	hover(kSeenPoint);
	EXPECT_TRUE(placement->handleRelease());
}

TEST_F(PlacementVisibilityTest, RevealTurnsTheRuleOff) {
	seen.visibility.setRevealed(true);
	hover(kUnseenPoint);
	EXPECT_TRUE(placement->ghostValid());
	EXPECT_TRUE(placement->handleRelease());
}

namespace {

	using world_sim::testing::kCreekHalfWidthMeters;
	using world_sim::testing::kCreekY;

	constexpr const char* kBox = "Test_PlacementBox";
	constexpr const char* kBareBox = "Test_PlacementBareBox";
	constexpr const char* kWideBox = "Test_PlacementWideBox";
	constexpr float		  kBoxHalfExtent = 0.4F;

	// Nothing is set up in a stream a colonist could wade: the placement tool refuses a target whose
	// footprint touches the creek, the way it refuses water.
	class PlacementFordTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			assets::AssetDefinition box;
			box.defName = kBox;
			box.setCollision(0, {.type = assets::CollisionShapeType::Rect, .halfExtentsMeters = {kBoxHalfExtent, kBoxHalfExtent}});
			assets::AssetRegistry::Get().registerTestDefinition(box);

			navigation = &world_sim::testing::meshCreekWorld(seen.world, emptyPlacement);
			placement = std::make_unique<world_sim::PlacementSystem>(world_sim::PlacementSystem::Args{
				.world = &seen.world, .camera = &seen.camera, .placementExecutor = nullptr, .navigation = navigation, .worldSeed = 7, .callbacks = {}
			});
			placement->selectBuildItem(kBox);
		}
		void TearDown() override { assets::AssetRegistry::Get().clearDefinitions(); }

		void hover(glm::vec2 meters) {
			const glm::vec2 screen = seen.screenOf(meters);
			placement->handleMouseMove(screen.x, screen.y, kViewportWidth, kViewportHeight, false);
		}

		world_sim::testing::SeenWorld seen;
		ecs::EmptyPlacement			  emptyPlacement;
		ecs::NavigationSystem*		  navigation = nullptr;
		std::unique_ptr<world_sim::PlacementSystem> placement;
	};

} // namespace

TEST_F(PlacementFordTest, TargetInTheCreekIsRefusedAndBesideItIsPlaced) {
	const glm::vec2 inCreek{10.0F, kCreekY};
	ASSERT_TRUE(navigation->isValidPosition(inCreek)) << "a colonist wades the creek";
	ASSERT_FALSE(navigation->isPointBuildableOnceCleared(inCreek)) << "the creek is a ford in the mesh";

	hover(inCreek);
	EXPECT_FALSE(placement->ghostValid());
	EXPECT_FALSE(placement->handleRelease());
	EXPECT_EQ(placement->state(), world_sim::PlacementState::Placing);

	// The centre is on the bank, the footprint reaches into the creek.
	const glm::vec2 overhanging{10.0F, kCreekY + kCreekHalfWidthMeters + kBoxHalfExtent * 0.5F};
	ASSERT_TRUE(navigation->isPointBuildableNow(overhanging));
	hover(overhanging);
	EXPECT_FALSE(placement->ghostValid()) << "the footprint is tested, not only its centre";

	const glm::vec2 onTheBank{10.0F, kCreekY + 2.0F};
	hover(onTheBank);
	EXPECT_TRUE(placement->ghostValid());
	EXPECT_TRUE(placement->handleRelease());
}

// Moving an installed item re-packages it and picks its new spot with the same tool: a spot in the
// creek is refused and leaves it without a target, one on the bank becomes its target.
TEST_F(PlacementFordTest, MovingAnInstalledItemIntoTheCreekIsRefused) {
	placement->cancel();
	const ecs::EntityID item = seen.world.createEntity();
	seen.world.addComponent<ecs::Position>(item, ecs::Position{{10.0F, kCreekY - 3.0F}});
	seen.world.addComponent<ecs::Packaged>(item, ecs::Packaged{std::nullopt, false});
	placement->beginRelocation(item, kBox);

	hover({10.0F, kCreekY});
	EXPECT_FALSE(placement->ghostValid());
	EXPECT_FALSE(placement->handleRelease());
	EXPECT_FALSE(seen.world.getComponent<ecs::Packaged>(item)->targetPosition.has_value());

	const glm::vec2 onTheBank{10.0F, kCreekY + 2.0F};
	hover(onTheBank);
	ASSERT_TRUE(placement->handleRelease());
	const std::optional<glm::vec2> target = seen.world.getComponent<ecs::Packaged>(item)->targetPosition;
	ASSERT_TRUE(target.has_value());
	EXPECT_LT(glm::distance(*target, onTheBank), 0.01F);
}

// A def with no declared outline is tested at its centre alone.
TEST_F(PlacementFordTest, DefWithNoOutlineIsTestedAtItsCentre) {
	assets::AssetDefinition bare;
	bare.defName = kBareBox;
	assets::AssetRegistry::Get().registerTestDefinition(bare);
	EXPECT_FALSE(placement->footprintBuildableNow(kBareBox, {10.0F, kCreekY}, 0));
	EXPECT_TRUE(placement->footprintBuildableNow(kBareBox, {10.0F, kCreekY + kCreekHalfWidthMeters + 0.1F}, 0));
}

// A shipped box declares no outline, so it is tested at its centre: refused in the ford, accepted on the bank.
TEST_F(PlacementFordTest, ShippedBoxCentredInTheFordIsRefusedAndOnTheBankIsAccepted) {
	// <root>/apps/world-sim/scenes/game/world/placement/PlacementSystem.test.cpp -> <root>.
	std::filesystem::path root = std::filesystem::path(__FILE__);
	for (int up = 0; up < 7; ++up) {
		root = root.parent_path();
	}
	const std::filesystem::path boxXml = root / "assets" / "world" / "storage" / "BasicBox" / "BasicBox.xml";
	ASSERT_TRUE(std::filesystem::exists(boxXml)) << "the shipped BasicBox is not at " << boxXml.string();
	auto& registry = assets::AssetRegistry::Get();
	ASSERT_TRUE(registry.loadDefinitions(boxXml.string()));
	ASSERT_FALSE(registry.getDefinition("BasicBox")->collisionFor(0).blocks()) << "the box declares an outline now; this test is moot";

	EXPECT_FALSE(placement->footprintBuildableNow("BasicBox", {10.0F, kCreekY}, 0));
	EXPECT_TRUE(placement->footprintBuildableNow("BasicBox", {10.0F, kCreekY + kCreekHalfWidthMeters + 0.5F}, 0));
}

namespace {

	// The shipped TestPartsBox, a parts item 1.2 m by 0.6 m (rotatable-placed-items 14).
	void loadTestPartsBox() {
		// <root>/apps/world-sim/scenes/game/world/placement/PlacementSystem.test.cpp -> <root>.
		std::filesystem::path root = std::filesystem::path(__FILE__);
		for (int up = 0; up < 7; ++up) {
			root = root.parent_path();
		}
		const std::filesystem::path xml = root / "assets" / "world" / "misc" / "TestPartsBox" / "TestPartsBox.xml";
		ASSERT_TRUE(assets::loadTestViewConfig()) << "a parts def is read under the view config";
		ASSERT_TRUE(assets::AssetRegistry::Get().loadDefinitions(xml.string()));
		ASSERT_TRUE(world_sim::PlacementSystem::turns("TestPartsBox"));
	}

	const ecs::Rotation* placedRotation(ecs::World& world, const char* defName) {
		for (auto [entity, appearance, rotation] : world.view<ecs::Appearance, ecs::Rotation>()) {
			if (appearance.defName == defName) {
				return &rotation;
			}
		}
		return nullptr;
	}

} // namespace

// Q and E step the turn by the angle step, Alt by a degree; the item is placed at the turn shown, and the next
// placement of it starts there (rotatable-placed-items 14.1, 14.3).
TEST_F(PlacementFordTest, TurnKeysTurnTheItemAndItIsPlacedAtThatTurn) {
	loadTestPartsBox();
	placement->cancel();
	placement->selectBuildItem("TestPartsBox");
	EXPECT_EQ(placement->ghostTurnQuanta(), 0U) << "front down the first time";
	hover({10.0F, kCreekY + 3.0F});
	EXPECT_TRUE(placement->handleTurnKey(1, false));
	EXPECT_EQ(placement->ghostTurnQuanta(), assets::turnQuantaFromDegrees(15.0));
	EXPECT_TRUE(placement->handleTurnKey(1, false));
	EXPECT_TRUE(placement->handleTurnKey(-1, true));
	EXPECT_EQ(placement->ghostTurnQuanta(), assets::turnQuantaFromDegrees(29.0));
	ASSERT_TRUE(placement->ghostValid());
	ASSERT_TRUE(placement->handleRelease());

	const ecs::Rotation* rotation = placedRotation(seen.world, "TestPartsBox");
	ASSERT_NE(rotation, nullptr);
	EXPECT_EQ(assets::turnQuantaFromRadians(rotation->radians), assets::turnQuantaFromDegrees(29.0));

	placement->selectBuildItem("TestPartsBox");
	EXPECT_EQ(placement->ghostTurnQuanta(), assets::turnQuantaFromDegrees(29.0)) << "the last turn used for it";
	placement->cancel();
	placement->selectBuildItem(kBox);
	EXPECT_FALSE(placement->handleTurnKey(1, false)) << "a def that isn't parts doesn't turn";
}

// A wall built beside a still ghost reaches it on the next update: the construction world's revision is part of what
// a move settles on, so the ghost slides flush without the pointer moving.
TEST_F(PlacementFordTest, AWallBuiltBesideAStillGhostSlidesItOnTheNextUpdate) {
	loadTestPartsBox();
	std::filesystem::path root = std::filesystem::path(__FILE__);
	for (int up = 0; up < 7; ++up) {
		root = root.parent_path();
	}
	ASSERT_TRUE(assets::ConstructionRegistry::Get().load((root / "assets" / "config" / "construction").string()));
	construction::ConstructionWorld walls;
	placement->setConstructionWorld(&walls);
	placement->cancel();
	placement->selectBuildItem("TestPartsBox");
	const glm::vec2 pivot{10.0F, kCreekY + 4.0F};
	hover(pivot);
	ASSERT_LT(glm::distance(glm::vec2{placement->ghostPosition().x, placement->ghostPosition().y}, pivot), 0.001F);

	// The box's back edge is 0.3 m north of its pivot; a 0.2 m wall whose south face is 0.15 m beyond it.
	const double centreline = static_cast<double>(pivot.y) - 0.3 - 0.15 - 0.1;
	ASSERT_TRUE(walls
					.commitSegment(
						geometry::quantize({pivot.x - 2.0F, static_cast<float>(centreline)}),
						geometry::quantize({pivot.x + 2.0F, static_cast<float>(centreline)}), "Wood", "Standard", construction::kInvalidFoundation, 0
					)
					.ok());
	hover(pivot);
	EXPECT_NEAR(placement->ghostPosition().y, pivot.y - 0.15F, 0.002F) << "slid flush with the pointer still";
	placement->setConstructionWorld(nullptr);
	assets::ConstructionRegistry::Get().clear();
}

// A ghost over another item reads "Overlaps an item", installed or pending: the overlap is tested before the ground,
// which the installed item's collider in the mesh also refuses. A box flush against another is placeable. A still ghost
// learns of a target given under it on the next update, through the placed items' revision (rotatable-placed-items
// 11.3, 14.2).
TEST_F(PlacementFordTest, AGhostOverAnotherItemIsRefusedWithItsReason) {
	loadTestPartsBox();
	placement->cancel();
	const Foundation::Vec2 boxAt{10.0F, kCreekY + 4.0F};
	placement->spawnEntity("TestPartsBox", boxAt, assets::turnQuantaFromDegrees(45.0));
	seen.world.update(0.016F);
	seen.world.update(0.016F);
	placement->selectBuildItem("TestPartsBox");

	hover({boxAt.x + 0.3F, boxAt.y});
	EXPECT_FALSE(placement->ghostValid());
	EXPECT_EQ(placement->ghostRefusal(), construction::PlacementRefusal::OverlapsItem);
	EXPECT_FALSE(placement->handleRelease());

	const Foundation::Vec2 squareAt{boxAt.x - 4.0F, boxAt.y};
	placement->spawnEntity("TestPartsBox", squareAt, 0);
	seen.world.update(0.016F);
	seen.world.update(0.016F);
	hover({squareAt.x + 1.2F, squareAt.y});
	EXPECT_TRUE(placement->ghostValid()) << "flush against a box in the mesh";

	const glm::vec2		crateTarget{boxAt.x, boxAt.y + 3.0F};
	const ecs::EntityID crate = seen.world.createEntity();
	seen.world.addComponent<ecs::Position>(crate, ecs::Position{{boxAt.x - 6.0F, boxAt.y}});
	seen.world.addComponent<ecs::Appearance>(crate, ecs::Appearance{"TestPartsBox"});
	seen.world.addComponent<ecs::Packaged>(crate, ecs::Packaged{.targetPosition = crateTarget});
	seen.world.update(0.016F);
	hover({crateTarget.x + 0.3F, crateTarget.y});
	EXPECT_FALSE(placement->ghostValid());
	EXPECT_EQ(placement->ghostRefusal(), construction::PlacementRefusal::OverlapsItem);

	const glm::vec2 clear{boxAt.x + 4.0F, boxAt.y};
	hover(clear);
	ASSERT_TRUE(placement->ghostValid());
	EXPECT_EQ(placement->ghostRefusal(), std::nullopt);
	// A target moves nothing in the mesh, so only the index's revision can tell the still ghost.
	seen.world.getComponent<ecs::Packaged>(crate)->targetPosition = clear;
	hover(clear);
	EXPECT_TRUE(placement->ghostValid()) << "the index has not seen it before the update";
	seen.world.update(0.016F);
	hover(clear);
	EXPECT_FALSE(placement->ghostValid()) << "settled again with the pointer still";
	EXPECT_EQ(placement->ghostRefusal(), construction::PlacementRefusal::OverlapsItem);
}

// A body across a wall, blueprint or built, reads "A wall is in the way"; a blueprint wall drawn through a still ghost
// reaches it on the next move through the construction world's version.
TEST_F(PlacementFordTest, AGhostAcrossAWallIsRefused) {
	loadTestPartsBox();
	std::filesystem::path root = std::filesystem::path(__FILE__);
	for (int up = 0; up < 7; ++up) {
		root = root.parent_path();
	}
	ASSERT_TRUE(assets::ConstructionRegistry::Get().load((root / "assets" / "config" / "construction").string()));
	construction::ConstructionWorld walls;
	placement->setConstructionWorld(&walls);
	const glm::vec2 pivot{10.0F, kCreekY + 4.0F};
	const auto		wallThrough = [&](glm::vec2 point) {
		 ASSERT_TRUE(walls
						 .commitSegment(
							 geometry::quantize({point.x - 3.0F, point.y}), geometry::quantize({point.x + 3.0F, point.y}), "Wood", "Standard",
							 construction::kInvalidFoundation, 0
						 )
						 .ok());
	};
	wallThrough(pivot);
	placement->cancel();
	placement->selectBuildItem("TestPartsBox");
	hover({pivot.x, pivot.y});
	EXPECT_FALSE(placement->ghostValid());
	EXPECT_EQ(placement->ghostRefusal(), construction::PlacementRefusal::WallInTheWay);

	const glm::vec2 still{pivot.x, pivot.y + 2.0F};
	hover(still);
	ASSERT_TRUE(placement->ghostValid());
	wallThrough(still);
	hover(still);
	EXPECT_EQ(placement->ghostRefusal(), construction::PlacementRefusal::WallInTheWay) << "a blueprint wall beside a still ghost";
	placement->setConstructionWorld(nullptr);
	assets::ConstructionRegistry::Get().clear();
}

// The flush slide is not taken when it would move a ghost from a spot it can stand on to one it can't: a wall in the
// creek with its face 0.1 m out from the bank would slide the box's back edge into the water.
TEST_F(PlacementFordTest, AFlushSlideOntoRefusedGroundIsNotTaken) {
	loadTestPartsBox();
	std::filesystem::path root = std::filesystem::path(__FILE__);
	for (int up = 0; up < 7; ++up) {
		root = root.parent_path();
	}
	ASSERT_TRUE(assets::ConstructionRegistry::Get().load((root / "assets" / "config" / "construction").string()));
	construction::ConstructionWorld walls;
	placement->setConstructionWorld(&walls);
	const float bank = kCreekY + kCreekHalfWidthMeters;
	// A 0.2 m wall: its south face at bank - 0.1.
	ASSERT_TRUE(walls
					.commitSegment(
						geometry::quantize({6.0F, bank - 0.2F}), geometry::quantize({14.0F, bank - 0.2F}), "Wood", "Standard",
						construction::kInvalidFoundation, 0
					)
					.ok());
	placement->cancel();
	placement->selectBuildItem("TestPartsBox");
	// The box's back edge 0.15 m south of the bank, 0.25 m from the face: within the slide's reach.
	const glm::vec2 pointer{10.0F, bank + 0.15F + 0.3F};
	hover(pointer);
	EXPECT_TRUE(placement->ghostValid());
	EXPECT_NEAR(placement->ghostPosition().y, pointer.y, 0.001F) << "not slid into the creek";
	EXPECT_EQ(placement->refusalAt("TestPartsBox", {pointer.x, bank - 0.1F + 0.3F}, 0, ecs::kInvalidEntity), construction::PlacementRefusal::NotBuildableGround)
		<< "where the slide would have put it";
	placement->setConstructionWorld(nullptr);
	assets::ConstructionRegistry::Get().clear();
}

// Two crates crafted in one tick are given targets that keep off each other: a target handed out is held in the placed
// item index before its crate is spawned.
TEST_F(PlacementFordTest, TwoInstallTargetsInOneTickKeepApart) {
	loadTestPartsBox();
	const glm::vec2				   station{10.0F, kCreekY + 6.0F};
	const std::optional<glm::vec2> first = placement->installTargetNear("TestPartsBox", station, {1.0F, 0.0F});
	const std::optional<glm::vec2> second = placement->installTargetNear("TestPartsBox", station, {1.0F, 0.0F});
	ASSERT_TRUE(first.has_value());
	ASSERT_TRUE(second.has_value());
	const glm::vec2 apart = glm::abs(*second - *first);
	EXPECT_TRUE(apart.x >= 1.2F - 0.002F || apart.y >= 0.6F - 0.002F)
		<< "overlapping targets (" << first->x << ", " << first->y << ") and (" << second->x << ", " << second->y << ")";
}


// Press fixes the position; past the click distance the front follows the pointer, snapped to the step; release places.
TEST_F(PlacementFordTest, PressAndDragTurnsTheFrontToThePointer) {
	loadTestPartsBox();
	placement->cancel();
	placement->selectBuildItem("TestPartsBox");
	const glm::vec2 pivot{10.0F, kCreekY + 3.0F};
	const glm::vec2 pressAt = seen.screenOf(pivot);
	ASSERT_TRUE(placement->handlePress(pressAt.x, pressAt.y, kViewportWidth, kViewportHeight));
	placement->handleMouseMove(pressAt.x + 2.0F, pressAt.y, kViewportWidth, kViewportHeight, false);
	EXPECT_EQ(placement->ghostTurnQuanta(), 0U) << "within the click distance";

	const glm::vec2 rightAndALittleDown = seen.screenOf(pivot + glm::vec2{2.0F, 0.2F});
	placement->handleMouseMove(rightAndALittleDown.x, rightAndALittleDown.y, kViewportWidth, kViewportHeight, false);
	EXPECT_EQ(placement->ghostTurnQuanta(), assets::turnQuantaFromDegrees(270.0)) << "front to the right, snapped";
	EXPECT_LT(glm::distance(glm::vec2{placement->ghostPosition().x, placement->ghostPosition().y}, pivot), 0.01F) << "stays at the press";
	placement->handleMouseMove(rightAndALittleDown.x, rightAndALittleDown.y, kViewportWidth, kViewportHeight, true);
	EXPECT_NE(placement->ghostTurnQuanta(), assets::turnQuantaFromDegrees(270.0)) << "Alt: the raw direction";

	ASSERT_TRUE(placement->handleRelease());
	const ecs::Rotation* rotation = placedRotation(seen.world, "TestPartsBox");
	ASSERT_NE(rotation, nullptr);
	EXPECT_NEAR(static_cast<double>(assets::turnQuantaFromRadians(rotation->radians)) / 100.0, 275.7, 0.1);
}

// Relocation starts from the item's own turn, and the turn given goes onto the crate's target.
TEST_F(PlacementFordTest, MovingATurnedItemStartsAtItsTurnAndCarriesTheNewOne) {
	loadTestPartsBox();
	placement->cancel();
	const ecs::EntityID item = seen.world.createEntity();
	seen.world.addComponent<ecs::Position>(item, ecs::Position{{10.0F, kCreekY + 6.0F}});
	seen.world.addComponent<ecs::Rotation>(item, ecs::Rotation{assets::turnRadiansFromQuanta(assets::turnQuantaFromDegrees(45.0))});
	seen.world.addComponent<ecs::Packaged>(item, ecs::Packaged{std::nullopt, false});
	placement->beginRelocation(item, "TestPartsBox");
	EXPECT_EQ(placement->ghostTurnQuanta(), assets::turnQuantaFromDegrees(45.0));

	hover({10.0F, kCreekY + 3.0F});
	EXPECT_TRUE(placement->handleTurnKey(1, false));
	ASSERT_TRUE(placement->handleRelease());
	const ecs::Packaged* packaged = seen.world.getComponent<ecs::Packaged>(item);
	ASSERT_TRUE(packaged->targetPosition.has_value());
	ASSERT_TRUE(packaged->targetRotation.has_value()) << "set with the position";
	EXPECT_EQ(assets::turnQuantaFromRadians(*packaged->targetRotation), assets::turnQuantaFromDegrees(60.0));
}

// A crafted crate is installed off the creek: beside a station on the bank, aimed into the creek, the
// target is a spot its footprint takes.
TEST_F(PlacementFordTest, InstallTargetBesideAStationOnTheBankIsOffTheCreek) {
	const glm::vec2				   station{10.0F, kCreekY - kCreekHalfWidthMeters - 0.5F};
	const glm::vec2				   intoTheCreek{0.0F, 1.0F};
	const std::optional<glm::vec2> anyGround = navigation->findValidPositionNear(station, 1.0F, intoTheCreek);
	ASSERT_TRUE(anyGround.has_value());
	ASSERT_FALSE(navigation->isPointBuildableNow(*anyGround)) << "clear ground alone puts it in the creek";

	const std::optional<glm::vec2> target = placement->installTargetNear(kBox, station, intoTheCreek);
	ASSERT_TRUE(target.has_value());
	EXPECT_TRUE(placement->footprintBuildableNow(kBox, {target->x, target->y}, 0));
	EXPECT_GE(glm::distance(*target, station), 0.99F);
}

// With no spot near the station that the footprint takes, the target is unset and the player places it.
TEST_F(PlacementFordTest, InstallTargetIsUnsetWhenNoSpotTakesTheFootprint) {
	assets::AssetDefinition wide;
	wide.defName = kWideBox;
	wide.setCollision(0, {.type = assets::CollisionShapeType::Rect, .halfExtentsMeters = {3.0F, 3.0F}});
	assets::AssetRegistry::Get().registerTestDefinition(wide);
	EXPECT_FALSE(placement->installTargetNear(kWideBox, {10.0F, kCreekY - 1.0F}, {0.0F, 1.0F}).has_value());
}

namespace {

	// Furniture is never set up where a foundation is still to be built: an L-shaped unbuilt site on the
	// south bank of the creek, its notch cut from the north-east corner.
	class PlacementUnbuiltSiteTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			assets::AssetDefinition box;
			box.defName = kBox;
			box.setCollision(0, {.type = assets::CollisionShapeType::Rect, .halfExtentsMeters = {kBoxHalfExtent, kBoxHalfExtent}});
			assets::AssetRegistry::Get().registerTestDefinition(box);

			navigation = &world_sim::testing::meshCreekWorld(seen.world, emptyPlacement);
			placement = std::make_unique<world_sim::PlacementSystem>(world_sim::PlacementSystem::Args{
				.world = &seen.world,
				.camera = &seen.camera,
				.placementExecutor = nullptr,
				.navigation = navigation,
				.worldSeed = 7,
				.callbacks = {},
				.insideUnbuiltSite = [this](glm::vec2 point) { return ecs::anyFootprintContains(unbuiltSites, point); }
			});
			placement->selectBuildItem(kBox);
		}
		void TearDown() override { assets::AssetRegistry::Get().clearDefinitions(); }

		void hover(glm::vec2 meters) {
			const glm::vec2 screen = seen.screenOf(meters);
			placement->handleMouseMove(screen.x, screen.y, kViewportWidth, kViewportHeight, false);
		}

		world_sim::testing::SeenWorld				seen;
		ecs::EmptyPlacement							emptyPlacement;
		ecs::NavigationSystem*						navigation = nullptr;
		std::unique_ptr<world_sim::PlacementSystem> placement;
		std::vector<ecs::FootprintBounds>			unbuiltSites{
			   ecs::FootprintBounds{geometry::Ring{{2000, 6000}, {10000, 6000}, {10000, 10000}, {6000, 10000}, {6000, 14000}, {2000, 14000}}}
		};
	};

} // namespace

TEST_F(PlacementUnbuiltSiteTest, TargetInsideTheSiteIsRefusedAndOutsideItIsPlaced) {
	const glm::vec2 inside{4.0F, 8.0F};
	ASSERT_TRUE(navigation->isPointBuildableNow(inside)) << "bare ground: only the site rules it out";
	hover(inside);
	EXPECT_FALSE(placement->ghostValid());
	EXPECT_FALSE(placement->handleRelease());
	EXPECT_EQ(placement->state(), world_sim::PlacementState::Placing);

	hover({4.0F, 17.0F});
	EXPECT_TRUE(placement->ghostValid());
	EXPECT_TRUE(placement->handleRelease());
}

// The site is its polygon: the notch of the L is inside its bounding box and takes furniture.
TEST_F(PlacementUnbuiltSiteTest, TargetInTheNotchOfTheLIsPlaced) {
	const glm::vec2 inNotch{8.0F, 12.0F};
	const auto&		site = unbuiltSites.front();
	ASSERT_TRUE(inNotch.x > site.min.x && inNotch.x < site.max.x && inNotch.y > site.min.y && inNotch.y < site.max.y);
	ASSERT_FALSE(site.contains(inNotch));
	hover(inNotch);
	EXPECT_TRUE(placement->ghostValid());
}

// A crafted crate's install target keeps off the site, and the footprint gate refuses inside it.
TEST_F(PlacementUnbuiltSiteTest, InstallTargetSearchKeepsOffTheSite) {
	const glm::vec2				   station{1.0F, 8.0F};
	const std::optional<glm::vec2> target = placement->installTargetNear(kBox, station, {1.0F, 0.0F});
	ASSERT_TRUE(target.has_value());
	EXPECT_FALSE(unbuiltSites.front().contains(*target));
	EXPECT_GE(glm::distance(*target, station), 0.99F);
	EXPECT_FALSE(placement->footprintBuildableNow(kBox, {4.0F, 8.0F}, 0));
}
