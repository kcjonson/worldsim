#include "resources/RenderToTexture.h"

#include <stdexcept>

namespace Renderer {

RenderToTexture::RenderToTexture(int width, int height) : width_(width), height_(height) {
	glGenTextures(1, &texture_);
	glBindTexture(GL_TEXTURE_2D, texture_);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenFramebuffers(1, &fbo_);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		destroy();
		throw std::runtime_error("RenderToTexture FBO incomplete");
	}
}

RenderToTexture::~RenderToTexture() {
	destroy();
}

RenderToTexture::RenderToTexture(RenderToTexture&& other) noexcept {
	*this = std::move(other);
}

RenderToTexture& RenderToTexture::operator=(RenderToTexture&& other) noexcept {
	if (this == &other) {
		return *this;
	}
	destroy();
	width_ = other.width_;
	height_ = other.height_;
	fbo_ = other.fbo_;
	texture_ = other.texture_;
	inUse_ = false;
	other.width_ = 0;
	other.height_ = 0;
	other.fbo_ = 0;
	other.texture_ = 0;
	return *this;
}

void RenderToTexture::begin() {
	if (inUse_) {
		return;
	}
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo_);
	glGetIntegerv(GL_VIEWPORT, prevViewport_);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
	glViewport(0, 0, width_, height_);
	inUse_ = true;
}

void RenderToTexture::end() {
	if (!inUse_) {
		return;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo_));
	glViewport(prevViewport_[0], prevViewport_[1], prevViewport_[2], prevViewport_[3]);
	inUse_ = false;
}

void RenderToTexture::destroy() {
	if (fbo_ != 0) {
		glDeleteFramebuffers(1, &fbo_);
		fbo_ = 0;
	}
	if (texture_ != 0) {
		glDeleteTextures(1, &texture_);
		texture_ = 0;
	}
}

} // namespace Renderer
