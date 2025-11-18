// Input Test Scene - InputManager Testing and Demonstration
// Displays real-time input state from InputManager

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <font/font_renderer.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <input/input_manager.h>
#include <memory>
#include <primitives/primitives.h>
#include <scene/scene.h>
#include <scene/scene_manager.h>
#include <sstream>
#include <utils/log.h>

namespace {

	class InputTestScene : public engine::IScene {
	  public:
		void OnEnter() override {
			LOG_INFO(UI, "InputTestScene::OnEnter()");

			// Initialize font renderer
			m_fontRenderer = std::make_unique<ui::FontRenderer>();
			if (!m_fontRenderer->Initialize()) {
				LOG_ERROR(UI, "Failed to initialize FontRenderer!");
				return;
			}

			// Get actual viewport dimensions
			int viewportWidth = 0;
			int viewportHeight = 0;
			Renderer::Primitives::GetViewport(viewportWidth, viewportHeight);

			// Set up projection matrix (orthographic for 2D text)
			glm::mat4 projection = glm::ortho(0.0F, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0.0F);
			m_fontRenderer->SetProjectionMatrix(projection);

			LOG_INFO(UI, "InputTestScene initialized (%dx%d)", viewportWidth, viewportHeight);
		}

		void HandleInput(float dt) override {
			// Just read input state - InputManager handles everything
		}

		void Update(float dt) override {
			// No update logic needed - input state is read in Render()
		}

