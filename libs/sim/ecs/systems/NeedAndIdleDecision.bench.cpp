// The colonist AI's need and idle decisions, the paths behavior-data-migration.md moves to data, timed on
// the hand-written code so the replacement can be held to it. Headless, as AIDecision.bench.cpp's are: no
// nav system, so every remembered source counts as reachable and no route is asked for, and no
// ActionSystem, so a remembered loose pile has no live count and skips the pile index's lookups. The
// routed half of a need decision is BM_NeedDecisionRouted's (NavigationSystem.bench.cpp). Development
// builds include the claim index validator the update runs. Every timed update also includes the
// suspended-work refresh, the claim index rebuild, and the whole decision trace. Logging is silenced for
// each bench's run (QuietLogs): every category at Warning, so no Info line is printed in the timed
// update, though a log call still formats its message. Each scenario builds its own World and clears the
// goal registry, so scenarios are independent of each other and of --benchmark_filter.
//
// BM_NeedDecision: range(0) colonists, hungry and thirsty (40 and 35, under both seek thresholds and
// neither urgent), each remembering the same full memory within 100 m of the colony: 8,500 berry bushes
// and 1,000 loose piles of berries (9,500 edible sources), and 500 water points. Memory holds 10,000 world
// entities (Memory::kMaxWorldEntities). No goal exists, so the decision is the needs path and the idle
// options. After an untimed first decision, every timed update re-decides every colonist (as
// BM_WorkDecision does). range(1) picks the path:
// - 0, kept: he is heading for a bush for Hunger, so Hunger asks to admit only that one, and the decision
//   keeps his task. Collecting the candidates still walks every remembered edible source and water; only
//   the admits are short. The run fails if a task's instance changed, or (development builds) if the
//   update's need-source admits are not one per colonist for Hunger and kNearestOfCount for Thirst.
// - 1, fresh: his task is cleared before each update (untimed), the state ActionSystem leaves when an
//   action completes, so every need walks its nearest sources from scratch and he takes a new task, which
//   adds building its reason string and recording its claims. The run fails if a task's instance did not change,
//   or (development builds) if the admits are fewer than kNearestOfCount for each need of each colonist.
// Either fails if a colonist did not decide, is not serving the need he served at the first decision, or
// has no source.
//
// BM_IdleDecision: range(0) colonists with every need met and nothing to do, each remembering range(1)
// world entities: trees no goal wants and a tenth as many water points. The needs read their sources by
// capability or by def and none is a tree, so only the waters, a tenth of memory, are scanned, twice: the
// water is drinkable and washable, so Thirst and Hygiene each scan it. After a first decision every
// colonist wanders, and a wander that is walking decides every frame (shouldReEvaluate), so no decision
// is asked for: the run fails if a timed update left one undecided or one stopped wandering. Nothing moves
// him, so each decision keeps the wander's point and probes none. The wander is offered headless only
// because buildDecisionTrace's canNavigate lets a world with no nav system wander; the bench depends on it.
//
// Both report the update's mean, p99, and worst time, its mean per colonist, and the options in the
// first colonist's decision trace after the last decision.

#include "AIDecisionSystem.h"
#include "ShippedPriorityConfig.test.h"

#include "../GoalTaskRegistry.h"
#include "../ShippedConfig.test.h"
#include "../World.h"
#include "../components/AgentRadius.h"
#include "../components/Colonist.h"
#include "../components/DecisionTrace.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/MemoryQueries.h"
#include "../components/Movement.h"
#include "../components/Needs.h"
#include "../components/SourceReachMemo.h"
#include "../components/Task.h"
#include "../components/Transform.h"

