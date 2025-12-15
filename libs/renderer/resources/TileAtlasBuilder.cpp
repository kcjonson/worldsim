#include "resources/TileAtlasBuilder.h"

#include "resources/TilePatternBaker.h"

#include <utils/Log.h>
#include <utils/ResourcePath.h>

#include <algorithm>

namespace Renderer {

TileAtlasBuilder::TileAtlasBuilder(const Config& config) : config_(config) {}

std::vector<glm::vec4> TileAtlasBuilder::buildForSurfaces(
	int surfaceCount,
	const SurfaceNameFn& surfaceNameFn,
	const SurfaceColorFn& surfaceColorFn) {

	atlas_ = std::make_unique<TileTextureAtlas>(config_.atlasSize);
	std::vector<glm::vec4> rects;
	rects.reserve(static_cast<size_t>(surfaceCount));

	for (int i = 0; i < surfaceCount; ++i) {
		auto region = atlas_->allocate(config_.patternSize, config_.patternSize);
		if (!region.valid) {
			LOG_WARNING(
				Renderer,
				"Tile atlas (%dx%d) ran out of space at surface %d. "
				"Consider increasing atlas size or reducing pattern dimensions.",
				config_.atlasSize,
				config_.atlasSize,
				i);
			break;
		}

		std::vector<uint8_t> pixels;
		bool baked = false;

		// Attempt to bake SVG if present
		std::string surfaceName = surfaceNameFn(i);
		baked = loadSvgPattern(surfaceName, pixels);

		if (!baked) {
			// Fallback checker pattern using surface color
			Foundation::Color color = surfaceColorFn(i);
			generateFallbackPattern(color, pixels, region.width, region.height);
		}

		if (!atlas_->upload(region, pixels.data())) {
			LOG_WARNING(Renderer, "Failed to upload texture for surface %d", i);
		}

		rects.push_back(calculateUVRect(region));
	}

	return rects;
}

GLuint TileAtlasBuilder::texture() const {
	return atlas_ ? atlas_->texture() : 0;
}

bool TileAtlasBuilder::loadSvgPattern(const std::string& surfaceName, std::vector<uint8_t>& pixels) {
	std::string svgPath = Foundation::findResourceString("assets/tiles/surfaces/" + surfaceName + "/pattern.svg");
	if (svgPath.empty()) {
		return false;
	}

	return bakeSvgToRgba(svgPath, config_.patternSize, config_.patternSize, pixels);
}

void TileAtlasBuilder::generateFallbackPattern(
	const Foundation::Color& c,
	std::vector<uint8_t>& pixels,
	int width,
	int height) {

	pixels.assign(static_cast<size_t>(width * height * 4), 255);

	auto r = static_cast<uint8_t>(std::clamp(c.r, 0.0F, 1.0F) * 255.0F);
	auto g = static_cast<uint8_t>(std::clamp(c.g, 0.0F, 1.0F) * 255.0F);
	auto b = static_cast<uint8_t>(std::clamp(c.b, 0.0F, 1.0F) * 255.0F);
	auto a = static_cast<uint8_t>(std::clamp(c.a, 0.0F, 1.0F) * 255.0F);

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			auto idx = static_cast<size_t>((y * width + x) * 4);
			bool checker = ((x / 8) + (y / 8)) % 2 == 0;
			float shade = checker ? 1.05F : 0.85F;
			pixels[idx + 0] = static_cast<uint8_t>(std::clamp(static_cast<float>(r) * shade, 0.0F, 255.0F));
			pixels[idx + 1] = static_cast<uint8_t>(std::clamp(static_cast<float>(g) * shade, 0.0F, 255.0F));
			pixels[idx + 2] = static_cast<uint8_t>(std::clamp(static_cast<float>(b) * shade, 0.0F, 255.0F));
			pixels[idx + 3] = a;
		}
	}
}

glm::vec4 TileAtlasBuilder::calculateUVRect(const AtlasRegion& region) const {
	float invSize = 1.0F / static_cast<float>(atlas_->size());
	float u0 = static_cast<float>(region.x) * invSize;
	float v0 = static_cast<float>(region.y) * invSize;
	float u1 = static_cast<float>(region.x + region.width) * invSize;
	float v1 = static_cast<float>(region.y + region.height) * invSize;
	return {u0, v0, u1, v1};
}

} // namespace Renderer
