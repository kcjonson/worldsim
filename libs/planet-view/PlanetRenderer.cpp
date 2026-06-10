#include "PlanetRenderer.h"

#include "OrbitCamera.h"
#include "PlanetColorizer.h"
#include "PlanetMesh.h"

#include <shader/ShaderLoader.h>
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

void PlanetRenderer::destroyFbo() {
    if (fbo)      { glDeleteFramebuffers(1,  &fbo);      fbo      = 0; }
    if (colorTex) { glDeleteTextures(1,      &colorTex); colorTex = 0; }
    if (depthRbo) { glDeleteRenderbuffers(1, &depthRbo); depthRbo = 0; }
}

void PlanetRenderer::render(const PlanetMesh& mesh, const PlanetColorizer& colorizer,
                            const OrbitCamera& camera, int widthPx, int heightPx) {
    if (!isReady() || !mesh.isBuilt() || !colorizer.isReady()) return;

    resize(widthPx, heightPx);

    // ── Save all GL state we'll touch ──
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    GLint prevViewport[4] = {};
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLboolean prevDepthTest  = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevCullFace   = glIsEnabled(GL_CULL_FACE);
    GLboolean prevBlend      = glIsEnabled(GL_BLEND);
    GLint prevDepthFunc      = 0;
    glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
    GLint prevProgram        = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);

    // ── Planet pass ──
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, fboWidth, fboHeight);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    glClearColor(0.0F, 0.0F, 0.02F, 1.0F); // near-black space
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspect = (fboHeight > 0) ? (static_cast<float>(fboWidth) / static_cast<float>(fboHeight)) : 1.0F;
    glm::mat4 mvp   = camera.mvpMatrix(aspect);
    glm::mat4 model = glm::mat4(1.0F);
    glm::vec3 camPos = camera.position();

    glUseProgram(planetShader);

    glUniformMatrix4fv(glGetUniformLocation(planetShader, "u_mvp"),   1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix4fv(glGetUniformLocation(planetShader, "u_model"), 1, GL_FALSE, glm::value_ptr(model));
    glUniform3fv(glGetUniformLocation(planetShader, "u_sunDir"),    1, glm::value_ptr(sunDir));
    glUniform3fv(glGetUniformLocation(planetShader, "u_cameraPos"), 1, glm::value_ptr(camPos));
    glUniform1i(glGetUniformLocation(planetShader, "u_colorTex"), 0);

    for (uint32_t r = 0; r < 10U; ++r) {
        const auto& rm = mesh.rhombus(r);
        if (!rm.vao) continue;

        colorizer.bind(r, 0);

        glBindVertexArray(rm.vao);
        glDrawElements(GL_TRIANGLES,
                       static_cast<GLsizei>(mesh.indexCount),
                       GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // ── Restore state ──
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevCullFace)  glEnable(GL_CULL_FACE);  else glDisable(GL_CULL_FACE);
    if (prevBlend)     glEnable(GL_BLEND);       else glDisable(GL_BLEND);
    glDepthFunc(static_cast<GLenum>(prevDepthFunc));
    glUseProgram(static_cast<GLuint>(prevProgram));
}

void PlanetRenderer::blitToScreen(int widthPx, int heightPx) {
    if (!isReady() || !colorTex) return;

    // Save relevant state.
    GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevBlend     = glIsEnabled(GL_BLEND);
    GLint prevProgram       = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    GLint prevVao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(blitShader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glUniform1i(glGetUniformLocation(blitShader, "u_tex"), 0);

    glBindVertexArray(blitVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(static_cast<GLuint>(prevVao));

    glBindTexture(GL_TEXTURE_2D, 0);

    // Restore state.
    if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevBlend)     glEnable(GL_BLEND);       else glDisable(GL_BLEND);
    glUseProgram(static_cast<GLuint>(prevProgram));

    (void)widthPx; (void)heightPx; // viewport already set by caller
}

} // namespace planetview
