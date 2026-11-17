#include "NearbyTerms.h"

#include "ConditionVocabulary.h"
#include "SymbolBindings.h"

#include <ecs/World.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Transform.h>
#include <ecs/systems/TimeSystem.h>

#include <assets/AssetRegistry.h>
#include <assets/MemoryConfig.h>

#include <glm/geometric.hpp>

#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace conditions {

	namespace {

		/// A kind that is a mover: the asset definition its sightings are remembered under.
		struct MoverKind {
			std::string defName;
		};

		using KindSource = std::variant<assets::CapabilityType, MoverKind>;

		/// The colonist's memory and where he stands, the two things every nearby count reads.
		struct Rememberer {
			const ecs::Memory& memory;
			glm::vec2		   position;
			float			   radiusSquared;

			[[nodiscard]] bool within(glm::vec2 point) const {
				const glm::vec2 offset = point - position;
				return glm::dot(offset, offset) <= radiusSquared;
			}
		};

		std::optional<Rememberer> remembererOf(float radius, const ConditionContext& context) {
			const ecs::Memory*	 memory = context.world.getComponent<ecs::Memory>(context.colonist);
			const ecs::Position* position = context.world.getComponent<ecs::Position>(context.colonist);
			if (memory == nullptr || position == nullptr) {
				return std::nullopt;
			}
			return Rememberer{.memory = *memory, .position = position->value, .radiusSquared = radius * radius};
		}

		/// How many things with `capability` the colonist remembers within the radius, counting no further
		/// than `limit`. Memory has no spatial index, so this walks every remembered thing with it.
		uint32_t countRemembered(assets::CapabilityType capability, const Rememberer& rememberer, uint32_t limit) {
			uint32_t count = 0;
			for (const uint64_t key : rememberer.memory.getEntitiesWithCapability(capability)) {
				if (count >= limit) {
					break;
				}
				const ecs::KnownWorldEntity* entity = rememberer.memory.getWorldEntity(key);
				if (entity != nullptr && rememberer.within(entity->position)) {
					++count;
				}
			}
			return count;
		}

		/// How many movers of `mover` the colonist saw within the recent-sighting window and last saw within
		/// the radius, counting no further than `limit`. Nothing without a game clock, a loaded memory
		/// config, or an asset of that definition.
		std::optional<uint32_t> countRemembered(const MoverKind& mover, const Rememberer& rememberer, uint32_t limit, const ConditionContext& context) {
			const ecs::TimeSystem*		time = context.world.tryGetSystem<ecs::TimeSystem>();
			const assets::MemoryConfig& config = assets::MemoryConfig::Get();
			const uint32_t				defNameId = assets::AssetRegistry::Get().getDefNameId(mover.defName);
			if (time == nullptr || !config.loaded() || defNameId == 0) {
				return std::nullopt;
			}
			uint32_t count = 0;
			if (count >= limit) {
				return count;
			}
			rememberer.memory.forEachDynamicEntitySeenWithin(
				defNameId,
				time->simulationSeconds(),
				config.recentSightingGameMinutes(),
				[&](const ecs::KnownDynamicEntity& known) {
					if (rememberer.within(known.lastKnownPosition)) {
						++count;
					}
					return count < limit;
				}
			);
			return count;
		}

	} // namespace

	void provideNearbyTerms(ConditionVocabulary& vocabulary, const assets::ConditionConfig& config, std::vector<std::string>& errors) {
		auto kinds = std::make_shared<SymbolBindings<KindSource>>();
		for (const assets::ConditionKindDef& kind : config.kinds) {
			const std::optional<SymbolId> symbol = vocabulary.findSymbol(kind.name);
			if (!symbol.has_value()) {
				continue;
			}
			if (kind.capability.has_value()) {
				kinds->bind(*symbol, *kind.capability);
			} else if (!kind.mover.empty()) {
				kinds->bind(*symbol, MoverKind{.defName = kind.mover});
			}
		}
		vocabulary.reportUnbound("near", 0, *kinds);
		vocabulary.reportUnbound("count_near", 0, *kinds);

		const auto answer = [kinds](const TermCall& call, uint32_t limit) -> std::optional<uint32_t> {
			const KindSource*				 source = kinds->find(call.arguments[0].symbol());
			const std::optional<Rememberer> rememberer =
				source == nullptr ? std::nullopt : remembererOf(static_cast<float>(call.arguments[1].number()), call.context);
			if (!rememberer.has_value()) {
				return std::nullopt;
			}
			if (const auto* capability = std::get_if<assets::CapabilityType>(source)) {
				return countRemembered(*capability, *rememberer, limit);
			}
			return countRemembered(std::get<MoverKind>(*source), *rememberer, limit, call.context);
		};

		const auto nearRefused =
			vocabulary.provide("near", ValueType::Truth, {ValueType::Symbol, ValueType::Number}, [answer](const TermCall& call) {
				const std::optional<uint32_t> count = answer(call, 1);
				return count.has_value() ? TermValue::fromTruth(*count > 0) : TermValue::unknown();
			});
		if (nearRefused.has_value()) {
			errors.push_back(*nearRefused);
		}

		const auto countRefused =
			vocabulary.provide("count_near", ValueType::Number, {ValueType::Symbol, ValueType::Number}, [answer](const TermCall& call) {
				const std::optional<uint32_t> count = answer(call, call.countLimit);
				return count.has_value() ? TermValue::fromNumber(*count) : TermValue::unknown();
			});
		if (countRefused.has_value()) {
			errors.push_back(*countRefused);
		}
	}

} // namespace conditions
