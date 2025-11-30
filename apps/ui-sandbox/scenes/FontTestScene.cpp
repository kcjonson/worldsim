// Font Test Scene - Font Rendering Demonstration
// Demonstrates text rendering using FreeType-based FontRenderer

#include <GL/glew.h>
#include <font/FontRenderer.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <utils/Log.h>

namespace {

	class FontTestScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(UI, "FontTestScene::OnEnter()");

			// Initialize font renderer
			fontRenderer = std::make_unique<ui::FontRenderer>();
			if (!fontRenderer->Initialize()) {
				LOG_ERROR(UI, "Failed to initialize FontRenderer!");
				return;
			}

			// Get actual viewport dimensions for proper text rendering
			int viewportWidth = 0;
			int viewportHeight = 0;
			Renderer::Primitives::getViewport(viewportWidth, viewportHeight);

			// Set up projection matrix (orthographic for 2D text)
			glm::mat4 projection = glm::ortho(0.0F, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0.0F);
			fontRenderer->setProjectionMatrix(projection);

			LOG_INFO(UI, "FontRenderer initialized successfully (%dx%d)", viewportWidth, viewportHeight);
		}

		void handleInput(float dt) override {
			// No input handling needed - static scene
		}

		void update(float dt) override {
			// No update logic needed for static text
		}

		void render() override {
			// Clear background to dark blue
			glClearColor(0.1F, 0.1F, 0.2F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			if (!fontRenderer) {
				return;
			}

			// Render "Hello World" text at different positions and scales
			fontRenderer->renderText("Hello World!", glm::vec2(50, 100), 2.0F, glm::vec3(1.0F, 1.0F, 1.0F));
			fontRenderer->renderText("Font Rendering System", glm::vec2(50, 200), 1.5F, glm::vec3(0.0F, 1.0F, 0.0F));
			fontRenderer->renderText("Ported from ColonySim", glm::vec2(50, 280), 1.0F, glm::vec3(1.0F, 0.5F, 0.0F));

			// Render colored text examples
			fontRenderer->renderText("Red Text", glm::vec2(50, 360), 1.2F, glm::vec3(1.0F, 0.0F, 0.0F));
			fontRenderer->renderText("Green Text", glm::vec2(50, 420), 1.2F, glm::vec3(0.0F, 1.0F, 0.0F));
			fontRenderer->renderText("Blue Text", glm::vec2(50, 480), 1.2F, glm::vec3(0.0F, 0.0F, 1.0F));

			// Small text
			fontRenderer->renderText("Small text at 0.8 scale", glm::vec2(400, 100), 0.8F, glm::vec3(0.8F, 0.8F, 0.8F));
		}

		void onExit() override {
			LOG_INFO(UI, "FontTestScene::OnExit()");
			fontRenderer.reset();
		}

		std::string exportState() override { // NOLINT(readability-convert-member-functions-to-static)
			return R"({
			"scene": "font_test",
			"description": "Font rendering demonstration",
			"renderer": "FreeType-based"
		})";
		}

		const char* getName() const override { return "font_test"; }

	  private:
		std::unique_ptr<ui::FontRenderer> fontRenderer;
	};

	// Register scene with SceneManager
	bool g_registered = []() {
		engine::SceneManager::Get().registerScene("font_test", []() { return std::make_unique<FontTestScene>(); });
		return true;
	}();

} // anonymous namespace
