#include "Facts.h"

#include <algorithm>

namespace queries {

	namespace {

		[[nodiscard]] constexpr bool namesMatchTheLoaders() {
			for (std::size_t index = 0; index < kFactSpecs.size(); ++index) {
				if (kFactSpecs[index].name != assets::kFacts[index].name) {
					return false;
				}
			}
			return true;
		}
		static_assert(namesMatchTheLoaders(), "kFactSpecs lists assets::kFacts in the same order");

		/// A weigh or scale row on an unanswered fact gives every candidate the same 0; an exclude or prefer row
		/// would draw its line on a value the sim does not have, so the query is not walked.
		[[nodiscard]] std::string
		usedUnanswered(const assets::QueryEntry& query, uint16_t position, std::string_view element, uint16_t factIndex) {
			const std::string_view shortId = std::string_view(query.id).substr(query.id.rfind('/') + 1);
			const bool			   weighs = element == "weigh" || element == "scale";
			return query.file + ": <query id=\"" + std::string(shortId) + "\"> row " + std::to_string(position + 1) + " <" +
				   std::string(element) + "> fact \"" + std::string(kFactSpecs[factIndex].name) + "\" is answered by nothing" +
				   (weighs ? ", so it reads 0 for every candidate" : ", so the query answers that the fact is not answered");
		}

	} // namespace

	uint16_t writtenPositionOfFactRow(const assets::QueryEntry& query, std::size_t index) {
		const std::size_t total = query.from.size() + query.rows.size() + query.scales.size();
		std::size_t		  factRows = 0;
		for (uint16_t position = 0; position < total; ++position) {
			const bool fromRow = std::ranges::any_of(query.from, [&](const assets::FromRow& from) { return from.row == position; });
			const bool scaleRow = std::ranges::any_of(query.scales, [&](const assets::ScaleRow& scale) { return scale.row == position; });
			if (fromRow || scaleRow) {
				continue;
			}
			if (factRows++ == index) {
				return position;
			}
		}
		return static_cast<uint16_t>(total);
	}

	std::vector<std::string> unansweredFactUses(const assets::EntryLibrary& library) {
		std::vector<std::string> lines;
		for (const assets::QueryEntry& query : library.queries) {
			for (std::size_t index = 0; index < query.rows.size(); ++index) {
				const assets::FactRow& row = query.rows[index];
				if (!kFactSpecs[row.fact.fact].answered) {
					const char* element = row.kind == assets::FactRowKind::Weigh	 ? "weigh"
										  : row.kind == assets::FactRowKind::Exclude ? "exclude"
																					 : "prefer";
					lines.push_back(usedUnanswered(query, writtenPositionOfFactRow(query, index), element, row.fact.fact));
				}
			}
			for (const assets::ScaleRow& scale : query.scales) {
				if (!kFactSpecs[scale.fact.fact].answered) {
					lines.push_back(usedUnanswered(query, scale.row, "scale", scale.fact.fact));
				}
			}
		}
		return lines;
	}

} // namespace queries
