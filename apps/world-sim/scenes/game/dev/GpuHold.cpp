#include "scenes/game/dev/GpuHold.h"

#include <backend/gl/GLFramebuffer.h>
#include <backend/gl/GLTexture.h>
#include <backend/gl/GLVertexArray.h>
#include <backend/gl/Shader.h>
#include <utils/Log.h>

#include <GL/glew.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace world_sim {

	namespace {

		constexpr int	kTargetSize = 512; // the offscreen target's side, in pixels
		constexpr int	kIterations = 128; // the fragment loop: a pass is about 55 us at P0 on a 3090
		constexpr float kMaxPasses = 1024.0F;
		constexpr float kMinPassCount = 0.25F; // below one pass draws nothing, and grows back from here
		constexpr float kMinTarget = 0.05F;
		constexpr float kMaxTarget = 0.95F;

		// Each frame the pass count moves by kGain of the load's relative error, at most kMaxStep either
		// way. The GPU timer's result is four frames old, and this stays stable under that lag.
		constexpr float kGain = 0.1F;
		constexpr float kMaxStep = 0.1F;
		// The frame interval is smoothed, and one interval moves it at most by half or double, so a
		// hitch barely shifts the budget while a view that is slow throughout still reaches its rate.
		constexpr float kSmoothing = 0.1F;

		// A triangle covering the target, one per instance, each instance a pass over every pixel.
		constexpr const char* kVertexSource = R"(#version 330 core
flat out int vPass;
void main() {
	vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
	gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
	vPass = gl_InstanceID;
}
)";

		// Arithmetic the compiler can't fold: it depends on the pixel, the pass and a uniform count.
		constexpr const char* kFragmentSource = R"(#version 330 core
