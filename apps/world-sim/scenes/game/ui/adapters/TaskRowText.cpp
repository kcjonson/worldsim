#include "TaskRowText.h"

#include <format>
#include <utility>

namespace world_sim::adapters {

	namespace {

		constexpr float kFarDistance = 50.0F;

		/// The status, its flags, and the status's own part of the detail.
		TaskRowText rowStatus(const TaskRowFacts& facts) {
			const bool working = !facts.workers.empty();
			if (facts.buildSite) {
				if (!facts.underConstruction) {
					return {.status = "Needs materials", .detail = facts.workers, .blocked = true};
				}
				if (working) {
					return {.status = "Building", .detail = std::format("{} ({}%)", facts.workers, facts.buildPercent)};
				}
				return {.status = "Ready to build", .detail = std::format("{}%", facts.buildPercent)};
			}
			if (facts.blocked) {
				const std::string materials =
					facts.materials ? std::format("{}/{} materials", facts.materials->landed, facts.materials->required) : std::string{};
				return {.status = "Blocked", .detail = materials, .blocked = true};
			}
			if (facts.noSource) {
				return {.status = "Waiting (no source found)", .blocked = true};
			}
			if (working) {
				return {.status = "Working", .detail = facts.workers};
			}
			if (facts.allSourcesClaimed) {
				return {.status = "Waiting (all known sources claimed)", .blocked = true};
			}
			if (facts.distance > kFarDistance) {
				return {.status = "Available", .detail = "far"};
			}
			return {.status = "Unassigned", .unassigned = true};
		}

		void appendPart(std::string& detail, const std::string& part) {
			if (!part.empty()) {
				detail += detail.empty() ? part : "; " + part;
			}
		}

	} // namespace

	std::string joinNames(std::span<const std::string> names) {
		constexpr std::size_t kShown = 2;
		std::string			  joined;
		for (std::size_t index = 0; index < names.size() && index < kShown; ++index) {
			joined += index == 0 ? names[index] : ", " + names[index];
		}
		if (names.size() > kShown) {
			joined += std::format(" +{}", names.size() - kShown);
		}
		return joined;
	}

	std::string quantityText(uint32_t needed, uint32_t onTheWay) {
		if (needed > 0 && onTheWay > 0) {
			return std::format("needs {}, {} on the way", needed, onTheWay);
		}
		if (needed > 0) {
			return std::format("needs {}", needed);
		}
		if (onTheWay > 0) {
			return std::format("{} on the way", onTheWay);
		}
		return {};
	}

	TaskRowText taskRowText(const TaskRowFacts& facts) {
		TaskRowText row = rowStatus(facts);
		std::string detail;
		appendPart(detail, row.detail);
		appendPart(detail, quantityText(facts.needed, facts.onTheWay));
		if (facts.workers.empty() && !facts.knownTo.empty()) {
			appendPart(detail, "known to " + facts.knownTo);
		}
		row.detail = std::move(detail);
		return row;
	}

} // namespace world_sim::adapters
