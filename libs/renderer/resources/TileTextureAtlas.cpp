#include "resources/TileTextureAtlas.h"

#include <algorithm>

namespace Renderer {

TileTextureAtlas::TileTextureAtlas(int atlasSize)
	// Create square texture using RAII wrapper (sets up filtering and wrapping)
	: texture_(atlasSize, atlasSize, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, nullptr) {
	// Unbind texture after GLTexture constructor (it leaves it bound)
	GLTexture::unbind();
}

AtlasRegion TileTextureAtlas::allocate(int width, int height) {
	int atlasSize = texture_.width();
	if (width <= 0 || height <= 0 || width > atlasSize || height > atlasSize) {
		return {};
	}

	if (cursorX_ + width > atlasSize) {
		cursorX_ = 0;
		cursorY_ += currentRowHeight_;
		currentRowHeight_ = 0;
	}

	if (cursorY_ + height > atlasSize) {
		return {};
	}

	AtlasRegion region{cursorX_, cursorY_, width, height, true};
	cursorX_ += width;
	currentRowHeight_ = std::max(currentRowHeight_, height);
	return region;
}

bool TileTextureAtlas::upload(const AtlasRegion& region, const uint8_t* rgbaData) {
	if (!region.valid || !texture_.isValid() || rgbaData == nullptr) {
		return false;
	}
	texture_.bind();
	glTexSubImage2D(
		GL_TEXTURE_2D,
		0,
		region.x,
		region.y,
		region.width,
		region.height,
		GL_RGBA,
		GL_UNSIGNED_BYTE,
		rgbaData);
	GLTexture::unbind();
	return true;
}

} // namespace Renderer
