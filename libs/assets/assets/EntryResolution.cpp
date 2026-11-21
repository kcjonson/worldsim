// The load's ids and references: registering every entry's full id, resolving each reference to a handle, and the
// rules that need the whole library (chain cycles, claim counts, group coverage, chains nothing starts).

#include "assets/EntryLoading.h"

#include <algorithm>
#include <bit>
#include <type_traits>

namespace assets::entry_loading {

	// --- Ids -------------------------------------------------------------------------------------------------

	template <typename Entry>
	void
	Loader::registerEach(std::vector<Entry>& entries, EntryLibrary::IdIndex& index, std::string_view element, std::string_view typeName) {
		for (uint32_t position = 0; position < entries.size(); ++position) {
			const Entry& entry = entries[position];
			const auto [found, added] = index.try_emplace(entry.id, position);
			if (!added) {
				fail(
					entry.file + ": " + entryLabel(element, entry.id) + " " + std::string(typeName) + " " + inQuotes(entry.id) +
					" is defined twice, first in " + entries[found->second].file
				);
			}
		}
		for (const Entry& entry : entries) {
			if (folderOf(entry.id).empty()) {
				continue;
			}
			const auto topLevel = index.find(shortOf(entry.id));
			if (topLevel != index.end()) {
				warn(
					entry.file + ": " + entryLabel(element, entry.id) + " " + std::string(typeName) + " " + inQuotes(entry.id) +
					" hides the top-level " + std::string(typeName) + " " + inQuotes(shortOf(entry.id)) + " (" +
					entries[topLevel->second].file + ") inside " + std::string(folderOf(entry.id)) + "/"
				);
			}
		}
	}

	void Loader::registerIds() {
		EntryLibrary& library = result.library;
		registerEach(library.behaviors, library.behaviorIds, "behavior", "behavior");
		registerEach(library.chains, library.chainIds, "chain", "chain");
		registerEach(library.queries, library.queryIds, "query", "query");
		for (uint32_t position = 0; position < library.actions.size(); ++position) {
			const ActionEntry& entry = library.actions[position];
			const char*		   element = entry.group ? "actionGroup" : "action";
			const auto [found, added] = library.actionIds.try_emplace(entry.id, position);
			if (!added) {
				fail(
					entry.file + ": " + entryLabel(element, entry.id) + " action " + inQuotes(entry.id) + " is defined twice, first in " +
					library.actions[found->second].file
				);
			}
		}
		for (const ActionEntry& entry : library.actions) {
			if (folderOf(entry.id).empty()) {
				continue;
			}
			const auto topLevel = library.actionIds.find(shortOf(entry.id));
			if (topLevel != library.actionIds.end()) {
				warn(
					entry.file + ": " + entryLabel(entry.group ? "actionGroup" : "action", entry.id) + " action " + inQuotes(entry.id) +
					" hides the top-level action " + inQuotes(shortOf(entry.id)) + " (" + library.actions[topLevel->second].file +
					") inside " + std::string(folderOf(entry.id)) + "/"
				);
			}
		}
		registerEach(library.categories, library.categoryIds, "category", "category");
		registerEach(library.work, library.workIds, "work", "work entry");
		registerEach(library.orders, library.orderIds, "order", "order");
	}

	// --- References ------------------------------------------------------------------------------------------

	template <typename Entry>
	bool Loader::resolveReference(
		EntryReference<Entry>&		 reference,
		const EntryLibrary::IdIndex& index,
		std::string_view			 inFolder,
		const std::string&			 where,
		const char*					 attribute,
		std::string_view			 typeName
	) {
		if (reference.written.empty()) {
			return false;
		}
		auto found = index.end();
		if (reference.written.find('/') != std::string::npos) {
			found = index.find(reference.written);
		} else {
			if (!inFolder.empty()) {
				found = index.find(std::string(inFolder) + "/" + reference.written);
			}
			if (found == index.end()) {
				found = index.find(reference.written);
			}
		}
		if (found == index.end()) {
			fail(where + " " + attribute + " " + inQuotes(reference.written) + " names no " + std::string(typeName));
			return false;
		}
		reference.handle = EntryHandle<Entry>{found->second};
		return true;
	}

	void Loader::checkNeedDefined(NeedId need, const std::string& where, const char* attribute) {
		if (need.index < result.library.needIds.size() && !definedNeeds.contains(need.index)) {
			fail(
				where + " " + attribute + " " + inQuotes(result.library.needIds[need.index]) + " names no need of any behavior or creature"
			);
		}
	}

