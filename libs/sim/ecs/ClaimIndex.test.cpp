#include "ClaimIndex.h"

#include "World.h"

#include <gtest/gtest.h>

#include <initializer_list>

namespace ecs::test {

	namespace {
		constexpr uint64_t kPile = 0xA11CE;
		constexpr uint64_t kTree = 0x7EE;
		constexpr EntityID kBox = 501;
		constexpr EntityID kStation = 502;
		constexpr uint64_t kRoot = 77;
		constexpr uint32_t kWood = 11;
		constexpr uint32_t kStone = 12;
		constexpr uint32_t kBerry = 13;

		struct TaskSpec {
			uint64_t					 instanceId = 0;
			uint64_t					 claimSeq = 0;
			int							 tier = 6;
			std::initializer_list<Claim> claims;
		};

		EntityID addColonist(World& world, const TaskSpec& spec) {
			const EntityID colonist = world.createEntity();
			Task&		   task = world.addComponent<Task>(colonist);
			task.type = TaskType::Haul;
			task.instanceId = spec.instanceId;
			task.claimSeq = spec.claimSeq;
			task.priorityTier = spec.tier;
			for (const Claim& claim : spec.claims) {
				EXPECT_TRUE(task.claims.push(claim));
			}
			return colonist;
		}

		const ClaimTarget kPileTarget{.kind = TargetKind::WorldKey, .key = kPile};
		const ClaimTarget kTreeTarget{.kind = TargetKind::WorldKey, .key = kTree};
		const ClaimTarget kStationTarget{.kind = TargetKind::Entity, .key = kStation};
	} // namespace

	TEST(ClaimIndexTest, RebuildSumsEveryKindAcrossTasks) {
		World world;
		addColonist(world, {.instanceId = 1,
							.claims = {Claim::sourceQuantity(TargetKind::WorldKey, kPile, kWood, 5), Claim::inbound(kBox, kWood, 5),
									   Claim::inFlight(kRoot, kWood, 5)}});
		addColonist(world, {.instanceId = 2,
							.claims = {Claim::sourceQuantity(TargetKind::WorldKey, kPile, kWood, 3), Claim::inbound(kBox, kWood, 3),
									   Claim::inFlight(kRoot, kWood, 3)}});
		addColonist(world, {.instanceId = 3, .claims = {Claim::use(TargetKind::WorldKey, kTree), Claim::inFlight(kRoot, kWood, 8)}});
		addColonist(world, {.instanceId = 4, .tier = 4, .claims = {Claim::slot(kStation)}});

		ClaimIndex index;
		index.rebuild(world);

		EXPECT_EQ(index.size(), 9U);
		EXPECT_EQ(index.sourceClaimed(kPileTarget, kWood, {}), 8U);
		EXPECT_EQ(index.sourceClaimed(kPileTarget, kStone, {}), 0U);
		EXPECT_EQ(index.inboundItems(kBox, kWood, {}), 8U);
		EXPECT_EQ(index.inFlight(kRoot, kWood, {}), 16U);
		EXPECT_EQ(index.useHolder(kTreeTarget, {})->instanceId, 3U);
		EXPECT_EQ(index.slotHolder(kStationTarget, kAnySlot, {})->instanceId, 4U);
		// The station's one unnumbered slot is held against every numbered slot on it too.
		EXPECT_EQ(index.slotHolder(kStationTarget, 0, {})->instanceId, 4U);
		// The same integer in another key space is a different target.
		EXPECT_FALSE(index.useHolder({.kind = TargetKind::Entity, .key = kTree}, {}).has_value());
	}

	TEST(ClaimIndexTest, QueriesLeaveOutTheExcludedInstance) {
		World world;
		addColonist(world, {.instanceId = 1, .claims = {Claim::sourceQuantity(TargetKind::WorldKey, kPile, kWood, 5), Claim::use(TargetKind::WorldKey, kTree)}});
		addColonist(world, {.instanceId = 2, .claims = {Claim::sourceQuantity(TargetKind::WorldKey, kPile, kWood, 3)}});

		ClaimIndex index;
		index.rebuild(world);

		const ClaimExclusion self{.current = 1};
		EXPECT_EQ(index.sourceClaimed(kPileTarget, kWood, self), 3U);
		EXPECT_FALSE(index.useHolder(kTreeTarget, self).has_value());
		EXPECT_EQ(index.useHolder(kTreeTarget, {.current = 2})->instanceId, 1U);
		EXPECT_EQ(index.sourceHolder(kPileTarget, kWood, self)->instanceId, 2U);
		EXPECT_EQ(index.sourceHolder(kPileTarget, kWood, {})->instanceId, 1U) << "lower instance id ranks first on a tie";
		EXPECT_FALSE(index.sourceHolder(kPileTarget, kStone, {}).has_value());
	}

