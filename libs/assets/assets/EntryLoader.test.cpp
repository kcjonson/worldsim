// The behavior library's loaders (behaviors, actions, work, orders, facts) and the creature loader: each entry
// form the spec gives, the id rules, and each load error once with its exact text.

#include "assets/EntryLoader.h"

#include "assets/NeedsConfig.h"
#include "assets/PriorityConfig.h"
#include "assets/ShippedEntries.test.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
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

	/// A token unique to this run, so test binaries ctest runs in parallel never share a folder.
	const std::string& runToken() {
		static const std::string token = [] {
			std::random_device device;
			return std::to_string(device()) + "_" + std::to_string(device());
		}();
		return token;
	}

	/// One case of a table: the text written, and the one error (or warning) it gives.
	struct Case {
		std::string written;
		std::string expected;
	};

	/// A config root and a creatures root under a fresh temp folder, the shared folders present and the facts tuned.
	class EntryLoaderTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
			root = std::filesystem::temp_directory_path() /
				   ("worldsim-entries-" + runToken() + "-" + std::string(info->test_suite_name()) + "-" + info->name());
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
				.trackedNeeds = trackedNeeds,
				.definitionId = [](std::string_view name) -> std::optional<uint32_t> {
					static const std::map<std::string, uint32_t, std::less<>> kDefinitions = {
						{"Berry", 11}, {"Misc_BioPile", 12}, {"Milk", 13}
					};
					const auto found = kDefinitions.find(name);
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
			EXPECT_TRUE(loaded.warnings.empty()) << "a failed load keeps no warning";
			return loaded.errors.size() == 1 ? loaded.errors.front() : std::string();
		}

		/// The load's warnings; fails the test when it has an error.
		[[nodiscard]] std::vector<std::string> warnings() const {
			const EntryLoad loaded = load();
			EXPECT_TRUE(loaded.errors.empty()) << ::testing::PrintToString(loaded.errors);
			return loaded.warnings;
		}

		/// A clean load; fails the test on any error.
		[[nodiscard]] EntryLoad clean(const std::vector<std::string>& creatures = {}) const {
			EntryLoad loaded = load(creatures);
			EXPECT_TRUE(loaded.errors.empty()) << ::testing::PrintToString(loaded.errors);
			return loaded;
		}

		std::filesystem::path root;
		/// The sim's tracked needs the load is given; none unless a test sets them.
		std::vector<std::string> trackedNeeds;
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

	constexpr const char* kThirst =
		R"(<need id="thirst" label="Thirst" seek="50" urgent="25" critical="10"><drains perGameMinute="0.1"/></need>)";

	template <typename Step>
	const Step* stepOf(const ChainEntry& chain, std::size_t index) {
		return index < chain.steps.size() ? std::get_if<Step>(&chain.steps[index].step) : nullptr;
	}

} // namespace

// --- Behaviors -------------------------------------------------------------------------------------------

TEST_F(BehaviorLoaderTest, TheSpecsHumanBehaviorsLoadWithEveryReferenceResolved) {
	write("config/actions/shared.xml", kSharedActions);
	write("config/behaviors/human/eating.xml", R"xml(<behavior id="eating" label="Eating">
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
</behavior>)xml");
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
    <spawn def="Misc_BioPile"/>
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
	// The step and effect forms the spec's human behaviors leave out.
	write("config/behaviors/human/tinkering.xml", R"(<behavior id="tinkering">
  <idle chain="tinker_about"/>
  <chain id="tinker_about">
    <have capability="edible" fetch="fetch_food"/>
    <go query="berry_spot"><ends on="target_gone"/><ends on="held_gone"/></go>
    <need id="hunger" change="-5"/>
    <do action="tinker"/>
    <drop/>
    <run chain="common/wander"/>
    <leave_colony/>
  </chain>
  <query id="berry_spot"><from item="Berry"/><weigh fact="distance" by="-1"/></query>
  <action id="tinker" hands="free" interruptable="true">
    <craft/>
    <damage amount="2"/>
    <spawn def="Berry" into="hands"/>
    <consume capability="edible" count="1" seconds="2" upTo="95" need="hunger"/>
  </action>
</behavior>)");

	const EntryLoad loaded = clean();
	EXPECT_TRUE(loaded.warnings.empty()) << ::testing::PrintToString(loaded.warnings);
	const EntryLibrary& library = loaded.library;

	EXPECT_EQ(library.behaviors.size(), 8U);
	ASSERT_TRUE(library.findBehavior("human/eating").valid()) << "a behavior's id is its path under behaviors/";
	ASSERT_TRUE(library.findChain("human/fetch_food").valid());
	const ChainEntry& fetch = library.chains[library.findChain("human/fetch_food").index];
	const GoStep*	  go = stepOf<GoStep>(fetch, 0);
	ASSERT_NE(go, nullptr);
	ASSERT_TRUE(go->query.has_value());
	EXPECT_EQ(go->query->handle, library.findQuery("human/food_to_eat")) << "a short id names an entry of the same folder";
	const DoStep* take = stepOf<DoStep>(fetch, 1);
	ASSERT_NE(take, nullptr);
	EXPECT_EQ(take->action.handle, library.findAction("take")) << "else a top-level entry of the shared folders";
	EXPECT_EQ(take->claims, (1U << static_cast<int>(ClaimKind::Use)) | (1U << static_cast<int>(ClaimKind::SourceQuantity)));
	EXPECT_EQ(take->amount, StepAmount::Meal);

	const BehaviorEntry& eating = library.behaviors[library.findBehavior("human/eating").index];
	ASSERT_EQ(eating.needs.size(), 1U);
	const NeedEntry& hunger = library.needs[eating.needs.front().index];
	EXPECT_EQ(library.needIds[hunger.id.index], "hunger");
	EXPECT_FLOAT_EQ(hunger.move.perGameMinute, 0.08F);
	ASSERT_EQ(hunger.satisfiedBy.size(), 2U);
	EXPECT_EQ(hunger.satisfiedBy[1].when, "carried_food < 1");
	ASSERT_TRUE(library.findBehavior("human/temperature").valid());
	const BehaviorEntry& temperatureBehavior = library.behaviors[library.findBehavior("human/temperature").index];
	ASSERT_EQ(temperatureBehavior.needs.size(), 1U);
	EXPECT_EQ(library.needs[temperatureBehavior.needs.front().index].move.kind, NeedMove::Kind::SetBy);

	ASSERT_TRUE(library.findAction("human/enjoy_nature").valid());
	const ActionEntry& enjoy = library.actions[library.findAction("human/enjoy_nature").index];
	EXPECT_EQ(enjoy.duration, ActionDuration::OpenEnded);
	EXPECT_EQ(enjoy.pays, Pays::Along);
	ASSERT_TRUE(library.findAction("human/sleep").valid());
	EXPECT_TRUE(library.actions[library.findAction("human/sleep").index].asleep);
	ASSERT_TRUE(library.findAction("harvest").valid());
	EXPECT_EQ(library.actions[library.findAction("harvest").index].duration, ActionDuration::FromDurability);
	ASSERT_TRUE(library.findQuery("human/place_to_relieve").valid());
	const QueryEntry& relief = library.queries[library.findQuery("human/place_to_relieve").index];
	ASSERT_EQ(relief.from.size(), 3U);
	EXPECT_TRUE(relief.from[2].fallback);
	ASSERT_TRUE(library.findQuery("common/wander_point").valid());
	EXPECT_EQ(library.queries[library.findQuery("common/wander_point").index].pick, QueryPick::Weighted);

	const BehaviorEntry& soil = library.behaviors[library.findBehavior("human/soil_bed").index];
	ASSERT_TRUE(soil.lowMood.has_value());
	EXPECT_EQ(soil.lowMood->effect.handle, library.findChain("human/soil_bed"));
	EXPECT_EQ(soil.lowMood->relief, Relief::Medium);
	EXPECT_TRUE(soil.lowMood->endsAfterTask);
	ASSERT_EQ(soil.lowMood->weights.size(), 2U);
	EXPECT_EQ(soil.lowMood->weights[1].per, "holds_in_a_row");

	ASSERT_TRUE(library.findChain("human/tinker_about").valid());
	const ChainEntry& tinker = library.chains[library.findChain("human/tinker_about").index];
	ASSERT_EQ(tinker.steps.size(), 7U);
	const HaveStep* have = stepOf<HaveStep>(tinker, 0);
	ASSERT_NE(have, nullptr);
	EXPECT_EQ(have->capability, CapabilityType::Edible);
	EXPECT_EQ(have->fetch.handle, library.findChain("human/fetch_food"));
	EXPECT_EQ(tinker.steps[1].ends, (1U << static_cast<int>(EndEvent::TargetGone)) | (1U << static_cast<int>(EndEvent::HeldGone)));
	const NeedStep* needStep = stepOf<NeedStep>(tinker, 2);
	ASSERT_NE(needStep, nullptr);
	EXPECT_EQ(needStep->need, hunger.id);
	EXPECT_FLOAT_EQ(needStep->change, -5.0F);
	EXPECT_NE(stepOf<DropStep>(tinker, 4), nullptr);
	const RunStep* run = stepOf<RunStep>(tinker, 5);
	ASSERT_NE(run, nullptr);
	EXPECT_EQ(run->chain.handle, library.findChain("common/wander")) << "a full id reaches another folder";
	EXPECT_NE(stepOf<LeaveColonyStep>(tinker, 6), nullptr);
	ASSERT_TRUE(library.findQuery("human/berry_spot").valid());
	const QueryEntry& berrySpot = library.queries[library.findQuery("human/berry_spot").index];
	ASSERT_EQ(berrySpot.from.size(), 1U);
	EXPECT_EQ(berrySpot.from.front().item.kind, ItemArgument::Kind::Definition);
	EXPECT_EQ(berrySpot.from.front().item.defNameId, 11U);
	ASSERT_TRUE(library.findAction("human/tinker").valid());
	const ActionEntry& tinkerAction = library.actions[library.findAction("human/tinker").index];
	ASSERT_EQ(tinkerAction.effects.size(), 4U);
	EXPECT_TRUE(std::holds_alternative<CraftEffect>(tinkerAction.effects[0]));
	ASSERT_TRUE(std::holds_alternative<DamageEffect>(tinkerAction.effects[1]));
	EXPECT_FLOAT_EQ(std::get<DamageEffect>(tinkerAction.effects[1]).amount, 2.0F);
	ASSERT_TRUE(std::holds_alternative<SpawnEffect>(tinkerAction.effects[2]));
	EXPECT_TRUE(std::get<SpawnEffect>(tinkerAction.effects[2]).intoHands);
	ASSERT_TRUE(std::holds_alternative<ConsumeEffect>(tinkerAction.effects[3]));
	EXPECT_EQ(std::get<ConsumeEffect>(tinkerAction.effects[3]).need, hunger.id);
	EXPECT_EQ(tinkerAction.duration, ActionDuration::OpenEnded);
}

