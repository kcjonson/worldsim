#include "KnownTerms.h"

namespace conditions {

	std::vector<TermSignature> knownTerms() {
		using enum ValueType;
		const std::vector<std::string> nearbyKinds = {
			"colonist", "food", "storage", "station", "structure", "bed", "lit_fire", "loose_item"
		};
		const std::vector<std::string> scarcityLevels = {"plenty", "tight", "short"};

		return {
			{.name = "near", .result = Boolean, .parameters = {Symbol, Number}, .parameterSymbols = {nearbyKinds}},
			{.name = "count_near", .result = Number, .parameters = {Symbol, Number}, .parameterSymbols = {nearbyKinds}},
			{.name = "in_room", .result = Boolean},
			{.name = "room_occupants", .result = Number},
			{.name = "unexplored_in_sight", .result = Boolean},
			{.name = "observed", .result = Boolean},
			{.name = "owner", .result = Target, .parameters = {Target}},
			{.name = "owner_present", .result = Boolean, .parameters = {Target}},
			{.name = "opinion", .result = Number, .parameters = {Target}},
			{.name = "likes", .result = Boolean, .parameters = {Target}},
			{.name = "dislikes", .result = Boolean, .parameters = {Target}},
			{.name = "recent_event", .result = Boolean, .parameters = {Target, Symbol, Number}},
			{.name = "mood", .result = Number},
			{.name = "mood_cause", .result = Symbol},
			{.name = "need", .result = Number, .parameters = {Symbol}},
			{.name = "attribute", .result = Number, .parameters = {Symbol}},
			{.name = "trait", .result = Boolean, .parameters = {Symbol}},
			{.name = "current_task", .result = Boolean, .parameters = {Symbol}},
			{.name = "mid_craft", .result = Boolean},
			{.name = "hours_worked", .result = Number, .parameters = {Number}},
			{.name = "holds_in_a_row", .result = Number},
			{.name = "affinity", .result = Number, .parameters = {Symbol}},
			{.name = "time_below", .result = Boolean, .parameters = {Number, Number}},
			{.name = "scarcity", .result = Symbol, .parameters = {Symbol}, .resultSymbols = scarcityLevels},
			{.name = "colony_mood", .result = Number},
			{.name = "hour", .result = Number},
			{.name = "is_night", .result = Boolean},
		};
	}

} // namespace conditions
