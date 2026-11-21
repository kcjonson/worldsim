// resolveActionEntries: the shipped entries resolve to the values the hand-written factories used, and each entry
// content a factory does not read is refused with its exact text.

#include "ActionEntries.h"

#include "ShippedConfig.test.h"
#include "ThoughtTesting.test.h"

#include <assets/EntryLoader.h>
#include <assets/ShippedEntries.test.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace ecs::test {

	namespace {

		/// The shipped behaviors, actions, and facts copied under a fresh temp root, for a case to change one thing in.
		class ActionEntriesTest : public ::testing::Test {
		  protected:
			void SetUp() override {
				root = std::filesystem::temp_directory_path() / ("worldsim-action-entries-" + std::to_string(std::random_device{}()) + "-" +
																 ::testing::UnitTest::GetInstance()->current_test_info()->name());
				const std::filesystem::path shipped = assets::testing::shippedRepoRoot() / "assets" / "config";
				std::filesystem::create_directories(root);
				for (const char* folder : {"behaviors", "actions", "facts"}) {
					std::filesystem::copy(shipped / folder, root / folder, std::filesystem::copy_options::recursive);
				}
				std::filesystem::create_directories(root / "work");
				std::filesystem::create_directories(root / "orders");
			}

			void TearDown() override { std::filesystem::remove_all(root); }

			void write(const std::string& relative, const std::string& body) const {
				std::filesystem::create_directories((root / relative).parent_path());
				std::ofstream(root / relative) << body;
			}

			/// Replace the first `from` in the copied shipped file `relative` with `to`; fails the test when it is not there.
			void change(const std::string& relative, const std::string& from, const std::string& to) const {
				std::ifstream	  in(root / relative);
				std::string		  text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				const std::size_t at = text.find(from);
				ASSERT_NE(at, std::string::npos) << relative << " holds no " << from;
				in.close();
				text.replace(at, from.size(), to);
				std::ofstream(root / relative) << text;
			}

			/// The resolver's load over the library in the temp root; fails the test when the library does not load.
			[[nodiscard]] ActionEntriesLoad resolved() const {
				assets::EntrySources sources{.configRoot = root, .trackedNeeds = trackedNeedIds()};
				sources.definitionId = [](std::string_view name) -> std::optional<uint32_t> {
					const auto found = assets::testing::shippedDefinitionNames().find(name);
					return found == assets::testing::shippedDefinitionNames().end() ? std::nullopt : std::optional<uint32_t>(found->second);
				};
				const assets::EntryLoad loaded = assets::loadEntries(sources);
				EXPECT_TRUE(loaded.errors.empty()) << ::testing::PrintToString(loaded.errors);
				return resolveActionEntries(loaded.library);
			}

			std::filesystem::path root;
		};

		/// One field-by-field expectation of an action's resolved values.
		struct Values {
			const char*	 name;
			ActionValues ActionEntries::* member;
			float						  seconds;
			bool						  interruptable;
			bool						  freeHands;
			const char*					  skill;
			uint8_t						  restoreCount;
			RestoreValue				  primary;
			RestoreValue				  side;
			uint32_t					  consumes;
			bool						  spawns;
		};

		/// One change to a shipped file and the one error it gives.
		struct Case {
			std::string file;
			std::string from;
			std::string to;
			std::string error;
		};

	} // namespace

	// The literals are the hand-written factories' values on main before the entries: the one place a shipped value
	// drifting from them shows.
	TEST_F(ActionEntriesTest, TheShippedEntriesResolveToTheFactoriesValues) {
		constexpr RestoreValue	  kNone{};
		const std::vector<Values> expected = {
			{"eat", &ActionEntries::eat, 2.0F, false, true, "", 2, {NeedType::Hunger, 100.0F}, {NeedType::Digestion, -100.0F}, 1, false},
			{"drink", &ActionEntries::drink, 1.5F, false, true, "", 2, {NeedType::Thirst, 100.0F}, {NeedType::Bladder, -15.0F}, 0, false},
			{"wash", &ActionEntries::wash, 3.0F, false, true, "", 1, {NeedType::Hygiene, 100.0F}, kNone, 0, false},
			{"sleep", &ActionEntries::sleep, 8.0F, true, false, "", 1, {NeedType::Energy, 60.0F}, kNone, 0, false},
			{"urinate", &ActionEntries::urinate, 2.0F, false, false, "", 1, {NeedType::Bladder, 100.0F}, kNone, 0, false},
			{"defecate", &ActionEntries::defecate, 4.0F, false, false, "", 1, {NeedType::Digestion, 100.0F}, kNone, 0, true},
			{"relieve_both",
			 &ActionEntries::relieveBoth,
			 5.0F,
			 false,
			 false,
			 "",
			 2,
			 {NeedType::Bladder, 100.0F},
			 {NeedType::Digestion, 100.0F},
			 0,
			 true},
			{"pickup", &ActionEntries::pickup, 0.5F, false, true, "", 0, kNone, kNone, 0, false},
			{"withdraw", &ActionEntries::withdraw, 0.5F, false, true, "", 0, kNone, kNone, 0, false},
			{"harvest", &ActionEntries::harvest, 0.0F, false, true, "Harvesting", 0, kNone, kNone, 0, false},
			{"craft", &ActionEntries::craft, 0.0F, false, true, "", 0, kNone, kNone, 0, false},
			{"deposit", &ActionEntries::deposit, 1.0F, false, false, "", 0, kNone, kNone, 0, false},
			{"build", &ActionEntries::build, 0.0F, false, true, "Construction", 0, kNone, kNone, 0, false},
			{"deconstruct", &ActionEntries::deconstruct, 0.0F, false, true, "Construction", 0, kNone, kNone, 0, false},
			{"pickup_packaged", &ActionEntries::pickupPackaged, 1.5F, false, true, "", 0, kNone, kNone, 0, false},
			{"place_packaged", &ActionEntries::placePackaged, 2.0F, false, true, "", 0, kNone, kNone, 0, false},
		};
		const ActionEntriesLoad load = resolved();
		EXPECT_TRUE(load.errors.empty()) << ::testing::PrintToString(load.errors);
		for (const Values& want : expected) {
			SCOPED_TRACE(want.name);
			const ActionValues& got = load.actions.*(want.member);
			EXPECT_TRUE(got.entry.valid());
			EXPECT_FLOAT_EQ(got.seconds, want.seconds);
			EXPECT_EQ(got.interruptable, want.interruptable);
			EXPECT_EQ(got.freeHands, want.freeHands);
			EXPECT_EQ(got.skill, want.skill);
			EXPECT_EQ(got.restoreCount, want.restoreCount);
			EXPECT_EQ(got.restores[0].need, want.primary.need);
			EXPECT_FLOAT_EQ(got.restores[0].amount, want.primary.amount);
			EXPECT_EQ(got.restores[1].need, want.side.need);
			EXPECT_FLOAT_EQ(got.restores[1].amount, want.side.amount);
			EXPECT_EQ(got.consumes, want.consumes);
			EXPECT_EQ(got.spawns, want.spawns);
		}
		EXPECT_FLOAT_EQ(load.actions.groundSleepQuality, 0.5F);
	}

	TEST_F(ActionEntriesTest, TheNeedAnActionServesIsItsPrimaryRestoreWhereverItIsWritten) {
		change("behaviors/common/drinking_at_water.xml", R"(<restore need="thirst" amount="100"/>)", "");
		change(
			"behaviors/common/drinking_at_water.xml",
			R"(<restore need="bladder" amount="-15"/>)",
			R"(<restore need="bladder" amount="-15"/><restore need="thirst" amount="100"/>)"
		);
		const ActionEntriesLoad load = resolved();
		EXPECT_TRUE(load.errors.empty()) << ::testing::PrintToString(load.errors);
		EXPECT_EQ(load.actions.drink.restores[0].need, NeedType::Thirst);
		EXPECT_FLOAT_EQ(load.actions.drink.restores[0].amount, 100.0F);
		EXPECT_EQ(load.actions.drink.restores[1].need, NeedType::Bladder);
		EXPECT_FLOAT_EQ(load.actions.drink.restores[1].amount, -15.0F);
		EXPECT_EQ(load.actions.drink.restoreCount, 2U);
	}

	TEST_F(ActionEntriesTest, EachEntryContentAFactoryDoesNotReadIsOneError) {
		const std::string		eating = "behaviors/human/eating.xml";
		const std::string		washing = "behaviors/human/washing.xml";
		const std::string		sleeping = "behaviors/human/sleeping.xml";
		const std::string		relieving = "behaviors/human/relieving.xml";
		const std::string		drinking = "behaviors/common/drinking_at_water.xml";
		const std::string		carrying = "actions/carrying.xml";
		const std::string		making = "actions/making.xml";
		const std::string		eat = "[behaviors] human/eating.xml: <action id=\"eat\"> ";
		const std::string		wash = "[behaviors] human/washing.xml: <action id=\"wash\"> ";
		const std::string		sleep = "[behaviors] human/sleeping.xml: <action id=\"sleep\"> ";
		const std::string		drink = "[behaviors] common/drinking_at_water.xml: <action id=\"drink\"> ";
		const std::string		urinate = "[behaviors] human/relieving.xml: <action id=\"urinate\"> ";
		const std::string		defecate = "[behaviors] human/relieving.xml: <action id=\"defecate\"> ";
		const std::string		washRow = R"(<restore need="hygiene" amount="100"/>)";
		const std::string		defecateSpawn = R"(<spawn def="Misc_BioPile"/>)";
		const std::vector<Case> cases = {
			// Entries.
			{washing,
			 R"(id="wash")",
			 R"(id="scrub")",
			 "action \"human/wash\" is missing; the sim's Wash action reads it from behaviors/human/washing.xml"},
			{carrying,
			 R"(<action id="deposit")",
			 R"(<actionGroup id="deposit">pickup withdraw</actionGroup><action id="old_deposit")",
			 "[actions] carrying.xml: <actionGroup id=\"deposit\"> is an action group; the sim's Deposit action reads one action"},
			// Durations.
			{carrying,
			 R"(id="pickup" on="loose_item" seconds="0.5")",
			 R"(id="pickup" on="loose_item" workFrom="durability" skill="Harvesting")",
			 "[actions] carrying.xml: <action id=\"pickup\"> takes a fixed duration (seconds) in the sim's Pickup action"},
			{carrying,
			 R"(workFrom="durability" skill="Harvesting")",
			 R"(seconds="2")",
			 "[actions] carrying.xml: <action id=\"harvest\"> takes a duration from the target's durability (workFrom=\"durability\") in "
			 "the sim's Harvest action"},
			{making,
			 R"(workFrom="job")",
			 R"(seconds="5")",
			 "[actions] making.xml: <action id=\"craft\"> takes a duration from the job's work amount (workFrom=\"job\") in the sim's "
			 "Craft action"},
			// Entry attributes.
			{washing,
			 R"(interruptable="false">)",
			 R"(interruptable="false" pays="along">)",
			 wash + "pays must be end; the sim's Wash action pays when it completes"},
			{making,
			 R"(pays="along">)",
			 R"(pays="along" curve="2">)",
			 "[actions] making.xml: <action id=\"build\"> curve must be left out; the sim's Build action advances its work evenly"},
			{washing,
			 R"(interruptable="false">)",
			 R"(interruptable="false" asleep="true">)",
			 wash + "asleep must be left out; the sim's Wash action is not sleep"},
			{sleeping, R"( asleep="true")", "", sleep + "asleep must be true; the sim's Sleep action is sleep"},
			{carrying,
			 R"(on="storage")",
			 R"(on="loose_item")",
			 "[actions] carrying.xml: <action id=\"withdraw\"> on must be storage, what the sim's Withdraw action works on"},
			{carrying,
			 R"(<action id="deposit")",
			 R"(<action id="deposit" on="storage")",
			 "[actions] carrying.xml: <action id=\"deposit\"> on must be left out; the sim's Deposit action is chosen by no target"},
			// Effects a factory does not perform, or performs once.
			{washing,
			 washRow,
			 washRow + R"(<consume capability="edible" count="1"/>)",
			 wash + "effect 2 <consume> is not an effect the sim's Wash action performs"},
			{carrying,
			 "<put/>",
			 R"(<put/><damage amount="2"/>)",
			 "[actions] carrying.xml: <action id=\"deposit\"> effect 2 <damage> is not an effect the sim's Deposit action performs"},
			{carrying,
			 "<put/>",
			 R"(<put/><restore need="hygiene" amount="5"/>)",
			 "[actions] carrying.xml: <action id=\"deposit\"> effect 2 <restore> is not an effect the sim's Deposit action performs"},
			{carrying,
			 "<put/>",
			 "<put/><put/>",
			 "[actions] carrying.xml: <action id=\"deposit\"> effect 2 <put> is a second <put>; the sim's Deposit action performs one"},
			{carrying,
			 "<take/>",
			 R"(<take clears="true"/>)",
			 "[actions] carrying.xml: <action id=\"harvest\"> effect 1 <take> clears must be left out; the sim's Harvest action takes "
			 "without clearing"},
			// Consume.
			{eating, R"(<consume capability="edible" count="1"/>)", "", eat + "has no <consume> effect; the sim's Eat action performs one"},
			{eating,
			 R"(capability="edible")",
			 R"(capability="drinkable")",
			 eat + "effect 1 <consume> must consume capability=\"edible\" with no seconds, upTo, or need; the sim's Eat action eats one "
				   "food at a time"},
			// Spawn.
			{relieving,
			 R"(<restore need="bladder" amount="100"/>)",
			 R"(<restore need="bladder" amount="100"/><spawn def="Misc_BioPile"/>)",
			 urinate + "effect 2 <spawn> is not an effect the sim's Toilet action performs"},
			{relieving,
			 defecateSpawn,
			 R"(<spawn def="Berry"/>)",
			 defecate + "effect 2 <spawn> must be def=\"Misc_BioPile\" with no into: the engine spawns only that definition for a relief, "
						"where the creature stands"},
			{relieving,
			 defecateSpawn,
			 R"(<spawn def="Misc_BioPile" into="hands"/>)",
			 defecate + "effect 2 <spawn> must be def=\"Misc_BioPile\" with no into: the engine spawns only that definition for a relief, "
						"where the creature stands"},
			{relieving,
			 defecateSpawn,
			 defecateSpawn + defecateSpawn,
			 defecate + "effect 3 <spawn> is a second <spawn>; the sim's Toilet action spawns one bio pile"},
			{relieving, defecateSpawn, "", defecate + "has no <spawn> effect; the sim's Toilet action spawns one bio pile"},
			// Restores.
			{drinking,
			 R"(<restore need="thirst" amount="100"/>)",
			 "",
			 drink + "has no <restore> of thirst, the need the sim's Drink action serves"},
			{drinking,
			 R"(<restore need="bladder" amount="-15"/>)",
			 R"(<restore need="bladder" amount="-15"/><restore need="hygiene" amount="5"/>)",
			 drink + "effect 3 <restore> restores hygiene; the sim's Drink action restores thirst and at most one side effect"},
			{washing,
			 washRow,
			 washRow + R"(<restore need="mojo" amount="5"/>)",
			 wash + "effect 2 <restore> need \"mojo\" is no need the sim tracks"},
			{washing,
			 R"(amount="100")",
			 R"(amount="-50")",
			 wash + "effect 1 <restore> restores hygiene, the need the sim's Wash action serves, so its amount must be above 0; only its "
					"side effect may lower a need"},
			{washing,
			 washRow,
			 washRow + washRow,
			 wash + "effect 2 <restore> restores hygiene a second time; the sim's Wash action restores it once"},
			{sleeping,
			 R"(scaleBy="sleep_quality"/>)",
			 R"(scaleBy="sleep_quality"/><restore need="hunger" amount="5"/>)",
			 sleep + "effect 2 <restore> restores hunger; the sim's Sleep action restores energy alone"},
			{eating,
			 R"(<restore need="hunger" from="nutrition" scale="100"/>)",
			 R"(<restore need="hunger" amount="30"/>)",
			 eat + "effect 2 <restore> must restore from=\"nutrition\" with a scale, the creature's own, with no seconds or upTo; that is "
				   "what the sim's Eat action applies"},
			{washing,
			 R"(amount="100")",
			 R"(amount="100" who="target")",
			 wash + "effect 1 <restore> must restore a plain amount, the creature's own, with no seconds or upTo; that is what the sim's "
					"Wash action applies"},
			{washing,
			 R"(amount="100")",
			 R"(amount="100" scaleBy="sleep_quality")",
			 wash + "effect 1 <restore> must restore a plain amount, the creature's own, with no seconds or upTo; that is what the sim's "
					"Wash action applies"},
			{sleeping,
			 R"( scaleBy="sleep_quality")",
			 "",
			 sleep + "effect 1 <restore> must restore an amount with scaleBy=\"sleep_quality\", the creature's own, with no seconds or "
					 "upTo; that is what the sim's Sleep action applies"},
		};
		for (const Case& tried : cases) {
			SCOPED_TRACE(tried.file + ": " + tried.from + " -> " + tried.to);
			TearDown();
			SetUp();
			write(
				"behaviors/test/mojo.xml",
				R"(<behavior id="mojo"><need id="mojo" label="Mojo" seek="50" urgent="25" critical="10"><drains perGameMinute="0.1"/></need></behavior>)"
			);
			change(tried.file, tried.from, tried.to);
			EXPECT_EQ(resolved().errors, std::vector<std::string>{tried.error});
		}
	}

	TEST_F(ActionEntriesTest, AConsumeThatRunsUntilABoundIsRefusedBesideTheDurationItTakesAway) {
		// An upTo makes the action open-ended, which the loader allows only with no seconds; the Eat factory reads both.
		change("behaviors/human/eating.xml", R"(seconds="2" )", "");
		change("behaviors/human/eating.xml", R"(count="1"/>)", R"(count="1" seconds="2" upTo="95" need="hunger"/>)");
		EXPECT_EQ(
			resolved().errors,
			(std::vector<std::string>{
				"[behaviors] human/eating.xml: <action id=\"eat\"> takes a fixed duration (seconds) in the sim's Eat action",
				"[behaviors] human/eating.xml: <action id=\"eat\"> effect 1 <consume> must consume capability=\"edible\" with no seconds, "
				"upTo, or need; the sim's Eat action eats one food at a time",
			})
		);
	}

	TEST_F(ActionEntriesTest, AThoughtSourceNamingAnActionGroupIsAnError) {
		write("actions/groups.xml", R"(<actions><actionGroup id="take">harvest pickup withdraw</actionGroup></actions>)");
		assets::EntrySources sources{.configRoot = root, .trackedNeeds = trackedNeedIds()};
		sources.definitionId = [](std::string_view name) -> std::optional<uint32_t> {
			const auto found = assets::testing::shippedDefinitionNames().find(name);
			return found == assets::testing::shippedDefinitionNames().end() ? std::nullopt : std::optional<uint32_t>(found->second);
		};
		const assets::EntryLoad loaded = assets::loadEntries(sources);
		ASSERT_TRUE(loaded.errors.empty()) << ::testing::PrintToString(loaded.errors);

		testing::TestThoughtTypes rules(R"(<thought id="took" label="Took" importance="trivial">
    <source trigger="actionFinishes" action="human/eat"/><source trigger="actionFinishes" action="take"/><feeling is="content"/>
  </thought>)");
		EXPECT_EQ(
			rules.types.resolveActions(loaded.library),
			std::vector<std::string>{"thoughts.xml: thought 'took' source 2: 'take' is an action group; a source names one action"}
		);
	}

} // namespace ecs::test
