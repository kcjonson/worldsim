#include "ItemClearance.h"

#include "ConstructionWorld.h"
#include "SnapTestFixtures.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/PartsAsset.h>
#include <ecs/ColliderEntityIndex.h>
#include <ecs/World.h>
#include <ecs/components/Appearance.h>
#include <ecs/components/Packaged.h>
#include <ecs/components/Transform.h>

#include <glm/geometric.hpp>

#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <numbers>
#include <vector>

using namespace construction;
using assets::AssetDefinition;
using assets::AssetRegistry;
using snap_test::millimeters;

namespace {

	constexpr const char* kBench = "Test_ClearanceBench";
	constexpr const char* kBox = "Test_ClearanceBox";
	constexpr const char* kLetterL = "Test_ClearanceL";
	constexpr float		  kPi = std::numbers::pi_v<float>;

	std::vector<glm::vec2> rect(float minX, float minY, float maxX, float maxY) {
		return {{minX, minY}, {maxX, minY}, {maxX, maxY}, {minX, maxY}};
	}

	void registerParts(const char* defName, const std::vector<glm::vec2>& collider, const std::vector<glm::vec2>& clearance) {
		AssetDefinition def;
		def.defName = defName;
		def.assetType = assets::AssetType::Parts;
		assets::PartsAsset parts{.bodyTopMeters = 0.9F};
		if (!clearance.empty()) {
			parts.clearance.polygons.push_back({.outer = clearance});
		}
		def.parts = std::make_shared<const assets::PartsAsset>(std::move(parts));
		def.partsColliders.push_back({.type = assets::CollisionShapeType::Polygon, .pointsMeters = collider});
		AssetRegistry::Get().registerTestDefinition(def);
	}

	// Rotatable-placed-items 13 and 20: a bench 1.6 m by 0.5 m keeping 0.6 m clear in front of it (+y at turn 0), a box
	// 0.6 m square and an L 1 m across with arms 0.4 m wide, neither with clearance; Wood Standard walls (0.2 m thick)
	// from the shipped construction config.
	class ItemClearanceTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			registerParts(kBench, rect(-0.8F, -0.25F, 0.8F, 0.25F), rect(-0.8F, 0.25F, 0.8F, 0.85F));
			registerParts(kBox, rect(-0.3F, -0.3F, 0.3F, 0.3F), {});
			registerParts(kLetterL, {{-0.5F, -0.5F}, {0.5F, -0.5F}, {0.5F, -0.1F}, {-0.1F, -0.1F}, {-0.1F, 0.5F}, {-0.5F, 0.5F}}, {});
			assets::ConstructionRegistry::Get().clear();
			std::filesystem::path folder = __FILE__;
			folder = folder.parent_path().parent_path().parent_path().parent_path() / "assets" / "config" / "construction";
			ASSERT_TRUE(assets::ConstructionRegistry::Get().load(folder.string()));
		}
		void TearDown() override {
			AssetRegistry::Get().clearDefinitions();
			assets::ConstructionRegistry::Get().clear();
		}

		void refresh() {
			static_cast<void>(obstacles.refresh(world, AssetRegistry::Get()));
			items.refresh(obstacles, world, AssetRegistry::Get());
		}

		ecs::EntityID install(const char* defName, glm::vec2 at, float turnRadians = 0.0F) {
			const ecs::EntityID entity = world.createEntity();
			world.addComponent<ecs::Position>(entity, ecs::Position{at});
			world.addComponent<ecs::Rotation>(entity, ecs::Rotation{turnRadians});
			world.addComponent<ecs::Appearance>(entity, ecs::Appearance{defName});
			refresh();
			return entity;
		}

		[[nodiscard]] ecs::ItemOutlines held(const char* defName, glm::vec2 at, float turnRadians = 0.0F) const {
			return ecs::placedOutlines(items.outlinesOf(defName, AssetRegistry::Get()), at, turnRadians);
		}

		// The body rules, then the clearance rule, against the items and the walls.
		[[nodiscard]] std::optional<PlacementRefusal> refusal(const char* defName, glm::vec2 at, float turnRadians = 0.0F) const {
			const std::vector<std::vector<glm::vec2>> wallRings = wallOutlines(walls);
			const ecs::ItemOutlines					  outlines = held(defName, at, turnRadians);
			if (std::optional<PlacementRefusal> body = bodyRefusal(items, wallRings, outlines, ecs::kInvalidEntity)) {
				return body;
			}
			return clearanceRefusal(items, wallRings, outlines, ecs::kInvalidEntity);
		}

		void wall(glm::vec2 start, glm::vec2 end) {
			ASSERT_TRUE(walls.commitSegment(millimeters(start.x, start.y), millimeters(end.x, end.y), "Wood", "Standard", kInvalidFoundation, 0).ok());
		}

		ecs::World				 world;
		ecs::ColliderEntityIndex obstacles;
		ecs::PlacedItemIndex	 items;
		ConstructionWorld		 walls;
	};

} // namespace

