#include "ColonistListModel.h"

#include <cmath>

namespace world_sim {

	namespace {

		// Mood values within this threshold are considered equal
		// This prevents UI flicker from tiny mood fluctuations
		constexpr float kMoodChangeThreshold = 0.5F;

		bool moodChanged(float oldMood, float newMood) {
			return std::abs(oldMood - newMood) > kMoodChangeThreshold;
		}

	} // namespace

	bool ColonistListModel::refresh(ecs::World& world) {
		auto newData = adapters::getColonists(world);

		// First refresh always triggers rebuild
		if (isFirstRefresh) {
			isFirstRefresh = false;
			colonistsData = std::move(newData);
			return true;
		}

		// Check for changes
		if (hasChanged(newData)) {
			colonistsData = std::move(newData);
			return true;
		}

		return false;
	}

	bool ColonistListModel::hasChanged(const std::vector<ColonistData>& newData) const {
		// Structural change: different number of colonists
		if (newData.size() != colonistsData.size()) {
			return true;
		}

		// Value changes: check each colonist
		for (size_t i = 0; i < newData.size(); ++i) {
			const auto& oldCol = colonistsData[i];
			const auto& newCol = newData[i];

			// Different entity (reordering or replacement)
			if (oldCol.id != newCol.id) {
				return true;
			}

			// Name changed (unlikely but possible)
			if (oldCol.name != newCol.name) {
				return true;
			}

			// Mood changed significantly
			if (moodChanged(oldCol.mood, newCol.mood)) {
				return true;
			}
		}

		return false;
	}

} // namespace world_sim
