#include "scenes/shared/SalalUi.h"

#include <primitives/Primitives.h>

#include <array>

namespace world_sim {

	ui::Style text(const ui::Theme& theme, const Type& type) {
		const float size = theme.get(type.size);
		return {
			.color = theme.get(type.color),
			.fontFamily = theme.get(type.family),
			.fontSize = size,
			.textAlign = type.align,
			.letterSpacing = type.tracking ? size * theme.get(*type.tracking) : 0.0F,
			.textTransform = type.transform,
		};
	}

	std::optional<Foundation::Vec2> Diamond::intrinsicSize(std::optional<float> /*width*/, float /*availableWidth*/) const {
		return Foundation::Vec2{radius * 2.0F, radius * 2.0F};
	}

	void Diamond::render() {
		const Foundation::Vec2				  centre = getSize() * 0.5F;
		const std::array<Foundation::Vec2, 4> corners{{
			{centre.x, centre.y - radius},
			{centre.x + radius, centre.y},
			{centre.x, centre.y + radius},
			{centre.x - radius, centre.y},
		}};
		const std::array<uint16_t, 6>		  indices{0, 1, 2, 0, 2, 3};
		Foundation::Color					  accent = getContext()->getTheme().get(tokens::color::accent);
		accent.a *= opacity();
		Renderer::Primitives::drawTriangles({
			.vertices = corners.data(),
			.indices = indices.data(),
			.vertexCount = corners.size(),
			.indexCount = indices.size(),
			.color = accent,
			.key = drawKey(),
		});
	}

} // namespace world_sim
