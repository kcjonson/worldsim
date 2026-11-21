#include "scenes/game/world/selection/SelectionSystem.h"

#include "scenes/game/world/PlayerVisibilityFixture.test.h"

#include <assets/AssetRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <assets/placement/SpatialIndex.h>
#include <construction/ConstructionWorld.h>
#include <ecs/components/AgentRadius.h>
#include <ecs/components/Appearance.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Packaged.h>
#include <ecs/components/ResourceStack.h>
#include <ecs/components/Transform.h>

#include <assets/PartsAsset.test.h>
#include <core/Vec2i64.h>
#include <predicates/Predicates.h>

#include <gtest/gtest.h>

#include <algorithm>

#include <unordered_map>
#include <memory>
#include <variant>
#include <vector>

namespace {

	using world_sim::testing::kSeenPoint;
	using world_sim::testing::kUnseenPoint;
	using world_sim::testing::kViewportHeight;
	using world_sim::testing::kViewportWidth;

	constexpr const char* kTree = "Test_SelectableTree";
	constexpr const char* kPile = "Test_SelectablePile";

	class SelectionVisibilityTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			assets::AssetDefinition tree;
			tree.defName = kTree;
			tree.capabilities.harvestable = assets::HarvestableCapability{};
			assets::AssetRegistry::Get().registerTestDefinition(tree);

			placement = std::make_unique<assets::PlacementExecutor>(assets::AssetRegistry::Get());
			selection = std::make_unique<world_sim::SelectionSystem>(world_sim::SelectionSystem::Args{
				.world = &seen.world,
				.camera = &seen.camera,
				.placementExecutor = placement.get(),
				.constructionWorld = &construction,
				.callbacks = {}
			});
		}

		void clickAt(glm::vec2 meters) {
			selection->clearSelection();
			const glm::vec2 screen = seen.screenOf(meters);
			selection->handleClick(screen.x, screen.y, kViewportWidth, kViewportHeight);
		}

		template <typename Kind>
		[[nodiscard]] bool selected() const {
			return std::holds_alternative<Kind>(selection->current());
		}

		ecs::EntityID addPile(glm::vec2 at) {
			const ecs::EntityID pile = seen.world.createEntity();
			seen.world.addComponent<ecs::Position>(pile, ecs::Position{at});
			seen.world.addComponent<ecs::Appearance>(pile, ecs::Appearance{kPile, 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
			seen.world.addComponent<ecs::ResourceStack>(pile, ecs::ResourceStack{3});
			return pile;
		}

		void placeTrees(const std::vector<glm::vec2>& positions) {
			std::unordered_map<world::ChunkCoordinate, assets::AsyncChunkPlacementResult> byChunk;
			for (const glm::vec2 at : positions) {
				assets::AsyncChunkPlacementResult& result = byChunk[world::worldToChunk({at.x, at.y})];
				result.coord = world::worldToChunk({at.x, at.y});
				assets::PlacedEntity tree;
				tree.defName = kTree;
				tree.position = at;
				result.spatialIndex.insert(tree);
			}
			for (auto& [coord, result] : byChunk) {
				placement->storeChunkResult(std::move(result));
			}
		}

		world_sim::testing::SeenWorld				seen;
		construction::ConstructionWorld				construction;
		std::unique_ptr<assets::PlacementExecutor>	placement;
		std::unique_ptr<world_sim::SelectionSystem> selection;
	};

} // namespace

TEST_F(SelectionVisibilityTest, ColonistIsSelectableOnUnseenGround) {
	const ecs::EntityID colonist = seen.addColonist(kUnseenPoint, 5.0F);
	clickAt(kUnseenPoint);
	ASSERT_TRUE(selected<world_sim::ColonistSelection>());
	EXPECT_EQ(std::get<world_sim::ColonistSelection>(selection->current()).entityId, colonist);
}

