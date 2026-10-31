#include "AssetDetailView.h"
#include "AssetRow.h"
#include "AssetThumbnail.h"
#include "SceneTypes.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <theme/Theme.h>
#include <theme/Tokens.h>
#include <ui/components/Button.h>
#include <ui/components/TextInput.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>
#include <ui/theme/Tokens.h>
#include <utils/Log.h>
#include <utils/ResourcePath.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

	constexpr const char* kSceneName = "browser";
	constexpr float		  kTopBar = 56.0F;
	constexpr float		  kTreeWidth = 324.0F;

	namespace t = theme::tokens::theme;

	std::string toLower(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

	// The asset-manager is always run from a build tree, but authors edit the SOURCE
	// assets/. Walk up from the executable (then cwd) to the repo root -- the first
	// ancestor carrying assets/world plus a repo marker -- and return its assets/ dir,
	// so Reload re-reads the files being edited, not the build-time copy beside the
	// exe (which lacks a repo marker and is therefore skipped). Empty when run outside
	// a source tree (a shipped build), where the caller falls back to findResource.
	std::filesystem::path findSourceAssetsRoot() {
		namespace fs = std::filesystem;
		std::vector<fs::path> starts;
		if (auto exeDir = Foundation::getExecutableDir(); !exeDir.empty()) {
			starts.push_back(std::move(exeDir));
		}
		try {
			starts.push_back(fs::current_path());
		} catch (const fs::filesystem_error&) {
			// cwd was deleted (common in IDE terminals); the exe-dir start still covers us.
		}
		for (const auto& start : starts) {
			std::error_code ec;
			for (fs::path dir = start; !dir.empty(); dir = dir.parent_path()) {
				const bool hasAssets = fs::exists(dir / "assets" / "world", ec);
				const bool isRepo = fs::exists(dir / ".git", ec) || fs::exists(dir / "CMakeLists.txt", ec);
				if (hasAssets && isRepo) {
					return dir / "assets";
				}
				if (dir == dir.root_path()) {
					break;
				}
			}
		}
		return {};
	}

	// A hairline along one edge of its parent.
	ui::Layout edge(ui::Inset inset) {
		return {.position = ui::Position::Absolute, .inset = inset};
	}

	class BrowserScene : public app::Scene {
	  public:
		void onEnter(app::App& /*app*/) override {
			LOG_INFO(Engine, "AssetManager BrowserScene - Entering");

			// Prefer the SOURCE assets/ -- this is a live authoring tool, so Reload should
			// re-read the files being edited, not the build-time copy beside the exe.
			if (const std::filesystem::path src = findSourceAssetsRoot(); !src.empty()) {
				assetsRoot = (src / "world").string();
				sharedScripts = (src / "shared" / "scripts").string();
				LOG_INFO(Engine, "AssetManager: live source assets at %s", assetsRoot.c_str());
			} else {
				assetsRoot = Foundation::findResourceString("assets/world");
				sharedScripts = Foundation::findResourceString("assets/shared/scripts");
			}
			refreshNames();

			theme::apply(ui());
			const ui::Theme& palette = ui().getTheme();
			const float		 hairline = palette.get(t::border::bw);
			const ui::Style	 surface{.backgroundColor = palette.get(t::color::bgPanel)};
			root().get()->setLayout({.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column});

			ui::Ref<ui::Rectangle> topBar = root().addChild<ui::Rectangle>({
				.id = "am_top_bar",
				.size = {ui::kAuto, kTopBar},
				.layout =
					{.display = ui::Display::Flex,
					 .flexDirection = ui::FlexDirection::Row,
					 .gap = 12.0F,
					 .padding = {0.0F, 12.0F},
					 .alignItems = ui::AlignItems::Center},
				.style = surface,
			});
			topBar.addChild<ui::Rectangle>(
				{.size = {ui::kAuto, hairline}, .layout = edge({.right = 0.0F, .bottom = 0.0F, .left = 0.0F}), .style = ui::styles::divider}
			);
			topBar.addChild<ui::TextInput>({
				.id = "am_search",
				.size = {kTreeWidth - 12.0F, ui::kAuto},
				.placeholder = "Search assets...",
				.onChange = [this](const std::string& text) {
					searchText = text;
					pendingRebuild = true;
				},
			});
			topBar.addChild<ui::Button>({.id = "am_reload", .label = "Reload", .onPress = [this] { reload(); }});
			summary = topBar.addChild<ui::Text>({.id = "am_summary", .style = ui::styles::text::secondary});

			ui::Ref<ui::Component> body = root().addChild<ui::Component>(
				{.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Row, .flexGrow = 1.0F}}
			);
			ui::Ref<ui::Rectangle> sidebar = body.addChild<ui::Rectangle>({
				.size = {kTreeWidth, ui::kAuto},
				.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .padding = 12.0F},
				.style = surface,
			});
			sidebar.addChild<ui::Rectangle>(
				{.size = {hairline, ui::kAuto}, .layout = edge({.top = 0.0F, .right = 0.0F, .bottom = 0.0F}), .style = ui::styles::divider}
			);
			tree = sidebar.addChild<ui::Component>({
				.id = "am_tree_scroll",
				.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .flexGrow = 1.0F},
				.overflow = ui::Overflow::Scroll,
			});
			detail = body.addChild<asset_manager::AssetDetailView>({.layout = {.flexGrow = 1.0F}});

			updateSummary();
			rebuildTree();
		}

		void update(const app::FrameContext& /*frame*/) override {
			if (pendingRebuild) {
				pendingRebuild = false;
				rebuildTree();
			}
		}

		void render(float /*alpha*/) override { Renderer::Primitives::clear(ui().getTheme().get(t::color::bgVoid)); }

		std::string exportState() const override { return R"({"scene": "browser", "selected": ")" + selected + "\"}"; }
		const char* name() const override { return kSceneName; }

	  private:
		void refreshNames() {
			allNames = engine::assets::AssetRegistry::Get().getDefinitionNames();
			std::sort(allNames.begin(), allNames.end());
			LOG_INFO(Engine, "AssetManager: %zu definitions", allNames.size());
		}

		void updateSummary() {
			const engine::assets::ValidationReport& report = engine::assets::AssetRegistry::Get().getValidationReport();
			const int								errors = report.errorCount();
			ui::Text*								shown = summary.get();
			shown->setText(
				std::to_string(allNames.size()) + " assets    -    " + std::to_string(errors) + " errors, " +
				std::to_string(report.warningCount()) + " warnings"
			);
			shown->setStyle(
				ui::styles::text::secondary.extend({.color = ui().getTheme().get(errors > 0 ? t::color::statusCrit : t::color::textDim)})
			);
		}

		std::string categoryOf(const std::string& defName) const {
			const engine::assets::AssetDefinition* def = engine::assets::AssetRegistry::Get().getDefinition(defName);
			if (def == nullptr || def->baseFolder.empty()) {
				return "other";
			}
			const std::string parent = def->baseFolder.parent_path().filename().string();
			return parent.empty() ? "other" : parent;
		}

		// Callbacks from the tree's rows defer the rebuild out of the event that removes them.
		void selectAsset(const std::string& defName) {
			selected = defName;
			detail.get()->setAsset(defName);
			pendingRebuild = true;
		}

		void toggleCategory(const std::string& category) {
			auto it = collapsed.find(category);
			if (it == collapsed.end()) {
				collapsed.insert(category);
			} else {
				collapsed.erase(it);
			}
			pendingRebuild = true;
		}

		void reload() {
			asset_manager::AssetThumbnail::clearCache();
			auto& registry = engine::assets::AssetRegistry::Get();
			registry.clear();
			if (!sharedScripts.empty()) {
				registry.setSharedScriptsPath(sharedScripts);
			}
			registry.loadDefinitionsFromFolder(assetsRoot);
			refreshNames();
			updateSummary();

			if (std::find(allNames.begin(), allNames.end(), selected) == allNames.end()) {
				selected.clear();
			}
			detail.get()->setAsset(selected);
			pendingRebuild = true;
			LOG_INFO(Engine, "AssetManager: reloaded from disk (%zu definitions)", allNames.size());
		}

		void rebuildTree() {
			if (list.get() != nullptr) {
				tree.get()->removeChild(list);
			}
			list = tree.addChild<ui::Component>(
				{.id = "am_tree_layout", .layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column}}
			);

			const std::string								needle = toLower(searchText);
			std::map<std::string, std::vector<std::string>> groups;
			for (const auto& name : allNames) {
				if (!needle.empty() && toLower(name).find(needle) == std::string::npos) {
					continue;
				}
				groups[categoryOf(name)].push_back(name);
			}

			for (auto& [category, names] : groups) {
				const bool expanded = !collapsed.contains(category);
				list.addChild<asset_manager::GroupHeaderRow>({.label = category, .expanded = expanded, .onToggle = [this, category]() {
																  toggleCategory(category);
															  }});
				if (!expanded) {
					continue;
				}
				for (const auto& name : names) {
					list.addChild<asset_manager::AssetRow>(
						{.defName = name, .selected = name == selected, .onSelect = [this](const std::string& chosen) {
							 selectAsset(chosen);
						 }}
					);
				}
			}
		}

		ui::Ref<ui::Text>						summary;
		ui::Ref<ui::Component>					tree;
		ui::Ref<ui::Component>					list;
		ui::Ref<asset_manager::AssetDetailView> detail;

		std::string				 assetsRoot;
		std::string				 sharedScripts;
		std::vector<std::string> allNames;
		std::string				 searchText;
		std::string				 selected;
		std::set<std::string>	 collapsed;
		bool					 pendingRebuild = false;
	};

} // namespace

namespace asset_manager::scenes {
	extern const asset_manager::SceneInfo Browser = {kSceneName, []() { return std::make_unique<BrowserScene>(); }};
} // namespace asset_manager::scenes
