#include "scenes/game/world/construction/DrawingStatusHint.h"

namespace world_sim {

	StatusHint statusHint(const DrawingStatus& status) {
		StatusHint hint{.text = "Ready", .tone = StatusHintTone::Ok};
		if (status.opening) {
			if (status.valid) {
				hint.text = "Click a wall to place";
			} else {
				// No wall in range is pending (move onto a built wall); a wall in range that fails the
				// placement gate is blocked.
				const bool blocked = !status.message.empty() && status.message != "no wall in range";
				hint.tone = blocked ? StatusHintTone::Crit : StatusHintTone::Warn;
				hint.text = status.message.empty() ? "Hover a built wall" : status.message;
			}
		} else if (status.wall) {
			if (status.pointCount == 0) {
				hint.tone = StatusHintTone::Warn;
				hint.text = "Click a foundation to start";
			} else {
				hint.text = "Right-click / Enter to finish";
			}
			if (!status.valid && !status.message.empty()) {
				hint.tone = StatusHintTone::Crit;
				hint.text = status.message;
			}
		} else if (status.foundationTool == FoundationTool::Rectangle) {
			hint.text = status.pointCount == 0 ? "Click or drag from a corner" : "Click the opposite corner";
			if (!status.valid && !status.message.empty()) {
				hint.tone = StatusHintTone::Crit;
				hint.text = status.message;
			}
		} else {
			if (status.foundationEdit && status.pointCount == 0) {
				hint.text = "Draw a region across the foundation edge";
			}
			if (status.pointCount > 0 && status.pointCount < 3) {
				hint.tone = StatusHintTone::Warn;
				hint.text = "Keep placing points";
			}
			if (!status.valid && !status.message.empty()) {
				hint.tone = StatusHintTone::Crit;
				hint.text = status.message;
			} else if (status.valid && status.pointCount >= 3) {
				hint.tone = StatusHintTone::Ok;
				hint.text = "Click origin to close";
			}
		}
		return hint;
	}

} // namespace world_sim
