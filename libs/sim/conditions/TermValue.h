#pragma once

#include <cstdint>

namespace conditions {

	/// What a term or an argument produces. A Symbol is a name interned by the vocabulary ("hunger",
	/// "short"); a Target is an entity a term names (owner(bed) is the bed's owner).
	enum class ValueType : uint8_t { Boolean, Number, Symbol, Target };

	/// A condition's answer. Unknown is the answer of a known term that no system provides yet, and spreads
	/// by Kleene logic: `false and unknown` is false, `true or unknown` is true, `not unknown` is unknown.
	enum class Truth : uint8_t { False, True, Unknown };

	/// One term's value as a provider hands it back. A value that is not `known` is Unknown, whatever its type.
	struct TermValue {
		double	 number = 0.0; // Number, and Boolean as 0 or 1
		uint64_t identity = 0; // Symbol id, or Target entity
		bool	 known = false;

		[[nodiscard]] static TermValue unknown() { return {}; }
		[[nodiscard]] static TermValue fromBoolean(bool value) { return {.number = value ? 1.0 : 0.0, .identity = 0, .known = true}; }
		[[nodiscard]] static TermValue fromNumber(double value) { return {.number = value, .identity = 0, .known = true}; }
		[[nodiscard]] static TermValue fromSymbol(uint32_t symbolId) { return {.number = 0.0, .identity = symbolId, .known = true}; }
		[[nodiscard]] static TermValue fromTarget(uint64_t entity) { return {.number = 0.0, .identity = entity, .known = true}; }
	};

} // namespace conditions
