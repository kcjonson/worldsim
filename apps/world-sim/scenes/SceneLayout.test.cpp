// The main screens laid out at the window sizes and interface sizes the players have, linted clean, with a scrollbar
// only on what is meant to scroll. The splash and the loading scene aren't here: their updates bake the ground tiles
// and load the planet, which need the GPU and the quickstart planet. The game scene needs both too, so its HUD and
// dialogs are the game's GameUI over a small world of their own.

#include "NewGameSetup.h"
#include "SceneTesting.test.h"
#include "scenes/game/ui/GameUI.h"
#include "scenes/game/world/placement/PlacementSystem.h"

#include "GameStartConfig.h"
#include <ecs/components/Colonist.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Needs.h>
#include <ecs/components/Task.h>
#include <ecs/components/Transform.h>
#include <theme/Theme.h>
#include <world/chunk/SyntheticWorldSampler.h>

#include <functional>
#include <memory>
#include <utility>

namespace world_sim::testing {

	namespace {

		constexpr const char* kHudScene = "hud";

		// What GameScene shows over the world, fed as GameScene feeds it, over a world of one colonist, a crafting
		// spot and a box. `open` opens a dialog on them once it's built.
		class HudScene final : public app::Scene {
		  public:
			struct Entities {
				ecs::EntityID colonist{0};
				ecs::EntityID station{0};
				ecs::EntityID box{0};
			};
			using Open = std::function<void(GameUI&, const Entities&)>;

			explicit HudScene(Open open)
				: open(std::move(open)) {}

			[[nodiscard]] const char* name() const override { return kHudScene; }

			void onEnter(app::App& /*app*/) override {
				theme::apply(ui());
				gameUI = std::make_unique<GameUI>(GameUI::Args{.root = root()});

				std::vector<std::pair<std::string, std::string>> productionItems;
				for (const assets::RecipeDef* recipe : assets::RecipeRegistry::Get().getInnateRecipes()) {
					if (recipe->isStationless() && !recipe->outputs.empty()) {
						productionItems.emplace_back(recipe->outputs[0].defName, recipe->label);
					}
				}
				gameUI->setProductionItems(productionItems);
				std::vector<std::pair<std::string, float>> materials;
				for (const auto& [name, material] : assets::ConstructionRegistry::Get().getAllMaterials()) {
					materials.emplace_back(name, material.costRatePerSquareMeter);
				}
				gameUI->setConstructionMaterials(materials);
				gameUI->setMinimapContext({0.0F, 0.0F}, 45.0, 10.0, 8.0F);

				world.registerSystem<ecs::TimeSystem>();
				entities.colonist = world.createEntity();
				world.addComponent<ecs::Position>(entities.colonist, ecs::Position{{0.0F, 0.0F}});
				world.addComponent<ecs::Colonist>(entities.colonist, ecs::Colonist{"Ada Lovelace"});
				world.addComponent<ecs::NeedsComponent>(entities.colonist, ecs::NeedsComponent::createDefault());
				world.addComponent<ecs::Inventory>(entities.colonist, ecs::Inventory::createForColonist());
				world.addComponent<ecs::Memory>(entities.colonist, ecs::Memory{.owner = entities.colonist});
				world.addComponent<ecs::Task>(entities.colonist, ecs::Task{});
				PlacementSystem placement(PlacementSystem::Args{.world = &world, .camera = &camera});
				entities.station = placement.spawnEntity("CraftingSpot", {4.0F, 0.0F});
				entities.box = placement.spawnEntity("BasicBox", {-4.0F, 0.0F});

				if (open) {
					open(*gameUI, entities);
				}
			}

			void update(const app::FrameContext& frame) override {
				gameUI->update(
					static_cast<float>(frame.dt),
					camera,
					chunks,
					world,
					assets::AssetRegistry::Get(),
					assets::RecipeRegistry::Get(),
					ColonistSelection{.entityId = entities.colonist}
				);
			}

			void render(float /*alpha*/) override {}

			void onExit() override { gameUI.reset(); }

		  private:
			Open					open;
			ecs::World				world;
			world::WorldCamera		camera;
			world::ChunkManager		chunks{std::make_unique<world::SyntheticWorldSampler>(7U, world::SyntheticMap{})};
			Entities				entities;
			std::unique_ptr<GameUI> gameUI;
		};

		class SceneLayout : public SceneTest {
		  protected:
			void SetUp() override {
				SceneTest::SetUp();
				// The menu's backdrop and Quick Start read the shipped planet; here, as on CI, there is none.
				ASSERT_FALSE(Foundation::findResource(kQuickstartPlanetResource).has_value());
				NewGameSetup::Reset();
			}

			void openHud(HudScene::Open open = {}) {
				app->scenes().add(kHudScene, [open] { return std::make_unique<HudScene>(open); });
				go(kHudScene);
			}
		};

	} // namespace

	TEST_F(SceneLayout, TheMainMenuHoldsTogether) {
		go(scenes::MainMenu.name);
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({}), "");
	}

	TEST_F(SceneLayout, SettingsHoldTogetherWithOnlyTheListScrolling) {
		go(scenes::Settings.name);
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({"settings_list"}), "");
	}

	TEST_F(SceneLayout, ScenarioSelectHoldsTogether) {
		go(scenes::ScenarioSelect.name);
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({}), "");
	}

	TEST_F(SceneLayout, PartySelectHoldsTogether) {
		go(scenes::PartySelect.name);
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({}), "");
	}

	// Configuring, before a world is made: generating and reviewing need worldgen's run and the globe's GL.
	TEST_F(SceneLayout, TheWorldCreatorHoldsTogether) {
		go(scenes::WorldCreator.name);
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({}), "");
	}

	TEST_F(SceneLayout, TheHudHoldsTogether) {
		openHud();
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({}), "");
	}

	TEST_F(SceneLayout, TheColonistDossierHoldsTogetherWithOnlyItsTabScrolling) {
		openHud([](GameUI& hud, const HudScene::Entities& entities) { hud.showColonistDetails(entities.colonist); });
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({"dossier_tab_area"}), "");
	}

	TEST_F(SceneLayout, TheCraftingDialogHoldsTogether) {
		openHud([](GameUI& hud, const HudScene::Entities& entities) { hud.showCraftingDialog(entities.station, "CraftingSpot"); });
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({}), "");
	}

	TEST_F(SceneLayout, TheStorageDialogHoldsTogether) {
		openHud([](GameUI& hud, const HudScene::Entities& entities) { hud.showStorageConfigDialog(entities.box, "BasicBox"); });
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({}), "");
	}

	TEST_F(SceneLayout, ThePauseMenuHoldsTogether) {
		openHud([](GameUI& hud, const HudScene::Entities& /*entities*/) { hud.showPauseMenu(); });
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({}), "");
	}

} // namespace world_sim::testing
