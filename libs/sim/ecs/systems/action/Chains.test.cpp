// The chain runner on synthetic chains, queries, and actions loaded through the real loader (beside the shipped
// actions, which the chains' do steps may name), run by the real AIDecisionSystem, MovementSystem, and ActionSystem.
// One test per requirement on the chain runner (queries-and-chains.md, Requirements on the chain runner), numbered
// R01 to R22, then every step kind, every way a step ends, every primitive started with no factory, and the claims.

#include "../AIDecisionSystem.h"
#include "../ActionSystem.h"
#include "../MovementSystem.h"
#include "../PhysicsSystem.h"
#include "../ShippedPriorityConfig.test.h"
#include "../ThoughtSystem.h"
#include "../TimeSystem.h"

#include "../../ChainEntries.h"
#include "../../GoalTaskRegistry.h"
#include "../../InventoryMass.h"
#include "../../ShippedConfig.test.h"
#include "../../SyntheticEntries.test.h"
#include "../../ThoughtTesting.test.h"
#include "../../World.h"
#include "../../components/Action.h"
#include "../../components/AgentRadius.h"
#include "../../components/Appearance.h"
#include "../../components/DecisionTrace.h"
#include "../../components/Inventory.h"
#include "../../components/Memory.h"
#include "../../components/Movement.h"
#include "../../components/NavPath.h"
#include "../../components/Needs.h"
#include "../../components/Packaged.h"
#include "../../components/ResourceStack.h"
#include "../../components/SourceReachMemo.h"
#include "../../components/StructureBlueprint.h"
#include "../../components/Task.h"
#include "../../components/Thoughts.h"
#include "../../components/Transform.h"
#include "../../components/WorkQueue.h"
#include "../ChainClaims.h"

#include <assets/AssetRegistry.h>
#include <assets/RecipeRegistry.h>
#include <queries/QueryEvaluator.h>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ecs::test {

	namespace {

		constexpr const char* kChains = R"(<behavior id="chains">
  <query id="water"><from capability="drinkable"/><weigh fact="distance" by="-1"/></query>
  <query id="bush"><from yields="Berry"/><weigh fact="distance" by="-1"/></query>
  <query id="toilet_or_here"><from capability="toilet"/><from place="here" fallback="true"/><weigh fact="distance" by="-1"/></query>
  <query id="ring"><from place="around" minRadius="2" radius="6" count="1"/><exclude fact="distance" above="6"/></query>

  <action id="sip" seconds="1" interruptable="false"><restore need="thirst" amount="40"/></action>
  <action id="rinse" seconds="1" interruptable="false"><restore need="hygiene" amount="30"/></action>
  <action id="snack" seconds="1" interruptable="false"><consume capability="edible" count="1"/><restore need="hunger" from="nutrition" scale="100"/></action>
  <action id="dawdle" seconds="30" interruptable="true"><restore need="recreation" amount="5"/></action>
  <action id="linger" seconds="30" interruptable="false"><restore need="recreation" amount="5"/></action>
  <action id="snooze" seconds="1" interruptable="false"><restore need="energy" amount="40" scaleBy="sleep_quality"/></action>
  <action id="mark" seconds="1" interruptable="false"><spawn def="Misc_BioPile"/></action>

  <chain id="drink"><go query="water"/><do action="sip"/></chain>
  <chain id="drink_and_rinse"><go query="water"/><do action="sip"/><do action="rinse"/></chain>
  <chain id="snack_then_settle"><do action="snack"/><need id="digestion" change="-10"/></chain>
  <chain id="dawdle"><go to="target"/><do action="dawdle"/></chain>
  <chain id="dawdle_watching"><go to="target"/><do action="dawdle"><ends on="target_gone"/><ends on="target_left"/></do><need id="hygiene" change="-7"/></chain>
  <action id="nibble" seconds="30" interruptable="true"><consume capability="edible" count="1"/><restore need="hunger" from="nutrition" scale="100"/></action>
  <chain id="snack_watching"><do action="nibble"><ends on="held_gone"/></do></chain>
  <chain id="linger"><go to="target"/><do action="linger"/></chain>
  <chain id="gather"><go query="bush"/><do action="take" claims="use source_quantity"/></chain>
  <chain id="take_target"><go to="target"/><do action="take" claims="use source_quantity"/></chain>
  <chain id="take_and_drop"><go to="target"/><do action="take" claims="use source_quantity"/><drop/></chain>
  <chain id="relieve"><go query="toilet_or_here"/><do action="mark"/></chain>
  <chain id="sleep"><go query="toilet_or_here"/><do action="snooze"/></chain>
  <chain id="wander"><go query="ring"/></chain>
  <chain id="sip_and_think"><do action="sip"/><think thought="sipped" to="self"/></chain>
  <chain id="fetch_berry"><go to="source"/><do action="pickup" claims="source_quantity"/></chain>
  <chain id="have_berry"><have item="Berry" fetch="fetch_berry"/><do action="snack"/></chain>
  <chain id="run_drink"><run chain="drink"/><need id="hunger" change="5"/></chain>
  <chain id="deliver"><go to="source"/><do action="pickup" claims="source_quantity"/><go to="destination"/><do action="deposit" claims="inbound in_flight" amount="carried"/></chain>
  <chain id="build"><go to="destination"/><do action="build" claims="slot"/></chain>
  <chain id="tear_down"><go to="destination"/><do action="deconstruct"/></chain>
  <chain id="craft"><go to="destination"/><do action="craft" claims="slot"/></chain>
  <query id="sticks"><from yields="Stick"/><weigh fact="distance" by="-1"/></query>
  <query id="bed_or_here"><from capability="sleepable"/><from place="here" fallback="true"/><weigh fact="distance" by="-1"/></query>
  <chain id="gather_sticks"><go query="sticks"/><do action="take" claims="use"/></chain>
  <chain id="sleep_in_bed"><go query="bed_or_here"/><do action="snooze"/></chain>
  <chain id="cut_and_deliver"><go to="source"/><do action="harvest" claims="use"/><go to="destination"/><do action="deposit" claims="inbound in_flight" amount="carried"/></chain>
  <chain id="take_twice"><go to="target"/><do action="take" claims="use source_quantity"/><go to="source"/><do action="take" claims="use source_quantity"/></chain>
  <chain id="deliver_carried"><go to="destination"/><do action="deposit" claims="inbound in_flight" amount="carried"/></chain>
  <chain id="deliver_for_goal"><go to="destination"/><do action="deposit"/></chain>
  <chain id="go_watching"><go to="target"><ends on="target_gone"/></go><do action="sip"/></chain>
</behavior>)";

		/// A group is shared, so it lives in actions/; a `have` naming the goal's item is work's, so it lives in work/.
		constexpr const char* kGroups = R"(<actions><actionGroup id="take">harvest pickup withdraw</actionGroup></actions>)";
		constexpr const char* kWork = R"(<colonyWork>
  <chain id="lift_package"><go to="target"/><do action="pickup_packaged" claims="use"/></chain>
  <chain id="place"><have item="goal" fetch="lift_package"/><go to="destination"/><do action="place_packaged"/></chain>
  <chain id="fetch_for_goal"><go to="source"/><do action="pickup" claims="source_quantity"/></chain>
  <chain id="haul_from_pile"><have item="goal" fetch="fetch_for_goal"/><go to="destination"/><do action="deposit" claims="inbound in_flight" amount="carried"/></chain>
