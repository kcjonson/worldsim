#include "ThoughtSourceSystem.h"

#include "../World.h"
#include "../components/Colonist.h"
#include "../components/Needs.h"
#include "../components/ThoughtSourceState.h"
#include "../components/Thoughts.h"
#include "ThoughtSystem.h"
#include "TimeSystem.h"

#include <assets/AssetRegistry.h>

#include <random/Pcg32.h>
#include <random/SplitMix64.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace ecs {

	namespace {
		/// The latest game time a thought arrived on the colonist, a repeat folded into a held one included.
		std::optional<double> latestThoughtAt(const Thoughts& thoughts) {
			if (thoughts.record.empty()) {
				return std::nullopt;
			}
			double latest = thoughts.record.back().arrivedAt;
			forEachHeldThought(thoughts, [&latest](const Thought& thought) { latest = std::max(latest, thought.lastRepeatedAt); });
			return latest;
		}

		/// A kind a line can name: one with a definition that has a display label.
		bool hasDisplayLabel(uint32_t definition) {
			const assets::AssetDefinition* found = assets::AssetRegistry::Get().getDefinitionById(definition);
			return found != nullptr && !found->label.empty();
		}
	} // namespace

	ThoughtSourceSystem::ThoughtSourceSystem(const ThoughtTypes& types, uint64_t randomSeed)
		: thoughtTypes(types),
		  seed(randomSeed) {
		const std::vector<ThoughtSource>& sources = thoughtTypes.sources();
		for (std::size_t index = 0; index < sources.size(); ++index) {
			if (sources[index].trigger == ThoughtTrigger::Idle) {
				idleSources.push_back(index);
			}
		}
	}

	void ThoughtSourceSystem::listenTo(ActionSystem& actions, VisionSystem& vision) {
		actions.addActionObserver([this](const ActionEvent& event) {
			heardEvents.push_back({
				.colonist = event.colonist,
				.trigger = event.moment == ActionMoment::Started ? ThoughtTrigger::ActionStarts : ThoughtTrigger::ActionFinishes,
				.action = event.type,
				.definition = event.aboutDefinition,
			});
		});
		vision.addDiscoveryObserver([this](const VisionSystem::Discovery& discovery) {
			heardEvents.push_back({
				.colonist = discovery.colonist,
				.trigger =
					discovery.kind == VisionSystem::DiscoveryKind::NewKind ? ThoughtTrigger::FirstSight : ThoughtTrigger::RecipeLearned,
				.definition = discovery.definition,
			});
		});
	}

	void ThoughtSourceSystem::update(float /*deltaTime*/) {
		const double now = world->getSystem<TimeSystem>().simulationSeconds();

		// What was heard is taken first: a thought created below may lead an observer to report more, which
		// waits for the next tick.
		std::swap(heardScratch, heardEvents);
		heardEvents.clear();
		for (const HeardEvent& event : heardScratch) {
			if (world->hasComponent<Colonist>(event.colonist)) {
				hear(event, now);
			}
		}
		heardScratch.clear();

		colonistScratch.clear();
		for (auto [colonist, colonistComponent, needs] : world->view<Colonist, NeedsComponent>()) {
			colonistScratch.push_back(colonist);
		}
		for (const EntityID colonist : colonistScratch) {
			if (!world->isAlive(colonist)) {
				continue;
			}
			watchNeeds(colonist, stateOf(colonist, now));
		}
		for (const EntityID colonist : colonistScratch) {
			if (!world->isAlive(colonist)) {
				continue;
			}
			rollIdle(colonist, stateOf(colonist, now), now);
		}
	}

	ThoughtSourceState& ThoughtSourceSystem::stateOf(EntityID colonist, double now) {
		if (ThoughtSourceState* state = world->getComponent<ThoughtSourceState>(colonist); state != nullptr) {
			return *state;
		}
		ThoughtSourceState				  state;
		const std::vector<ThoughtSource>& sources = thoughtTypes.sources();
		state.idleRollsAt.reserve(idleSources.size());
		for (const std::size_t index : idleSources) {
			state.idleRollsAt.push_back(now + sources[index].rollEverySimulationSeconds);
		}
		return world->addComponent<ThoughtSourceState>(colonist, std::move(state));
	}

	void ThoughtSourceSystem::hear(const HeardEvent& event, double now) {
		if (event.trigger == ThoughtTrigger::FirstSight && !hasDisplayLabel(event.definition)) {
			return;
		}
		const std::vector<ThoughtSource>& sources = thoughtTypes.sources();
		for (std::size_t index = 0; index < sources.size(); ++index) {
			const ThoughtSource& source = sources[index];
			if (source.trigger != event.trigger || !world->isAlive(event.colonist)) {
				continue;
			}
			const bool actionTrigger = source.trigger == ThoughtTrigger::ActionStarts || source.trigger == ThoughtTrigger::ActionFinishes;
			if (actionTrigger && source.action != event.action) {
				continue;
			}
			if (!thingMatches(source, event.definition)) {
				continue;
			}
			if (source.first) {
				const uint64_t key = (static_cast<uint64_t>(index) << 32U) | event.definition;
				if (!stateOf(event.colonist, now).firstsFired.insert(key).second) {
					continue;
				}
			}
			think(event.colonist, source.thought, event.definition);
		}
	}

	void ThoughtSourceSystem::watchNeeds(EntityID colonist, ThoughtSourceState& state) {
		const NeedsComponent&											   needs = *world->getComponent<NeedsComponent>(colonist);
		std::array<std::size_t, static_cast<std::size_t>(NeedType::Count)> levels{};
		for (std::size_t need = 0; need < levels.size(); ++need) {
			levels[need] = needs.needs[need].level();
		}
		if (!state.needLevels.has_value()) {
			state.needLevels = levels;
			return;
		}
		const auto previous = *state.needLevels;
		state.needLevels = levels;
		// A change crossing several thresholds at once is one change: a fall is told by the lowest threshold
		// crossed, the level it landed in, and a rise by the highest.
		for (const ThoughtSource& source : thoughtTypes.sources()) {
			if (source.trigger != ThoughtTrigger::NeedFalls && source.trigger != ThoughtTrigger::NeedRises) {
				continue;
			}
			const auto		  need = static_cast<std::size_t>(source.need);
			const std::size_t before = previous[need];
			const std::size_t after = levels[need];
			const bool		  fell = source.trigger == ThoughtTrigger::NeedFalls && after > before && after == source.threshold + 1;
			const bool		  rose = source.trigger == ThoughtTrigger::NeedRises && after < before && after == source.threshold;
			if ((fell || rose) && world->isAlive(colonist)) {
				think(colonist, source.thought, 0);
			}
		}
	}

	void ThoughtSourceSystem::rollIdle(EntityID colonist, ThoughtSourceState& state, double now) {
		const std::vector<ThoughtSource>& sources = thoughtTypes.sources();
		for (std::size_t idle = 0; idle < idleSources.size(); ++idle) {
			const ThoughtSource& source = sources[idleSources[idle]];
			if (now < state.idleRollsAt[idle]) {
				continue;
			}
			// One roll however long the jump since the last: rolls make no backlog.
			state.idleRollsAt[idle] = now + source.rollEverySimulationSeconds;
			const Thoughts*				thoughts = world->getComponent<Thoughts>(colonist);
			const std::optional<double> latest = thoughts != nullptr ? latestThoughtAt(*thoughts) : std::nullopt;
			if (latest.has_value() && now - *latest < source.rollEverySimulationSeconds) {
				continue;
			}
			const uint64_t colonistStream = foundation::deriveSeed(seed, static_cast<uint64_t>(colonist));
			const float	   roll = foundation::Pcg32(foundation::deriveSeed(colonistStream, state.idleRolls++)).nextFloat();
			if (roll < source.chance && world->isAlive(colonist)) {
				think(colonist, source.thought, 0);
			}
		}
	}

	bool ThoughtSourceSystem::thingMatches(const ThoughtSource& source, uint32_t definition) const {
		switch (source.thing) {
			case ThingFilter::Any:
				return true;
			case ThingFilter::Ground:
				return definition == 0;
			case ThingFilter::Capability:
				return definition != 0 && assets::AssetRegistry::Get().hasCapability(definition, source.capability);
		}
		return false;
	}

	void ThoughtSourceSystem::think(EntityID colonist, ThoughtEventTypeId thought, uint32_t definition) {
		const ThoughtSubject subject = definition != 0 ? ThoughtSubject::aboutKind(definition) : ThoughtSubject::aboutNothing();
		world->getSystem<ThoughtSystem>().create({.colonist = colonist, .eventType = thought, .subject = subject});
	}

} // namespace ecs
