// The behavior library's loaders (behaviors, actions, work, orders, facts) and the creature loader: each entry
// form the spec gives, the id rules, and each load error once with its exact text.

#include "assets/EntryLoader.h"

#include "assets/PriorityConfig.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <variant>
#include <vector>

using namespace assets;

namespace {

	const std::vector<std::string> kRungs = {
		"survival",
		"critical_need",
		"low_mood",
		"player_order",
		"urgent_need",
		"own_unfinished_work",
		"committed_work",
		"ordinary_need",
		"opportunistic_work",
		"idle"
	};

	constexpr const char* kFactTuning = R"(<facts>
  <fact id="on_shore" within="1.5"/>
  <fact id="waste_nearby" within="8"/>
  <fact id="food_nearby" within="15"/>
  <fact id="sleep_quality" ground="0.5"/>
</facts>)";

	std::filesystem::path repoRoot() {
		// <root>/libs/assets/assets/EntryLoader.test.cpp -> <root>
		return std::filesystem::path(__FILE__).lexically_normal().parent_path().parent_path().parent_path().parent_path();
	}

	/// A config root and a creatures root under a fresh temp folder, the shared folders present and the facts tuned.
	class EntryLoaderTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			root = std::filesystem::temp_directory_path() /
				   ("worldsim-entries-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->test_suite_name()) + "-" +
					::testing::UnitTest::GetInstance()->current_test_info()->name());
			std::filesystem::remove_all(root);
			for (const char* folder : {"behaviors", "actions", "work", "orders", "facts"}) {
				std::filesystem::create_directories(root / "config" / folder);
			}
			std::filesystem::create_directories(root / "creatures");
			write("config/facts/facts.xml", kFactTuning);
		}

		void TearDown() override { std::filesystem::remove_all(root); }

		void write(const std::string& relative, const std::string& body) const {
			std::filesystem::create_directories((root / relative).parent_path());
			std::ofstream(root / relative) << body;
		}

		/// A behavior file `behaviors/test/sample.xml` holding `body`.
		void sample(const std::string& body) const {
			write("config/behaviors/test/sample.xml", "<behavior id=\"sample\">" + body + "</behavior>");
		}

		[[nodiscard]] EntryLoad load(const std::vector<std::string>& creatures = {}) const {
			EntrySources sources{
				.configRoot = root / "config",
				.rungs = kRungs,
				.definitionId = [](std::string_view name) -> std::optional<uint32_t> {
					static const std::map<std::string, uint32_t, std::less<>> kDefinitions = {{"Berry", 11}, {"BioPile", 12}, {"Milk", 13}};
					const auto												  found = kDefinitions.find(name);
					return found == kDefinitions.end() ? std::nullopt : std::optional<uint32_t>(found->second);
				},
			};
			for (const std::string& name : creatures) {
				CreatureConfigFiles files = creatureConfigFiles(root / "creatures" / name);
				EXPECT_TRUE(files.errors.empty()) << ::testing::PrintToString(files.errors);
				sources.creatures.push_back(std::move(files));
			}
			return loadEntries(sources);
		}

		/// The load's one error; fails the test when there is not exactly one, and checks the library holds nothing.
		[[nodiscard]] std::string onlyError(const std::vector<std::string>& creatures = {}) const {
			const EntryLoad loaded = load(creatures);
			EXPECT_EQ(loaded.errors.size(), 1U) << ::testing::PrintToString(loaded.errors);
			EXPECT_TRUE(loaded.library.chains.empty() && loaded.library.behaviors.empty()) << "an error leaves the library empty";
			return loaded.errors.size() == 1 ? loaded.errors.front() : std::string();
		}

		/// The load's one warning; fails the test when it has an error or not exactly one warning.
		[[nodiscard]] std::string onlyWarning() const {
			const EntryLoad loaded = load();
			EXPECT_TRUE(loaded.errors.empty()) << ::testing::PrintToString(loaded.errors);
			EXPECT_EQ(loaded.warnings.size(), 1U) << ::testing::PrintToString(loaded.warnings);
			return loaded.warnings.size() == 1 ? loaded.warnings.front() : std::string();
		}

		std::filesystem::path root;
	};

	class BehaviorLoaderTest : public EntryLoaderTest {};
	class ActionLoaderTest : public EntryLoaderTest {};
	class WorkLoaderTest : public EntryLoaderTest {};
	class OrderLoaderTest : public EntryLoaderTest {};
	class FactLoaderTest : public EntryLoaderTest {};

	/// The spec's shared actions, with the take group.
	constexpr const char* kSharedActions = R"(<actions>
  <action id="harvest" on="harvestable" workFrom="durability" skill="harvesting" hands="free" interruptable="false"><take/></action>
  <action id="clear" on="harvestable" workFrom="durability" skill="harvesting" hands="free" interruptable="false"><take clears="true"/></action>
  <action id="pickup" on="loose_item" seconds="0.5" hands="free" interruptable="false"><take/></action>
  <action id="withdraw" on="storage" seconds="0.5" hands="free" interruptable="false"><take/></action>
  <action id="deposit" seconds="1" interruptable="false"><put/></action>
  <action id="build" hands="free" interruptable="false" pays="along"><work rate="construction" direction="up"/></action>
  <action id="deconstruct" hands="free" interruptable="false" pays="along"><work rate="construction" direction="down"/></action>
  <action id="pickup_packaged" on="packaged" seconds="1.5" hands="free" interruptable="false"><take/></action>
  <action id="place_packaged" seconds="2" hands="free" interruptable="false"><install/></action>
  <actionGroup id="take">harvest pickup withdraw</actionGroup>
</actions>)";

	/// A small sample behavior whose one chain an idle row starts.
	constexpr const char* kIdleSample = R"(<idle chain="go_there"/>
  <chain id="go_there"><go query="spot"/></chain>
  <query id="spot"><from place="here"/><weigh fact="distance" by="-1"/></query>)";

} // namespace

// --- Behaviors -------------------------------------------------------------------------------------------