	void Loader::resolveSatisfiedBy(std::vector<SatisfiedBy>& rows, std::string_view inFolder, const std::string& where) {
		const EntryLibrary& library = result.library;
		for (std::size_t index = 0; index < rows.size(); ++index) {
			const std::string rowWhere = where + " " + rowLabel(index, "satisfiedBy");
			if (rows[index].chain) {
				resolveReference(*rows[index].chain, library.chainIds, inFolder, rowWhere, "chain", "chain");
			}
			if (rows[index].work) {
				resolveReference(*rows[index].work, library.workIds, inFolder, rowWhere, "work", "work entry");
			}
		}
	}

	void Loader::resolve() {
		EntryLibrary& library = result.library;
		for (ChainEntry& chain : library.chains) {
			const std::string_view inFolder = folderOf(chain.id);
			const std::string	   label = chain.file + ": " + entryLabel("chain", chain.id);
			for (std::size_t index = 0; index < chain.steps.size(); ++index) {
				std::visit(
					[&](auto& step) {
						using Step = std::decay_t<decltype(step)>;
						if constexpr (std::is_same_v<Step, GoStep>) {
							if (step.query) {
								resolveReference(
									*step.query, library.queryIds, inFolder, label + " " + stepLabel(index, "go"), "query", "query"
								);
							}
						} else if constexpr (std::is_same_v<Step, DoStep>) {
							resolveReference(
								step.action, library.actionIds, inFolder, label + " " + stepLabel(index, "do"), "action", "action"
							);
						} else if constexpr (std::is_same_v<Step, HaveStep>) {
							resolveReference(
								step.fetch, library.chainIds, inFolder, label + " " + stepLabel(index, "have"), "fetch", "chain"
							);
						} else if constexpr (std::is_same_v<Step, RunStep>) {
							resolveReference(
								step.chain, library.chainIds, inFolder, label + " " + stepLabel(index, "run"), "chain", "chain"
							);
						}
					},
					chain.steps[index].step
				);
			}
		}
		for (ActionEntry& action : library.actions) {
			const std::string label = action.file + ": " + entryLabel(action.group ? "actionGroup" : "action", action.id);
			if (!action.group) {
				continue;
			}
			std::vector<const ActionEntry*> members;
			for (EntryReference<ActionEntry>& member : action.members) {
				if (!resolveReference(member, library.actionIds, folderOf(action.id), label, "member", "action")) {
					continue;
				}
				const ActionEntry& resolved = library.actions[member.handle.index];
				if (resolved.group) {
					fail(label + " member " + inQuotes(member.written) + " is an action group; a group's members are actions");
				} else if (!resolved.on) {
					fail(label + " member " + inQuotes(member.written) + " has no on, so no target can choose it");
				} else {
					for (const ActionEntry* earlier : members) {
						if (sameTarget(*earlier->on, *resolved.on)) {
							fail(
								label + " members " + inQuotes(shortOf(earlier->id)) + " and " + inQuotes(member.written) +
								" both work on " + std::string(targetName(*resolved.on))
							);
						}
					}
					members.push_back(&resolved);
				}
			}
		}
		for (NeedEntry& need : library.needs) {
			const std::string_view inFolder = folderOf(library.behaviors[need.behavior.index].id);
			resolveSatisfiedBy(need.satisfiedBy, inFolder, need.file + ": <need id=" + inQuotes(library.needIds[need.id.index]) + ">");
		}
		for (BehaviorEntry& behavior : library.behaviors) {
			const std::string_view inFolder = folderOf(behavior.id);
			const std::string	   label = behavior.file + ": " + entryLabel("behavior", behavior.id);
			for (std::size_t index = 0; index < behavior.idle.size(); ++index) {
				resolveReference(
					behavior.idle[index].chain, library.chainIds, inFolder, label + " " + rowLabel(index, "idle"), "chain", "chain"
				);
			}
			if (behavior.lowMood) {
				if (behavior.lowMood->target) {
					resolveReference(
						*behavior.lowMood->target, library.queryIds, inFolder, label + " <lowMood> <target>", "query", "query"
					);
				}
				resolveReference(behavior.lowMood->effect, library.chainIds, inFolder, label + " <lowMood> <effect>", "chain", "chain");
			}
		}
		for (WorkEntry& work : library.work) {
			const std::string label = work.file + ": " + entryLabel("work", work.id);
			resolveReference(work.category, library.categoryIds, "", label, "category", "category");
			if (work.source) {
				resolveReference(*work.source, library.queryIds, "", label + " <source>", "query", "query");
			}
			resolveReference(work.chain, library.chainIds, "", label + " <chain>", "id", "chain");
		}
		for (OrderEntry& order : library.orders) {
			resolveReference(order.chain, library.chainIds, "", order.file + ": " + entryLabel("order", order.id), "chain", "chain");
		}
	}

