// TreeView Scene - Demonstrates the TreeView component for hierarchical data
// Shows expandable/collapsible tree structure for browsing categories

#include <GL/glew.h>

#include "SceneTypes.h"
#include <components/scroll/ScrollContainer.h>
#include <components/treeview/TreeView.h>
#include <graphics/Color.h>
#include <input/InputEvent.h>
#include <memory>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <shapes/Shapes.h>
#include <theme/Theme.h>
#include <utils/Log.h>

namespace {

	constexpr const char* kSceneName = "treeview";

	class TreeViewScene : public engine::IScene {
	  public:
		const char* getName() const override { return kSceneName; }
		std::string exportState() override { return "{}"; }

		void onEnter() override {
			using namespace UI;
			using namespace Foundation;

			// Create title
			title = std::make_unique<Text>(Text::Args{
				.position = {50.0F, 30.0F},
				.text = "TreeView Component Demo",
				.style = {.color = Color::white(), .fontSize = 20.0F},
				.id = "title"});

			// ================================================================
			// Demo 1: Basic TreeView (like Resources panel)
			// ================================================================
			label1 = std::make_unique<Text>(Text::Args{
				.position = {50.0F, 70.0F},
				.text = "1. Materials by Category:",
				.style = {.color = Color::yellow(), .fontSize = 14.0F},
				.id = "label_1"});

			treeView1 = std::make_unique<TreeView>(TreeView::Args{
				.position = {50.0F, 95.0F},
				.size = {200.0F, 250.0F},
				.id = "tree_materials"});

			// Create a mock materials tree
			treeView1->setRootNodes({
				TreeNode{
					.label = "Food",
					.count = 45,
					.children =
						{
							TreeNode{.label = "Vegetables", .count = 20},
							TreeNode{.label = "Fruits",
									 .count = 15,
									 .children =
										 {
											 TreeNode{.label = "Berries", .count = 10},
											 TreeNode{.label = "Apples", .count = 5},
										 }},
							TreeNode{.label = "Meat", .count = 10},
						},
				},
				TreeNode{
					.label = "Materials",
					.count = 120,
					.children =
						{
							TreeNode{.label = "Wood",
									 .count = 50,
									 .children =
										 {
											 TreeNode{.label = "Logs", .count = 30},
											 TreeNode{.label = "Planks", .count = 20},
										 }},
							TreeNode{.label = "Stone",
									 .count = 40,
									 .children =
										 {
											 TreeNode{.label = "Rough Stone", .count = 25},
											 TreeNode{.label = "Cut Stone", .count = 15},
										 }},
							TreeNode{.label = "Metal", .count = 30},
						},
				},
				TreeNode{
					.label = "Medicine",
					.count = 8,
					.children =
						{
							TreeNode{.label = "Herbal", .count = 5},
							TreeNode{.label = "Industrial", .count = 3},
						},
				},
				TreeNode{
					.label = "Other",
					.count = 25,
				},
			});

			treeView1->setOnExpand([](TreeNode& node) { LOG_INFO(UI, "Expanded: %s", node.label.c_str()); });

			treeView1->setOnCollapse([](TreeNode& node) { LOG_INFO(UI, "Collapsed: %s", node.label.c_str()); });

			// ================================================================
			// Demo 2: TreeView in ScrollContainer
			// ================================================================
			label2 = std::make_unique<Text>(Text::Args{
				.position = {300.0F, 70.0F},
				.text = "2. Scrollable Tree (many items):",
				.style = {.color = Color::yellow(), .fontSize = 14.0F},
				.id = "label_2"});

			scrollContainer = std::make_unique<ScrollContainer>(
				ScrollContainer::Args{.position = {300.0F, 95.0F}, .size = {220.0F, 200.0F}, .id = "scroll_tree"});

			treeView2 = std::make_unique<TreeView>(TreeView::Args{
				.position = {0.0F, 0.0F},
				.size = {212.0F, 0.0F}, // Auto-height
				.id = "tree_large"});

			// Create a larger tree for scrolling demo
			std::vector<TreeNode> largeTree;
			for (int i = 0; i < 10; ++i) {
				TreeNode category;
				category.label = "Category " + std::to_string(i + 1);
				category.count = (i + 1) * 10;

				for (int j = 0; j < 5; ++j) {
					TreeNode item;
					item.label = "Item " + std::to_string(i + 1) + "." + std::to_string(j + 1);
					item.count = j + 1;
					category.children.push_back(item);
				}

				largeTree.push_back(category);
			}
			treeView2->setRootNodes(std::move(largeTree));
			treeView2->expandAll(); // Start expanded to show scrolling

			scrollContainer->addChild(std::move(*treeView2));
			treeView2.reset(); // Ownership transferred

			// ================================================================
			// Demo 3: Info
			// ================================================================
			label3 = std::make_unique<Text>(Text::Args{
				.position = {550.0F, 70.0F},
				.text = "3. Features:",
				.style = {.color = Color::yellow(), .fontSize = 14.0F},
				.id = "label_3"});

			expandAllHint = std::make_unique<Text>(Text::Args{
				.position = {550.0F, 100.0F},
				.text = "- Nested hierarchy with indentation",
				.style = {.color = Color(0.7F, 0.7F, 0.75F, 1.0F), .fontSize = 12.0F},
				.id = "expand_hint"});

			collapseAllHint = std::make_unique<Text>(Text::Args{
				.position = {550.0F, 120.0F},
				.text = "- Optional count badge per node",
				.style = {.color = Color(0.7F, 0.7F, 0.75F, 1.0F), .fontSize = 12.0F},
				.id = "collapse_hint"});

			// ================================================================
			// Instructions
			// ================================================================
			instructions = std::make_unique<Text>(Text::Args{
				.position = {50.0F, 370.0F},
				.text = "Click > to expand | Click v to collapse | Hover for highlight",
				.style = {.color = Color(0.6F, 0.6F, 0.7F, 1.0F), .fontSize = 12.0F},
				.id = "instructions"});

			LOG_INFO(UI, "TreeView scene initialized");
		}

