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
		m_randomize = std::make_unique<UI::Button>(UI::Button::Args{
			.label = "Randomize",
			.position = {0.0F, 0.0F},
			.size = {110.0F, 28.0F},
			.type = UI::Button::Type::Secondary,
			.onClick = [this]() { randomize(); },
			.id = "am_randomize",
		});
	}

	void AssetDetailView::setBounds(const Foundation::Rect& bounds) {
		m_bounds = bounds;
		relayout();
	}

	void AssetDetailView::applyVariantSeeds() {
		if (m_defName.empty()) {
			return;
		}
		if (m_procedural) {
			for (int i = 0; i < kVariants; ++i) {
				m_variants[i].setAsset(m_defName, m_variantBase + static_cast<uint32_t>(i));
			}
		} else {
			m_variants[0].setAsset(m_defName, 42U);
		}
	}

	void AssetDetailView::randomize() {
		if (!m_procedural) {
			return;
		}
		m_variantBase += static_cast<uint32_t>(kVariants); // step to the next block of seeds
		applyVariantSeeds();
	}

	void AssetDetailView::setAsset(const std::string& defName) {
		if (defName.empty()) {
			m_hasAsset = false;
			return;
		}
		const engine::assets::AssetDefinition* def = engine::assets::AssetRegistry::Get().getDefinition(defName);
		if (def == nullptr) {
			m_hasAsset = false;
			return;
		}
		m_hasAsset = true;
		m_defName = defName;

		const bool simple = def->assetType == engine::assets::AssetType::Simple;
		m_procedural = !simple;
		m_cellCount = simple ? 1 : kVariants;
		m_variantBase = 1U;
		applyVariantSeeds();

		const std::string category = def->baseFolder.empty() ? "asset" : def->baseFolder.parent_path().filename().string();
		m_kicker.text = toUpper(category);
		m_name.text = defName;

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
		m_warnings.style.color = anyError ? theme::statusCrit : theme::statusWarn;

		const std::filesystem::path xmlPath = def->baseFolder / (def->baseFolder.filename().string() + ".xml");
		m_xmlHeader.text = toUpper("Definition  -  " + xmlPath.filename().string());
		m_xmlText = readFile(xmlPath);

		relayout();
	}

	void AssetDetailView::relayout() {
		const float bx = m_bounds.x;
		const float by = m_bounds.y;
		const float cx = bx + kPad;
		const float cw = std::max(160.0F, m_bounds.width - (2.0F * kPad));
		m_placeholder.position = {cx, by + kPad};

		// --- header band (full width) ---
		float y = by + kPad;
		m_kicker.position = {cx, y};
		y += theme::fsLabel + theme::s1;
		m_name.position = {cx, y};
		y += theme::fsTitle + theme::s2;
		m_badgeRect = {cx, y, std::max(60.0F, m_badge.getWidth() + (2.0F * theme::s2)), 20.0F};
		m_badge.position = {cx + theme::s2, y + 10.0F};
		y += 20.0F + theme::s4;

		const float colsY = y;
		const float colsBottom = by + m_bounds.height - kPad;

		// --- two columns: wide preview on the left, info + XML on the right ---
		const float gap = theme::s5;
		float		rightW = std::clamp(cw * 0.36F, 240.0F, 440.0F);
		rightW = std::min(rightW, cw - 220.0F); // keep the left column usable on narrow windows
		rightW = std::max(rightW, 160.0F);
		const float leftW = cw - rightW - gap;
		const float leftX = cx;
		const float rightX = cx + leftW + gap;

		// left: a Randomize button (procedural only) above the preview frame(s)
		float gridTop = colsY;
		if (m_procedural && m_randomize) {
			m_randomize->setPosition(leftX, colsY);
			gridTop = colsY + 28.0F + theme::s3;
		}
		const float gridH = std::max(40.0F, colsBottom - gridTop);

		auto placeCell = [&](int i, const Foundation::Rect& cell) {
			const float side = std::min(cell.width, cell.height);
			const float fx = cell.x + ((cell.width - side) * 0.5F);
			const float fy = cell.y + ((cell.height - side) * 0.5F);
			m_cellFrames[i] = {fx, fy, side, side};
			const float inner = std::max(8.0F, side - (2.0F * theme::s2));
			m_variants[i].setSize(inner, inner);
			m_variants[i].setPosition(fx + ((side - inner) * 0.5F), fy + ((side - inner) * 0.5F));
			m_variants[i].setAnimated(true); // detail preview sweeps motion; nav thumbnails don't
		};

		if (m_procedural) {
			const float cellGap = theme::s3;
			const float cellW = (leftW - cellGap) * 0.5F;
			const float cellH = (gridH - (2.0F * cellGap)) / 3.0F;
			for (int i = 0; i < kVariants; ++i) {
				const float col = static_cast<float>(i % 2);
				const float row = static_cast<float>(i / 2);
				placeCell(i, {leftX + (col * (cellW + cellGap)), gridTop + (row * (cellH + cellGap)), cellW, cellH});
			}
		} else {
			placeCell(0, {leftX, gridTop, leftW, gridH});
		}

		// right: meta, warnings, XML header, XML well
		float ry = colsY;
		m_meta.width = rightW;
		m_meta.position = {rightX, ry};
		ry += m_meta.getHeight() + theme::s3;
		if (!m_warnings.text.empty()) {
			m_warnings.width = rightW;
			m_warnings.position = {rightX, ry};
			ry += m_warnings.getHeight() + theme::s3;
		}
		m_xmlHeader.position = {rightX, ry};
		ry += theme::fsLabel + theme::s2;
		const float wellH = std::max(0.0F, colsBottom - ry);
		m_xmlWell = {rightX, ry, rightW, wellH};

		const float xmlTextW = std::max(40.0F, rightW - (2.0F * theme::s3) - 12.0F);
		m_xmlScroll->clearChildren();
		m_xmlScroll->addChild(UI::Text(UI::Text::Args{
			.position = {0.0F, 0.0F},
			.width = xmlTextW,
			.text = m_xmlText,
			.style = {.color = theme::text, .fontSize = theme::fsSmall, .wordWrap = true},
			.id = "am_xml_text",
		}));
		m_xmlScroll->setPosition(rightX + theme::s3, ry + theme::s3);
		m_xmlScroll->setViewportSize({rightW - (2.0F * theme::s3), std::max(0.0F, wellH - (2.0F * theme::s3))});
	}

	void AssetDetailView::render() {
		if (!m_hasAsset) {
			m_placeholder.render();
			return;
		}

		// Header.
		m_kicker.render();
		m_name.render();
		panel(m_badgeRect, m_badgeFill, m_badgeBorder);
		m_badge.render();

		// Left column: preview frame(s) + per-cell thumbnail.
		for (int i = 0; i < m_cellCount; ++i) {
			panel(m_cellFrames[i], theme::bgInset, theme::lineHairline);
			m_variants[i].render();
		}
		// Collider once, over the first cell, so the grid stays uncluttered.
		drawCollisionOverlay(m_variants[0], m_cellFrames[0]);
		if (m_procedural && m_randomize) {
			m_randomize->render();
		}

		// Right column: info + source XML.
		m_meta.render();
		if (!m_warnings.text.empty()) {
			m_warnings.render();
		}
		m_xmlHeader.render();
		panel(m_xmlWell, theme::bgInset, theme::lineHairline);
		if (m_xmlScroll) {
			m_xmlScroll->render();
		}
	}

	void AssetDetailView::drawCollisionOverlay(AssetThumbnail& thumb, const Foundation::Rect& card) {
		// Re-resolve from the name each frame instead of caching a raw AssetDefinition*: a reload
		// clears the registry, so a stored pointer would dangle until the next setAsset.
		const engine::assets::AssetDefinition* def = engine::assets::AssetRegistry::Get().getDefinition(m_defName);
		if (def == nullptr) {
			return;
		}

		// No collider: make the absence explicit with a corner label.
		if (!def->collision.blocks()) {
			Renderer::Primitives::drawText({
				.text = "no collider",
				.position = {card.x + theme::s2, card.y + theme::s2},
				.scale = theme::fsSmall / 16.0F,
				.color = theme::textFaint,
				.id = "am_no_collider",
			});
			return;
		}

		if (!thumb.hasMesh()) {
			return;
		}

		// Collision points are in the asset's local meter space; how they map to the
		// preview depends on the collider's frame (procedural = mesh coords, simple SVG =
		// bbox-centered), handled in the draw loop below. Both reuse the preview's own
		// fitToRect math so the outline lands on the art.
		std::vector<glm::vec2> local;
		if (def->collision.type == engine::assets::CollisionShapeType::Rect) {
			const std::array<glm::vec2, 4> corners = def->collision.rectCornersLocal();
			local.assign(corners.begin(), corners.end());
		} else { // Polygon
			local = def->collision.pointsMeters;
		}
		if (local.size() < 2) {
			return;
		}

		std::vector<Foundation::Vec2> screen;
		screen.reserve(local.size());
		// Simple (SVG) assets author the collider in SVG <metadata>, converted to the
		// bbox-CENTERED frame (relative to the entity origin), so it maps from the preview
		// center; procedural colliders are in the generator's mesh-coord frame.
		const bool centered = def->assetType == engine::assets::AssetType::Simple;
		for (const glm::vec2& p : local) {
			screen.push_back(centered ? thumb.centeredToScreen(p) : thumb.localToScreen(p));
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
		if (!m_hasAsset) {
			return false;
		}
		if (m_procedural && m_randomize && m_randomize->handleEvent(event)) {
			return true;
		}
		if (m_xmlScroll) {
			return m_xmlScroll->handleEvent(event);
		}
		return false;
	}

	void AssetDetailView::update(float dt) {
		if (m_randomize) {
			m_randomize->update(dt);
		}
		if (m_xmlScroll) {
			m_xmlScroll->update(dt);
		}
	}

} // namespace asset_manager
