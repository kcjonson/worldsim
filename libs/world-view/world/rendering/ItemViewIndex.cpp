#include "ItemViewIndex.h"

#include <glm/common.hpp>

#include <algorithm>
#include <limits>

namespace worldview {

	bool ItemViewIndex::follow(uint64_t generation) {
		if (followed && generation == seenGeneration) {
			return false;
		}
		const bool dropped = followed;
		followed = true;
		seenGeneration = generation;
		slotOfKey.clear();
		frames.clear();
		return dropped;
	}

	std::pair<uint32_t, bool> ItemViewIndex::viewSlot(uint32_t templateId, uint32_t turnQuanta, uint8_t level) {
		const uint64_t key = (static_cast<uint64_t>(templateId) << 24U) | (static_cast<uint64_t>(turnQuanta) << 8U) | level;
		const auto [found, inserted] = slotOfKey.try_emplace(key, static_cast<uint32_t>(slotOfKey.size()));
		return {found->second, inserted};
	}

	bool ItemViewIndex::settle(const assets::ItemLayout& layout, uint64_t frame, uint32_t& nextMesh, LayoutFrame*& out) {
		if (layout.id >= frames.size()) {
			frames.resize(static_cast<size_t>(layout.id) + 1);
		}
		LayoutFrame& settled = frames[layout.id];
		out = &settled;
		if (settled.frame == frame) {
			return false;
		}
		settled.frame = frame;
		settled.view = kNotAView;
		settled.firstMesh = nextMesh;
		nextMesh += static_cast<uint32_t>(layout.layout.strips.size());
		settled.itemsMeet = false;
		settled.items.clear();
		settled.boxMin = glm::vec2{std::numeric_limits<float>::max()};
		settled.boxMax = glm::vec2{std::numeric_limits<float>::lowest()};
		for (const assets::ItemStrip& strip : layout.layout.strips) {
			settled.boxMin = glm::min(settled.boxMin, glm::vec2{strip.minX, strip.minY});
			settled.boxMax = glm::max(settled.boxMax, glm::vec2{strip.maxX, strip.maxY});
		}
		return true;
	}

	void ItemViewIndex::noteItem(LayoutFrame& frame, glm::vec2 position) {
		if (frame.itemsMeet || std::find(frame.items.begin(), frame.items.end(), position) != frame.items.end()) {
			return;
		}
		const glm::vec2 size = frame.boxMax - frame.boxMin;
		for (const glm::vec2 other : frame.items) {
			const glm::vec2 apart = glm::abs(position - other);
			if (apart.x < size.x && apart.y < size.y) {
				frame.itemsMeet = true;
				return;
			}
		}
		frame.items.push_back(position);
	}

} // namespace worldview
