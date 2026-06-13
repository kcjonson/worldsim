#pragma once

// ConstructionConfigStrip - docked strip above the GameplayBar, shown while a
// structure tool is active (building-construction D11 config strip).
//
// Holds material cards (Wood/Stone from ConstructionRegistry, click to select),
// live readouts (area in m^2, point count), and a validity message line colored
// via Theme status colors. Hidden when the tool is inactive. Renders directly
// through Primitives rather than the Layer tree: the readouts and validity text
// change every frame, so immediate-mode drawing is simpler than reconciling
// child text nodes.

#include "scenes/game/world/construction/DrawingSystem.h"

#include <component/Component.h>
#include <graphics/Rect.h>
#include <input/InputEvent.h>

#include <functional>
#include <string>
#include <vector>

namespace world_sim {

	class ConstructionConfigStrip : public UI::Component {
	  public:
		struct Args {
			/// Called when a material card is clicked.
			std::function<void(const std::string&)> onMaterialSelected;
			std::string								id = "construction_config_strip";
		};

		explicit ConstructionConfigStrip(const Args& args);

		/// Set the available materials (name + per-m^2 cost), from the registry.
		void setMaterials(std::vector<std::pair<std::string, float>> materials);

		/// Push the latest tool status (drives readouts, validity line, selection
		/// highlight, and visibility).
		void setStatus(const DrawingStatus& status);

		void  layout(const Foundation::Rect& viewportBounds) override;
		bool  handleEvent(UI::InputEvent& event) override;
		void  render() override;
		float getHeight() const override { return kStripHeight; }

	  private:
		static constexpr float kStripHeight = 56.0F;
		static constexpr float kGameplayBarReserve = 64.0F; // bar height + margins
		static constexpr float kCardWidth = 120.0F;
		static constexpr float kCardHeight = 40.0F;
		static constexpr float kCardSpacing = 8.0F;
		static constexpr float kPadding = 12.0F;

		std::function<void(const std::string&)> onMaterialSelected;

		std::vector<std::pair<std::string, float>> materials_;
		DrawingStatus							   status_;

		Foundation::Rect stripBounds_{0.0F, 0.0F, 0.0F, 0.0F};
		// Hit rects for each material card, parallel to materials_.
		std::vector<Foundation::Rect> cardRects_;

		void positionCards();
	};

} // namespace world_sim
