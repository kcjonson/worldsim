// Font Test Scene - Font Rendering Demonstration
// Demonstrates text rendering using FreeType-based FontRenderer

#include <scene/scene.h>
#include <scene/scene_manager.h>
#include <font/font_renderer.h>
#include <primitives/primitives.h>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <memory>

namespace {

class FontTestScene : public engine::IScene {
public:
	void OnEnter() override {
		std::cout << "FontTestScene::OnEnter()" << std::endl;

		// Initialize font renderer
		m_fontRenderer = std::make_unique<UI::FontRenderer>();
		if (!m_fontRenderer->Initialize()) {
			std::cerr << "Failed to initialize FontRenderer!" << std::endl;
			return;
		}

		// Get actual viewport dimensions for proper text rendering
		int viewportWidth, viewportHeight;
		Renderer::Primitives::GetViewport(viewportWidth, viewportHeight);

		// Set up projection matrix (orthographic for 2D text)
		glm::mat4 projection = glm::ortho(
			0.0f, static_cast<float>(viewportWidth),
			static_cast<float>(viewportHeight), 0.0f
		);
		m_fontRenderer->SetProjectionMatrix(projection);

		std::cout << "FontRenderer initialized successfully (" << viewportWidth << "x" << viewportHeight << ")" << std::endl;
	}

	void Update(float dt) override {
		// No update logic needed for static text
	}

	void Render() override {
		// Clear background to dark blue
		glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		// Begin primitive rendering (required for menu to work)
		Renderer::Primitives::BeginFrame();

		if (!m_fontRenderer) {
			Renderer::Primitives::EndFrame();
			return;
		}

		// Render "Hello World" text at different positions and scales
		m_fontRenderer->RenderText("Hello World!", glm::vec2(50, 100), 2.0f, glm::vec3(1.0f, 1.0f, 1.0f));
		m_fontRenderer->RenderText("Font Rendering System", glm::vec2(50, 200), 1.5f, glm::vec3(0.0f, 1.0f, 0.0f));
		m_fontRenderer->RenderText("Ported from ColonySim", glm::vec2(50, 280), 1.0f, glm::vec3(1.0f, 0.5f, 0.0f));

		// Render colored text examples
		m_fontRenderer->RenderText("Red Text", glm::vec2(50, 360), 1.2f, glm::vec3(1.0f, 0.0f, 0.0f));
		m_fontRenderer->RenderText("Green Text", glm::vec2(50, 420), 1.2f, glm::vec3(0.0f, 1.0f, 0.0f));
		m_fontRenderer->RenderText("Blue Text", glm::vec2(50, 480), 1.2f, glm::vec3(0.0f, 0.0f, 1.0f));

		// Small text
		m_fontRenderer->RenderText("Small text at 0.8 scale", glm::vec2(400, 100), 0.8f, glm::vec3(0.8f, 0.8f, 0.8f));

		// End primitive rendering (flushes queued primitives like the menu)
		Renderer::Primitives::EndFrame();
	}

	void OnExit() override {
		std::cout << "FontTestScene::OnExit()" << std::endl;
		m_fontRenderer.reset();
	}

	std::string ExportState() override {
		return R"({
			"scene": "font_test",
			"description": "Font rendering demonstration",
			"renderer": "FreeType-based"
		})";
	}

	const char* GetName() const override {
		return "font_test";
	}

private:
	std::unique_ptr<UI::FontRenderer> m_fontRenderer;
};

// Register scene with SceneManager
static bool s_registered = []() {
	engine::SceneManager::Get().RegisterScene("font_test", []() {
		return std::make_unique<FontTestScene>();
	});
	return true;
}();

} // anonymous namespace
