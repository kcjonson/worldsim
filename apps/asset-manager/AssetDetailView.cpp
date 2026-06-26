#include "AssetDetailView.h"

#include "Theme.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <graphics/PrimitiveStyles.h>
#include <primitives/Primitives.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

#include <glm/vec2.hpp>

namespace asset_manager {

	namespace {
		std::string readFile(const std::filesystem::path& path) {
			std::ifstream file(path, std::ios::binary);
			if (!file) {
				return "(could not read " + path.string() + ")";
			}
			std::ostringstream ss;
			ss << file.rdbuf();
			std::string content = ss.str();
			content.erase(std::remove(content.begin(), content.end(), '\r'), content.end());
			return content;
		}

		std::string fmtFloat(float v) {
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
			return buf;
		}

		std::string toUpper(std::string s) {
			for (auto& c : s) {
				c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			}
			return s;
		}

		void panel(const Foundation::Rect& r, const Foundation::Color& fill, const Foundation::Color& border) {
			UI::Rectangle rect(UI::Rectangle::Args{
				.position = {r.x, r.y},
				.size = {r.width, r.height},
				.style = {.fill = fill, .border = Foundation::BorderStyle{.color = border, .width = theme::borderWidth, .cornerRadius = theme::radius}},
				.id = "am_panel",
			});
			rect.render();
		}

		void fillRect(const Foundation::Rect& r, const Foundation::Color& color) {
			UI::Rectangle rect(UI::Rectangle::Args{.position = {r.x, r.y}, .size = {r.width, r.height}, .style = {.fill = color}, .id = "am_fill"});
			rect.render();
		}
	} // namespace

	AssetDetailView::AssetDetailView() {
		m_placeholder = UI::Text(UI::Text::Args{
			.text = "Select an asset from the tree",
			.style = {.color = theme::textFaint, .fontSize = theme::fsMd},
			.id = "am_placeholder",
		});
		m_kicker = UI::Text(UI::Text::Args{.text = "", .style = {.color = theme::accent, .fontSize = theme::fsLabel}, .id = "am_kicker"});
		m_name = UI::Text(UI::Text::Args{.text = "", .style = {.color = theme::textBright, .fontSize = theme::fsTitle}, .id = "am_name"});
		m_badge = UI::Text(UI::Text::Args{
			.text = "",
			.style = {.color = theme::accentBright, .fontSize = theme::fsLabel, .vAlign = Foundation::VerticalAlign::Middle},
			.id = "am_badge",
		});
		m_meta = UI::Text(UI::Text::Args{.text = "", .style = {.color = theme::textDim, .fontSize = theme::fsBody, .wordWrap = true}, .id = "am_meta"});
		m_warnings = UI::Text(UI::Text::Args{.text = "", .style = {.color = theme::statusWarn, .fontSize = theme::fsBody, .wordWrap = true}, .id = "am_warn"});
		m_xmlHeader = UI::Text(UI::Text::Args{.text = "", .style = {.color = theme::textDim, .fontSize = theme::fsLabel}, .id = "am_xmlhdr"});
		m_xmlScroll = std::make_unique<UI::ScrollContainer>(UI::ScrollContainer::Args{.position = {0.0F, 0.0F}, .size = {400.0F, 200.0F}, .id = "am_xml_scroll"});
	}

	void AssetDetailView::setBounds(const Foundation::Rect& bounds) {
		m_bounds = bounds;
		relayout();
	}

