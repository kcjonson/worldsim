// Input Test Scene - InputManager Testing and Demonstration
// Displays real-time input state from InputManager

#include <GL/glew.h>
#include <graphics/Color.h>
#include <input/InputManager.h>
#include <input/InputTypes.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <shapes/Shapes.h>
#include <sstream>
#include <utils/Log.h>

namespace {

// Scene name declared here - the scene owns its human-readable name
constexpr const char* kSceneName = "input_test";

	class InputTestScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(UI, "InputTestScene::OnEnter()");
		}

		void handleInput(float /*dt*/) override {
			// Just read input state - InputManager handles everything
		}

		void update(float /*dt*/) override {
			// No update logic needed - input state is read in render()
		}

		void render() override {
			using namespace Foundation;

			// Clear background to dark gray
			glClearColor(0.15F, 0.15F, 0.15F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Helper to draw text using UI::Text shapes
			auto drawText = [](const std::string& str, float x, float y, float fontSize, const Color& color) {
				UI::Text text(UI::Text::Args{.position = {x, y}, .text = str, .style = {.color = color, .fontSize = fontSize}});
				text.render();
			};

			const float fontSize = 20.0F;
			float		yPos = 50.0F;
			const float lineHeight = 35.0F;
			const Color white = Color::white();
			const Color green = Color::green();
			const Color yellow = Color::yellow();
			const Color cyan = Color::cyan();

			// Title
			drawText("Input Test Scene", 50, yPos, 28.0F, white);
			yPos += lineHeight * 1.5F;

			// Get input state from InputManager
			auto& input = engine::InputManager::Get();

			// Mouse Position
			glm::vec2		   mousePos = input.getMousePosition();
			std::ostringstream oss;
			oss << "Mouse Position: (" << static_cast<int>(mousePos.x) << ", " << static_cast<int>(mousePos.y) << ")";
			drawText(oss.str(), 50, yPos, fontSize, white);
			yPos += lineHeight;

			// Mouse Delta
			glm::vec2 mouseDelta = input.getMouseDelta();
			oss.str("");
			oss << "Mouse Delta: (" << static_cast<int>(mouseDelta.x) << ", " << static_cast<int>(mouseDelta.y) << ")";
			drawText(oss.str(), 50, yPos, fontSize, white);
			yPos += lineHeight;

			// Mouse Buttons
			bool leftDown = input.isMouseButtonDown(engine::MouseButton::Left);
			bool rightDown = input.isMouseButtonDown(engine::MouseButton::Right);
			bool middleDown = input.isMouseButtonDown(engine::MouseButton::Middle);

			oss.str("");
			oss << "Mouse Buttons: L:" << (leftDown ? "DOWN" : "UP") << "  R:" << (rightDown ? "DOWN" : "UP")
				<< "  M:" << (middleDown ? "DOWN" : "UP");
			drawText(oss.str(), 50, yPos, fontSize, leftDown || rightDown || middleDown ? green : white);
			yPos += lineHeight;

			// Dragging
			bool dragging = input.isDragging();
			if (dragging) {
				glm::vec2 dragStart = input.getDragStartPosition();
				glm::vec2 dragDelta = input.getDragDelta();
				oss.str("");
				oss << "Dragging: Start(" << static_cast<int>(dragStart.x) << "," << static_cast<int>(dragStart.y) << ") Delta("
					<< static_cast<int>(dragDelta.x) << "," << static_cast<int>(dragDelta.y) << ")";
				drawText(oss.str(), 50, yPos, fontSize, yellow);
			} else {
				drawText("Dragging: No", 50, yPos, fontSize, white);
			}
			yPos += lineHeight;

			// Scroll
			float scrollDelta = input.getScrollDelta();
			oss.str("");
			oss << "Scroll Delta: " << scrollDelta;
			drawText(oss.str(), 50, yPos, fontSize, scrollDelta != 0.0F ? yellow : white);
			yPos += lineHeight;

			// Cursor in window
			bool cursorIn = input.isCursorInWindow();
			oss.str("");
			oss << "Cursor In Window: " << (cursorIn ? "YES" : "NO");
			drawText(oss.str(), 50, yPos, fontSize, cursorIn ? green : yellow);
			yPos += lineHeight * 1.5F;

			// Keyboard Section
			drawText("Keyboard (Try WASD, Arrow Keys, Space, Enter):", 50, yPos, fontSize, cyan);
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

				Color color = white;
				if (isPressed)
					color = green;
				else if (isReleased)
					color = yellow;
				else if (isDown)
					color = cyan;

				drawText(oss.str(), 50, yPos, fontSize, color);
				yPos += lineHeight;
			}

			// Instructions
			yPos += lineHeight;
			drawText("Try moving mouse, clicking, dragging, scrolling, and pressing keys!", 50, yPos, 16.0F, Color(0.7F, 0.7F, 0.7F, 1.0F));
		}

		void onExit() override {
			LOG_INFO(UI, "InputTestScene::OnExit()");
		}

		std::string exportState() override { // NOLINT(readability-convert-member-functions-to-static)
			return R"({
			"scene": "input_test",
			"description": "InputManager testing and demonstration"
		})";
		}

		const char* getName() const override { return kSceneName; }
	};

} // anonymous namespace

// Export factory and name for scene registry (scene owns its name)
namespace ui_sandbox::scenes {
	std::unique_ptr<engine::IScene> createInputTestScene() { return std::make_unique<InputTestScene>(); }
	const char* getInputTestSceneName() { return kSceneName; }
}