TEST_F(BehaviorLoaderTest, TheSpecsHumanBehaviorsLoadWithEveryReferenceResolved) {
	write("config/actions/shared.xml", kSharedActions);
	write("config/behaviors/human/eating.xml", R"(<behavior id="eating" label="Eating">
  <need id="hunger" label="Hunger" seek="50" urgent="25" critical="10">
    <drains perGameMinute="0.08"/>
    <satisfiedBy chain="eat_from_pack"/>
    <satisfiedBy chain="fetch_food" when="carried_food &lt; 1"/>
  </need>
  <idle chain="gather_food" when="carried_food &lt; 1"/>
  <chain id="eat_from_pack"><do action="eat"/></chain>
  <chain id="fetch_food">
    <go query="food_to_eat"/>
    <do action="take" claims="use source_quantity" amount="meal"/>
  </chain>
  <chain id="gather_food"><go query="ripe_food_bush"/><do action="harvest" claims="use"/></chain>
  <query id="food_to_eat">
    <from yields="edible"/><from pile="edible"/><from box="edible"/>
    <prefer fact="held"/>
    <weigh fact="distance" by="-1"/>
  </query>
  <query id="ripe_food_bush"><from yields="edible"/><weigh fact="distance" by="-1"/></query>
  <action id="eat" seconds="2" hands="free" interruptable="false">
    <consume capability="edible" count="1"/>
    <restore need="hunger" from="nutrition" scale="100"/>
    <restore need="digestion" from="nutrition" scale="-100"/>
  </action>
</behavior>)");
	write("config/behaviors/human/relieving.xml", R"xml(<behavior id="relieving" label="Relieving self">
  <need id="bladder" label="Bladder" seek="30" urgent="20" critical="10">
    <drains perGameMinute="0.03"/>
    <satisfiedBy chain="relieve_bladder"/>
    <satisfiedBy chain="relieve_both" when="seeking(digestion)"/>
  </need>
  <need id="digestion" label="Digestion" seek="30" urgent="20" critical="10">
    <drains perGameMinute="0.02"/>
    <satisfiedBy chain="relieve_both" when="seeking(bladder)"/>
  </need>
  <chain id="relieve_bladder"><go query="place_to_relieve"/><do action="urinate"/></chain>
  <chain id="relieve_both"><go query="place_to_relieve"/><do action="relieve_both"/></chain>
  <query id="place_to_relieve">
    <from capability="toilet"/>
    <from place="grid" spacing="3" radius="25"/>
    <from place="here" fallback="true"/>
    <exclude fact="on_shore"/>
    <prefer fact="is_place"/>
    <weigh fact="distance" by="-1"/>
    <weigh fact="waste_nearby" by="10"/>
    <weigh fact="food_nearby" by="-20"/>
  </query>
  <action id="urinate" seconds="2" hands="any" interruptable="false"><restore need="bladder" amount="100"/></action>
  <action id="relieve_both" seconds="5" hands="any" interruptable="false">
    <restore need="bladder" amount="100"/>
    <restore need="digestion" amount="100"/>
    <spawn def="BioPile"/>
  </action>
</behavior>)xml");
	write("config/behaviors/human/sleeping.xml", R"(<behavior id="sleeping" label="Sleeping">
  <need id="energy" label="Energy" seek="30" urgent="20" critical="10">
    <drains perGameMinute="0.05"/>
    <satisfiedBy chain="sleep"/>
  </need>
  <chain id="sleep"><go query="place_to_sleep"/><do action="sleep"/></chain>
  <query id="place_to_sleep">
    <from capability="sleepable"/><from place="here"/>
    <prefer fact="sleep_quality" below="1"/>
    <weigh fact="distance" by="-1"/>
  </query>
  <action id="sleep" seconds="8" hands="any" interruptable="true" asleep="true">
    <restore need="energy" amount="60" scaleBy="sleep_quality"/>
  </action>
</behavior>)");
	write("config/behaviors/human/temperature.xml", R"(<behavior id="temperature" label="Temperature">
  <need id="temperature" label="Temperature" seek="40" urgent="25" critical="15"><setBy provider="thermal_comfort"/></need>
</behavior>)");
	write("config/behaviors/human/enjoying_nature.xml", R"(<behavior id="enjoying_nature" label="Enjoying nature">
  <need id="recreation" label="Recreation" seek="30" urgent="20" critical="10">
    <drains perGameMinute="0.01"/>
    <satisfiedBy chain="enjoy_nature"/>
  </need>
  <chain id="enjoy_nature"><go query="pleasant_spot"/><do action="enjoy_nature"/></chain>
  <query id="pleasant_spot" pick="weighted">
    <from place="around" radius="30" count="8"/>
    <exclude fact="distance" above="30"/>
    <weigh fact="distance" by="-0.1"/>
  </query>
  <action id="enjoy_nature" hands="any" interruptable="true" pays="along">
    <restore need="recreation" amount="2" seconds="60" upTo="90"/>
  </action>
</behavior>)");
	write("config/behaviors/common/wandering.xml", R"(<behavior id="wandering" label="Wandering">
  <idle chain="wander"/>
  <chain id="wander"><go query="wander_point"/></chain>
  <query id="wander_point" pick="weighted">
    <from place="around" minRadius="2.4" radius="8" count="1"/>
    <exclude fact="distance" above="8"/>
  </query>
</behavior>)");
	write("config/behaviors/human/soil_bed.xml", R"xml(<behavior id="soil_bed" label="Soiling a bed">
  <lowMood rampStart="25" rampFull="15" base="4">
    <target query="bed_to_soil"/>
    <weight when="dislikes(owner(target))" multiply="4"/>
    <weight per="holds_in_a_row" multiply="0.8"/>
    <requires when="count_of(own_bed) &lt; 1"/>
    <effect chain="soil_bed"/>
    <ends after="task"/>
    <relief amount="medium"/>
  </lowMood>
  <query id="bed_to_soil" pick="weighted">
    <from kind="bed"/>
    <exclude fact="distance" above="15"/>
    <exclude fact="owned_by_self"/>
    <weigh fact="owner_opinion" by="-5"/>
  </query>
  <chain id="soil_bed">
    <go to="target"/>
    <do action="urinate"/>
    <think thought="soiled_a_bed" to="self"/>
  </chain>
</behavior>)xml");

	const EntryLoad loaded = load();
	ASSERT_TRUE(loaded.errors.empty()) << ::testing::PrintToString(loaded.errors);
	EXPECT_TRUE(loaded.warnings.empty()) << ::testing::PrintToString(loaded.warnings);
	const EntryLibrary& library = loaded.library;

	EXPECT_EQ(library.behaviors.size(), 7U);
	ASSERT_TRUE(library.findBehavior("human/eating").valid()) << "a behavior's id is its path under behaviors/";
	const ChainEntry& fetch = library.chains[library.findChain("human/fetch_food").index];
	ASSERT_EQ(fetch.steps.size(), 2U);
	const auto& go = std::get<GoStep>(fetch.steps[0].step);
	EXPECT_EQ(go.query->handle, library.findQuery("human/food_to_eat")) << "a short id names an entry of the same folder";
	const auto& take = std::get<DoStep>(fetch.steps[1].step);
	EXPECT_EQ(take.action.handle, library.findAction("take")) << "else a top-level entry of the shared folders";
	EXPECT_EQ(take.claims, (1U << static_cast<int>(ClaimKind::Use)) | (1U << static_cast<int>(ClaimKind::SourceQuantity)));
	EXPECT_EQ(take.amount, StepAmount::Meal);

	const NeedEntry& hunger = library.needs[library.behaviors[library.findBehavior("human/eating").index].needs.front().index];
	EXPECT_EQ(library.needIds[hunger.id.index], "hunger");
	EXPECT_FLOAT_EQ(hunger.move.perGameMinute, 0.08F);
	ASSERT_EQ(hunger.satisfiedBy.size(), 2U);
	EXPECT_EQ(hunger.satisfiedBy[1].when, "carried_food < 1");
	const NeedEntry& temperature = library.needs[library.behaviors[library.findBehavior("human/temperature").index].needs.front().index];
	EXPECT_EQ(temperature.move.kind, NeedMove::Kind::SetBy);

	const ActionEntry& enjoy = library.actions[library.findAction("human/enjoy_nature").index];
	EXPECT_EQ(enjoy.duration, ActionDuration::OpenEnded);
	EXPECT_EQ(enjoy.pays, Pays::Along);
	EXPECT_TRUE(library.actions[library.findAction("human/sleep").index].asleep);
	EXPECT_EQ(library.actions[library.findAction("harvest").index].duration, ActionDuration::FromDurability);
	const QueryEntry& relief = library.queries[library.findQuery("human/place_to_relieve").index];
	ASSERT_EQ(relief.from.size(), 3U);
	EXPECT_TRUE(relief.from[2].fallback);
	EXPECT_EQ(library.queries[library.findQuery("common/wander_point").index].pick, QueryPick::Weighted);

	const BehaviorEntry& soil = library.behaviors[library.findBehavior("human/soil_bed").index];
	ASSERT_TRUE(soil.lowMood.has_value());
	EXPECT_EQ(soil.lowMood->effect.handle, library.findChain("human/soil_bed"));
	EXPECT_EQ(soil.lowMood->relief, Relief::Medium);
	EXPECT_EQ(soil.lowMood->weights[1].per, "holds_in_a_row");
}

