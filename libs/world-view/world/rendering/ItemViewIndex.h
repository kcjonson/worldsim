#pragma once

// What InstancedEntityRenderer keeps about parts items' views and strip layouts apart from their GL meshes
// (rotatable-placed-items 9.2 and 10): the view slot each (template, turn, level) has and the state it is in, which
// levels of a (template, turn) are ready, which views nothing wants any more, and, per kept layout, what this frame
// settled for it at its first strip. GL-free, so the bookkeeping is tested without a context.

#include "world/rendering/StripRuns.h"

#include <assets/DetailLevels.h>
#include <assets/ItemView.h>

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace worldview {

	/// A layout frame whose view draws at no level the renderer counts.
	inline constexpr uint32_t kNoLevelUse = std::numeric_limits<uint32_t>::max();

	/// A kept layout's frame: the view its strips draw from, the planner mesh id of its first strip, and the
	/// positions of its items seen so far, to tell whether any two of them meet on the screen.
	struct LayoutFrame {
		uint64_t			   frame = 0;
		uint32_t			   view = kNotAView;
		uint32_t			   firstMesh = 0;
		uint32_t			   levelUse = kNoLevelUse; // the renderer's level statistics entry for its view this frame
		glm::vec2			   boxMin{0.0F, 0.0F}; // the whole layout's box about the pivot
		glm::vec2			   boxMax{0.0F, 0.0F};
		bool				   itemsMeet = false;
		std::vector<glm::vec2> items;
	};

	enum class ViewState : uint8_t {
		Absent,	   // not built
		Requested, // with the view builder
		Waiting,   // built, waiting for an upload
		Ready,	   // on the GPU
		Failed,	   // the build or upload failed (logged); not retried while the slot is kept
	};

	/// One view slot's bookkeeping.
	struct ViewRecord {
		uint32_t  templateId = 0;
		uint32_t  turnQuanta = 0;
		uint8_t	  level = 0;
		bool	  reference = false; // the level everything that isn't drawing reads
		ViewState state = ViewState::Absent;
		uint64_t  wantedFrame = 0;
		uint64_t  drawnFrame = 0;
	};

	class ItemViewIndex {
	  public:
		/// Forgets every view slot and layout frame when the registry's generation isn't the one they were made
		/// under (template ids and layouts started over); true when it did, and the caller drops its views.
		bool follow(uint64_t generation);

		/// Whether a stream record's layout, asked for under `layoutGeneration`, may be read: only when that is the
		/// generation followed, since a clear frees the layouts made before it.
		[[nodiscard]] bool readable(uint64_t layoutGeneration) const { return followed && layoutGeneration == seenGeneration; }

		/// The view slot of (template, turn, level), and whether it is new (in state Absent, wanted and drawn at
		/// `frame`). A released slot's number is handed out again.
		[[nodiscard]] std::pair<uint32_t, bool> viewSlot(uint32_t templateId, uint32_t turnQuanta, uint8_t level, bool reference, uint64_t frame);

		/// The view slot of (template, turn, level), if it has one.
		[[nodiscard]] std::optional<uint32_t> findView(uint32_t templateId, uint32_t turnQuanta, uint8_t level) const;

		[[nodiscard]] ViewRecord&		view(uint32_t slot) { return records[slot]; }
		[[nodiscard]] const ViewRecord& view(uint32_t slot) const { return records[slot]; }

		/// Sets a slot's state, keeping its (template, turn)'s ready levels in step.
		void setState(uint32_t slot, ViewState state);

		/// Bit i: level i of (template, turn) is Ready.
		[[nodiscard]] uint8_t readyMask(uint32_t templateId, uint32_t turnQuanta) const;

		/// Releases the views nothing wants (rotatable-placed-items 10) and returns their slots, for the caller to free
		/// their meshes: a Ready view not drawn for more than `evictAfter` frames, any other not wanted for that long,
		/// and of a reference view only one whose (template, turn) no installed item stands at, as `kept` answers.
		std::vector<uint32_t> releaseStale(uint64_t frame, uint64_t evictAfter, const std::function<bool(uint32_t templateId, uint32_t turnQuanta)>& kept);

		/// Slots in use.
		[[nodiscard]] uint32_t viewCount() const { return static_cast<uint32_t>(slotOfKey.size()); }
		/// Slots ever numbered: the views vector's size.
		[[nodiscard]] uint32_t slotBound() const { return static_cast<uint32_t>(records.size()); }

		/// The layout's frame record; at its first strip of a frame (returned true) it is reset, and its strips are
		/// given planner mesh ids from `nextMesh` on, which moves past them.
		bool settle(const assets::ItemLayout& layout, uint64_t frame, uint32_t& nextMesh, LayoutFrame*& out);

		/// Notes one of a layout's items, at its position, once a frame: once two items' boxes meet on the screen,
		/// the layout's frame says so for the rest of the frame.
		static void noteItem(LayoutFrame& frame, glm::vec2 position);

		[[nodiscard]] const LayoutFrame& frameOf(uint32_t layoutId) const { return frames[layoutId]; }

	  private:
		[[nodiscard]] static uint64_t viewKey(uint32_t templateId, uint32_t turnQuanta, uint8_t level) {
			return (static_cast<uint64_t>(templateId) << 24U) | (static_cast<uint64_t>(turnQuanta) << 8U) | level;
		}
		[[nodiscard]] static uint64_t turnKey(uint32_t templateId, uint32_t turnQuanta) {
			return (static_cast<uint64_t>(templateId) << 32U) | turnQuanta;
		}

		uint64_t							   seenGeneration = 0;
		bool								   followed = false;
		std::unordered_map<uint64_t, uint32_t> slotOfKey;
		std::vector<ViewRecord>				   records; // by slot
		std::vector<uint32_t>				   freeSlots;
		std::unordered_map<uint64_t, uint8_t>  readyOfTurn; // a (template, turn) with a level Ready
		std::vector<LayoutFrame>			   frames;		// by ItemLayout::id
	};

} // namespace worldview
