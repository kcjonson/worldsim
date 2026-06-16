#include "TreeView.h"

#include "graphics/PrimitiveStyles.h"
#include "primitives/Primitives.h"
#include "theme/Tokens.h"
#include "theme/Variants.h"

namespace UI {

namespace {

	// drawText scale is relative to a 16px base.
	constexpr float kTextBasePx = 16.0F;

	float textScale(float sizePx) { return sizePx / kTextBasePx; }

} // namespace

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

	using Renderer::Primitives::drawLine;
	using Renderer::Primitives::drawRect;
	using Renderer::Primitives::drawText;

	Foundation::Vec2 contentPos = getContentPosition();

	// Calculate effective height for clipping (auto-height mode uses content height)
	float effectiveHeight = size.y > 0.0F ? size.y : static_cast<float>(flattenedRows.size()) * rowHeight;

	// Marker glyph metrics: a small chevron drawn from two diagonal strokes.
	constexpr float kMarkerBox = 16.0F;	  // column reserved for the chevron
	constexpr float kChevronArm = 3.5F;	  // half-extent of the chevron
	constexpr float kChevronStroke = bw_thick; // stroke width

	const float rowTextPx = fs_sm;
	const float labelZ = static_cast<float>(zIndex) + 0.1F;

	// Render each visible row
	for (size_t i = 0; i < flattenedRows.size(); ++i) {
		const FlatRow& row = flattenedRows[i];
		float		   rowY = contentPos.y + static_cast<float>(i) * rowHeight;

		// Check if row is within visible bounds (only clip if fixed height mode)
		if (size.y > 0.0F && (rowY + rowHeight < contentPos.y || rowY > contentPos.y + effectiveHeight)) {
			continue; // Skip rows outside viewport
		}

		const bool hovered = static_cast<int>(i) == hoveredRowIndex;

		float indent = static_cast<float>(row.depth) * indentWidth;
		float rowX = contentPos.x + indent;

		// Hairline separator below each row, drawn first so washes sit on top.
		drawRect({.bounds = {contentPos.x, rowY + rowHeight - bw_hair, size.x, bw_hair},
				  .style = {.fill = line_hairline},
				  .zIndex = zIndex});

		// Hover wash spanning the full row width.
		if (hovered) {
			drawRect({.bounds = {contentPos.x, rowY, size.x, rowHeight},
					  .style = {.fill = bg_hover},
					  .zIndex = zIndex});
		}

		// Expand/collapse marker: a chevron built from two short legs. Points down
		// (v) when expanded, right (>) when collapsed. Accent when open, dim when
		// shut; brightens slightly on hover.
		const bool hasChildren = !row.node->children.empty();
		if (hasChildren) {
			const bool				expanded = row.node->expanded;
			const Foundation::Color markerColor = expanded
				? (hovered ? accent_bright : accent)
				: (hovered ? text : text_dim);

			const float cx = rowX + (kMarkerBox * 0.5F);
			const float cy = rowY + (rowHeight * 0.5F);
			const auto	stroke = [&](Foundation::Vec2 a, Foundation::Vec2 b) {
				drawLine({.start = a, .end = b, .style = {.color = markerColor, .width = kChevronStroke}, .zIndex = zIndex + 1});
			};

			if (expanded) {
				// Downward chevron (v): two arms meeting at the bottom vertex.
				stroke({cx - kChevronArm, cy - kChevronArm * 0.5F}, {cx, cy + kChevronArm * 0.5F});
				stroke({cx, cy + kChevronArm * 0.5F}, {cx + kChevronArm, cy - kChevronArm * 0.5F});
			} else {
				// Rightward chevron (>): two arms meeting at the right vertex.
				stroke({cx - kChevronArm * 0.5F, cy - kChevronArm}, {cx + kChevronArm * 0.5F, cy});
				stroke({cx + kChevronArm * 0.5F, cy}, {cx - kChevronArm * 0.5F, cy + kChevronArm});
			}
		}

		// Label, offset past the marker column. Body text in UI font.
		const float labelX = rowX + (hasChildren ? kMarkerBox : space_2);
		drawText({.text = row.node->label,
				  .position = {labelX, rowY},
				  .scale = textScale(rowTextPx),
				  .color = hovered ? text_bright : text,
				  .font = fontUi,
				  .vAlign = Foundation::VerticalAlign::Middle,
				  .boxHeight = rowHeight,
				  .zIndex = labelZ});

		// Count badge, right-aligned in mono and dimmed.
		if (row.node->count.has_value()) {
			drawText({.text = std::to_string(row.node->count.value()),
					  .position = {contentPos.x, rowY},
					  .scale = textScale(fs_xs),
					  .color = text_dim,
					  .font = fontMono,
					  .hAlign = Foundation::HorizontalAlign::Right,
					  .vAlign = Foundation::VerticalAlign::Middle,
					  .boxWidth = size.x - space_2,
					  .boxHeight = rowHeight,
					  .zIndex = labelZ});
		}
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