		void Render() override {
			// Clear background to dark gray
			glClearColor(0.15F, 0.15F, 0.15F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			if (!m_fontRenderer) {
				return;
			}

			const float		scale = 1.5F;
			float			yPos = 50.0F;
			const float		lineHeight = 50.0F;
			const glm::vec3 white(1.0F, 1.0F, 1.0F);
			const glm::vec3 green(0.0F, 1.0F, 0.0F);
			const glm::vec3 yellow(1.0F, 1.0F, 0.0F);
			const glm::vec3 cyan(0.0F, 1.0F, 1.0F);

			// Title
			m_fontRenderer->RenderText("Input Test Scene", glm::vec2(50, yPos), 2.0F, white);
			yPos += lineHeight * 1.5F;

			// Get input state from InputManager
			auto& input = engine::InputManager::Get();

			// Mouse Position
			glm::vec2		   mousePos = input.GetMousePosition();
			std::ostringstream oss;
			oss << "Mouse Position: (" << static_cast<int>(mousePos.x) << ", " << static_cast<int>(mousePos.y) << ")";
			m_fontRenderer->RenderText(oss.str(), glm::vec2(50, yPos), scale, white);
			yPos += lineHeight;

			// Mouse Delta
			glm::vec2 mouseDelta = input.GetMouseDelta();
			oss.str("");
			oss << "Mouse Delta: (" << static_cast<int>(mouseDelta.x) << ", " << static_cast<int>(mouseDelta.y) << ")";
			m_fontRenderer->RenderText(oss.str(), glm::vec2(50, yPos), scale, white);
			yPos += lineHeight;

			// Mouse Buttons
			bool leftDown = input.IsMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT);
			bool rightDown = input.IsMouseButtonDown(GLFW_MOUSE_BUTTON_RIGHT);
			bool middleDown = input.IsMouseButtonDown(GLFW_MOUSE_BUTTON_MIDDLE);

			oss.str("");
			oss << "Mouse Buttons: L:" << (leftDown ? "DOWN" : "UP") << "  R:" << (rightDown ? "DOWN" : "UP")
				<< "  M:" << (middleDown ? "DOWN" : "UP");
			m_fontRenderer->RenderText(oss.str(), glm::vec2(50, yPos), scale, leftDown || rightDown || middleDown ? green : white);
			yPos += lineHeight;

			// Dragging
			bool dragging = input.IsDragging();
			if (dragging) {
				glm::vec2 dragStart = input.GetDragStartPosition();
				glm::vec2 dragDelta = input.GetDragDelta();
				oss.str("");
				oss << "Dragging: Start(" << static_cast<int>(dragStart.x) << "," << static_cast<int>(dragStart.y) << ") Delta("
					<< static_cast<int>(dragDelta.x) << "," << static_cast<int>(dragDelta.y) << ")";
				m_fontRenderer->RenderText(oss.str(), glm::vec2(50, yPos), scale, yellow);
			} else {
				m_fontRenderer->RenderText("Dragging: No", glm::vec2(50, yPos), scale, white);
			}
			yPos += lineHeight;

			// Scroll
			float scrollDelta = input.GetScrollDelta();
			oss.str("");
			oss << "Scroll Delta: " << scrollDelta;
			m_fontRenderer->RenderText(oss.str(), glm::vec2(50, yPos), scale, scrollDelta != 0.0F ? yellow : white);
			yPos += lineHeight;

			// Cursor in window
			bool cursorIn = input.IsCursorInWindow();
			oss.str("");
			oss << "Cursor In Window: " << (cursorIn ? "YES" : "NO");
			m_fontRenderer->RenderText(oss.str(), glm::vec2(50, yPos), scale, cursorIn ? green : yellow);
			yPos += lineHeight * 1.5F;

			// Keyboard Section
			m_fontRenderer->RenderText("Keyboard (Try WASD, Arrow Keys, Space, Enter):", glm::vec2(50, yPos), scale, cyan);
			yPos += lineHeight;

			// Test common keys
			struct KeyTest {
				int			key;
				const char* name;
			};

			KeyTest keys[] = {
				{GLFW_KEY_W, "W"},
				{GLFW_KEY_A, "A"},
				{GLFW_KEY_S, "S"},
				{GLFW_KEY_D, "D"},
				{GLFW_KEY_SPACE, "SPACE"},
				{GLFW_KEY_ENTER, "ENTER"},
				{GLFW_KEY_ESCAPE, "ESC"},
				{GLFW_KEY_UP, "UP"},
				{GLFW_KEY_DOWN, "DOWN"},
				{GLFW_KEY_LEFT, "LEFT"},
				{GLFW_KEY_RIGHT, "RIGHT"}
			};

			for (const auto& keyTest : keys) {
				bool isDown = input.IsKeyDown(keyTest.key);
				bool isPressed = input.IsKeyPressed(keyTest.key);
				bool isReleased = input.IsKeyReleased(keyTest.key);

				oss.str("");
				oss << keyTest.name << ": ";
				if (isPressed) {
					oss << "PRESSED";
				} else if (isReleased) {
					oss << "RELEASED";
				} else if (isDown) {
					oss << "DOWN";
				} else {
					oss << "UP";
				}

				glm::vec3 color = white;
				if (isPressed)
					color = green;
				else if (isReleased)
					color = yellow;
				else if (isDown)
					color = cyan;

				m_fontRenderer->RenderText(oss.str(), glm::vec2(50, yPos), scale, color);
				yPos += lineHeight;
			}

			// Instructions
			yPos += lineHeight;
			m_fontRenderer->RenderText(
				"Try moving mouse, clicking, dragging, scrolling, and pressing keys!",
				glm::vec2(50, yPos),
				1.2F,
				glm::vec3(0.7F, 0.7F, 0.7F)
			);
		}

		void OnExit() override {
			LOG_INFO(UI, "InputTestScene::OnExit()");
			m_fontRenderer.reset();
		}

		std::string ExportState() override { // NOLINT(readability-convert-member-functions-to-static)
			return R"({
			"scene": "input_test",
			"description": "InputManager testing and demonstration"
		})";
		}

		const char* GetName() const override { return "input_test"; }

	  private:
		std::unique_ptr<ui::FontRenderer> m_fontRenderer;
	};

	// Register scene with SceneManager
	bool g_registered = []() {
		engine::SceneManager::Get().RegisterScene("input_test", []() { return std::make_unique<InputTestScene>(); });
		return true;
	}();

} // anonymous namespace
