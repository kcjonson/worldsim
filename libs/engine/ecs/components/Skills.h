#pragma once

// Skills Component for Colonist Skill System
// Tracks skill levels (0.0 to 20.0) for each colonist.
// Used for:
// - Task priority bonuses (skilled workers prefer their specialty)
// - Work type access (some work requires minimum skill level)
// - Efficiency scaling (future: faster work, better quality)
//
// See /docs/design/game-systems/colonists/skills.md for design details.

#include "assets/WorkTypeDef.h"

#include <string>
#include <unordered_map>

namespace ecs {

/// Skills component - tracks a colonist's proficiency in various skills.
/// Skill levels range from 0.0 (untrained) to 20.0 (master).
struct Skills {
	/// Skill levels by skill defName (e.g., "Farming" → 5.0)
	std::unordered_map<std::string, float> levels;

	/// Get skill level for a skill (0.0 if not in map = untrained)
	/// @param skillDefName The skill identifier (e.g., "Farming", "Medicine")
	/// @return Skill level (0.0 to 20.0)
	[[nodiscard]] float getLevel(const std::string& skillDefName) const {
		auto it = levels.find(skillDefName);
		return it != levels.end() ? it->second : 0.0F;
	}

	/// Set skill level for a skill
	/// @param skillDefName The skill identifier
	/// @param level The skill level (clamped to 0.0-20.0)
	void setLevel(const std::string& skillDefName, float level) {
		// Clamp to valid range
		level = std::max(0.0F, std::min(20.0F, level));
		levels[skillDefName] = level;
	}

	/// Check if colonist meets a minimum skill requirement
	/// @param skillDefName The skill to check
	/// @param minLevel The minimum required level
	/// @return true if colonist has at least minLevel in this skill
	[[nodiscard]] bool meetsRequirement(const std::string& skillDefName, float minLevel) const {
		return getLevel(skillDefName) >= minLevel;
	}

	/// Check if colonist can perform a specific work type
	/// @param workType The work type definition
	/// @return true if colonist meets skill requirements (or work has no requirements)
	[[nodiscard]] bool canPerformWorkType(const engine::assets::WorkTypeDef& workType) const {
		if (!workType.skillRequired.has_value()) {
			return true; // No skill required - anyone can do it
		}
		return meetsRequirement(*workType.skillRequired, workType.minSkillLevel);
	}

	/// Get total skill points across all skills (for display/comparison)
	[[nodiscard]] float totalSkillPoints() const {
		float total = 0.0F;
		for (const auto& [_, level] : levels) {
			total += level;
		}
		return total;
	}

	/// Get count of skills at or above a threshold
	/// @param minLevel Minimum level to count
	/// @return Number of skills at or above minLevel
	[[nodiscard]] size_t countSkillsAbove(float minLevel) const {
		size_t count = 0;
		for (const auto& [skill, level] : levels) {
			if (level >= minLevel) {
				++count;
			}
		}
		return count;
	}

	/// Clear all skills (reset to untrained)
	void clear() { levels.clear(); }
};

/// Skill level descriptions for UI display
namespace SkillLevels {
	constexpr float kUntrained = 0.0F;
	constexpr float kNoviceMin = 1.0F;
	constexpr float kNoviceMax = 4.0F;
	constexpr float kCompetentMin = 5.0F;
	constexpr float kCompetentMax = 9.0F;
	constexpr float kSkilledMin = 10.0F;
	constexpr float kSkilledMax = 14.0F;
	constexpr float kExpertMin = 15.0F;
	constexpr float kExpertMax = 19.0F;
	constexpr float kMaster = 20.0F;

	/// Get human-readable skill level description
	inline const char* getDescription(float level) {
		if (level < kNoviceMin) {
			return "Untrained";
		}
		if (level < kCompetentMin) {
			return "Novice";
		}
		if (level < kSkilledMin) {
			return "Competent";
		}
		if (level < kExpertMin) {
			return "Skilled";
		}
		if (level < kMaster) {
			return "Expert";
		}
		return "Master";
	}
} // namespace SkillLevels

} // namespace ecs
