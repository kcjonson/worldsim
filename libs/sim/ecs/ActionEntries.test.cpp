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
#include <sstream>
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

			/// The library in the temp root; fails the test when it does not load.
			[[nodiscard]] assets::EntryLibrary loaded() const {
				assets::EntrySources sources{.configRoot = root, .trackedNeeds = trackedNeedIds()};
				sources.definitionId = [](std::string_view name) -> std::optional<uint32_t> {
					const auto found = assets::testing::shippedDefinitionNames().find(name);
					return found == assets::testing::shippedDefinitionNames().end() ? std::nullopt : std::optional<uint32_t>(found->second);
				};
				assets::EntryLoad load = assets::loadEntries(sources);
				EXPECT_TRUE(load.errors.empty()) << ::testing::PrintToString(load.errors);
				return std::move(load.library);
			}

			/// The resolver's load over the library in the temp root.
			[[nodiscard]] ActionEntriesLoad resolved() const { return resolveActionEntries(loaded()); }

			std::filesystem::path root;
		};

		/// One field-by-field expectation of an action's resolved values, its rows as `describe` writes them.
		struct Values {
			const char*				 name;
			FactoryAction			 action;
			assets::ActionDuration	 duration;
			float					 seconds;
			bool					 interruptable;
			bool					 freeHands;
			assets::Pays			 pays;
			const char*				 skill;
			ActionBound				 bound;
			NeedType				 primary;
			std::vector<std::string> rows;
		};

		/// A row in words: its element, then what it carries.
		[[nodiscard]] std::string describe(const EffectRow& row) {
			std::ostringstream text;
			if (const auto* restore = std::get_if<RestoreRow>(&row)) {
				text << "restore " << needLabel(restore->need) << " " << restore->amount << (restore->fromNutrition ? " nutrition" : "")
					 << (restore->bySleepQuality ? " sleep_quality" : "");
			} else if (const auto* consume = std::get_if<ConsumeRow>(&row)) {
				text << "consume " << assets::capabilityName(consume->capability) << " " << consume->count;
			} else if (const auto* take = std::get_if<TakeRow>(&row)) {
				text << "take" << (take->clears ? " clears" : "");
			} else if (std::holds_alternative<PutRow>(row)) {
				text << "put";
			} else if (const auto* work = std::get_if<WorkRow>(&row)) {
				text << "work " << work->skill << (work->down ? " down" : " up");
			} else if (std::holds_alternative<CraftRow>(row)) {
				text << "craft";
			} else if (const auto* spawn = std::get_if<SpawnRow>(&row)) {
				text << "spawn " << spawn->def << (spawn->intoHands ? " into hands" : "");
			} else {
				text << "install";
			}
			return text.str();
		}

		[[nodiscard]] std::vector<std::string> describe(const ActionValues& values) {
			std::vector<std::string> rows;
			for (const EffectRow& row : values.effects) {
				rows.push_back(describe(row));
			}
			return rows;
		}

		/// The need the action serves: its primary row's.
		[[nodiscard]] NeedType primaryNeed(const ActionValues& values) {
			return values.primaryRow == kNoRow ? NeedType::Count : std::get<RestoreRow>(values.effects[values.primaryRow]).need;
		}

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
		using enum assets::ActionDuration;
		using assets::Pays;
		constexpr NeedType		  kNone = NeedType::Count;
		const std::vector<Values> expected = {
			{"eat",
			 FactoryAction::Eat,
			 Seconds,
			 2.0F,
			 false,
			 true,
			 Pays::End,
			 "",
			 ActionBound::None,
			 NeedType::Hunger,
			 {"consume edible 1", "restore Hunger 100 nutrition", "restore Digestion -100 nutrition"}},
			{"drink",
			 FactoryAction::Drink,
			 Seconds,
			 1.5F,
			 false,
			 true,
			 Pays::End,
			 "",
			 ActionBound::None,
			 NeedType::Thirst,
			 {"restore Thirst 100", "restore Bladder -15"}},
			{"wash",
			 FactoryAction::Wash,
			 Seconds,
			 3.0F,
			 false,
			 true,
			 Pays::End,
			 "",
			 ActionBound::None,
			 NeedType::Hygiene,
			 {"restore Hygiene 100"}},
			{"sleep",
			 FactoryAction::Sleep,
			 Seconds,
			 8.0F,
			 true,
			 false,
			 Pays::End,
			 "",
			 ActionBound::None,
			 NeedType::Energy,
			 {"restore Energy 60 sleep_quality"}},
			{"urinate",
			 FactoryAction::Urinate,
			 Seconds,
			 2.0F,
			 false,
			 false,
			 Pays::End,
			 "",
			 ActionBound::None,
			 NeedType::Bladder,
			 {"restore Bladder 100"}},
			{"defecate",
			 FactoryAction::Defecate,
			 Seconds,
			 4.0F,
			 false,
			 false,
			 Pays::End,
			 "",
			 ActionBound::None,
			 NeedType::Digestion,
			 {"restore Digestion 100", "spawn Misc_BioPile"}},
			{"relieve_both",
			 FactoryAction::RelieveBoth,
			 Seconds,
			 5.0F,
			 false,
			 false,
			 Pays::End,
			 "",
			 ActionBound::None,
			 NeedType::Bladder,
			 {"restore Bladder 100", "restore Digestion 100", "spawn Misc_BioPile"}},
			{"pickup", FactoryAction::Pickup, Seconds, 0.5F, false, true, Pays::End, "", ActionBound::None, kNone, {"take"}},
			{"withdraw", FactoryAction::Withdraw, Seconds, 0.5F, false, true, Pays::End, "", ActionBound::None, kNone, {"take"}},
			{"harvest",
			 FactoryAction::Harvest,
			 FromDurability,
			 0.0F,
			 false,
			 true,
			 Pays::End,
			 "Harvesting",
			 ActionBound::None,
			 kNone,
			 {"take"}},
			{"craft", FactoryAction::Craft, FromJob, 0.0F, false, true, Pays::End, "", ActionBound::None, kNone, {"craft"}},
			{"deposit", FactoryAction::Deposit, Seconds, 1.0F, false, false, Pays::End, "", ActionBound::None, kNone, {"put"}},
			{"build",
			 FactoryAction::Build,
			 OpenEnded,
			 0.0F,
			 false,
			 true,
			 Pays::Along,
			 "Construction",
			 ActionBound::Work,
			 kNone,
			 {"work Construction up"}},
			{"deconstruct",
			 FactoryAction::Deconstruct,
			 OpenEnded,
			 0.0F,
			 false,
			 true,
			 Pays::Along,
			 "Construction",
			 ActionBound::Work,
			 kNone,
			 {"work Construction down"}},
			{"pickup_packaged",
			 FactoryAction::PickupPackaged,
			 Seconds,
			 1.5F,
			 false,
			 true,
			 Pays::End,
			 "",
			 ActionBound::None,
			 kNone,
			 {"take"}},
			{"place_packaged",
			 FactoryAction::PlacePackaged,
			 Seconds,
			 2.0F,
			 false,
			 true,
			 Pays::End,
			 "",
			 ActionBound::None,
			 kNone,
			 {"install"}},
		};
		const ActionEntriesLoad load = resolved();
		EXPECT_TRUE(load.errors.empty()) << ::testing::PrintToString(load.errors);
		for (const Values& want : expected) {
			SCOPED_TRACE(want.name);
			const ActionValues& got = load.actions.of(want.action);
			EXPECT_TRUE(got.entry.valid());
			EXPECT_EQ(got.duration, want.duration);
			EXPECT_FLOAT_EQ(got.seconds, want.seconds);
			EXPECT_EQ(got.interruptable, want.interruptable);
			EXPECT_EQ(got.freeHands, want.freeHands);
			EXPECT_EQ(got.pays, want.pays);
			EXPECT_FLOAT_EQ(got.curve, 1.0F);
			EXPECT_EQ(got.skill, want.skill);
			EXPECT_EQ(got.bound, want.bound);
			EXPECT_EQ(got.effects.size(), want.rows.size());
			EXPECT_EQ(describe(got), want.rows);
			EXPECT_EQ(primaryNeed(got), want.primary);
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
		const ActionValues& drink = load.actions.drink();
		EXPECT_EQ(primaryNeed(drink), NeedType::Thirst);
		EXPECT_FLOAT_EQ(std::get<RestoreRow>(drink.effects[drink.primaryRow]).amount, 100.0F);
		EXPECT_EQ(std::get<RestoreRow>(drink.effects[0]).need, NeedType::Bladder);
		EXPECT_FLOAT_EQ(std::get<RestoreRow>(drink.effects[0]).amount, -15.0F);
		EXPECT_EQ(drink.effects.size(), 2U);
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
			 R"(<consume capability="edible" count="1"/>)" + washRow,
			 wash + "effect 1 <consume> is not an effect the sim's Wash action performs"},
			{carrying,
			 "<put/>",
			 R"(<put/><damage amount="2"/>)",
			 "[actions] carrying.xml: <action id=\"deposit\"> effect 2 <damage> lowers a condition, which nothing in the sim has"},
			{carrying,
			 "<put/>",
			 R"(<put/><restore need="hygiene" amount="5"/>)",
			 "[actions] carrying.xml: <action id=\"deposit\"> effect 2 <restore> is not an effect the sim's Deposit action performs"},
			{carrying,
			 "<put/>",
			 "<put/><put/>",
			 "[actions] carrying.xml: <action id=\"deposit\"> effect 2 <put> is a second <put>; the sim's Deposit action performs one"},
			{carrying,
			 R"(id="pickup" on="loose_item" seconds="0.5" hands="free" interruptable="false">)",
			 R"(id="pickup" on="loose_item" seconds="0.5" hands="free" interruptable="false"><take clears="true"/></action>)"
			 R"(<action id="spare" seconds="1" interruptable="false">)",
			 "[actions] carrying.xml: <action id=\"pickup\"> effect 1 <take> clears must be left out; the sim's Pickup action takes "
			 "without clearing"},
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
			 eat + "effect 1 <consume> capability must be edible, the one capability the engine finds held units of"},
			// Spawn.
			{relieving,
			 R"(<restore need="bladder" amount="100"/>)",
			 R"(<restore need="bladder" amount="100"/><spawn def="Misc_BioPile"/>)",
			 urinate + "effect 2 <spawn> is not an effect the sim's Toilet action performs"},
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

	TEST_F(ActionEntriesTest, ARelieveSpawnsTheDefinitionItsRowNames) {
		// The spawn primitive reads its row's definition and where it goes, so the relief's factory takes any.
		change("behaviors/human/relieving.xml", R"(<spawn def="Misc_BioPile"/>)", R"(<spawn def="Berry" into="hands"/>)");
		const ActionEntriesLoad load = resolved();
		EXPECT_TRUE(load.errors.empty()) << ::testing::PrintToString(load.errors);
		ASSERT_EQ(load.actions.defecate().effects.size(), 2U);
		EXPECT_EQ(std::get<SpawnRow>(load.actions.defecate().effects[1]).def, "Berry");
		EXPECT_TRUE(std::get<SpawnRow>(load.actions.defecate().effects[1]).intoHands);
	}

	TEST_F(ActionEntriesTest, EveryActionEntryIsResolvedAtLoad) {
		// An entry no factory starts is held by its handle, and an engine rule it breaks is a load error.
		write(
			"actions/extra.xml", R"(<actions><action id="bash" seconds="1" interruptable="false"><damage amount="2"/></action></actions>)"
		);
		const ActionEntriesLoad load = resolved();
		EXPECT_EQ(
			load.errors,
			std::vector<std::string>{
				"[actions] extra.xml: <action id=\"bash\"> effect 1 <damage> lowers a condition, which nothing in the sim has"
			}
		);
		write(
			"actions/extra.xml",
			R"(<actions><action id="soak" seconds="2" interruptable="false"><restore need="hygiene" amount="40"/></action></actions>)"
		);
		const ActionEntriesLoad clean = resolved();
		ASSERT_TRUE(clean.errors.empty()) << ::testing::PrintToString(clean.errors);
		EXPECT_EQ(clean.actions.entries.size(), loaded().actions.size());
		const assets::EntryHandle<assets::ActionEntry> soak = loaded().findAction("soak");
		ASSERT_TRUE(soak.valid());
		EXPECT_EQ(describe(clean.actions.of(soak)), std::vector<std::string>{"restore Hygiene 40"});
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
