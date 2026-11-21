#include "ItemClearance.h"

#include "ConstructionWorld.h"
#include "SnapTestFixtures.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/PartsAsset.h>
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

	// Rotatable-placed-items 13 and 20: a bench 1.6 m by 0.5 m keeping 0.6 m clear in front of it (+y at turn 0), and
	// a box 0.6 m square with no clearance; a Wood Standard wall (0.2 m thick) from the shipped construction config.
	class ItemClearanceTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			registerParts(kBench, rect(-0.8F, -0.25F, 0.8F, 0.25F), rect(-0.8F, 0.25F, 0.8F, 0.85F));
			registerParts(kBox, rect(-0.3F, -0.3F, 0.3F, 0.3F), {});
			assets::ConstructionRegistry::Get().clear();
			std::filesystem::path folder = __FILE__;
			folder = folder.parent_path().parent_path().parent_path().parent_path() / "assets" / "config" / "construction";
			ASSERT_TRUE(assets::ConstructionRegistry::Get().load(folder.string()));
		}
		void TearDown() override {
			AssetRegistry::Get().clearDefinitions();
			assets::ConstructionRegistry::Get().clear();
		}

		ecs::EntityID install(const char* defName, glm::vec2 at, float turnRadians = 0.0F) {
			const ecs::EntityID entity = world.createEntity();
			world.addComponent<ecs::Position>(entity, ecs::Position{at});
			world.addComponent<ecs::Rotation>(entity, ecs::Rotation{turnRadians});
			world.addComponent<ecs::Appearance>(entity, ecs::Appearance{defName});
			items.refresh(world, AssetRegistry::Get());
			return entity;
		}

		[[nodiscard]] ecs::ItemOutlines held(const char* defName, glm::vec2 at, float turnRadians = 0.0F) const {
			return ecs::placedOutlines(items.outlinesOf(defName, AssetRegistry::Get()), at, turnRadians);
		}

		[[nodiscard]] std::optional<PlacementRefusal> refusal(const char* defName, glm::vec2 at, float turnRadians = 0.0F) const {
			return itemsRefusal(items, &walls, held(defName, at, turnRadians), ecs::kInvalidEntity);
		}

		// A Wood Standard wall, 0.2 m thick, along y = `centreY` from x = 0 to x = 20.
		void wallAlong(float centreY) {
			ASSERT_TRUE(walls.commitSegment(millimeters(0.0, centreY), millimeters(20.0, centreY), "Wood", "Standard", kInvalidFoundation, 0).ok());
		}

		ecs::World			 world;
		ecs::PlacedItemIndex items;
		ConstructionWorld	 walls;
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

// A body may not enter another item's clearance, whichever was placed first; a box may sit flush against a box.
TEST_F(ItemClearanceTest, ABodyIntoAClearanceIsRefusedBothWays) {
	install(kBench, {10.0F, 10.0F});
	EXPECT_EQ(refusal(kBox, {10.0F, 10.7F}), PlacementRefusal::TooClose) << "the box in front of the bench";
	EXPECT_EQ(refusal(kBox, {10.0F, 11.15F}), std::nullopt) << "past the clearance, flush with its edge";
	EXPECT_EQ(refusal(kBox, {10.0F, 9.45F}), std::nullopt) << "flush against the bench's back, which keeps nothing clear";
	EXPECT_EQ(refusal(kBox, {10.0F, 9.6F}), PlacementRefusal::OverlapsItem);

	ecs::World			 other;
	ecs::PlacedItemIndex otherItems;
	const ecs::EntityID	 box = other.createEntity();
	other.addComponent<ecs::Position>(box, ecs::Position{{20.0F, 20.7F}});
	other.addComponent<ecs::Rotation>(box, ecs::Rotation{0.0F});
	other.addComponent<ecs::Appearance>(box, ecs::Appearance{kBox});
	otherItems.refresh(other, AssetRegistry::Get());
	EXPECT_EQ(itemsRefusal(otherItems, nullptr, held(kBench, {20.0F, 20.0F}), ecs::kInvalidEntity), PlacementRefusal::TooClose)
		<< "the bench placed with the box in its clearance";
	EXPECT_EQ(itemsRefusal(otherItems, nullptr, held(kBench, {20.0F, 20.0F}, kPi), ecs::kInvalidEntity), std::nullopt)
		<< "turned round, its clearance is behind it";

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
	items.refresh(world, AssetRegistry::Get());
	EXPECT_EQ(refusal(kBox, {2.0F, 2.0F}), std::nullopt);

	world.getComponent<ecs::Packaged>(crate)->targetPosition = glm::vec2{30.0F, 30.0F};
	items.refresh(world, AssetRegistry::Get());
	EXPECT_EQ(refusal(kBox, {30.2F, 30.0F}), PlacementRefusal::OverlapsItem);
	EXPECT_EQ(itemsRefusal(items, nullptr, held(kBox, {30.2F, 30.0F}), crate), std::nullopt) << "its own target";
}

// A wall's band, not its centreline, is tested against reserved regions, both ways: a wall whose centreline clears a
// bench's clearance but whose band meets it is refused, and so is the bench placed after the wall. An item backed
// onto the wall face is allowed.
TEST_F(ItemClearanceTest, AWallsBandIsTestedBothWays) {
	install(kBench, {10.0F, 10.0F});
	// The clearance's front edge is at y = 10.85; a centreline 0.05 m past it puts the band 0.05 m into it.
	EXPECT_TRUE(bandMeetsItems(items, bandRing({5.0F, 10.9F}, {15.0F, 10.9F}, 0.1F)));
	EXPECT_FALSE(bandMeetsItems(items, bandRing({5.0F, 10.95F}, {15.0F, 10.95F}, 0.1F))) << "the band's face on the clearance's edge";
	EXPECT_TRUE(bandMeetsItems(items, bandRing({10.0F, 5.0F}, {10.0F, 15.0F}, 0.1F))) << "through the body";

	wallAlong(20.9F);
	EXPECT_EQ(refusal(kBench, {10.0F, 20.0F}), PlacementRefusal::TooClose) << "the bench placed after the wall";
	EXPECT_EQ(refusal(kBench, {10.0F, 21.25F}), std::nullopt) << "backed onto the wall's face, its clearance away from it";
}
