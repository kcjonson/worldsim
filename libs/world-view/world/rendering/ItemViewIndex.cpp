#include "ItemViewIndex.h"

#include <glm/common.hpp>

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
		records.clear();
		freeSlots.clear();
		readyOfTurn.clear();
		frames.clear();
		return dropped;
	}

	std::pair<uint32_t, bool> ItemViewIndex::viewSlot(uint32_t templateId, uint32_t turnQuanta, uint8_t level, bool reference, uint64_t frame) {
		const uint64_t key = viewKey(templateId, turnQuanta, level);
		if (const auto found = slotOfKey.find(key); found != slotOfKey.end()) {
			return {found->second, false};
		}
		uint32_t slot = static_cast<uint32_t>(records.size());
		if (!freeSlots.empty()) {
			slot = freeSlots.back();
			freeSlots.pop_back();
		} else {
			records.emplace_back();
		}
		records[slot] = {
			.templateId = templateId, .turnQuanta = turnQuanta, .level = level, .reference = reference, .wantedFrame = frame, .drawnFrame = frame
		};
		slotOfKey.emplace(key, slot);
		return {slot, true};
	}

	std::optional<uint32_t> ItemViewIndex::findView(uint32_t templateId, uint32_t turnQuanta, uint8_t level) const {
		const auto found = slotOfKey.find(viewKey(templateId, turnQuanta, level));
		return found != slotOfKey.end() ? std::optional<uint32_t>{found->second} : std::nullopt;
	}

	void ItemViewIndex::setState(uint32_t slot, ViewState state) {
		ViewRecord&	   record = records[slot];
		const uint64_t key = turnKey(record.templateId, record.turnQuanta);
		const auto	   bit = static_cast<uint8_t>(1U << record.level);
		record.state = state;
		if (state == ViewState::Ready) {
			readyOfTurn[key] |= bit;
			return;
		}
		if (const auto found = readyOfTurn.find(key); found != readyOfTurn.end()) {
			found->second = static_cast<uint8_t>(found->second & ~bit);
			if (found->second == 0) {
				readyOfTurn.erase(found);
			}
		}
	}

	uint8_t ItemViewIndex::readyMask(uint32_t templateId, uint32_t turnQuanta) const {
		const auto found = readyOfTurn.find(turnKey(templateId, turnQuanta));
		return found != readyOfTurn.end() ? found->second : 0;
	}

	std::vector<uint32_t> ItemViewIndex::releaseStale(
		uint64_t frame, uint64_t evictAfter, const std::function<bool(uint32_t templateId, uint32_t turnQuanta)>& kept
	) {
		std::vector<uint32_t> released;
		for (const auto& [key, slot] : slotOfKey) {
			const ViewRecord& record = records[slot];
			const uint64_t	  lastUse = record.state == ViewState::Ready ? record.drawnFrame : record.wantedFrame;
			if (frame - lastUse <= evictAfter || (record.reference && kept(record.templateId, record.turnQuanta))) {
				continue;
			}
			released.push_back(slot);
		}
		for (const uint32_t slot : released) {
			setState(slot, ViewState::Absent);
			const ViewRecord& record = records[slot];
			slotOfKey.erase(viewKey(record.templateId, record.turnQuanta, record.level));
			freeSlots.push_back(slot);
		}
		return released;
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
		settled.levelUse = UINT32_MAX;
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
		if (frame.itemsMeet) {
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
