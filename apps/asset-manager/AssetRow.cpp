#include "AssetRow.h"

#include <graphics/Color.h>

namespace asset_manager {

	AssetRow::AssetRow(const Args& args) : m_defName(args.defName), m_onSelect(args.onSelect), m_width(args.width) {
		m_thumb.setAsset(m_defName);
		m_thumb.setSize(kThumbSize, kThumbSize);
		m_label = UI::Text(UI::Text::Args{
			.text = m_defName,
			.style =
				{
					.color = Foundation::Color(0.85F, 0.87F, 0.92F, 1.0F),
					.fontSize = 14.0F,
					.vAlign = Foundation::VerticalAlign::Middle,
				},
			.id = "am_row_label",
		});
	}

	void AssetRow::setPosition(float x, float y) {
		m_pos = {x, y};
		m_thumb.setPosition(x + kIndent, y + ((kRowHeight - kThumbSize) * 0.5F));
		m_label.position = {x + kIndent + kThumbSize + 8.0F, y + (kRowHeight * 0.5F)};
	}

	void AssetRow::render() {
		if (!visible) {
			return;
		}
		if (m_selected) {
			UI::Rectangle highlight(UI::Rectangle::Args{
				.position = m_pos,
				.size = {m_width, kRowHeight},
				.style = {.fill = Foundation::Color(0.20F, 0.32F, 0.55F, 0.55F)},
				.id = "am_row_sel",
			});
			highlight.render();
		}
		m_thumb.render();
		m_label.render();
	}

	bool AssetRow::handleEvent(UI::InputEvent& event) {
		if (event.type == UI::InputEvent::Type::MouseDown && containsPoint(event.position)) {
			if (m_onSelect) {
				m_onSelect(m_defName);
			}
			event.consume();
			return true;
		}
		return false;
	}

	bool AssetRow::containsPoint(Foundation::Vec2 point) const {
		return point.x >= m_pos.x && point.x <= m_pos.x + m_width && point.y >= m_pos.y && point.y <= m_pos.y + kRowHeight;
	}

	GroupHeaderRow::GroupHeaderRow(const Args& args) : m_onToggle(args.onToggle), m_width(args.width) {
		m_label = UI::Text(UI::Text::Args{
			.text = (args.expanded ? "v  " : ">  ") + args.label,
			.style =
				{
					.color = Foundation::Color(0.55F, 0.58F, 0.66F, 1.0F),
					.fontSize = 12.0F,
					.vAlign = Foundation::VerticalAlign::Middle,
				},
			.id = "am_group",
		});
	}

	void GroupHeaderRow::setPosition(float x, float y) {
		m_pos = {x, y};
		m_label.position = {x + 8.0F, y + (kRowHeight * 0.5F)};
	}

	void GroupHeaderRow::render() {
		if (!visible) {
			return;
		}
		m_label.render();
	}

	bool GroupHeaderRow::handleEvent(UI::InputEvent& event) {
		if (event.type == UI::InputEvent::Type::MouseDown && containsPoint(event.position)) {
			if (m_onToggle) {
				m_onToggle();
			}
			event.consume();
			return true;
		}
		return false;
	}

	bool GroupHeaderRow::containsPoint(Foundation::Vec2 point) const {
		return point.x >= m_pos.x && point.x <= m_pos.x + m_width && point.y >= m_pos.y && point.y <= m_pos.y + kRowHeight;
	}

} // namespace asset_manager