	void AssetDetailView::setAsset(const std::string& defName) {
		if (defName.empty()) {
			m_hasAsset = false;
			m_def = nullptr;
			return;
		}
		const engine::assets::AssetDefinition* def = engine::assets::AssetRegistry::Get().getDefinition(defName);
		if (def == nullptr) {
			m_hasAsset = false;
			m_def = nullptr;
			return;
		}
		m_hasAsset = true;
		m_def = def;

		m_preview.setAsset(defName);
		m_preview.setSize(kPreview - (2.0F * theme::s2), kPreview - (2.0F * theme::s2));

		const std::string category = def->baseFolder.empty() ? "asset" : def->baseFolder.parent_path().filename().string();
		m_kicker.text = toUpper(category);
		m_name.text = defName;

		const bool simple = def->assetType == engine::assets::AssetType::Simple;
		m_badge.text = simple ? "SIMPLE" : "PROCEDURAL";
		m_badge.style.color = simple ? theme::dataBright : theme::accentBright;
		m_badgeFill = simple ? theme::dataFill : theme::accentFill;
		m_badgeBorder = simple ? theme::dataBorder : theme::accentBorder;

		std::string meta;
		if (!def->label.empty()) {
			meta += def->label + "\n";
		}
		meta += "worldHeight  " + fmtFloat(def->worldHeight);
		if (!def->svgPath.empty()) {
			meta += "\nsvg  " + def->svgPath;
		}
		if (!def->scriptPath.empty()) {
			meta += "\nscript  " + def->scriptPath;
		}
		if (!def->placement.groups.empty()) {
			std::string groups;
			for (size_t i = 0; i < def->placement.groups.size(); ++i) {
				groups += (i == 0 ? "" : ", ") + def->placement.groups[i];
			}
			meta += "\ngroups  " + groups;
		}
		m_meta.text = meta;

		std::string warnings;
		const engine::assets::ValidationReport& report = engine::assets::AssetRegistry::Get().getValidationReport();
		int										shown = 0;
		bool									anyError = false;
		for (const auto& issue : report.issues) {
			if (issue.defName != defName) {
				continue;
			}
			if (shown++ > 0) {
				warnings += "\n";
			}
			const bool isError = issue.severity == engine::assets::Severity::Error;
			anyError = anyError || isError;
			warnings += (isError ? "[error] " : "[warning] ");
			if (!issue.field.empty()) {
				warnings += issue.field + ": ";
			}
			warnings += issue.message;
		}
		m_warnings.text = warnings;
		// Color by the highest severity shown for THIS asset, not the registry-wide error count.
		m_warnings.style.color = anyError ? theme::statusCrit : theme::statusWarn;

		const std::filesystem::path xmlPath = def->baseFolder / (def->baseFolder.filename().string() + ".xml");
		m_xmlHeader.text = toUpper("Definition  -  " + xmlPath.filename().string());

		const float xmlWidth = std::max(80.0F, m_bounds.width - (2.0F * kPad) - (2.0F * theme::s3) - 16.0F);
		m_xmlScroll->clearChildren();
		m_xmlScroll->addChild(UI::Text(UI::Text::Args{
			.position = {0.0F, 0.0F},
			.width = xmlWidth,
			.text = readFile(xmlPath),
			.style = {.color = theme::text, .fontSize = theme::fsSmall, .wordWrap = true},
			.id = "am_xml_text",
		}));

		relayout();
	}

	void AssetDetailView::relayout() {
		const float bx = m_bounds.x;
		const float by = m_bounds.y;

		m_placeholder.position = {bx + kPad, by + kPad};

		// Preview card (left) + measured right column.
		m_preview.setPosition(bx + kPad + theme::s2, by + kPad + theme::s2);

		const float colX = bx + kPad + kPreview + theme::s5;
		const float colW = std::max(120.0F, (bx + m_bounds.width - kPad) - colX);
		float		y = by + kPad;

		m_kicker.position = {colX, y};
		y += theme::fsLabel + theme::s1;
		m_name.position = {colX, y};
		y += theme::fsTitle + theme::s2;

		m_badgeRect = {colX, y, std::max(60.0F, m_badge.getWidth() + (2.0F * theme::s2)), 20.0F};
		m_badge.position = {colX + theme::s2, y + 10.0F};
		y += 20.0F + theme::s3;

		m_meta.width = colW;
		m_meta.position = {colX, y};
		y += m_meta.getHeight() + theme::s3;

		if (!m_warnings.text.empty()) {
			m_warnings.width = colW;
			m_warnings.position = {colX, y};
			y += m_warnings.getHeight();
		}

		// Divider below the taller of (preview, right column).
		const float previewBottom = by + kPad + kPreview;
		m_dividerY = std::max(previewBottom, y) + theme::s4;

		const float xmlHeaderY = m_dividerY + theme::s3;
		m_xmlHeader.position = {bx + kPad, xmlHeaderY};

		const float wellY = xmlHeaderY + theme::fsLabel + theme::s2;
		const float wellH = std::max(0.0F, (by + m_bounds.height - kPad) - wellY);
		m_xmlScroll->setPosition(bx + kPad + theme::s3, wellY + theme::s3);
		m_xmlScroll->setViewportSize({m_bounds.width - (2.0F * kPad) - (2.0F * theme::s3), std::max(0.0F, wellH - (2.0F * theme::s3))});
	}