TEST_F(SelectionVisibilityTest, PilePlacedThingAndFoundationAreSelectableOnlyWhereSeen) {
	addPile(kSeenPoint);
	addPile(kUnseenPoint);
	clickAt(kSeenPoint);
	EXPECT_TRUE(selected<world_sim::ResourcePileSelection>());
	clickAt(kUnseenPoint);
	EXPECT_TRUE(selected<world_sim::NoSelection>());

	placeTrees({{20.0F, 20.0F}, {50.0F, 20.0F}});
	clickAt({20.0F, 20.0F});
	EXPECT_TRUE(selected<world_sim::WorldEntitySelection>());
	clickAt({50.0F, 20.0F});
	EXPECT_TRUE(selected<world_sim::NoSelection>());

	construction.commitFoundation(std::vector<Foundation::Vec2>{{30.0F, -10.0F}, {60.0F, -10.0F}, {60.0F, 10.0F}, {30.0F, 10.0F}}, "Wood");
	clickAt({35.0F, 0.0F});
	EXPECT_TRUE(selected<world_sim::FoundationSelection>());
	clickAt({50.0F, 0.0F});
	EXPECT_TRUE(selected<world_sim::NoSelection>());
}

TEST_F(SelectionVisibilityTest, MoverThatIsNotAColonistIsSelectableOnlyWhileVisible) {
	seen.addColonist({0.0F, 0.0F}, 15.0F);
	const auto addMover = [this](glm::vec2 at) {
		const ecs::EntityID mover = addPile(at);
		seen.world.addComponent<ecs::AgentRadius>(mover, ecs::AgentRadius{});
		return mover;
	};
	addMover({10.0F, 0.0F});
	addMover({30.0F, 0.0F});
	seen.look();
	ASSERT_TRUE(seen.visibility.isVisible({10.0F, 0.0F}));
	ASSERT_TRUE(seen.visibility.isSeen({30.0F, 0.0F}));
	ASSERT_FALSE(seen.visibility.isVisible({30.0F, 0.0F}));
	clickAt({10.0F, 0.0F});
	EXPECT_TRUE(selected<world_sim::ResourcePileSelection>());
	clickAt({30.0F, 0.0F});
	EXPECT_TRUE(selected<world_sim::NoSelection>());
}

TEST_F(SelectionVisibilityTest, RevealTurnsTheRulesOff) {
	addPile(kUnseenPoint);
	placeTrees({{50.0F, 20.0F}});
	seen.visibility.setRevealed(true);
	clickAt(kUnseenPoint);
	EXPECT_TRUE(selected<world_sim::ResourcePileSelection>());
	clickAt({50.0F, 20.0F});
	EXPECT_TRUE(selected<world_sim::WorldEntitySelection>());
}

// ─── Placed parts items (rotatable-placed-items 15) ─────────────────────────────────────────────────────────

namespace {

	using namespace assets::parts_fixtures;

	// TestPartsBox-sized boxes on seen ground.
	class SelectionPartsTest : public PartsTest {
	  protected:
		void SetUp() override {
			PartsTest::SetUp();
			writeDef("Box", "<parts>" + block("box", -0.6F, -0.3F, 0.6F, 0.3F, 0.0F, 0.8F) + "</parts><placements>" + place("box") + "</placements>");
			ASSERT_NE(load("Box"), nullptr);
			placement = std::make_unique<assets::PlacementExecutor>(assets::AssetRegistry::Get());
			selection = std::make_unique<world_sim::SelectionSystem>(world_sim::SelectionSystem::Args{
				.world = &seen.world, .camera = &seen.camera, .placementExecutor = placement.get(), .constructionWorld = &construction, .callbacks = {}
			});
		}