	void Loader::checkNeedReferences() {
		const EntryLibrary& library = result.library;
		for (const ChainEntry& chain : library.chains) {
			for (std::size_t index = 0; index < chain.steps.size(); ++index) {
				if (const auto* needStep = std::get_if<NeedStep>(&chain.steps[index].step)) {
					checkNeedDefined(
						needStep->need, chain.file + ": " + entryLabel("chain", chain.id) + " " + stepLabel(index, "need"), "id"
					);
				}
			}
		}
		for (const ActionEntry& action : library.actions) {
			const std::string label = action.file + ": " + entryLabel("action", action.id);
			for (std::size_t index = 0; index < action.effects.size(); ++index) {
				const std::string where = label + " effect " + std::to_string(index + 1);
				if (const auto* restore = std::get_if<RestoreEffect>(&action.effects[index])) {
					checkNeedDefined(restore->need, where + " <restore>", "need");
				} else if (const auto* consume = std::get_if<ConsumeEffect>(&action.effects[index]); consume && consume->need) {
					checkNeedDefined(*consume->need, where + " <consume>", "need");
				}
			}
		}
		for (const WorkEntry& work : library.work) {
			if (work.need) {
				checkNeedDefined(*work.need, work.file + ": " + entryLabel("work", work.id), "need");
			}
		}
	}

	// --- Chain rules -----------------------------------------------------------------------------------------

