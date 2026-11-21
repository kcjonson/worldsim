// Query evaluation (/docs/technical/queries-and-chains.md, Queries): each source, each row, both picks, the
// walk's horizon and the alternates that widen it, fallback rows, claims, the wait and everything that ends it,
// the Reserved row, meals sized per item, work gates, the kept target, the cost at 10,000 remembered things, no
// allocation once warm, determinism, and the nearest source against nearestAdmitted. Entries are read by the
// real loader (QueryEntries.test.h). No nav system is wired here, so everything is reachable and every place
// walkable; the relief query against the toilet finder on real terrain, and the wait's component and epoch
// stamps, are in AIDecisionSystem.test.cpp.

#include "QueryEntries.test.h"
#include "QueryEvaluator.h"

#include <ecs/ClaimIndex.h>
#include <ecs/LoosePileIndex.h>
#include <ecs/World.h>
#include <ecs/components/Memory.h>
#include <ecs/components/MemoryQueries.h>
#include <ecs/components/ResourceStack.h>
#include <ecs/components/SourceReachMemo.h>
#include <ecs/components/Task.h>

#include <assets/AssetRegistry.h>

#include <gtest/gtest.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cstdlib>
#include <new>
#include <optional>
#include <random>
#include <string>
#include <tuple>
#include <utility>
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

		constexpr uint8_t kUseClaim = 1U << static_cast<unsigned>(assets::ClaimKind::Use);
		constexpr uint8_t kSourceClaim = 1U << static_cast<unsigned>(assets::ClaimKind::SourceQuantity);

		/// The pick, after checking there is one; an empty candidate (and a failure) when there is none.
		const QueryCandidate& pickOf(const QueryResult& result) {
			static const QueryCandidate kNone;
			EXPECT_EQ(result.outcome, QueryOutcome::Found);
			if (result.target() == nullptr) {
				ADD_FAILURE() << "no pick";
				return kNone;
			}
			return *result.target();
		}

	} // namespace

	class QueriesTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			assets::AssetRegistry& registry = assets::AssetRegistry::Get();
			berry = registerEdible(registry, "Berry", assets::CapabilityQuality::Normal, 0.3F);
			stew = registerEdible(registry, "Stew", assets::CapabilityQuality::Good, 0.3F);
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

		static uint32_t
		registerEdible(assets::AssetRegistry& registry, const char* name, assets::CapabilityQuality quality, float nutrition) {
			assets::AssetDefinition def;
			def.defName = name;
			def.label = name;
			def.capabilities.carryable = assets::CarryableCapability{1};
			def.itemProperties = assets::ItemProperties{};
			def.itemProperties->stackSize = 20;
			def.itemProperties->edible = assets::EdibleCapability{nutrition, quality, false};
			registry.registerTestDefinition(std::move(def));
			return registry.getDefNameId(name);
		}

		void load(const std::string& queries, const std::string& workQueries = {}) {
			library = test_entries::loadQueryEntries(queries, workQueries);
			evaluator.emplace(library, assets::AssetRegistry::Get(), kinds);
		}

		[[nodiscard]] QueryRequest request(std::string_view name) {
			const auto handle = library.findQuery(
				name.find('/') == std::string_view::npos && behaviorQuery ? "test/" + std::string(name) : std::string(name)
			);
			EXPECT_TRUE(handle.valid()) << name;
			return QueryRequest{.query = handle, .creature = kSelf, .position = {0.0F, 0.0F}, .memory = &memory};
		}

		QueryResult run(const QueryRequest& asked) { return evaluator->evaluate(asked, world); }
		QueryResult run(std::string_view name) { return run(request(name)); }

		void remember(glm::vec2 position, uint32_t def, assets::CapabilityType capability) {
			memory.rememberWorldEntity(position, def, bitOf(capability));
		}

		[[nodiscard]] static uint64_t keyOf(glm::vec2 position, uint32_t def) { return ecs::hashWorldEntity(position, def); }

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

		/// Bushes regrow until `readyAt`, on the simulation clock.
		void regrowUntil(double newReadyAt) {
			readyAt = newReadyAt;
			world.regrowth = &regrowth;
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
		double					   readyAt = 0.0;
		RegrowthQuery			   regrowth = [this](uint32_t, glm::vec2, double now) -> std::optional<double> {
			 return now < readyAt ? std::optional<double>(readyAt) : std::nullopt;
		};
		bool	 behaviorQuery = true;
		uint32_t berry = 0;
		uint32_t stew = 0;
		uint32_t bush = 0;
		uint32_t human = 0;
	};

	// --- Sources ---------------------------------------------------------------------------------------------------

	TEST_F(QueriesTest, CapabilityRowTakesRememberedThingsWithTheCapability) {
		load(R"(<query id="water"><from capability="drinkable"/><weigh fact="distance" by="-1"/></query>)");
		EXPECT_EQ(run("water").outcome, QueryOutcome::NothingKnown);
		remember({5.0F, 0.0F}, kWater, assets::CapabilityType::Drinkable);
		remember({3.0F, 0.0F}, kWater, assets::CapabilityType::Drinkable);
		remember({1.0F, 0.0F}, kOtherWater, assets::CapabilityType::Edible);

		const QueryCandidate& water = pickOf(run("water"));
		EXPECT_EQ(water.kind, CandidateKind::Thing);
		EXPECT_EQ(water.key, keyOf({3.0F, 0.0F}, kWater));
		EXPECT_EQ(water.claimKind, assets::ClaimKind::Use);
		EXPECT_FLOAT_EQ(water.distance, 3.0F);
	}

	TEST_F(QueriesTest, KindRowTakesItsCapabilityOrItsRecentlySeenMovers) {
		load(R"(<query id="bed"><from kind="bed"/><weigh fact="distance" by="-1"/></query>
			<query id="someone"><from kind="colonist"/><weigh fact="distance" by="-1"/></query>)");
		remember({4.0F, 0.0F}, kBed, assets::CapabilityType::Sleepable);
		memory.rememberDynamicEntity(7, human, {2.0F, 0.0F}, 100.0);
		memory.rememberDynamicEntity(8, human, {1.0F, 0.0F}, 10.0);
		world.now = 105.0;
		world.recentSightingGameMinutes = 30.0;

		EXPECT_EQ(pickOf(run("bed")).key, keyOf({4.0F, 0.0F}, kBed));
		const QueryCandidate& someone = pickOf(run("someone"));
		EXPECT_EQ(someone.kind, CandidateKind::Mover);
		EXPECT_EQ(someone.entity, 7U) << "the nearer mover was seen too long ago to count";
	}

	TEST_F(QueriesTest, ItemRowTakesOneDefinition) {
		load(R"(<query id="berries"><from item="Berry"/><weigh fact="distance" by="-1"/></query>)");
		remember({6.0F, 0.0F}, berry, assets::CapabilityType::Carryable);
		remember({2.0F, 0.0F}, stew, assets::CapabilityType::Carryable);
		EXPECT_EQ(pickOf(run("berries")).key, keyOf({6.0F, 0.0F}, berry));
	}

	// A regrowing bush is passed over for a ready one; when every bush regrows the query waits on the nearest.
	TEST_F(QueriesTest, YieldsRowTakesRememberedHarvestablesOfTheItem) {
		load(R"(<query id="bushes"><from yields="Berry"/><weigh fact="distance" by="-1"/></query>
			<query id="food_bushes"><from yields="edible"/><weigh fact="distance" by="-1"/></query>)");
		remember({5.0F, 0.0F}, bush, assets::CapabilityType::Harvestable);
		remember({2.0F, 0.0F}, bush, assets::CapabilityType::Carryable); // not remembered as a harvestable
		remember({1.0F, 0.0F}, berry, assets::CapabilityType::Carryable);

		for (const char* name : {"bushes", "food_bushes"}) {
			const QueryCandidate& found = pickOf(run(name));
			EXPECT_EQ(found.key, keyOf({5.0F, 0.0F}, bush)) << name;
			EXPECT_EQ(found.item, berry) << name;
		}

		regrowUntil(50.0);
		const QueryResult regrowing = run("bushes");
		EXPECT_EQ(regrowing.outcome, QueryOutcome::AllPassedOver);
		EXPECT_EQ(regrowing.waitingOnReason, PassOverReason::Regrowing);
		ASSERT_TRUE(regrowing.waitingOn.has_value());
		EXPECT_EQ(regrowing.waitingOn->key, keyOf({5.0F, 0.0F}, bush));
	}

	TEST_F(QueriesTest, PileRowTakesLoosePilesOfTheItem) {
		load(R"(<query id="piles"><from pile="edible"/><weigh fact="distance" by="-1"/></query>)");
		remember({4.0F, 0.0F}, berry, assets::CapabilityType::Carryable);
		remember({3.0F, 0.0F}, stew, assets::CapabilityType::Carryable);
		remember({1.0F, 0.0F}, bush, assets::CapabilityType::Harvestable);

		const QueryCandidate& pile = pickOf(run("piles"));
		EXPECT_EQ(pile.key, keyOf({3.0F, 0.0F}, stew));
		EXPECT_EQ(pile.item, stew);
		EXPECT_EQ(pile.claimKind, assets::ClaimKind::SourceQuantity);
	}

	TEST_F(QueriesTest, BoxRowTakesEveryBoxAndItemTheColonyKnows) {
		load(R"(<query id="berry_box"><from box="Berry"/><weigh fact="distance" by="-1"/></query>
			<query id="food_box"><from box="edible"/><weigh fact="distance" by="-1"/></query>)");
		const std::vector<KnownBox> boxes = {
			{.box = 50, .key = 500, .position = {5.0F, 0.0F}, .item = berry, .quantity = 3},
			{.box = 50, .key = 500, .position = {5.0F, 0.0F}, .item = stew, .quantity = 2},
			{.box = 51, .key = 510, .position = {2.0F, 0.0F}, .item = stew, .quantity = 1},
			{.box = 52, .key = 520, .position = {1.0F, 0.0F}, .item = berry, .quantity = 4, .packaged = true},
		};
		world.boxes = boxes;

		const QueryResult berries = run("berry_box");
		const QueryCandidate& berryBox = pickOf(berries);
		EXPECT_EQ(berryBox.entity, 50U) << "a packaged box is no source";
		EXPECT_EQ(berryBox.item, berry);
		EXPECT_EQ(berryBox.left, 3U);
		EXPECT_EQ(berries.ranked.size(), 1U);

		QueryRequest asked = request("food_box");
		asked.alternates = 2;
		const QueryResult food = run(asked);
		ASSERT_EQ(food.ranked.size(), 3U) << "a box with two foods is two candidates";
		EXPECT_EQ(food.ranked[0].entity, 51U);
		EXPECT_EQ(std::make_pair(food.ranked[1].entity, food.ranked[1].item), std::make_pair(boxes[0].box, std::min(berry, stew)));
		EXPECT_EQ(std::make_pair(food.ranked[2].entity, food.ranked[2].item), std::make_pair(boxes[0].box, std::max(berry, stew)));

		const std::vector<KnownBox> onlyPackaged = {boxes[3]};
		world.boxes = onlyPackaged;
		const QueryResult packaged = run("berry_box");
		EXPECT_EQ(packaged.outcome, QueryOutcome::AllPassedOver);
		EXPECT_EQ(packaged.waitingOnReason, PassOverReason::Packaged);
	}

	TEST_F(QueriesTest, PlaceHereIsWhereTheCreatureStands) {
		load(R"(<query id="ground"><from place="here"/><weigh fact="distance" by="-1"/></query>)");
		QueryRequest asked = request("ground");
		asked.position = {12.5F, -3.0F};
		const QueryCandidate& ground = pickOf(run(asked));
		EXPECT_EQ(ground.kind, CandidateKind::Place);
		EXPECT_EQ(ground.position, asked.position);
	}

	TEST_F(QueriesTest, PlaceAroundDrawsSeededPointsInTheRing) {
		// A go step names the query: a query only counted may not draw points.
		load(
			R"(<chain id="wander"><go query="ring"/></chain>
			<query id="ring" pick="weighted"><from place="around" minRadius="2" radius="8" count="5"/><exclude fact="distance" above="8"/></query>)"
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
		const QueryCandidate& spot = pickOf(run("spot"));
		EXPECT_EQ(spot.position, glm::vec2(6.0F, 0.0F));
		EXPECT_FLOAT_EQ(spot.score, 3.375F);

		uint32_t inRadius = 0;
		for (int dy = -9; dy <= 9; ++dy) {
			for (int dx = -9; dx <= 9; ++dx) {
				inRadius += glm::length(glm::vec2(static_cast<float>(dx) * 3.0F, static_cast<float>(dy) * 3.0F)) <= 25.0F ? 1U : 0U;
			}
		}
		const QueryCount counted = evaluator->count(request("spot"), world);
		EXPECT_EQ(counted.outcome, QueryOutcome::Found);
		EXPECT_EQ(counted.survivors, inRadius);
	}

	TEST_F(QueriesTest, GoalRowsTakeTheDestinationAndItsFreeSpots) {
		behaviorQuery = false;
		load("", R"(<query id="to_destination"><from goal="destination"/><weigh fact="distance" by="-1"/></query>
			<query id="to_spot"><from goal="spots"/><weigh fact="distance" by="-1"/></query>)");
		const std::vector<glm::vec2> spots = {{9.0F, 1.0F}, {11.0F, 1.0F}, {10.0F, 2.0F}};
		const QueryGoal				 goal{.goalId = 4, .destination = 70, .destinationPosition = {10.0F, 0.0F}, .builderSpots = spots};

		QueryRequest toDestination = request("to_destination");
		toDestination.goal = &goal;
		const QueryCandidate& destination = pickOf(run(toDestination));
		EXPECT_EQ(destination.kind, CandidateKind::Destination);
		EXPECT_EQ(destination.entity, 70U);

		QueryRequest toSpot = request("to_spot");
		toSpot.goal = &goal;
		toSpot.claims.kinds = 1U << static_cast<unsigned>(assets::ClaimKind::Slot);
		EXPECT_EQ(pickOf(run(toSpot)).key, 0U);
		holdClaim(ecs::Claim::slot(70, 0), ecs::TaskType::Build, 4);
		const QueryCandidate& spot = pickOf(run(toSpot));
		EXPECT_EQ(spot.key, 2U) << "the nearest spot is another builder's";
		EXPECT_EQ(spot.claimKind, assets::ClaimKind::Slot);
	}

	// --- Rows ------------------------------------------------------------------------------------------------------

	TEST_F(QueriesTest, WeighAddsEachFactTimesItsWeight) {
		load(R"(<query id="food"><from pile="edible"/><weigh fact="distance" by="-0.1"/><weigh fact="quality" by="4"/></query>)");
		remember({5.0F, 0.0F}, berry, assets::CapabilityType::Carryable);
		remember({20.0F, 0.0F}, stew, assets::CapabilityType::Carryable);
		// Berries -0.5 + 4 x 0.5 = 1.5; stew at 20 m -2 + 4 x 0.75 = 1.
		EXPECT_EQ(pickOf(run("food")).item, berry);
		EXPECT_FLOAT_EQ(pickOf(run("food")).score, 1.5F);

		remember({10.0F, 0.0F}, stew, assets::CapabilityType::Carryable);
		EXPECT_EQ(pickOf(run("food")).key, keyOf({10.0F, 0.0F}, stew)) << "stew at 10 m scores 2";
	}

	TEST_F(QueriesTest, ExcludeRemovesWhatIsBeyondItsLine) {
		load(R"(<query id="near"><from capability="drinkable"/><exclude fact="distance" above="8"/><weigh fact="distance" by="-1"/></query>
			<query id="good_food"><from pile="edible"/><exclude fact="quality" below="0.6"/><weigh fact="distance" by="-1"/></query>)");
		remember({10.0F, 0.0F}, kWater, assets::CapabilityType::Drinkable);
		EXPECT_EQ(run("near").outcome, QueryOutcome::AllExcluded);
		remember({6.0F, 0.0F}, kWater, assets::CapabilityType::Drinkable);
		EXPECT_EQ(pickOf(run("near")).key, keyOf({6.0F, 0.0F}, kWater));

		// A definition-scoped exclusion removes every berry before any is visited.
		for (int i = 0; i < 20; ++i) {
			remember({1.0F + static_cast<float>(i), 1.0F}, berry, assets::CapabilityType::Carryable);
		}
		remember({30.0F, 0.0F}, stew, assets::CapabilityType::Carryable);
		const QueryResult good = run("good_food");
		EXPECT_EQ(pickOf(good).item, stew);
		EXPECT_EQ(good.visits, 1U);
	}

	// Prefer by an item fact at a place (sleep), above a line (distance), and by a definition fact (quality).
	TEST_F(QueriesTest, PreferRanksWhatItWouldExcludeBelowEverythingItKeeps) {
		load(
			R"(<query id="sleep"><from capability="sleepable"/><from place="here"/><prefer fact="sleep_quality" below="1"/><weigh fact="distance" by="-1"/></query>
			<query id="near_first"><from pile="edible"/><prefer fact="distance" above="10"/><weigh fact="quality" by="4"/></query>
			<query id="good_first"><from pile="edible"/><prefer fact="quality" below="0.6"/><weigh fact="distance" by="-1"/></query>)"
		);
		const QueryCandidate& ground = pickOf(run("sleep"));
		EXPECT_EQ(ground.kind, CandidateKind::Place);
		EXPECT_EQ(ground.preferClass, 1U);

		remember({30.0F, 0.0F}, kBed, assets::CapabilityType::Sleepable);
		const QueryResult bed = run("sleep");
		EXPECT_EQ(pickOf(bed).key, keyOf({30.0F, 0.0F}, kBed)) << "any bed before the ground, at any distance";
		EXPECT_EQ(bed.visits, 1U) << "the ground, the lower class, is not walked once a bed survives";

		remember({6.0F, 0.0F}, berry, assets::CapabilityType::Carryable);
		remember({12.0F, 0.0F}, stew, assets::CapabilityType::Carryable);
		EXPECT_EQ(pickOf(run("near_first")).item, berry) << "the better stew is past the line";
		EXPECT_EQ(pickOf(run("good_first")).item, stew) << "the nearer berries are below the quality line";
	}

	TEST_F(QueriesTest, ScaleMultipliesAFactsWeightForACreatureWithTheTrait) {
		load(R"(<query id="food"><from pile="edible"/><weigh fact="distance" by="-1"/><weigh fact="quality" by="4"/>
			<scale trait="gourmand" fact="quality" by="2"/></query>)");
		remember({1.0F, 0.0F}, berry, assets::CapabilityType::Carryable);
		remember({2.5F, 0.0F}, stew, assets::CapabilityType::Carryable);
		EXPECT_EQ(pickOf(run("food")).item, berry) << "-1 + 2 against -2.5 + 3";

		const std::vector<std::string> traits = {"gourmand"};
		QueryRequest				   gourmand = request("food");
		gourmand.traits = traits;
		EXPECT_EQ(pickOf(run(gourmand)).item, stew) << "-1 + 4 against -2.5 + 6";
	}

	// --- Picks -----------------------------------------------------------------------------------------------------

	TEST_F(QueriesTest, PickBestTakesTheHighestScoreThenTheNearestThenTheLowestKey) {
		load(R"(<query id="water"><from capability="drinkable"/><weigh fact="distance" by="-1"/></query>
			<query id="food"><from pile="edible"/><weigh fact="distance" by="-1"/><weigh fact="quality" by="4"/></query>)");
		for (const glm::vec2 position : {glm::vec2{3.0F, 0.0F}, glm::vec2{0.0F, 3.0F}, glm::vec2{-3.0F, 0.0F}, glm::vec2{0.0F, 4.0F}}) {
			remember(position, kWater, assets::CapabilityType::Drinkable);
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

		// Berries at 3 m score -3 + 2, stew at 4 m -4 + 3: equal, and the nearer is first.
		remember({4.0F, 0.0F}, stew, assets::CapabilityType::Carryable);
		remember({0.0F, -3.0F}, berry, assets::CapabilityType::Carryable);
		const QueryCandidate& food = pickOf(run("food"));
		EXPECT_FLOAT_EQ(food.score, -1.0F);
		EXPECT_EQ(food.item, berry);
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
				++picks[static_cast<std::size_t>(pickOf(run("water")).distance)];
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
	// or after the alternates a need's nearest-of round asks for (kNearestOfCount, widened by doubling). A
	// definition-scoped fact with no upper bound (colony_stock) is worked out per definition, so it still leaves
	// each definition a horizon.
	TEST_F(QueriesTest, TheHorizonEndsTheWalkAndAlternatesWidenIt) {
		load(
			R"(<query id="water"><from capability="drinkable"/><weigh fact="distance" by="-1"/></query>
			<query id="water_by_stock"><from capability="drinkable"/><weigh fact="distance" by="-1"/><weigh fact="colony_stock" by="1"/></query>)"
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
		// colony_stock reads 0 for every water (no box holds one), so each definition's bound is its own terms.
		EXPECT_EQ(run("water_by_stock").visits, 1U);
	}

	// A definition with more than 32 occupied cells is walked outward in rings: from beside a block of cells
	// the walk looks in a few rings and finds the nearest; from far away it switches partway to opening every
	// occupied cell left. Both pick what a brute-force sort picks.
	TEST_F(QueriesTest, TheRingWalkFindsWhatABruteForceSortFinds) {
		load(R"(<query id="water"><from capability="drinkable"/><weigh fact="distance" by="-1"/></query>)");
		for (int cellX = 1; cellX <= 10; ++cellX) {
			for (int cellY = 0; cellY < 10; ++cellY) {
				const glm::vec2 position{static_cast<float>(cellX) * 16.0F + 3.0F, static_cast<float>(cellY) * 16.0F + 5.0F};
				remember(position, kWater, assets::CapabilityType::Drinkable);
			}
		}
		const std::size_t occupied = memory.rememberedOfDefinition(kWater)->cells.size();
		ASSERT_GT(occupied, 32U);
		const auto bruteForce = [&](glm::vec2 from) {
			std::vector<std::pair<float, uint64_t>> everything;
			for (const auto& [key, known] : memory.knownWorldEntities) {
				const glm::vec2 offset = known.position - from;
				everything.emplace_back(glm::dot(offset, offset), key);
			}
			std::ranges::sort(everything);
			return everything;
		};
		for (const glm::vec2 from : {glm::vec2{8.0F, 80.0F}, glm::vec2{-3000.0F, -2500.0F}}) {
			QueryRequest asked = request("water");
			asked.position = from;
			asked.alternates = ecs::kNearestOfCount - 1;
			const QueryResult result = run(asked);
			const auto		  expected = bruteForce(from);
			ASSERT_EQ(result.ranked.size(), ecs::kNearestOfCount);
			for (std::size_t index = 0; index < ecs::kNearestOfCount; ++index) {
				EXPECT_EQ(result.ranked[index].key, expected[index].second) << "from (" << from.x << ", " << from.y << ") " << index;
			}
			if (from.x > 0.0F) {
				EXPECT_LT(result.cellsTouched, occupied) << "beside the block, a few rings";
			} else {
				EXPECT_GT(result.cellsTouched, occupied) << "far away, rings cost more, and every occupied cell is opened";
			}
		}
	}

	TEST_F(QueriesTest, AFallbackRowIsTakenOnlyWhenNothingElseSurvivesAndIgnoresTheRows) {
		load(
			R"(<query id="relief"><from capability="toilet"/><from place="here" fallback="true"/><exclude fact="is_place"/><exclude fact="distance" above="20"/><weigh fact="distance" by="-1"/></query>)"
		);
		const QueryCandidate& ground = pickOf(run("relief"));
		EXPECT_TRUE(ground.fallback);
		EXPECT_EQ(ground.kind, CandidateKind::Place) << "no exclusion removes a fallback candidate";

		remember({30.0F, 0.0F}, kToilet, assets::CapabilityType::Toilet);
		EXPECT_TRUE(pickOf(run("relief")).fallback) << "the latrine is excluded, so nothing else survives";
		remember({12.0F, 0.0F}, kToilet, assets::CapabilityType::Toilet);
		const QueryCandidate& latrine = pickOf(run("relief"));
		EXPECT_FALSE(latrine.fallback);
		EXPECT_EQ(latrine.key, keyOf({12.0F, 0.0F}, kToilet));
	}

	// The place facts read 0 on a thing: a latrine by bio piles is no better than a bare one, and a latrine is
	// never on the shore.
	TEST_F(QueriesTest, PlaceFactsReadZeroOnAThing) {
		load(
			R"(<query id="relief"><from capability="toilet"/><weigh fact="distance" by="-1"/><weigh fact="waste_nearby" by="10"/></query>)"
		);
		remember({4.0F, 0.0F}, kToilet, assets::CapabilityType::Toilet);
		remember({9.0F, 0.0F}, kToilet, assets::CapabilityType::Toilet);
		remember({9.0F, 0.5F}, kWaste, assets::CapabilityType::Waste);
		const QueryCandidate& latrine = pickOf(run("relief"));
		EXPECT_EQ(latrine.key, keyOf({4.0F, 0.0F}, kToilet));
		EXPECT_FLOAT_EQ(latrine.score, -4.0F);
		EXPECT_TRUE(onShoreAtDistance(1500.0, library.factTuning[fact::kOnShore])) << "exactly the tuned distance is on the shore";
		EXPECT_FALSE(onShoreAtDistance(1500.5, library.factTuning[fact::kOnShore]));
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
		asked.claims = {.kinds = kUseClaim, .tier = 5};

		holdClaim(ecs::Claim::use(ecs::TargetKind::WorldKey, keyOf(nearBush, bush)), ecs::TaskType::Harvest, 6);
		const QueryCandidate& heldByWork = pickOf(run(asked));
		EXPECT_EQ(heldByWork.key, keyOf(nearBush, bush)) << "work never takes food from a hungry creature";
		EXPECT_FLOAT_EQ(heldByWork.held, 1.0F);
		QueryRequest preferFree = request("free_food");
		preferFree.claims = asked.claims;
		EXPECT_EQ(pickOf(run(preferFree)).key, keyOf(farBush, bush)) << "free food before held food";

		claimWorld.destroyEntity(holders.back());
		holdClaim(ecs::Claim::use(ecs::TargetKind::WorldKey, keyOf(nearBush, bush)), ecs::TaskType::FulfillNeed, 5);
		EXPECT_EQ(pickOf(run(asked)).key, keyOf(farBush, bush));

		const uint64_t farEater =
			holdClaim(ecs::Claim::use(ecs::TargetKind::WorldKey, keyOf(farBush, bush)), ecs::TaskType::FulfillNeed, 3);
		const QueryResult waiting = run(asked);
		EXPECT_EQ(waiting.outcome, QueryOutcome::AllPassedOver);
		ASSERT_TRUE(waiting.waitingOn.has_value());
		EXPECT_EQ(waiting.waitingOn->key, keyOf(nearBush, bush));
		EXPECT_EQ(waiting.waitingOnReason, PassOverReason::Claimed);
		EXPECT_EQ(waiting.visits, 2U);
		const QueryResult stillWaiting = run(asked);
		EXPECT_TRUE(stillWaiting.fromWait);
		EXPECT_EQ(stillWaiting.visits, 0U);
		EXPECT_EQ(stillWaiting.outcome, QueryOutcome::AllPassedOver);

		claims.retire(farEater);
		const QueryResult released = run(asked);
		EXPECT_FALSE(released.fromWait);
		EXPECT_EQ(pickOf(released).key, keyOf(farBush, bush));
	}

	TEST_F(QueriesTest, TheReservedRowIsTheBestTakenCandidateAheadOfThePick) {
		load(R"(<query id="food"><from pile="Berry"/><weigh fact="distance" by="-1"/></query>)");
		const glm::vec2 nearPile{2.0F, 0.0F};
		const glm::vec2 farPile{9.0F, 0.0F};
		remember(nearPile, berry, assets::CapabilityType::Carryable);
		remember(farPile, berry, assets::CapabilityType::Carryable);
		holdClaim(ecs::Claim::sourceQuantity(ecs::TargetKind::WorldKey, keyOf(nearPile, berry), berry, 1), ecs::TaskType::FulfillNeed, 2);
		QueryRequest asked = request("food");
		asked.claims = {.kinds = kSourceClaim, .tier = 5};

		const QueryResult result = run(asked);
		EXPECT_EQ(pickOf(result).key, keyOf(farPile, berry));
		ASSERT_TRUE(result.reserved.has_value());
		EXPECT_EQ(result.reserved->key, keyOf(nearPile, berry));
		EXPECT_EQ(result.reservedBy, holders.front());

		asked.claims.tier = 1;
		EXPECT_EQ(pickOf(run(asked)).key, keyOf(nearPile, berry)) << "a more urgent eater is not held back";
	}

	// A meal's units follow the item's nutrition: 70 points short is 3 berries (30 a unit) but 1 piece of meat
	// (80). Each box holds 2, one of them claimed by a hauler. By the decision's rule (unclaimed < the smaller
	// of the meal and what is left) the berries are held and the meat is free.
	TEST_F(QueriesTest, AMealIsSizedPerItemAndHeldFollowsIt) {
		const uint32_t meat = registerEdible(assets::AssetRegistry::Get(), "Meat", assets::CapabilityQuality::Normal, 0.8F);
		load(R"(<query id="food_box"><from box="edible"/><prefer fact="held"/><weigh fact="distance" by="-1"/></query>)");
		const std::vector<KnownBox> boxes = {
			{.box = 60, .key = 600, .position = {3.0F, 0.0F}, .item = berry, .quantity = 2},
			{.box = 61, .key = 610, .position = {5.0F, 0.0F}, .item = meat, .quantity = 2},
		};
		world.boxes = boxes;
		holdClaim(ecs::Claim::sourceQuantity(ecs::TargetKind::Entity, 60, berry, 1), ecs::TaskType::Haul, 6);
		holdClaim(ecs::Claim::sourceQuantity(ecs::TargetKind::Entity, 61, meat, 1), ecs::TaskType::Haul, 6);

		QueryRequest meal = request("food_box");
		meal.alternates = 1;
		meal.claims = {.kinds = kSourceClaim, .tier = 5, .mealShortfall = 70.0F};
		const QueryResult	  sized = run(meal);
		const QueryCandidate& meatBox = pickOf(sized);
		EXPECT_EQ(meatBox.item, meat);
		EXPECT_FLOAT_EQ(meatBox.held, 0.0F);
		EXPECT_EQ(meatBox.wanted, 1U);
		EXPECT_EQ(meatBox.left, 2U);
		EXPECT_EQ(meatBox.claimQuantity, 1U);
		ASSERT_EQ(sized.ranked.size(), 1U) << "the held berries are in the lower class, not an alternate";

		QueryRequest flat = request("food_box");
		flat.claims = {.kinds = kSourceClaim, .tier = 5, .wanted = 3};
		const QueryCandidate& berryBox = pickOf(run(flat));
		EXPECT_EQ(berryBox.item, berry) << "with one count for every item both read held, and the nearer wins";
		EXPECT_FLOAT_EQ(berryBox.held, 1.0F);
		EXPECT_EQ(berryBox.claimQuantity, 2U);
	}

	// --- The wait --------------------------------------------------------------------------------------------------

	// A wait is the answer to one request: asked for another goal item, or by a creature whose need turned
	// critical (so the eater ahead of it no longer takes the food), the query is walked again.
	TEST_F(QueriesTest, AWaitHoldsOnlyForTheRequestItWasRecordedFor) {
		behaviorQuery = false;
		load(
			R"(<query id="food"><from yields="Berry"/><weigh fact="distance" by="-1"/></query>)",
			R"(<query id="pile_for_goal"><from pile="goal"/><weigh fact="distance" by="-1"/></query>)"
		);
		remember({2.0F, 0.0F}, berry, assets::CapabilityType::Carryable);
		remember({4.0F, 0.0F}, stew, assets::CapabilityType::Carryable);
		holdClaim(ecs::Claim::sourceQuantity(ecs::TargetKind::WorldKey, keyOf({2.0F, 0.0F}, berry), berry, 1), ecs::TaskType::Haul, 6);

		const QueryGoal forBerries{.goalId = 7, .destination = 70, .item = berry};
		const QueryGoal forStew{.goalId = 7, .destination = 70, .item = stew};
		QueryRequest	haul = request("pile_for_goal");
		haul.claims = {.kinds = kSourceClaim, .everyHolderTakes = true};
		haul.goal = &forBerries;
		EXPECT_EQ(run(haul).outcome, QueryOutcome::AllPassedOver);
		EXPECT_TRUE(run(haul).fromWait);
		haul.goal = &forStew;
		const QueryResult stewResult = run(haul);
		EXPECT_FALSE(stewResult.fromWait);
		EXPECT_EQ(pickOf(stewResult).item, stew);

		remember({6.0F, 0.0F}, bush, assets::CapabilityType::Harvestable);
		holdClaim(ecs::Claim::use(ecs::TargetKind::WorldKey, keyOf({6.0F, 0.0F}, bush)), ecs::TaskType::FulfillNeed, 3);
		QueryRequest eat = request("test/food");
		eat.claims = {.kinds = kUseClaim, .tier = 5};
		EXPECT_EQ(run(eat).outcome, QueryOutcome::AllPassedOver);
		EXPECT_TRUE(run(eat).fromWait);
		eat.claims.tier = 2;
		const QueryResult critical = run(eat);
		EXPECT_FALSE(critical.fromWait);
		EXPECT_EQ(pickOf(critical).key, keyOf({6.0F, 0.0F}, bush));
	}

	// Each thing a wait read ends it: the memory, a claim released and a claim added, a box's contents, a verdict,
	// and the source turning ready. Moving does not, for a query with no place rows and no distance bound.
	TEST_F(QueriesTest, EverythingAWaitReadEndsItAndMovingDoesNot) {
		load(R"(<query id="food"><from yields="Berry"/><from box="Stew"/><weigh fact="distance" by="-1"/></query>)");
		remember({3.0F, 0.0F}, bush, assets::CapabilityType::Harvestable);
		remember({8.0F, 0.0F}, bush, assets::CapabilityType::Harvestable);
		// A box's one stew is another eater's, so the claims are read and stamped.
		const std::vector<KnownBox> boxes = {{.box = 55, .key = 550, .position = {20.0F, 0.0F}, .item = stew, .quantity = 1}};
		world.boxes = boxes;
		holdClaim(ecs::Claim::sourceQuantity(ecs::TargetKind::Entity, 55, stew, 1), ecs::TaskType::FulfillNeed, 3);
		regrowUntil(100.0);
		world.now = 10.0;
		ecs::SourceReachMemo reach;
		QueryRequest		 asked = request("food");
		asked.claims = {.kinds = static_cast<uint8_t>(kUseClaim | kSourceClaim), .tier = 5};
		asked.reach = &reach;
		const auto waits = [&] {
			return run(asked).outcome == QueryOutcome::AllPassedOver && run(asked).fromWait;
		};
		const auto walksAgain = [&] {
			return !run(asked).fromWait;
		};

		ASSERT_TRUE(waits());
		asked.position = {40.0F, -25.0F};
		EXPECT_TRUE(run(asked).fromWait) << "moving changes nothing this query read";

		remember({50.0F, 50.0F}, kWater, assets::CapabilityType::Drinkable);
		EXPECT_TRUE(walksAgain()) << "the memory moved";
		ASSERT_TRUE(waits());
		const uint64_t added = holdClaim(ecs::Claim::use(ecs::TargetKind::WorldKey, 99), ecs::TaskType::Haul, 6);
		EXPECT_TRUE(walksAgain()) << "a claim was added";
		ASSERT_TRUE(waits());
		claims.retire(added);
		EXPECT_TRUE(walksAgain()) << "a claim was released";
		ASSERT_TRUE(waits());
		world.boxesStamp = 1;
		EXPECT_TRUE(walksAgain()) << "a box's contents changed";
		ASSERT_TRUE(waits());
		reach.recordVerdict(12345, {.connectivityEpoch = 0, .beliefVersion = memory.beliefVersion}, 0);
		EXPECT_TRUE(walksAgain()) << "a verdict was found";
		ASSERT_TRUE(waits());
		world.now = 100.0;
		const QueryResult ready = run(asked);
		EXPECT_FALSE(ready.fromWait);
		EXPECT_EQ(pickOf(ready).key, keyOf({8.0F, 0.0F}, bush)) << "the sources are ready, and from (40, -25) this one is nearer";
	}

	// A pile's live count is read from the world, and grows with no claim, memory, or box changing, so a pile
	// passed over records no wait.
	TEST_F(QueriesTest, APilePassedOverOnItsLiveCountRecordsNoWait) {
		load(R"(<query id="food"><from pile="Berry"/><weigh fact="distance" by="-1"/></query>)");
		const glm::vec2 pileAt{2.0F, 0.0F};
		remember(pileAt, berry, assets::CapabilityType::Carryable);
		ecs::World			pileWorld;
		ecs::LoosePileIndex piles;
		const ecs::EntityID pile = pileWorld.createEntity();
		pileWorld.addComponent<ecs::ResourceStack>(pile, ecs::ResourceStack{.quantity = 1});
		piles.add(pile, berry, pileAt);
		world.world = &pileWorld;
		world.piles = &piles;
		holdClaim(ecs::Claim::sourceQuantity(ecs::TargetKind::WorldKey, keyOf(pileAt, berry), berry, 1), ecs::TaskType::FulfillNeed, 3);
		QueryRequest asked = request("food");
		asked.claims = {.kinds = kSourceClaim, .tier = 5};

		EXPECT_EQ(run(asked).outcome, QueryOutcome::AllPassedOver);
		pileWorld.getComponent<ecs::ResourceStack>(pile)->quantity += 5;
		const QueryResult dropped = run(asked);
		EXPECT_FALSE(dropped.fromWait);
		EXPECT_EQ(pickOf(dropped).left, 5U);
	}

	TEST_F(QueriesTest, ForgettingACreatureDropsItsWait) {
		load(R"(<query id="food"><from yields="Berry"/><weigh fact="distance" by="-1"/></query>)");
		remember({3.0F, 0.0F}, bush, assets::CapabilityType::Harvestable);
		regrowUntil(100.0);
		EXPECT_EQ(run("food").outcome, QueryOutcome::AllPassedOver);
		EXPECT_TRUE(run("food").fromWait);
		evaluator->forget(kSelf);
		EXPECT_FALSE(run("food").fromWait);
	}

	// What a wait does not stamp, and where it records none. Bushes that only regrow wait across a claim anyone
	// adds (the claims are read only when a claim took a candidate). A mover row records no wait (a sighting moves
	// no memory version), nor does an exclusion on `held` (it read the units wanted and live counts).
	TEST_F(QueriesTest, AWaitStampsClaimsOnlyWhenOneWasReadAndRecordsNoneWhereNothingCoversIt) {
		load(R"(<query id="food"><from yields="Berry"/><weigh fact="distance" by="-1"/></query>
			<query id="someone"><from kind="colonist"/><weigh fact="distance" by="-1"/></query>
			<query id="free_pile"><from pile="Berry"/><exclude fact="held"/><weigh fact="distance" by="-1"/></query>)");
		remember({3.0F, 0.0F}, bush, assets::CapabilityType::Harvestable);
		regrowUntil(100.0);
		EXPECT_EQ(run("food").outcome, QueryOutcome::AllPassedOver);
		holdClaim(ecs::Claim::use(ecs::TargetKind::WorldKey, 99), ecs::TaskType::Haul, 6);
		EXPECT_TRUE(run("food").fromWait) << "no claim took a bush, so the claims were not stamped";

		memory.rememberDynamicEntity(7, human, {2.0F, 0.0F}, 100.0);
		world.now = 105.0;
		world.recentSightingGameMinutes = 30.0;
		holdClaim(ecs::Claim::use(ecs::TargetKind::Entity, 7), ecs::TaskType::FulfillNeed, 3);
		QueryRequest toSomeone = request("someone");
		toSomeone.claims = {.kinds = kUseClaim, .tier = 5};
		EXPECT_EQ(run(toSomeone).outcome, QueryOutcome::AllPassedOver);
		EXPECT_FALSE(run(toSomeone).fromWait);

		remember({2.0F, 1.0F}, berry, assets::CapabilityType::Carryable);
		remember({5.0F, 1.0F}, berry, assets::CapabilityType::Carryable);
		holdClaim(ecs::Claim::sourceQuantity(ecs::TargetKind::WorldKey, keyOf({2.0F, 1.0F}, berry), berry, 1), ecs::TaskType::Haul, 6);
		holdClaim(
			ecs::Claim::sourceQuantity(ecs::TargetKind::WorldKey, keyOf({5.0F, 1.0F}, berry), berry, 1), ecs::TaskType::FulfillNeed, 3
		);
		QueryRequest toPile = request("free_pile");
		toPile.claims = {.kinds = kSourceClaim, .tier = 5};
		EXPECT_EQ(run(toPile).outcome, QueryOutcome::AllPassedOver);
		EXPECT_FALSE(run(toPile).fromWait);
	}

	// --- Gates and the kept target ---------------------------------------------------------------------------------

	// The request's gate answers per definition, before any thing of it is visited: no tool for the bush, no room
	// for the stew, so the berries are taken. It is asked each evaluation; nothing of it is kept.
	TEST_F(QueriesTest, TheWorkGateIsAskedPerDefinitionBeforeAnyThing) {
		load(R"(<query id="food"><from yields="edible"/><from pile="edible"/><weigh fact="distance" by="-1"/></query>)");
		remember({1.0F, 0.0F}, bush, assets::CapabilityType::Harvestable);
		remember({2.0F, 0.0F}, stew, assets::CapabilityType::Carryable);
		remember({5.0F, 0.0F}, berry, assets::CapabilityType::Carryable);
		bool				 haveTool = false;
		bool				 haveRoomForStew = false;
		const auto			 gate = [&](uint32_t definition) {
			  return (definition != bush || haveTool) && (definition != stew || haveRoomForStew);
		};
		QueryRequest asked = request("food");
		asked.gate = DefinitionGate::of(gate);
		const QueryResult gated = run(asked);
		EXPECT_EQ(pickOf(gated).item, berry);
		EXPECT_EQ(gated.visits, 1U);
		haveTool = true;
		EXPECT_EQ(pickOf(run(asked)).key, keyOf({1.0F, 0.0F}, bush));
	}

	// The target the creature heads for is asked first: taken alone while it survives (one visit, as
	// nearestAdmitted's keepKey), and when it is passed over the round goes on to the nearest 8 with 9 visits.
	TEST_F(QueriesTest, TheKeptTargetIsAskedFirstAsNearestAdmittedAsksIt) {
		load(R"(<query id="water"><from capability="drinkable"/><weigh fact="distance" by="-1"/></query>)");
		for (uint32_t i = 0; i < 50; ++i) {
			remember({static_cast<float>(i + 1), 0.0F}, kWater + 100 + i, assets::CapabilityType::Drinkable);
		}
		const uint64_t kept = keyOf({20.0F, 0.0F}, kWater + 119);
		const auto	   mainRound = [&](bool keptAdmitted, uint64_t& admitted) {
			std::vector<ecs::SourceCandidate> scratch;
			std::vector<ecs::SourceCandidate> chosen;
			ecs::collectWithCapability(
				memory,
				assets::CapabilityType::Drinkable,
				{0.0F, 0.0F},
				[](uint64_t, const ecs::KnownWorldEntity&) { return true; },
				scratch
			);
			ecs::nearestAdmitted(
				scratch,
				chosen,
				[&](ecs::SourceCandidate& candidate) {
					++admitted;
					return keptAdmitted || candidate.key != kept;
				},
				ecs::kNearestOfCount,
				kept
			);
			return chosen;
		};
		QueryRequest asked = request("water");
		asked.alternates = ecs::kNearestOfCount - 1;
		asked.keepKey = kept;
		asked.claims = {.kinds = kUseClaim, .tier = 5};

		uint64_t		  admitted = 0;
		const auto		  chosenKept = mainRound(true, admitted);
		const QueryResult keptResult = run(asked);
		ASSERT_EQ(keptResult.ranked.size(), 1U);
		EXPECT_EQ(keptResult.ranked[0].key, kept);
		EXPECT_EQ(chosenKept.size(), 1U);
		EXPECT_EQ(keptResult.visits, admitted);
		EXPECT_EQ(keptResult.visits, 1U);

		holdClaim(ecs::Claim::use(ecs::TargetKind::WorldKey, kept), ecs::TaskType::FulfillNeed, 3);
		admitted = 0;
		const auto		  chosenWithout = mainRound(false, admitted);
		const QueryResult passedOver = run(asked);
		ASSERT_EQ(passedOver.ranked.size(), chosenWithout.size());
		for (std::size_t index = 0; index < chosenWithout.size(); ++index) {
			EXPECT_EQ(passedOver.ranked[index].key, chosenWithout[index].key) << index;
		}
		EXPECT_EQ(passedOver.visits, admitted);
		EXPECT_EQ(passedOver.visits, 9U);
	}

	// A storage box is remembered under the same key the colony files it by: kept, it is still asked as a box.
	// A kept bush held by work lands in the lower prefer class, so it is not taken alone and a free bush wins.
	TEST_F(QueriesTest, AKeptBoxIsAskedAsABoxAndAKeptHeldBushDoesNotEndTheChoice) {
		constexpr uint32_t kBoxDef = 3010;
		load(R"(<query id="food_box"><from box="edible"/><weigh fact="distance" by="-1"/></query>
			<query id="free_food"><from yields="Berry"/><prefer fact="held"/><weigh fact="distance" by="-1"/></query>)");
		const glm::vec2 boxAt{6.0F, 0.0F};
		remember(boxAt, kBoxDef, assets::CapabilityType::Storage);
		const std::vector<KnownBox> boxes = {{.box = 50, .key = keyOf(boxAt, kBoxDef), .position = boxAt, .item = berry, .quantity = 3}};
		world.boxes = boxes;
		QueryRequest toBox = request("food_box");
		toBox.keepKey = keyOf(boxAt, kBoxDef);
		const QueryResult kept = run(toBox);
		EXPECT_EQ(pickOf(kept).entity, 50U);
		EXPECT_EQ(kept.ranked.size(), 1U);

		const glm::vec2 heldBush{2.0F, 0.0F};
		const glm::vec2 freeBush{9.0F, 0.0F};
		remember(heldBush, bush, assets::CapabilityType::Harvestable);
		remember(freeBush, bush, assets::CapabilityType::Harvestable);
		holdClaim(ecs::Claim::use(ecs::TargetKind::WorldKey, keyOf(heldBush, bush)), ecs::TaskType::Harvest, 6);
		QueryRequest eat = request("free_food");
		eat.claims = {.kinds = kUseClaim, .tier = 5};
		eat.keepKey = keyOf(heldBush, bush);
		EXPECT_EQ(pickOf(run(eat)).key, keyOf(freeBush, bush)) << "free food first, at any distance";
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
			for (const auto& [position, def] : things) {
				memory.rememberWorldEntity(
					position, def, static_cast<uint16_t>(bitOf(assets::CapabilityType::Carryable) | bitOf(assets::CapabilityType::Edible))
				);
			}
		}
	} // namespace

	TEST_F(QueriesTest, TenThousandRememberedThingsVisitOnlyTheNearest) {
		const uint32_t cabbage = registerEdible(assets::AssetRegistry::Get(), "Cabbage", assets::CapabilityQuality::Poor, 0.3F);
		load(R"(<query id="food"><from pile="edible"/><weigh fact="distance" by="-1"/></query>)");
		rememberTenThousand(memory, {berry, stew, cabbage}, 11, false);
		ASSERT_EQ(memory.worldEntityCount(), ecs::Memory::kMaxWorldEntities);
		std::size_t occupiedCells = 0;
		for (const uint32_t food : {berry, stew, cabbage}) {
			occupiedCells += memory.rememberedOfDefinition(food)->cells.size();
		}

		QueryRequest asked = request("food");
		asked.alternates = ecs::kNearestOfCount - 1;
		const QueryResult result = run(asked);
		ASSERT_EQ(result.ranked.size(), ecs::kNearestOfCount);
		EXPECT_EQ(result.visits, ecs::kNearestOfCount);
		EXPECT_LT(result.cellsTouched, 400U) << "of " << occupiedCells << " occupied cells: the walk goes out in rings";
		RecordProperty("cellsTouched", static_cast<int>(result.cellsTouched));
		RecordProperty("occupiedCells", static_cast<int>(occupiedCells));

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
			<weigh fact="distance" by="-1"/><weigh fact="quality" by="2"/></query>
			<query id="spot"><from place="grid" spacing="3" radius="25"/><weigh fact="distance" by="-1"/><weigh fact="waste_nearby" by="10"/><weigh fact="food_nearby" by="-20"/></query>)"
		);
		rememberTenThousand(memory, {berry, stew}, 5, false);
		remember({4.0F, 4.0F}, kWaste, assets::CapabilityType::Waste);
		holdClaim(ecs::Claim::use(ecs::TargetKind::WorldKey, 1), ecs::TaskType::Harvest, 6);
		const std::vector<KnownBox> boxes = {{.box = 50, .key = 500, .position = {3.0F, 3.0F}, .item = berry, .quantity = 2}};
		world.boxes = boxes;
		QueryRequest food = request("food");
		food.alternates = ecs::kNearestOfCount - 1;
		food.claims = {.kinds = kSourceClaim, .tier = 5, .mealShortfall = 40.0F};
		const QueryRequest spot = request("spot");
		// A gate that refuses stew, made from a lambda per evaluation, and a kept target that is gone, so it is
		// looked for and the walk goes on, as the decision asks.
		uint32_t   refused = 0;
		const auto noStew = [&refused, this](uint32_t definition) {
			refused += definition == stew ? 1U : 0U;
			return definition != stew;
		};
		const auto gatedAndKept = [&] {
			QueryRequest asked = food;
			asked.gate = DefinitionGate::of(noStew);
			asked.keepKey = 501;
			return run(asked);
		};

		for (int warmUp = 0; warmUp < 2; ++warmUp) {
			(void)run(food);
			(void)run(spot);
			(void)gatedAndKept();
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
			found += gatedAndKept().outcome == QueryOutcome::Found ? 1U : 0U;
		}
		countAllocations = false;
		EXPECT_EQ(found, 150U);
		EXPECT_GT(refused, 0U) << "the gate was asked";
		EXPECT_EQ(allocationsCounted, 0U);
	}

	// The same memory filled in another order: the same picks, keys, and scores to the bit, with exact ties among
	// mirrored piles broken by key.
	TEST_F(QueriesTest, TheSameMemoryFilledInAnotherOrderGivesTheSameResult) {
		load(R"(<query id="food"><from pile="edible"/><weigh fact="distance" by="-1"/><weigh fact="quality" by="1"/></query>
			<query id="spot"><from place="grid" spacing="3" radius="25"/><weigh fact="distance" by="-1"/><weigh fact="food_nearby" by="-20"/></query>
			<query id="mirrored"><from capability="drinkable"/><weigh fact="distance" by="-1"/></query>)");
		ecs::Memory shuffled;
		rememberTenThousand(memory, {berry, stew}, 3, false);
		rememberTenThousand(shuffled, {berry, stew}, 3, true);
		const std::vector<glm::vec2> mirror = {{13.0F, -2.0F}, {13.0F, -12.0F}, {8.0F, -7.0F}, {18.0F, -7.0F}};
		for (std::size_t index = 0; index < mirror.size(); ++index) {
			memory.rememberWorldEntity(mirror[index], kWater, bitOf(assets::CapabilityType::Drinkable));
			shuffled.rememberWorldEntity(mirror[mirror.size() - 1 - index], kWater, bitOf(assets::CapabilityType::Drinkable));
		}
		ASSERT_EQ(memory.worldEntityCount(), shuffled.worldEntityCount());

		for (const char* name : {"food", "spot", "mirrored"}) {
			QueryRequest asked = request(name);
			asked.position = {13.0F, -7.0F};
			asked.alternates = 5;
			const QueryResult				  first = run(asked);
			const std::vector<QueryCandidate> inOrder(first.ranked.begin(), first.ranked.end());
			asked.memory = &shuffled;
			asked.creature = kSelf + 1;
			const QueryResult other = run(asked);
			ASSERT_EQ(other.ranked.size(), inOrder.size()) << name;
			for (std::size_t index = 0; index < inOrder.size(); ++index) {
				EXPECT_EQ(other.ranked[index].key, inOrder[index].key) << name << " " << index;
				EXPECT_EQ(other.ranked[index].position, inOrder[index].position) << name << " " << index;
				EXPECT_EQ(other.ranked[index].score, inOrder[index].score) << name << " " << index << ": scores equal to the bit";
			}
			if (std::string_view(name) == "mirrored") {
				ASSERT_EQ(inOrder.size(), 4U);
				for (std::size_t index = 1; index < inOrder.size(); ++index) {
					EXPECT_EQ(inOrder[index].score, inOrder[0].score) << "all four tie";
					EXPECT_LT(inOrder[index - 1].key, inOrder[index].key) << "ties go by key";
				}
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

	// --- What is not evaluated -----------------------------------------------------------------------------------

	// A condition on a row is not evaluated; an exclude or prefer row on a fact nothing answers would decide on a
	// value the sim does not have; both name the row and walk nothing. A weigh row on such a fact reads 0 for
	// every candidate, and the load log names each row.
	TEST_F(QueriesTest, RowsThatCannotBeEvaluatedAreNamedAndWalkNothing) {
		load(
			R"xml(<query id="seat"><from capability="sleepable"/><weigh fact="distance" by="-1"/><weigh fact="comfort" by="3" when="need(energy) &lt; 30"/></query>
			<query id="fresh_food"><from pile="edible"/><weigh fact="distance" by="-1"/><exclude fact="freshness" below="0.5"/></query>
			<query id="own_bed"><from kind="bed"/><exclude fact="owned_by_self" below="1"/></query>
			<query id="meaty"><from pile="edible"/><weigh fact="distance" by="-1"/><weigh fact="contains(meat)" by="5"/></query>)xml"
		);
		remember({2.0F, 0.0F}, kBed, assets::CapabilityType::Sleepable);
		remember({3.0F, 0.0F}, berry, assets::CapabilityType::Carryable);

		const QueryResult seat = run("seat");
		EXPECT_EQ(seat.outcome, QueryOutcome::ConditionNotEvaluated);
		EXPECT_EQ(seat.row, 2U);
		EXPECT_TRUE(seat.ranked.empty());
		EXPECT_EQ(evaluator->count(request("seat"), world).outcome, QueryOutcome::ConditionNotEvaluated);

		const QueryResult fresh = run("fresh_food");
		EXPECT_EQ(fresh.outcome, QueryOutcome::FactNotAnswered);
		EXPECT_EQ(fresh.row, 2U);
		EXPECT_EQ(fresh.visits, 0U);
		const QueryCount ownBeds = evaluator->count(request("own_bed"), world);
		EXPECT_EQ(ownBeds.outcome, QueryOutcome::FactNotAnswered);
		EXPECT_EQ(ownBeds.row, 1U);
		EXPECT_FLOAT_EQ(pickOf(run("meaty")).score, -3.0F);

		const std::string file = "[behaviors] test/sample.xml: <query id=\"";
		const std::string readsZero = " is answered by nothing, so it reads 0 for every candidate";
		const std::string notAnswered = " is answered by nothing, so the query answers that the fact is not answered";
		EXPECT_EQ(
			unansweredFactUses(library),
			(std::vector<std::string>{
				file + "seat\"> row 3 <weigh> fact \"comfort\"" + readsZero,
				file + "fresh_food\"> row 3 <exclude> fact \"freshness\"" + notAnswered,
				file + "own_bed\"> row 2 <exclude> fact \"owned_by_self\"" + notAnswered,
				file + "meaty\"> row 3 <weigh> fact \"contains\"" + readsZero,
			})
		);
	}

	// --- Counting and the definition facts ------------------------------------------------------------------------

	TEST_F(QueriesTest, CountOfCountsWhatSurvivesTheExclusions) {
		load(R"(<query id="free_water"><from capability="drinkable"/><exclude fact="held"/></query>)");
		for (int i = 0; i < 3; ++i) {
			remember({static_cast<float>(i) * 100.0F, 0.0F}, kWater, assets::CapabilityType::Drinkable);
		}
		EXPECT_EQ(evaluator->count(request("free_water"), world).survivors, 3U);
		holdClaim(ecs::Claim::use(ecs::TargetKind::WorldKey, keyOf({100.0F, 0.0F}, kWater)), ecs::TaskType::Haul, 6);
		EXPECT_EQ(evaluator->count(request("free_water"), world).survivors, 2U);
	}

	// A harvestable's definition facts read its yield's definition; colony_stock reads the boxes the colony knows,
	// and the cached definition terms are worked out again when the boxes move.
	TEST_F(QueriesTest, DefinitionFactsReadTheYieldAndTheColonysStock) {
		load(R"(<query id="bush_quality"><from yields="Berry"/><weigh fact="distance" by="-1"/><weigh fact="quality" by="4"/></query>
			<query id="stocked"><from pile="edible"/><weigh fact="distance" by="-1"/><weigh fact="colony_stock" by="-1"/></query>)");
		remember({1.0F, 0.0F}, bush, assets::CapabilityType::Harvestable);
		EXPECT_FLOAT_EQ(pickOf(run("bush_quality")).score, -1.0F + 4.0F * 0.5F);

		remember({1.0F, 0.0F}, berry, assets::CapabilityType::Carryable);
		remember({3.0F, 0.0F}, stew, assets::CapabilityType::Carryable);
		EXPECT_EQ(pickOf(run("stocked")).item, berry);
		const std::vector<KnownBox> boxes = {{.box = 50, .key = 500, .position = {9.0F, 0.0F}, .item = berry, .quantity = 5}};
		world.boxes = boxes;
		world.boxesStamp = 1;
		const QueryCandidate& stocked = pickOf(run("stocked"));
		EXPECT_EQ(stocked.item, stew) << "berries the colony holds five of score -6, stew at 3 m -3";
		EXPECT_FLOAT_EQ(stocked.score, -3.0F);
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
		EXPECT_EQ(pickOf(run(asked)).entity, 51U) << "only a box strictly below the destination's priority is pulled from";
	}

} // namespace queries::test
