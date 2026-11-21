#include "ChainEntries.h"

#include "ThoughtTypes.h"
#include "components/ChainProgress.h"
#include "components/Task.h"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <string_view>
#include <variant>

namespace ecs {

	namespace {

		constexpr std::array<const char*, 8> kStepElements = {"go", "do", "have", "think", "need", "drop", "run", "leave_colony"};
		static_assert(
			kStepElements.size() == std::variant_size_v<decltype(assets::ChainStep::step)>,
			"an element name for every step kind"
		);

		[[nodiscard]] std::string_view shortOf(std::string_view id) {
			const std::size_t slash = id.rfind('/');
			return slash == std::string_view::npos ? id : id.substr(slash + 1);
		}

		[[nodiscard]] std::string chainLabel(const assets::ChainEntry& chain) {
			return chain.file + ": <chain id=\"" + std::string(shortOf(chain.id)) + "\">";
		}

		[[nodiscard]] std::string whereOf(const assets::ChainEntry& chain, std::size_t step) {
			return chainLabel(chain) + " step " + std::to_string(step + 1) + " <" + kStepElements[chain.steps[step].step.index()] + ">";
		}

		[[nodiscard]] uint8_t bit(assets::ClaimKind kind) {
			return static_cast<uint8_t>(1U << static_cast<unsigned>(kind));
		}

		/// The chain a `run` or `have` step runs; none for any other step.
		[[nodiscard]] assets::EntryHandle<assets::ChainEntry> nested(const assets::ChainStep& step) {
			if (const auto* run = std::get_if<assets::RunStep>(&step.step)) {
				return run->chain.handle;
			}
			if (const auto* have = std::get_if<assets::HaveStep>(&step.step)) {
				return have->fetch.handle;
			}
			return {};
		}

		/// Whether `chain` and every chain it may run nest within ChainProgress::kMaxDepth, `running` holding the chains
		/// already running above it; the first nesting past the limit is reported once. The loader refuses a chain that runs
		/// itself, so the limit ends every walk.
		bool checkNesting(
			const assets::EntryLibrary&				library,
			assets::EntryHandle<assets::ChainEntry> chain,
			std::vector<uint32_t>&					running,
			std::vector<std::string>&				errors,
			const assets::ChainEntry&				root
		) {
			if (running.size() >= ChainProgress::kMaxDepth) {
				errors.push_back(
					chainLabel(root) + " nests chains more than " + std::to_string(ChainProgress::kMaxDepth) +
					" deep through <run> and <have>"
				);
				return false;
			}
			running.push_back(chain.index);
			bool fits = true;
			for (const assets::ChainStep& step : library.chains[chain.index].steps) {
				const assets::EntryHandle<assets::ChainEntry> inner = nested(step);
				if (inner.valid() && !checkNesting(library, inner, running, errors, root)) {
					fits = false;
					break;
				}
			}
			running.pop_back();
			return fits;
		}

		enum class TargetData : uint8_t { None, Take, Put, Work, Craft, Install };

		[[nodiscard]] uint8_t dataBit(TargetData data) {
			return static_cast<uint8_t>(1U << static_cast<unsigned>(data));
		}

		/// The kinds of target data `values`' rows need, as bits of TargetData.
		[[nodiscard]] uint8_t targetDataOf(const ActionValues& values) {
			uint8_t needed = 0;
			for (const EffectRow& row : values.effects) {
				if (std::holds_alternative<TakeRow>(row)) {
					needed |= dataBit(TargetData::Take);
				} else if (std::holds_alternative<PutRow>(row)) {
					needed |= dataBit(TargetData::Put);
				} else if (std::holds_alternative<WorkRow>(row)) {
					needed |= dataBit(TargetData::Work);
				} else if (std::holds_alternative<CraftRow>(row)) {
					needed |= dataBit(TargetData::Craft);
				} else if (std::holds_alternative<InstallRow>(row)) {
					needed |= dataBit(TargetData::Install);
				} else if (const auto* restore = std::get_if<RestoreRow>(&row); restore != nullptr && restore->onTarget) {
					needed |= dataBit(TargetData::None);
				}
			}
			return needed;
		}