// Shapes meeting along an edge don't meet; sharing ground past the tolerance they do. A box turned 45 degrees and set
// against another's edge by float arithmetic is flush, not overlapping.
TEST_F(ItemClearanceTest, FlushShapesDoNotMeetAndOverlappingOnesDo) {
	EXPECT_FALSE(ringsMeet(rect(0.0F, 0.0F, 1.0F, 1.0F), rect(1.0F, 0.0F, 2.0F, 1.0F)));
	EXPECT_FALSE(ringsMeet(rect(0.0F, 0.0F, 1.0F, 1.0F), rect(1.0F, 1.0F, 2.0F, 2.0F))) << "a corner";
	EXPECT_TRUE(ringsMeet(rect(0.0F, 0.0F, 1.0F, 1.0F), rect(0.99F, 0.0F, 2.0F, 1.0F)));
	EXPECT_TRUE(ringsMeet(rect(0.0F, 0.0F, 1.0F, 1.0F), rect(0.2F, 0.2F, 0.8F, 0.8F))) << "inside";

	const glm::vec2 corner{31.234F, 27.71F};
	const float		turn = kPi / 4.0F;
	const glm::vec2 along{std::cos(turn), std::sin(turn)};
	const glm::vec2 across{-along.y, along.x};
	const auto		turnedSquare = [&](glm::vec2 origin) {
		 return std::vector<glm::vec2>{origin, origin + along * 0.6F, origin + along * 0.6F + across * 0.6F, origin + across * 0.6F};
	};
	EXPECT_FALSE(ringsMeet(turnedSquare(corner), turnedSquare(corner + along * 0.6F))) << "turned, flush";
	EXPECT_TRUE(ringsMeet(turnedSquare(corner), turnedSquare(corner + along * 0.55F))) << "turned, 5 cm in";
}

// Two points that round to one micrometre are one point, so the ring still tests as it should; a ring that rounds to
// fewer than three points meets anything its box meets, never nothing.
TEST_F(ItemClearanceTest, ARingDegenerateInMicrometresFailsClosed) {
	std::vector<glm::vec2> doubled = rect(0.0F, 0.0F, 1.0F, 1.0F);
	doubled.insert(doubled.begin() + 1, glm::vec2{0.0000003F, 0.0F});
	EXPECT_FALSE(ringsMeet(doubled, rect(1.0F, 0.0F, 2.0F, 1.0F))) << "a repeat point dropped, still flush";
	EXPECT_TRUE(ringsMeet(doubled, rect(0.5F, 0.0F, 2.0F, 1.0F)));

	const std::vector<glm::vec2> sliver{{0.5F, 0.5F}, {0.5000004F, 0.5F}, {0.5000002F, 0.5000003F}};
	EXPECT_TRUE(ringsMeet(sliver, rect(0.0F, 0.0F, 1.0F, 1.0F)));
	EXPECT_TRUE(ringsMeet(rect(0.0F, 0.0F, 1.0F, 1.0F), sliver));
	EXPECT_FALSE(ringsMeet(sliver, rect(3.0F, 3.0F, 4.0F, 4.0F))) << "nowhere near";
}

// A concave collider is taken in like any other: an L flush against a box, along its outer edge and inside its notch,
// is accepted, and one 3 mm into it refused.
TEST_F(ItemClearanceTest, AConcaveColliderSitsFlush) {
	install(kBox, {10.0F, 10.0F});
	EXPECT_EQ(refusal(kLetterL, {10.8F, 10.0F}), std::nullopt) << "the L's left edge on the box's right";
	EXPECT_EQ(refusal(kLetterL, {10.797F, 10.0F}), PlacementRefusal::OverlapsItem) << "3 mm in";
	// The box in the notch: the notch's walls at local x = -0.1 and y = -0.1, the box's 0.3 m from its centre.
	EXPECT_EQ(refusal(kLetterL, {9.8F, 9.8F}), std::nullopt) << "the box in the notch, both its walls flush";
	EXPECT_EQ(refusal(kLetterL, {9.803F, 9.8F}), PlacementRefusal::OverlapsItem);
}