	// A filtered holder query passes over holders the filter rejects, however they rank.
	TEST(ClaimIndexTest, UseHolderWhereSkipsHoldersTheFilterRejects) {
		World		   world;
		const EntityID worker = addColonist(world, {.instanceId = 1, .tier = 4, .claims = {Claim::use(TargetKind::WorldKey, kTree)}});
		const EntityID eater = addColonist(world, {.instanceId = 2, .tier = 5, .claims = {Claim::use(TargetKind::WorldKey, kTree)}});
		world.getComponent<Task>(eater)->type = TaskType::FulfillNeed;

		ClaimIndex index;
		index.rebuild(world);
		const auto eats = [](const ClaimHolder& holder) { return holder.taskType == TaskType::FulfillNeed; };
		EXPECT_EQ(index.useHolder(kTreeTarget, {})->colonist, worker) << "the worker ranks first";
		EXPECT_EQ(index.useHolderWhere(kTreeTarget, {}, eats)->colonist, eater);
		EXPECT_FALSE(index.useHolderWhere(kTreeTarget, {.current = 2}, eats).has_value());
	}

	TEST(ClaimIndexTest, ClearedTaskContributesNothing) {
		World		   world;
		const EntityID colonist = addColonist(world, {.instanceId = 1, .claims = {Claim::use(TargetKind::WorldKey, kTree), Claim::inFlight(kRoot, kWood, 4)}});

		ClaimIndex index;
		index.rebuild(world);
		ASSERT_EQ(index.size(), 2U);

		world.getComponent<Task>(colonist)->clear();
		index.rebuild(world);

		EXPECT_EQ(index.size(), 0U);
		EXPECT_FALSE(index.useHolder(kTreeTarget, {}).has_value());
		EXPECT_EQ(index.inFlight(kRoot, kWood, {}), 0U);
	}

	TEST(ClaimIndexTest, SuspendedRecordSurvivesClearAndCountsForOthersOnly) {
		World		   world;
		const EntityID colonist = addColonist(world, {.instanceId = 5, .claimSeq = 5, .claims = {Claim::inbound(kBox, kWood, 6), Claim::inFlight(kRoot, kWood, 6)}});
		Task&		   task = *world.getComponent<Task>(colonist);
		task.suspended = SuspendedWork{.instanceId = 5, .claimSeq = 5, .rootGoalId = kRoot, .itemDefNameId = kWood};
		ASSERT_TRUE(task.suspended->claims.push(Claim::inbound(kBox, kWood, 6)));
		ASSERT_TRUE(task.suspended->claims.push(Claim::inFlight(kRoot, kWood, 6)));
		task.clear();
		ASSERT_TRUE(task.suspended.has_value());
		task.type = TaskType::FulfillNeed;
		task.instanceId = 9;
		task.priorityTier = 2;

		ClaimIndex index;
		index.rebuild(world);

		EXPECT_EQ(index.inboundItems(kBox, kWood, {}), 6U);
		EXPECT_EQ(index.inFlight(kRoot, kWood, {}), 6U);
		EXPECT_EQ(index.inFlight(kRoot, kWood, ClaimExclusion::of(task)), 0U);

		task.discardSuspended();
		index.rebuild(world);
		EXPECT_EQ(index.inFlight(kRoot, kWood, {}), 0U);
	}