		/// The actions a `do` step may perform: its action, or each member of its group.
		[[nodiscard]] std::vector<assets::EntryHandle<assets::ActionEntry>>
		performedBy(const assets::EntryLibrary& library, const assets::DoStep& step) {
			const assets::ActionEntry& entry = library.actions[step.action.handle.index];
			if (!entry.group) {
				return {step.action.handle};
			}
			std::vector<assets::EntryHandle<assets::ActionEntry>> members;
			for (const auto& member : entry.members) {
				members.push_back(member.handle);
			}
			return members;
		}

		/// Whether `chain`, or a chain it runs, has a step that takes.
		[[nodiscard]] bool
		takesIn(const assets::EntryLibrary& library, const ActionEntries& actions, const assets::ChainEntry& chain, std::size_t steps) {
			for (std::size_t index = 0; index < steps && index < chain.steps.size(); ++index) {
				const assets::ChainStep& step = chain.steps[index];
				if (const auto* doStep = std::get_if<assets::DoStep>(&step.step)) {
					for (const auto handle : performedBy(library, *doStep)) {
						if ((targetDataOf(actions.of(handle)) & dataBit(TargetData::Take)) != 0) {
							return true;
						}
					}
				}
				const assets::EntryHandle<assets::ChainEntry> inner = nested(step);
				if (inner.valid() && takesIn(library, actions, library.chains[inner.index], library.chains[inner.index].steps.size())) {
					return true;
				}
			}
			return false;
		}

		/// Whether an action with `on` can act on what a `<from>` row of `source` finds; true for a source it says nothing of.
		[[nodiscard]] bool actsOn(const assets::ActionTarget& on, assets::CandidateSource source) {
			switch (source) {
				case assets::CandidateSource::Yields:
					return on.kind == assets::ActionTarget::Kind::Capability && on.capability == assets::CapabilityType::Harvestable;
				case assets::CandidateSource::Pile:
					return on.kind == assets::ActionTarget::Kind::LooseItem;
				case assets::CandidateSource::Box:
					return on.kind == assets::ActionTarget::Kind::Capability && on.capability == assets::CapabilityType::Storage;
				default:
					return true;
			}
		}

		void checkDoStep(
			const assets::EntryLibrary& library,
			const ActionEntries&		actions,
			const assets::ChainEntry&	chain,
			std::size_t					index,
			const std::string&			where,
			std::vector<std::string>&	errors
		) {
			const auto&	  doStep = std::get<assets::DoStep>(chain.steps[index].step);
			const bool	  inbound = (doStep.claims & bit(assets::ClaimKind::Inbound)) != 0;
			const bool	  inFlight = (doStep.claims & bit(assets::ClaimKind::InFlight)) != 0;
			const auto	  performed = performedBy(library, doStep);
			const uint8_t takeAndInstall = dataBit(TargetData::Take) | dataBit(TargetData::Install);
			if (inbound != inFlight) {
				errors.push_back(
					where + " names " + (inbound ? "inbound without in_flight" : "in_flight without inbound") +
					"; goods on their way to a destination are claimed toward their root too"
				);
			}
			// The nearest go before this step in its chain, whatever steps lie between; none when no go comes before it.
			const assets::GoStep* go = nullptr;
			for (std::size_t earlier = index; earlier-- > 0;) {
				go = std::get_if<assets::GoStep>(&chain.steps[earlier].step);
				if (go != nullptr) {
					break;
				}
			}
			for (const auto handle : performed) {
				const ActionValues&		   values = actions.of(handle);
				const assets::ActionEntry& entry = library.actions[handle.index];
				const uint8_t			   needed = targetDataOf(values);
				const uint8_t			   targeted = needed & static_cast<uint8_t>(~dataBit(TargetData::Install));
				if (std::popcount(static_cast<uint8_t>(needed & ~dataBit(TargetData::None))) > 1 &&
					(needed & ~dataBit(TargetData::None)) != takeAndInstall) {
					errors.push_back(
						where + " performs \"" + entry.id +
						"\", whose rows act on two kinds of target (a take, a put, a work, a craft, an install); one step has one target"
					);
				}
				if ((needed & dataBit(TargetData::Take)) != 0 && (inbound || inFlight)) {
					errors.push_back(
						where + " claims inbound or in_flight on a take; the goods' claims are on the step that delivers them"
					);
				}
				if (targeted != 0 && !doStep.on && go == nullptr) {
					errors.push_back(where + " performs \"" + entry.id + "\", which acts on a target, with no `on` and no <go> before it");
				}
				// An action that names what it acts on, after a query that can return nothing of that kind.
				const bool fromGroup = library.actions[doStep.action.handle.index].group;
				if (!fromGroup && entry.on.has_value() && !doStep.on && go != nullptr && go->query) {
					const auto& from = library.queries[go->query->handle.index].from;
					const bool	reachable =
						std::ranges::any_of(from, [&](const assets::FromRow& row) { return actsOn(*entry.on, row.source); });
					if (!from.empty() && !reachable) {
						errors.push_back(
							where + " performs \"" + entry.id + "\" on what query \"" + library.queries[go->query->handle.index].id +
							"\" picks, which is never a kind it acts on"
						);
					}
				}
			}
		}

	} // namespace

