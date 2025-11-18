#include "shapes/shapes.h"
#include "primitives/primitives.h"

namespace ui {

	void Rectangle::Render() const {
		Renderer::Primitives::DrawRect({.bounds = {this->position.x, this->position.y, this->size.x, this->size.y}, .style = this->style, .id = this->id});
	}

	void Circle::Render() const {
		// TODO: Add DrawCircle to Primitives API
		// For now, render as a square (placeholder)
		Renderer::Primitives::DrawRect(
			{.bounds = {this->center.x - this->radius, this->center.y - this->radius, this->radius * 2.0F, this->radius * 2.0F}, .style = {.fill = this->color}, .id = this->id}
		);
	}

	void Line::Render() const {
		Renderer::Primitives::DrawLine({.start = this->start, .end = this->end, .style = this->style, .id = this->id});
	}

	void Text::Render() const {
		// TODO: Add DrawText to Primitives API
		// For now, render as a small rectangle (placeholder)
		Renderer::Primitives::DrawRect({.bounds = {this->position.x, this->position.y, 100.0F, 20.0F}, .style = {.fill = this->color}, .id = this->id});
	}

} // namespace ui
