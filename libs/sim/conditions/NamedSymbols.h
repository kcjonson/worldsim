#pragma once

#include "ConditionVocabulary.h"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace conditions {

	/// The names a provider accepts for one Symbol parameter, each paired with what it stands for, interned
	/// once at registration so evaluation compares ids.
	template <typename Value>
	class NamedSymbols {
	  public:
		NamedSymbols(ConditionVocabulary& vocabulary, std::initializer_list<std::pair<const char*, Value>> entries) {
			names.reserve(entries.size());
			byId.reserve(entries.size());
			for (const auto& [name, value] : entries) {
				names.emplace_back(name);
				byId.emplace_back(vocabulary.symbolId(name), value);
			}
		}

		[[nodiscard]] const std::vector<std::string>& accepted() const { return names; }

		[[nodiscard]] const Value* find(uint64_t symbolId) const {
			for (const auto& [id, value] : byId) {
				if (id == symbolId) {
					return &value;
				}
			}
			return nullptr;
		}

	  private:
		std::vector<std::string>				names;
		std::vector<std::pair<uint32_t, Value>> byId;
	};

} // namespace conditions
