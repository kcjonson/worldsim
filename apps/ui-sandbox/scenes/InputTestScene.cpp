// Input Test Scene - InputManager Testing and Demonstration
// Displays real-time input state from InputManager

#include <GL/glew.h>
#include <font/FontRenderer.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <input/InputManager.h>
#include <input/InputTypes.h>
#include <memory>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <sstream>
#include <utils/Log.h>

namespace {

	class InputTestScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(UI, "InputTestScene::OnEnter()");

			// Initialize font renderer
			fontRenderer = std::make_unique<ui::FontRenderer>();
			if (!fontRenderer->Initialize()) {
				LOG_ERROR(UI, "Failed to initialize FontRenderer!");
				return;
			}

			// Get actual viewport dimensions
			int viewportWidth = 0;
			int viewportHeight = 0;
			Renderer::Primitives::getViewport(viewportWidth, viewportHeight);

			// Set up projection matrix (orthographic for 2D text)
			glm::mat4 projection = glm::ortho(0.0F, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0.0F);
			fontRenderer->setProjectionMatrix(projection);

			LOG_INFO(UI, "InputTestScene initialized (%dx%d)", viewportWidth, viewportHeight);
		}

		void handleInput(float dt) override {
			// Just read input state - InputManager handles everything
		}

		void update(float dt) override {
			// No update logic needed - input state is read in Render()
		}

		void render() override {
			// Clear background to dark gray
			glClearColor(0.15F, 0.15F, 0.15F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			if (!fontRenderer) {
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
			fontRenderer->renderText("Input Test Scene", glm::vec2(50, yPos), 2.0F, white);
			yPos += lineHeight * 1.5F;

			// Get input state from InputManager
			auto& input = engine::InputManager::Get();

			// Mouse Position
			glm::vec2		   mousePos = input.getMousePosition();
			std::ostringstream oss;
			oss << "Mouse Position: (" << static_cast<int>(mousePos.x) << ", " << static_cast<int>(mousePos.y) << ")";
			fontRenderer->renderText(oss.str(), glm::vec2(50, yPos), scale, white);
			yPos += lineHeight;

			// Mouse Delta
			glm::vec2 mouseDelta = input.getMouseDelta();
			oss.str("");
			oss << "Mouse Delta: (" << static_cast<int>(mouseDelta.x) << ", " << static_cast<int>(mouseDelta.y) << ")";
			fontRenderer->renderText(oss.str(), glm::vec2(50, yPos), scale, white);
			yPos += lineHeight;

			// Mouse Buttons
			bool leftDown = input.isMouseButtonDown(engine::MouseButton::Left);
			bool rightDown = input.isMouseButtonDown(engine::MouseButton::Right);
			bool middleDown = input.isMouseButtonDown(engine::MouseButton::Middle);

			oss.str("");
			oss << "Mouse Buttons: L:" << (leftDown ? "DOWN" : "UP") << "  R:" << (rightDown ? "DOWN" : "UP")
				<< "  M:" << (middleDown ? "DOWN" : "UP");
			fontRenderer->renderText(oss.str(), glm::vec2(50, yPos), scale, leftDown || rightDown || middleDown ? green : white);
			yPos += lineHeight;

			// Dragging
			bool dragging = input.isDragging();
			if (dragging) {
				glm::vec2 dragStart = input.getDragStartPosition();
				glm::vec2 dragDelta = input.getDragDelta();
				oss.str("");
				oss << "Dragging: Start(" << static_cast<int>(dragStart.x) << "," << static_cast<int>(dragStart.y) << ") Delta("
					<< static_cast<int>(dragDelta.x) << "," << static_cast<int>(dragDelta.y) << ")";
				fontRenderer->renderText(oss.str(), glm::vec2(50, yPos), scale, yellow);
			} else {
				fontRenderer->renderText("Dragging: No", glm::vec2(50, yPos), scale, white);
			}
			yPos += lineHeight;

			// Scroll
			float scrollDelta = input.getScrollDelta();
			oss.str("");
			oss << "Scroll Delta: " << scrollDelta;
			fontRenderer->renderText(oss.str(), glm::vec2(50, yPos), scale, scrollDelta != 0.0F ? yellow : white);
			yPos += lineHeight;

			// Cursor in window
			bool cursorIn = input.isCursorInWindow();
			oss.str("");
			oss << "Cursor In Window: " << (cursorIn ? "YES" : "NO");
			fontRenderer->renderText(oss.str(), glm::vec2(50, yPos), scale, cursorIn ? green : yellow);
			yPos += lineHeight * 1.5F;

			// Keyboard Section
			fontRenderer->renderText("Keyboard (Try WASD, Arrow Keys, Space, Enter):", glm::vec2(50, yPos), scale, cyan);
			yPos += lineHeight;

			// Test common keys
			struct KeyTest {
				engine::Key	key;
				const char* name;
			};

			KeyTest keys[] = {
				{engine::Key::W, "W"},
				{engine::Key::A, "A"},
				{engine::Key::S, "S"},
				{engine::Key::D, "D"},
				{engine::Key::Space, "SPACE"},
				{engine::Key::Enter, "ENTER"},
				{engine::Key::Escape, "ESC"},
				{engine::Key::Up, "UP"},
				{engine::Key::Down, "DOWN"},
				{engine::Key::Left, "LEFT"},
				{engine::Key::Right, "RIGHT"}
			};

			for (const auto& keyTest : keys) {
				bool isDown = input.isKeyDown(keyTest.key);
				bool isPressed = input.isKeyPressed(keyTest.key);
				bool isReleased = input.isKeyReleased(keyTest.key);

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

				fontRenderer->renderText(oss.str(), glm::vec2(50, yPos), scale, color);
				yPos += lineHeight;
			}

			// Instructions
			yPos += lineHeight;
			fontRenderer->renderText(
				"Try moving mouse, clicking, dragging, scrolling, and pressing keys!",
				glm::vec2(50, yPos),
				1.2F,
				glm::vec3(0.7F, 0.7F, 0.7F)
			);
		}

		void onExit() override {
			LOG_INFO(UI, "InputTestScene::OnExit()");
			fontRenderer.reset();
		}

		std::string exportState() override { // NOLINT(readability-convert-member-functions-to-static)
			return R"({
			"scene": "input_test",
			"description": "InputManager testing and demonstration"
		})";
		}

		const char* getName() const override { return "input_test"; }

	  private:
		std::unique_ptr<ui::FontRenderer> fontRenderer;
	};

	// Register scene with SceneManager
	bool g_registered = []() {
		engine::SceneManager::Get().registerScene("input_test", []() { return std::make_unique<InputTestScene>(); });
		return true;
	}();

} // anonymous namespace
