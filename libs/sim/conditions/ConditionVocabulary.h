#pragma once

// The shared vocabulary of conditions: behaviors' requires and weights, thoughts' feelings and lines.
// See /docs/design/game-systems/colonists/mood.md (Conditions) and thoughts.md (Data).

#include "Condition.h"
#include "SymbolBindings.h"
#include "TermSignature.h"
#include "TermValue.h"

#include <assets/ConditionConfig.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
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
	/// A name its set does not hold is a parse error, except in an open set, which takes any name. A
	/// declared term nothing provides, a kind with no mapping, and an unlisted name in an open set parse,
	/// evaluate to Unknown, and are logged once per condition when it is parsed.
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

		/// Add the names a system knows to the owned set (assets::kOwnedSymbolSets) a declared term's Symbol
		/// parameter takes; names already there are kept. Returns why it was refused (not declared, not a
		/// Symbol parameter, a set the conditions files fill, a name a condition cannot use, the vocabulary
		/// frozen), or nothing.
		std::optional<std::string> addSymbols(std::string_view term, std::size_t parameter, std::span<const std::string> names);

		/// Record each name the term's Symbol parameter takes that `bindings` leaves without a value; a
		/// condition naming one is logged when parsed. Called by a provider once it has bound.
		template <typename Value>
		void reportUnbound(std::string_view term, std::size_t parameter, const SymbolBindings<Value>& bindings) {
			const std::optional<uint32_t> slot = findTerm(term);
			for (const DeclaredSymbol& symbol : parameterSymbols(term, parameter)) {
				if (bindings.find(symbol.id) == nullptr) {
					unbound.emplace(*slot, parameter, symbol.id);
				}
			}
		}

		[[nodiscard]] bool isUnbound(uint32_t term, std::size_t parameter, SymbolId symbol) const {
			return unbound.contains({term, parameter, symbol});
		}

		[[nodiscard]] std::optional<SymbolId>		  findSymbol(std::string_view name) const;
		[[nodiscard]] std::optional<uint32_t>		  findTerm(std::string_view name) const;
		[[nodiscard]] std::size_t					  termCount() const { return terms.size(); }
		[[nodiscard]] const TermSignature&			  signature(uint32_t term) const { return terms[term].signature; }
		[[nodiscard]] bool							  isProvided(uint32_t term) const { return static_cast<bool>(terms[term].provider); }
		[[nodiscard]] std::span<const DeclaredSymbol> symbolSet(uint32_t set) const { return symbolSets[set].symbols; }
		[[nodiscard]] bool							  isOpenSet(uint32_t set) const { return symbolSets[set].open; }

	  private:
		struct Term {
			TermSignature signature;
			TermProvider  provider;
		};

		struct SymbolSet {
			std::string					name;
			std::vector<DeclaredSymbol> symbols;
			bool						open = false;
		};

		void declareSymbolSet(const std::string& name, const assets::ConditionSymbolSetDef& declared, std::vector<std::string>& errors);
		void addSymbol(SymbolSet& set, const std::string& name);
		SymbolId intern(const std::string& name);

		[[nodiscard]] Truth		truthOf(const Condition& condition, uint16_t node, const ConditionContext& context) const;
		[[nodiscard]] TermValue valueOf(const Condition& condition, uint16_t node, const ConditionContext& context) const;

		std::vector<Term>						  terms;
		std::unordered_map<std::string, uint32_t> termsByName;
		std::vector<SymbolSet>					  symbolSets;
		std::unordered_map<std::string, uint32_t> symbolSetsByName;
		std::unordered_map<std::string, SymbolId> symbols;
		std::set<std::tuple<uint32_t, std::size_t, SymbolId>> unbound;
		bool									  frozen = false;
	};

} // namespace conditions
