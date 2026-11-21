#pragma once

// The facts a query weighs: each one's scope, its range, and whether the sim answers it yet. Indexed like
// assets::kFacts, the names the loaders accept. See /docs/technical/queries-and-chains.md (Facts).

#include <assets/Entries.h>

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace queries {

	/// Definition when a fact depends only on the candidate's asset definition and the creature, item when it
	/// depends on the one thing.
	enum class FactScope : uint8_t { Definition, Item };

	struct FactSpec {
		std::string_view name;
		FactScope		 scope;
		float			 low;
		float			 high;
		/// False for a fact whose input no system holds yet: it reads 0, and the load logs once each query
		/// that uses it.
		bool answered;
	};

	inline constexpr float kUnbounded = std::numeric_limits<float>::infinity();

	inline constexpr std::array<FactSpec, assets::kFacts.size()> kFactSpecs = {{
		{.name = "distance", .scope = FactScope::Item, .low = 0.0F, .high = kUnbounded, .answered = true},
		{.name = "held", .scope = FactScope::Item, .low = 0.0F, .high = 1.0F, .answered = true},
		{.name = "on_shore", .scope = FactScope::Item, .low = 0.0F, .high = 1.0F, .answered = true},
		{.name = "waste_nearby", .scope = FactScope::Item, .low = 0.0F, .high = kUnbounded, .answered = true},
		{.name = "food_nearby", .scope = FactScope::Item, .low = 0.0F, .high = kUnbounded, .answered = true},
		{.name = "is_place", .scope = FactScope::Item, .low = 0.0F, .high = 1.0F, .answered = true},
		{.name = "sleep_quality", .scope = FactScope::Item, .low = 0.0F, .high = 1.0F, .answered = true},
		{.name = "below_destination", .scope = FactScope::Item, .low = 0.0F, .high = 1.0F, .answered = true},
		{.name = "quality", .scope = FactScope::Definition, .low = 0.0F, .high = 1.0F, .answered = true},
		{.name = "liking", .scope = FactScope::Definition, .low = -1.0F, .high = 1.0F, .answered = false},
		{.name = "against_diet", .scope = FactScope::Definition, .low = 0.0F, .high = 1.0F, .answered = false},
		{.name = "contains", .scope = FactScope::Definition, .low = 0.0F, .high = 1.0F, .answered = false},
		{.name = "eaten_recently", .scope = FactScope::Definition, .low = 0.0F, .high = 1.0F, .answered = false},
		{.name = "freshness", .scope = FactScope::Item, .low = 0.0F, .high = 1.0F, .answered = false},
		{.name = "maker_opinion", .scope = FactScope::Item, .low = -1.0F, .high = 1.0F, .answered = false},
		{.name = "fits_meal_time", .scope = FactScope::Definition, .low = 0.0F, .high = 1.0F, .answered = false},
		{.name = "comfort", .scope = FactScope::Definition, .low = 0.0F, .high = 1.0F, .answered = false},
		{.name = "opinion", .scope = FactScope::Item, .low = -1.0F, .high = 1.0F, .answered = false},
		{.name = "mood_source", .scope = FactScope::Item, .low = -1.0F, .high = 1.0F, .answered = false},
		{.name = "owner_opinion", .scope = FactScope::Item, .low = -1.0F, .high = 1.0F, .answered = false},
		{.name = "has_owner", .scope = FactScope::Item, .low = 0.0F, .high = 1.0F, .answered = false},
		{.name = "owned_by_self", .scope = FactScope::Item, .low = 0.0F, .high = 1.0F, .answered = false},
		{.name = "owner_present", .scope = FactScope::Item, .low = 0.0F, .high = 1.0F, .answered = false},
		{.name = "built_by_self", .scope = FactScope::Item, .low = 0.0F, .high = 1.0F, .answered = false},
		{.name = "flammable", .scope = FactScope::Definition, .low = 0.0F, .high = 1.0F, .answered = false},
		{.name = "colony_stock", .scope = FactScope::Definition, .low = 0.0F, .high = kUnbounded, .answered = true},
	}};

	/// The index of the fact named `name` in kFactSpecs (and assets::kFacts); kFactSpecs.size() for none.
	[[nodiscard]] constexpr uint16_t factIndex(std::string_view name) {
		for (uint16_t index = 0; index < kFactSpecs.size(); ++index) {
			if (kFactSpecs[index].name == name) {
				return index;
			}
		}
		return static_cast<uint16_t>(kFactSpecs.size());
	}

	namespace fact {
		inline constexpr uint16_t kDistance = factIndex("distance");
		inline constexpr uint16_t kHeld = factIndex("held");
		inline constexpr uint16_t kOnShore = factIndex("on_shore");
		inline constexpr uint16_t kWasteNearby = factIndex("waste_nearby");
		inline constexpr uint16_t kFoodNearby = factIndex("food_nearby");
		inline constexpr uint16_t kIsPlace = factIndex("is_place");
		inline constexpr uint16_t kSleepQuality = factIndex("sleep_quality");
		inline constexpr uint16_t kBelowDestination = factIndex("below_destination");
		inline constexpr uint16_t kQuality = factIndex("quality");
		inline constexpr uint16_t kColonyStock = factIndex("colony_stock");
	} // namespace fact

	/// The position among `query`'s rows, as written, of its fact row `index` (the loaders keep the position of
	/// `<from>` and `<scale>` rows; a fact row's is the one left between them, in order).
	[[nodiscard]] uint16_t writtenPositionOfFactRow(const assets::QueryEntry& query, std::size_t index);

	/// One line per query row in `library` that reads a fact nothing answers yet, naming the file, the query,
	/// the row, and the fact, for the load to log once.
	[[nodiscard]] std::vector<std::string> unansweredFactUses(const assets::EntryLibrary& library);

} // namespace queries
