#pragma once

#include "TermValue.h"

#include <ecs/EntityID.h>

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace ecs {
	class World;
}

namespace conditions {

	/// Whom a condition is asked about, and the world his components live in.
	struct ConditionContext {
		const ecs::World& world;
		ecs::EntityID	  colonist = ecs::kInvalidEntity;
	};

	/// Answers one term for one colonist. `arguments` are already evaluated, in parameter order, and all
	/// known: a term with an unknown argument is unknown without being asked. Called during evaluation, so it
	/// must not allocate.
	using TermProvider = std::function<TermValue(std::span<const TermValue> arguments, const ConditionContext& context)>;

	/// A term's name and types. A Symbol parameter takes a bare name; a Number parameter takes a number or a
	/// Number term; a Target parameter takes a bare name or a Target term.
	struct TermSignature {
		std::string			   name;
		ValueType			   result = ValueType::Boolean;
		std::vector<ValueType> parameters;
		/// Per parameter, the names a Symbol parameter accepts. Missing or empty accepts any name.
		std::vector<std::vector<std::string>> parameterSymbols;
		/// The names a Symbol result can be compared against with `is`. Empty accepts any name.
		std::vector<std::string> resultSymbols;
	};

} // namespace conditions