		void onExit() override {
			title.reset();
			label1.reset();
			label2.reset();
			label3.reset();
			expandAllHint.reset();
			collapseAllHint.reset();
			instructions.reset();
			treeView1.reset();
			treeView2.reset();
			scrollContainer.reset();
			LOG_INFO(UI, "TreeView scene exited");
		}

		bool handleInput(UI::InputEvent& event) override {
			// Dispatch to tree views
			if (treeView1 && treeView1->handleEvent(event)) {
				return true;
			}
			if (scrollContainer && scrollContainer->handleEvent(event)) {
				return true;
			}
			return false;
		}

		void update(float deltaTime) override {
			if (treeView1) {
				treeView1->update(deltaTime);
			}
			if (scrollContainer) {
				scrollContainer->update(deltaTime);
			}
		}

		void render() override {
			// Clear background
			glClearColor(0.10F, 0.10F, 0.13F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Render labels
			if (title) {
				title->render();
			}
			if (label1) {
				label1->render();
			}
			if (label2) {
				label2->render();
			}
			if (label3) {
				label3->render();
			}
			if (expandAllHint) {
				expandAllHint->render();
			}
			if (collapseAllHint) {
				collapseAllHint->render();
			}
			if (instructions) {
				instructions->render();
			}

			// Render tree views
			if (treeView1) {
				treeView1->render();
			}
			if (scrollContainer) {
				scrollContainer->render();
			}
		}

	  private:
		// Labels
		std::unique_ptr<UI::Text> title;
		std::unique_ptr<UI::Text> label1;
		std::unique_ptr<UI::Text> label2;
		std::unique_ptr<UI::Text> label3;
		std::unique_ptr<UI::Text> expandAllHint;
		std::unique_ptr<UI::Text> collapseAllHint;
		std::unique_ptr<UI::Text> instructions;

		// Tree views
		std::unique_ptr<UI::TreeView> treeView1;
		std::unique_ptr<UI::TreeView> treeView2;

		// Scroll container for second tree
		std::unique_ptr<UI::ScrollContainer> scrollContainer;
	};

} // anonymous namespace

// Export scene info for registry
namespace ui_sandbox::scenes {
	extern const ui_sandbox::SceneInfo TreeView_ = {kSceneName, []() { return std::make_unique<TreeViewScene>(); }};
} // namespace ui_sandbox::scenes
