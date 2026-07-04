#pragma once

#include "component/Container.h"
#include "layout/LayoutTypes.h"
#include "math/Types.h"

// LayoutContainer - Automatic layout for child components
//
// Arranges children in a stack (vertical or horizontal). Children report
// their size via getWidth()/getHeight() (margin included); LayoutContainer
// positions them via setPosition() and resizes Fill/Stretch children via
// setLayoutSize().
//
// Layout passes (in order):
// 1. Cross pass: Stretch/Fill children on the cross axis adopt the content
//    box cross size (minus their margin).
// 2. Main pass: Fixed/Hug children are measured (wrap-aware for Text); the
//    leftover main-axis space goes to Fill children by fillWeight.
// 3. Position pass: distribution + gap on the main axis, cross alignment on
//    the cross axis; nested LayoutContainers are re-laid-out via
//    layout(assignedBounds).
//
// Sizing semantics (settled during the A2 engine work):
// - Reported size (getWidth/getHeight) is the margin box: content + margin*2,
//   consistent with every other component. An explicit size is the CONTENT
//   size, so a 100x50 container with margin 10 reports 120x70.
// - A constructed size > 0 maps to SizeMode::Fixed for that axis, size 0 to
//   Hug (legacy auto-size). Set widthMode/heightMode = Fill before adding the
//   container to a parent to make it share leftover space.
// - Hug axes measure live from children: max child cross size, or the sum of
//   child main sizes plus gaps, plus padding.
// - layout(bounds) is a final-rect assignment: position and size are adopted
//   (content = bounds - margin*2; zero-sized bounds axes are ignored). A
//   LayoutContainer parent passes Fixed children their own measured size, so
//   Fixed is never overridden by the engine itself.
// - CrossAlign::Stretch resizes Hug/Fill children to the content box; Fixed
//   children keep their explicit size and align at Start. A cross-axis Fill
//   child stretches regardless of crossAlign.
// - Fill children share the main-axis leftover by fillWeight as exact float
//   shares (no rounding). In a Hug main axis there is no leftover, so Fill
//   children are measured at their intrinsic size.
// - Alignment and distribution never produce negative offsets: on overflow
//   every mode degrades to Start and children overflow past the end edge.
// - No parent back-pointers (v1): after mutating a child's content (text,
//   size, visibility), call invalidateLayout() on the owning container.
//
// Usage:
//   auto layout = LayoutContainer(LayoutContainer::Args{
//       .position = {50, 50},
//       .size = {200, 400},
//       .direction = Direction::Vertical,
//       .gap = 8,
//       .padding = Insets{16},
//       .distribution = Distribution::SpaceBetween,
//       .crossAlign = CrossAlign::Stretch
//   });
//   layout.addChild(Button({.label = "One", .margin = 5}));
//
// See: /docs/technical/ui-framework/layout-system.md

namespace UI {

class LayoutContainer : public Container {
  public:
	struct Args {
		Foundation::Vec2 position{0.0F, 0.0F};
		Foundation::Vec2 size{0.0F, 0.0F};
		Direction		 direction = Direction::Vertical;
		float			 gap{0.0F};
		Insets			 padding{};
		Distribution	 distribution = Distribution::Start;
		CrossAlign		 crossAlign = CrossAlign::Start;
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
		invalidateLayout();
		return Container::addChild(std::forward<T>(child));
	}

	// ILayer overrides
	void update(float deltaTime) override;
	void render() override;

	// Final-rect assignment: adopts position and size (see doc block above)
	void layout(const Foundation::Rect& bounds) override;

	// Parent-assigned content size for Fill/Stretch axes (kSizeKeep skips an axis)
	void setLayoutSize(float w, float h) override;

	// Override setPosition to mark layout dirty
	void setPosition(float x, float y) override {
		if (position.x != x || position.y != y) {
			position = {x, y};
			layoutDirty = true;
		}
	}

	// Margin-box size: resolved size when set, otherwise measured from children
	[[nodiscard]] float getWidth() const override;
	[[nodiscard]] float getHeight() const override;

	const char* debugTypeName() const override { return "LayoutContainer"; }
	const char* debugId() const override { return id; }

	// Re-run layout on next render. Call after mutating child content.
	void invalidateLayout() {
		layoutDirty = true;
		childSizesDirty = true;
	}

	// Setters for layout properties
	void setDirection(Direction dir) {
		direction = dir;
		invalidateLayout();
	}
	void setGap(float value) {
		gap = value;
		invalidateLayout();
	}
	void setPadding(const Insets& value) {
		padding = value;
		invalidateLayout();
	}
	void setDistribution(Distribution value) {
		distribution = value;
		invalidateLayout();
	}
	void setCrossAlign(CrossAlign value) {
		crossAlign = value;
		invalidateLayout();
	}

  private:
	Direction	 direction{Direction::Vertical};
	float		 gap{0.0F};
	Insets		 padding{};
	Distribution distribution{Distribution::Start};
	CrossAlign	 crossAlign{CrossAlign::Start};

	bool		layoutDirty{true};
	bool		childSizesDirty{true};
	const char* id{nullptr};

	void computeLayout();
	void resolveChildSizesIfDirty();
	void resolveChildSizes();
	void positionChildren();

	// Content extents measured from children (padding excluded)
	[[nodiscard]] float hugMainContent() const;
	[[nodiscard]] float hugCrossContent() const;
};

} // namespace UI
