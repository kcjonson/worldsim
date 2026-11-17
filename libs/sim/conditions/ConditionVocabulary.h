#pragma once

// The shared vocabulary of conditions: behaviors' requires and weights, thoughts' feelings and lines.
// See /docs/design/game-systems/colonists/mood.md (Conditions) and thoughts.md (Data).

#include "Condition.h"
#include "TermSignature.h"
#include "TermValue.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace conditions {

	/// The most arguments a term takes.
	inline constexpr std::size_t kMaxArguments = 4;

	/// Every term a condition may name, and the systems that answer them. It starts with the terms mood.md
	/// lists, none of them provided; each system provides its own, and a mod adds new ones the same way.
	/// Providers are registered before the conditions that use them are parsed, since a parse checks symbol
	/// names against what each term accepts.
	///
	/// A known term nothing provides parses, and evaluates to Unknown. A name that is neither known nor
	/// provided is a parse error. A condition whose answer is Unknown does not hold: a behavior that requires
	/// it is not in the roll, a weight or feeling with it does not apply.
	class ConditionVocabulary {
	  public:
		ConditionVocabulary();

		/// Provide a term. For a known term the result and parameter types must match the known ones; the
		/// symbol names it gives narrow what parses. A new name adds a term. Returns why it was refused (a
		/// type mismatch, a term already provided, too many parameters), or nothing when it was taken.
		std::optional<std::string> provide(TermSignature signature, TermProvider provider);

		/// Parse an expression once, at load. Every error is reported here with its position, never later.
		[[nodiscard]] ParseResult parse(std::string_view text);

		/// Run a condition this vocabulary parsed for one colonist. Does not allocate.
		[[nodiscard]] Truth evaluate(const Condition& condition, const ConditionContext& context) const;

		/// The id of a symbol name, interned on first use. Providers take the ids of the names they answer
		/// with or compare against when they are registered.
		[[nodiscard]] uint32_t symbolId(std::string_view name);

		[[nodiscard]] std::optional<uint32_t> findTerm(std::string_view name) const;
		[[nodiscard]] const TermSignature&	  signature(uint32_t term) const { return terms[term].signature; }
		[[nodiscard]] bool					  isProvided(uint32_t term) const { return static_cast<bool>(terms[term].provider); }

	  private:
		struct Term {
			TermSignature signature;
			TermProvider  provider;
		};

		[[nodiscard]] Truth		truthOf(const Condition& condition, uint16_t node, const ConditionContext& context) const;
		[[nodiscard]] TermValue valueOf(const Condition& condition, uint16_t node, const ConditionContext& context) const;

		std::vector<Term>						  terms;
		std::unordered_map<std::string, uint32_t> termsByName;
		std::unordered_map<std::string, uint32_t> symbols;
	};

} // namespace conditions
