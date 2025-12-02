// Minimal SDF Test Scene - Single line of text to verify SDF rendering works
// Uses the unified uber shader for combined shape + text rendering

#include <font/FontRenderer.h>
#include <graphics/Color.h>
#include <primitives/BatchRenderer.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include "SceneTypes.h"
#include <utils/Log.h>

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <vector>

namespace {

constexpr const char* kSceneName = "sdf_minimal";

class SDFMinimalScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(UI, "=== SDF Minimal Test Scene (Uber Shader) ===");

			// Get font renderer from Primitives API (initialized in main.cpp)
			fontRenderer = Renderer::Primitives::getFontRenderer();
			if (fontRenderer == nullptr) {
				LOG_ERROR(UI, "FontRenderer not available from Primitives API");
				return;
			}

			// Get batch renderer from Primitives API
			batchRenderer = Renderer::Primitives::getBatchRenderer();
			if (batchRenderer == nullptr) {
				LOG_ERROR(UI, "BatchRenderer not available from Primitives API");
				return;
			}

			LOG_INFO(UI, "SDF Minimal Scene initialized with uber shader");
		}

		void handleInput(float dt) override {}

		void update(float dt) override {}

		void render() override {
			// Clear to dark blue
			glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);

			// Begin frame for primitives
			Renderer::Primitives::beginFrame();

			float scale = 2.0f; // 2.0 = 32px (base is 16px)
			Foundation::Color textColor(1.0F, 1.0F, 1.0F, 1.0F); // White

			// Render uppercase alphabet
			renderTextLine("ABCDEFGHIJKLMNOPQRSTUVWXYZ", glm::vec2(50.0f, 150.0f), scale, textColor);

			// Render lowercase alphabet
			renderTextLine("abcdefghijklmnopqrstuvwxyz", glm::vec2(50.0f, 250.0f), scale, textColor);

			// Render numbers for reference
			renderTextLine("0123456789", glm::vec2(50.0f, 350.0f), scale, textColor);

			// End frame flushes all batched geometry
			Renderer::Primitives::endFrame();
		}

		void onExit() override {
			// Font and batch renderers are owned by Primitives API, not this scene
			fontRenderer = nullptr;
			batchRenderer = nullptr;
		}

		std::string exportState() override { return R"({"scene": "sdf_minimal", "description": "Minimal SDF rendering test with uber shader"})"; }

		const char* getName() const override { return kSceneName; }

	  private:
		// Helper to render a line of text using the unified batch renderer
		void renderTextLine(const std::string& text, const glm::vec2& position, float scale, const Foundation::Color& color) {
			if (fontRenderer == nullptr || batchRenderer == nullptr) {
				return;
			}

			// Generate glyph quads
			glm::vec4 glyphColor(color.r, color.g, color.b, color.a);
			std::vector<ui::FontRenderer::GlyphQuad> glyphs;
			fontRenderer->generateGlyphQuads(text, position, scale, glyphColor, glyphs);

			// Add each glyph to the unified batch renderer
			for (const auto& glyph : glyphs) {
				batchRenderer->addTextQuad(
					Foundation::Vec2(glyph.position.x, glyph.position.y),
					Foundation::Vec2(glyph.size.x, glyph.size.y),
					Foundation::Vec2(glyph.uvMin.x, glyph.uvMin.y),
					Foundation::Vec2(glyph.uvMax.x, glyph.uvMax.y),
					color
				);
			}
		}

		ui::FontRenderer* fontRenderer = nullptr;
		Renderer::BatchRenderer* batchRenderer = nullptr;
	};

} // anonymous namespace

// Export scene info for registry
namespace ui_sandbox::scenes {
	extern const ui_sandbox::SceneInfo SdfMinimal = {kSceneName, []() { return std::make_unique<SDFMinimalScene>(); }};
}
