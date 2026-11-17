#include "NearbyTerms.h"

#include "ConditionVocabulary.h"
#include "NamedSymbols.h"

#include <ecs/World.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Transform.h>

#include <assets/AssetDefinition.h>

#include <glm/geometric.hpp>

#include <cassert>
#include <limits>
#include <memory>
#include <optional>

namespace conditions {

	namespace {

		enum class NearbySource : uint8_t {
			Capability, // remembered world entities with `capability`
			Colonist,	// remembered colonists
			Unremembered
		};

		struct NearbyKind {
			NearbySource		   source = NearbySource::Unremembered;
			assets::CapabilityType capability = assets::CapabilityType::Edible;
		};

		/// How many of `kind` the colonist remembers within `radius` of where he stands, counting no further
		/// than `limit`. Nothing when the kind or the colonist cannot be answered for.
		std::optional<uint32_t> countRemembered(const NearbyKind& kind, float radius, uint32_t limit, const ConditionContext& context) {
			const ecs::Memory*	 memory = context.world.getComponent<ecs::Memory>(context.colonist);
			const ecs::Position* position = context.world.getComponent<ecs::Position>(context.colonist);
			if (kind.source == NearbySource::Unremembered || memory == nullptr || position == nullptr) {
				return std::nullopt;
			}
			const float radiusSquared = radius * radius;
			const auto	within = [&](glm::vec2 point) {
				 const glm::vec2 offset = point - position->value;
				 return glm::dot(offset, offset) <= radiusSquared;
			};

			uint32_t count = 0;
			if (kind.source == NearbySource::Colonist) {
				for (const auto& [entity, known] : memory->knownDynamicEntities) {
					if (entity != context.colonist && within(known.lastKnownPosition) &&
						context.world.hasComponent<ecs::Colonist>(entity) && ++count >= limit) {
						break;
					}
				}
				return count;
			}
			for (const uint64_t key : memory->getEntitiesWithCapability(kind.capability)) {
				const ecs::KnownWorldEntity* entity = memory->getWorldEntity(key);
				if (entity != nullptr && within(entity->position) && ++count >= limit) {
					break;
				}
			}
			return count;
		}

	} // namespace

	void provideNearbyTerms(ConditionVocabulary& vocabulary) {
		using enum assets::CapabilityType;
		auto kinds = std::make_shared<const NamedSymbols<NearbyKind>>(
			vocabulary,
			std::initializer_list<std::pair<const char*, NearbyKind>>{
				{"colonist", {.source = NearbySource::Colonist}},
				{"food", {.source = NearbySource::Capability, .capability = Edible}},
				{"storage", {.source = NearbySource::Capability, .capability = Storage}},
				{"station", {.source = NearbySource::Capability, .capability = Craftable}},
				{"bed", {.source = NearbySource::Capability, .capability = Sleepable}},
				{"loose_item", {.source = NearbySource::Capability, .capability = Carryable}},
				{"structure", {.source = NearbySource::Unremembered}},
				{"lit_fire", {.source = NearbySource::Unremembered}},
			}
		);

		[[maybe_unused]] const auto nearRefused = vocabulary.provide(
			{.name = "near",
			 .result = ValueType::Boolean,
			 .parameters = {ValueType::Symbol, ValueType::Number},
			 .parameterSymbols = {kinds->accepted()}},
			[kinds](std::span<const TermValue> arguments, const ConditionContext& context) {
				const NearbyKind* kind = kinds->find(arguments[0].identity);
				if (kind == nullptr) {
					return TermValue::unknown();
				}
				const std::optional<uint32_t> count = countRemembered(*kind, static_cast<float>(arguments[1].number), 1, context);
				return count.has_value() ? TermValue::fromBoolean(*count > 0) : TermValue::unknown();
			}
		);
		assert(!nearRefused.has_value());

		[[maybe_unused]] const auto countRefused = vocabulary.provide(
			{.name = "count_near",
			 .result = ValueType::Number,
			 .parameters = {ValueType::Symbol, ValueType::Number},
			 .parameterSymbols = {kinds->accepted()}},
			[kinds](std::span<const TermValue> arguments, const ConditionContext& context) {
				const NearbyKind* kind = kinds->find(arguments[0].identity);
				if (kind == nullptr) {
					return TermValue::unknown();
				}
				const std::optional<uint32_t> count =
					countRemembered(*kind, static_cast<float>(arguments[1].number), std::numeric_limits<uint32_t>::max(), context);
				return count.has_value() ? TermValue::fromNumber(*count) : TermValue::unknown();
			}
		);
		assert(!countRefused.has_value());
	}

} // namespace conditions
