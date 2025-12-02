#include "ZoomControl.h"

#include <sstream>

namespace world_sim {

	namespace {
		constexpr float kButtonSize = 28.0F;
		constexpr float kTextWidth = 50.0F;
		constexpr float kSpacing = 4.0F;
		constexpr float kFontSize = 14.0F;
	} // namespace

	ZoomControl::ZoomControl(const Args& args)
		: position(args.position) {
		float x = position.x;
		float y = position.y;

		// Zoom out button (-)
		zoomOutButton = std::make_unique<UI::Button>(UI::Button::Args{
			.label = "-",
			.position = {x, y},
			.size = {kButtonSize, kButtonSize},
			.type = UI::Button::Type::Primary,
			.onClick = args.onZoomOut,
			.id = "btn_zoom_out"
		});
		x += kButtonSize + kSpacing;

		// Zoom percentage text (centered between buttons)
		zoomText = std::make_unique<UI::Text>(UI::Text::Args{
			.position = {x + kTextWidth * 0.5F, y + kButtonSize * 0.5F},
			.text = "100%",
			.style =
				{
					.color = Foundation::Color::white(),
					.fontSize = kFontSize,
					.hAlign = Foundation::HorizontalAlign::Center,
					.vAlign = Foundation::VerticalAlign::Middle,
				},
			.id = "zoom_text"
		});
		x += kTextWidth + kSpacing;

		// Zoom in button (+)
		zoomInButton = std::make_unique<UI::Button>(UI::Button::Args{
			.label = "+",
			.position = {x, y},
			.size = {kButtonSize, kButtonSize},
			.type = UI::Button::Type::Primary,
			.onClick = args.onZoomIn,
			.id = "btn_zoom_in"
		});
	}

	void ZoomControl::setZoomPercent(int percent) {
		if (zoomPercent != percent) {
			zoomPercent = percent;
			updateZoomText();
		}
	}

	void ZoomControl::setPosition(Foundation::Vec2 newPosition) {
		if (position.x == newPosition.x && position.y == newPosition.y) {
			return;
		}
		position = newPosition;

		float x = position.x;
		float y = position.y;

		// Update button positions
		if (zoomOutButton) {
			zoomOutButton->position = {x, y};
		}
		x += kButtonSize + kSpacing;

		// Text is recreated in updateZoomText(), just update position reference
		x += kTextWidth + kSpacing;

		if (zoomInButton) {
			zoomInButton->position = {x, y};
		}

		// Update text position
		updateZoomText();
	}

	void ZoomControl::updateZoomText() {
		std::ostringstream oss;
		oss << zoomPercent << "%";

		float x = position.x + kButtonSize + kSpacing;
		float y = position.y;

		// Update existing text in-place (created in constructor)
		zoomText->text = oss.str();
		zoomText->position = {x + kTextWidth * 0.5F, y + kButtonSize * 0.5F};
	}

	void ZoomControl::handleInput() {
		if (zoomOutButton) {
			zoomOutButton->handleInput();
		}
		if (zoomInButton) {
			zoomInButton->handleInput();
		}
	}

	void ZoomControl::render() {
		if (zoomOutButton) {
			zoomOutButton->render();
		}
		if (zoomText) {
			zoomText->render();
		}
		if (zoomInButton) {
			zoomInButton->render();
		}
	}

} // namespace world_sim
