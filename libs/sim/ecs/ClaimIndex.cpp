#include "ClaimIndex.h"

#include "World.h"

#include <algorithm>
#include <tuple>

namespace ecs {

	namespace {
		constexpr size_t kMinBuckets = 64;

		uint64_t mix(uint64_t value) {
			value += 0x9E3779B97F4A7C15ULL;
			value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
			value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
			return value ^ (value >> 31);
		}

		uint8_t tierOf(const Task& task) {
			return static_cast<uint8_t>(std::clamp(task.priorityTier, 0, 255));
		}
	} // namespace

	uint64_t ClaimIndex::bucketOf(const Key& key) {
		return mix(key.target ^ mix((uint64_t{key.sub} << 16) | (uint64_t{static_cast<uint8_t>(key.tag)} << 8) |
									static_cast<uint8_t>(key.targetKind)));
	}

	bool outranks(const ClaimHolder& a, const ClaimHolder& b) {
		return std::tie(a.tier, a.claimSeq, a.instanceId) < std::tie(b.tier, b.claimSeq, b.instanceId);
	}

	void ClaimIndex::clearAll() {
		entries.clear();
		groups.clear();
		usedBuckets = 0;
		if (++generation == 0) {
			for (auto& bucket : table) {
				bucket.generation = 0;
			}
			generation = 1;
		}
	}

	void ClaimIndex::rebuild(World& world) {
		clearAll();
		for (auto [colonist, task] : world.view<Task>()) {
			append(colonist, task);
			if (task.suspended) {
				// A record has no tier of its own; it ranks with the task holding it, and possessed
				// goods are never bumped anyway.
				const ClaimHolder holder{
					.colonist = colonist,
					.instanceId = task.suspended->instanceId,
					.claimSeq = task.suspended->claimSeq,
					.tier = tierOf(task),
					.suspended = true,
				};
				for (const Claim& claim : task.suspended->claims) {
					add(claim, holder);
				}
			}
		}
	}

	void ClaimIndex::append(EntityID colonist, const Task& task) {
		const ClaimHolder holder{.colonist = colonist, .instanceId = task.instanceId, .claimSeq = task.claimSeq, .tier = tierOf(task)};
		for (const Claim& claim : task.claims) {
			add(claim, holder);
		}
	}

	void ClaimIndex::add(const Claim& claim, const ClaimHolder& holder) {
		const auto entryIndex = static_cast<uint32_t>(entries.size());
		entries.push_back({.claim = claim, .holder = holder});

		Key key{.target = claim.target, .targetKind = claim.targetKind};
		switch (claim.kind) {
			case ClaimKind::Use:
				key.tag = KeyTag::Use;
				break;
			case ClaimKind::SourceQuantity:
				key.tag = KeyTag::Source;
				key.sub = claim.itemDefNameId;
				break;
			case ClaimKind::Slot:
				key.tag = KeyTag::Slot;
				key.sub = claim.slotIndex;
				break;
			case ClaimKind::InFlight:
				key.tag = KeyTag::InFlight;
				key.sub = claim.itemDefNameId;
				break;
			case ClaimKind::Inbound: {
				key.tag = KeyTag::InboundItem;
				key.sub = claim.itemDefNameId;
				// Each findOrInsert may grow the table, so no bucket reference outlives the next insert.
				uint32_t groupIndex = find(key);
				if (groupIndex == kNone) {
					groupIndex = static_cast<uint32_t>(groups.size());
					uint32_t& firstGroup = findOrInsert({.target = claim.target, .tag = KeyTag::InboundDestination, .targetKind = claim.targetKind});
					groups.push_back({.itemDefNameId = claim.itemDefNameId, .nextGroup = firstGroup});
					firstGroup = groupIndex;
					findOrInsert(key) = groupIndex;
				}
				InboundGroup& group = groups[groupIndex];
				entries[entryIndex].next = group.firstEntry;
				group.firstEntry = entryIndex;
				return;
			}
		}
		uint32_t& head = findOrInsert(key);
		entries[entryIndex].next = head;
		head = entryIndex;
	}