TEST_F(BehaviorLoaderTest, AFileDeclaresTheLastPartOfItsIdAndSitsInANamedFolder) {
	write("config/behaviors/test/sample.xml", std::string("<behavior id=\"other\">") + kIdleSample + "</behavior>");
	EXPECT_EQ(onlyError(), "[behaviors] test/sample.xml: <behavior id=\"other\"> id must be the file's name, \"sample\", got \"other\"");

	std::filesystem::remove(root / "config/behaviors/test/sample.xml");
	write("config/behaviors/loose.xml", std::string("<behavior id=\"loose\">") + kIdleSample + "</behavior>");
	EXPECT_EQ(onlyError(), "[behaviors] loose.xml: a behavior file sits in a folder under behaviors/, the folder its id starts with");

	std::filesystem::remove(root / "config/behaviors/loose.xml");
	write("config/behaviors/test/sample.xml", "<behaviour id=\"sample\"/>");
	EXPECT_EQ(onlyError(), "[behaviors] test/sample.xml: the root element is not <behavior>");

	std::filesystem::remove_all(root / "config/behaviors/test");
	write("config/behaviors/hu-man/eating.xml", std::string("<behavior id=\"eating\">") + kIdleSample + "</behavior>");
	EXPECT_EQ(onlyError(), "[behaviors] hu-man/eating.xml: the folder name \"hu-man\" must be letters, digits, and underscores");

	std::filesystem::remove_all(root / "config/behaviors/hu-man");
	write("config/behaviors/human/eat ing.xml", std::string("<behavior id=\"eat ing\">") + kIdleSample + "</behavior>");
	EXPECT_EQ(onlyError(), "[behaviors] human/eat ing.xml: the file name \"eat ing\" must be letters, digits, and underscores");
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
	write("config/actions/more.xml", R"(<actions><action id="deposit" seconds="1" interruptable="false"><put/></action></actions>)");
	write(
		"config/work/w.xml",
		R"(<colonyWork><category id="k" label="K" order="1"/>
  <work id="w" goal="haul" category="k" rung="opportunistic_work"><source query="q"/><chain id="c"/></work>
  <chain id="c"><go to="destination"/></chain>
  <query id="q"><from pile="goal"/><weigh fact="distance" by="-1"/></query></colonyWork>)"
	);
	sample(std::string(kThirst).insert(std::string(kThirst).find("</need>"), R"(<satisfiedBy chain="drink_here"/>)") + R"(
  <chain id="drink_here"><do action="drink"/><do action="deposit"/></chain>
  <action id="drink" seconds="1.5" hands="free" interruptable="false"><restore need="thirst" amount="100"/></action>
  <idle chain="c"/>
  <chain id="c"><go query="q"/></chain>
  <query id="q"><from place="here"/><weigh fact="distance" by="-1"/></query>)");

	EXPECT_EQ(
		warnings(),
		(std::vector<std::string>{
			"[behaviors] test/sample.xml: <chain id=\"c\"> chain \"test/c\" hides the top-level chain \"c\" ([work] w.xml) inside test/",
			"[behaviors] test/sample.xml: <query id=\"q\"> query \"test/q\" hides the top-level query \"q\" ([work] w.xml) inside test/",
			"[behaviors] test/sample.xml: <action id=\"drink\"> action \"test/drink\" hides the top-level action \"drink\" ([actions] "
			"shared.xml) "
			"inside test/",
		})
	);
	const EntryLibrary library = load().library;
	ASSERT_TRUE(library.findChain("test/drink_here").valid());
	const ChainEntry& chain = library.chains[library.findChain("test/drink_here").index];
	ASSERT_NE(stepOf<DoStep>(chain, 0), nullptr);
	ASSERT_NE(stepOf<DoStep>(chain, 1), nullptr);
	EXPECT_EQ(stepOf<DoStep>(chain, 0)->action.handle, library.findAction("test/drink"));
	EXPECT_EQ(stepOf<DoStep>(chain, 1)->action.handle, library.findAction("deposit"));
}

TEST_F(BehaviorLoaderTest, AChainNothingStartsLoadsWithAWarning) {
	sample(std::string(kIdleSample) + R"(<chain id="lonely"><go query="spot"/></chain>)");
	EXPECT_EQ(
		warnings(),
		std::vector<std::string>{"[behaviors] test/sample.xml: <chain id=\"lonely\"> chain \"test/lonely\" is started by nothing: no need, "
								 "idle row, work entry, "
								 "order, or episode names it"}
	);
}

