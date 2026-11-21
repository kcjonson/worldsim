// The primitive effects run from action entries no factory reads: synthetic entries loaded through the real loader,
// resolved by resolveAction, and run by the ActionSystem. Each effect row kind, the order rows are written in, pays end
// against along with a curve, and the open-ended upTo forms. See /docs/technical/queries-and-chains.md (Actions).

#include "ActionSystem.h"

#include "../InventoryMass.h"
#include "../ShippedConfig.test.h"
#include "../World.h"
#include "../components/Action.h"
#include "../components/Appearance.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/Needs.h"
#include "../components/Packaged.h"
#include "../components/StructureBlueprint.h"
#include "../components/Task.h"
#include "../components/Transform.h"
#include "../components/WorkQueue.h"

#include <assets/AssetRegistry.h>
#include <assets/EntryLoader.h>
#include <assets/ShippedEntries.test.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace ecs::test {

	namespace {

		constexpr const char* kSyntheticActions = R"(<actions>
  <action id="soak" seconds="2" interruptable="false">
    <restore need="hygiene" amount="40"/>
    <restore need="thirst" amount="-10"/>
  </action>
  <action id="snack" seconds="1" interruptable="false">
    <consume capability="edible" count="2"/>
    <restore need="hunger" from="nutrition" scale="50"/>
  </action>
  <action id="snooze" seconds="4" interruptable="true" asleep="true">
    <restore need="energy" amount="40" scaleBy="sleep_quality"/>
  </action>
  <action id="soothe" seconds="1" interruptable="false">
    <restore need="hygiene" amount="30" who="target"/>
  </action>
  <action id="tidy" seconds="1" interruptable="false">
    <restore need="hygiene" amount="10"/>
    <consume capability="edible" count="1"/>
    <restore need="hunger" amount="10"/>
  </action>
  <action id="nap" seconds="4" interruptable="true">
    <restore need="energy" amount="40"/>
  </action>
  <action id="rest" seconds="4" interruptable="true" pays="along">
    <restore need="energy" amount="40"/>
  </action>
  <action id="doze" seconds="4" interruptable="true" pays="along" curve="2">
    <restore need="energy" amount="40"/>
  </action>
  <action id="unwind" interruptable="true" pays="along">
    <restore need="recreation" amount="2" seconds="60" upTo="90"/>
  </action>
  <action id="feast" interruptable="false" pays="along">
    <consume capability="edible" count="1" seconds="2" upTo="95" need="hunger"/>
    <restore need="hunger" from="nutrition" scale="100"/>
  </action>
  <action id="lift" seconds="1" interruptable="false"><take/></action>
  <action id="stow" seconds="1" interruptable="false"><put/></action>
  <action id="raise" interruptable="false" pays="along"><work rate="Construction" direction="up"/></action>
  <action id="raze" interruptable="false" pays="along"><work rate="Construction" direction="down"/></action>
  <action id="make" workFrom="job" interruptable="false"><craft/></action>
  <action id="leave_mark" seconds="1" interruptable="false"><spawn def="Misc_BioPile"/></action>
  <action id="pick_berry" seconds="1" interruptable="false"><spawn def="Berry" into="hands"/></action>
  <action id="settle" seconds="1" interruptable="false"><install/></action>
</actions>)";

		constexpr const char* kFaultyActions = R"(<actions>
  <action id="grab" on="loose_item" seconds="1" interruptable="false"><take/></action>
  <actionGroup id="both">grab</actionGroup>
  <action id="bash" seconds="1" interruptable="false"><damage amount="2"/></action>
  <action id="mope" seconds="1" interruptable="false"><restore need="mojo" amount="5"/></action>
  <action id="primp" seconds="1" interruptable="false"><restore need="hygiene" amount="5" scaleBy="quality"/></action>
  <action id="loaf" interruptable="true"><restore need="recreation" amount="2" seconds="60" upTo="90"/></action>
  <action id="sprawl" interruptable="true" pays="along" curve="2"><restore need="recreation" amount="2" seconds="60" upTo="90"/></action>
  <action id="dither" interruptable="true" pays="along"><restore need="recreation" amount="0" seconds="60" upTo="90"/></action>
  <action id="juggle" interruptable="false" pays="along"><work rate="Construction" direction="up"/><restore need="recreation" amount="2" seconds="60" upTo="90"/></action>