TEST_F(BehaviorLoaderTest, AFileDeclaresTheLastPartOfItsIdAndSitsInAFolder) {
	sample(kIdleSample);
	write("config/behaviors/test/sample.xml", std::string("<behavior id=\"other\">") + kIdleSample + "</behavior>");
	EXPECT_EQ(onlyError(), "[behaviors] test/sample.xml: <behavior id=\"other\"> id must be the file's name, \"sample\", got \"other\"");

	std::filesystem::remove(root / "config/behaviors/test/sample.xml");
	write("config/behaviors/loose.xml", std::string("<behavior id=\"loose\">") + kIdleSample + "</behavior>");
	EXPECT_EQ(onlyError(), "[behaviors] loose.xml: a behavior file sits in a folder under behaviors/, the folder its id starts with");

	std::filesystem::remove(root / "config/behaviors/loose.xml");
	write("config/behaviors/test/sample.xml", "<behaviour id=\"sample\"/>");
	EXPECT_EQ(onlyError(), "[behaviors] test/sample.xml: the root element is not <behavior>");
}

TEST_F(BehaviorLoaderTest, TheSameFullIdTwiceIsAnErrorNamingBothFiles) {
	write("config/behaviors/test/a.xml", std::string("<behavior id=\"a\">") + kIdleSample + "</behavior>");
	write(
		"config/behaviors/test/b.xml",
		"<behavior id=\"b\"><idle chain=\"go_there\"/><chain id=\"go_there\"><go query=\"spot\"/></chain></behavior>"
	);
	EXPECT_EQ(
		onlyError(),
		"[behaviors] test/b.xml: <chain id=\"go_there\"> chain \"test/go_there\" is defined twice, first in [behaviors] test/a.xml"
	);
}

TEST_F(BehaviorLoaderTest, AFolderEntryHidingATopLevelOneLoadsWithAWarningAndWinsInsideItsFolder) {
	write("config/actions/shared.xml", R"(<actions><action id="drink" seconds="1" interruptable="false"><put/></action></actions>)");
	sample(
		R"(<need id="thirst" label="Thirst" seek="50" urgent="25" critical="10"><drains perGameMinute="0.1"/><satisfiedBy chain="drink_here"/></need>
  <chain id="drink_here"><do action="drink"/><do action="deposit"/></chain>
  <action id="drink" seconds="1.5" hands="free" interruptable="false"><restore need="thirst" amount="100"/></action>)"
	);
	write("config/actions/more.xml", R"(<actions><action id="deposit" seconds="1" interruptable="false"><put/></action></actions>)");

	EXPECT_EQ(
		onlyWarning(),
		"[behaviors] test/sample.xml: <action id=\"drink\"> action \"test/drink\" hides the top-level action \"drink\" ([actions] "
		"shared.xml) "
		"inside test/"
	);
	const EntryLibrary library = load().library;
	const ChainEntry&  chain = library.chains[library.findChain("test/drink_here").index];
	EXPECT_EQ(std::get<DoStep>(chain.steps[0].step).action.handle, library.findAction("test/drink"));
	EXPECT_EQ(std::get<DoStep>(chain.steps[1].step).action.handle, library.findAction("deposit"));
}

TEST_F(BehaviorLoaderTest, AChainNothingStartsLoadsWithAWarning) {
	sample(std::string(kIdleSample) + R"(<chain id="lonely"><go query="spot"/></chain>)");
	EXPECT_EQ(
		onlyWarning(),
		"[behaviors] test/sample.xml: <chain id=\"lonely\"> chain \"test/lonely\" is started by nothing: no need, idle row, work entry, "
		"order, or "
		"episode names it"
	);
}

TEST_F(BehaviorLoaderTest, TheScriptAttributeIsReservedOnEveryEntry) {
	sample(R"(<idle chain="go_there"/><chain id="go_there" script="go.lua"><go query="spot"/></chain>
  <query id="spot"><from place="here"/><weigh fact="distance" by="-1"/></query>)");
	EXPECT_EQ(
		onlyError(),
		"[behaviors] test/sample.xml: <chain id=\"go_there\"> script must be left out; it is reserved for a Lua hook that runs nothing "
		"yet, got "
		"\"go.lua\""
	);
}

TEST_F(BehaviorLoaderTest, UnknownElementsAttributesAndNamesAreErrors) {
	sample(std::string(kIdleSample) + "<task/>");
	EXPECT_EQ(onlyError(), "[behaviors] test/sample.xml: <behavior id=\"sample\">: unknown element <task>");

	sample(R"(<idle chain="go_there"/><chain id="go_there" loop="true"><go query="spot"/></chain>
  <query id="spot"><from place="here"/><weigh fact="distance" by="-1"/></query>)");
	EXPECT_EQ(onlyError(), "[behaviors] test/sample.xml: <chain id=\"go_there\">: unknown attribute 'loop'");

	sample(R"(<idle chain="go_there"/><chain id="go_there"><go query="spott"/></chain>
  <query id="spot"><from place="here"/><weigh fact="distance" by="-1"/></query>)");
	EXPECT_EQ(onlyError(), "[behaviors] test/sample.xml: <chain id=\"go_there\"> step 1 <go> query \"spott\" names no query");
}

