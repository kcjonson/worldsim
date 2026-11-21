// Query evaluation (/docs/technical/queries-and-chains.md, Queries): each source, each row, both picks, the
// walk's horizon and the alternates that widen it, fallback rows, claims, the wait, the Reserved row, the cost
// at 10,000 remembered things, no allocation once warm, determinism, and the nearest source against
// nearestAdmitted. Entries are read by the real loader (QueryEntries.test.h). No nav system is wired here, so
// everything is reachable and every place walkable; the relief query against the toilet finder on real
// terrain is in AIDecisionSystem.test.cpp, beside the tests that pin the finder.

#include "QueryEntries.test.h"
#include "QueryEvaluator.h"

#include <ecs/ClaimIndex.h>
#include <ecs/World.h>
#include <ecs/components/Memory.h>
#include <ecs/components/MemoryQueries.h>
#include <ecs/components/Task.h>

#include <assets/AssetRegistry.h>

#include <gtest/gtest.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cstdlib>
#include <new>
#include <random>
#include <tuple>
#include <vector>

// Allocations are counted only while a test asks (NoAllocationOnceWarm); every other allocation of the binary
// goes straight to malloc. Replacing the scalar forms is enough: the array and nothrow forms call them.
namespace {
	thread_local bool	  countAllocations = false;
	thread_local uint64_t allocationsCounted = 0;
} // namespace

void* operator new(std::size_t size) {
	if (countAllocations) {
		++allocationsCounted;
	}
	if (void* memory = std::malloc(size == 0 ? 1 : size)) {
		return memory;
	}
	throw std::bad_alloc();
}

void operator delete(void* memory) noexcept {
	std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
	std::free(memory);
}

namespace queries::test {

	namespace {

		[[nodiscard]] uint16_t bitOf(assets::CapabilityType capability) {
			return static_cast<uint16_t>(1U << static_cast<unsigned>(capability));
		}

		constexpr uint32_t kWater = 3001;
		constexpr uint32_t kOtherWater = 3002;
		constexpr uint32_t kBed = 3003;
		constexpr uint32_t kToilet = 3004;
		constexpr uint32_t kWaste = 3005;

		constexpr ecs::EntityID kSelf = 900;

	} // namespace