	ChainEntriesLoad resolveChainEntries(const assets::EntryLibrary& library, const ActionEntries& actions, const ThoughtTypes* thoughts) {
		ChainEntriesLoad	  load;
		ChainEntries&		  chains = load.chains;
		std::vector<uint32_t> running;
		for (const assets::ChainEntry& chain : library.chains) {
			chains.firstStep.push_back(static_cast<uint32_t>(chains.steps.size()));
			if (chain.steps.size() > std::numeric_limits<uint8_t>::max()) {
				load.errors.push_back(chainLabel(chain) + " has more than 255 steps");
			}
			for (std::size_t index = 0; index < chain.steps.size(); ++index) {
				const assets::ChainStep& step = chain.steps[index];
				const std::string		 where = whereOf(chain, index);
				ChainStepValues			 values;
				if (const auto* need = std::get_if<assets::NeedStep>(&step.step)) {
					const std::optional<NeedType> tracked = trackedNeed(library, need->need);
					if (tracked) {
						values.need = *tracked;
					} else {
						load.errors.push_back(where + " need \"" + library.needIds[need->need.index] + "\" is no need the sim tracks");
					}
				} else if (const auto* think = std::get_if<assets::ThinkStep>(&step.step)) {
					const std::optional<ThoughtEventTypeId> thought =
						thoughts != nullptr ? thoughts->findType(think->thought) : std::nullopt;
					if (thought) {
						values.thought = *thought;
					} else {
						load.errors.push_back(where + " thought \"" + think->thought + "\" is no thought type");
					}
				} else if (std::holds_alternative<assets::DoStep>(step.step)) {
					checkDoStep(library, actions, chain, index, where, load.errors);
				} else if (std::holds_alternative<assets::GoStep>(step.step)) {
					if ((step.ends & (1U << static_cast<unsigned>(assets::EndEvent::HeldGone))) != 0) {
						load.errors.push_back(where + " ends on held_gone, which only a <do> holds goods for");
					}
				} else if (std::holds_alternative<assets::DropStep>(step.step)) {
					if (!takesIn(library, actions, chain, index)) {
						load.errors.push_back(where + " sets down what the chain took, and no step before it takes");
					}
				} else if (std::holds_alternative<assets::LeaveColonyStep>(step.step)) {
					load.errors.push_back(where + " ends colony membership, which the sim does not have");
				}
				chains.steps.push_back(values);
			}
			checkNesting(library, {static_cast<uint32_t>(&chain - library.chains.data())}, running, load.errors, chain);
			running.clear();
		}
		return load;
	}

} // namespace ecs
