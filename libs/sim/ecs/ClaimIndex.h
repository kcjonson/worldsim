#pragma once

// Lookup over the claims every colonist's Task holds (current and suspended). Derived data: rebuilt
// from the tasks, never saved, never timed out. See /docs/technical/task-claims.md.
//
// Layout: one flat entry array, plus an open-addressed table from a claim key to the head of an
// intrusive chain of the entries under that key. Keys are (Use, target), (SourceQuantity, target,
// item), (Slot, target), (InFlight, root, item), and for inbound (destination, item) to a group whose
// chain holds that item's entries, with the destination's groups chained off (destination) so the
// slot query walks one group per item. A second chain links each instance's entries, so a rewrite
// within an update retires exactly that instance's entries. Every query is one probe plus a walk of
// the entries under one key. Table slots carry a generation, so a rebuild empties the table without
// touching it; entries, groups, and table keep their storage across rebuilds.

#include "EntityID.h"
#include "components/Task.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ecs {

	class World;

	struct ClaimTarget {
		TargetKind kind = TargetKind::Entity;
		uint64_t   key = 0;
	};

	/// The instance ids a query leaves out: a colonist's own current task and its suspended record,
	/// so the work it is doing does not read as taken. 0 excludes nothing.
	struct ClaimExclusion {
		uint64_t current = 0;
		uint64_t suspended = 0;

		[[nodiscard]] static ClaimExclusion of(const Task& task) {
			return {.current = task.instanceId, .suspended = task.suspended ? task.suspended->instanceId : 0};
		}
		[[nodiscard]] bool excludes(uint64_t instanceId) const {
			return instanceId != 0 && (instanceId == current || instanceId == suspended);
		}
	};

	struct ClaimHolder {
		EntityID colonist = kInvalidEntity;
		uint64_t instanceId = 0;
		uint64_t claimSeq = 0;
		uint8_t	 tier = 7; // the holding task's priorityTier at index time; lower is higher rank
		bool	 suspended = false;
	};

	/// Rank order: lower tier first, then earlier claimSeq, then lower instance id (total and
	/// deterministic). True when `a` ranks above `b`.
	[[nodiscard]] bool outranks(const ClaimHolder& a, const ClaimHolder& b);

	/// What a destination box offers one inbound item before it needs whole slots: the headroom in
	/// that item's existing partial stacks, and its stack size (0 or UINT32_MAX: unbounded).
	struct InboundStackRoom {
		uint32_t partialRoom = 0;
		uint32_t stackSize = 0;
	};

	class ClaimIndex {
	  public:
		/// Rebuild from every Task in `world`, current claims and suspended records, in view order.
		void rebuild(World& world);

		/// Add `task`'s current claims, for a selection made after the last rebuild. Its suspended
		/// record is not appended: a record's claims were indexed when they were the current task's,
		/// and suspend() rewrites them. An instance already in the index is retired first, so an
		/// adopted record's instance is never counted twice.
		void append(EntityID colonist, const Task& task);

		/// The current task of `colonist` has just become its suspended record, with smaller or equal
		/// claims under the same instance id: replace that instance's entries with the record's.
		void suspend(EntityID colonist, const Task& task);

		/// Drop every entry of `instanceId` (a discarded record, or a task released outside an AI
		/// update) until the next rebuild. Unknown ids are ignored.
		void retire(uint64_t instanceId);

		/// Units of `item` other tasks will take from `source`.
		[[nodiscard]] uint32_t sourceClaimed(ClaimTarget source, uint32_t itemDefNameId, ClaimExclusion exclude) const;

		/// Units of `item` other tasks will deposit at `destination`.
		[[nodiscard]] uint32_t inboundItems(EntityID destination, uint32_t itemDefNameId, ClaimExclusion exclude) const;

		/// Slots other tasks' inbound goods will take at a box: per item, what overflows its partial
		/// stacks, in whole stacks. `stackRoom(itemDefNameId)` returns an InboundStackRoom.
		template <typename StackRoomFn>
		[[nodiscard]] uint32_t inboundSlots(EntityID destination, ClaimExclusion exclude, StackRoomFn&& stackRoom) const;

		/// Units of `item` other tasks are bringing toward `rootGoalId`.
		[[nodiscard]] uint32_t inFlight(uint64_t rootGoalId, uint32_t itemDefNameId, ClaimExclusion exclude) const;

		/// The highest-ranked other holder of a Use target, if any.
		[[nodiscard]] std::optional<ClaimHolder> useHolder(ClaimTarget target, ClaimExclusion exclude) const;

		/// The highest-ranked other holder of slot `slotIndex` on `target`, if any. kAnySlot, the
		/// target's one unnumbered slot, conflicts with every slot on the target, numbered or not.
		[[nodiscard]] std::optional<ClaimHolder> slotHolder(ClaimTarget target, uint16_t slotIndex, ClaimExclusion exclude) const;

		/// Every claim still in the index (retired ones left out), with its holder.
		template <typename Fn>
		void forEachClaim(Fn&& fn) const {
			for (const Entry& entry : entries) {
				if (!entry.retired) {
					fn(entry.claim, entry.holder);
				}
			}
		}

		/// Entries held, retired ones included.
		[[nodiscard]] size_t size() const { return entries.size(); }

#ifdef DEVELOPMENT_BUILD
		/// The subset rule: a fresh rebuild from `world` (into `scratch`) is a subset of this index,
		/// and every surplus entry belongs to an instance id no live task or suspended record holds.
		/// Also no claims on an inactive task, none without an instance id, and every Inbound paired
		/// with an InFlight of the same item on the same holder. Returns the first violation.
		[[nodiscard]] std::optional<std::string> findViolation(World& world, ClaimIndex& scratch) const;
#endif

	  private:
		static constexpr uint32_t kNone = 0xFFFFFFFF;

		enum class KeyTag : uint8_t { Use, Source, Slot, InboundItem, InboundDestination, InFlight, Instance };

		struct Entry {
			Claim		claim;
			ClaimHolder holder;
			uint32_t	next = kNone;
			uint32_t	nextOfInstance = kNone;
			bool		retired = false;
		};

		struct InboundGroup {
			uint32_t itemDefNameId = 0;
			uint32_t firstEntry = kNone;
			uint32_t nextGroup = kNone;
		};

		// Built only through keyOf, which names every field, so no key falls back to a default key space.
		struct Key {
			uint64_t   target = 0;
			uint32_t   sub = 0; // item, or 0
			KeyTag	   tag = KeyTag::Use;
			TargetKind targetKind = TargetKind::WorldKey;

			bool operator==(const Key&) const = default;
		};
		[[nodiscard]] static Key keyOf(KeyTag tag, TargetKind targetKind, uint64_t target, uint32_t sub) {
			return {.target = target, .sub = sub, .tag = tag, .targetKind = targetKind};
		}

		struct Bucket {
			Key		 key;
			uint32_t value = kNone;
			uint32_t generation = 0;
		};

		[[nodiscard]] static uint64_t bucketOf(const Key& key);
		void						  add(const Claim& claim, const ClaimHolder& holder);
		void						  addCurrent(EntityID colonist, const Task& task);
		void						  addSuspended(EntityID colonist, const Task& task);
		[[nodiscard]] static Key	  keyFor(const Claim& claim);
		void clearAll();

		/// The value under `key`, or kNone.
		[[nodiscard]] uint32_t find(const Key& key) const;
		/// The value slot under `key`, inserted as kNone when absent.
		uint32_t& findOrInsert(const Key& key);
		void	  grow();

		template <typename Fn>
		void forEachUnder(const Key& key, ClaimExclusion exclude, Fn&& fn) const {
			for (uint32_t index = find(key); index != kNone; index = entries[index].next) {
				if (!entries[index].retired && !exclude.excludes(entries[index].holder.instanceId)) {
					fn(entries[index]);
				}
			}
		}

		template <typename Accept>
		[[nodiscard]] std::optional<ClaimHolder> topHolder(const Key& key, ClaimExclusion exclude, Accept&& accept) const {
			std::optional<ClaimHolder> best;
			forEachUnder(key, exclude, [&](const Entry& entry) {
				if (accept(entry.claim) && (!best || outranks(entry.holder, *best))) {
					best = entry.holder;
				}
			});
			return best;
		}

#ifdef DEVELOPMENT_BUILD
		// One claim's identity for the validator's subset comparison. Tier is left out: isSameTask may
		// re-tier a live task between the rebuild and the check without changing what it claims.
		struct ClaimIdentity {
			uint64_t   instanceId;
			uint64_t   target;
			uint32_t   itemDefNameId;
			uint16_t   slotIndex;
			ClaimKind  kind;
			TargetKind targetKind;
			uint32_t   quantity;
		};
		/// Every unretired entry's identity into `out`, sorted, equal identities merged into one summed
		/// quantity.
		void collectIdentities(std::vector<ClaimIdentity>& out) const;

		// The validator's working storage, kept on the scratch index it is handed, so a warm check
		// allocates nothing.
		std::vector<uint64_t>	   checkLive;
		std::vector<ClaimIdentity> checkFresh;
		std::vector<ClaimIdentity> checkAppended;
#endif

		std::vector<Entry>		  entries;
		std::vector<InboundGroup> groups;
		std::vector<Bucket>		  table;
		uint32_t				  generation = 1;
		uint32_t				  usedBuckets = 0;
	};

	template <typename StackRoomFn>
	uint32_t ClaimIndex::inboundSlots(EntityID destination, ClaimExclusion exclude, StackRoomFn&& stackRoom) const {
		uint64_t slots = 0;
		for (uint32_t group = find(keyOf(KeyTag::InboundDestination, TargetKind::Entity, destination, 0)); group != kNone;
			 group = groups[group].nextGroup) {
			uint64_t quantity = 0;
			for (uint32_t index = groups[group].firstEntry; index != kNone; index = entries[index].next) {
				if (!entries[index].retired && !exclude.excludes(entries[index].holder.instanceId)) {
					quantity += entries[index].claim.quantity();
				}
			}
			const InboundStackRoom room = stackRoom(groups[group].itemDefNameId);
			if (quantity <= room.partialRoom) {
				continue;
			}
			const uint64_t rest = quantity - room.partialRoom;
			const uint64_t stack = room.stackSize == 0 ? UINT32_MAX : room.stackSize;
			slots += (rest + stack - 1) / stack;
		}
		return slots > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(slots);
	}

} // namespace ecs
