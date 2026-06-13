#include "ConstructionConfigStrip.h"

#include <primitives/Primitives.h>
#include <theme/Theme.h>

#include <cstdio>

namespace world_sim {

	ConstructionConfigStrip::ConstructionConfigStrip(const Args& args)
		: onMaterialSelected(args.onMaterialSelected) {
		visible = false;
	}

	void ConstructionConfigStrip::setMaterials(std::vector<std::pair<std::string, float>> materials) {
		materials_ = std::move(materials);
		positionCards();
	}

	void ConstructionConfigStrip::setStatus(const DrawingStatus& status) {
		status_ = status;
		visible = status.active;
	}

	void ConstructionConfigStrip::layout(const Foundation::Rect& viewportBounds) {
		Component::layout(viewportBounds);

		// Dock the strip horizontally centered, sitting just above the gameplay bar.
		const float width = viewportBounds.width * 0.6F;
		stripBounds_ = {
			viewportBounds.x + (viewportBounds.width - width) * 0.5F,
			viewportBounds.y + viewportBounds.height - kGameplayBarReserve - kStripHeight,
			width,
			kStripHeight,
		};
		positionCards();
	}

	void ConstructionConfigStrip::positionCards() {
		cardRects_.clear();
		float		x = stripBounds_.x + kPadding;
		const float y = stripBounds_.y + (kStripHeight - kCardHeight) * 0.5F;
		for (std::size_t i = 0; i < materials_.size(); ++i) {
			cardRects_.push_back({x, y, kCardWidth, kCardHeight});
			x += kCardWidth + kCardSpacing;
		}
	}

	bool ConstructionConfigStrip::handleEvent(UI::InputEvent& event) {
		if (!visible) {
			return false;
		}

		// Block clicks anywhere on the strip from falling through to the world.
		const bool overStrip = stripBounds_.contains(event.position);
		if (event.type == UI::InputEvent::Type::MouseUp && overStrip) {
			for (std::size_t i = 0; i < cardRects_.size(); ++i) {
				if (cardRects_[i].contains(event.position)) {
					if (onMaterialSelected) {
						onMaterialSelected(materials_[i].first);
					}
					break;
				}
			}
			event.consume();
			return true;
		}
		if ((event.type == UI::InputEvent::Type::MouseDown || event.type == UI::InputEvent::Type::MouseMove) && overStrip) {
			event.consume();
			return true;
		}
		return false;
	}

	void ConstructionConfigStrip::render() {
		if (!visible) {
			return;
		}

		// Strip background.
		Renderer::Primitives::drawRect({
			.bounds = stripBounds_,
			.style =
				{.fill = UI::Theme::Colors::sidebarBackground,
				 .border = Foundation::BorderStyle{.color = UI::Theme::Colors::cardBorder, .width = 1.0F}},
			.id = "config_strip_bg",
		});

		// Material cards.
		for (std::size_t i = 0; i < materials_.size() && i < cardRects_.size(); ++i) {
			const bool selected = (materials_[i].first == status_.material);
			Renderer::Primitives::drawRect({
				.bounds = cardRects_[i],
				.style =
					{.fill = selected ? UI::Theme::Colors::selectionBackground : UI::Theme::Colors::cardBackground,
					 .border =
						 Foundation::BorderStyle{
							 .color = selected ? UI::Theme::Colors::selectionBorder : UI::Theme::Colors::cardBorder,
							 .width = selected ? 2.0F : 1.0F
						 }},
				.id = "config_strip_card",
			});

			Renderer::Primitives::drawText({
				.text = materials_[i].first,
				.position = {cardRects_[i].x + 8.0F, cardRects_[i].y + 5.0F},
				.scale = 0.85F,
				.color = UI::Theme::Colors::textTitle,
			});

			char cost[32];
			std::snprintf(cost, sizeof(cost), "%.1f /m\xC2\xB2", static_cast<double>(materials_[i].second));
			Renderer::Primitives::drawText({
				.text = cost,
				.position = {cardRects_[i].x + 8.0F, cardRects_[i].y + 22.0F},
				.scale = 0.7F,
				.color = UI::Theme::Colors::textSecondary,
			});
		}

		// Readouts: area + point count, to the right of the cards.
		const float readoutX = stripBounds_.x + kPadding + static_cast<float>(materials_.size()) * (kCardWidth + kCardSpacing) + 8.0F;
		char		areaBuf[48];
		std::snprintf(areaBuf, sizeof(areaBuf), "Area: %.1f m\xC2\xB2", static_cast<double>(status_.areaSquareMeters));
		Renderer::Primitives::drawText({
			.text = areaBuf,
			.position = {readoutX, stripBounds_.y + 8.0F},
			.scale = 0.8F,
			.color = UI::Theme::Colors::textBody,
		});

		char ptsBuf[32];
		std::snprintf(ptsBuf, sizeof(ptsBuf), "Points: %d", status_.pointCount);
		Renderer::Primitives::drawText({
			.text = ptsBuf,
			.position = {readoutX, stripBounds_.y + 26.0F},
			.scale = 0.8F,
			.color = UI::Theme::Colors::textBody,
		});

		// Validity message line, colored by status.
		Foundation::Color msgColor = UI::Theme::Colors::statusActive;
		std::string		  message = "Ready";
		if (status_.pointCount > 0 && status_.pointCount < 3) {
			msgColor = UI::Theme::Colors::statusPending;
			message = "Keep placing points";
		}
		if (!status_.valid && !status_.message.empty()) {
			msgColor = UI::Theme::Colors::statusBlocked;
			message = status_.message;
		} else if (status_.valid && status_.pointCount >= 3) {
			msgColor = UI::Theme::Colors::statusActive;
			message = "Click origin to close";
		}

		Renderer::Primitives::drawText({
			.text = message,
			.position = {readoutX + 160.0F, stripBounds_.y + 18.0F},
			.scale = 0.85F,
			.color = msgColor,
		});
	}

} // namespace world_sim
