#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <cstdint>

namespace planetview {

class PlanetMesh;
class PlanetColorizer;
class OrbitCamera;

// Where render() composites the planet: a rectangle of the window's framebuffer
// in pixels, origin bottom-left as GL's, and the window framebuffer's size.
struct ScreenArea {
    int x{0};
    int y{0};
    int width{0};
    int height{0};
    int windowWidth{0};
    int windowHeight{0};
};

// Owns the offscreen FBO (RGBA8 colour + DEPTH24 renderbuffer), planet shader,
// and a blit shader for compositing the FBO onto the window's framebuffer.
//
// Compositing strategy: render() draws the planet into the FBO over an alpha-0
// clear, then alpha-composites that texture onto the window via a full-screen
// triangle BEFORE Primitives draws 2D UI: the planet disc lands on whatever the
// scene painted first (e.g. a starfield) and 2D UI still batches on top, so
// flush anything that must sit beneath the planet first.
//
// GL state follows salal's renderer contract: every renderer draw makes its own
// bindings, and Primitives::invalidateState() puts back its fixed state (depth
// test, culling and scissor off). render() sets what its passes need and puts
// back by hand only what the renderer never rebinds on its own: the window's
// framebuffer and its viewport. Call it with no RenderTarget begun.
class PlanetRenderer {
  public:
    PlanetRenderer() = default;
    ~PlanetRenderer();

    PlanetRenderer(const PlanetRenderer&) = delete;
    PlanetRenderer& operator=(const PlanetRenderer&) = delete;

    // Must be called once with a live GL context.
    // shaderDir: directory that contains planet.vert / planet.frag / blit.vert / blit.frag.
    bool init(const char* shaderDir, int widthPx, int heightPx);

    // Draw the planet offscreen at area's size and composite it onto the window
    // over `area`. `subdivision` is the grid n, which drives the shader's per-pixel
    // hex assignment (cell fill, AA edges, and outline). `alpha` scales the whole
    // disc (for fade-in); pixels outside the disc leave the window untouched.
    void render(const PlanetMesh& mesh, const PlanetColorizer& colorizer,
                uint32_t subdivision, const OrbitCamera& camera,
                const ScreenArea& area, float alpha = 1.0F);

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
        GLint mvp       {-1};
        GLint model     {-1};
        GLint sunDir    {-1};
        GLint cameraPos {-1};
        GLint baseTex   {-1};
        GLint n         {-1};
    } planetUniforms;

    struct BlitUniforms {
        GLint tex{-1};
        GLint alpha{-1};
    } blitUniforms;

    void drawPlanet(const PlanetMesh& mesh, const PlanetColorizer& colorizer,
                    uint32_t subdivision, const OrbitCamera& camera);
    void composite(const ScreenArea& area, float alpha);
    void resize(int widthPx, int heightPx);
    void destroyFbo();
    bool createFbo(int w, int h);
    void cacheUniforms();
};

} // namespace planetview
