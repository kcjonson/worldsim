#pragma once

#include <GL/glew.h>
#include <cstdint>

namespace Renderer {

// Simple FBO wrapper for render-to-texture use cases (tile atlas baking, tests).
class RenderToTexture {
  public:
	RenderToTexture(int width, int height);
	~RenderToTexture();

	RenderToTexture(const RenderToTexture&) = delete;
	RenderToTexture& operator=(const RenderToTexture&) = delete;

	RenderToTexture(RenderToTexture&& other) noexcept;
	RenderToTexture& operator=(RenderToTexture&& other) noexcept;

	// Bind FBO and set viewport to texture dimensions. Saves previous FBO/viewport.
	void begin();

	// Restore previous FBO/viewport bindings.
	void end();

	// Get the color texture handle (GL_TEXTURE_2D, RGBA8).
	[[nodiscard]] GLuint texture() const { return texture_; }
	[[nodiscard]] int width() const { return width_; }
	[[nodiscard]] int height() const { return height_; }

  private:
	int width_ = 0;
	int height_ = 0;
	GLuint fbo_ = 0;
	GLuint texture_ = 0;
	GLint prevViewport_[4] = {0, 0, 0, 0};
	GLint prevFbo_ = 0;
	bool inUse_ = false;

	void destroy();
};

} // namespace Renderer