TEST_F(BehaviorLoaderTest, EachEntryMistakeIsOneErrorNamingTheEntryElementAndAttribute) {
	const std::string spot = R"(<query id="spot"><from place="here"/><weigh fact="distance" by="-1"/></query>)";
	const std::string lowMood =
		R"(<chain id="c"><go to="target"/></chain><query id="q" pick="weighted"><from kind="bed"/><exclude fact="distance" above="5"/></query>)";
	const std::vector<Case> cases = {
		{std::string(kIdleSample) + "<task/>", "<behavior id=\"sample\">: unknown element <task>"},
		{R"(<idle chain="go_there"/><chain id="go_there" loop="true"><go query="spot"/></chain>)" + spot,
		 "<chain id=\"go_there\">: unknown attribute 'loop'"},
		{R"(<idle chain="go_there"/><chain id="go_there"><go query="spott"/></chain>)" + spot,
		 "<chain id=\"go_there\"> step 1 <go> query \"spott\" names no query"},
		{R"(<idle chain="go_there"/><chain id="go_there" script="go.lua"><go query="spot"/></chain>)" + spot,
		 "<chain id=\"go_there\"> script must be left out; it is reserved for a Lua hook that runs nothing yet, got \"go.lua\""},
		{R"(<chain id="go "><go to="target"/></chain>)", "<chain> id must be letters, digits, and underscores, got \"go \""},
		// Steps.
		{R"(<chain id="c"><go query="q" to="target"/></chain>)",
		 "<chain id=\"c\"> step 1 <go> names both a query and a target; give exactly one of query and to"},
		{R"(<chain id="c"><go/></chain>)",
		 "<chain id=\"c\"> step 1 <go> names neither a query nor a target; give exactly one of query and to"},
		{R"(<chain id="c"><have item="Berry" capability="edible" fetch="c"/></chain>)",
		 "<chain id=\"c\"> step 1 <have> names both an item and a capability; give exactly one"},
		{R"(<chain id="c"><have fetch="c"/></chain>)",
		 "<chain id=\"c\"> step 1 <have> names neither an item nor a capability; give exactly one"},
		{R"(<chain id="c"/>)", "<chain id=\"c\"> has no step"},
		{R"(<chain id="c"><walk/></chain>)", "<chain id=\"c\">: unknown element <walk>"},
		{R"(<chain id="c"><go to="target"><ends on="target_gone"/><ends on="target_gone"/></go></chain>)",
		 "<chain id=\"c\"> step 1 <go> <ends> on \"target_gone\" is given twice"},
		{R"(<chain id="c"><do action="x" claims="use use"/></chain>)", "<chain id=\"c\"> step 1 <do> claims names \"use\" twice"},
		{R"(<chain id="c"><do action="x" claims="use grab"/></chain>)",
		 "<chain id=\"c\"> step 1 <do> claims \"grab\" is not a claim kind (use, source_quantity, slot, inbound, in_flight)"},
		// Needs.
		{R"(<need id="thirst" label="Thirst" seek="20" urgent="25" critical="10"><drains perGameMinute="0.1"/></need>)",
		 "<need id=\"thirst\"> urgent must be below seek (20), got \"25\""},
		{R"(<need id="thirst" label="Thirst" seek="50" urgent="25" critical="30"><drains perGameMinute="0.1"/></need>)",
		 "<need id=\"thirst\"> critical must be below urgent (25), got \"30\""},
		{R"(<need id="thirst" label="Thirst" seek="abc" urgent="25" critical="10"><drains perGameMinute="0.1"/></need>)",
		 "<need id=\"thirst\"> seek must be a number, got \"abc\""},
		{R"(<need id="thirst!" label="Thirst" seek="50" urgent="25" critical="10"><drains perGameMinute="0.1"/></need>)",
		 "<need> id must be letters, digits, and underscores, got \"thirst!\""},
		{R"(<need id="thirst" label="Thirst" seek="50" urgent="25" critical="10"><drains perGameMinute="0.1"/><setBy provider="thermal_comfort"/></need>)",
		 "<need id=\"thirst\"> has more than one of <drains> and <setBy>; a level moves in exactly one way"},
		{R"(<need id="thirst" label="Thirst" seek="50" urgent="25" critical="10"><setBy provider="weather"/></need>)",
		 "<need id=\"thirst\"> <setBy> provider must be one of thermal_comfort, got \"weather\""},
		{R"(<need id="thirst" label="Thirst" seek="50" urgent="25" critical="10"><drains perGameMinute="0.1"/><satisfiedBy when="x"/></need>)",
		 "<need id=\"thirst\"> row 1 <satisfiedBy> names neither a chain nor a work entry; give exactly one"},
		{R"(<need id="thirst" label="Thirst" seek="50" urgent="25" critical="10"><drains perGameMinute="0.1"/><satisfiedBy chain="c" work="w"/></need>)",
		 "<need id=\"thirst\"> row 1 <satisfiedBy> names both a chain and a work entry; give exactly one"},
		{std::string(kThirst) + kThirst, "<behavior id=\"sample\"> <need id=\"thirst\"> is given twice in one behavior"},
		{R"(<action id="drink" seconds="1" interruptable="false"><restore need="thrist" amount="1"/></action>)",
		 "<action id=\"drink\"> effect 1 <restore> need \"thrist\" names no need of any behavior or creature"},
		// Queries.
		{R"(<chain id="c"><go query="q"/></chain><query id="q"><from place="here"/></query>)",
		 "<query id=\"q\"> neither weighs distance by a negative number nor excludes it above a bound, so nothing ends its search"},
		{R"(<query id="q" pick="weighted"><from place="here"/><weigh fact="distance" by="-1"/></query>)",
		 "<query id=\"q\"> picks weighted but excludes no distance above a bound, so its draw has no bound"},
		{R"(<query id="q"><weigh fact="distance" by="-1"/></query>)", "<query id=\"q\"> has no <from> row, so it has no candidates"},
		{R"(<query id="q"><from place="here"/><weigh fact="nearness" by="-1"/></query>)",
		 "<query id=\"q\"> row 2 <weigh> fact \"nearness\" names no fact the sim provides"},
		{R"(<query id="q"><from place="here"/><weigh fact="distance" by="-1"/><weigh fact="contains" by="1"/></query>)",
		 "<query id=\"q\"> row 3 <weigh> fact \"contains\" takes one argument, as contains(tag)"},
		{R"(<query id="q"><from capability="drinkable" place="here"/><weigh fact="distance" by="-1"/></query>)",
		 "<query id=\"q\"> row 1 <from> names more than one source (capability, place); give exactly one of capability, kind, item, "
		 "yields, pile, box, place, goal"},
		{R"(<query id="q"><from goal="destination"/><weigh fact="distance" by="-1"/></query>)",
		 "<query id=\"q\"> row 1 <from> goal \"destination\" reads the goal a work option serves, which only a query in work/ has"},
		{R"(<query id="q"><from item="Wod"/><weigh fact="distance" by="-1"/></query>)",
		 "<query id=\"q\"> row 1 <from> item \"Wod\" names no definition"},
		{R"(<query id="q" pick="weighted"><from place="around" radius="8" count="1" minRadius="9"/><exclude fact="distance" above="8"/></query>)",
		 "<query id=\"q\"> row 1 <from> minRadius must be below radius (8), got \"9\""},
		{R"(<query id="q"><from place="here"/><exclude fact="distance" above="1" below="2"/><weigh fact="distance" by="-1"/></query>)",
		 "<query id=\"q\"> row 2 <exclude> gives both above and below; a row draws one line"},
		// Actions.
		{std::string(kThirst) + R"(<action id="drink" interruptable="false"><put/></action>)",
		 "<action id=\"drink\"> has no duration; give seconds, workFrom with skill, or an open-ended effect"},
		{std::string(kThirst) +
			 R"(<action id="enjoy" seconds="5" interruptable="true" pays="along"><restore need="thirst" amount="2" seconds="60" upTo="90"/></action>)",
		 "<action id=\"enjoy\"> seconds must be left out; an open-ended action (a <work> effect or an upTo bound) runs until its bound, "
		 "got \"5\""},
		{R"(<action id="drink" seconds="1"><put/></action>)", "<action id=\"drink\"> interruptable is missing"},
		{R"(<action id="drink" seconds="1" interruptable="false" curve="2"><put/></action>)",
		 "<action id=\"drink\"> curve must be left out; it shapes an action that pays along, got \"2\""},
		{R"(<action id="drink" seconds="1" interruptable="false"><put/><heal/></action>)", "<action id=\"drink\">: unknown element <heal>"},
		{R"(<action id="drink" seconds="1" interruptable="false"><heal/></action>)", "<action id=\"drink\">: unknown element <heal>"},
		{std::string(kThirst) +
			 R"(<action id="drink" seconds="1" interruptable="false"><restore need="thirst" amount="1" from="nutrition" scale="1"/></action>)",
		 "<action id=\"drink\"> effect 1 <restore> gives both amount and from; give exactly one"},
		// Low-mood episodes.
		{R"(<lowMood rampStart="25" rampFull="15" base="4"><effect chain="c"/><relief amount="small"/></lowMood>)" + lowMood,
		 "<behavior id=\"sample\"> <lowMood> has no <ends> row; it takes an <ends> row"},
		{R"(<lowMood rampStart="25" rampFull="15" base="4"><effect chain="c"/><ends after="task"/><ends after="task"/><relief amount="small"/></lowMood>)" +
			 lowMood,
		 "<behavior id=\"sample\"> <lowMood> row 2 <ends> after \"task\" is given twice"},
		{R"(<lowMood rampStart="25" rampFull="30" base="4"><effect chain="c"/><ends after="task"/><relief amount="small"/></lowMood>)" +
			 lowMood,
		 "<behavior id=\"sample\"> <lowMood> rampFull must be below rampStart (25), got \"30\""},
		{R"(<lowMood rampStart="25" rampFull="15" base="4"><target query="q"/><target query="q"/><effect chain="c"/><ends after="task"/><relief amount="small"/></lowMood>)" +
			 lowMood,
		 "<behavior id=\"sample\"> <lowMood> has 2 <target> rows; it takes at most one"},
		{R"(<lowMood rampStart="25" rampFull="15" base="4"><weight when="x" per="y" multiply="2"/><effect chain="c"/><ends after="task"/><relief amount="small"/></lowMood>)" +
			 lowMood,
		 "<behavior id=\"sample\"> <lowMood> row 1 <weight> gives both when and per; give exactly one"},
		{R"(<lowMood rampStart="25" rampFull="15" base="4"><effect chain="c"/><ends after="task"/><relief amount="huge"/></lowMood>)" +
			 lowMood,
		 "<behavior id=\"sample\"> <lowMood> <relief> amount must be one of none, small, medium, large, got \"huge\""},
		{R"(<lowMood rampStart="25" rampFull="15" base="4"><effect chain="c"/><ends after="task"/><relief amount="small"/></lowMood><lowMood rampStart="25" rampFull="15" base="4"><effect chain="c"/><ends after="task"/><relief amount="small"/></lowMood>)" +
			 lowMood,
		 "<behavior id=\"sample\"> has more than one <lowMood>; a low-mood behavior holds one"},
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.written);
		sample(entry.written);
		EXPECT_EQ(onlyError(), "[behaviors] test/sample.xml: " + entry.expected);
	}
}

