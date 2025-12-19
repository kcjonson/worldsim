#include "resources/RenderToTexture.h"

#include <stdexcept>

namespace Renderer {

RenderToTexture::RenderToTexture(int width, int height)
	// Create texture using RAII wrapper (sets up filtering and wrapping)
	: texture_(width, height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, nullptr)
	, fbo_(GLFramebuffer::create()) {

	// Unbind texture after GLTexture constructor (it leaves it bound)
	GLTexture::unbind();

	// Attach texture to framebuffer
	fbo_.bind();
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	GLFramebuffer::unbind();
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		// RAII wrappers will clean up automatically when exception is thrown
		throw std::runtime_error("RenderToTexture FBO incomplete");
	}
}

void RenderToTexture::begin() {
	if (inUse_) {
		return;
	}
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo_);
	glGetIntegerv(GL_VIEWPORT, prevViewport_);
	fbo_.bind();
	glViewport(0, 0, texture_.width(), texture_.height());
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

} // namespace Renderer
