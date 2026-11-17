#pragma once

// The shared vocabulary of conditions: behaviors' requires and weights, thoughts' feelings and lines.
// See /docs/design/game-systems/colonists/mood.md (Conditions) and thoughts.md (Data).

#include "Condition.h"
#include "TermSignature.h"
#include "TermValue.h"

#include <assets/ConditionConfig.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace conditions {

	/// The most arguments a term takes.
	inline constexpr std::size_t kMaxArguments = 4;

	struct DeclaredSymbol {
		std::string name;
		SymbolId	id{};
	};

	/// Every term a condition may name, and the systems that answer them. It is built in two steps and then
	/// frozen: the terms and symbol sets are declared from data (assets/config/conditions), then each system
	/// provides the terms it answers. Conditions are parsed only once it is frozen.
	///
	/// A name no file declares is a parse error. A declared term nothing provides parses, evaluates to
	/// Unknown, and is logged when a condition using it is parsed.
	///
	/// Unknown is returned as is. Consumers: a `requires` that is Unknown fails; a weight whose condition is
	/// Unknown is not applied; a `while` that is Unknown keeps its last state.
	class ConditionVocabulary {
	  public:
		/// Declare the terms and symbol sets of `config`. Returns what was refused: a name that is not an
		/// identifier or is a word of the grammar, a term declared twice, too many parameters, or a call
		/// after the freeze.
		std::vector<std::string> declare(const assets::ConditionConfig& config);

		/// Answer a declared term. Its types must match the declaration. Returns why it was refused (not
		/// declared, a type mismatch, already provided, the vocabulary frozen), or nothing when it was taken.
		std::optional<std::string>
		provide(std::string_view name, ValueType result, std::vector<ValueType> parameters, TermProvider provider);

		void			   freeze() { frozen = true; }
		[[nodiscard]] bool isFrozen() const { return frozen; }

		/// Parse an expression once, at load. Every error is reported here with its position, never later.
		/// Refused before the freeze.
		[[nodiscard]] ParseResult parse(std::string_view text) const;

		/// Run a condition this vocabulary parsed. Does not allocate.
		[[nodiscard]] Truth evaluate(const Condition& condition, const ConditionContext& context) const;

		/// The names a declared term's Symbol parameter takes, with their ids; empty for anything else.
		/// Providers bind to these when they provide.
		[[nodiscard]] std::span<const DeclaredSymbol> parameterSymbols(std::string_view term, std::size_t parameter) const;

		[[nodiscard]] std::optional<SymbolId>		  findSymbol(std::string_view name) const;
		[[nodiscard]] std::optional<uint32_t>		  findTerm(std::string_view name) const;
		[[nodiscard]] const TermSignature&			  signature(uint32_t term) const { return terms[term].signature; }
		[[nodiscard]] bool							  isProvided(uint32_t term) const { return static_cast<bool>(terms[term].provider); }
		[[nodiscard]] std::span<const DeclaredSymbol> symbolSet(uint32_t set) const { return symbolSets[set].symbols; }

	  private:
		struct Term {
			TermSignature signature;
			TermProvider  provider;
		};

		struct SymbolSet {
			std::string					name;
			std::vector<DeclaredSymbol> symbols;
		};

		void	 declareSymbolSet(const std::string& name, const std::vector<std::string>& names, std::vector<std::string>& errors);
		SymbolId intern(const std::string& name);

		[[nodiscard]] Truth		truthOf(const Condition& condition, uint16_t node, const ConditionContext& context) const;
		[[nodiscard]] TermValue valueOf(const Condition& condition, uint16_t node, const ConditionContext& context) const;

		std::vector<Term>						  terms;
		std::unordered_map<std::string, uint32_t> termsByName;
		std::vector<SymbolSet>					  symbolSets;
		std::unordered_map<std::string, uint32_t> symbolSetsByName;
		std::unordered_map<std::string, SymbolId> symbols;
		bool									  frozen = false;
	};

} // namespace conditions