</colonyWork>)";

		constexpr std::string_view kThoughts = R"(
  <thought id="sipped" label="Sipped" importance="trivial"><feeling is="content"/></thought>
)";

		constexpr float kTick = 0.1F;

		/// The chains above with the shipped actions, run on a headless world: the AI decides (no nav mesh, so movement
		/// follows its target straight), MovementSystem walks, ActionSystem runs the chains.
		class ChainsTest : public ::testing::Test {
		  protected:
			void SetUp() override {
				ASSERT_TRUE(loadShippedPriorityConfig());
				GoalTaskRegistry::Get().clear();
				registerDefinitions();

				entries.emplace(
					"chains",
					ecs::testing::SyntheticEntries::Options{
						.definitions = ecs::testing::SyntheticEntries::Definitions::RegistryThenShipped,
						.trackedNeeds = trackedNeedIds(),
						.shippedFolders = {"behaviors", "actions"},
					}
				);
				entries->write("behaviors/test/chains.xml", kChains);
				entries->write("actions/test_groups.xml", kGroups);
				entries->write("work/test_work.xml", kWork);
				assets::EntryLoad loaded = entries->load();
				ASSERT_TRUE(loaded.errors.empty()) << ::testing::PrintToString(loaded.errors);
				library = std::move(loaded.library);
				ActionEntriesLoad actions = resolveActionEntries(library);
				ASSERT_TRUE(actions.errors.empty()) << ::testing::PrintToString(actions.errors);
				actionEntries = std::move(actions.actions);
				thoughts = std::make_unique<ecs::testing::TestThoughtTypes>(kThoughts);
				ASSERT_TRUE(thoughts->errors.empty()) << ::testing::PrintToString(thoughts->errors);
				ChainEntriesLoad chains = resolveChainEntries(library, actionEntries, &thoughts->types);
				ASSERT_TRUE(chains.errors.empty()) << ::testing::PrintToString(chains.errors);
				chainEntries = std::move(chains.chains);
				evaluator = std::make_unique<queries::QueryEvaluator>(
					library, assets::AssetRegistry::Get(), std::span<const assets::ConditionKindDef>{}
				);

				world = std::make_unique<World>();
				world->registerSystem<TimeSystem>();
				ai = &world->registerSystem<AIDecisionSystem>(
					assets::AssetRegistry::Get(), assets::RecipeRegistry::Get(), ecs::testing::shippedActions(), 42U
				);
				world->registerSystem<MovementSystem>();
				world->registerSystem<PhysicsSystem>();
				actionSystem = &world->registerSystem<ActionSystem>(actionEntries, 42U);
				world->registerSystem<ThoughtSystem>(thoughts->types, 42U);
				actionSystem->setChains(
					{.library = &library,
					 .chains = &chainEntries,
					 .evaluator = evaluator.get(),
					 .regrowth = regrowing,
					 .appendClaims = [this](EntityID creature, const Task& task) { ai->appendClaims(creature, task); }}
				);
				actionSystem->setDropResourceCallback([this](const std::string& defName, float, float, uint32_t quantity, glm::vec2) {
					dropped.emplace_back(defName, quantity);
				});
				actionSystem->addActionObserver([this](const ActionEvent& event) {
					(event.moment == ActionMoment::Started ? started : finished).push_back(library.actions[event.action.index].id);
				});
			}

			void TearDown() override {
				world.reset();
				evaluator.reset();
				assets::AssetRegistry::Get().clearDefinitions();
				assets::RecipeRegistry::Get().clear();
				GoalTaskRegistry::Get().clear();
				entries.reset();
			}

			static void registerDefinitions() {
				auto&					registry = assets::AssetRegistry::Get();
				assets::AssetDefinition berry;
				berry.defName = "Berry";
				berry.label = "Berry";
				berry.itemProperties = assets::ItemProperties{};
				berry.itemProperties->stackSize = 20;
				berry.itemProperties->edible = assets::EdibleCapability{0.3F, assets::CapabilityQuality::Normal, true};
				berry.capabilities.carryable = assets::CarryableCapability{.quantity = 5};
				registry.registerTestDefinition(std::move(berry));
				assets::AssetDefinition bush;
				bush.defName = "BerryBush";
				bush.capabilities.harvestable = assets::HarvestableCapability{
					.yieldDefName = "Berry",
					.amountMin = 3,
					.amountMax = 3,
					.durability = 10.0F,
					.destructive = false,
					.regrowthTime = 60.0F
				};
				registry.registerTestDefinition(std::move(bush));
				assets::AssetDefinition pond;
				pond.defName = "Pond";
				pond.capabilities.drinkable = assets::DrinkableCapability{};
				registry.registerTestDefinition(std::move(pond));
				assets::AssetDefinition rock;
				rock.defName = "Rock";
				registry.registerTestDefinition(std::move(rock));
				assets::AssetDefinition stick;
				stick.defName = "Stick";
				stick.label = "Stick";
				stick.itemProperties = assets::ItemProperties{};
				stick.itemProperties->stackSize = 20;
				stick.capabilities.carryable = assets::CarryableCapability{.quantity = 4};
				registry.registerTestDefinition(std::move(stick));
				assets::AssetDefinition box;
				box.defName = "Box";
				box.capabilities.storage = assets::StorageCapability{};
				registry.registerTestDefinition(std::move(box));
				assets::AssetDefinition crate;
				crate.defName = "Crate";
				registry.registerTestDefinition(std::move(crate));
				assets::AssetDefinition stickBush;
				stickBush.defName = "StickBush";
				stickBush.capabilities.harvestable = assets::HarvestableCapability{
					.yieldDefName = "Stick",
					.amountMin = 5,
					.amountMax = 5,
					.durability = 10.0F,
					.destructive = false,
					.regrowthTime = 60.0F
				};
				registry.registerTestDefinition(std::move(stickBush));
				assets::AssetDefinition ironTree;
				ironTree.defName = "IronTree";
				ironTree.capabilities.harvestable =
					assets::HarvestableCapability{.yieldDefName = "Stick", .durability = 10.0F, .requiredToolType = "Axe"};
				registry.registerTestDefinition(std::move(ironTree));
				for (const char* name : {"Log", "Boulder"}) {
					assets::AssetDefinition armful;
					armful.defName = name;
					armful.label = name;
					armful.itemProperties = assets::ItemProperties{};
					armful.itemProperties->stackSize = 10;
					armful.handsRequired = 2;
					registry.registerTestDefinition(std::move(armful));
				}
				assets::AssetDefinition tree;
				tree.defName = "Tree";
				tree.capabilities.harvestable = assets::HarvestableCapability{
					.yieldDefName = "Log", .amountMin = 3, .amountMax = 3, .durability = 10.0F, .destructive = true
				};
				registry.registerTestDefinition(std::move(tree));
				assets::AssetDefinition bed;
				bed.defName = "Bed";
				bed.capabilities.sleepable = assets::SleepableCapability{};
				registry.registerTestDefinition(std::move(bed));
			}

			[[nodiscard]] static uint32_t id(const std::string& defName) { return assets::AssetRegistry::Get().getDefNameId(defName); }

			EntityID createColonist(glm::vec2 position = {0.0F, 0.0F}) {
				const EntityID entity = world->createEntity();
				world->addComponent<Position>(entity, Position{position});
				world->addComponent<AgentRadius>(entity, AgentRadius{});
				world->addComponent<Velocity>(entity, Velocity{{0.0F, 0.0F}});
				world->addComponent<MovementTarget>(entity, MovementTarget{{0.0F, 0.0F}, 2.0F, false});
				NeedsComponent needs = ecs::testing::shippedNeeds();
				for (Need& need : needs.needs) {
					need.value = 100.0F;
				}
				world->addComponent<NeedsComponent>(entity, needs);
				world->addComponent<Memory>(entity, Memory{});
				world->addComponent<Inventory>(entity, Inventory::createForColonist());
				world->addComponent<Task>(entity, Task{});
				world->addComponent<Action>(entity, Action{});
				world->addComponent<DecisionTrace>(entity, DecisionTrace{});
				world->addComponent<SourceReachMemo>(entity, SourceReachMemo{});
				world->addComponent<Thoughts>(entity, Thoughts{});
				return entity;
			}

			/// A remembered thing of `defName` at `at`, as a chain's target: what a query's pick of it would be.
			ChainTarget remember(EntityID colonist, glm::vec2 at, const std::string& defName, assets::CapabilityType capability) {
				Memory& memory = *world->getComponent<Memory>(colonist);
				memory.rememberWorldEntity(at, id(defName), static_cast<uint16_t>(1U << static_cast<unsigned>(capability)));
				const auto* def = assets::AssetRegistry::Get().getDefinition(defName);
				ChainTarget target;
				target.kind = ChainTarget::Kind::Thing;
				target.key = hashWorldEntity(at, id(defName));
				target.defNameId = id(defName);
				target.item =
					def != nullptr && def->capabilities.harvestable ? id(def->capabilities.harvestable->yieldDefName) : id(defName);
				target.position = at;
				target.goal = at;
				return target;
			}

			[[nodiscard]] ChainTarget entityTarget(EntityID entity, glm::vec2 at) const {
				ChainTarget target;
				target.kind = ChainTarget::Kind::Entity;
				target.entity = entity;
				target.position = at;
				target.goal = at;
				if (const auto* appearance = world->getComponent<Appearance>(entity)) {
					target.defNameId = id(appearance->defName);
				}
				return target;
			}

			[[nodiscard]] static ChainTarget placeTarget(glm::vec2 at) {
				ChainTarget target;
				target.kind = ChainTarget::Kind::Place;
				target.position = at;
				target.goal = at;
				return target;
			}

			/// A loose pile of `quantity` units of `defName` at `at`: a real pile entity filed with the pile index, which the
			/// colonist remembers. Its target carries the source share the query would size.
			ChainTarget pileAt(
				EntityID		   colonist,
				glm::vec2		   at,
				const std::string& defName,
				uint32_t		   quantity,
				uint32_t		   share,
				EntityID*		   pile = nullptr
			) {
				const EntityID entity = world->createEntity();
				world->addComponent<Position>(entity, Position{at});
				world->addComponent<Appearance>(entity, Appearance{defName});
				world->addComponent<ResourceStack>(entity, ResourceStack{.quantity = quantity});
				actionSystem->loosePiles().add(entity, id(defName), at);
				if (pile != nullptr) {
					*pile = entity;
				}
				ChainTarget target = remember(colonist, at, defName, assets::CapabilityType::Carryable);
				target.claimKind = assets::ClaimKind::SourceQuantity;
				target.claimQuantity = share;
				return target;
			}

			struct Root {
				uint64_t goal = 0;
				EntityID site = kInvalidEntity;
			};

			/// A build site at `at` whose foundation still needs `quantity` of `defName`, and its build goal: the root a work
			/// chain delivers toward.
			Root buildRoot(const std::string& defName, uint32_t quantity, glm::vec2 at) {
				const EntityID	   site = world->createEntity();
				StructureBlueprint blueprint;
				blueprint.phase = StructureBlueprint::BuildPhase::AwaitingMaterials;
				blueprint.required = {{defName, quantity}};
				world->addComponent<Position>(site, Position{at});
				world->addComponent<StructureBlueprint>(site, blueprint);
				GoalTask goal;
				goal.type = GoalType::Build;
				goal.owner = GoalOwner::ConstructionGoalSystem;
				goal.destinationEntity = site;
				goal.destinationPosition = at;
				goal.acceptedDefNameIds = {id(defName)};
				return {.goal = GoalTaskRegistry::Get().createGoal(goal), .site = site};
			}

			[[nodiscard]] static uint32_t claimed(const Task& task, ClaimKind kind) {
				uint32_t quantity = 0;
				for (const Claim& claim : task.claims) {
					quantity += claim.kind() == kind ? claim.quantity() : 0U;
				}
				return quantity;
			}

			struct ChainOption {
				ChainTarget source;
				ChainTarget destination;
				ChainGoal	goal;
				/// The rung it was selected on; committed work unless a test names one, so the needs a test lowers and the
				/// work its world offers do not take the creature from the chain under test.
				std::optional<uint8_t> tier;
			};

			/// The one way a test starts a chain (behavior-data-migration.md, Porting tests): `chain` on `colonist` at
			/// `step`, with the option's target and, for work, its source, destination, goal, and rung.
			bool startChainAt(
				EntityID		   colonist,
				const std::string& chain,
				uint8_t			   step,
				const ChainTarget& target,
				const ChainOption& option = {}
			) {
				const auto handle = library.findChain(chainId(chain));
				EXPECT_TRUE(handle.valid()) << chain;
				return actionSystem->startChain(
					colonist,
					{.chain = handle,
					 .step = step,
					 .target = target,
					 .source = option.source,
					 .destination = option.destination,
					 .goal = option.goal,
					 .tier = option.tier.value_or(assets::PriorityConfig::Get().decisionRungs().committedWork)}
				);
			}

			void run(float seconds) {
				for (int tick = 0; tick < static_cast<int>(seconds / kTick + 0.5F); ++tick) {
					world->update(kTick);
				}
			}

			/// Ticks until `done` holds, at most `seconds`; whether it held.
			bool runUntil(const std::function<bool()>& done, float seconds = 30.0F) {
				for (int tick = 0; tick < static_cast<int>(seconds / kTick); ++tick) {
					if (done()) {
						return true;
					}
					world->update(kTick);
				}
				return done();
			}

			[[nodiscard]] Task&		 task(EntityID colonist) { return *world->getComponent<Task>(colonist); }
			[[nodiscard]] Action&	 action(EntityID colonist) { return *world->getComponent<Action>(colonist); }
			[[nodiscard]] Inventory& inventory(EntityID colonist) { return *world->getComponent<Inventory>(colonist); }
			[[nodiscard]] Memory&	 memory(EntityID colonist) { return *world->getComponent<Memory>(colonist); }
			[[nodiscard]] glm::vec2	 position(EntityID colonist) { return world->getComponent<Position>(colonist)->value; }
			[[nodiscard]] float		 need(EntityID colonist, NeedType type) {
				 return world->getComponent<NeedsComponent>(colonist)->get(type).value;
			}
			void setNeed(EntityID colonist, NeedType type, float value) {
				world->getComponent<NeedsComponent>(colonist)->get(type).value = value;
			}
			[[nodiscard]] bool running(EntityID colonist, const std::string& chain) {
				const Task& current = task(colonist);
				return current.type == TaskType::Chain && current.chain.active() &&
					   library.chains[current.chain.frames[0].chain.index].id == chainId(chain);
			}
			/// A chain's full id: the behavior's, or work's top-level one.
			[[nodiscard]] std::string chainId(const std::string& chain) const {
				return library.findChain("test/" + chain).valid() ? "test/" + chain : chain;
			}
			[[nodiscard]] std::size_t finishedCount(const std::string& action) const { return std::ranges::count(finished, action); }
			[[nodiscard]] std::size_t startedCount(const std::string& action) const { return std::ranges::count(started, action); }

			/// Claims the index holds for `colonist`'s current task, after an AI update rebuilt it.
			[[nodiscard]] std::vector<Claim> indexedClaims(uint64_t instance) const {
				std::vector<Claim> claims;
				ai->claimIndex().forEachClaim([&](const Claim& claim, const ClaimHolder& holder) {
					if (holder.instanceId == instance) {
						claims.push_back(claim);
					}
				});
				return claims;
			}

			std::optional<ecs::testing::SyntheticEntries>	entries;
			assets::EntryLibrary							library;
			ActionEntries									actionEntries;
			ChainEntries									chainEntries;
			std::unique_ptr<ecs::testing::TestThoughtTypes> thoughts;
			std::unique_ptr<queries::QueryEvaluator>		evaluator;
			std::unique_ptr<World>							world;
			AIDecisionSystem*								ai = nullptr;
			ActionSystem*									actionSystem = nullptr;
			std::vector<std::string>						started;
			std::vector<std::string>						finished;
			std::vector<std::pair<std::string, uint32_t>>	dropped;
			/// The regrowing sources the runner's queries read: position x to when it is ready again.
			std::vector<std::pair<float, double>> regrowingAt;
			queries::RegrowthQuery				  regrowing = [this](uint32_t, glm::vec2 at, double) -> std::optional<double> {
				   for (const auto& [x, readyAt] : regrowingAt) {
					   if (x == at.x) {
						   return readyAt;
					   }
				   }
				   return std::nullopt;
			};
		};

	} // namespace

	// --- The 22 requirements on the chain runner ---

	TEST_F(ChainsTest, R01_TheActionGoesWithTheTaskWhenAChainIsReplacedOrTheCreatureTakenUnderControl) {
		const EntityID colonist = createColonist();
		ASSERT_TRUE(startChainAt(colonist, "dawdle", 0, placeTarget({0.0F, 0.0F})));
		run(0.5F);
		ASSERT_TRUE(action(colonist).isActive());
		ASSERT_TRUE(startChainAt(colonist, "dawdle", 0, placeTarget({4.0F, 0.0F})));
		EXPECT_FALSE(action(colonist).isActive()) << "the new chain's walk started with no action left from the old one";

		run(3.0F);
		ASSERT_TRUE(action(colonist).isActive());
		ai->enterControl(colonist);
		EXPECT_FALSE(action(colonist).isActive());
		EXPECT_FALSE(task(colonist).chain.active());
	}

	TEST_F(ChainsTest, R02_AChainKeepsItsIdentityAcrossDecisionsAndOneWhoseRouteFailedIsAFreshPick) {
		const EntityID colonist = createColonist();
		ASSERT_TRUE(startChainAt(colonist, "dawdle", 0, placeTarget({6.0F, 0.0F})));
		const uint64_t instance = task(colonist).instanceId;
		run(4.0F);
		EXPECT_TRUE(running(colonist, "dawdle"));
		EXPECT_EQ(task(colonist).instanceId, instance) << "decisions on the way and on arrival kept the chain";
		EXPECT_EQ(task(colonist).chain.frames[0].step, 1) << "its step moved on under the same task";

		ASSERT_TRUE(startChainAt(colonist, "dawdle", 0, placeTarget({-20.0F, 0.0F})));
		// As a route's answer leaves it: a path it walks, then no way found.
		world->addComponent<NavPath>(colonist, NavPath{.waypoints = {position(colonist), {-20.0F, 0.0F}}, .valid = true});
		task(colonist).navState = NavState::CantFindWayTo;
		world->update(kTick);
		EXPECT_FALSE(running(colonist, "dawdle")) << "a chain whose route failed is not held";
	}

	TEST_F(ChainsTest, R03_AnOptionOnTheChainsOwnRungNeverSwitchesIt) {
		const EntityID colonist = createColonist();
		const uint8_t  ordinary = assets::PriorityConfig::Get().decisionRungs().ordinaryNeed;
		remember(colonist, {1.0F, 0.0F}, "Pond", assets::CapabilityType::Drinkable); // water the ordinary thirst could go to
		ASSERT_TRUE(startChainAt(colonist, "dawdle", 0, placeTarget({0.0F, 0.0F}), {.tier = ordinary}));
		const uint64_t instance = task(colonist).instanceId;
		setNeed(colonist, NeedType::Thirst, 40.0F); // an ordinary need, the chain's rung
		run(3.0F);
		EXPECT_TRUE(running(colonist, "dawdle"));
		EXPECT_EQ(task(colonist).instanceId, instance);
		EXPECT_EQ(task(colonist).priorityTier, ordinary) << "the chain keeps the rung it was started on";
	}

	TEST_F(ChainsTest, R04_ANonInterruptableDoBlocksEverySwitchAndAnInterruptableOneYieldsToAHigherRung) {
		const EntityID colonist = createColonist();
		const uint8_t  ordinary = assets::PriorityConfig::Get().decisionRungs().ordinaryNeed;
		remember(colonist, {0.5F, 0.0F}, "Pond", assets::CapabilityType::Drinkable);
		ASSERT_TRUE(startChainAt(colonist, "linger", 0, placeTarget({0.0F, 0.0F}), {.tier = ordinary}));
		run(0.3F);
		setNeed(colonist, NeedType::Thirst, 5.0F);
		run(2.0F);
		EXPECT_TRUE(running(colonist, "linger")) << "a non-interruptable action holds even against a critical need";

		ASSERT_TRUE(startChainAt(colonist, "dawdle", 0, placeTarget({0.0F, 0.0F}), {.tier = ordinary}));
		EXPECT_TRUE(runUntil([&] { return task(colonist).type == TaskType::FulfillNeed; }, 1.0F))
			<< "an interruptable action yields to the critical need";
		EXPECT_FALSE(running(colonist, "dawdle"));
	}

	TEST_F(ChainsTest, R05_AChainThatEndsLeavesTheCreatureToDecideAtOnce) {
		const EntityID colonist = createColonist();
		inventory(colonist).addItem("Berry", 1);
		ASSERT_TRUE(startChainAt(colonist, "snack_then_settle", 0, {}));
		ASSERT_TRUE(runUntil([&] { return !task(colonist).isActive() || task(colonist).type != TaskType::Chain; }));
		const float before = world->getComponent<DecisionTrace>(colonist)->lastEvaluationTime;
		world->update(kTick);
		EXPECT_NE(world->getComponent<DecisionTrace>(colonist)->lastEvaluationTime, before) << "decided in the next update";
	}

	TEST_F(ChainsTest, R06_ATakeWhoseTargetIsNoLongerWhatItWasForgetsItAndEndsTheChain) {
		const EntityID	  colonist = createColonist();
		const ChainTarget rock = remember(colonist, {0.0F, 0.0F}, "Rock", assets::CapabilityType::Harvestable);
		ASSERT_TRUE(startChainAt(colonist, "take_target", 0, rock)) << "it starts at the rock";
		world->update(kTick);
		EXPECT_FALSE(running(colonist, "take_target")) << "and its take started nothing";
		EXPECT_EQ(startedCount("harvest") + startedCount("pickup"), 0U);
		EXPECT_EQ(memory(colonist).getWorldEntity(rock.key), nullptr) << "the stale memory of it is forgotten";
	}

	TEST_F(ChainsTest, R07_AConsumeWhoseFoodVanishedFinishesNothingAndBeginsNoNextStep) {
		const EntityID colonist = createColonist();
		inventory(colonist).addItem("Berry", 1);
		setNeed(colonist, NeedType::Digestion, 50.0F);
		ASSERT_TRUE(startChainAt(colonist, "snack_then_settle", 0, {}));
		run(0.3F);
		inventory(colonist).removeItem("Berry", 1);
		run(2.0F);
		EXPECT_EQ(finishedCount("test/snack"), 0U);
		EXPECT_FLOAT_EQ(need(colonist, NeedType::Digestion), 50.0F) << "the need step after it never ran";
		EXPECT_FALSE(running(colonist, "snack_then_settle"));
	}

	TEST_F(ChainsTest, R08_AChainWhoseRouteFailedIsLeftForWhatTheCreatureCanReach) {
		const EntityID colonist = createColonist();
		remember(colonist, {2.0F, 0.0F}, "Pond", assets::CapabilityType::Drinkable);
		setNeed(colonist, NeedType::Thirst, 40.0F);
		const uint8_t ordinary = assets::PriorityConfig::Get().decisionRungs().ordinaryNeed;
		ASSERT_TRUE(startChainAt(colonist, "dawdle", 0, placeTarget({-30.0F, 0.0F}), {.tier = ordinary}));
		world->addComponent<NavPath>(colonist, NavPath{.waypoints = {position(colonist), {-30.0F, 0.0F}}, .valid = true});
		task(colonist).navState = NavState::CantFindWayTo;
		world->update(kTick);
		EXPECT_EQ(task(colonist).type, TaskType::FulfillNeed) << "not frozen on the chain: the water it can reach is taken";
	}

	TEST_F(ChainsTest, R09_ATakeActsOnItsTargetByMemoryKeyFromAStandPointBesideIt) {
		const EntityID colonist = createColonist();
		ChainTarget	   bush = remember(colonist, {4.0F, 0.0F}, "BerryBush", assets::CapabilityType::Harvestable);
		remember(colonist, {2.6F, 0.0F}, "BerryBush", assets::CapabilityType::Harvestable); // a decoy nearer the stand point
		bush.goal = {2.5F, 0.0F};
		std::vector<glm::vec2> taken;
		actionSystem->setTakeSourceCallback([&](const std::string&, float x, float y, ActionSystem::SourceTake, float) {
			taken.emplace_back(x, y);
			return ActionSystem::SourceState::Ready;
		});
		ASSERT_TRUE(startChainAt(colonist, "take_target", 0, bush));
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "take_target"); }));
		ASSERT_EQ(taken.size(), 1U);
		EXPECT_FLOAT_EQ(taken[0].x, 4.0F) << "the bush it was sent to, not the one nearer where it stood";
		EXPECT_EQ(inventory(colonist).getQuantity("Berry"), 3U);
	}

	TEST_F(ChainsTest, R10_ANewLegInvalidatesTheOldPathAndLeavesTheAIToAskARoute) {
		const EntityID colonist = createColonist();
		world->addComponent<NavPath>(colonist, NavPath{});
		world->getComponent<NavPath>(colonist)->valid = true;
		ASSERT_TRUE(startChainAt(colonist, "dawdle", 0, placeTarget({5.0F, 0.0F})));
		EXPECT_FALSE(world->getComponent<NavPath>(colonist)->valid);
		EXPECT_EQ(task(colonist).state, TaskState::Moving);
		EXPECT_EQ(task(colonist).targetPosition, glm::vec2(5.0F, 0.0F));
		EXPECT_TRUE(world->getComponent<MovementTarget>(colonist)->active);
		EXPECT_EQ(world->getComponent<MovementTarget>(colonist)->target, glm::vec2(5.0F, 0.0F));
	}

	TEST_F(ChainsTest, R11_ADoStartsOnlyWithinReachOfItsTarget) {
		const EntityID colonist = createColonist();
		ASSERT_TRUE(startChainAt(colonist, "dawdle", 0, placeTarget({5.0F, 0.0F})));
		ASSERT_TRUE(runUntil([&] { return action(colonist).isActive(); }));
		EXPECT_LE(glm::distance(position(colonist), glm::vec2(5.0F, 0.0F)), 0.4F);
	}

	TEST_F(ChainsTest, R12_APackageACarrierLeavesForAHigherRungIsSetDownStillPackaged) {
		const EntityID colonist = createColonist();
		const EntityID crate = world->createEntity();
		world->addComponent<Position>(crate, Position{{0.0F, 0.0F}});
		world->addComponent<Appearance>(crate, Appearance{"Crate"});
		world->addComponent<Packaged>(crate, Packaged{.targetPosition = glm::vec2{30.0F, 0.0F}});
		remember(colonist, {0.5F, 0.0F}, "Pond", assets::CapabilityType::Drinkable);
		ASSERT_TRUE(startChainAt(colonist, "place", 0, entityTarget(crate, {0.0F, 0.0F}), {.destination = placeTarget({30.0F, 0.0F})}));
		ASSERT_TRUE(runUntil([&] { return inventory(colonist).carryingPackagedEntity.has_value(); }));
		inventory(colonist).addItem("Berry", 1);
		setNeed(colonist, NeedType::Hunger, 5.0F); // eating needs the hands the package fills
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "place"); }));
		EXPECT_FALSE(inventory(colonist).carryingPackagedEntity.has_value());
		EXPECT_TRUE(world->hasComponent<Packaged>(crate)) << "set down still packaged";
		EXPECT_FALSE(world->getComponent<Packaged>(crate)->beingCarried);
	}

	TEST_F(ChainsTest, R13_AGoQueryTargetsOnlyWhatIsRememberedAndPassesOverARegrowingSource) {
		const EntityID colonist = createColonist();
		EXPECT_FALSE(startChainAt(colonist, "gather", 0, {})) << "a bush it does not remember is no target";
		remember(colonist, {2.0F, 0.0F}, "BerryBush", assets::CapabilityType::Harvestable);
		const ChainTarget ready = remember(colonist, {5.0F, 0.0F}, "BerryBush", assets::CapabilityType::Harvestable);
		regrowingAt.emplace_back(2.0F, 1.0e9);
		ASSERT_TRUE(startChainAt(colonist, "gather", 0, {}));
		EXPECT_EQ(task(colonist).chain.stepTarget.key, ready.key);
	}

	TEST_F(ChainsTest, R14_TheSameSeedPicksTheSameRingPoint) {
		const EntityID colonist = createColonist();
		ASSERT_TRUE(startChainAt(colonist, "wander", 0, {}));
		const glm::vec2 first = task(colonist).targetPosition;
		// A second world seeded alike, its runner wired to the same library.
		World		  other;
		ActionSystem& otherActions = other.registerSystem<ActionSystem>(actionEntries, 42U);
		otherActions.setChains({.library = &library, .chains = &chainEntries, .evaluator = evaluator.get()});
		const EntityID again = other.createEntity();
		other.addComponent<Position>(again, Position{{0.0F, 0.0F}});
		other.addComponent<NeedsComponent>(again, ecs::testing::shippedNeeds());
		other.addComponent<Memory>(again, Memory{});
		other.addComponent<Inventory>(again, Inventory::createForColonist());
		other.addComponent<Task>(again, Task{});
		other.addComponent<Action>(again, Action{});
		ASSERT_TRUE(otherActions.startChain(again, {.chain = library.findChain("test/wander"), .tier = 0}));
		EXPECT_EQ(other.getComponent<Task>(again)->targetPosition, first);
	}

	TEST_F(ChainsTest, R15_AWalkingGoKeepsItsTargetWhenANearerOneAppears) {
		const EntityID	  colonist = createColonist();
		const ChainTarget far = remember(colonist, {8.0F, 0.0F}, "Pond", assets::CapabilityType::Drinkable);
		ASSERT_TRUE(startChainAt(colonist, "drink", 0, {}));
		run(0.5F);
		remember(colonist, {1.0F, 1.0F}, "Pond", assets::CapabilityType::Drinkable);
		run(1.0F);
		EXPECT_EQ(task(colonist).chain.stepTarget.key, far.key) << "no query runs while it walks to a target still there";
	}

	TEST_F(ChainsTest, R16_TaskClearResetsEveryChainFieldAndKeepsTheSuspendedRecord) {
		const EntityID colonist = createColonist();
		ASSERT_TRUE(startChainAt(colonist, "dawdle", 0, placeTarget({3.0F, 0.0F})));
		task(colonist).suspended = SuspendedWork{.instanceId = 99, .rootGoalId = 7, .itemDefNameId = id("Stick")};
		task(colonist).clear();
		const ChainProgress& progress = task(colonist).chain;
		EXPECT_FALSE(progress.active());
		EXPECT_EQ(progress.depth, 0);
		EXPECT_FALSE(progress.target.resolved());
		EXPECT_FALSE(progress.stepTarget.resolved());
		EXPECT_EQ(progress.stepStartedAt, 0.0);
		EXPECT_EQ(progress.takenQuantity, 0U);
		EXPECT_EQ(progress.goal.rootGoalId, 0U);
		ASSERT_TRUE(task(colonist).suspended.has_value());
		EXPECT_EQ(task(colonist).suspended->instanceId, 99U);
	}

	TEST_F(ChainsTest, R17_AChainWaitingForGroundKeepsItsTarget) {
		const EntityID colonist = createColonist();
		ASSERT_TRUE(startChainAt(colonist, "dawdle", 0, placeTarget({7.0F, 0.0F})));
		// As nav leaves a route whose ground is on its way.
		world->addComponent<NavPath>(colonist, NavPath{.waypoints = {position(colonist)}, .valid = true});
		task(colonist).navState = NavState::AwaitingMesh;
		world->getComponent<MovementTarget>(colonist)->active = false;
		run(2.0F);
		EXPECT_TRUE(running(colonist, "dawdle"));
		EXPECT_EQ(task(colonist).chain.stepTarget.position, glm::vec2(7.0F, 0.0F));
	}

	TEST_F(ChainsTest, R19_AGoQueryWhoseEveryCandidateRegrowsWaitsUntilOneIsReady) {
		const EntityID colonist = createColonist();
		remember(colonist, {3.0F, 0.0F}, "BerryBush", assets::CapabilityType::Harvestable);
		regrowingAt.emplace_back(3.0F, 1.0e9);
		EXPECT_FALSE(startChainAt(colonist, "gather", 0, {}));
		regrowingAt.clear();
		remember(colonist, {40.0F, 40.0F}, "Rock", assets::CapabilityType::Carryable); // its memory moves
		EXPECT_TRUE(startChainAt(colonist, "gather", 0, {})) << "walked again once something it read changed";
	}

	TEST_F(ChainsTest, R20_ARingPointFallsInsideItsRing) {
		const EntityID colonist = createColonist({10.0F, 10.0F});
		ASSERT_TRUE(startChainAt(colonist, "wander", 0, {}));
		const float distance = glm::distance(task(colonist).targetPosition, glm::vec2(10.0F, 10.0F));
		EXPECT_GE(distance, 2.0F);
		EXPECT_LE(distance, 6.0F);
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "wander"); }));
		EXPECT_FALSE(action(colonist).isActive()) << "a chain that ends on a go acts on nothing";
	}

	TEST_F(ChainsTest, R21_AReliefOrASleepWithNoBetterPlaceHappensWhereTheCreatureStands) {
		const EntityID colonist = createColonist({3.0F, 4.0F});
		ASSERT_TRUE(startChainAt(colonist, "relieve", 0, {}));
		world->update(kTick);
		ASSERT_TRUE(action(colonist).isActive()) << "started where it stands, with no walk";
		EXPECT_EQ(position(colonist), glm::vec2(3.0F, 4.0F));
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "relieve"); }));
		std::size_t piles = 0;
		for (auto [entity, appearance, at] : world->view<Appearance, Position>()) {
			piles += appearance.defName == "Misc_BioPile" && at.value == glm::vec2(3.0F, 4.0F) ? 1U : 0U;
		}
		EXPECT_EQ(piles, 1U);

		setNeed(colonist, NeedType::Energy, 20.0F);
		ASSERT_TRUE(startChainAt(colonist, "sleep", 0, {}));
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "sleep"); }));
		EXPECT_NEAR(need(colonist, NeedType::Energy), 20.0F + 40.0F * actionEntries.groundSleepQuality, 0.01F) << "at the ground's quality";
	}

	TEST_F(ChainsTest, R22_TwoDoStepsAtOnePlaceRunOneAfterTheOther) {
		const EntityID colonist = createColonist();
		remember(colonist, {2.0F, 0.0F}, "Pond", assets::CapabilityType::Drinkable);
		setNeed(colonist, NeedType::Thirst, 30.0F);
		setNeed(colonist, NeedType::Hygiene, 30.0F);
		ASSERT_TRUE(startChainAt(colonist, "drink_and_rinse", 0, {}));
		ASSERT_TRUE(runUntil([&] { return finishedCount("test/sip") == 1; }));
		EXPECT_EQ(finishedCount("test/rinse"), 0U);
		ASSERT_TRUE(runUntil([&] { return finishedCount("test/rinse") == 1; }));
		EXPECT_FLOAT_EQ(need(colonist, NeedType::Thirst), 70.0F);
		EXPECT_FLOAT_EQ(need(colonist, NeedType::Hygiene), 60.0F);
	}

	// --- Step kinds ---

	TEST_F(ChainsTest, AHaveMetSkipsItsFetch) {
		const EntityID colonist = createColonist();
		inventory(colonist).addItem("Berry", 1);
		setNeed(colonist, NeedType::Hunger, 50.0F);
		ASSERT_TRUE(startChainAt(colonist, "have_berry", 0, {}));
		EXPECT_EQ(task(colonist).chain.depth, 1) << "no fetch chain was run";
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "have_berry"); }));
		EXPECT_FLOAT_EQ(need(colonist, NeedType::Hunger), 80.0F);
	}

	TEST_F(ChainsTest, AHaveUnmetRunsItsFetchThenGoesOn) {
		const EntityID	  colonist = createColonist();
		const ChainTarget pile = remember(colonist, {3.0F, 0.0F}, "Berry", assets::CapabilityType::Carryable);
		setNeed(colonist, NeedType::Hunger, 50.0F);
		ASSERT_TRUE(startChainAt(colonist, "have_berry", 0, {}, {.source = pile}));
		EXPECT_EQ(task(colonist).chain.depth, 2) << "its fetch chain runs";
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "have_berry"); }));
		EXPECT_EQ(finishedCount("pickup"), 1U);
		EXPECT_EQ(finishedCount("test/snack"), 1U);
		EXPECT_FLOAT_EQ(need(colonist, NeedType::Hunger), 80.0F);
	}

	TEST_F(ChainsTest, AThinkStepGivesItsThought) {
		const EntityID colonist = createColonist();
		ASSERT_TRUE(startChainAt(colonist, "sip_and_think", 0, {}));
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "sip_and_think"); }));
		const Thoughts& held = *world->getComponent<Thoughts>(colonist);
		ASSERT_EQ(held.record.size(), 1U);
		EXPECT_EQ(held.record[0].eventType, thoughts->type("sipped"));
	}

	TEST_F(ChainsTest, ANeedStepMovesTheNeed) {
		const EntityID colonist = createColonist();
		inventory(colonist).addItem("Berry", 1);
		setNeed(colonist, NeedType::Digestion, 50.0F);
		ASSERT_TRUE(startChainAt(colonist, "snack_then_settle", 0, {}));
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "snack_then_settle"); }));
		EXPECT_FLOAT_EQ(need(colonist, NeedType::Digestion), 40.0F);
	}

	TEST_F(ChainsTest, ADropStepSetsDownWhatTheChainTookAndItsClaimsGo) {
		const EntityID	  colonist = createColonist();
		EntityID		  heap = kInvalidEntity;
		const ChainTarget pile = pileAt(colonist, {1.0F, 0.0F}, "Stick", 4, 1, &heap);
		ASSERT_TRUE(startChainAt(colonist, "take_and_drop", 0, pile));
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "take_and_drop"); }));
		EXPECT_EQ(finishedCount("pickup"), 1U);
		EXPECT_EQ(inventory(colonist).getQuantity("Stick"), 0U);
		ASSERT_EQ(dropped.size(), 1U);
		EXPECT_EQ(dropped[0], std::make_pair(std::string("Stick"), 1U));
		EXPECT_EQ(world->getComponent<ResourceStack>(heap)->quantity, 3U);
	}

	TEST_F(ChainsTest, ARunStepRunsItsChainThenTheNextStep) {
		const EntityID colonist = createColonist();
		remember(colonist, {2.0F, 0.0F}, "Pond", assets::CapabilityType::Drinkable);
		setNeed(colonist, NeedType::Thirst, 30.0F);
		setNeed(colonist, NeedType::Hunger, 50.0F);
		ASSERT_TRUE(startChainAt(colonist, "run_drink", 0, {}));
		EXPECT_EQ(task(colonist).chain.depth, 2);
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "run_drink"); }));
		EXPECT_FLOAT_EQ(need(colonist, NeedType::Thirst), 70.0F);
		EXPECT_FLOAT_EQ(need(colonist, NeedType::Hunger), 55.0F);
	}

	TEST_F(ChainsTest, TheLoadRefusesWhatTheRunnerCannotRun) {
		entries->write("behaviors/test/faulty.xml", R"(<behavior id="faulty">
  <chain id="leave"><leave_colony/></chain>
  <chain id="deep_1"><run chain="deep_2"/></chain>
  <chain id="deep_2"><run chain="deep_3"/></chain>
  <chain id="deep_3"><run chain="deep_4"/></chain>
  <chain id="deep_4"><run chain="deep_5"/></chain>
  <chain id="deep_5"><need id="hunger" change="1"/></chain>
  <chain id="half"><go to="destination"/><do action="deposit" claims="inbound"/></chain>
  <chain id="musing"><think thought="pondered" to="self"/></chain>
  <chain id="empty_drop"><go to="target"/><drop/></chain>
  <chain id="claimed_take"><go to="source"/><do action="pickup" claims="inbound in_flight"/></chain>
  <chain id="nowhere"><do action="deposit"/></chain>
  <query id="piles_only"><from pile="Stick"/><weigh fact="distance" by="-1"/></query>
  <chain id="wrong_kind"><go query="piles_only"/><do action="harvest"/></chain>
  <chain id="held_walk"><go to="target"><ends on="held_gone"/></go></chain>
</behavior>)");
		assets::EntryLoad loaded = entries->load();
		ASSERT_TRUE(loaded.errors.empty()) << ::testing::PrintToString(loaded.errors);
		const ActionEntriesLoad actions = resolveActionEntries(loaded.library);
		const ChainEntriesLoad	chains = resolveChainEntries(loaded.library, actions.actions, &thoughts->types);
		const std::string		all = ::testing::PrintToString(chains.errors);
		EXPECT_NE(all.find("<chain id=\\\"leave\\\"> step 1 <leave_colony> ends colony membership"), std::string::npos) << all;
		EXPECT_NE(all.find("<chain id=\\\"deep_1\\\"> nests chains more than 4 deep"), std::string::npos) << all;
		EXPECT_EQ(all.find("<chain id=\\\"deep_2\\\"> nests"), std::string::npos) << all;
		EXPECT_NE(all.find("<chain id=\\\"half\\\"> step 2 <do> names inbound without in_flight"), std::string::npos) << all;
		EXPECT_NE(all.find("<chain id=\\\"musing\\\"> step 1 <think> thought \\\"pondered\\\" is no thought type"), std::string::npos)
			<< all;
		EXPECT_NE(
			all.find("<chain id=\\\"empty_drop\\\"> step 2 <drop> sets down what the chain took, and no step before it takes"),
			std::string::npos
		) << all;
		EXPECT_NE(all.find("<chain id=\\\"claimed_take\\\"> step 2 <do> claims inbound or in_flight on a take"), std::string::npos) << all;
		EXPECT_NE(
			all.find("<chain id=\\\"nowhere\\\"> step 1 <do> performs \\\"deposit\\\", which acts on a target, with no `on`"),
			std::string::npos
		) << all;
		EXPECT_NE(
			all.find("<chain id=\\\"wrong_kind\\\"> step 2 <do> performs \\\"harvest\\\" on what query \\\"test/piles_only\\\" picks"),
			std::string::npos
		) << all;
		EXPECT_NE(all.find("<chain id=\\\"held_walk\\\"> step 1 <go> ends on held_gone"), std::string::npos) << all;
	}

	// --- Ways a step ends ---

	TEST_F(ChainsTest, ADoEndsOnItsTargetGoneAndTheChainGoesOn) {
		const EntityID	  colonist = createColonist();
		const ChainTarget pond = remember(colonist, {0.0F, 0.0F}, "Pond", assets::CapabilityType::Drinkable);
		setNeed(colonist, NeedType::Hygiene, 50.0F);
		ASSERT_TRUE(startChainAt(colonist, "dawdle_watching", 0, pond));
		run(1.0F);
		ASSERT_TRUE(action(colonist).isActive());
		memory(colonist).forgetWorldEntity({0.0F, 0.0F}, id("Pond"));
		run(0.3F);
		EXPECT_FALSE(running(colonist, "dawdle_watching"));
		EXPECT_EQ(finishedCount("test/dawdle"), 0U) << "cut short, it finishes nothing";
		EXPECT_FLOAT_EQ(need(colonist, NeedType::Hygiene), 43.0F) << "the step after it ran";
	}

	TEST_F(ChainsTest, ADoEndsWhenItsCreatureTargetLeaves) {
		const EntityID colonist = createColonist();
		const EntityID friendly = world->createEntity();
		world->addComponent<Position>(friendly, Position{{0.2F, 0.0F}});
		ChainTarget target = entityTarget(friendly, {0.2F, 0.0F});
		target.kind = ChainTarget::Kind::Mover;
		target.goal = {0.0F, 0.0F};
		setNeed(colonist, NeedType::Hygiene, 50.0F);
		ASSERT_TRUE(startChainAt(colonist, "dawdle_watching", 0, target));
		run(0.5F);
		world->getComponent<Position>(friendly)->value = {3.0F, 0.0F};
		run(0.3F);
		EXPECT_FALSE(running(colonist, "dawdle_watching"));
		EXPECT_FLOAT_EQ(need(colonist, NeedType::Hygiene), 43.0F);
	}

	TEST_F(ChainsTest, ADoEndsWhenWhatItHeldIsUsedUp) {
		const EntityID colonist = createColonist();
		inventory(colonist).addItem("Berry", 2);
		ASSERT_TRUE(startChainAt(colonist, "snack_watching", 0, {}));
		run(0.5F);
		inventory(colonist).removeItem("Berry", 2);
		run(0.3F);
		EXPECT_FALSE(running(colonist, "snack_watching"));
		EXPECT_EQ(finishedCount("test/nibble"), 0U);
	}

	TEST_F(ChainsTest, AGoWhoseTargetIsForgottenOnTheWayAsksItsQueryAgain) {
		const EntityID	  colonist = createColonist();
		const ChainTarget first = remember(colonist, {6.0F, 0.0F}, "Pond", assets::CapabilityType::Drinkable);
		const ChainTarget second = remember(colonist, {-8.0F, 0.0F}, "Pond", assets::CapabilityType::Drinkable);
		ASSERT_TRUE(startChainAt(colonist, "drink", 0, {}));
		ASSERT_EQ(task(colonist).chain.stepTarget.key, first.key);
		run(0.5F);
		memory(colonist).forgetWorldEntity({6.0F, 0.0F}, id("Pond"));
		run(0.2F);
		EXPECT_EQ(task(colonist).chain.stepTarget.key, second.key);
		memory(colonist).forgetWorldEntity({-8.0F, 0.0F}, id("Pond"));
		run(0.2F);
		EXPECT_FALSE(running(colonist, "drink")) << "with nothing left to walk to, the chain ends";
	}

	// --- Primitives started through the do path, with no factory ---

	TEST_F(ChainsTest, RestoreAndSpawnAndConsumeRunFromTheDoPath) {
		const EntityID colonist = createColonist();
		remember(colonist, {1.0F, 0.0F}, "Pond", assets::CapabilityType::Drinkable);
		setNeed(colonist, NeedType::Thirst, 30.0F);
		ASSERT_TRUE(startChainAt(colonist, "drink", 0, {}));
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "drink"); }));
		EXPECT_FLOAT_EQ(need(colonist, NeedType::Thirst), 70.0F);
		EXPECT_EQ(action(colonist).type, ActionType::None);

		inventory(colonist).addItem("Berry", 1);
		setNeed(colonist, NeedType::Hunger, 40.0F);
		ASSERT_TRUE(startChainAt(colonist, "snack_then_settle", 0, {}));
		world->update(kTick);
		EXPECT_EQ(action(colonist).type, ActionType::Chain);
		EXPECT_STREQ(actionName(action(colonist)), "test/snack");
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "snack_then_settle"); }));
		EXPECT_EQ(inventory(colonist).getQuantity("Berry"), 0U);
		EXPECT_FLOAT_EQ(need(colonist, NeedType::Hunger), 70.0F);
		ASSERT_TRUE(startChainAt(colonist, "snack_then_settle", 0, {}));
		world->update(kTick);
		EXPECT_FALSE(running(colonist, "snack_then_settle")) << "a consume with nothing held starts nothing";
	}

	TEST_F(ChainsTest, AGroupTakesFromABushWithItsHarvestMemberForItsDurability) {
		const EntityID colonist = createColonist();
		remember(colonist, {1.0F, 0.0F}, "BerryBush", assets::CapabilityType::Harvestable);
		ASSERT_TRUE(startChainAt(colonist, "gather", 0, {}));
		ASSERT_TRUE(runUntil([&] { return action(colonist).isActive(); }));
		EXPECT_STREQ(actionName(action(colonist)), "harvest");
		EXPECT_FLOAT_EQ(action(colonist).duration, 1.0F) << "durability 10 at the untrained rate of 10 a second";
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "gather"); }));
		EXPECT_EQ(inventory(colonist).getQuantity("Berry"), 3U);
	}

	TEST_F(ChainsTest, AGroupTakesFromAPileWithItsPickupMember) {
		const EntityID	  colonist = createColonist();
		EntityID		  heap = kInvalidEntity;
		const ChainTarget pile = pileAt(colonist, {1.0F, 0.0F}, "Berry", 5, 2, &heap);
		ASSERT_TRUE(startChainAt(colonist, "take_target", 0, pile));
		ASSERT_TRUE(runUntil([&] { return action(colonist).isActive(); }));
		EXPECT_STREQ(actionName(action(colonist)), "pickup");
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "take_target"); }));
		EXPECT_EQ(inventory(colonist).getQuantity("Berry"), 2U);
		EXPECT_EQ(world->getComponent<ResourceStack>(heap)->quantity, 3U) << "the pile keeps the rest";
	}

	TEST_F(ChainsTest, AGroupTakesFromABoxWithItsWithdrawMember) {
		const EntityID colonist = createColonist();
		const EntityID box = world->createEntity();
		world->addComponent<Position>(box, Position{{1.0F, 0.0F}});
		world->addComponent<Appearance>(box, Appearance{"Box"});
		world->addComponent<Inventory>(box, Inventory::createForStorage());
		world->getComponent<Inventory>(box)->addItem("Berry", 6);
		ChainTarget target = entityTarget(box, {1.0F, 0.0F});
		target.kind = ChainTarget::Kind::Box;
		target.item = id("Berry");
		target.claimKind = assets::ClaimKind::SourceQuantity;
		target.claimQuantity = 4;
		ASSERT_TRUE(startChainAt(colonist, "take_target", 0, target));
		ASSERT_TRUE(runUntil([&] { return action(colonist).isActive(); }));
		EXPECT_STREQ(actionName(action(colonist)), "withdraw");
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "take_target"); }));
		EXPECT_EQ(inventory(colonist).getQuantity("Berry"), 4U);
		EXPECT_EQ(world->getComponent<Inventory>(box)->getQuantity("Berry"), 2U);
	}

	TEST_F(ChainsTest, ADeliveryTakesThenPutsItsClaimsFollowingTheGoods) {
		const EntityID colonist = createColonist();
		const Root	   root = buildRoot("Berry", 3, {6.0F, 0.0F});
		createColonist({20.0F, 20.0F}); // a bystander, whose decisions rebuild the claim index every update
		EntityID		  heap = kInvalidEntity;
		const ChainTarget pile = pileAt(colonist, {2.0F, 0.0F}, "Berry", 3, 3, &heap);
		const ChainTarget destination = entityTarget(root.site, {6.0F, 0.0F});
		ASSERT_TRUE(startChainAt(
			colonist,
			"deliver",
			0,
			{},
			{.source = pile, .destination = destination, .goal = {.rootGoalId = root.goal, .item = id("Berry"), .quantity = 3}}
		));
		const uint64_t instance = task(colonist).instanceId;
		ASSERT_EQ(task(colonist).claims.size(), 3U) << "the source share, and the goods' inbound and in-flight, all taken at the start";
		world->update(kTick);
		EXPECT_EQ(indexedClaims(instance).size(), 3U) << "and indexed for every other creature";

		ASSERT_TRUE(runUntil([&] { return finishedCount("pickup") == 1; }));
		ASSERT_EQ(task(colonist).claims.size(), 2U) << "the source share is done with; the goods' claims are held across the leg";
		for (const Claim& claim : task(colonist).claims) {
			EXPECT_NE(claim.kind(), ClaimKind::SourceQuantity);
			EXPECT_EQ(claim.quantity(), 3U) << "rewritten to what the take lifted";
		}
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "deliver"); }));
		EXPECT_EQ(world->getComponent<StructureBlueprint>(root.site)->remaining("Berry"), 0U) << "recorded on the site's manifest";
		EXPECT_FALSE(
			world->isAlive(heap) && world->getComponent<ResourceStack>(heap) != nullptr &&
			world->getComponent<ResourceStack>(heap)->quantity > 0
		);
		EXPECT_EQ(inventory(colonist).getQuantity("Berry"), 0U);
		EXPECT_TRUE(task(colonist).claims.empty());
		world->update(kTick);
		EXPECT_TRUE(indexedClaims(instance).empty()) << "a finished chain leaves no claim";
	}

	TEST_F(ChainsTest, BuildWorksABlueprintUpAndDeconstructWorksItDown) {
		const EntityID colonist = createColonist();
		const EntityID site = world->createEntity();
		world->addComponent<Position>(site, Position{{2.0F, 0.0F}});
		StructureBlueprint blueprint;
		blueprint.phase = StructureBlueprint::BuildPhase::UnderConstruction;
		blueprint.workTotal = 10.0F;
		world->addComponent<StructureBlueprint>(site, blueprint);
		int built = 0;
		int razed = 0;
		actionSystem->setStructureCompletedCallback([&](EntityID) { ++built; });
		actionSystem->setStructureDeconstructedCallback([&](EntityID) { ++razed; });

		ChainTarget spot = placeTarget({2.0F, 1.0F});
		spot.claimKind = assets::ClaimKind::Slot;
		spot.key = 2;
		ASSERT_TRUE(startChainAt(colonist, "build", 0, {}, {.source = spot, .destination = entityTarget(site, {2.0F, 0.0F})}));
		ASSERT_EQ(task(colonist).claims.size(), 1U) << "a foundation's builder spot";
		EXPECT_EQ(task(colonist).claims[0].slotIndex(), 2U);
		ASSERT_TRUE(startChainAt(colonist, "build", 0, {}, {.destination = entityTarget(site, {2.0F, 0.0F})}));
		EXPECT_TRUE(task(colonist).claims.empty()) << "a build with no spot takes no claim";
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "build"); }));
		EXPECT_EQ(built, 1);
		EXPECT_FLOAT_EQ(world->getComponent<StructureBlueprint>(site)->workDone, 10.0F);

		ASSERT_TRUE(startChainAt(colonist, "tear_down", 0, {}, {.destination = entityTarget(site, {2.0F, 0.0F})}));
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "tear_down"); }));
		EXPECT_EQ(razed, 1);
		EXPECT_FLOAT_EQ(world->getComponent<StructureBlueprint>(site)->workDone, 0.0F);
	}

	TEST_F(ChainsTest, CraftMakesTheStationsJobForItsWorkAmount) {
		assets::RecipeDef recipe;
		recipe.defName = "Recipe_Stick";
		recipe.label = "Stick";
		recipe.inputs = {{.defName = "Berry", .count = 2}};
		recipe.outputs = {{.defName = "Stick", .count = 1}};
		recipe.workAmount = 150.0F;
		assets::RecipeRegistry::Get().registerTestRecipe(recipe);
		const EntityID colonist = createColonist();
		const EntityID station = world->createEntity();
		world->addComponent<Position>(station, Position{{1.0F, 0.0F}});
		world->addComponent<Inventory>(station, Inventory::createForStorage());
		world->getComponent<Inventory>(station)->addItem("Berry", 2);
		WorkQueue queue;
		queue.addJob("Recipe_Stick", 2);
		world->addComponent<WorkQueue>(station, queue);

		ASSERT_TRUE(startChainAt(colonist, "craft", 0, {}, {.destination = entityTarget(station, {1.0F, 0.0F})}));
		ASSERT_TRUE(runUntil([&] { return action(colonist).isActive(); }));
		EXPECT_FLOAT_EQ(action(colonist).duration, 1.5F) << "150 work ticks at 100 a second";
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "craft"); }));
		EXPECT_EQ(ecs::availableQuantity(inventory(colonist), "Stick"), 1U) << "the output in the crafter's hands";
		EXPECT_EQ(world->getComponent<Inventory>(station)->getQuantity("Berry"), 0U);
		EXPECT_EQ(world->getComponent<WorkQueue>(station)->getNextJob()->completed, 1U) << "one of the job's two made";

		// A put into the station's store is metered to the job's need.
		inventory(colonist).addItem("Berry", 5);
		ASSERT_TRUE(startChainAt(
			colonist,
			"deliver_for_goal",
			0,
			{},
			{.destination = entityTarget(station, {1.0F, 0.0F}), .goal = {.item = id("Berry"), .quantity = 5}}
		));
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "deliver_for_goal"); }));
		EXPECT_EQ(world->getComponent<Inventory>(station)->getQuantity("Berry"), 2U);
		EXPECT_EQ(inventory(colonist).getQuantity("Berry"), 3U);
	}

	TEST_F(ChainsTest, APackageIsLiftedThenInstalledAtItsPlacement) {
		const EntityID colonist = createColonist();
		const EntityID crate = world->createEntity();
		world->addComponent<Position>(crate, Position{{2.0F, 0.0F}});
		world->addComponent<Appearance>(crate, Appearance{"Crate"});
		world->addComponent<Packaged>(crate, Packaged{.targetPosition = glm::vec2{5.0F, 0.0F}});
		ASSERT_TRUE(startChainAt(colonist, "place", 0, entityTarget(crate, {2.0F, 0.0F}), {.destination = placeTarget({5.0F, 0.0F})}));
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "place"); }));
		EXPECT_EQ(finishedCount("pickup_packaged"), 1U);
		EXPECT_EQ(finishedCount("place_packaged"), 1U);
		EXPECT_FALSE(world->hasComponent<Packaged>(crate));
		EXPECT_EQ(world->getComponent<Position>(crate)->value, glm::vec2(5.0F, 0.0F));
		EXPECT_FALSE(inventory(colonist).carryingPackagedEntity.has_value());
	}

	// --- Claims on every exit ---

	TEST_F(ChainsTest, ClaimsGoWithTheChainOnInterruptionFailureAndDestruction) {
		const EntityID colonist = createColonist();
		createColonist({20.0F, 20.0F}); // a bystander, whose decisions rebuild the claim index every update
		const auto claimedBush = [&]() {
			const ChainTarget bush = remember(colonist, {4.0F, 0.0F}, "BerryBush", assets::CapabilityType::Harvestable);
			EXPECT_TRUE(startChainAt(colonist, "take_target", 0, bush));
			EXPECT_EQ(task(colonist).claims.size(), 1U);
			EXPECT_EQ(task(colonist).claims[0].kind(), ClaimKind::Use);
			return task(colonist).instanceId;
		};

		uint64_t instance = claimedBush();
		world->update(kTick);
		ASSERT_EQ(indexedClaims(instance).size(), 1U) << "held while it walks";
		ai->enterControl(colonist);
		EXPECT_TRUE(task(colonist).claims.empty());
		ai->releaseControl(colonist);
		world->update(kTick);
		EXPECT_TRUE(indexedClaims(instance).empty()) << "interrupted";

		instance = claimedBush();
		memory(colonist).forgetWorldEntity({4.0F, 0.0F}, id("BerryBush"));
		world->update(kTick);
		world->update(kTick);
		EXPECT_FALSE(running(colonist, "take_target"));
		EXPECT_TRUE(indexedClaims(instance).empty()) << "its target gone";

		instance = claimedBush();
		world->update(kTick);
		world->destroyEntity(colonist);
		world->update(kTick);
		EXPECT_TRUE(indexedClaims(instance).empty()) << "its creature destroyed";
	}

	TEST_F(ChainsTest, AHaulClaimsItsAmountAtTheSourceAndItsAmountWithTheCargoForTheGoods) {
		const EntityID colonist = createColonist();
		const Root	   root = buildRoot("Stick", 10, {8.0F, 0.0F});
		inventory(colonist).addItem("Stick", 2); // the cargo handed to it
		const ChainTarget pile = pileAt(colonist, {2.0F, 0.0F}, "Stick", 4, 3);
		ASSERT_TRUE(startChainAt(
			colonist,
			"deliver",
			0,
			{},
			{.source = pile,
			 .destination = entityTarget(root.site, {8.0F, 0.0F}),
			 .goal = {.rootGoalId = root.goal, .item = id("Stick"), .quantity = 3, .cargo = 2}}
		));
		EXPECT_EQ(claimed(task(colonist), ClaimKind::SourceQuantity), 3U);
		EXPECT_EQ(claimed(task(colonist), ClaimKind::Inbound), 5U);
		EXPECT_EQ(claimed(task(colonist), ClaimKind::InFlight), 5U);
	}

	TEST_F(ChainsTest, ACutClaimsItsCargoInboundAndTheCargoWithItsExpectedYieldInFlight) {
		const EntityID	  colonist = createColonist();
		const Root		  root = buildRoot("Stick", 10, {8.0F, 0.0F});
		const ChainTarget bush = remember(colonist, {2.0F, 0.0F}, "StickBush", assets::CapabilityType::Harvestable);
		inventory(colonist).addItem("Stick", 1);
		ASSERT_TRUE(startChainAt(
			colonist,
			"cut_and_deliver",
			0,
			{},
			{.source = bush,
			 .destination = entityTarget(root.site, {8.0F, 0.0F}),
			 .goal = {.rootGoalId = root.goal, .item = id("Stick"), .cargo = 1, .expectedYield = 3}}
		));
		EXPECT_EQ(claimed(task(colonist), ClaimKind::Use), 1U);
		EXPECT_EQ(claimed(task(colonist), ClaimKind::Inbound), 1U);
		EXPECT_EQ(claimed(task(colonist), ClaimKind::InFlight), 4U);

		// With nothing handed over, its uncut goods are no cargo: a route that fails at once leaves nothing it carries.
		inventory(colonist).removeItem("Stick", 1);
		ASSERT_TRUE(startChainAt(
			colonist,
			"cut_and_deliver",
			0,
			{},
			{.source = bush,
			 .destination = entityTarget(root.site, {8.0F, 0.0F}),
			 .goal = {.rootGoalId = root.goal, .item = id("Stick"), .expectedYield = 3}}
		));
		EXPECT_EQ(claimed(task(colonist), ClaimKind::Inbound), 0U);
		EXPECT_EQ(cargoOnClaims(task(colonist).claims, root.goal, id("Stick")), 0U);
	}

	TEST_F(ChainsTest, ACutWithFullHandsEndsAtTheSourceHoldingNoClaims) {
		const EntityID colonist = createColonist();
		const Root	   root = buildRoot("Log", 10, {12.0F, 0.0F});
		ecs::addArmful(inventory(colonist), assets::AssetRegistry::Get(), "Boulder", 1);
		const ChainTarget tree = remember(colonist, {1.0F, 0.0F}, "Tree", assets::CapabilityType::Harvestable);
		ASSERT_TRUE(startChainAt(
			colonist,
			"cut_and_deliver",
			0,
			{},
			{.source = tree,
			 .destination = entityTarget(root.site, {12.0F, 0.0F}),
			 .goal = {.rootGoalId = root.goal, .item = id("Log"), .expectedYield = 3}}
		));
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "cut_and_deliver"); }));
		EXPECT_EQ(finishedCount("harvest"), 1U) << "the tree fell";
		EXPECT_LT(position(colonist).x, 3.0F) << "it never set off for the site";
		EXPECT_TRUE(task(colonist).claims.empty());
	}

	TEST_F(ChainsTest, AHaveCountsOnlyGoodsBeyondTheHandedCargo) {
		const EntityID colonist = createColonist();
		const Root	   root = buildRoot("Stick", 10, {6.0F, 0.0F});
		inventory(colonist).addItem("Stick", 5);
		const ChainTarget pile = pileAt(colonist, {-3.0F, 0.0F}, "Stick", 4, 4);
		ASSERT_TRUE(startChainAt(
			colonist,
			"haul_from_pile",
			0,
			{},
			{.source = pile,
			 .destination = entityTarget(root.site, {6.0F, 0.0F}),
			 .goal = {.rootGoalId = root.goal, .item = id("Stick"), .quantity = 5}}
		));
		EXPECT_EQ(task(colonist).chain.depth, 1) << "five held for a goal of five: no fetch";
		EXPECT_GT(task(colonist).targetPosition.x, 0.0F) << "on its way to the site";
	}

	TEST_F(ChainsTest, ACutsClaimsAreRewrittenToWhatItLiftedCappedAtTheRootsNeed) {
		const EntityID	  colonist = createColonist();
		const Root		  root = buildRoot("Stick", 6, {12.0F, 0.0F});
		const ChainTarget bush = remember(colonist, {1.0F, 0.0F}, "StickBush", assets::CapabilityType::Harvestable);
		ASSERT_TRUE(startChainAt(
			colonist,
			"cut_and_deliver",
			0,
			{},
			{.source = bush,
			 .destination = entityTarget(root.site, {12.0F, 0.0F}),
			 .goal = {.rootGoalId = root.goal, .item = id("Stick"), .expectedYield = 3}}
		));
		ASSERT_TRUE(runUntil([&] { return finishedCount("harvest") == 1; }));
		EXPECT_EQ(claimed(task(colonist), ClaimKind::InFlight), 5U) << "planned 3, the cut gave 5, the root needs 6";
		EXPECT_EQ(claimed(task(colonist), ClaimKind::Inbound), 5U);
	}

	TEST_F(ChainsTest, TwoCreaturesRequeryingInOneUpdateNeverBothTakeOneThing) {
		const uint8_t  idle = assets::PriorityConfig::Get().decisionRungs().idle;
		const EntityID first = createColonist({0.0F, 0.0F});
		const EntityID second = createColonist({0.0F, 1.0F});
		ChainTarget	   gone;
		ChainTarget	   left;
		for (const EntityID colonist : {first, second}) {
			gone = remember(colonist, {-10.0F, 0.0F}, "StickBush", assets::CapabilityType::Harvestable);
			left = remember(colonist, {10.0F, 0.0F}, "StickBush", assets::CapabilityType::Harvestable);
		}
		for (const EntityID colonist : {first, second}) {
			ASSERT_TRUE(startChainAt(colonist, "gather_sticks", 0, gone, {.tier = idle}));
		}
		world->update(kTick);
		for (const EntityID colonist : {first, second}) {
			memory(colonist).forgetWorldEntity({-10.0F, 0.0F}, id("StickBush"));
		}
		world->update(kTick);
		int onLeft = 0;
		for (const EntityID colonist : {first, second}) {
			onLeft += running(colonist, "gather_sticks") && task(colonist).chain.stepTarget.key == left.key ? 1 : 0;
		}
		EXPECT_EQ(onLeft, 1) << "the first to ask again takes the bush, and the second reads its claim";
	}

	TEST_F(ChainsTest, ACarriedPutDeliversTheCargoItsClaimsCoverAndTheDefaultPutTheGoal) {
		const EntityID colonist = createColonist();
		const EntityID box = world->createEntity();
		world->addComponent<Position>(box, Position{{1.0F, 0.0F}});
		world->addComponent<Inventory>(box, Inventory::createForStorage());
		inventory(colonist).addItem("Stick", 7);
		ASSERT_TRUE(startChainAt(
			colonist,
			"deliver_carried",
			0,
			{},
			{.destination = entityTarget(box, {1.0F, 0.0F}), .goal = {.rootGoalId = 77, .item = id("Stick"), .quantity = 3}}
		));
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "deliver_carried"); }));
		EXPECT_EQ(world->getComponent<Inventory>(box)->getQuantity("Stick"), 3U) << "the inbound it held, not all seven";
		ASSERT_TRUE(startChainAt(
			colonist,
			"deliver_for_goal",
			0,
			{},
			{.destination = entityTarget(box, {1.0F, 0.0F}), .goal = {.item = id("Stick"), .quantity = 2}}
		));
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "deliver_for_goal"); }));
		EXPECT_EQ(world->getComponent<Inventory>(box)->getQuantity("Stick"), 5U) << "the goal's two";
		EXPECT_EQ(inventory(colonist).getQuantity("Stick"), 2U);
	}

	TEST_F(ChainsTest, ATakeOfGoodsAlreadyCarriedCountsWhatTheChainTookOnceAndAsksNothingOfItsSource) {
		const EntityID	  colonist = createColonist();
		const Root		  root = buildRoot("Stick", 10, {12.0F, 0.0F});
		const ChainTarget first = pileAt(colonist, {1.0F, 0.0F}, "Stick", 2, 2);
		const ChainTarget second = pileAt(colonist, {-1.0F, 0.0F}, "Stick", 2, 2);
		ASSERT_TRUE(startChainAt(
			colonist, "take_twice", 0, first, {.source = second, .goal = {.rootGoalId = root.goal, .item = id("Stick"), .quantity = 2}}
		));
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "take_twice"); }));
		EXPECT_EQ(finishedCount("pickup"), 2U) << "the second take lifts its own two";
		EXPECT_EQ(inventory(colonist).getQuantity("Stick"), 4U);

		// Goods beyond what it was handed, at a source it no longer remembers: done as lifted, not refused.
		ChainTarget stale = second;
		stale.key = hashWorldEntity({30.0F, 30.0F}, id("Stick"));
		stale.position = stale.goal = position(colonist);
		ASSERT_TRUE(
			startChainAt(colonist, "take_target", 0, stale, {.goal = {.rootGoalId = root.goal, .item = id("Stick"), .quantity = 2}})
		);
		world->update(kTick);
		EXPECT_FALSE(running(colonist, "take_target"));
		EXPECT_FALSE(task(colonist).decideAtInterval) << "it finished; nothing refused";
		EXPECT_EQ(startedCount("pickup"), 2U);
	}

	TEST_F(ChainsTest, ARefusedStartDecidesAgainOnlyAtTheInterval) {
		const EntityID	  colonist = createColonist();
		const ChainTarget tree = remember(colonist, {0.0F, 0.0F}, "IronTree", assets::CapabilityType::Harvestable);
		ASSERT_TRUE(startChainAt(colonist, "take_target", 0, tree));
		world->update(kTick);
		EXPECT_FALSE(running(colonist, "take_target")) << "no axe: the cut starts nothing";
		EXPECT_EQ(startedCount("harvest"), 0U);
		const float refusedAt = world->getComponent<DecisionTrace>(colonist)->lastEvaluationTime;
		world->update(kTick);
		EXPECT_FLOAT_EQ(world->getComponent<DecisionTrace>(colonist)->lastEvaluationTime, refusedAt) << "no decision the next frame";
		run(0.6F);
		EXPECT_NE(world->getComponent<DecisionTrace>(colonist)->lastEvaluationTime, refusedAt) << "one at the interval";
	}

	TEST_F(ChainsTest, AGoWhoseTargetIsGoneEndsTheChainAndAGoFollowsACreature) {
		const EntityID colonist = createColonist();
		const EntityID box = world->createEntity();
		world->addComponent<Position>(box, Position{{8.0F, 0.0F}});
		ASSERT_TRUE(startChainAt(colonist, "go_watching", 0, entityTarget(box, {8.0F, 0.0F})));
		run(0.5F);
		world->destroyEntity(box);
		run(0.3F);
		EXPECT_FALSE(running(colonist, "go_watching"));
		EXPECT_EQ(startedCount("test/sip"), 0U) << "nothing to act on";

		const EntityID friendly = world->createEntity();
		world->addComponent<Position>(friendly, Position{{10.0F, 0.0F}});
		ChainTarget target = entityTarget(friendly, {10.0F, 0.0F});
		target.kind = ChainTarget::Kind::Mover;
		ASSERT_TRUE(startChainAt(colonist, "dawdle", 0, target));
		world->getComponent<Position>(friendly)->value = {10.0F, 6.0F};
		world->update(kTick);
		EXPECT_EQ(task(colonist).targetPosition, glm::vec2(10.0F, 6.0F)) << "it walks on toward where the creature now stands";
	}

	TEST_F(ChainsTest, ADoOnABoxOrAnEntityEndsWhenItIsGoneOrMoved) {
		const EntityID colonist = createColonist();
		const EntityID box = world->createEntity();
		world->addComponent<Position>(box, Position{{0.2F, 0.0F}});
		ChainTarget boxTarget = entityTarget(box, {0.2F, 0.0F});
		boxTarget.kind = ChainTarget::Kind::Box;
		boxTarget.goal = {0.0F, 0.0F};
		setNeed(colonist, NeedType::Hygiene, 50.0F);
		ASSERT_TRUE(startChainAt(colonist, "dawdle_watching", 0, boxTarget));
		run(0.3F);
		world->destroyEntity(box);
		run(0.3F);
		EXPECT_FALSE(running(colonist, "dawdle_watching"));
		EXPECT_FLOAT_EQ(need(colonist, NeedType::Hygiene), 43.0F) << "the box gone ends the step and the chain goes on";

		const EntityID station = world->createEntity();
		world->addComponent<Position>(station, Position{{0.2F, 0.0F}});
		ChainTarget stationTarget = entityTarget(station, {0.2F, 0.0F});
		stationTarget.goal = {0.0F, 0.0F};
		ASSERT_TRUE(startChainAt(colonist, "dawdle_watching", 0, stationTarget));
		run(0.3F);
		world->getComponent<Position>(station)->value = {3.0F, 0.0F};
		run(0.3F);
		EXPECT_FALSE(running(colonist, "dawdle_watching"));
		EXPECT_FLOAT_EQ(need(colonist, NeedType::Hygiene), 36.0F) << "an entity no longer where it was is gone";
	}

	TEST_F(ChainsTest, ASleepInABedRestoresAtTheBedsQuality) {
		const EntityID colonist = createColonist();
		remember(colonist, {1.0F, 0.0F}, "Bed", assets::CapabilityType::Sleepable);
		setNeed(colonist, NeedType::Energy, 20.0F);
		ASSERT_TRUE(startChainAt(colonist, "sleep_in_bed", 0, {}));
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "sleep_in_bed"); }));
		EXPECT_NEAR(need(colonist, NeedType::Energy), 60.0F, 0.01F) << "a bed's quality is 1";
	}

	TEST_F(ChainsTest, AHaveIsMetByAPackageAlreadyAtItsPlacement) {
		const EntityID colonist = createColonist();
		const EntityID crate = world->createEntity();
		world->addComponent<Position>(crate, Position{{3.0F, 0.0F}});
		world->addComponent<Appearance>(crate, Appearance{"Crate"});
		world->addComponent<Packaged>(crate, Packaged{.targetPosition = glm::vec2{3.0F, 0.0F}});
		ASSERT_TRUE(startChainAt(colonist, "place", 0, entityTarget(crate, {3.0F, 0.0F}), {.destination = placeTarget({3.0F, 0.0F})}));
		EXPECT_EQ(task(colonist).chain.depth, 1) << "nothing to fetch";
		ASSERT_TRUE(runUntil([&] { return !running(colonist, "place"); }));
		EXPECT_EQ(startedCount("pickup_packaged"), 0U);
		EXPECT_EQ(finishedCount("place_packaged"), 1U);
		EXPECT_FALSE(world->hasComponent<Packaged>(crate));
	}

} // namespace ecs::test