	uint32_t ClaimIndex::find(const Key& key) const {
		if (table.empty()) {
			return kNone;
		}
		const size_t mask = table.size() - 1;
		for (size_t slot = bucketOf(key) & mask; table[slot].generation == generation; slot = (slot + 1) & mask) {
			if (table[slot].key == key) {
				return table[slot].value;
			}
		}
		return kNone;
	}

	uint32_t& ClaimIndex::findOrInsert(const Key& key) {
		if ((usedBuckets + 1) * 2 > table.size()) {
			grow();
		}
		const size_t mask = table.size() - 1;
		size_t		 slot = bucketOf(key) & mask;
		while (table[slot].generation == generation) {
			if (table[slot].key == key) {
				return table[slot].value;
			}
			slot = (slot + 1) & mask;
		}
		table[slot] = {.key = key, .value = kNone, .generation = generation};
		++usedBuckets;
		return table[slot].value;
	}

	void ClaimIndex::grow() {
		std::vector<Bucket> old;
		old.swap(table);
		table.assign(std::max(kMinBuckets, old.size() * 2), Bucket{});
		usedBuckets = 0;
		for (const Bucket& bucket : old) {
			if (bucket.generation == generation) {
				findOrInsert(bucket.key) = bucket.value;
			}
		}
	}

	std::optional<ClaimHolder> ClaimIndex::topHolder(const Key& key, ClaimExclusion exclude) const {
		std::optional<ClaimHolder> best;
		forEachUnder(key, exclude, [&](const Entry& entry) {
			if (!best || outranks(entry.holder, *best)) {
				best = entry.holder;
			}
		});
		return best;
	}

	uint32_t ClaimIndex::sourceClaimed(ClaimTarget source, uint32_t itemDefNameId, ClaimExclusion exclude) const {
		uint64_t total = 0;
		forEachUnder({.target = source.key, .sub = itemDefNameId, .tag = KeyTag::Source, .targetKind = source.kind}, exclude,
					 [&](const Entry& entry) { total += entry.claim.quantity; });
		return total > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(total);
	}

	uint32_t ClaimIndex::inboundItems(EntityID destination, uint32_t itemDefNameId, ClaimExclusion exclude) const {
		const uint32_t group = find({.target = destination, .sub = itemDefNameId, .tag = KeyTag::InboundItem});
		if (group == kNone) {
			return 0;
		}
		uint64_t total = 0;
		for (uint32_t index = groups[group].firstEntry; index != kNone; index = entries[index].next) {
			if (!exclude.excludes(entries[index].holder.instanceId)) {
				total += entries[index].claim.quantity;
			}
		}
		return total > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(total);
	}

	uint32_t ClaimIndex::inFlight(uint64_t rootGoalId, uint32_t itemDefNameId, ClaimExclusion exclude) const {
		uint64_t total = 0;
		forEachUnder({.target = rootGoalId, .sub = itemDefNameId, .tag = KeyTag::InFlight, .targetKind = TargetKind::Goal}, exclude,
					 [&](const Entry& entry) { total += entry.claim.quantity; });
		return total > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(total);
	}

	std::optional<ClaimHolder> ClaimIndex::useHolder(ClaimTarget target, ClaimExclusion exclude) const {
		return topHolder({.target = target.key, .tag = KeyTag::Use, .targetKind = target.kind}, exclude);
	}

	std::optional<ClaimHolder> ClaimIndex::slotHolder(ClaimTarget target, uint16_t slotIndex, ClaimExclusion exclude) const {
		return topHolder({.target = target.key, .sub = slotIndex, .tag = KeyTag::Slot, .targetKind = target.kind}, exclude);
	}

#ifdef DEVELOPMENT_BUILD
	namespace {
		// One claim's identity for the subset comparison. Tier is left out: isSameTask may re-tier a
		// live task between the rebuild and the check without changing what it claims.
		struct ClaimIdentity {
			uint64_t   instanceId;
			uint64_t   target;
			uint32_t   itemDefNameId;
			uint16_t   slotIndex;
			ClaimKind  kind;
			TargetKind targetKind;
			uint32_t   quantity;

