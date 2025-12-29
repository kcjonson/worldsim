#include "MemoryTabView.h"
#include "TabStyles.h"

#include <components/scroll/ScrollContainer.h>
#include <components/treeview/TreeView.h>
#include <layout/LayoutContainer.h>
#include <shapes/Shapes.h>

#include <sstream>
#include <unordered_map>

namespace world_sim {

void MemoryTabView::create(const Foundation::Rect& contentBounds) {
	using namespace tabs;

	constexpr float kCompactRowHeight = 18.0F;
	float headerHeight = kLabelSize + 8.0F;
	float treeViewHeight = contentBounds.height - headerHeight;

	auto layout = UI::LayoutContainer(UI::LayoutContainer::Args{
		.position = {contentBounds.x, contentBounds.y},
		.size = {contentBounds.width, contentBounds.height},
		.direction = UI::Direction::Vertical,
		.id = "memory_content"
	});

	// Header - store handle for dynamic updates
	auto headerText = UI::Text(UI::Text::Args{
		.height = kLabelSize,
		.text = "Known Entities: 0 total",
		.style = {.color = labelColor(), .fontSize = kLabelSize},
		.margin = 4.0F
	});
	headerTextHandle = layout.addChild(std::move(headerText));

	// ScrollContainer with TreeView - store handles for dynamic updates
	float scrollWidth = contentBounds.width - 8.0F;
	auto scrollContainer = UI::ScrollContainer(UI::ScrollContainer::Args{
		.size = {scrollWidth, treeViewHeight},
		.id = "memory_scroll"
	});

	auto treeView = UI::TreeView(UI::TreeView::Args{
		.position = {0.0F, 0.0F},
		.size = {scrollWidth - 8.0F, 0.0F},  // Auto-height
		.rowHeight = kCompactRowHeight,
		.id = "memory_tree"
	});
	treeViewHandle = scrollContainer.addChild(std::move(treeView));

	scrollContainerHandle = layout.addChild(std::move(scrollContainer));

	layoutHandle = addChild(std::move(layout));
}

void MemoryTabView::update(const MemoryData& memory) {
	auto* layout = getChild<UI::LayoutContainer>(layoutHandle);
	if (layout == nullptr) return;

	// Update header using stored handle
	if (auto* text = layout->getChild<UI::Text>(headerTextHandle)) {
		std::ostringstream ss;
		ss << "Known Entities: " << memory.totalKnown << " total";
		text->text = ss.str();
	}

	// Get ScrollContainer and TreeView using stored handles
	auto* scrollContainer = layout->getChild<UI::ScrollContainer>(scrollContainerHandle);
	if (scrollContainer == nullptr) return;

	auto* treeView = scrollContainer->getChild<UI::TreeView>(treeViewHandle);
	if (treeView == nullptr) return;

	// Preserve expanded state (categories and type groups)
	std::unordered_map<std::string, bool> expandedState;
	for (const auto& categoryNode : treeView->getRootNodes()) {
		expandedState[categoryNode.label] = categoryNode.expanded;
		for (const auto& typeNode : categoryNode.children) {
			expandedState[categoryNode.label + "/" + typeNode.label] = typeNode.expanded;
		}
	}

	std::vector<UI::TreeNode> nodes;

	for (const auto& category : memory.categories) {
		UI::TreeNode categoryNode;
		categoryNode.label = category.name;
		categoryNode.count = static_cast<int>(category.count);

		auto catIt = expandedState.find(category.name);
		categoryNode.expanded = (catIt != expandedState.end()) ? catIt->second : false;

		// Group entities by type
		std::unordered_map<std::string, std::vector<const MemoryEntity*>> byType;
		for (const auto& entity : category.entities) {
			byType[entity.name].push_back(&entity);
		}

		// Create child node for each type
		for (const auto& [typeName, entities] : byType) {
			UI::TreeNode typeNode;
			typeNode.label = typeName;
			typeNode.count = static_cast<int>(entities.size());

			std::string typeKey = category.name + "/" + typeName;
			auto typeIt = expandedState.find(typeKey);
			typeNode.expanded = (typeIt != expandedState.end()) ? typeIt->second : false;

			// Add location children
			for (const auto* entity : entities) {
				UI::TreeNode locationNode;
				std::ostringstream ss;
				ss << "at (" << static_cast<int>(entity->x) << ", " << static_cast<int>(entity->y) << ")";
				locationNode.label = ss.str();
				typeNode.children.push_back(locationNode);
			}

			categoryNode.children.push_back(typeNode);
		}

		nodes.push_back(categoryNode);
	}

	treeView->setRootNodes(std::move(nodes));
}

} // namespace world_sim