	TEST(ClaimIndexTest, InboundSlotsCountEachItemsOverflowSeparately) {
		World world;
		addColonist(world, {.instanceId = 1, .claims = {Claim::inbound(kBox, kWood, 20), Claim::inFlight(kRoot, kWood, 20)}});
		addColonist(world, {.instanceId = 2, .claims = {Claim::inbound(kBox, kWood, 10), Claim::inFlight(kRoot, kWood, 10)}});
		addColonist(world, {.instanceId = 3, .claims = {Claim::inbound(kBox, kStone, 3), Claim::inFlight(kRoot, kStone, 3)}});
		addColonist(world, {.instanceId = 4, .claims = {Claim::inbound(kBox, kBerry, 4), Claim::inFlight(kRoot, kBerry, 4)}});

		ClaimIndex index;
		index.rebuild(world);

		// Wood: 30 inbound, 5 fit the partial stack, 25 need two stacks of 20. Stone: 3 need one
		// stack. Berries: 4 fit the partial stack.
		const auto room = [](uint32_t item) -> InboundStackRoom {
			switch (item) {
				case kWood:
					return {.partialRoom = 5, .stackSize = 20};
				case kStone:
					return {.partialRoom = 0, .stackSize = 50};
				default:
					return {.partialRoom = 10, .stackSize = 10};
			}
		};
		EXPECT_EQ(index.inboundSlots(kBox, {}, room), 3U);
		// Without the 10-wood haul: 15 overflow, one stack.
		EXPECT_EQ(index.inboundSlots(kBox, {.current = 2}, room), 2U);
		EXPECT_EQ(index.inboundItems(kBox, kWood, {.current = 2}), 20U);
	}

	TEST(ClaimIndexTest, HolderIsTheHighestRankedClaimant) {
		World world;
		addColonist(world, {.instanceId = 1, .claimSeq = 1, .tier = 6, .claims = {Claim::use(TargetKind::WorldKey, kTree), Claim::slot(kStation)}});
		addColonist(world, {.instanceId = 2, .claimSeq = 3, .tier = 2, .claims = {Claim::use(TargetKind::WorldKey, kTree)}});
		addColonist(world, {.instanceId = 3, .claimSeq = 2, .tier = 2, .claims = {Claim::use(TargetKind::WorldKey, kTree)}});
		addColonist(world, {.instanceId = 4, .claimSeq = 4, .tier = 4, .claims = {Claim::slot(kStation)}});

		ClaimIndex index;
		index.rebuild(world);

		// Lower tier number first; equal tiers go to the earlier sequence.
		EXPECT_EQ(index.useHolder(kTreeTarget, {})->instanceId, 3U);
		EXPECT_EQ(index.slotHolder(kStationTarget, kAnySlot, {})->instanceId, 4U);
	}

	TEST(ClaimIndexTest, RebuildGrowsPastItsTableAndKeepsEveryKey) {
		World world;
		for (uint64_t colonist = 1; colonist <= 100; ++colonist) {
			addColonist(world, {.instanceId = colonist,
								.claims = {Claim::sourceQuantity(TargetKind::WorldKey, kPile + colonist, kWood, 2), Claim::inbound(kBox + colonist, kWood, 2),
										   Claim::inFlight(kRoot, kWood, 2), Claim::use(TargetKind::WorldKey, kTree + colonist)}});
		}

		ClaimIndex index;
		index.rebuild(world);
		index.rebuild(world);

		EXPECT_EQ(index.size(), 400U);
		EXPECT_EQ(index.inFlight(kRoot, kWood, {}), 200U);
		for (uint64_t colonist = 1; colonist <= 100; ++colonist) {
			ASSERT_EQ(index.sourceClaimed({.kind = TargetKind::WorldKey, .key = kPile + colonist}, kWood, {}), 2U);
			ASSERT_EQ(index.inboundItems(kBox + colonist, kWood, {}), 2U);
			ASSERT_EQ(index.useHolder({.kind = TargetKind::WorldKey, .key = kTree + colonist}, {})->instanceId, colonist);
		}
	}

	// A station has one unnumbered slot; it conflicts with any numbered slot on the same target, while
	// two numbered slots (builder positions) are held independently.
	TEST(ClaimIndexTest, UnnumberedSlotSeesNumberedSlotsOnTheTarget) {
		World world;
		addColonist(world, {.instanceId = 1, .claims = {Claim::slot(kStation, 2)}});

		ClaimIndex index;
		index.rebuild(world);

		EXPECT_EQ(index.slotHolder(kStationTarget, kAnySlot, {})->instanceId, 1U);
		EXPECT_EQ(index.slotHolder(kStationTarget, 2, {})->instanceId, 1U);
		EXPECT_FALSE(index.slotHolder(kStationTarget, 3, {}).has_value());

		addColonist(world, {.instanceId = 2, .claims = {Claim::slot(kStation)}});
		index.rebuild(world);
		EXPECT_EQ(index.slotHolder(kStationTarget, 3, {.current = 1})->instanceId, 2U);
	}