TEST_F(BehaviorLoaderTest, NeedMistakesNameTheNeedAndTheValue) {
	const auto needError = [this](const std::string& need) {
		sample(std::string(kIdleSample) + need);
		return onlyError();
	};
	EXPECT_EQ(
		needError(R"(<need id="thirst" label="Thirst" seek="20" urgent="25" critical="10"><drains perGameMinute="0.1"/></need>)"),
		"[behaviors] test/sample.xml: <need id=\"thirst\"> urgent must be below seek (20), got \"25\""
	);
	EXPECT_EQ(
		needError(R"(<need id="thirst" label="Thirst" seek="abc" urgent="25" critical="10"><drains perGameMinute="0.1"/></need>)"),
		"[behaviors] test/sample.xml: <need id=\"thirst\"> seek must be a number, got \"abc\""
	);
	EXPECT_EQ(
		needError(
			R"(<need id="thirst" label="Thirst" seek="50" urgent="25" critical="10"><drains perGameMinute="0.1"/><setBy provider="thermal_comfort"/></need>)"
		),
		"[behaviors] test/sample.xml: <need id=\"thirst\"> has more than one of <drains> and <setBy>; a level moves in exactly one way"
	);
	EXPECT_EQ(
		needError(R"(<need id="thirst" label="Thirst" seek="50" urgent="25" critical="10"><setBy provider="weather"/></need>)"),
		"[behaviors] test/sample.xml: <need id=\"thirst\"> <setBy> provider must be one of thermal_comfort, got \"weather\""
	);
	EXPECT_EQ(
		needError(
			R"(<need id="thirst" label="Thirst" seek="50" urgent="25" critical="10"><drains perGameMinute="0.1"/><satisfiedBy when="x"/></need>)"
		),
		"[behaviors] test/sample.xml: <need id=\"thirst\"> row 1 <satisfiedBy> names neither a chain nor a work entry; give exactly one"
	);
	const std::string thirst =
		R"(<need id="thirst" label="Thirst" seek="50" urgent="25" critical="10"><drains perGameMinute="0.1"/></need>)";
	EXPECT_EQ(
		needError(thirst + thirst),
		"[behaviors] test/sample.xml: <behavior id=\"sample\"> <need id=\"thirst\"> is given twice in one behavior"
	);
	EXPECT_EQ(
		needError(R"(<action id="drink" seconds="1" interruptable="false"><restore need="thrist" amount="1"/></action>)"),
		"[behaviors] test/sample.xml: <action id=\"drink\"> effect 1 <restore> need \"thrist\" names no need of any behavior or creature"
	);
}

TEST_F(BehaviorLoaderTest, QueryMistakesNameTheQueryAndTheRow) {
	const auto queryError = [this](const std::string& query) {
		sample(std::string(R"(<idle chain="go_there"/><chain id="go_there"><go query="spot"/></chain>)") + query);
		return onlyError();
	};
	EXPECT_EQ(
		queryError(R"(<query id="spot"><from place="here"/></query>)"),
		"[behaviors] test/sample.xml: <query id=\"spot\"> neither weighs distance by a negative number nor excludes it above a bound, so "
		"nothing ends its search"
	);
	EXPECT_EQ(
		queryError(R"(<query id="spot" pick="weighted"><from place="here"/><weigh fact="distance" by="-1"/></query>)"),
		"[behaviors] test/sample.xml: <query id=\"spot\"> picks weighted but excludes no distance above a bound, so its draw has no bound"
	);
	EXPECT_EQ(
		queryError(R"(<query id="spot"><from place="here"/><weigh fact="nearness" by="-1"/></query>)"),
		"[behaviors] test/sample.xml: <query id=\"spot\"> row 2 <weigh> fact \"nearness\" names no fact the sim provides"
	);
	EXPECT_EQ(
		queryError(R"(<query id="spot"><from place="here"/><weigh fact="distance" by="-1"/><weigh fact="contains" by="1"/></query>)"),
		"[behaviors] test/sample.xml: <query id=\"spot\"> row 3 <weigh> fact \"contains\" takes one argument, as contains(tag)"
	);
	EXPECT_EQ(
		queryError(R"(<query id="spot"><from capability="drinkable" place="here"/><weigh fact="distance" by="-1"/></query>)"),
		"[behaviors] test/sample.xml: <query id=\"spot\"> row 1 <from> names more than one source (capability, place); give exactly one of "
		"capability, kind, item, yields, pile, box, place, goal"
	);
	EXPECT_EQ(
		queryError(R"(<query id="spot"><from goal="destination"/><weigh fact="distance" by="-1"/></query>)"),
		"[behaviors] test/sample.xml: <query id=\"spot\"> row 1 <from> goal \"destination\" reads the goal a work option serves, which "
		"only a "
		"query in work/ has"
	);
	EXPECT_EQ(
		queryError(R"(<query id="spot"><from item="Wod"/><weigh fact="distance" by="-1"/></query>)"),
		"[behaviors] test/sample.xml: <query id=\"spot\"> row 1 <from> item \"Wod\" names no definition"
	);
}

TEST_F(BehaviorLoaderTest, ActionMistakesNameTheActionAndTheEffect) {
	const auto actionError = [this](const std::string& action) {
		sample(
			std::string(kIdleSample) +
			R"(<need id="thirst" label="Thirst" seek="50" urgent="25" critical="10"><drains perGameMinute="0.1"/></need>)" + action
		);
		return onlyError();
	};
	EXPECT_EQ(
		actionError(R"(<action id="drink" interruptable="false"><put/></action>)"),
		"[behaviors] test/sample.xml: <action id=\"drink\"> has no duration; give seconds, workFrom with skill, or an open-ended effect"
	);
	EXPECT_EQ(
		actionError(
			R"(<action id="enjoy" seconds="5" interruptable="true" pays="along"><restore need="thirst" amount="2" seconds="60" upTo="90"/></action>)"
		),
		"[behaviors] test/sample.xml: <action id=\"enjoy\"> seconds must be left out; an open-ended action (a <work> effect or an upTo "
		"bound) runs "
		"until its bound, got \"5\""
	);
	EXPECT_EQ(
		actionError(R"(<action id="drink" seconds="1"><put/></action>)"),
		"[behaviors] test/sample.xml: <action id=\"drink\"> interruptable is missing"
	);
	EXPECT_EQ(
		actionError(R"(<action id="drink" seconds="1" interruptable="false" curve="2"><put/></action>)"),
		"[behaviors] test/sample.xml: <action id=\"drink\"> curve must be left out; it shapes an action that pays along, got \"2\""
	);
	EXPECT_EQ(
		actionError(R"(<action id="drink" seconds="1" interruptable="false"><put/><heal/></action>)"),
		"[behaviors] test/sample.xml: <action id=\"drink\">: unknown element <heal>"
	);
	EXPECT_EQ(
		actionError(
			R"(<action id="drink" seconds="1" interruptable="false"><restore need="thirst" amount="1" from="nutrition" scale="1"/></action>)"
		),
		"[behaviors] test/sample.xml: <action id=\"drink\"> effect 1 <restore> gives both amount and from; give exactly one"
	);
}