			[[nodiscard]] auto order() const { return std::tie(instanceId, kind, targetKind, target, itemDefNameId, slotIndex); }
		};

		std::string describe(const ClaimIdentity& claim) {
			return "instance " + std::to_string(claim.instanceId) + " kind " + std::to_string(static_cast<int>(claim.kind)) +
				   " target " + std::to_string(claim.target) + " item " + std::to_string(claim.itemDefNameId) + " slot " +
				   std::to_string(claim.slotIndex);
		}

		// Sorted, with equal identities merged into one summed quantity.
		std::vector<ClaimIdentity> identities(const std::vector<ClaimIdentity>& raw) {
			std::vector<ClaimIdentity> sorted = raw;
			std::sort(sorted.begin(), sorted.end(), [](const ClaimIdentity& a, const ClaimIdentity& b) { return a.order() < b.order(); });
			std::vector<ClaimIdentity> merged;
			for (const ClaimIdentity& claim : sorted) {
				if (!merged.empty() && merged.back().order() == claim.order()) {
					merged.back().quantity += claim.quantity;
				} else {
					merged.push_back(claim);
				}
			}
			return merged;
		}
	} // namespace

	std::optional<std::string> ClaimIndex::findViolation(World& world, ClaimIndex& scratch) const {
		std::vector<uint64_t> live;
		for (auto [colonist, task] : world.view<Task>()) {
			if (!task.isActive() && !task.claims.empty()) {
				return "inactive task on colonist " + std::to_string(colonist) + " holds claims";
			}
			if (!task.claims.empty() && task.instanceId == 0) {
				return "task on colonist " + std::to_string(colonist) + " holds claims without an instance id";
			}
			if (task.instanceId != 0) {
				live.push_back(task.instanceId);
			}
			if (task.suspended) {
				live.push_back(task.suspended->instanceId);
			}
			const auto unpairedInbound = [](const ClaimList& claims) {
				return std::any_of(claims.begin(), claims.end(), [&](const Claim& inbound) {
					return inbound.kind == ClaimKind::Inbound && std::none_of(claims.begin(), claims.end(), [&](const Claim& other) {
							   return other.kind == ClaimKind::InFlight && other.itemDefNameId == inbound.itemDefNameId;
						   });
				});
			};
			if (unpairedInbound(task.claims) || (task.suspended && unpairedInbound(task.suspended->claims))) {
				return "Inbound without a matching InFlight on colonist " + std::to_string(colonist);
			}
		}
		std::sort(live.begin(), live.end());

		scratch.rebuild(world);
		const auto collect = [](const ClaimIndex& index) {
			std::vector<ClaimIdentity> raw;
			raw.reserve(index.entries.size());
			for (const Entry& entry : index.entries) {
				raw.push_back({entry.holder.instanceId, entry.claim.target, entry.claim.itemDefNameId, entry.claim.slotIndex,
							   entry.claim.kind, entry.claim.targetKind, entry.claim.quantity});
			}
			return identities(raw);
		};
		const std::vector<ClaimIdentity> fresh = collect(scratch);
		const std::vector<ClaimIdentity> appended = collect(*this);

		size_t a = 0;
		for (const ClaimIdentity& claim : fresh) {
			while (a < appended.size() && appended[a].order() < claim.order()) {
				if (std::binary_search(live.begin(), live.end(), appended[a].instanceId)) {
					return "surplus claim of a live task: " + describe(appended[a]);
				}
				++a;
			}
			if (a == appended.size() || appended[a].order() != claim.order() || appended[a].quantity < claim.quantity) {
				return "claim missing from the index: " + describe(claim);
			}
			if (appended[a].quantity > claim.quantity && std::binary_search(live.begin(), live.end(), claim.instanceId)) {
				return "surplus quantity on a live task: " + describe(claim);
			}
			++a;
		}
		for (; a < appended.size(); ++a) {
			if (std::binary_search(live.begin(), live.end(), appended[a].instanceId)) {
				return "surplus claim of a live task: " + describe(appended[a]);
			}
		}
		return std::nullopt;
	}
#endif

} // namespace ecs
