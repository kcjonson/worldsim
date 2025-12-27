#pragma once

// TreeView - Hierarchical data display with expand/collapse
//
// Displays tree-structured data with expandable/collapsible nodes.
// Used for browsing hierarchical lists like the Resources panel.
//
// Features:
// - Expand/collapse nodes via ▶/▼ indicators
// - Nested hierarchy with indentation
// - Optional count badge per node
// - Hover highlighting
// - Callbacks for expand/collapse events
//
// Note: This is a VIEW component (no selection). For selectable trees,
// use a List component with TreeView-style rendering.

#include "component/Component.h"
#include "graphics/Color.h"
#include "theme/Theme.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace UI {

/// Data model for a tree node
struct TreeNode {
	std::string				label;
	std::optional<int>		count;			 // Optional count badge (e.g., "Vegetables (45)")
	std::vector<TreeNode>	children;
	bool					expanded{false}; // Whether children are visible
	void*					userData{nullptr}; // Optional application data
};

class TreeView : public Component {
  public:
	struct Args {
		Foundation::Vec2 position{0.0F, 0.0F};
		Foundation::Vec2 size{200.0F, 300.0F};
		float			 rowHeight{Theme::TreeView::rowHeight};
		float			 indentWidth{Theme::TreeView::indentWidth};
		const char*		 id = nullptr;
		float			 margin{0.0F};
	};

	explicit TreeView(const Args& args);
	~TreeView() override = default;

	// Disable copy (owns internal data)
	TreeView(const TreeView&) = delete;
	TreeView& operator=(const TreeView&) = delete;

	// Allow move
	TreeView(TreeView&&) noexcept = default;
	TreeView& operator=(TreeView&&) noexcept = default;

	// Data management
	void				   setRootNodes(std::vector<TreeNode> nodes);
	std::vector<TreeNode>& getRootNodes() { return rootNodes; }
	const std::vector<TreeNode>& getRootNodes() const { return rootNodes; }

	// State control
	void expandAll();
	void collapseAll();
	void toggleNode(size_t flatIndex);

	// Callbacks
	using OnExpandCallback = std::function<void(TreeNode& node)>;
	using OnCollapseCallback = std::function<void(TreeNode& node)>;
	void setOnExpand(OnExpandCallback callback) { onExpand = std::move(callback); }
	void setOnCollapse(OnCollapseCallback callback) { onCollapse = std::move(callback); }

	// Getters
	[[nodiscard]] float	 getRowHeight() const { return rowHeight; }
	[[nodiscard]] float	 getIndentWidth() const { return indentWidth; }
	[[nodiscard]] size_t getVisibleRowCount() {
		if (flattenDirty) {
			rebuildFlatList();
		}
		return flattenedRows.size();
	}

	// Layout API override for auto-height mode
	[[nodiscard]] float getHeight() const override;

	// IComponent overrides
	void render() override;
	bool handleEvent(InputEvent& event) override;
	bool containsPoint(Foundation::Vec2 point) const override;

	// Position update
	void setPosition(float x, float y) override;

  private:
	std::vector<TreeNode> rootNodes;
	float				  rowHeight;
	float				  indentWidth;
	int					  hoveredRowIndex{-1};

	OnExpandCallback  onExpand;
	OnCollapseCallback onCollapse;

	// Flattened visible rows for rendering (mutable for lazy rebuild in const methods)
	struct FlatRow {
		TreeNode* node;
		int		  depth;
		size_t	  nodeIndex; // Index in parent's children array
	};
	mutable std::vector<FlatRow> flattenedRows;
	mutable bool				 flattenDirty{true};

	void rebuildFlatList() const;
	void flattenNode(const TreeNode& node, int depth) const;
	int	 getRowAtPoint(Foundation::Vec2 point) const;
	bool isPointInExpandIndicator(Foundation::Vec2 point, int rowIndex) const;

	void expandAllRecursive(TreeNode& node);
	void collapseAllRecursive(TreeNode& node);
};

} // namespace UI
