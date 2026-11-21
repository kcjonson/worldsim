#include "ChainEntries.h"

#include "ThoughtTypes.h"
#include "components/ChainProgress.h"
#include "components/Task.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <string_view>
#include <variant>

namespace ecs {

	namespace {

		constexpr std::array<const char*, 8> kStepElements = {"go", "do", "have", "think", "need", "drop", "run", "leave_colony"};

		[[nodiscard]] std::string_view shortOf(std::string_view id) {
			const std::size_t slash = id.rfind('/');
			return slash == std::string_view::npos ? id : id.substr(slash + 1);
		}

		[[nodiscard]] std::string whereOf(const assets::ChainEntry& chain, std::size_t step) {
			return chain.file + ": <chain id=\"" + std::string(shortOf(chain.id)) + "\"> step " + std::to_string(step + 1) + " <" +
				   kStepElements[chain.steps[step].step.index()] + ">";
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
					root.file + ": <chain id=\"" + std::string(shortOf(root.id)) + "\"> nests chains more than " +
					std::to_string(ChainProgress::kMaxDepth) + " deep through <run> and <have>"
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

		/// The kinds of target data `values`' rows need, as bits of TargetData.
		[[nodiscard]] uint8_t targetDataOf(const ActionValues& values) {
			uint8_t needed = 0;
			for (const EffectRow& row : values.effects) {
				TargetData data = TargetData::None;
				if (std::holds_alternative<TakeRow>(row)) {
					data = TargetData::Take;
				} else if (std::holds_alternative<PutRow>(row)) {
					data = TargetData::Put;
				} else if (std::holds_alternative<WorkRow>(row)) {
					data = TargetData::Work;
				} else if (std::holds_alternative<CraftRow>(row)) {
					data = TargetData::Craft;
				} else if (std::holds_alternative<InstallRow>(row)) {
					data = TargetData::Install;
				}
				if (data != TargetData::None) {
					needed |= static_cast<uint8_t>(1U << static_cast<unsigned>(data));
				}
			}
			return needed;
		}

		void checkDoStep(
			const assets::EntryLibrary& library,
			const ActionEntries&		actions,
			const assets::DoStep&		step,
			const std::string&			where,
			std::vector<std::string>&	errors
		) {
			const bool inbound = (step.claims & bit(assets::ClaimKind::Inbound)) != 0;
			const bool inFlight = (step.claims & bit(assets::ClaimKind::InFlight)) != 0;
			if (inbound != inFlight) {
				errors.push_back(
					where + " names " + (inbound ? "inbound without in_flight" : "in_flight without inbound") +
					"; goods on their way to a destination are claimed toward their root too"
				);
			}
			const assets::ActionEntry&							  entry = library.actions[step.action.handle.index];
			std::vector<assets::EntryHandle<assets::ActionEntry>> performed;
			if (entry.group) {
				for (const auto& member : entry.members) {
					performed.push_back(member.handle);
				}
			} else {
				performed.push_back(step.action.handle);
			}
			const auto takeAndInstall =
				static_cast<uint8_t>((1U << static_cast<unsigned>(TargetData::Take)) | (1U << static_cast<unsigned>(TargetData::Install)));
			for (const auto handle : performed) {
				const uint8_t needed = targetDataOf(actions.of(handle));
				if (std::popcount(needed) > 1 && needed != takeAndInstall) {
					errors.push_back(
						where + " performs \"" + library.actions[handle.index].id +
						"\", whose rows act on two kinds of target (a take, a put, a work, a craft, an install); one step has one target"
					);
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
				load.errors.push_back(chain.file + ": <chain id=\"" + std::string(shortOf(chain.id)) + "\"> has more than 255 steps");
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
				} else if (const auto* doStep = std::get_if<assets::DoStep>(&step.step)) {
					checkDoStep(library, actions, *doStep, where, load.errors);
				} else if (std::holds_alternative<assets::LeaveColonyStep>(step.step)) {
					load.errors.push_back(
						where + " ends colony membership, which the sim does not have yet; colony membership (behavior-data-migration.md, "
								"ticket 32) builds it"
					);
				}
				chains.steps.push_back(values);
			}
			checkNesting(library, {static_cast<uint32_t>(&chain - library.chains.data())}, running, load.errors, chain);
			running.clear();
		}
		return load;
	}

} // namespace ecs
