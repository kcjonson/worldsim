#include "ColonistListItem.h"

#include <components/avatar/Avatar.h>
#include <graphics/PrimitiveStyles.h>
#include <primitives/Primitives.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>

#include <algorithm>
#include <string>

namespace world_sim {

namespace {
	constexpr float kAvatar = 30.0F;
	constexpr float kPad = 8.0F;
	float textScale(float px) { return px / 16.0F; }
}  // namespace

ColonistListItem::ColonistListItem(const Args& args)
	: entityId(args.colonist.id)
	, name(args.colonist.name)
	, firstName(firstNameOf(args.colonist.name))
	, mood(args.colonist.mood)
	, selected(args.isSelected)
	, onSelect(args.onSelect) {
	size = {args.width, args.height};
	margin = args.itemMargin;
}

std::string ColonistListItem::firstNameOf(const std::string& full) {
	const size_t space = full.find(' ');
	return space == std::string::npos ? full : full.substr(0, space);
}

void ColonistListItem::setColonistData(const adapters::ColonistData& data) {
	entityId = data.id;
	name = data.name;
	firstName = firstNameOf(data.name);
	mood = data.mood;
}

void ColonistListItem::render() {
	using Renderer::Primitives::drawRect;
	using Renderer::Primitives::drawText;

	const Foundation::Vec2 p = getContentPosition();
	const float w = size.x;
	const float h = size.y;
	const float moodRatio = std::clamp(mood / 100.0F, 0.0F, 1.0F);
	const Foundation::Color moodCol = UI::toneColor(UI::Tone::Auto, moodRatio);

	// Card surface (raised + amber left edge when active).
	drawRect(Renderer::Primitives::RectArgs{
		.bounds = {p.x, p.y, w, h},
		.style = {.fill = selected ? UI::bg_panel_raised : UI::bg_panel,
				  .border = Foundation::BorderStyle{
					  .color = UI::line_hairline, .width = UI::bw, .cornerRadius = UI::r_md, .position = Foundation::BorderPosition::Inside}}});
	if (selected) {
		drawRect(Renderer::Primitives::RectArgs{
			.bounds = {p.x, p.y + 2.0F, 3.0F, h - 4.0F},
			.style = {.fill = UI::accent}});
	}

	// Avatar (mood-tinted, selected ring).
	UI::Avatar(UI::Avatar::Args{
				   .position = {p.x + kPad, p.y + (h - kAvatar) * 0.5F},
				   .size = kAvatar,
				   .seed = name,
				   .mood = moodRatio,
				   .hasMood = true,
				   .selected = selected})
		.render();

	// Info column.
	const float infoX = p.x + kPad + kAvatar + UI::space_2;
	const float infoW = (p.x + w) - infoX - kPad;

	drawText(Renderer::Primitives::TextArgs{
		.text = firstName,
		.position = {infoX, p.y + kPad},
		.scale = textScale(UI::fs_sm),
		.color = UI::text_bright,
		.font = UI::fontDisplay,
		.hAlign = Foundation::HorizontalAlign::Left,
		.vAlign = Foundation::VerticalAlign::Top});
	drawText(Renderer::Primitives::TextArgs{
		.text = std::to_string(static_cast<int>(moodRatio * 100.0F)) + "%",
		.position = {infoX, p.y + kPad},
		.scale = textScale(UI::fs_2xs),
		.color = moodCol,
		.font = UI::fontMono,
		.hAlign = Foundation::HorizontalAlign::Right,
		.vAlign = Foundation::VerticalAlign::Top,
		.boxWidth = infoW});

	// Mood meter (track + tone-colored fill).
	constexpr float kBarH = 4.0F;
	const float barY = p.y + h - kPad - kBarH;
	drawRect(Renderer::Primitives::RectArgs{
		.bounds = {infoX, barY, infoW, kBarH},
		.style = {.fill = UI::bg_inset,
				  .border = Foundation::BorderStyle{
					  .color = UI::line_hairline, .width = UI::bw, .cornerRadius = UI::r_sm, .position = Foundation::BorderPosition::Inside}}});
	if (moodRatio > 0.0F) {
		drawRect(Renderer::Primitives::RectArgs{
			.bounds = {infoX, barY, infoW * moodRatio, kBarH},
			.style = {.fill = moodCol,
					  .border = Foundation::BorderStyle{
						  .color = moodCol, .width = 0.0F, .cornerRadius = UI::r_sm, .position = Foundation::BorderPosition::Inside}}});
	}
}

bool ColonistListItem::handleEvent(UI::InputEvent& event) {
	if (event.type != UI::InputEvent::Type::MouseUp) {
		return false;
	}
	if (event.button != engine::MouseButton::Left) {
		return false;
	}
	if (!containsPoint(event.position)) {
		return false;
	}
	if (onSelect) {
		onSelect(entityId);
	}
	event.consume();
	return true;
}

bool ColonistListItem::containsPoint(Foundation::Vec2 point) const {
	const Foundation::Vec2 p = getContentPosition();
	return point.x >= p.x && point.x <= p.x + size.x && point.y >= p.y && point.y <= p.y + size.y;
}

}  // namespace world_sim