	TEST(ClaimIndexTest, AppendReplacesAnInstanceAlreadyIndexed) {
		World		   world;
		const EntityID colonist = addColonist(world, {.instanceId = 5, .claims = {Claim::inbound(kBox, kWood, 6), Claim::inFlight(kRoot, kWood, 6)}});
		ClaimIndex	   index;
		index.rebuild(world);

		// The same instance appended again (an adopted record) counts once.
		index.append(colonist, *world.getComponent<Task>(colonist));
		EXPECT_EQ(index.inboundItems(kBox, kWood, {}), 6U);
		EXPECT_EQ(index.inFlight(kRoot, kWood, {}), 6U);

		index.retire(5);
		EXPECT_EQ(index.inFlight(kRoot, kWood, {}), 0U);
		EXPECT_EQ(index.inboundSlots(kBox, {}, [](uint32_t) { return InboundStackRoom{.partialRoom = 0, .stackSize = 1}; }), 0U);
	}

	TEST(ClaimIndexTest, SuspendRewritesTheInstanceToItsRecord) {
		World		   world;
		const EntityID colonist = addColonist(world, {.instanceId = 5, .claimSeq = 5, .claims = {Claim::inbound(kBox, kWood, 6), Claim::inFlight(kRoot, kWood, 6)}});
		ClaimIndex	   index;
		index.rebuild(world);

		Task& task = *world.getComponent<Task>(colonist);
		task.suspended = SuspendedWork{.instanceId = 5, .claimSeq = 5, .rootGoalId = kRoot, .itemDefNameId = kWood};
		ASSERT_TRUE(task.suspended->claims.push(Claim::inbound(kBox, kWood, 4)));
		ASSERT_TRUE(task.suspended->claims.push(Claim::inFlight(kRoot, kWood, 4)));
		index.suspend(colonist, task);
		task.clear();

		EXPECT_EQ(index.inboundItems(kBox, kWood, {}), 4U);
		EXPECT_EQ(index.inFlight(kRoot, kWood, {}), 4U);
	}

#ifdef DEVELOPMENT_BUILD
	// A rewrite to smaller claims within an update goes through suspend(), so the subset rule holds
	// without an exception for live instances.
	TEST(ClaimIndexValidatorTest, AcceptsASuspendWithSmallerClaims) {
		World		   world;
		const EntityID colonist = addColonist(world, {.instanceId = 5, .claimSeq = 5, .claims = {Claim::inbound(kBox, kWood, 6), Claim::inFlight(kRoot, kWood, 6)}});
		ClaimIndex	   index;
		ClaimIndex	   scratch;
		index.rebuild(world);

		Task& task = *world.getComponent<Task>(colonist);
		task.suspended = SuspendedWork{.instanceId = 5, .claimSeq = 5, .rootGoalId = kRoot, .itemDefNameId = kWood};
		ASSERT_TRUE(task.suspended->claims.push(Claim::inbound(kBox, kWood, 4)));
		ASSERT_TRUE(task.suspended->claims.push(Claim::inFlight(kRoot, kWood, 4)));
		index.suspend(colonist, task);
		task.clear();

		EXPECT_EQ(index.findViolation(world, scratch), std::nullopt);
	}

	TEST(ClaimIndexValidatorTest, AcceptsSurplusLeftByAClearedInstance) {
		World		   world;
		const EntityID colonist = addColonist(world, {.instanceId = 1, .claims = {Claim::use(TargetKind::WorldKey, kTree)}});
		ClaimIndex	   index;
		ClaimIndex	   scratch;
		index.rebuild(world);

		Task& task = *world.getComponent<Task>(colonist);
		task.clear();
		task.type = TaskType::Haul;
		task.instanceId = 2;
		ASSERT_TRUE(task.claims.push(Claim::sourceQuantity(TargetKind::WorldKey, kPile, kWood, 4)));
		index.append(colonist, task);

		EXPECT_EQ(index.findViolation(world, scratch), std::nullopt);
	}

	TEST(ClaimIndexValidatorTest, RejectsAClaimMissingFromTheIndex) {
		World		   world;
		const EntityID colonist = addColonist(world, {.instanceId = 1, .claims = {Claim::use(TargetKind::WorldKey, kTree)}});
		ClaimIndex	   index;
		ClaimIndex	   scratch;
		index.rebuild(world);

		ASSERT_TRUE(world.getComponent<Task>(colonist)->claims.push(Claim::inFlight(kRoot, kWood, 4)));

		EXPECT_NE(index.findViolation(world, scratch), std::nullopt);
	}
#endif

} // namespace ecs::test
