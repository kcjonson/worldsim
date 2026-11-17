#pragma once

#include "TermValue.h"

#include <ecs/EntityID.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ecs {
	class World;
}

namespace conditions {

	/// Whom a condition is asked about, the world his components live in, and the entity `target` names
	/// (a bed a behavior acts on); none leaves `target` Unknown.
	struct ConditionContext {
		const ecs::World& world;
		ecs::EntityID	  colonist = ecs::kInvalidEntity;
		ecs::EntityID	  target = ecs::kInvalidEntity;
	};

	/// One call of a term: its arguments, evaluated, in parameter order and all known (a term with an
	/// unknown argument is Unknown without being asked), and `countLimit`, past which a counting term's
	/// answer changes nothing, so it may stop counting there (`count_near(colonist, 5) >= 2` needs 3).
	struct TermCall {
		std::span<const TermValue> arguments;
		const ConditionContext&	   context;
		uint32_t				   countLimit = std::numeric_limits<uint32_t>::max();
	};

	/// Answers one term. Called during evaluation, so it must not allocate.
	using TermProvider = std::function<TermValue(const TermCall& call)>;

	struct TermParameter {
		ValueType				type = ValueType::Number;
		std::optional<uint32_t> symbolSet; // a Symbol parameter's set, in the vocabulary
	};

	struct TermSignature {
		std::string				   name;
		ValueType				   result = ValueType::Truth;
		std::optional<uint32_t>	   resultSymbolSet; // a Symbol result's set, for `is`
		std::vector<TermParameter> parameters;
	};

} // namespace conditions
