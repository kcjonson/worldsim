#include "AssetRow.h"

#include "Theme.h"

#include <graphics/Rect.h>
#include <primitives/Primitives.h>

#include <cctype>
#include <cstdint>
#include <string>

namespace asset_manager {

	namespace {
		std::string toUpper(std::string s) {
			for (auto& c : s) {
				c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			}
			return s;
		}

		void fillRect(const Foundation::Rect& r, const Foundation::Color& color) {
			UI::Rectangle rect(UI::Rectangle::Args{.position = {r.x, r.y}, .size = {r.width, r.height}, .style = {.fill = color}, .id = "am_fill"});
			rect.render();
		}

		// Small chevron triangle (down when expanded, right when collapsed).
		void chevron(float x, float y, bool expanded, const Foundation::Color& color) {
			constexpr float s = 8.0F;
			Foundation::Vec2 verts[3];
			if (expanded) {
				verts[0] = {x, y};
				verts[1] = {x + s, y};
				verts[2] = {x + (s * 0.5F), y + (s * 0.6F)};
			} else {
				verts[0] = {x, y - (s * 0.5F)};
				verts[1] = {x, y + (s * 0.5F)};
				verts[2] = {x + (s * 0.6F), y};
			}
			uint16_t idx[3] = {0, 1, 2};
			Renderer::Primitives::drawTriangles({.vertices = verts, .indices = idx, .vertexCount = 3, .indexCount = 3, .color = color});
		}
	} // namespace

	AssetRow::AssetRow(const Args& args) : m_defName(args.defName), m_onSelect(args.onSelect), m_width(args.width) {
		m_thumb.setAsset(m_defName);
		m_thumb.setSize(kThumbSize, kThumbSize);
		m_label = UI::Text(UI::Text::Args{
			.text = m_defName,
			.style = {.color = theme::text, .fontSize = theme::fsBody, .vAlign = Foundation::VerticalAlign::Middle},
			.id = "am_row_label",
		});
	}

	void AssetRow::setPosition(float x, float y) {
		m_pos = {x, y};
		m_thumb.setPosition(x + kIndent, y + ((kRowHeight - kThumbSize) * 0.5F));
		m_label.position = {x + kIndent + kThumbSize + theme::s2, y + (kRowHeight * 0.5F)};
	}

	void AssetRow::render() {
		if (!visible) {
			return;
		}
		if (m_selected) {
			fillRect({m_pos.x, m_pos.y, m_width, kRowHeight}, theme::accentFill);
			fillRect({m_pos.x, m_pos.y, 3.0F, kRowHeight}, theme::accent);
		} else if (m_hovered) {
			fillRect({m_pos.x, m_pos.y, m_width, kRowHeight}, theme::rowHover);
		}
		m_label.style.color = m_selected ? theme::textBright : theme::text;
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
		if (event.type == UI::InputEvent::Type::MouseMove) {
			m_hovered = containsPoint(event.position);
		}
		return false;
	}

	bool AssetRow::containsPoint(Foundation::Vec2 point) const {
		return point.x >= m_pos.x && point.x <= m_pos.x + m_width && point.y >= m_pos.y && point.y <= m_pos.y + kRowHeight;
	}

	GroupHeaderRow::GroupHeaderRow(const Args& args) : m_onToggle(args.onToggle), m_width(args.width), m_expanded(args.expanded) {
		m_label = UI::Text(UI::Text::Args{
			.text = toUpper(args.label),
			.style = {.color = theme::textDim, .fontSize = theme::fsLabel, .vAlign = Foundation::VerticalAlign::Middle},
			.id = "am_group",
		});
	}

	void GroupHeaderRow::setPosition(float x, float y) {
		m_pos = {x, y};
		m_label.position = {x + 22.0F, y + (kRowHeight * 0.5F)};
	}

	void GroupHeaderRow::render() {
		if (!visible) {
			return;
		}
		chevron(m_pos.x + 8.0F, m_pos.y + (kRowHeight * 0.5F), m_expanded, theme::textFaint);
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
