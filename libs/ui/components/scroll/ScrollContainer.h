#pragma once

// ScrollContainer - Scrollable viewport with scrollbar
//
// Encapsulates scroll mechanics:
// - Clipping: Content is masked to viewport bounds
// - Content offset: Children scroll within the viewport
// - Scrollbar: Visual track + draggable thumb
// - Mouse wheel: Scroll events update position
//
// Usage:
//   auto scroll = ScrollContainer({.size = {200, 300}});
//   scroll.addChild(LayoutContainer{...}); // Content can be taller than viewport
//   scroll.setContentHeight(500);          // Total content height
//   scroll.scrollTo(100);                  // Scroll to position
//
// Content height can be set manually via setContentHeight(), or the container
// will auto-detect from the first child's getHeight() during render.

#include "component/Container.h"
#include "graphics/ClipTypes.h"
#include "graphics/Color.h"
#include "input/InputEvent.h"
#include "math/Types.h"
#include "shapes/Shapes.h"

namespace UI {

class ScrollContainer : public Container {
  public:
	struct Args {
		Foundation::Vec2 position{0.0F, 0.0F};
		Foundation::Vec2 size{200.0F, 300.0F}; // Viewport size
		const char*		 id = nullptr;
		float			 margin{0.0F};
	};

	explicit ScrollContainer(const Args& args);
	~ScrollContainer() override = default;

	// Disable copy (owns visual elements)
	ScrollContainer(const ScrollContainer&) = delete;
	ScrollContainer& operator=(const ScrollContainer&) = delete;

	// Allow move
	ScrollContainer(ScrollContainer&&) noexcept = default;
	ScrollContainer& operator=(ScrollContainer&&) noexcept = default;

	// Scroll control
	void  scrollTo(float y);
	void  scrollBy(float delta);
	void  scrollToTop();
	void  scrollToBottom();
	float getScrollPosition() const { return scrollY; }
	float getMaxScroll() const { return maxScroll; }

	// Content management
	void  setContentHeight(float height);
	float getContentHeight() const { return contentHeight; }

	// Viewport size (can be changed after construction)
	void setViewportSize(Foundation::Vec2 newSize);

	// Position update
	void setPosition(float x, float y) override;

	// ILayer overrides
	void update(float deltaTime) override;
	void render() override;

	// IComponent overrides
	bool handleEvent(InputEvent& event) override;
	bool containsPoint(Foundation::Vec2 point) const override;

  private:
	// Layout constants (internal, use Theme colors)
	static constexpr float kScrollbarWidth = 8.0F;
	static constexpr float kMinThumbHeight = 20.0F;
	static constexpr float kScrollSpeed = 40.0F; // Pixels per scroll tick

	Foundation::Vec2 viewportSize;
	float			 scrollY{0.0F};
	float			 contentHeight{0.0F};
	float			 maxScroll{0.0F};
	bool			 contentHeightSet{false}; // True if manually set

	// Scrollbar state
	bool  isDraggingThumb{false};
	float dragStartY{0.0F};
	float dragStartScroll{0.0F};

	// Scrollbar geometry (computed)
	float scrollbarX{0.0F};
	float scrollbarY{0.0F};
	float trackHeight{0.0F};
	float thumbHeight{0.0F};
	float thumbY{0.0F};

	void updateScrollBounds();
	void updateScrollbar();
	void updateClipAndOffset();

	// Check if point is over the scrollbar thumb
	bool isPointOverThumb(Foundation::Vec2 point) const;
	bool isPointOverTrack(Foundation::Vec2 point) const;
};

} // namespace UI
