// resolveActionEntries: the shipped entries resolve, and each entry content a factory does not read is refused with its
// exact text.

#include "ActionEntries.h"

#include "ShippedConfig.test.h"
#include "ThoughtTesting.test.h"

#include <assets/EntryLoader.h>
#include <assets/ShippedEntries.test.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace ecs::test {

	namespace {

		/// The shipped behaviors, actions, and facts copied under a fresh temp root, for a case to change one file of.
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

			/// The resolver's errors over the library in the temp root; fails the test when the library does not load.
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

		struct Case {
			std::string				 file;
			std::string				 body;
			std::vector<std::string> errors;
		};

		std::string behavior(const std::string& id, const std::string& actions) {
			return "<behavior id=\"" + id + "\">" + actions + "</behavior>";
		}

		const std::string kWash = R"(<action id="wash" seconds="3" hands="free" interruptable="false">)";
		const std::string kDrink = R"(<action id="drink" seconds="1.5" hands="free" interruptable="false">)";
		const std::string kCarrying =
			R"(<action id="harvest" on="harvestable" workFrom="durability" skill="Harvesting" hands="free" interruptable="false"><take/></action>
<action id="pickup" on="loose_item" seconds="0.5" hands="free" interruptable="false"><take/></action>
<action id="withdraw" on="storage" seconds="0.5" hands="free" interruptable="false"><take/></action>)";
		const std::string kMaking = R"(<action id="craft" workFrom="job" hands="free" interruptable="false"><craft/></action>
<action id="deconstruct" hands="free" interruptable="false" pays="along"><work rate="Construction" direction="down"/></action>
<action id="pickup_packaged" on="packaged" seconds="1.5" hands="free" interruptable="false"><take/></action>
<action id="place_packaged" seconds="2" hands="free" interruptable="false"><install/></action>)";
		const std::string kDeposit = R"(<action id="deposit" seconds="1" hands="any" interruptable="false"><put/></action>)";
		const std::string kBuild =
			R"(<action id="build" hands="free" interruptable="false" pays="along"><work rate="Construction" direction="up"/></action>)";
		const std::string kRelieving =
			R"(<action id="urinate" seconds="2" hands="any" interruptable="false"><restore need="bladder" amount="100"/></action>