// A body may not enter another item's clearance, whichever was placed first; a box may sit flush against a box.
TEST_F(ItemClearanceTest, ABodyIntoAClearanceIsRefusedBothWays) {
	install(kBench, {10.0F, 10.0F});
	EXPECT_EQ(refusal(kBox, {10.0F, 10.7F}), PlacementRefusal::TooClose) << "the box in front of the bench";
	EXPECT_EQ(refusal(kBox, {10.0F, 11.15F}), std::nullopt) << "past the clearance, flush with its edge";
	EXPECT_EQ(refusal(kBox, {10.0F, 9.45F}), std::nullopt) << "flush against the bench's back, which keeps nothing clear";
	EXPECT_EQ(refusal(kBox, {10.0F, 9.6F}), PlacementRefusal::OverlapsItem);

	install(kBox, {20.0F, 20.7F});
	EXPECT_EQ(refusal(kBench, {20.0F, 20.0F}), PlacementRefusal::TooClose) << "the bench placed with the box in its clearance";
	EXPECT_EQ(refusal(kBench, {20.0F, 20.0F}, kPi), std::nullopt) << "turned round, its clearance is behind it";

	install(kBox, {14.0F, 10.0F});
	EXPECT_EQ(refusal(kBox, {14.6F, 10.0F}), std::nullopt) << "box against box";
}

// Two benches facing each other 1.0 m apart share the aisle their clearances overlap in.
TEST_F(ItemClearanceTest, ClearancesMayOverlap) {
	install(kBench, {10.0F, 10.0F});
	EXPECT_EQ(refusal(kBench, {10.0F, 11.5F}, kPi), std::nullopt);
	EXPECT_EQ(refusal(kBench, {10.0F, 10.9F}, kPi), PlacementRefusal::TooClose) << "0.4 m apart, each body in the other's clearance";
}

// A crate's install target is taken like an installed item; a crate with none stands nowhere; the item being moved
// is not tested against its own target.
TEST_F(ItemClearanceTest, APendingTargetRefusesLikeAnInstalledItem) {
	const ecs::EntityID crate = world.createEntity();
	world.addComponent<ecs::Position>(crate, ecs::Position{{2.0F, 2.0F}});
	world.addComponent<ecs::Appearance>(crate, ecs::Appearance{kBox});
	world.addComponent<ecs::Packaged>(crate, ecs::Packaged{});
	refresh();
	EXPECT_EQ(refusal(kBox, {2.0F, 2.0F}), std::nullopt);

	world.getComponent<ecs::Packaged>(crate)->targetPosition = glm::vec2{30.0F, 30.0F};
	refresh();
	EXPECT_EQ(refusal(kBox, {30.2F, 30.0F}), PlacementRefusal::OverlapsItem);
	EXPECT_EQ(bodyRefusal(items, {}, held(kBox, {30.2F, 30.0F}), crate), std::nullopt) << "its own target";
}

// A body across a wall is "A wall is in the way"; a clearance into a wall is "Too close". A wall's band, not its
// centreline, is tested, both ways; an item backed onto the face is allowed.
TEST_F(ItemClearanceTest, AWallIsTestedByItsBandBothWays) {
	install(kBench, {10.0F, 10.0F});
	// The clearance's front edge is at y = 10.85; a centreline 0.05 m past it puts the band 0.05 m into it.
	const auto drawn = [&](glm::vec2 start, glm::vec2 end) { return wallMeetsItems(items, drawnWallOutline(walls, {start, end}, 100)); };
	EXPECT_TRUE(drawn({5.0F, 10.9F}, {15.0F, 10.9F}));
	EXPECT_FALSE(drawn({5.0F, 10.95F}, {15.0F, 10.95F})) << "its face on the clearance's edge";
	EXPECT_TRUE(drawn({10.0F, 5.0F}, {10.0F, 15.0F})) << "through the body";

	wall({0.0F, 20.9F}, {20.0F, 20.9F});
	EXPECT_EQ(refusal(kBench, {10.0F, 20.0F}), PlacementRefusal::TooClose) << "the bench placed after the wall, its clearance in the band";
	EXPECT_EQ(refusal(kBench, {10.0F, 21.25F}), std::nullopt) << "backed onto the wall's face, its clearance away from it";
	EXPECT_EQ(refusal(kBox, {10.0F, 20.9F}), PlacementRefusal::WallInTheWay) << "a body across the wall";
}

