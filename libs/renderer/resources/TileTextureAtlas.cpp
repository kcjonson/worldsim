#include "resources/TileTextureAtlas.h"

#include <algorithm>
#include <stdexcept>

namespace Renderer {

TileTextureAtlas::TileTextureAtlas(int atlasSize) : size_(atlasSize) {
	glGenTextures(1, &texture_);
	glBindTexture(GL_TEXTURE_2D, texture_);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size_, size_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glBindTexture(GL_TEXTURE_2D, 0);
}

TileTextureAtlas::~TileTextureAtlas() {
	destroy();
}

TileTextureAtlas::TileTextureAtlas(TileTextureAtlas&& other) noexcept {
	*this = std::move(other);
}

TileTextureAtlas& TileTextureAtlas::operator=(TileTextureAtlas&& other) noexcept {
	if (this == &other) {
		return *this;
	}
	destroy();
	size_ = other.size_;
	cursorX_ = other.cursorX_;
	cursorY_ = other.cursorY_;
	currentRowHeight_ = other.currentRowHeight_;
	texture_ = other.texture_;
	other.size_ = 0;
	other.cursorX_ = 0;
	other.cursorY_ = 0;
	other.currentRowHeight_ = 0;
	other.texture_ = 0;
	return *this;
}

AtlasRegion TileTextureAtlas::allocate(int width, int height) {
	if (width <= 0 || height <= 0 || width > size_ || height > size_) {
		return {};
	}

	if (cursorX_ + width > size_) {
		cursorX_ = 0;
		cursorY_ += currentRowHeight_;
		currentRowHeight_ = 0;
	}

	if (cursorY_ + height > size_) {
		return {};
	}

	AtlasRegion region{cursorX_, cursorY_, width, height, true};
	cursorX_ += width;
	currentRowHeight_ = std::max(currentRowHeight_, height);
	return region;
}

bool TileTextureAtlas::upload(const AtlasRegion& region, const uint8_t* rgbaData) {
	if (!region.valid || texture_ == 0 || rgbaData == nullptr) {
		return false;
	}
	glBindTexture(GL_TEXTURE_2D, texture_);
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
	glBindTexture(GL_TEXTURE_2D, 0);
	return true;
}

void TileTextureAtlas::destroy() {
	if (texture_ != 0) {
		glDeleteTextures(1, &texture_);
		texture_ = 0;
	}
}

} // namespace Renderer