// A query no go step, work source, or low-mood target names can only be counted (count_of), which ranks
// nothing and walks nothing nearest first, so it need not bound distance; named by a low-mood target, it must.
TEST_F(BehaviorLoaderTest, OnlyARankedQueryMustBoundDistance) {
	const std::string counted = R"(<query id="liked_near"><from capability="drinkable"/><exclude fact="held"/></query>)";
	sample(counted);
	const EntryLoad loaded = load();
	EXPECT_TRUE(loaded.errors.empty()) << ::testing::PrintToString(loaded.errors);
	EXPECT_TRUE(loaded.library.findQuery("test/liked_near").valid());

	sample(
		counted +
		R"(<lowMood rampStart="25" rampFull="15" base="4"><target query="liked_near"/><effect chain="c"/><ends after="task"/><relief amount="small"/></lowMood><chain id="c"><go to="target"/></chain>)"
	);
	EXPECT_EQ(
		onlyError(),
		"[behaviors] test/sample.xml: <query id=\"liked_near\"> neither weighs distance by a negative number nor excludes it "
		"above a bound, so nothing ends its search"
	);

	// Counting random points would draw from the sim's generator and count a different set each time.
	sample(R"(<query id="spots_near"><from place="around" radius="8" count="4"/><exclude fact="distance" above="8"/></query>)");
	EXPECT_EQ(
		onlyError(),
		"[behaviors] test/sample.xml: <query id=\"spots_near\"> row 1 <from> place \"around\" draws random points, which a query only "
		"counted (count_of) cannot have"
	);
}

TEST_F(BehaviorLoaderTest, ChainRulesNameTheChainAndTheStep) {
	write("config/actions/shared.xml", kSharedActions);
	const std::string base = std::string(kThirst).insert(std::string(kThirst).find("</need>"), R"(<satisfiedBy chain="a"/>)") +
							 R"(<action id="drink" seconds="1" interruptable="false"><restore need="thirst" amount="1"/></action>)";
	const std::vector<Case> cases = {
		{R"(<chain id="a"><do action="pickup" claims="use source_quantity slot"/><do action="deposit" claims="inbound in_flight"/><do action="pickup" claims="use"/></chain>)",
		 "<chain id=\"a\"> names 5 claims with the chains it runs; a task holds four"},
		{R"(<chain id="a"><do action="deposit" claims="inbound in_flight"/><do action="deposit" claims="inbound in_flight"/></chain>)",
		 "<chain id=\"a\"> names 4 inbound and in_flight claims with the chains it runs; a suspended record holds two"},
		{R"(<chain id="a"><run chain="b"/></chain><chain id="b"><run chain="a"/></chain>)",
		 "<chain id=\"a\"> runs itself: \"test/a\" -> \"test/b\" -> \"test/a\""},
		{R"(<chain id="a"><run chain="b"/></chain><chain id="b"><run chain="b"/></chain>)",
		 "<chain id=\"b\"> runs itself: \"test/b\" -> \"test/b\""},
		{R"(<chain id="a"><do action="drink" amount="meal"/></chain>)",
		 "<chain id=\"a\"> step 1 <do> amount is for a step whose action takes or puts; \"drink\" does neither"},
		{R"(<chain id="a"><go query="food"/><do action="take"/></chain>
  <query id="food"><from yields="edible"/><from pile="edible"/><from box="edible"/><from capability="drinkable"/><weigh fact="distance" by="-1"/></query>)",
		 "<chain id=\"a\"> step 2 <do> action group \"take\" has no member for drinkable, which query \"food\" can return"},
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.written);
		sample(base + entry.written);
		EXPECT_EQ(onlyError(), "[behaviors] test/sample.xml: " + entry.expected);
	}

	sample(
		base +
		R"(<chain id="a"><do action="pickup" claims="use source_quantity slot"/><do action="deposit" claims="inbound in_flight"/></chain>)"
	);
	EXPECT_TRUE(load().errors.empty()) << "use and source_quantity on one step hold one claim, so four in all";
}

// --- Actions ---------------------------------------------------------------------------------------------

TEST_F(ActionLoaderTest, TheSharedActionsLoadAndTheGroupResolvesItsMembers) {
	write("config/actions/shared.xml", kSharedActions);
	const EntryLoad		loaded = clean();
	const EntryLibrary& library = loaded.library;
	EXPECT_EQ(library.actions.size(), 10U);
	ASSERT_TRUE(library.findAction("take").valid());
	const ActionEntry& take = library.actions[library.findAction("take").index];
	ASSERT_TRUE(take.group);
	ASSERT_EQ(take.members.size(), 3U);
	EXPECT_EQ(take.members[2].handle, library.findAction("withdraw"));
	ASSERT_TRUE(library.findAction("clear").valid());
	const ActionEntry& clear = library.actions[library.findAction("clear").index];
	ASSERT_FALSE(clear.effects.empty());
	ASSERT_TRUE(std::holds_alternative<TakeEffect>(clear.effects.front()));
	EXPECT_TRUE(std::get<TakeEffect>(clear.effects.front()).clears);
	ASSERT_TRUE(library.findAction("deconstruct").valid());
	const ActionEntry& deconstruct = library.actions[library.findAction("deconstruct").index];
	ASSERT_FALSE(deconstruct.effects.empty());
	ASSERT_TRUE(std::holds_alternative<WorkEffect>(deconstruct.effects.front()));
	EXPECT_EQ(std::get<WorkEffect>(deconstruct.effects.front()).direction, WorkDirection::Down);
}

