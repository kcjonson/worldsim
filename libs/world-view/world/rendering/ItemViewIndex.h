#pragma once

// What InstancedEntityRenderer keeps about parts items' views and strip layouts apart from their GL meshes
// (rotatable-placed-items 9.2 and 10): which view slot a (template, turn, level) has, and, per kept layout, what
// this frame settled for it at its first strip. GL-free, so the bookkeeping is tested without a context.

#include "world/rendering/StripRuns.h"

#include <assets/ItemView.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace worldview {

	/// A kept layout's frame: the view its strips draw from, the planner mesh id of its first strip, and the
	/// positions of its items seen so far, to tell whether any two of them meet on the screen.
	struct LayoutFrame {
		uint64_t			   frame = 0;
		uint32_t			   view = kNotAView;
		uint32_t			   firstMesh = 0;
		glm::vec2			   boxMin{0.0F, 0.0F}; // the whole layout's box about the pivot
		glm::vec2			   boxMax{0.0F, 0.0F};
		bool				   itemsMeet = false;
		std::vector<glm::vec2> items;
	};

	class ItemViewIndex {
	  public:
		/// Forgets every view slot and layout frame when the registry's generation isn't the one they were made
		/// under (template ids and layouts started over); true when it did, and the caller drops its views.
		bool follow(uint64_t generation);

		/// The view slot of (template, turn, level), and whether it is new: a new slot is numbered viewCount() - 1.
		[[nodiscard]] std::pair<uint32_t, bool> viewSlot(uint32_t templateId, uint32_t turnQuanta, uint8_t level);
		[[nodiscard]] uint32_t					viewCount() const { return static_cast<uint32_t>(slotOfKey.size()); }

		/// The layout's frame record; at its first strip of a frame (returned true) it is reset, and its strips are
		/// given planner mesh ids from `nextMesh` on, which moves past them.
		bool settle(const assets::ItemLayout& layout, uint64_t frame, uint32_t& nextMesh, LayoutFrame*& out);

		/// Notes one of a layout's items, at its position, among the frame's strips: once two items' boxes meet on
		/// the screen, the layout's frame says so for the rest of the frame.
		static void noteItem(LayoutFrame& frame, glm::vec2 position);

		[[nodiscard]] const LayoutFrame& frameOf(uint32_t layoutId) const { return frames[layoutId]; }

	  private:
		uint64_t								   seenGeneration = 0;
		bool									   followed = false;
		std::unordered_map<uint64_t, uint32_t>	   slotOfKey;
		std::vector<LayoutFrame>				   frames; // by ItemLayout::id
	};

} // namespace worldview
