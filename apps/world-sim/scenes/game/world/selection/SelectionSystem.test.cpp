#include "scenes/game/world/selection/SelectionSystem.h"

#include "scenes/game/world/PlayerVisibilityFixture.test.h"

#include <assets/AssetRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <assets/placement/SpatialIndex.h>
#include <construction/ConstructionWorld.h>
#include <ecs/components/AgentRadius.h>
#include <ecs/components/Appearance.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/ResourceStack.h>

#include <gtest/gtest.h>

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
