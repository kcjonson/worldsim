#pragma once

#include <ecs/EntityID.h>

#include <assets/ConditionConfig.h>

#include <cassert>
#include <cstdint>

namespace conditions {

	using ValueType = assets::ConditionValueType;

	/// A name the vocabulary interned from a declared symbol set. Its own type, so it is never taken for an
	/// entity.
	enum class SymbolId : uint32_t {};

	/// A condition's answer. Unknown is what a term nothing answers gives, and spreads by Kleene logic:
	/// `false and unknown` is false, `true or unknown` is true, `not unknown` is unknown.
	enum class Truth : uint8_t { False, True, Unknown };

	/// One value a term hands back, typed. A value that is not known is Unknown, whatever its type.
	class TermValue {
	  public:
		TermValue() = default;

		[[nodiscard]] static TermValue unknown() { return {}; }
		[[nodiscard]] static TermValue fromTruth(bool value) { return {ValueType::Truth, value ? 1.0 : 0.0, 0}; }
		[[nodiscard]] static TermValue fromNumber(double value) { return {ValueType::Number, value, 0}; }
		[[nodiscard]] static TermValue fromSymbol(SymbolId symbol) { return {ValueType::Symbol, 0.0, static_cast<uint64_t>(symbol)}; }
		/// An invalid entity is no one, so Unknown.
		[[nodiscard]] static TermValue fromTarget(ecs::EntityID entity) {
			return entity == ecs::kInvalidEntity ? unknown() : TermValue{ValueType::Target, 0.0, entity};
		}

		[[nodiscard]] bool		known() const { return isKnown; }
		[[nodiscard]] ValueType type() const { return valueType; }

		[[nodiscard]] bool truth() const {
			assert(isKnown && valueType == ValueType::Truth);
			return numberValue != 0.0;
		}
		[[nodiscard]] double number() const {
			assert(isKnown && valueType == ValueType::Number);
			return numberValue;
		}
		[[nodiscard]] SymbolId symbol() const {
			assert(isKnown && valueType == ValueType::Symbol);
			return static_cast<SymbolId>(identity);
		}
		[[nodiscard]] ecs::EntityID target() const {
			assert(isKnown && valueType == ValueType::Target);
			return identity;
		}

	  private:
		TermValue(ValueType type, double number, uint64_t newIdentity)
			: numberValue(number),
			  identity(newIdentity),
			  valueType(type),
			  isKnown(true) {}

		double	  numberValue = 0.0;
		uint64_t  identity = 0;
		ValueType valueType = ValueType::Truth;
		bool	  isKnown = false;
	};

} // namespace conditions
