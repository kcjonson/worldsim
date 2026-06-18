#pragma once

// ListRow - a selectable, left-aligned row for scrollable lists.
//
// Fills the gap Button can't: Button always centers and uppercases its label,
// which is wrong for a list of names. ListRow draws a left-aligned label (and
// an optional right-aligned trailing string), washes its background on hover
// and when selected, and carries a bottom hairline + a left accent bar when
// selected. It handles its own hover/click, so it drops straight into a
// ScrollContainer or LayoutContainer with no manual hit-testing.

#include "component/Component.h"
#include "graphics/Color.h"
#include "primitives/Primitives.h"
#include "theme/Tokens.h"
#include "theme/Variants.h"

#include <input/InputTypes.h>

#include <functional>
#include <string>

namespace UI {

class ListRow : public Component {
  public:
	struct Args {
		std::string			  label;
		std::string			  trailing;					 // optional right-aligned secondary text (mono)
		Foundation::Vec2	  size{160.0F, 24.0F};
		bool				  selected = false;
		bool				  dim = false;				 // unavailable look (still clickable)
		std::function<void()> onClick;
		float				  margin = 0.0F;
		std::string			  id = "list_row";
	};

	explicit ListRow(const Args& args)
		: labelText(args.label),
		  trailingText(args.trailing),
		  selected(args.selected),
		  dim(args.dim),
		  onClick(args.onClick),
		  rowId(args.id) {
		position = {0.0F, 0.0F};
		size = args.size;
		margin = args.margin;
	}

	void			   setSelected(bool value) { selected = value; }
	[[nodiscard]] bool isSelected() const { return selected; }

	void render() override {
		if (!visible) {
			return;
		}
		using Renderer::Primitives::drawRect;
		using Renderer::Primitives::drawText;

		const Foundation::Vec2 pos = getContentPosition();
		const Foundation::Rect bounds{pos.x, pos.y, size.x, size.y};

		// State wash: selected wins over hover.
		if (selected) {
			drawRect({.bounds = bounds, .style = {.fill = bg_active}, .id = rowId.c_str()});
		} else if (hovered) {
			drawRect({.bounds = bounds, .style = {.fill = bg_hover}, .id = rowId.c_str()});
		}

		// Bottom hairline separator.
		drawRect({.bounds = {bounds.x, bounds.y + bounds.height - bw, bounds.width, bw}, .style = {.fill = line_hairline}});

		// Left accent bar marks the selection.
		if (selected) {
			drawRect({.bounds = {bounds.x, bounds.y, bw_thick, bounds.height}, .style = {.fill = accent}});
		}

		const Foundation::Color fg = dim ? text_dim : (selected ? text_bright : text);
		drawText({.text = labelText,
				  .position = {bounds.x + kPadX, bounds.y},
				  .scale = fs_sm / 16.0F,
				  .color = fg,
				  .font = fontUi,
				  .hAlign = Foundation::HorizontalAlign::Left,
				  .vAlign = Foundation::VerticalAlign::Middle,
				  .boxHeight = bounds.height,
				  .id = rowId.c_str()});

		if (!trailingText.empty()) {
			drawText({.text = trailingText,
					  .position = {bounds.x, bounds.y},
					  .scale = fs_xs / 16.0F,
					  .color = text_dim,
					  .font = fontMono,
					  .hAlign = Foundation::HorizontalAlign::Right,
					  .vAlign = Foundation::VerticalAlign::Middle,
					  .boxWidth = bounds.width - kPadX,
					  .boxHeight = bounds.height});
		}
	}

	bool handleEvent(InputEvent& event) override {
		if (!visible) {
			return false;
		}
		switch (event.type) {
			case InputEvent::Type::MouseMove:
				hovered = containsPoint(event.position);
				break;

			case InputEvent::Type::MouseDown:
				if (event.button == engine::MouseButton::Left && containsPoint(event.position)) {
					pressed = true;
					event.consume();
					return true;
				}
				break;

			case InputEvent::Type::MouseUp:
				if (pressed && event.button == engine::MouseButton::Left) {
					pressed = false;
					if (containsPoint(event.position) && onClick) {
						onClick();
					}
					event.consume();
					return true;
				}
				break;

			default:
				break;
		}
		return false;
	}

	bool containsPoint(Foundation::Vec2 point) const override {
		return point.x >= position.x && point.x <= position.x + getWidth() && point.y >= position.y && point.y <= position.y + getHeight();
	}

  private:
	static constexpr float kPadX = 8.0F;

	std::string			  labelText;
	std::string			  trailingText;
	bool				  selected{false};
	bool				  dim{false};
	std::function<void()> onClick;
	std::string			  rowId;

	bool hovered{false};
	bool pressed{false};
};

} // namespace UI