	void Loader::checkChains() {
		const EntryLibrary& library = result.library;
		const auto			childChains = [&](const ChainEntry& chain) {
			 std::vector<uint32_t> children;
			 for (const ChainStep& step : chain.steps) {
				 if (const auto* have = std::get_if<HaveStep>(&step.step); have && have->fetch.handle.valid()) {
					 children.push_back(have->fetch.handle.index);
				 } else if (const auto* run = std::get_if<RunStep>(&step.step); run && run->chain.handle.valid()) {
					 children.push_back(run->chain.handle.index);
				 }
			 }
			 return children;
		};

		// A chain that runs itself, through `run` or a `have` fetch, never ends.
		enum class Visit : uint8_t { New, Open, Done };
		std::vector<Visit>	  visits(library.chains.size(), Visit::New);
		std::vector<uint32_t> path;
		bool				  anyCycle = false;
		const auto			  visit = [&](auto& self, uint32_t chain) -> void {
			   visits[chain] = Visit::Open;
			   path.push_back(chain);
			   for (const uint32_t child : childChains(library.chains[chain])) {
				   if (visits[child] == Visit::Open) {
					   std::string loop;
					   const auto  start = std::ranges::find(path, child);
					   for (auto at = start; at != path.end(); ++at) {
						   loop += inQuotes(library.chains[*at].id) + " -> ";
					   }
					   anyCycle = true;
					   fail(
						   library.chains[child].file + ": " + entryLabel("chain", library.chains[child].id) + " runs itself: " + loop +
						   inQuotes(library.chains[child].id)
					   );
				   } else if (visits[child] == Visit::New) {
					   self(self, child);
				   }
			   }
			   path.pop_back();
			   visits[chain] = Visit::Done;
		};
		for (uint32_t chain = 0; chain < library.chains.size(); ++chain) {
			if (visits[chain] == Visit::New) {
				visit(visit, chain);
			}
		}

		// The claims a chain names in its own steps and in every chain it may run: four on a task, of which the
		// two that follow the goods (inbound, in-flight) are what a suspended record keeps. A step naming both use
		// and source_quantity holds the one that fits its target, so the two count once. The count walks the runs,
		// so it waits until no chain runs itself.
		constexpr uint8_t kGoodsClaims =
			(1U << static_cast<uint8_t>(ClaimKind::Inbound)) | (1U << static_cast<uint8_t>(ClaimKind::InFlight));
		constexpr uint8_t kTargetClaims =
			(1U << static_cast<uint8_t>(ClaimKind::Use)) | (1U << static_cast<uint8_t>(ClaimKind::SourceQuantity));
		std::vector<std::optional<std::pair<int, int>>> totals(library.chains.size());
		const auto										claimTotal = [&](auto& self, uint32_t chain) -> std::pair<int, int> {
			 if (totals[chain]) {
				 return *totals[chain];
			 }
			 std::pair<int, int> total{0, 0};
			 for (const ChainStep& step : library.chains[chain].steps) {
				 if (const auto* doStep = std::get_if<DoStep>(&step.step)) {
					 total.first += std::popcount(doStep->claims) - ((doStep->claims & kTargetClaims) == kTargetClaims ? 1 : 0);
					 total.second += std::popcount(static_cast<uint8_t>(doStep->claims & kGoodsClaims));
				 }
			 }
			 for (const uint32_t child : childChains(library.chains[chain])) {
				 const std::pair<int, int> childTotal = self(self, child);
				 total.first += childTotal.first;
				 total.second += childTotal.second;
			 }
			 totals[chain] = total;
			 return total;
		};
		for (uint32_t chain = 0; chain < library.chains.size(); ++chain) {
			const ChainEntry& entry = library.chains[chain];
			const std::string label = entry.file + ": " + entryLabel("chain", entry.id);
			if (!anyCycle) {
				const std::pair<int, int> total = claimTotal(claimTotal, chain);
				if (total.first > 4) {
					fail(label + " names " + std::to_string(total.first) + " claims with the chains it runs; a task holds four");
				}
				if (total.second > 2) {
					fail(
						label + " names " + std::to_string(total.second) +
						" inbound and in_flight claims with the chains it runs; a suspended record holds two"
					);
				}
			}

			for (std::size_t index = 0; index < entry.steps.size(); ++index) {
				const auto* doStep = std::get_if<DoStep>(&entry.steps[index].step);
				if (doStep == nullptr || !doStep->action.handle.valid()) {
					continue;
				}
				const ActionEntry&				action = library.actions[doStep->action.handle.index];
				std::vector<const ActionEntry*> performed;
				if (action.group) {
					for (const EntryReference<ActionEntry>& member : action.members) {
						if (member.handle.valid()) {
							performed.push_back(&library.actions[member.handle.index]);
						}
					}
				} else {
					performed.push_back(&action);
				}
				const std::string where = label + " " + stepLabel(index, "do");
				if (doStep->amount) {
					const bool movesGoods = std::ranges::any_of(performed, [](const ActionEntry* candidate) {
						return std::ranges::any_of(candidate->effects, [](const EffectRow& row) {
							return std::holds_alternative<TakeEffect>(row) || std::holds_alternative<PutEffect>(row);
						});
					});
					if (!movesGoods) {
						fail(
							where + " amount is for a step whose action takes or puts; " + inQuotes(doStep->action.written) +
							" does neither"
						);
					}
				}
				if (!action.group || index == 0) {
					continue;
				}
				// The group's members must cover every target the query of the go step before it can return.
				const GoStep* go = nullptr;
				for (std::size_t back = index; back-- > 0;) {
					if ((go = std::get_if<GoStep>(&entry.steps[back].step)) != nullptr) {
						break;
					}
				}
				if (go == nullptr || !go->query || !go->query->handle.valid()) {
					continue;
				}
				std::vector<std::string_view> uncovered;
				for (const FromRow& from : library.queries[go->query->handle.index].from) {
					std::optional<ActionTarget> target;
					switch (from.source) {
						case CandidateSource::Yields:
							target = ActionTarget{.kind = ActionTarget::Kind::Capability, .capability = CapabilityType::Harvestable};
							break;
						case CandidateSource::Pile:
							target = ActionTarget{.kind = ActionTarget::Kind::LooseItem};
							break;
						case CandidateSource::Box:
							target = ActionTarget{.kind = ActionTarget::Kind::Capability, .capability = CapabilityType::Storage};
							break;
						case CandidateSource::Capability:
							target = ActionTarget{.kind = ActionTarget::Kind::Capability, .capability = from.capability};
							break;
						default:
							break;
					}
					if (!target) {
						continue;
					}
					const bool covered = std::ranges::any_of(performed, [&](const ActionEntry* member) {
						return member->on && sameTarget(*member->on, *target);
					});
					if (!covered && std::ranges::find(uncovered, targetName(*target)) == uncovered.end()) {
						uncovered.push_back(targetName(*target));
					}
				}
				if (!uncovered.empty()) {
					fail(
						where + " action group " + inQuotes(doStep->action.written) + " has no member for " + joined(uncovered) +
						", which query " + inQuotes(go->query->written) + " can return"
					);
				}
			}
		}
	}

