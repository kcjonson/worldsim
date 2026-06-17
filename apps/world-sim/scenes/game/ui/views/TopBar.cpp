#include "TopBar.h"

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

	// The Salvage sub-lines use a middot separator, but the MSDF atlas is ASCII-only
	// for now; use a hyphen until the charset gains the punctuation glyphs.
	survivorStr = std::to_string(survivorCount) + (survivorCount == 1 ? " survivor" : " survivors") +
				  " - Sol " + std::to_string(d.day);

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

	// Menu button, right aligned.
	if (auto* b = getChild<UI::Button>(menuButtonHandle)) {
		b->setPosition(bounds.x + bounds.width - kMenuWidth - kPadH, rowY - kMenuHeight * 0.5F);
	}
}

void TopBar::render() {
	using Renderer::Primitives::drawRect;
	using Renderer::Primitives::drawText;

	// Bar background + bottom hairline.
	drawRect(Renderer::Primitives::RectArgs{
		.bounds = {bounds.x, bounds.y, bounds.width, kBarHeight},
		.style = {.fill = UI::bg_panel}});
	drawRect(Renderer::Primitives::RectArgs{
		.bounds = {bounds.x, bounds.y + kBarHeight - 1.0F, bounds.width, 1.0F},
		.style = {.fill = UI::line_hairline}});

	// Left: colony identity.
	drawText(Renderer::Primitives::TextArgs{
		.text = colonyName,
		.position = {bounds.x + kPadH, bounds.y + 9.0F},
		.scale = textScale(UI::fs_md),
		.color = UI::text_bright,
		.font = UI::fontDisplay,
		.hAlign = Foundation::HorizontalAlign::Left,
		.vAlign = Foundation::VerticalAlign::Top});
	drawText(Renderer::Primitives::TextArgs{
		.text = survivorStr,
		.position = {bounds.x + kPadH, bounds.y + 30.0F},
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

	// Child controls (speed buttons + menu) paint on top.
	UI::Component::render();
}

}  // namespace world_sim
