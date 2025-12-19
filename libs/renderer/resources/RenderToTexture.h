#pragma once

#include "gl/GLFramebuffer.h"
#include "gl/GLTexture.h"
#include <GL/glew.h>
#include <cstdint>

namespace Renderer {

// Simple FBO wrapper for render-to-texture use cases (tile atlas baking, tests).
// Uses RAII for automatic GPU resource cleanup.
class RenderToTexture {
  public:
	RenderToTexture(int width, int height);
	~RenderToTexture() = default;

	RenderToTexture(const RenderToTexture&) = delete;
	RenderToTexture& operator=(const RenderToTexture&) = delete;

	RenderToTexture(RenderToTexture&&) noexcept = default;
	RenderToTexture& operator=(RenderToTexture&&) noexcept = default;

	// Bind FBO and set viewport to texture dimensions. Saves previous FBO/viewport.
	void begin();

	// Restore previous FBO/viewport bindings.
	void end();

	// Get the color texture handle (GL_TEXTURE_2D, RGBA8).
	[[nodiscard]] GLuint texture() const { return texture_; }
	[[nodiscard]] int width() const { return texture_.width(); }
	[[nodiscard]] int height() const { return texture_.height(); }

  private:
	GLFramebuffer fbo_;		 // RAII framebuffer wrapper
	GLTexture texture_;		 // RAII texture wrapper
	GLint prevViewport_[4] = {0, 0, 0, 0};
	GLint prevFbo_ = 0;
	bool inUse_ = false;
};

} // namespace Renderer
