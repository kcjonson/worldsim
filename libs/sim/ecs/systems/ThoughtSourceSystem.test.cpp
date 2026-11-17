#include "ThoughtSourceSystem.h"

#include "../ColonistRules.h"
#include "../ThoughtTesting.test.h"
#include "../World.h"
#include "../components/Colonist.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/Needs.h"
#include "../components/Task.h"
#include "../components/Transform.h"
#include "ActionSystem.h"
#include "ThoughtSystem.h"
#include "TimeSystem.h"
#include "VisionSystem.h"

#include <assets/AssetRegistry.h>

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
				for (const std::string& error : loadNeedLevels(levels, testing::repoConfig() / "colonists" / "needs.xml")) {
					ADD_FAILURE() << error;
				}
				time = &world.registerSystem<TimeSystem>();
				thoughts = &world.registerSystem<ThoughtSystem>(rules.types, seed);
				VisionSystem& vision = world.registerSystem<VisionSystem>();
				ActionSystem& actions = world.registerSystem<ActionSystem>(seed);
				world.registerSystem<ThoughtSourceSystem>(rules.types, levels, seed).listenTo(actions, vision);
				thoughts->addArrivedObserver([this](EntityID, const Thought& thought, ThoughtArrival arrival) {
					arrivals.emplace_back(rules.types.type(thought.eventType).name, arrival);
				});

				colonist = world.createEntity();
				world.addComponent<Colonist>(colonist, Colonist{.name = "Ada"});
				world.addComponent<Position>(colonist, Position{{0.0F, 0.0F}});
				world.addComponent<NeedsComponent>(colonist, NeedsComponent::createDefault());
				world.addComponent<Memory>(colonist, Memory{});
				world.addComponent<Inventory>(colonist, Inventory::createForColonist());
				world.addComponent<Task>(colonist, Task{});
				world.addComponent<Action>(colonist, Action{});
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

			testing::TestThoughtTypes							 rules;
			NeedLevels											 levels;
			World												 world;
			TimeSystem*											 time = nullptr;
			ThoughtSystem*										 thoughts = nullptr;
			EntityID											 colonist = kInvalidEntity;
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
	}

	TEST(ThoughtSources, AnActionThoughtFollowsWhereItHappenedAndAFirstIsOncePerKind) {
		auto& registry = assets::AssetRegistry::Get();
		registry.clearDefinitions();
		// Id 0 is the registry's "no definition", which an action about no thing reports.
		registry.registerSyntheticDefinition("Test_Thought_IdZeroReservation", 0);
		const uint32_t bed = registry.registerSyntheticDefinition(
			"Test_Thought_Bed", static_cast<uint16_t>(1U << static_cast<unsigned>(assets::CapabilityType::Sleepable))
		);
		Harness harness(kActionTypes);
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

		const auto rollsUnder = [](uint32_t seed) {
			Harness seeded(kIdleTypes, seed);
			seeded.world.update(0.0F);
			for (int roll = 0; roll < 40; ++roll) {
				seeded.advanceGameMinutes(11.0F);
			}
			return seeded.arrivals;
		};
		const auto first = rollsUnder(7);
		EXPECT_EQ(first, rollsUnder(7));
		EXPECT_GT(std::ranges::count(first, std::pair<std::string, ThoughtArrival>("coin_toss", ThoughtArrival::New)), 0);
	}

} // namespace ecs::test