// At an L joint the junction fills the outer corner square: an item there meets the wall whichever came first, the
// wall drawn to the joint resolved with the junction it will have.
TEST_F(ItemClearanceTest, AWallsOuterCornerIsWall) {
	wall({0.0F, 0.0F}, {10.0F, 0.0F});
	// The outer corner square runs from (10, -0.1) to (10.1, 0); a box with its corner 5 cm into it.
	const glm::vec2 inCorner{10.35F - 0.05F, -0.35F + 0.05F};
	const ecs::EntityID cornerBox = install(kBox, inCorner);
	EXPECT_TRUE(wallMeetsItems(items, drawnWallOutline(walls, {{10.0F, 0.0F}, {10.0F, 10.0F}}, 100))) << "the wall drawn after the box";
	EXPECT_FALSE(wallMeetsItems(items, drawnWallOutline(walls, {{10.0F, 1.0F}, {10.0F, 10.0F}}, 100))) << "a wall clear of the joint";

	world.destroyEntity(cornerBox);
	refresh();
	wall({10.0F, 0.0F}, {10.0F, 10.0F});
	EXPECT_EQ(refusal(kBox, inCorner), PlacementRefusal::WallInTheWay) << "the box placed after the wall";
	EXPECT_EQ(refusal(kBox, {10.4F, -0.4F}), std::nullopt) << "clear of the corner";
}

// At a 30 degree joint the miter reaches far past the joint (about 3.9 half thicknesses): an item in its tip meets the
// wall whichever came first, and one past the tip meets nothing.
TEST_F(ItemClearanceTest, AnAcuteJointsMiterIsWallBothWays) {
	wall({0.0F, 0.0F}, {10.0F, 0.0F});
	const glm::vec2 back{10.0F - 8.660254F, 5.0F};
	// The miter's tip lies near (10.37, -0.1); a box whose left edge reaches 10.25 there, and one whose reaches 10.55.
	const glm::vec2 inTip{10.55F, -0.1F};
	const glm::vec2 pastTip{10.85F, -0.1F};
	install(kBox, inTip);
	EXPECT_TRUE(wallMeetsItems(items, drawnWallOutline(walls, {{10.0F, 0.0F}, back}, 100))) << "drawn after the box";

	const ecs::PlacedItemIndex noItems;
	wall({10.0F, 0.0F}, back);
	EXPECT_EQ(bodyRefusal(noItems, wallOutlines(walls), held(kBox, inTip), ecs::kInvalidEntity), PlacementRefusal::WallInTheWay)
		<< "the box placed after the wall";
	EXPECT_EQ(bodyRefusal(noItems, wallOutlines(walls), held(kBox, pastTip), ecs::kInvalidEntity), std::nullopt) << "past the tip";
}

// The flush tolerance is the same in either order: a box turned 45 degrees with its corner 2.5 mm into a square box's
// edge meets it placed first or second, and with its corner 2.3 mm in (under the 2.41 mm a right corner reaches) meets
// it neither way.
TEST_F(ItemClearanceTest, TheToleranceIsTheSameInEitherOrder) {
	const auto turnedBoxDown = [](float into) {
		const float		half = 0.3F * std::numbers::sqrt2_v<float>;
		const glm::vec2 apex{0.5F, 1.0F - into};
		return std::vector<glm::vec2>{apex, apex + glm::vec2{half, half}, apex + glm::vec2{0.0F, 2.0F * half}, apex + glm::vec2{-half, half}};
	};
	const std::vector<glm::vec2> square = rect(0.0F, 0.0F, 1.0F, 1.0F);
	EXPECT_TRUE(ringsMeet(turnedBoxDown(0.0025F), square));
	EXPECT_TRUE(ringsMeet(square, turnedBoxDown(0.0025F)));
	EXPECT_FALSE(ringsMeet(turnedBoxDown(0.0023F), square));
	EXPECT_FALSE(ringsMeet(square, turnedBoxDown(0.0023F)));
}
