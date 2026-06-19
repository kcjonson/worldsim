#include "AssetDetailView.h"
#include "AssetRow.h"
#include "SceneTypes.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <components/TextInput/TextInput.h>
#include <components/scroll/ScrollContainer.h>
#include <graphics/Color.h>
#include <layout/LayoutContainer.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <shapes/Shapes.h>
#include <utils/Log.h>

#include <GL/glew.h>
#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr const char*  kSceneName = "browser";
constexpr float		   kTopBar = 56.0F;
constexpr float		   kTreeWidth = 300.0F;

std::string toLower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

class BrowserScene : public engine::IScene {
  public:
	void onEnter() override {
		LOG_INFO(Engine, "AssetManager BrowserScene - Entering");

		m_allNames = engine::assets::AssetRegistry::Get().getDefinitionNames();
		std::sort(m_allNames.begin(), m_allNames.end());
		LOG_INFO(Engine, "AssetManager: %zu definitions", m_allNames.size());

		m_title = std::make_unique<UI::Text>(UI::Text::Args{
			.position = {14.0F, 16.0F},
			.text = "Asset manager",
			.style = {.color = Foundation::Color::white(), .fontSize = 22.0F},
			.id = "am_title",
		});

		m_search = std::make_unique<UI::TextInput>(UI::TextInput::Args{
			.position = {14.0F, kTopBar - 42.0F},
			.size = {kTreeWidth - 4.0F, 30.0F},
			.text = "",
			.placeholder = "Search assets...",
			.tabIndex = 0,
			.id = "am_search",
			.enabled = true,
			.onChange =
				[this](const std::string& text) {
					m_searchText = text;
					m_pendingRebuild = true;
				},
		});

		m_treeScroll = std::make_unique<UI::ScrollContainer>(UI::ScrollContainer::Args{
			.position = {12.0F, kTopBar},
			.size = {kTreeWidth, 200.0F},
			.id = "am_tree_scroll",
		});

		layout();
		rebuildTree();
		m_detail.setAsset("");
	}

	bool handleInput(UI::InputEvent& event) override {
		if (m_search && m_search->handleEvent(event)) {
			return true;
		}
		if (m_treeScroll && m_treeScroll->handleEvent(event)) {
			return true;
		}
		return m_detail.handleInput(event);
	}

	void update(float dt) override {
		if (m_pendingRebuild) {
			rebuildTree();
			m_pendingRebuild = false;
		}
		if (m_search) {
			m_search->update(dt);
		}
		if (m_treeScroll) {
			m_treeScroll->update(dt);
		}
		m_detail.update(dt);
	}

	void render() override {
		glClearColor(0.09F, 0.09F, 0.11F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);

		if (m_title) {
			m_title->render();
		}
		if (m_search) {
			m_search->render();
		}
		if (m_treeScroll) {
			m_treeScroll->render();
		}
		m_detail.render();
	}

	void onExit() override {
		m_search.reset();
		m_treeScroll.reset();
		m_title.reset();
	}

	std::string exportState() override { return R"({"scene": "browser", "selected": ")" + m_selected + "\"}"; }
	const char* getName() const override { return kSceneName; }

  private:
	void layout() {
		int viewportW = 0;
		int viewportH = 0;
		Renderer::Primitives::getLogicalViewport(viewportW, viewportH);
		const float vw = static_cast<float>(viewportW);
		const float vh = static_cast<float>(viewportH);

		m_treeScroll->setPosition(12.0F, kTopBar);
		m_treeScroll->setViewportSize({kTreeWidth, vh - kTopBar - 12.0F});

		const float detailX = kTreeWidth + 24.0F;
		m_detail.setBounds({detailX, kTopBar, vw - detailX - 12.0F, vh - kTopBar - 12.0F});
	}

	std::string categoryOf(const std::string& defName) const {
		const engine::assets::AssetDefinition* def = engine::assets::AssetRegistry::Get().getDefinition(defName);
		if (def == nullptr || def->baseFolder.empty()) {
			return "other";
		}
		const std::string parent = def->baseFolder.parent_path().filename().string();
		return parent.empty() ? "other" : parent;
	}

	void selectAsset(const std::string& defName) {
		m_selected = defName;
		m_detail.setAsset(defName);
		m_pendingRebuild = true; // refresh selection highlight (deferred out of event dispatch)
	}

	void toggleCategory(const std::string& category) {
		auto it = m_collapsed.find(category);
		if (it == m_collapsed.end()) {
			m_collapsed.insert(category);
		} else {
			m_collapsed.erase(it);
		}
		m_pendingRebuild = true;
	}

	void rebuildTree() {
		if (!m_treeScroll) {
			return;
		}
		m_treeScroll->clearChildren();

		const float			rowWidth = kTreeWidth - 16.0F;
		UI::LayoutContainer layoutContainer(UI::LayoutContainer::Args{
			.position = {0.0F, 0.0F},
			.size = {rowWidth, 0.0F},
			.direction = UI::Direction::Vertical,
			.id = "am_tree_layout",
		});

		const std::string needle = toLower(m_searchText);

		std::map<std::string, std::vector<std::string>> groups;
		for (const auto& name : m_allNames) {
			if (!needle.empty() && toLower(name).find(needle) == std::string::npos) {
				continue;
			}
			groups[categoryOf(name)].push_back(name);
		}

		for (auto& [category, names] : groups) {
			const bool expanded = m_collapsed.find(category) == m_collapsed.end();
			layoutContainer.addChild(asset_manager::GroupHeaderRow(asset_manager::GroupHeaderRow::Args{
				.label = category,
				.expanded = expanded,
				.onToggle = [this, category]() { toggleCategory(category); },
				.width = rowWidth,
			}));
			if (!expanded) {
				continue;
			}
			for (const auto& name : names) {
				asset_manager::AssetRow row(asset_manager::AssetRow::Args{
					.defName = name,
					.onSelect = [this](const std::string& selected) { selectAsset(selected); },
					.width = rowWidth,
				});
				row.setSelected(name == m_selected);
				layoutContainer.addChild(std::move(row));
			}
		}

		m_treeScroll->addChild(std::move(layoutContainer));
	}

	std::unique_ptr<UI::Text>			 m_title;
	std::unique_ptr<UI::TextInput>		 m_search;
	std::unique_ptr<UI::ScrollContainer> m_treeScroll;
	asset_manager::AssetDetailView		 m_detail;

	std::vector<std::string>	  m_allNames;
	std::string					  m_searchText;
	std::string					  m_selected;
	std::set<std::string>		  m_collapsed;
	bool						  m_pendingRebuild = false;
};

} // namespace

namespace asset_manager::scenes {
	extern const asset_manager::SceneInfo Browser = {kSceneName, []() { return std::make_unique<BrowserScene>(); }};
}