TEST_F(BehaviorLoaderTest, ChainRulesNameTheChainAndTheStep) {
	write("config/actions/shared.xml", kSharedActions);
	const auto chainError = [this](const std::string& chains) {
		sample(
			R"(<need id="thirst" label="Thirst" seek="50" urgent="25" critical="10"><drains perGameMinute="0.1"/><satisfiedBy chain="a"/></need>
  <action id="drink" seconds="1" interruptable="false"><restore need="thirst" amount="1"/></action>
  <query id="spot"><from place="here"/><weigh fact="distance" by="-1"/></query>)" +
			chains
		);
		return onlyError();
	};
	EXPECT_EQ(
		chainError(
			R"(<chain id="a"><do action="pickup" claims="use source_quantity slot"/><do action="deposit" claims="inbound in_flight"/></chain>)"
		),
		"[behaviors] test/sample.xml: <chain id=\"a\"> names 5 claims with the chains it runs; a task holds four"
	);
	EXPECT_EQ(
		chainError(
			R"(<chain id="a"><do action="deposit" claims="inbound in_flight"/><do action="deposit" claims="inbound in_flight"/></chain>)"
		),
		"[behaviors] test/sample.xml: <chain id=\"a\"> names 4 inbound and in_flight claims with the chains it runs; a suspended record "
		"holds two"
	);
	EXPECT_EQ(
		chainError(R"(<chain id="a"><run chain="b"/></chain><chain id="b"><run chain="a"/></chain>)"),
		"[behaviors] test/sample.xml: <chain id=\"a\"> runs itself: \"test/a\" -> \"test/b\" -> \"test/a\""
	);
	EXPECT_EQ(
		chainError(R"(<chain id="a"><do action="pickup" claims="use grab"/></chain>)"),
		"[behaviors] test/sample.xml: <chain id=\"a\"> step 1 <do> claims \"grab\" is not a claim kind (use, source_quantity, slot, "
		"inbound, in_flight)"
	);
	EXPECT_EQ(
		chainError(R"(<chain id="a"><do action="drink" amount="meal"/></chain>)"),
		"[behaviors] test/sample.xml: <chain id=\"a\"> step 1 <do> amount is for a step whose action takes or puts; \"drink\" does neither"
	);
	EXPECT_EQ(
		chainError(
			R"(<chain id="a"><go query="food"/><do action="take"/></chain>
  <query id="food"><from yields="edible"/><from pile="edible"/><from box="edible"/><from capability="drinkable"/><weigh fact="distance" by="-1"/></query>)"
		),
		"[behaviors] test/sample.xml: <chain id=\"a\"> step 2 <do> action group \"take\" has no member for drinkable, which query \"food\" "
		"can return"
	);
}

// --- Actions ---------------------------------------------------------------------------------------------

TEST_F(ActionLoaderTest, TheSharedActionsLoadAndTheGroupResolvesItsMembers) {
	write("config/actions/shared.xml", kSharedActions);
	const EntryLoad loaded = load();
	ASSERT_TRUE(loaded.errors.empty()) << ::testing::PrintToString(loaded.errors);
	const EntryLibrary& library = loaded.library;
	EXPECT_EQ(library.actions.size(), 10U);
	const ActionEntry& take = library.actions[library.findAction("take").index];
	ASSERT_TRUE(take.group);
	ASSERT_EQ(take.members.size(), 3U);
	EXPECT_EQ(take.members[2].handle, library.findAction("withdraw"));
	const ActionEntry& clear = library.actions[library.findAction("clear").index];
	EXPECT_TRUE(std::get<TakeEffect>(clear.effects.front()).clears);
	EXPECT_EQ(
		std::get<WorkEffect>(library.actions[library.findAction("deconstruct").index].effects.front()).direction, WorkDirection::Down
	);
}

TEST_F(ActionLoaderTest, GroupAndFolderMistakesNameTheFileAndTheEntry) {
	const auto actionsError = [this](const std::string& actions) {
		write("config/actions/shared.xml", "<actions>" + actions + "</actions>");
		return onlyError();
	};
	const std::string pickup = R"(<action id="pickup" on="loose_item" seconds="0.5" interruptable="false"><take/></action>)";
	EXPECT_EQ(
		actionsError(pickup + R"(<actionGroup id="take">pickup</actionGroup><actionGroup id="outer">take</actionGroup>)"),
		"[actions] shared.xml: <actionGroup id=\"outer\"> member \"take\" is an action group; a group's members are actions"
	);
	EXPECT_EQ(
		actionsError(
			pickup +
			R"(<action id="deposit" seconds="1" interruptable="false"><put/></action><actionGroup id="take">pickup deposit</actionGroup>)"
		),
		"[actions] shared.xml: <actionGroup id=\"take\"> member \"deposit\" has no on, so no target can choose it"
	);
	EXPECT_EQ(
		actionsError(
			pickup +
			R"(<action id="lift" on="loose_item" seconds="1" interruptable="false"><take/></action><actionGroup id="take">pickup lift</actionGroup>)"
		),
		"[actions] shared.xml: <actionGroup id=\"take\"> members \"pickup\" and \"lift\" both work on loose_item"
	);
	EXPECT_EQ(actionsError(R"(<actionGroup id="take"/>)"), "[actions] shared.xml: <actionGroup id=\"take\"> names no member action");
	EXPECT_EQ(
		actionsError(pickup + R"(<actionGroup id="pickup">pickup</actionGroup>)"),
		"[actions] shared.xml: <actionGroup id=\"pickup\"> action \"pickup\" is defined twice, first in [actions] shared.xml"
	);
	EXPECT_EQ(
		actionsError(R"(<action id="lift" on="tree" seconds="1" interruptable="false"><take/></action>)"),
		"[actions] shared.xml: <action id=\"lift\"> on must be a capability (edible, drinkable, sleepable, toilet, waste, carryable, "
		"harvestable, "
		"craftable, storage, washable), loose_item, or packaged, got \"tree\""
	);

	write("config/actions/shared.xml", "<actionList/>");
	EXPECT_EQ(onlyError(), "[actions] shared.xml: the root element is not <actions>");

	std::filesystem::remove_all(root / "config/actions");
	EXPECT_EQ(onlyError(), "[actions] " + (root / "config" / "actions").generic_string() + ": not a folder");
}

// --- Work ------------------------------------------------------------------------------------------------

TEST_F(WorkLoaderTest, TheSpecsHaulLoadsWithItsRungsAndSources) {
	write("config/actions/shared.xml", kSharedActions);
	write("config/work/hauling.xml", R"(<colonyWork>
  <category id="hauling" label="Hauling" order="6"/>
  <work id="haul_pile" goal="haul" category="hauling" rung="opportunistic_work" gathers="true">
    <source query="pile_for_goal"/>
    <chain id="haul_from_pile"/>
  </work>
  <work id="pull_from_box" goal="haul" category="hauling" rung="opportunistic_work">
    <source query="box_to_pull_from"/>
    <chain id="haul_from_box"/>
  </work>
  <work id="carry_in" goal="root" source="carried" category="hauling" rung="opportunistic_work" gathers="true">
    <chain id="carry_in"/>
  </work>
  <chain id="haul_from_pile">
    <have item="goal" fetch="pick_up_for_goal"/>
    <go to="destination"/>
    <do action="deposit" claims="inbound in_flight" amount="carried"/>
  </chain>
  <chain id="pick_up_for_goal"><go to="source"/><do action="pickup" claims="source_quantity"/></chain>
  <chain id="haul_from_box">
    <have item="goal" fetch="withdraw_for_goal"/>
    <go to="destination"/>
    <do action="deposit" claims="inbound in_flight" amount="carried"/>
  </chain>
  <chain id="withdraw_for_goal"><go to="source"/><do action="withdraw" claims="source_quantity"/></chain>
  <chain id="carry_in"><go to="destination"/><do action="deposit" claims="inbound in_flight" amount="carried"/></chain>
  <query id="pile_for_goal"><from pile="goal"/><weigh fact="distance" by="-1"/></query>
  <query id="box_to_pull_from">
    <from box="goal"/>
    <exclude fact="below_destination" below="1"/>
    <weigh fact="distance" by="-1"/>
  </query>
</colonyWork>)");

	const EntryLoad loaded = load();
	ASSERT_TRUE(loaded.errors.empty()) << ::testing::PrintToString(loaded.errors);
	EXPECT_TRUE(loaded.warnings.empty()) << ::testing::PrintToString(loaded.warnings);
	const EntryLibrary& library = loaded.library;
	const WorkEntry&	pile = library.work[library.findWork("haul_pile").index];
	EXPECT_EQ(kRungs[pile.rung], "opportunistic_work");
	EXPECT_TRUE(pile.gathers);
	EXPECT_EQ(pile.source->handle, library.findQuery("pile_for_goal"));
	EXPECT_EQ(pile.chain.handle, library.findChain("haul_from_pile"));
	EXPECT_EQ(pile.category.handle, library.findCategory("hauling"));
	const WorkEntry& carry = library.work[library.findWork("carry_in").index];
	EXPECT_EQ(carry.goal, WorkGoal::Root);
	EXPECT_TRUE(carry.sourceCarried);
	EXPECT_EQ(carry.chain.handle, library.findChain("carry_in")) << "a work entry and its chain may share an id";
	const ChainEntry& haul = library.chains[library.findChain("haul_from_pile").index];
	EXPECT_EQ(std::get<HaveStep>(haul.steps[0].step).item->kind, ItemArgument::Kind::Goal);
	EXPECT_EQ(library.categories[library.findCategory("hauling").index].order, 6);
	EXPECT_TRUE(library.categories[library.findCategory("hauling").index].canDisable);
}

