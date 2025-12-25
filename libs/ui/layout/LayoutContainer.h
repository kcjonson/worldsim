#pragma once

#include "component/Container.h"
#include "layout/LayoutTypes.h"
#include "math/Types.h"

// LayoutContainer - Automatic layout for child components
//
// Arranges children in a stack (vertical or horizontal) based on their sizes.
// Children report their size via getWidth()/getHeight() (including margin).
// LayoutContainer positions children via setPosition().
//
// Layout Model (hybrid):
// - Stacking axis (Y for Vertical): child-driven, queries getHeight()
// - Cross axis (X for Vertical): parent-driven, children get container's width
//
// Usage:
//   auto layout = LayoutContainer(LayoutContainer::Args{
//       .position = {50, 50},
//       .size = {200, 400},
//       .direction = Direction::Vertical
//   });
//   layout.addChild(Button({.label = "One", .margin = 5}));
//   layout.addChild(Button({.label = "Two", .margin = 5}));
//
// See: /docs/technical/ui-framework/layout-system.md

namespace UI {

class LayoutContainer : public Container {
  public:
	struct Args {
		Foundation::Vec2 position{0.0F, 0.0F};
		Foundation::Vec2 size{0.0F, 0.0F};
		Direction		 direction = Direction::Vertical;
		HAlign			 hAlign = HAlign::Left;
		VAlign			 vAlign = VAlign::Top;
		const char*		 id = nullptr;
		float			 margin{0.0F};
	};

	explicit LayoutContainer(const Args& args);
	~LayoutContainer() override = default;

	// Disable copy (Container owns arena memory)
	LayoutContainer(const LayoutContainer&) = delete;
	LayoutContainer& operator=(const LayoutContainer&) = delete;

	// Allow move
	LayoutContainer(LayoutContainer&&) = default;
	LayoutContainer& operator=(LayoutContainer&&) = default;

	// Override addChild to mark layout dirty
	template <typename T>
	LayerHandle addChild(T&& child) {
		layoutDirty = true;
		return Container::addChild(std::forward<T>(child));
	}

	// ILayer overrides
	void update(float deltaTime) override;
	void render() override;

	// Override layout to perform automatic child positioning
	void layout(const Foundation::Rect& bounds) override;

	// Override setPosition to mark layout dirty
	void setPosition(float x, float y) override {
		if (position.x != x || position.y != y) {
			position = {x, y};
			layoutDirty = true;
		}
	}

	// Override getWidth/getHeight to compute from children when size is 0
	[[nodiscard]] float getWidth() const override {
		if (size.x > 0.0F) {
			return size.x + margin * 2.0F;
		}
		// Compute from children
		float maxWidth = 0.0F;
		for (const auto* child : children) {
			if (!child->visible) continue;
			maxWidth = std::max(maxWidth, child->getWidth());
		}
		return maxWidth + margin * 2.0F;
	}

	[[nodiscard]] float getHeight() const override {
		if (size.y > 0.0F) {
			return size.y + margin * 2.0F;
		}
		// Compute from children based on direction
		float totalHeight = 0.0F;
		for (const auto* child : children) {
			if (!child->visible) continue;
			if (direction == Direction::Vertical) {
				totalHeight += child->getHeight();
			} else {
				totalHeight = std::max(totalHeight, child->getHeight());
			}
		}
		return totalHeight + margin * 2.0F;
	}

	// Setters for layout properties
	void setDirection(Direction dir) {
		direction = dir;
		layoutDirty = true;
	}
	void setHAlign(HAlign align) {
		hAlign = align;
		layoutDirty = true;
	}
	void setVAlign(VAlign align) {
		vAlign = align;
		layoutDirty = true;
	}

  private:
	Direction direction{Direction::Vertical};
	HAlign	  hAlign{HAlign::Left};
	VAlign	  vAlign{VAlign::Top};

	bool				 layoutDirty{true};
	Foundation::Rect	 lastBounds;
	const char*			 id{nullptr};

	// Perform the actual layout computation
	void computeLayout();
};

} // namespace UI