TEST_F(ActionLoaderTest, ACraftTakesTheJobsWorkAmountAtTheFixedRate) {
	write("config/actions/craft.xml", R"(<actions>
  <action id="craft" workFrom="job" hands="free" interruptable="false"><craft/></action>
</actions>)");
	const EntryLoad loaded = clean();
	ASSERT_TRUE(loaded.library.findAction("craft").valid());
	const ActionEntry& craft = loaded.library.actions[loaded.library.findAction("craft").index];
	EXPECT_EQ(craft.duration, ActionDuration::FromJob);
	EXPECT_TRUE(craft.skill.empty());

	write("config/actions/craft.xml", R"(<actions>
  <action id="craft" workFrom="job" skill="Crafting" hands="free" interruptable="false"><craft/></action>
</actions>)");
	EXPECT_EQ(
		onlyError(),
		"[actions] craft.xml: <action id=\"craft\"> skill must be left out; a job's work amount is worked at a fixed rate of 100 work "
		"ticks a second, got \"Crafting\""
	);

	write("config/actions/craft.xml", R"(<actions>
  <action id="craft" workFrom="jobs" skill="Crafting" hands="free" interruptable="false"><craft/></action>
</actions>)");
	EXPECT_EQ(onlyError(), "[actions] craft.xml: <action id=\"craft\"> workFrom must be one of durability, job, got \"jobs\"");
}

TEST_F(ActionLoaderTest, AnEffectMayNameANeedTheSimTracksBeforeAnyBehaviorBringsIt) {
	write("config/actions/drink.xml", R"(<actions>
  <action id="drink" seconds="1.5" hands="free" interruptable="false"><restore need="thirst" amount="100"/></action>
</actions>)");
	EXPECT_EQ(
		onlyError(),
		"[actions] drink.xml: <action id=\"drink\"> effect 1 <restore> need \"thirst\" names no need of any behavior or creature"
	);
	trackedNeeds = {"thirst"};
	const EntryLoad loaded = clean();
	ASSERT_TRUE(loaded.library.findNeed("thirst").has_value());
	ASSERT_TRUE(loaded.library.findAction("drink").valid());
	const ActionEntry& drink = loaded.library.actions[loaded.library.findAction("drink").index];
	ASSERT_EQ(drink.effects.size(), 1U);
	EXPECT_EQ(std::get<RestoreEffect>(drink.effects.front()).need, *loaded.library.findNeed("thirst"));
}

TEST_F(ActionLoaderTest, GroupAndFolderMistakesNameTheFileAndTheEntry) {
	const std::string		pickup = R"(<action id="pickup" on="loose_item" seconds="0.5" interruptable="false"><take/></action>)";
	const std::vector<Case> cases = {
		{pickup + R"(<actionGroup id="take">pickup</actionGroup><actionGroup id="outer">take</actionGroup>)",
		 "<actionGroup id=\"outer\"> member \"take\" is an action group; a group's members are actions"},
		{pickup +
			 R"(<action id="deposit" seconds="1" interruptable="false"><put/></action><actionGroup id="take">pickup deposit</actionGroup>)",
		 "<actionGroup id=\"take\"> member \"deposit\" has no on, so no target can choose it"},
		{pickup +
			 R"(<action id="lift" on="loose_item" seconds="1" interruptable="false"><take/></action><actionGroup id="take">pickup lift</actionGroup>)",
		 "<actionGroup id=\"take\"> members \"pickup\" and \"lift\" both work on loose_item"},
		{R"(<actionGroup id="take"/>)", "<actionGroup id=\"take\"> names no member action"},
		{pickup + R"(<actionGroup id="pickup">pickup</actionGroup>)",
		 "<actionGroup id=\"pickup\"> action \"pickup\" is defined twice, first in [actions] shared.xml"},
		{R"(<action id="lift" on="tree" seconds="1" interruptable="false"><take/></action>)",
		 "<action id=\"lift\"> on must be a capability (edible, drinkable, sleepable, toilet, waste, carryable, harvestable, craftable, "
		 "storage, "
		 "washable), loose_item, or packaged, got \"tree\""},
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.written);
		write("config/actions/shared.xml", "<actions>" + entry.written + "</actions>");
		EXPECT_EQ(onlyError(), "[actions] shared.xml: " + entry.expected);
	}

	write("config/actions/shared.xml", "<actionList/>");
	EXPECT_EQ(onlyError(), "[actions] shared.xml: the root element is not <actions>");

	write("config/actions/shared.xml", "<actions/>");
	write("config/actions/extra/more.xml", "<actions/>");
	EXPECT_EQ(onlyError(), "[actions] extra: a subfolder, which actions/ may not have; its entries take top-level ids");
	std::filesystem::remove_all(root / "config/actions/extra");
	std::filesystem::create_directories(root / "config/actions/empty");
	EXPECT_EQ(onlyError(), "[actions] empty: a subfolder, which actions/ may not have; its entries take top-level ids")
		<< "an empty subfolder is refused too";
	std::filesystem::remove_all(root / "config/actions/empty");

	std::filesystem::remove_all(root / "config/actions");
	EXPECT_EQ(onlyError(), "[actions] " + (root / "config" / "actions").generic_string() + ": not a folder");
}

// --- Work ------------------------------------------------------------------------------------------------

TEST_F(WorkLoaderTest, TheSpecsHaulAndACareEntryLoadWithTheirRungsAndSources) {
	write("config/actions/shared.xml", kSharedActions);
	write("config/work/hauling.xml", R"(<colonyWork>
  <category id="hauling" label="Hauling" order="6"/>
  <category id="emergency" label="Emergency" order="1" canDisable="false"/>
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
  <work id="build" goal="build" category="emergency" rung="committed_work">
    <source query="builder_spot"/>
    <chain id="build_at_spot"/>
  </work>
  <work id="milk" goal="care" need="milking" category="emergency" rung="opportunistic_work">
    <chain id="milk_a_cow"/>
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
  <chain id="build_at_spot"><go to="source"/><do action="build" on="destination" claims="slot"/></chain>
  <chain id="milk_a_cow"><go to="destination"/><do action="milk" claims="use"/></chain>
  <query id="pile_for_goal"><from pile="goal"/><weigh fact="distance" by="-1"/></query>
  <query id="box_to_pull_from">
    <from box="goal"/>
    <exclude fact="below_destination" below="1"/>
    <weigh fact="distance" by="-1"/>
  </query>
  <query id="builder_spot"><from goal="spots"/><weigh fact="distance" by="-1"/></query>
  <action id="milk" seconds="5" hands="free" interruptable="false">
    <restore who="target" need="milking" amount="100"/>
    <spawn def="Milk" into="hands"/>
  </action>
</colonyWork>)");
	write("config/behaviors/test/being_milked.xml", R"(<behavior id="being_milked">
  <need id="milking" label="Milking" seek="40" urgent="20" critical="5"><drains perGameMinute="0.02"/><satisfiedBy work="milk"/></need>
</behavior>)");

	const EntryLoad loaded = clean();
	EXPECT_TRUE(loaded.warnings.empty()) << ::testing::PrintToString(loaded.warnings);
	const EntryLibrary& library = loaded.library;
	ASSERT_TRUE(library.findWork("haul_pile").valid());
	const WorkEntry& pile = library.work[library.findWork("haul_pile").index];
	ASSERT_LT(pile.rung, kRungs.size());
	EXPECT_EQ(kRungs[pile.rung], "opportunistic_work");
	EXPECT_TRUE(pile.gathers);
	ASSERT_TRUE(pile.source.has_value());
	EXPECT_EQ(pile.source->handle, library.findQuery("pile_for_goal"));
	EXPECT_EQ(pile.chain.handle, library.findChain("haul_from_pile"));
	EXPECT_EQ(pile.category.handle, library.findCategory("hauling"));
	ASSERT_TRUE(library.findWork("carry_in").valid());
	const WorkEntry& carry = library.work[library.findWork("carry_in").index];
	EXPECT_EQ(carry.goal, WorkGoal::Root);
	EXPECT_TRUE(carry.sourceCarried);
	EXPECT_EQ(carry.chain.handle, library.findChain("carry_in")) << "a work entry and its chain may share an id";
	ASSERT_TRUE(library.findChain("haul_from_pile").valid());
	const HaveStep* have = stepOf<HaveStep>(library.chains[library.findChain("haul_from_pile").index], 0);
	ASSERT_NE(have, nullptr);
	ASSERT_TRUE(have->item.has_value());
	EXPECT_EQ(have->item->kind, ItemArgument::Kind::Goal);
	ASSERT_TRUE(library.findQuery("builder_spot").valid());
	const QueryEntry& spots = library.queries[library.findQuery("builder_spot").index];
	ASSERT_EQ(spots.from.size(), 1U);
	EXPECT_EQ(spots.from.front().source, CandidateSource::GoalSpots);
	ASSERT_TRUE(library.findCategory("hauling").valid() && library.findCategory("emergency").valid());
	EXPECT_EQ(library.categories[library.findCategory("hauling").index].order, 6);
	EXPECT_TRUE(library.categories[library.findCategory("hauling").index].canDisable);
	EXPECT_FALSE(library.categories[library.findCategory("emergency").index].canDisable);
	ASSERT_TRUE(library.findWork("milk").valid());
	const WorkEntry& milk = library.work[library.findWork("milk").index];
	ASSERT_TRUE(milk.need.has_value());
	EXPECT_EQ(library.needIds[milk.need->index], "milking");
	ASSERT_TRUE(library.findBehavior("test/being_milked").valid());
	const BehaviorEntry& milked = library.behaviors[library.findBehavior("test/being_milked").index];
	ASSERT_EQ(milked.needs.size(), 1U);
	const NeedEntry& milking = library.needs[milked.needs.front().index];
	ASSERT_EQ(milking.satisfiedBy.size(), 1U);
	ASSERT_TRUE(milking.satisfiedBy.front().work.has_value());
	EXPECT_EQ(milking.satisfiedBy.front().work->handle, library.findWork("milk"));
}