</actions>)";

		/// Synthetic action entries loaded through the real loader from a fresh temp root, run by an ActionSystem.
		class ActionEffectsTest : public ::testing::Test {
		  protected:
			void SetUp() override {
				root = std::filesystem::temp_directory_path() / ("worldsim-action-effects-" + std::to_string(std::random_device{}()) + "-" +
																 ::testing::UnitTest::GetInstance()->current_test_info()->name());
				std::filesystem::create_directories(root);
				std::filesystem::copy(
					assets::testing::shippedRepoRoot() / "assets" / "config" / "facts",
					root / "facts",
					std::filesystem::copy_options::recursive
				);
				for (const char* folder : {"behaviors", "actions", "work", "orders"}) {
					std::filesystem::create_directories(root / folder);
				}
				write("actions/synthetic.xml", kSyntheticActions);

				assets::AssetDefinition berry;
				berry.defName = "Berry";
				berry.label = "Berry";
				berry.itemProperties = assets::ItemProperties{};
				berry.itemProperties->stackSize = 20;
				berry.itemProperties->edible = assets::EdibleCapability{0.3F, assets::CapabilityQuality::Normal, true};
				assets::AssetRegistry::Get().registerTestDefinition(std::move(berry));

				world = std::make_unique<World>();
				actionSystem = &world->registerSystem<ActionSystem>(ecs::testing::shippedActions(), 42U);
				actionSystem->addActionObserver([this](const ActionEvent& event) {
					if (event.moment == ActionMoment::Finished) {
						++finished[event.colonist];
					}
				});
			}

			void TearDown() override {
				world.reset();
				assets::AssetRegistry::Get().clearDefinitions();
				std::filesystem::remove_all(root);
			}

			void write(const std::string& relative, const std::string& body) const {
				std::filesystem::create_directories((root / relative).parent_path());
				std::ofstream(root / relative) << body;
			}

			/// The library in the temp root; fails the test when it does not load.
			void load() {
				assets::EntrySources sources{.configRoot = root, .trackedNeeds = trackedNeedIds()};
				sources.definitionId = [](std::string_view name) -> std::optional<uint32_t> {
					const auto found = assets::testing::shippedDefinitionNames().find(name);
					return found == assets::testing::shippedDefinitionNames().end() ? std::nullopt : std::optional<uint32_t>(found->second);
				};
				assets::EntryLoad loaded = assets::loadEntries(sources);
				ASSERT_TRUE(loaded.errors.empty()) << ::testing::PrintToString(loaded.errors);
				library = std::move(loaded.library);
			}

			/// The action `id`, resolved with no error; kept for the test, since an action points at its values.
			const ActionValues& values(const std::string& id) {
				if (library.actions.empty()) {
					load();
				}
				ActionValuesLoad resolved = resolveAction(library, library.findAction(id));
				EXPECT_TRUE(resolved.errors.empty()) << ::testing::PrintToString(resolved.errors);
				resolvedValues.push_back(std::make_unique<ActionValues>(std::move(resolved.values)));
				return *resolvedValues.back();
			}

			EntityID createColonist(glm::vec2 position = {0.0F, 0.0F}) {
				const EntityID entity = world->createEntity();
				world->addComponent<Position>(entity, Position{position});
				world->addComponent<NeedsComponent>(entity, ecs::testing::shippedNeeds());
				world->addComponent<Memory>(entity, Memory{});
				world->addComponent<Inventory>(entity, Inventory::createForColonist());
				world->addComponent<Task>(entity, Task{});
				world->addComponent<Action>(entity, Action{});
				return entity;
			}

			/// Start `action` on `colonist`, who has arrived where he works.
			void start(EntityID colonist, const Action& action) {
				auto* task = world->getComponent<Task>(colonist);
				task->type = TaskType::FulfillNeed;
				task->state = TaskState::Arrived;
				task->targetPosition = world->getComponent<Position>(colonist)->value;
				*world->getComponent<Action>(colonist) = action;
			}

			[[nodiscard]] float need(EntityID colonist, NeedType type) {
				return world->getComponent<NeedsComponent>(colonist)->get(type).value;
			}
			void setNeed(EntityID colonist, NeedType type, float value) {
				world->getComponent<NeedsComponent>(colonist)->get(type).value = value;
			}
			[[nodiscard]] Inventory& inventory(EntityID colonist) { return *world->getComponent<Inventory>(colonist); }
			[[nodiscard]] Action&	 action(EntityID colonist) { return *world->getComponent<Action>(colonist); }

			/// An action eating `item`, its nutrition in the context.
			static Action eating(const ActionValues& values, const std::string& item, float nutrition) {
				Action action = Action::fromEntry(ActionType::Eat, values);
				action.context.nutrition = nutrition;
				ConsumptionEffect effect;
				effect.itemDefName = item;
				action.effect = effect;
				return action;
			}

			std::filesystem::path					   root;
			assets::EntryLibrary					   library;
			std::vector<std::unique_ptr<ActionValues>> resolvedValues;
			std::unique_ptr<World>					   world;
			ActionSystem*							   actionSystem = nullptr;
			std::map<EntityID, int>					   finished;
		};

	} // namespace

	TEST_F(ActionEffectsTest, ResolveActionRefusesWhatTheEngineCannotDo) {
		write("actions/faulty.xml", kFaultyActions);
		write(
			"behaviors/test/mojo.xml",
			R"(<behavior id="mojo"><need id="mojo" label="Mojo" seek="50" urgent="25" critical="10"><drains perGameMinute="0.1"/></need></behavior>)"
		);
		load();
		const std::string									   faulty = "[actions] faulty.xml: ";
		const std::vector<std::pair<std::string, std::string>> cases = {
			{"both", faulty + "<actionGroup id=\"both\"> is an action group; the engine performs one action"},
			{"bash", faulty + "<action id=\"bash\"> effect 1 <damage> lowers a condition, and nothing in the sim has one yet"},
			{"mope", faulty + "<action id=\"mope\"> effect 1 <restore> need \"mojo\" is no need the sim tracks"},
			{"primp",
			 faulty +
				 "<action id=\"primp\"> effect 1 <restore> scaleBy must be sleep_quality, the one fact of a target the engine scales an "
				 "action by"},
			{"loaf", faulty + "<action id=\"loaf\"> pays must be along; an open-ended action pays at its rate as it goes"},
			{"sprawl",
			 faulty +
				 "<action id=\"sprawl\"> curve must be left out; an open-ended action pays at its rate, with no fraction done to shape"},
			{"dither",
			 faulty + "<action id=\"dither\"> effect 1 <restore> amount must not be 0; an open-ended restore moves its need toward upTo"},
			{"juggle", faulty + "<action id=\"juggle\"> effect 2 <restore> is a second bound; an open-ended action runs until one"},
		};
		for (const auto& [id, error] : cases) {
			SCOPED_TRACE(id);
			EXPECT_EQ(resolveAction(library, library.findAction(id)).errors, std::vector<std::string>{error});
		}

		const ActionValues& soak = values("soak");
		ASSERT_EQ(soak.effects.size(), 2U);
		EXPECT_EQ(std::get<RestoreRow>(soak.effects[0]).need, NeedType::Hygiene);
		EXPECT_EQ(std::get<RestoreRow>(soak.effects[1]).need, NeedType::Thirst) << "rows keep the order written";
		EXPECT_EQ(soak.bound, ActionBound::None);
		const ActionValues& unwind = values("unwind");
		EXPECT_EQ(unwind.bound, ActionBound::Restore);
		EXPECT_EQ(unwind.boundRow, 0U);
	}

	TEST_F(ActionEffectsTest, RestoreMovesItsNeedByEachFormOfItsRow) {
		const EntityID bob = createColonist();

		// A plain amount: a rise stops at 100, a fall at 0.
		setNeed(bob, NeedType::Hygiene, 30.0F);
		setNeed(bob, NeedType::Thirst, 5.0F);
		start(bob, Action::fromEntry(ActionType::Wash, values("soak")));
		world->update(2.0F);
		EXPECT_FLOAT_EQ(need(bob, NeedType::Hygiene), 70.0F);
		EXPECT_FLOAT_EQ(need(bob, NeedType::Thirst), 0.0F);

		// From what the action consumes: the unit's nutrition times the scale.
		setNeed(bob, NeedType::Hunger, 20.0F);
		inventory(bob).addItem("Berry", 3);
		start(bob, eating(values("snack"), "Berry", 0.3F));
		world->update(1.0F);
		EXPECT_FLOAT_EQ(need(bob, NeedType::Hunger), 35.0F);

		// Scaled by the target's sleep quality.
		setNeed(bob, NeedType::Energy, 10.0F);
		Action snooze = Action::fromEntry(ActionType::Sleep, values("snooze"));
		snooze.context.sleepQuality = 0.5F;
		start(bob, snooze);
		world->update(4.0F);
		EXPECT_FLOAT_EQ(need(bob, NeedType::Energy), 30.0F);

		// The target creature's need, not the creature's own.
		const EntityID eve = createColonist({1.0F, 0.0F});
		setNeed(eve, NeedType::Hygiene, 50.0F);
		setNeed(bob, NeedType::Hygiene, 50.0F);
		Action soothe = Action::fromEntry(ActionType::Wash, values("soothe"));
		soothe.context.targetCreature = eve;
		start(bob, soothe);
		world->update(1.0F);
		EXPECT_FLOAT_EQ(need(eve, NeedType::Hygiene), 80.0F);
		EXPECT_FLOAT_EQ(need(bob, NeedType::Hygiene), 50.0F);
	}

	TEST_F(ActionEffectsTest, ConsumeUsesUpItsCountAndAConsumeThatFindsNothingStopsTheRowsAfterIt) {
		const EntityID bob = createColonist();
		inventory(bob).addItem("Berry", 3);
		setNeed(bob, NeedType::Hunger, 20.0F);
		start(bob, eating(values("snack"), "Berry", 0.3F));
		world->update(1.0F);
		EXPECT_EQ(inventory(bob).getQuantity("Berry"), 1U);
		EXPECT_EQ(finished[bob], 1);

		// Effects apply in the order written: the restore before the consume applies, the one after it does not, and
		// the action does not finish.
		inventory(bob).removeItem("Berry", 1);
		setNeed(bob, NeedType::Hygiene, 50.0F);
		setNeed(bob, NeedType::Hunger, 50.0F);
		start(bob, eating(values("tidy"), "Berry", 0.3F));
		world->update(1.0F);
		EXPECT_FLOAT_EQ(need(bob, NeedType::Hygiene), 60.0F);
		EXPECT_FLOAT_EQ(need(bob, NeedType::Hunger), 50.0F);
		EXPECT_EQ(finished[bob], 1);

		inventory(bob).addItem("Berry", 1);
		start(bob, eating(values("tidy"), "Berry", 0.3F));
		world->update(1.0F);
		EXPECT_FLOAT_EQ(need(bob, NeedType::Hygiene), 70.0F);
		EXPECT_FLOAT_EQ(need(bob, NeedType::Hunger), 60.0F);
		EXPECT_EQ(finished[bob], 2);
	}

	TEST_F(ActionEffectsTest, TakeAndPutMoveGoodsBetweenABoxAndTheCreature) {
		const EntityID bob = createColonist();
		const EntityID box = world->createEntity();
		world->addComponent<Position>(box, Position{{0.5F, 0.0F}});
		Inventory store = Inventory::createForStorage();
		store.addItem("Berry", 5);
		world->addComponent<Inventory>(box, std::move(store));

		Action			 lift = Action::fromEntry(ActionType::Withdraw, values("lift"));
		CollectionEffect take;
		take.itemDefName = "Berry";
		take.quantity = 2;
		take.sourceStorageId = box;
		lift.effect = take;
		start(bob, lift);
		world->update(1.0F);
		EXPECT_EQ(inventory(bob).getQuantity("Berry"), 2U);
		EXPECT_EQ(world->getComponent<Inventory>(box)->getQuantity("Berry"), 3U);

		Action		  stow = Action::fromEntry(ActionType::Deposit, values("stow"));
		DepositEffect put;
		put.itemDefName = "Berry";
		put.quantity = 2;
		put.storageEntityId = box;
		stow.effect = put;
		start(bob, stow);
		world->update(1.0F);
		EXPECT_EQ(inventory(bob).getQuantity("Berry"), 0U);
		EXPECT_EQ(world->getComponent<Inventory>(box)->getQuantity("Berry"), 5U);
		EXPECT_EQ(finished[bob], 2);
	}

	TEST_F(ActionEffectsTest, WorkPaysAlongAtItsRateAndKeepsTheConstructionGuards) {
		int built = 0;
		int tornDown = 0;
		actionSystem->setStructureCompletedCallback([&built](EntityID) { ++built; });
		actionSystem->setStructureDeconstructedCallback([&tornDown](EntityID) { ++tornDown; });
		const auto blueprintOf = [this](float workDone, float workTotal) {
			const EntityID	   entity = world->createEntity();
			StructureBlueprint blueprint;
			blueprint.phase = StructureBlueprint::BuildPhase::UnderConstruction;
			blueprint.workDone = workDone;
			blueprint.workTotal = workTotal;
			world->addComponent<StructureBlueprint>(entity, blueprint);
			return entity;
		};
		const auto working = [](const ActionValues& values, EntityID blueprint) {
			Action		   action = Action::fromEntry(ActionType::Build, values);
			ProgressEffect progress;
			progress.targetEntityId = blueprint;
			action.effect = progress;
			return action;
		};

		// Two builders on one site: the work adds up, and completion fires once.
		const EntityID site = blueprintOf(0.0F, 20.0F);
		const EntityID bob = createColonist();
		const EntityID eve = createColonist({1.0F, 0.0F});
		start(bob, working(values("raise"), site));
		start(eve, working(values("raise"), site));
		world->update(1.0F);
		EXPECT_EQ(world->getComponent<StructureBlueprint>(site)->phase, StructureBlueprint::BuildPhase::Complete);
		EXPECT_EQ(built, 1);
		start(bob, working(values("raise"), site));
		world->update(1.0F);
		EXPECT_FALSE(action(bob).isActive()) << "a builder arriving after completion is cleared";
		EXPECT_EQ(built, 1);

		// A site ordered demolished takes no build work.
		const EntityID demolished = blueprintOf(5.0F, 20.0F);
		world->getComponent<StructureBlueprint>(demolished)->demolishing = true;
		start(bob, working(values("raise"), demolished));
		world->update(1.0F);
		EXPECT_FALSE(action(bob).isActive());
		EXPECT_FLOAT_EQ(world->getComponent<StructureBlueprint>(demolished)->workDone, 5.0F);

		// A teardown begins once it takes work off, and is refunded once, at 0.
		const EntityID standing = blueprintOf(15.0F, 20.0F);
		start(bob, working(values("raze"), standing));
		world->update(1.0F);
		EXPECT_FLOAT_EQ(world->getComponent<StructureBlueprint>(standing)->workDone, 5.0F);
		EXPECT_TRUE(world->getComponent<StructureBlueprint>(standing)->teardownBegun);
		EXPECT_EQ(tornDown, 0);
		world->update(1.0F);
		EXPECT_EQ(tornDown, 1);
		start(bob, working(values("raze"), standing));
		world->update(1.0F);
		EXPECT_FALSE(action(bob).isActive()) << "a teardown with no work done is cleared";
		EXPECT_EQ(tornDown, 1);
	}

	TEST_F(ActionEffectsTest, TheCraftServiceSpendsTheStoreMakesTheOutputAndAdvancesTheJob) {
		const EntityID station = world->createEntity();
		world->addComponent<Position>(station, Position{{0.5F, 0.0F}});
		WorkQueue queue;
		queue.addJob("Recipe_Test", 2);
		world->addComponent<WorkQueue>(station, queue);
		Inventory store = Inventory::createForStorage();
		store.addItem("Stick", 2);
		store.addItem("Stone", 1);
		world->addComponent<Inventory>(station, std::move(store));

		const auto crafting = [&](const ActionValues& values) {
			Action action = Action::fromEntry(ActionType::Craft, values);
			action.duration = 1.0F;
			CraftingEffect craft;
			craft.recipeDefName = "Recipe_Test";
			craft.stationEntityId = station;
			craft.inputs = {{"Stick", 2}, {"Stone", 1}};
			craft.outputs = {{"Berry", 1}};
			action.effect = craft;
			return action;
		};
		const EntityID bob = createColonist();
		start(bob, crafting(values("make")));
		world->update(1.0F);
		EXPECT_EQ(world->getComponent<Inventory>(station)->getQuantity("Stick"), 0U);
		EXPECT_EQ(world->getComponent<Inventory>(station)->getQuantity("Stone"), 0U);
		EXPECT_EQ(availableQuantity(inventory(bob), "Berry"), 1U);
		EXPECT_EQ(world->getComponent<WorkQueue>(station)->getNextJob()->completed, 1U);
		EXPECT_EQ(finished[bob], 1);

		// Checked again at completion: a store short of an input makes and spends nothing.
		world->getComponent<Inventory>(station)->addItem("Stick", 2);
		start(bob, crafting(values("make")));
		world->update(1.0F);
		EXPECT_EQ(world->getComponent<Inventory>(station)->getQuantity("Stick"), 2U);
		EXPECT_EQ(availableQuantity(inventory(bob), "Berry"), 1U);
		EXPECT_EQ(world->getComponent<WorkQueue>(station)->getNextJob()->completed, 1U);
		EXPECT_EQ(finished[bob], 1);
	}

	TEST_F(ActionEffectsTest, SpawnMakesItsDefinitionWhereTheCreatureStandsOrInItsHands) {
		const EntityID bob = createColonist({3.0F, 4.0F});
		Action		   mark = Action::fromEntry(ActionType::Toilet, values("leave_mark"));
		mark.targetPosition = {3.0F, 4.0F};
		start(bob, mark);
		world->update(1.0F);
		int piles = 0;
		for (auto [entity, position, appearance] : world->view<Position, Appearance>()) {
			if (appearance.defName == "Misc_BioPile") {
				++piles;
				EXPECT_FLOAT_EQ(position.value.x, 3.0F);
				EXPECT_FLOAT_EQ(position.value.y, 4.0F);
			}
		}
		EXPECT_EQ(piles, 1);

		start(bob, Action::fromEntry(ActionType::Toilet, values("pick_berry")));
		world->update(1.0F);
		ASSERT_TRUE(inventory(bob).rightHand.has_value());
		EXPECT_EQ(inventory(bob).rightHand->defName, "Berry");
	}

	TEST_F(ActionEffectsTest, InstallPutsTheCarriedPackageDownAtItsTarget) {
		const EntityID bob = createColonist();
		const EntityID shelf = world->createEntity();
		world->addComponent<Position>(shelf, Position{{0.0F, 0.0F}});
		world->addComponent<Packaged>(shelf, Packaged{glm::vec2{5.0F, 5.0F}, true});
		inventory(bob).carryingPackagedEntity = shelf;
		inventory(bob).leftHand = ItemStack{"BasicShelf", 1};
		inventory(bob).rightHand = ItemStack{"BasicShelf", 1};

		Action				settle = Action::fromEntry(ActionType::PlacePackaged, values("settle"));
		PlacePackagedEffect place;
		place.packagedEntityId = shelf;
		place.targetPosition = {5.0F, 5.0F};
		settle.effect = place;
		start(bob, settle);
		world->update(1.0F);
		EXPECT_FALSE(world->hasComponent<Packaged>(shelf));
		EXPECT_FLOAT_EQ(world->getComponent<Position>(shelf)->value.x, 5.0F);
		EXPECT_FALSE(inventory(bob).leftHand.has_value());
		EXPECT_FALSE(inventory(bob).carryingPackagedEntity.has_value());
		EXPECT_EQ(finished[bob], 1);
	}

	TEST_F(ActionEffectsTest, PaysEndGivesNothingUntilItCompletesAndAlongPaysAsItGoesByItsCurve) {
		const EntityID nap = createColonist();
		const EntityID rest = createColonist({1.0F, 0.0F});
		const EntityID doze = createColonist({2.0F, 0.0F});
		for (const EntityID colonist : {nap, rest, doze}) {
			setNeed(colonist, NeedType::Energy, 10.0F);
		}
		start(nap, Action::fromEntry(ActionType::Sleep, values("nap")));
		start(rest, Action::fromEntry(ActionType::Sleep, values("rest")));
		start(doze, Action::fromEntry(ActionType::Sleep, values("doze")));

		world->update(2.0F);
		EXPECT_FLOAT_EQ(need(nap, NeedType::Energy), 10.0F) << "paying at its end, nothing yet";
		EXPECT_FLOAT_EQ(need(rest, NeedType::Energy), 30.0F) << "curve 1: half done, half paid";
		EXPECT_FLOAT_EQ(need(doze, NeedType::Energy), 20.0F) << "curve 2: half done, a quarter paid";

		world->update(2.0F);
		for (const EntityID colonist : {nap, rest, doze}) {
			EXPECT_FLOAT_EQ(need(colonist, NeedType::Energy), 50.0F) << "a whole action earns the whole, and completion adds nothing";
		}

		// Interrupted halfway: along keeps what it earned, end keeps nothing.
		for (const EntityID colonist : {nap, doze}) {
			setNeed(colonist, NeedType::Energy, 10.0F);
		}
		start(nap, Action::fromEntry(ActionType::Sleep, values("nap")));
		start(doze, Action::fromEntry(ActionType::Sleep, values("doze")));
		world->update(2.0F);
		action(nap).clear();
		action(doze).clear();
		world->getComponent<Task>(nap)->clear();
		world->getComponent<Task>(doze)->clear();
		world->update(2.0F);
		EXPECT_FLOAT_EQ(need(nap, NeedType::Energy), 10.0F);
		EXPECT_FLOAT_EQ(need(doze, NeedType::Energy), 20.0F);
	}

	TEST_F(ActionEffectsTest, AnOpenEndedRestoreRunsAtItsRateUntilItsNeedReachesUpTo) {
		const EntityID bob = createColonist();
		setNeed(bob, NeedType::Recreation, 80.0F);
		start(bob, Action::fromEntry(ActionType::Wash, values("unwind")));
		world->update(60.0F);
		EXPECT_FLOAT_EQ(need(bob, NeedType::Recreation), 82.0F);
		EXPECT_TRUE(action(bob).isActive());
		world->update(600.0F);
		EXPECT_FLOAT_EQ(need(bob, NeedType::Recreation), 90.0F) << "it stops at upTo";
		EXPECT_FALSE(action(bob).isActive());
		EXPECT_EQ(finished[bob], 1);

		setNeed(bob, NeedType::Recreation, 95.0F);
		start(bob, Action::fromEntry(ActionType::Wash, values("unwind")));
		world->update(1.0F);
		EXPECT_FALSE(action(bob).isActive()) << "a need already at its bound ends the action at once";
		EXPECT_FLOAT_EQ(need(bob, NeedType::Recreation), 95.0F);
	}

	TEST_F(ActionEffectsTest, AnOpenEndedConsumeEatsAUnitARoundUntilItsNeedReachesUpTo) {
		const EntityID bob = createColonist();
		inventory(bob).addItem("Berry", 3);
		setNeed(bob, NeedType::Hunger, 60.0F);
		start(bob, eating(values("feast"), "Berry", 0.2F));
		world->update(1.0F);
		EXPECT_FLOAT_EQ(need(bob, NeedType::Hunger), 60.0F) << "a round pays at the end of its seconds";
		world->update(1.0F);
		EXPECT_FLOAT_EQ(need(bob, NeedType::Hunger), 80.0F);
		EXPECT_EQ(inventory(bob).getQuantity("Berry"), 2U);
		world->update(2.0F);
		EXPECT_FLOAT_EQ(need(bob, NeedType::Hunger), 100.0F);
		EXPECT_EQ(inventory(bob).getQuantity("Berry"), 1U);
		EXPECT_FALSE(action(bob).isActive()) << "past upTo after a round, it ends";
		EXPECT_EQ(finished[bob], 1);

		// Out of food after a round: it ends, finished, with what it ate.
		setNeed(bob, NeedType::Hunger, 60.0F);
		start(bob, eating(values("feast"), "Berry", 0.2F));
		world->update(4.0F);
		EXPECT_FLOAT_EQ(need(bob, NeedType::Hunger), 80.0F);
		EXPECT_EQ(inventory(bob).getQuantity("Berry"), 0U);
		EXPECT_FALSE(action(bob).isActive());
		EXPECT_EQ(finished[bob], 2);

		// Already there: it ends at once and finishes nothing.
		inventory(bob).addItem("Berry", 1);
		setNeed(bob, NeedType::Hunger, 96.0F);
		start(bob, eating(values("feast"), "Berry", 0.2F));
		world->update(2.0F);
		EXPECT_EQ(inventory(bob).getQuantity("Berry"), 1U);
		EXPECT_FALSE(action(bob).isActive());
		EXPECT_EQ(finished[bob], 2);
	}

} // namespace ecs::test