	class QueriesTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			assets::AssetRegistry& registry = assets::AssetRegistry::Get();
			berry = registerEdible(registry, "Berry", assets::CapabilityQuality::Normal);
			stew = registerEdible(registry, "Stew", assets::CapabilityQuality::Good);
			assets::AssetDefinition bushDef;
			bushDef.defName = "BerryBush";
			bushDef.label = "Berry bush";
			bushDef.capabilities.harvestable = assets::HarvestableCapability{};
			bushDef.capabilities.harvestable->yieldDefName = "Berry";
			registry.registerTestDefinition(std::move(bushDef));
			bush = registry.getDefNameId("BerryBush");
			assets::AssetDefinition humanDef;
			humanDef.defName = "Human";
			humanDef.label = "Human";
			registry.registerTestDefinition(std::move(humanDef));
			human = registry.getDefNameId("Human");
		}

		void TearDown() override { assets::AssetRegistry::Get().clearDefinitions(); }

		static uint32_t registerEdible(assets::AssetRegistry& registry, const char* name, assets::CapabilityQuality quality) {
			assets::AssetDefinition def;
			def.defName = name;
			def.label = name;
			def.capabilities.carryable = assets::CarryableCapability{1};
			def.itemProperties = assets::ItemProperties{};
			def.itemProperties->stackSize = 20;
			def.itemProperties->edible = assets::EdibleCapability{0.3F, quality, false};
			registry.registerTestDefinition(std::move(def));
			return registry.getDefNameId(name);
		}

		void load(const std::string& queries, const std::string& workQueries = {}) {
			library = testing::loadQueryEntries(queries, workQueries);
			evaluator.emplace(library, assets::AssetRegistry::Get(), kinds);
		}

		[[nodiscard]] QueryRequest request(std::string_view id) {
			const auto handle =
				library.findQuery(id.find('/') == std::string_view::npos && behaviorQuery ? "test/" + std::string(id) : std::string(id));
			EXPECT_TRUE(handle.valid()) << id;
			return QueryRequest{.query = handle, .creature = kSelf, .position = {0.0F, 0.0F}, .memory = &memory};
		}

		QueryResult run(const QueryRequest& asked) { return evaluator->evaluate(asked, world); }
		QueryResult run(std::string_view id) { return run(request(id)); }

		void remember(glm::vec2 at, uint32_t def, assets::CapabilityType capability) {
			memory.rememberWorldEntity(at, def, bitOf(capability));
		}

		[[nodiscard]] static uint64_t keyOf(glm::vec2 at, uint32_t def) { return ecs::hashWorldEntity(at, def); }

		/// A task in `claimWorld` holding `claim`, at `tier`, doing `type`; returns its instance id.
		uint64_t holdClaim(const ecs::Claim& claim, ecs::TaskType type, int tier) {
			const ecs::EntityID holder = claimWorld.createEntity();
			ecs::Task&			task = claimWorld.addComponent<ecs::Task>(holder);
			task.type = type;
			task.instanceId = ++instances;
			task.claimSeq = instances;
			task.priorityTier = tier;
			EXPECT_TRUE(task.claims.push(claim));
			holders.push_back(holder);
			claims.rebuild(claimWorld);
			world.claims = &claims;
			return task.instanceId;
		}

		ecs::Memory							  memory;
		assets::EntryLibrary				  library;
		std::optional<QueryEvaluator>		  evaluator;
		QueryWorld							  world;
		std::vector<assets::ConditionKindDef> kinds{
			{.name = "bed", .capability = assets::CapabilityType::Sleepable},
			{.name = "colonist", .mover = "Human"}
		};
		ecs::World				   claimWorld;
		ecs::ClaimIndex			   claims;
		std::vector<ecs::EntityID> holders;
		uint64_t				   instances = 0;
		bool					   behaviorQuery = true;
		uint32_t				   berry = 0;
		uint32_t				   stew = 0;
		uint32_t				   bush = 0;
		uint32_t				   human = 0;
	};

	// --- Sources ---------------------------------------------------------------------------------------------------

	TEST_F(QueriesTest, CapabilityRowTakesRememberedThingsWithTheCapability) {
		load(R"(<query id="water"><from capability="drinkable"/><weigh fact="distance" by="-1"/></query>)");
		remember({5.0F, 0.0F}, kWater, assets::CapabilityType::Drinkable);
		remember({3.0F, 0.0F}, kWater, assets::CapabilityType::Drinkable);
		remember({1.0F, 0.0F}, kOtherWater, assets::CapabilityType::Edible);

		const QueryResult result = run("water");
		ASSERT_EQ(result.outcome, QueryOutcome::Found);
		EXPECT_EQ(result.target()->kind, CandidateKind::Thing);
		EXPECT_EQ(result.target()->key, keyOf({3.0F, 0.0F}, kWater));
		EXPECT_EQ(result.target()->claimKind, assets::ClaimKind::Use);
		EXPECT_FLOAT_EQ(result.target()->distance, 3.0F);
	}

	TEST_F(QueriesTest, KindRowTakesItsCapabilityOrItsRecentlySeenMovers) {
		load(R"(<query id="bed"><from kind="bed"/><weigh fact="distance" by="-1"/></query>
			<query id="someone"><from kind="colonist"/><weigh fact="distance" by="-1"/></query>)");
		remember({4.0F, 0.0F}, kBed, assets::CapabilityType::Sleepable);
		memory.rememberDynamicEntity(7, human, {2.0F, 0.0F}, 100.0);
		memory.rememberDynamicEntity(8, human, {1.0F, 0.0F}, 10.0);
		world.now = 105.0;
		world.recentSightingGameMinutes = 30.0;

		const QueryResult bed = run("bed");
		ASSERT_EQ(bed.outcome, QueryOutcome::Found);
		EXPECT_EQ(bed.target()->key, keyOf({4.0F, 0.0F}, kBed));
		const QueryResult someone = run("someone");
		ASSERT_EQ(someone.outcome, QueryOutcome::Found);
		EXPECT_EQ(someone.target()->kind, CandidateKind::Mover);
		EXPECT_EQ(someone.target()->entity, 7U) << "the nearer mover was seen too long ago to count";
	}

	TEST_F(QueriesTest, ItemRowTakesOneDefinition) {
		load(R"(<query id="berries"><from item="Berry"/><weigh fact="distance" by="-1"/></query>)");
		remember({6.0F, 0.0F}, berry, assets::CapabilityType::Carryable);
		remember({2.0F, 0.0F}, stew, assets::CapabilityType::Carryable);

		const QueryResult result = run("berries");
		ASSERT_EQ(result.outcome, QueryOutcome::Found);
		EXPECT_EQ(result.target()->key, keyOf({6.0F, 0.0F}, berry));
	}

	TEST_F(QueriesTest, YieldsRowTakesRememberedHarvestablesOfTheItem) {
		load(R"(<query id="bushes"><from yields="Berry"/><weigh fact="distance" by="-1"/></query>
			<query id="food_bushes"><from yields="edible"/><weigh fact="distance" by="-1"/></query>)");
		remember({5.0F, 0.0F}, bush, assets::CapabilityType::Harvestable);
		remember({2.0F, 0.0F}, bush, assets::CapabilityType::Carryable); // not remembered as a harvestable
		remember({1.0F, 0.0F}, berry, assets::CapabilityType::Carryable);

		for (const char* id : {"bushes", "food_bushes"}) {
			const QueryResult result = run(id);
			ASSERT_EQ(result.outcome, QueryOutcome::Found) << id;
			EXPECT_EQ(result.target()->key, keyOf({5.0F, 0.0F}, bush)) << id;
			EXPECT_EQ(result.target()->item, berry) << id;
		}
	}

	TEST_F(QueriesTest, PileRowTakesLoosePilesOfTheItem) {
		load(R"(<query id="piles"><from pile="edible"/><weigh fact="distance" by="-1"/></query>)");
		remember({4.0F, 0.0F}, berry, assets::CapabilityType::Carryable);
		remember({3.0F, 0.0F}, stew, assets::CapabilityType::Carryable);
		remember({1.0F, 0.0F}, bush, assets::CapabilityType::Harvestable);

		const QueryResult result = run("piles");
		ASSERT_EQ(result.outcome, QueryOutcome::Found);
		EXPECT_EQ(result.target()->key, keyOf({3.0F, 0.0F}, stew));
		EXPECT_EQ(result.target()->item, stew);
		EXPECT_EQ(result.target()->claimKind, assets::ClaimKind::SourceQuantity);
	}

	TEST_F(QueriesTest, BoxRowTakesEveryBoxAndItemTheColonyKnows) {
		load(R"(<query id="berry_box"><from box="Berry"/><weigh fact="distance" by="-1"/></query>
			<query id="food_box"><from box="edible"/><weigh fact="distance" by="-1"/></query>)");
		const std::vector<KnownBox> boxes = {
			{.box = 50, .key = 500, .position = {5.0F, 0.0F}, .item = berry, .quantity = 3},
			{.box = 50, .key = 500, .position = {5.0F, 0.0F}, .item = stew, .quantity = 2},
			{.box = 51, .key = 510, .position = {2.0F, 0.0F}, .item = stew, .quantity = 1},
		};
		world.boxes = boxes;

		const QueryResult berries = run("berry_box");
		ASSERT_EQ(berries.outcome, QueryOutcome::Found);
		EXPECT_EQ(berries.target()->entity, 50U);
		EXPECT_EQ(berries.target()->item, berry);
		EXPECT_EQ(berries.target()->left, 3U);
		EXPECT_EQ(berries.ranked.size(), 1U);

		QueryRequest asked = request("food_box");
		asked.alternates = 2;
		const QueryResult food = run(asked);
		ASSERT_EQ(food.ranked.size(), 3U) << "a box with two foods is two candidates";
		EXPECT_EQ(food.ranked[0].entity, 51U);
		EXPECT_EQ(std::make_pair(food.ranked[1].entity, food.ranked[1].item), std::make_pair(boxes[0].box, std::min(berry, stew)));
		EXPECT_EQ(std::make_pair(food.ranked[2].entity, food.ranked[2].item), std::make_pair(boxes[0].box, std::max(berry, stew)));
	}

	TEST_F(QueriesTest, PlaceHereIsWhereTheCreatureStands) {
		load(R"(<query id="ground"><from place="here"/><weigh fact="distance" by="-1"/></query>)");
		QueryRequest asked = request("ground");
		asked.position = {12.5F, -3.0F};
		const QueryResult result = run(asked);
		ASSERT_EQ(result.outcome, QueryOutcome::Found);
		EXPECT_EQ(result.target()->kind, CandidateKind::Place);
		EXPECT_EQ(result.target()->position, asked.position);
	}

	TEST_F(QueriesTest, PlaceAroundDrawsSeededPointsInTheRing) {
		load(
			R"(<query id="ring" pick="weighted"><from place="around" minRadius="2" radius="8" count="5"/><exclude fact="distance" above="8"/></query>)"
		);
		QueryRequest asked = request("ring");
		asked.alternates = 4;
		const auto points = [&](uint32_t seed) {
			std::mt19937 generator(seed);
			world.generator = &generator;
			std::vector<glm::vec2> drawn;
			for (const QueryCandidate& candidate : run(asked).ranked) {
				EXPECT_GE(candidate.distance, 2.0F - 1.0e-4F);
				EXPECT_LE(candidate.distance, 8.0F);
				drawn.push_back(candidate.position);
			}
			std::ranges::sort(drawn, [](glm::vec2 first, glm::vec2 second) {
				return std::tie(first.x, first.y) < std::tie(second.x, second.y);
			});
			return drawn;
		};
		const std::vector<glm::vec2> first = points(42);
		EXPECT_EQ(first.size(), 5U);
		EXPECT_EQ(points(42), first) << "the same seed draws the same points";
		EXPECT_NE(points(43), first);
	}

	TEST_F(QueriesTest, PlaceGridLaysTheFindersGridAndWeighsItsSpots) {
		load(
			R"(<query id="spot"><from place="grid" spacing="3" radius="25"/><weigh fact="distance" by="-1"/><weigh fact="waste_nearby" by="10"/></query>)"
		);
		remember({6.5F, 0.0F}, kWaste, assets::CapabilityType::Waste);

		// -6 + 10 x (1 - 0.5 / 8) = 3.375 at (6, 0), against 2.625 at (3, 0) and 1.875 at (0, 0).
		const QueryResult result = run("spot");
		ASSERT_EQ(result.outcome, QueryOutcome::Found);
		EXPECT_EQ(result.target()->position, glm::vec2(6.0F, 0.0F));
		EXPECT_FLOAT_EQ(result.target()->score, 3.375F);

		uint32_t inRadius = 0;
		for (int dy = -9; dy <= 9; ++dy) {
			for (int dx = -9; dx <= 9; ++dx) {
				inRadius += glm::length(glm::vec2(static_cast<float>(dx) * 3.0F, static_cast<float>(dy) * 3.0F)) <= 25.0F ? 1U : 0U;
			}
		}
		EXPECT_EQ(evaluator->count(request("spot"), world), std::optional<uint32_t>(inRadius));
	}

	TEST_F(QueriesTest, GoalRowsTakeTheDestinationAndItsFreeSpots) {
		behaviorQuery = false;
		load("", R"(<query id="to_destination"><from goal="destination"/><weigh fact="distance" by="-1"/></query>
			<query id="to_spot"><from goal="spots"/><weigh fact="distance" by="-1"/></query>)");
		const std::vector<glm::vec2> spots = {{9.0F, 1.0F}, {11.0F, 1.0F}, {10.0F, 2.0F}};
		const QueryGoal				 goal{.goalId = 4, .destination = 70, .destinationPosition = {10.0F, 0.0F}, .builderSpots = spots};

		QueryRequest toDestination = request("to_destination");
		toDestination.goal = &goal;
		const QueryResult destination = run(toDestination);
		ASSERT_EQ(destination.outcome, QueryOutcome::Found);
		EXPECT_EQ(destination.target()->kind, CandidateKind::Destination);
		EXPECT_EQ(destination.target()->entity, 70U);

		QueryRequest toSpot = request("to_spot");
		toSpot.goal = &goal;
		toSpot.claims.kinds = 1U << static_cast<unsigned>(assets::ClaimKind::Slot);
		EXPECT_EQ(run(toSpot).target()->key, 0U);
		holdClaim(ecs::Claim::slot(70, 0), ecs::TaskType::Build, 4);
		const QueryResult spot = run(toSpot);
		ASSERT_EQ(spot.outcome, QueryOutcome::Found);
		EXPECT_EQ(spot.target()->key, 2U) << "the nearest spot is another builder's";
		EXPECT_EQ(spot.target()->claimKind, assets::ClaimKind::Slot);
	}

	// --- Rows ------------------------------------------------------------------------------------------------------

	TEST_F(QueriesTest, WeighAddsEachFactTimesItsWeight) {
		load(R"(<query id="food"><from pile="edible"/><weigh fact="distance" by="-0.1"/><weigh fact="quality" by="4"/></query>)");
		remember({5.0F, 0.0F}, berry, assets::CapabilityType::Carryable);
		remember({20.0F, 0.0F}, stew, assets::CapabilityType::Carryable);
		// Berries -0.5 + 4 x 0.5 = 1.5; stew at 20 m -2 + 4 x 0.75 = 1.
		EXPECT_EQ(run("food").target()->item, berry);
		EXPECT_FLOAT_EQ(run("food").target()->score, 1.5F);

		remember({10.0F, 0.0F}, stew, assets::CapabilityType::Carryable);
		EXPECT_EQ(run("food").target()->key, keyOf({10.0F, 0.0F}, stew)) << "stew at 10 m scores 2";
	}

	TEST_F(QueriesTest, ExcludeRemovesWhatIsBeyondItsLine) {
		load(R"(<query id="near"><from capability="drinkable"/><exclude fact="distance" above="8"/><weigh fact="distance" by="-1"/></query>
			<query id="good_food"><from pile="edible"/><exclude fact="quality" below="0.6"/><weigh fact="distance" by="-1"/></query>)");
		remember({10.0F, 0.0F}, kWater, assets::CapabilityType::Drinkable);
		EXPECT_EQ(run("near").outcome, QueryOutcome::AllExcluded);
		remember({6.0F, 0.0F}, kWater, assets::CapabilityType::Drinkable);
		EXPECT_EQ(run("near").target()->key, keyOf({6.0F, 0.0F}, kWater));

		// A definition-scoped exclusion removes every berry before any is visited.
		for (int i = 0; i < 20; ++i) {
			remember({1.0F + static_cast<float>(i), 1.0F}, berry, assets::CapabilityType::Carryable);
		}
		remember({30.0F, 0.0F}, stew, assets::CapabilityType::Carryable);
		const QueryResult good = run("good_food");
		ASSERT_EQ(good.outcome, QueryOutcome::Found);
		EXPECT_EQ(good.target()->item, stew);
		EXPECT_EQ(good.visits, 1U);
	}

	TEST_F(QueriesTest, PreferRanksWhatItWouldExcludeBelowEverythingItKeeps) {
		load(
			R"(<query id="sleep"><from capability="sleepable"/><from place="here"/><prefer fact="sleep_quality" below="1"/><weigh fact="distance" by="-1"/></query>)"
		);
		const QueryResult ground = run("sleep");
		ASSERT_EQ(ground.outcome, QueryOutcome::Found);
		EXPECT_EQ(ground.target()->kind, CandidateKind::Place);
		EXPECT_EQ(ground.target()->preferClass, 1U);

		remember({30.0F, 0.0F}, kBed, assets::CapabilityType::Sleepable);
		const QueryResult bed = run("sleep");
		ASSERT_EQ(bed.outcome, QueryOutcome::Found);
		EXPECT_EQ(bed.target()->key, keyOf({30.0F, 0.0F}, kBed)) << "any bed before the ground, at any distance";
		EXPECT_EQ(bed.visits, 1U) << "the ground, the lower class, is not walked once a bed survives";
	}

	TEST_F(QueriesTest, ScaleMultipliesAFactsWeightForACreatureWithTheTrait) {
		load(R"(<query id="food"><from pile="edible"/><weigh fact="distance" by="-1"/><weigh fact="quality" by="4"/>
			<scale trait="gourmand" fact="quality" by="2"/></query>)");
		remember({1.0F, 0.0F}, berry, assets::CapabilityType::Carryable);
		remember({2.5F, 0.0F}, stew, assets::CapabilityType::Carryable);
		EXPECT_EQ(run("food").target()->item, berry) << "-1 + 2 against -2.5 + 3";

		const std::vector<std::string> traits = {"gourmand"};
		QueryRequest				   gourmand = request("food");
		gourmand.traits = traits;
		EXPECT_EQ(run(gourmand).target()->item, stew) << "-1 + 4 against -2.5 + 6";
	}

	// --- Picks -----------------------------------------------------------------------------------------------------

	TEST_F(QueriesTest, PickBestTakesTheHighestScoreThenTheNearestThenTheLowestKey) {
		load(R"(<query id="water"><from capability="drinkable"/><weigh fact="distance" by="-1"/></query>)");
		for (const glm::vec2 at : {glm::vec2{3.0F, 0.0F}, glm::vec2{0.0F, 3.0F}, glm::vec2{-3.0F, 0.0F}, glm::vec2{0.0F, 4.0F}}) {
			remember(at, kWater, assets::CapabilityType::Drinkable);
		}
		QueryRequest asked = request("water");
		asked.alternates = 3;
		const QueryResult result = run(asked);
		ASSERT_EQ(result.ranked.size(), 4U);
		std::vector<uint64_t> tied = {keyOf({3.0F, 0.0F}, kWater), keyOf({0.0F, 3.0F}, kWater), keyOf({-3.0F, 0.0F}, kWater)};
		std::ranges::sort(tied);
		for (std::size_t index = 0; index < tied.size(); ++index) {
			EXPECT_EQ(result.ranked[index].key, tied[index]);
		}
		EXPECT_EQ(result.ranked[3].key, keyOf({0.0F, 4.0F}, kWater));
	}

	TEST_F(QueriesTest, PickWeightedDrawsBySeedFromTheBestSixteen) {
		load(
			R"(<query id="water" pick="weighted"><from capability="drinkable"/><exclude fact="distance" above="30"/><weigh fact="distance" by="-1"/></query>)"
		);
		for (int distance = 1; distance <= 20; ++distance) {
			remember({static_cast<float>(distance), 0.0F}, kWater, assets::CapabilityType::Drinkable);
		}
		const auto draws = [&](uint32_t seed) {
			std::mt19937 generator(seed);
			world.generator = &generator;
			std::vector<int> picks(21, 0);
			for (int draw = 0; draw < 2000; ++draw) {
				++picks[static_cast<std::size_t>(run("water").target()->distance)];
			}
			return picks;
		};
		const std::vector<int> picks = draws(7);
		EXPECT_EQ(draws(7), picks) << "the same seed, the same picks";
		// Each of the best 16 by its score less the worst of them, plus 1: 16 for the nearest, 1 for the 16th.
		for (int distance = 1; distance <= 16; ++distance) {
			EXPECT_GT(picks[static_cast<std::size_t>(distance)], 0) << distance;
		}
		for (int distance = 17; distance <= 20; ++distance) {
			EXPECT_EQ(picks[static_cast<std::size_t>(distance)], 0) << distance;
		}
		EXPECT_GT(picks[1], 5 * picks[16]);
	}

	// --- The walk --------------------------------------------------------------------------------------------------

	// Each thing its own definition, so each is its own seed of the walk: the horizon stops it after the winner,
	// or after the alternates a need's nearest-of round asks for (kNearestOfCount, widened by doubling); a weight
	// on a fact with no upper bound leaves no horizon.
	TEST_F(QueriesTest, TheHorizonEndsTheWalkAndAlternatesWidenIt) {
		load(
			R"(<query id="water"><from capability="drinkable"/><weigh fact="distance" by="-1"/></query>
			<query id="water_by_waste"><from capability="drinkable"/><weigh fact="distance" by="-1"/><weigh fact="waste_nearby" by="1"/></query>)"
		);
		for (uint32_t i = 0; i < 50; ++i) {
			remember({static_cast<float>(i + 1), 0.0F}, kWater + 100 + i, assets::CapabilityType::Drinkable);
		}
		QueryRequest asked = request("water");
		EXPECT_EQ(run(asked).visits, 1U);
		for (const std::size_t round : {ecs::kNearestOfCount, ecs::kNearestOfCount << 1, ecs::kNearestOfCount << 2}) {
			asked.alternates = round - 1;
			const QueryResult result = run(asked);
			EXPECT_EQ(result.visits, round);
			ASSERT_EQ(result.ranked.size(), round);
			EXPECT_FLOAT_EQ(result.ranked.back().distance, static_cast<float>(round));
		}
		EXPECT_EQ(run("water_by_waste").visits, 50U);
	}

	TEST_F(QueriesTest, AFallbackRowIsTakenOnlyWhenNothingElseSurvivesAndIgnoresTheRows) {
		load(
			R"(<query id="relief"><from capability="toilet"/><from place="here" fallback="true"/><exclude fact="is_place"/><exclude fact="distance" above="20"/><weigh fact="distance" by="-1"/></query>)"
		);
		const QueryResult ground = run("relief");
		ASSERT_EQ(ground.outcome, QueryOutcome::Found);
		EXPECT_TRUE(ground.target()->fallback);
		EXPECT_EQ(ground.target()->kind, CandidateKind::Place) << "no exclusion removes a fallback candidate";

		remember({30.0F, 0.0F}, kToilet, assets::CapabilityType::Toilet);
		EXPECT_TRUE(run("relief").target()->fallback) << "the latrine is excluded, so nothing else survives";
		remember({12.0F, 0.0F}, kToilet, assets::CapabilityType::Toilet);
		const QueryResult latrine = run("relief");
		EXPECT_FALSE(latrine.target()->fallback);
		EXPECT_EQ(latrine.target()->key, keyOf({12.0F, 0.0F}, kToilet));
	}

	// --- Claims ----------------------------------------------------------------------------------------------------

	// An eater at least as urgent takes a bush (passed over); work only holds it (`held`). When every candidate is
	// passed over the query waits, and is not walked again until a claim is released.
	TEST_F(QueriesTest, ClaimsPassOverWhatTakesItAndTheQueryWaitsWhenAllAre) {
		load(R"(<query id="food"><from yields="Berry"/><weigh fact="distance" by="-1"/></query>
			<query id="free_food"><from yields="Berry"/><prefer fact="held"/><weigh fact="distance" by="-1"/></query>)");
		const glm::vec2 nearBush{2.0F, 0.0F};
		const glm::vec2 farBush{6.0F, 0.0F};
		remember(nearBush, bush, assets::CapabilityType::Harvestable);
		remember(farBush, bush, assets::CapabilityType::Harvestable);
		QueryRequest asked = request("food");
		asked.claims = {.kinds = 1U << static_cast<unsigned>(assets::ClaimKind::Use), .tier = 5};

		holdClaim(ecs::Claim::use(ecs::TargetKind::WorldKey, keyOf(nearBush, bush)), ecs::TaskType::Harvest, 6);
		const QueryResult heldByWork = run(asked);
		EXPECT_EQ(heldByWork.target()->key, keyOf(nearBush, bush)) << "work never takes food from a hungry creature";
		EXPECT_FLOAT_EQ(heldByWork.target()->held, 1.0F);
		QueryRequest preferFree = request("free_food");
		preferFree.claims = asked.claims;
		EXPECT_EQ(run(preferFree).target()->key, keyOf(farBush, bush)) << "free food before held food";

		claimWorld.destroyEntity(holders.back());
		holdClaim(ecs::Claim::use(ecs::TargetKind::WorldKey, keyOf(nearBush, bush)), ecs::TaskType::FulfillNeed, 5);
		EXPECT_EQ(run(asked).target()->key, keyOf(farBush, bush));

		const uint64_t farEater =
			holdClaim(ecs::Claim::use(ecs::TargetKind::WorldKey, keyOf(farBush, bush)), ecs::TaskType::FulfillNeed, 3);
		const QueryResult waiting = run(asked);
		EXPECT_EQ(waiting.outcome, QueryOutcome::AllPassedOver);
		ASSERT_TRUE(waiting.waitingOn.has_value());
		EXPECT_EQ(waiting.waitingOn->key, keyOf(nearBush, bush));
		EXPECT_EQ(waiting.visits, 2U);
		const QueryResult stillWaiting = run(asked);
		EXPECT_TRUE(stillWaiting.fromWait);
		EXPECT_EQ(stillWaiting.visits, 0U);
		EXPECT_EQ(stillWaiting.outcome, QueryOutcome::AllPassedOver);

		claims.retire(farEater);
		const QueryResult released = run(asked);
		EXPECT_FALSE(released.fromWait);
		ASSERT_EQ(released.outcome, QueryOutcome::Found);
		EXPECT_EQ(released.target()->key, keyOf(farBush, bush));
	}

	TEST_F(QueriesTest, TheReservedRowIsTheBestTakenCandidateAheadOfThePick) {
		load(R"(<query id="food"><from pile="Berry"/><weigh fact="distance" by="-1"/></query>)");
		const glm::vec2 nearPile{2.0F, 0.0F};
		const glm::vec2 farPile{9.0F, 0.0F};
		remember(nearPile, berry, assets::CapabilityType::Carryable);
		remember(farPile, berry, assets::CapabilityType::Carryable);
		holdClaim(ecs::Claim::sourceQuantity(ecs::TargetKind::WorldKey, keyOf(nearPile, berry), berry, 1), ecs::TaskType::FulfillNeed, 2);
		QueryRequest asked = request("food");
		asked.claims = {.kinds = 1U << static_cast<unsigned>(assets::ClaimKind::SourceQuantity), .tier = 5};

		const QueryResult result = run(asked);
		ASSERT_EQ(result.outcome, QueryOutcome::Found);
		EXPECT_EQ(result.target()->key, keyOf(farPile, berry));
		ASSERT_TRUE(result.reserved.has_value());
		EXPECT_EQ(result.reserved->key, keyOf(nearPile, berry));
		EXPECT_EQ(result.reservedBy, holders.front());

		asked.claims.tier = 1;
		EXPECT_EQ(run(asked).target()->key, keyOf(nearPile, berry)) << "a more urgent eater is not held back";
	}

	// --- Cost, allocation, determinism ---------------------------------------------------------------------------

	namespace {
		/// 10,000 remembered piles of food (carryable and edible) over a 2 km square, by a fixed sequence.
		void rememberTenThousand(ecs::Memory& memory, const std::vector<uint32_t>& foods, uint32_t seed, bool shuffled) {
			std::vector<std::pair<glm::vec2, uint32_t>> things;
			std::mt19937								generator(seed);
			for (std::size_t i = 0; i < ecs::Memory::kMaxWorldEntities; ++i) {
				const float x = static_cast<float>(generator() % 200000) / 100.0F - 1000.0F;
				const float y = static_cast<float>(generator() % 200000) / 100.0F - 1000.0F;
				things.emplace_back(glm::vec2{x, y}, foods[i % foods.size()]);
			}
			if (shuffled) {
				std::mt19937 shuffler(seed + 1);
				for (std::size_t i = things.size() - 1; i > 0; --i) {
					std::swap(things[i], things[shuffler() % (i + 1)]);
				}
			}
			for (const auto& [at, def] : things) {
				memory.rememberWorldEntity(
					at, def, static_cast<uint16_t>(bitOf(assets::CapabilityType::Carryable) | bitOf(assets::CapabilityType::Edible))
				);
			}
		}
	} // namespace

	TEST_F(QueriesTest, TenThousandRememberedThingsVisitOnlyTheNearest) {
		const uint32_t cabbage = registerEdible(assets::AssetRegistry::Get(), "Cabbage", assets::CapabilityQuality::Poor);
		load(R"(<query id="food"><from pile="edible"/><weigh fact="distance" by="-1"/></query>)");
		rememberTenThousand(memory, {berry, stew, cabbage}, 11, false);
		ASSERT_EQ(memory.worldEntityCount(), ecs::Memory::kMaxWorldEntities);

		QueryRequest asked = request("food");
		asked.alternates = ecs::kNearestOfCount - 1;
		const QueryResult result = run(asked);
		ASSERT_EQ(result.ranked.size(), ecs::kNearestOfCount);
		EXPECT_EQ(result.visits, ecs::kNearestOfCount);

		std::vector<std::pair<float, uint64_t>> everything;
		for (const auto& [key, known] : memory.knownWorldEntities) {
			const glm::vec2 offset = known.position - asked.position;
			everything.emplace_back(glm::dot(offset, offset), key);
		}
		std::ranges::sort(everything);
		for (std::size_t index = 0; index < ecs::kNearestOfCount; ++index) {
			EXPECT_EQ(result.ranked[index].key, everything[index].second) << index;
		}
	}

	TEST_F(QueriesTest, NoAllocationOnceWarm) {
		load(
			R"(<query id="food"><from pile="edible"/><from box="edible"/><from place="here" fallback="true"/><prefer fact="held"/>
			<weigh fact="distance" by="-1"/><weigh fact="quality" by="2"/><weigh fact="food_nearby" by="-0.1"/></query>
			<query id="spot"><from place="grid" spacing="3" radius="25"/><weigh fact="distance" by="-1"/><weigh fact="waste_nearby" by="10"/></query>)"
		);
		rememberTenThousand(memory, {berry, stew}, 5, false);
		remember({4.0F, 4.0F}, kWaste, assets::CapabilityType::Waste);
		holdClaim(ecs::Claim::use(ecs::TargetKind::WorldKey, 1), ecs::TaskType::Harvest, 6);
		const std::vector<KnownBox> boxes = {{.box = 50, .key = 500, .position = {3.0F, 3.0F}, .item = berry, .quantity = 2}};
		world.boxes = boxes;
		QueryRequest food = request("food");
		food.alternates = ecs::kNearestOfCount - 1;
		food.claims = {.kinds = 1U << static_cast<unsigned>(assets::ClaimKind::SourceQuantity), .tier = 5};
		const QueryRequest spot = request("spot");

		for (int warmUp = 0; warmUp < 2; ++warmUp) {
			(void)run(food);
			(void)run(spot);
		}
		countAllocations = true;
		{ const std::vector<int> probe(1); }
		countAllocations = false;
		ASSERT_GT(allocationsCounted, 0U) << "the counter sees an allocation";

		allocationsCounted = 0;
		countAllocations = true;
		uint32_t found = 0;
		for (int i = 0; i < 50; ++i) {
			found += run(food).outcome == QueryOutcome::Found ? 1U : 0U;
			found += run(spot).outcome == QueryOutcome::Found ? 1U : 0U;
		}
		countAllocations = false;
		EXPECT_EQ(found, 100U);
		EXPECT_EQ(allocationsCounted, 0U);
	}

	TEST_F(QueriesTest, TheSameMemoryFilledInAnotherOrderGivesTheSameResult) {
		load(
			R"(<query id="food"><from pile="edible"/><weigh fact="distance" by="-1"/><weigh fact="food_nearby" by="0.5"/></query>
			<query id="spot"><from place="grid" spacing="3" radius="25"/><weigh fact="distance" by="-1"/><weigh fact="food_nearby" by="-20"/></query>)"
		);
		ecs::Memory shuffled;
		rememberTenThousand(memory, {berry, stew}, 3, false);
		rememberTenThousand(shuffled, {berry, stew}, 3, true);
		ASSERT_EQ(memory.worldEntityCount(), shuffled.worldEntityCount());

		for (const char* id : {"food", "spot"}) {
			QueryRequest asked = request(id);
			asked.position = {13.0F, -7.0F};
			asked.alternates = 5;
			const QueryResult				  first = run(asked);
			const std::vector<QueryCandidate> inOrder(first.ranked.begin(), first.ranked.end());
			asked.memory = &shuffled;
			asked.creature = kSelf + 1;
			const QueryResult other = run(asked);
			ASSERT_EQ(other.ranked.size(), inOrder.size()) << id;
			for (std::size_t index = 0; index < inOrder.size(); ++index) {
				EXPECT_EQ(other.ranked[index].key, inOrder[index].key) << id << " " << index;
				EXPECT_EQ(other.ranked[index].position, inOrder[index].position) << id << " " << index;
				EXPECT_EQ(other.ranked[index].score, inOrder[index].score) << id << " " << index << ": scores equal to the bit";
			}
		}
	}

	// --- Equivalence ---------------------------------------------------------------------------------------------

	// FindsANeedSourceInALargeMemory's thirsty colonist: 9,990 waters, each its own definition, beside piles and
	// trees, and no mesh. The need search's nearest-of round (nearestAdmitted over collectWithCapability) and a
	// nearest-water query keeping kNearestOfCount ranked pick the same sources in the same order, each asking
	// about 8 of them.
	TEST_F(QueriesTest, ANearestSourceQueryMatchesNearestAdmittedOverALargeMemory) {
		load(R"(<query id="water"><from capability="drinkable"/><weigh fact="distance" by="-1"/></query>)");
		for (int i = 0; i < 3; ++i) {
			remember({3.0F + static_cast<float>(i), 0.0F}, berry, assets::CapabilityType::Carryable);
		}
		for (int i = 0; i < 2; ++i) {
			remember({-3.0F - static_cast<float>(i), 0.0F}, bush, assets::CapabilityType::Harvestable);
		}
		for (int i = 0; i < 9990; ++i) {
			remember(
				{static_cast<float>(i), static_cast<float>(i)}, kWater + 1000 + static_cast<uint32_t>(i), assets::CapabilityType::Drinkable
			);
		}

		std::vector<ecs::SourceCandidate> scratch;
		std::vector<ecs::SourceCandidate> chosen;
		uint64_t						  admitted = 0;
		ecs::collectWithCapability(
			memory, assets::CapabilityType::Drinkable, {0.0F, 0.0F}, [](uint64_t, const ecs::KnownWorldEntity&) { return true; }, scratch
		);
		ecs::nearestAdmitted(
			scratch,
			chosen,
			[&](ecs::SourceCandidate&) {
				++admitted;
				return true;
			},
			ecs::kNearestOfCount
		);

		QueryRequest asked = request("water");
		asked.alternates = ecs::kNearestOfCount - 1;
		const QueryResult result = run(asked);
		ASSERT_EQ(result.ranked.size(), chosen.size());
		for (std::size_t index = 0; index < chosen.size(); ++index) {
			EXPECT_EQ(result.ranked[index].key, chosen[index].key) << index;
		}
		EXPECT_EQ(admitted, 8U);
		EXPECT_EQ(result.visits, 8U);
	}

	// --- What is not evaluated yet -------------------------------------------------------------------------------

	TEST_F(QueriesTest, ARowWithAConditionIsNotEvaluatedAndAnUnansweredFactIsNamed) {
		load(
			R"(<query id="seat"><from capability="sleepable"/><weigh fact="distance" by="-1"/><weigh fact="comfort" by="3" when="need(energy) &lt; 30"/></query>)"
		);
		remember({2.0F, 0.0F}, kBed, assets::CapabilityType::Sleepable);
		const QueryResult result = run("seat");
		EXPECT_EQ(result.outcome, QueryOutcome::ConditionNotEvaluated);
		EXPECT_EQ(result.conditionRow, 2U);
		EXPECT_TRUE(result.ranked.empty());
		EXPECT_FALSE(evaluator->count(request("seat"), world).has_value());
		EXPECT_EQ(
			unansweredFactUses(library),
			std::vector<std::string>{
				"[behaviors] test/sample.xml: <query id=\"seat\"> row 3 <weigh> fact \"comfort\" is answered by nothing yet and reads 0"
			}
		);
	}

	// --- Counting and the definition facts ------------------------------------------------------------------------

	TEST_F(QueriesTest, CountOfCountsWhatSurvivesTheExclusions) {
		load(R"(<query id="free_water"><from capability="drinkable"/><exclude fact="held"/></query>)");
		for (int i = 0; i < 3; ++i) {
			remember({static_cast<float>(i) * 100.0F, 0.0F}, kWater, assets::CapabilityType::Drinkable);
		}
		EXPECT_EQ(evaluator->count(request("free_water"), world), std::optional<uint32_t>(3U));
		holdClaim(ecs::Claim::use(ecs::TargetKind::WorldKey, keyOf({100.0F, 0.0F}, kWater)), ecs::TaskType::Haul, 6);
		EXPECT_EQ(evaluator->count(request("free_water"), world), std::optional<uint32_t>(2U));
	}

	// A harvestable's definition facts read its yield's definition; colony_stock reads the boxes the colony knows,
	// and the cached definition terms are worked out again when the boxes move.
	TEST_F(QueriesTest, DefinitionFactsReadTheYieldAndTheColonysStock) {
		load(R"(<query id="bush_quality"><from yields="Berry"/><weigh fact="distance" by="-1"/><weigh fact="quality" by="4"/></query>
			<query id="stocked"><from pile="edible"/><weigh fact="distance" by="-1"/><weigh fact="colony_stock" by="-1"/></query>)");
		remember({1.0F, 0.0F}, bush, assets::CapabilityType::Harvestable);
		EXPECT_FLOAT_EQ(run("bush_quality").target()->score, -1.0F + 4.0F * 0.5F);

		remember({1.0F, 0.0F}, berry, assets::CapabilityType::Carryable);
		remember({3.0F, 0.0F}, stew, assets::CapabilityType::Carryable);
		EXPECT_EQ(run("stocked").target()->item, berry);
		const std::vector<KnownBox> boxes = {{.box = 50, .key = 500, .position = {9.0F, 0.0F}, .item = berry, .quantity = 5}};
		world.boxes = boxes;
		world.boxesStamp = 1;
		const QueryResult stocked = run("stocked");
		EXPECT_EQ(stocked.target()->item, stew) << "berries the colony holds five of score -6, stew at 3 m -3";
		EXPECT_FLOAT_EQ(stocked.target()->score, -3.0F);
	}

	TEST_F(QueriesTest, BelowDestinationReadsTheBoxsPriorityAgainstTheGoals) {
		behaviorQuery = false;
		load(
			"",
			R"(<query id="pull"><from box="goal"/><exclude fact="below_destination" below="1"/><weigh fact="distance" by="-1"/></query>)"
		);
		const std::vector<KnownBox> boxes = {
			{.box = 50, .key = 500, .position = {2.0F, 0.0F}, .item = berry, .quantity = 4, .priority = 2},
			{.box = 51, .key = 510, .position = {6.0F, 0.0F}, .item = berry, .quantity = 4, .priority = 0},
		};
		world.boxes = boxes;
		const QueryGoal goal{.goalId = 9, .destination = 60, .destinationPosition = {20.0F, 0.0F}, .item = berry, .destinationPriority = 2};
		QueryRequest	asked = request("pull");
		asked.goal = &goal;
		const QueryResult result = run(asked);
		ASSERT_EQ(result.outcome, QueryOutcome::Found);
		EXPECT_EQ(result.target()->entity, 51U) << "only a box strictly below the destination's priority is pulled from";
	}

} // namespace queries::test