		ecs::EntityID addBox(glm::vec2 at, double degrees) {
			const ecs::EntityID box = seen.world.createEntity();
			seen.world.addComponent<ecs::Position>(box, ecs::Position{at});
			seen.world.addComponent<ecs::Rotation>(box, ecs::Rotation{assets::turnRadiansFromQuanta(assets::turnQuantaFromDegrees(degrees))});
			seen.world.addComponent<ecs::Appearance>(box, ecs::Appearance{"Box", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
			return box;
		}

		// The furniture a click at `meters` selects, or 0.
		ecs::EntityID clickAt(glm::vec2 meters) {
			selection->clearSelection();
			const glm::vec2 screen = seen.screenOf(meters);
			selection->handleClick(screen.x, screen.y, kViewportWidth, kViewportHeight);
			const auto* furniture = std::get_if<world_sim::FurnitureSelection>(&selection->current());
			return furniture != nullptr ? furniture->entityId : 0;
		}

		// Whether a point lies in a box's hit region at its turn.
		static bool inHitRegion(glm::vec2 boxAt, double degrees, glm::vec2 point) {
			assets::AssetRegistry&		  registry = assets::AssetRegistry::Get();
			const assets::ItemSilhouette* item = registry.itemSilhouette(registry.templateId("Box", {}), assets::turnQuantaFromDegrees(degrees));
			const geometry::Vec2i64		  local = geometry::quantize(Foundation::Vec2{point.x - boxAt.x, point.y - boxAt.y});
			return std::any_of(item->silhouette.hitRegion.begin(), item->silhouette.hitRegion.end(), [&](const geometry::Ring& ring) {
				return geometry::pointInPolygon(local, ring) == geometry::PointInPolygon::Inside;
			});
		}

		world_sim::testing::SeenWorld				seen;
		construction::ConstructionWorld				construction;
		std::unique_ptr<assets::PlacementExecutor>	placement;
		std::unique_ptr<world_sim::SelectionSystem> selection;
	};

} // namespace

// A turned box is clicked on its turned shape, and a click beside it selects nothing.
TEST_F(SelectionPartsTest, ATurnedItemIsClickedOnItsTurnedShape) {
	const glm::vec2		at{5.0F, 5.0F};
	const ecs::EntityID box = addBox(at, 35.0);
	EXPECT_EQ(clickAt(at), box);
	EXPECT_EQ(clickAt(at + glm::vec2{1.5F, 0.0F}), 0U);
	const glm::vec2 squareCorner = at + glm::vec2{0.58F, 0.28F}; // inside the box at turn 0
	ASSERT_FALSE(inHitRegion(at, 35.0, squareCorner));
	EXPECT_EQ(clickAt(squareCorner), 0U);
}

// Where two placed boxes overlap on the screen, a click picks the one drawn in front there, not the nearer centre.
TEST_F(SelectionPartsTest, OfTwoOverlappingItemsTheOneDrawnInFrontIsPicked) {
	const glm::vec2		behindAt{5.0F, 5.0F};
	const glm::vec2		frontAt = behindAt + glm::vec2{0.6F, 0.7F};
	(void)addBox(behindAt, 35.0);
	const ecs::EntityID front = addBox(frontAt, 0.0);
	size_t				overlapping = 0;
	size_t				nearerTheBack = 0;
	for (float x = -0.5F; x <= 1.5F; x += 0.05F) {
		for (float y = -0.8F; y <= 1.2F; y += 0.05F) {
			const glm::vec2 point = behindAt + glm::vec2{x, y};
			if (!inHitRegion(behindAt, 35.0, point) || !inHitRegion(frontAt, 0.0, point)) {
				continue;
			}
			++overlapping;
			const glm::vec2 toBehind = point - behindAt;
			const glm::vec2 toFront = point - frontAt;
			nearerTheBack += (toBehind.x * toBehind.x) + (toBehind.y * toBehind.y) < (toFront.x * toFront.x) + (toFront.y * toFront.y) ? 1 : 0;
			EXPECT_EQ(clickAt(point), front) << "at (" << point.x << ", " << point.y << ")";
		}
	}
	EXPECT_GT(overlapping, 0U);
	EXPECT_GT(nearerTheBack, 0U) << "some of those clicks are nearer the box behind";
}

// A packaged item a colonist is carrying is not drawn where it was, so it isn't picked there.
TEST_F(SelectionPartsTest, ACarriedPackagedItemIsNotPicked) {
	const glm::vec2		at{5.0F, 5.0F};
	const ecs::EntityID box = addBox(at, 0.0);
	seen.world.addComponent<ecs::Packaged>(box, ecs::Packaged{});
	EXPECT_EQ(clickAt(at), box) << "packaged and on the ground";
	seen.world.getComponent<ecs::Packaged>(box)->beingCarried = true;
	EXPECT_EQ(clickAt(at), 0U);
}
