#pragma once

// SelectionOutline - a selected entity's silhouette, injected into the entity depth-sort stream so a nearer entity
// correctly occludes a farther selection's outline. Built by SelectionSystem (world-space lines and the depth each
// piece draws at), consumed by InstancedEntityRenderer::emitSorted. Most entities are one piece of closed rings at
// their ground-contact anchorY; a parts item is a piece of open lines a depth strip, each at its strip's anchor, so a
// colonist in front of one end of a turned item draws over that end's outline and under the other's
// (rotatable-placed-items 15).

#include <assets/ItemView.h>
#include <math/Types.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace worldview {

	struct SelectionOutline {
		struct Piece {
			std::vector<std::vector<Foundation::Vec2>> lines; // world metres
			bool									   closed = true;
			float									   anchorY = 0.0F; // drawn just behind entities with a larger anchorY
		};
		std::vector<Piece> pieces; // ascending anchorY
		/// A parts item's layout and position, so its own strips, which the outline lies over, don't split its pieces.
		const assets::ItemLayout* itemLayout = nullptr;
		glm::vec2				  itemPosition{0.0F, 0.0F};
		glm::vec4				  rgba{1.0F, 0.85F, 0.0F, 0.9F};
		float					  widthPx = 4.0F;
		bool					  valid = false;
	};

	/// A box on the screen, in pixels.
	struct ScreenBox {
		Foundation::Vec2 min{0.0F, 0.0F};
		Foundation::Vec2 max{0.0F, 0.0F};

		[[nodiscard]] bool meets(const ScreenBox& other) const {
			return min.x <= other.max.x && other.min.x <= max.x && min.y <= other.max.y && other.min.y <= max.y;
		}
	};

	/// Where in the stream an outline's pieces draw (rotatable-placed-items 15). A piece draws before the first upright
	/// strictly nearer than its anchor; consecutive pieces join into one draw at the largest anchor among them unless
	/// an upright other than the item's own strips sorts between them and its box meets theirs. Each draw closes every
	/// open run, so the fewer the better; a lone piece draws exactly where a single outline always has.
	class OutlineCursor {
	  public:
		/// Starts a frame over pieces, ascending by anchor, each with its screen box (padded by the line's half width).
		void begin(std::span<const float> anchors, std::span<const ScreenBox> boxes) {
			pieceAnchors = anchors;
			pieceBoxes = boxes;
			drawn = 0;
			passed = 0;
		}

		/// How many pieces, from drawnSoFar(), to draw before an upright at `anchorY`: its box (nullopt when not known,
		/// taken to meet), and whether it is one of the outlined item's own strips.
		[[nodiscard]] size_t before(float anchorY, const std::optional<ScreenBox>& box, bool ownStrip) {
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

		/// The pieces still to draw once the stream has ended, from drawnSoFar().
		[[nodiscard]] size_t remaining() {
			const size_t count = pieceAnchors.size() - drawn;
			drawn = pieceAnchors.size();
			return count;
		}

		[[nodiscard]] size_t drawnSoFar() const { return drawn; }

	  private:
		std::span<const float>	   pieceAnchors;
		std::span<const ScreenBox> pieceBoxes;
		size_t					   drawn = 0;
		size_t					   passed = 0;
	};

} // namespace worldview
