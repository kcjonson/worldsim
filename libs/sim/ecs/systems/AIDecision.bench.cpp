// The colonist AI's work decision and the claim index it reads (task-claims.md, Performance
// requirements), headless: no nav system, so every remembered source counts as reachable, as in the
// AIDecisionSystem tests. Each scenario builds its own World and clears the goal registry, so
// scenarios are independent of each other and of --benchmark_filter.
//
// BM_ClaimIndexRebuild: 200 colonists across 300 goals, each holding a task with claims of the
// usual shapes, half a harvest carried in (Use on a tree, Inbound at the goal's destination,
// InFlight toward its root), half a fetch from stock (SourceQuantity on a box, Inbound, InFlight),
// and every fourth with a suspended record (Inbound, InFlight). Times one warm ClaimIndex::rebuild.
// A warm rebuild allocates only if its entry, group, or table storage moves or grows, so each
// rebuild compares their data pointers and capacities before and after (storageChanges).
//
// BM_WorkDecision: range(0) colonists, each remembering the same range(1) trees that yield wood,
// with three goals that want wood: a box stocking it by cutting, a craft whose recipe needs it, and
// a build site that needs it. Every timed AIDecisionSystem::update re-decides every colonist (as
// BM_NeedDecision does), and fails the run if one did not. Development builds include the claim
// index validator the update runs. Reports the options in the first colonist's decision trace
// after the last decision, all of them and the harvests among them.

#include "AIDecisionSystem.h"

#include "../ClaimIndex.h"
#include "../GoalTaskRegistry.h"
#include "../World.h"
#include "../components/AgentRadius.h"
#include "../components/Appearance.h"
#include "../components/Colonist.h"
#include "../components/DecisionTrace.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/Movement.h"
#include "../components/Needs.h"
#include "../components/SourceReachMemo.h"
#include "../components/StorageConfiguration.h"
#include "../components/StructureBlueprint.h"
#include "../components/Task.h"
#include "../components/Transform.h"
#include "../components/WorkQueue.h"