TEST_F(WorkLoaderTest, WorkMistakesNameTheEntryAndTheAttribute) {
	const std::vector<Case> cases = {
		{R"(<work id="milk" goal="care" category="farming" rung="opportunistic_work"><chain id="c"/></work>)",
		 "<work id=\"milk\"> need is missing"},
		{R"(<work id="w" goal="haul" need="hunger" category="farming" rung="opportunistic_work"><chain id="c"/></work>)",
		 "<work id=\"w\"> need must be left out; it names the need of a care goal, got \"hunger\""},
		{R"(<work id="w" goal="mine" need="hunger" category="farming" rung="committed_work"><chain id="c"/></work>)",
		 "<work id=\"w\"> goal must be one of haul, harvest, clear, craft, build, deconstruct, place_packaged, care, root, got \"mine\""},
		{R"(<work id="w" goal="haul" category="farming" rung="survival"><chain id="c"/></work>)",
		 "<work id=\"w\"> rung must be a rung other than survival, which is reserved, got \"survival\""},
		{R"(<work id="w" goal="haul" category="farming" rung="work"><chain id="c"/></work>)",
		 "<work id=\"w\"> rung must be a rung of the ladder (survival, critical_need, low_mood, player_order, urgent_need, "
		 "own_unfinished_work, "
		 "committed_work, ordinary_need, opportunistic_work, idle), got \"work\""},
		{R"(<work id="w" goal="haul" category="farm" rung="committed_work"><chain id="c"/></work>)",
		 "<work id=\"w\"> category \"farm\" names no category"},
		{R"(<work id="w" goal="haul" category="farming" rung="committed_work"><chain id="c"/><chain id="c"/></work>)",
		 "<work id=\"w\"> has 2 <chain> rows; it names exactly one"},
		{R"(<work id="w" goal="root" source="carried" category="farming" rung="committed_work"><source query="q"/><chain id="c"/></work>)",
		 "<work id=\"w\"> gives both source=\"carried\" and a <source> row; give one"},
		{R"(<category id="hauling" label="Hauling" order="first"/>)",
		 "<category id=\"hauling\"> order must be a whole number from 0 to 2147483647, got \"first\""},
		{R"(<work id="w" goal="haul" category="farming" rung="opportunistic_work"><source query="q"/><chain id="c"/></work><query id="q"><from pile="goal"/></query>)",
		 "<query id=\"q\"> neither weighs distance by a negative number nor excludes it above a bound, so nothing ends its search"},
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.written);
		write(
			"config/work/w.xml",
			"<colonyWork><category id=\"farming\" label=\"Farming\" order=\"3\"/><chain id=\"c\"><go to=\"destination\"/></chain>" +
				entry.written + "</colonyWork>"
		);
		EXPECT_EQ(onlyError(), "[work] w.xml: " + entry.expected);
	}
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
	const EntryLoad		loaded = clean();
	const EntryLibrary& library = loaded.library;
	ASSERT_TRUE(library.findOrder("relieve_here").valid());
	const OrderEntry& relieve = library.orders[library.findOrder("relieve_here").index];
	EXPECT_EQ(relieve.on.kind, OrderTarget::Kind::Ground);
	EXPECT_EQ(relieve.label, "Relieve self");
	ASSERT_TRUE(relieve.chain.handle.valid());
	const DoStep* urinate = stepOf<DoStep>(library.chains[relieve.chain.handle.index], 1);
	ASSERT_NE(urinate, nullptr);
	EXPECT_EQ(urinate->action.handle, library.findAction("test/urinate"));
	ASSERT_TRUE(library.findOrder("harvest").valid());
	EXPECT_EQ(library.orders[library.findOrder("harvest").index].on.capability, CapabilityType::Harvestable);
}

TEST_F(OrderLoaderTest, OrderMistakesNameTheOrder) {
	const std::vector<Case> cases = {
		{R"(<order id="harvest" label="Harvest" on="tree" chain="harvest_target"/>)",
		 "<order id=\"harvest\"> on must be a capability (edible, drinkable, sleepable, toilet, waste, carryable, harvestable, craftable, "
		 "storage, "
		 "washable) or ground, got \"tree\""},
		{R"(<order id="harvest" label="Harvest" on="harvestable" chain="harvest_targt"/>)",
		 "<order id=\"harvest\"> chain \"harvest_targt\" names no chain"},
		{R"(<order id="harvest" on="harvestable" chain="harvest_target"/>)", "<order id=\"harvest\"> label is missing"},
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.written);
		write("config/orders/orders.xml", "<orders><chain id=\"harvest_target\"><go to=\"target\"/></chain>" + entry.written + "</orders>");
		EXPECT_EQ(onlyError(), "[orders] orders.xml: " + entry.expected);
	}
}

// --- Facts -----------------------------------------------------------------------------------------------