TEST_F(WorkLoaderTest, WorkMistakesNameTheEntryAndTheAttribute) {
	const auto workError = [this](const std::string& work) {
		write(
			"config/work/w.xml",
			"<colonyWork><category id=\"farming\" label=\"Farming\" order=\"3\"/><chain id=\"c\"><go to=\"destination\"/></chain>" + work +
				"</colonyWork>"
		);
		return onlyError();
	};
	EXPECT_EQ(
		workError(R"(<work id="milk" goal="care" category="farming" rung="opportunistic_work"><chain id="c"/></work>)"),
		"[work] w.xml: <work id=\"milk\"> need is missing"
	);
	EXPECT_EQ(
		workError(R"(<work id="w" goal="haul" need="hunger" category="farming" rung="opportunistic_work"><chain id="c"/></work>)"),
		"[work] w.xml: <work id=\"w\"> need must be left out; it names the need of a care goal, got \"hunger\""
	);
	EXPECT_EQ(
		workError(R"(<work id="w" goal="haul" category="farming" rung="survival"><chain id="c"/></work>)"),
		"[work] w.xml: <work id=\"w\"> rung must be a rung other than survival, which is reserved, got \"survival\""
	);
	EXPECT_EQ(
		workError(R"(<work id="w" goal="haul" category="farming" rung="work"><chain id="c"/></work>)"),
		"[work] w.xml: <work id=\"w\"> rung must be a rung of the ladder (survival, critical_need, low_mood, player_order, urgent_need, "
		"own_unfinished_work, committed_work, ordinary_need, opportunistic_work, idle), got \"work\""
	);
	EXPECT_EQ(
		workError(R"(<work id="w" goal="haul" category="farm" rung="committed_work"><chain id="c"/></work>)"),
		"[work] w.xml: <work id=\"w\"> category \"farm\" names no category"
	);
	EXPECT_EQ(
		workError(R"(<work id="w" goal="haul" category="farming" rung="committed_work"><chain id="c"/><chain id="c"/></work>)"),
		"[work] w.xml: <work id=\"w\"> has 2 <chain> rows; it names exactly one"
	);
	EXPECT_EQ(
		workError(
			R"(<work id="w" goal="root" source="carried" category="farming" rung="committed_work"><source query="q"/><chain id="c"/></work>)"
		),
		"[work] w.xml: <work id=\"w\"> gives both source=\"carried\" and a <source> row; give one"
	);
	EXPECT_EQ(
		workError(R"(<work id="w" goal="mine" category="farming" rung="committed_work"><chain id="c"/></work>)"),
		"[work] w.xml: <work id=\"w\"> goal must be one of haul, harvest, clear, craft, build, deconstruct, place_packaged, care, root, "
		"got \"mine\""
	);
	EXPECT_EQ(
		workError(R"(<category id="hauling" label="Hauling" order="first"/>)"),
		"[work] w.xml: <category id=\"hauling\"> order must be a whole number from 0 to 2147483647, got \"first\""
	);
}

// --- Orders ----------------------------------------------------------------------------------------------

TEST_F(OrderLoaderTest, TheSpecsOrdersLoadAndReachIntoABehaviorsFolderByFullId) {
	write("config/actions/shared.xml", kSharedActions);
	sample(R"(<need id="bladder" label="Bladder" seek="30" urgent="20" critical="10"><drains perGameMinute="0.03"/></need>
  <action id="urinate" seconds="2" hands="any" interruptable="false"><restore need="bladder" amount="100"/></action>)");
	write("config/orders/orders.xml", R"(<orders>
  <order id="harvest" label="Harvest" on="harvestable" chain="harvest_target"/>
  <order id="relieve_here" label="Relieve self" on="ground" chain="relieve_at_target"/>
  <chain id="harvest_target"><go to="target"/><do action="harvest" claims="use"/></chain>
  <chain id="relieve_at_target"><go to="target"/><do action="test/urinate"/></chain>
</orders>)");
	const EntryLoad loaded = load();
	ASSERT_TRUE(loaded.errors.empty()) << ::testing::PrintToString(loaded.errors);
	const EntryLibrary& library = loaded.library;
	const OrderEntry&	relieve = library.orders[library.findOrder("relieve_here").index];
	EXPECT_EQ(relieve.on.kind, OrderTarget::Kind::Ground);
	EXPECT_EQ(relieve.label, "Relieve self");
	const ChainEntry& chain = library.chains[relieve.chain.handle.index];
	EXPECT_EQ(std::get<DoStep>(chain.steps[1].step).action.handle, library.findAction("test/urinate"));
	EXPECT_EQ(library.orders[library.findOrder("harvest").index].on.capability, CapabilityType::Harvestable);
}

TEST_F(OrderLoaderTest, OrderMistakesNameTheOrder) {
	const auto orderError = [this](const std::string& order) {
		write("config/orders/orders.xml", "<orders><chain id=\"harvest_target\"><go to=\"target\"/></chain>" + order + "</orders>");
		return onlyError();
	};
	EXPECT_EQ(
		orderError(R"(<order id="harvest" label="Harvest" on="tree" chain="harvest_target"/>)"),
		"[orders] orders.xml: <order id=\"harvest\"> on must be a capability (edible, drinkable, sleepable, toilet, waste, carryable, "
		"harvestable, "
		"craftable, storage, washable) or ground, got \"tree\""
	);
	EXPECT_EQ(
		orderError(R"(<order id="harvest" label="Harvest" on="harvestable" chain="harvest_targt"/>)"),
		"[orders] orders.xml: <order id=\"harvest\"> chain \"harvest_targt\" names no chain"
	);
	EXPECT_EQ(
		orderError(R"(<order id="harvest" on="harvestable" chain="harvest_target"/>)"),
		"[orders] orders.xml: <order id=\"harvest\"> label is missing"
	);
}

// --- Facts -----------------------------------------------------------------------------------------------