#include <assets/AssetRegistry.h>
#include <assets/RecipeRegistry.h>

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace {

	struct Storage {
		const void* data	 = nullptr;
		std::size_t capacity = 0;

		bool operator==(const Storage&) const = default;
	};

	template <typename V> Storage storageOf(const V& v) { return {v.data(), v.capacity()}; }

	// Reads a private member's storage without editing its class: a member pointer named in an explicit
	// instantiation's template arguments is exempt from access checking ([temp.spec.general]), so
	// Expose<&X::m, Tag> hands Tag::read the storage of m in any X (as NavigationSystem.bench.cpp's Rob).
	template <auto Member, typename Tag> struct Expose {
		static Storage read(const ecs::ClaimIndex& index) { return storageOf(index.*Member); }
		static inline const bool registered = (Tag::read = &read, true);
	};
	struct EntriesOf {
		static inline Storage (*read)(const ecs::ClaimIndex&) = nullptr;
	};
	struct GroupsOf {
		static inline Storage (*read)(const ecs::ClaimIndex&) = nullptr;
	};
	struct TableOf {
		static inline Storage (*read)(const ecs::ClaimIndex&) = nullptr;
	};

	template struct Expose<&ecs::ClaimIndex::entries, EntriesOf>;
	template struct Expose<&ecs::ClaimIndex::groups, GroupsOf>;
	template struct Expose<&ecs::ClaimIndex::table, TableOf>;

	std::array<Storage, 3> indexStorage(const ecs::ClaimIndex& index) {
		return {EntriesOf::read(index), GroupsOf::read(index), TableOf::read(index)};
	}

	using Clock = std::chrono::steady_clock;

	constexpr float kTickSeconds = 1.0F / 60.0F;

	std::uint32_t hashU32(std::uint32_t x) {
		x ^= x >> 16U;
		x *= 0x7feb352dU;
		x ^= x >> 15U;
		x *= 0x846ca68bU;
		x ^= x >> 16U;
		return x;
	}

	double atPercentile(std::vector<double> values, double p) {
		if (values.empty()) {
			return 0.0;
		}
		std::sort(values.begin(), values.end());
		return values[std::min(values.size() - 1, static_cast<std::size_t>(p * static_cast<double>(values.size())))];
	}

	double meanOf(const std::vector<double>& values) {
		double sum = 0.0;
		for (const double v : values) {
			sum += v;
		}
		return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
	}

	void BM_ClaimIndexRebuild(benchmark::State& state) {
		constexpr int		kColonists = 200;
		constexpr int		kGoals	   = 300;
		constexpr int		kRebuilds  = 1000;
		constexpr uint32_t	kWood	   = 11;
		constexpr uint32_t	kStone	   = 12;
		constexpr uint64_t	kTreeBase  = 0x7EE00000ULL;
		constexpr uint64_t	kDestBase  = 50000; // goal g's destination entity
		constexpr uint64_t	kStockBase = 60000; // the box a fetch takes from
		for (auto _ : state) {
			ecs::World world;
			for (int c = 0; c < kColonists; ++c) {
				const ecs::EntityID colonist = world.createEntity();
				ecs::Task&			task	 = world.addComponent<ecs::Task>(colonist);
				const auto			goal	 = static_cast<uint64_t>(hashU32(static_cast<std::uint32_t>(c)) % kGoals) + 1;
				const uint64_t		dest	 = kDestBase + goal;
				task.instanceId				 = static_cast<uint64_t>(c) + 1;
				task.claimSeq				 = static_cast<uint64_t>(c) + 1;
				if (c % 2 == 0) {
					task.type		  = ecs::TaskType::Harvest;
					task.priorityTier = 4;
					(void)task.claims.push(ecs::Claim::use(ecs::TargetKind::WorldKey, kTreeBase + static_cast<uint64_t>(c)));
					(void)task.claims.push(ecs::Claim::inbound(dest, kWood, 3));
					(void)task.claims.push(ecs::Claim::inFlight(goal, kWood, 3));
				} else {
					task.type		  = ecs::TaskType::Haul;
					task.priorityTier = 6;
					(void)task.claims.push(ecs::Claim::sourceQuantity(ecs::TargetKind::Entity, kStockBase + static_cast<uint64_t>(c % 20), kStone, 5));
					(void)task.claims.push(ecs::Claim::inbound(dest, kStone, 5));
					(void)task.claims.push(ecs::Claim::inFlight(goal, kStone, 5));
				}
				if (c % 4 == 0) {
					ecs::SuspendedWork record;
					record.instanceId	 = 100000 + static_cast<uint64_t>(c);
					record.claimSeq		 = record.instanceId;
					record.rootGoalId	 = (goal % kGoals) + 1;
					record.itemDefNameId = kStone;
					(void)record.claims.push(ecs::Claim::inbound(kDestBase + record.rootGoalId, kStone, 4));
					(void)record.claims.push(ecs::Claim::inFlight(record.rootGoalId, kStone, 4));
					task.suspended = record;
				}
			}

			ecs::ClaimIndex index;
			index.rebuild(world); // cold: sizes the storage
			std::vector<double> rebuildUs;
			rebuildUs.reserve(kRebuilds);
			int storageChanges = 0;
			for (int i = 0; i < kRebuilds; ++i) {
				const std::array<Storage, 3> before = indexStorage(index);
				const Clock::time_point		 start	= Clock::now();
				index.rebuild(world);
				rebuildUs.push_back(std::chrono::duration<double, std::micro>(Clock::now() - start).count());
				storageChanges += indexStorage(index) == before ? 0 : 1;
			}
			benchmark::DoNotOptimize(index.size());
			state.counters["colonists"]		 = kColonists;
			state.counters["goals"]			 = kGoals;
			state.counters["entries"]		 = static_cast<double>(index.size());
			state.counters["rebuildMeanUs"]	 = meanOf(rebuildUs);
			state.counters["rebuildP99Us"]	 = atPercentile(rebuildUs, 0.99);
			state.counters["rebuildMaxUs"]	 = atPercentile(rebuildUs, 1.0);
			state.counters["storageChanges"] = storageChanges;
		}
	}

	constexpr const char* kWoodDef = "BenchWork_Wood";
	constexpr const char* kTreeDef = "BenchWork_Tree";
	constexpr const char* kBoxDef  = "BenchWork_Box";
	constexpr const char* kRecipe  = "BenchWork_Recipe";

	// The defs and the recipe every BM_WorkDecision run shares, registered once.
	void registerWorkDefs() {
		static const bool registered = [] {
			auto&					registry = assets::AssetRegistry::Get();
			assets::AssetDefinition wood;
			wood.defName						= kWoodDef;
			wood.label							= kWoodDef;
			wood.handsRequired					= 2;
			wood.category						= assets::ItemCategory::RawMaterial;
			wood.capabilities.carryable			= assets::CarryableCapability{1};
			wood.itemProperties					= assets::ItemProperties{};
			wood.itemProperties->stackSize		= 40;
			wood.itemProperties->massKg			= 2.5F;
			registry.registerTestDefinition(std::move(wood));

			assets::AssetDefinition tree;
			tree.defName								= kTreeDef;
			tree.label									= kTreeDef;
			tree.capabilities.harvestable				= assets::HarvestableCapability{};
			tree.capabilities.harvestable->yieldDefName = kWoodDef;
			tree.capabilities.harvestable->amountMin	= 2;
			tree.capabilities.harvestable->amountMax	= 3;
			tree.capabilities.harvestable->durability	= 1.0F;
			registry.registerTestDefinition(std::move(tree));

			assets::AssetDefinition box;
			box.defName				 = kBoxDef;
			box.label				 = kBoxDef;
			box.capabilities.storage = assets::StorageCapability{};
			registry.registerTestDefinition(std::move(box));

			assets::RecipeDef recipe;
			recipe.defName = kRecipe;
			recipe.label   = kRecipe;
			recipe.inputs.push_back({kWoodDef, 0, 400});
			assets::RecipeRegistry::Get().registerTestRecipe(recipe);
			return true;
		}();
		(void)registered;
	}

	// The three goals that want wood, each a root and the Harvest child that cuts for it.
	void wantWood(ecs::World& world) {
		auto&		   goals  = ecs::GoalTaskRegistry::Get();
		const uint32_t woodId = assets::AssetRegistry::Get().getDefNameId(kWoodDef);

		const auto cut = [&](uint64_t root, ecs::GoalOwner owner, ecs::EntityID destination, glm::vec2 at) {
			ecs::GoalTask harvest;
			harvest.type				= ecs::TaskType::Harvest;
			harvest.owner				= owner;
			harvest.destinationEntity	= destination;
			harvest.destinationPosition = at;
			harvest.acceptedDefNameIds	= {woodId};
			harvest.yieldDefNameId		= woodId;
			harvest.parentGoalId		= root;
			harvest.status				= ecs::GoalStatus::Available;
			goals.createGoal(std::move(harvest));
		};

		// A box stocking wood by cutting, toward a minimum far above what one cut lands.
		const glm::vec2		boxAt{10.0F, 10.0F};
		const ecs::EntityID box = world.createEntity();
		world.addComponent<ecs::Position>(box, ecs::Position{boxAt});
		world.addComponent<ecs::Appearance>(box, ecs::Appearance{kBoxDef, 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
		world.addComponent<ecs::Inventory>(box, ecs::Inventory::createForStorage());
		ecs::StorageConfiguration config;
		config.addRule(ecs::StorageRule{.defName = kWoodDef, .category = assets::ItemCategory::RawMaterial, .priority = ecs::StoragePriority::Medium, .minAmount = 400});
		world.addComponent<ecs::StorageConfiguration>(box, std::move(config));
		ecs::GoalTask umbrella;
		umbrella.type				 = ecs::TaskType::Haul;
		umbrella.owner				 = ecs::GoalOwner::StorageGoalSystem;
		umbrella.destinationEntity	 = box;
		umbrella.destinationPosition = boxAt;
		umbrella.acceptedDefNameIds	 = {woodId};
		umbrella.status				 = ecs::GoalStatus::Available;
		cut(goals.createGoal(std::move(umbrella)), ecs::GoalOwner::StorageGoalSystem, box, boxAt);

		// A station whose queued job needs wood.
		const glm::vec2		stationAt{-10.0F, 10.0F};
		const ecs::EntityID station = world.createEntity();
		world.addComponent<ecs::Position>(station, ecs::Position{stationAt});
		world.addComponent<ecs::WorkQueue>(station).addJob(kRecipe, 1);
		world.addComponent<ecs::Inventory>(station, ecs::Inventory::createForStorage());
		ecs::GoalTask craft;
		craft.type				  = ecs::TaskType::Craft;
		craft.owner				  = ecs::GoalOwner::CraftingGoalSystem;
		craft.destinationEntity	  = station;
		craft.destinationPosition = stationAt;
		craft.status			  = ecs::GoalStatus::Blocked;
		cut(goals.createGoal(std::move(craft)), ecs::GoalOwner::CraftingGoalSystem, station, stationAt);

		// A blueprint awaiting wood.
		const glm::vec2		siteAt{0.0F, -12.0F};
		const ecs::EntityID site = world.createEntity();
		world.addComponent<ecs::Position>(site, ecs::Position{siteAt});
		ecs::StructureBlueprint blueprint;
		blueprint.phase	   = ecs::StructureBlueprint::BuildPhase::AwaitingMaterials;
		blueprint.required = {{kWoodDef, 400}};
		world.addComponent<ecs::StructureBlueprint>(site, std::move(blueprint));
		ecs::GoalTask build;
		build.type				  = ecs::TaskType::Build;
		build.owner				  = ecs::GoalOwner::ConstructionGoalSystem;
		build.destinationEntity	  = site;
		build.destinationPosition = siteAt;
		build.acceptedDefNameIds  = {woodId};
		build.status			  = ecs::GoalStatus::Blocked;
		cut(goals.createGoal(std::move(build)), ecs::GoalOwner::ConstructionGoalSystem, site, siteAt);
	}

	void BM_WorkDecision(benchmark::State& state) {
		const int colonistCount = static_cast<int>(state.range(0));
		const int treeCount		= static_cast<int>(state.range(1));
		registerWorkDefs();
		const uint32_t treeId = assets::AssetRegistry::Get().getDefNameId(kTreeDef);
		const auto	   harvestableMask = static_cast<std::uint16_t>(1U << static_cast<std::size_t>(assets::CapabilityType::Harvestable));

		for (auto _ : state) {
			ecs::GoalTaskRegistry::Get().clear();
			ecs::World				world;
			ecs::AIDecisionSystem&	ai = world.registerSystem<ecs::AIDecisionSystem>(assets::AssetRegistry::Get(), assets::RecipeRegistry::Get(), 1234U);
			wantWood(world);

			// One memory of trees within 100 m of the colony, which every colonist holds. Memory keys a
			// tree by its decimeter cell, so a draw landing in a known tree's cell is drawn again.
			ecs::Memory	  trees;
			std::uint32_t seed = 37;
			const auto	  unit = [&seed] {
				   seed = hashU32(seed + 0x9e3779b9U);
				   return static_cast<float>(seed & 0xFFFFFFU) / static_cast<float>(0xFFFFFFU);
			};
			for (int draws = 0; trees.worldEntityCount() < static_cast<std::size_t>(treeCount) && draws < 2 * treeCount; ++draws) {
				const float x = (unit() - 0.5F) * 200.0F;
				const float y = (unit() - 0.5F) * 200.0F;
				trees.rememberWorldEntity({x, y}, treeId, harvestableMask);
			}
			if (trees.worldEntityCount() != static_cast<std::size_t>(treeCount)) {
				state.SkipWithError("could not place every tree in its own memory cell");
				break;
			}

			std::vector<ecs::EntityID> colonists;
			std::vector<glm::vec2>	   homes;
			for (int c = 0; c < colonistCount; ++c) {
				const glm::vec2		home{static_cast<float>(c) * 2.0F, 0.0F};
				const ecs::EntityID e = world.createEntity();
				world.addComponent<ecs::Position>(e, ecs::Position{home});
				world.addComponent<ecs::Velocity>(e, ecs::Velocity{{0.0F, 0.0F}});
				world.addComponent<ecs::MovementTarget>(e, ecs::MovementTarget{{0.0F, 0.0F}, 2.0F, false});
				ecs::NeedsComponent needs = ecs::NeedsComponent::createDefault();
				for (const ecs::NeedType need : ecs::NeedsComponent::kActionableNeeds) {
					needs.get(need).value = 100.0F;
				}
				world.addComponent<ecs::NeedsComponent>(e, needs);
				world.addComponent<ecs::Memory>(e, trees);
				world.addComponent<ecs::Inventory>(e, ecs::Inventory::createForColonist());
				world.addComponent<ecs::Task>(e, ecs::Task{});
				world.addComponent<ecs::DecisionTrace>(e, ecs::DecisionTrace{});
				world.addComponent<ecs::SourceReachMemo>(e, ecs::SourceReachMemo{});
				world.addComponent<ecs::Colonist>(e, ecs::Colonist{"Worker"});
				world.addComponent<ecs::AgentRadius>(e, ecs::AgentRadius{0.3F, 1.0F});
				colonists.push_back(e);
				homes.push_back(home);
			}

			// One update deciding every colonist, its time in us; nullopt if one did not decide.
			const auto decide = [&]() -> std::optional<double> {
				for (std::size_t c = 0; c < colonists.size(); ++c) {
					world.getComponent<ecs::Position>(colonists[c])->value = homes[c];
					ecs::requestDecision(*world.getComponent<ecs::Task>(colonists[c]));
				}
				const Clock::time_point start = Clock::now();
				ai.update(kTickSeconds);
				const double us = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
				for (const ecs::EntityID e : colonists) {
					if (world.getComponent<ecs::Task>(e)->timeSinceEvaluation > 1.0F) {
						return std::nullopt;
					}
				}
				return us;
			};

			constexpr int kWarmup	 = 3;
			constexpr int kDecisions = 200;
			bool		  allDecided = true;
			for (int i = 0; i < kWarmup && allDecided; ++i) {
				allDecided = decide().has_value();
			}
			std::vector<double> updateUs;
			updateUs.reserve(kDecisions);
			for (int i = 0; i < kDecisions && allDecided; ++i) {
				const std::optional<double> us = decide();
				allDecided					   = us.has_value();
				updateUs.push_back(us.value_or(0.0));
			}
			if (!allDecided) {
				state.SkipWithError("a timed update did not re-decide every colonist");
				break;
			}

			const auto& options	 = world.getComponent<ecs::DecisionTrace>(colonists.front())->options;
			const auto	harvests = std::count_if(options.begin(), options.end(), [](const ecs::EvaluatedOption& o) { return o.taskType == ecs::TaskType::Harvest; });
			double		optionsAll = 0.0;
			for (const ecs::EntityID e : colonists) {
				optionsAll += static_cast<double>(world.getComponent<ecs::DecisionTrace>(e)->options.size());
			}
			state.counters["colonists"]			 = colonistCount;
			state.counters["trees"]				 = treeCount;
			state.counters["updateMeanUs"]		 = meanOf(updateUs);
			state.counters["updateP99Us"]		 = atPercentile(updateUs, 0.99);
			state.counters["updateMaxUs"]		 = atPercentile(updateUs, 1.0);
			state.counters["perColonistMeanUs"] = meanOf(updateUs) / static_cast<double>(colonistCount);
			state.counters["options"]			 = static_cast<double>(options.size());
			state.counters["harvestOptions"]	 = static_cast<double>(harvests);
			state.counters["optionsMeanPerCol"] = optionsAll / static_cast<double>(colonistCount);
		}
		ecs::GoalTaskRegistry::Get().clear();
	}

} // namespace

BENCHMARK(BM_ClaimIndexRebuild)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_WorkDecision)
	->ArgsProduct({{1, 8}, {100, 1000, 5000}})
	->Iterations(1)
	->Unit(benchmark::kMillisecond);