	void Loader::checkQueryBounds() {
		const EntryLibrary& library = result.library;
		std::vector<bool>	ranked(library.queries.size(), false);
		const auto			ranks = [&](const std::optional<EntryReference<QueryEntry>>& query) {
			 if (query && query->handle.valid()) {
				 ranked[query->handle.index] = true;
			 }
		};
		for (const ChainEntry& chain : library.chains) {
			for (const ChainStep& step : chain.steps) {
				if (const auto* go = std::get_if<GoStep>(&step.step)) {
					ranks(go->query);
				}
			}
		}
		for (const WorkEntry& work : library.work) {
			ranks(work.source);
		}
		for (const BehaviorEntry& behavior : library.behaviors) {
			if (behavior.lowMood) {
				ranks(behavior.lowMood->target);
			}
		}
		for (std::size_t index = 0; index < library.queries.size(); ++index) {
			const QueryEntry& query = library.queries[index];
			if (ranked[index] && !boundsDistance(query, FactRowKind::Weigh) && !boundsDistance(query, FactRowKind::Exclude)) {
				fail(
					query.file + ": " + entryLabel("query", query.id) +
					" neither weighs distance by a negative number nor excludes it above a bound, so nothing ends its search"
				);
			}
		}
	}

	void Loader::warnUnstarted() {
		const EntryLibrary&	  library = result.library;
		std::vector<bool>	  started(library.chains.size(), false);
		std::vector<uint32_t> pending;
		const auto			  start = [&](const EntryReference<ChainEntry>& chain) {
			   if (chain.handle.valid() && !started[chain.handle.index]) {
				   started[chain.handle.index] = true;
				   pending.push_back(chain.handle.index);
			   }
		};
		const auto startRows = [&](const std::vector<SatisfiedBy>& rows) {
			for (const SatisfiedBy& row : rows) {
				if (row.chain) {
					start(*row.chain);
				}
			}
		};
		for (const NeedEntry& need : library.needs) {
			startRows(need.satisfiedBy);
		}
		for (const BehaviorEntry& behavior : library.behaviors) {
			for (const IdleRow& idle : behavior.idle) {
				start(idle.chain);
			}
			if (behavior.lowMood) {
				start(behavior.lowMood->effect);
			}
		}
		for (const WorkEntry& work : library.work) {
			start(work.chain);
		}
		for (const OrderEntry& order : library.orders) {
			start(order.chain);
		}
		for (const CreatureEntry& creature : library.creatures) {
			for (const CreatureNeed& need : creature.needs) {
				startRows(need.satisfiedBy);
			}
		}
		while (!pending.empty()) {
			const uint32_t chain = pending.back();
			pending.pop_back();
			for (const ChainStep& step : library.chains[chain].steps) {
				if (const auto* have = std::get_if<HaveStep>(&step.step)) {
					start(have->fetch);
				} else if (const auto* run = std::get_if<RunStep>(&step.step)) {
					start(run->chain);
				}
			}
		}
		for (uint32_t chain = 0; chain < library.chains.size(); ++chain) {
			if (!started[chain]) {
				const ChainEntry& entry = library.chains[chain];
				warn(
					entry.file + ": " + entryLabel("chain", entry.id) + " chain " + inQuotes(entry.id) +
					" is started by nothing: no need, idle row, work entry, order, or episode names it"
				);
			}
		}
	}

} // namespace assets::entry_loading

namespace assets {

	std::vector<std::string> checkEntryNames(const EntryLibrary& library, const EntryNameSets& names) {
		using namespace entry_loading;
		std::vector<std::string> errors;
		for (const QueryEntry& query : library.queries) {
			const std::string label = query.file + ": " + entryLabel("query", query.id);
			for (const FromRow& from : query.from) {
				if (from.source == CandidateSource::Kind && !names.isKind(from.kind)) {
					errors.push_back(label + " " + rowLabel(from.row, "from") + " kind " + inQuotes(from.kind) + " names no kind");
				}
			}
			for (const ScaleRow& scale : query.scales) {
				if (!names.isTrait(scale.trait)) {
					errors.push_back(label + " " + rowLabel(scale.row, "scale") + " trait " + inQuotes(scale.trait) + " names no trait");
				}
			}
		}
		for (const ChainEntry& chain : library.chains) {
			for (std::size_t index = 0; index < chain.steps.size(); ++index) {
				const auto* think = std::get_if<ThinkStep>(&chain.steps[index].step);
				if (think != nullptr && !names.isThought(think->thought)) {
					errors.push_back(
						chain.file + ": " + entryLabel("chain", chain.id) + " " + stepLabel(index, "think") + " thought " +
						inQuotes(think->thought) + " names no thought type"
					);
				}
			}
		}
		return errors;
	}

} // namespace assets
