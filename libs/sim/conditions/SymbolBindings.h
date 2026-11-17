#pragma once

#include "TermValue.h"

#include <utility>
#include <vector>

namespace conditions {

	/// What each declared name of a Symbol parameter stands for in the sim, bound once when a provider is
	/// registered so evaluation compares ids. A declared name with nothing bound is one the sim does not
	/// answer.
	template <typename Value>
	class SymbolBindings {
	  public:
		void bind(SymbolId symbol, Value value) { entries.emplace_back(symbol, std::move(value)); }

		[[nodiscard]] const Value* find(SymbolId symbol) const {
			for (const auto& [id, value] : entries) {
				if (id == symbol) {
					return &value;
				}
			}
			return nullptr;
		}

	  private:
		std::vector<std::pair<SymbolId, Value>> entries;
	};

} // namespace conditions