TEST_F(FactLoaderTest, EachTunedFactReadsItsValue) {
	const EntryLoad loaded = load();
	ASSERT_TRUE(loaded.errors.empty()) << ::testing::PrintToString(loaded.errors);
	const auto tuning = [&loaded](std::string_view fact) {
		const auto found = std::ranges::find(kFacts, fact, &FactDeclaration::name);
		return loaded.library.factTuning[static_cast<std::size_t>(found - kFacts.begin())];
	};
	EXPECT_FLOAT_EQ(tuning("on_shore"), 1.5F);
	EXPECT_FLOAT_EQ(tuning("food_nearby"), 15.0F);
	EXPECT_FLOAT_EQ(tuning("sleep_quality"), 0.5F);
	EXPECT_TRUE(std::isnan(tuning("distance"))) << "a fact that reads no tuning has no value";
}

TEST_F(FactLoaderTest, FactMistakesNameTheFact) {
	write(
		"config/facts/facts.xml",
		R"(<facts><fact id="waste_nearby" within="8"/><fact id="food_nearby" within="15"/><fact id="sleep_quality" ground="0.5"/></facts>)"
	);
	EXPECT_EQ(onlyError(), "[facts] <fact id=\"on_shore\"> within is given by no file in facts/");

	write("config/facts/facts.xml", kFactTuning);
	write("config/facts/more.xml", R"(<facts><fact id="on_shore" within="2"/></facts>)");
	EXPECT_EQ(onlyError(), "[facts] more.xml: <fact id=\"on_shore\"> is also given in [facts] facts.xml");
	std::filesystem::remove(root / "config/facts/more.xml");

	const auto factError = [this](const std::string& fact) {
		write("config/facts/facts.xml", std::string(kFactTuning).insert(std::string(kFactTuning).find("</facts>"), fact));
		return onlyError();
	};
	EXPECT_EQ(
		factError(R"(<fact id="distance" within="3"/>)"),
		"[facts] facts.xml: <fact id=\"distance\"> reads no tuning value, so it has no row here"
	);
	EXPECT_EQ(
		factError(R"(<fact id="wetness" within="1"/>)"), "[facts] facts.xml: <fact> id must be a fact the sim provides, got \"wetness\""
	);
	write(
		"config/facts/facts.xml",
		R"(<facts><fact id="on_shore" within="1.5"/><fact id="waste_nearby" within="8"/><fact id="food_nearby" within="15"/><fact id="sleep_quality" ground="2"/></facts>)"
	);
	EXPECT_EQ(onlyError(), "[facts] facts.xml: <fact id=\"sleep_quality\"> ground must be a number from 0 to 1, got \"2\"");
}

// --- Creatures -------------------------------------------------------------------------------------------

namespace {

	constexpr const char* kGrazing = R"(<behavior id="eating" label="Grazing">
  <need id="hunger" label="Hunger" seek="50" urgent="25" critical="10">
    <drains perGameMinute="0.08"/>
    <satisfiedBy chain="graze"/>
  </need>
  <chain id="graze"><go query="grass_nearby"/><do action="graze"/></chain>
  <query id="grass_nearby"><from capability="harvestable"/><weigh fact="distance" by="-1"/></query>
  <action id="graze" seconds="10" hands="any" interruptable="true" pays="along"><restore need="hunger" amount="30"/></action>
</behavior>)";

	constexpr const char* kDrinking = R"(<behavior id="drinking_at_water" label="Drinking at water">
  <need id="thirst" label="Thirst" seek="50" urgent="25" critical="10"><drains perGameMinute="0.12"/></need>
</behavior>)";

	constexpr const char* kMilking = R"(<behavior id="being_milked">
  <need id="milking" label="Milking" seek="40" urgent="20" critical="5"><drains perGameMinute="0.02"/></need>
</behavior>)";

} // namespace

/// A cow folder with its own behavior file, and the library behaviors it includes.
class CreatureLoaderTest : public EntryLoaderTest {
  protected:
	void SetUp() override {
		EntryLoaderTest::SetUp();
		write("config/behaviors/herbivore/eating.xml", kGrazing);
		write("config/behaviors/common/drinking_at_water.xml", kDrinking);
		write("creatures/Cow/being_milked.xml", kMilking);
	}

	void cow(const std::string& block, const std::string& variants = "") const {
		write(
			"creatures/Cow/Cow.xml",
			"<AssetDefinitions><AssetDef><defName>Cow</defName><assetType>simple</assetType>" + block + "</AssetDef>" + variants +
				"</AssetDefinitions>"
		);
	}

	[[nodiscard]] std::string cowError() const { return onlyError({"Cow"}); }
};

TEST_F(CreatureLoaderTest, TheSpecsCowIncludesItsBehaviorsAndOverridesOneValue) {
	cow(R"(<creature mood="false">
  <behavior id="herbivore/eating"/>
  <behavior id="common/drinking_at_water"/>
  <behavior id="Cow/being_milked"/>
  <need id="hunger"><drains perGameMinute="0.05"/></need>
</creature>)");
	const EntryLoad loaded = load({"Cow"});
	ASSERT_TRUE(loaded.errors.empty()) << ::testing::PrintToString(loaded.errors);
	const EntryLibrary&	 library = loaded.library;
	const CreatureEntry* creature = library.findCreature("Cow");
	ASSERT_NE(creature, nullptr);
	EXPECT_FALSE(creature->mood);
	ASSERT_EQ(creature->behaviors.size(), 3U);
	EXPECT_EQ(creature->behaviors[2], library.findBehavior("Cow/being_milked")) << "a creature's own behavior takes its name as the folder";
	ASSERT_EQ(creature->needs.size(), 3U);
	const CreatureNeed& hunger = creature->needs[0];
	EXPECT_EQ(library.needIds[hunger.id.index], "hunger");
	EXPECT_FLOAT_EQ(hunger.move.perGameMinute, 0.05F) << "the row changes only the value it gives";
	EXPECT_EQ(hunger.label, "Hunger");
	EXPECT_FLOAT_EQ(hunger.seek, 50.0F);
	EXPECT_EQ(hunger.satisfiedBy.front().chain->handle, library.findChain("herbivore/graze"));
	EXPECT_EQ(library.needIds[creature->needs[2].id.index], "milking");
	EXPECT_FALSE(hunger.moodCost.has_value());
}

TEST_F(CreatureLoaderTest, OwnBehaviorsJoinTheOrderAfterTheIncludesAndCreatureFilesAddRows) {
	cow(R"(<creature mood="true"><behavior id="herbivore/eating"/><moodCost need="hunger" cost="25"/></creature>)");
	write(
		"creatures/Cow/herd.xml",
		R"(<creature><behavior id="common/drinking_at_water"/><moodCost need="thirst" cost="25"/><moodCost need="milking" cost="5"/></creature>)"
	);
	const EntryLoad loaded = load({"Cow"});
	ASSERT_TRUE(loaded.errors.empty()) << ::testing::PrintToString(loaded.errors);
	const CreatureEntry* creature = loaded.library.findCreature("Cow");
	ASSERT_NE(creature, nullptr);
	ASSERT_EQ(creature->behaviors.size(), 3U);
	EXPECT_EQ(creature->behaviors[1], loaded.library.findBehavior("common/drinking_at_water"));
	EXPECT_EQ(creature->behaviors[2], loaded.library.findBehavior("Cow/being_milked"));
	EXPECT_EQ(creature->needs[2].moodCost, 5.0F);
}

