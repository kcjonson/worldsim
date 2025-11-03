#include "shapes/shapes.h"
#include "primitives/primitives.h"

namespace UI {

void Rectangle::Render() const {
	Renderer::Primitives::DrawRect({.bounds = {position.x, position.y, size.x, size.y},
									.style	= style,
									.id		= id});
}

void Circle::Render() const {
	// TODO: Add DrawCircle to Primitives API
	// For now, render as a square (placeholder)
	Renderer::Primitives::DrawRect({.bounds = {center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f},
									.style	= {.fill = color},
									.id		= id});
}

void Line::Render() const {
	Renderer::Primitives::DrawLine({.start = start, .end = end, .style = style, .id = id});
}

void Text::Render() const {
	// TODO: Add DrawText to Primitives API
	// For now, render as a small rectangle (placeholder)
	Renderer::Primitives::DrawRect({.bounds = {position.x, position.y, 100.0f, 20.0f},
									.style	= {.fill = color},
									.id		= id});
}

} // namespace UI