#include <assets/AssetRegistry.h>
#include <assets/RecipeRegistry.h>
#include <utils/Log.h>

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace {

	using Clock = std::chrono::steady_clock;

	// Every log category at Warning for a bench's run, the levels before it put back after. Initialized
	// first, so the levels don't depend on whether another bench initialized the logger.
	class QuietLogs {
	  public:
		QuietLogs() {
			foundation::Logger::initialize();
			for (std::size_t category = 0; category < saved.size(); ++category) {
				saved[category] = foundation::Logger::getLevel(static_cast<foundation::LogCategory>(category));
				foundation::Logger::setLevel(static_cast<foundation::LogCategory>(category), foundation::LogLevel::Warning);
			}
		}
		~QuietLogs() {
			for (std::size_t category = 0; category < saved.size(); ++category) {
				foundation::Logger::setLevel(static_cast<foundation::LogCategory>(category), saved[category]);
			}
		}
		QuietLogs(const QuietLogs&) = delete;
		QuietLogs& operator=(const QuietLogs&) = delete;

	  private:
		std::array<foundation::LogLevel, static_cast<std::size_t>(foundation::LogCategory::Count)> saved{};
	};

#ifdef DEVELOPMENT_BUILD
	// Reads AIDecisionSystem's private needSourceVisits without editing its class: a member pointer named
	// in an explicit instantiation's template arguments is exempt from access checking
	// ([temp.spec.general]), as AIDecision.bench.cpp's Expose.
	struct NeedVisits {
		static inline std::uint64_t (*read)(const ecs::AIDecisionSystem&) = nullptr;
	};
	template <auto Member>
	struct ExposeNeedVisits {
		static std::uint64_t	 read(const ecs::AIDecisionSystem& decisions) { return decisions.*Member; }
		static inline const bool registered = (NeedVisits::read = &read, true);
	};
	template struct ExposeNeedVisits<&ecs::AIDecisionSystem::needSourceVisits>;
#endif

	constexpr float kTickSeconds = 1.0F / 60.0F;
	constexpr int	kWarmup = 3;
	constexpr int	kDecisions = 200;

	constexpr const char* kBerryDef = "BenchNeed_Berry";
	constexpr const char* kBushDef = "BenchNeed_BerryBush";
	constexpr const char* kWaterDef = "BenchNeed_Water";
	constexpr const char* kWoodDef = "BenchIdle_Wood";
	constexpr const char* kTreeDef = "BenchIdle_Tree";

	std::uint32_t hashU32(std::uint32_t value) {
		value ^= value >> 16U;
		value *= 0x7feb352dU;
		value ^= value >> 15U;
		value *= 0x846ca68bU;
		value ^= value >> 16U;
		return value;
	}

	double atPercentile(std::vector<double> values, double percentile) {
		if (values.empty()) {
			return 0.0;
		}
		std::sort(values.begin(), values.end());
		return values[std::min(values.size() - 1, static_cast<std::size_t>(percentile * static_cast<double>(values.size())))];
	}

	double meanOf(const std::vector<double>& values) {
		double sum = 0.0;
		for (const double value : values) {
			sum += value;
		}
		return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
	}

	// The defs both benches remember, registered once.
	void registerDefs() {
		static const bool registered = [] {
			auto& registry = assets::AssetRegistry::Get();

			assets::AssetDefinition berry;
			berry.defName = kBerryDef;
			berry.label = kBerryDef;
			berry.category = assets::ItemCategory::Food;
			berry.capabilities.carryable = assets::CarryableCapability{1};
			berry.itemProperties = assets::ItemProperties{};
			berry.itemProperties->stackSize = 20;
			berry.itemProperties->massKg = 0.05F;
			berry.itemProperties->edible = assets::EdibleCapability{0.3F, assets::CapabilityQuality::Normal, true};
			registry.registerTestDefinition(std::move(berry));

			assets::AssetDefinition bush;
			bush.defName = kBushDef;
			bush.label = kBushDef;
			bush.capabilities.harvestable = assets::HarvestableCapability{};
			bush.capabilities.harvestable->yieldDefName = kBerryDef;
			bush.capabilities.harvestable->destructive = false;
			bush.capabilities.harvestable->regrowthTime = 600.0F;
			registry.registerTestDefinition(std::move(bush));

			assets::AssetDefinition water;
			water.defName = kWaterDef;
			water.label = kWaterDef;
			water.capabilities.drinkable = assets::DrinkableCapability{};
			water.capabilities.washable = assets::WashableCapability{};
			registry.registerTestDefinition(std::move(water));

			assets::AssetDefinition wood;
			wood.defName = kWoodDef;
			wood.label = kWoodDef;
			wood.handsRequired = 2;
			wood.category = assets::ItemCategory::RawMaterial;
			wood.capabilities.carryable = assets::CarryableCapability{1};
			wood.itemProperties = assets::ItemProperties{};
			wood.itemProperties->stackSize = 40;
			wood.itemProperties->massKg = 2.5F;
			registry.registerTestDefinition(std::move(wood));

			assets::AssetDefinition tree;
			tree.defName = kTreeDef;
			tree.label = kTreeDef;
			tree.capabilities.harvestable = assets::HarvestableCapability{};
			tree.capabilities.harvestable->yieldDefName = kWoodDef;
			registry.registerTestDefinition(std::move(tree));
			return true;
		}();
		(void)registered;
	}

	// Remembers `count` of `defName` at random points within 100 m of the origin, each in its own memory
	// cell (Memory keys a world entity by its decimeter cell and def). False if one could not be placed.
	bool rememberAround(ecs::Memory& memory, const char* defName, int count, std::uint32_t seed) {
		const auto&	   registry = assets::AssetRegistry::Get();
		const uint32_t defId = registry.getDefNameId(defName);
		const uint16_t mask = registry.getCapabilityMask(defId);
		const auto	   unit = [&seed] {
			seed = hashU32(seed + 0x9e3779b9U);
			return static_cast<float>(seed & 0xFFFFFFU) / static_cast<float>(0xFFFFFFU);
		};
		int placed = 0;
		for (int draws = 0; placed < count && draws < 2 * count; ++draws) {
			const glm::vec2 at{(unit() - 0.5F) * 200.0F, (unit() - 0.5F) * 200.0F};
			placed += memory.rememberWorldEntity(at, defId, mask) ? 1 : 0;
		}
		return placed == count;
	}

	// A colonist at `home` with the shipped needs, each at `needValue`, and `memory`.
	ecs::EntityID addColonist(ecs::World& world, glm::vec2 home, float needValue, const ecs::Memory& memory) {
		const ecs::EntityID colonist = world.createEntity();
		world.addComponent<ecs::Position>(colonist, ecs::Position{home});
		world.addComponent<ecs::Velocity>(colonist, ecs::Velocity{{0.0F, 0.0F}});
		world.addComponent<ecs::MovementTarget>(colonist, ecs::MovementTarget{{0.0F, 0.0F}, 2.0F, false});
		ecs::NeedsComponent needs = ecs::testing::shippedNeeds();
		for (const ecs::NeedType need : ecs::NeedsComponent::kActionableNeeds) {
			needs.get(need).value = needValue;
		}
		world.addComponent<ecs::NeedsComponent>(colonist, needs);
		world.addComponent<ecs::Memory>(colonist, memory);
		world.addComponent<ecs::Inventory>(colonist, ecs::Inventory::createForColonist());
		world.addComponent<ecs::Task>(colonist, ecs::Task{});
		world.addComponent<ecs::DecisionTrace>(colonist, ecs::DecisionTrace{});
		world.addComponent<ecs::SourceReachMemo>(colonist, ecs::SourceReachMemo{});
		world.addComponent<ecs::Colonist>(colonist, ecs::Colonist{"Bencher"});
		world.addComponent<ecs::AgentRadius>(colonist, ecs::AgentRadius{0.3F, 1.0F});
		return colonist;
	}

	struct UpdateTimes {
		std::vector<double> microseconds;
		bool				allDecided = true;
	};

	// kWarmup and then kDecisions updates, each timed; `prepare` runs before each, `decided` after it.
	template <typename Prepare, typename Decided>
	UpdateTimes timeUpdates(ecs::AIDecisionSystem& decisions, Prepare&& prepare, Decided&& decided) {
		UpdateTimes times;
		times.microseconds.reserve(kDecisions);
		for (int index = 0; index < kWarmup + kDecisions && times.allDecided; ++index) {
			prepare();
			const Clock::time_point start = Clock::now();
			decisions.update(kTickSeconds);
			const double elapsed = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
			times.allDecided = decided();
			if (index >= kWarmup) {
				times.microseconds.push_back(elapsed);
			}
		}
		return times;
	}

	void report(benchmark::State& state, const UpdateTimes& times, int colonistCount, const ecs::DecisionTrace& firstTrace) {
		state.counters["colonists"] = colonistCount;
		state.counters["updateMeanUs"] = meanOf(times.microseconds);
		state.counters["updateP99Us"] = atPercentile(times.microseconds, 0.99);
		state.counters["updateMaxUs"] = atPercentile(times.microseconds, 1.0);
		state.counters["perColonistMeanUs"] = meanOf(times.microseconds) / static_cast<double>(colonistCount);
		state.counters["options"] = static_cast<double>(firstTrace.options.size());
	}

	void BM_NeedDecision(benchmark::State& state) {
		constexpr int	kBushes = 8500;
		constexpr int	kPiles = 1000;
		constexpr int	kWaters = 500;
		const int		colonistCount = static_cast<int>(state.range(0));
		const bool		freshPick = state.range(1) == 1;
		const QuietLogs quiet;
		registerDefs();
		if (!ecs::loadShippedPriorityConfig()) {
			state.SkipWithError("priority-tuning.xml did not load");
			return;
		}

		ecs::Memory remembered;
		if (!rememberAround(remembered, kBushDef, kBushes, 41) || !rememberAround(remembered, kBerryDef, kPiles, 43) ||
			!rememberAround(remembered, kWaterDef, kWaters, 47)) {
			state.SkipWithError("could not place every source in its own memory cell");
			return;
		}

		for (auto _ : state) {
			ecs::GoalTaskRegistry::Get().clear();
			ecs::World			   world;
			ecs::AIDecisionSystem& decisions =
				world.registerSystem<ecs::AIDecisionSystem>(assets::AssetRegistry::Get(), assets::RecipeRegistry::Get(), 1234U);
			std::vector<ecs::EntityID> colonists;
			std::vector<glm::vec2>	   homes;
			for (int index = 0; index < colonistCount; ++index) {
				const glm::vec2		home{static_cast<float>(index) * 2.0F, 0.0F};
				const ecs::EntityID colonist = addColonist(world, home, 100.0F, remembered);
				auto&				needs = *world.getComponent<ecs::NeedsComponent>(colonist);
				needs.get(ecs::NeedType::Hunger).value = 40.0F;
				needs.get(ecs::NeedType::Thirst).value = 35.0F;
				colonists.push_back(colonist);
				homes.push_back(home);
			}
			decisions.update(kTickSeconds);

			// Each colonist's task instance and the need it serves after the last update: kept is the same
			// instance, fresh a new one serving the same need.
			std::vector<std::uint64_t> instances;
			std::vector<ecs::NeedType> served;
			for (const ecs::EntityID colonist : colonists) {
				const ecs::Task& task = *world.getComponent<ecs::Task>(colonist);
				instances.push_back(task.instanceId);
				served.push_back(task.needToFulfill);
			}
			std::vector<double> needVisits;
			std::uint64_t		visitsBefore = 0;
			const UpdateTimes	times = timeUpdates(
				  decisions,
				  [&] {
					  for (std::size_t index = 0; index < colonists.size(); ++index) {
						  world.getComponent<ecs::Position>(colonists[index])->value = homes[index];
						  ecs::Task& task = *world.getComponent<ecs::Task>(colonists[index]);
						  if (freshPick) {
							  task.clear();
						  }
						  ecs::requestDecision(task);
					  }
#ifdef DEVELOPMENT_BUILD
					visitsBefore = NeedVisits::read(decisions);
#endif
				},
				[&] {
					bool pathHeld = true;
					for (std::size_t index = 0; index < colonists.size(); ++index) {
						const ecs::Task& task = *world.getComponent<ecs::Task>(colonists[index]);
						const bool		 newPick = task.instanceId != 0 && task.instanceId != instances[index];
						const bool		 onThePath = freshPick ? newPick : task.instanceId == instances[index];
						pathHeld = pathHeld && onThePath && task.needToFulfill == served[index] && task.needSourceKey != 0 &&
								   task.timeSinceEvaluation <= 1.0F && task.type == ecs::TaskType::FulfillNeed;
						instances[index] = task.instanceId;
					}
#ifdef DEVELOPMENT_BUILD
					// Kept: each colonist serves Hunger and asks only about the bush he heads for, and
					// Thirst, not served, walks its nearest kNearestOfCount waters. Fresh: both needs walk
					// at least their nearest kNearestOfCount (Hunger more, past bushes other colonists hold).
					const std::uint64_t visits = NeedVisits::read(decisions) - visitsBefore;
					const auto			walk = static_cast<std::uint64_t>(ecs::kNearestOfCount);
					const auto			deciders = static_cast<std::uint64_t>(colonists.size());
					const bool			allHunger =
						std::all_of(served.begin(), served.end(), [](ecs::NeedType need) { return need == ecs::NeedType::Hunger; });
					pathHeld = pathHeld && (freshPick ? visits >= deciders * 2 * walk : allHunger && visits == deciders * (1 + walk));
					needVisits.push_back(static_cast<double>(visits));
#endif
					return pathHeld;
				}
			);
			if (!times.allDecided) {
				state.SkipWithError(
					freshPick ? "an update did not make every colonist a new pick serving a need"
							  : "an update did not keep every colonist's task serving a need"
				);
				break;
			}
			report(state, times, colonistCount, *world.getComponent<ecs::DecisionTrace>(colonists.front()));
			state.counters["freshPick"] = freshPick ? 1.0 : 0.0;
			state.counters["needVisitsMin"] = needVisits.empty() ? 0.0 : *std::min_element(needVisits.begin(), needVisits.end());
			state.counters["needVisitsMax"] = needVisits.empty() ? 0.0 : *std::max_element(needVisits.begin(), needVisits.end());
			std::size_t hungerServed = 0;
			for (const ecs::NeedType need : served) {
				hungerServed += need == ecs::NeedType::Hunger ? 1 : 0;
			}
			state.counters["servingHunger"] = static_cast<double>(hungerServed);
			state.counters["edibleSources"] = kBushes + kPiles;
			state.counters["waterSources"] = kWaters;
		}
		ecs::GoalTaskRegistry::Get().clear();
	}

	void BM_IdleDecision(benchmark::State& state) {
		const int		colonistCount = static_cast<int>(state.range(0));
		const int		remembered = static_cast<int>(state.range(1));
		const int		waters = remembered / 10;
		const QuietLogs quiet;
		registerDefs();
		if (!ecs::loadShippedPriorityConfig()) {
			state.SkipWithError("priority-tuning.xml did not load");
			return;
		}

		ecs::Memory memory;
		if (!rememberAround(memory, kTreeDef, remembered - waters, 53) || !rememberAround(memory, kWaterDef, waters, 59)) {
			state.SkipWithError("could not place every entity in its own memory cell");
			return;
		}

		for (auto _ : state) {
			ecs::GoalTaskRegistry::Get().clear();
			ecs::World			   world;
			ecs::AIDecisionSystem& decisions =
				world.registerSystem<ecs::AIDecisionSystem>(assets::AssetRegistry::Get(), assets::RecipeRegistry::Get(), 1234U);
			std::vector<ecs::EntityID> colonists;
			for (int index = 0; index < colonistCount; ++index) {
				colonists.push_back(addColonist(world, {static_cast<float>(index) * 2.0F, 0.0F}, 100.0F, memory));
			}
			decisions.update(kTickSeconds);

			const UpdateTimes times = timeUpdates(
				decisions,
				[] {},
				[&] {
					return std::all_of(colonists.begin(), colonists.end(), [&](ecs::EntityID colonist) {
						const ecs::Task& task = *world.getComponent<ecs::Task>(colonist);
						return task.timeSinceEvaluation == 0.0F && task.type == ecs::TaskType::Wander;
					});
				}
			);
			if (!times.allDecided) {
				state.SkipWithError("an update left a colonist undecided or not wandering");
				break;
			}
			report(state, times, colonistCount, *world.getComponent<ecs::DecisionTrace>(colonists.front()));
			state.counters["remembered"] = remembered;
			state.counters["waters"] = waters;
		}
		ecs::GoalTaskRegistry::Get().clear();
	}

} // namespace

BENCHMARK(BM_NeedDecision)->ArgsProduct({{1, 8}, {0, 1}})->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_IdleDecision)->ArgsProduct({{1, 8}, {1000, 10000}})->Iterations(1)->Unit(benchmark::kMillisecond);
