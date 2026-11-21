#include "SelectionOutline.h"

namespace worldview {

	void OutlineCursor::begin(std::span<const float> anchors, std::span<const ScreenBox> boxes) {
		pieceAnchors = anchors;
		pieceBoxes = boxes;
		drawn = 0;
		passed = 0;
	}

	size_t OutlineCursor::before(float anchorY, const std::optional<ScreenBox>& box, bool ownStrip) {
		while (passed < pieceAnchors.size() && pieceAnchors[passed] < anchorY) {
			++passed;
		}
		if (passed == drawn) {
			return 0;
		}
		bool draw = passed == pieceAnchors.size(); // nothing left to join
		if (!draw && !ownStrip) {
			draw = !box.has_value();
			for (size_t piece = drawn; piece < passed && !draw; ++piece) {
				draw = pieceBoxes[piece].meets(*box);
			}
		}
		if (!draw) {
			return 0;
		}
		const size_t count = passed - drawn;
		drawn = passed;
		return count;
	}

	size_t OutlineCursor::remaining() {
		const size_t count = pieceAnchors.size() - drawn;
		drawn = pieceAnchors.size();
		return count;
	}

} // namespace worldview
