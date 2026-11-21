#pragma once

// The ranged merge of rotatable-placed-items 9.2: at a close, neighbouring runs that are consecutive depth strips
// of one item view, holding the same instances, are drawn as one ranged draw, since consecutive strips are
// contiguous index ranges of the view. GL-free; InstancedEntityRenderer issues the draws.

#include "primitives/InstanceData.h"
#include "world/rendering/RunPlanner.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace worldview {

	/// A run that draws a template, not a strip of a view.
	inline constexpr uint32_t kNotAView = std::numeric_limits<uint32_t>::max();

	/// One draw at a close: `count` runs of the closing list from `first`, drawn at once.
	struct RunSpan {
		uint32_t first = 0;
		uint32_t count = 1;
	};

	[[nodiscard]] inline bool sameInstances(const std::vector<Renderer::InstanceData>& left, const std::vector<Renderer::InstanceData>& right) {
		static_assert(sizeof(Renderer::InstanceData) == 32, "compared bytewise: no padding");
		return left.size() == right.size() && (left.empty() || std::memcmp(left.data(), right.data(), left.size() * sizeof(Renderer::InstanceData)) == 0);
	}

	/// Splits a close's runs, in the order they draw, into draws. `RunType` has `view` (kNotAView for a template),
	/// `strip`, `instances`, and `itemsApart` (no two of its view's items in the frame meet on the screen); `runs`
	/// is indexed by the planner's draw index. A merged draw draws each instance's strips together, which reorders
	/// strips of different items against each other, so runs of several instances merge only when their items are
	/// apart; one instance's strips in stored order are its own depth order.
	template <typename RunType>
	void spanRuns(const RunPlanner& planner, std::span<const uint32_t> closing, const std::vector<RunType>& runs, std::vector<RunSpan>& out) {
		out.clear();
		for (uint32_t i = 0; i < closing.size(); ++i) {
			const RunType& run = runs[closing[i]];
			if (!out.empty() && run.view != kNotAView && (run.instances.size() == 1 || run.itemsApart)) {
				RunSpan&	   last = out.back();
				const uint32_t previousIndex = closing[last.first + last.count - 1];
				const RunType& previous = runs[previousIndex];
				if (previous.view == run.view && run.strip == previous.strip + 1 && planner.neighbours(previousIndex, closing[i]) &&
					sameInstances(previous.instances, run.instances)) {
					++last.count;
					continue;
				}
			}
			out.push_back({.first = i, .count = 1});
		}
	}

} // namespace worldview
