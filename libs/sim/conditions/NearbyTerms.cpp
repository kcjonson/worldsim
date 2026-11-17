#include "NearbyTerms.h"

#include "ConditionVocabulary.h"
#include "SymbolBindings.h"

#include <ecs/World.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Transform.h>

#include <glm/geometric.hpp>

#include <memory>
#include <optional>

namespace conditions {

	namespace {

		/// How many things with `capability` the colonist remembers within `radius` of where he stands,
		/// counting no further than `limit`. Memory has no spatial index, so this walks every remembered
		/// thing with it. Nothing when the colonist cannot be answered for.
		std::optional<uint32_t>
		countRemembered(assets::CapabilityType capability, float radius, uint32_t limit, const ConditionContext& context) {
			const ecs::Memory*	 memory = context.world.getComponent<ecs::Memory>(context.colonist);
			const ecs::Position* position = context.world.getComponent<ecs::Position>(context.colonist);
			if (memory == nullptr || position == nullptr) {
				return std::nullopt;
			}
			const float radiusSquared = radius * radius;
			const auto	within = [&](glm::vec2 point) {
				 const glm::vec2 offset = point - position->value;
				 return glm::dot(offset, offset) <= radiusSquared;
			};

			uint32_t count = 0;
			if (count >= limit) {
				return count;
			}
			for (const uint64_t key : memory->getEntitiesWithCapability(capability)) {
				const ecs::KnownWorldEntity* entity = memory->getWorldEntity(key);
				if (entity != nullptr && within(entity->position) && ++count >= limit) {
					break;
				}
			}
			return count;
		}

	} // namespace

	void provideNearbyTerms(ConditionVocabulary& vocabulary, const assets::ConditionConfig& config, std::vector<std::string>& errors) {
		auto kinds = std::make_shared<SymbolBindings<assets::CapabilityType>>();
		for (const assets::ConditionKindDef& kind : config.kinds) {
			const std::optional<SymbolId> symbol = vocabulary.findSymbol(kind.name);
			if (symbol.has_value() && kind.capability.has_value()) {
				kinds->bind(*symbol, *kind.capability);
			}
		}
		vocabulary.reportUnbound("near", 0, *kinds);
		vocabulary.reportUnbound("count_near", 0, *kinds);

		const auto answer = [kinds](const TermCall& call, uint32_t limit) -> std::optional<uint32_t> {
			const assets::CapabilityType* capability = kinds->find(call.arguments[0].symbol());
			if (capability == nullptr) {
				return std::nullopt;
			}
			return countRemembered(*capability, static_cast<float>(call.arguments[1].number()), limit, call.context);
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
