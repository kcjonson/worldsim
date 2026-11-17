#include "ThoughtSourceSystem.h"

#include "../ShippedNeeds.test.h"
#include "../ThoughtTesting.test.h"
#include "../World.h"
#include "../components/Appearance.h"
#include "../components/Colonist.h"
#include "../components/Inventory.h"
#include "../components/Knowledge.h"
#include "../components/Memory.h"
#include "../components/Needs.h"
#include "../components/Task.h"
#include "../components/Transform.h"
#include "ActionSystem.h"
#include "ThoughtSystem.h"
#include "TimeSystem.h"
#include "VisionSystem.h"

#include <assets/AssetRegistry.h>
#include <assets/RecipeRegistry.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace ecs::test {

	namespace {
		constexpr uint32_t kSeed = 20261117;

		constexpr std::string_view kNeedTypes = R"(
  <thought id="peckish" importance="trivial">
    <source trigger="needFalls" need="Hunger" threshold="seek"/>
    <feeling is="annoyed"/>
  </thought>
  <thought id="starving" importance="normal">
    <source trigger="needFalls" need="Hunger" threshold="critical"/>
    <feeling is="miserable"/>
  </thought>
  <thought id="fed" importance="trivial">
    <source trigger="needRises" need="Hunger" threshold="seek"/>
    <feeling is="relieved"/>
  </thought>
)";

		constexpr std::string_view kActionTypes = R"(
  <thought id="dozing_off" importance="trivial">
    <source trigger="actionStarts" action="Sleep"/>
    <feeling is="content"/>
  </thought>
  <thought id="slept_rough" importance="normal">
    <source trigger="actionFinishes" action="Sleep" on="ground"/>
    <feeling is="annoyed" mood="-5" hours="8"/>
  </thought>
  <thought id="slept_well" importance="normal">
    <source trigger="actionFinishes" action="Sleep" on="sleepable"/>
    <feeling is="content"/>
  </thought>
  <thought id="first_night_there" importance="normal">
    <source trigger="actionFinishes" action="Sleep" first="true"/>
    <feeling is="delighted"/>
  </thought>
)";

		constexpr std::string_view kIdleTypes = R"(
  <thought id="musing" importance="trivial">
    <source trigger="idle" everyMinutes="60" chance="1"/>
    <feeling is="content"/>
  </thought>
  <thought id="coin_toss" importance="trivial">
    <source trigger="idle" everyMinutes="10" chance="0.5"/>
    <feeling is="delighted"/>
  </thought>
)";

		/// A world with the thought systems and the systems they hear, and one colonist.
		class Harness {
		  public:
			explicit Harness(std::string_view types, uint32_t seed = kSeed)
				: rules(types) {
				for (const std::string& error : rules.errors) {
					ADD_FAILURE() << error;
				}
				time = &world.registerSystem<TimeSystem>();
				thoughts = &world.registerSystem<ThoughtSystem>(rules.types, seed);
				vision = &world.registerSystem<VisionSystem>();
				vision->setUpdateInterval(1);
				ActionSystem& actions = world.registerSystem<ActionSystem>(seed);
				world.registerSystem<ThoughtSourceSystem>(rules.types, seed).listenTo(actions, *vision);
				thoughts->addArrivedObserver([this](EntityID who, const Thought& thought, ThoughtArrival arrival) {
					if (who != colonist) {
						return;
					}
					arrivals.emplace_back(rules.types.type(thought.eventType).name, arrival);
				});

				colonist = addColonist("Ada");
			}

			/// A colonist at the origin with what the thought sources and the systems they hear read.
			EntityID addColonist(const std::string& name) {
				const EntityID added = world.createEntity();
				world.addComponent<Colonist>(added, Colonist{.name = name});
				world.addComponent<Position>(added, Position{{0.0F, 0.0F}});
				world.addComponent<NeedsComponent>(added, shippedNeeds());
				world.addComponent<Memory>(added, Memory{});
				world.addComponent<Knowledge>(added, Knowledge{});
				world.addComponent<Inventory>(added, Inventory::createForColonist());
				world.addComponent<Task>(added, Task{});
				world.addComponent<Action>(added, Action{});
				return added;
			}

			void setHunger(float value) { world.getComponent<NeedsComponent>(colonist)->hunger().value = value; }

			void advanceGameMinutes(float minutes) {
				time->skipTime(minutes);
				world.update(0.0F);
			}

			/// Sleeps where he stands, about the kind remembered under `sourceKey` (0 for the bare ground).
			void sleep(uint64_t sourceKey) {
				Task& task = *world.getComponent<Task>(colonist);
				task.type = TaskType::FulfillNeed;
				task.state = TaskState::Arrived;
				task.needToFulfill = NeedType::Energy;
				task.targetPosition = world.getComponent<Position>(colonist)->value;
				task.needSourceKey = sourceKey;
				world.update(100.0F);
			}

			[[nodiscard]] long count(std::string_view type, ThoughtArrival arrival = ThoughtArrival::New) const {
				return std::ranges::count(arrivals, std::pair<std::string, ThoughtArrival>(type, arrival));
			}

			testing::TestThoughtTypes							rules;
			World												world;
			TimeSystem*											time = nullptr;
			ThoughtSystem*										thoughts = nullptr;
			VisionSystem*										vision = nullptr;
			EntityID											colonist = kInvalidEntity;
			std::vector<std::pair<std::string, ThoughtArrival>> arrivals;
		};
	} // namespace

	TEST(ThoughtSources, ANeedThoughtArrivesOncePerChangeAndNeverWhileTheNeedStaysWhereItIs) {
		Harness harness(kNeedTypes);
		harness.world.update(0.0F);
		EXPECT_TRUE(harness.arrivals.empty()) << "the first look is a starting point, not a change";

		harness.setHunger(45.0F);
		harness.world.update(0.0F);
		harness.world.update(0.0F);
		harness.world.update(0.0F);
		EXPECT_EQ(harness.count("peckish"), 1);

		// Below urgent and critical in one step: told once, by the level it lands in.
		harness.setHunger(5.0F);
		harness.world.update(0.0F);
		harness.world.update(0.0F);
		EXPECT_EQ(harness.count("starving"), 1);
		EXPECT_EQ(harness.count("peckish"), 1);

		harness.setHunger(80.0F);
		harness.world.update(0.0F);
		EXPECT_EQ(harness.count("fed"), 1);

		harness.setHunger(30.0F);
		harness.world.update(0.0F);
		EXPECT_EQ(harness.count("peckish"), 2);
		EXPECT_EQ(harness.arrivals.size(), 4U);

		// The levels are the colonist's own thresholds, the ones the AI seeks by: a seek of 60 moves the thought too.
		harness.setHunger(80.0F);
		harness.world.update(0.0F);
		harness.world.getComponent<NeedsComponent>(harness.colonist)->hunger().seekThreshold = 60.0F;
		harness.setHunger(55.0F);
		harness.world.update(0.0F);
		EXPECT_EQ(harness.count("peckish"), 3);
	}

	TEST(ThoughtSources, AnActionThoughtFollowsWhereItHappenedAndAFirstIsOncePerKind) {
		auto& registry = assets::AssetRegistry::Get();
		registry.clearDefinitions();
		// Id 0 is the registry's "no definition", which an action about no thing reports.
		registry.registerSyntheticDefinition("Test_Thought_IdZeroReservation", 0);
		const uint32_t bed = registry.registerSyntheticDefinition(
			"Test_Thought_Bed", static_cast<uint16_t>(1U << static_cast<unsigned>(assets::CapabilityType::Sleepable))
		);
		Harness		   harness(kActionTypes);
		Memory&		   memory = *harness.world.getComponent<Memory>(harness.colonist);
		const uint64_t bedKey = hashWorldEntity({0.0F, 0.0F}, bed);
		memory.rememberWorldEntity({0.0F, 0.0F}, bed, registry.getCapabilityMask(bed));

		harness.sleep(0);
		EXPECT_EQ(harness.count("dozing_off"), 1);
		EXPECT_EQ(harness.count("slept_rough"), 1);
		EXPECT_EQ(harness.count("first_night_there"), 1);
		EXPECT_EQ(harness.count("slept_well"), 0);

		harness.sleep(0);
		EXPECT_EQ(harness.count("slept_rough", ThoughtArrival::Repeat), 1) << "a second night on the ground refreshes the first";
		EXPECT_EQ(harness.count("first_night_there"), 1);

		harness.sleep(bedKey);
		EXPECT_EQ(harness.count("slept_well"), 1);
		EXPECT_EQ(harness.count("first_night_there"), 2) << "the first night in a bed is a first of its own";
		EXPECT_EQ(harness.count("slept_rough"), 1);

		registry.clearDefinitions();
	}

	TEST(ThoughtSources, AnIdleRollWaitsForQuietAndFollowsTheSeed) {
		Harness harness(R"(
  <thought id="musing" importance="trivial">
    <source trigger="idle" everyMinutes="60" chance="1"/>
    <feeling is="content"/>
  </thought>
  <thought id="nudge" importance="trivial">
    <feeling is="annoyed"/>
  </thought>
)");
		harness.world.update(0.0F);
		harness.advanceGameMinutes(30.0F);
		EXPECT_EQ(harness.count("musing"), 0) << "no roll before its first interval";
		harness.advanceGameMinutes(31.0F);
		EXPECT_EQ(harness.count("musing"), 1);

		harness.advanceGameMinutes(39.0F);
		harness.thoughts->create({.colonist = harness.colonist, .eventType = harness.rules.type("nudge")});
		harness.advanceGameMinutes(22.0F);
		EXPECT_EQ(harness.count("musing"), 1) << "a thought arrived within the interval: nothing else on his mind is false";
		harness.advanceGameMinutes(61.0F);
		EXPECT_EQ(harness.count("musing"), 2);

		const auto rollsUnder = [](uint32_t seed, bool withCompany) {
			Harness seeded(kIdleTypes, seed);
			if (withCompany) {
				seeded.addColonist("Grace");
			}
			seeded.world.update(0.0F);
			for (int roll = 0; roll < 40; ++roll) {
				seeded.advanceGameMinutes(11.0F);
			}
			return seeded.arrivals;
		};
		const auto first = rollsUnder(7, false);
		EXPECT_EQ(first, rollsUnder(7, false));
		EXPECT_EQ(first, rollsUnder(7, true)) << "another colonist rolls on a stream of his own";
		EXPECT_GT(std::ranges::count(first, std::pair<std::string, ThoughtArrival>("coin_toss", ThoughtArrival::New)), 0);
	}

	TEST(ThoughtSources, AFirstSightIsAboutANamedKindAndTheLandingLookIsSilent) {
		auto& registry = assets::AssetRegistry::Get();
		registry.clearDefinitions();
		assets::AssetDefinition berry;
		berry.defName = "Berry";
		berry.label = "Berry";
		berry.capabilities.edible = assets::EdibleCapability{};
		registry.registerTestDefinition(std::move(berry));
		assets::AssetDefinition stick;
		stick.defName = "Stick";
		stick.label = "Stick";
		registry.registerTestDefinition(std::move(stick));
		assets::AssetDefinition unnamed;
		unnamed.defName = "Test_Thought_Unnamed";
		registry.registerTestDefinition(std::move(unnamed));
		assets::RecipeDef recipe;
		recipe.defName = "Recipe_Test_BerryStick";
		recipe.label = "Berry on a stick";
		recipe.inputs.push_back({"Berry", 0, 1});
		recipe.outputs.push_back({"Stick", 0, 1});
		assets::RecipeRegistry::Get().registerTestRecipe(recipe);

		Harness	   harness(R"(
  <thought id="noticed" importance="trivial">
    <source trigger="firstSight"/>
    <feeling is="content"/>
    <line>Look, a {subject}.</line>
  </thought>
  <thought id="noticed_food" importance="trivial">
    <source trigger="firstSight" of="edible"/>
    <feeling is="delighted"/>
  </thought>
  <thought id="aha" importance="normal">
    <source trigger="recipeLearned"/>
    <feeling is="delighted"/>
  </thought>
)");
		const auto place = [&harness](const char* defName, glm::vec2 where) {
			const EntityID thing = harness.world.createEntity();
			harness.world.addComponent<Position>(thing, Position{where});
			harness.world.addComponent<Appearance>(thing, Appearance{.defName = defName});
		};

		// The landing look learns the stick by the landing site, and creates nothing.
		place("Stick", {1.0F, 0.0F});
		harness.vision->rememberArea(harness.colonist, {0.0F, 0.0F}, 2.0F);
		harness.world.update(0.0F);
		EXPECT_TRUE(harness.arrivals.empty());

		// Later sights: a named kind is a first sight about the kind, an unnamed one is none.
		place("Berry", {5.0F, 0.0F});
		place("Test_Thought_Unnamed", {6.0F, 0.0F});
		harness.world.update(0.0F);
		harness.world.update(0.0F);
		EXPECT_EQ(harness.count("noticed"), 1);
		EXPECT_EQ(harness.count("noticed_food"), 1);
		EXPECT_EQ(harness.count("aha"), 1) << "the berry was the recipe's last unknown input";
		EXPECT_EQ(harness.arrivals.size(), 3U);

		const Thoughts* held = harness.world.getComponent<Thoughts>(harness.colonist);
		ASSERT_NE(held, nullptr);
		const Thoughts& thoughts = *held;
		const auto		noticed = std::ranges::find(thoughts.record, harness.rules.type("noticed"), &Thought::eventType);
		ASSERT_NE(noticed, thoughts.record.end());
		EXPECT_EQ(noticed->subject.kind, ThoughtSubjectKind::Kind);
		ASSERT_TRUE(noticed->subjectName.has_value());
		EXPECT_EQ(thoughts.subjectNames[*noticed->subjectName], "Berry");
		const auto aha = std::ranges::find(thoughts.record, harness.rules.type("aha"), &Thought::eventType);
		ASSERT_NE(aha, thoughts.record.end());
		EXPECT_EQ(aha->subject.definition, registry.getDefNameId("Stick")) << "a recipe learned is about what it makes";

		assets::RecipeRegistry::Get().clear();
		registry.clearDefinitions();
	}

} // namespace ecs::test