<action id="relieve_both" seconds="5" hands="any" interruptable="false"><restore need="bladder" amount="100"/><restore need="digestion" amount="100"/><spawn def="Misc_BioPile"/></action>)";

	} // namespace

	TEST_F(ActionEntriesTest, TheShippedEntriesResolveToTheFactoriesValues) {
		const ActionEntriesLoad load = resolved();
		EXPECT_TRUE(load.errors.empty()) << ::testing::PrintToString(load.errors);
		EXPECT_EQ(load.actions.drink.restores[0].need, NeedType::Thirst);
		EXPECT_FLOAT_EQ(load.actions.drink.restores[1].amount, -15.0F);
		EXPECT_TRUE(load.actions.craft.skill.empty()) << "a craft reads no skill";
		EXPECT_EQ(load.actions.harvest.skill, "Harvesting");
		EXPECT_FLOAT_EQ(load.actions.groundSleepQuality, 0.5F);
	}

	TEST_F(ActionEntriesTest, TheNeedAnActionServesIsItsPrimaryRestoreWhereverItIsWritten) {
		write(
			"behaviors/common/drinking_at_water.xml",
			behavior(
				"drinking_at_water", kDrink + R"(<restore need="bladder" amount="-15"/><restore need="thirst" amount="100"/></action>)"
			)
		);
		const ActionEntriesLoad load = resolved();
		EXPECT_TRUE(load.errors.empty()) << ::testing::PrintToString(load.errors);
		EXPECT_EQ(load.actions.drink.restores[0].need, NeedType::Thirst);
		EXPECT_FLOAT_EQ(load.actions.drink.restores[0].amount, 100.0F);
		EXPECT_EQ(load.actions.drink.restores[1].need, NeedType::Bladder);
		EXPECT_FLOAT_EQ(load.actions.drink.restores[1].amount, -15.0F);
		EXPECT_EQ(load.actions.drink.restoreCount, 2U);
	}

	TEST_F(ActionEntriesTest, EachEntryContentAFactoryDoesNotReadIsAnError) {
		const std::string		washing = "[behaviors] human/washing.xml: <action id=\"wash\"> ";
		const std::string		drinking = "[behaviors] common/drinking_at_water.xml: <action id=\"drink\"> ";
		const std::string		eating = "[behaviors] human/eating.xml: <action id=\"eat\"> ";
		const std::string		relieving = "[behaviors] human/relieving.xml: <action id=\"defecate\"> ";
		const std::vector<Case> cases = {
			{"behaviors/human/washing.xml",
			 behavior("washing", ""),
			 {"action \"human/wash\" is missing; the sim's Wash action reads it from behaviors/human/washing.xml"}},
			{"actions/carrying.xml",
			 "<actions>" + kCarrying + R"(<actionGroup id="deposit">pickup withdraw</actionGroup></actions>)",
			 {"[actions] carrying.xml: <actionGroup id=\"deposit\"> is an action group; the sim's Deposit action reads one action"}},
			{"actions/making.xml",
			 "<actions>" + kMaking + R"(<action id="build" seconds="1" hands="free" interruptable="false"><put/></action></actions>)",
			 {"[actions] making.xml: <action id=\"build\"> takes no duration, running until its <work> reaches its bound in the sim's "
			  "Build action",
			  "[actions] making.xml: <action id=\"build\"> pays must be along; the sim's Build action advances its work as it goes",
			  "[actions] making.xml: <action id=\"build\"> effect 1 <put> is not an effect the sim's Build action performs",
			  "[actions] making.xml: <action id=\"build\"> has 0 <work> effects; the sim's Build action performs one"}},
			{"actions/carrying.xml",
			 "<actions>" + kCarrying.substr(0, kCarrying.find("<action id=\"pickup\"")) +
				 R"(<action id="pickup" on="loose_item" workFrom="durability" skill="Harvesting" hands="free" interruptable="false"><take/></action>
<action id="withdraw" on="storage" seconds="0.5" hands="free" interruptable="false"><take/></action>)" +
				 kDeposit + "</actions>",
			 {"[actions] carrying.xml: <action id=\"pickup\"> takes a fixed duration (seconds) in the sim's Pickup action"}},
			{"behaviors/common/drinking_at_water.xml",
			 behavior("drinking_at_water", kDrink + R"(<restore need="bladder" amount="-15"/></action>)"),
			 {drinking + "has no <restore> of thirst, the need the sim's Drink action serves"}},
			{"behaviors/common/drinking_at_water.xml",
			 behavior(
				 "drinking_at_water",
				 kDrink +
					 R"(<restore need="thirst" amount="100"/><restore need="bladder" amount="-15"/><restore need="hygiene" amount="5"/></action>)"
			 ),
			 {drinking + "effect 3 <restore> restores hygiene; the sim's Drink action restores thirst and at most one side effect"}},
			{"behaviors/human/washing.xml",
			 behavior("washing", kWash + R"(<restore need="hygiene" amount="100"/><consume capability="edible" count="1"/></action>)"),
			 {washing + "effect 2 <consume> is not an effect the sim's Wash action performs"}},
			{"behaviors/human/eating.xml",
			 behavior(
				 "eating",
				 R"(<action id="eat" seconds="2" hands="free" interruptable="false"><restore need="hunger" from="nutrition" scale="100"/></action>)"
			 ),
			 {eating + "has 0 <consume> effects; the sim's Eat action performs one"}},
			{"behaviors/human/eating.xml",
			 behavior(
				 "eating",
				 R"(<action id="eat" seconds="2" hands="free" interruptable="false"><consume capability="drinkable" count="1"/><restore need="hunger" from="nutrition" scale="100"/></action>)"
			 ),
			 {eating + "effect 1 <consume> must consume capability=\"edible\" with no seconds, upTo, or need; the sim's Eat action eats "
					   "one food at a time"}},
			{"behaviors/human/washing.xml",
			 behavior("washing", kWash + R"(<restore need="hygiene" amount="100"/><restore need="mojo" amount="5"/></action>)") + "",
			 {washing + "effect 2 <restore> need \"mojo\" is no need the sim tracks"}},
			{"behaviors/human/washing.xml",
			 behavior("washing", kWash + R"(<restore need="hygiene" amount="-50"/></action>)"),
			 {washing + "effect 1 <restore> restores hygiene, the need the sim's Wash action serves, so its amount must be above 0; "
						"only its side effect may lower a need",
			  washing + "has no <restore> of hygiene, the need the sim's Wash action serves"}},
			{"behaviors/human/washing.xml",
			 behavior("washing", kWash + R"(<restore need="hygiene" amount="100"/><spawn def="Misc_BioPile"/></action>)"),
			 {washing + "effect 2 <spawn> is not an effect the sim's Wash action performs"}},
			{"behaviors/human/relieving.xml",
			 behavior(
				 "relieving",
				 kRelieving +
					 R"(<action id="defecate" seconds="4" hands="any" interruptable="false"><restore need="digestion" amount="100"/><spawn def="Berry"/></action>)"
			 ),
			 {relieving + "effect 2 <spawn> must be def=\"Misc_BioPile\" with no into: the engine spawns only that definition here "
						  "(ticket 13 moves the effect to data)"}},
			{"behaviors/human/relieving.xml",
			 behavior(
				 "relieving",
				 kRelieving +
					 R"(<action id="defecate" seconds="4" hands="any" interruptable="false"><restore need="digestion" amount="100"/></action>)"
			 ),
			 {relieving + "has 0 <spawn> effects; the sim's Toilet action spawns one bio pile"}},
			{"actions/carrying.xml",
			 "<actions>" + kCarrying +
				 R"(<action id="deposit" seconds="1" hands="any" interruptable="false"><put/><damage amount="2"/></action></actions>)",
			 {"[actions] carrying.xml: <action id=\"deposit\"> effect 2 <damage> is not an effect the sim's Deposit action performs"}},
			{"actions/carrying.xml",
			 "<actions>" + kCarrying.substr(0, kCarrying.find("<action id=\"pickup\"")) +
				 R"(<action id="pickup" on="loose_item" seconds="0.5" hands="free" interruptable="false"><take clears="true"/></action>
<action id="withdraw" on="loose_item" seconds="0.5" hands="free" interruptable="false"><take/></action>
<action id="deposit" on="storage" seconds="1" hands="any" interruptable="false"><put/></action></actions>)",
			 {"[actions] carrying.xml: <action id=\"pickup\"> effect 1 <take> clears must be left out; the sim's Pickup action takes "
			  "without clearing",
			  "[actions] carrying.xml: <action id=\"withdraw\"> on must be storage, what the sim's Withdraw action works on",
			  "[actions] carrying.xml: <action id=\"deposit\"> on must be left out; the sim's Deposit action is chosen by no target"}},
			{"behaviors/human/washing.xml",
			 behavior(
				 "washing",
				 R"(<action id="wash" seconds="3" hands="free" interruptable="false" pays="along" asleep="true"><restore need="hygiene" amount="100"/></action>)"
			 ),
			 {washing + "pays must be end; the sim's Wash action pays when it completes",
			  washing + "asleep must be left out; the sim's Wash action is not sleep"}},
			{"behaviors/human/sleeping.xml",
			 behavior(
				 "sleeping",
				 R"(<action id="sleep" seconds="8" hands="any" interruptable="true"><restore need="energy" amount="60"/></action>)"
			 ),
			 {"[behaviors] human/sleeping.xml: <action id=\"sleep\"> asleep must be true; the sim's Sleep action is sleep",
			  "[behaviors] human/sleeping.xml: <action id=\"sleep\"> effect 1 <restore> must restore an amount with "
			  "scaleBy=\"sleep_quality\", the creature's own, with no seconds or upTo; that is what the sim's Sleep action applies",
			  "[behaviors] human/sleeping.xml: <action id=\"sleep\"> has no <restore> of energy, the need the sim's Sleep action serves"}},
		};
		for (const Case& entry : cases) {
			SCOPED_TRACE(entry.file + ": " + entry.body);
			TearDown();
			SetUp();
			write(
				"behaviors/test/mojo.xml",
				R"(<behavior id="mojo"><need id="mojo" label="Mojo" seek="50" urgent="25" critical="10"><drains perGameMinute="0.1"/></need></behavior>)"
			);
			if (entry.file == "actions/making.xml") {
				write("actions/carrying.xml", "<actions>" + kCarrying + kDeposit + "</actions>");
			}
			if (entry.file == "actions/carrying.xml") {
				write("actions/making.xml", "<actions>" + kMaking + kBuild + "</actions>");
			}
			write(entry.file, entry.body);
			EXPECT_EQ(resolved().errors, entry.errors);
		}
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