TEST_F(FactLoaderTest, EachTunedFactReadsItsValue) {
	const EntryLoad loaded = clean();
	const auto		tuning = [&loaded](std::string_view fact) {
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

	const std::vector<Case> cases = {
		{R"(<fact id="distance" within="3"/>)", "<fact id=\"distance\"> reads no tuning value, so it has no row here"},
		{R"(<fact id="wetness" within="1"/>)", "<fact> id must be a fact the sim provides, got \"wetness\""},
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.written);
		write("config/facts/facts.xml", std::string(kFactTuning).insert(std::string(kFactTuning).find("</facts>"), entry.written));
		EXPECT_EQ(onlyError(), "[facts] facts.xml: " + entry.expected);
	}
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

	void cow(const std::string& block, const std::string& variants = "", const std::string& defName = "Cow") const {
		write(
			"creatures/Cow/Cow.xml",
			"<AssetDefinitions><AssetDef><defName>" + defName + "</defName><assetType>simple</assetType>" + block + "</AssetDef>" +
				variants + "</AssetDefinitions>"
		);
	}

	void browsing() const {
		write("config/behaviors/common/browsing.xml", std::string(kGrazing).replace(std::string(kGrazing).find("eating"), 6, "browsing"));
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
	const EntryLoad		 loaded = clean({"Cow"});
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
	ASSERT_FALSE(hunger.satisfiedBy.empty());
	ASSERT_TRUE(hunger.satisfiedBy.front().chain.has_value());
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
	const EntryLoad		 loaded = clean({"Cow"});
	const CreatureEntry* creature = loaded.library.findCreature("Cow");
	ASSERT_NE(creature, nullptr);
	ASSERT_EQ(creature->behaviors.size(), 3U);
	EXPECT_EQ(creature->behaviors[1], loaded.library.findBehavior("common/drinking_at_water"));
	EXPECT_EQ(creature->behaviors[2], loaded.library.findBehavior("Cow/being_milked"));
	ASSERT_EQ(creature->needs.size(), 3U);
	EXPECT_EQ(creature->needs[2].moodCost, 5.0F);
}

TEST_F(CreatureLoaderTest, ACreaturesOwnNeedIsANeedEveryEffectMayName) {
	cow(
		R"(<creature mood="false"><need id="salt" label="Salt" seek="40" urgent="20" critical="5"><drains perGameMinute="0.01"/></need></creature>)"
	);
	write("config/behaviors/herbivore/licking.xml", R"(<behavior id="licking">
  <action id="lick" seconds="2" hands="any" interruptable="false"><restore need="salt" amount="50"/></action>
</behavior>)");
	const EntryLoad		 loaded = clean({"Cow"});
	const CreatureEntry* creature = loaded.library.findCreature("Cow");
	ASSERT_NE(creature, nullptr);
	ASSERT_EQ(creature->needs.size(), 2U);
	EXPECT_EQ(loaded.library.needIds[creature->needs[1].id.index], "salt");
	EXPECT_FALSE(creature->needs[1].behavior.valid()) << "the creature's own need has no behavior";
}

TEST_F(CreatureLoaderTest, SettlingTwoBehaviorsThatBringOneNeedTakesTheNamedOne) {
	browsing();
	cow(
		R"(<creature mood="false"><behavior id="herbivore/eating"/><behavior id="common/browsing"/><need id="hunger" from="common/browsing"/></creature>)"
	);
	const EntryLoad loaded = clean({"Cow"});
	ASSERT_NE(loaded.library.findCreature("Cow"), nullptr);
	ASSERT_FALSE(loaded.library.findCreature("Cow")->needs.empty());
	EXPECT_EQ(loaded.library.findCreature("Cow")->needs.front().behavior, loaded.library.findBehavior("common/browsing"));
}

TEST_F(CreatureLoaderTest, EachCreatureMistakeIsOneError) {
	const std::string		twoGrazers = R"(<behavior id="herbivore/eating"/><behavior id="common/browsing"/>)";
	const std::vector<Case> cases = {
		{R"(<creature mood="false"><behavior id="herbivore/eatin"/></creature>)",
		 "[creatures] Cow/Cow.xml: <creature> row 1 <behavior> id \"herbivore/eatin\" names no behavior"},
		{R"(<creature/>)", "[creatures] Cow/Cow.xml: <creature> mood is missing"},
		{R"(<creature mood="false" script="cow.lua"/>)",
		 "[creatures] Cow/Cow.xml: <creature> script must be left out; it is reserved for a Lua hook that runs nothing yet, got \"cow.lua\""},
		{"<creature mood=\"false\">" + twoGrazers + "</creature>",
		 "[creatures] Cow/Cow.xml: <creature> need \"hunger\" comes from more than one included behavior (\"herbivore/eating\", "
		 "\"common/browsing\"); settle it with <need id=\"hunger\" from=\"...\"/>"},
		{R"(<creature mood="false"><behavior id="herbivore/eating"/><need id="hunger" from="common/drinking_at_water"/></creature>)",
		 "[creatures] Cow/Cow.xml: <need id=\"hunger\"> from \"common/drinking_at_water\" names no included behavior that brings the need"},
		{R"(<creature mood="false"><behavior id="herbivore/eating"/><need id="thirst" from="herbivore/eating"/></creature>)",
		 "[creatures] Cow/Cow.xml: <need id=\"thirst\"> from \"herbivore/eating\" settles a need no included behavior brings"},
		{R"(<creature mood="false"><behavior id="herbivore/eating"/><need id="hunger" urgent="60"/></creature>)",
		 "[creatures] Cow/Cow.xml: <creature> need \"hunger\" urgent must be below seek (50), got \"60\""},
		{R"(<creature mood="false"><behavior id="herbivore/eating"/><need id="hunger" critical="30"/></creature>)",
		 "[creatures] Cow/Cow.xml: <creature> need \"hunger\" critical must be below urgent (25), got \"30\""},
		{R"(<creature mood="false"><behavior id="herbivore/eating"/><need id="hunger"><satisfiedBy chain="herbivore/graze"/></need></creature>)",
		 "[creatures] Cow/Cow.xml: <need id=\"hunger\"> has <satisfiedBy> rows; what meets an included need comes from its behavior"},
		{R"(<creature mood="false"><need id="calving" label="Calving"/></creature>)",
		 "[creatures] Cow/Cow.xml: <creature> need \"calving\" is no included behavior's need, so the creature's rows give all of it, and "
		 "they give "
		 "no seek, urgent, critical, <drains> or <setBy>"},
		{R"(<creature mood="true"><behavior id="herbivore/eating"/><behavior id="common/drinking_at_water"/><moodCost need="hunger" cost="25"/><moodCost need="milking" cost="5"/></creature>)",
		 "[creatures] Cow/Cow.xml: <creature> need \"thirst\" has no <moodCost>; a creature with mood gives every need a cost"},
		{R"(<creature mood="false"><behavior id="herbivore/eating"/><moodCost need="hunger" cost="25"/></creature>)",
		 "[creatures] Cow/Cow.xml: <moodCost need=\"hunger\"> gives a mood cost, and the creature has no mood (mood=\"false\")"},
		{R"(<creature mood="true"><behavior id="herbivore/eating"/><moodCost need="hunger" cost="25"/><moodCost need="milking" cost="5"/><moodCost need="hygiene" cost="5"/></creature>)",
		 "[creatures] Cow/Cow.xml: <moodCost need=\"hygiene\"> names a need the creature does not have"},
		{R"(<creature mood="true"><behavior id="herbivore/eating"/><moodCost need="hunger" cost="25"/><moodCost need="hunger" cost="20"/><moodCost need="milking" cost="5"/></creature>)",
		 "[creatures] Cow/Cow.xml: <moodCost need=\"hunger\"> is also given in [creatures] Cow/Cow.xml"},
		// One fault, one error: what a failed include or an unsettled need would have settled says nothing more.
		{R"(<creature mood="true"><behavior id="herbivore/eatin"/><need id="hunger"><drains perGameMinute="0.05"/></need><moodCost need="hunger" cost="25"/><moodCost need="milking" cost="5"/></creature>)",
		 "[creatures] Cow/Cow.xml: <creature> row 1 <behavior> id \"herbivore/eatin\" names no behavior"},
		{"<creature mood=\"true\">" + twoGrazers + R"(<moodCost need="hunger" cost="25"/><moodCost need="milking" cost="5"/></creature>)",
		 "[creatures] Cow/Cow.xml: <creature> need \"hunger\" comes from more than one included behavior (\"herbivore/eating\", "
		 "\"common/browsing\"); settle it with <need id=\"hunger\" from=\"...\"/>"},
	};
	browsing();
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.written);
		cow(entry.written);
		EXPECT_EQ(cowError(), entry.expected);
	}

	// A failed include hides only what it could have brought: a mood cost naming no need of any behavior is
	// still its own fault.
	cow(
		R"(<creature mood="true"><behavior id="herbivore/eatin"/><moodCost need="thrist" cost="25"/><moodCost need="milking" cost="5"/></creature>)"
	);
	EXPECT_EQ(
		load({"Cow"}).errors,
		(std::vector<std::string>{
			"[creatures] Cow/Cow.xml: <creature> row 1 <behavior> id \"herbivore/eatin\" names no behavior",
			"[creatures] Cow/Cow.xml: <moodCost need=\"thrist\"> names a need the creature does not have",
		})
	);
}