TEST_F(CreatureLoaderTest, TheCreatureBlockSitsOnTheBaseDefinitionOnly) {
	cow("");
	EXPECT_EQ(
		cowError(),
		"[creatures] Cow/Cow.xml: <AssetDef defName=\"Cow\"> has no <creature> block; the base definition of a creature folder carries one"
	);
	cow(R"(<creature mood="false"/>)", R"(<AssetDef><defName>Cow_up</defName><creature mood="false"/></AssetDef>)");
	EXPECT_EQ(
		cowError(),
		"[creatures] Cow/Cow.xml: <AssetDef defName=\"Cow_up\"> has a <creature> block, which only the base definition (Cow) carries"
	);
	cow("<creature/>");
	EXPECT_EQ(cowError(), "[creatures] Cow/Cow.xml: <creature> mood is missing");
	write(
		"creatures/Cow/being_milked.xml",
		R"(<behavior id="milked"><need id="milking" label="Milking" seek="40" urgent="20" critical="5"><drains perGameMinute="0.02"/></need></behavior>)"
	);
	cow(R"(<creature mood="false"/>)");
	EXPECT_EQ(
		cowError(),
		"[creatures] Cow/being_milked.xml: <behavior id=\"milked\"> id must be the file's name, \"being_milked\", got \"milked\""
	);
}

TEST_F(CreatureLoaderTest, IncludesAndNeedRowsAreCheckedAgainstTheBehaviors) {
	cow(R"(<creature mood="false"><behavior id="herbivore/eatin"/></creature>)");
	EXPECT_EQ(cowError(), "[creatures] Cow/Cow.xml: <creature> row 1 <behavior> id \"herbivore/eatin\" names no behavior");

	cow(R"(<creature mood="false"><behavior id="herbivore/eating"/></creature>)");
	write("creatures/Cow/herd.xml", R"(<creature><behavior id="herbivore/eating"/></creature>)");
	EXPECT_EQ(
		cowError(),
		"[creatures] Cow/herd.xml: <creature> row 1 <behavior> id \"herbivore/eating\" includes a behavior already included at [creatures] "
		"Cow/Cow.xml: <creature> row 1 <behavior>"
	);
	std::filesystem::remove(root / "creatures/Cow/herd.xml");

	write("config/behaviors/common/browsing.xml", std::string(kGrazing).replace(std::string(kGrazing).find("eating"), 6, "browsing"));
	cow(R"(<creature mood="false"><behavior id="herbivore/eating"/><behavior id="common/browsing"/></creature>)");
	EXPECT_EQ(
		cowError(),
		"[creatures] Cow/Cow.xml: <creature> need \"hunger\" comes from more than one included behavior (\"herbivore/eating\", "
		"\"common/browsing\"); settle it with <need id=\"hunger\" from=\"...\"/>"
	);
	cow(
		R"(<creature mood="false"><behavior id="herbivore/eating"/><behavior id="common/browsing"/><need id="hunger" from="common/browsing"/></creature>)"
	);
	const EntryLoad settled = load({"Cow"});
	ASSERT_TRUE(settled.errors.empty()) << ::testing::PrintToString(settled.errors);
	EXPECT_EQ(settled.library.findCreature("Cow")->needs.front().behavior, settled.library.findBehavior("common/browsing"));
	std::filesystem::remove(root / "config/behaviors/common/browsing.xml");

	cow(R"(<creature mood="false"><behavior id="herbivore/eating"/><need id="hunger"><drains perGameMinute="0.05"/></need></creature>)");
	write("creatures/Cow/herd.xml", R"(<creature><need id="hunger"><drains perGameMinute="0.06"/></need></creature>)");
	EXPECT_EQ(
		cowError(),
		"[creatures] Cow/herd.xml: <need id=\"hunger\"> how the level moves (<drains> or <setBy>) is also set in [creatures] Cow/Cow.xml"
	);
	std::filesystem::remove(root / "creatures/Cow/herd.xml");

	cow(
		R"(<creature mood="false"><behavior id="herbivore/eating"/><need id="hunger"><satisfiedBy chain="herbivore/graze"/></need></creature>)"
	);
	EXPECT_EQ(
		cowError(),
		"[creatures] Cow/Cow.xml: <need id=\"hunger\"> has <satisfiedBy> rows; what meets an included need comes from its behavior"
	);

	cow(R"(<creature mood="false"><need id="calving" label="Calving"/></creature>)");
	EXPECT_EQ(
		cowError(),
		"[creatures] Cow/Cow.xml: <creature> need \"calving\" is no included behavior's need, so the creature's rows give all of it, and "
		"they give "
		"no seek, urgent, critical, <drains> or <setBy>"
	);
}

TEST_F(CreatureLoaderTest, MoodCostsFollowTheCreaturesMood) {
	cow(
		R"(<creature mood="true"><behavior id="herbivore/eating"/><behavior id="common/drinking_at_water"/><moodCost need="hunger" cost="25"/><moodCost need="milking" cost="5"/></creature>)"
	);
	EXPECT_EQ(
		cowError(), "[creatures] Cow/Cow.xml: <creature> need \"thirst\" has no <moodCost>; a creature with mood gives every need a cost"
	);
	cow(R"(<creature mood="false"><behavior id="herbivore/eating"/><moodCost need="hunger" cost="25"/></creature>)");
	EXPECT_EQ(
		cowError(), "[creatures] Cow/Cow.xml: <moodCost need=\"hunger\"> gives a mood cost, and the creature has no mood (mood=\"false\")"
	);
	cow(
		R"(<creature mood="true"><behavior id="herbivore/eating"/><moodCost need="hunger" cost="25"/><moodCost need="milking" cost="5"/><moodCost need="hygiene" cost="5"/></creature>)"
	);
	EXPECT_EQ(cowError(), "[creatures] Cow/Cow.xml: <moodCost need=\"hygiene\"> names a need the creature does not have");
}

// --- The shipped config ----------------------------------------------------------------------------------

TEST(ShippedEntriesTest, TheShippedConfigAndCreaturesLoadClean) {
	const std::filesystem::path config = repoRoot() / "assets" / "config";
	ASSERT_TRUE(PriorityConfig::Get().loadFromFile(config / "arbitration", "priority-tuning.xml"));
	const CreatureConfigFiles human = creatureConfigFiles(repoRoot() / "assets" / "world" / "creatures" / "Human");
	ASSERT_TRUE(human.errors.empty()) << ::testing::PrintToString(human.errors);
	const EntryLoad loaded = loadEntries(
		EntrySources{
			.configRoot = config,
			.creatures = {human},
			.rungs = PriorityConfig::Get().rungs(),
			.definitionId = [](std::string_view) { return std::optional<uint32_t>(); },
		}
	);
	PriorityConfig::Get().clear();
	EXPECT_TRUE(loaded.errors.empty()) << ::testing::PrintToString(loaded.errors);
	EXPECT_TRUE(loaded.warnings.empty()) << ::testing::PrintToString(loaded.warnings);
	const CreatureEntry* creature = loaded.library.findCreature("Human");
	ASSERT_NE(creature, nullptr);
	EXPECT_TRUE(creature->mood);
}