uniform int uIterations;
flat in int vPass;
out vec4 fragColor;
void main() {
	vec4 v = vec4(gl_FragCoord.xy * (1.0 / 512.0), float(vPass) * (1.0 / 1024.0), 0.5);
	for (int i = 0; i < uIterations; ++i) {
		v = fract(v.yzwx * v * 1.618 + vec4(0.137, 0.271, 0.419, 0.563));
	}
	fragColor = v;
}
)";

		bool parseFloat(const std::string& text, float& out) {
			char*		end = nullptr;
			const float parsed = std::strtof(text.c_str(), &end);
			if (end == text.c_str() || *end != '\0' || !std::isfinite(parsed)) {
				return false;
			}
			out = parsed;
			return true;
		}

	} // namespace

	struct GpuHold::Gl {
		Renderer::Shader		shader;
		GLint					iterationsLocation = -1;
		Renderer::GLTexture		colour;
		Renderer::GLFramebuffer framebuffer;
		Renderer::GLVertexArray vertexArray; // empty: the vertex shader makes the triangle
	};

	GpuHold::GpuHold() = default;
	GpuHold::~GpuHold() = default;

	bool GpuHold::configure(const std::map<std::string, std::string>& params, std::string& error) {
		std::optional<bool>	 on;
		std::optional<float> fraction;
		if (const auto it = params.find("value"); it != params.end()) {
			if (it->second != "0" && it->second != "1") {
				error = "gpuhold: value must be 0 or 1";
				return false;
			}
			on = it->second == "1";
		}
		if (const auto it = params.find("target"); it != params.end()) {
			float parsed = 0.0F;
			if (!parseFloat(it->second, parsed) || parsed < kMinTarget || parsed > kMaxTarget) {
				error = "gpuhold: target must be a number from 0.05 to 0.95";
				return false;
			}
			fraction = parsed;
		}
		if (fraction) {
			target = *fraction;
		}
		if (on && *on != enabled) {
			enabled = *on;
			reset();
			LOG_INFO(Game, "GPU hold %s (target %.2f of the frame)", enabled ? "on" : "off", static_cast<double>(target));
		}
		return true;
	}

	void GpuHold::reset() {
		lastRender.reset();
		frameMs = 0.0F;
		passCount = 1.0F;
		passes = 0;
		lastSceneGpuMs = 0.0F;
		timer = Renderer::GPUTimer{}; // so a fresh run doesn't read the last one's result
	}

	bool GpuHold::createGl() {
		auto created = std::make_unique<Gl>();
		if (!created->shader.loadFromSource(kVertexSource, kFragmentSource)) {
			LOG_ERROR(Game, "GPU hold: the shader didn't build, so the hold stays off");
			return false;
		}
		created->iterationsLocation = glGetUniformLocation(created->shader.getProgram(), "uIterations");

		created->colour = Renderer::GLTexture(kTargetSize, kTargetSize, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		Renderer::GLTexture::unbind(); // the constructor leaves it on the active unit

		GLint previous = 0;
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous);
		created->framebuffer = Renderer::GLFramebuffer::create();
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, created->framebuffer);
		glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, created->colour, 0);
		const GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous));
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			LOG_ERROR(Game, "GPU hold: framebuffer incomplete (0x%x), so the hold stays off", status);
			return false;
		}

		created->vertexArray = Renderer::GLVertexArray::create();
		gl = std::move(created);
		return true;
	}

	void GpuHold::render(float sceneGpuMs) {
		if (!enabled) {
			return;
		}
		if (!gl && !createGl()) {
			enabled = false;
			return;
		}

		const auto now = std::chrono::steady_clock::now();
		if (lastRender) {
			const float intervalMs = std::chrono::duration<float, std::milli>(now - *lastRender).count();
			if (frameMs <= 0.0F) {
				frameMs = intervalMs;
			} else {
				frameMs += (std::clamp(intervalMs, frameMs * 0.5F, frameMs * 2.0F) - frameMs) * kSmoothing;
			}
		}
		lastRender = now;
		lastSceneGpuMs = sceneGpuMs;

		// Size this frame's work from the load of a few frames ago: the scene's GPU time plus the hold's.
		if (frameMs > 0.0F) {
			const float budgetMs = target * frameMs;
			const float loadMs = sceneGpuMs + timer.getTimeMs();
			const float step = loadMs > 0.0F ? std::clamp(kGain * ((budgetMs / loadMs) - 1.0F), -kMaxStep, kMaxStep) : kMaxStep;
			passCount = std::clamp(passCount * (1.0F + step), kMinPassCount, kMaxPasses);
		}
		passes = static_cast<int>(passCount);

		GLint previousFramebuffer = 0;
		GLint viewport[4] = {};
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
		glGetIntegerv(GL_VIEWPORT, viewport);
		const bool scissor = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
		const bool blend = glIsEnabled(GL_BLEND) == GL_TRUE;

		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl->framebuffer);
		glViewport(0, 0, kTargetSize, kTargetSize);
		if (scissor) {
			glDisable(GL_SCISSOR_TEST);
		}
		if (blend) {
			glDisable(GL_BLEND);
		}
		gl->shader.use();
		glUniform1i(gl->iterationsLocation, kIterations);
		gl->vertexArray.bind();

		// The timer brackets the passes even when there are none, so its reading falls to zero with them.
		timer.begin();
		if (passes > 0) {
			glDrawArraysInstanced(GL_TRIANGLES, 0, 3, passes);
		}
		timer.end();

		Renderer::GLVertexArray::unbind();
		gl->shader.unbind();
		if (blend) {
			glEnable(GL_BLEND);
		}
		if (scissor) {
			glEnable(GL_SCISSOR_TEST);
		}
		glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
	}

	std::string GpuHold::statusJson() const {
		std::ostringstream json;
		json << R"({"enabled":)" << (enabled ? "true" : "false") << R"(,"target":)" << target << R"(,"passes":)" << passes
			 << R"(,"holdGpuMs":)" << timer.getTimeMs() << R"(,"sceneGpuMs":)" << lastSceneGpuMs << R"(,"frameMs":)" << frameMs << "}";
		return json.str();
	}

} // namespace world_sim
