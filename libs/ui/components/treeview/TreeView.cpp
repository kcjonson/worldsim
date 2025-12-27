#include "TreeView.h"

#include "primitives/Primitives.h"
#include "shapes/Shapes.h"

namespace UI {

TreeView::TreeView(const Args& args)
	: rowHeight(args.rowHeight),
	  indentWidth(args.indentWidth) {
	position = args.position;
	size = args.size;
	margin = args.margin;
}

float TreeView::getHeight() const {
	// If size.y is set (non-zero), use fixed height mode
	if (size.y > 0.0F) {
		return size.y + margin * 2.0F;
	}

	// Auto-height mode: calculate from content
	if (flattenDirty) {
		rebuildFlatList();
	}

	float contentHeight = static_cast<float>(flattenedRows.size()) * rowHeight;
	return contentHeight + margin * 2.0F;
}

void TreeView::setRootNodes(std::vector<TreeNode> nodes) {
	rootNodes = std::move(nodes);
	flattenDirty = true;
}

void TreeView::expandAll() {
	for (auto& node : rootNodes) {
		expandAllRecursive(node);
	}
	flattenDirty = true;
}

void TreeView::collapseAll() {
	for (auto& node : rootNodes) {
		collapseAllRecursive(node);
	}
	flattenDirty = true;
}

void TreeView::toggleNode(size_t flatIndex) {
	// Rebuild flat list if dirty before accessing
	if (flattenDirty) {
		rebuildFlatList();
	}

	if (flatIndex >= flattenedRows.size()) {
		return;
	}

	FlatRow& row = flattenedRows[flatIndex];
	if (row.node->children.empty()) {
		return; // Leaf node, nothing to toggle
	}

	row.node->expanded = !row.node->expanded;

	if (row.node->expanded) {
		if (onExpand) {
			onExpand(*row.node);
		}
	} else {
		if (onCollapse) {
			onCollapse(*row.node);
		}
	}

	flattenDirty = true;
}

void TreeView::setPosition(float x, float y) {
	position = {x, y};
}

bool TreeView::containsPoint(Foundation::Vec2 point) const {
	Foundation::Vec2 contentPos = getContentPosition();

	// Use actual content height in auto-height mode
	float effectiveHeight = getHeight() - margin * 2.0F;

	return point.x >= contentPos.x && point.x < contentPos.x + size.x && point.y >= contentPos.y &&
		   point.y < contentPos.y + effectiveHeight;
}

bool TreeView::handleEvent(InputEvent& event) {
	if (!visible) {
		return false;
	}

	// Rebuild flat list if dirty before processing events
	if (flattenDirty) {
		rebuildFlatList();
	}

	switch (event.type) {
		case InputEvent::Type::MouseMove: {
			if (containsPoint(event.position)) {
				hoveredRowIndex = getRowAtPoint(event.position);
			} else {
				hoveredRowIndex = -1;
			}
			// Don't consume mouse move - allow other components to also track hover
			return false;
		}

		case InputEvent::Type::MouseDown: {
			if (!containsPoint(event.position)) {
				return false;
			}

			int rowIndex = getRowAtPoint(event.position);
			if (rowIndex >= 0 && static_cast<size_t>(rowIndex) < flattenedRows.size()) {
				// Check if click was on expand indicator
				if (isPointInExpandIndicator(event.position, rowIndex)) {
					toggleNode(static_cast<size_t>(rowIndex));
					event.consume();
					return true;
				}
			}
			break;
		}

		default:
			break;
	}

	return false;
}

void TreeView::render() {
	if (!visible) {
		return;
	}

	// Rebuild flat list if dirty
	if (flattenDirty) {
		rebuildFlatList();
	}

	Foundation::Vec2 contentPos = getContentPosition();

	// Calculate effective height for clipping (auto-height mode uses content height)
	float effectiveHeight = size.y > 0.0F ? size.y : static_cast<float>(flattenedRows.size()) * rowHeight;

	// Render each visible row
	for (size_t i = 0; i < flattenedRows.size(); ++i) {
		const FlatRow& row = flattenedRows[i];
		float		   rowY = contentPos.y + static_cast<float>(i) * rowHeight;

		// Check if row is within visible bounds (only clip if fixed height mode)
		if (size.y > 0.0F && (rowY + rowHeight < contentPos.y || rowY > contentPos.y + effectiveHeight)) {
			continue; // Skip rows outside viewport
		}

		float indent = static_cast<float>(row.depth) * indentWidth;
		float rowX = contentPos.x + indent;

		// Hover highlight
		if (static_cast<int>(i) == hoveredRowIndex) {
			Renderer::Primitives::drawRect(Renderer::Primitives::RectArgs{
				.bounds = {contentPos.x, rowY, size.x, rowHeight},
				.style = {.fill = Theme::TreeView::rowHover},
				.zIndex = zIndex,
			});
		}

		// Expand/collapse indicator
		bool hasChildren = !row.node->children.empty();
		if (hasChildren) {
			const char* indicator = row.node->expanded ? "v" : ">";
			Renderer::Primitives::drawText(Renderer::Primitives::TextArgs{
				.text = indicator,
				.position = {rowX, rowY + (rowHeight - 12.0F) / 2.0F},
				.scale = 12.0F / 16.0F,
				.color = Theme::Colors::textSecondary,
				.zIndex = static_cast<float>(zIndex) + 0.1F,
			});
		}

		// Label text (offset past indicator)
		float labelX = rowX + (hasChildren ? 16.0F : 8.0F);
		std::string displayText = row.node->label;

		// Add count if present
		if (row.node->count.has_value()) {
			displayText += " (" + std::to_string(row.node->count.value()) + ")";
		}

		Renderer::Primitives::drawText(Renderer::Primitives::TextArgs{
			.text = displayText,
			.position = {labelX, rowY + (rowHeight - 12.0F) / 2.0F},
			.scale = 12.0F / 16.0F,
			.color = row.node->count.has_value() ? Theme::Colors::textBody : Theme::Colors::textBody,
			.zIndex = static_cast<float>(zIndex) + 0.1F,
		});
	}
}

void TreeView::rebuildFlatList() const {
	flattenedRows.clear();

	for (auto& node : rootNodes) {
		flattenNode(node, 0);
	}

	flattenDirty = false;
}

void TreeView::flattenNode(const TreeNode& node, int depth) const {
	// const_cast is safe here: we own rootNodes and need non-const pointers
	// for later modification in toggleNode(). This allows getHeight() to be const.
	flattenedRows.push_back(FlatRow{
		.node = const_cast<TreeNode*>(&node),
		.depth = depth,
		.nodeIndex = flattenedRows.size(),
	});

	if (node.expanded) {
		for (const auto& child : node.children) {
			flattenNode(child, depth + 1);
		}
	}
}

int TreeView::getRowAtPoint(Foundation::Vec2 point) const {
	Foundation::Vec2 contentPos = getContentPosition();

	if (!containsPoint(point)) {
		return -1;
	}

	float relativeY = point.y - contentPos.y;
	int	  rowIndex = static_cast<int>(relativeY / rowHeight);

	if (rowIndex < 0 || static_cast<size_t>(rowIndex) >= flattenedRows.size()) {
		return -1;
	}

	return rowIndex;
}

bool TreeView::isPointInExpandIndicator(Foundation::Vec2 point, int rowIndex) const {
	if (rowIndex < 0 || static_cast<size_t>(rowIndex) >= flattenedRows.size()) {
		return false;
	}

	const FlatRow& row = flattenedRows[static_cast<size_t>(rowIndex)];

	// Only nodes with children have expand indicators
	if (row.node->children.empty()) {
		return false;
	}

	Foundation::Vec2 contentPos = getContentPosition();
	float			 indent = static_cast<float>(row.depth) * indentWidth;
	float			 indicatorX = contentPos.x + indent;

	// Indicator is roughly 16px wide
	return point.x >= indicatorX && point.x < indicatorX + 16.0F;
}

void TreeView::expandAllRecursive(TreeNode& node) {
	if (!node.children.empty()) {
		node.expanded = true;
		for (auto& child : node.children) {
			expandAllRecursive(child);
		}
	}
}

void TreeView::collapseAllRecursive(TreeNode& node) {
	node.expanded = false;
	for (auto& child : node.children) {
		collapseAllRecursive(child);
	}
}

} // namespace UI
