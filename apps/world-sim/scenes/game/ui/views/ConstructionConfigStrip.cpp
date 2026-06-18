#include "ConstructionConfigStrip.h"

#include <primitives/Primitives.h>
#include <theme/Tokens.h>

#include <cstdio>

namespace world_sim {

	ConstructionConfigStrip::ConstructionConfigStrip(const Args& args)
		: onMaterialSelected(args.onMaterialSelected),
		  onThicknessSelected(args.onThicknessSelected) {
		visible = false;
	}

	void ConstructionConfigStrip::setMaterials(std::vector<std::pair<std::string, float>> materials) {
		materials_ = std::move(materials);
		positionCards();
	}

	void ConstructionConfigStrip::setThicknessPresets(std::vector<ThicknessPresetInfo> presets) {
		presets_ = std::move(presets);
		positionCards();
	}

	void ConstructionConfigStrip::setStatus(const DrawingStatus& status) {
		const bool wallChanged = status_.wall != status.wall;
		status_ = status;
		visible = status.active;
		// Preset cards only exist in wall mode; reposition when the mode flips.
		if (wallChanged) {
			positionCards();
		}
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
		presetRects_.clear();
		float		x = stripBounds_.x + kPadding;
		const float y = stripBounds_.y + (kStripHeight - kCardHeight) * 0.5F;
		for (std::size_t i = 0; i < materials_.size(); ++i) {
			cardRects_.push_back({x, y, kCardWidth, kCardHeight});
			x += kCardWidth + kCardSpacing;
		}

		// Wall mode: thickness-preset cards follow the material cards, after a gap.
		// Narrower than material cards (just a name + thickness).
		if (status_.wall) {
			x += kCardSpacing * 2.0F;
			for (std::size_t i = 0; i < presets_.size(); ++i) {
				presetRects_.push_back({x, y, kPresetCardWidth, kCardHeight});
				x += kPresetCardWidth + kCardSpacing;
			}
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
					event.consume();
					return true;
				}
			}
			for (std::size_t i = 0; i < presetRects_.size() && i < presets_.size(); ++i) {
				if (presetRects_[i].contains(event.position)) {
					if (onThicknessSelected) {
						onThicknessSelected(presets_[i].name);
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
				{.fill = UI::bg_panel,
				 .border = Foundation::BorderStyle{.color = UI::line_edge, .width = 1.0F}},
			.id = "config_strip_bg",
		});

		// Material cards.
		for (std::size_t i = 0; i < materials_.size() && i < cardRects_.size(); ++i) {
			const bool selected = (materials_[i].first == status_.material);
			Renderer::Primitives::drawRect({
				.bounds = cardRects_[i],
				.style =
					{.fill = selected ? UI::bg_active : UI::bg_inset,
					 .border =
						 Foundation::BorderStyle{
							 .color = selected ? UI::accent : UI::line_edge,
							 .width = selected ? 2.0F : 1.0F
						 }},
				.id = "config_strip_card",
			});

			Renderer::Primitives::drawText({
				.text = materials_[i].first,
				.position = {cardRects_[i].x + 8.0F, cardRects_[i].y + 5.0F},
				.scale = 0.85F,
				.color = UI::text_bright,
			});

			char cost[32];
			std::snprintf(cost, sizeof(cost), "%.1f /m\xC2\xB2", static_cast<double>(materials_[i].second));
			Renderer::Primitives::drawText({
				.text = cost,
				.position = {cardRects_[i].x + 8.0F, cardRects_[i].y + 22.0F},
				.scale = 0.7F,
				.color = UI::text_dim,
			});
		}

		// Thickness-preset cards (wall mode).
		for (std::size_t i = 0; i < presets_.size() && i < presetRects_.size(); ++i) {
			const bool selected = (presets_[i].name == status_.thicknessPreset);
			Renderer::Primitives::drawRect({
				.bounds = presetRects_[i],
				.style =
					{.fill = selected ? UI::bg_active : UI::bg_inset,
					 .border =
						 Foundation::BorderStyle{
							 .color = selected ? UI::accent : UI::line_edge,
							 .width = selected ? 2.0F : 1.0F
						 }},
				.id = "config_strip_preset",
			});
			Renderer::Primitives::drawText({
				.text = presets_[i].name,
				.position = {presetRects_[i].x + 8.0F, presetRects_[i].y + 5.0F},
				.scale = 0.8F,
				.color = UI::text_bright,
			});
			char thick[24];
			std::snprintf(thick, sizeof(thick), "%.2f m", static_cast<double>(presets_[i].thicknessMeters));
			Renderer::Primitives::drawText({
				.text = thick,
				.position = {presetRects_[i].x + 8.0F, presetRects_[i].y + 22.0F},
				.scale = 0.7F,
				.color = UI::text_dim,
			});
		}

		// Readouts, to the right of whichever set of cards is last.
		float readoutX = stripBounds_.x + kPadding + static_cast<float>(materials_.size()) * (kCardWidth + kCardSpacing) + 8.0F;
		if (status_.wall && !presetRects_.empty()) {
			readoutX = presetRects_.back().x + kPresetCardWidth + 16.0F;
		}

		if (status_.opening) {
			// Opening mode: the type (Door/Window) is chosen from the Build menu (no
			// in-strip selector in v1), so the strip just displays it plus the clear
			// width. The validity line below carries the snap / placement feedback.
			Renderer::Primitives::drawText({
				.text = std::string("Opening: ") + status_.openingType,
				.position = {readoutX, stripBounds_.y + 8.0F},
				.scale = 0.8F,
				.color = UI::text,
			});
			char widthBuf[32];
			std::snprintf(widthBuf, sizeof(widthBuf), "Width: %.2f m", static_cast<double>(status_.openingWidthMeters));
			Renderer::Primitives::drawText({
				.text = widthBuf,
				.position = {readoutX, stripBounds_.y + 26.0F},
				.scale = 0.8F,
				.color = UI::text,
			});
		} else if (status_.wall) {
			char lenBuf[48];
			std::snprintf(
				lenBuf,
				sizeof(lenBuf),
				"Len: %.1f m  (seg %.1f m)",
				static_cast<double>(status_.totalLengthMeters),
				static_cast<double>(status_.segmentLengthMeters)
			);
			Renderer::Primitives::drawText({
				.text = lenBuf,
				.position = {readoutX, stripBounds_.y + 8.0F},
				.scale = 0.8F,
				.color = UI::text,
			});
			char costBuf[48];
			std::snprintf(
				costBuf,
				sizeof(costBuf),
				"Cost: %.0f  Work: %.0f",
				static_cast<double>(status_.wallCost),
				static_cast<double>(status_.wallWork)
			);
			Renderer::Primitives::drawText({
				.text = costBuf,
				.position = {readoutX, stripBounds_.y + 26.0F},
				.scale = 0.8F,
				.color = UI::text,
			});
		} else {
			char areaBuf[48];
			std::snprintf(areaBuf, sizeof(areaBuf), "Area: %.1f m\xC2\xB2", static_cast<double>(status_.areaSquareMeters));
			Renderer::Primitives::drawText({
				.text = areaBuf,
				.position = {readoutX, stripBounds_.y + 8.0F},
				.scale = 0.8F,
				.color = UI::text,
			});
			char ptsBuf[32];
			std::snprintf(ptsBuf, sizeof(ptsBuf), "Points: %d", status_.pointCount);
			Renderer::Primitives::drawText({
				.text = ptsBuf,
				.position = {readoutX, stripBounds_.y + 26.0F},
				.scale = 0.8F,
				.color = UI::text,
			});
		}

		// Validity message line, colored by status.
		Foundation::Color msgColor = UI::status_ok;
		std::string		  message = "Ready";
		if (status_.opening) {
			if (status_.valid) {
				msgColor = UI::status_ok;
				message = "Click a wall to place";
			} else {
				// No wall in range reads as pending (move onto a built wall); an in-range
				// wall that fails the placement gate reads as blocked.
				const bool blocked = !status_.message.empty() && status_.message != "no wall in range";
				msgColor = blocked ? UI::status_crit : UI::status_warn;
				message = status_.message.empty() ? "Hover a built wall" : status_.message;
			}
		} else if (status_.wall) {
			if (status_.pointCount == 0) {
				msgColor = UI::status_warn;
				message = "Click a foundation to start";
			} else {
				msgColor = UI::status_ok;
				message = "Right-click / Enter to finish";
			}
			if (!status_.valid && !status_.message.empty()) {
				msgColor = UI::status_crit;
				message = status_.message;
			}
		} else {
			if (status_.pointCount > 0 && status_.pointCount < 3) {
				msgColor = UI::status_warn;
				message = "Keep placing points";
			}
			if (!status_.valid && !status_.message.empty()) {
				msgColor = UI::status_crit;
				message = status_.message;
			} else if (status_.valid && status_.pointCount >= 3) {
				msgColor = UI::status_ok;
				message = "Click origin to close";
			}
		}

		Renderer::Primitives::drawText({
			.text = message,
			.position = {readoutX + 180.0F, stripBounds_.y + 18.0F},
			.scale = 0.85F,
			.color = msgColor,
		});
	}

} // namespace world_sim
