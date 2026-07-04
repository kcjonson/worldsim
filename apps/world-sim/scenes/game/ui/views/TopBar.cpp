#include "TopBar.h"

#include <components/badge/Badge.h>
#include <font/FontRenderer.h>
#include <graphics/PrimitiveStyles.h>
#include <primitives/Primitives.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>

#include <cctype>
#include <cstdio>
#include <string>

namespace world_sim {

namespace {
	// drawText scale is relative to a 16px base.
	float textScale(float px) { return px / 16.0F; }

	// Amber ghost styling for the alert bell: transparent surface, warn-tinted
	// icon, hover wash only.
	UI::ButtonAppearance alertBellAppearance() {
		UI::ButtonAppearance appearance;
		auto style = [](Foundation::Color fill) {
			UI::ButtonStyle s;
			s.background.fill = fill;
			s.background.border = Foundation::BorderStyle{
				.color = Foundation::Color::transparent(), .width = 0.0F, .cornerRadius = UI::r_sm, .position = Foundation::BorderPosition::Inside};
			s.textColor = UI::status_warn;
			return s;
		};
		appearance.normal = style(Foundation::Color::transparent());
		appearance.hover = style(UI::bg_hover);
		appearance.pressed = style(UI::bg_active);
		appearance.focused = style(Foundation::Color::transparent());
		appearance.disabled = style(Foundation::Color::transparent());
		return appearance;
	}
}  // namespace

TopBar::TopBar(const Args& args)
	: onPause(args.onPause)
	, onSpeedChange(args.onSpeedChange)
	, onMenuClick(args.onMenuClick)
	, colonyName(args.colonyName) {

	pauseButtonHandle = addChild(SpeedButton(SpeedButton::Args{
		.iconPath = "ui/icons/pause.svg",
		.onClick = [this]() { if (onPause) { onPause(); } },
		.id = args.id + "_pause"}));

	speed1ButtonHandle = addChild(SpeedButton(SpeedButton::Args{
		.iconPath = "ui/icons/play.svg",
		.onClick = [this]() { if (onSpeedChange) { onSpeedChange(ecs::GameSpeed::Normal); } },
		.id = args.id + "_speed1"}));

	speed2ButtonHandle = addChild(SpeedButton(SpeedButton::Args{
		.iconPath = "ui/icons/fast_forward.svg",
		.onClick = [this]() { if (onSpeedChange) { onSpeedChange(ecs::GameSpeed::Fast); } },
		.id = args.id + "_speed2"}));

	speed3ButtonHandle = addChild(SpeedButton(SpeedButton::Args{
		.iconPath = "ui/icons/very_fast.svg",
		.onClick = [this]() { if (onSpeedChange) { onSpeedChange(ecs::GameSpeed::VeryFast); } },
		.id = args.id + "_speed3"}));

	// Alert bell: shows the critical-toast count badge. No alert-history view
	// exists yet, so the click is intentionally dead.
	alertButtonHandle = addChild(UI::Button(UI::Button::Args{
		.label = "",  // Icon-only
		.size = {kBellSize, kBellSize},
		.type = UI::Button::Type::Custom,
		.id = "top_bar_alerts",
		.iconPath = "assets/ui/icons/alert.svg",
		.iconSize = 16.0F}));
	if (auto* bell = getChild<UI::Button>(alertButtonHandle)) {
		bell->appearance = alertBellAppearance();
	}

	// Menu button: Salvage secondary variant.
	menuButtonHandle = addChild(UI::Button(UI::Button::Args{
		.label = "Menu",
		.size = {kMenuWidth, kMenuHeight},
		.type = UI::Button::Type::Secondary,
		.onClick = [this]() { if (onMenuClick) { onMenuClick(); } },
		.id = "top_bar_menu"}));

	updateSpeedButtonStates(ecs::GameSpeed::Normal);
}

void TopBar::layout(const Foundation::Rect& newBounds) {
	Component::layout(newBounds);
	// Reported bounds are the bar strip itself (lint reads these)
	position = {newBounds.x, newBounds.y};
	size = {newBounds.width, kBarHeight};
	positionElements();
}

void TopBar::updateData(const TimeModel& timeModel, int survivorCount) {
	const auto& d = timeModel.data();

	dayStr = "Day " + std::to_string(d.day);

	seasonStr = d.season;
	for (char& c : seasonStr) {
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	}

	char buf[8];
	std::snprintf(buf, sizeof(buf), "%02d:%02d", d.hour, d.minute);
	timeStr = buf;

	// Split the sub-line so the middot separator can be drawn as a vector dot
	// (symbols are iconography, not font glyphs).
	survivorPart = std::to_string(survivorCount) + (survivorCount == 1 ? " survivor" : " survivors");
	solPart = "Sol " + std::to_string(d.day);

	updateSpeedButtonStates(d.speed);
	positionElements();
}

bool TopBar::handleEvent(UI::InputEvent& event) {
	return dispatchEvent(event);
}

void TopBar::updateSpeedButtonStates(ecs::GameSpeed currentSpeed) {
	if (auto* btn = getChild<SpeedButton>(pauseButtonHandle)) {
		btn->setActive(currentSpeed == ecs::GameSpeed::Paused);
	}
	if (auto* btn = getChild<SpeedButton>(speed1ButtonHandle)) {
		btn->setActive(currentSpeed == ecs::GameSpeed::Normal);
	}
	if (auto* btn = getChild<SpeedButton>(speed2ButtonHandle)) {
		btn->setActive(currentSpeed == ecs::GameSpeed::Fast);
	}
	if (auto* btn = getChild<SpeedButton>(speed3ButtonHandle)) {
		btn->setActive(currentSpeed == ecs::GameSpeed::VeryFast);
	}
}

void TopBar::positionElements() {
	rowY = bounds.y + kBarHeight * 0.5F;

	// Identity text sits right of the colony mark.
	identityX = bounds.x + kPadH + kMarkHalf * 2.0F + UI::space_3;

	const ui::FontRenderer* fr = Renderer::Primitives::getFontRenderer();
	auto measure = [&](const std::string& s, float px, Renderer::FontFamily fam, float ls = 0.0F) -> float {
		if (fr != nullptr) {
			return fr->MeasureText(s, textScale(px), fam, ls).x;
		}
		return static_cast<float>(s.size()) * px * 0.55F;	// fallback before the font is ready
	};

	const float w1 = measure(dayStr, UI::fs_md, UI::fontDisplay);
	const float w2 = measure(seasonStr, UI::fs_xs, UI::fontMono, UI::fs_xs * UI::ls_wide);
	const float w3 = measure(timeStr, UI::fs_md, UI::fontMono);

	// Sub-line layout: "<survivors> . <Sol D>", the dot drawn as a vector middot.
	const float subW = measure(survivorPart, UI::fs_2xs, UI::fontMono);
	subDotX = identityX + subW + UI::space_1_5;
	subSolX = subDotX + UI::space_1_5;

	float btnW = 0.0F;
	float btnH = 0.0F;
	if (auto* b = getChild<SpeedButton>(pauseButtonHandle)) {
		btnW = b->getWidth();
		btnH = b->getHeight();
	}
	const float buttonsW = btnW * 4.0F + kSpeedSpacing * 3.0F;
	const float pillW = buttonsW + kPillPadding * 2.0F;
	const float pillH = btnH + kPillPadding * 2.0F;

	const float gap = UI::space_2;
	const float gapPill = UI::space_3;
	const float total = w1 + gap + w2 + gap + w3 + gapPill + pillW;
	float x = bounds.x + bounds.width * 0.5F - total * 0.5F;

	dayX = x;
	x += w1 + gap;
	seasonX = x;
	x += w2 + gap;
	timeX = x;
	x += w3 + gapPill;

	pillRect = {x, rowY - pillH * 0.5F, pillW, pillH};

	// Place the speed buttons inside the pill.
	float bx = x + kPillPadding;
	const float by = rowY - btnH * 0.5F;
	auto place = [&](UI::LayerHandle handle) {
		if (auto* b = getChild<SpeedButton>(handle)) {
			b->setPosition(bx, by);
			bx += btnW + kSpeedSpacing;
		}
	};
	place(pauseButtonHandle);
	place(speed1ButtonHandle);
	place(speed2ButtonHandle);
	place(speed3ButtonHandle);

	// Menu button, right aligned; the alert bell sits just left of it.
	const float menuX = bounds.x + bounds.width - kMenuWidth - kPadH;
	if (auto* b = getChild<UI::Button>(menuButtonHandle)) {
		b->setPosition(menuX, rowY - kMenuHeight * 0.5F);
	}
	alertRect = {menuX - UI::space_3 - kBellSize, rowY - kBellSize * 0.5F, kBellSize, kBellSize};
	if (auto* b = getChild<UI::Button>(alertButtonHandle)) {
		b->setPosition(alertRect.x, alertRect.y);
	}
}

void TopBar::setAlertCount(size_t count) {
	alertCount = count;
}

void TopBar::render() {
	using Renderer::Primitives::drawCircle;
	using Renderer::Primitives::drawRect;
	using Renderer::Primitives::drawText;
	using Renderer::Primitives::drawTriangles;

	constexpr float kSubY = 30.0F;	  // sub-line text top, relative to bar top

	// Bar background (subtle vertical fade, per the prototype's top bar) +
	// bottom hairline.
	drawRect(Renderer::Primitives::RectArgs{
		.bounds = {bounds.x, bounds.y, bounds.width, kBarHeight},
		.style = {.gradient = Foundation::LinearGradient{
					  .from = UI::bg_base, .to = UI::withAlpha(UI::bg_base, 0.86F), .horizontal = false}}});
	drawRect(Renderer::Primitives::RectArgs{
		.bounds = {bounds.x, bounds.y + kBarHeight - 1.0F, bounds.width, 1.0F},
		.style = {.fill = UI::line_hairline}});

	// Colony mark: nested diamonds (the prototype's ◈ glyph, drawn as vectors).
	{
		const Foundation::Vec2 c{bounds.x + kPadH + kMarkHalf, rowY};
		auto diamond = [&](float half, Foundation::Color color) {
			const Foundation::Vec2 verts[4] = {
				{c.x, c.y - half}, {c.x + half, c.y}, {c.x, c.y + half}, {c.x - half, c.y}};
			const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
			drawTriangles(Renderer::Primitives::TrianglesArgs{
				.vertices = verts,
				.indices = indices,
				.vertexCount = 4,
				.indexCount = 6,
				.color = color,
				.id = "colony_mark"});
		};
		diamond(kMarkHalf, UI::accent);
		diamond(kMarkHalf - 3.0F, UI::bg_base);
		diamond(kMarkHalf - 5.5F, UI::accent);
	}

	// Left: colony identity.
	drawText(Renderer::Primitives::TextArgs{
		.text = colonyName,
		.position = {identityX, bounds.y + 9.0F},
		.scale = textScale(UI::fs_md),
		.color = UI::text_bright,
		.font = UI::fontDisplay,
		.hAlign = Foundation::HorizontalAlign::Left,
		.vAlign = Foundation::VerticalAlign::Top});
	drawText(Renderer::Primitives::TextArgs{
		.text = survivorPart,
		.position = {identityX, bounds.y + kSubY},
		.scale = textScale(UI::fs_2xs),
		.color = UI::text_faint,
		.font = UI::fontMono,
		.hAlign = Foundation::HorizontalAlign::Left,
		.vAlign = Foundation::VerticalAlign::Top});
	drawCircle(Renderer::Primitives::CircleArgs{
		.center = {subDotX, bounds.y + kSubY + 5.0F},
		.radius = 1.4F,
		.style = {.fill = UI::text_faint}});
	drawText(Renderer::Primitives::TextArgs{
		.text = solPart,
		.position = {subSolX, bounds.y + kSubY},
		.scale = textScale(UI::fs_2xs),
		.color = UI::text_faint,
		.font = UI::fontMono,
		.hAlign = Foundation::HorizontalAlign::Left,
		.vAlign = Foundation::VerticalAlign::Top});

	// Center: clock cluster (day / season / time).
	drawText(Renderer::Primitives::TextArgs{
		.text = dayStr,
		.position = {dayX, bounds.y},
		.scale = textScale(UI::fs_md),
		.color = UI::text_bright,
		.font = UI::fontDisplay,
		.hAlign = Foundation::HorizontalAlign::Left,
		.vAlign = Foundation::VerticalAlign::Middle,
		.boxHeight = kBarHeight});
	drawText(Renderer::Primitives::TextArgs{
		.text = seasonStr,
		.position = {seasonX, bounds.y},
		.scale = textScale(UI::fs_xs),
		.color = UI::data,
		.font = UI::fontMono,
		.hAlign = Foundation::HorizontalAlign::Left,
		.vAlign = Foundation::VerticalAlign::Middle,
		.boxHeight = kBarHeight,
		.letterSpacing = UI::fs_xs * UI::ls_wide});
	drawText(Renderer::Primitives::TextArgs{
		.text = timeStr,
		.position = {timeX, bounds.y},
		.scale = textScale(UI::fs_md),
		.color = UI::data_bright,
		.font = UI::fontMono,
		.hAlign = Foundation::HorizontalAlign::Left,
		.vAlign = Foundation::VerticalAlign::Middle,
		.boxHeight = kBarHeight});

	// Speed pill background behind the speed buttons.
	drawRect(Renderer::Primitives::RectArgs{
		.bounds = pillRect,
		.style = {.fill = UI::bg_inset,
				  .border = Foundation::BorderStyle{
					  .color = UI::line_edge, .width = UI::bw, .cornerRadius = UI::r_md, .position = Foundation::BorderPosition::Inside}}});

	// Child controls (speed buttons + bell + menu) paint on top.
	UI::Component::render();

	// Critical-count badge over the bell's top-right corner; hidden at zero.
	if (alertCount > 0) {
		const std::string label = std::to_string(alertCount);
		const float badgeW = UI::Badge::MeasureWidth(label);
		UI::Badge(UI::Badge::Args{
					  .position = {alertRect.x + alertRect.width - badgeW + UI::space_1, bounds.y + UI::space_1},
					  .label = label,
					  .tone = UI::Tone::Crit})
			.render();
	}
}

}  // namespace world_sim