	void AssetDetailView::render() {
		if (!m_hasAsset) {
			m_placeholder.render();
			return;
		}

		// Preview card.
		const Foundation::Rect previewCard{m_bounds.x + kPad, m_bounds.y + kPad, kPreview, kPreview};
		panel(previewCard, theme::bgInset, theme::lineHairline);
		m_preview.render();
		drawCollisionOverlay(previewCard);

		m_kicker.render();
		m_name.render();

		// Type badge.
		panel(m_badgeRect, m_badgeFill, m_badgeBorder);
		m_badge.render();

		m_meta.render();
		if (!m_warnings.text.empty()) {
			m_warnings.render();
		}

		// Divider.
		fillRect({m_bounds.x + kPad, m_dividerY, m_bounds.width - (2.0F * kPad), 1.0F}, theme::lineHairline);

		m_xmlHeader.render();

		// Code well behind the scrolled XML.
		const float wellY = m_xmlHeader.position.y + theme::fsLabel + theme::s2;
		const float wellH = std::max(0.0F, (m_bounds.y + m_bounds.height - kPad) - wellY);
		panel({m_bounds.x + kPad, wellY, m_bounds.width - (2.0F * kPad), wellH}, theme::bgInset, theme::lineHairline);
		if (m_xmlScroll) {
			m_xmlScroll->render();
		}
	}

	void AssetDetailView::drawCollisionOverlay(const Foundation::Rect& previewCard) {
		if (m_def == nullptr) {
			return;
		}

		// No collider: make the absence explicit with a corner label.
		if (!m_def->collision.blocks()) {
			Renderer::Primitives::drawText({
				.text = "no collider",
				.position = {previewCard.x + theme::s2, previewCard.y + theme::s2},
				.scale = theme::fsSmall / 16.0F,
				.color = theme::textFaint,
				.id = "am_no_collider",
			});
			return;
		}

		if (!m_preview.hasMesh()) {
			return;
		}

		// Collision points live in the asset's local meter space, the same space as the
		// raw mesh the preview was built from. localToScreen replays the preview's exact
		// fitToRect math, so the outline lands on the art. ONE transform source.
		std::vector<glm::vec2> local;
		if (m_def->collision.type == engine::assets::CollisionShapeType::Rect) {
			const std::array<glm::vec2, 4> corners = m_def->collision.rectCornersLocal();
			local.assign(corners.begin(), corners.end());
		} else { // Polygon
			local = m_def->collision.pointsMeters;
		}
		if (local.size() < 2) {
			return;
		}

		std::vector<Foundation::Vec2> screen;
		screen.reserve(local.size());
		for (const glm::vec2& p : local) {
			screen.push_back(m_preview.localToScreen(p));
		}

		const Foundation::Color outline{0.0F, 1.0F, 1.0F, 1.0F}; // bright cyan, high contrast on art
		constexpr float			thickness = 2.0F;
		for (size_t i = 0; i < screen.size(); ++i) {
			const Foundation::Vec2& a = screen[i];
			const Foundation::Vec2& b = screen[(i + 1) % screen.size()];
			Renderer::Primitives::drawLine({
				.start = a,
				.end = b,
				.style = {.color = outline, .width = thickness},
				.id = "am_collision_edge",
			});
		}
	}

	bool AssetDetailView::handleInput(UI::InputEvent& event) {
		if (m_hasAsset && m_xmlScroll) {
			return m_xmlScroll->handleEvent(event);
		}
		return false;
	}

	void AssetDetailView::update(float dt) {
		if (m_xmlScroll) {
			m_xmlScroll->update(dt);
		}
	}

} // namespace asset_manager
