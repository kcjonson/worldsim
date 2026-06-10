#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <cstdint>

namespace planetview {

class PlanetMesh;
class PlanetColorizer;
class OrbitCamera;

// Owns the offscreen FBO (RGBA8 colour + DEPTH24 renderbuffer), planet shader,
// and a blit shader for compositing the FBO onto the default framebuffer.
//
// Compositing strategy: render() draws the planet into the FBO, then blitToScreen()
// copies that texture to the backbuffer via a full-screen triangle BEFORE Primitives
// draws 2D UI — making the planet always the bottom layer.
//
// All touched GL state is saved and restored around each pass.
class PlanetRenderer {
  public:
    PlanetRenderer() = default;
    ~PlanetRenderer();

    PlanetRenderer(const PlanetRenderer&) = delete;
    PlanetRenderer& operator=(const PlanetRenderer&) = delete;

    // Must be called once with a live GL context.
    // shaderDir: directory that contains planet.vert / planet.frag / blit.vert / blit.frag.
    bool init(const char* shaderDir, int widthPx, int heightPx);

    // Resize the FBO when the window changes.
    void resize(int widthPx, int heightPx);

    // Draw the planet into the offscreen FBO.
    void render(const PlanetMesh& mesh, const PlanetColorizer& colorizer,
                const OrbitCamera& camera, int widthPx, int heightPx);

    // Blit the FBO colour texture to the currently bound default framebuffer.
    // Call this BEFORE 2D UI rendering so the planet is below the UI.
    void blitToScreen(int widthPx, int heightPx);

    // Expose colour texture for custom compositing if needed.
    GLuint colorTexture() const { return colorTex; }

    bool isReady() const { return planetShader != 0 && blitShader != 0 && fbo != 0; }

  private:
    GLuint fbo{0};
    GLuint colorTex{0};
    GLuint depthRbo{0};

    GLuint planetShader{0};
    GLuint blitShader{0};
    GLuint blitVao{0}; // empty VAO for gl_VertexID full-screen triangle

    int fboWidth{0};
    int fboHeight{0};

    // Sun direction (world space, unit vector).
    glm::vec3 sunDir{0.6F, 0.4F, 0.7F};

    // Cached uniform locations (queried once after shader link).
    struct PlanetUniforms {
        GLint mvp      {-1};
        GLint model    {-1};
        GLint sunDir   {-1};
        GLint cameraPos{-1};
        GLint colorTex {-1};
    } planetUniforms;

    struct BlitUniforms {
        GLint tex{-1};
    } blitUniforms;

    void destroyFbo();
    bool createFbo(int w, int h);
    void cacheUniforms();
};

} // namespace planetview
