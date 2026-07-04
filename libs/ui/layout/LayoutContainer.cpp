#include "layout/LayoutContainer.h"

#include <algorithm>

namespace UI {

namespace {

	float mainSize(bool vertical, const IComponent& child) {
		return vertical ? child.getHeight() : child.getWidth();
	}

	float crossSize(bool vertical, const IComponent& child) {
		return vertical ? child.getWidth() : child.getHeight();
	}

	SizeMode mainMode(bool vertical, const IComponent& child) {
		return vertical ? child.heightMode : child.widthMode;
	}

	SizeMode crossMode(bool vertical, const IComponent& child) {
		return vertical ? child.widthMode : child.heightMode;
	}

	void assignMain(bool vertical, IComponent& child, float value) {
		if (vertical) {
			child.setLayoutSize(kSizeKeep, value);
		} else {
			child.setLayoutSize(value, kSizeKeep);
		}
	}

	void assignCross(bool vertical, IComponent& child, float value) {
		if (vertical) {
			child.setLayoutSize(value, kSizeKeep);
		} else {
			child.setLayoutSize(kSizeKeep, value);
		}
	}

} // namespace

LayoutContainer::LayoutContainer(const Args& args)
	: direction(args.direction),
	  gap(args.gap),
	  padding(args.padding),
	  distribution(args.distribution),
	  crossAlign(args.crossAlign),
	  id(args.id) {
	position = args.position;
	size = args.size;
	margin = args.margin;
	widthMode = size.x > 0.0F ? SizeMode::Fixed : SizeMode::Hug;
	heightMode = size.y > 0.0F ? SizeMode::Fixed : SizeMode::Hug;
	widthDefinite = widthMode == SizeMode::Fixed;
	heightDefinite = heightMode == SizeMode::Fixed;
}

void LayoutContainer::update(float deltaTime) {
	Container::update(deltaTime);
}

void LayoutContainer::render() {
	if (layoutDirty) {
		computeLayout();
		layoutDirty = false;
	}
	Container::render();
}

void LayoutContainer::layout(const Foundation::Rect& bounds) {
	setPosition(bounds.x, bounds.y);
	const float w = bounds.width > 0.0F ? std::max(bounds.width - margin * 2.0F, 0.0F) : kSizeKeep;
	const float h = bounds.height > 0.0F ? std::max(bounds.height - margin * 2.0F, 0.0F) : kSizeKeep;
	setLayoutSize(w, h);
}

void LayoutContainer::setLayoutSize(float w, float h) {
	if (w >= 0.0F && (!widthDefinite || size.x != w)) {
		widthDefinite = true;
		size.x = w;
		invalidateLayout();
	}
	if (h >= 0.0F && (!heightDefinite || size.y != h)) {
		heightDefinite = true;
		size.y = h;
		invalidateLayout();
	}
}

float LayoutContainer::getWidth() const {
	if (widthDefinite) {
		return size.x + margin * 2.0F;
	}
	const float content = direction == Direction::Vertical ? hugCrossContent() : hugMainContent();
	return content + padding.horizontal() + margin * 2.0F;
}

float LayoutContainer::getHeight() const {
	if (heightDefinite) {
		return size.y + margin * 2.0F;
	}
	const float content = direction == Direction::Vertical ? hugMainContent() : hugCrossContent();
	return content + padding.vertical() + margin * 2.0F;
}

float LayoutContainer::hugMainContent() const {
	const bool vertical = direction == Direction::Vertical;
	float	   total = 0.0F;
	int		   count = 0;
	for (const auto* child : children) {
		if (!child->visible) {
			continue;
		}
		total += mainSize(vertical, *child);
		count++;
	}
	if (count > 1) {
		total += gap * static_cast<float>(count - 1);
	}
	return total;
}

float LayoutContainer::hugCrossContent() const {
	const bool vertical = direction == Direction::Vertical;
	float	   maxCross = 0.0F;
	for (const auto* child : children) {
		if (!child->visible) {
			continue;
		}
		maxCross = std::max(maxCross, crossSize(vertical, *child));
	}
	return maxCross;
}

void LayoutContainer::computeLayout() {
	resolveChildSizesIfDirty();
	positionChildren();
}

void LayoutContainer::resolveChildSizesIfDirty() {
	if (!childSizesDirty) {
		return;
	}
	childSizesDirty = false;
	resolveChildSizes();
}

void LayoutContainer::resolveChildSizes() {
	const bool vertical = direction == Direction::Vertical;

	// Nested containers resolve first so hug measurements below are current
	for (auto* child : children) {
		if (!child->visible) {
			continue;
		}
		if (auto* nested = dynamic_cast<LayoutContainer*>(child)) {
			nested->resolveChildSizesIfDirty();
		}
	}

	// Cross pass: Stretch/Fill children adopt the content box cross size
	const float sizeCross = vertical ? size.x : size.y;
	const bool	crossDef = vertical ? widthDefinite : heightDefinite;
	const float paddingCross = vertical ? padding.horizontal() : padding.vertical();
	const float contentCross = crossDef ? std::max(sizeCross - paddingCross, 0.0F) : hugCrossContent();
	for (auto* child : children) {
		if (!child->visible) {
			continue;
		}
		const SizeMode mode = crossMode(vertical, *child);
		const bool	   stretched =
			mode == SizeMode::Fill || (crossAlign == CrossAlign::Stretch && mode != SizeMode::Fixed);
		if (!stretched) {
			continue;
		}
		assignCross(vertical, *child, std::max(contentCross - child->margin * 2.0F, 0.0F));
		// A nested container's main size can depend on its new cross size
		// (wrapping text), so re-resolve before the main measurement below
		if (auto* nested = dynamic_cast<LayoutContainer*>(child)) {
			nested->resolveChildSizesIfDirty();
		}
	}

	// Main pass: leftover space goes to Fill children by fillWeight.
	// A Hug main axis has no leftover; Fill children keep their intrinsic size.
	// A main axis resolved to zero IS definite: Fill children get 0.
	const float sizeMain = vertical ? size.y : size.x;
	const bool	mainDef = vertical ? heightDefinite : widthDefinite;
	if (!mainDef) {
		return;
	}
	const float paddingMain = vertical ? padding.vertical() : padding.horizontal();
	const float contentMain = std::max(sizeMain - paddingMain, 0.0F);

	float used = 0.0F;
	float totalWeight = 0.0F;
	int	  count = 0;
	for (const auto* child : children) {
		if (!child->visible) {
			continue;
		}
		count++;
		if (mainMode(vertical, *child) == SizeMode::Fill) {
			totalWeight += std::max(child->fillWeight, 0.0F);
			used += child->margin * 2.0F;
		} else {
			used += mainSize(vertical, *child);
		}
	}
	if (totalWeight <= 0.0F) {
		return;
	}
	const float gaps = count > 1 ? gap * static_cast<float>(count - 1) : 0.0F;
	const float leftover = std::max(contentMain - used - gaps, 0.0F);
	for (auto* child : children) {
		if (!child->visible || mainMode(vertical, *child) != SizeMode::Fill) {
			continue;
		}
		assignMain(vertical, *child, leftover * std::max(child->fillWeight, 0.0F) / totalWeight);
	}
}

void LayoutContainer::positionChildren() {
	const bool			   vertical = direction == Direction::Vertical;
	const Foundation::Vec2 contentOrigin{position.x + margin + padding.left, position.y + margin + padding.top};

	const float sizeMain = vertical ? size.y : size.x;
	const bool	mainDef = vertical ? heightDefinite : widthDefinite;
	const float paddingMain = vertical ? padding.vertical() : padding.horizontal();
	const float contentMain = mainDef ? std::max(sizeMain - paddingMain, 0.0F) : hugMainContent();
	const float sizeCross = vertical ? size.x : size.y;
	const bool	crossDef = vertical ? widthDefinite : heightDefinite;
	const float paddingCross = vertical ? padding.horizontal() : padding.vertical();
	const float contentCross = crossDef ? std::max(sizeCross - paddingCross, 0.0F) : hugCrossContent();

	float totalMain = 0.0F;
	int	  count = 0;
	for (const auto* child : children) {
		if (!child->visible) {
			continue;
		}
		totalMain += mainSize(vertical, *child);
		count++;
	}
	if (count == 0) {
		return;
	}

	const float gaps = gap * static_cast<float>(count - 1);
	const float leftover = std::max(contentMain - totalMain - gaps, 0.0F);

	float offset = 0.0F;
	float between = 0.0F;
	switch (distribution) {
		case Distribution::Start:
			break;
		case Distribution::Center:
			offset = leftover * 0.5F;
			break;
		case Distribution::End:
			offset = leftover;
			break;
		case Distribution::SpaceBetween:
			if (count > 1) {
				between = leftover / static_cast<float>(count - 1);
			}
			break;
		case Distribution::SpaceAround:
			between = leftover / static_cast<float>(count);
			offset = between * 0.5F;
			break;
		case Distribution::SpaceEvenly:
			between = leftover / static_cast<float>(count + 1);
			offset = between;
			break;
	}

	const float crossOrigin = vertical ? contentOrigin.x : contentOrigin.y;
	float		mainPos = (vertical ? contentOrigin.y : contentOrigin.x) + offset;
	for (auto* child : children) {
		if (!child->visible) {
			continue;
		}
		const float childMain = mainSize(vertical, *child);
		const float childCross = crossSize(vertical, *child);

		float crossPos = crossOrigin;
		switch (crossAlign) {
			case CrossAlign::Start:
			case CrossAlign::Stretch:
				break;
			case CrossAlign::Center:
				crossPos += std::max((contentCross - childCross) * 0.5F, 0.0F);
				break;
			case CrossAlign::End:
				crossPos += std::max(contentCross - childCross, 0.0F);
				break;
		}

		const float x = vertical ? crossPos : mainPos;
		const float y = vertical ? mainPos : crossPos;
		child->setPosition(x, y);
		if (auto* nested = dynamic_cast<LayoutContainer*>(child)) {
			nested->layout(Foundation::Rect{x, y, child->getWidth(), child->getHeight()});
		}
		mainPos += childMain + gap + between;
	}
}

} // namespace UI