TEST_F(CreatureLoaderTest, ACreaturesFolderAndOwnFileNamesAreIdParts) {
	std::filesystem::rename(root / "creatures/Cow/being_milked.xml", root / "creatures/Cow/being milked.xml");
	cow(R"(<creature mood="false"/>)");
	EXPECT_EQ(cowError(), "[creatures] Cow/being milked.xml: the file name \"being milked\" must be letters, digits, and underscores");

	write(
		"creatures/Cow-2/Cow-2.xml",
		R"(<AssetDefinitions><AssetDef><defName>Cow-2</defName><creature mood="false"/></AssetDef></AssetDefinitions>)"
	);
	std::filesystem::remove(root / "creatures/Cow/being milked.xml");
	EXPECT_EQ(onlyError({"Cow-2"}), "[creatures] Cow-2/Cow-2.xml: the folder name \"Cow-2\" must be letters, digits, and underscores");
}

TEST_F(CreatureLoaderTest, CreatureFilesAndTheBlocksPlaceAreChecked) {
	cow(R"(<creature mood="false"><behavior id="herbivore/eating"/></creature>)");
	write("creatures/Cow/herd.xml", R"(<creature><behavior id="herbivore/eating"/></creature>)");
	EXPECT_EQ(
		cowError(),
		"[creatures] Cow/herd.xml: <creature> row 1 <behavior> id \"herbivore/eating\" includes a behavior already included at [creatures] "
		"Cow/Cow.xml: <creature> row 1 <behavior>"
	);

	cow(R"(<creature mood="false"><behavior id="herbivore/eating"/><need id="hunger" from="herbivore/eating"/></creature>)");
	write("creatures/Cow/herd.xml", R"(<creature><need id="hunger" from="herbivore/eating"/></creature>)");
	EXPECT_EQ(cowError(), "[creatures] Cow/herd.xml: <need id=\"hunger\"> from is also given in [creatures] Cow/Cow.xml");

	cow(R"(<creature mood="false"><behavior id="herbivore/eating"/><need id="hunger"><drains perGameMinute="0.05"/></need></creature>)");
	write("creatures/Cow/herd.xml", R"(<creature><need id="hunger"><drains perGameMinute="0.06"/></need></creature>)");
	EXPECT_EQ(
		cowError(),
		"[creatures] Cow/herd.xml: <need id=\"hunger\"> how the level moves (<drains> or <setBy>) is also set in [creatures] Cow/Cow.xml"
	);
	std::filesystem::remove(root / "creatures/Cow/herd.xml");

	cow("");
	EXPECT_EQ(
		cowError(),
		"[creatures] Cow/Cow.xml: <AssetDef defName=\"Cow\"> has no <creature> block; the base definition of a creature folder carries one"
	);
	cow(R"(<creature mood="false"/><creature mood="false"/>)");
	EXPECT_EQ(cowError(), "[creatures] Cow/Cow.xml: <AssetDef defName=\"Cow\"> has more than one <creature> block");
	cow(R"(<creature mood="false"/>)", R"(<AssetDef><defName>Cow_up</defName><creature mood="false"/></AssetDef>)");
	EXPECT_EQ(
		cowError(),
		"[creatures] Cow/Cow.xml: <AssetDef defName=\"Cow_up\"> has a <creature> block, which only the base definition (Cow) carries"
	);
	cow("", "", "Ox");
	EXPECT_EQ(cowError(), "[creatures] Cow/Cow.xml: no <AssetDef> has the folder's name, \"Cow\", so the creature has no base definition");

	cow(R"(<creature mood="false"/>)");
	write(
		"creatures/Cow/being_milked.xml",
		R"(<behavior id="milked"><need id="milking" label="Milking" seek="40" urgent="20" critical="5"><drains perGameMinute="0.02"/></need></behavior>)"
	);
	EXPECT_EQ(
		cowError(),
		"[creatures] Cow/being_milked.xml: <behavior id=\"milked\"> id must be the file's name, \"being_milked\", got \"milked\""
	);
}

// --- Names from the config that loads after the entries --------------------------------------------------

TEST_F(BehaviorLoaderTest, EachBorrowedNameIsCheckedOnceTheirConfigHasLoaded) {
	sample(R"(<idle chain="soil"/>
  <chain id="soil"><go query="bed"/><think thought="soiled_a_bed" to="self"/><think thought="soiled_bed" to="self"/></chain>
  <query id="bed" pick="weighted">
    <from kind="bed"/><from kind="bedd"/>
    <exclude fact="distance" above="5"/>
    <scale trait="gourmand" fact="distance" by="2"/>
    <scale trait="gourmet" fact="distance" by="2"/>
  </query>)");
	const EntryLoad		loaded = clean();
	const EntryNameSets names{
		.isTrait = [](std::string_view name) { return name == "gourmand"; },
		.isKind = [](std::string_view name) { return name == "bed"; },
		.isThought = [](std::string_view name) { return name == "soiled_a_bed"; },
	};
	EXPECT_EQ(
		checkEntryNames(loaded.library, names),
		(std::vector<std::string>{
			"[behaviors] test/sample.xml: <query id=\"bed\"> row 2 <from> kind \"bedd\" names no kind",
			"[behaviors] test/sample.xml: <query id=\"bed\"> row 5 <scale> trait \"gourmet\" names no trait",
			"[behaviors] test/sample.xml: <chain id=\"soil\"> step 3 <think> thought \"soiled_bed\" names no thought type",
		})
	);
}

// --- The shipped config ----------------------------------------------------------------------------------

namespace {

	class ShippedEntriesTest : public ::testing::Test {
	  protected:
		void TearDown() override { PriorityConfig::Get().clear(); }
	};

	/// The needs the human's needs config gives, as need ids: the sim's tracked needs (its labels in lower case).
	std::vector<std::string> humanNeedIds() {
		const CreatureConfigFiles human = creatureConfigFiles(repoRoot() / "assets" / "world" / "creatures" / "Human");
		const NeedsConfigLoad	  needs = loadNeedsConfig(human.needs, human.folder);
		EXPECT_TRUE(needs.errors.empty()) << ::testing::PrintToString(needs.errors);
		std::vector<std::string> ids;
		for (const NeedDef& need : needs.config.needs) {
			std::string id = need.need;
			std::ranges::transform(id, id.begin(), [](char letter) {
				return static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
			});
			ids.push_back(std::move(id));
		}
		return ids;
	}

} // namespace

TEST_F(ShippedEntriesTest, TheShippedConfigAndCreaturesLoadClean) {
	const std::filesystem::path config = repoRoot() / "assets" / "config";
	ASSERT_TRUE(PriorityConfig::Get().loadFromFile(config / "arbitration", "priority-tuning.xml"))
		<< ::testing::PrintToString(PriorityConfig::Get().problems());
	const auto& definitions = assets::testing::shippedDefinitionNames();
	ASSERT_TRUE(definitions.contains("Human") && definitions.contains("Misc_BioPile")) << "the shipped definitions were read";
	const assets::testing::ShippedEntrySources shipped =
		assets::testing::shippedEntrySources(PriorityConfig::Get().rungs(), humanNeedIds());
	ASSERT_TRUE(shipped.errors.empty()) << ::testing::PrintToString(shipped.errors);
	const EntryLoad loaded = loadEntries(shipped.sources);
	EXPECT_TRUE(loaded.errors.empty()) << ::testing::PrintToString(loaded.errors);
	EXPECT_TRUE(loaded.warnings.empty()) << ::testing::PrintToString(loaded.warnings);
	const CreatureEntry* creature = loaded.library.findCreature("Human");
	ASSERT_NE(creature, nullptr);
	EXPECT_TRUE(creature->mood);
	ASSERT_FALSE(creature->behaviors.empty());
	EXPECT_EQ(creature->behaviors.front(), loaded.library.findBehavior("human/eating"))
		<< "the human includes the behaviors its actions are in";
	EXPECT_TRUE(loaded.library.findAction("craft").valid()) << "the shared actions load beside the behaviors";
	const auto tuning = [&loaded](std::string_view fact) {
		const auto found = std::ranges::find(kFacts, fact, &FactDeclaration::name);
		return loaded.library.factTuning[static_cast<std::size_t>(found - kFacts.begin())];
	};
	EXPECT_FLOAT_EQ(tuning("on_shore"), 1.5F);
	EXPECT_FLOAT_EQ(tuning("waste_nearby"), 8.0F);
	EXPECT_FLOAT_EQ(tuning("food_nearby"), 15.0F);
	EXPECT_FLOAT_EQ(tuning("sleep_quality"), 0.5F);
}
