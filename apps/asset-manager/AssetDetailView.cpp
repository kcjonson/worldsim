#include "AssetDetailView.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <graphics/Color.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

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
			// Strip CR: the text renderer only breaks on '\n', so a CRLF file would
			// render a stray glyph at each line end.
			content.erase(std::remove(content.begin(), content.end(), '\r'), content.end());
			return content;
		}

		std::string fmtFloat(float v) {
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
			return buf;
		}
	} // namespace

	AssetDetailView::AssetDetailView() {
		m_placeholder = UI::Text(UI::Text::Args{
			.text = "Select an asset from the tree",
			.style = {.color = Foundation::Color(0.5F, 0.52F, 0.58F, 1.0F), .fontSize = 16.0F},
			.id = "am_detail_placeholder",
		});
		m_name = UI::Text(UI::Text::Args{
			.text = "",
			.style = {.color = Foundation::Color::white(), .fontSize = 22.0F},
			.id = "am_detail_name",
		});
		m_meta = UI::Text(UI::Text::Args{
			.text = "",
			.style = {.color = Foundation::Color(0.70F, 0.72F, 0.78F, 1.0F), .fontSize = 13.0F, .wordWrap = true},
			.id = "am_detail_meta",
		});
		m_warnings = UI::Text(UI::Text::Args{
			.text = "",
			.style = {.color = Foundation::Color(0.92F, 0.72F, 0.35F, 1.0F), .fontSize = 12.0F, .wordWrap = true},
			.id = "am_detail_warnings",
		});
		m_xmlHeader = UI::Text(UI::Text::Args{
			.text = "Definition",
			.style = {.color = Foundation::Color(0.55F, 0.58F, 0.66F, 1.0F), .fontSize = 12.0F},
			.id = "am_xml_header",
		});
		m_xmlScroll = std::make_unique<UI::ScrollContainer>(UI::ScrollContainer::Args{
			.position = {0.0F, 0.0F},
			.size = {400.0F, 200.0F},
			.id = "am_xml_scroll",
		});
	}

	void AssetDetailView::setBounds(const Foundation::Rect& bounds) {
		m_bounds = bounds;
		relayout();
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

		m_preview.setAsset(defName);
		m_preview.setSize(kPreview, kPreview);

		m_name.text = defName;

		const char* type = def->assetType == engine::assets::AssetType::Simple ? "simple" : "procedural";
		std::string meta = type;
		if (!def->label.empty()) {
			meta += "    " + def->label;
		}
		meta += "\nworldHeight: " + fmtFloat(def->worldHeight);
		if (!def->svgPath.empty()) {
			meta += "\nsvg: " + def->svgPath;
		}
		if (!def->scriptPath.empty()) {
			meta += "\nscript: " + def->scriptPath;
		}
		if (!def->placement.groups.empty()) {
			std::string groups;
			for (size_t i = 0; i < def->placement.groups.size(); ++i) {
				if (i != 0) {
					groups += ", ";
				}
				groups += def->placement.groups[i];
			}
			meta += "\ngroups: " + groups;
		}
		m_meta.text = meta;

		std::string									   warnings;
		const engine::assets::ValidationReport&		   report = engine::assets::AssetRegistry::Get().getValidationReport();
		int											   shown = 0;
		for (const auto& issue : report.issues) {
			if (issue.defName != defName) {
				continue;
			}
			if (shown++ > 0) {
				warnings += "\n";
			}
			warnings += (issue.severity == engine::assets::Severity::Error ? "[error] " : "[warning] ");
			if (!issue.field.empty()) {
				warnings += issue.field + ": ";
			}
			warnings += issue.message;
		}
		m_warnings.text = warnings;

		const std::filesystem::path xmlPath = def->baseFolder / (def->baseFolder.filename().string() + ".xml");
		m_xmlHeader.text = "Definition - " + xmlPath.filename().string();

		const float xmlWidth = std::max(80.0F, m_bounds.width - (2.0F * kPad) - 16.0F);
		m_xmlScroll->clearChildren();
		m_xmlScroll->addChild(UI::Text(UI::Text::Args{
			.position = {0.0F, 0.0F},
			.width = xmlWidth,
			.text = readFile(xmlPath),
			.style =
				{
					.color = Foundation::Color(0.78F, 0.85F, 0.80F, 1.0F),
					.fontSize = 12.0F,
					.wordWrap = true,
				},
			.id = "am_xml_text",
		}));

		relayout();
	}

	void AssetDetailView::relayout() {
		const float bx = m_bounds.x;
		const float by = m_bounds.y;

		m_placeholder.position = {bx + kPad, by + kPad};
		m_preview.setPosition(bx + kPad, by + kPad);
		const float rightColX = bx + kPad + kPreview + 20.0F;
		const float rightColW = std::max(80.0F, (m_bounds.x + m_bounds.width - kPad) - rightColX);
		m_name.position = {rightColX, by + kPad + 6.0F};
		m_meta.position = {rightColX, by + kPad + 40.0F};
		m_meta.width = rightColW;
		m_warnings.position = {rightColX, by + kPad + 120.0F};
		m_warnings.width = rightColW;

		const float xmlTop = by + kPad + kPreview + 18.0F;
		m_xmlHeader.position = {bx + kPad, xmlTop};

		const float scrollTop = xmlTop + 20.0F;
		const float scrollH = std::max(0.0F, (m_bounds.y + m_bounds.height) - scrollTop - kPad);
		m_xmlScroll->setPosition(bx + kPad, scrollTop);
		m_xmlScroll->setViewportSize({m_bounds.width - (2.0F * kPad), scrollH});
	}

	void AssetDetailView::render() {
		if (!m_hasAsset) {
			m_placeholder.render();
			return;
		}
		m_preview.render();
		m_name.render();
		m_meta.render();
		m_warnings.render();
		m_xmlHeader.render();
		if (m_xmlScroll) {
			m_xmlScroll->render();
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
