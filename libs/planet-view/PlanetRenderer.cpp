#include "PlanetRenderer.h"

#include "OrbitCamera.h"
#include "PlanetColorizer.h"
#include "PlanetMesh.h"

#include <backend/gl/ShaderLoader.h>
#include <primitives/Primitives.h>
#include <utils/Log.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <string>

namespace planetview {

PlanetRenderer::~PlanetRenderer() {
    destroyFbo();
    if (planetShader) { glDeleteProgram(planetShader); planetShader = 0; }
    if (blitShader)   { glDeleteProgram(blitShader);   blitShader   = 0; }
    if (blitVao)      { glDeleteVertexArrays(1, &blitVao); blitVao  = 0; }
}

bool PlanetRenderer::init(const char* shaderDir, int widthPx, int heightPx) {
    std::string dir(shaderDir);
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';

    std::string pVert = dir + "planet.vert";
    std::string pFrag = dir + "planet.frag";
    std::string bVert = dir + "blit.vert";
    std::string bFrag = dir + "blit.frag";

    planetShader = Renderer::ShaderLoader::loadShaderProgram(pVert.c_str(), pFrag.c_str());
    if (!planetShader) {
        LOG_ERROR(Renderer, "PlanetRenderer: failed to load planet shaders from %s", dir.c_str());
        return false;
    }

    blitShader = Renderer::ShaderLoader::loadShaderProgram(bVert.c_str(), bFrag.c_str());
    if (!blitShader) {
        LOG_ERROR(Renderer, "PlanetRenderer: failed to load blit shaders from %s", dir.c_str());
        return false;
    }

    glGenVertexArrays(1, &blitVao);

    sunDir = glm::normalize(glm::vec3(0.6F, 0.4F, 0.7F));

    cacheUniforms();

    return createFbo(widthPx, heightPx);
}

void PlanetRenderer::resize(int widthPx, int heightPx) {
    if (widthPx == fboWidth && heightPx == fboHeight) return;
    destroyFbo();
    createFbo(widthPx, heightPx);
}

bool PlanetRenderer::createFbo(int w, int h) {
    fboWidth  = w;
    fboHeight = h;

    // Colour texture.
    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Depth renderbuffer.
    glGenRenderbuffers(1, &depthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // FBO.
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRbo);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR(Renderer, "PlanetRenderer: FBO incomplete (status 0x%X)", status);
        destroyFbo();
        return false;
    }
    return true;
}

void PlanetRenderer::cacheUniforms() {
    planetUniforms.mvp          = glGetUniformLocation(planetShader, "u_mvp");
    planetUniforms.model        = glGetUniformLocation(planetShader, "u_model");
    planetUniforms.sunDir       = glGetUniformLocation(planetShader, "u_sunDir");
    planetUniforms.cameraPos    = glGetUniformLocation(planetShader, "u_cameraPos");
    planetUniforms.baseTex      = glGetUniformLocation(planetShader, "u_baseTex");
    planetUniforms.n            = glGetUniformLocation(planetShader, "u_n");

    blitUniforms.tex   = glGetUniformLocation(blitShader, "u_tex");
    blitUniforms.alpha = glGetUniformLocation(blitShader, "u_alpha");
}

void PlanetRenderer::destroyFbo() {
    if (fbo)      { glDeleteFramebuffers(1,  &fbo);      fbo      = 0; }
    if (colorTex) { glDeleteTextures(1,      &colorTex); colorTex = 0; }
    if (depthRbo) { glDeleteRenderbuffers(1, &depthRbo); depthRbo = 0; }
}

void PlanetRenderer::render(const PlanetMesh& mesh, const PlanetColorizer& colorizer,
                            uint32_t subdivision, const OrbitCamera& camera,
                            const ScreenArea& area, float alpha) {
    if (!isReady() || !mesh.isBuilt() || !colorizer.isReady()) return;
    if (area.width <= 0 || area.height <= 0) return;

    resize(area.width, area.height);
    drawPlanet(mesh, colorizer, subdivision, camera);
    // Depth test and culling back off, as the renderer and the composite both draw.
    Renderer::Primitives::invalidateState();
    composite(area, alpha);
}

void PlanetRenderer::drawPlanet(const PlanetMesh& mesh, const PlanetColorizer& colorizer,
                                uint32_t subdivision, const OrbitCamera& camera) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, fboWidth, fboHeight);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CCW); // mesh assumes CCW winding
    glDisable(GL_BLEND);

    // Use glClearBuffer* to avoid touching global clear-color state.
    // Alpha 0: composite() blends, so only the planet disc reaches the
    // window and the scene's own backdrop shows around it.
    const GLfloat clearColor[] = {0.0F, 0.0F, 0.02F, 0.0F};
    glClearBufferfv(GL_COLOR, 0, clearColor);
    const GLfloat clearDepth = 1.0F;
    glClearBufferfv(GL_DEPTH, 0, &clearDepth);

    float aspect = (fboHeight > 0) ? (static_cast<float>(fboWidth) / static_cast<float>(fboHeight)) : 1.0F;
    glm::mat4 mvp   = camera.mvpMatrix(aspect);
    glm::mat4 model = glm::mat4(1.0F);
    glm::vec3 camPos = camera.position();

    // Sun rides with the camera (offset up-right) so the facing hemisphere is
    // always inspectable; the terminator stays near the limb for depth cues.
    glm::vec3 toCam    = glm::normalize(camPos);
    glm::vec3 worldUp  = glm::abs(toCam.y) > 0.95F ? glm::vec3{0.0F, 0.0F, 1.0F}
                                                   : glm::vec3{0.0F, 1.0F, 0.0F};
    glm::vec3 camRight = glm::normalize(glm::cross(worldUp, toCam));
    sunDir = glm::normalize(toCam + 0.45F * camRight + 0.35F * worldUp);

    glUseProgram(planetShader);

    glUniformMatrix4fv(planetUniforms.mvp,       1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix4fv(planetUniforms.model,     1, GL_FALSE, glm::value_ptr(model));
    glUniform3fv(planetUniforms.sunDir,       1, glm::value_ptr(sunDir));
    glUniform3fv(planetUniforms.cameraPos,    1, glm::value_ptr(camPos));
    glUniform1i(planetUniforms.baseTex, 0);
    glUniform1f(planetUniforms.n, static_cast<float>(subdivision));

    for (uint32_t r = 0; r < 10U; ++r) {
        const auto& rm = mesh.rhombus(r);
        if (!rm.vao) continue;

        colorizer.bind(r, 0);

        glBindVertexArray(rm.vao);
        glDrawElements(GL_TRIANGLES,
                       static_cast<GLsizei>(mesh.indexCount),
                       GL_UNSIGNED_INT, nullptr);
    }
    // Unbound so later GL can't write into the rhombus VAOs or textures.
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void PlanetRenderer::composite(const ScreenArea& area, float alpha) {
    // The renderer never rebinds the window or its viewport between its draws.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(area.x, area.y, area.width, area.height);

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(blitShader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glUniform1i(blitUniforms.tex, 0);
    glUniform1f(blitUniforms.alpha, alpha);

    glBindVertexArray(blitVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glViewport(0, 0, area.windowWidth, area.windowHeight);
}

} // namespace planetview
